# Stage 9.0 诊断架构与源码审计

基线为 Stage 8.5-C4-R1 板上运行的冻结 C3 O2。仓库根内核作为上游实现入口，ZET6 集成使用 app/stm32f103ze_st7789/Middlewares/InsightRTOS 的生产快照。已核对的关键 C、H 和 ARM 移植文件在两处逐字节一致。后续实现先在上游验证，再以版本清单更新集成快照。

| 面向 | 真实源码 | 直接复用 | 尚未暴露 |
|---|---|---|---|
| Trace | [公开头](../../include/rtos_trace.h)、[写读实现](../../kernel/trace.c) | 128×24 B 环、generation、9 个事件、覆盖计数、最多 8 条的线性化读 | C 提交阶段、显式 yield 请求、普通 Delay 到期原因、扩展游标 |
| 调度 | [scheduler.c](../../kernel/scheduler.c)、[ready_queue.c](../../kernel/ready_queue.c) | 32 个有效优先级 FIFO，较大值先行。同级 yield/时间片轮转，较高优先级抢占时保留前端位置和剩余时间片，Idle 回退 | 合并请求的唯一触发者、完整候选队列快照、任务实际开始执行时刻 |
| Cortex-M3 异常 | [移植汇编](../../arch/arm/cortex_m3/portasm.S)、[SysTick](../../arch/arm/cortex_m3/systick.c)、[端口](../../arch/arm/cortex_m3/port.c) | SVC 首次恢复 PSP，PendSV 保存 R4–R11/PSP 后调用 C 再恢复新任务，PRIMASK 原子区 | 无异常返回完成事件或通用 IRQ 出入计量。现有 switch hook 属于测试 |
| 任务及生命周期 | [task.c](../../kernel/task.c)、[rtos_task.h](../../include/rtos_task.h) | 8 用户槽和独立 Idle、8 B 引用、12 B ARM 快照、72 B ARM TCB、终止与回收分离 | 多任务同一时刻快照、每任务时间累计与 job |
| 等待、IPC、PI | [wait.c](../../kernel/wait.c)、[delay.c](../../kernel/delay.c)、[mutex.c](../../kernel/mutex.c)、[priority_graph.c](../../kernel/priority_graph.c)、[queue.c](../../kernel/queue.c)、[semaphore.c](../../kernel/semaphore.c) | IPC 唯一完成路径，Mutex owner/wait_object，有效优先级和 PI Trace | 资源图公开快照。Queue/Semaphore 不具备同等任务 owner 语义 |
| 板级时间与 UI | [ZET6 Timebase](../../app/stm32f103ze_st7789/Middlewares/InsightRTOS/port/insight_timebase.c)、[display_app.c](../../app/stm32f103ze_st7789/Core/display_app.c) | 72 MHz HCLK、100 Hz SysTick、1 ms TIM2、已启动 DWT、Monitor 的公开任务快照 | 端口时间能力声明、计数延伸与误差报告 |

rtos_task_get_snapshot() 按单引用保护状态和优先级。连续读四次只得到四个分别一致的时刻。创建期间用 UNUSED 保留槽，栈填充在解锁后执行，发布后才进入 READY。旧代完全终止、脱离 current 和队列且无锁时才可回收。generation 非零且禁止回绕，配置、图和记录均用 (slot,generation)。Idle 和未知仅用 Trace 专用哨兵。

## 三个模块

- **可解释调度**共用唯一 Trace 环，在原临界区写固定记录，在线程或离线解码。Tick 给时间，sequence 给顺序。
- **实时性**采用独立按槽索引的 side table。发布、阻塞、唤醒、生命周期和 C 切换提交更新常数项。累计直接取内核事务，Trace 覆盖不影响统计。端口提供 DWT 能力和读数，Host/QEMU 可以注入时钟。
- **死锁**在线程按需取 Mutex 图。一段 PRIMASK 复制至调用方工作区，随后解锁搜索。owner 和等待引用来自已提交的原内核状态。图以捕获时间和代数标识，结果描述捕获当时。
- **应用**本阶段沿用原 Monitor 和 LCD 分工。未来如需显示，由 Monitor 在线程内解码并发布有界摘要，LCD Task 继续独占 LCD/SPI3/DMA。

三个开关独立、默认 OFF。Explain ON 需要 Trace ON，独立历史 Trace ON 仍支持。RT 和 DL 单开不依赖 Trace。OFF 构建预处理消除新增调用点、状态和时间采样。诊断数据与失败均不参与任务选择、等待完成、PI、锁拥有者或错误码决策。

## 一致性和执行上下文

单核普通 ISR 通过原 IPC 交易写入事件。NMI/HardFault 不调用这些路径。关键路径不搜索图、不格式化、不输出，也不调用用户回调。64 位状态的读写同样受 PRIMASK 保护。每类快照有自己的线性化点；跨模块关联需检查各自时戳。Trace 批数据和窗口元数据可在同一锁内读取。[API 约定](public_api.md)
