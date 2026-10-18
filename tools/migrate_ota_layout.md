# Offline NOR layout migration

Requires a complete 32 MB dump of the old layout. Produces migration.hex and
migration.json without accessing hardware. Installs paired compatibility FSBL/Agent,
preserves the full first runtime sector while adding profile metadata, moves a
verified staged image and previous-image backup, and clears the old boot request.
Corrupt valid-looking headers/CRCs reject the migration instead of discarding data.

Known old OTA manager images are replaced with the compatibility manager, including
any such image in staging or rollback. Its old hardcoded flash addresses must never
be used on the new layout. This is explicitly reported in migration.json; the exact
original bytes remain in the input NOR dump. Review that report before programming.

Profile arguments use 0=current 800 MHz, 1=SDK 600 MHz, 2=SDK AI 800 MHz.
The caller must select profiles from the firmware's actual build requirements.
Preserve the input dump until hardware migration and rollback have been verified.
