# Reproducible build

`python tools/build_ota.py` compiles the local checkout with CubeIDE GCC and signs
FSBL with STM32 Signing Tool. Optional `--toolchain` and `--signer` override paths.
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
