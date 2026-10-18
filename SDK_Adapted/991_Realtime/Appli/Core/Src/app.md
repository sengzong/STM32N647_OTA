# Realtime application

Uses app_capture.h for latest-frame ownership, app_nn.h for persistent runtime,
and retains SRAM/MVE preparation. Per-output-slot timing travels with the output
buffer until postprocessing copies it with the matching boxes. This avoids mixing
a new NN duration/timestamp with previous detections. FPS counts completed
inferences over a one-second sampling interval rather than the last loop period.
Cam measures completed NN camera pipe frames; Age measures capture completion to
the start of the result draw. Exposure, readout, scanout and servo travel are not
included. RT30 R2 identifies the restored SDK-camera build; MHz is the CPU clock.
The withdrawn camera experiment is labeled RT60 TRIAL in future diagnostic builds.

The existing touch, gimbal and WiFi behavior is retained. Camera preview and
inference continue independently. No dummy detections, interpolation or repeated
inferences of the same completed frame are used to inflate FPS.
