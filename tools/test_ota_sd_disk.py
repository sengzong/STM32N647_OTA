"""Exercise the production SD port with HAL failures and unaligned callers."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def main():
    folder=ROOT/"Build/ota-sd-tests";folder.mkdir(parents=True,exist_ok=True)
    (folder/"main.h").write_text(r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef struct {uint32_t ClockEdge,ClockPowerSave,BusWide,HardwareFlowControl,ClockDiv;} SD_Init;
typedef struct {uint32_t Instance;SD_Init Init;} SD_HandleTypeDef;
typedef struct {uint32_t LogBlockNbr;} HAL_SD_CardInfoTypeDef;
typedef struct {uint32_t PeriphClockSelection,Sdmmc1ClockSelection;} RCC_PeriphCLKInitTypeDef;
typedef struct {uint32_t Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
typedef struct {uint32_t MasterCID,SecPriv;} RIMC_MasterConfig_t;
enum {RIF_CID_1=1,RIF_ATTRIBUTE_SEC=2,RIF_ATTRIBUTE_PRIV=4,RIF_MASTER_INDEX_SDMMC1=1,RIF_RISC_PERIPH_INDEX_SDMMC1=1,
GPIO_PIN_2=4,GPIO_PIN_8=256,GPIO_PIN_9=512,GPIO_PIN_10=1024,GPIO_PIN_11=2048,GPIO_PIN_12=4096,
GPIO_PIN_SEC=1,GPIO_PIN_NPRIV=2,RCC_PERIPHCLK_SDMMC1=1,RCC_SDMMC1CLKSOURCE_HCLK=2,GPIOC=1,GPIOH=2,
GPIO_MODE_AF_PP=1,GPIO_NOPULL=0,GPIO_SPEED_FREQ_VERY_HIGH=3,GPIO_AF10_SDMMC1=10,SDMMC1=1,
SDMMC_CLOCK_EDGE_RISING=0,SDMMC_CLOCK_POWER_SAVE_DISABLE=0,SDMMC_BUS_WIDE_4B=4,
SDMMC_HARDWARE_FLOW_CONTROL_ENABLE=1,HAL_OK=0,HAL_SD_CARD_TRANSFER=4};
#define __HAL_RCC_RIFSC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOH_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SDMMC1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SDMMC1_FORCE_RESET() ((void)0)
#define __HAL_RCC_SDMMC1_RELEASE_RESET() ((void)0)
static unsigned tick,reads,initializations,init_error,read_error,card_busy,clock_error;
static unsigned pc_pins,ph_pins;
static uint32_t HAL_GetTick(void) {return tick++;}
static void HAL_RIF_RIMC_ConfigMasterAttributes(unsigned m,RIMC_MasterConfig_t *c) {(void)m;assert(c->MasterCID==1 && c->SecPriv==6);}
static void HAL_RIF_RISC_SetSlaveSecureAttributes(unsigned p,unsigned a) {(void)p;assert(a==6);}
static void HAL_GPIO_ConfigPinAttributes(unsigned p,unsigned pins,unsigned a) {(void)p;(void)pins;assert(a==3);}
static unsigned HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c) {assert(c->Sdmmc1ClockSelection==RCC_SDMMC1CLKSOURCE_HCLK);return clock_error;}
static void HAL_GPIO_Init(unsigned p,GPIO_InitTypeDef *c) {assert(c->Alternate==10);if(p==GPIOC)pc_pins|=c->Pin;else ph_pins|=c->Pin;}
static unsigned HAL_SD_Init(SD_HandleTypeDef *s) {initializations++;assert(s->Init.BusWide==4 && s->Init.ClockDiv==4);return init_error;}
static unsigned HAL_SD_GetCardInfo(SD_HandleTypeDef *s,HAL_SD_CardInfoTypeDef *i) {(void)s;i->LogBlockNbr=100;return 0;}
static unsigned HAL_SD_ReadBlocks(SD_HandleTypeDef *s,uint8_t *out,unsigned sector,unsigned count,unsigned timeout) {
 (void)s;assert(!((uintptr_t)out&31u) && count==1 && timeout==100);reads++;
 for(unsigned i=0;i<512;i++)out[i]=(uint8_t)(sector+i);return read_error;
}
static unsigned HAL_SD_GetCardState(SD_HandleTypeDef *s) {(void)s;return card_busy?0:4;}
''',encoding="ascii")
    source=r'''
#include <stdio.h>
#include <string.h>
#include "PORT_SOURCE"
int main(void) {
 uint8_t output[1026];memset(output,0xaa,sizeof(output));
 assert(disk_initialize(1)==STA_NOINIT && !initializations);
 init_error=1;assert(disk_initialize(0)==STA_NOINIT);
 init_error=0;assert(disk_initialize(0)==0 && initializations==2);
 assert(pc_pins==(GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12) && ph_pins==GPIO_PIN_2);
 assert(disk_read(0,output+1,7,2)==RES_OK && reads==2);
 for(unsigned i=0;i<1024;i++)assert(output[i+1]==(uint8_t)(7+i/512+i%512));
 assert(output[0]==0xaa && output[1025]==0xaa);
 unsigned before=reads;assert(disk_read(0,output,99,2)==RES_PARERR && reads==before);
 read_error=1;assert(disk_read(0,output,0,1)==RES_ERROR && disk_status(0)==STA_NOINIT);
 read_error=0;assert(disk_initialize(0)==0);card_busy=1;before=tick;
 assert(disk_read(0,output,0,1)==RES_ERROR && tick-before<=105u);
 card_busy=0;assert(disk_initialize(0)==0);tick+=2001;
 assert(disk_read(0,output,0,1)==RES_ERROR);
 assert(disk_initialize(0)==0);
 DWORD value=0;assert(disk_ioctl(0,GET_BLOCK_SIZE,&value)==RES_OK && value==1);
 assert(disk_ioctl(0,GET_SECTOR_COUNT,&value)==RES_OK && value==100);
 puts("PASS actual SDMMC port: reference pins, aligned bounce, read errors, bounded timeouts and no write path");
 return 0;
}
'''.replace("PORT_SOURCE",(ROOT/"Appli/Core/Src/ota_sd_disk.c").as_posix())
    fixture=folder/"sd_host.c";fixture.write_text(source,encoding="ascii")
    compiler=shutil.which("gcc");assert compiler
    environment=dict(os.environ);environment["PATH"]=str(Path(compiler).parent)+os.pathsep+environment.get("PATH","")
    binary=folder/"sd_host.exe"
    subprocess.run([compiler,"-std=c11","-O0","-I"+str(folder),"-I"+str(ROOT/"Middlewares/FATFS"),str(fixture),"-o",str(binary)],env=environment,check=True)
    subprocess.run([str(binary)],env=environment,check=True,timeout=10)

if __name__=="__main__":main()
