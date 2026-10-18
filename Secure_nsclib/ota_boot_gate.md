# One-shot boot gate

`ota_boot_gate.h` defines the shared SRAM mailbox at 0x34000000, in the 80 KB
AXISRAM1 retention bank. AXISRAM2 at 0x341B0080 does not retain this token
reliably across a system reset and ROM loading.
FSBL puts WINDOW there before entering Agent on a normal reset. Agent consumes it,
waits 10 seconds for an update, and writes RUN_APP before a software reset if idle.
FSBL consumes RUN_APP and boots the application once. The following ordinary reset
again enters the independent receiver. An invalid application or explicit recovery
request starts Agent without WINDOW, so it waits indefinitely.

Magic plus a complemented mode reject uninitialised data. Application and Agent
RAM regions start at 0x34000100 so startup copying/zeroing leaves the token alone.
FSBL runs entirely in AXISRAM2 and consumes the token after disabling D-cache.
An arbitrary application may use SRAM1 after boot: FSBL has already consumed
RUN_APP before starting it. The next boot writes a fresh WINDOW token.
