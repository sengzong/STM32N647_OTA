# LVGL latest firmware installation page

Only one staged latest firmware is displayed. Installing it asks FSBL to save
the current application as the previous-version backup and boot the replacement.
The independent PC-side receiver also supports requesting rollback without this UI.

The former white framebuffer UI is now a dark LVGL management screen, using
native labels, buttons and the pointer input driver. It shares ota_screen's
display, font cache and SD theme. The selection and confirmation states reuse
the same objects; the progress screen is a separate LVGL root.

The sole ready image is selected automatically. Yes, review opens confirmation;
a second distinct press/release on Yes, install requests installation. Back
cancels confirmation. The empty state disables the install button. Boot-held,
out-of-range and transfer-held fingers are blocked until physical release.
Dragging out of the button cancels that click. Incoming transfers cancel pending
installation, load the progress screen and return to refreshed firmware details.
Installation errors remain visible and allow another attempt.

UART logs identify LVGL manager v4 and report LCD dimensions, touch initialization,
confirmation events and install failures. The startup banner is FW-LVGL v4.
The LCD initializes with the dark background before LVGL's first frame.

Build with `python tools/build_ota.py --compat-package`. The management screen is
in Binary/compat/appli.hex, not solely in Agent. Agent handles the independent
reset-time receiver; updating it alone cannot change an older management app.
Binary/compat/initial_install.hex contains both new components, but also replaces
the application and initializes OTA records. Use Binary/compat/management_update.hex
for a direct Programmer management-app update: it also refreshes the runtime image
length/profile so the next rollback backup covers the complete image, without
initializing staged/rollback data. Keep raw appli.hex for the OTA sender and migration
tools. Hardware touch still needs validation.

`python tools/test_model_select.py` exercises real LVGL input and rendering with
an SD filesystem image and again without a card. Previews are in Build/ui-tests.
