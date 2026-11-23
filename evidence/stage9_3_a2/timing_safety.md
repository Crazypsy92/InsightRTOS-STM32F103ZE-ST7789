# InsightRTOS Stage 9.3-A2 时序可信度与安全性契约

## 1. 时钟能力与可信度来源

InsightRTOS Stage 9.3 引入的实时诊断模块（Real-Time Diagnostics）采用双时间源协同机制：

1. **高精度周期时钟（DWT CYCCNT）**：
   - 物理硬件：Cortex-M3 Data Watchpoint and Trace (DWT) 计数器。
   - 标称频率：72 MHz（对应 CPU 主频 $f_{\text{CPU}}$）。
   - 寄存器位宽：32 位无符号整数（约 59.65 秒回绕一次：$2^{32} / 72{,}000{,}000 \approx 59.652\text{ s}$）。
   - 硬件使能：由 `CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk` 与 `DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk` 激活。

2. **内核基准节拍（SysTick）**：
   - 物理硬件：Cortex-M3 系统滴答定时器。
   - 标称频率：100 Hz（周期 10 ms）。
   - 角色：提供粗粒度时间基准、低频采样看门狗以及 32 位回绕周期看门检查。

---

## 2. 64 位单调纪元扩展与回绕可信度判定

### 2.1 扩展算法
在内核切换 Hook 及快照查询中，通过内部静态锁区函数 `get_cycles_64_locked(current_tick)` 维护 64 位纪元：
$$
\text{cycles}_{64} = \text{cycle\_epoch} \mid \text{current32}
$$
当连续两次采样发现 $\text{current32} < \text{last\_cycle32}$ 时，判定发生单次 32 位正向回绕：
$$
\text{cycle\_epoch} \leftarrow \text{cycle\_epoch} + 2^{32}
$$

### 2.2 多重回绕与不确定性检测（Uncertainty Window）
- **边界条件**：若系统在 59.0 秒（对应 100 Hz 下 5900 ticks）内未执行任何周期更新（例如长临界区、SysTick 中断挂起或调试器挂起恢复）：
- **行为**：置位 `RTOS_DIAG_RT_CLOCK_UNCERTAIN` 标志，向诊断调用者明确指示时钟可能经历了一次以上的无感知回绕，拒绝将伪造的时间差返回给消费者。

### 2.3 调试器同时冻结双时钟的不可检测性限制（Critical Boundary Caveat）
> [!WARNING]
> 当外部硬件调试器（如 ST-Link / J-Link 通过 Cortex-M3 DHCSR 挂起核心）冻结 MCU 时，若 MCU 调试配置（`DBGMCU_CR` 中的 `DBG_TIM2_STOP`、`DBG_TRACE_STOP`）使得 DWT CYCCNT 与 SysTick 同时停顿，则在 MCU 恢复运行时，两者之间的相对采样间隔不会表现出跳变。
> **物理限制**：在此类双时钟同时冻结的场景下，仅靠 MCU 内部时钟源无法自主推断出外部真实物理时间的流逝。
> **工程规范**：InsightRTOS 不试图通过不可靠的启发式猜测“弥补”停机时间；文档明确此项物理边界。若调试采样跨越调试断点，建议调用 `rtos_diag_rt_test_reset()` 显式复位诊断会话。

---

## 3. CPU 周期归属边界（Occupancy vs Pure Thread Execution）

1. **线程执行周期（Thread CPU Cycles）**：
   - 计量定义：任务从切入（Switch In）到切出（Switch Out）之间流逝的 CPU 周期数。
   - 动态在途补偿（In-Flight Compensation）：若任务当前处于 `RUNNING` 态，快照接口通过瞬时采样时钟差动态补足本次派发的在途执行时间，保证读数实时准确。
2. **ISR 执行时间的归属**：
   - 在 Cortex-M3 单核无硬件浮动嵌套计数器架构下，任务运行期间触发的中断服务程序（如 TIM2 1ms 中断、SPI3/DMA 中断）所消耗的 CPU 周期被计入该当前任务的 CPU 占用周期（CPU Occupancy Cycles）。
   - **不能**将此数值等同于该任务在完全不受中断打扰下的纯指令耗时（Pure Instruction Execution Time）。
3. **阻塞与空闲隔离**：
   - 任务处于 `BLOCKED`、`SUSPENDED` 或 `EXITED` 态期间，零计费。
   - 系统空闲由专有空闲记录 `s_idle_record` 独立累计，通过 `rtos_diag_rt_idle_snapshot()` 读取，严格不与业务任务混淆。

---

## 4. 事件流水线有界历史与 ACK 韧性机制

### 4.1 8 槽历史环与 32 队列容量的边界问题
在 Stage 9.2-H5 架构中，按键队列容量为 32，但事件详细追踪环 `history` 仅保留 8 槽。在高频按键突发（如 10+ 连续事件）时，在途事件尚未被 LCD Task 处理并反馈 ACK，其槽位即被新事件覆盖。

### 4.2 韧性设计与零丢失契约
在 Stage 9.3-A2 中扩展了 `display_event_tracker_t` 结构并强化了状态流转：

```c
typedef struct {
    display_tracked_event_t history[APP_EVENT_HISTORY_CAPACITY]; // 最近 8 槽详情
    uint32_t latest_id;
    uint32_t confirmed_id;
    key_event_t latest_event;
    uint32_t completed_ms;
    uint32_t success_count;         // 累计成功确认总数（永不丢失）
    uint32_t failure_count;         // 累计失败确认总数（永不丢失）
    uint32_t retry_count;           // 重试总数
    uint32_t drop_count;            // 丢弃总数
    uint32_t evicted_unacked_count; // 尚未 ACK 即被环形槽覆盖的事件数
    uint32_t orphaned_ack_count;    // 收到 ACK 时槽位已被覆盖的孤儿 ACK 数
} display_event_tracker_t;
```

- **分配时覆盖检测**：若覆盖尚未进入 `ACKED`/`FAILED` 的槽位，递增 `evicted_unacked_count`。
- **孤儿 ACK 准确结算**：当延迟到达的 ACK 其对应 ID 在 8 槽内已找不到时，判定为孤儿 ACK，递增 `orphaned_ack_count`，同时**准确累加 `success_count` 或 `failure_count`**，更新 `confirmed_id` 与 `completed_ms`。
- **结论**：高频压力下允许历史详情槽位发生有界淘汰，但全生命周期的成功/失败统计实现**绝对零静默丢失**。

---

## 5. 故障恢复与安全中止路径

| 故障场景 | 表现 / 触发机制 | 内核 / 应用恢复行为 | 验证证据 |
| :--- | :--- | :--- | :--- |
| **队列满载（Backpressure）** | 事件产生速率 > 任务处理能力 | `App_IPC_SendKeyFromISR` 返回 `RTOS_ERR_FULL`；递增 `key_overflows` 与 `drop_count`；安全丢弃，不发生内存越界或系统卡死。 | 5 分钟压力测试记录 574 次受控丢弃，0 传输错误。 |
| **错误 / 伪造 ACK** | 控制器收到不合法 ACK ID（如 0 或非法编号） | `App_Events_RecordAck` 进行范围检测与非零断言保护，拒绝污染状态机。 | Host Test 9 覆盖 ID=0 与 ID=9999 异常 ACK。 |
| **DWT 硬件缺失 / 停用** | 芯片未使能 DWT 计数器 | 快照返回 `RTOS_DIAG_RT_CLOCK_UNAVAILABLE`，周期字段清零，不伪造随机数据。 | Host Test 2 覆盖 `CLOCK_UNAVAILABLE`。 |
| **任务句柄失效 / 槽位复用** | 任务已退出，Generation 已变更 | 快照返回 `RTOS_DIAG_RT_STALE` 或 `RTOS_ERR_NOT_FOUND`，拒绝跨生命周期乱序读数。 | Host Test 8 覆盖 Task Stale 判定。 |
| **高频压力过载** | 连续 10 events/s 注入 300 秒 | 触发有界拒绝保护，0 HardFault，0 Assert，MCU 保持 100 Hz / 1000 Hz 正常调度。 | 300 秒压力测试日志 100% 验证通过。 |
