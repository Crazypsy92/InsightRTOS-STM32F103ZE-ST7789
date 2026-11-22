# InsightRTOS Stage 9.0 设计验收

**DESIGN AUDIT PASS。** 已核查上游和 ZET6 集成快照 21 个关键源文件，逐字节一致。C3 Debug/O2 ELF/BIN/HEX、地址和 13 项包哈希通过独立校验。C4-R1 的 70 项硬件证据哈希复核通过。三组调度示例在 Stage 8.2 ARM Debug/O2 原始 Trace 中逐条核对。使用临时 Cortex-M3 C11 编译检查了设计结构的 24/72/120/216/384 B 等布局断言和 4,504 B 汇总。核验了历史 2,155 项测试 ID 计数。未在 Stage 9.0 重跑完整回归或构建诊断固件。

| 设计问题 | 结论 |
|---|---|
| 三模块能否保持 ABI | 可以。基于 Trace 环、代数 side table 和只读图抓取。保持 ARM TCB 72 B、sp 偏移 0、上下文汇编和 SVC/PendSV ABI |
| Trace 扩展 | 保持 128×24 B 和 ID 1–9；新增事件集版本、扩展游标、显式 yield/延时到期/C 移交提交。Tick+seq 负责事件时序，实时计量使用独立 DWT 统计 |
| 新增静态 RAM | 全关 0 B；Explain 3,096 B，RT 1,024 B，DL 一工作区 384 B。全开预计 4,504 B，链接准入硬上限建议 4,608 B。Flash 增量目标总计 ≤7 KiB，待链接 |
| 直接复用 | READY/BLOCK、IPC grant/cancel/timeout、EXIT/RECLAIM、SCHED_SELECT、PI；现有任务 snapshot/get-ref 及 Mutex owner/wait_object |
| 最少新 Hook | Explain 显式 yield、普通 Delay 到期、PendSV C 提交；RT 任务驻留/等待/生命周期边界、C tick 启动及 SysTick 心跳；DL 只有线程按需的只读快照入口 |
| 实施顺序 | Explain 与兼容 Trace → Mutex 图 → 时间诊断。各阶段 Host → QEMU → 获授权的 STM32，再组合及板上 60 秒回归 |
| ST7789 影响 | 当前 C3 固件和页面无变更。后续 OFF 保持原有路径；开启后的时延、栈与显示刷新代价以 Debug/O2 和实际负载验收。任何 UI 摘要由 Monitor 发布，LCD Task 继续独占总线 |

C 上下文移交证据不等于真正异常返回；首指令由外部观测验证。DWT 第一版的任务驻留统计包含 ISR 和边界开销，净线程周期保留为不可测。Mutex-only 图能报告有限/无限循环风险，Queue/Semaphore 联合图和自动解锁不在本设计范围。

交付 [架构](../../docs/diagnostics_design/architecture.md)、[事件](../../docs/diagnostics_design/event_schema.md)、[调度解释](../../docs/diagnostics_design/explainable_scheduler.md)、[实时性](../../docs/diagnostics_design/realtime_diagnostics.md)、[死锁](../../docs/diagnostics_design/deadlock_diagnostics.md)、[API](../../docs/diagnostics_design/public_api.md)、[资源](../../docs/diagnostics_design/resource_budget.md)、[测试矩阵](../../docs/diagnostics_design/test_matrix.md)、[实现顺序](../../docs/diagnostics_design/implementation_plan.md)。

本轮只新增文档。生产内核、Mutex、TCB、上下文汇编、应用和冻结固件未修改。新版 WCET/栈水位/功能为 **NOT MEASURED / NOT EXECUTED**。本轮未连接、烧录或复位 STM32，未修改 Option Bytes。
