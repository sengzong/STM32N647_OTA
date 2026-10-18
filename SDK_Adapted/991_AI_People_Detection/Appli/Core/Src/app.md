# 991 input pipeline

`app.c` is the SDK 991 application with only the input buffer definition and
initialization changed. It allocates `NN_INPUT_BUFFER_COUNT` images, fills a
temporary pointer array, and lets `app_bqueue_init` copy those pointers into its
own queue. The temporary array is not retained after initialization.

The capture callback still publishes completed frames only after it reserves
another free image. Three images allow capture, queued input and inference to
overlap. Existing output buffers, callbacks, model and business code are retained.

Build with `tools/build_991.py`; verify the real queue and callback using
`tools/test_991_buffers.py`. Hardware performance has not been measured yet.
