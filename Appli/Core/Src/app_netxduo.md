# OTA receiver

One framed transfer per TCP connection, then disconnect and retry. NetX packet
chains are extracted through the packet API; transient one-second receive gaps
do not count as EOF. An idle timeout or truncated frame never programs flash.

Receive v3 containers with CRC32, stage one latest image, verify the bytes, then
program its validity magic without erasing again. Interrupted staging stays invalid.
The Agent queues PERSIST so standalone applications need no confirmation code.
The normal application exposes an installation UI for its staged image.

The SRAM programming code copy and execution are protected against thread
preemption, preserving the caller's interrupt state. An acknowledgement is sent
only after verified staging and, for Agent, a committed boot request.
The rollback command verifies the previous-image backup before queueing restore.
Display metadata is published after the complete description arrives. Progress
counts the received bytes and separately the verified flash bytes. Storage uses
a 4 KiB internal SRAM bounce buffer, restores XIP/interrupts between sectors and
yields for LVGL/network processing. Slot magic is committed after all sectors verify.

All resets use the internal SRAM executor: reset the NOR out of octal DTR mode,
restore ROM peripheral clocks per ES0620 2.2.22, then issue CMSIS SYSRESETREQ.
The boot-window token is in the SRAM1 retention bank, reserved by Agent startup.
