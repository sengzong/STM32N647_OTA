/* Read-only SDMMC1 port based on the ATK 38_SD_Card pin/clock configuration. */
#include "main.h"
#include "ff.h"
#include "diskio.h"
#include <string.h>

static SD_HandleTypeDef sd;
static DSTATUS sd_status = STA_NOINIT;
static uint32_t storage_started;
static uint32_t sector_buffer[128] __attribute__((aligned(32)));

static int sd_hardware_init(void)
{
    RCC_PeriphCLKInitTypeDef clock = {0};
    GPIO_InitTypeDef pins = {0};
    RIMC_MasterConfig_t master = {0};
    __HAL_RCC_RIFSC_CLK_ENABLE();
    master.MasterCID=RIF_CID_1; master.SecPriv=RIF_ATTRIBUTE_SEC|RIF_ATTRIBUTE_PRIV;
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_SDMMC1,&master);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SDMMC1,RIF_ATTRIBUTE_SEC|RIF_ATTRIBUTE_PRIV);
    __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOH_CLK_ENABLE();
    HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12,
                                 GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOH,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    clock.PeriphClockSelection=RCC_PERIPHCLK_SDMMC1;
    clock.Sdmmc1ClockSelection=RCC_SDMMC1CLKSOURCE_HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&clock)!=HAL_OK) return -1;
    __HAL_RCC_SDMMC1_CLK_ENABLE();
    __HAL_RCC_SDMMC1_FORCE_RESET(); __HAL_RCC_SDMMC1_RELEASE_RESET();
    pins.Pin=GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
    pins.Mode=GPIO_MODE_AF_PP; pins.Pull=GPIO_NOPULL;
    pins.Speed=GPIO_SPEED_FREQ_VERY_HIGH; pins.Alternate=GPIO_AF10_SDMMC1;
    HAL_GPIO_Init(GPIOC,&pins); pins.Pin=GPIO_PIN_2; HAL_GPIO_Init(GPIOH,&pins);
    sd.Instance=SDMMC1;
    sd.Init.ClockEdge=SDMMC_CLOCK_EDGE_RISING;
    sd.Init.ClockPowerSave=SDMMC_CLOCK_POWER_SAVE_DISABLE;
    sd.Init.BusWide=SDMMC_BUS_WIDE_4B;
    sd.Init.HardwareFlowControl=SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    sd.Init.ClockDiv=4;
    /* No Error_Handler or retry loop: absent/bad cards select built-in UI. */
    return HAL_SD_Init(&sd)==HAL_OK ? 0 : -1;
}

DSTATUS disk_initialize(BYTE drive)
{
    if (drive!=0u) return STA_NOINIT;
    if (sd_status==0u) return 0u;
    storage_started=HAL_GetTick();
    sd_status=sd_hardware_init()==0 ? 0u : STA_NOINIT;
    return sd_status;
}

DSTATUS disk_status(BYTE drive) { return drive==0u ? sd_status : STA_NOINIT; }

DRESULT disk_read(BYTE drive, BYTE *out, LBA_t sector, UINT count)
{
    if (drive || !out || !count) return RES_PARERR;
    if (sd_status) return RES_NOTRDY;
    HAL_SD_CardInfoTypeDef info;
    if (HAL_SD_GetCardInfo(&sd,&info)!=HAL_OK || sector>=info.LogBlockNbr ||
        count>info.LogBlockNbr-sector) return RES_PARERR;
    for (UINT i=0u;i<count;++i)
    {
        if (HAL_GetTick()-storage_started>=2000u) { sd_status=STA_NOINIT;return RES_ERROR; }
        if (HAL_SD_ReadBlocks(&sd,(uint8_t *)sector_buffer,sector+i,1u,100u)!=HAL_OK)
        { sd_status=STA_NOINIT; return RES_ERROR; }
        uint32_t start=HAL_GetTick();
        while (HAL_SD_GetCardState(&sd)!=HAL_SD_CARD_TRANSFER)
            if (HAL_GetTick()-start>=100u) { sd_status=STA_NOINIT; return RES_ERROR; }
        memcpy(out+i*512u,sector_buffer,512u);
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *out)
{
    if (drive || sd_status) return RES_NOTRDY;
    if (command==CTRL_SYNC) return RES_OK;
    if (!out) return RES_PARERR;
    HAL_SD_CardInfoTypeDef info;
    if (HAL_SD_GetCardInfo(&sd,&info)!=HAL_OK) return RES_ERROR;
    if (command==GET_SECTOR_COUNT) *(LBA_t *)out=info.LogBlockNbr;
    else if (command==GET_SECTOR_SIZE) *(WORD *)out=512u;
    else if (command==GET_BLOCK_SIZE) *(DWORD *)out=1u;
    else return RES_PARERR;
    return RES_OK;
}
