"""Real FatFs/LVGL tests for SD resource loading and safe fallback."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def fat16_image(font,theme):
    image=bytearray(16384*512)
    boot=memoryview(image)[:512]
    boot[:11]=b"\xeb\x3c\x90MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII",boot,11,512,1,1,2,512,16384,0xf8,64,32,64,0,0)
    boot[36]=0x80;boot[38]=0x29;boot[43:54]=b"OTA ASSETS ";boot[54:62]=b"FAT16   "
    boot[510:512]=b"\x55\xaa"
    fat=bytearray(64*512);struct.pack_into("<HHH",fat,0,0xfff8,0xffff,0xffff)
    first_data=161
    def entry(name,attribute,cluster,size):
        data=bytearray(32);data[:11]=name;data[11]=attribute
        struct.pack_into("<H",data,26,cluster);struct.pack_into("<I",data,28,size)
        return data
    image[129*512:129*512+32]=entry(b"OTA_UI     ",0x10,2,0)
    directory=first_data*512
    image[directory:directory+32]=entry(b".          ",0x10,2,0)
    image[directory+32:directory+64]=entry(b"..         ",0x10,0,0)
    next_cluster=3
    for index,(name,data) in enumerate(((b"CJK16   BIN",font),(b"THEME   CFG",theme))):
        first=next_cluster;count=(len(data)+511)//512
        for cluster in range(first,first+count):
            struct.pack_into("<H",fat,cluster*2,cluster+1 if cluster<first+count-1 else 0xffff)
        start=(first_data+first-2)*512;image[start:start+len(data)]=data
        image[directory+(index+2)*32:directory+(index+3)*32]=entry(name,0x20,first,len(data))
        next_cluster+=count
    image[512:65*512]=fat;image[65*512:129*512]=fat
    return bytes(image)

def main():
    folder=ROOT/"Build/ota-assets-tests";folder.mkdir(parents=True,exist_ok=True)
    fixture=folder/"assets_host.c"
    source=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "ota_assets.h"
#include "ff.h"
#include "diskio.h"
#undef LV_FONT_SIMSUN_16_CJK
#define LV_FONT_SIMSUN_16_CJK 1
#define lv_font_simsun_16_cjk reference_cjk
#include "FONT_SOURCE"
static FILE *card;
static unsigned reads;
uint32_t ota_lvgl_time_ms(void) {return 0;}
DSTATUS disk_initialize(BYTE drive) {return drive || !card ? STA_NOINIT : 0;}
DSTATUS disk_status(BYTE drive) {return disk_initialize(drive);}
DRESULT disk_read(BYTE drive,BYTE *data,LBA_t sector,UINT count) {
    if(drive || !card) return RES_NOTRDY;
    reads++;
    if(fseek(card,(long)sector*512,SEEK_SET) || fread(data,512,count,card)!=count) return RES_ERROR;
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *data) {(void)drive;(void)command;(void)data;return RES_PARERR;}
int main(int argc,char **argv) {
    assert(argc==3);int valid=!strcmp(argv[1],"valid");
    if(strcmp(argv[1],"missing")) {card=fopen(argv[2],"rb");assert(card);}
    ota_assets_init();assert(ota_assets_sd_loaded()==valid);
    const lv_font_t *font=ota_assets_font();
    const uint32_t required[]={0x672a,0x63d0,0x4f9b};
    for(unsigned i=0;i<3;i++) {lv_font_glyph_dsc_t d;assert(font->get_glyph_dsc(font,&d,required[i],0));}
    if(valid) {
        assert(ota_assets_theme()->accent==0xff6600u);
        unsigned compared=0;
        for(uint32_t code=32;code<=65535;code++) {
            lv_font_glyph_dsc_t a={0},b={0};
            bool expected=reference_cjk.get_glyph_dsc(&reference_cjk,&a,code,0);
            bool actual=font->get_glyph_dsc(font,&b,code,0);
            if(expected && !actual) {
                /* LVGL 8.4's reader also accepts one-past cmap endpoints.
                 * The exported file retains only the declared character ranges. */
                const lv_font_fmt_txt_dsc_t *d=reference_cjk.dsc;bool boundary=false;
                for(unsigned c=0;c<d->cmap_num;c++)
                    if(code==d->cmaps[c].range_start+d->cmaps[c].range_length) boundary=true;
                assert(boundary);continue;
            }
            assert(expected==actual);
            if(expected) {
                assert(a.adv_w==b.adv_w && a.box_w==b.box_w && a.box_h==b.box_h && a.ofs_x==b.ofs_x && a.ofs_y==b.ofs_y);
                assert(!memcmp(reference_cjk.get_glyph_bitmap(&reference_cjk,code),font->get_glyph_bitmap(font,code),(a.box_w*a.box_h+1u)/2u));
                compared++;
            }
        }
        printf("PASS %u SD glyphs match original LVGL font bitmaps and metrics\n",compared);
    }
    /* Once cached, card removal cannot cause rendering to read sectors. */
    if(card) {fclose(card);card=NULL;}
    unsigned before=reads;
    for(unsigned repeat=0;repeat<1000;repeat++) {
        lv_font_glyph_dsc_t d;assert(font->get_glyph_dsc(font,&d,required[repeat%3],0));
        assert(font->get_glyph_bitmap(font,required[repeat%3]));
    }
    assert(reads==before);
    printf("PASS %s card path, built-in fallback and cached rendering after removal\n",argv[1]);
    return 0;
}
'''.replace("FONT_SOURCE",(ROOT/"Middlewares/LVGL/src/font/lv_font_simsun_16_cjk.c").as_posix())
    fixture.write_text(source,encoding="utf-8")
    compiler=shutil.which("gcc");assert compiler
    environment=dict(os.environ);environment["PATH"]=str(Path(compiler).parent)+os.pathsep+environment.get("PATH","")
    includes=["-I"+str(ROOT/p) for p in ("Appli/Core/Inc","Middlewares/LVGL","Middlewares/FATFS")]
    sources=sorted((ROOT/"Middlewares/LVGL/src").rglob("*.c"))
    sources += [fixture]+[ROOT/"Appli/Core/Src"/n for n in ("ota_assets.c","ota_asset_font.c")]
    sources += [ROOT/"Middlewares/FATFS"/n for n in ("ff.c","ffunicode.c")]
    def compile_one(path):
        obj=folder/(path.stem+"_"+hashlib.sha256(str(path).encode()).hexdigest()[:12]+".o")
        result=subprocess.run([compiler,"-O1","-std=c11","-DLV_CONF_INCLUDE_SIMPLE","-ffunction-sections","-fdata-sections"]+includes+["-c",str(path),"-o",str(obj)],capture_output=True,text=True,env=environment)
        if result.returncode:raise RuntimeError(str(path)+"\n"+result.stdout+result.stderr)
        return obj
    with ThreadPoolExecutor(max_workers=8) as pool:objects=list(pool.map(compile_one,sources))
    response=folder/"objects.list";response.write_text("\n".join('"'+p.as_posix()+'"' for p in objects),encoding="utf-8")
    binary=folder/"assets_host.exe"
    subprocess.run([compiler,"@"+str(response),"-Wl,--gc-sections","-o",str(binary)],env=environment,check=True)
    font=(ROOT/"SD_Card/ota_ui/cjk16.bin").read_bytes()
    valid=fat16_image(font,b"accent=FF6600\n")
    corrupt=bytearray(font);corrupt[-1]^=1
    images={"valid":valid,"missing":b"","corrupt":fat16_image(corrupt,b"bad-theme\n"),
            "truncated":fat16_image(font[:50],b"accent=BAD!\n"),"bad_volume":bytes(16384*512)}
    for name,data in images.items():
        path=folder/(name+".img");path.write_bytes(data)
        subprocess.run([str(binary),name,str(path)],check=True,env=environment,timeout=15)
    print("PASS real FatFs read-only FAT16 traversal, resource CRC, truncation and absent-volume fallback")

if __name__=="__main__":main()
