"""Check STM32N6 ROM header alignment and the actual FSBL reset entry."""
import argparse
from pathlib import Path
import struct

FSBL_VECTOR = 0x34180400
PAYLOAD_OFFSET = 0x400
HEADER_SIZE = 0x240


def verify_fsbl_image(raw, signed):
    if len(raw) < 8 or len(signed) < PAYLOAD_OFFSET + 8:
        raise ValueError("FSBL image is truncated")
    if signed[:4] != b"STM2":
        raise ValueError("FSBL ROM header magic is missing")
    if len(signed) != PAYLOAD_OFFSET + len(raw) or signed[PAYLOAD_OFFSET:] != raw:
        raise ValueError("FSBL payload must start at 0x400: sign with -align")
    checksum, version, image_length, entry = struct.unpack_from("<IIII", signed, 0x64)
    stack, reset = struct.unpack_from("<II", raw)
    if version != 0x00020300:
        raise ValueError("FSBL header must use STM32N6 version 2.3")
    if entry != reset:
        raise ValueError(f"FSBL header entry 0x{entry:08X} differs from Reset_Handler 0x{reset:08X}")
    if not (0x34000000 <= stack <= 0x34200000 and stack % 8 == 0):
        raise ValueError("FSBL initial stack is outside SRAM")
    if not (reset & 1 and FSBL_VECTOR <= (reset & ~1) < FSBL_VECTOR + len(raw)):
        raise ValueError("FSBL reset entry is outside its linked payload")
    if image_length != len(signed) - HEADER_SIZE:
        raise ValueError("FSBL header image length is wrong")
    if checksum != sum(signed[HEADER_SIZE:]) & 0xFFFFFFFF:
        raise ValueError("FSBL ROM payload checksum is wrong")
    return {"payload_offset": PAYLOAD_OFFSET, "entry": entry, "stack": stack,
            "signed_bytes": len(signed)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", required=True, type=Path)
    parser.add_argument("--signed", required=True, type=Path)
    args = parser.parse_args()
    result = verify_fsbl_image(args.raw.read_bytes(), args.signed.read_bytes())
    print(f"FSBL ROM header OK: payload +0x400, entry=0x{result['entry']:08X}, "
          f"stack=0x{result['stack']:08X}")


if __name__ == "__main__":
    main()
