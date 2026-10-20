# SD resource regression

Compiles production resource loading/font callbacks with actual FatFs and LVGL.
Creates synthetic local FAT16 images and verifies real directory/file reads,
all 1436 exported glyph metrics/bitmaps against the upstream font, missing cards,
invalid volumes, corrupt/truncated fonts and rendering after card removal.
No physical disk or development board is accessed.
