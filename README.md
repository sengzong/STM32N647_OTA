# LED_2_noai — STM32N647 ThreadX/NetXDuo + RGB LCD 触摸 UI + 以太网 OTA 与上一版回滚

## 当前烧录文件入口

支持软件包 1～61、99 AI 及配套模型的 OTA，首次安装使用
[Binary/compat/initial_install.hex](Binary/compat/initial_install.hex)。
它会替换当前应用并初始化升级/回滚记录；保留旧数据需先备份或迁移，详见
[SDK_COMPAT_GUIDE.md](SDK_COMPAT_GUIDE.md)。
日常网口升级文件在 [Binary/sdk_packages](Binary/sdk_packages/README.md)。
当前保留的 991 实时版在 Binary/991_realtime_30；旧试验烧录文件已清理。
完整文件用途见 [Binary/README.md](Binary/README.md)。

以下原有说明对应根目录 Binary 中的旧 512KB 布局恢复版本，不能与 compat 混烧。

当前版本采用“最新固件暂存 + 上一版回滚备份”。每次启动先进入独立 Agent 的 10 秒升级窗口，因此应用没有网口代码也能继续升级。首次需更新 FSBL、Agent 和应用，操作见 [OTA_GUIDE.md](OTA_GUIDE.md)。

基于 STM32N6 系列的完整可运行工程:官方 FSBL 引导 + 应用 XIP 执行,ThreadX 实时操作系统,
NetXDuo TCP/IP 协议栈,RGB LCD + 电容触摸的槽位选择 UI,以及通过网口完成的多槽位固件 OTA 升级。

## 功能概览

- **引导**:ST 官方 FSBL(`FSBL/`)完成外部存储初始化后跳转应用;应用代码在 XSPI2 NOR Flash 上 XIP 原地执行
- **Boot Manager**(FSBL 内置,`FSBL/Core/Src/bootmgr.c`):MCUboot 风格"拷贝 + 确认/回滚",见下文
- **常驻 OTA Agent**(`Agent/`,NOR 0x1400000,入口 0x71400400):独立接收程序。
  每次启动先提供升级窗口,应用损坏时持续等待,且它**只写槽位、绝不写引导区**
- **系统**:ThreadX 实时操作系统 + NetXDuo TCP/IP 协议栈(裸机 → RTOS 移植)
- **显示**:800x480 RGB LCD(LTDC),帧缓冲放在外部 PSRAM;触摸选槽 UI(FT5206 / GT9xxx,I2C)
- **网络**:ETH + YT8512C PHY,TCP 客户端方式接收固件
- **OTA**:一份最新固件暂存 + 一份上一版备份，带描述/CRC32 校验，支持网口升级与回滚
- **上位机**:纯 Python 标准库的发送工具(命令行 + 网页拖拽两种)

## 三个工程

| 工程 | 运行位置 | 职责 | 是否参与 OTA 升级 |
|---|---|---|---|
| `FSBL/` | AXISRAM2(0x34180000) | 初始化外部存储、执行 Boot Manager(拷槽位/确认/回滚)、跳转应用或 Agent | 否(SWD 升级) |
| `Appli/` | NOR XIP(入口 0x70100400) | LCD+触摸 UI、ETH/NetXDuo 收包、槽位管理、确认引导 | **是** |
| `Agent/` | NOR XIP(入口 0x71400400) | 只做 ETH/NetXDuo 收包、暂存、请求安装与回滚,无 LCD/触摸 | 否(常驻,不自我更新) |

## 硬件

| 部件 | 型号 / 说明 |
|---|---|
| MCU | STM32N647X0HxQ(Cortex-M55) |
| NOR Flash | MX25UM25645G,XSPI2,OPI-DTR,XIP 基址 0x70000000 |
| PSRAM | W958D8NBYA5I,XSPI1,基址 0x90000000(帧缓冲) |
| 显示 | 800x480 RGB LCD(LTDC) |
| 触摸 | FT5206 / GT9xxx(I2C) |
| 网口 | ETH + YT8512C PHY |

## 目录结构

```
FSBL/            第一阶段引导(官方例程改,AXISRAM2 运行)+ Boot Manager
Appli/           应用工程(ThreadX + NetXDuo + LCD 触摸 UI + OTA)
Agent/           常驻 OTA Agent 工程(复用 Appli 源码,无 LCD/触摸,只写槽位)
Drivers/         CMSIS + STM32N6 HAL + 板级 BSP(LED/RGBLCD/TOUCH/UART/EEPROM/YT8512C...)
Middlewares/     Azure RTOS(ThreadX / NetXDuo)+ ExtMem_Manager
Secure_nsclib/   安全非安全调用接口头文件
Binary/          烧录产物(appli.hex / agent.hex / fsbl_universal.hex)
ota_gui.py       PC 端 OTA 发送工具(网页版,带描述,多槽位)
ota_send.py      PC 端 OTA 发送工具(命令行,裸流,仅 Appli 可用)
OTA_DEBUG_NOTES.md   OTA 调试记录
LOCAL_NOTES.md       工程内约定与"重新生成后会丢的东西"
```

## 构建

1. 安装 **STM32CubeIDE 1.17+**(内置 GCC 12.3)
2. 导入仓库根目录下的三个工程:`FSBL`、`Appli`、`Agent`
3. 各构建 **Release** 配置
4. 每个工程各有一个 postbuild 脚本,自动生成 `Binary/` 下的镜像:
   - `FSBL/postbuild.sh` → 签名(`-t fsbl -hv 2.3 -align`)、校验 0x400 对齐与真实入口、ihex@`0x70000000` → `Binary/fsbl_universal.hex`
   - `Appli/postbuild.sh` → ihex@`0x70100400` → `Binary/appli.hex`
   - `Agent/postbuild.sh` → ihex@`0x71400400` → `Binary/agent.hex`

> `Agent` 工程没有独立的源码副本:它通过 Eclipse 链接资源直接复用
> `Appli/Core/Src/app_netxduo.c`、`app_ota_flash.h` 等,只是多定义了 `LED2_OTA_AGENT`
> 并按自己的链接脚本(入口 `0x71400400`)编译。因此**改 OTA 收包逻辑只需改 Appli 一处**,
> 两个映像天然一致;反过来,任何改动都要**同时重编 Appli 与 Agent**。
> 工程文件由 `.workbuddy/tools/gen_agent_project.py` 生成(改链接资源后重跑它)。

> 注意:Appli 必须使用 Release(XIP)配置运行,链接脚本 `STM32N647X0HXQ_ROMxspi2_RAMxspi1.ld`;
> Debug(LRUN)配置仅用于 SRAM 调试,帧缓冲放不下会黑屏。CubeMX 重新生成代码后
> 链接脚本/postbuild 会被重置,需要恢复(详见工程内 `.cproject` 与 OTA_DEBUG_NOTES.md)。
>
> FSBL 工程同样会被重生成打回:它必须链接 `stm32n6xx_hal_uart.c`、`stm32n6xx_hal_xspi.c`
> 与 `Drivers/BSP/HyperRAM/hyperram.c`,并带 `../../Drivers/BSP/HyperRAM` 包含路径,
> 详见 LOCAL_NOTES.md §七。
>
> `Agent` 工程不在 `.ioc` 的生成范围内,所以 CubeMX 重生成**不会动它**;
> 但它复用的 `Appli/Core/Inc` 头文件与 `app_netxduo.c` 会被重生成影响,改完记得重编 Agent。
> Agent 自身的必查项见 LOCAL_NOTES.md §八。

## 烧录

使用 STM32CubeProgrammer 烧录 `Binary/` 下的三个 hex:

- `fsbl_universal.hex` — FSBL(NOR 0x00000000 区域)
- `appli.hex` — 应用(XIP,入口 0x70100400)
- `agent.hex` — 常驻 OTA Agent(XIP,入口 0x71400400;**只需烧一次**,它不参与 OTA)

BOOT0=0 上电即由 FSBL 引导应用。

> FSBL 只在「新增外设 / 换了板级驱动 / 改了 bootmgr」时才需要重烧;
> 日常改应用只烧 `appli.hex` 即可。
> `agent.hex` 是"保险丝":只有 FSBL 跳不到应用时才会用到它。
> **首次启用 Boot Manager 时建议三个一起烧**,那样才具备完整的救援能力。
> 不烧 `agent.hex` 也不会坏 —— FSBL 只是多打印一句 `[BM] WARN no valid agent partition`
> 然后回退去启动应用。

### 烧录命令

```bash
bash .workbuddy/tools/flash.sh probe     # 只列调试器，确认插的是 N647 板
bash .workbuddy/tools/flash.sh all       # 首次：全片擦除 + 依次烧 fsbl / appli / agent
bash .workbuddy/tools/flash.sh appli     # 日常改应用：只烧 appli.hex
bash .workbuddy/tools/flash.sh stack     # appli + agent
```

三个镜像都在外部 NOR（XSPI2 映射 0x70000000），所以擦写都要**板级专用 external loader**：

```
F:\STM32CubeProgrammer\bin\ExternalLoader\MX25UM25645G_ATK-CNN647B_ExtMemLoader.stldr
```

手工等价命令、`-w` 为什么不该跟地址、以及各 hex 的基址见 `LOCAL_NOTES.md` §四。

> ⚠️ **别插错板子**：这个 loader 只对 ATK-CNN647B / STM32N647 有效。
> `flash.sh` 会先读板名，读到 `NUCLEO-C542RC`（另一个 C542 项目的板子）就**直接拒写**；
> 手工拷命令时没有这层保护，要自己确认。

### 首次上机验证顺序（Layer 1 → Layer 2）

1. 烧 `fsbl_universal.hex` + `appli.hex`，BOOT0=0 上电 → 串口出现 `[BM]` 引导日志，屏幕进 model_select 页；
2. 用 `ota_gui.py` 发一个镜像 → 期望 FSBL 记 `TRIAL_BOOTED`，3 秒后应用确认成 `CONFIRMED`；
3. 故意发一个 CRC 不对的坏镜像 → 期望 FSBL 用 golden backup 回滚，并跳去 Agent；
4. 在 Agent 模式下再发一次容器 → 验证「收到即写槽位 + 请求试运行」的自愈闭环。
   判定 Agent 在跑：串口横幅 `resident OTA AGENT [AGENT-TARGET v1]` + LED0 慢闪 4Hz。

## 内存布局

| 区域 | 地址 | 大小 | 用途 |
|---|---|---|---|
| AXISRAM2 | 0x34180000 | 512KB | FSBL 代码+数据(引导后让出) |
| AXISRAM1+2 | 0x34000000 | 2MB | 应用数据(ThreadX/NetX/OTA 缓冲/栈) |
| XSPI2 NOR XIP | 0x70000000 | 32MB | 见下表(引导区 / 槽位 / 备份 / 记录) |
| XSPI1 PSRAM | 0x90000000 | 32MB | 帧缓冲 / 大块数据 |

NOR 偏移布局(`bootmgr.h` 为准):

| 偏移 | 大小 | 用途 |
|---|---|---|
| 0x000000 | 1MB | FSBL 映像(仅 SWD 升级,引导管理不碰) |
| 0x0FC000 | 4KB | **BOOTREC 引导记录**(32B,自带扇区) |
| 0x100000 | 3MB | 引导区 `[0x400 保留头 | 应用映像]`,入口 `0x70100400` |
| 0x400000 | 4MB | 最新固件暂存 `[64B 头 | 镜像]`；旧 0x800000 / 0xC00000 槽位不再使用 |
| 0x1000000 | 4KB | golden backup 头(64B,自带扇区) |
| 0x1001000 | 4MB | 上一版备份数据(每次升级前刷新) |
| 0x1400000 | 4MB | **常驻 OTA Agent 分区** `[0x400 保留头 | 映像]`,入口 `0x71400400` |

## OTA 使用

完整步骤见 [OTA_GUIDE.md](OTA_GUIDE.md)。电脑有线网卡设为 `192.168.2.10/24`，板子为 `192.168.2.11`。

1. 运行 `python ota_gui.py`，选应用固件并点“发送新固件”；
2. 复位或重新上电板子，独立 Agent 在启动窗口接收、校验并暂存固件；
3. FSBL 保存当前应用为上一版备份，再安装并启动最新固件；
4. 下一次仍可用同样步骤升级，即使新应用完全没有网口代码。

网页的“回滚上一版”也按“先点击、再复位”的顺序使用。命令行：

```
python ota_send.py --file Binary/appli.bin --desc "Camera v2" --yes
python ota_send.py --rollback --yes
```

固件必须链接到 `0x70100400`，最大 524,224 字节。发送工具等待板子校验写入确认，
不再使用无长度信息的旧裸流。当前应用运行期间若没有网络功能，需要手动复位才能进入升级窗口。

## Boot Manager(BOOTREC 协议)

普通安装使用 `REQ_PERSIST`，无需应用端确认；`REQ_ROLLBACK` 从上一版备份恢复。以下启动确认机制只适用于旧 `REQ_TRIAL` 请求。待安装记录的 `rsv[0]` 可由 FSBL 标为 `BACKUP_READY`，防止拷贝中断后覆盖有效备份。

`FSBL/Core/Inc/bootmgr.h` 与 `Appli/Core/Inc/app_ota_flash.h` 是同一份协议的**两侧拷贝**,
magic、状态码、32B 布局、CRC 覆盖范围必须逐字节一致(改一侧就要同步另一侧)。

32 字节记录 = `magic | state | slot | seq | img_len | rsv[2] | crc32`,其中 `crc32` 位于偏移 28,
覆盖 `[0, 28)`。

| state | 写入方 | 含义 |
|---|---|---|
| 0 IDLE | — | 无待办 |
| 1 REQ_TRIAL | 应用 | 请把该槽位拷入引导区,作为"试运行"启动 |
| 2 REQ_PERSIST | 应用 | 同上,但直接采纳(不参与回滚) |
| 3 TRIAL_BOOTED | FSBL | 试运行已跳转,等待应用确认 |
| 4 CONFIRMED | 应用 | 确认采纳,保留当前固件 |
| 5 ROLLED_BACK | FSBL | 已用 golden backup 回滚 |
| 6 REQ_AGENT | 应用 | 请启动常驻 OTA Agent 分区 |
| 7 REQ_ROLLBACK | 接收程序 | 请恢复上一版备份 |

FSBL 串口日志(USART1 / PE5-TX PE6-RX / 115200,与 App 同一路)会打印 `[BM]` 前缀的每一步结果,
例如 `[BM] rec: state=1 slot=0 seq=2` / `[BM] TRIAL_BOOTED` / `[BM] rolled back ok, len=0x...`。

## 常驻 OTA Agent

Agent 固定在 NOR `0x1400000`，入口 `0x71400400`。它不随普通应用 OTA 被覆盖。
每次正常启动先等待 10 秒，无任务就通过一次性 SRAM 标志复位进入应用；收到数据后等待完整传输，
校验并写入暂存区，再请求 FSBL 安装。应用分区损坏或显式请求救援时持续等待。

Agent 接收普通固件时使用 `REQ_PERSIST`，不要求摄像头等应用实现启动确认协议。
上一版备份在每次安装前刷新，用户可通过网口请求回滚。旧 `REQ_TRIAL` 协议继续支持。

串口标识为 `resident OTA AGENT [AGENT-TARGET v2]`。Agent 等待时不初始化 LCD，
监督线程负责启动窗口和 LED 心跳；网络接收线程与本工程应用共用 OTA 源码。

推荐构建和验证：

```
python tools/test_ota.py
python tools/build_ota.py
```

`Binary/` 产出三份可烧录 HEX 和 `appli.bin`。编译检查保证 FSBL 使用真正的板级 NOR 驱动，
Agent 不包含应用确认路径。模拟检查覆盖连续升级与回滚，真机还需验证网口、软复位和新应用启动。

## 软件包例程兼容版本

`python tools/build_ota.py --compat-package` 生成配套固件到 `Binary/compat`；
`python tools/package_sdk.py` 将 1～61 和 99 的预编译应用/模型生成到 `Binary/sdk_packages`。
兼容版包含 600/800MHz 启动配置、PSRAM 大包接收、应用与模型成套备份及回滚。
它使用新的 NOR 分区，首次安装与旧数据迁移步骤见 [SDK_COMPAT_GUIDE.md](SDK_COMPAT_GUIDE.md)。
本轮兼容版本尚未在板子上验收，原始 SDK 工程未修改。

## License

[MIT](LICENSE)
