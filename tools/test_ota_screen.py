"""Render the production screen with real LVGL and a guarded host framebuffer."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def build_host(folder, fixture):
    compiler = shutil.which("gcc")
    if not compiler:
        raise RuntimeError("Host GCC is required")
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment.get("PATH", "")
    flags = ["-std=c11", "-O1", "-DLV_CONF_INCLUDE_SIMPLE", "-ffunction-sections", "-fdata-sections"]
    for path in (folder, ROOT / "Appli/Core/Inc", ROOT / "Middlewares/LVGL", ROOT/"Middlewares/FATFS"):
        flags += ["-I" + str(path)]
    sources = sorted((ROOT / "Middlewares/LVGL/src").rglob("*.c"))
    sources += [fixture]+[ROOT/"Appli/Core/Src"/n for n in ("ota_status.c","ota_assets.c","ota_asset_font.c")]
    sources += [ROOT/"Middlewares/FATFS"/n for n in ("ff.c","ffunicode.c")]
    def compile_one(path):
        tag = hashlib.sha256(str(path).encode()).hexdigest()[:12]
        obj = folder / (path.stem+"_"+tag+".o")
        result = subprocess.run([compiler]+flags+["-c",str(path),"-o",str(obj)],
                                capture_output=True,text=True,env=environment)
        if result.returncode: raise RuntimeError(str(path)+"\n"+result.stdout+result.stderr)
        return obj
    with ThreadPoolExecutor(max_workers=8) as pool: objects=list(pool.map(compile_one,sources))
    response = folder / "objects.list"
    response.write_text("\n".join('"'+p.as_posix()+'"' for p in objects),encoding="utf-8")
    binary = folder / "screen_host.exe"
    subprocess.run([compiler,"@"+str(response),"-Wl,--gc-sections","-o",str(binary)],env=environment,check=True)
    return binary, environment


def save_previews(folder):
    from PIL import Image
    for path in folder.rglob("*.rgb565"):
        data=path.read_bytes();rgb=bytearray()
        for i in range(0,len(data),2):
            value=data[i]|data[i+1]<<8
            rgb.extend((((value>>11)&31)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
        Image.frombytes("RGB",(480,800),bytes(rgb)).save(path.with_suffix(".png"))


def main():
    folder = ROOT / "Build/ota-screen-tests"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "rgblcd.h").write_text(r'''
#include <stdint.h>
typedef struct { uint32_t pwidth,pheight; uint16_t width,height; uint8_t dir; } TestLcd;
extern TestLcd rgblcddev;
extern uint16_t *g_ltdc_lcd_framebuf;
uint32_t HAL_GetTick(void);
void SCB_CleanDCache(void);
''', encoding="ascii")
    source = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "ota_status.h"
#include "ota_display_blit.h"
#include "ff.h"
#include "diskio.h"
#include "SCREEN_SOURCE"
static FILE *card_image;
DSTATUS disk_initialize(BYTE drive) {return drive || !card_image ? STA_NOINIT:0;}
DSTATUS disk_status(BYTE drive) {return disk_initialize(drive);}
DRESULT disk_read(BYTE drive,BYTE *data,LBA_t sector,UINT count) {
    if(drive || !card_image) return RES_NOTRDY;
    if(fseek(card_image,(long)sector*512,SEEK_SET) || fread(data,512,count,card_image)!=count) return RES_ERROR;
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *data) {(void)drive;(void)command;(void)data;return RES_PARERR;}
TestLcd rgblcddev = {800,480,480,800,0};
static uint16_t guarded[800*480+2];
uint16_t *g_ltdc_lcd_framebuf = guarded+1;
static uint32_t now;
uint32_t HAL_GetTick(void) {return now;}
void SCB_CleanDCache(void) {}
static void render(const char *file) {
    now+=50; ota_screen_poll(); lv_refr_now(NULL);
    assert(guarded[0]==0x1234 && guarded[800*480+1]==0x5678);
    FILE *out=fopen(file,"wb"); assert(out);
    for (unsigned y=0;y<800;y++) for (unsigned x=0;x<480;x++) {
        uint16_t value=g_ltdc_lcd_framebuf[(479-x)*800+y];
        assert(fwrite(&value,2,1,out)==1);
    }
    fclose(out);
}
static void label_inside(lv_obj_t *obj, lv_obj_t *parent) {
    lv_area_t child, card;
    lv_obj_get_coords(obj,&child);lv_obj_get_coords(parent,&card);
    assert(child.x1>=card.x1 && child.x2<=card.x2 && child.y1>=card.y1 && child.y2<=card.y2);
}
int main(int argc,char **argv) {
    if(argc>1) {card_image=fopen(argv[1],"rb");assert(card_image);}
    guarded[0]=0x1234;guarded[800*480+1]=0x5678;
    ota_screen_init(); ota_screen_show(); render("waiting.rgb565");
    /* The literal used for an empty version must have actual CJK glyphs. */
    for (unsigned i=0;i<3;i++) {
        const uint32_t chars[]={0x672a,0x63d0,0x4f9b};lv_font_glyph_dsc_t glyph;
        assert(lv_font_get_glyph_dsc(&text_font,&glyph,chars[i],0) && !glyph.is_placeholder);
    }
    const char *meta="OTA-META1\n1\n99_Applications__991_AI_People_Detection\n1.2.0\n991";
    ota_status_begin();assert(ota_status_package((const uint8_t *)meta,strlen(meta),2)==0);
    ota_status_progress(OTA_UI_RECEIVING,1504295,4065660);render("receiving.rgb565");
    assert(!strcmp(lv_label_get_text(clock_value),"800 MHz"));
    assert(!strcmp(lv_label_get_text(version_value),"1.2.0"));
    assert(!strcmp(lv_label_get_text(model_value),"Included"));
    assert(lv_bar_get_value(progress_bar)==370);
    ota_status_progress(OTA_UI_RECEIVING,4065660,4065660);
    ota_status_progress(OTA_UI_WRITING,2520710,4065660);render("writing.rgb565");
    assert(!strcmp(lv_label_get_text(phase_value),"Writing flash"));
    assert(lv_bar_get_value(progress_bar)==620);
    ota_status_phase(OTA_UI_ERROR,-15);render("failed.rgb565");
    assert(strstr(lv_label_get_text(footer),"-15"));
    ota_status_begin();
    const char *ordinary="OTA-META1\n0\n27 Touch\n\nTouch";
    assert(ota_status_package((const uint8_t *)ordinary,strlen(ordinary),1)==0);
    ota_status_progress(OTA_UI_RECEIVING,100,100);render("ordinary.rgb565");
    assert(!strcmp(lv_label_get_text(version_value),"\xe6\x9c\xaa\xe6\x8f\x90\xe4\xbe\x9b"));
    assert(!strcmp(lv_label_get_text(clock_value),"600 MHz"));
    assert(!strcmp(lv_label_get_text(model_value),"None"));
    for (unsigned i=0;i<500;i++) {ota_status_progress(OTA_UI_WRITING,i,500);now+=50;ota_screen_poll();}
    lv_mem_monitor_t memory;lv_mem_monitor(&memory);
    assert(memory.free_size>16384 && lv_mem_test()==LV_RES_OK);
    label_inside(name_value,lv_obj_get_parent(name_value));
    label_inside(version_value,lv_obj_get_parent(version_value));
    label_inside(clock_value,lv_obj_get_parent(clock_value));
    label_inside(model_value,lv_obj_get_parent(model_value));
    label_inside(phase_value,lv_obj_get_parent(phase_value));
    label_inside(bytes_value,lv_obj_get_parent(bytes_value));
    /* Rotation, clipping and input stride are checked with unique pixels. */
    uint16_t frame[14], tile[]={1,2,3,4,5,6};
    memset(frame,0,sizeof(frame));frame[0]=0x1234;frame[13]=0x5678;
    ota_display_blit(frame+1,4,3,0,-1,0,1,1,tile);
    assert(frame[1+8]==2 && frame[1+4]==3 && frame[1+9]==5 && frame[1+5]==6);
    assert(frame[0]==0x1234 && frame[13]==0x5678);
    memset(frame+1,0,24);ota_display_blit(frame+1,4,3,1,2,1,4,2,tile);
    assert(frame[1+6]==1 && frame[1+7]==2 && frame[1+10]==4 && frame[1+11]==5);
    assert(frame[0]==0x1234 && frame[13]==0x5678);
    printf("PASS real LVGL rendering, CJK, target clocks/model/version, clipping and heap (%u bytes free)\n",memory.free_size);
    return 0;
}
'''.replace("SCREEN_SOURCE", (ROOT / "Appli/Core/Src/ota_screen.c").as_posix())
    fixture = folder / "screen_host.c"
    fixture.write_text(source, encoding="utf-8")
    binary, environment = build_host(folder, fixture)
    from test_ota_assets import fat16_image
    card=folder/"card.img"
    card.write_bytes(fat16_image((ROOT/"SD_Card/ota_ui/cjk16.bin").read_bytes(),
                                (ROOT/"SD_Card/ota_ui/theme.cfg").read_bytes()))
    for mode,args in (("sd",[str(card)]),("fallback",[])):
        output=folder/mode;output.mkdir(exist_ok=True)
        subprocess.run([str(binary)]+args,cwd=output,env=environment,check=True,timeout=30)
    save_previews(folder)
    print("Screen previews:",folder)


if __name__ == "__main__": main()
