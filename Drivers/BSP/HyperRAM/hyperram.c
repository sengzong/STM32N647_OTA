#include "hyperram.h"

#define HYPERRAM_IDENTIFICATION_REGISTER_0              ((uint32_t)0x00000000 << 1)
#define HYPERRAM_IDENTIFICATION_REGISTER_1              ((uint32_t)0x00000001 << 1)
#define HYPERRAM_CONFIGURATION_REGISTER_1               ((uint32_t)0x00000801 << 1)

#define HYPERRAM_IDENTIFICATION_0_MANUFACTURER_POS      (0)
#define HYPERRAM_IDENTIFICATION_0_COL_COUNT_POS         (8)
#define HYPERRAM_IDENTIFICATION_0_ROW_COUNT_POS         (4)
#define HYPERRAM_IDENTIFICATION_1_DEVICE_TYPE_POS       (0)
#define HYPERRAM_CONFIGURATION_1_MASTER_CLOCK_TYPE_POS  (6)

#define HYPERRAM_IDENTIFICATION_0_MANUFACTURER_MASK     ((uint16_t)0x000F << HYPERRAM_IDENTIFICATION_0_MANUFACTURER_POS)
#define HYPERRAM_IDENTIFICATION_0_COL_COUNT_MASK        ((uint16_t)0x000F << HYPERRAM_IDENTIFICATION_0_COL_COUNT_POS)
#define HYPERRAM_IDENTIFICATION_0_ROW_COUNT_MASK        ((uint16_t)0x001F << HYPERRAM_IDENTIFICATION_0_ROW_COUNT_POS)
#define HYPERRAM_IDENTIFICATION_1_DEVICE_TYPE_MASK      ((uint16_t)0x000F << HYPERRAM_IDENTIFICATION_1_DEVICE_TYPE_POS)
#define HYPERRAM_CONFIGURATION_1_MASTER_CLOCK_TYPE_MASK ((uint16_t)0x0001 << HYPERRAM_CONFIGURATION_1_MASTER_CLOCK_TYPE_POS)

#define HYPERRAM_IDENTIFICATION_0_MANUFACTURER          ((uint16_t)0x0006)
#define HYPERRAM_IDENTIFICATION_1_DEVICE_TYPE           ((uint16_t)0x0001)

static HyperRAM_StatusTypeDef HyperRAM_ReadRegister(HyperRAM_ObjectTypeDef *HyperRAMObject, uint32_t Address, uint16_t *Data);
static HyperRAM_StatusTypeDef HyperRAM_WriteRegister(HyperRAM_ObjectTypeDef *HyperRAMObject, uint32_t Address, uint16_t Data);
static HyperRAM_StatusTypeDef HyperRAM_GetID0(HyperRAM_ObjectTypeDef *HyperRAMObject, uint16_t *ID);
static HyperRAM_StatusTypeDef HyperRAM_GetID1(HyperRAM_ObjectTypeDef *HyperRAMObject, uint16_t *ID);
static HyperRAM_StatusTypeDef HyperRAM_EnableDifferentialClock(HyperRAM_ObjectTypeDef *HyperRAMObject);

HyperRAM_StatusTypeDef HyperRAM_Init(HyperRAM_ObjectTypeDef *HyperRAMObject, XSPI_HandleTypeDef *hxspi)
{
    uint16_t ID[2];

    HyperRAMObject->XSPIHandle = hxspi;
    HyperRAMObject->BaseCommand.AddressSpace = HAL_XSPI_REGISTER_ADDRESS_SPACE;
    HyperRAMObject->BaseCommand.Address = 0x00000000;
    HyperRAMObject->BaseCommand.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    HyperRAMObject->BaseCommand.DataLength = 2;
    HyperRAMObject->BaseCommand.DQSMode = HAL_XSPI_DQS_ENABLE;
    HyperRAMObject->BaseCommand.DataMode = HAL_XSPI_DATA_8_LINES;

    HyperRAM_GetID0(HyperRAMObject, &ID[0]);
    if ((ID[0] & HYPERRAM_IDENTIFICATION_0_MANUFACTURER_MASK) != HYPERRAM_IDENTIFICATION_0_MANUFACTURER)
    {
        goto Error;
    }

    HyperRAM_GetID1(HyperRAMObject, &ID[1]);
    if ((ID[1] & HYPERRAM_IDENTIFICATION_1_DEVICE_TYPE_MASK) != HYPERRAM_IDENTIFICATION_1_DEVICE_TYPE)
    {
        goto Error;
    }

    HyperRAMObject->Size = (uint32_t)1 << (((((ID[0] & HYPERRAM_IDENTIFICATION_0_COL_COUNT_MASK) >> HYPERRAM_IDENTIFICATION_0_COL_COUNT_POS) + 1) + (((ID[0] & HYPERRAM_IDENTIFICATION_0_ROW_COUNT_MASK) >> HYPERRAM_IDENTIFICATION_0_ROW_COUNT_POS) + 1)) + 1);

    if (HyperRAM_EnableDifferentialClock(HyperRAMObject) != HyperRAM_OK)
    {
        goto Error;
    }

    return HyperRAM_OK;

Error:
    return HyperRAM_ERROR;
}

HyperRAM_StatusTypeDef HyperRAM_EnableMemoryMappedMode(HyperRAM_ObjectTypeDef *HyperRAMObject)
{
    XSPI_HyperbusCmdTypeDef Cmd = HyperRAMObject->BaseCommand;
    XSPI_MemoryMappedTypeDef Cfg = {0};

    Cmd.AddressSpace = HAL_XSPI_MEMORY_ADDRESS_SPACE;
    if (HAL_XSPI_HyperbusCmd(HyperRAMObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    Cfg.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_ENABLE;
    Cfg.TimeoutPeriodClock = 0x34;
    if (HAL_XSPI_MemoryMapped(HyperRAMObject->XSPIHandle, &Cfg) != HAL_OK)
    {
        goto Error;
    }

    return HyperRAM_OK;

Error:
    return HyperRAM_ERROR;
}

HyperRAM_StatusTypeDef HyperRAM_DisableMemoryMappedMode(HyperRAM_ObjectTypeDef *HyperRAMObject)
{
    __DSB();
    if (HAL_XSPI_Abort(HyperRAMObject->XSPIHandle) != HAL_OK)
    {
        goto Error;
    }

    return HyperRAM_OK;

Error:
    return HyperRAM_ERROR;
}

HyperRAM_StatusTypeDef HyperRAM_GetMemoryMappedAddress(HyperRAM_ObjectTypeDef *HyperRAMObject, uint32_t *BaseAddress)
{
    if (HyperRAMObject->XSPIHandle->Instance == XSPI1)
    {
        *BaseAddress = XSPI1_BASE;
    }
    else if (HyperRAMObject->XSPIHandle->Instance == XSPI2)
    {
        *BaseAddress = XSPI2_BASE;
    }
    else
    {
        goto Error;
    }

    return HyperRAM_OK;

Error:
    return HyperRAM_ERROR;
}

static HyperRAM_StatusTypeDef HyperRAM_ReadRegister(HyperRAM_ObjectTypeDef *HyperRAMObject, uint32_t Address, uint16_t *Data)
{
    XSPI_HyperbusCmdTypeDef Cmd = HyperRAMObject->BaseCommand;

    Cmd.Address = Address;
    if (HAL_XSPI_HyperbusCmd(HyperRAMObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    if (HAL_XSPI_Receive(HyperRAMObject->XSPIHandle, (uint8_t *)Data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    return HyperRAM_OK;

Error:
    HAL_XSPI_Abort(HyperRAMObject->XSPIHandle);
    return HyperRAM_ERROR;
}

static HyperRAM_StatusTypeDef HyperRAM_WriteRegister(HyperRAM_ObjectTypeDef *HyperRAMObject, uint32_t Address, uint16_t Data)
{
    XSPI_HyperbusCmdTypeDef Cmd = HyperRAMObject->BaseCommand;

    Cmd.Address = Address;
    if (HAL_XSPI_HyperbusCmd(HyperRAMObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    if (HAL_XSPI_Transmit(HyperRAMObject->XSPIHandle, (uint8_t *)&Data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    return HyperRAM_OK;

Error:
    HAL_XSPI_Abort(HyperRAMObject->XSPIHandle);
    return HyperRAM_ERROR;
}

static HyperRAM_StatusTypeDef HyperRAM_GetID0(HyperRAM_ObjectTypeDef *HyperRAMObject, uint16_t *ID)
{
    if (HyperRAM_ReadRegister(HyperRAMObject, HYPERRAM_IDENTIFICATION_REGISTER_0, ID) != HyperRAM_OK)
    {
        goto Error;
    }

    return HyperRAM_OK;

Error:
    HAL_XSPI_Abort(HyperRAMObject->XSPIHandle);
    return HyperRAM_ERROR;
}

static HyperRAM_StatusTypeDef HyperRAM_GetID1(HyperRAM_ObjectTypeDef *HyperRAMObject, uint16_t *ID)
{
    if (HyperRAM_ReadRegister(HyperRAMObject, HYPERRAM_IDENTIFICATION_REGISTER_1, ID) != HyperRAM_OK)
    {
        goto Error;
    }

    return HyperRAM_OK;

Error:
    HAL_XSPI_Abort(HyperRAMObject->XSPIHandle);
    return HyperRAM_ERROR;
}

static HyperRAM_StatusTypeDef HyperRAM_EnableDifferentialClock(HyperRAM_ObjectTypeDef *HyperRAMObject)
{
    uint16_t Cfg;

    if (HyperRAM_ReadRegister(HyperRAMObject, HYPERRAM_CONFIGURATION_REGISTER_1, &Cfg) != HyperRAM_OK)
    {
        goto Error;
    }

    Cfg &= ~HYPERRAM_CONFIGURATION_1_MASTER_CLOCK_TYPE_MASK;
    if (HyperRAM_WriteRegister(HyperRAMObject, HYPERRAM_CONFIGURATION_REGISTER_1, Cfg) != HyperRAM_OK)
    {
        goto Error;
    }

    return HyperRAM_OK;

Error:
    HAL_XSPI_Abort(HyperRAMObject->XSPIHandle);
    return HyperRAM_ERROR;
}
