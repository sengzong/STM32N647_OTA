# Boot manager

One staging image at NOR 0x400000 and one previous-image backup at 0x1000000/0x1001000.
The active application stays at 0x70100400. A valid independent Agent is offered
before every ordinary application boot; successful installation goes straight to the app.

Before replacing the active application, save it and commit the backup header last.
Then set `rsv[0]=BM_REC_BACKUP_READY` in the pending record before erasing any active
application sectors. A reboot during the installation reuses this backup instead of
snapshotting a partially replaced application. A new request starts with this flag clear.

The copy engine accepts the unaligned +64 source and +1024 destination offsets and
erases the destination once before programming blocks. It rejects overlapping ranges.
Memory-mapped transitions are idempotent and boot-record writes are read back.
The real board NOR driver must override the weak ExtMem user-driver stubs.

Requests: TRIAL=1 (legacy), PERSIST=2 (normal), AGENT=6, ROLLBACK=7.
Standalone applications use PERSIST and require no application-side confirmation.
The independent receiver remains available for manual rollback or replacement.

The one-shot boot gate is at 0x34000000 in retained AXISRAM1. It is read after
cleaning/disabling D-cache. AXISRAM2 cannot hold this token across a ROM restart.

Shared layout constants select the original layout or a 10 MB application/model
layout. The reserved runtime prefix now stores a CRC-protected clock profile and
exact image length; this metadata is backed up and restored with model bytes.
Known lengths avoid including stale model data in a later small-image backup.
