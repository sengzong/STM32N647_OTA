#ifndef __NORFLASH_XSPI_H
#define __NORFLASH_XSPI_H

#include "stm32n6xx_hal.h"

typedef enum
{
    NORFlash_XSPI_OK,
    NORFlash_XSPI_ERROR,
} NORFlash_XSPI_StatusTypeDef;

typedef enum {
    NORFlash_PHY_LINK_1S1S1S,
    NORFlash_PHY_LINK_4S4S4S,
    NORFlash_PHY_LINK_8D8D8D,
} NORFlash_XSPI_PhysicalLinkTypeDef;

typedef struct
{
    XSPI_HandleTypeDef *XSPIHandle;
    XSPI_RegularCmdTypeDef BaseCommand;
    NORFlash_XSPI_PhysicalLinkTypeDef PhyLink;
} NORFlash_XSPI_ObjectTypeDef;

void NORFlash_XSPI_Init(NORFlash_XSPI_ObjectTypeDef *XSPIObject, XSPI_HandleTypeDef *XSPIHandle);
void NORFlash_XSPI_Deinit(NORFlash_XSPI_ObjectTypeDef *XSPIObject);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_SetClock(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint32_t ClockInput, uint32_t ClockRequested, uint32_t *ClockReal);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_EnableMapMode(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t CommandRead, uint8_t DummyRead, uint8_t CommandWrite, uint8_t DummyWrite);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_DisableMapMode(NORFlash_XSPI_ObjectTypeDef *XSPIObject);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_ConfigPHYLink(NORFlash_XSPI_ObjectTypeDef *XSPIObject, NORFlash_XSPI_PhysicalLinkTypeDef PhyLink);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandSendData(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint8_t *Data, uint16_t DataSize);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandSendAddress(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint32_t Address);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandRead(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint8_t *Data, uint16_t DataSize);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandSendAddressReadData(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint32_t Address, uint8_t *Data, uint16_t DataSize);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandSendAddressSendData(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint32_t Address, uint8_t *Data, uint16_t DataSize);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_WaitStatusRegister(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint16_t MatchValue, uint16_t MatchMask, uint8_t DataSize, uint32_t Timeout);
NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_Write(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint8_t Dummy, uint32_t Address, const uint8_t *Data, uint32_t DataSize);

#endif
