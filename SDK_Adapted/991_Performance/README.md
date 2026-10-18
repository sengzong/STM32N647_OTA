# 991 第二轮性能优化

上一版三缓冲用户反馈为 Inference 约 42ms、FPS 约 25；此前约 15 FPS。
上一版文件保留在 Binary/991_triple_buffer，本版单独输出 Binary/991_performance。

## 本次改动

- 三个 320×320 RGB 输入缓冲搬到内部 AXI SRAM1/2，保持 32 字节对齐，
  总计 921600 字节。摄像头预览和绘图缓冲继续在 PSRAM。
- uint8 → int8 保持逐字节异或 0x80，使用 Cortex-M55 MVE 一次处理 16 字节。
  不改变颜色顺序、缩放、量化规则或模型参数。
- 明确缓存交接：首次采集前清理 BSS 缓冲；DMA 完成后、CPU 读取前失效；
  转换后 clean 到内存让 NPU 读到新数据；推理完成后失效，再交还采集队列。
- 对应用、模型的 CPU 调度/软件处理代码、后处理和 ll_aton_lib 源码使用 -O3。
  不启用 -ffast-math，不更换预编译算法库，不修改频率或模型。
- 屏幕增加 Prep、NN、PP、Draw 和 CPU MHz。Inference 仍保留输入准备加
  模型调用的计时范围，方便与 42ms 对比。Draw 为前一次绘图耗时，各项为毫秒。

本次目标是降低外存访问和 CPU 处理开销；是否达到 30 FPS 需要实测。
NN 是整次模型调用耗时，包含 CPU 软件算子和 NPU 工作，不是纯 NPU 耗时。

## 构建与使用

在 LED_2_noai 工程运行：

```powershell
python tools/build_991.py --performance
python tools/test_991_performance.py
```

全部构建和修改位于当前工程；外部原软件包只读。
输出在 Binary/991_performance：

- **`991_performance.hex`**：名称明确的第二轮应用文件，内容与本目录的 appli.hex 完全相同；
  现有 991 配套模型无需重复烧录。请勿误选 Binary/991_triple_buffer/appli.hex，那是第一轮。
- `appli.hex`：已在运行配套 991 模型的板子可只更新此应用。
- `application_and_network.hex`：应用及原始配套模型合并的 Programmer 文件。
- `991_performance.ota`：应用和模型的网口包，只适用于已迁移兼容布局的 FSBL/Agent。
- `manifest.json`：输入缓冲地址、内部 RAM 余量、模型和所有输出的哈希。

应用入口保持 0x70100400，模型保持 0x70200000。无需为此修改 FSBL。
烧录后在同样场景对比 Inference、FPS 和新增 Prep/NN 数值，检查检测结果、
预览、触摸及云台行为，并观察持续运行稳定性。此构建不操作板子。

## 验证范围

构建检查内部 RAM 边界、三缓冲大小、MVE 转换指令、应用/模型地址与整包 CRC。
主机测试检查转换数值、CPU/DMA/NPU 缓存交接顺序、缓冲区归属和循环复用。
已完成上述检查。输入实际链接在 0x34008620，BSS 结束于 0x340FED30，
系统栈顶仍为 0x34200000，堆和系统栈有约 1MiB 空间。
与第一轮构建比较，306 个编译单元中只有计划优化的 5 个单元改变，
其余 301 个单元的代码和可分配数据一致；模型 HEX 完全一致。
主机测试无法代替真实摄像头、缓存、总线访问以及识别精度验证。

依据 ST 文档，DCMIPP 可访问 AXI SRAM1/2，模型输入也可位于应用 RAM：
- https://www.st.com/content/st_com/en/technical-documents/an6211-introduction-to-digital-camera-interface-pixel-pipeline-for-stm32-mcus-stmicroelectronics.html
- https://github.com/STMicroelectronics/STM32N6-GettingStarted-ImageClassification/blob/main/Doc/Neural-ART-Description-and-Operation.md
