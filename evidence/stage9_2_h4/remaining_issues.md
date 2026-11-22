# Stage 9.2-H4: 遗留项与待确认事项 (Remaining Issues)

## 一、软件与测试状态

- **软件代码实现**：**100% 完成**。
  - 单屏界面精简：删除第 11 行 `Trace:<数字>` 及相关 API 调用，第 11 行改为 `事件: 成功:<n> 失败:<m>` / `重试:<k>`，第 12 行改为 `详情: 运行次数: <runs>`。
  - 内核 Trace 模块：未做任何修改，完整保留 `trace.c`、`rtos_trace.h`、`INSIGHT_TRACE_ENABLED`、`INSIGHT_DIAG_EXPLAIN`。
  - 事件 ID 全链路透传与闭环 ACK：通过 `display_key_t.id`、`display_command_t.id`、`display_ack_t.id` 实现 8 槽位有界历史闭环匹配，杜绝跨事件互相覆盖与阶段回退。
  - 任务优先级文档校正：`README.md` 中任务优先级已修正为 KEY=5, CTRL=4, LCD=2, MON=1。
  - 测试套件入库：`tests/stage9_2_h4/test_stage9_2_h4.c` 与 `run_tests.sh` 已直接纳入仓库版本控制。
- **Host 单元测试**：**9/9 PASS (100%)**。
- **内核回归测试**：**360/360 PASS (100%)**。
- **固件构建产物**：
  - `artifacts/stage9_2_h4/insightrtos_display_o2_trace.bin` (49,424 字节)
  - SHA256: `0d5a98319a2a8f3c7e959596f1c4813f90e5d7aadb923ff65b2987765cd9648a`

---

## 二、实机待确认事项 (User Confirmation Pending)

1. **硬件探针连接状态**：
   - 当前 ST-Link V2 探针无法读取目标 MCU 的 DP-IDCODE（报错 `STLINK_JTAG_GET_IDCODE_ERROR`）。
   - 请确认 STM32F103ZET6 开发板独立电源是否开启，SWD 杜邦线（PA13/PA14/GND）是否接触良好，或按下板载 RESET 按键后重试。
2. **物理交互与视觉核验 (USER CONFIRMATION PENDING)**：
   - 固件烧录上电后，需进行实机物理触感与屏幕观测：
     - 单击 KEY1 (PA15)：观察选中标记 `>` 是否在 KEY → CTRL → LCD → MON 之间顺畅循环。
     - 双击 KEY1：观察 Row 12 是否正确展开显示 `详情: 运行次数: <runs>`，再次双击关闭。
     - 长按 KEY1：观察板载 PC13 LED 是否翻转亮灭，Row 01 是否同步更新 `LED: 开` 或 `LED: 关`。
     - 观察 Row 11：确认显示为 `事件: 成功:<n> 失败:<m>`，无任何 `Trace:` 残留。
     - 观察 Row 02：按键触发后在 2 秒内显示 `链路: 识别>KEY>CTRL>LCD`，2 秒后平滑恢复为 `链路: 待机`。
