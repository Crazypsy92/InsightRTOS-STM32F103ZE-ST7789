# InsightRTOS Stage 9.3-A: 实时性诊断时间计量契约与边界规范 (Timing Contract)

## 一、设计目标与测量基准

本规范根据《InsightRTOS 诊断架构设计》（`docs/diagnostics_design/architecture.md`、`resource_budget.md`）及 Stage 9.3-A 任务要求，明确实时性观测基础设施的计时单位、时间源能力、CPU 周期归属、就绪延迟以及时钟回绕判定边界。

---

## 二、时钟源能力与状态模型 (Clock Capabilities)

### 1. 时间基准定义

| 计时维度 | 基础频率 | 分辨率 | 来源硬件 | 回绕周期 |
| :--- | :---: | :---: | :--- | :---: |
| **CPU Cycles (周期)** | 72,000,000 Hz (标称) | 13.889 ns | Cortex-M3 DWT `CYCCNT` 寄存器 | 32 位：~59.652 秒 |
| **Kernel Tick (调度节拍)** | 100 Hz | 10.0 ms | Cortex-M3 SysTick 定时器 | 32 位：~497.1 天 |
| **TIM2 Timebase (硬件基准)** | 1,000 Hz | 1.0 ms | STM32 TIM2 独立硬件定时器 | 32 位：~49.7 天 |

### 2. 时钟源能力分类 (`rtos_diag_clock_type_t`)

- **`RTOS_DIAG_CLOCK_UNAVAILABLE (0)`**：
  硬件缺乏周期计数器（如部分未开启 DWT 的 QEMU 模拟器），或 DWT `CYCCNT` 未被使能（`DWT->CTRL & 1 == 0` 或 `CoreDebug->DEMCR & TRCENA == 0`）。此时内核 cycle 级 API 返回 `RTOS_ERR_UNAVAILABLE`，严禁伪造假数据。
- **`RTOS_DIAG_CLOCK_TICK_ONLY (1)`**：
  仅具备离散的 100 Hz RTOS Tick 计数能力，不支持 cycle 级纳秒测量。
- **`RTOS_DIAG_CLOCK_CYCLE_ACCURATE (2)`**：
  硬件 DWT 周期计数器正常运行，支持 72 MHz cycle 级单周期高精度测量。

---

## 三、线程态 CPU 执行周期计量与 IRQ 归属模型

### 1. 计量边界 (Accounting Boundaries)

- **进入执行 (Dispatch / Switch In)**：
  在 `rtos_sched_switch()` 提交点，选中的 `next` 任务被置为 `RTOS_TASK_RUNNING`，记录当前时刻的 64 位单调周期 `dispatch_cycles`。
- **退出执行 (Preempt / Yield / Block / Exit)**：
  在 `rtos_sched_switch()` 提交点，切出的 `previous` 任务停止执行，计算此段持续周期：
  $$\Delta \text{cycles} = \text{now\_cycles} - \text{last\_dispatch\_cycles}$$
  并将 $\Delta \text{cycles}$ 累加至该任务的 `charged_cycles`。
- **当前运行段在途快照 (In-Flight Query)**：
  当查询当前正在 `RUNNING` 态的任务时，诊断快照在短临界区内实时采样当前时钟，并返回：
  $$\text{snapshot\_charged\_cycles} = \text{charged\_cycles} + (\text{current\_cycles} - \text{last\_dispatch\_cycles})$$
  确保外部采样无论在何时读取，均能获得精确单调递增的 CPU 执行周期。
- **阻塞态隔离 (Blocked Isolation)**：
  任务在处于 `RTOS_TASK_BLOCKED`（等待信号量、互斥锁、队列、延时）期间，不分配 CPU 时间，其 `charged_cycles` 严格保持不变。
- **空闲任务归属 (Idle Task Attribution)**：
  当系统中无就绪业务任务而运行 `Idle` 任务时，所有的执行周期单独计入 `idle_record.charged_cycles`，严禁摊派给任何业务任务。

### 2. 中断 (ISR/IRQ) 周期归属声明

> [!IMPORTANT]
> **硬件限制与归属约定**：
> ARM Cortex-M3 内核没有针对每一个 ISR 的独立硬件硬件执行时间扣除硬件。
> 在业务任务处于 `RUNNING` 期间发生的中断（包括 100 Hz SysTick、1 kHz TIM2、按键 EXTI、SPI/DMA 传输完成中断等）：
> - 其执行耗时包含在该任务的 `charged_cycles` 中；
> - 诊断快照明确标识此项为 **"Thread Occupancy Cycles (含内嵌 IRQ 开销)"**，不得宣称为无中断扰动的纯算法指令耗时；
> - 对于需要精确扣除 IRQ 的高可靠性场景，在 Stage 9.4 配合专用 GPIO 边沿采样测试给出标准中断开销基线。

---

## 四、Ready-to-Run 就绪响应延迟模型

### 1. 就绪等待延迟定义

任务从“具备运行资格（READY）”到“调度器真正将其切换为 RUNNING”所经历的排队等待时长。

- **起始点 (Ready Mark)**：
  - 新建任务发布就绪：`task_create` 完成时。
  - 阻塞唤醒：信号量/互斥量/队列等待完成或超时到期，任务进入就绪队列。
  - 抢占/时间片重新入队：正在运行的高优先级或同优先级任务让出 CPU 重回就绪队列。
- **结束点 (Running Mark)**：
  - 在 `rtos_sched_switch()` 中，`next` 任务被从就绪队列取出并切换为 `RUNNING`。
- **双重时间维度**：
  1. `latency_cycles`：以 72 MHz 周期计算的响应延迟，反映微秒级精确排队耗时。
  2. `latency_ticks`：以 100 Hz 节拍计算的离散延迟。若唤醒与调度发生在同一个 10 ms 节拍内，`latency_ticks = 0`，代表发生在同节拍内，不代表零延迟。

---

## 五、32 位 DWT 回绕拓展与多回绕不确定性判定

### 1. 64 位单调周期时钟扩展算法

- 硬件寄存器 `DWT->CYCCNT` 为 32 位无符号整型，以 72 MHz 运行时约 59.652 秒溢出回绕（$2^{32} / 72,000,000 \approx 59.6523\text{ s}$）。
- 内核维护全局 64 位单调时间：`s_cycle_epoch`（高 32 位）与 `s_last_cycle32`（低 32 位缓存）。
- **定期更新**：在每次调度切换 `rtos_sched_switch` 和每次 SysTick 心跳 `rtos_sched_on_tick`（每 10 ms 发生一次）调用时：
  ```c
  uint32_t current32 = rtos_port_clock_cycles_32();
  if (current32 < s_last_cycle32) {
      s_cycle_epoch += (1ULL << 32); /* 发生单次回绕 */
  }
  s_last_cycle32 = current32;
  s_last_sample_tick = current_tick;
  ```
  由于 SysTick 周期为 10 ms，远小于 59.65 秒回绕窗口（采样率充裕比达 5965:1），在正常运行状态下能够 100% 可靠捕获单次回绕并保持 64 位单调递增。

### 2. 多次回绕与不确定性判定 (`CLOCK_UNCERTAIN`)

若因外部调试器长时间断点挂起（Halt）、临界区长时间关中断、或时钟源突变，导致距离上一次采样的挂钟时间差超过 **59.0 秒**（即 $\ge 5900$ 个 SysTick ticks）：
- 内核**无法通过 32 位读数差推断究竟发生了一次回绕还是多次回绕**；
- 此时严禁简单作差，内核自动置位状态标志 `RTOS_DIAG_RT_CLOCK_UNCERTAIN`；
- 所有受影响任务的诊断快照返回该标志，明确告知外部监控数据存在时间不确定性，保证系统“宁可报不确定，绝不报假数据”。

---

## 六、任务插槽代数 (Generation) 与生命周期契约

1. **生命周期隔离**：
   - 系统最大支持 8 个用户任务插槽 (`slot = 0..7`)。
   - 每个插槽维护独立的 `task_generation[slot]`，严格单调递增，不回绕。
2. **状态封存与复用**：
   - 任务终止（`RTOS_TASK_TERMINATED`）时，该代的数据被封存，外部持有该代 `ref` 仍可读取其终止前的最终执行数据。
   - 当该插槽被分配给一个新任务时，代数递增（`generation++`），旧代的统计数据被清空并初始化为新任务的起始数据。
   - 持有失效代数引用发起快照查询时，系统返回 `RTOS_ERR_STALE`。
