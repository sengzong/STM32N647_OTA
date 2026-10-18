# Migration regression

Builds a synthetic full old-layout NOR dump, then checks current-sector vector
preservation, relocated staging, previous application/model/profile preservation,
recomputed backup CRC, cleared boot request, and HEX serialization. Corrupt
staging/backup CRCs and incomplete dumps must reject migration.
Synthetic HEX output is for host testing only and must not be programmed.
