# Camera build selection

RT30 R2 uses the original SDK camera sources; this header supplies only the
displayed mode label to app.c in that build. The BIN60 branch is withdrawn after
the board noise report and retained for diagnosis only.

Default uses the original 2592x1944 camera mode at 30 FPS. APP_CAPTURE_BIN60=1
selects 1296x972 binning targeting 60 FPS. The AI input remains 320x320 RGB.
Resolution ID 7 extends the SDK's original ID 6 without changing SDK headers.
Exposure is limited to 16600 us in the fixed 60 FPS mode. Register facts and
hardware-validation limitations are documented in the overlay README and driver.
