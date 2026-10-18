/**
 ****************************************************************************************************
 * @file        model_select.c
 * @brief       多槽位固件切换 UI - 列出各槽描述, 点选 -> Yes 确认 -> 切换(复位启动该固件)
 *              接收固件时切换为"进度页"(由网络线程更新 g_ota_progress)。
 *              触摸事件只赋值目标页/动作; 绘制走 switch(current_page)。
 *              Ghosting 修复: 全部 framebuffer 写入走 CPU(cpu_fill), 每次批量后 SCB_CleanDCache。
 ****************************************************************************************************
 */

#include "model_select.h"
#include "../../Drivers/BSP/RGBLCD/rgblcd.h"
#include "../../Drivers/BSP/TOUCH/touch.h"
#include "app_ota_flash.h"
#include "tx_api.h"
#include "stdio.h"
#include "string.h"

#define UI_SLOTS   OTA_SLOT_COUNT   /* 列表项 = 槽位数 */

typedef enum
{
    PAGE_SELECT = 0,   /* 槽位列表 + Yes */
    PAGE_CONFIRM,      /* 切换确认 + Yes/Back */
    PAGE_COUNT
} ui_page_e;

static ui_page_e g_page = PAGE_SELECT;
static ui_page_e g_page_drawn = PAGE_COUNT;
static int8_t    g_selected = -1;
static uint8_t   g_do_switch = 0;          /* 确认页 Yes 按下 -> 触发切换 */
static int      g_switch_error;

/* 槽位项描述(刷新自 g_slots) */
static char     g_item[UI_SLOTS][OTA_SLOT_DESC_MAX + 8];
static uint16_t g_item_ready[UI_SLOTS];

/* layout */
static uint16_t L_X, L_Y, L_W, I_H, I_GAP, B_X, B_Y, B_W, B_H, TITLE_H, HINT_Y;

/* theme */
#define C_BG   WHITE
#define C_TEXT BLACK
#define C_SEL  GREEN
#define C_DIS  LGRAY
#define FB_PITCH rgblcddev.pwidth

/* ---------- CPU 侧填充(与文本同路径, 无 DMA) ---------- */
static void cpu_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t xi, yi;
    for (xi = x; xi < (uint16_t)(x + w); xi++)
    {
        uint16_t memrow = (uint16_t)(rgblcddev.pheight - 1 - xi);
        uint16_t *dst = &g_ltdc_lcd_framebuf[memrow * FB_PITCH];
        for (yi = y; yi < (uint16_t)(y + h); yi++)
            dst[yi] = color;
    }
}

static void fb_flush(void)
{
    SCB_CleanDCache();
}

static void layout_init(void)
{
    TITLE_H = rgblcddev.height / 10;
    L_X = rgblcddev.width / 24;
    L_W = rgblcddev.width - L_X * 2;
    L_Y = TITLE_H + L_X * 2;
    I_GAP = 12;
    I_H = (rgblcddev.height - L_Y) / (UI_SLOTS + 3) - I_GAP;
    B_W = rgblcddev.width / 2;
    B_H = I_H - 10;
    B_X = (rgblcddev.width - B_W) / 2;
    HINT_Y = L_Y + UI_SLOTS * (I_H + I_GAP) + 16;
    B_Y = HINT_Y + 40;
}

/* ---------- 槽位列表 ---------- */
static void slot_refresh_items(void)
{
    uint8_t i;
    ota_slot_refresh();
    for (i = 0; i < UI_SLOTS; i++)
    {
        g_item_ready[i] = g_slots[i].ready;
        if (g_slots[i].ready)
        {
            if (g_slots[i].desc[0])
                snprintf(g_item[i], sizeof(g_item[i]), "Latest: %s", g_slots[i].desc);
            else
                snprintf(g_item[i], sizeof(g_item[i]), "Latest firmware");
        }
        else
            snprintf(g_item[i], sizeof(g_item[i]), "No staged firmware");
    }
    /* There is one staging image, so a valid image is selected immediately. */
    if (g_selected < 0 || g_selected >= UI_SLOTS || !g_item_ready[(uint8_t)g_selected])
    {
        g_selected = -1;
        for (i = 0; i < UI_SLOTS; i++)
            if (g_item_ready[i]) { g_selected = (int8_t)i; break; }
    }
}

static void draw_green_dot(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color)
{
    int32_t dy;
    for (dy = -(int32_t)r; dy <= (int32_t)r; dy++)
    {
        int32_t w2 = (int32_t)r * (int32_t)r - dy * dy;
        int32_t dx = 0;
        while ((dx + 1) * (dx + 1) <= w2) dx++;
        cpu_fill(cx - (uint16_t)dx, cy + (uint16_t)dy, (uint16_t)(dx * 2 + 1), 1, color);
    }
}

static void draw_item(uint8_t idx, uint8_t selected)
{
    uint16_t y = L_Y + idx * (I_H + I_GAP);
    uint16_t bw = 4;

    cpu_fill(L_X, y, L_W, I_H, C_BG);
    if (selected)
    {
        cpu_fill(L_X, y, L_W, bw, C_SEL);
        cpu_fill(L_X, y + I_H - bw, L_W, bw, C_SEL);
        cpu_fill(L_X, y, bw, I_H, C_SEL);
        cpu_fill(L_X + L_W - bw, y, bw, I_H, C_SEL);
        draw_green_dot(L_X + 52, y + I_H / 2, 10, C_SEL);
    }
    else
    {
        rgblcd_draw_rectangle(L_X, y, L_X + L_W - 1, y + I_H - 1, C_TEXT);
    }
    rgblcd_show_string(L_X + 96, y + 16, L_W - 110, 32, 32, (char *)g_item[idx],
                       g_item_ready[idx] ? C_TEXT : C_DIS);
    rgblcd_show_string(L_X + 96, y + I_H - 36, L_W - 110, 24, 24,
                       g_item_ready[idx] ? "tap to switch" : "empty", C_DIS);
}

static void draw_yes_button(char *label, uint8_t available)
{
    uint16_t frame = available ? C_TEXT : C_DIS;
    uint16_t fg    = available ? C_TEXT : C_DIS;

    cpu_fill(B_X, B_Y, B_W, B_H, C_BG);
    rgblcd_draw_rectangle(B_X, B_Y, B_X + B_W - 1, B_Y + B_H - 1, frame);
    rgblcd_draw_rectangle(B_X + 3, B_Y + 3, B_X + B_W - 4, B_Y + B_H - 4, frame);
    rgblcd_show_string(B_X + (B_W - 3 * 32) / 2, B_Y + (B_H - 32) / 2, B_W - 8, 32, 32, label, fg);
}

/* 返回按钮(确认页, 左上角) */
static void draw_back_button(void)
{
    cpu_fill(L_X, HINT_Y - 40, 120, 44, C_BG);
    rgblcd_draw_rectangle(L_X, HINT_Y - 40, L_X + 119, HINT_Y + 3, C_TEXT);
    rgblcd_show_string(L_X + 24, HINT_Y - 34, 90, 32, 32, "Back", C_TEXT);
}

static void draw_select_page(int8_t selected)
{
    uint8_t i;
    char hint[64];
    uint16_t ok = (selected >= 0) ? g_item_ready[(uint8_t)selected] : 0;

    cpu_fill(0, 0, rgblcddev.width, rgblcddev.height, C_BG);
    rgblcd_show_string(L_X, (TITLE_H - 32) / 2, rgblcddev.width - L_X * 2, 32, 32,
                       "Install Latest Firmware", C_TEXT);
    rgblcd_draw_rectangle(0, TITLE_H - 2, rgblcddev.width - 1, TITLE_H - 1, C_TEXT);

    for (i = 0; i < UI_SLOTS; i++)
        draw_item(i, (selected == (int8_t)i) ? 1 : 0);

    if (g_switch_error != 0)
        snprintf(hint, sizeof(hint), "Install failed (%d). Please resend.", g_switch_error);
    else
        snprintf(hint, sizeof(hint), "%s", ok ? "Press Yes to review installation" : "Send firmware from the computer first");
    rgblcd_show_string(L_X, HINT_Y, L_W, 24, 24, hint, C_TEXT);
    draw_yes_button((char *)"Yes", ok);
}

static void draw_confirm_page(int8_t selected)
{
    cpu_fill(0, 0, rgblcddev.width, rgblcddev.height, C_BG);
    cpu_fill(0, 0, rgblcddev.width, TITLE_H, C_SEL);
    rgblcd_show_string(L_X, (TITLE_H - 32) / 2, rgblcddev.width - L_X * 2, 32, 32,
                       "Confirm Switch", BLACK);
    rgblcd_show_string(L_X, rgblcddev.height / 8, L_W, 32, 32,
                       (char *)g_item[(uint8_t)selected], C_TEXT);
    rgblcd_show_string(L_X, rgblcddev.height / 5, L_W, 24, 24,
                       "Save previous version, install and reboot.", C_DIS);
    draw_back_button();
    draw_yes_button((char *)"Yes", 1);
}

/* 接收进度页(网络线程更新 g_ota_progress) */
static void draw_progress_page(void)
{
    char buf[32];
    uint32_t pct = g_ota_progress;   /* 0..1000 */
    uint16_t bw;

    if (pct > 1000u) pct = 1000u;
    bw = (uint16_t)((uint32_t)L_W * pct / 1000u);

    cpu_fill(0, 0, rgblcddev.width, rgblcddev.height, C_BG);
    rgblcd_show_string(L_X, (TITLE_H - 32) / 2, rgblcddev.width - L_X * 2, 32, 32,
                       "Receiving Firmware", C_TEXT);
    rgblcd_draw_rectangle(0, TITLE_H - 2, rgblcddev.width - 1, TITLE_H - 1, C_TEXT);

    cpu_fill(L_X, rgblcddev.height / 3, L_W, 6, LGRAY);                       /* 底槽 */
    if (bw > 0u) cpu_fill(L_X, rgblcddev.height / 3, bw, 6, C_SEL);            /* 进度 */
    snprintf(buf, sizeof(buf), "Receiving %lu%%", (unsigned long)(pct / 10u));
    rgblcd_show_string(L_X, rgblcddev.height / 3 + 24, L_W, 32, 32, buf, C_TEXT);
    rgblcd_show_string(L_X, rgblcddev.height / 3 + 80, L_W, 24, 24,
                       "saving latest firmware, keep powered", C_DIS);
}

/* "切换中…" 覆盖页, 画完即调 ota_switch_slot(内部复位, 不返回) */
static void draw_switching_page(int8_t selected)
{
    cpu_fill(0, 0, rgblcddev.width, rgblcddev.height, C_BG);
    rgblcd_show_string(L_X, rgblcddev.height / 3, L_W, 32, 32, "Switching...", C_TEXT);
    rgblcd_show_string(L_X, rgblcddev.height / 3 + 50, L_W, 24, 24,
                       (char *)g_item[(uint8_t)selected], C_DIS);
    fb_flush();
}

/* ---------- 页面分发 ---------- */
static uint32_t s_prog_pct = 0xFFFFFFFFu;

static void ui_dispatch(void)
{
    if (g_ota_receiving)
    {
        /* 接收进度页: 每变化 1% 才整篇重绘, 避免每帧全屏刷 */
        uint32_t pct = (g_ota_progress > 1000u) ? 1000u : g_ota_progress;
        if ((pct / 10u) == (s_prog_pct / 10u)) return;
        s_prog_pct = pct;
        rgblcd_clear(C_BG);
        draw_progress_page();
        fb_flush();
        g_page_drawn = PAGE_COUNT;          /* 离开接收后强制重画列表 */
        return;
    }
    s_prog_pct = 0xFFFFFFFFu;

    if (g_page == g_page_drawn)
        return;

    fb_flush();
    rgblcd_clear(C_BG);
    fb_flush();
    slot_refresh_items();                   /* 每次进页都重读(接收完新槽立刻可见) */
    switch (g_page)
    {
        case PAGE_SELECT:  draw_select_page(g_selected);  break;
        case PAGE_CONFIRM: draw_confirm_page(g_selected); break;
        default: break;
    }
    fb_flush();
    g_page_drawn = g_page;
}

/* ---------- 触摸 ---------- */
static uint8_t in_rect(uint16_t x, uint16_t y, uint16_t rx, uint16_t ry, uint16_t rw, uint16_t rh)
{
    return (x >= rx) && (x <= rx + rw - 1) && (y >= ry) && (y <= ry + rh - 1);
}

static void ui_select_follow(uint16_t x, uint16_t y)
{
    uint8_t i;
    int8_t old = g_selected;

    if (g_page != PAGE_SELECT) return;

    for (i = 0; i < UI_SLOTS; i++)
    {
        uint16_t iy = L_Y + i * (I_H + I_GAP);
        if (g_item_ready[i] && in_rect(x, y, L_X, iy, L_W, I_H))
        {
            if (g_selected != (int8_t)i)
            {
                g_selected = (int8_t)i;
                if (old >= 0) draw_item((uint8_t)old, 0);
                draw_item((uint8_t)i, 1);
                draw_yes_button((char *)"Yes", 1);
                fb_flush();
            }
            break;
        }
    }
}

static void ui_touch_process(uint16_t x, uint16_t y)
{
    if (g_ota_receiving || x >= rgblcddev.width || y >= rgblcddev.height) return;
    switch (g_page)
    {
        case PAGE_SELECT:
            ui_select_follow(x, y);
            if ((g_selected >= 0) && g_item_ready[(uint8_t)g_selected] &&
                in_rect(x, y, B_X, B_Y, B_W, B_H))
            {
                g_page = PAGE_CONFIRM;
                printf("UI: installation confirmation opened\r\n");
            }
            break;

        case PAGE_CONFIRM:
            if (g_selected >= 0 && g_item_ready[(uint8_t)g_selected] &&
                in_rect(x, y, B_X, B_Y, B_W, B_H))
            {
                g_do_switch = 1;
                printf("UI: install confirmed, slot=%d\r\n", g_selected);
            }
            else if (in_rect(x, y, L_X, HINT_Y - 40, 120, 44))  g_page = PAGE_SELECT;
            break;

        default:
            break;
    }
}

/* ---------- 主循环 ---------- */
void model_select_run(void)
{
    uint8_t last_press = 0;
    uint8_t was_receiving = 0;
    uint8_t touch_status;
    uint16_t x, y;

    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_10, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_SET);
    touch_status = tp_dev.init();
    layout_init();
    g_page = PAGE_SELECT;
    g_selected = -1;
    g_do_switch = 0;
    g_switch_error = 0;
    slot_refresh_items();

    printf("UI: LCD=%04X size=%ux%u touch=%02X init=%u selected=%d\r\n",
           rgblcddev.id, rgblcddev.width, rgblcddev.height,
           tp_dev.touchtype, touch_status, g_selected);
    printf("UI: Yes area x=%u..%u y=%u..%u\r\n", B_X, B_X+B_W-1, B_Y, B_Y+B_H-1);

    ui_dispatch();

    while (1)
    {
        uint8_t press;

        tp_dev.scan(0);
        press = (tp_dev.sta & TP_PRES_DOWN) ? 1 : 0;

        if (was_receiving && !g_ota_receiving)
        {
            g_page = PAGE_SELECT;
            g_page_drawn = PAGE_COUNT;
            g_switch_error = 0;
            g_do_switch = 0;
        }
        if (g_ota_receiving) g_do_switch = 0;
        was_receiving = g_ota_receiving;
        if (!g_ota_receiving)
        {
            if (press) { x = tp_dev.x[0]; y = tp_dev.y[0]; ui_select_follow(x, y); }
            if (press && !last_press)
            {
                x = tp_dev.x[0]; y = tp_dev.y[0];
                printf("UI: touch x=%u y=%u page=%u\r\n", x, y, (unsigned)g_page);
                ui_touch_process(x, y);
            }
        }
        last_press = press;

        if (g_do_switch && !g_ota_receiving)
        {
            int s;
            g_do_switch = 0;
            draw_switching_page(g_selected);
            /* 只写引导记录 + 复位: 拷贝/校验/回滚由 FSBL 做, 成功不返回 */
            s = ota_switch_slot((uint32_t)((g_selected >= 0) ? (uint8_t)g_selected : 0u));
            if (s != 0)   /* 能返回到这里 = 记录没写成功, 没切 */
            {
                printf("SWITCH FAIL rc=%d\r\n", s);
                g_switch_error = s;
                g_page = PAGE_SELECT;
                g_page_drawn = PAGE_COUNT;   /* 强制回到列表 */
            }
            else
            {
                for (;;) { }   /* 理论走不到 */
            }
        }

        /* 本固件若是 FSBL 刚拷进来的"试运行"版本, 跑满 3 秒且自检通过就确认为正式版本;
         * 不确认则下次复位由 FSBL 回滚。只在第一次生效, 之后的调用是空转。
         * 放在主循环里(而不是阻塞 3 秒)是为了让触摸在等待期内仍然响应。 */
        ota_confirm_poll();

        ui_dispatch();
        tx_thread_sleep(1);
    }
}
