# Installation UI regression

Compiles production model_select.c and ota_screen.c with the real LVGL engine,
FatFs, resource loader and simulated LCD/OTA APIs. Feeds timed pointer samples
through LVGL's actual input timer rather than calling UI event callbacks directly.
Checks two distinct confirmations, held fingers at boot and during transfer,
Back, disabled empty entries, out-of-range coordinates, dragged-out clicks,
installation failure, progress/management screen return, and 100 screen round
trips without heap growth. Runs with a real FAT image and without a card, exports
480x800 PNG previews under Build/ui-tests, and checks framebuffer bounds.
Uses the shared host build/render helpers in test_ota_screen.py. Run with GCC in PATH:
`python tools/test_model_select.py`.
