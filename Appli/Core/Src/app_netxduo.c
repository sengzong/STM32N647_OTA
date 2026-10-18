/* NetXDuo OTA client: receive framed firmware, verify it, then commit a slot.
 * LED2_OTA_AGENT builds the independent receiver used before application boot.
 */
#include "app_netxduo.h"
#include "main.h"
#include "nx_stm32_eth_driver.h"
#include "nx_stm32_phy_driver.h"
#include "nxd_dhcp_client.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "../../../Secure_nsclib/ota_boot_gate.h"
#include "../../../Secure_nsclib/ota_layout.h"
#include "../../../Secure_nsclib/ota_reset.h"
#include "ota_wire.h"

#define PC_SERVER_IP       IP_ADDRESS(192, 168, 2, 10)   /* PC 上 OTA 服务端监听 IP */
#define PC_SERVER_PORT     8080

/* IP 方式: 0=静态IP(PC 直连), 1=DHCP */
#define APP_USE_DHCP         0
#define APP_STATIC_IP        IP_ADDRESS(192, 168, 2, 11)
#define APP_NETMASK          IP_ADDRESS(255, 255, 255, 0)

#define PAYLOAD_SIZE         1536
#define PACKET_POOL_SIZE     ((PAYLOAD_SIZE + sizeof(NX_PACKET)) * 10)
#define IP_THREAD_STACK_SIZE (2 * 1024)
#define IP_THREAD_PRIORITY   10
#define LINK_THREAD_STACK_SIZE (2 * 1024)
#define LINK_THREAD_PRIORITY 11
#define APP_THREAD_STACK_SIZE (4 * 1024)
#define APP_THREAD_PRIORITY  10

/* OTA 接收缓冲(内部 SRAM, 512KB) */
#define OTA_RX_BUFFER_SIZE OTA_LAYOUT_RX_CAPACITY

#if defined(LED2_PACKAGE_COMPAT)
static uint8_t ota_rx_buffer[OTA_RX_BUFFER_SIZE] __attribute__((section(".EXTRAM"), aligned(32)));
#else
static uint8_t  ota_rx_buffer[OTA_RX_BUFFER_SIZE] __attribute__((aligned(32)));
#endif
static volatile uint32_t ota_rx_len = 0;
static uint32_t ota_received_profile;

static NX_PACKET_POOL packet_pool;
static NX_IP          ip_instance;
static TX_THREAD      link_thread;
static TX_THREAD      app_thread;
#if (APP_USE_DHCP == 1)
static NX_DHCP        dhcp_client;
static TX_SEMAPHORE   dhcp_semaphore;
static ULONG          ip_address = 0;
static ULONG          network_mask = 0;
#endif

static VOID link_thread_entry(ULONG id);
static VOID app_thread_entry(ULONG id);
#if (APP_USE_DHCP == 1)
static VOID ip_address_change_notify_callback(NX_IP *ip_instance, VOID *additional_info);
#endif

/* ======================= OTA 写 NOR 代理 =======================
 * 硬约束: XSPI2 切 IO 模式瞬间, 0x70000000 的 XIP 取指断开, 代理必须整体在内部 SRAM 跑。
 * 本文件实现两层:
 *   ota_agent_main()  —— 自包含代理(直写 XSPI2 寄存器, 零 HAL/printf/memcpy 外部依赖),
 *                        独立 section .ota_agent, OTA 时刻被 memcpy 进 SRAM 跳转执行。
 *   ota_run_agent()    —— NOR 侧装载器: 拷贝代理机器码矩形窗进 SRAM, 刷 I-cache, 跳转。
 * OPI-DTR 常量取自通用 FSBL 的 MX25UM25645G 驱动: 8D-8D-8D, 16bit 指令 DTR, 32bit 地址 DTR。
 * NOR 在 XSPI2(非安全): 寄存器基址 0x4000A000。 */
#include "app_ota_flash.h"

/* ---- 扇区/其它 ---- */
#define OTA_APP_FLASH_BASE   0x70000000u      /* XSPI2 XIP 基址 */
#define OTA_APP_OFFSET       0x100000u        /* appli 在 NOR 的偏移(= FSBL EXTMEM_XIP_IMAGE_OFFSET) */
#define OTA_APP_HEADER       0x400u           /* FSBL 头部(偏移 0x100000) */
#define OTA_APP_TARGET       (OTA_APP_FLASH_BASE + OTA_APP_OFFSET)  /* 0x70100000: 新固件起点 */
#define OTA_APP_IMAGE_START  (OTA_APP_FLASH_BASE + OTA_APP_OFFSET + OTA_APP_HEADER) /* 0x70100400 当前运行点 */
#define OTA_SECTOR           0x1000u
#define OTA_PAGE             0x100u

/* 代理结果落盘: 固定 SRAM 0x341B0000(0x34143270..0x341F8000 之间全为空洞, startup 不碰, 跨复位保留)。
 * 布局 4 words: [0]=本轮 flags  [1]=rc  [2]=status_byte  [3]=OTA_HANDOFF_MAGIC */
#define OTA_HANDOFF_ADDR   0x341B0000u
#define OTA_HANDOFF_MAGIC  0x31525453u      /* "STR1"  */

/* ---- XSPI 寄存器偏移(见 stm32n647xx.h XSPI_TypeDef) ---- */
#define R_CR    0x000u
#define R_DCR1  0x008u
#define R_DCR2  0x00Cu
#define R_SR    0x020u
#define R_FCR   0x024u
#define R_DLR   0x040u
#define R_AR    0x048u
#define R_DR    0x050u
#define R_CCR   0x100u
#define R_TCR   0x108u
#define R_IR    0x110u
#define R_WCCR  0x180u
#define R_WTCR  0x188u
#define R_WIR   0x190u

/* CR 域 */
#define CR_FMODE     0x30000000u   /* FMODE[1:0] 掩码; 0x30000000 = memory-mapped */
#define CR_FMODE_IR  0x10000000u   /* indirect read */
#define CR_ABORT     0x00000002u
#define CR_MSEL      0xC0000000u   /* bit31:30 */
/* CCR 域(8D-8D-8D) */
#define CCR_BASE  0x20000000u  /* DQSE */
#define CCR_INST_ADDR_DATA (CCR_BASE | 0x00000004u /*IMODE2*/ | 0x00000008u /*IDTR*/ | 0x00000010u /*ISZ16*/ \
        | 0x00000400u /*ADMODE2*/ | 0x00000800u /*ADDTR*/ | 0x00003000u /*ADSZ32*/ \
        | 0x04000000u /*DMODE2*/ | 0x08000000u /*DDTR*/)
#define CCR_INST_ADDR      (CCR_BASE | 0x00000004u | 0x00000008u | 0x00000010u \
        | 0x00000400u | 0x00000800u | 0x00003000u)
#define CCR_INST_ONLY      (CCR_BASE | 0x00000004u | 0x00000008u | 0x00000010u)
#define CCR_DDTR           0x08000000u
/* TCR 域 */
#define TCR_DCYC    0x0000001Fu
#define TCR_SSHIFT  0x40000000u
#define TCR_DHQC    0x10000000u
/* SR 域 */
#define SR_BUSY  0x00000020u
#define SR_TCF   0x00000002u
#define SR_FTF   0x00000004u
#define SR_TEF   0x00000001u
#define SR_FLEV  0x00007F00u
/* FCR */
#define FC_CTCF  0x00000002u
#define FC_CTEF  0x00000001u

/* OPI-DTR 16bit 指令: MX25UM25645G 在 DTR 下第二字节为反码 */
#define OPI_INSTR16(c)  (uint16_t)((((uint16_t)(c)) << 8) | (uint16_t)((uint8_t)(~(c) & 0xFFu)))


/* ============ 自包含代理(仅直写寄存器, 位置无关, 在 SRAM 运行) ============ */

#if defined(__GNUC__)
#  define OAAI static __attribute__((always_inline)) inline
#else
#  define OAAI static inline
#endif

OAAI uint32_t oa_rd(const OtaJob *j, uint32_t off) { return *(volatile uint32_t *)(j->reg_base + off); }
OAAI void     oa_wr(const OtaJob *j, uint32_t off, uint32_t v) { *(volatile uint32_t *)(j->reg_base + off) = v; }

/* 等 SR 某组位达到状态; want_set=1 任一置位, 0 全部清; 超时返回 -1 */
OAAI int oa_wait(const OtaJob *j, uint32_t msk, int want_set, uint32_t tries)
{
    while (tries--) {
        uint32_t s = oa_rd(j, R_SR) & msk;
        if (want_set ? (s != 0u) : (s == 0u)) return 0;
    }
    return -1;
}

/* LED 打点: 代理自包含诊断。XIP 断后 fault handler 取指也会 fault(锁死), 因此进度只能用
 * SRAM 直写 GPIO 的方式标记——挂机后看 LED 组合即知崩在哪个阶段。
 * LED0=PG10(GPIOG_NS ODR 0x46020814)、LED1=PE10(GPIOE_NS ODR 0x46021014), 低电平点亮。
 *   LED1(g0=0)：代理进入                 = (亮,亮)
 *   abort 通过                          = (灭,亮)
 *   状态读通过                           = (亮,灭)
 *   恢复映射/准备返回                     = (灭,灭) */
#define OPI_LED0_ODR (0x46020814u)
#define OPI_LED1_ODR (0x46021014u)
OAAI void oa_led(int g0, int e0)
{
    uint32_t v;
    v = *(volatile uint32_t *)OPI_LED0_ODR; v &= ~(1u << 10u); v |= (g0 ? 0u : (1u << 10u));
    *(volatile uint32_t *)OPI_LED0_ODR = v;
    v = *(volatile uint32_t *)OPI_LED1_ODR; v &= ~(1u << 10u); v |= (e0 ? 0u : (1u << 10u));
    *(volatile uint32_t *)OPI_LED1_ODR = v;
}

/* ---- 自包含 USART1 直报(代理专用, 不依赖 HAL/printf/返回路径) ----
 * 恢复映射崩 / 复位失效时, 代理也能把进度打到串口(轮询 TXE)。
 * USART1_NS=0x42001000; ISR@0x1C(TXE bit7), TDR@0x28。 */
#define UA_UART (0x42001000u)
#define UA_ISR  (UA_UART + 0x1Cu)
#define UA_TDR  (UA_UART + 0x28u)
OAAI void oa_c(char c)
{
    while (((*(volatile uint32_t *)UA_ISR) & 0x80u) == 0u) { }
    *(volatile uint32_t *)UA_TDR = (uint32_t)(uint8_t)c;
}
OAAI void oa_hex(uint32_t v)
{
    int i;
    oa_c('0'); oa_c('x');
    for (i = 28; i >= 0; i -= 4) {
        uint32_t d = (v >> i) & 0xFu;
        oa_c((char)((d < 10u) ? ('0' + (int)d) : ('A' + (int)(d - 10u))));
    }
}
/* 打一行: [OTA <tag> rc=0x.. st=0x..] */
OAAI void oa_report(char tag, uint32_t rc, uint32_t st)
{
    oa_c('['); oa_c('O'); oa_c('T'); oa_c('A'); oa_c(']'); oa_c(' ');
    oa_c(tag); oa_c(' '); oa_c('r'); oa_c('c'); oa_c('=');
    oa_hex(rc); oa_c(' '); oa_c('s'); oa_c('t'); oa_c('=');
    oa_hex(st); oa_c('\r'); oa_c('\n');
}

OAAI void oa_flush_fifo(const OtaJob *j);   /* 前置声明(定义在 oa_abort_mm 附近) */

/* 完成一条间接命令(带/不带数据)
 * has_addr / has_data / dlen / dummy (dlen=dummy 仅 data 时有效) */
OAAI int oa_cfg(const OtaJob *j, uint32_t instr16, uint32_t addr, uint32_t dlen,
                int has_addr, int has_data, uint32_t dummy)
{
    uint32_t cr, ccr, tcr;

    oa_flush_fifo(j);               /* 每个命令前清 FIFO: 否则之前 DTR 读残留的字节会让
                                       无数据命令(如 WEN/擦除)误以为要发数据而卡 BUSY(上一次
                                       t 行 SR=0x124: BUSY+FLEVEL=1 正是这) */
    tcr = oa_rd(j, R_TCR);

    cr = oa_rd(j, R_CR);
    cr &= ~(CR_FMODE | CR_MSEL);          /* FMODE=0(indirect write), MSEL=0(IO7-0) */
    oa_wr(j, R_CR, cr);

    ccr = CCR_BASE;
    if (has_data) {
        ccr |= CCR_INST_ADDR_DATA;
        oa_wr(j, R_DLR, dlen - 1u);
        tcr &= ~TCR_SSHIFT;               /* DTR 收数须清 sample shift */
    } else if (has_addr) {
        ccr |= CCR_INST_ADDR;
        if ((tcr & TCR_DHQC) != 0u) ccr |= CCR_DDTR;   /* 与 HAL ConfigCmd 的 DHQC/DDTR 联动一致 */
    } else {
        ccr |= CCR_INST_ONLY;
        if ((tcr & TCR_DHQC) != 0u) ccr |= CCR_DDTR;
    }
    oa_wr(j, R_CCR, ccr);

    tcr &= ~(TCR_DCYC | TCR_SSHIFT);
    tcr |= (dummy & TCR_DCYC);
    oa_wr(j, R_TCR, tcr);

    oa_wr(j, R_IR, instr16);
    if (has_addr || has_data) oa_wr(j, R_AR, addr);

    if (!has_data) {
        if (oa_wait(j, SR_TCF, 1, 3000000u)) {
            oa_report('t', (uint32_t)instr16, oa_rd(j, R_SR));   /* 无数据命令没完成, 报 SR 看 TEF/BUSY */
            return -1;
        }
        oa_wr(j, R_FCR, FC_CTCF);
    }
    return 0;
}

/* 间接写数据(已 oa_cfg 为 data-write 后调用) */
OAAI int oa_tx(const OtaJob *j, const uint8_t *p, uint32_t n)
{
    while (n--) {
        if (oa_wait(j, SR_FTF, 1, 2000000u)) return -1;
        *(volatile uint8_t *)(j->reg_base + R_DR) = *p++;
    }
    if (oa_wait(j, SR_TCF, 1, 3000000u)) return -1;
    oa_wr(j, R_FCR, FC_CTCF);
    return 0;
}

/* 间接读 dlen 字节(已 oa_cfg 为 data-read 后调用; 触发 = 重写 AR) */
OAAI int oa_rx(const OtaJob *j, uint32_t addr, uint32_t dlen, uint8_t *out)
{
    uint32_t cr = oa_rd(j, R_CR);
    cr = (cr & ~CR_FMODE) | CR_FMODE_IR;  /* indirect read */
    cr &= ~CR_MSEL;
    oa_wr(j, R_CR, cr);
    oa_wr(j, R_AR, addr);                 /* 触发 */
    while (dlen--) {
        if (oa_wait(j, SR_FTF | SR_TCF, 1, 3000000u)) return -1;
        *out++ = *(volatile uint8_t *)(j->reg_base + R_DR);
    }
    if (oa_wait(j, SR_TCF, 1, 3000000u)) return -1;
    oa_wr(j, R_FCR, FC_CTCF);
    return 0;
}

/* 一条读命令(input/status): instr16 + 地址 + dlen 字节 + dummy */
OAAI int oa_read(const OtaJob *j, uint32_t instr16, uint32_t addr, uint32_t dlen, uint32_t dummy, uint8_t *out)
{
    if (oa_cfg(j, instr16, addr, dlen, 1, 1, dummy)) return -1;
    return oa_rx(j, addr, dlen, out);
}

/* 轮询 WIP(bit0=1 忙); 每次空转降频, tries 设大点覆盖扇区擦除 400ms */
OAAI int oa_wip(const OtaJob *j, uint32_t tries)
{
    uint32_t t;
    for (t = 0; t < tries; t++) {
        uint8_t st;
        if (oa_read(j, OPI_INSTR16(0x05), 0u, 1u, 4u, &st)) return -1;
        if ((st & 1u) == 0u) return 0;
        { volatile uint32_t d = 60000u; while (d) d--; }
    }
    return -2;
}

/* 写使能 0x06 + 等 WEL(bit1)置位(诊断期验证过, 现净链) */
OAAI int oa_wen(const OtaJob *j)
{
    uint32_t t;
    uint8_t st = 0u;
    if (oa_cfg(j, OPI_INSTR16(0x06), 0u, 0u, 0, 0, 0u)) return -1;
    for (t = 0; t < 400000u; t++) {
        if (oa_read(j, OPI_INSTR16(0x05), 0u, 1u, 4u, &st)) return -1;
        if ((st & 2u) != 0u) return 0;                    /* WEL */
    }
    return -2;
}

/* 4KB 扇区擦除 [start, end) */
OAAI int oa_erase_sectors(const OtaJob *j, uint32_t start, uint32_t end)
{
    uint32_t a;
    for (a = start; a < end; a += OTA_SECTOR) {
        if (oa_wip(j, 2000u)) return -10;
        if (oa_wen(j))        return -11;
        if (oa_cfg(j, OPI_INSTR16(0x21), a, 0u, 1, 0, 0u)) return -12;
        if (oa_wip(j, 4000000u)) return -13;   /* 等扇区擦除完成 */
    }
    return 0;
}

/* 页编程, 自动按 256B 页切开 */
OAAI int oa_program(const OtaJob *j, uint32_t addr, const uint8_t *src, uint32_t n)
{
    while (n) {
        uint32_t chunk = OTA_PAGE - (addr & (OTA_PAGE - 1u));
        if (chunk > n) chunk = n;
        if (oa_wip(j, 2000u))  return -20;
        if (oa_wen(j))         return -21;
        if (oa_cfg(j, OPI_INSTR16(0x12), addr, chunk, 1, 1, 0u)) return -22;
        if (oa_tx(j, src, chunk)) return -23;
        src += chunk; addr += chunk; n -= chunk;
    }
    return oa_wip(j, 2000u);
}

/* OPI 读回校验(0xEE 读, 20 dummy) */
OAAI int oa_verify(const OtaJob *j, uint32_t addr, const uint8_t *src, uint32_t n)
{
    uint8_t buf[OTA_PAGE];
    while (n) {
        uint32_t chunk = (n < OTA_PAGE) ? n : OTA_PAGE;
        uint32_t i;
        if (oa_read(j, OPI_INSTR16(0xEE), addr, chunk, 20u, buf)) return -31;
        for (i = 0; i < chunk; i++) {
            if (buf[i] != src[i]) return -1000 - (int)(addr + i);
        }
        addr += chunk; src += chunk; n -= chunk;
    }
    return 0;
}

/* 清空 XSPI FIFO(换向/恢复映射前必须, 否则残留旧读数据污染恢复后的首个映射读) */
OAAI void oa_flush_fifo(const OtaJob *j)
{
    uint32_t tries = 128u;
    while (((oa_rd(j, R_SR) & SR_FLEV) != 0u) && (tries-- != 0u)) {
        (void)oa_rd(j, R_DR);            /* 每读一个字节弹出一个 */
    }
    oa_wr(j, R_FCR, FC_CTCF | FC_CTEF);  /* 顺带清 TC/TE 标志 */
}

/* 退出内存映射 -> 命令模式。
 * 注意: 清 FMODE 后 XIP 立即断, fault 处理函数(在NOR)取指也会 fault -> 锁死无声。
 * 因此本函数前后由代理自带 LED/UART 打点标记进度(在 SRAM 里跑, 不受 XIP 影响)。 */
OAAI int oa_abort_mm(const OtaJob *j)
{
    __DSB(); __DMB();
    /* 先等 XIP 在途读排空(CPU 已全在 SRAM, 无新取指; 让最后一条 NOR 取指完成) */
    (void)oa_wait(j, SR_BUSY, 0, 2000000u);
    __DSB();
    if ((oa_rd(j, R_SR) & SR_BUSY) != 0u) {
        oa_wr(j, R_CR, oa_rd(j, R_CR) | CR_ABORT);
        if (oa_wait(j, SR_TCF, 1, 3000000u)) return -1;
        oa_wr(j, R_FCR, FC_CTCF);
        if (oa_wait(j, SR_BUSY, 0, 3000000u)) return -2;
    }
    oa_wr(j, R_CR, oa_rd(j, R_CR) & ~(CR_FMODE | CR_MSEL));
    oa_flush_fifo(j);                   /* 排掉 XIP 中断前的残留读数据 */
    __DSB();
    return 0;
}

/* 恢复内存映射: 只需读配置(0xEE/20dummy)+FMODE=MM 即可让 XIP 读回来。
 * appli 只从 NOR 读, 从不经 MM 写, 故无需回写 WCCR/WTCR/WIR。
 * 先排空 FIFO + 清 FMODE, 再配读配置 + 进 MM; 每步用自包含 UART 上报('1''2''3')。
 * 若恢复后首个映射取指还崩, 串口会显示最后到哪一步。 */
OAAI void oa_restore_mm(const OtaJob *j)
{
    uint32_t cr, tcr;

    oa_flush_fifo(j);                                   /* 清残留数据 + TC/TE */
    cr = oa_rd(j, R_CR) & ~(CR_FMODE | CR_MSEL);        /* FMODE=0, MSEL=0 */
    oa_wr(j, R_CR, cr);
    oa_report('1', 0u, oa_rd(j, R_SR));

    oa_wr(j, R_CCR, CCR_INST_ADDR_DATA);
    tcr = oa_rd(j, R_TCR);
    tcr &= ~(TCR_DCYC | TCR_SSHIFT);
    oa_wr(j, R_TCR, tcr | 20u);
    oa_wr(j, R_IR, OPI_INSTR16(0xEE));
    oa_wr(j, R_AR, 0u);
    oa_report('2', 0u, oa_rd(j, R_CR));

    cr = oa_rd(j, R_CR) & ~CR_FMODE;
    cr |= CR_FMODE;                     /* memory-mapped */
    oa_wr(j, R_CR, cr);
    oa_wr(j, R_FCR, FC_CTCF | FC_CTEF);
    __DSB(); __ISB();
    oa_report('3', 0u, oa_rd(j, R_CR));
}

/* The ROM starts in SPI mode, while the running image uses octal DTR.
 * Reset the flash and restore ROM clocks entirely from SRAM before resetting. */
OAAI void oa_reset(const OtaJob *j)
{
    (void)oa_cfg(j, OPI_INSTR16(0x66), 0u, 0u, 0, 0, 0u);
    (void)oa_cfg(j, OPI_INSTR16(0x99), 0u, 0u, 0, 0, 0u);
    for (volatile uint32_t wait = 0u; wait < 100000u; wait++) { __NOP(); }
    oa_wr(j, R_CR, oa_rd(j, R_CR) & ~1u);
    ota_reset_prepare();
    NVIC_SystemReset();
    __DSB();
    for (;;) { }
}

/* ---- 代理入口(整个函数体 + 其内嵌字面池会整体拷进 SRAM) ----
 * 说明: 编译带 -ffunction-sections, 每个函数独占 .text.ota_agent_main 段, 字面池随函数
 * 生成在段尾, 因此 ota_run_agent 拷贝一个包含它的固定矩形窗即可完整搬走(无需自定义 .ld 段)。 */
__attribute__((used, noinline))
int ota_agent_main(const OtaJob *j)
{
    int rc = 0;
    uint32_t sbyte = 0u;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    __DSB();
    oa_led(1, 1);                        /* 打点A: 已进代理, 且未退出内存映射 */

    if ((j->flags & OTA_F_PROBE) != 0u) {
        /* 只读寄存器快照(不切 IO / 不下 flash 命令): 供启动探针校验访问权与继承配置 */
        if (j->probe_out != 0u) {
            OtaProbeInfo *p = (OtaProbeInfo *)(uintptr_t)j->probe_out;
            p->cr   = oa_rd(j, R_CR);
            p->sr   = oa_rd(j, R_SR);
            p->dcr1 = oa_rd(j, R_DCR1);
            p->dcr2 = oa_rd(j, R_DCR2);
            p->ccr  = oa_rd(j, R_CCR);
            p->tcr  = oa_rd(j, R_TCR);
        }
        __DSB();
        __set_PRIMASK(primask);
        return 0;
    }

    rc = oa_abort_mm(j);
    oa_report('a', (uint32_t)rc, oa_rd(j, R_SR));   /* 串口自报: abort 结果 */
    if (rc != 0) goto done;
    oa_led(0, 1);                        /* 打点B: 已退出内存映射(此后 NOR 取指断裂) */

    if ((j->flags & OTA_F_STAT) != 0u) {
        uint8_t st = 0u;
        rc = oa_read(j, OPI_INSTR16(0x05), 0u, 1u, 4u, &st);
        sbyte = st;
        if (j->probe_out != 0u)
            ((OtaProbeInfo *)(uintptr_t)j->probe_out)->status_byte = st;
        if (rc != 0) rc = -3;
        oa_led(1, 0);                    /* 打点C: 状态读命令已执行 */
        oa_report('s', (uint32_t)rc, sbyte);        /* 串口自报: 状态读结果 */
    }

    if ((j->flags & OTA_F_ID) != 0u) {
        /* 读 JEDEC ID(0x9F, 6 字节, 0 dummy): 验证 OPI 读路径对 flash 回话是否真实有效 */
        uint8_t id[6], i;
        rc = oa_read(j, OPI_INSTR16(0x9F), 0u, 6u, 0u, id);
        sbyte = 0u;
        for (i = 0; i < 4u; i++) sbyte = (sbyte << 8) | (uint32_t)id[i];   /* 前 4 字节 */
        oa_report('j', (uint32_t)rc, sbyte);
    }

    if (((j->flags & OTA_F_PROGRAM) != 0u) && (rc == 0)) {
        uint32_t start = j->target & ~(OTA_SECTOR - 1u);
        uint32_t end   = ((j->target + j->len + OTA_SECTOR - 1u) & ~(OTA_SECTOR - 1u));
        if ((j->flags & OTA_F_WRITE_ONLY) == 0u)
            rc = oa_erase_sectors(j, start, end);
        if (rc == 0) rc = oa_program(j, j->target, j->data, j->len);
        if ((rc == 0) && ((j->flags & OTA_F_VERIFY) != 0u))
            rc = oa_verify(j, j->target, j->data, j->len);
        oa_report('p', (uint32_t)rc, 0u);   /* 串口自报: 擦+写+读回结果 */
    }

done:
    if (j->probe_out != 0u)
        ((OtaProbeInfo *)(uintptr_t)j->probe_out)->result = rc;
    if (j->res_addr != 0u) {
        volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)j->res_addr;
        r[0] = j->flags;
        r[1] = (uint32_t)rc;
        r[2] = (j->probe_out != 0u) ? ((OtaProbeInfo *)(uintptr_t)j->probe_out)->status_byte : 0u;
        r[3] = OTA_HANDOFF_MAGIC;
        __DSB();
    }
    oa_report('d', (uint32_t)rc, sbyte);   /* 串口自报: 最终结果(崩前必达) */
    if ((j->flags & OTA_F_RESET) != 0u) {
        oa_led(1, 1);                    /* 生产 OTA 完成, 复位前亮点双灯 */
        oa_reset(j);
    } else {
        oa_restore_mm(j);
        oa_led(0, 0);                    /* 打点D: 已恢复内存映射(到此才允许返回 NOR) */
        __set_PRIMASK(primask);
    }
    return rc;
}


/* ============ NOR 侧装载器与探针 ============ */

#define OTA_AGENT_FRAME_SIZE 16384u
static uint8_t ota_agent_sram[OTA_AGENT_FRAME_SIZE + 32u] __attribute__((aligned(32)));
static OtaProbeInfo ota_boot_probe;

const OtaProbeInfo *ota_get_boot_probe(void) { return &ota_boot_probe; }

/* 拷贝代理帧(含函数 + 其内嵌字面池)进 SRAM, 刷 I-cache 后跳转执行 */
int ota_run_agent(const OtaJob *job)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t fn    = (uint32_t)(uintptr_t)&ota_agent_main;
    uint32_t delta = fn & 31u;
    uint32_t frame = fn & ~31u;
    int (*fnp)(const OtaJob *);
    int rc;

    /* Protect the shared executable buffer from UI confirmation preemption. */
    __disable_irq();
    memcpy(ota_agent_sram, (const void *)(uintptr_t)frame, OTA_AGENT_FRAME_SIZE);
    __DSB();
    SCB_InvalidateICache();
    __ISB();
    fnp = (int (*)(const OtaJob *))(uintptr_t)((uint32_t)(uintptr_t)ota_agent_sram + delta);
    rc = fnp(job);
    __set_PRIMASK(primask);
    return rc;
}

/* 启动探针: 代理拷入 SRAM 执行(仅寄存器快照), 成功后返回 'OTA1' 魔数 */
uint32_t ota_probe_run(void)
{
    OtaJob job = {0};
    int rc;

    job.reg_base  = (uint32_t)XSPI2;   /* 非安全 XSPI2 寄存器基址 0x4000A000 */
    job.target    = 0u;
    job.len       = 0u;
    job.data      = 0u;
    job.flags     = OTA_F_PROBE;
    job.probe_out = (uint32_t *)&ota_boot_probe;

    rc = ota_run_agent(&job);
    if (rc == 0) {
        ota_boot_probe.flash_map_addr = OTA_APP_FLASH_BASE;
        /* 探针每次上电都往固定 SRAM 写 '1BRP' 标记:
         * 用来证明(1)该地址真实可写 (2)startup/FSBL 不会清它 (3)软复位保留。 */
        ((volatile uint32_t *)OTA_HANDOFF_ADDR)[4] = 0x50425231u;   /* "1BRP" */
        return OTA_F_IMAGE_OK;
    }
    return OTA_F_IMAGE_OK ^ (uint32_t)(rc & 0xFFu);   /* 出错时返回非魔数 */
}

/* ============ NOR 侧辅助: sanity 校验 / 写盘 / 分步测试阶梯 ============ */

/* 真机分步测试阶梯级别(诊断/验收用, 生产设 0):
 * 0 = 启动不跑;  1 = 阶段1(OPI 状态读+JEDEC, 只读);  2 = 阶段1 + scratch 擦+写+读回 */
#ifndef OTA_BOOT_LADDER_LEVEL
#define OTA_BOOT_LADDER_LEVEL 0
#endif

/* 测试用的 scratch 扇区(距运行代码几 MB, 可安全擦写) */
#define OTA_SCRATCH_OFFSET OTA_LAYOUT_SLOT_BASE

/* 简易保险: 新 appli 顶部 8 字节应像向量表(初始 MSP 在内部 SRAM, Reset 向量在 NOR XIP 区) */
static int ota_image_sane(const uint8_t *img, uint32_t len)
{
    uint32_t sp, rv;
    if (len < 8u) return 0;
    if (len > OTA_LAYOUT_IMAGE_MAX) return 0;
    sp = (uint32_t)img[0] | ((uint32_t)img[1] << 8) | ((uint32_t)img[2] << 16) | ((uint32_t)img[3] << 24);
    rv = (uint32_t)img[4] | ((uint32_t)img[5] << 8) | ((uint32_t)img[6] << 16) | ((uint32_t)img[7] << 24);
    if ((sp < 0x34000000u) || (sp > 0x34200000u) || (sp & 7u) != 0u) return 0;
    if ((rv & 1u) == 0u || rv < OTA_APP_IMAGE_START ||
        (rv & ~1u) >= OTA_APP_IMAGE_START + len) return 0;
    return 1;
}

/* ============ 多槽位: CRC / 槽位表 / 存储 / 切换 ============ */

volatile uint8_t  g_ota_receiving = 0;
volatile uint32_t g_ota_progress  = 0;
volatile int8_t   g_ota_last_slot = -1;
OtaSlotView g_slots[OTA_SLOT_COUNT];

/* Set once the NetXDuo/ETH stack has been built. Used by ota_confirm_poll() as a
 * "the network capability is present" proof. Cable/link state is intentionally
 * excluded: an unplugged cable must never cause a rollback. */
volatile uint8_t  g_net_stack_ready = 0;

/* slot NOR 偏移 -> XIP 可读地址(0x70000000) */
static uint32_t ota_slot_xip(uint32_t slot)
{
    return OTA_APP_FLASH_BASE + OTA_SLOT_BASE + slot * OTA_SLOT_SPACING;
}

uint32_t ota_crc32(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu, i;
    for (i = 0; i < len; i++) {
        uint32_t b = p[i], k;
        crc ^= b;
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* 经 XIP 重读各槽位头 -> g_slots(UI 用) */
void ota_slot_refresh(void)
{
    uint8_t i;
    for (i = 0; i < OTA_SLOT_COUNT; i++) {
        const OtaSlotHeader *h = (const OtaSlotHeader *)ota_slot_xip(i);
        g_slots[i].off = OTA_SLOT_BASE + (uint32_t)i * OTA_SLOT_SPACING;
        g_slots[i].ready = 0;
        g_slots[i].img_len = 0;
        g_slots[i].desc[0] = 0;
        if (h->magic == OTA_SLOT_MAGIC &&
            (h->img_len >= 8u) && (h->img_len <= OTA_LAYOUT_IMAGE_MAX)) {
            uint8_t dl = (h->desc_len > OTA_SLOT_DESC_MAX) ? OTA_SLOT_DESC_MAX : (uint8_t)h->desc_len;
            g_slots[i].ready = 1;
            g_slots[i].img_len = h->img_len;
            memset(g_slots[i].desc, 0, sizeof(g_slots[i].desc));
            memcpy(g_slots[i].desc, h->desc, dl);
        }
    }
}

/* 把 [img] 连同描述存进下一个空槽(不动引导区, 不复位) */
int ota_store_to_next_slot(const uint8_t *img, uint32_t img_len, const char *desc, uint32_t desc_len)
{
    OtaSlotHeader *h;
    OtaJob j;
    uint32_t slot, off, len, crc;
    int rc;
    uint8_t i;
    /* desc 可能与 ota_rx_buffer 重叠(等于 OtaSlotHeader.desc 的位置), 若先 memset 会把源清掉,
     * 故先拷到本地再组装 */
    uint8_t desc_save[OTA_SLOT_DESC_MAX];

    if ((img == 0u) || (img_len < 8u) || (img_len > OTA_LAYOUT_IMAGE_MAX)) return -1;
    if (!ota_image_sane(img, img_len)) return -3;   /* 向量表 SP/RV 不合法 -> 拒收(防坏镜像进槽) */
    ota_slot_refresh();
    slot = OTA_SLOT_COUNT;
    for (i = 0; i < OTA_SLOT_COUNT; i++) { if (!g_slots[i].ready) { slot = i; break; } }
    if (slot >= OTA_SLOT_COUNT)
    {
        /* 全满: 轮询覆盖下一个槽(自然清掉旧槽), 避免无法继续存 */
        slot = (uint32_t)((g_ota_last_slot >= 0) ? ((uint8_t)g_ota_last_slot + 1u) : 0u) % OTA_SLOT_COUNT;
        printf("OTA: replacing the staged latest firmware\r\n");
    }

    memset(desc_save, 0, sizeof(desc_save));
    if (desc_len > OTA_SLOT_DESC_MAX) desc_len = OTA_SLOT_DESC_MAX;
    memcpy(desc_save, desc, desc_len);

    off = OTA_SLOT_BASE + slot * OTA_SLOT_SPACING;
    len = OTA_SLOT_HEADER + img_len;

    /* 组装 [64B 槽头 | 镜像] 复用 OTA 收包缓冲。
     * 关键顺序(曾踩坑): 镜像原址 img 在 buf[16+desc_len], 与槽头 region [0..64) 重叠。
     * 必须先算 CRC(源未坏)、再 memmove 把镜像挪到 buf+64、最后才 memset/写槽头 ——
     * 若先 memset(h->desc) 会把镜像开头的几十字节清零, 存进 flash 的镜像永远无法启动。 */
    h = (OtaSlotHeader *)ota_rx_buffer;
    crc = ota_crc32(img, img_len);
    memmove(ota_rx_buffer + OTA_SLOT_HEADER, img, img_len);   /* 先挪镜像到镜像位 */
    memset(h, 0, sizeof(*h));
    h->magic    = 0xFFFFFFFFu;                 /* Commit magic only after payload verify. */
    h->crc32    = crc;
    h->img_len  = img_len;
    h->desc_len = desc_len;
    memset(h->desc, 0, sizeof(h->desc));
    memcpy(h->desc, desc_save, desc_len);
    memcpy(h->rsv, &ota_received_profile, sizeof(ota_received_profile));

    j.reg_base   = (uint32_t)XSPI2;
    j.target     = off;                    /* NOR 偏移 */
    j.len        = len;
    j.data       = ota_rx_buffer;
    j.flags      = OTA_F_PROGRAM | OTA_F_VERIFY;   /* 不动引导区, 不 RESET */
    j.probe_out  = 0;
    j.res_addr   = OTA_HANDOFF_ADDR;
    rc = ota_run_agent(&j);
    if (rc != 0) return (rc < 0) ? rc : -rc;
    {
        uint32_t magic = OTA_SLOT_MAGIC;
        j.len = sizeof(magic);
        j.data = (const uint8_t *)&magic;
        j.flags = OTA_F_PROGRAM | OTA_F_VERIFY | OTA_F_WRITE_ONLY;
        rc = ota_run_agent(&j);
        if (rc != 0) return (rc < 0) ? rc : -rc;
    }

    g_ota_last_slot = (int8_t)slot;
    ota_slot_refresh();
    return (int)slot;
}

/* ================= 引导记录: app <-> FSBL 的 32B 握手(协议见 app_ota_flash.h) =================
 * 切换不再由 app 自己擦写引导区: app 只写一条记录然后复位, 由 FSBL 完成
 * "槽位 -> 引导区" 的拷贝与校验, 并通过同一条记录回传结果。
 *
 *   app 写 REQ_TRIAL   -> 复位
 *   FSBL 拷贝 + 校验   -> 记 TRIAL_BOOTED
 *   新固件跑满 3 秒且自检通过 -> 记 CONFIRMED (正式采纳)
 *   新固件始终不确认   -> FSBL 用 golden backup 回滚
 *
 * NOTE: comments added in this block stay ASCII-only (source-file convention).
 */

/* Reuse the SRAM executor for reset-only jobs. Never return to XIP after
 * resetting the NOR device or changing its clock selection. */
static void ota_system_reset(void)
{
    OtaJob job = {0};
    job.reg_base = (uint32_t)XSPI2;
    job.flags = OTA_F_RESET;
    (void)ota_run_agent(&job);
    for (;;) { }
}

#if defined(LED2_OTA_AGENT)
static uint8_t ota_gateway_window;

void ota_gateway_init(void)
{
    ota_gateway_window = (ota_gate_take() == OTA_GATE_WINDOW);
}

void ota_gateway_poll(void)
{
    if (ota_gateway_window && !g_ota_receiving &&
        tx_time_get() >= (OTA_GATE_WAIT_MS * TX_TIMER_TICKS_PER_SECOND / 1000u))
    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        /* A connection may have arrived just before the supervisor ran. */
        if (g_ota_receiving) { __set_PRIMASK(primask); return; }
        printf("AGENT: OTA window expired, starting application\r\n");
        ota_gate_write(OTA_GATE_RUN_APP);
        ota_system_reset();
    }
}
#endif

/* The record lives in its own NOR sector just below the boot area and is read
 * back through the XIP window. The D-cache is disabled in this project, so a
 * freshly programmed record is always re-read from flash - no cache maintenance
 * is needed here. A record whose CRC does not match (e.g. torn write) counts as
 * "no record" and is simply ignored. */
static const OtaBootRec *ota_bootrec_xip(void)
{
    return (const OtaBootRec *)(OTA_APP_FLASH_BASE + OTA_BOOTREC_OFFSET);
}

int ota_bootrec_read(OtaBootRec *rec)
{
    OtaBootRec tmp;

    memcpy(&tmp, ota_bootrec_xip(), OTA_BOOTREC_SIZE);
    if (tmp.magic != OTA_BOOTREC_MAGIC) return -1;
    if (ota_crc32((const uint8_t *)&tmp, 28u) != tmp.crc32) return -2;
    if (rec != 0) *rec = tmp;
    return 0;
}

int ota_bootrec_write(uint32_t state, uint32_t slot, uint32_t seq, uint32_t img_len)
{
    OtaBootRec rec;
    OtaJob     j;
    uint8_t    buf[OTA_BOOTREC_SIZE] __attribute__((aligned(32)));
    int        rc;

    memset(&rec, 0, sizeof(rec));
    rec.magic   = OTA_BOOTREC_MAGIC;
    rec.state   = state;
    rec.slot    = slot;
    rec.seq     = seq;
    rec.img_len = img_len;
    rec.crc32   = ota_crc32((const uint8_t *)&rec, 28u);
    memcpy(buf, &rec, OTA_BOOTREC_SIZE);

    /* Programming NOR while executing from NOR -> must go through the SRAM agent. */
    j.reg_base  = (uint32_t)XSPI2;
    j.target    = OTA_BOOTREC_OFFSET;
    j.len       = OTA_BOOTREC_SIZE;
    j.data      = buf;
    j.flags     = OTA_F_PROGRAM | OTA_F_VERIFY;
    j.probe_out = 0;
    j.res_addr  = OTA_HANDOFF_ADDR;
    rc = ota_run_agent(&j);
    if (rc != 0)
    {
        printf("BOOTREC: write FAIL rc=%d state=%lu\r\n", rc, (unsigned long)state);
        return (rc < 0) ? rc : -rc;
    }
    return 0;
}

uint32_t ota_bootrec_next_seq(void)
{
    OtaBootRec rec;
    if (ota_bootrec_read(&rec) != 0) return 1u;
    return rec.seq + 1u;
}

/* Accept the firmware we are running right now. Only meaningful while the FSBL
 * has recorded TRIAL_BOOTED - that is the state a freshly copied image boots in,
 * and the state that gets rolled back if we never confirm. */
#if !defined(LED2_OTA_AGENT)
int ota_confirm_boot(void)
{
    OtaBootRec rec;

    if (ota_bootrec_read(&rec) != 0)
    {
        printf("BOOTREC: nothing to confirm\r\n");
        return -1;
    }
    if (rec.state == OTA_BOOT_ST_CONFIRMED) return 0;        /* already accepted */
    if (rec.state != OTA_BOOT_ST_TRIAL_BOOTED)
    {
        /* e.g. ROLLED_BACK: we are running the golden backup, leave it alone. */
        printf("BOOTREC: state=%lu is not a trial, no confirm\r\n", (unsigned long)rec.state);
        return -2;
    }
    if (ota_bootrec_write(OTA_BOOT_ST_CONFIRMED, rec.slot, rec.seq + 1u, rec.img_len) != 0)
        return -3;

    printf("BOOTREC: trial confirmed (slot=%lu img_len=%lu)\r\n",
           (unsigned long)rec.slot, (unsigned long)rec.img_len);
    return 0;
}

/* Ask the FSBL to bring up the resident OTA agent instead of this firmware.
 * The FSBL clears the request before it jumps, so a missing/invalid agent
 * partition can only cost one extra boot, never a reset loop. */
int ota_request_agent(void)
{
    if (ota_bootrec_write(OTA_BOOT_ST_REQ_AGENT, 0u, ota_bootrec_next_seq(), 0u) != 0)
        return -1;
    printf("BOOTREC: OTA agent requested, resetting\r\n");
    ota_system_reset();
    return 0;   /* not reached */
}

/* Call this periodically from the UI thread (see model_select_run). It confirms
 * at most once per boot, OTA_CONFIRM_DELAY_MS after the first call.
 *
 * Why wait: the point is to let the new firmware prove it can hold its main loop,
 * not merely that its reset vector points somewhere valid.
 *
 * Why the self-test: a firmware that renders its UI but lost the OTA path would
 * otherwise confirm itself and become permanent - the exact hole this closes.
 * Deliberately NOT part of the test: the cable / link state. An unplugged cable
 * is an operator condition, not a firmware defect, and must never cause a
 * rollback. ota_probe_run() only takes a register snapshot (no IO-mode switch,
 * no flash command), so it is safe to run with the RTOS live. */
void ota_confirm_poll(void)
{
    static uint8_t done = 0;
    static ULONG   t0   = 0u;
    ULONG          now;
    OtaBootRec     rec;

    if (done != 0u) return;

    now = tx_time_get();
    if (t0 == 0u) { t0 = now; return; }        /* first call: start the clock */

    /* ThreadX ticks at 100 Hz here, i.e. TX_TIMER_TICKS_PER_SECOND == 100. */
    if ((now - t0) < (ULONG)((OTA_CONFIRM_DELAY_MS * TX_TIMER_TICKS_PER_SECOND) / 1000u)) return;

    if (ota_bootrec_read(&rec) != 0 || rec.state != OTA_BOOT_ST_TRIAL_BOOTED)
    {
        done = 1u;
        return;
    }

    if (g_net_stack_ready == 0u)
    {
        return;
    }
    if (ota_probe_run() != OTA_F_IMAGE_OK)
    {
        printf("CONFIRM: skip - NOR/XSPI2 probe failed\r\n");
        return;
    }
    if (ota_confirm_boot() == 0) { done = 1u; }
    else { t0 = now; }
}
#endif /* !LED2_OTA_AGENT */

/* Install standalone applications without requiring a confirmation callback.
 * The FSBL owns copying, the previous-image snapshot and requested rollback. */
static int ota_prepare_slot(uint32_t slot)
{
    uint32_t il;

    ota_slot_refresh();
    if (slot >= OTA_SLOT_COUNT) return -1;
    if (!g_slots[slot].ready)   return -2;

    il = g_slots[slot].img_len;
    if ((il < 8u) || (il > OTA_LAYOUT_IMAGE_MAX)) return -3;
    /* The FSBL re-checks the slot CRC before it touches anything, but a cheap
     * sanity check here keeps obviously broken images from ever being queued. */
    if (!ota_image_sane((const uint8_t *)(ota_slot_xip(slot) + OTA_SLOT_HEADER), il)) return -4;

    if (ota_crc32((const uint8_t *)(ota_slot_xip(slot) + OTA_SLOT_HEADER), il) !=
        ((const OtaSlotHeader *)ota_slot_xip(slot))->crc32) return -6;

    /* Standalone applications need not implement our confirmation protocol.
     * A manual rollback remains available through the independent receiver. */
    if (ota_bootrec_write(OTA_BOOT_ST_REQ_PERSIST, slot, ota_bootrec_next_seq(), il) != 0)
        return -5;

    return 0;
}

int ota_switch_slot(uint32_t slot)
{
    int rc = ota_prepare_slot(slot);
    if (rc != 0) { return rc; }
    printf("switch: boot record written, resetting - FSBL will copy Slot-%lu\r\n",
           (unsigned long)slot);
    ota_system_reset();
    return 0;   /* not reached */
}

/* 代理崩前结果落盘到固定 SRAM 的跨复位手递手
 *   宽带: [0]=本轮 flags  [1]=rc  [2]=status  [3]=MAGIC('1STR')  4 字 */
static void handoff_show(void)
{
    volatile uint32_t *hh = (volatile uint32_t *)OTA_HANDOFF_ADDR;
    if (hh[3] == OTA_HANDOFF_MAGIC) {
        printf(" prev-run: flags=0x%08lX rc=%ld status=0x%02lX  %s\r\n",
               (unsigned long)hh[0], (long)(int32_t)hh[1], (unsigned long)(hh[2] & 0xFFu),
               ((int32_t)hh[1] == 0) ? "OK" : "FAIL");
    }
}

/* 单次启动内的返回式阶梯测试: 每阶段代理执行 -> 恢复映射 -> 返回 -> 直接打印。
 * 若恢复映射仍崩, 代理会在崩前用自包含 UART 打 [OTA x rc=.. st=..] 行 + 把结果写
 * 固定 SRAM(0x341B0000), 所以照样看得见结果。 */
void ota_dbg_ladder(void)
{
    OtaJob j;
    volatile uint32_t *hh = (volatile uint32_t *)OTA_HANDOFF_ADDR;
    int rc;
#if (OTA_BOOT_LADDER_LEVEL >= 2)
    static uint8_t pat[OTA_SECTOR];
    uint32_t i;
#endif

    printf("\r\n=== OTA ladder (XSPI2 OPI-DTR agent in SRAM) ===\r\n");
    handoff_show();
    printf("L1 regs:  XSPI2=0x%08lX CR=0x%08lX SR=0x%08lX\r\n"
           "          DCR1=0x%08lX DCR2=0x%08lX CCR=0x%08lX TCR=0x%08lX\r\n",
           (unsigned long)(uintptr_t)XSPI2,
           (unsigned long)ota_boot_probe.cr, (unsigned long)ota_boot_probe.sr,
           (unsigned long)ota_boot_probe.dcr1, (unsigned long)ota_boot_probe.dcr2,
           (unsigned long)ota_boot_probe.ccr, (unsigned long)ota_boot_probe.tcr);

    j.reg_base   = (uint32_t)XSPI2;
    j.probe_out  = (uint32_t *)OTA_HANDOFF_ADDR;
    j.res_addr   = OTA_HANDOFF_ADDR;
    j.target     = 0u; j.len = 0u; j.data = 0u;
    hh[3] = 0u;

    /* L2: 状态读(返回式) */
    j.flags = OTA_F_STAT;
    rc = ota_run_agent(&j);
    printf("L2 status-read: rc=%d status=0x%02lX (WIP=%lu WEL=%lu)\r\n",
           rc, (unsigned long)hh[2],
           (unsigned long)((hh[2] >> 0) & 1u), (unsigned long)((hh[2] >> 1) & 1u));

    /* L2.5: JEDEC ID(验证读路径; 代理自报 [OTA] j 行含 ID) */
    j.flags = OTA_F_ID;
    (void)ota_run_agent(&j);

#if (OTA_BOOT_LADDER_LEVEL >= 2)
    /* L3: scratch 扇区 擦+编程+读回(真正写 NOR 的链路) */
    for (i = 0; i < sizeof(pat); i++) pat[i] = (uint8_t)(0x5Au ^ (uint8_t)(i & 0x3Fu));
    j.flags   = OTA_F_PROGRAM | OTA_F_VERIFY;
    j.target  = OTA_SCRATCH_OFFSET;
    j.len     = OTA_SECTOR;
    j.data    = pat;
    rc = ota_run_agent(&j);
    printf("L3 scratch RW: rc=%d target=0x%08lX len=0x%lX (erase+program+readback)\r\n",
           rc, (unsigned long)OTA_SCRATCH_OFFSET, (unsigned long)OTA_SECTOR);
#endif
    printf("=== ladder end ===\r\n");
}

UINT app_netxduo_init(VOID *memory_ptr)
{
    TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL *)memory_ptr;
    CHAR *pointer;

    printf("NetXDuo OTA client config\r\n");
    printf("PC server %lu.%lu.%lu.%lu:%lu\r\n",
           (PC_SERVER_IP >> 24) & 0xFF, (PC_SERVER_IP >> 16) & 0xFF,
           (PC_SERVER_IP >> 8) & 0xFF, PC_SERVER_IP & 0xFF, (unsigned long)PC_SERVER_PORT);
    printf("\r\n");

    nx_system_initialize();

    if (tx_byte_allocate(byte_pool, (VOID **)&pointer, PACKET_POOL_SIZE, TX_NO_WAIT) != TX_SUCCESS)
        return TX_POOL_ERROR;
    if (nx_packet_pool_create(&packet_pool, "Packet Pool", PAYLOAD_SIZE, pointer, PACKET_POOL_SIZE) != NX_SUCCESS)
        return NX_POOL_ERROR;

    if (tx_byte_allocate(byte_pool, (VOID **)&pointer, IP_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
        return TX_POOL_ERROR;
    if (nx_ip_create(&ip_instance, "IP Instance", 0, 0, &packet_pool, nx_stm32_eth_driver, pointer,
                     IP_THREAD_STACK_SIZE, IP_THREAD_PRIORITY) != NX_SUCCESS)
        return NX_NOT_SUCCESSFUL;

#if (APP_USE_DHCP == 0)
    if (nx_ip_address_set(&ip_instance, APP_STATIC_IP, APP_NETMASK) != NX_SUCCESS)
        return NX_NOT_SUCCESSFUL;
#endif

    if (tx_byte_allocate(byte_pool, (VOID **)&pointer, 1024, TX_NO_WAIT) != TX_SUCCESS)
        return TX_POOL_ERROR;
    if (nx_arp_enable(&ip_instance, (VOID *)pointer, 1024) != NX_SUCCESS)
        return NX_NOT_SUCCESSFUL;
    if (nx_icmp_enable(&ip_instance) != NX_SUCCESS)
        return NX_NOT_SUCCESSFUL;
    if (nx_tcp_enable(&ip_instance) != NX_SUCCESS)
        return NX_NOT_SUCCESSFUL;
    if (nx_udp_enable(&ip_instance) != NX_SUCCESS)
        return NX_NOT_SUCCESSFUL;

    #if (APP_USE_DHCP == 1)
    if (nx_dhcp_create(&dhcp_client, &ip_instance, "DHCP Client") != NX_SUCCESS)
        return NX_DHCP_ERROR;
    if (tx_semaphore_create(&dhcp_semaphore, "DHCP Semaphore", 0) != TX_SUCCESS)
        return TX_SEMAPHORE_ERROR;
    #endif

    if (tx_byte_allocate(byte_pool, (VOID **)&pointer, LINK_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
        return TX_POOL_ERROR;
    if (tx_thread_create(&link_thread, "Link Thread", link_thread_entry, 0, pointer, LINK_THREAD_STACK_SIZE,
                         LINK_THREAD_PRIORITY, LINK_THREAD_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
        return TX_THREAD_ERROR;

    if (tx_byte_allocate(byte_pool, (VOID **)&pointer, APP_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
        return TX_POOL_ERROR;
    if (tx_thread_create(&app_thread, "OTA Thread", app_thread_entry, 0, pointer, APP_THREAD_STACK_SIZE,
                         APP_THREAD_PRIORITY, APP_THREAD_PRIORITY, TX_NO_TIME_SLICE, TX_DONT_START) != TX_SUCCESS)
        return TX_THREAD_ERROR;

    g_net_stack_ready = 1u;
    return TX_SUCCESS;
}

/* Reply only after verified storage (and a boot request in the agent). */
static void ota_send_result(NX_TCP_SOCKET *sock, int status, uint32_t slot, uint32_t crc)
{
    NX_PACKET *packet = NX_NULL;
    uint32_t reply[4] = { OTA_ACK_MAGIC, (uint32_t)status, slot, crc };
    if (nx_packet_allocate(&packet_pool, &packet, NX_TCP_PACKET, NX_IP_PERIODIC_RATE) != NX_SUCCESS)
        return;
    if (nx_packet_data_append(packet, reply, sizeof(reply), &packet_pool,
                              NX_IP_PERIODIC_RATE) != NX_SUCCESS ||
        nx_tcp_socket_send(sock, packet, 2u * NX_IP_PERIODIC_RATE) != NX_SUCCESS)
        nx_packet_release(packet);
}

static int ota_receive_container(NX_TCP_SOCKET *sock, OtaWireView *view)
{
    ULONG last_data = tx_time_get();
    ota_rx_len = 0u;
    memset(view, 0, sizeof(*view));
    for (;;)
    {
        NX_PACKET *pkt = NX_NULL;
        ULONG plen = 0u, copied = 0u;
        UINT ret = nx_tcp_socket_receive(sock, &pkt, NX_IP_PERIODIC_RATE);
        int parsed;
        if (ret == NX_NO_PACKET)
        {
            if (tx_time_get() - last_data < 30u * TX_TIMER_TICKS_PER_SECOND) continue;
            return -10;                 /* Idle is not a complete firmware. */
        }
        if (ret != NX_SUCCESS) return -11;
        ret = nx_packet_length_get(pkt, &plen);
        if (ret != NX_SUCCESS || plen > OTA_RX_BUFFER_SIZE - ota_rx_len)
        {
            nx_packet_release(pkt);
            return -12;
        }
        /* NetX can return a packet chain; the total is not one contiguous span. */
        ret = nx_packet_data_extract_offset(pkt, 0u, ota_rx_buffer + ota_rx_len,
                                            plen, &copied);
        nx_packet_release(pkt);
        if (ret != NX_SUCCESS || copied != plen) return -13;
        if (plen == 0u) continue;
        ota_rx_len += copied;
        last_data = tx_time_get();
        parsed = ota_wire_parse(ota_rx_buffer, ota_rx_len, OTA_RX_BUFFER_SIZE, view);
        if (parsed < 0) return -14;
        if (view->total != 0u)
            g_ota_progress = (uint32_t)(((uint64_t)ota_rx_len * 1000u) / view->total);
        if (parsed == 1)
        {
            if (view->command == 0u && view->version >= 3u &&
                ota_crc32(ota_rx_buffer + view->image_offset, view->image_len) != view->crc)
                return -15;
            return 0;
        }
    }
}

static int ota_prepare_rollback(void)
{
    const uint32_t *backup = (const uint32_t *)(OTA_APP_FLASH_BASE + OTA_BACKUP_OFFSET);
    uint32_t length = backup[1];
    if (backup[0] != OTA_BACKUP_MAGIC || length <= OTA_APP_HEADER ||
        length > OTA_BACKUP_MAX_LEN || (length & (OTA_SECTOR - 1u)) != 0u) return -20;
    if (ota_crc32((const uint8_t *)(OTA_APP_FLASH_BASE + OTA_BACKUP_DATA), length) != backup[2]) return -21;
    return ota_bootrec_write(OTA_BOOT_ST_REQ_ROLLBACK, 0u, ota_bootrec_next_seq(), 0u);
}

/* Every connection is one framed transfer. Failure always returns to connect. */
static VOID app_thread_entry(ULONG id)
{
    NX_TCP_SOCKET ota_socket;
    UINT ret;
    (void)id;
#if (APP_USE_DHCP == 1)
    if (nx_ip_address_change_notify(&ip_instance, ip_address_change_notify_callback, NULL) != NX_SUCCESS)
        Error_Handler();
    if (nx_dhcp_start(&dhcp_client) != NX_SUCCESS) Error_Handler();
    if (tx_semaphore_get(&dhcp_semaphore, TX_WAIT_FOREVER) != TX_SUCCESS) Error_Handler();
#else
    printf("Static IP: 192.168.2.11, PC: 192.168.2.10:8080\r\n");
#endif
    for (;;)
    {
        int result, slot = -1, reboot = 0;
        uint32_t crc = 0u;
        OtaWireView view;
        if (nx_tcp_socket_create(&ip_instance, &ota_socket, "OTA Socket",
                                 NX_IP_NORMAL, NX_FRAGMENT_OKAY, NX_IP_TIME_TO_LIVE,
                                 4096u, NX_NULL, NX_NULL) != NX_SUCCESS)
        {
            tx_thread_sleep(NX_IP_PERIODIC_RATE);
            continue;
        }
        ret = nx_tcp_client_socket_bind(&ota_socket, 0u, NX_IP_PERIODIC_RATE);
        if (ret == NX_SUCCESS)
            ret = nx_tcp_client_socket_connect(&ota_socket, PC_SERVER_IP, PC_SERVER_PORT,
                                               2u * NX_IP_PERIODIC_RATE);
        if (ret != NX_SUCCESS)
        {
            (void)nx_tcp_client_socket_unbind(&ota_socket);
            (void)nx_tcp_socket_delete(&ota_socket);
            tx_thread_sleep(NX_IP_PERIODIC_RATE / 2u);
            continue;
        }
        printf("OTA: connected, waiting for OFW2/OFW3 container\r\n");
        g_ota_receiving = 1u;
        g_ota_progress = 0u;
        result = ota_receive_container(&ota_socket, &view);
        if (result == 0 && view.command == OTA_COMMAND_ROLLBACK)
        {
            result = ota_prepare_rollback();
            slot = 0;
            reboot = 1;
        }
        else if (result == 0)
        {
            ota_received_profile = view.profile;
            crc = ota_crc32(ota_rx_buffer + view.image_offset, view.image_len);
            slot = ota_store_to_next_slot(ota_rx_buffer + view.image_offset, view.image_len,
                        (const char *)(ota_rx_buffer + view.image_offset - view.desc_len),
                        view.desc_len);
            if (slot < 0) result = slot;
#if defined(LED2_OTA_AGENT)
            else
            {
                result = ota_prepare_slot((uint32_t)slot);
                reboot = 1;
            }
#endif
        }
        if (result == 0 && view.command == OTA_COMMAND_ROLLBACK)
            printf("OTA: previous-image backup verified, rollback queued\r\n");
        else if (result == 0)
            printf("OTA: verified Slot-%d, crc=%08lX\r\n", slot, (unsigned long)crc);
        else
            printf("OTA: transfer rejected rc=%d, waiting for another image\r\n", result);
        ota_send_result(&ota_socket, result, (uint32_t)slot, crc);
        (void)nx_tcp_socket_disconnect(&ota_socket, 2u * NX_IP_PERIODIC_RATE);
        (void)nx_tcp_client_socket_unbind(&ota_socket);
        (void)nx_tcp_socket_delete(&ota_socket);
        if (result == 0 && reboot)
        {
            printf("OTA: boot request ready; FSBL will start the requested application\r\n");
            ota_system_reset();
        }
        g_ota_receiving = 0u;
        tx_thread_sleep(NX_IP_PERIODIC_RATE / 2u);
    }
}

/* 链接线程：监测网线插拔，link up 后使能接口并启动 OTA 线程 */
static VOID link_thread_entry(ULONG id)
{
    UINT status;
    ULONG actual_status;
    UINT linkdown = 2;
    int32_t link_state;

    (void)id;

    while (1)
    {
        status = nx_ip_interface_status_check(&ip_instance, 0, NX_IP_LINK_ENABLED, &actual_status, 10);
        if (status == NX_SUCCESS)
        {
            if ((linkdown == 1) || (linkdown == 2))
            {
                linkdown = 0;
                printf("Network cable connected.\r\n");
                link_state = nx_eth_phy_get_link_state();
                if (link_state == ETH_PHY_STATUS_100MBITS_FULLDUPLEX)
                    printf("100Mbps full-duplex.\r\n");
                else if (link_state == ETH_PHY_STATUS_100MBITS_HALFDUPLEX)
                    printf("100Mbps half-duplex.\r\n");
                else if (link_state == ETH_PHY_STATUS_10MBITS_FULLDUPLEX)
                    printf("10Mbps full-duplex.\r\n");
                else
                    printf("10Mbps half-duplex.\r\n");

                nx_ip_driver_direct_command(&ip_instance, NX_LINK_ENABLE, &actual_status);
                tx_thread_resume(&app_thread);
                tx_thread_relinquish();
            }
        }
        else
        {
            if ((linkdown == 0) || (linkdown == 2))
            {
                linkdown = 1;
                printf("Network cable not connected.\r\n");
                nx_ip_driver_direct_command(&ip_instance, NX_LINK_DISABLE, &actual_status);
            }
        }
        tx_thread_sleep(NX_IP_PERIODIC_RATE);
    }
}

#if (APP_USE_DHCP == 1)
static VOID ip_address_change_notify_callback(NX_IP *nx_ip_ptr, VOID *additional_info)
{
    if (nx_ip_address_get(nx_ip_ptr, &ip_address, &network_mask) != NX_SUCCESS)
        Error_Handler();
    if (ip_address != 0)
        tx_semaphore_put(&dhcp_semaphore);
}
#endif
