# 991 latest-frame and persistent-runtime builds

2026-10-18：用户上板反馈 RT60“全是噪点”，该模式验证失败，暂停使用。
原因仍待区分传感器输出、CSI/ISP 匹配和曝光增益，不能将主机测试通过当作成像正常。

当前使用 [RT30 R2 固件](../../Binary/991_realtime_30/991_realtime_30.hex)。
R2 完全使用原 SDK 的 app_camera.c、cmw_imx335.c 和 imx335.c，
不编译试验版的摄像头模式适配；保留最新帧处理、推理环境复用和预览延迟优化。
这三个摄像头对象与此前正常工作的性能版逐字节核对。
屏幕显示 RT30 R2，便于区别此前 RT30。新应用的实际成像仍须上板确认。

若需要先恢复到用户已确认正常的版本，使用
`tools/build_991.py --performance` 可重新生成此前的性能版。
旧性能版、三缓冲版和 RT60 试验版的发布产物已按用户要求清理；源码保留。
当前 NN 33ms 对应识别约 30 FPS；本轮修复不承诺提高这个识别上限。
查看 FPS、NN、Cam、Age；Age 是检测结果使用的图像年龄，
未包含曝光、屏幕扫描和机械转动。

Build with `python tools/build_991.py --realtime`. Output:
`Binary/991_realtime_30/991_realtime_30.hex`.
R2 compiles the original SDK camera application/middleware/sensor sources with
no local camera-mode overrides, retaining its full-resolution 30 FPS camera mode.
Three SRAM input
buffers now retain the latest completed frame instead of a FIFO backlog. The
camera cannot overwrite the buffer owned by inference. Runtime and network
initialization happen once; each inference uses the vendor Reset_Network API.
Model weights, 320x320 input, quantization and system clocks are unchanged.
The preview displays the just-completed image instead of delaying one frame;
three display buffers remain for vertical-blanking reload.

Withdrawn experiment, retained only for investigating the failed image:
`python tools/build_991.py --realtime --capture60` prints a hardware-failure warning.
Output: `Binary/991_realtime_60_trial/991_realtime_60_trial.hex`.
Previously published RT60 files were deleted during firmware cleanup; rebuilding
would recreate a failed experimental mode and is not recommended for use.
This changes IMX335 capture to 1296x972 2x2 binning, targeting 60 FPS. Timing and
mode register facts follow the ST-contributed
[Zephyr IMX335 driver](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/video/imx335.c).
The binning-specific inverted-readout phase facts also follow the
[Linux IMX335 driver](https://github.com/torvalds/linux/blob/master/drivers/media/i2c/imx335.c),
with the AREA3 start adapted to this mode's normal start plus its vertical area.
Exposure conversion, maximum exposure and ISP statistics area are adapted.
The board's 37 MHz input-clock setup and two-lane RAW10 CSI link are retained.
Camera color, orientation, exposure behavior, recognition accuracy and preview
tearing in this mode require board validation. Do not treat the 60 FPS capture
target as achieved hardware performance or as 60 inferences per second.

Screen: `RT30` / `RT60` identifies the build. `Cam` counts completed camera
frames per second; `FPS` counts completed inferences over a one-second interval.
`Age` is milliseconds from completion of the input frame to the start of drawing
its detection result. It excludes exposure/readout before capture completion,
LCD scanout and mechanical movement. Prep/NN/PP/Draw keep their previous meaning.
Timing metadata follows its matching output buffer, preventing mixed-frame stats.

User baseline: FPS about 30, Prep 1 ms, NN 33 ms, CPU 800 MHz. Even with faster
capture, a 33 ms inference limits throughput to roughly 30 FPS before other costs.
This change reduces queuing/initialization costs; no numerical speedup is claimed
without a board measurement. Higher capture FPS can reduce the age of the frame
selected by inference and smooth the independent camera preview.

Use RT30 R2 as the retained realtime build. Prior performance-build sources
remain available, but its redundant published artifacts were removed. Do not
use RT60 for normal operation after the reported noise failure.
Application-only HEX retains absolute addresses and can replace the current 991
application when the matching model is already installed. If not, use the paired
application_and_network.hex. OTA packages require the large-image compatible
FSBL/Agent layout described in SDK_COMPAT_GUIDE.md. These tools never flash a board.

The persistent runtime follows the
[ST Neural ART execution sequence](https://stedgeai-dc.st.com/assets/embedded-docs/stneuralart_api_and_stack.html).
