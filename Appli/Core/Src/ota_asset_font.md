# Bounded SD bitmap-font format

32-byte little-endian header: magic SOF1, version 1, glyph count, bitmap bytes,
line height, baseline, bits per pixel (4), CRC32 of records and bitmaps.
Each 20-byte sorted record contains codepoint u32, bitmap offset u32, rounded
advance width u16, box width/height u16, signed x/y offsets i16, reserved u16.
Uncompressed packed 4-bit glyph bitmaps follow the records.

The binder checks total capacity, bounded character count/dimensions, sorted unique
Unicode scalar values, every bitmap extent and CRC before installing callbacks.
Callbacks do binary search in the cached file and never allocate or read the card.
The format is an integration resource format, not LVGL's lv_font_load format.
tools/make_ota_assets.py exports the upstream CJK font and minimal built-in subset.
