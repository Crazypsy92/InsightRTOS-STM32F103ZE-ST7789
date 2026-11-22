# 公开 C11 API 草案

拟新增头 rtos_diag.h，透传现有 rtos_status_t、rtos_task_ref_t 和 Trace 原始记录类型。以下为提议的接口，Stage 9.0 未生成头文件或链接符号。

~~~c
/* Trace batch_info holds capacity, count, versions, tick_hz,
 * oldest/next sequence, overwritten, producer_dropped and status. */
rtos_status_t rtos_diag_trace_info(rtos_diag_trace_info_t *out);
rtos_status_t rtos_diag_trace_read(uint64_t first_sequence,
                                  rtos_trace_record_t *out_records,
                                  uint32_t capacity,
                                  rtos_diag_trace_batch_t *out_batch);

/* flags includes ENABLED. A zero period/deadline/budget means unset. */
typedef struct {
    uint32_t flags;
    uint32_t period_ticks;
    uint32_t budget_cycles;
    uint32_t deadline_ticks;
} rtos_diag_rt_config_t;
rtos_status_t rtos_diag_rt_configure(rtos_task_ref_t ref,
                                     const rtos_diag_rt_config_t *cfg);
rtos_status_t rtos_diag_rt_job_release(rtos_task_ref_t target, uint32_t *out_job_id);
rtos_status_t rtos_diag_rt_job_complete(uint32_t job_id); /* current user */
rtos_status_t rtos_diag_rt_snapshot(rtos_task_ref_t ref,
                                    rtos_diag_rt_snapshot_t *out);

/* Caller owns a statically allocated workspace and its result until reused. */
rtos_status_t rtos_diag_dl_capture(rtos_diag_dl_workspace_t *work);
rtos_status_t rtos_diag_dl_analyze(rtos_diag_dl_workspace_t *work);
const rtos_diag_dl_result_t *rtos_diag_dl_result(const rtos_diag_dl_workspace_t *work);
~~~

## 结构、错误和一致性

- **通用上下文**：新接口在线程且 PRIMASK/BASEPRI/FAULTMASK 全清时使用。只读 API 可在 prestart/Idle 使用（实时统计没有可用样本时返回未配置/无效指标）；job 发布/完成需调度器运行且调用者为 RUNNING 用户任务。可在 prestart 配置已发布引用。IRQ、NMI、Fault 和外层屏蔽调用返回 CONTEXT，旧 API 的上下文契约不变。
- **所有权**：调用方提供有效、可写、互不重叠的输出和独占 workspace，并在收到结果后再发布给其他任务。C API 不检查任意坏地址。参数、上下文或 stale 错误不覆盖成功输出或已存在的快照。图流程区域为调用方 scratch；错误时原结果不被发布。图捕获完成后持有的是值，分析及其返回指针不会引用内核对象。
- **Trace 批**：最多 8 条，返回 copied 和同一线性化点的 window。first=next 成功且 copied=0；会话内 first<oldest 返回 STALE；first>next 返回 ARGUMENT。禁止跨会话游标复用。在模块关闭的生产配置只保留旧 Trace-only ABI（若该功能独立启用）。采集方遇到 STALE 后读取起点并记录丢失，避免无限重试。
- **实时快照**：输出身份、原生时间域时间戳与时代、开始/完成/无效样本计数、配置、驻留累计、最大 READY/阻塞/首次派发/完成延迟、已结算预算超限和 miss、活动 job/overdue、时间源和指标有效位。单次快照一个代的值，包含当时有效的当前片段。现有一般任务状态继续用 rtos_task_get_snapshot()。未配置或端口能力缺失由逐指标 validity 表达，数值 0 不能替代 N/A。
- **配置/作业**：未知标志或越界参数返回 ARGUMENT。周期/截止非零值上限 RTOS_DELAY_MAX_TICKS。设置非零周期预算但没有 DWT 能力时返回 UNAVAILABLE，调用者可选择只启用 Tick。启用配置只允许活动用户代；改在途 job 的配置返回 STATE，显式停用会中止在途 job 并封存统计。无活动 job 时可更新配置。job ID 单调非零，同代饱和返回 CAPACITY；release 输出非零 ID，重叠返回 STATE 并计一笔拒绝。complete 只接受当前代的活动 ID。终止后读取同代封存值，复用后旧 ref 返回 STALE。
- **死锁**：capture 写一份一致的节点和边；新一轮 capture 会使旧分析结果失效。analyze 完全在锁外，workspace 被 capture 后才可分析；result 仅在分析成功后有效。graph anomaly 是成功抓取的数据质量标志，保留异常节点并禁止推断。捕获的边、周期分类、参与身份和 tick 为对外结果；不提供可用的资源指针。result 返回 NULL 表示结果未就绪、上下文不支持或参数非法。关闭模块时不链接相应接口。

接口用定宽字段、明确版本/容量和编译检查，避免枚举、指针宽度或编译器填充变成跨端口序列化约定。读输出的共享发布仍由应用现有 Queue 或受保护的拷贝完成。
