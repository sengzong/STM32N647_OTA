# Performance application

Derived from the local three-buffer application, app.c moves the input array into
32-byte-aligned internal BSS and calls app_prepare_input for cache-safe MVE
conversion. The NPU gets the same pixel values, model and output queue as before.

Prep and NN subdivide the existing Inference measurement. NN includes all model
runtime/CPU/NPU work. PP is postprocessing; Draw uses the previous completed draw.
MHz comes from SystemCoreClock, which the SDK startup updates from RCC registers.
The camera/display and cloud/gimbal application logic remain otherwise unchanged.
