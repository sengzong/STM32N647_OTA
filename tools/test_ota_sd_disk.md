# SD hardware-port host test

Compiles the production SDMMC disk port with HAL behavior mocked at its boundary.
Checks the reference pin/clock setup, aligned single-sector bounce buffer for
unaligned readers, multi-sector addressing, failure status, card bounds, bounded
busy/read-budget timeout and disk ioctl values. Real hardware remains unverified.
