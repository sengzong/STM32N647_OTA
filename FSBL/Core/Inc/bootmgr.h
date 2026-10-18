#ifndef __BOOTMGR_H
#define __BOOTMGR_H

/* ============================ FSBL boot manager ============================
 * Copy + confirm / rollback (MCUboot style) for a single-slot boot area,
 * plus a rescue jump into a resident OTA agent partition.
 *
 * The FSBL runs from AXISRAM2 (not from NOR), so it may freely abort the
 * memory-mapped mode of XSPI2 and erase / program the NOR even while the
 * boot area holds the firmware it is about to boot.
 *
 * This header is the bootloader-side copy of the BOOTREC protocol. The
 * application-side copy lives in Appli/Core/Inc/app_ota_flash.h and the two
 * MUST stay byte-compatible (magic, state values, 32-byte layout, CRC range).
 *
 * ------------------------------- NOR layout -------------------------------
 *   offset      size     content
 *   0x000000    1MB      FSBL image (SWD upgrade only, never touched here)
 *   0x0FC000    4KB      BOOTREC sector: one 32-byte record, sector erase first
 *   0x100000    3MB      boot area: [0x400 reserved header | app image]
 *                        app entry = 0x70100400
 *   0x400000    4MB      latest staged image: [64B "SLT1" header | image]
 *   0x800000/0xC00000   former slots, no longer used
 *   0x1000000   4KB      previous-application backup header (64B)
 *   0x1001000   4MB      previous-application backup data
 *   0x1400000   4MB      resident OTA agent: [0x400 reserved | image]
 *                        agent entry = 0x71400400
 *
 * ---------------------------- power-fail safety ----------------------------
 * A pending record receives BACKUP_READY after the previous image is saved and
 * before the active image is erased. A reset retries the copy without replacing
 * that backup. The final CONFIRMED/TRIAL_BOOTED state follows read-back CRC.
 * ========================================================================== */

#include <stdint.h>
#include "../../../Secure_nsclib/ota_layout.h"

/* ------------------------------ NOR layout ------------------------------ */
#define BM_NOR_BASE            0x70000000u  /* XSPI2 XIP window base          */
#define BM_SECTOR              0x1000u      /* NOR erase sector size          */
#define BM_PAGE                0x100u       /* NOR program page size          */
#define BM_BLK                 0x1000u      /* copy granularity (one sector)  */

#define BM_BOOTREC_OFFSET      0x0FC000u
#define BM_BOOTREC_SIZE        32u

#define BM_APP_OFFSET          0x100000u    /* boot area                      */
#define BM_APP_HEADER          0x400u       /* reserved header before image   */
#define BM_APP_MAX_LEN         OTA_LAYOUT_IMAGE_MAX
#define BM_BOOT_AREA_LIMIT     OTA_LAYOUT_ACTIVE_END

#define BM_SLOT_BASE           OTA_LAYOUT_SLOT_BASE
#define BM_SLOT_SPACING        OTA_LAYOUT_SLOT_SIZE
#define BM_SLOT_COUNT          1u
#define BM_SLOT_HEADER         64u
#define BM_SLOT_MAGIC          0x31544C53u  /* "SLT1"                         */

#define BM_BACKUP_OFFSET       OTA_LAYOUT_BACKUP_HEADER
#define BM_BACKUP_DATA         OTA_LAYOUT_BACKUP_DATA
#define BM_BACKUP_MAGIC        0x314B4247u  /* "GBAK1"                        */
#define BM_BACKUP_MAX_LEN      OTA_LAYOUT_BACKUP_MAX

#define BM_AGENT_OFFSET        OTA_LAYOUT_AGENT
#define BM_AGENT_HEADER        0x400u
#define BM_AGENT_MAGIC         0x3147414Fu  /* "OAG1" marker (informational)  */

/* --------------------------- BOOTREC states -----------------------------
 * Values are written by the application, EXCEPT TRIAL_BOOTED / ROLLED_BACK
 * which are written by the FSBL. Keep in sync with app_ota_flash.h. */
#define BM_ST_IDLE             0u   /* nothing pending                        */
#define BM_ST_REQ_TRIAL        1u   /* app: switch to slot, boot as trial     */
#define BM_ST_REQ_PERSIST      2u   /* app: switch to slot, accept right away */
#define BM_ST_TRIAL_BOOTED     3u   /* fsbl: trial was jumped to, awaiting ack*/
#define BM_ST_CONFIRMED        4u   /* app: trial accepted, keep it           */
#define BM_ST_ROLLED_BACK      5u   /* fsbl: restored the golden backup       */
#define BM_ST_REQ_AGENT        6u   /* app: boot the resident OTA agent       */
#define BM_ST_REQ_ROLLBACK     7u   /* receiver: restore the previous image  */
#define BM_REC_BACKUP_READY    0x31504B42u /* rsv[0]: snapshot committed for this request */

#define BM_REC_MAGIC           0x314D424Fu  /* "OBM1"                         */

/* 32 bytes, naturally aligned, no padding.
 * crc32 sits at offset 28 and covers bytes [0, 28) exactly. */
typedef struct {
    uint32_t magic;      /*  0: BM_REC_MAGIC                                  */
    uint32_t state;      /*  4: BM_ST_*                                       */
    uint32_t slot;       /*  8: target slot index                             */
    uint32_t seq;        /* 12: monotonically increasing counter              */
    uint32_t img_len;    /* 16: image length in bytes                         */
    uint32_t rsv[2];     /* 20: rsv[0]=BACKUP_READY while a copy is pending   */
    uint32_t crc32;      /* 28: CRC32 over bytes [0, 28)                      */
} BootRec;

/* Slot header written by the application; only the fields the boot manager
 * needs are named here (64 bytes total, see OtaSlotHeader). */
typedef struct {
    uint32_t magic;      /* BM_SLOT_MAGIC                                     */
    uint32_t img_len;
    uint32_t desc_len;
    uint32_t crc32;      /* CRC32 over the image that follows the header      */
    uint8_t  desc[32];
    uint8_t  rsv[16];
} BmSlotHeader;

/* Golden backup header: one 64-byte header in its own sector. */
typedef struct {
    uint32_t magic;      /* BM_BACKUP_MAGIC                                   */
    uint32_t len;        /* bytes copied, includes the boot area 0x400 header */
    uint32_t crc32;      /* CRC32 over the backup data region                 */
    uint8_t  rsv[52];
} BmBackupHeader;

/* ------------------------------ entry point -----------------------------
 * Called from main() after MX_EXTMEM_MANAGER_Init() and the HyperRAM setup,
 * right before BOOT_Application().
 *
 * Return 0  -> the caller should proceed with BOOT_Application() (normal boot
 *              of the boot area, which is the legacy behaviour).
 * Never returns when the boot manager jumps into the resident OTA agent.
 */
int BOOTMGR_Process(void);
uint32_t BOOTMGR_GetProfile(void);

#endif /* __BOOTMGR_H */
