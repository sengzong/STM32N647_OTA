/* LVGL is called only by the UI/supervisor thread. All buffers are internal. */
#include "lvgl.h"
#include "ota_screen.h"
#include "ota_status.h"
#include "ota_display_blit.h"
#include "ota_assets.h"
#include "rgblcd.h"
#include <stdio.h>
#include <string.h>

static lv_disp_draw_buf_t draw_buffer;
static lv_disp_drv_t display;
static lv_color_t pixels[480*24];
static lv_obj_t *name_value, *version_value, *clock_value, *model_value;
static lv_obj_t *phase_value, *percent_value, *bytes_value, *progress_bar, *footer;
static OtaStatus shown;
static uint8_t initialized, visible;
static lv_font_t text_font;
static const OtaTheme *theme;
static lv_obj_t *progress_screen;

uint32_t ota_lvgl_time_ms(void) { return HAL_GetTick(); }

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *data)
{
    ota_display_blit(g_ltdc_lcd_framebuf, rgblcddev.pwidth, rgblcddev.pheight,
                     rgblcddev.dir, area->x1, area->y1, area->x2, area->y2,
                     (const uint16_t *)data);
    SCB_CleanDCache();
    lv_disp_flush_ready(driver);
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const char *text,
                       uint32_t color, const lv_font_t *font)
{
    lv_obj_t *item = lv_label_create(parent);
    if(color==0xf4f8fcu) color=theme->text;
    else if(color==0x8da5bdu) color=theme->muted;
    else if(color==0x66dfb1u) color=theme->accent;
    lv_obj_set_pos(item, x, y); lv_obj_set_width(item, width);
    lv_obj_set_style_text_font(item, font, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(color), 0);
    lv_label_set_long_mode(item, LV_LABEL_LONG_WRAP);
    lv_label_set_text(item, text);
    return item;
}

static lv_obj_t *card(lv_obj_t *screen, int x, int y, int width, int height)
{
    lv_obj_t *obj = lv_obj_create(screen);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,width,height);
    lv_obj_set_style_bg_color(obj,lv_color_hex(theme->panel),0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_radius(obj,16,0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

void ota_screen_init(void)
{
    if (initialized) return;
    initialized = 1u;
    ota_assets_init();
    theme=ota_assets_theme();
    lv_init();
    text_font = lv_font_montserrat_20;
    text_font.fallback = ota_assets_font();
    lv_disp_draw_buf_init(&draw_buffer,pixels,NULL,sizeof(pixels)/sizeof(pixels[0]));
    lv_disp_drv_init(&display);
    display.hor_res=rgblcddev.width; display.ver_res=rgblcddev.height;
    display.draw_buf=&draw_buffer; display.flush_cb=flush;
    if (!lv_disp_drv_register(&display)) { initialized=0u; return; }
    lv_obj_t *screen=lv_scr_act();
    progress_screen=screen;
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen,lv_color_hex(theme->background),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    int w=rgblcddev.width, h=rgblcddev.height, margin=24, cw=w-2*margin;
    label(screen,margin,24,cw,"STM32N647",0x66dfb1,&lv_font_montserrat_20);
    label(screen,margin,62,cw,"Firmware Update",0xf4f8fc,&lv_font_montserrat_28);
    label(screen,margin,108,cw,"Ethernet OTA",0x8da5bd,&lv_font_montserrat_16);
    lv_obj_t *info=card(screen,margin,151,cw,288);
    label(info,20,20,cw-40,"FIRMWARE",0x8da5bd,&lv_font_montserrat_16);
    name_value=label(info,20,49,cw-40,"Waiting for package...",0xf4f8fc,&text_font);
    lv_obj_set_height(name_value,70); lv_label_set_long_mode(name_value,LV_LABEL_LONG_DOT);
    label(info,20,132,100,"Version",0x8da5bd,&lv_font_montserrat_16);
    version_value=label(info,130,130,cw-150,"未提供",0xf4f8fc,&text_font);
    lv_obj_set_height(version_value,28); lv_label_set_long_mode(version_value,LV_LABEL_LONG_DOT);
    label(info,20,181,100,"Target CPU",0x8da5bd,&lv_font_montserrat_16);
    clock_value=label(info,130,179,cw-150,"-- MHz",0x66dfb1,&lv_font_montserrat_20);
    label(info,20,230,100,"AI model",0x8da5bd,&lv_font_montserrat_16);
    model_value=label(info,130,228,cw-150,"未提供",0xf4f8fc,&text_font);
    lv_obj_t *transfer=card(screen,margin,461,cw,196);
    phase_value=label(transfer,20,20,cw-110,"Ready",0xf4f8fc,&lv_font_montserrat_20);
    percent_value=label(transfer,cw-90,20,70,"0%",0x66dfb1,&lv_font_montserrat_20);
    progress_bar=lv_bar_create(transfer);
    lv_obj_remove_style_all(progress_bar);
    lv_obj_set_pos(progress_bar,20,68); lv_obj_set_size(progress_bar,cw-40,14);
    lv_obj_set_style_radius(progress_bar,7,LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar,lv_color_hex(0x304258),LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progress_bar,LV_OPA_COVER,LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar,7,LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar,lv_color_hex(theme->accent),LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress_bar,LV_OPA_COVER,LV_PART_INDICATOR);
    lv_bar_set_range(progress_bar,0,1000); lv_bar_set_value(progress_bar,0,LV_ANIM_OFF);
    bytes_value=label(transfer,20,105,cw-40,"Waiting for computer",0x8da5bd,&lv_font_montserrat_16);
    label(transfer,20,149,cw-40,"Receive > Check > Write > Ready",0x8da5bd,&lv_font_montserrat_16);
    footer=label(screen,margin,h-104,cw,"Keep power connected.\nDo not reset while updating.",0x8da5bd,&lv_font_montserrat_16);
    memset(&shown,0xff,sizeof(shown));
}

void ota_screen_show(void)
{
    if (!initialized) ota_screen_init();
    if (!initialized) return;
    visible=1u; memset(&shown,0xff,sizeof(shown));
    lv_scr_load(progress_screen);
    lv_obj_invalidate(progress_screen);
}

const lv_font_t *ota_screen_text_font(void) { return &text_font; }

void ota_screen_poll(void)
{
    OtaStatus s;
    if (!initialized || !visible || lv_scr_act()!=progress_screen) return;
    if (ota_status_snapshot(&s) && memcmp(&s,&shown,sizeof(s)))
    {
        char text[96]; uint32_t done=s.received,total=s.receive_total;
        const char *phase="Waiting for package";
        lv_label_set_text(name_value,s.metadata_ready ? (s.name[0]?s.name:"Application") : "Waiting for package...");
        lv_label_set_text(version_value,s.version[0]?s.version:"未提供");
        if (s.metadata_ready) snprintf(text,sizeof(text),"%u MHz",s.profile==1u?600u:800u);
        else snprintf(text,sizeof(text),"-- MHz");
        lv_label_set_text(clock_value,text);
        lv_label_set_text(model_value,s.model==1u?"Included":s.model==0u?"None":"未提供");
        if (s.write_total && (s.phase==OTA_UI_READY || s.phase==OTA_UI_ERROR)) {
            done=s.written; total=s.write_total;
        }
        if (s.phase==OTA_UI_RECEIVING) phase="Receiving";
        else if (s.phase==OTA_UI_CHECKING) phase="Checking CRC";
        else if (s.phase==OTA_UI_WRITING) { phase="Writing flash"; done=s.written; total=s.write_total; }
        else if (s.phase==OTA_UI_READY) phase="Stored and verified";
        else if (s.phase==OTA_UI_ERROR) phase="Update failed";
        else if (s.phase==OTA_UI_ROLLBACK) phase="Rollback queued";
        lv_label_set_text(phase_value,phase);
        uint32_t per=total?(uint32_t)((uint64_t)done*1000u/total):0u;
        if (per>1000u) per=1000u;
        lv_bar_set_value(progress_bar,per,LV_ANIM_OFF);
        snprintf(text,sizeof(text),"%lu%%",(unsigned long)(per/10u)); lv_label_set_text(percent_value,text);
        if (total) snprintf(text,sizeof(text),"%lu / %lu B",(unsigned long)done,(unsigned long)total);
        else snprintf(text,sizeof(text),"PC 192.168.2.10 : 8080");
        lv_label_set_text(bytes_value,text);
        if (s.phase==OTA_UI_ERROR) {
            snprintf(text,sizeof(text),"Error %ld. Please resend.\nKeep the Ethernet cable connected.",(long)s.error);
            lv_label_set_text(footer,text);
        } else lv_label_set_text(footer,"Keep power connected.\nDo not reset while updating.");
        shown=s;
    }
    lv_timer_handler();
}
