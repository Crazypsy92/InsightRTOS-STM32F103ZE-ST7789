# 可解释调度

## 观察层次

1. **请求**由 rtos_sched_request 合并到 pending_reasons 并置 PendSV。显式 yield 记录请求；在屏蔽区被合并或延迟服务时，不能从请求计数推断切换次数。
2. **决策**由 rtos_sched_switch 执行。ready_queue_pop 按最高有效优先级选 FIFO 首项；无用户可运行时走 Idle。现有 SCHED_SELECT 在 next 标为 RUNNING 并发布 current 后写入。首次 rtos_start 也是 C 决策，随后通过 SVC 启动。已服务的预抢占 PendSV 在 reasons=0 时可无记录返回。
3. **C 上下文移交提交**由同一 C 位置追加。ARM 仅在 previous/next 非空、身份不同且 IPSR 为 PendSV 时发 CONTEXT_SWITCH。此时 outgoing PSP 和 R4–R11 已保存，incoming 恢复与异常返回仍在之后。Host 对等记录带模拟来源，QEMU 验证 ARM 路径。START 单独保留 SCHED_SELECT。same-task 逻辑选择或空 PendSV 不计任务切换。现有 SVC/PendSV 汇编无需改动。
4. **执行证明**通过现有断点符号和已选任务的实际入口/指令验证，或外部硬件跟踪。故障可能发生在 C 提交与目标任务首指令之间。运行诊断不将 CONTEXT_SWITCH 解释为“异常返回已完成”。

## 归因规则

保留 pending_reasons 原始位掩码、前任务原始状态、决策点前后有效优先级。确认 previous 原为 RUNNING、不是 Idle、next 优先级更高并且没有相冲突的 YIELD/SLICE 时，才输出较高优先级 TASK_PREEMPT。轮转需要同级实际移交及相应请求。Idle 往返单列，BLOCK/EXIT 优先反映前任务已不可运行；原因互相竞争时保留 MIXED。调度器当前会在从 Idle 或前任务已不 RUNNING 的部分唤醒路径设置 HIGHER。因此仅看到 HIGHER 位时，只能说这是请求的一部分。

BLOCK→READY 可能在 PendSV 保存 outgoing 之前完成。现有调度不会重复入队，也可能重新选中同一任务。此时应解释成“曾提交等待且已解除，仍选择原任务”。PI 的 READY 队列重排不产生另一条 READY。旧记录无法说明某个具体 IRQ 是唯一触发者，也无法恢复被覆盖的候选集合。

## 三组已存在的真实记录

以下为 Stage 8.2 的 QEMU ARM Debug/O2 实际 Trace。槽号以 slot/generation 表示，时间为该场景的 kernel tick。解释为原事件可支持的结论，新 ID 尚未部署。[原始记录](../../evidence/stage8_2/trace-records.json)

| 位置 | 原始事件 | 可解释结论 |
|---|---|---|
| seq 22–25，Tick 2 | CREATE 3/1，base=7；3/1 READY，0/1 重新 READY；SELECT 3/1，previous=0/1，reasons=0x08 | 新就绪的高优先级任务被选中。原场景 0/1 的创建优先级为 4；样本中它此前没有有效优先级变化。该段说明选择方向，不声称 incoming 已执行 |
| seq 26–29，Tick 2 | 1/1 effective 3→7，2/1 effective 3→7；3/1 BLOCK，wait_reason=Mutex；SELECT 0/1，previous=3/1，reasons=0x40 | 3/1 等 Mutex，等待链上两任务继承至 7。调度选择当时可运行的 0/1。优先级提升本身不会使被阻塞的 holder 就绪 |
| seq 39–43，Tick 3 | 1/1 effective 7→1；2/1 WAIT_COMPLETE=GRANTED(Mutex)，2/1 READY，1/1 重新 READY；SELECT 2/1，previous=1/1，reasons=0x88 | 锁交接与 PI 回落后，获得锁的任务被选中。GRANTED 描述等待完成，而 0x88 给出 WAKE 与 HIGHER 请求。seq 60–64 可进一步看到旧 1/1 回收、同槽 1/2 发布，新旧身份不合并 |

最小新增 Hook 是纯 Delay 到期分支、显式 yield 接受点及 C 发布 current 的提交点。IPC 的 TIMEOUT/GRANTED/CANCELLED、任务终止、有效优先级变更直接解码既有记录。不开启 Explain 时保留原事件集合。
