# Firmware metadata regression

Runs the actual Python sender against existing ordinary and AI packages, checking
that names, explicit/empty versions, model presence, image bytes, CRC and CPU
profile survive transmission. Exercises existing embedded metadata, UTF-8 bounds
and control-character rejection. Compiles the production C state publisher/parser
to check retries clear stale versions and progress/error phases stay independent.

Run with Python and host GCC: `python tools/test_ota_metadata.py`.
