# Boot record and layout

One latest-image staging slot and one separate previous-image backup are used.
`BM_ST_REQ_ROLLBACK=7` requests an explicit restore. `rsv[0]` holds
`BM_REC_BACKUP_READY` only while a pending installation has saved its rollback data.
Application writes start with this flag clear. Existing 32-byte record size,
magic and CRC coverage stay compatible.
