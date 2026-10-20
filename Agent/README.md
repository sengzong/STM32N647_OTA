# 独立 OTA Agent

兼容布局 Agent 位于 NOR 偏移 `0x1F00000`，入口 `0x71F00400`，不随应用更新被覆盖。
旧 512KB 布局的入口为 `0x71400400`，两套地址不能混烧。

当前方案：**最新固件暂存区 + 上一版回滚区**。详细操作见 [OTA_GUIDE.md](../OTA_GUIDE.md)。

## 启动与接收

每次正常启动，FSBL 先进入 Agent 的 10 秒窗口。电脑先点发送或回滚，再复位板子。
没有任务时，Agent 写一次性 SRAM 标志并软复位，让 FSBL 直接启动应用一次。
应用损坏、安装失败或显式请求救援时，Agent 持续等待。

接收开始后允许继续传输；连续 30 秒没有数据则中止。LVGL 进度画面显示
包含包名称、目标 CPU 频率、模型信息、手填版本号和接收/写入进度。
空版本显示“未提供”。完整中文字库和主题优先从 SD 卡加载，缺卡时使用内置界面，
资源复制方式见 [SD_Card/README.md](../SD_Card/README.md)。本版已完成 ARM 编译和
主机渲染/协议/存储检查，尚未烧入板子验证 LCD 和真实 SD 卡。

## 安装与回滚

固件经长度、向量表和 CRC32 检查后写入唯一的最新固件暂存区，校验成功后最后提交有效标志。
Agent 再写 `REQ_PERSIST` 并复位，由 FSBL 备份当前应用、安装新应用。
普通应用不需要实现 OTA 或启动确认协议。

“回滚上一版”指令先校验备份，再写 `REQ_ROLLBACK` 并复位，由 FSBL 恢复。
没有合法备份时返回失败。Agent 自身始终不直接擦写当前运行区。

## 复用和构建

Agent 通过 Eclipse 链接资源复用 `Appli/Core/Src/app_netxduo.c` 及 ThreadX/NetXDuo。
宏 `LED2_OTA_AGENT` 启用启动窗口与自动安装，禁止应用确认路径进入 Agent。
App 与 Agent 的 OTA 代码修改后应一起重建。

```powershell
python tools/test_ota.py
python tools/build_ota.py --compat-package
```

兼容构建产物为 `Binary/compat/agent.hex`。当前已装兼容 FSBL 的板子可只更新此文件，
保留当前应用和回滚数据；全新安装仍使用 Binary/compat/initial_install.hex。
串口横幅为 `resident OTA AGENT [AGENT-LVGL-SD v4]`。
