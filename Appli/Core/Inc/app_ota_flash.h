#ifndef __APP_OTA_FLASH_H
#define __APP_OTA_FLASH_H

#include <stdint.h>
#include "../../../Secure_nsclib/ota_layout.h"

/* ========================= OTA 写 NOR 代理(OPI 直写 XSPI2) =========================
 * 硬约束: XSPI2 切 IO 模式的瞬间, 0x70000000 XIP 取指断开 -> 代理必须整体在内部 SRAM 执行。
 * 自包含: 代理(ota_agent_main)绝不调用外部函数/HAL/printf, 全部直写寄存器常量,
 * 位置无关(Thumb PC-relative 字面量)。编译带 -ffunction-sections, 函数与其字面池
 * 独占一个 .text 子段, 由 ota_run_agent 在 OTA 时刻把包含它的 8KB 矩形窗 memcpy 进
 * SRAM 再按函数首地址跳转执行。
 * OPI-DTR 常量(8D-8D-8D / 16bit 指令 DTR / 32bit 地址 DTR / 8 线数据 DTR+DQS)
 * 取自通用 FSBL 的 MX25UM25645G 驱动。NOR 在 XSPI2: XIP 区 0x70000000,
 * 外设寄存器 NS 基址 0x4000A000(若被安全门控改 S 基址 0x5000A000)。
 */

/* 任务描述: 全部字段由 NOR 侧装载器填写, 代理(SRAM)只读 */
typedef struct {
    uint32_t         reg_base;  /* XSPI 外设寄存器基址(本板 NS=0x5802A000) */
    uint32_t         target;    /* NOR 绝对地址偏移(擦除/编程起点, 建议 4KB 对齐, 如 0x100000) */
    uint32_t         len;       /* payload 字节数, data[0] 对应 target 处字节 */
    const uint8_t   *data;      /* SRAM 内 payload(编程模式); PROBE 模式未用 */
    uint32_t         flags;     /* OTA_F_* */
    uint32_t        *probe_out; /* 指向 OtaProbeInfo(SRAM), 代理回填 */
    uint32_t         res_addr;  /* 结果回写地址(固定 SRAM, 跨复位保留); 代理崩前写 {flags,rc,status,MAGIC} */
} OtaJob;

#define OTA_F_PROBE   0x00000001u  /* 仅读 XSPI 寄存器快照回填(不切 IO、不下命令), 供启动探针 */
#define OTA_F_STAT    0x00000002u  /* 切命令模式后发 OPI 0x05 状态读, status_byte 回填, 随即恢复 MM */
#define OTA_F_PROGRAM 0x00000004u  /* 擦除 [target, target+len) 扇区并页编程 payload */
#define OTA_F_VERIFY  0x00000008u  /* 编程后 OPI 读回校验 */
#define OTA_F_RESET   0x00000010u  /* 完成后 CPU 复位(生产 OTA); 未置则恢复内存映射后返回 */
#define OTA_F_ID      0x00000020u  /* 读 JEDEC ID(0x9F, 6 字节), 用于验证 OPI 读路径真伪 */
#define OTA_F_WRITE_ONLY 0x00000040u /* Program an already erased range, no erase. */
#define OTA_F_QUIET      0x00000080u /* Suppress per-sector success diagnostics. */

/* ---- 多槽位固件: 引导区(0x100000)常驻当前固件; 新固件先入槽位, 切换=拷槽位覆盖引导区 ----
 * 槽位布局: [64B 描述头 | 固件镜像], 等宽间距排在 NOR 高区(避开 FSBL/引导区) */
#define OTA_SLOT_BASE    OTA_LAYOUT_SLOT_BASE
#define OTA_SLOT_SPACING OTA_LAYOUT_SLOT_SIZE
#define OTA_SLOT_COUNT   1u /* One staging image; the rollback area is separate. */
#define OTA_SLOT_HEADER  64u
#define OTA_SLOT_DESC_MAX 32u

#define OTA_SLOT_MAGIC   0x31544C53u   /* "SLT1" */
#define OTA_BACKUP_OFFSET OTA_LAYOUT_BACKUP_HEADER
#define OTA_BACKUP_DATA   OTA_LAYOUT_BACKUP_DATA
#define OTA_BACKUP_MAGIC  0x314B4247u
#define OTA_BACKUP_MAX_LEN OTA_LAYOUT_BACKUP_MAX

/* 槽位头(64B, 放槽位首地址): magic/img_len/desc_len/crc32(对镜像)/desc[..]/rsv */
typedef struct {
    uint32_t magic;
    uint32_t img_len;
    uint32_t desc_len;
    uint32_t crc32;
    uint8_t  desc[OTA_SLOT_DESC_MAX];
    uint8_t  rsv[64u - 16u - OTA_SLOT_DESC_MAX];
} OtaSlotHeader;

/* 容器(PC ota_gui.py 发): [magic u32 "OFW2" | ver u32 | desc_len u32 | img_len u32 | desc | img] */
#define OTA_CTR_MAGIC  0x3257464Fu   /* "OFW2" */
#define OTA_CTR_VER    2u
#define OTA_CTR_HDRZ   16u           /* 容器头字节数 */

#define OTA_F_IMAGE_OK 0x314F5441u /* ota_probe_run 成功魔数 (= 'OTA1') */

typedef struct {
    uint32_t cr;   /* XSPI CR    */
    uint32_t sr;   /* XSPI SR    */
    uint32_t dcr1; /* XSPI DCR1  */
    uint32_t dcr2; /* XSPI DCR2  */
    uint32_t ccr;  /* XSPI CCR   */
    uint32_t tcr;  /* XSPI TCR   */
    uint32_t status_byte; /* OTA_F_STAT: OPI 状态寄存器(bit0=WIP, bit1=WEL) */
    uint32_t flash_map_addr; /* NOR 内存映射基址(XSPI2 AXI) */
    int32_t  result;   /* 代理回写的 return code(0=成功); 崩溃前写定, 可跨复位读 */
} OtaProbeInfo;

/* ---- NOR 侧(由 app_netxduo.c 实现) ---- */
uint32_t ota_probe_run(void);               /* 启动探针: 代理拷入 SRAM 执行 OTA_F_PROBE; 成功返回 OTA_F_IMAGE_OK */
int      ota_run_agent(const OtaJob *job);  /* 装载器: memcpy 代理帧进 SRAM -> 刷 I-cache -> 跳转, 返回代理 return code */
int      ota_agent_main(const OtaJob *job); /* SRAM 自包含 OPI 代理(绝不外部调用) */
void     ota_dbg_ladder(void);              /* 真机分步测试阶梯(见 OTA_BOOT_LADDER_LEVEL) */

/* ---- 多槽位 OTA(NOR 侧辅助, 供 UI/网络线程) ---- */
extern volatile uint8_t  g_ota_receiving;   /* 1=正在接收, UI 画进度页 */
extern volatile uint32_t g_ota_progress;    /* 0..1000 千分比 */
extern volatile int8_t   g_ota_last_slot;   /* 最近写入的槽位(-1=无) */

typedef struct {
    uint32_t off;               /* NOR 槽位偏移 */
    uint32_t img_len;
    uint16_t ready;             /* 1=槽内有合法固件 */
    char     desc[OTA_SLOT_DESC_MAX + 1];
} OtaSlotView;
extern OtaSlotView g_slots[OTA_SLOT_COUNT];

uint32_t ota_crc32(const uint8_t *p, uint32_t len);
void     ota_slot_refresh(void);            /* 经 XIP 重读各槽位头 -> g_slots */
int      ota_store_to_next_slot(const uint8_t *img, uint32_t img_len, const char *desc, uint32_t desc_len); /* 存下一空槽, 返回槽号/-1 */
int      ota_switch_slot(uint32_t slot);    /* Queue PERSIST and reset; FSBL owns copy and backup. */
#if defined(LED2_OTA_AGENT)
void     ota_gateway_init(void);
void     ota_gateway_poll(void);
#endif

/* ===================== Boot record: app <-> FSBL handshake =====================
 * The application no longer writes the boot area itself. It only leaves a
 * 32-byte record in its own NOR sector and resets; the FSBL performs the
 * slot -> boot-area copy, verifies it, and reports back through the same record.
 *
 *   app  writes REQ_TRIAL / REQ_PERSIST  -> resets
 *   fsbl copies + verifies               -> writes TRIAL_BOOTED (or CONFIRMED)
 *   app  runs 3 s with a healthy OTA path-> writes CONFIRMED    (accept)
 *   app  never confirms                  -> fsbl rolls back to the golden backup
 *
 * Magic, state values, 32-byte layout and CRC range MUST stay byte-identical to
 * the bootloader-side copy in FSBL/Core/Inc/bootmgr.h.
 *
 *   offset     size   content
 *   0x0FC000   4KB    BOOTREC sector - one 32-byte record, whole sector erased
 *                     per write, placed just below the boot area (0x100000)
 */
#define OTA_BOOTREC_OFFSET    0x0FC000u
#define OTA_BOOTREC_SIZE      32u
#define OTA_BOOTREC_MAGIC     0x314D424Fu   /* "OBM1" */

#define OTA_BOOT_ST_IDLE         0u  /* nothing pending                           */
#define OTA_BOOT_ST_REQ_TRIAL    1u  /* app: copy this slot, boot as a trial      */
#define OTA_BOOT_ST_REQ_PERSIST  2u  /* app: copy this slot, accept right away    */
#define OTA_BOOT_ST_TRIAL_BOOTED 3u  /* fsbl: trial was jumped to, awaiting ack   */
#define OTA_BOOT_ST_CONFIRMED    4u  /* app: trial accepted, keep it              */
#define OTA_BOOT_ST_ROLLED_BACK  5u  /* fsbl: restored the golden backup          */
#define OTA_BOOT_ST_REQ_AGENT    6u  /* app: boot the resident OTA agent          */
#define OTA_BOOT_ST_REQ_ROLLBACK 7u  /* receiver: restore the previous application */

/* 32 bytes, naturally aligned. crc32 sits at offset 28 and covers bytes [0,28). */
typedef struct {
    uint32_t magic;     /*  0: OTA_BOOTREC_MAGIC                             */
    uint32_t state;     /*  4: OTA_BOOT_ST_*                                 */
    uint32_t slot;      /*  8: source slot index                             */
    uint32_t seq;       /* 12: monotonic switch counter                      */
    uint32_t img_len;   /* 16: image length in bytes                         */
    uint32_t rsv[2];    /* 20: reserved for future use (write zero)          */
    uint32_t crc32;     /* 28: CRC32 over bytes [0, 28)                      */
} OtaBootRec;

/* How long the freshly copied firmware must survive before it is allowed to
 * confirm itself (see ota_confirm_poll). */
#ifndef OTA_CONFIRM_DELAY_MS
#define OTA_CONFIRM_DELAY_MS 3000u
#endif

/* ---- implemented in app_netxduo.c ---- */
int      ota_bootrec_read(OtaBootRec *rec);   /* 0 = valid record (magic + CRC ok)          */
int      ota_bootrec_write(uint32_t state, uint32_t slot, uint32_t seq, uint32_t img_len);
uint32_t ota_bootrec_next_seq(void);          /* current seq + 1 (1 when there is none)     */
int      ota_confirm_boot(void);              /* accept the running trial firmware          */
int      ota_request_agent(void);             /* ask the FSBL for the OTA agent, then reset */
void     ota_confirm_poll(void);              /* call periodically: 3 s + self-test -> confirm once */

/* Set once app_netxduo_init() has built the NetXDuo/ETH stack. The cable state is
 * deliberately NOT part of this: an unplugged cable must never cause a rollback. */
extern volatile uint8_t g_net_stack_ready;

#endif /* __APP_OTA_FLASH_H */
