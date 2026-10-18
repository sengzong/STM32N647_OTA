#include "norflash_xspi.h"

void NORFlash_XSPI_Init(NORFlash_XSPI_ObjectTypeDef *XSPIObject, XSPI_HandleTypeDef *XSPIHandle)
{
    XSPIObject->XSPIHandle = XSPIHandle;
    XSPIObject->BaseCommand.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
    XSPIObject->BaseCommand.IOSelect = HAL_XSPI_SELECT_IO_7_0;
    XSPIObject->BaseCommand.Instruction = 0;
    XSPIObject->BaseCommand.InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
    XSPIObject->BaseCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
    XSPIObject->BaseCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    XSPIObject->BaseCommand.Address = 0;
    XSPIObject->BaseCommand.AddressMode = HAL_XSPI_ADDRESS_1_LINE;
    XSPIObject->BaseCommand.AddressWidth = HAL_XSPI_ADDRESS_24_BITS;
    XSPIObject->BaseCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
    XSPIObject->BaseCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    XSPIObject->BaseCommand.DataMode = HAL_XSPI_DATA_1_LINE;
    XSPIObject->BaseCommand.DataLength = 0;
    XSPIObject->BaseCommand.DataDTRMode = HAL_XSPI_DATA_DTR_DISABLE;
    XSPIObject->BaseCommand.DummyCycles = 0;
    XSPIObject->BaseCommand.DQSMode = HAL_XSPI_DQS_DISABLE;

    __DSB();
    HAL_XSPI_Abort(XSPIObject->XSPIHandle);
}

void NORFlash_XSPI_Deinit(NORFlash_XSPI_ObjectTypeDef *XSPIObject)
{
    XSPIObject->XSPIHandle = NULL;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_SetClock(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint32_t ClockInput, uint32_t ClockRequested, uint32_t *ClockReal)
{
    uint32_t Divider;
    
    Divider = ClockInput / ClockRequested;
    if (Divider >= 1)
    {
        if ((ClockInput / Divider) <= ClockRequested)
        {
            Divider--;
        }
    }

    if (HAL_XSPI_SetClockPrescaler(XSPIObject->XSPIHandle, Divider) != HAL_OK)
    {
        return NORFlash_XSPI_ERROR;
    }

    if (ClockReal != NULL)
    {
        *ClockReal = ClockInput / (Divider + 1);
    }

    return NORFlash_XSPI_OK;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_EnableMapMode(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t CommandRead, uint8_t DummyRead, uint8_t CommandWrite, uint8_t DummyWrite)
{
    XSPI_RegularCmdTypeDef Cmd = XSPIObject->BaseCommand;
    XSPI_MemoryMappedTypeDef Cfg = {0};

    Cmd.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
    Cmd.Instruction = (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS) ? (((uint16_t)CommandRead << 8) | (uint8_t)(~CommandRead & 0xFF)) : CommandRead;
    Cmd.DummyCycles = DummyRead;
    if (HAL_XSPI_Command(XSPIObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    Cmd.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
    Cmd.Instruction = (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS) ? (((uint16_t)CommandWrite << 8) | (uint8_t)(~CommandWrite & 0xFF)) : CommandWrite;
    Cmd.DummyCycles = DummyWrite;
    if (HAL_XSPI_Command(XSPIObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    Cfg.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;
    Cfg.TimeoutPeriodClock = 0x50;
    if (HAL_XSPI_MemoryMapped(XSPIObject->XSPIHandle, &Cfg) != HAL_OK)
    {
        goto Error;
    }

    return NORFlash_XSPI_OK;

Error:
    HAL_XSPI_Abort(XSPIObject->XSPIHandle);
    return NORFlash_XSPI_ERROR;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_DisableMapMode(NORFlash_XSPI_ObjectTypeDef *XSPIObject)
{
    __DSB();
    if (HAL_XSPI_Abort(XSPIObject->XSPIHandle) != HAL_OK)
    {
        return NORFlash_XSPI_ERROR;
    }

    return NORFlash_XSPI_OK;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_ConfigPHYLink(NORFlash_XSPI_ObjectTypeDef *XSPIObject, NORFlash_XSPI_PhysicalLinkTypeDef PhyLink)
{
    switch (PhyLink)
    {
        case NORFlash_PHY_LINK_1S1S1S:
        {
            XSPIObject->BaseCommand.InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
            XSPIObject->BaseCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
            XSPIObject->BaseCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
            XSPIObject->BaseCommand.AddressMode = HAL_XSPI_ADDRESS_1_LINE;
            XSPIObject->BaseCommand.AddressWidth = HAL_XSPI_ADDRESS_24_BITS;
            XSPIObject->BaseCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
            XSPIObject->BaseCommand.DataMode = HAL_XSPI_DATA_1_LINE;
            XSPIObject->BaseCommand.DataDTRMode = HAL_XSPI_DATA_DTR_DISABLE;
            XSPIObject->BaseCommand.DummyCycles = 0;
            XSPIObject->BaseCommand.DQSMode = HAL_XSPI_DQS_DISABLE;
            break;
        }
        case NORFlash_PHY_LINK_4S4S4S:
        {
            XSPIObject->BaseCommand.InstructionMode = HAL_XSPI_INSTRUCTION_4_LINES;
            XSPIObject->BaseCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
            XSPIObject->BaseCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
            XSPIObject->BaseCommand.AddressMode = HAL_XSPI_ADDRESS_4_LINES;
            XSPIObject->BaseCommand.AddressWidth = HAL_XSPI_ADDRESS_24_BITS;
            XSPIObject->BaseCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
            XSPIObject->BaseCommand.DataMode = HAL_XSPI_DATA_4_LINES;
            XSPIObject->BaseCommand.DataDTRMode = HAL_XSPI_DATA_DTR_DISABLE;
            XSPIObject->BaseCommand.DummyCycles = 0;
            XSPIObject->BaseCommand.DQSMode = HAL_XSPI_DQS_DISABLE;
            break;
        }
        case NORFlash_PHY_LINK_8D8D8D:
        {
            XSPIObject->BaseCommand.InstructionMode = HAL_XSPI_INSTRUCTION_8_LINES;
            XSPIObject->BaseCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_16_BITS;
            XSPIObject->BaseCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
            XSPIObject->BaseCommand.AddressMode = HAL_XSPI_ADDRESS_8_LINES;
            XSPIObject->BaseCommand.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
            XSPIObject->BaseCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_ENABLE;
            XSPIObject->BaseCommand.DataMode = HAL_XSPI_DATA_8_LINES;
            XSPIObject->BaseCommand.DataDTRMode = HAL_XSPI_DATA_DTR_ENABLE;
            XSPIObject->BaseCommand.DummyCycles = 0;
            XSPIObject->BaseCommand.DQSMode = HAL_XSPI_DQS_ENABLE;
            break;
        }
        default:
        {
            goto Error;
        }
    }

    XSPIObject->PhyLink = PhyLink;

    return NORFlash_XSPI_OK;

Error:
    return NORFlash_XSPI_ERROR;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandSendData(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint8_t *Data, uint16_t DataSize)
{
    XSPI_RegularCmdTypeDef Cmd = XSPIObject->BaseCommand;

    Cmd.Instruction = (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS) ? (((uint16_t)Command << 8) | (uint8_t)(~Command & 0xFF)) : Command;
    Cmd.AddressMode = HAL_XSPI_ADDRESS_NONE;
    Cmd.DataMode = (DataSize == 0) ? HAL_XSPI_DATA_NONE : Cmd.DataMode;
    Cmd.DataLength = DataSize;
    if (HAL_XSPI_Command(XSPIObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    if (DataSize != 0)
    {
        if (HAL_XSPI_Transmit(XSPIObject->XSPIHandle, Data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            goto Error;
        }
    }

    return NORFlash_XSPI_OK;

Error:
    HAL_XSPI_Abort(XSPIObject->XSPIHandle);
    return NORFlash_XSPI_ERROR;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandSendAddress(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint32_t Address)
{
    XSPI_RegularCmdTypeDef Cmd = XSPIObject->BaseCommand;

    Cmd.Instruction = (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS) ? (((uint16_t)Command << 8) | (uint8_t)(~Command & 0xFF)) : Command;
    Cmd.Address = Address;
    Cmd.DataMode = HAL_XSPI_DATA_NONE;
    if (HAL_XSPI_Command(XSPIObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    return NORFlash_XSPI_OK;

Error:
    HAL_XSPI_Abort(XSPIObject->XSPIHandle);
    return NORFlash_XSPI_ERROR;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandRead(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint8_t *Data, uint16_t DataSize)
{
    XSPI_RegularCmdTypeDef Cmd = XSPIObject->BaseCommand;

    Cmd.Instruction = (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS) ? (((uint16_t)Command << 8) | (uint8_t)(~Command & 0xFF)) : Command;
    Cmd.AddressMode = HAL_XSPI_ADDRESS_NONE;
    Cmd.DataMode = (DataSize == 0) ? HAL_XSPI_DATA_NONE : Cmd.DataMode;
    Cmd.DataLength = DataSize;
    if (HAL_XSPI_Command(XSPIObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    if (DataSize != 0)
    {
        if (HAL_XSPI_Receive(XSPIObject->XSPIHandle, Data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            goto Error;
        }
    }

    return NORFlash_XSPI_OK;

Error:
    HAL_XSPI_Abort(XSPIObject->XSPIHandle);
    return NORFlash_XSPI_ERROR;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandSendAddressReadData(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint32_t Address, uint8_t *Data, uint16_t DataSize)
{
    XSPI_RegularCmdTypeDef Cmd = XSPIObject->BaseCommand;

    Cmd.Instruction = (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS) ? (((uint16_t)Command << 8) | (uint8_t)(~Command & 0xFF)) : Command;
    Cmd.Address = Address;
    Cmd.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    Cmd.DataMode = (DataSize == 0) ? HAL_XSPI_DATA_NONE : Cmd.DataMode;
    Cmd.DataLength = DataSize;
    if (HAL_XSPI_Command(XSPIObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    if (DataSize != 0)
    {
        if (HAL_XSPI_Receive(XSPIObject->XSPIHandle, Data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            goto Error;
        }
    }

    return NORFlash_XSPI_OK;

Error:
    HAL_XSPI_Abort(XSPIObject->XSPIHandle);
    return NORFlash_XSPI_ERROR;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_CommandSendAddressSendData(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint32_t Address, uint8_t *Data, uint16_t DataSize)
{
    XSPI_RegularCmdTypeDef Cmd = XSPIObject->BaseCommand;

    Cmd.Instruction = (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS) ? (((uint16_t)Command << 8) | (uint8_t)(~Command & 0xFF)) : Command;
    Cmd.Address = Address;
    Cmd.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    Cmd.DataMode = (DataSize == 0) ? HAL_XSPI_DATA_NONE : Cmd.DataMode;
    Cmd.DataLength = DataSize;
    if (HAL_XSPI_Command(XSPIObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    if (DataSize != 0)
    {
        if (HAL_XSPI_Transmit(XSPIObject->XSPIHandle, Data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            goto Error;
        }
    }

    return NORFlash_XSPI_OK;

Error:
    HAL_XSPI_Abort(XSPIObject->XSPIHandle);
    return NORFlash_XSPI_ERROR;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_WaitStatusRegister(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint16_t MatchValue, uint16_t MatchMask, uint8_t DataSize, uint32_t Timeout)
{
    XSPI_RegularCmdTypeDef Cmd = XSPIObject->BaseCommand;
    XSPI_AutoPollingTypeDef Cfg = {0};

    Cmd.Instruction = (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS) ? (((uint16_t)Command << 8) | (uint8_t)(~Command & 0xFF)) : Command;
    Cmd.AddressMode = (Cmd.InstructionMode == HAL_XSPI_INSTRUCTION_8_LINES) ? HAL_XSPI_ADDRESS_8_LINES : HAL_XSPI_ADDRESS_NONE;
    Cmd.Address = 0x00000000;
    Cmd.DataLength = DataSize;
    Cmd.DummyCycles = (Cmd.InstructionMode == HAL_XSPI_INSTRUCTION_8_LINES) ? 4 : Cmd.DummyCycles;
    if (HAL_XSPI_Command(XSPIObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    Cfg.MatchValue = MatchValue;
    Cfg.MatchMask = MatchMask;
    Cfg.MatchMode = HAL_XSPI_MATCH_MODE_AND;
    Cfg.AutomaticStop = HAL_XSPI_AUTOMATIC_STOP_ENABLE;
    Cfg.IntervalTime = 0x10;
    if (HAL_XSPI_AutoPolling(XSPIObject->XSPIHandle, &Cfg, Timeout) != HAL_OK)
    {
        goto Error;
    }

    return NORFlash_XSPI_OK;

Error:
    HAL_XSPI_Abort(XSPIObject->XSPIHandle);
    return NORFlash_XSPI_ERROR;
}

NORFlash_XSPI_StatusTypeDef NORFlash_XSPI_Write(NORFlash_XSPI_ObjectTypeDef *XSPIObject, uint8_t Command, uint8_t Dummy, uint32_t Address, const uint8_t *Data, uint32_t DataSize)
{
    XSPI_RegularCmdTypeDef Cmd = XSPIObject->BaseCommand;

    Cmd.Instruction = (Cmd.InstructionWidth == HAL_XSPI_INSTRUCTION_16_BITS) ? (((uint16_t)Command << 8) | (uint8_t)(~Command & 0xFF)) : Command;
    Cmd.Address = Address;
    Cmd.DataLength = DataSize;
    Cmd.DummyCycles = Dummy;
    if (HAL_XSPI_Command(XSPIObject->XSPIHandle, &Cmd, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    if (HAL_XSPI_Transmit(XSPIObject->XSPIHandle, Data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        goto Error;
    }

    return NORFlash_XSPI_OK;

Error:
    HAL_XSPI_Abort(XSPIObject->XSPIHandle);
    return NORFlash_XSPI_ERROR;
}
