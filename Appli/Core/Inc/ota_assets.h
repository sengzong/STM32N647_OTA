#ifndef OTA_ASSETS_H
#define OTA_ASSETS_H
#include "lvgl.h"

typedef struct {
    uint32_t background, panel, accent, text, muted;
} OtaTheme;

void ota_assets_init(void);
const lv_font_t *ota_assets_font(void);
const OtaTheme *ota_assets_theme(void);
int ota_assets_sd_loaded(void);

/* Bounded font-file binding; it never retains a partially validated file. */
typedef struct {
    lv_font_t font;
    const uint8_t *records, *bitmap;
    uint32_t count;
} OtaAssetFont;
int ota_asset_font_bind(OtaAssetFont *font, const uint8_t *data, uint32_t length);
#endif
