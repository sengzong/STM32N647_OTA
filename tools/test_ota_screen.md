# Actual LVGL screen validation

Compile the production ota_screen.c and ota_status.c against the checked-in
LVGL v8.4 library and a guarded host LCD framebuffer. HAL time/cache operations
are supplied by the host fixture; layout, fonts, widgets, rendering, metadata
and framebuffer rotation use production code.

Checks CJK glyphs for 未提供, 600/800 MHz target profiles, included/absent models,
explicit/empty versions, receive/write percentages, error text, text bounds,
500 repeated updates without heap corruption and portrait/landscape clipping.
Runs against an actual FatFs card image and again with no card. Rendered previews
are in Build/ota-screen-tests/sd and fallback. They validate rendering,
not electrical panel timing, NOR operation, or actual hardware throughput.

Run `python tools/test_ota_screen.py` with host GCC and Pillow available.
