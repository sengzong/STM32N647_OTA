/* SD resources are optional and loaded once, before LVGL creates the screen. */
#include "ota_assets.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
#include "ota_font_fallback.inc"

static FATFS filesystem;
static OtaTheme theme={0x0b1420,0x182536,0x39d598,0xf4f8fc,0x8da5bd};
static OtaAssetFont builtin,sd_font;
static uint8_t resource_cache[256u*1024u] __attribute__((section(".OTA_ASSETS"),aligned(32)));
static uint8_t initialized,sd_loaded;

static void read_theme(void)
{
    FIL file;UINT count;char data[257];
    if(f_open(&file,"0:/ota_ui/theme.cfg",FA_READ)!=FR_OK) return;
    if(f_size(&file)>sizeof(data)-1u || f_read(&file,data,sizeof(data)-1u,&count)!=FR_OK) {
        f_close(&file);return;
    }
    f_close(&file);data[count]=0;
    char *line=data;
    while(*line) {
        char *end=strchr(line,'\n');if(end) *end=0;
        char *equal=strchr(line,'=');
        if(equal) {
            *equal++=0;uint32_t value=0u;unsigned digits=0u;
            while(equal[digits] && equal[digits]!='\r' && digits<6u) {
                unsigned char c=(unsigned char)equal[digits];unsigned n;
                if(c>='0' && c<='9') n=c-'0';
                else if(c>='a' && c<='f') n=c-'a'+10u;
                else if(c>='A' && c<='F') n=c-'A'+10u;
                else break;
                value=(value<<4)|n;++digits;
            }
            if(digits==6u && (equal[digits]==0 || equal[digits]=='\r')) {
                if(!strcmp(line,"background")) theme.background=value;
                else if(!strcmp(line,"panel")) theme.panel=value;
                else if(!strcmp(line,"accent")) theme.accent=value;
                else if(!strcmp(line,"text")) theme.text=value;
                else if(!strcmp(line,"muted")) theme.muted=value;
            }
        }
        if(!end) break;
        line=end+1;
    }
}

void ota_assets_init(void)
{
    FIL file;UINT received;uint32_t length,done=0u;
    if(initialized) return;
    initialized=1u;
    (void)ota_asset_font_bind(&builtin,fallback_font_data,sizeof(fallback_font_data));
    if(f_mount(&filesystem,"0:",1u)!=FR_OK) {
        printf("UI assets: built-in fallback (SD unavailable)\r\n");return;
    }
    read_theme();
    if(f_open(&file,"0:/ota_ui/cjk16.bin",FA_READ)!=FR_OK) {
        printf("UI assets: built-in fallback (font file missing)\r\n");return;
    }
    length=(uint32_t)f_size(&file);
    if(length>sizeof(resource_cache) || length<32u) { f_close(&file);return; }
    while(done<length) {
        UINT chunk=(length-done)>2048u ? 2048u : length-done;
        if(f_read(&file,resource_cache+done,chunk,&received)!=FR_OK || received!=chunk) break;
        done+=received;
    }
    f_close(&file);
    if(done==length && ota_asset_font_bind(&sd_font,resource_cache,length)==0) sd_loaded=1u;
    printf("UI assets: %s (%lu bytes)\r\n",sd_loaded?"SD font cached":"built-in fallback, invalid font",
           (unsigned long)done);
}

const lv_font_t *ota_assets_font(void) {return sd_loaded?&sd_font.font:&builtin.font;}
const OtaTheme *ota_assets_theme(void) {return &theme;}
int ota_assets_sd_loaded(void) {return sd_loaded;}
