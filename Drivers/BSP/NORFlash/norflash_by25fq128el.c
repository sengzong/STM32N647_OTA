#include "norflash_by25fq128el.h"

static NORFlash_StatusTypeDef BY25FQ128EL_Init(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t ClockInput, uint32_t *ClockRequested, uint8_t IsDualChip)
{
    uint8_t Data[2];
    uint8_t DataSize = sizeof(Data) / 2;

    NORFlashObject->Information.FlashSize = 0x1000000;
    NORFlashObject->Information.SectorSize = 0x1000;
    NORFlashObject->Information.PageSize = 0x100;
    NORFlashObject->Timing.EraseChip = 60000;
    NORFlashObject->Timing.EraseSector = 200;
    NORFlashObject->Timing.ProgramPage = 3;
#ifdef CONFIG_NORFLASH_BY25FQ128EL_IN_SPI_MODE
    NORFlashObject->Command.MapRead.Command = 0x03;
    NORFlashObject->Command.MapRead.Dummy = 0;
#else
    NORFlashObject->Command.MapRead.Command = 0x0B;
    NORFlashObject->Command.MapRead.Dummy = 4;
#endif
    NORFlashObject->Command.MapWrite.Command = 0x02;
    NORFlashObject->Command.MapWrite.Dummy = 0;
    NORFlashObject->Command.ProgramPage.Command = 0x02;
    NORFlashObject->Command.ProgramPage.Dummy = 0;
    NORFlashObject->Command.EraseSector.Command = 0x20;
    NORFlashObject->Command.EraseSector.Dummy = 0;

    if (IsDualChip)
    {
        DataSize *= 2;

        NORFlashObject->Information.FlashSize *= 2;
        NORFlashObject->Information.SectorSize *= 2;
        NORFlashObject->Information.PageSize *= 2;
    }

#ifndef CONFIG_NORFLASH_BY25FQ128EL_IN_SPI_MODE
    NORFlash_XSPI_CommandRead(&(NORFlashObject->XSPIObject), 0x35, Data, DataSize);
    if (((Data[0] & (1 << 1)) != (1 << 1)) || (IsDualChip && ((Data[1] & (1 << 1)) != (1 << 1))))
    {
        Data[0] |= (1 << 1);
        Data[1] |= (1 << 1);

        NORFlash_EnableWrite(NORFlashObject, 1000);

        NORFlash_XSPI_CommandSendData(&(NORFlashObject->XSPIObject), 0x31, Data, DataSize);

        NORFlash_WaitBusy(NORFlashObject, 1000);

        Data[0] &= ~(1 << 1);
        Data[1] &= ~(1 << 1);
        NORFlash_XSPI_CommandRead(&(NORFlashObject->XSPIObject), 0x35, Data, DataSize);
        if (((Data[0] & (1 << 1)) != (1 << 1)) || (IsDualChip && ((Data[1] & (1 << 1)) != (1 << 1))))
        {
            return NORFlash_ERROR;
        }
    }

    if (NORFlash_XSPI_CommandSendData(&(NORFlashObject->XSPIObject), 0x38, NULL, 0) != NORFlash_XSPI_OK)
    {
        return NORFlash_ERROR;
    }

    NORFlash_XSPI_ConfigPHYLink(&(NORFlashObject->XSPIObject), NORFlash_PHY_LINK_4S4S4S);

    Data[0] = 0 << 0;
    Data[1] = 0 << 0;
    if (ClockInput <= 80000000)
    {
        Data[0] |= 0 << 4;
        Data[1] |= 0 << 4;
        NORFlashObject->Command.MapRead.Dummy = 4;
    }
    else if (ClockInput < 133333333)
    {
        Data[0] |= 1 << 4;
        Data[1] |= 1 << 4;
        NORFlashObject->Command.MapRead.Dummy = 6;
    }
    else
    {
        Data[0] |= 2 << 4;
        Data[1] |= 2 << 4;
        NORFlashObject->Command.MapRead.Dummy = 8;
    }
    if (NORFlash_XSPI_CommandSendData(&(NORFlashObject->XSPIObject), 0xC0, Data, DataSize) != NORFlash_XSPI_OK)
    {
        return NORFlash_ERROR;
    }
#endif

    if (ClockRequested != NULL)
    {
        if (ClockInput <= 80000000)
        {
            *ClockRequested = ClockInput;
        }
        else if (ClockInput < 108000000)
        {
            *ClockRequested = 80000000;
        }
        else if (ClockInput < 133333333)
        {
            *ClockRequested = 108000000;
        }
        else
        {
            *ClockRequested = 133333333;
        }
    }

    return NORFlash_OK;
}

NORFlash_StatusTypeDef NORFlash_BY25FQ128EL_Init(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t ClockInput, uint32_t *ClockRequested)
{
    return BY25FQ128EL_Init(NORFlashObject, ClockInput, ClockRequested, 0);
}

NORFlash_StatusTypeDef NORFlash_DUAL_BY25FQ128EL_Init(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t ClockInput, uint32_t *ClockRequested)
{
    return BY25FQ128EL_Init(NORFlashObject, ClockInput, ClockRequested, 1);
}
