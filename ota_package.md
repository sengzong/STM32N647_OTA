# SDK application/model packages

`ota_package.py` merges application HEX at 0x70100400 and matching model HEX
at 0x70200000 or higher. Absolute addresses and sparse gaps are preserved.
FSBL and out-of-range addresses are rejected. The result is a version 4 OFW2
frame with a boot profile: 0=current/default, 1=SDK 600 MHz, 2=SDK AI 800 MHz.
The .ota file can be sent by ota_gui.py or ota_send.py after installing the
matching compatibility-layout loader and receiver.

Image CRC covers the entire application, erased padding and model payload.
Rollback copies the runtime prefix, application, padding and model together.
