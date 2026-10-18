# STM32N647 OTA 调试踩坑记录

> 工程：`LED_2_noai`（正点原子 ATK-CNN647B，Cortex-M55）
> 目标：把收到的固件写进外部 NOR + 复位切新固件；并做成"多槽位 + 描述 + 切换"系统。
> 记录人：Claude Code + 用户联合调试。所有条目都是真机验证过的经验。

---

## 0. 一句话背景

代码在**外部 NOR flash（0x70000000，XIP）**里直接跑。要"擦掉正在跑自己的 flash 写新固件"——但 XSPI 一切到 IO 模式，CPU 立刻无法再从 flash 取指，程序当场崩。所以写 flash 的**代理必须整体搬到内部 SRAM 里执行**，擦改完再复位。

NOR 挂在 **XSPI2**（XIP 区 0x70000000，寄存器 NS 基址 0x5802A000）。0x90000000 是 XSPI1 上的 PSRAM（**已坏**，读 0xAAAA/0x5555）。

---

## 1. 构建/环境问题

| 问题 | 根因 | 解决 |
|---|---|---|
| 构建报 `-fcyclomatic-complexity` 不支持 | 系统 GCC10 太旧 | PATH 前置 CubeIDE 自带 **GCC12.3** |
| 链接报 `SECURE_*` 多重定义 | 手改的 `objects.list` 误收了 `secure_nsclib.o`（`--out-implib` 产物，git 基线里没有） | 从 objects.list 删掉该行 |
| `allocated section not in segment` 致命 | 自定义 `.ld` 段（`.ota_agent` / `.ota_ram AT>RAM>ROM`） | **不要自定义段**。靠 `-ffunction-sections` 让函数+字面池独占子段 |
| `ota_gui.py` 语法错 | 函数里 `global PORT` 但之前已读过 PORT | 把端口改成实例属性传入 |
| `ota_gui.py` 运行报 `No module named 'tkinter'` | 本机 Python（MS Store/精简版）无 tkinter，且 tkinter 无法 pip 装 | **改用浏览器界面**（`http.server` + 内嵌 HTML，纯标准库，支持拖拽 .bin/.hex） |

**构建命令（无 CubeIDE GUI 也能编）**：
```bash
export PATH="/f/stm32CubeIde/STM32CubeIDE_1.17.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.12.3.rel1.win32_1.1.0.202410251130/tools/bin:..."
make -j8 --no-print-directory all   # 在 Appli/Release 下
```
出 hex：
```bash
arm-none-eabi-objcopy -I binary LED_2_Appli.bin --change-addresses 0x70100400 -O ihex appli.hex
```

---

## 2. SRAM 代理（位置无关代码）—— 核心

**问题**：擦写代码必须进 SRAM 跑，且**拷到哪都能执行**（位置无关）。

**要点**：
- 代理 `ota_agent_main(const OtaJob*)` **纯寄存器直写**，零 HAL/printf/memcpy。
- 拷贝靠 `-ffunction-sections` + 固定**16KB 矩形窗** `memcpy` 进 SRAM（8KB 时被函数体撑爆过，改 16KB）。
- 跳转后必须 `SCB_InvalidateICache()` + `__ISB()`（I-cache 开着，D-cache 关着 → 只需刷 I）。
- **反汇编验证代理体内 0 个 bl/blx** = 自包含确实成立。

---

## 3. 真机调试遇到的坑（按顺序）

### 3.1 第一次发命令就静默死机
**现象**：探针（寄存器只读）OK，但第一次切命令模式就黑屏 + 串口全哑。
**根因**：清 FMODE 退出内存映射后 **XIP 断了**，而 **fault handler 本身在 NOR flash 里** → 它去取指也 fault → LOCKUP，UART 全哑。
**解法**：诊断不用 flash 里的 fault handler，改**代理自己在 SRAM 直写 UART** 上报进度（`[OTA] a/s/p/d/...`），不依赖 XIP/返回/复位。

### 3.2 非安全域写复位寄存器选错别名
**现象**：`oa_reset()` 复位不生效，板子卡死/闪一下。
**根因**：硬编码**安全域 SCS** `0xE000ED0C`，从非安全域写 → bus fault。本 app 是非安全跑（XSPI2=0x5802A000、USART1=0x42001000 都是 NS 基址）。
**解法**：改**非安全别名 `0xE002ED0C`**。

### 3.3 XSPI FIFO 残留字节卡住命令
**现象**：WEN（0x06，无数据命令）等 TCF 超时；`t` 行 SR=0x124 = **BUSY+FLEVEL=1（FIFO 残留 1 字节）**。
**根因**：DTR 读后 FIFO 留了 1 字节，下一条无数据命令被误以为"还有数据要发"。
**解法**：`oa_cfg` **每条命令前先排空 FIFO**（`oa_flush_fifo`：按 SR_FLEV 读 DR 弹空 + 清 TC/TE）。→ 这是擦写最终打通的临门一脚。

### 3.4 sanity（镜像预检）字节序看反
**现象**：真 OTA 被 `sanity check FAILED` 拦下。
**根因**：`xxd` 显示 `00 00 20 34`，小端其实是 **SP=0x34200000（=RAM 顶 `_estack`）**，我检查写 `sp >= 0x34200000` 判非法 → 正好把合法顶值拒掉。
**解法**：上限改 `sp > 0x34200000`。**新固件首 8 字节务必先 `xxd -l8` 核 SP/RV。**

### 3.5 恢复内存映射崩溃（早期）
**现象**：状态读 OK 后，restore-MM（回到 XIP）崩。
**解法**：去掉 app 用不到的 `WCCR/WTCR/WIR` 写 + FIFO 排空 + `__DSB/__ISB`，恢复正常返回。

### 3.6 跨复位 SRAM 结果不可靠
**现象**：想靠固定 SRAM 地址跨复位传"阶段结果"，读全 0。
**根因**：一次完整复位后 ROM/FSBL 阶段会重排/清 SRAM，不可靠。
**解法**：放弃跨复位传递，改**单次启动内跑完 + 直接串口上报**。生产 OTA 反正要复位，借用复位后下一版打印结果。

---

## 4. 内存相关（含"2MB 够不够"）

- **唯一可用内存 = 内部 2MB**（0x34000000~0x34200000）。PSRAM（0x90000000）坏了，等于没有外扩。
- **代码不占 RAM**（在 NOR 里），RAM 只放变量/缓冲/栈。当前约用 1.4MB，剩 ~600KB，OTA 需要的代理 16KB + 收包缓冲 512KB 足够。
- SRAM 有两个别名（S=0x34 / NS=0x24），本工程用 0x34。
- 二进制 `.bss 结束≈0x3414326c`，以太网描述符在 0x341F8000 附近，中间约 700KB 空洞可用。

---

## 5. 多槽位系统 + 两个就地组包 bug

**架构**：
```
BOOTREC  (0x0FC000) : 32B 引导记录(magic/state/slot/seq/img_len/crc32), 自带扇区
引导区   (0x100000) : 当前固件(常驻, 切换时被覆盖)
Slot-0/1/2          : 0x400000 / 0x800000 / 0xC00000, 每槽=[64B槽头|描述|镜像]
golden backup       : 0x1000000(64B头) / 0x1001000(数据), 首次切换时快照
OTA Agent 分区      : 0x1400000, 入口 0x71400400 (常驻接收器, 已完成 2026-09-14)
容器(PC发)          : [magic OFW2|ver|desc_len|img_len|描述|镜像]

切换(应用侧)        : 校验槽位 → 写 BOOTREC(REQ_TRIAL) → 复位        ← 只写标志, 不碰引导区
切换(FSBL 侧)       : 校验槽CRC → 首次快照 golden backup → 4KB 分块拷贝
                      → 读回 CRC 比对 → 记 TRIAL_BOOTED / 失败则还原
确认/回滚           : 新固件跑满 3s 且自检通过 → CONFIRMED(采纳)
                      始终不确认 → 下次复位 FSBL 用 golden backup 回滚
                      → 回滚成功则跳 OTA Agent(救援通道, 免 ST-Link)
```

**为什么把拷贝搬进 FSBL**：应用运行在 NOR 上，自己擦写引导区需要把代理搬进 SRAM、且中途断电
必然留下半写坏的引导区。FSBL 运行在 AXISRAM2，可以自由切 XIP 内存映射，于是"拷贝 + 校验 +
断电可重试"三件事都集中到它身上；应用只剩"写 32 字节标志 + 复位"这一件不可能坏中间态的事。

**为什么还要一个常驻 Agent**：引导区只有一份应用，回滚之后引导区里跑的**不是用户想升的那个固件**。
如果这时还得靠应用自己收下一包，就变成"坏了就再也收不了"。Agent 独立于 OTA 升级链路存在
（不会被新固件覆盖），因此永远能收包；它只写槽位、绝不写引导区，写坏了最多是槽坏，引导区照旧。

### 5.1 描述存储为空
**现象**：`stored Slot-N ""`，描述没存进去。
**根因**：`desc` 源在 `ota_rx_buffer[16]`，与槽头 `h->desc` **同一位置**；先 `memset` 清掉源再 memcpy → 拷进去全 0。**源和目的地重叠**。
**解法**：先拷到本地 `desc_save[]`，再组槽头。

### 5.2 槽镜像开头 29 字节被清 0（导致切换永不可启动）
**现象**：`store` 看镜像头正常，`switch` 读 flash 镜像头全 0 → `sanity` 拒切换 → 每次切换都黑屏。
**根因**：就地组装时，槽头 `memset(h->desc,0,32)` 清 `buf[16..48)`，而镜像在 `buf[16+desc_len]=buf[19]`——**两区重叠**，memset 把镜像开头 29B 清成 0 后搬进槽。
**解法**：**先算 CRC → 先 memmove 镜像到 buf+64 → 最后组槽头**（让槽头区和镜像区物理分开）。

**经验总结**：在共享缓冲里就地组装"头 + 正文"时，若正文源与头区重叠，会互相踩——**先挪正文、再写头**。

### 5.3 切换卡死 / 不响应
**现象**：确认切换后画面停 "Switching..."，串口无声。
**根因**（早期版）：UI 在切换失败后 `for(;;)` 死循环 + 无失败码。
**解法**：`ota_switch_slot` 每个失败分支打串口码（-1~-4 带原因），UI 失败后回列表不卡死。

---

## 6. 版本文件清理

- 只留 `Binary/fsbl_universal.hex` + `Binary/appli.hex`（最新）+ `network-data.hex`。
- 其余版本 hex / `LED_2_Appli_v2_fix.bin` 等已删。
- 槽位、描述、bin 都从 `Appli/Release/LED_2_Appli.bin`（原始镜像）出，构建一次收口。

---

## 7. 待办 / 未来

- 诊断打印（`store: img[0..8)=...`、`switch: hdr/flash-img=...`）收尾清掉（保留 `stored Slot-N`、错误码）。
- `ota_send.py` 旧裸流统一成带描述容器，只留 `ota_gui.py`。
- **Boot Dispatcher**：FSBL Boot Manager 已完成（拷贝/确认/回滚/串口日志，2026-09-14）；
  **常驻 OTA Agent 分区也已完成**（NOR `0x1400000`，入口 `0x71400400`），现已具备
  "接收网线"与"当前跑哪个 App"彻底解耦 + 回滚后免 ST-Link 的救援通道。
  - FSBL 侧：读到 `REQ_AGENT` 或刚刚回滚成功 → 校验 `0x71400400` 向量表 → `bm_jump_to()`；
    Agent 分区无效时只多打一句警告并回退启动应用，不会复位循环。
  - Agent 侧（`Agent/` 工程）：复用 `app_netxduo.c` + `app_ota_flash.h`，去掉 LCD/触摸/相机/帧缓冲；
    独立链接脚本入口 `0x71400400`；**只写槽位、绝不写引导区**（靠 `-DLED2_OTA_AGENT` 编掉直写分支）；
    收到容器后自动写 `REQ_TRIAL` 复位，形成自愈闭环。
  - 工程文件由 `.workbuddy/tools/gen_agent_project.py` 生成，验证脚本 `.workbuddy/tools/agent_verify.sh`。
  - **离线验证已全绿（2026-09-14）**：`agent_verify.sh` exit=0，text 77024 / bin 77248 B，
    `bm_image_sane` PASS（SP=0x34200000 / RV=0x71411521），`ota_flash_write_image` 确认已编掉；
    `Binary/agent.hex`(217328 B, 0x71400400..0x714131BF) 已生成。**仍缺真机验证。**
- **postbuild 的 objcopy 路径坑（已修）**：`arm-none-eabi-objcopy.exe` 是 Windows 原生 exe，不认
  `/f/...` 形式；而 `[ -f ]` 测试会通过 → 表现为"静默失败 + `Binary/*.hex` 不更新"，
  会把旧固件当新固件烧。`Appli/` 与 `Agent/` 的 postbuild 均已加 `to_win()` 转换。详见 `LOCAL_NOTES.md` §九。
- `ota_gui.py` 发送前可加 CRC 校验；分隔符/校验可选项。
- 生成的 makefile 指向旧路径 `F:/stm32CubeIde/Project/LED_2`（FSBL 的 HAL/Middlewares、Appli 的 BSP 包含路径），
  建议在 CubeIDE 里对两个工程各做一次 Clean + Rebuild 让 CDT 重生成，之后可删掉旧的 `LED_2` 目录以免混编。

---

## 8. 金句清单

- **代码在 flash 里跑，要擦它就得先把"擦它的程序"搬进 RAM。**
- **不用自定义 .ld 段；-ffunction-sections + 拷贝窗口就能搬函数。**
- **fault handler 在 NOR 里，XIP 断了它自己也取不了指 → 锁死无声；调试要走"代理自报"。**
- **非安全域写系统寄存器要用 NS 别名。**
- **XSPI 每条命令前排空 FIFO。**
- **就地组包头/正文重叠会互相踩：先挪正文再写头。**
- **xxd 的小端要看清；SP 上限别把 `_estack` 拒了。**

## 2026-10-17 连续网口升级与两份固件方案

- 原因：应用被替换后，若新固件没有网络代码，原来的 OTA 接收入口也消失。
- 修复：FSBL 每次启动先运行独立 Agent 的 10 秒窗口，无任务后通过一次性 SRAM 标志复位进应用。
- 只使用最新暂存区与上一版回滚区，备份在每次安装前刷新。
- 备份提交后先写 BACKUP_READY，再擦运行区；复制中断重试不会备份半写的新运行区。
- 修复 +64/+1024 非 4KB 对齐复制，以及重复开关内存映射的问题。
- FSBL 原来只链接弱 ExtMem user driver，本次补齐真实 NOR BSP 与强符号检查。
- 改用带长度与 CRC32 的容器，链式包通过 NetX API 提取；超时和截断不写 Flash。
- Flash 内容校验后提交暂存有效标志，电脑等待板子确认才报告完成。
- 新增网口回滚指令，普通应用不再需要启动确认；旧试运行协议仍兼容。
- 主机模拟测试覆盖 6 轮升级、上一版回滚、断电重试与网络传输失败。真机验证待进行。

使用步骤见 OTA_GUIDE.md。


## 2026-10-18 FSBL 签名布局修复

现象：板上串口与网口均无输出。开发模式下 ST-LINK 识别 N647 Rev B，供电约 3.15V。
读回 Binary 对应区域确认：应用和 Agent 正常，BOOTREC 空白；FSBL payload 从 +0x240 开始，
签名头 entry=0x34182021 (Default_Handler)，实际 Reset_Handler=0x34181FC5。
原因：使用 STM32CubeProgrammer 2.22 签名时漏加 -align。
修复：tools/build_ota.py 与 FSBL/postbuild.sh 都加入 -align；新校验器检查 payload +0x400、
真实复位入口、ROM 头版本/长度/校验和。5 项签名回归测试通过。
修正镜像已发布至 Binary 和 Build/ota，FSBL 签名镜像为 35968 字节。
ST 官方说明：https://community.st.com/t5/stm32cubeprogrammer-mcus/signingtool-for-stm32n6-in-stm32cubeprogrammer-v2-21-0/td-p/859154
板上旧内容备份在 Build/board_read/；修正后的重烧与启动验收继续进行。

2026-10-18：已通过 ST-LINK SN 0814060A2316303030303032、AP1 对 N647 Rev B 重烧修正 FSBL。
只擦写外部 NOR 扇区 0..8；Programmer 校验成功。另读回 64KB，前 35968 字节与签名镜像完全一致，
0x9000 之后与原备份一致。板上 payload_offset=0x400、entry=0x34181FC5。运行模式启动验收待继续。

## 2026-10-18 升级窗口结束后进入 DFU

抓取完整上电日志确认 FSBL 已运行，Agent 完成 ETH/PHY/ThreadX 初始化；
板子 192.168.2.11 通过 TCP 连到电脑，3 次 ping 成功。日志最后为
`AGENT: OTA window expired, starting application`，随后无新 FSBL 日志，USB 枚举为 ROM DFU。
对应用户观察到 LED 闪约 20 秒后熄灭。

修复：所有 OTA 复位经 SRAM executor，退出映射后按现有 NOR BSP 的 OPI 指令格式发送
0x6699/0x9966，等待 Flash 复位完成，再按 ES0620 2.2.22 恢复 RCC_CCIPR6/7/8/13、
选择 core power-on reset 类型并使用 CMSIS SYSRESETREQ。整段不能返回 XIP。
原启动标志 0x341B0080 属于 AXISRAM2，系统复位/ROM 装载会破坏，改至保留区
0x34000000；Agent/本工程应用 RAM 从 0x34000100 开始，避免启动清零覆盖。
FSBL 在关闭 D-cache 后读取标志。模拟器新增系统复位清空 AXISRAM2 的回归场景；
构建检查 SRAM executor 小于复制范围、反汇编无外部 BL/BLX。

修正 FSBL 35936 字节、Agent 80288 字节已通过 ST-LINK 写入并逐字节读回一致，
日志位于 Build/board_read/runtime_bootfix.log。运行模式验证继续进行。

真机验收结果：修正后日志为 `AGENT: OTA window expired` → SRAM executor rc=0 →
新一轮 `[BM] boot manager` → `[BM] -> app` → `FW-TARGET v3` →
`ThreadX: UI thread started`。用户确认屏幕已亮；应用运行后连续 5 次 ping 全成功。
当前已验证冷启动、窗口超时后的软复位、一次性跳过窗口、应用界面及持续网口响应。
尚未在真机执行新固件传输与回滚，不把模拟器结果当作实机升级验收。

## 2026-10-18 屏幕 Yes 无响应：应用更新准备

代码确认：初始 g_selected=-1，即使只有一个有效暂存固件也必须先点条目；
选中条目只重画条目，没有重画 Yes 按钮，会保持灰色外观。
板上应用还是修复复位路径前的版本；先前只更新了 FSBL 和 Agent。

修正：自动选中唯一有效暂存固件，选中时重画 Yes，空条目不可选择，
接收时不处理安装触摸并清除旧确认动作，安装失败在屏幕显示错误。
保留两次确认：第一次 Yes 打开确认页，第二次 Yes 才请求安装。
新增 LCD/触摸初始化状态、GT 产品 ID、点击坐标及确认动作的串口日志。
GT 初始化失败不再悄悄返回成功。tools/test_model_select.py 的生产 UI 事件模拟通过，
同时连续升级/回滚模拟仍通过。修正版应用已重新编译，等待下载模式后只更新 Appli。
板上最新暂存描述为 touch，约 44KB，烧录 Appli 时必须保留该暂存区。

## 2026-10-18 软件包 01～61、99 与模型适配（源码/生成物，尚未烧板）

检查外部软件包确认：公共 FSBL 为 600MHz，五个 AI FSBL 为 800MHz，另有 VDDIO4
初始化差异，当前平台已启用对应 3.3V 域。预编译应用均在 0x70100400；AI 模型从
0x70200000 开始，最大结束于 0x70AB7351，会覆盖旧暂存区，不能沿用旧分区直接烧模型。

兼容编译开关 LED2_PACKAGE_COMPAT：运行区约 10MiB、最新暂存 10MiB、上一版备份 10MiB；
Agent 迁到 0x71F00400。接收缓冲在 PSRAM 0x90000000，大小 10MiB。
V4 容器带启动配置；运行前按标准/AI 时钟重新初始化外存。运行头记录镜像长度和配置，
应用与模型、配置一起保存/恢复。旧布局仍可单独编译，兼容产物输出 Binary/compat。

已生成并逐段核对 77 个 SDK 升级包，覆盖全部 01～61 编号和 5 个 AI 应用；
最大合并镜像 10,186,577 字节。主机模拟通过两种布局的连续升级、配置保存、大包模型
配对回滚、V4 解析以及迁移数据检查；网页上传也验证了配置字段保留。
兼容构建检查签名、向量、真实 NOR 驱动与 SRAM executor 的大小和无外部调用。

兼容固件及 initial_install.hex 尚未写入板子。初始安装会初始化新布局记录，需先备份；
离线迁移工具可保留有效暂存与回滚，旧管理程序会同步更新以免继续使用旧分区地址。
步骤和限制见 SDK_COMPAT_GUIDE.md；所有生成升级包在 Binary/sdk_packages。

## 2026-10-18 991 三缓冲优化（已编译，尚未烧板）

用户报告 991 的 CPU load 约 42%、Inference 约 42ms、FPS 约 15。
核对原应用 HEX 与 Release ELF 一致，源码和二进制均只配置两个 AI 输入缓冲。
采集回调需先取得下一块空闲缓冲才提交当前帧；30 FPS 输入、42ms 处理时，
双缓冲会等下一次帧边界，主机时序模拟约 15 FPS，三缓冲约 23.8 FPS。

用户授权修改后，在 SDK_Adapted/991_AI_People_Detection 保存修改版 app.c、
app_config.h；原外部软件包未写入。NN_INPUT_BUFFER_COUNT=3，队列容量同步增加。
额外 PSRAM 307200 字节；链接后 EXTRAM 起点 0x91000000、长度 4761600 字节。
模型、分辨率、时钟和其他业务逻辑保持原值。

tools/build_991.py 完整重编译 306 个源文件。与原版对象逐段比较，仅 app.o 和
app_bqueue.o 改变，其余 304 个对象代码/数据一致（Build/991-triple-buffer/original-comparison.json）。
实际队列/采集回调主机测试、输出队列、包 CRC/模型逐段比较、发送端和 77 原版包检查通过。

产物在 Binary/991_triple_buffer：appli.hex、配套模型、application_and_network.hex、
991_triple_buffer.ota。当前运行配套 991 的板子可经 Programmer 只更新 appli.hex；
网口 .ota 仍要求已完成兼容 FSBL/Agent 和布局迁移。此次没有烧录，实际 FPS 待验证。

## 2026-10-18 991 第二轮 SRAM/MVE 优化（已编译，尚未烧板）

用户反馈第一轮三缓冲版 Inference 仍约 42ms，FPS 已约 25（此前约 15）。
按用户继续优化要求，单独创建 SDK_Adapted/991_Performance，保留第一轮文件。

第二轮将三个输入缓冲移入内部 BSS（0x34008620 起，921600 字节，结束 0x340E9620），
BSS 结束 0x340FED30，系统栈仍为 0x34200000，下方余约 1MiB。模型工作区从
0x34200000 起，图像/模型/系统栈区域未重叠。LCD 缓冲仍在 PSRAM，长度 3840000 字节。

输入转换使用 MVE 16 字节 XOR，与原 uint8→int8 数值逐字节等价；补充首次 DMA 前
clean/invalidate、采集完成后 invalidate、转换后 clean、推理完成后 invalidate 的缓存交接。
仅对 app、app_postprocess、Model/network 及 ll_aton_lib 两个源文件改 -O3，无 fast-math。
CPU/NPU/外存时钟、模型参数、输入分辨率和业务逻辑保持原值。
新增屏幕 Prep/NN/PP/Draw/CPU MHz，Inference 保持准备+模型调用的原计时范围。

完整编译 306 单元成功，反汇编确认 MVE load/XOR/store；内部 RAM/栈边界检查通过。
标量及 16-lane shim 转换测试覆盖全部像素值、缓存交接、重复 DMA 帧、尾段及越界保护。
实际采集回调/队列、模型逐段比较、CRC、发送端检查通过。未做硬件性能/精度验证。
与第一轮编译对象比较，只有计划优化的 5 个单元改变，其余 301 个代码/数据一致；
比较报告为 Build/991-performance/baseline-comparison.json。
原 SDK 未写入。新产物 Binary/991_performance，板上有配套模型时只更新 appli.hex 即可；
991_performance.ota 仍要求完成兼容布局。性能增益以烧板后同场景实测为准。

随后用户反馈“没有效果”，确认屏幕没有 SRAM MVE 标识，并说明实际烧录路径为
Binary/991_triple_buffer/appli.hex。因此该反馈对应第一轮文件，不能作为第二轮
性能结果。已提供明确命名的 Binary/991_performance/991_performance.hex，
它与同目录 appli.hex 内容一致；第二轮上板结果仍待用户验证。

## 2026-10-18 991 第三轮实时采集优化（生成物，未由本轮烧板）

用户已确认第二轮约 30 FPS，并回报 Inference/Prep/NN/MHz 为 33/1/33/800。
这些独立毫秒计时存在取整，不能相加后当作精确小数。瓶颈主要在模型调用，
单帧约 33ms 对应约 30 次推理每秒；用户要求同时提高识别率和降低跟随延迟。

SDK_Adapted/991_Realtime 新增三个输入缓冲的“只保留最新待处理帧”交接，
推理缓冲不得被采集重用。先取输出空位，再领取最新输入，避免后处理阻塞时
提前占有旧画面。运行环境/网络只初始化一次，每次调用 Reset_Network；
原模板每帧会重建 ThreadX 对象并重拷贝约 7KB epoch 程序。模型和时钟保持原值。
预览去掉一帧延时，保留三个背景缓冲以等待 LCD 垂直消隐。计时随输出槽位传递。

RT30 使用原 2592x1944/30 FPS 模式；另建 RT60 试验版使用 1296x972 2x2 binning，
目标采集 60 FPS，模型输入仍 320x320。按 ST 的 Zephyr 驱动寄存器事实适配，
同时修改曝光行时间/上限、ISP 统计区域和垂直翻转相位配置。此摄像头模式在
本板的实际输出、方向、颜色、识别精度、曝光和 LCD 撕裂尚须硬件验证。
60 仅为采集目标，不等于识别 60 FPS；保持 NN 33ms 时仍约 30 FPS。

屏幕 RT30/RT60 区分版本；Cam 是实际采集完成数/秒，FPS 是推理完成数/秒，
Age 是输入帧采集完成到开始画检测结果的毫秒数，不含曝光、LCD 扫描和机械动作。
产物分别在 Binary/991_realtime_30 和 Binary/991_realtime_60_trial；旧性能版保留。
网口包仍要求已完成兼容 FSBL/Agent 布局，板上有配套模型时可只刷对应命名 HEX。

两版各 306 单元完整编译通过，输入起点 0x34008840、921600 字节，BSS 结束
0x340FEF60，外部显示缓冲 3840000 字节；MVE 指令和各区域边界检查通过。
生产代码主机测试通过：最新帧覆盖/推理缓冲保护、取信号后的中断交错、计数回绕，
100 次复用运行环境、真实传感器驱动的曝光/翻转/错误处理、LCD 换帧的消隐相位遍历。
两版所有文件哈希、模型原字节、每个 HEX 段对应的 OTA 数据和 CRC 均通过；
已有性能版的缓存转换/队列/打包回归也通过。这些检查不代表硬件性能或精度结果。

## 2026-10-18 RT60 噪点反馈及 RT30 R2 恢复

用户已实际烧入 991_realtime_60_trial.hex，反馈“效果不是很好，全是噪点”。
RT60 的硬件成像验证失败，暂停使用；保留原失败二进制以便追溯，并标记 manifest
和目录提示。高帧率分支改变了读出/曝光/行场时序，仍沿用原 ISP 调校参数。
噪点的具体原因尚未确定，不能据现象区分 CSI/读出/ISP 问题与短曝光、高增益。
已询问是无法辨认景物的雪花还是景物仍可见的颗粒，并请确认 RT30 是否恢复。

生成 RT30 R2：构建时排除 app_camera.c、cmw_imx335.c、imx335.c 三个试验适配文件，
直接编译原 SDK 三份摄像头源文件，恢复原自动探测初始化和完整摄像头驱动。
保留最新帧处理、推理环境复用、SRAM/MVE 和预览一帧等待优化。屏幕显示 RT30 R2。
source.patch 排除未采用的摄像头修改，manifest 新增实际 source_overrides 列表。
RT60 显式构建仍供诊断，但会提示已发生硬件成像失败，不作为推荐产物。

RT30 R2 完整 306 单元编译通过。三个摄像头 .o 与用户此前正常的性能版逐字节
完全一致；队列所有权/异步运行/换帧、模型配对、OTA CRC 和输出哈希检查通过。
失败 RT60 的原二进制未改写，只补充失败状态和目录提示。R2 未由本轮烧入板子，
需用户更新应用并断电重上电后确认画质，不能宣称噪点已在硬件上修复。

## 2026-10-18 烧录文件清理

用户要求指出 OTA 烧录入口并删除没用的其他烧录文件。推荐兼容布局首次安装入口为
Binary/compat/initial_install.hex；它包含 FSBL、Agent、管理应用和初始化记录，
会替换当前应用并初始化升级/回滚状态。日常例程包保留在 Binary/sdk_packages。
文件用途统一在 Binary/README.md，根 README 和兼容说明已同步。

删除 Binary/991_triple_buffer、Binary/991_performance、Binary/991_realtime_60_trial
三个过时发布目录，并删除 Build/ota、ota-compat 和四个 991 构建目录中的重复 HEX/BIN
发布副本，共 38 个文件、84,110,585 字节。保留全部源码、构建对象/必要中间二进制、
当前 RT30 R2、77 个 SDK 升级包、兼容安装组件和 Build/board_read 的真实板子备份。
根 Binary 的旧 512KB 布局恢复镜像仍有恢复用途，保留并注明不可与兼容版混烧。

删除前校验两套 OTA 和 RT30 的所有发布哈希、合并安装镜像与三个组件逐字节一致，
并验证兼容 FSBL 签名头、0x400 载荷偏移和入口 0x34182221。
删除目标先解析为工程内绝对路径并排除重解析点；删除后 191 个保留文件哈希均未变化。
删除清单及哈希保存在 Build/firmware_cleanup_20261018.json，仅记录文件清理，未操作板子。

## 2026-10-18 网页无法选择 OTA 包

用户反馈升级包不是 HEX，无法发送。实际 GET 127.0.0.1:17800 页面证实，
旧后台 PID 12744 仍提供仅允许 .bin/.hex 的选择框；磁盘新版已支持 .ota。
旧后台状态为 done，无活动升级。保存旧状态到 Build/ota-web 后，重启为新版
PID 25212；实际 HTTP 页面已核对 accept 包含 .ota，新后台为 idle，未发送固件。

网页明确提示 .ota 直接发送、无需改后缀；HTML/API 增加 Cache-Control: no-store。
5 项升级包测试通过，包括 77 包的原应用/模型逐段比对及网页上传保留启动配置。
首次安装仍由 Programmer 烧 Binary/compat/initial_install.hex，日常 .ota 包由网页发送。

## 2026-10-18 4 MB OTA 连接中断及存储确认

用户已烧入完整 compat/initial_install.hex。复位后的真实串口确认新版 FSBL 启动
0x71F00400 的 Agent，10 MB 外部接收缓冲初始化成功；无需因旧区域有内容而整片擦除。
旧发送流程两次在电脑进度约 1.4～1.8 MB 时出现 WinError 10054；板端返回 rc=-11，
只能确认接收连接出错，不能将此状态码直接解释为容量或 CRC 错误。

诊断改为先发 24 字节包头、正文每次至多 1024 字节并间隔 1 ms，关闭 Nagle。
约 4 MB 的同一个 991 包完整接收，随后整包 NOR 操作期间屏幕停在 99%，串口及网络
处理暂停。诊断发送器仍提前半关闭连接，最终电脑报连接中断，但板端稍后明确输出
“OTA: verified Slot-0, crc=04CEBCE7”。用户触屏确认后，FSBL 完成应用/模型复制，
输出 CONFIRMED (persist) 并跳转应用，串口持续输出云台 JSON，说明本次确已运行。

生产发送器保留上述包头探测和正文节奏，传输中读取板端拒收回复；大包等待确认上限
600 秒。去掉提前 shutdown(SHUT_WR)，保持连接直到取得完整 16 字节存储确认。
推测提前 FIN 与长时间中断屏蔽共同影响 TCP 连接，此因果关系尚未通过板上对照验证。
电脑提交进度与板端存储成功分开显示，缺失确认仍报告失败，不能仅凭电脑发送满量
把升级标成成功。本轮未修改板上整包 Flash 写入流程，也未重新烧录 FSBL/Agent。

两套布局的生产解析/存储/启动 NOR 模拟检查和 6 项发送器测试均通过，包含大包、
提前拒收、伪造提前成功、分片确认、禁止确认前发送 FIN 及重复连接。新发送流程的
完整板上 ACK 仍待下一次实际升级验证。后续从不带 OTA 的 991 程序升级时，先在
网页点发送，再按一次复位进入开机升级窗口；接收、写入期间保持供电且不要复位。

收尾时发现另一个旧 ota_gui.py 进程 PID 8644 占用网页端口，单独重启先前记录的
25212 进程并不能更新实际页面。确认后台无升级任务后，停止旧监听者和本轮启动的
重复进程，更新后台为 PID 29492。已通过端口归属和实际 HTTP 响应核对新版进度提示、
no-store 及 idle 状态。此次未再次向板子发送包；COM16 监听已关闭并释放。
