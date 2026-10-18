# OTA layout and clock profiles

Default builds retain the existing 512 KB receiver and NOR partition addresses.
`LED2_PACKAGE_COMPAT` builds reserve 10 MB for the receive buffer in PSRAM and
use this 32 MB NOR layout:

| NOR offsets | Purpose |
|---|---|
| 0..0x100000 | FSBL, including boot record at 0xFC000 |
| 0x100000..0xB00000 | Runtime application/model, vector at +0x400 |
| 0xB00000..0x1500000 | One staged latest application/model |
| 0x1500000..0x1F00000 | One previous application/model, header sector first |
| 0x1F00000..0x2000000 | Independent receiver, vector at +0x400 |

The final runtime sector is reserved so an aligned snapshot and its separate
backup header fit inside the backup area. Maximum image length is 0x9FEC00.
The existing SDK's largest model end address 0x70AB7351 fits this bound.

Profiles 1 and 2 match the SDK common and AI FSBL clock trees. A CRC-protected
runtime header stores the profile and exact image length in the existing +0x400
prefix. Backup/restoration includes that prefix. The receiver always runs in
the initial 800 MHz profile; the final application profile is applied from SRAM.
