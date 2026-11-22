# ZET6 资源与关键路径预算

冻结 C3 的物理 Flash 512 KiB，SRAM 64 KiB。O2 MAP/ELF 的静态区截至 0x20009330，MSP 独立位于 [0x2000F000,0x20010000)，输出 Flash 为 58,804 B；Debug Flash 77,416 B，静态区截至 0x20009338。SRAM 计入对齐与 MSP。8 个用户栈共 32,768 B，Idle 512 B，TCB 池 576 B，heap=0。C4-R1 的栈填充使用估计为 KEY 176 B、Controller 328 B、LCD 696 B、Monitor 248 B、Idle 72 B；MSP 物理高水位无数据。[C3 链接资源](../../evidence/stage8_5_c3/memory_report.md) · [C4-R1 栈与稳定性](../../evidence/stage8_5_c4_r1/stability_60s.md)

## 可复核的静态分配

| 模块 | 32 位目标拟定布局 | 增量 |
|---|---|---:|
| Explain 单开 | 128×24 + 16 现有 Trace，外加 seq 高字与状态 2×4 | 3,096 B |
| RT 单开 | 8×120 任务 side entry + 64 全局时钟、当前驻留锚点、计数和状态 | 1,024 B |
| DL 单开 | 一实例静态调用方 workspace 384 B，含 216 B 公共快照、32 B 任务临时引用、32 B 资源键、24 B 迭代 scratch、48 B 结果及 32 B 预留 | 384 B |
| 全开 | 仅一份 Trace 和一份图工作区 | 4,504 B |

RT 任务项按 9×uint64 和 12×uint32，8 字节对齐。64 位项为 charged_cycles、release_tick、release_charge、ready_at、blocked_at、max_ready_cycles、max_block_cycles、max_first_dispatch_ticks、max_finish_ticks。32 位项为 generation、period_ticks、deadline_ticks、budget_cycles、job_id、completed_jobs、budget_overruns、deadline_misses、aborted_jobs、rejected_releases、invalid_jobs、flags。flags 含等待类别和最大阻塞类别、时间有效性、配置与 job 状态。全局 64 B 封顶，包含宽时间、周期低字、有效性、epoch 和驻留/丢失计数。实际 C11 定义以静态大小断言核验；公开输出按需组合，避免第二份 per-task 数据。

DL 快照为 24 B 头加 8×24 B 槽。每槽含 8 B 任务引用、4 B 快照内资源序号、4 B wake_tick、8 B owner_slot、类型、状态、优先级和标志或保留。owner generation 从对应槽取。ARM workspace 预算不等同于 Host 64 位指针尺寸。裸指针 scratch 仅在捕获内部存在。图不需要内核常驻工作区。配置只预算一个调用方实例；多实例另计。

| 组合 | SRAM 增量计划 | Debug 已用 / 余量 | O2 已用 / 余量 | Flash 新增上限目标 |
|---|---:|---:|---:|---:|
| 全关闭（含 Trace OFF） | 0 B | 41,784 / 23,752 | 41,776 / 23,760 | 0 B |
| Explain | 3,096 B | 44,880 / 20,656 | 44,872 / 20,664 | 2,048 B |
| RT | 1,024 B | 42,808 / 22,728 | 42,800 / 22,736 | 3,072 B |
| DL | 384 B | 42,168 / 23,368 | 42,160 / 23,376 | 2,048 B |
| 全开 | 4,504 B | 46,288 / 19,248 | 46,280 / 19,256 | 7,168 B |

表为预算，不是新固件链接结果。Stage 8.2 仅开历史 Trace 在独立 STM32 上实际增加 3,088 B SRAM 和 Debug/O2 660/708 B Flash；该成本已包含于 Explain，不能再叠加。全开配置预留 4,608 B 硬上限，涵盖链接对齐及接口调整，留出至少 19,144 B SRAM（按 Debug），并保留原栈和 heap=0。表内含一个图工作区。若应用静态分配 Trace×8 读缓冲，应另计 192 B 加元数据，或复用已有调用方 scratch；不从现有栈预留减项。Flash 以实际 MAP、ELF 批准。在没有新链接证据时仍按上限目标处理。

## 栈与锁持有预算

每用户任务的**容量增量目标为 0 B**，实际调用链额外深度目标 Explain ≤64 B，RT ≤96 B，DL ≤96 B（使用静态工作区），全开最坏可达路径 ≤128 B，Idle 仍用 512 B。MSP 对单模块的增量目标 Explain/RT 各 ≤64 B，DL 的捕获在 Thread 栈，MSP 不新增调用；全开保守目标 ≤128 B。以旧深嵌套 Debug 3,788/4,096 B 为持续约束，全部增量仍至少留 180 B 目标余量；该历史数字不得替代 C3 及新固件的链分析。重新核查 IRQ、PSP 与 MSP 的可达最坏链、8 B 对齐、异常栈帧、64 位帮助函数和各配置 .su 及反汇编。报告实际水位和可测范围。[历史 MSP 预算](../../evidence/stage8_4a/stack-budget-report.md)

环满覆盖最旧，写入 O(1) 并更新覆盖计数。线程按批拉取最多 8 条；解码和存储均在解锁后执行。连续高负载下 128 条窗口可很快被覆盖，60 秒测试需记录窗口丢失数，不能把环容量当 60 秒完整日志。

## 最坏路径验证方案

1. 固定相同工具链、优化、时钟和 Flash wait state，构建 OFF/单开/全开各 Debug/O2。检查 ELF/MAP、符号、汇编及 .su。OFF 保持内核关键路径机器码与基线相同（忽略地址重定位/调试元数据后比对），无诊断 RAM 或调用点。
2. STM32 专用测试构建用 DWT 测已有临界区的进入到恢复、PendSV 中 C 决策边界、SysTick 最忙路径。另用 GPIO/逻辑分析仪独立测 IRQ 整体及屏蔽窗口。测试测量 Hook 与产品构建隔离，先校准空读开销。保留原始 cycle、样本量、最大值、分位值、屏蔽窗口、编译选项和反汇编。
3. 覆盖首次开环、非满/已满、序号回绕，同时 8 槽可就绪/同 tick 超时、ARM 合并 PendSV、Mutex PI 链/循环重算、TIM2 FromISR 叠加、读满 8 条、图 8 活动节点全边以及多任务栈最深链。软件输出解析在测试线程或板外。
4. 作为第一版附加开销待测门槛，72 MHz 下 PendSV C 路径 ≤800 cycle，最重 SysTick ≤2,000 cycle，满批 Trace 和图抓取各自额外连续屏蔽 ≤2,000 cycle（约 27.8 µs）。比较同一输入的 ON−OFF，独立报告绝对 IRQ 延迟和现有工作负载是否满足其时限。若超目标，先调整记录/采集频率，保留实际数据。QEMU 或静态指令数用于覆盖和归因，板上最大样本仍只是实测上界样本。
