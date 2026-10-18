# 独立 OTA Agent

Agent 位于 NOR 偏移 `0x1400000`，入口 `0x71400400`，不随应用更新被覆盖。

当前方案：**最新固件暂存区 + 上一版回滚区**。详细操作见 [OTA_GUIDE.md](../OTA_GUIDE.md)。

## 启动与接收

每次正常启动，FSBL 先进入 Agent 的 10 秒窗口。电脑先点发送或回滚，再复位板子。
没有任务时，Agent 写一次性 SRAM 标志并软复位，让 FSBL 直接启动应用一次。
应用损坏、安装失败或显式请求救援时，Agent 持续等待。

接收开始后允许继续传输；连续 30 秒没有数据则中止。本程序不初始化屏幕，
等待期间屏幕可能保持原样或无画面，进入应用后由应用处理显示。

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
python tools/build_ota.py
```

构建产物为 `Binary/agent.hex`。初次启用新方案需同时更新 FSBL、Agent 和应用。
串口横幅为 `resident OTA AGENT [AGENT-TARGET v2]`。
