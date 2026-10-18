# OTA regression tests

`python tools/test_ota.py` requires the local Windows host GCC and Python.
It compiles production boot-manager code and extracted production receiver/storage
functions against a memory-backed NOR model. Only the hardware jump and reset are
replaced with test control transfers. The model enforces erase-before-program,
sector alignment, mapping transitions and one-to-zero NOR programming.

Cases include six updates through the same staging area, refreshed previous-image
backups, manual and trial rollback, reset during boot copy, interrupted slot commit,
boot-window timeout and bypass, malformed/fragmented frames and missing acknowledgements.
Boot-window tests clear AXISRAM2 on reset and require the SRAM1 token to survive.
Python socket tests exercise the actual sender protocol and partial acknowledgements.
They also cover a large package, header/body rejection, premature success, bounded
send progress and keeping the connection open until verified storage acknowledgement.
