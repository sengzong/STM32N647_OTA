# OTA transport

Shared by `ota_gui.py` and `ota_send.py`. Uses only the Python standard library.

Firmware frame: five little-endian u32 fields: `OFW2` magic, version 3,
description length, image length, image CRC32; followed by UTF-8 description and image.
The board also accepts legacy version 2, which has no image CRC field.

Rollback command: four little-endian u32 fields: `OCM1` magic, command 1, zero, zero.

Acknowledgement: `OAK1` magic, signed status, staging index 0, image CRC32.
Status zero means verified storage and, in Agent mode, a queued installation.
For rollback it means a verified backup and queued restore; the CRC field is zero.
This does not attest that the subsequently booted application is healthy.

Senders use the length in the container header and keep the connection open while
waiting for verified storage acknowledgement (90 seconds, or 600 for large packages).
They do not send FIN before acknowledgement: a lengthy interrupt-masked NOR write
can otherwise delay the board's processing of FIN until the PC connection fails.
Missing acknowledgements, partial sends and mismatched CRCs fail the operation.
Both `.bin` and checksummed Intel HEX applications must target 0x70100400.

The sender submits the container header first and checks for early rejection.
The body uses at most 1024 bytes per write, TCP_NODELAY and a 1 ms interval,
matching the on-board diagnostic transfer that completed reception of the 4 MB
image. It reads replies during transmission, so negative acknowledgements
do not get lost behind a large PC-side send queue. A success reply before the
entire payload has been submitted is rejected. Fragmented acknowledgements still
require all 16 bytes and the expected CRC/staging index. Socket progress is the
PC-submitted byte count, not proof of board reception or verified NOR storage.
