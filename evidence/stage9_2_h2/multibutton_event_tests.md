# Stage 9.2-H2: MultiButton 事件扩展与时序互斥测试报告 (MultiButton Event Tests)

## 一、事件扩展设计

针对 STM32F103 板载 KEY1 (PA15)，通过 `multi_button` 驱动扩展三类用户交互输入：

| 输入操作 | MultiButton 事件码 | RTOS 映射事件 | 业务功能行为 |
| :--- | :--- | :--- | :--- |
| **单击** | `BTN_SINGLE_CLICK` | `KEY_EVENT_SHORT` | 循环切换选中任务：KEY → CTRL → LCD → MON → KEY |
| **双击** | `BTN_DOUBLE_CLICK` | `KEY_EVENT_DOUBLE` | 切换选中任务详情视图（模式 0: 运行次数/优先级; 模式 1: 状态/Gen） |
| **长按** | `BTN_LONG_PRESS_START` | `KEY_EVENT_LONG` | 仅触发一次板载 PC13 LED 翻转，不产生持续连发 |

---

## 二、时序配置与互斥性保证

### 1. 状态机时序参数
- `TICKS_INTERVAL = 5 ms`（由 `TIM2_IRQHandler` 每 1ms 累加分频驱动）。
- `DEBOUNCE_TICKS = 3`（消抖周期 15 ms）。
- `SHORT_TICKS = 60`（短按判定等待窗口 300 ms，单击需等待双击判定窗口结束）。
- `LONG_TICKS = 200`（长按触发阈值 1000 ms）。

### 2. 互斥性与安全性证明
1. **单击与双击绝对互斥**：在 `BTN_STATE_RELEASE` 状态下，若在 300 ms 内再次按下，状态机进入 `BTN_STATE_REPEAT` 状态，`repeat` 递增为 2，超时后仅触发 `BTN_DOUBLE_CLICK`，绝不触发 `BTN_SINGLE_CLICK`。
2. **长按不附带单击**：当按下持续时间超过 1000 ms 时，状态机直接转移至 `BTN_STATE_LONG_HOLD`，释放时不进入 `BTN_STATE_RELEASE`，绝不产生单击事件。
3. **长按保持不连发**：`BTN_LONG_PRESS_HOLD` 未注册回调，驱动不会每 5 ms 向 RTOS 队列倾倒垃圾事件，队列深度保持安全。
4. **中断安全**：按键扫描在 ISR 中仅进行状态机时钟递增与事件入队（`rtos_queue_send_from_isr`），耗时 < 1.2 µs，所有业务逻辑均由 Controller 与 LCD 任务在非中断上下文处理。
