# SD UI resource exporter

Run python tools/make_ota_assets.py to export the local official LVGL 8.4 CJK font
to SD_Card/ota_ui/cjk16.bin, a manifest and the three-glyph built-in fallback.
The original font source is retained unchanged with its license. Theme files that
already exist are preserved. The script does not access or format physical cards.
