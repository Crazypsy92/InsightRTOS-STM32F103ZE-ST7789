# Stage 9.2-H4: 遗留项与待确认事项 (Remaining Issues)

## 一、完成项核验记录

- **软件代码实现**：**100% 完成**。
  - 单屏界面精简：删除第 11 行 `Trace:<数字>` 及相关 API 调用，第 11 行改为 `事件: 成功:<n> 失败:<m>` / `重试:<k>`，第 12 行改为 `详情: 运行次数: <runs>`。
  - 内核 Trace 模块：未做任何修改，完整保留 `trace.c`、`rtos_trace.h`、`INSIGHT_TRACE_ENABLED`、`INSIGHT_DIAG_EXPLAIN`。
  - 事件 ID 全链路透传与闭环 ACK：通过 `display_key_t.id`、`display_command_t.id`、`display_ack_t.id` 实现 8 槽位有界历史闭环匹配，杜绝跨事件互相覆盖与阶段回退。
  - 任务优先级文档校正：`README.md` 中任务优先级已修正为 KEY=5, CTRL=4, LCD=2, MON=1。
  - 测试套件入库：`tests/stage9_2_h4/test_stage9_2_h4.c` 与 `run_tests.sh` 已直接纳入仓库版本控制。
- **Host 单元测试**：**9/9 PASS (100%)**。
- **内核回归测试**：**360/360 PASS (100%)**。
- **受控 Flash 烧录与回读**：**PASS (100%)**。
  - Flash 页 0～30 精确擦除，固件（49,424 字节）写入，读回 SHA256 100% 匹配。
  - 擦除剩余尾部（14,064 字节）全为 0xFF，Option Bytes 保持 0x5AA5 Level 0。
- **实机 60 秒稳定性测试**：**PASS**。
  - TIM2 频率 1000.02 Hz，RTOS 心跳 100.00 Hz，LCD 刷新 3.85 Hz。
  - 传输错误 0、初始化错误 0、按键溢出 0、命令溢出 0。
  - 冷重启复位验证正常。

---

## 二、实机待确认事项 (User Confirmation Pending)

**物理交互与视觉核验 (USER CONFIRMATION PENDING)**：
由于物理按键触感与屏幕实际肉眼观感无法通过调试探针自动替代，特保留如下实机核验项待用户物理确认：
1. **单击 KEY1 (PA15)**：观察选中标记 `>` 是否在 KEY → CTRL → LCD → MON 之间顺畅循环，Row 08 是否切换职责说明。
2. **双击 KEY1**：观察 Row 12 是否正确展开显示 `详情: 运行次数: <runs>`，再次双击关闭。
3. **长按 KEY1**：观察板载 PC13 LED 是否翻转亮灭，Row 01 是否同步更新 `LED: 开` 或 `LED: 关`。
4. **观察 Row 11**：确认显示为 `事件: 成功:<n> 失败:<m>`，无任何 `Trace:` 残留。
5. **观察 Row 02**：按键触发后在 2 秒内显示 `链路: 识别>KEY>CTRL>LCD`，2 秒后平滑恢复为 `链路: 待机`。
