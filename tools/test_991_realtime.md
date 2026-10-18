# Realtime validation

Run `python tools/test_991_realtime.py` after building both realtime variants.
Tests compile production capture/runtime headers, the actual adapted sensor
driver with the original register layer, and the actual LCD switching function.
Mocks provide ThreadX, interrupt masks, asynchronous runtime statuses and I2C.
Capture tests protect the inference buffer, replace pending old frames, inject
an ISR after semaphore consumption, wrap frame counters, and simulate sustained
30/60 FPS producers against 25/33/80 ms consumers. Simulation rates are not
hardware performance measurements. LCD tests sweep vblank phase against 60 FPS.

Artifact checks cover every published file hash, application alias equality,
unchanged model bytes, every application/model HEX segment inside each OTA image,
package CRC/profile, RAM bounds, build-mode flags and actual source overrides.
RT30 R2 has two overrides and must use the original three camera source/object
files, byte-identical to the previously working performance build. RT60 keeps
the historical five overrides for investigating its board noise failure.
The unchanged SRAM/MVE helper is covered by test_991_performance.py.
