# 991 三缓冲修改版

2026-10-18 用户上板反馈：Inference 约 42ms、FPS 约 25，原先约 15 FPS。
这是用户反馈的当前运行数据，尚未记录长期稳定性或自动性能采样。
第二轮 SRAM/MVE 优化另见 `SDK_Adapted/991_Performance/README.md`，本版保留用于对比。

本目录保存修改后的 app.c、app_config.h；其余文件仍从原软件包读取。
原工程 `F:\stm32CubeIde\Project\Software_Package\Software_Package` 未被写入。

## 改动

- AI 输入缓冲从 2 个增加到 3 个，新增 `NN_INPUT_BUFFER_COUNT=3`。
- 队列指针数组容量同步增加，输入缓冲分配和队列初始化使用同一配置。
- 输入缓冲额外占用 307,200 字节（300KiB）PSRAM，仍从 0x91000000 分配。
- 保持模型、320×320 输入尺寸、时钟和原来的其他业务逻辑。

双缓冲下，采集回调必须先找到下一块空闲缓冲，才会提交当前帧。当摄像头
每 33ms 完成一帧、推理占用约 42ms 时，另一块缓冲仍被占用，容易出现
每两帧才提交一次、约 15 FPS 的情况。第三块缓冲用于衔接采集与推理。
30 FPS 采集、42ms 处理的时序模拟从约 15 FPS 提高到约 23.8 FPS；
这不是板上实测结果，不代表能达到 30 FPS。

屏幕 FPS 是识别更新频率，不是 LCD 的刷新率。Inference 的原计时范围包含
输入像素转换及模型调用；CPU load 也不表示 NPU 的占用率。

## 构建与文件

在 `F:\stm32CubeIde\Project\LED_2_noai` 运行：

```powershell
python tools/build_991.py
python tools/test_991_buffers.py
```

构建工具完整重编译应用，所有头文件使用同一套队列配置，保留原 Release
编译选项。构建日志、链接映射、修改对比保存在 `Build/991-triple-buffer`。
直接构建外部原版 991 工程仍会使用双缓冲；修改版请使用这里的构建脚本。
升级文件保存在 `Binary/991_triple_buffer`：

- `991_triple_buffer.ota`：应用与配套模型合并的网口升级包，要求已安装
  `SDK_COMPAT_GUIDE.md` 中的兼容版 FSBL 与 Agent。
- `application_and_network.hex`：给 Programmer 使用的应用与模型合并 HEX，
  原地址分别为 0x70100400、0x70200000；不包含 FSBL 或 Agent。
- `appli.hex` / `network-data.hex`：同一版本的独立文件。
- `manifest.json`：缓冲区、模型哈希和输出文件校验信息。

若板上已经运行配套的 991，模型没有改变，可在下载模式通过 Programmer
仅烧录 `appli.hex` 后校验，再切回运行模式。首次安装或模型来源不确定时，
使用 `application_and_network.hex` 并搭配原版 991 或兼容版 FSBL。
网口 `.ota` 只适用于已经完成兼容布局迁移的板子，旧布局不能直接接收此包。
本构建过程不烧录板子。

## 已完成验证

- 完整重编译 306 个源文件，链接结果确认三个输入缓冲和 PSRAM 边界。
- 原版队列代码与修改版实际采集回调的主机测试通过：42ms 处理时约
  15 → 23.8 FPS；检查缓冲归属、队列循环和饱和，以及两个输出缓冲仍可用。
- 与原版 Release 对比，除应用初始化和队列结构相关的两个编译单元，
  其余 304 个单元的可分配代码/数据段完全一致。
- 新包 CRC、文件哈希、应用地址、模型逐段内容和发送端兼容性检查通过。
- 原版 77 个 SDK 包的回归检查通过。

上板后对比同一场景的 Inference 和 FPS，并确认预览、检测框、触摸等仍正常。
持续运行稳定性仍需实测。
