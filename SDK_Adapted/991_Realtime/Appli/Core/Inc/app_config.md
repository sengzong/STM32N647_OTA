# Realtime buffer configuration

Retains three SRAM input buffers and queue capacity three from the performance
overlay. DISPLAY_DELAY is zero; DISPLAY_BUFFER_NB remains three explicitly for
safe asynchronous background reload at the original LCD refresh rate. Foreground
drawing keeps its original two buffers. No CPU/NPU/system-clock changes.
