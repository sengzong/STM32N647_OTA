/* bootmgr.c -- FSBL boot manager: copy + confirm/rollback + agent rescue.
 *
 * Runs from AXISRAM2, therefore it can abort the XSPI2 memory-mapped mode and
 * erase / program the NOR while the boot area holds the very firmware it is
 * about to boot. That is the whole reason this code lives in the FSBL and not
 * in the application.
 *
 * Copy engine, one 4 KB block at a time:
 *      enable MM  -> read source block into RAM
 *      disable MM -> erase + program destination block
 *      enable MM  -> next block
 * The memory-mapped mode is re-enabled after every block because the *next*
 * source block has to be read through XIP again.
 *
 * Failure policy: never brick. Every failure path clears the BOOTREC record,
 * tries to restore the golden backup, and lets the caller fall through to the
 * legacy BOOT_Application() path. If the boot area itself is unusable and a
 * valid OTA agent partition exists, control is handed to the agent instead.
 *
 * NOTE: source comments are ASCII-only on purpose (GBK console host).
 */

#include "bootmgr.h"
#include "main.h"
#include "stm32_extmem.h"
#include "stm32_extmem_conf.h"
#include <string.h>
#include "../../../Secure_nsclib/ota_boot_gate.h"

/* =============================== logging ================================ */
/* Polled USART1 (PE5 TX / PE6 RX, AF7, 115200) - same pins and baud rate as
 * the application, so a single terminal session covers FSBL and app alike. */
#ifndef BM_LOG_ENABLE
#define BM_LOG_ENABLE 1
#endif

#if (BM_LOG_ENABLE == 1)

UART_HandleTypeDef huart1;

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef          gpio = {0};
    RCC_PeriphCLKInitTypeDef  pclk = {0};

    if (huart->Instance == USART1)
    {
        pclk.PeriphClockSelection   = RCC_PERIPHCLK_USART1;
        pclk.Usart1ClockSelection   = RCC_USART1CLKSOURCE_CLKP;
        (void)HAL_RCCEx_PeriphCLKConfig(&pclk);

        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOE_CLK_ENABLE();

        gpio.Pin       = GPIO_PIN_5 | GPIO_PIN_6;
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Pull      = GPIO_NOPULL;
        gpio.Speed     = GPIO_SPEED_FREQ_LOW;
        gpio.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOE, &gpio);
    }
}

static void bm_uart_init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling  = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.Init.ClockPrescaler  = UART_PRESCALER_DIV1;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    (void)HAL_UART_Init(&huart1);
}

static void bm_tx(const char *s, uint32_t n)
{
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)(uintptr_t)s, (uint16_t)n, 2000u);
}

static void bm_puts(const char *s)
{
    uint32_t n = 0u;
    while (s[n] != '\0') { n++; }
    if (n != 0u) { bm_tx(s, n); }
}

static void bm_dec(uint32_t v)
{
    char b[12];
    int32_t i = 11;
    b[11] = '\0';
    if (v == 0u) { b[10] = '0'; bm_tx(&b[10], 1u); return; }
    while ((v != 0u) && (i > 0))
    {
        i--;
        b[i] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    bm_tx(&b[i], (uint32_t)(11 - i));
}

static void bm_hex(uint32_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    char b[8];
    int32_t i;
    bm_puts("0x");
    for (i = 7; i >= 0; i--) { b[i] = hx[v & 0xFu]; v >>= 4; }
    bm_tx(b, 8u);
}

#else
#define bm_uart_init()      do { } while (0)
#define bm_puts(s)          do { (void)(s); } while (0)
#define bm_dec(v)           do { (void)(v); } while (0)
#define bm_hex(v)           do { (void)(v); } while (0)
#endif /* BM_LOG_ENABLE */

/* ============================== internals =============================== */
static uint32_t bm_base = BM_NOR_BASE;
static int bm_mapped;
static uint8_t  bm_buf[BM_BLK] __attribute__((aligned(32)));

typedef void (*bm_fn_t)(void);

/* ------------------------- raw NOR access helpers ----------------------- */
static int bm_mm(int enable)
{
    enable = (enable != 0);
    if (bm_mapped == enable) { return 0; }
    if (EXTMEM_MemoryMappedMode(EXTMEMORY_1,
                               enable ? EXTMEM_ENABLE : EXTMEM_DISABLE) != EXTMEM_OK) { return -1; }
    bm_mapped = enable;
    return 0;
}

static int bm_erase(uint32_t off, uint32_t len)
{
    uint32_t start = off & ~(BM_SECTOR - 1u);
    uint32_t end   = (off + len + BM_SECTOR - 1u) & ~(BM_SECTOR - 1u);

    if (end <= start) { return -1; }
    return (EXTMEM_EraseSector(EXTMEMORY_1, start, end - start) == EXTMEM_OK) ? 0 : -1;
}

static int bm_prog(uint32_t off, const uint8_t *data, uint32_t len)
{
    return (EXTMEM_Write(EXTMEMORY_1, off, data, len) == EXTMEM_OK) ? 0 : -1;
}

/* XIP read: requires the memory-mapped mode to be enabled */
static void bm_xip_rd(uint32_t off, uint8_t *dst, uint32_t len)
{
    memcpy(dst, (const void *)(uintptr_t)(bm_base + off), len);
}

/* ------------------------------- CRC32 ---------------------------------- */
/* Identical implementation to ota_crc32() on the application side
 * (poly 0xEDB88320, reflected, init/xorout 0xFFFFFFFF). */
static uint32_t bm_crc32(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i, k;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint32_t)p[i];
        for (k = 0u; k < 8u; k++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

/* Streaming CRC over a NOR range, read back through XIP in 4 KB blocks */
static uint32_t bm_crc32_xip(uint32_t off, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t done = 0u;

    while (done < len)
    {
        uint32_t n = ((len - done) > BM_BLK) ? BM_BLK : (len - done);
        uint32_t i, k;

        bm_xip_rd(off + done, bm_buf, n);
        for (i = 0u; i < n; i++)
        {
            crc ^= (uint32_t)bm_buf[i];
            for (k = 0u; k < 8u; k++)
            {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
        done += n;
    }
    return ~crc;
}

/* --------------------------- BOOTREC handling --------------------------- */
typedef struct {
    uint32_t magic, profile, image_len, image_crc, reserved[3], crc;
} BmRuntimeHeader;

static int bm_runtime_read(BmRuntimeHeader *h)
{
    if (bm_mm(1) != 0) return -1;
    bm_xip_rd(BM_APP_OFFSET, (uint8_t *)h, sizeof(*h));
    if (h->magic != OTA_RUNTIME_MAGIC || h->profile > OTA_PROFILE_AI ||
        h->image_len < 8u || h->image_len > BM_APP_MAX_LEN ||
        h->crc != bm_crc32((const uint8_t *)h, 28u)) return -1;
    return 0;
}

uint32_t BOOTMGR_GetProfile(void)
{
    BmRuntimeHeader h;
    int mapped = bm_mapped;
    uint32_t profile = (bm_runtime_read(&h) == 0) ? h.profile : OTA_PROFILE_DEFAULT;
    if (!mapped) (void)bm_mm(0);
    return profile;
}

static int bm_runtime_write(uint32_t profile, uint32_t len, uint32_t image_crc)
{
    BmRuntimeHeader h;
    memset(&h, 0, sizeof(h));
    h.magic = OTA_RUNTIME_MAGIC; h.profile = profile;
    h.image_len = len; h.image_crc = image_crc;
    h.crc = bm_crc32((const uint8_t *)&h, 28u);
    /* bm_copy already erased this reserved prefix with the first image sector. */
    if (bm_mm(0) != 0) return -1;
    if (bm_prog(BM_APP_OFFSET, (const uint8_t *)&h, sizeof(h)) != 0) { (void)bm_mm(1); return -1; }
    if (bm_mm(1) != 0) return -1;
    BmRuntimeHeader check;
    return bm_runtime_read(&check) == 0 && memcmp(&h, &check, sizeof(h)) == 0 ? 0 : -1;
}

static int bm_read_rec(BootRec *rec)
{
    BootRec tmp;

    if (bm_mm(1) != 0) { return -1; }
    bm_xip_rd(BM_BOOTREC_OFFSET, (uint8_t *)&tmp, BM_BOOTREC_SIZE);

    if (tmp.magic != BM_REC_MAGIC) { return -2; }
    /* A half-written record fails this test and is treated as "no record". */
    if (bm_crc32((const uint8_t *)&tmp, 28u) != tmp.crc32) { return -3; }

    *rec = tmp;
    return 0;
}

static int bm_write_rec_flags(uint32_t state, uint32_t slot, uint32_t seq,
                              uint32_t img_len, uint32_t flags)
{
    BootRec r;

    memset(&r, 0, sizeof(r));
    r.magic   = BM_REC_MAGIC;
    r.state   = state;
    r.slot    = slot;
    r.seq     = seq;
    r.img_len = img_len;
    r.rsv[0]  = flags;
    r.crc32   = bm_crc32((const uint8_t *)&r, 28u);

    if (bm_mm(0) != 0) { return -1; }
    if (bm_erase(BM_BOOTREC_OFFSET, BM_BOOTREC_SIZE) != 0) { (void)bm_mm(1); return -2; }
    if (bm_prog(BM_BOOTREC_OFFSET, (const uint8_t *)&r, BM_BOOTREC_SIZE) != 0) { (void)bm_mm(1); return -3; }
    if (bm_mm(1) != 0) { return -4; }
    {
        BootRec check;
        bm_xip_rd(BM_BOOTREC_OFFSET, (uint8_t *)&check, BM_BOOTREC_SIZE);
        if (memcmp(&r, &check, sizeof(r)) != 0) { return -5; }
    }
    return 0;
}

static int bm_write_rec(uint32_t state, uint32_t slot, uint32_t seq, uint32_t img_len)
{
    return bm_write_rec_flags(state, slot, seq, img_len, 0u);
}

static int bm_clear_rec(void)
{
    int rc;

    if (bm_mm(0) != 0) { return -1; }
    rc = bm_erase(BM_BOOTREC_OFFSET, BM_BOOTREC_SIZE);
    (void)bm_mm(1);
    return rc;
}

/* ---------------------------- image sanity ------------------------------ */
/* A NOR offset to a candidate vector table; checks the reset vector pattern
 * without trusting anything inside the image. */
static int bm_image_sane(uint32_t off)
{
    uint32_t v[2];

    if (bm_mm(1) != 0) { return 0; }
    bm_xip_rd(off, (uint8_t *)v, 8u);

    if ((v[0] < 0x34000000u) || (v[0] > 0x34200000u)) { return 0; }  /* MSP  */
    if ((v[0] & 7u) != 0u || (v[1] & 1u) == 0u) { return 0; }
    if (off == BM_AGENT_OFFSET + BM_AGENT_HEADER)
    {
        if (v[1] < BM_NOR_BASE + off || v[1] >= BM_NOR_BASE + BM_AGENT_OFFSET + OTA_LAYOUT_AGENT_SIZE) { return 0; }
    }
    else if (v[1] < BM_NOR_BASE + BM_APP_OFFSET + BM_APP_HEADER ||
             v[1] >= BM_NOR_BASE + BM_BOOT_AREA_LIMIT) { return 0; }
    return 1;
}

/* ------------------------------ block copy ------------------------------ */
/* Erase the destination once before copying. Slot data starts at +64 and the
 * application at +1024, so neither address is sector aligned. Per-block erase
 * would erase bytes written by the preceding block. Leaves MM enabled. */
static int bm_copy(uint32_t src, uint32_t dst, uint32_t len)
{
    uint32_t done = 0u;

    if (len == 0u || src >= 0x2000000u || dst >= 0x2000000u ||
        len > 0x2000000u - src || len > 0x2000000u - dst) { return -1; }
    if ((src < ((dst + len + BM_SECTOR - 1u) & ~(BM_SECTOR - 1u))) &&
        (src + len > (dst & ~(BM_SECTOR - 1u)))) { return -1; }
    if (bm_mm(0) != 0) { return -3; }
    if (bm_erase(dst, len) != 0) { (void)bm_mm(1); return -4; }

    while (done < len)
    {
        uint32_t n = ((len - done) > BM_BLK) ? BM_BLK : (len - done);

        if (bm_mm(1) != 0) { return -2; }
        bm_xip_rd(src + done, bm_buf, n);

        if (bm_mm(0) != 0) { return -3; }
        if (bm_prog(dst + done, bm_buf, n) != 0) { (void)bm_mm(1); return -5; }

        done += n;
    }
    return bm_mm(1);
}

/* ---------------------------- golden backup ----------------------------- */
static int bm_backup_load(BmBackupHeader *bh)
{
    if (bm_mm(1) != 0) { return -1; }
    bm_xip_rd(BM_BACKUP_OFFSET, (uint8_t *)bh, (uint32_t)sizeof(BmBackupHeader));

    if (bh->magic != BM_BACKUP_MAGIC) { return -2; }
    if ((bh->len <= BM_APP_HEADER) || (bh->len > BM_BACKUP_MAX_LEN)) { return -3; }
    if ((bh->len % BM_SECTOR) != 0u) { return -4; }
    return 0;
}

/* Save the currently running image before each new switch. The pending request
 * gets BACKUP_READY before any boot-area erase, so a reset during the copy can
 * never replace this backup with the half-written destination. */
static int bm_backup_create(BmBackupHeader *bh)
{
    BmBackupHeader h;
    uint32_t off, last = 0u, len, crc;
    BmRuntimeHeader active;

    if (bm_mm(1) != 0) { return -1; }

    if (bm_runtime_read(&active) == 0)
    {
        last = BM_APP_OFFSET + ((BM_APP_HEADER + active.image_len - 1u) & ~(BM_SECTOR - 1u));
    }
    else for (off = BM_APP_OFFSET; off < BM_BOOT_AREA_LIMIT; off += BM_SECTOR)
    {
        uint32_t i;
        int used = 0;

        bm_xip_rd(off, bm_buf, BM_SECTOR);
        for (i = 0u; i < BM_SECTOR; i++)
        {
            if (bm_buf[i] != 0xFFu) { used = 1; break; }
        }
        if (used != 0) { last = off; }
    }

    if (last < BM_APP_OFFSET) { return -2; }               /* boot area blank */
    len = (last - BM_APP_OFFSET) + BM_SECTOR;
    if (len > BM_BACKUP_MAX_LEN) return -2;

    bm_puts("[BM] backup create len=");
    bm_hex(len);
    bm_puts("\r\n");

    /* Invalidate the old header before replacing its data. The current boot
     * area is still intact, so a reset during this step can retry the snapshot. */
    if (bm_mm(0) != 0) { return -3; }
    if (bm_erase(BM_BACKUP_OFFSET, (uint32_t)sizeof(h)) != 0) { (void)bm_mm(1); return -3; }
    if (bm_copy(BM_APP_OFFSET, BM_BACKUP_DATA, len) != 0) { return -3; }

    crc = bm_crc32_xip(BM_BACKUP_DATA, len);
    if (bm_crc32_xip(BM_APP_OFFSET, len) != crc) { return -4; }   /* read-back */

    memset(&h, 0, sizeof(h));
    h.magic = BM_BACKUP_MAGIC;
    h.len   = len;
    h.crc32 = crc;

    if (bm_mm(0) != 0) { return -5; }
    if (bm_erase(BM_BACKUP_OFFSET, (uint32_t)sizeof(h)) != 0) { (void)bm_mm(1); return -6; }
    if (bm_prog(BM_BACKUP_OFFSET, (const uint8_t *)&h, (uint32_t)sizeof(h)) != 0) { (void)bm_mm(1); return -7; }
    (void)bm_mm(1);

    *bh = h;
    return 0;
}

/* Verify the backup CRC first, then copy it over the boot area. */
static int bm_backup_restore(const BmBackupHeader *bh)
{
    if (bh->len == 0u) { return -1; }
    if (bm_mm(1) != 0) { return -2; }

    if (bm_crc32_xip(BM_BACKUP_DATA, bh->len) != bh->crc32) { return -3; }
    if (bm_copy(BM_BACKUP_DATA, BM_APP_OFFSET, bh->len) != 0) { return -4; }
    if (bm_crc32_xip(BM_APP_OFFSET, bh->len) != bh->crc32) { return -5; }
    return 0;
}

/* ------------------------------ switch action --------------------------- */
/* step 1/3: validate the slot, snapshot the boot area, copy the slot image
 * step 2/3: read the boot area back and compare the CRC
 * step 3/3: record TRIAL_BOOTED (trial) or CONFIRMED (persist) */
static int bm_do_switch(const BootRec *rec, int persist)
{
    BmSlotHeader   sh;
    BmBackupHeader bh;
    uint32_t       slot_off, img_len, crc, crc2, profile;
    int            rc;

    bm_puts("[BM] switch req: state=");
    bm_dec(rec->state);
    bm_puts(" slot=");
    bm_dec(rec->slot);
    bm_puts("\r\n");

    if (rec->slot >= BM_SLOT_COUNT) { bm_puts("[BM] FAIL bad slot\r\n"); (void)bm_clear_rec(); return -1; }

    slot_off = BM_SLOT_BASE + (rec->slot * BM_SLOT_SPACING);

    if (bm_mm(1) != 0) { return -1; }
    bm_xip_rd(slot_off, (uint8_t *)&sh, BM_SLOT_HEADER);

    if (sh.magic != BM_SLOT_MAGIC)
    {
        bm_puts("[BM] FAIL slot magic\r\n");
        (void)bm_clear_rec();       /* keep booting whatever is in the boot area */
        return -2;
    }

    img_len = sh.img_len;
    memcpy(&profile, sh.rsv, sizeof(profile));
    if (profile > OTA_PROFILE_AI) { (void)bm_clear_rec(); return -3; }
    if ((img_len < 8u) || (img_len > BM_APP_MAX_LEN))
    {
        bm_puts("[BM] FAIL slot len=");
        bm_hex(img_len);
        bm_puts("\r\n");
        (void)bm_clear_rec();
        return -3;
    }

    bm_puts("[BM] slot img_len=");
    bm_hex(img_len);
    bm_puts("\r\n");

    /* Never touch the boot area before the source image is proven intact. */
    crc = bm_crc32_xip(slot_off + BM_SLOT_HEADER, img_len);
    if (crc != sh.crc32)
    {
        bm_puts("[BM] FAIL slot crc=");
        bm_hex(crc);
        bm_puts(" exp=");
        bm_hex(sh.crc32);
        bm_puts("\r\n");
        (void)bm_clear_rec();
        return -4;
    }
    if (!bm_image_sane(slot_off + BM_SLOT_HEADER))
    {
        bm_puts("[BM] FAIL slot vectors\r\n");
        (void)bm_clear_rec();
        return -4;
    }

    if (rec->rsv[0] != BM_REC_BACKUP_READY)
    {
        if (!bm_image_sane(BM_APP_OFFSET + BM_APP_HEADER))
        {
            /* No valid application to save. A recovery upload can still fix it. */
            bm_puts("[BM] no running image to back up\r\n");
        }
        else
        {
            rc = bm_backup_create(&bh);
            if (rc != 0)
            {
                bm_puts("[BM] FAIL backup rc=");
                bm_dec((uint32_t)(0 - rc));
                bm_puts("\r\n");
                (void)bm_clear_rec();
                return -5;
            }
        }
        if (bm_write_rec_flags(rec->state, rec->slot, rec->seq, img_len,
                                BM_REC_BACKUP_READY) != 0) { return -5; }
    }
    else
    {
        bm_puts("[BM] resuming copy, keeping previous-image backup\r\n");
    }
    /* A recovery upload need not have any backup. Guard error recovery below. */
    if (bm_backup_load(&bh) != 0) { memset(&bh, 0, sizeof(bh)); }

    bm_puts("[BM] copy slot -> boot area\r\n");
    rc = bm_copy(slot_off + BM_SLOT_HEADER, BM_APP_OFFSET + BM_APP_HEADER, img_len);
    if (rc != 0)
    {
        bm_puts("[BM] FAIL copy rc=");
        bm_dec((uint32_t)(0 - rc));
        bm_puts(" restoring backup\r\n");
        (void)bm_backup_restore(&bh);
        (void)bm_clear_rec();
        return -6;
    }

    crc2 = bm_crc32_xip(BM_APP_OFFSET + BM_APP_HEADER, img_len);
    if (crc2 != crc || bm_runtime_write(profile, img_len, crc) != 0)
    {
        bm_puts("[BM] FAIL verify crc=");
        bm_hex(crc2);
        bm_puts(" exp=");
        bm_hex(crc);
        bm_puts(" restoring backup\r\n");
        (void)bm_backup_restore(&bh);
        (void)bm_clear_rec();
        return -7;
    }

    rc = bm_write_rec(persist ? BM_ST_CONFIRMED : BM_ST_TRIAL_BOOTED,
                      rec->slot, rec->seq + 1u, img_len);
    if (rc != 0)
    {
        bm_puts("[BM] FAIL rec write rc=");
        bm_dec((uint32_t)(0 - rc));
        bm_puts(" restoring backup\r\n");
        (void)bm_backup_restore(&bh);
        (void)bm_clear_rec();
        return -8;
    }

    bm_puts(persist ? "[BM] CONFIRMED (persist)\r\n" : "[BM] TRIAL_BOOTED\r\n");
    return 1;
}

/* ----------------------------- rollback action -------------------------- */
/* A trial that was jumped to but never confirmed means the new firmware did
 * not survive. Restore the golden backup and record the outcome. */
static int bm_do_rollback(const BootRec *rec)
{
    BmBackupHeader bh;
    int            rc;

    bm_puts("[BM] rollback: trial was never confirmed\r\n");

    rc = bm_backup_load(&bh);
    if (rc != 0)
    {
        /* Nothing to roll back to. Accept the trial firmware instead of
         * looping on TRIAL_BOOTED forever. */
        bm_puts("[BM] no backup, accepting trial\r\n");
        (void)bm_write_rec(BM_ST_CONFIRMED, rec->slot, rec->seq + 1u, rec->img_len);
        return -1;
    }

    rc = bm_backup_restore(&bh);
    if (rc != 0)
    {
        bm_puts("[BM] FAIL restore rc=");
        bm_dec((uint32_t)(0 - rc));
        bm_puts("\r\n");
        (void)bm_write_rec(BM_ST_CONFIRMED, rec->slot, rec->seq + 1u, rec->img_len);
        return -2;
    }

    (void)bm_write_rec(BM_ST_ROLLED_BACK, rec->slot, rec->seq + 1u, bh.len);
    bm_puts("[BM] rolled back ok, len=");
    bm_hex(bh.len);
    bm_puts("\r\n");
    return 1;
}

/* -------------------------------- jump ---------------------------------- */
/* Same sequence as BOOT_Application()/JumpToApplication(), but with a
 * parameterised NOR offset so we can also enter the OTA agent partition.
 * Requires the memory-mapped mode to be enabled (the vector table is XIP). */
static void bm_jump_to(uint32_t nor_off)
{
    uint32_t vector = BM_NOR_BASE + nor_off;
    bm_fn_t  entry;
    uint32_t primask;

    HAL_SuspendTick();

#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_IC_Msk) != 0u) { SCB_DisableICache(); }
#endif
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) { SCB_DisableDCache(); }
#endif

    primask = __get_PRIMASK();
    __disable_irq();

    SCB->VTOR = vector;
    entry = (bm_fn_t)(*(__IO uint32_t *)(vector + 4u));

#if defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8_1M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
    __set_MSPLIM(0u);
#endif
    __set_MSP(*(__IO uint32_t *)vector);

    __set_PRIMASK(primask);
    entry();
    /* not reached */
}

/* ============================== entry point ============================= */
int BOOTMGR_Process(void)
{
    BootRec rec;
    int     have;
    int     to_agent = 0;
    int     switched = 0;
    uint32_t gate;

    bm_uart_init();
    bm_puts("\r\n[BM] boot manager\r\n");

    /* The XIP window is cacheable by default, so a read-back of freshly
     * programmed flash could hit a stale D-cache line. Run this stage
     * uncached; the application disables the D-cache as well, so the state
     * is left as-is on purpose. */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u)
    {
        SCB_CleanDCache();
        SCB_DisableDCache();
    }
#endif

    gate = ota_gate_take();

    if (EXTMEM_GetMapAddress(EXTMEMORY_1, &bm_base) != EXTMEM_OK)
    {
        bm_puts("[BM] FAIL map address -> app\r\n");
        return -1;
    }

    have = (bm_read_rec(&rec) == 0);
    if (have == 0)
    {
        bm_puts("[BM] rec: none or invalid\r\n");
    }
    else
    {
        bm_puts("[BM] rec: state=");
        bm_dec(rec.state);
        bm_puts(" slot=");
        bm_dec(rec.slot);
        bm_puts(" seq=");
        bm_dec(rec.seq);
        bm_puts("\r\n");
    }

    if (have != 0)
    {
        switch (rec.state)
        {
            case BM_ST_REQ_TRIAL:
                switched = bm_do_switch(&rec, 0);
                if (switched < 0) { to_agent = 1; }
                break;

            case BM_ST_REQ_PERSIST:
                switched = bm_do_switch(&rec, 1);
                if (switched < 0) { to_agent = 1; }
                break;

            case BM_ST_TRIAL_BOOTED:
                /* only hand over to the agent when a rollback really happened */
                if (bm_do_rollback(&rec) == 1) { to_agent = 1; }
                break;

            case BM_ST_REQ_AGENT:
                bm_puts("[BM] agent requested by app\r\n");
                (void)bm_clear_rec();
                to_agent = 1;
                break;

            case BM_ST_REQ_ROLLBACK:
            {
                BmBackupHeader backup;
                if (bm_backup_load(&backup) == 0 && bm_backup_restore(&backup) == 0 &&
                    bm_write_rec(BM_ST_CONFIRMED, 0u, rec.seq + 1u,
                                  backup.len - BM_APP_HEADER) == 0)
                {
                    bm_puts("[BM] previous application restored\r\n");
                    switched = 1;
                }
                else
                {
                    bm_puts("[BM] FAIL requested rollback, staying in OTA recovery\r\n");
                    (void)bm_clear_rec();
                    to_agent = 1;
                }
                break;
            }

            default:
                break;
        }
    }

    if ((to_agent == 0) && (bm_image_sane(BM_APP_OFFSET + BM_APP_HEADER) != 0))
    {
        if (gate == OTA_GATE_RUN_APP || switched == 1 ||
            !bm_image_sane(BM_AGENT_OFFSET + BM_AGENT_HEADER))
        {
            bm_puts("[BM] -> app\r\n");
            (void)bm_mm(0);             /* BOOT_Application() maps it itself */
            return 0;
        }
        /* Run the independent receiver before every normal application boot.
         * It resets with a one-shot SRAM token when the window expires. */
        ota_gate_write(OTA_GATE_WINDOW);
        __DSB();
        bm_puts("[BM] -> OTA window (10 s), then app\r\n");
    }

    if (bm_image_sane(BM_AGENT_OFFSET + BM_AGENT_HEADER) != 0)
    {
        bm_puts("[BM] -> agent\r\n");
        if (bm_mm(1) == 0)
        {
            bm_jump_to(BM_AGENT_OFFSET + BM_AGENT_HEADER);
        }
        bm_puts("[BM] FAIL agent jump returned\r\n");
    }
    else if (to_agent != 0)
    {
        bm_puts("[BM] WARN no valid agent partition\r\n");
    }

    bm_puts("[BM] -> app (fallback)\r\n");
    (void)bm_mm(0);
    return 0;
}
