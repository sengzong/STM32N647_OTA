# Input conversion

Identical to the tested performance overlay: invalidate camera-written cache
lines, XOR every RGB byte with 0x80 using MVE where available, then clean for NPU
access. Scalar tails preserve the same int8 values. All three buffers are 32-byte
aligned internal SRAM. The realtime test checks the helper remains byte-identical
to that implementation, and the build verifies generated MVE instructions.
