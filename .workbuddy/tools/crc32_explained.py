"""CRC-32 exactly as the board computes it -- plus proof that it is right.

Board side, Appli/Core/Src/app_netxduo.c L626-636:

    uint32_t ota_crc32(const uint8_t *p, uint32_t len)
    {
        uint32_t crc = 0xFFFFFFFFu, i;
        for (i = 0; i < len; i++) {
            uint32_t b = p[i], k;
            crc ^= b;
            for (k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

The FSBL has a byte-identical copy (bootmgr.c bm_crc32), which is
mandatory: both sides must agree on the value bit for bit.

This script:
  1. reimplements it bit by bit and compares against zlib.crc32
  2. writes the same loop the long way (if/else) to expose the
     branchless mask trick on the inner line
  3. shows what a single flipped bit does to the result
  4. doubles as a CLI:  python crc32_explained.py <file> [...]
     so you can CRC a firmware file and compare it with the value
     stored in the NOR slot header.
"""

import sys
import zlib

POLY = 0xEDB88320          # reversed 0x04C11DB7 = the Ethernet/zlib poly
MASK32 = 0xFFFFFFFF


def crc32_board(data):
    """Bit-by-bit, mirroring the C code literally."""
    crc = MASK32
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (POLY & (0 - (crc & 1)))
    return (~crc) & MASK32


def crc32_long_way(data):
    """Identical result, written as an if/else so the trick is visible."""
    crc = MASK32
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ POLY
            else:
                crc >>= 1
    return (~crc) & MASK32


def crc32_table(data):
    """The same function again, via a 256-entry lookup table."""
    table = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ (POLY & (0 - (c & 1)))
        table.append(c)
    crc = MASK32
    for b in data:
        crc = (crc >> 8) ^ table[(crc ^ b) & 0xFF]
    return (~crc) & MASK32


def show_mask_trick():
    print("the inner line:  crc = (crc >> 1) ^ (POLY & (0u - (crc & 1u)))")
    print("  (crc & 1) is the bit being divided out; 0 - that bit is a mask")
    for lsb in (0, 1):
        m = 0 - lsb
        print("  lsb=%d -> 0-lsb=0x%08X -> POLY & mask = 0x%08X  (%s)"
              % (lsb, m & MASK32, POLY & m,
                 "subtract" if lsb else "just shift"))
    print()


def show_bit_flip():
    base = b"appli-image-110688-bytes"
    good = crc32_board(base)
    print("one flipped bit, and the whole result changes:")
    print("  original            crc32 = %08X" % good)
    for pos in (0, 8, 24):
        bad = bytearray(base)
        bad[pos // 8] ^= 1 << (pos % 8)
        print("  flip bit %-3d        crc32 = %08X   (%s)"
              % (pos, crc32_board(bytes(bad)),
                 "changed" if crc32_board(bytes(bad)) != good else "SAME"))
    print()


def main():
    print("step 1 -- it really is the standard CRC-32 (zlib / Ethernet FCS)")
    vectors = [b"", b"a", b"abc", b"123456789", bytes(range(256))]
    ok = True
    for v in vectors:
        mine = crc32_board(v)
        ref = zlib.crc32(v) & MASK32
        same = mine == ref
        ok = ok and same
        label = repr(v) if len(v) < 12 else "<0..255, %d bytes>" % len(v)
        print("  %-24s board=%08X zlib=%08X  %s"
              % (label, mine, ref, "same" if same else "DIFFERENT"))
    print("  all three loops agree: %s"
          % (crc32_board(vectors[3]) == crc32_long_way(vectors[3])
             == crc32_table(vectors[3])))
    print("  => the board's crc32 values can be reproduced on the PC with"
          " zlib.crc32\n")

    show_mask_trick()
    show_bit_flip()

    print("step 2 -- CLI mode: CRC a file and compare with the slot header")
    if len(sys.argv) > 1:
        for path in sys.argv[1:]:
            with open(path, "rb") as fh:
                data = fh.read()
            print("  %s" % path)
            print("    size  = %d bytes" % len(data))
            print("    crc32 = %08X" % crc32_board(data))
    else:
        print("  (pass one or more file paths to use it, e.g.)")
        print("  python crc32_explained.py Binary/appli.hex")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
