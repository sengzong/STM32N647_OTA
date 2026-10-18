# Performance image checks

Run `python tools/test_991_performance.py` after `build_991.py --performance`.
The real input-conversion helper is compiled on host with cache-operation mocks.
Fresh simulated DMA data replaces a stale CPU view only when invalidated, and
converted data reaches the simulated NPU view only when cleaned. The test checks
every pixel, repeat reuse, guards and scalar/vector-tail lengths. A 16-lane shim
checks the vector control flow; actual MVE execution requires hardware testing.

The target build rejects missing MVE load/XOR/store instructions. Host checks
also run the actual capture callback and queue with the performance configuration,
validate package/model bytes and enforce internal SRAM/stack bounds.
