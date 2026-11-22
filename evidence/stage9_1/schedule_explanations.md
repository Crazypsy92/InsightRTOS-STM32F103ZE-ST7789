# InsightRTOS Stage 9.1: Explainable Schedule Causal Analysis

## 一、可解释调度因果分析范例

可解释调度的核心价值在于：将原先分散、晦涩的内核底层切换，转化为具有明确因果解释的事件链。以下样例取自真实 QEMU ARM Cortex-M3 执行过程中 `rtos_diag_format_explanation()` 输出的真实日志。

---

## 二、六大典型因果解释场景

### 场景 1：高优先级任务唤醒抢占 (Preemption)
- **事件序列**：
  ```
  Task created [slot 1, gen 1]
  Task became READY [slot 1, gen 1]
  Task became READY [slot 0, gen 1]
  Scheduler select: next [slot 1, gen 1] from [slot 0, gen 1] reasons=0x8
  Context switch commit to [slot 1, gen 1, prio 6] from [slot 0, gen 1, prio 4] class=HIGHER_PREEMPT
  ```
- **因果解释**：
  - **当前运行**：Ctrl 任务 (优先级 4)。
  - **触发原因**：新创建的 High 任务 (优先级 6) 变为 READY。
  - **调度判定**：调度器检测到更高优先级任务存在 (`reasons=0x8`，就绪队列抢占)。
  - **提交分类**：`HIGHER_PREEMPT`。Ctrl 任务仍处于就绪态，被迫让出 CPU 给 High 任务。

---

### 场景 2：阻塞与切入空闲 (Block & Idle Transition)
- **事件序列**：
  ```
  Task BLOCKED [slot 0, gen 1] reason=0 wake_tick=3
  Scheduler select: next [slot 65535, gen 0] from [slot 0, gen 1] reasons=0x60
  Context switch commit to [slot 65535, gen 0, prio 0] from [slot 0, gen 1, prio 4] class=IDLE_TRANSITION
  ```
- **因果解释**：
  - **当前运行**：Ctrl 任务 (优先级 4)。
  - **触发原因**：调用 `rtos_delay(3)`，进入阻塞状态 (`reason=0` 即纯延时，计划在 tick 3 唤醒)。
  - **调度判定**：系统中所有就绪优先级位图清空，调度器选择全局 Idle 任务 (`slot 65535, prio 0`)。
  - **提交分类**：`IDLE_TRANSITION`。

---

### 场景 3：延时到期唤醒 (Delay Expired & Resume)
- **事件序列**：
  ```
  Delay expired [slot 1, gen 1] wake_tick=2
  Task became READY [slot 1, gen 1]
  Task became READY [slot 65535, gen 0]
  Scheduler select: next [slot 1, gen 1] from [slot 65535, gen 0] reasons=0xa8
  Context switch commit to [slot 1, gen 1, prio 6] from [slot 65535, gen 0, prio 0] class=IDLE_TRANSITION
  ```
- **因果解释**：
  - **触发原因**：SysTick 到达 tick 2，`rtos_delay_process_expired()` 扫描到 High 任务到期。
  - **状态演变**：记录 `RTOS_TRACE_DELAY_EXPIRED`，并将 High 任务重新挂入优先级 6 就绪队列。
  - **调度判定**：High 任务从 Idle 抢回 CPU，调度提交完成。

---

### 场景 4：同优先级显式 Yield (Explicit Yield)
- **事件序列**：
  ```
  Explicit yield [slot 2, gen 1] reason=0x2
  Task became READY [slot 2, gen 1]
  Scheduler select: next [slot 3, gen 1] from [slot 2, gen 1] reasons=0x2
  Context switch commit to [slot 3, gen 1, prio 2] from [slot 2, gen 1, prio 2] class=YIELD
  ```
- **因果解释**：
  - **当前运行**：FIFO1 任务 (`slot 2, prio 2`)。
  - **触发原因**：任务代码显式调用 `rtos_yield()`，记录独立 Yield 事件 (`reason=0x02`)。
  - **就绪调整**：FIFO1 任务被移至其就绪队列末尾。
  - **调度判定**：调度器从就绪队列头部选取同优先级的 FIFO2 任务 (`slot 3, prio 2`)。
  - **提交分类**：`YIELD`。清楚区隔于时间片耗尽引发的强制切换。

---

### 场景 5：任务自然退出与回收 (Natural Exit)
- **事件序列**：
  ```
  Task terminated [slot 2, gen 1]
  Scheduler select: next [slot 3, gen 1] from [slot 2, gen 1] reasons=0x10
  Context switch commit to [slot 3, gen 1, prio 2] from [slot 2, gen 1, prio 2] class=EXIT
  ```
- **因果解释**：
  - **当前运行**：FIFO1 任务 (`slot 2`) 从入口函数自然返回。
  - **状态演变**：内核将任务标记为 `RTOS_TASK_TERMINATED` 并记录 Terminate。
  - **提交分类**：`EXIT`。调度器移交 CPU 并释放原任务占用的运行状态。

---

### 场景 6：槽位复用失效检测 (Generation Protection)
- **检测机制**：
  当旧任务 `[slot 2, gen 1]` 终止后，该槽位被新任务重新分配为 `[slot 2, gen 2]`。
  如果外部诊断接口使用旧引用访问，`rtos_task_slot_generation_locked(2)` 返回 2。
  `rtos_diag_explain_record()` 检测到 `old_gen != current_gen`，立即将 `task_ref.is_stale` 标记为 `true`。
  系统能够准确警示：“该历史记录引用的任务实体已消亡，槽位已被复用”，防止诊断数据被误归因到新任务。
