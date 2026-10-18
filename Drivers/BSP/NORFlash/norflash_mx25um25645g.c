#include "norflash_mx25um25645g.h"

NORFlash_StatusTypeDef NORFlash_MX25UM25645G_Init(NORFlash_ObjectTypeDef *NORFlashObject, uint32_t ClockInput, uint32_t *ClockRequested)
{
    uint8_t Data;

    NORFlashObject->Information.FlashSize = 0x2000000;
    NORFlashObject->Information.SectorSize = 0x1000;
    NORFlashObject->Information.PageSize = 0x100;
    NORFlashObject->Timing.EraseChip = 150000;
    NORFlashObject->Timing.EraseSector = 400;
    NORFlashObject->Timing.ProgramPage = 1;
    NORFlashObject->Command.MapRead.Command = 0xEE;
    NORFlashObject->Command.MapRead.Dummy = 20;
    NORFlashObject->Command.MapWrite.Command = 0x12;
    NORFlashObject->Command.MapWrite.Dummy = 0;
    NORFlashObject->Command.ProgramPage.Command = 0x12;
    NORFlashObject->Command.ProgramPage.Dummy = 0;
    NORFlashObject->Command.EraseSector.Command = 0x21;
    NORFlashObject->Command.EraseSector.Dummy = 0;

    NORFlash_XSPI_CommandSendAddressReadData(&(NORFlashObject->XSPIObject), 0x71, 0x00000000, &Data, sizeof(Data));
    if ((Data & (3 << 0)) != 0)
    {
        return NORFlash_ERROR;
    }

    Data &= ~(3 << 0);
    Data |= (2 << 0);

    NORFlash_EnableWrite(NORFlashObject, 1000);

    NORFlash_XSPI_CommandSendAddressSendData(&(NORFlashObject->XSPIObject), 0x72, 0x00000000, &Data, sizeof(Data));

    NORFlash_XSPI_ConfigPHYLink(&(NORFlashObject->XSPIObject), NORFlash_PHY_LINK_8D8D8D);

    if (ClockRequested != NULL)
    {
        *ClockRequested = ClockInput;
    }

    return NORFlash_OK;
}
