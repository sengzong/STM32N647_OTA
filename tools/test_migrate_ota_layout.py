"""Check that old-layout migration preserves current, staged and rollback data."""
from pathlib import Path
import struct
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from migrate_ota_layout import prepare_migration, write_hex
sys.path.insert(0, str(ROOT))
from ota_package import hex_segments


def image(seed):
    data = bytearray((seed + n * 7) & 255 for n in range(256))
    struct.pack_into("<II", data, 0, 0x34200000, 0x70100421)
    return bytes(data)


class MigrationTests(unittest.TestCase):
    def dump(self):
        data = bytearray(b"\xff" * 0x2000000)
        data[0x100400:0x100500] = image(1)
        staged = image(2)
        header = bytearray(64)
        struct.pack_into("<IIII", header, 0, 0x31544C53, len(staged), 5, zlib.crc32(staged))
        header[16:21] = b"touch"
        data[0x400000:0x400040] = header
        data[0x400040:0x400140] = staged
        previous = bytearray(b"\xff" * 8192)
        previous[0x400:0x500] = image(3)
        previous[0x1000:0x1008] = b"MODELXYZ"
        header = bytearray(64)
        struct.pack_into("<III", header, 0, 0x314B4247, len(previous), zlib.crc32(previous))
        data[0x1000000:0x1000040] = header
        data[0x1001000:0x1003000] = previous
        return data

    def test_preservation_and_hex_roundtrip(self):
        original = self.dump()
        parts, report = prepare_migration(original, ROOT / "Binary/compat", 0, 1, 2)
        changed = dict(parts)
        self.assertEqual(changed[0x70100000][0x400:], original[0x100400:0x101000])
        self.assertEqual(changed[0x70B00000][64:], image(2))
        self.assertEqual(struct.unpack_from("<I", changed[0x70B00000], 48)[0], 1)
        previous = changed[0x71501000]
        self.assertEqual(previous[0x400:], original[0x1001400:0x1003000])
        self.assertEqual(struct.unpack_from("<I", previous, 4)[0], 2)
        self.assertEqual(struct.unpack_from("<I", changed[0x71500000], 8)[0], zlib.crc32(previous))
        self.assertTrue(report["staged"] and report["previous"])
        self.assertEqual(changed[0x700FC000], b"\xff" * 4096)
        out = ROOT / "Build/migration-tests"
        out.mkdir(parents=True, exist_ok=True)
        dest = out / "synthetic-only.hex"
        write_hex(parts, dest)
        self.assertEqual(sum(len(d) for _, d in hex_segments(dest.read_text())), sum(len(d) for _, d in parts))

    def test_reject_corrupt_backups_and_incomplete_dump(self):
        data = self.dump()
        with self.assertRaises(ValueError):
            prepare_migration(data[:-1], ROOT / "Binary/compat")
        data[0x400050] ^= 1
        with self.assertRaisesRegex(ValueError, "staging CRC"):
            prepare_migration(data, ROOT / "Binary/compat")

    def test_old_management_receiver_is_upgraded(self):
        data = self.dump()
        data[0x100480:0x100480 + 13] = b"OTA RTOS boot"
        parts, report = prepare_migration(data, ROOT / "Binary/compat")
        self.assertTrue(report["current_management_app_upgraded"])
        actual = dict(parts)[0x70100000]
        expected = (ROOT / "Binary/compat/appli.bin").read_bytes()
        self.assertEqual(actual[0x400:0x400 + len(expected)], expected)
        data = self.dump()
        data[0x1001500] ^= 1
        with self.assertRaisesRegex(ValueError, "rollback CRC"):
            prepare_migration(data, ROOT / "Binary/compat")


if __name__ == "__main__":
    unittest.main()
