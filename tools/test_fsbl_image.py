"""Regression checks for STM32N6 signed-image packaging, using real FSBL bytes."""
from pathlib import Path
import struct
import unittest

from verify_fsbl_image import verify_fsbl_image

ROOT = Path(__file__).resolve().parents[1]


class SignedImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = (ROOT / "Build/ota/LED_2_FSBL/LED_2_FSBL.bin").read_bytes()
        cls.signed = (ROOT / "Build/ota/LED_2_FSBL/LED_2_FSBL-trusted.bin").read_bytes()

    def test_aligned_image_and_entry(self):
        result = verify_fsbl_image(self.raw, self.signed)
        self.assertEqual(result["payload_offset"], 0x400)
        self.assertEqual(result["entry"], struct.unpack_from("<I", self.raw, 4)[0])

    def test_old_signer_default_is_rejected(self):
        misaligned = self.signed[:0x240] + self.raw
        with self.assertRaisesRegex(ValueError, "-align"):
            verify_fsbl_image(self.raw, misaligned)

    def test_wrong_entry_is_rejected(self):
        bad = bytearray(self.signed)
        struct.pack_into("<I", bad, 0x70, struct.unpack_from("<I", self.raw, 4)[0] + 4)
        with self.assertRaisesRegex(ValueError, "Reset_Handler"):
            verify_fsbl_image(self.raw, bad)

    def test_truncated_or_modified_payload_is_rejected(self):
        for bad in (self.signed[:-1], self.signed[:-1] + bytes([self.signed[-1] ^ 1])):
            with self.assertRaises(ValueError):
                verify_fsbl_image(self.raw, bad)

    def test_wrong_rom_length_and_checksum_are_rejected(self):
        for offset in (0x64, 0x6C):
            bad = bytearray(self.signed)
            bad[offset] ^= 1
            with self.assertRaises(ValueError):
                verify_fsbl_image(self.raw, bad)


if __name__ == "__main__":
    unittest.main()
