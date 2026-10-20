STM32N647 完整烧录包合成器（Windows 10/11 64 位便携版）

使用方法
1. 解压整个文件夹，双击 InitialInstallMerger.exe。无需安装 Python 或 CubeIDE。
2. 在工程内运行时，默认优先选用当前 Binary/compat 固件；移出工程则选用随附 firmware 文件夹。
3. 保持“本工程管理应用 · 800 MHz”，选择输出目录，点击“合成 initial_install.hex”。
4. 用 STM32CubeProgrammer 和本板 external loader 烧录生成的 HEX，地址已在文件中。
   下载模式 BOOT1=1、BOOT0=0；烧录后断电，切 BOOT1=0、BOOT0=0 再上电。

完整安装会替换当前应用，并初始化升级暂存、回滚及启动记录。
本工具只生成文件，不连接开发板，不执行擦除、烧录或签名。
当前网口升级配置：电脑有线网卡 192.168.2.10/24，开发板 192.168.2.11，接收端口 8080。

换用其他固件
- FSBL 和 Agent 请使用本工程 Binary/compat 的独立组件。
  不要选整个 initial_install.hex、management_update.hex 或旧布局 Agent。
- “启动应用”选择从 0x70100400 开始的独立 appli.hex。
- 软件包 1～61 的应用选择“SDK 1～61 · 600 MHz”。
- 99 AI 应用选择“SDK 99 AI · 800 MHz”；如果有单独 network-data.hex，在 AI 模型栏选择。
  没有单独模型文件时可留空。合成工具不会自动补找模型，也不能判断模型与应用是否匹配。
- 更新本工程后，可用新编译的三个独立组件替换 firmware 中的同名文件。
- LVGL 的 SD 卡资源仍需另行复制 ota_ui 文件夹；HEX 合成不包含 SD 卡文件。

检查内容
HEX 记录校验和、结束记录、地址重叠与分区范围；FSBL 2.3 签名头、0x400 对齐、
ROM 校验和与实际复位入口；兼容 Agent 地址；应用向量表；自动生成长度、CRC32
及启动配置。检查能发现常见选错文件，不能替代上板验证或证明任意第三方固件兼容。

输出范围（32 MB 兼容布局）
0x70000000  已签名 FSBL
0x700FC000  初始化启动请求记录
0x70100000  当前应用启动信息
0x70100400  当前应用，必要时含模型及填充
0x70B00000  初始化最新固件暂存记录
0x71500000  初始化上一版回滚记录
0x71F00400  独立 OTA Agent

命令行（可选）
InitialInstallMerger.exe --fsbl firmware\fsbl_universal.hex --agent firmware\agent.hex --app firmware\appli.hex --profile default --out output\initial_install.hex
参数：--model 模型.hex；--profile default / sdk600 / ai800；--overwrite 允许替换已有输出。
输出文件不能与输入文件同名同路径。默认拒绝覆盖已有命令行输出。

本版本附带固件：2026-10-19 LVGL 管理应用和兼容 Agent。
运行需要 Windows 自带的 .NET Framework 4.x，无需联网下载组件。
