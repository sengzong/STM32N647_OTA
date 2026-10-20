# Optional SD resources

The sole UI thread mounts volume 0 once at startup, reads /ota_ui/theme.cfg and
/ota_ui/cjk16.bin, validates the complete font, then publishes it to LVGL.
Only sector reads are compiled. A missing card, failed mount, missing/oversized/
truncated/corrupt font selects the built-in three-glyph 未提供 fallback.
Color lines are bounded and accept only six hex digits; unknown keys are ignored.

The full font cache is 256 KiB in .OTA_ASSETS. In the compatible layout it starts
at 0x90A00000, after the 10 MiB receive region 0x90000000..0x909FFFFF. It is never
accessed by the NOR SRAM executor. No filesystem reads occur during glyph rendering,
so removing the card after loading leaves the active interface intact.

The resource cache remains allocated even if the SD card is absent. This does not
expand the receive buffer or make ordinary applications depend on an inserted card.
Resources reload on reset; runtime insertion/reload is not implemented.
