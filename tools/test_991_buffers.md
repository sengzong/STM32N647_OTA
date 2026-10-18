# 991 capture/queue regression

`python tools/test_991_buffers.py` compiles the SDK's real app_bqueue.c and the
capture callback extracted from the modified application, with minimal host
ThreadX semaphores. It uses the modified app_config.h and original queue header.

The event harness supplies 30 FPS frames and checks buffer ownership and token
counts while processing inputs at different speeds. A 42ms workload reproduces
15 FPS with two buffers and about 23.8 FPS with three. Short and long workloads
exercise ring wraparound and queue saturation. The output queue remains at two
buffers and is checked separately. This verifies software scheduling behavior,
not camera DMA timing or real hardware FPS.

Host GCC is expected at F:/msys64/ucrt64/bin/gcc.exe. Generated files stay in
Build/991-buffer-tests. When target outputs are present, the test also checks
manifest hashes, package CRC/profile, every application/model segment, unchanged
model HEX and the actual sender's package-description override.

`--performance` selects the performance source/configuration and its package.
The same real-queue/callback regression is used for both variants.
