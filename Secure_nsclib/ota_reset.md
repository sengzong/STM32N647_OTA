# STM32N6 reset preparation

`ota_reset.h` provides an inline helper for the SRAM executor. It enables HSI,
selects the Cortex-M55 power-on reset type and restores RCC_CCIPR6/7/8/13 to
their reset settings, as required by ES0620 section 2.2.22. Old clock sources
remain enabled throughout the transition. It must run from internal SRAM with
interrupts disabled and must be followed by a reset without returning to XIP.

The NOR executor first exits memory-mapped mode, sends the MX25UM25645G octal
reset-enable/reset commands (0x6699/0x9966), waits for reset recovery, and disables
XSPI2. ROM can then read the FSBL using the flash's default SPI protocol.
CMSIS selects the SCS view appropriate to the build when issuing SYSRESETREQ.

References:
- https://www.st.com/resource/en/errata_sheet/es0620-stm32n6xxxx-device-errata-stmicroelectronics.pdf
- https://community.st.com/stm32-mcus-products-25/stm32n6-retain-ram-contents-across-resets-for-ota-firmware-updates-152296
