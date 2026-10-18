# 烧录与 OTA 文件入口

| 用途 | 文件或目录 |
|---|---|
| 首次安装支持 1～61、99 AI 的 OTA | [compat/initial_install.hex](compat/initial_install.hex) |
| 后续通过网线升级例程 | [sdk_packages/README.md](sdk_packages/README.md) 中对应的 `.ota` |
| 当前保留的 991 RT30 R2 | [991_realtime_30/991_realtime_30.hex](991_realtime_30/991_realtime_30.hex) |
| 当前 RT30 的应用和模型一起网口升级 | [991_realtime_30/991_realtime_30.ota](991_realtime_30/991_realtime_30.ota) |

`compat/initial_install.hex` 已包含 FSBL、独立网口 Agent、管理应用和初始化记录。
用 STM32CubeProgrammer 与板级 external loader 烧入；HEX 自带地址。
**首次安装会替换当前运行应用，并初始化暂存、回滚及启动记录。需要保留板上数据时，
先读回备份，或按 [兼容说明](../SDK_COMPAT_GUIDE.md) 生成迁移镜像。**

安装后启动根目录的 `ota_gui.py`，选择例程的 `.ota` 文件发送，再复位板子。
AI 包已包含配套模型。`.ota` 由上位机发送，不能当作 HEX 交给 Programmer 烧录。

`compat/` 内的 fsbl_universal.hex、agent.hex、appli.hex 是同一版本的独立组件，
保留用于单独维护和迁移工具，首次安装时使用合并的 initial_install.hex 即可。

根目录的 fsbl_universal.hex、agent.hex、appli.hex/appli.bin 是已使用过的
旧 512KB OTA 布局恢复文件。旧 Agent 位于 0x71400400，兼容版位于 0x71F00400，
**两套文件不能混烧**。支持 SDK 大应用/模型时使用 compat 整套。

已清理废弃的 991 三缓冲版、性能旧版、RT60 噪点试验版和 Build 中的重复烧录镜像。
源码仍在 SDK_Adapted，必要时可以重新构建。Build/board_read 保存板子读回备份，未清理。
