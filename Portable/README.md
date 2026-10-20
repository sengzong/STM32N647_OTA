# 完整烧录包便携合成工具

下载或复制 [InitialInstallMerger.zip](InitialInstallMerger.zip)，解压后双击
InitialInstallMerger.exe。工具自带当前 FSBL、Agent 和管理应用，可直接合成
initial_install.hex；无需安装 Python、CubeIDE 或编译器。

在工程内运行时优先选用当前 Binary/compat，移出工程后使用随附 firmware。
支持更换组件、600/800 MHz 启动配置和可选 AI 模型。详细用法见包内 README.txt。
生成的完整安装包会替换当前应用，并初始化暂存、回滚及启动记录。
