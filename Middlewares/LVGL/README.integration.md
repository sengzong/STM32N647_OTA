# LVGL integration

Official LVGL v8.4.0 (4495f428630cc1741bd8bfd977f080e8460e8e8d).
Source: https://github.com/lvgl/lvgl/tree/v8.4.0
The user downloaded the official release ZIP; src/, lvgl.h, lv_conf_template.h
and LICENCE.txt are preserved here. The complete original extracted download
is retained locally under Build/lvgl-downloaded-source, outside published sources.
Upstream source files are not modified.

Project settings are in Appli/Core/Inc/lv_conf.h. The display port and progress
screen are in Appli/Core/Src/ota_screen.c; metadata/state publishing is separate
in ota_status.c. Both the management application and independent Agent link it.
No LVGL files are included in an SDK AI application's own firmware.
