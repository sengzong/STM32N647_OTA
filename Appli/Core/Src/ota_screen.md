# LVGL OTA progress display

Requires official LVGL v8.4.0 in Middlewares/LVGL, with lvgl.h and src directly
inside that directory. Upstream source: https://github.com/lvgl/lvgl/tree/v8.4.0.
Display/tick integration follows https://docs.lvgl.io/8.4/porting/display.html
and https://docs.lvgl.io/8.4/porting/tick.html.

The current layout targets the board's 480x800 portrait LCD. It displays package
name, user-supplied firmware version, target CPU profile, model presence and a
phase-specific progress bar. An empty version is shown as 未提供. It is unrelated
to the OFW wire-protocol version. Legacy model metadata remains unknown.

The single LVGL owner is the application UI thread or independent Agent supervisor.
Rendering uses one 23 KB RGB565 draw buffer and the existing internal SRAM LTDC
framebuffer. The optional 256 KiB SD font cache follows the first 10 MiB PSRAM
receive area; missing/corrupt resources use a built-in three-glyph fallback. Dirty
rectangles are rotated into the existing portrait framebuffer without DMA2D.
HAL_GetTick supplies time. The flash writer restores XIP and interrupts between
4 KB sector operations, then yields so rendering and networking can run.

Completion means staged image storage is verified; FSBL installation follows reset.
This screen does not claim the new application is healthy or show FSBL copy progress.
Firmware must be rebuilt and the independent Agent updated before reset-time OTA
can display the screen. A PC webpage update alone cannot add board LCD support.
Fonts/themes are loaded once from SD_Card/ota_ui; rendering never reads the card.
See ota_assets.md and SD_Card/README.md for resources and memory placement.

The management application has its own LVGL root. ota_screen_show explicitly
loads the saved progress root; ota_screen_poll updates it only while active.
This prevents status updates from redrawing over management buttons. The text
font getter shares the cached SD fallback font with the management screen.
