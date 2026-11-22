# Trace 事件格式与覆盖规则

沿用 Stage 8.2 的 [schema 1](../stage8_2_trace_events.md)。物理记录仍为 24 B，按 ARM 小端输出，C11 验证大小、对齐及字段偏移。

| 偏移 | 字段 | 含义 |
|---:|---|---|
| 0/4/8 | uint32_t sequence,tick,generation | 环序号低 32 位、内核 Tick 低 32 位、主身份代数 |
| 12/16 | uint32_t arg0,arg1 | 由事件 ID 决定 |
| 20/22 | uint16_t slot,event_type | 用户 0–7、Idle=0xFFFF/代数 0、未知=0xFFFE/代数 0 |

布局版本仍为 1。导出头另外带事件集版本，历史为 1，Explain 扩展为 2。ID 1–9、字段含义和旧读函数保持。Explain 构建中旧 raw API 也会读到同一个混合环；导出必须标记事件集 2。支持跳过未知 ID 的旧工具可以保留位置并标记解释不完整；严格旧工具使用 Trace-only 配置。历史文件不重写。

| 诊断事件 | 来源或拟增 ID | 身份和载荷 |
|---|---|---|
| TASK_READY | 现有 2 | 进入 READY 者，包括创建、唤醒与调度重新入队 |
| TASK_BLOCK | 现有 3 | 阻塞者，arg0=wait_reason，arg1=wake_tick。纯 Delay 时有效；无限 IPC 有残值风险，单条无法判断 IPC 有期限 |
| TASK_WAKE | 现有 5 的 GRANTED/CANCELLED；拟增 10 DELAY_EXPIRED | IPC 结果和原等待原因分别为 arg0/arg1。普通 Delay 新记录 arg0=计划 wake_tick，arg1=0。取消解除等待，结果仍为 CANCELLED |
| TASK_TIMEOUT | 现有 5 的 TIMEOUT | 有限 IPC 超时已提交。普通 Delay 到期归 WAKE |
| TASK_EXIT | 现有 8 | 退出提交。7 是停止请求，9 是将回收 |
| TASK_YIELD | 拟增 11 | 已接受的显式 yield 请求，arg0=YIELD 位，arg1=0。选择同一任务时也可保留 |
| SCHED_SELECT | 现有 4 | next 为主身份。arg0=previous generation，arg1=(reason bits<<16) 按位 OR previous slot。含同任务选择与 START |
| CONTEXT_SWITCH | 拟增 12 | 真实 PendSV 中的 C 移交提交，next 为主身份，arg0=previous generation，arg1 如下 |
| TASK_PREEMPT | 从 12 派生 | 仅对已提交且归类为较高有效优先级抢占的任务对生成 |

ID 12 的 arg1 位 0–15 为 previous slot，16–20、21–25 分别为 previous 和 next 的有效优先级，26–29 为 commit_class，30–31 保留。分类建议 0=其他、1=HIGHER_PREEMPT、2=YIELD、3=SLICE_ROTATION、4=BLOCK_OR_WAKE_OTHER、5=EXIT、6=IDLE_TRANSITION、7=MIXED。选择原因位保持 START1/YIELD2/SLICE4/HIGHER8/EXIT16/IDLE32/BLOCK64/WAKE128。ID 4 与 12 在同一临界区相邻写入。关联检查相邻序号和相同任务对。若只能读到 ID 12，仍保留两方身份和分类，原因位标未知。

## 时间、排序和丢失

事件使用内核 Tick 时间戳，sequence 可排序同 Tick 内的事件。DWT 周期仅用于实时统计，Trace 不增加逐条时钟列。环满覆盖最旧，已有 overwritten 饱和。有效生产操作总是落一条记录，生产端丢弃为 0。模块禁用或事件未发生不记为丢弃。

Explain 增两个 32 位控制字，用于 seq 高字与状态。低字回绕时增加高字，同锁下以 next64−count 得 oldest。理论上的扩展序号耗尽将标记扩展 cursor 无效，旧环的 32 位行为保留。采集端应按设备启动建立会话，跨启动重新取游标。

单次读取最多 8 条（192 B），扩展批带同一临界区的窗口和版本。跨批可能覆盖。若 cursor 已落在窗口外，返回 STALE，重新读取窗口并记录 old cursor 至新 oldest 的缺口。若窗口再次移动，有限重试或稍后再读。对本会话有效游标可精确计算欠读量，overwritten 饱和后仅为历史下界。等于 next 时成功返回零条。[API 细节](public_api.md)
