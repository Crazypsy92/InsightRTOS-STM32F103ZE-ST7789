# Stage 9.2-H3: 按键事件处理链路与状态转移深度解析 (Event Flow)

## 一、信号流转架构与时序全链路

在 InsightRTOS STM32F103ZET6 + ST7789 系统中，按键事件从物理引脚动作到屏幕呈现的真实完整路径如下：

```text
[物理引脚 PA15] (内部上拉, 按下为低电平)
       │
       ▼ (1ms 硬件定时器轮询)
[TIM2 1ms 定时器中断 ISR]
       │ (每 5ms 分频调用一次 button_ticks())
       ▼
[MultiButton 状态机]
  - 3 节拍消抖滤波 (DEBOUNCE_TICKS = 3)
  - 短按/双击/长按识别 (SHORT_TICKS=300ms, LONG_TICKS=1000ms)
       │
       ▼ (识别到有效事件: SHORT / DOUBLE / LONG)
[Display_KeyFromISR()]
  - 记录事件追踪: sequence++, stage = EVENT_STAGE_IDENTIFIED, timestamp = now
  - 发送至 IPC 队列: rtos_queue_send_from_isr(&raw_keys, &key)
       │
       ▼ (唤醒最高优先级等待任务)
[KEY 任务 (优先级 5)]
  - 从 raw_keys 出队: rtos_queue_receive(&raw_keys, &key, WAIT_FOREVER)
  - 记录阶段: stage = EVENT_STAGE_KEY
  - 转发至控制队列: rtos_queue_send(&controller_keys, &key, WAIT_FOREVER)
  - 立即重新回到 BLOCKED 等待下一按键
       │
       ▼ (唤醒次高优先级等待任务)
[Controller 任务 (优先级 4)]
  - 从 controller_keys 出队: rtos_queue_receive(&controller_keys, &key, 0)
  - 逻辑决策:
      * 若为 LONG_PRESS: 执行 KEY_ToggleLED() (翻转 PC13 物理引脚)
      * 若为 SHORT / DOUBLE: 累加计数
  - 记录阶段: stage = EVENT_STAGE_CTRL
  - 组装显示命令: send_command(&sequence, key.event, key.at_ms) -> lcd_commands
  - 维持 10ms 调度周期
       │
       ▼ (唤醒 LCD 显示任务)
[LCD 任务 (优先级 2)]
  - 从 lcd_commands 出队: rtos_queue_receive(&lcd_commands, &cmd, WAIT_FOREVER)
  - 记录阶段: stage = EVENT_STAGE_LCD, completed_ms = now, total_processed++
  - 调用 Display_PagesExecute():
      * SHORT: 切换选中任务索引 (s_selected_task = (s_selected_task + 1) % 4)
      * DOUBLE: 切换详情显示 (s_detail_mode = !s_detail_mode)
      * LONG: 刷新界面按键状态
  - 差分局部行重绘 ST7789 显存 (通过 SPI3 + DMA)
  - 回传 ACK: rtos_queue_send(&lcd_acks, &ack, WAIT_FOREVER)
```

---

## 二、关键设计澄清与技术边界

### 1. 为什么禁止标注为“EXTI 中断”？
- **硬件事实**：PA15 虽可映射为 EXTI15，但在本工程硬件基线中，PA15 复用为 JTAG JTDI 引脚，工程在 `main.c` 中开启了 `SWJ_NOJTAG` 禁用 JTAG 保留 SWD。
- **轮询驱动**：按键扫描由 TIM2 1ms 硬件定时器驱动，每 5ms 调用 `button_ticks()` 采样 PA15 电平并进行软件多节拍消抖。
- **架构契约**：不得混淆定时器驱动 GPIO 轮询与外部边缘中断触发机制。

### 2. 为什么 KEY 任务显示“阻塞”是完全正确的稳态行为？
- **执行时间与刷新周期的数量级差异**：
  - KEY 任务只做一步出队并立即入队转发给 Controller，耗时小于 **10 微秒**（`0.01 ms`）。
  - 处理完毕后，KEY 任务立即调用 `rtos_queue_receive(..., RTOS_WAIT_FOREVER)` 进入 `BLOCKED` 状态，等待下一个物理按键。
  - LCD 任务每 250 ms 刷新一次屏幕状态快照。在 250 ms 的采样窗口中，KEY 任务有 99.996% 的时间都处于队列阻塞等待态。
- **严禁伪造状态**：若为了让用户看到“反应”而强行将快照中的 `BLOCKED` 改为 `RUNNING`，将彻底破坏 RTOS 诊断的真实性与可信度。

### 3. 如何通过“短时留痕”兼顾真实性与用户可视化？
- 在 `display_app.c` 中引入轻量追踪器 `g_event_tracker`，各任务在处理流水线中原子更新其实际到达的阶段：
  - `EVENT_STAGE_IDENTIFIED`（中断识别）
  - `EVENT_STAGE_KEY`（KEY 任务接收并转发）
  - `EVENT_STAGE_CTRL`（Controller 决策并控制 LED）
  - `EVENT_STAGE_LCD`（LCD 任务完成显存重绘）
- 在完成渲染后记录完成时间戳 `completed_ms`，并由 UI 层执行 **2000 ms 短时留痕**：
  - 当 `now - completed_ms < 2000ms` 时，屏幕持续高亮显示 `链路: 识别>KEY>CTRL>LCD`。
  - 当 2 秒超时后，自动平滑退回 `链路: 待机`。
  - 该机制完全在非阻塞的 UI 采样逻辑中计算，没有任何任务被 `sleep` 或阻塞，任务调度与 IPC 保持原生活跃。

### 4. 为什么没有“LED Task”？
- LED 是由 Controller 任务在响应长按事件时通过 `KEY_ToggleLED()` 直接操作的板载 GPIO 外设（PC13）。
- 将一个外设驱动虚构为一个 RTOS 任务既浪费 TCB 和栈空间（每个任务占用 4KB 独立栈），又违反了内核设计契约。系统维持原有的 4 个业务任务：`KEY`、`Controller`、`LCD`、`Monitor`。
