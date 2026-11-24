# Stage 9.3-B1 Job 生命周期

启用配置后，应用显式 release 创建一个 job。同一任务代只有一个在途 job，ID 从 1 单调增长至 UINT32_MAX。相同数值的 ID 在不同任务代属于不同命名空间。period_ticks 由诊断 API 校验，应用保留周期元数据，调度器不会自动发布或唤醒。

Config flags 的 ENABLED 控制启停，三个参数中的零表示未配置相应指标。非零 deadline/period 不超过 RTOS_DELAY_MAX_TICKS。非零预算需可用的 DWT。活动期间拒绝重新配置。显式停用或本任务 abort 会关闭活动 job 且计 aborted，任务退出也关闭活动 job。归档保留至槽位复用。观察旧代返回 STALE。API 的错误不会改变调度或在途 job；重叠 release/ID 饱和、无效 complete/abort，以及陈旧引用分别可计。

First dispatch 取现有 C 调度提交点。当目标已在运行时 release-to-first 为 0。finish 为 release→complete Tick 的差。deadline=1 时，release 后第一个 Tick 完成按时，第二个 Tick 才计 miss。SysTick 做固定 8 槽检查，避免阻塞任务漏计；同一 job 只记一次。无符号差和 RTOS_DELAY_MAX_TICKS 判定有效区间。

Budget 是 DWT 的 task occupancy 差，包含 IRQ 等边界开销。只对有效周期源判超；精确相等不算超。运行时 tick 检查当前任务，切出和完成时补上边界驻留。同一 job 至多计一次。DWT 不可用或时间源判为不确定时，活动周期结果带无效标记，Tick deadline 独立。两个时间源同时停走不能由自身检测。周期使用超过 UINT32_MAX 时导出饱和，并用状态位标识，内部比较用 64 位。

累计统计 16 位饱和。同代停启保留历史。读取返回临界区内副本，active、first/finish 有效位、当前 job 的 deadline/budget 结算位与归档值独立。旧 56 B 任务快照不变。禁止从诊断值改变任务优先级、等待或控制流。
