# SDMMC1 disk port

Based on the user's Projects/38_SD_Card: HCLK source, four-bit bus, ClockDiv=4,
PC12 clock, PH2 command and PC8-PC11 data, AF10. Secure RIF/GPIO attributes are
configured before HAL initialization. Uses polled block reads through an aligned
internal 512-byte bounce buffer; caller alignment and PSRAM/DMA access are avoided.

Read/state failures mark the disk uninitialized. Block reads/state waits use
100 ms limits, a 2 s loading budget prevents repeated bad reads, and
SDMMC_SWDATATIMEOUT=250 in the HAL configuration replaces the upstream infinite
software data timeout. A missing card never enters Error_Handler or a retry loop.
No card-write function or formatting entry point is compiled.

Host HAL simulation checks pin/clock values, unaligned callers, bounds, faults and
timeout exits; these checks do not replace actual SDMMC/card electrical validation.
