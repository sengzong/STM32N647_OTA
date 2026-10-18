"""Demonstrate the two overlap hazards inside ota_store_to_next_slot().

Mirrors Appli/Core/Src/app_netxduo.c L694-701:

    h->crc32 = ota_crc32(img, img_len);                     step 1
    memmove(ota_rx_buffer + OTA_SLOT_HEADER, img, img_len);  step 2
    h->magic = ...; memset(h->desc, 0, 32); ...              step 3

Hazard A -- the image source [16+dl, 16+dl+il) overlaps the slot
    header region [0, 64).  Writing the header first wipes the
    beginning of the image, so the header must be written LAST.

Hazard B -- the image source also overlaps its own destination
    [64, 64+il).  The destination sits at a HIGHER address, so a
    forward byte loop overwrites source bytes it has not read yet.
    That is why memmove (backward copy) is required, not memcpy.

Run:  python overlap_demo.py
No dependencies, no board needed.
"""

BUF = 192          # pretend the receive buffer is small, for clarity
HDRZ = 16          # container header size
DL = 8             # description length
IL = 48            # image length (chosen so both overlaps really happen)
SLOTHDR = 64       # OTA_SLOT_HEADER
SRC = HDRZ + DL    # 24  -> where the image currently sits
DST = SLOTHDR      # 64  -> where the image must end up
PAYLOAD = bytes(range(0x10, 0x10 + IL))


def build():
    """Reproduce the buffer right after the container was received."""
    b = bytearray(BUF)
    b[0:16] = (b"OFW2" + (2).to_bytes(4, "little")
               + DL.to_bytes(4, "little") + IL.to_bytes(4, "little"))
    b[16:16 + DL] = b"cam-v1.0"
    b[SRC:SRC + IL] = PAYLOAD
    return b


def write_header(b):
    """Step 3: memset the 64-byte slot header, then fill it in."""
    b[0:SLOTHDR] = bytes(SLOTHDR)
    b[0:4] = (0x31544C53).to_bytes(4, "little")
    b[4:8] = IL.to_bytes(4, "little")
    b[8:12] = DL.to_bytes(4, "little")
    b[16:16 + DL] = b"cam-v1.0"


def cp_forward(b, s, d, n):
    """What memcpy may do: low address first."""
    for i in range(n):
        b[d + i] = b[s + i]


def cp_backward(b, s, d, n):
    """What memmove does when dst > src: high address first."""
    for i in range(n - 1, -1, -1):
        b[d + i] = b[s + i]


def report(tag, b):
    got = bytes(b[DST:DST + IL])
    print(tag)
    print("   dst[64:112] first 8 bytes : %s" % list(got[:8]))
    print("   dst[64:112] last  8 bytes : %s" % list(got[-8:]))
    print("   equals the received image? %s" % (got == PAYLOAD))
    print()


def main():
    print("image lives at buf[%d:%d], must move to buf[%d:%d]"
          % (SRC, SRC + IL, DST, DST + IL))
    print("overlap region of source and destination: buf[%d:%d]"
          % (DST, SRC + IL))
    print("received image first 8 bytes: %s" % list(PAYLOAD[:8]))
    print()

    b = build()
    cp_backward(b, SRC, DST, IL)
    write_header(b)
    report("1) backward copy, then header   <-- the real code", b)

    b = build()
    cp_forward(b, SRC, DST, IL)
    write_header(b)
    report("2) forward copy, then header    <-- memcpy behaviour", b)

    b = build()
    write_header(b)
    cp_backward(b, SRC, DST, IL)
    report("3) header first, then copy      <-- wrong order", b)


if __name__ == "__main__":
    main()
