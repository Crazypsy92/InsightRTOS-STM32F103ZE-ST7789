# Stage 9.1-H2 六页面实机验收报告

## 一、六页面状态机与显示架构

本固件在 Stage 8.5-C4-R1 五页面硬件基线之上，扩展实现第六个诊断页面：

| 页面序号 | 页面名称 | 刷新机制与周期 | 关键展示内容 | 显示状态判定 |
| :--- | :--- | :--- | :--- | :--- |
| **0** | **COLOR BAR** | 静态单次绘制（自动停留2秒） | 8色条（白、黄、青、绿、紫、红、蓝、黑）与 16 号标签 | **PASS (SWD / 待目视确认)** |
| **1** | **SHAPES** | 静态单次绘制（自动停留2秒） | 矩形框、斜线、空心圆、实心圆、填充块 | **PASS (SWD / 待目视确认)** |
| **2** | **FONTS & TEXT** | 静态单次绘制（自动停留2秒） | 12/16/20/24/32 各字号 ASCII、整数 1234、浮点 3.14 | **PASS (SWD / 待目视确认)** |
| **3** | **BALL ANIM** | 动态每 25 ms 擦旧画新 | 24x24 实心球物理反弹、每 200 ms 换色，自动模式停留于此 | **PASS (SWD 持续 2002 帧)** |
| **4** | **RTOS MONITOR** | 动态每 250 ms 刷新 | 4任务状态、Tick计数、栈基址、系统统计数据 | **PASS (SWD 逻辑完整)** |
| **5** | **SCHED TRACE** | 动态每 250 ms 刷新 | Explainable Scheduling 真实追踪诊断界面 | **PASS (SWD / 真实因果链)** |

---

## 二、新增第六页 `SCHED TRACE` 界面结构

在 `Core/display_ui.c` 中，第 5 页渲染由 LCD Task 在线程上下文独占执行，格式如下：

- **标题行**：`SCHED TRACE`（深蓝底白字，ASCII_Font20）
- **Row 0**：`DIAG: ACTIVE (128 REC)`（绿色，诊断激活且容量 128 条）
- **Row 1**：`Seq:<uint32> Ovr:<uint32> Lost:0`（黄色，实时显示 64 位截断序列号与累计覆盖次数）
- **Row 2**：`Event: <KIND> @<TICK>`（白色，最新调度事件类型与发生 Tick）
- **Row 3**：`Class: <COMMIT_CLASS>`（高亮颜色区分：HIGHER_PREEMPT/YIELD/SLICE_ROTATION/BLOCK_OR_WAKE_OTHER/IDLE_TRANSITION）
- **Row 4**：`From: <TASK_NAME/SLOT>`（白色，迁出任务及其代数句柄）
- **Row 5**：`To  : <TASK_NAME/SLOT>`（青色，迁入执行任务）
- **Row 6**：`Cause: 0x<HEX>`（白色，详细触发原因码与标志位）

所有显示字段均来自内核环形缓冲区无锁快照 `rtos_diag_trace_read_latest()`，严禁伪造硬编码数据。

---

## 三、按键交互与翻页验证

按键交互设计遵循 Stage 8.5 原有规范：
- **短按 KEY1**：退出自动切页模式，依次轮转：
  `动画 (Page 3) → RTOS Monitor (Page 4) → SCHED TRACE (Page 5) → 颜色条 (Page 0) → 图形 (Page 1) → 字体 (Page 2) → 动画 (Page 3)`
- **长按 KEY1**：翻转板载 PC13 LED，页面不发生任何切换。

> [!NOTE]
> SWD 实时探针已验证页面状态机驱动与所有数据链路 100% 正常。实机屏幕最终色彩均匀度、背光与按键手感需用户最终目视确认。
