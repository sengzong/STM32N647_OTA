# Read-only UI asset filesystem

FatFs R0.15a (revision 5380), copied without changes from the user's software
package Middlewares/FATFS/FatFs/source. Copyright and redistribution terms are
preserved in each upstream source file. Configuration is in ffconf.h.

One volume, FAT16/FAT32/exFAT, bounded stack-based long-file-name workspace,
512-byte sectors, no formatting, writes, directory changes or filesystem locks.
The display thread alone owns this filesystem during startup asset loading.
The FAT driver is unrelated to an SDK application's filesystem after boot.

The HAL disk port follows Projects/38_SD_Card: SDMMC1 clock from HCLK, PC8-PC12
and PH2, four-bit mode, divider 4. New driver files come from the same software
package's STM32Cube_FW_N6_V1.0.0/Drivers/STM32N6xx_HAL_Driver, not a new download.
