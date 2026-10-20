#ifndef OTA_SCREEN_H
#define OTA_SCREEN_H
#include "lvgl.h"

void ota_screen_init(void);
void ota_screen_show(void);
void ota_screen_poll(void);
const lv_font_t *ota_screen_text_font(void);

#endif
