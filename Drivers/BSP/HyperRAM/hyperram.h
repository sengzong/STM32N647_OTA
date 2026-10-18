#ifndef __HYPERRAM_H
#define __HYPERRAM_H

#include "stm32n6xx_hal.h"

typedef enum
{
    HyperRAM_OK,
    HyperRAM_ERROR,
} HyperRAM_StatusTypeDef;

typedef struct
{
    XSPI_HandleTypeDef *XSPIHandle;
    XSPI_HyperbusCmdTypeDef BaseCommand;

    uint32_t Size;
} HyperRAM_ObjectTypeDef;

HyperRAM_StatusTypeDef HyperRAM_Init(HyperRAM_ObjectTypeDef *HyperRAMObject, XSPI_HandleTypeDef *hxspi);
HyperRAM_StatusTypeDef HyperRAM_EnableMemoryMappedMode(HyperRAM_ObjectTypeDef *HyperRAMObject);
HyperRAM_StatusTypeDef HyperRAM_DisableMemoryMappedMode(HyperRAM_ObjectTypeDef *HyperRAMObject);
HyperRAM_StatusTypeDef HyperRAM_GetMemoryMappedAddress(HyperRAM_ObjectTypeDef *HyperRAMObject, uint32_t *BaseAddress);

#endif
