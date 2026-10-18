# STM32N6 signed FSBL layout

STM32CubeProgrammer 2.21 and later no longer add 0x400 payload alignment by default.
The STM32N6 signing command must include `-align`. With this project's linker script,
the signed image is stored at NOR 0x70000000 and its vector table is at +0x400.
Boot ROM loads this image into AXISRAM2; the FSBL payload is linked to 0x34180400.

`verify_fsbl_image.py` checks the complete payload at offset 0x400, the header's
entry point against the actual vector reset word, version, length, checksum and
stack address before a build publishes any HEX file.

Run:

```
python tools/verify_fsbl_image.py --raw Build/ota/LED_2_FSBL/LED_2_FSBL.bin --signed Build/ota/LED_2_FSBL/LED_2_FSBL-trusted.bin
```

Reference: https://community.st.com/t5/stm32cubeprogrammer-mcus/signingtool-for-stm32n6-in-stm32cubeprogrammer-v2-21-0/td-p/859154
