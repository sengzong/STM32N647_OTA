# 1～61 与 99 AI 应用的 OTA 适配

兼容代码、固件和升级包已生成并通过主机验证。当前板已确认兼容 FSBL/Agent 启动，
约 4 MB 的 991 包接收、暂存校验及安装运行；尚未逐个上板运行所有例程。
软件包原工程未被修改；直接使用它们现有的 Binary/appli.hex 和对应模型生成升级包。

## 为什么需要配套版本

普通例程共用的软件包 FSBL 配置 CPU 600MHz；99 中五个 FSBL 配置 CPU 800MHz，
并启用另外两路 PLL。当前兼容 FSBL 按升级包的配置，在进入应用前选择对应时钟，
从 SRAM 中重新配置外部存储接口。独立接收程序自身使用 800MHz。

`network-data.hex` 是 AI 模型参数。五个应用均从 0x70100400 启动，模型从
0x70200000 开始；最大的模型结束于 0x70AB7351。不能只更新 appli，也不能把
network-data 当作另一个可执行固件。新 .ota 包将应用和对应模型合并，空洞补 FF，
原地址保持不变，整包 CRC32 覆盖应用与模型。

## 已生成的内容

- `Binary/compat/initial_install.hex`：首次安装文件，包含兼容 FSBL、独立 Agent、管理界面及初始化记录。
- `Binary/compat/fsbl_universal.hex`、`agent.hex`、`appli.hex`：同一版本的三个独立镜像。
- `Binary/sdk_packages/README.md`：77 个例程的清单，覆盖 01～61 的全部编号及其子例程、五个 99 AI 应用。
- `Binary/sdk_packages/*.ota`：网页或命令行直接发送的升级包；配套 JSON 记录源文件哈希和原始地址。
- 当前保留的 991 RT30 R2 在 `Binary/991_realtime_30`，网口升级使用同目录
  `991_realtime_30.ota`，已包含模型；源代码见
  [实时版说明](SDK_Adapted/991_Realtime/README.md)。
- 旧三缓冲版、性能旧版和噪点严重的 RT60 试验版产物已清理，源码保留。
- 所有烧录入口统一见 [Binary/README.md](Binary/README.md)。

最大的合并镜像为 992 图像分类的 10,186,577 字节，接收缓冲使用 PSRAM 的前 10MiB。
99 应用进入自身启动流程后可重新使用这块 PSRAM。

## 首次更新板子

这次分区改变了，旧 0x71400400 的 Agent 和新 0x71F00400 的 Agent 不能混用。
不能通过旧版 512KB 的普通应用 OTA 接口直接完成这次布局升级。

1. 先保存当前板子的 NOR 内容。下载模式为 BOOT1=1、BOOT0=0，断电重上电；
   Programmer 使用板级 external loader、SWD/Hot Plug/AP1。
2. 若要重新初始化，检查并烧录 `Binary/compat/initial_install.hex`，校验成功后再运行。
   **此文件会替换当前管理应用，并初始化新的暂存、回滚及启动记录。旧数据应先备份。**
3. 若要迁移现有暂存和回滚数据，先读回完整 32MiB NOR，使用下面的离线迁移工具。
   它生成待检查的 migration.json / migration.hex，不会操作硬件。
4. 烧录完成后，断电并切回 BOOT1=0、BOOT0=0，再重新上电。
5. 检查串口的 `package layout, 10 MB external receive buffer ready`，以及无升级任务时能够进入管理界面。

离线迁移示例：

```powershell
python tools/migrate_ota_layout.py --dump Build/old-nor-32mb.bin --staged-profile 1
```

profile 0 对应当前工程的 800MHz 管理程序，1 对应普通 SDK，2 对应 SDK AI。
示例的 staged-profile=1 适用于已暂存的原版 27_Touch；其他固件按其实际来源指定。
该工具只适合本工程的旧 OTA 布局。检测到旧管理程序时，会同步换成兼容版管理程序，
避免旧接收代码继续写旧分区；涉及的位置在 migration.json 中明确列出。
迁移前的精确原始内容始终保留在输入的整片读回文件中。

## 以后如何升级

1. 关闭旧的 OTA Python 后台并重新运行 `python ota_gui.py`；刷新 http://127.0.0.1:17800。
   网页应显示“选择 SDK 升级包 (.ota) 或应用 (.bin / .hex)”。若仍只能选 HEX，
   说明旧后台或旧页面仍在使用；只刷新页面不会重新加载旧 Python 进程中的发送代码。
2. 从 `Binary/sdk_packages` 选择需要的 `.ota` 文件，填写描述并点击发送。
3. 复位或重新上电板子，由独立 Agent 接收并安装；无需在 SDK 应用中增加网口代码。
4. AI 应用与模型一起切换，回滚时也一起恢复。原来的网络回滚按钮仍然可用。

命令行示例：

```powershell
python ota_send.py --file Binary/sdk_packages/43_Camera.ota --desc Camera --yes
python ota_send.py --file Binary/sdk_packages/99_Applications__991_AI_People_Detection.ota --desc People --yes
```

大包的写入、备份和首次启动会更慢；网页“板子已确认”表示已完成暂存校验并接受安装请求，
实际复制和应用运行结果仍应看串口。屏幕安装页依旧保留 Yes 确认流程。

如果重新编译了 SDK 的某个例程，重新生成升级包即可：

```powershell
python tools/package_sdk.py
```

## 新的 NOR 分区

| NOR 偏移 | 用途 |
|---|---|
| 0x000000～0x100000 | FSBL，启动记录仍在 0x0FC000 |
| 0x100000～0xB00000 | 当前应用及模型；向量表 0x70100400 |
| 0xB00000～0x1500000 | 唯一的最新暂存区 |
| 0x1500000～0x1F00000 | 唯一的上一版备份区，首扇区保存备份头 |
| 0x1F00000～0x2000000 | 独立 Agent；向量表 0x71F00400 |

运行区最后一扇区留作容量余量，最大合并镜像为 0x9FEC00 字节。
应用/模型保持原链接地址，只有暂存、回滚和 Agent 位置改变。

## 验证范围

```powershell
python tools/build_ota.py --compat-package
python tools/package_sdk.py
python tools/test_sdk_packages.py
python tools/test_migrate_ota_layout.py
python tools/test_ota.py
```

已验证所有升级包的原始地址、向量与 CRC；逐段比较了 77 个包和原始 HEX；
主机 NOR 模拟覆盖多轮升级、两种布局、启动配置保存、大应用/模型安装及配对回滚；
迁移模拟覆盖暂存与回滚 CRC、完整扇区保存和旧管理程序更新。

约 4 MB 的 991 包已通过板上接收、存储和启动；最大约 10 MB 包、600/800MHz
交替升级、全部 AI 应用及各例程外设仍需逐项真机验证。
电脑端最新发送修正已通过主机测试，完整板上存储确认回复仍待下一次实际升级验证，
详见 OTA_DEBUG_NOTES.md 的 4 MB OTA 记录。
SD 卡、摄像头连接、传感器、字库等配套资源仍按各原例程的要求准备。
