"""Exercise production installation UI with a simulated touch and LCD."""
from pathlib import Path
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    folder = ROOT / "Build/ui-tests"
    folder.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "Appli/Core/Src/model_select.c").read_text(encoding="utf-8")
    source = re.sub(r'^#include .*$', '', source, flags=re.MULTILINE)
    preamble = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_ota_flash.h"
#define WHITE 0xffffu
#define BLACK 0u
#define GREEN 0x07e0u
#define LGRAY 0xc618u
#define GPIOG 0
#define GPIOE 1
#define GPIO_PIN_10 1024
#define GPIO_PIN_SET 1
#define TP_PRES_DOWN 0x8000u
static struct { uint16_t width,height;uint32_t pwidth,pheight;uint16_t id; } rgblcddev;
static uint16_t g_ltdc_lcd_framebuf[800*480];
static struct {uint8_t (*init)(void);uint8_t (*scan)(uint8_t);uint16_t x[10],y[10],sta;uint8_t touchtype;} tp_dev;
OtaSlotView g_slots[OTA_SLOT_COUNT];
volatile uint8_t g_ota_receiving;
volatile uint32_t g_ota_progress;
static uint16_t yes_color;
static void SCB_CleanDCache(void) {}
static void HAL_GPIO_WritePin(int a,int b,int c) {(void)a;(void)b;(void)c;}
static void tx_thread_sleep(unsigned a) {(void)a;}
void ota_slot_refresh(void) {}
void ota_confirm_poll(void) {}
int ota_switch_slot(uint32_t slot) {(void)slot;return -1;}
static void rgblcd_clear(uint16_t c) {(void)c;}
static void rgblcd_draw_rectangle(uint16_t x,uint16_t y,uint16_t x2,uint16_t y2,uint16_t c)
{(void)x;(void)y;(void)x2;(void)y2;(void)c;}
static void rgblcd_show_string(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint8_t size,char *s,uint16_t c)
{(void)x;(void)y;(void)w;(void)h;(void)size;if(!strcmp(s,"Yes"))yes_color=c;}
'''
    cases = r'''
static void setup(int ready)
{
    memset(g_slots,0,sizeof(g_slots));g_slots[0].ready=(uint8_t)ready;
    strcpy(g_slots[0].desc,"camera");
    rgblcddev.width=480;rgblcddev.height=800;rgblcddev.pwidth=800;rgblcddev.pheight=480;
    g_selected=-1;g_page=PAGE_SELECT;g_page_drawn=PAGE_COUNT;g_do_switch=0;
    g_ota_receiving=0;g_switch_error=0;layout_init();slot_refresh_items();ui_dispatch();
}
static void yes(void) {ui_touch_process(B_X+B_W/2,B_Y+B_H/2);}
int main(void)
{
    setup(1);
    assert(g_selected==0 && yes_color==BLACK);
    assert(B_Y+B_H<=rgblcddev.height);
    yes();assert(g_page==PAGE_CONFIRM && !g_do_switch);
    ui_dispatch();yes();assert(g_do_switch);
    puts("PASS ready image is selected and two deliberate Yes taps confirm installation");

    setup(0);assert(g_selected==-1 && yes_color==LGRAY);
    yes();assert(g_page==PAGE_SELECT && !g_do_switch);
    ui_touch_process(L_X+10,L_Y+10);assert(g_selected==-1);
    puts("PASS empty image disables Yes and cannot be selected");

    setup(1);g_selected=-1;yes_color=LGRAY;
    ui_touch_process(L_X+10,L_Y+10);assert(g_selected==0 && yes_color==BLACK);
    yes();ui_dispatch();
    ui_touch_process(L_X+10,HINT_Y-30);
    assert(g_page==PAGE_SELECT && !g_do_switch);
    puts("PASS selecting an image redraws Yes and Back cancels confirmation");

    setup(1);g_ota_receiving=1;yes();assert(g_page==PAGE_SELECT && !g_do_switch);
    g_ota_receiving=0;yes();assert(g_page==PAGE_CONFIRM);
    g_ota_receiving=1;yes();assert(!g_do_switch);
    g_ota_receiving=0;
    ui_touch_process(rgblcddev.width,rgblcddev.height);assert(!g_do_switch);
    puts("PASS receiving and out-of-screen touches cannot start installation");
    return 0;
}
'''
    fixture = folder / "model_select_host.c"
    fixture.write_text(preamble + source + cases, encoding="utf-8")
    compiler = shutil.which("gcc")
    if not compiler:
        raise RuntimeError("Host GCC is required")
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment.get("PATH", "")
    binary = folder / "model_select_host.exe"
    subprocess.run([compiler, "-std=c11", "-O0", "-I" + str(ROOT / "Appli/Core/Inc"),
                    str(fixture), "-o", str(binary)], env=environment, check=True)
    subprocess.run([str(binary)], env=environment, check=True, timeout=10)


if __name__ == "__main__":
    main()
