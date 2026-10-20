/* The management application and OTA progress page share one LVGL display.
 * Only this UI thread may call LVGL. Network threads publish OTA status only. */
#include "model_select.h"
#include "rgblcd.h"
#include "touch.h"
#include "app_ota_flash.h"
#include "ota_screen.h"
#include "ota_assets.h"
#include "tx_api.h"
#include <stdio.h>

typedef enum { PAGE_SELECT, PAGE_CONFIRM, PAGE_SWITCHING } ui_page_e;
static ui_page_e g_page;
static int8_t g_selected = -1;
static int g_switch_error;
static uint8_t g_do_switch, was_receiving, touch_blocked;
static uint8_t touch_pressed;
static uint16_t touch_x, touch_y;
static lv_obj_t *manager_screen, *manager_title, *manager_subtitle;
static lv_obj_t *manager_badge, *manager_name, *manager_size, *manager_hint;
static lv_obj_t *manager_action, *manager_action_text, *manager_back;
static lv_obj_t *manager_note;
static lv_indev_t *manager_pointer;
static lv_indev_drv_t manager_input;
static const OtaTheme *manager_theme;

static int manager_ready(void)
{
    return g_selected >= 0 && g_selected < OTA_SLOT_COUNT &&
           g_slots[(uint8_t)g_selected].ready && g_slots[(uint8_t)g_selected].img_len;
}

static lv_obj_t *manager_label(lv_obj_t *parent, int x, int y, int width,
                               int height, const char *text, uint32_t color,
                               const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_label_set_text(obj, text);
    return obj;
}

static void manager_render(void)
{
    char text[96];
    int ready = manager_ready();
    const char *name = ready ? g_slots[(uint8_t)g_selected].desc : "No staged firmware";
    lv_label_set_text(manager_name, name[0] ? name : "Latest firmware");
    lv_label_set_text(manager_badge, ready ? "LATEST / READY" : "LATEST / EMPTY");
    lv_obj_set_style_text_color(manager_badge,
        lv_color_hex(ready ? manager_theme->accent : manager_theme->muted), 0);
    if (ready) snprintf(text, sizeof(text), "%lu bytes  |  Stored on board",
                        (unsigned long)g_slots[(uint8_t)g_selected].img_len);
    else snprintf(text, sizeof(text), "Send a package from the computer first.");
    lv_label_set_text(manager_size, text);

    lv_obj_add_flag(manager_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(manager_action, LV_STATE_DISABLED);
    if (!ready || g_page == PAGE_SWITCHING)
        lv_obj_add_state(manager_action, LV_STATE_DISABLED);

    if (g_page == PAGE_SWITCHING)
    {
        lv_label_set_text(manager_title, "Starting firmware");
        lv_label_set_text(manager_subtitle, "Preparing installation");
        lv_label_set_text(manager_hint, "Saving the boot request.\nThe board will restart automatically.");
        lv_label_set_text(manager_action_text, "Please wait...");
        lv_label_set_text(manager_note, "Keep power connected during installation.");
    }
    else if (g_page == PAGE_CONFIRM)
    {
        lv_label_set_text(manager_title, "Confirm installation");
        lv_label_set_text(manager_subtitle, "Review your selection");
        lv_label_set_text(manager_hint, "The current firmware will be saved for\nrollback. Install the selected firmware?");
        lv_label_set_text(manager_action_text, "Yes, install");
        lv_label_set_text(manager_note, "Installation restarts the board automatically.");
        lv_obj_clear_flag(manager_back, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(manager_title, "Firmware Center");
        lv_label_set_text(manager_subtitle, "Select the latest firmware to install");
        if (g_switch_error)
        {
            snprintf(text, sizeof(text), "Install failed (%d).\nPlease resend the firmware.", g_switch_error);
            lv_label_set_text(manager_hint, text);
        }
        else lv_label_set_text(manager_hint, ready ?
            "Your firmware is ready.\nTap Yes to review before installing." :
            "Waiting for a new package.\nKeep the Ethernet cable connected.");
        lv_label_set_text(manager_action_text, "Yes, review");
        lv_label_set_text(manager_note, "PC 192.168.2.10 : 8080\nSend firmware from the OTA webpage.");
    }
}

static void manager_click(lv_event_t *event)
{
    if (g_ota_receiving || touch_blocked || g_page == PAGE_SWITCHING) return;
    if (lv_event_get_target(event) == manager_back)
    {
        g_page = PAGE_SELECT;
        manager_render();
    }
    else if (manager_ready())
    {
        if (g_page == PAGE_SELECT)
        {
            g_page = PAGE_CONFIRM;
            printf("UI: installation confirmation opened\r\n");
            manager_render();
        }
        else
        {
            g_do_switch = 1u;
            printf("UI: install confirmed, slot=%d\r\n", g_selected);
        }
    }
}

static lv_obj_t *manager_button(int x, int y, int width, const char *text, int primary)
{
    lv_obj_t *obj = lv_btn_create(manager_screen);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, width, 64);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(primary ? manager_theme->accent : manager_theme->panel), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_opa(obj, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(obj, manager_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *caption = manager_label(obj, 0, 0, width - 12, 28, text,
        primary ? manager_theme->background : manager_theme->text, &lv_font_montserrat_20);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    if (primary) manager_action_text = caption;
    return obj;
}

static void manager_read_touch(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    data->point.x = touch_x; data->point.y = touch_y;
    data->state = touch_pressed && !g_ota_receiving && !touch_blocked ?
                  LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void manager_refresh_slots(void)
{
    ota_slot_refresh();
    g_selected = -1;
    for (unsigned i = 0; i < OTA_SLOT_COUNT; ++i)
        if (g_slots[i].ready && g_slots[i].img_len) { g_selected = (int8_t)i; break; }
}

static void manager_init(void)
{
    ota_screen_init();
    manager_theme = ota_assets_theme();
    manager_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(manager_screen);
    lv_obj_set_style_bg_color(manager_screen, lv_color_hex(manager_theme->background), 0);
    lv_obj_set_style_bg_opa(manager_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(manager_screen, LV_OBJ_FLAG_SCROLLABLE);
    int w = rgblcddev.width, cw = w - 48;
    manager_label(manager_screen, 24, 24, cw, 28, "STM32N647 / OTA", manager_theme->accent, &lv_font_montserrat_20);
    manager_title = manager_label(manager_screen, 24, 70, cw, 40, "", manager_theme->text, &lv_font_montserrat_28);
    manager_subtitle = manager_label(manager_screen, 24, 122, cw, 26, "", manager_theme->muted, &lv_font_montserrat_16);
    lv_obj_t *info = lv_obj_create(manager_screen);
    lv_obj_remove_style_all(info);
    lv_obj_set_pos(info, 24, 182); lv_obj_set_size(info, cw, 224);
    lv_obj_set_style_radius(info, 16, 0);
    lv_obj_set_style_bg_color(info, lv_color_hex(manager_theme->panel), 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(info, 1, 0);
    lv_obj_set_style_border_color(info, lv_color_hex(manager_theme->accent), 0);
    lv_obj_set_style_border_opa(info, LV_OPA_40, 0);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    manager_badge = manager_label(info, 20, 22, cw - 40, 26, "", manager_theme->accent, &lv_font_montserrat_16);
    manager_name = manager_label(info, 20, 65, cw - 40, 74, "", manager_theme->text, ota_screen_text_font());
    manager_size = manager_label(info, 20, 166, cw - 40, 26, "", manager_theme->muted, &lv_font_montserrat_16);
    manager_hint = manager_label(manager_screen, 24, 442, cw, 82, "", manager_theme->text, ota_screen_text_font());
    manager_action = manager_button(24, 552, cw, "", 1);
    manager_back = manager_button(24, 636, cw, "Back", 0);
    manager_note = manager_label(manager_screen, 24, 726, cw, 58, "", manager_theme->muted, &lv_font_montserrat_16);
    lv_indev_drv_init(&manager_input);
    manager_input.type = LV_INDEV_TYPE_POINTER;
    manager_input.read_cb = manager_read_touch;
    manager_pointer = lv_indev_drv_register(&manager_input);
    g_page = PAGE_SELECT;
    g_do_switch = was_receiving = 0;
    /* Ignore a finger held down during boot until it is physically released. */
    touch_blocked = 1;
    g_switch_error = 0;
    manager_refresh_slots();
    manager_render();
    lv_scr_load(manager_screen);
}

static void manager_step(uint8_t pressed, uint16_t x, uint16_t y)
{
    if (!pressed) touch_blocked = 0;
    else if (x >= rgblcddev.width || y >= rgblcddev.height)
    {
        touch_blocked = 1;
        lv_indev_reset(manager_pointer, NULL);
    }
    touch_pressed = pressed;
    if (x < rgblcddev.width && y < rgblcddev.height) { touch_x = x; touch_y = y; }
    if (g_ota_receiving)
    {
        g_do_switch = 0;
        touch_blocked = 1;
        if (!was_receiving)
        {
            lv_indev_reset(manager_pointer, NULL);
            ota_screen_show();
        }
        was_receiving = 1;
        ota_screen_poll();
        return;
    }
    if (was_receiving)
    {
        was_receiving = 0;
        g_page = PAGE_SELECT;
        g_do_switch = 0;
        g_switch_error = 0;
        manager_refresh_slots();
        manager_render();
        lv_scr_load(manager_screen);
    }
    lv_timer_handler();
    if (g_do_switch && !g_ota_receiving && manager_ready())
    {
        g_do_switch = 0;
        g_page = PAGE_SWITCHING;
        manager_render();
        lv_refr_now(NULL);
        /* Only write the boot record and reset; FSBL does the copy and rollback. */
        if (g_ota_receiving) { g_page = PAGE_SELECT; return; }
        int result = ota_switch_slot((uint32_t)(uint8_t)g_selected);
        if (!result) for (;;) { } /* Successful install requests reset in SRAM. */
        printf("SWITCH FAIL rc=%d\r\n", result);
        g_switch_error = result;
        g_page = PAGE_SELECT;
        manager_render();
    }
}

void model_select_run(void)
{
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_10, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_SET);
    uint8_t status = tp_dev.init();
    manager_init();
    printf("UI: LVGL manager v4 LCD=%04X size=%ux%u touch=%02X init=%u selected=%d\r\n",
           rgblcddev.id, rgblcddev.width, rgblcddev.height, tp_dev.touchtype, status, g_selected);
    while (1)
    {
        tp_dev.scan(0);
        manager_step((tp_dev.sta & TP_PRES_DOWN) != 0, tp_dev.x[0], tp_dev.y[0]);
        /* Confirm a healthy trial application after three seconds without
         * blocking touch input during the initial confirmation interval. */
        ota_confirm_poll();
        tx_thread_sleep(1);
    }
}
