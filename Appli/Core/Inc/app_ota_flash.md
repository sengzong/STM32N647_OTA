# OTA public interface

`OTA_SLOT_COUNT=1` exposes only the latest staged image. Previous-image backup
uses its dedicated header/data area and is managed by FSBL. State 7 requests
rollback. Normal installations request PERSIST, allowing applications without
any OTA or confirmation code. Existing trial confirmation is retained for old records.

`OTA_F_WRITE_ONLY` commits the staged validity magic after verified programming,
without erasing the payload. Agent builds expose startup-window polling and
exclude application-only confirmation functions.
