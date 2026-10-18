"""Prepare a migration HEX from a full old-layout NOR dump; never touch hardware."""
import argparse
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ota_package import hex_segments


def runtime_prefix(data, profile):
    if profile not in (0, 1, 2):
        raise ValueError("Invalid clock profile")
    used = len(data.rstrip(b"\xff"))
    length = (max(used, 0x408) + 4095) & ~4095
    image = data[0x400:length]
    sp, reset = struct.unpack_from("<II", image)
    if not 0x34000000 <= sp <= 0x34200000 or sp & 7 or not reset & 1 or not 0x70100400 <= (reset & ~1) < 0x70100400 + len(image):
        raise ValueError("Current/previous application vectors are invalid")
    h = struct.pack("<IIIIIII", 0x3254524F, profile, len(image), zlib.crc32(image), 0, 0, 0)
    first = bytearray(data[:4096])
    first[:32] = h + struct.pack("<I", zlib.crc32(h))
    return bytes(first), length


def prepare_migration(dump, compat, current_profile=0, staged_profile=0, previous_profile=0):
    if len(dump) != 0x2000000:
        raise ValueError("Migration requires an exact 32 MB old-layout NOR dump")
    changes = []
    for name in ("fsbl_universal.hex", "agent.hex"):
        changes.extend(hex_segments((compat / name).read_text(encoding="ascii")))
    def upgrade_manager(data):
        if b"OTA RTOS boot" not in data:
            return data, False
        image = (compat / "appli.bin").read_bytes()
        replacement = bytearray(b"\xff" * ((0x400 + len(image) + 4095) & ~4095))
        replacement[0x400:0x400 + len(image)] = image
        prefix, _ = runtime_prefix(replacement, 0)
        replacement[:4096] = prefix
        return bytes(replacement), True

    current, upgraded = upgrade_manager(dump[0x100000:0x400000])
    if upgraded: current_profile = 0
    first, current_len = runtime_prefix(current, current_profile)
    changes.append((0x70100000, current if upgraded else first))
    changes.append((0x700FC000, b"\xff" * 4096))
    report = {"current_bytes_with_prefix": current_len, "current_profile": current_profile,
              "staged": False, "previous": False,
              "current_management_app_upgraded": upgraded,
              "note": "Runtime first sector is included in full to preserve vectors during sector erase. Boot request is cleared."}
    slot = bytearray(dump[0x400000:0x400040])
    magic, length, desc_len, crc = struct.unpack_from("<IIII", slot)
    if magic == 0x31544C53:
        if not 8 <= length <= 512 * 1024 - 64 or desc_len > 32:
            raise ValueError("Old staging header is invalid")
        image = dump[0x400040:0x400040 + length]
        if zlib.crc32(image) != crc:
            raise ValueError("Old staging CRC failed; preserve the dump and inspect it")
        if b"OTA RTOS boot" in image:
            image = (compat / "appli.bin").read_bytes()
            length = len(image); staged_profile = 0
            struct.pack_into("<I", slot, 4, length)
            struct.pack_into("<I", slot, 12, zlib.crc32(image))
            report["staged_management_app_upgraded"] = True
        struct.pack_into("<I", slot, 48, staged_profile)
        changes.append((0x70B00000, bytes(slot) + image))
        report.update(staged=True, staged_bytes=length, staged_profile=staged_profile)
    else:
        changes.append((0x70B00000, b"\xff" * 4096))
    header = bytearray(dump[0x1000000:0x1000040])
    magic, length, crc = struct.unpack_from("<III", header)
    if magic == 0x314B4247:
        if not 0x400 < length <= 0x300000 or length & 4095:
            raise ValueError("Old rollback header is invalid")
        previous = bytearray(dump[0x1001000:0x1001000 + length])
        if zlib.crc32(previous) != crc:
            raise ValueError("Old rollback CRC failed; preserve the dump and inspect it")
        previous, upgraded = upgrade_manager(previous)
        previous = bytearray(previous)
        length = len(previous)
        if upgraded: previous_profile = 0
        prefix, _ = runtime_prefix(previous, previous_profile)
        previous[:4096] = prefix
        struct.pack_into("<I", header, 8, zlib.crc32(previous))
        struct.pack_into("<I", header, 4, length)
        changes.extend([(0x71500000, bytes(header)), (0x71501000, bytes(previous))])
        report.update(previous=True, previous_bytes=length, previous_profile=previous_profile)
        report["previous_management_app_upgraded"] = upgraded
    else:
        changes.append((0x71500000, b"\xff" * 4096))
    changes.sort()
    for (addr, data), (next_addr, _) in zip(changes, changes[1:]):
        if addr + len(data) > next_addr:
            raise ValueError("Migration ranges overlap")
    report["writes"] = [{"address": hex(addr), "bytes": len(data)} for addr, data in changes]
    return changes, report


def write_hex(parts, path):
    def record(kind, offset, payload):
        r = bytes([len(payload)]) + offset.to_bytes(2, "big") + bytes([kind]) + payload
        return ":" + (r + bytes([-sum(r) & 255])).hex().upper() + "\n"
    with path.open("w", encoding="ascii", newline="\n") as output:
        upper = None
        for address, data in sorted(parts):
            for offset in range(0, len(data), 16):
                absolute = address + offset
                if upper != absolute >> 16:
                    upper = absolute >> 16
                    output.write(record(4, 0, upper.to_bytes(2, "big")))
                output.write(record(0, absolute & 65535, data[offset:offset + 16]))
        output.write(record(1, 0, b""))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dump", type=Path, required=True)
    parser.add_argument("--compat", type=Path, default=ROOT / "Binary/compat")
    parser.add_argument("--out", type=Path, default=ROOT / "Build/sdk-migration")
    for name in ("current", "staged", "previous"):
        parser.add_argument("--" + name + "-profile", type=int, choices=[0, 1, 2], default=0)
    args = parser.parse_args()
    parts, report = prepare_migration(args.dump.read_bytes(), args.compat,
                                     args.current_profile, args.staged_profile, args.previous_profile)
    args.out.mkdir(parents=True, exist_ok=True)
    write_hex(parts, args.out / "migration.hex")
    (args.out / "migration.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    print("Prepared only; review migration.json before programming migration.hex")


if __name__ == "__main__":
    main()
