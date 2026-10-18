# 991 buffer configuration

`NN_INPUT_BUFFER_COUNT` is 3. `BQUEUE_MAX_BUFFERS` uses the same value so the
queue structure can hold every input buffer pointer. The output queue still uses
two buffers and fits within that capacity.

At 320 x 320 x 3 bytes per image, the change adds 307200 bytes of external RAM.
Every translation unit using the queue must be rebuilt with the updated header;
the build tool mirrors the original header directory and overlays this file.
