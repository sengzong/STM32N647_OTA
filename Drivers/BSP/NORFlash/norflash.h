#ifndef __NORFLASH_H
#define __NORFLASH_H

#include "norflash_xspi.h"

typedef enum
{
    NORFlash_OK,
    NORFlash_ERROR,
} NORFlash_StatusTypeDef;

typedef struct
{
    uint32_t FlashSize;
    uint32_t SectorSize;
    uint32_t PageSize;
} NORFlash_InformationTypeDef;

typedef struct
{
    uint8_t Command;
    uint8_t Dummy;
} NORFlash_CommandTypeDef;

typedef struct
{
    NORFlash_XSPI_ObjectTypeDef XSPIObject;

    NORFlash_InformationTypeDef Information;

    struct
    {
        uint32_t EraseChip;
        uint32_t EraseSector;
        uint32_t ProgramPage;
    } Timing;

    struct
    {
        NORFlash_CommandTypeDef MapRead;
        NORFlash_CommandTypeDef MapWrite;
        NORFlash_CommandTypeDef ProgramPage;
        NORFlash_CommandTypeDef EraseSector;
    } Command;
} NORFlash_ObjectTypeDef;

NORFlash_StatusTypeDef NORFlash_Init(NORFlash_ObjectTypeDef *NORFlashObject, XSPI_HandleTypeDef *hxspi, uint32_t ClockInput);
NORFlash_StatusTypeDef NORFlash_Deinit(NORFlash_ObjectTypeDef *NORFlashObject);
void NORFlash_Reset(NORFlash_ObjectTypeDef *NORFlashObject);
NORFlash_StatusTypeDef NORFlash_WaitBusy(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t Timeout);
NORFlash_StatusTypeDef NORFlash_EnableWrite(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t Timeout);
NORFlash_StatusTypeDef NORFlash_EnableMemoryMappedMode(NORFlash_ObjectTypeDef *NORFlashObject);
NORFlash_StatusTypeDef NORFlash_DisableMemoryMappedMode(NORFlash_ObjectTypeDef *NORFlashObject);
NORFlash_StatusTypeDef NORFlash_GetMemoryMappedAddress(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t *BaseAddress);
NORFlash_StatusTypeDef NORFlash_EraseChip(NORFlash_ObjectTypeDef *NORFlashObject);
NORFlash_StatusTypeDef NORFlash_EraseSector(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t Address, uint32_t Size);
NORFlash_StatusTypeDef NORFlash_Write(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t Address, const uint8_t *Data, uint32_t DataSize);
NORFlash_StatusTypeDef NORFlash_GetMemInfo(NORFlash_ObjectTypeDef *NORFlashObject, NORFlash_InformationTypeDef *Information);

#endif
