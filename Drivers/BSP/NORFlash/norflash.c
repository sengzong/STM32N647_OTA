#include "norflash.h"
#include "norflash_by25fq128el.h"
#include "norflash_mx25um25645g.h"

#define NORFlash_IS_DUAL_CHIP(NORFlashObject) (NORFlashObject->XSPIObject.XSPIHandle->Init.MemoryMode != HAL_XSPI_SINGLE_MEM)

NORFlash_StatusTypeDef NORFlash_Init(NORFlash_ObjectTypeDef *NORFlashObject, XSPI_HandleTypeDef *hxspi, uint32_t ClockInput)
{
    uint8_t JEDECID[3 * 2];
    uint8_t JEDECIDSize = sizeof(JEDECID) / 2;
    uint32_t ClockRequested;

    NORFlash_XSPI_Init(&(NORFlashObject->XSPIObject), hxspi);

    if (NORFlash_IS_DUAL_CHIP(NORFlashObject))
    {
        JEDECIDSize *= 2;
    }

    if (NORFlash_XSPI_SetClock(&(NORFlashObject->XSPIObject), ClockInput, 50000000, NULL) != NORFlash_XSPI_OK)
    {
        goto Error;
    }

    NORFlash_Reset(NORFlashObject);
    HAL_Delay(10);

    if (NORFlash_XSPI_CommandRead(&(NORFlashObject->XSPIObject), 0x9F, JEDECID, JEDECIDSize) != NORFlash_XSPI_OK)
    {
        goto Error;
    }

    if (NORFlash_IS_BY25FQ128EL(JEDECID))
    {
        if (NORFlash_BY25FQ128EL_Init(NORFlashObject, ClockInput, &ClockRequested) != NORFlash_OK)
        {
            goto Error;
        }
    }
    else if (NORFlash_IS_DUAL_BY25FQ128EL(JEDECID))
    {
        if (NORFlash_DUAL_BY25FQ128EL_Init(NORFlashObject, ClockInput, &ClockRequested) != NORFlash_OK)
        {
            goto Error;
        }
    }
    else if (NORFlash_IS_MX25UM25645G(JEDECID))
    {
        if (NORFlash_MX25UM25645G_Init(NORFlashObject, ClockInput, &ClockRequested) != NORFlash_OK)
        {
            goto Error;
        }
    }
    else
    {
        goto Error;
    }

    if (NORFlash_XSPI_SetClock(&(NORFlashObject->XSPIObject), ClockInput, ClockRequested, NULL) != NORFlash_XSPI_OK)
    {
        goto Error;
    }

    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}

NORFlash_StatusTypeDef NORFlash_Deinit(NORFlash_ObjectTypeDef *NORFlashObject)
{
    NORFlash_XSPI_Deinit(&(NORFlashObject->XSPIObject));

    return NORFlash_OK;
}

void NORFlash_Reset(NORFlash_ObjectTypeDef *NORFlashObject)
{
    uint8_t index;
    NORFlash_XSPI_PhysicalLinkTypeDef PhyLinkTable[] = {
        NORFlash_PHY_LINK_1S1S1S,
        NORFlash_PHY_LINK_4S4S4S,
        NORFlash_PHY_LINK_8D8D8D,
    };

    for (index = 0; index < (sizeof(PhyLinkTable) / sizeof(NORFlash_XSPI_PhysicalLinkTypeDef)); index++)
    {
        NORFlash_XSPI_ConfigPHYLink(&(NORFlashObject->XSPIObject), PhyLinkTable[index]);

        NORFlash_XSPI_CommandSendData(&(NORFlashObject->XSPIObject), 0x66, NULL, 0);
        NORFlash_XSPI_CommandSendData(&(NORFlashObject->XSPIObject), 0x99, NULL, 0);
    }

    NORFlash_XSPI_ConfigPHYLink(&(NORFlashObject->XSPIObject), NORFlash_PHY_LINK_1S1S1S);
}

NORFlash_StatusTypeDef NORFlash_WaitBusy(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t Timeout)
{
    uint16_t MatchValue = 0 << 0;
    uint16_t MatchMask = 1 << 0;
    uint8_t DataSize = 1;

    if (NORFlash_IS_DUAL_CHIP(NORFlashObject))
    {
        MatchValue |= (MatchValue << 8);
        MatchMask |= (MatchMask << 8);
        DataSize *= 2;
    }
    if (NORFlash_XSPI_WaitStatusRegister(&(NORFlashObject->XSPIObject), 0x05, MatchValue, MatchMask,DataSize, Timeout) != NORFlash_XSPI_OK)
    {
        goto Error;
    }

    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}

NORFlash_StatusTypeDef NORFlash_EnableWrite(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t Timeout)
{
    uint16_t MatchValue = 1 << 1;
    uint16_t MatchMask = 1 << 1;
    uint8_t DataSize = 1;

    if (NORFlash_XSPI_CommandSendData(&(NORFlashObject->XSPIObject), 0x06, NULL, 0) != NORFlash_XSPI_OK)
    {
        goto Error;
    }

    if (NORFlash_IS_DUAL_CHIP(NORFlashObject))
    {
        MatchValue |= (MatchValue << 8);
        MatchMask |= (MatchMask << 8);
        DataSize *= 2;
    }
    if (NORFlash_XSPI_WaitStatusRegister(&(NORFlashObject->XSPIObject), 0x05, MatchValue, MatchMask, DataSize, Timeout) != NORFlash_XSPI_OK)
    {
        goto Error;
    }

    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}

NORFlash_StatusTypeDef NORFlash_EnableMemoryMappedMode(NORFlash_ObjectTypeDef *NORFlashObject)
{
    if (NORFlash_XSPI_EnableMapMode(&(NORFlashObject->XSPIObject), NORFlashObject->Command.MapRead.Command, NORFlashObject->Command.MapRead.Dummy, NORFlashObject->Command.MapWrite.Command, NORFlashObject->Command.MapWrite.Dummy) != NORFlash_XSPI_OK)
    {
        goto Error;
    }

    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}

NORFlash_StatusTypeDef NORFlash_DisableMemoryMappedMode(NORFlash_ObjectTypeDef *NORFlashObject)
{
    if (NORFlash_XSPI_DisableMapMode(&(NORFlashObject->XSPIObject)) != NORFlash_XSPI_OK)
    {
        goto Error;
    }

    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}

NORFlash_StatusTypeDef NORFlash_GetMemoryMappedAddress(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t *BaseAddress)
{
    if (NORFlashObject->XSPIObject.XSPIHandle->Instance == XSPI1)
    {
        *BaseAddress = XSPI1_BASE;
    }
    else if (NORFlashObject->XSPIObject.XSPIHandle->Instance == XSPI2)
    {
        *BaseAddress = XSPI2_BASE;
    }
    else
    {
        goto Error;
    }

    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}

NORFlash_StatusTypeDef NORFlash_EraseChip(NORFlash_ObjectTypeDef *NORFlashObject)
{
    if (NORFlash_WaitBusy(NORFlashObject, 1000) != NORFlash_OK)
    {
        goto Error;
    }

    if (NORFlash_EnableWrite(NORFlashObject, 1000) != NORFlash_OK)
    {
        goto Error;
    }

    if (NORFlash_XSPI_CommandSendData(&(NORFlashObject->XSPIObject), 0x60, NULL, 0) != NORFlash_XSPI_OK)
    {
        goto Error;
    }

    if (NORFlash_WaitBusy(NORFlashObject, NORFlashObject->Timing.EraseChip) != NORFlash_OK)
    {
        goto Error;
    }

    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}

NORFlash_StatusTypeDef NORFlash_EraseSector(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t Address, uint32_t Size)
{
    while (Size != 0)
    {
        if ((Size >= NORFlashObject->Information.SectorSize) && ((Address % NORFlashObject->Information.SectorSize) == 0))
        {
            if (NORFlash_WaitBusy(NORFlashObject, 5000) != NORFlash_OK)
            {
                goto Error;
            }

            if (NORFlash_EnableWrite(NORFlashObject, 1000) != NORFlash_OK)
            {
                goto Error;
            }

            if (NORFlash_XSPI_CommandSendAddress(&(NORFlashObject->XSPIObject), NORFlashObject->Command.EraseSector.Command, Address) != NORFlash_XSPI_OK)
            {
                goto Error;
            }

            if (NORFlash_WaitBusy(NORFlashObject, NORFlashObject->Timing.EraseSector) != NORFlash_OK)
            {
                goto Error;
            }

            Address += NORFlashObject->Information.SectorSize;
            if (Size >= NORFlashObject->Information.SectorSize)
            {
                Size -= NORFlashObject->Information.SectorSize;
            }
            else
            {
                Size = 0;
            }
        }
        else
        {
            goto Error;
        }
    }
    
    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}

NORFlash_StatusTypeDef NORFlash_Write(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t Address, const uint8_t *Data, uint32_t DataSize)
{
    uint8_t Misalignment = 0;
    uint32_t WriteSize;

    if ((Address % NORFlashObject->Information.PageSize) != 0)
    {
        Misalignment = 1;
    }

    while (DataSize != 0)
    {
        if (Misalignment != 0)
        {
            WriteSize = NORFlashObject->Information.PageSize - (Address % NORFlashObject->Information.PageSize);
            if (DataSize < WriteSize)
            {
                WriteSize = DataSize;
            }
            Misalignment = 0;
        }
        else
        {
            WriteSize = (DataSize < NORFlashObject->Information.PageSize) ? DataSize : NORFlashObject->Information.PageSize;
        }

        if (NORFlash_WaitBusy(NORFlashObject, 5000) != NORFlash_OK)
        {
            goto Error;
        }

        if (NORFlash_EnableWrite(NORFlashObject, 1000) != NORFlash_OK)
        {
            goto Error;
        }

        if (NORFlash_XSPI_Write(&(NORFlashObject->XSPIObject), NORFlashObject->Command.ProgramPage.Command, NORFlashObject->Command.ProgramPage.Dummy, Address, Data, WriteSize) != NORFlash_XSPI_OK)
        {
            goto Error;
        }

        DataSize -= WriteSize;
        Address += WriteSize;
        Data += WriteSize;
    }

    if (NORFlash_WaitBusy(NORFlashObject, 5000) != NORFlash_OK)
    {
        goto Error;
    }

    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}

NORFlash_StatusTypeDef NORFlash_GetMemInfo(NORFlash_ObjectTypeDef *NORFlashObject, NORFlash_InformationTypeDef *Information)
{
    if (NORFlashObject->Information.FlashSize == 0)
    {
        goto Error;
    }

    Information->FlashSize = NORFlashObject->Information.FlashSize;
    Information->SectorSize = NORFlashObject->Information.SectorSize;
    Information->PageSize = NORFlashObject->Information.PageSize;

    return NORFlash_OK;

Error:
    return NORFlash_ERROR;
}
