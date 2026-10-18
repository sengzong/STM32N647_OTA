/**
 ****************************************************************************************************
 * @file        app_lcd.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-13
 * @brief       app_lcd.c文件
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 * 
 * 实验平台:正点原子 N647开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#include "app_lcd.h"
#include "app_config.h"
#include "bsp_lcd.h"
#include <string.h>

static uint8_t app_lcd_bg_buffer[DISPLAY_BUFFER_NB][LCD_BG_WIDTH * LCD_BG_HEIGHT * 2] __attribute__((aligned(32))) __attribute__((section(".EXTRAM")));
static uint8_t app_lcd_bg_buffer_disp_idx = 1;
static uint8_t app_lcd_bg_buffer_fill_idx = 0;
static uint8_t app_lcd_fg_buffer[2][LCD_FG_WIDTH * LCD_FG_HEIGHT * 2] __attribute__((aligned(32))) __attribute__((section(".EXTRAM")));
static uint8_t app_lcd_fg_buffer_load_idx;

void app_lcd_init(void)
{
    bsp_lcd_layer_config_t bsp_lcd_layer_config = {0};

    memset(app_lcd_bg_buffer, 0, sizeof(app_lcd_bg_buffer));
    SCB_CleanInvalidateDCache_by_Addr(app_lcd_bg_buffer, sizeof(app_lcd_bg_buffer));
    memset(app_lcd_fg_buffer, 0, sizeof(app_lcd_fg_buffer));
    SCB_CleanInvalidateDCache_by_Addr(app_lcd_fg_buffer, sizeof(app_lcd_fg_buffer));

    bsp_lcd_init();
    bsp_lcd_set_brightness(100);

    bsp_lcd_layer_config.x0 = (BSP_LCD_WIDTH - LCD_BG_WIDTH) / 2;
    bsp_lcd_layer_config.y0 = (BSP_LCD_HEIGHT - LCD_BG_HEIGHT) / 2;
    bsp_lcd_layer_config.x1 = bsp_lcd_layer_config.x0 + LCD_BG_WIDTH;
    bsp_lcd_layer_config.y1 = bsp_lcd_layer_config.y0 + LCD_BG_HEIGHT;
    bsp_lcd_layer_config.pixel_format = LCD_PIXEL_FORMAT_RGB565;
    bsp_lcd_layer_config.address = (uint32_t)app_lcd_bg_buffer[app_lcd_bg_buffer_disp_idx];
    bsp_lcd_config_layer(0, &bsp_lcd_layer_config);

    bsp_lcd_layer_config.x0 = (BSP_LCD_WIDTH - LCD_FG_WIDTH) / 2;
    bsp_lcd_layer_config.y0 = (BSP_LCD_HEIGHT - LCD_FG_HEIGHT) / 2;
    bsp_lcd_layer_config.x1 = bsp_lcd_layer_config.x0 + LCD_FG_WIDTH;
    bsp_lcd_layer_config.y1 = bsp_lcd_layer_config.y0 + LCD_FG_HEIGHT;
    bsp_lcd_layer_config.pixel_format = LCD_PIXEL_FORMAT_ARGB4444;
    bsp_lcd_layer_config.address = (uint32_t)app_lcd_fg_buffer[1];
    bsp_lcd_config_layer(1, &bsp_lcd_layer_config);

    UTIL_LCD_SetFuncDriver(&bsp_lcd_driver);
    UTIL_LCD_SetLayer(1);
    UTIL_LCD_Clear(0x00000000);
    UTIL_LCD_SetFont(&Font20);
    UTIL_LCD_SetTextColor(UTIL_LCD_COLOR_WHITE);
}

uint8_t *app_lcd_get_bg_buffer(void)
{
    return app_lcd_bg_buffer[app_lcd_bg_buffer_fill_idx];
}

void app_lcd_switch_bg_buffer(void)
{
    /* Request the just-completed image, leaving the former front buffer alone. */
    app_lcd_bg_buffer_disp_idx = app_lcd_bg_buffer_fill_idx;
    app_lcd_bg_buffer_fill_idx = (app_lcd_bg_buffer_fill_idx + DISPLAY_BUFFER_NB - 1) % DISPLAY_BUFFER_NB;

    HAL_LTDC_SetAddress_NoReload(bsp_lcd_get_ltdc_handle(), (uint32_t)app_lcd_bg_buffer[app_lcd_bg_buffer_disp_idx], 0);
    HAL_LTDC_ReloadLayer(bsp_lcd_get_ltdc_handle(), LTDC_RELOAD_VERTICAL_BLANKING, 0);
}

void app_lcd_draw_area_update(void)
{
    __disable_irq();
    HAL_LTDC_SetAddress_NoReload(bsp_lcd_get_ltdc_handle(), (uint32_t)app_lcd_fg_buffer[app_lcd_fg_buffer_load_idx], 1);
    __enable_irq();
}

void app_lcd_draw_area_commit(void)
{
    SCB_CleanDCache_by_Addr(app_lcd_fg_buffer[app_lcd_fg_buffer_load_idx], sizeof(app_lcd_fg_buffer[app_lcd_fg_buffer_load_idx]));
    __disable_irq();
    HAL_LTDC_ReloadLayer(bsp_lcd_get_ltdc_handle(), LTDC_RELOAD_VERTICAL_BLANKING, 1);
    __enable_irq();
    app_lcd_fg_buffer_load_idx = 1 - app_lcd_fg_buffer_load_idx;
}
