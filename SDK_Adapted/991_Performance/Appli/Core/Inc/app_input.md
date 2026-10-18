# Input conversion and cache ownership

app_prepare_input requires exclusive ownership of a completed, cache-line-aligned
camera buffer. It invalidates the previous CPU cache contents, XORs each byte
with 0x80 and cleans the result to memory for the NPU. MVE handles 16 bytes per
iteration; the portable tail is also used by host builds.

The application cleans/invalidates the entire input array before starting DMA,
and invalidates each completed inference input before returning it to the free
queue. It never converts the camera's currently active buffer. No CPU writes are
allowed between returning a buffer and acquiring its next completed image.

This preserves the original quantization exactly. Tests emulate fresh DMA frames
and check every output byte and guards for repeated buffer reuse. A target build
also checks for vector load/XOR/store instructions in the converter.
