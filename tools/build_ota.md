# Reproducible build

`python tools/build_ota.py` compiles the local checkout with CubeIDE GCC and signs
FSBL with STM32 Signing Tool. Optional `--toolchain` and `--signer` override paths.
The LCD receiver requires official LVGL v8.4.0 in Middlewares/LVGL, with lvgl.h
and src directly inside it. Appli/Core/Inc/lv_conf.h selects the CPU RGB565
renderer, internal 96 KB heap and enabled widgets/fonts. Both application and
Agent link the same LVGL/status/display sources.
Both images also link the local read-only FatFs port and SD HAL/LL drivers copied
from the user's SDK. Generate the optional SD font/theme folder with
`python tools/make_ota_assets.py`. Cache location and internal/mailbox/stack bounds
are checked during linking; the large font does not occupy NOR.
No board programming occurs. Outputs are under `Build/ota/` and published to `Binary/`.
All three images are rebuilt together. A build rejects weak NOR write/map stubs
in FSBL and application-only confirmation functions in Agent.
The SRAM executor must fit its 16 KB copied frame and contain no branch-with-link
instructions to external code. Disassembly is saved as `sram-executor.asm`.

`--compat-package` builds the shared code with LED2_PACKAGE_COMPAT, relocates
the receiver to 0x71F00400, and publishes separately to Binary/compat. The initial
installation HEX includes the three images, runtime profile/length metadata and
cleared new staging/backup/boot-request headers. It is a layout migration artifact
and must be reviewed with SDK_COMPAT_GUIDE.md before programming.

The same build publishes Binary/compat/management_update.hex for a direct SWD
management-application update. It contains only the application plus its 0x400-byte
runtime prefix at 0x70100000 (default profile, current length/CRC and header CRC).
Updating both prevents an older runtime length from truncating the next rollback
backup. It leaves FSBL, Agent, staged/rollback partitions and boot-request records
untouched. Raw appli.hex still starts at 0x70100400 for OTA sender compatibility.
