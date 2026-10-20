"""Framed firmware transfer shared by the web and command-line senders."""
import socket
import select
import struct
import time
import zlib

APP_BASE = 0x70100400
MAX_IMAGE = 512 * 1024 - 64
MAGIC = 0x3257464F
ACK_MAGIC = 0x314B414F
CONTROL_MAGIC = 0x314D434F


def hex_to_bin(text):
    upper, rows, ended = 0, [], False
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if ended or not line.startswith(":"):
            raise ValueError("HEX 格式错误")
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 255:
            raise ValueError("HEX 长度或校验和错误")
        count, addr, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
        data = record[4:-1]
        if kind == 0:
            absolute = upper + addr
            if absolute < APP_BASE or absolute + count > APP_BASE + MAX_IMAGE:
                raise ValueError("只接收链接到 0x70100400 的应用，不能发送 FSBL 或 Agent")
            rows.append((absolute, data))
        elif kind in (2, 4) and count == 2:
            upper = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        elif kind == 1 and count == 0:
            ended = True
        elif kind in (3, 5) and count == 4:
            pass
        else:
            raise ValueError("不支持的 HEX 记录")
    if not ended or not rows or min(addr for addr, _ in rows) != APP_BASE:
        raise ValueError("HEX 不完整或应用起始地址不是 0x70100400")
    size = max(addr + len(data) for addr, data in rows) - APP_BASE
    image = bytearray(b"\xff" * size)
    for addr, data in rows:
        image[addr - APP_BASE:addr - APP_BASE + len(data)] = data
    return bytes(image), APP_BASE


def load_image(filename, data):
    if filename.lower().endswith(".hex"):
        data, _ = hex_to_bin(data.decode("ascii"))
    validate_image(data)
    return data


def validate_image(data, max_image=MAX_IMAGE):
    if not 8 <= len(data) <= max_image:
        raise ValueError(f"应用大小必须为 8～{max_image:,} 字节")
    sp, rv = struct.unpack_from("<II", data)
    if not 0x34000000 <= sp <= 0x34200000 or sp & 7:
        raise ValueError("初始栈地址错误，请选择应用 .bin 或 .hex")
    if not rv & 1 or not APP_BASE <= (rv & ~1) < APP_BASE + len(data):
        raise ValueError("应用必须链接到 0x70100400，不能发送 FSBL、Agent 或 SRAM 调试固件")


def make_container(data, desc):
    validate_image(data)
    description = desc.encode("utf-8")
    if not description or len(description) > 2000:
        raise ValueError("描述需为 1～2000 字节")
    crc = zlib.crc32(data)
    header = struct.pack("<IIIII", MAGIC, 3, len(description), len(data), crc)
    if len(header) + len(description) + len(data) > 512 * 1024:
        raise ValueError("固件与描述总大小超过接收缓冲区")
    return header + description + data, crc


def make_rollback():
    return struct.pack("<IIII", CONTROL_MAGIC, 1, 0, 0), 0


def make_transfer(filename, data, desc, firmware_version=None):
    from pathlib import Path
    from ota_metadata import encode_metadata, decode_metadata
    if filename.lower().endswith(".ota"):
        from ota_package import unpack_package, make_package
        image, profile, embedded_desc = unpack_package(data)
        metadata = decode_metadata(embedded_desc)
        note = desc or (metadata["description"] if metadata else embedded_desc)
        if firmware_version is not None:
            model = metadata["model"] if metadata else (1 if profile == 2 and len(image) > 0xFFC00 else 2)
            if profile == 1 and not metadata:
                model = 0
            note = encode_metadata(metadata["name"] if metadata else Path(filename).stem,
                                   firmware_version, model, note)
        elif metadata:
            note = encode_metadata(metadata["name"], metadata["version"], metadata["model"], note)
        payload = make_package(image, profile, note)
        return payload, zlib.crc32(image)
    if firmware_version is not None:
        desc = encode_metadata(Path(filename).stem, firmware_version, 2, desc)
    return make_container(load_image(filename, data), desc)


def _receive_result(conn, crc, complete):
    reply = bytearray()
    while len(reply) < 16:
        chunk = conn.recv(16 - len(reply))
        if not chunk:
            raise ConnectionError("未收到板子写入确认，请核对串口日志及 FSBL/Agent 版本")
        reply.extend(chunk)
    magic, status, slot, actual_crc = struct.unpack("<IiII", reply)
    if magic != ACK_MAGIC:
        raise ValueError("板子返回的升级确认格式错误")
    if status != 0:
        hint = ""
        if not complete and status in (-12, -14):
            hint = "；接收端拒收包头，请核对升级包容量和配套 FSBL/Agent 版本"
        raise ValueError(f"板子拒绝升级，状态码 {status}{hint}")
    if not complete:
        raise ValueError("板子在固件尚未发送完整时提前返回成功，未确认升级")
    if actual_crc != crc or slot != 0:
        raise ValueError("板子确认的固件校验值或槽位不匹配")
    return slot


def send_firmware(conn, payload, crc, progress=None):
    conn.settimeout(600 if len(payload) > 512 * 1024 else 90)
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    # Let a receiver reject the header before queuing megabytes on the PC.
    version = struct.unpack_from("<I", payload, 4)[0] if len(payload) >= 8 else 0
    magic = struct.unpack_from("<I", payload)[0] if len(payload) >= 4 else 0
    header = (24 if version == 4 else 20 if version == 3 else 16) if magic == MAGIC else 16
    header = min(header, len(payload))
    sent = 0
    try:
        while sent < len(payload):
            if select.select([conn], [], [], 0)[0]:
                return _receive_result(conn, crc, sent == len(payload))
            end = header if sent < header else min(sent + 1024, len(payload))
            count = conn.send(payload[sent:end])
            if count == 0:
                raise ConnectionError("板子在传输完成前断开连接")
            sent += count
            if progress:
                progress(sent, len(payload))
            if sent == header:
                if select.select([conn], [], [], 0.3)[0]:
                    return _receive_result(conn, crc, sent == len(payload))
            elif sent < len(payload):
                # Match the complete on-board receive: small bounded writes.
                time.sleep(0.001)
        if select.select([conn], [], [], 0)[0]:
            return _receive_result(conn, crc, True)
        # The receiver stops at the length in the container header. Keep the
        # connection open while flash operations temporarily pause its stack;
        # an early FIN can otherwise time out before the verified ACK arrives.
        return _receive_result(conn, crc, True)
    except OSError as exc:
        raise ConnectionError(f"连接在板子确认写入前中断：{exc}。请查看串口日志；进度仅表示电脑发送量。") from exc
