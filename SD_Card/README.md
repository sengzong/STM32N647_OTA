# OTA 界面 SD 卡资源

将本目录中的 **ota_ui 文件夹整体复制到 SD 卡根目录**，保留卡上其他文件。
最终路径应为：

```text
SD 卡根目录/
└── ota_ui/
    ├── cjk16.bin
    └── theme.cfg
```

支持 FAT16、FAT32 和 exFAT。该功能只读文件，不会格式化、擦除或写入 SD 卡。
首次启用需用 Programmer 单独更新 Binary/compat/agent.hex。当前已安装兼容版
FSBL 的板子无需重新刷 initial_install.hex；单刷 Agent 保留当前应用、暂存及回滚数据。
HEX 自带地址，兼容 Agent 的向量表位于 0x71F00400。

LVGL 核心、基本英文、进度条和“未提供”的三个汉字保留在 NOR，完整中文字库及
配色放在 SD 卡。启动时读取一次并缓存在独立的 PSRAM 区域，不与固件接收缓冲重叠。
没插卡、缺文件或字库损坏时仍显示内置升级画面；收到包的名称、目标 600/800MHz、
模型状态和版本仍会更新，但扩展中文字形取决于卡上的字库。缓存成功后拔卡不影响显示。

theme.cfg 可使用六位十六进制颜色调整界面：

```ini
background=0B1420
panel=182536
accent=39D598
text=F4F8FC
muted=8DA5BD
```

修改资源后重新上电或复位加载。版本号仍从网页手动填写，留空显示“未提供”，
不从 SD 卡推断。资源格式、CRC 与来源哈希见 manifest.json；字库源自官方
LVGL v8.4.0 的 lv_font_simsun_16_cjk.c，许可证见 ../Middlewares/LVGL/LICENCE.txt。
当前字库包含 1436 个字符，不代表完整中文字符集。

新版管理应用的固件列表、Yes 确认页和接收进度页共用这套配色及字库。
用 Programmer 烧 Binary/compat/management_update.hex 更新管理应用后才会替换旧白色界面；
SD 卡只存字库、主题资源，复制文件本身不会替换页面程序。

主机文件系统、实际 LVGL 渲染及 ARM 编译已经检查；SDMMC 引脚电气和实际卡兼容性
仍需使用本板、实际 SD 卡上板确认。
