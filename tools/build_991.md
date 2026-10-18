# SDK 991 overlay build

`python tools/build_991.py` completely rebuilds the 991 Release application from
the SDK source list and compiler recipes, with the two local files under
`SDK_Adapted/991_AI_People_Detection` overriding their originals.

The tool reads the SDK without writing into it. All objects, logs, linker
outputs and a unified source diff are placed in `Build/991-triple-buffer`.
It calls the compiler with argument arrays and does not execute Make recipes
through a shell. All original Release objects must have source recipes.

All SDK Core headers are mirrored into the build directory and the modified
app_config.h is applied there. This matters because quote-includes in
app_bqueue.h must see the same queue size as the application.

The build checks the linked input size/address and external RAM bounds, then
publishes application HEX/BIN, the unmodified paired model, an addressed combined
HEX and profile-2 OTA package to `Binary/991_triple_buffer`. Hashes, source counts
and a no-hardware-validation notice are recorded in manifest.json. The original
SDK package catalog is left intact. This tool never programs hardware.

`--performance` selects SDK_Adapted/991_Performance and the separate
Build/991-performance / Binary/991_performance output directories. It uses -O3
for five application/model CPU/library units, retains the original ABI options
and avoids fast-math. It checks that the input array fits AXI SRAM1/2, reserves
at least 192 KB above BSS for heap/system stack, and disassembles the converter
as Armv8.1-M to verify actual MVE loads/XOR/stores. Base three-buffer outputs
are not overwritten. Source/object lists are matched by basename, not list
order, because CubeIDE lists mixed C/assembly sources in different orders.
The performance output includes 991_performance.hex as a clearly named byte-for-
byte copy of appli.hex, to distinguish it from the first-round application.

`--realtime` selects SDK_Adapted/991_Realtime and publishes to
Binary/991_realtime_30, using the original 30 FPS sensor mode. Adding
`--capture60` selects the experimental sensor mode and separate
Binary/991_realtime_60_trial outputs. All 306 units are rebuilt for each variant;
project-relative and SDK-relative overlay sources are both applied. The source
diff uses the corresponding original project/SDK file. Distinct HEX aliases,
target capture mode, latest-frame handling and persistent runtime are recorded
in each manifest. All application/model addresses and profile-2 packaging remain
unchanged. Capture targets and software simulation do not establish board FPS.

After the 2026-10-18 RT60 board noise report, RT30 R2 excludes the three
camera-mode source overrides (app_camera.c, cmw_imx335.c and imx335.c) and uses
the original SDK sources. Only app.c/app_lcd.c are overridden for RT30, with the
existing core-header overlay. Excluded files are also omitted from source.patch.
Manifest source_overrides records the sources actually replaced. The realtime
test compares the three camera object files byte-for-byte against the working
performance build. RT60 remains a failed experiment; explicitly building it
prints a warning and records failed_noise_reported_2026-10-18 in its manifest.
