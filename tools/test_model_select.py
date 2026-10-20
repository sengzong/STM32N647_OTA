"""Exercise production management pages with real LVGL pointer input and rendering."""
from pathlib import Path
import subprocess
from test_ota_screen import build_host, save_previews
from test_ota_assets import fat16_image

ROOT = Path(__file__).resolve().parents[1]


def main():
    folder = ROOT / "Build/ui-tests"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "rgblcd.h").write_text(r'''
#ifndef TEST_LCD_H
#define TEST_LCD_H
#include <stdint.h>
typedef struct {uint32_t pwidth,pheight;uint16_t width,height,id;uint8_t dir;} TestLcd;
extern TestLcd rgblcddev;
extern uint16_t *g_ltdc_lcd_framebuf;
uint32_t HAL_GetTick(void);
void SCB_CleanDCache(void);
#define GPIOG 0
#define GPIOE 1
#define GPIO_PIN_10 1024
#define GPIO_PIN_SET 1
static inline void HAL_GPIO_WritePin(int a,int b,int c) {(void)a;(void)b;(void)c;}
#endif
''', encoding="ascii")
    (folder / "touch.h").write_text(r'''
#include <stdint.h>
#define TP_PRES_DOWN 0x8000u
extern struct TestTouch {uint8_t (*init)(void);uint8_t (*scan)(uint8_t);
    uint16_t x[10],y[10],sta;uint8_t touchtype;} tp_dev;
''', encoding="ascii")
    (folder / "tx_api.h").write_text(
        "static inline void tx_thread_sleep(unsigned n) {(void)n;}\n", encoding="ascii")
    source = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "ota_status.h"
#include "SCREEN_SOURCE"
#include "MANAGER_SOURCE"

TestLcd rgblcddev = {800,480,480,800,0x4342,0};
struct TestTouch tp_dev;
static uint16_t frame[480*800+2];
uint16_t *g_ltdc_lcd_framebuf = frame+1;
static uint32_t now, install_calls;
static FILE *card_image;
OtaSlotView g_slots[OTA_SLOT_COUNT];
volatile uint8_t g_ota_receiving;
volatile uint32_t g_ota_progress;
uint32_t HAL_GetTick(void) {return now;}
void SCB_CleanDCache(void) {}
void ota_slot_refresh(void) {}
void ota_confirm_poll(void) {}
int ota_switch_slot(uint32_t slot) {
    assert(slot==0 && !g_ota_receiving && g_slots[0].ready);
    install_calls++;return -5;
}
DSTATUS disk_initialize(BYTE n) {return n||!card_image?STA_NOINIT:0;}
DSTATUS disk_status(BYTE n) {return disk_initialize(n);}
DRESULT disk_read(BYTE n,BYTE *data,LBA_t sector,UINT count) {
    if(n||!card_image)return RES_NOTRDY;
    if(fseek(card_image,(long)sector*512,SEEK_SET)||fread(data,512,count,card_image)!=count)return RES_ERROR;
    return RES_OK;
}
DRESULT disk_ioctl(BYTE n,BYTE cmd,void *data) {(void)n;(void)cmd;(void)data;return RES_PARERR;}
static void advance(unsigned ticks, int press, unsigned x, unsigned y) {
    for(unsigned i=0;i<ticks;i++){now+=10;manager_step(press,x,y);}
    assert(frame[0]==0x1234 && frame[480*800+1]==0x5678);
}
static void release(void) {advance(10,0,240,584);}
static void tap(lv_obj_t *button) {
    lv_area_t a;lv_obj_get_coords(button,&a);
    unsigned x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;
    advance(10,1,x,y);advance(10,0,x,y);
}
static void ready(int valid) {
    release();
    g_slots[0].ready=valid;
    g_slots[0].img_len=valid?4065660:0;
    strcpy(g_slots[0].desc,"991 AI People Detection");
    g_page=PAGE_SELECT;g_switch_error=0;g_do_switch=0;install_calls=0;
    manager_refresh_slots();manager_render();advance(10,0,0,0);
}
static void render(const char *path) {
    lv_refr_now(NULL);
    FILE *out=fopen(path,"wb");assert(out);
    for(unsigned y=0;y<800;y++)for(unsigned x=0;x<480;x++){
        uint16_t pixel=g_ltdc_lcd_framebuf[(479-x)*800+y];
        assert(fwrite(&pixel,2,1,out)==1);
    }
    fclose(out);
}
static void inside(lv_obj_t *obj) {
    lv_area_t a;lv_obj_get_coords(obj,&a);
    assert(a.x1>=0 && a.y1>=0 && a.x2<480 && a.y2<800);
}
int main(int argc,char **argv) {
    if(argc>1){card_image=fopen(argv[1],"rb");assert(card_image);}
    frame[0]=0x1234;frame[480*800+1]=0x5678;
    g_slots[0].ready=1;g_slots[0].img_len=4065660;
    strcpy(g_slots[0].desc,"991 AI People Detection");
    manager_init();
    advance(50,1,240,584);release();
    assert(!install_calls && g_page==PAGE_SELECT);
    render("ready.rgb565");
    assert(lv_scr_act()==manager_screen && manager_ready());
    advance(150,1,240,584);
    assert(g_page==PAGE_SELECT && !install_calls);
    release();assert(g_page==PAGE_CONFIRM && !install_calls);
    advance(150,0,240,584);assert(!install_calls);
    render("confirm.rgb565");
    tap(manager_action);
    assert(install_calls==1 && g_page==PAGE_SELECT && g_switch_error==-5);
    assert(strstr(lv_label_get_text(manager_hint),"-5"));
    render("error.rgb565");
    puts("PASS real LVGL press/release, boot-held and long-held fingers, two confirmations, error recovery");

    ready(1);tap(manager_action);assert(g_page==PAGE_CONFIRM);
    tap(manager_back);assert(g_page==PAGE_SELECT && !install_calls);
    ready(0);assert(g_selected==-1 && lv_obj_has_state(manager_action,LV_STATE_DISABLED));
    render("empty.rgb565");tap(manager_action);
    assert(g_page==PAGE_SELECT && !install_calls);
    puts("PASS Back cancels and empty firmware disables the native LVGL install button");

    ready(1);tap(manager_action);
    advance(10,1,240,584); /* A transfer arrives while the confirmation button is held. */
    g_ota_receiving=1;ota_status_begin();advance(20,1,240,584);
    assert(lv_scr_act()==progress_screen && !install_calls && !g_do_switch);
    const char *meta="OTA-META1\n1\n991 People Detection\n1.2.0\n991";
    assert(!ota_status_package((const uint8_t *)meta,strlen(meta),2));
    ota_status_progress(OTA_UI_RECEIVING,1504295,4065660);
    advance(20,1,240,584);render("receiving.rgb565");
    strcpy(g_slots[0].desc,"27 Touch");
    g_ota_receiving=0;advance(20,1,240,584);release();
    assert(lv_scr_act()==manager_screen && g_page==PAGE_SELECT && !install_calls);
    assert(!strcmp(lv_label_get_text(manager_name),"27 Touch"));
    tap(manager_action);assert(g_page==PAGE_CONFIRM && !install_calls);
    puts("PASS transfer cancels held confirmation, uses progress screen, refreshes the name and requires a fresh tap");

    ready(1);advance(10,1,240,584);advance(10,1,65535,65535);release();
    assert(g_page==PAGE_SELECT && !install_calls);
    advance(10,1,480,800);advance(10,1,240,584);release();
    assert(g_page==PAGE_SELECT && !install_calls);
    tap(manager_action);assert(g_page==PAGE_CONFIRM);
    advance(10,1,240,584);advance(10,1,10,500);advance(10,0,10,500);
    assert(g_page==PAGE_CONFIRM && !install_calls);
    tap(manager_back);
    puts("PASS invalid and dragged-out touch input cannot confirm installation");

    lv_mem_monitor_t before,after;
    lv_mem_monitor(&before);
    for(unsigned i=0;i<100;i++){
        tap(manager_action);tap(manager_back);
        g_ota_receiving=1;advance(10,0,0,0);
        g_ota_receiving=0;advance(10,0,0,0);
    }
    lv_mem_monitor(&after);
    assert(after.free_size>16384 && after.free_size+128>=before.free_size && lv_mem_test()==LV_RES_OK);
    inside(manager_title);inside(manager_name);inside(manager_hint);
    inside(manager_action);inside(manager_back);inside(manager_note);
    printf("PASS 100 screen round trips without heap growth (%u bytes free)\n",after.free_size);
    g_page=PAGE_SWITCHING;manager_render();render("switching.rgb565");
    if(card_image)fclose(card_image);
    return 0;
}
'''.replace("SCREEN_SOURCE", (ROOT / "Appli/Core/Src/ota_screen.c").as_posix()
).replace("MANAGER_SOURCE", (ROOT / "Appli/Core/Src/model_select.c").as_posix())
    fixture = folder / "model_select_host.c"
    fixture.write_text(source, encoding="utf-8")
    binary, environment = build_host(folder, fixture)
    image = folder / "card.img"
    image.write_bytes(fat16_image((ROOT/"SD_Card/ota_ui/cjk16.bin").read_bytes(),
                                 (ROOT/"SD_Card/ota_ui/theme.cfg").read_bytes()))
    for mode, args in (("sd", [str(image)]), ("fallback", [])):
        output = folder / mode
        output.mkdir(exist_ok=True)
        result = subprocess.run([str(binary)]+args, cwd=output, env=environment,
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        print(mode + ":")
        print("\n".join(line for line in result.stdout.splitlines()
                        if line.startswith(("PASS", "UI assets:"))))
    save_previews(folder)
    print("Management previews:", folder)


if __name__ == "__main__":
    main()
