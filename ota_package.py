"""Build paired SDK application/model images without moving absolute addresses."""
import struct
import zlib
from ota_protocol import APP_BASE, MAGIC, validate_image

PACKAGE_MAX_IMAGE = 0x9FEC00
NETWORK_BASE = 0x70200000
ACTIVE_END = 0x70AFF000


def hex_segments(text):
    upper, rows, ended = 0, [], False
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if ended or not line.startswith(":"):
            raise ValueError("HEX 格式或结束记录错误")
        r = bytes.fromhex(line[1:])
        if len(r) < 5 or len(r) != r[0] + 5 or sum(r) & 255:
            raise ValueError("HEX 长度或校验和错误")
        n, addr, kind, data = r[0], int.from_bytes(r[1:3], "big"), r[3], r[4:-1]
        if kind == 0 and n:
            rows.append((upper + addr, data))
        elif kind in (2, 4) and n == 2 and addr == 0:
            upper = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        elif kind == 1 and n == 0 and addr == 0:
            ended = True
        elif kind in (3, 5) and n == 4:
            pass
        elif kind == 0 and n == 0:
            pass
        else:
            raise ValueError("不支持的 HEX 记录")
    if not ended or not rows:
        raise ValueError("HEX 不完整")
    merged = []
    for addr, data in sorted(rows):
        if merged and addr < merged[-1][0] + len(merged[-1][1]):
            raise ValueError("HEX 含重叠地址")
        if merged and addr == merged[-1][0] + len(merged[-1][1]):
            merged[-1][1].extend(data)
        else:
            merged.append((addr, bytearray(data)))
    return [(addr, bytes(data)) for addr, data in merged]


def merge_sdk_images(application_hex, network_hex=None):
    app = hex_segments(application_hex)
    if app[0][0] != APP_BASE:
        raise ValueError("应用必须从 0x70100400 开始，不能打包 FSBL")
    app_end = NETWORK_BASE if network_hex is not None else ACTIVE_END
    for addr, data in app:
        if addr < APP_BASE or addr + len(data) > app_end:
            raise ValueError("应用地址超出兼容分区")
    model = hex_segments(network_hex) if network_hex is not None else []
    for addr, data in model:
        if addr < NETWORK_BASE or addr + len(data) > ACTIVE_END:
            raise ValueError("network-data 地址超出模型区域")
    parts = app + model
    end = max(addr + len(data) for addr, data in parts)
    image = bytearray(b"\xff" * (end - APP_BASE))
    for addr, data in parts:
        offset = addr - APP_BASE
        image[offset:offset + len(data)] = data
    validate_image(image, PACKAGE_MAX_IMAGE)
    return bytes(image), [{"address": hex(addr), "bytes": len(data)} for addr, data in parts]


def make_package(image, profile, desc):
    validate_image(image, PACKAGE_MAX_IMAGE)
    if profile not in (0, 1, 2):
        raise ValueError("启动配置无效")
    description = desc.encode("utf-8")
    if not 1 <= len(description) <= 2000:
        raise ValueError("描述长度无效")
    return struct.pack("<IIIIII", MAGIC, 4, len(description), len(image), zlib.crc32(image), profile) + description + image


def unpack_package(data):
    if len(data) < 24:
        raise ValueError("升级包被截断")
    magic, version, desc_len, image_len, crc, profile = struct.unpack_from("<IIIIII", data)
    if magic != MAGIC or version != 4 or profile > 2 or not 1 <= desc_len <= 2000 or image_len > PACKAGE_MAX_IMAGE:
        raise ValueError("升级包头无效")
    if len(data) != 24 + desc_len + image_len:
        raise ValueError("升级包长度无效")
    image = data[24 + desc_len:]
    validate_image(image, PACKAGE_MAX_IMAGE)
    if zlib.crc32(image) != crc:
        raise ValueError("升级包 CRC32 错误")
    return image, profile, data[24:24 + desc_len].decode("utf-8")
