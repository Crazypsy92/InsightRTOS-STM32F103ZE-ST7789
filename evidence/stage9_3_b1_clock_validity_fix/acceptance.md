# Issue #14 活动 Job 快照的 DWT 有效性修复

固定基线为 PR #10 的 `agent/stage9-3-b1`，head `4a099a3b9224c5636c73e135c0548d1798cfdab8`。下表和日志只对应该提交上的修复。未执行实机复测。

## 根因与修复

Tick 路径记录全局 DWT 失效，但只更新运行槽的 Job 预算标志。就绪或阻塞 Job 的缓存 flags 因此可能在失效期间保持原值。现在 `rtos_diag_rt_job_snapshot()` 在原 PRIMASK 临界区内合并当前 Job 状态和时钟有效性。活动 Job 若遇时钟不可用或已确认不确定，导出 `CYCLES_INVALID`。若快照是 DWT 失效的唯一观察者，原本已初始化的时钟也会记为不确定，保证恢复启用后不会把采样间隙当作可信时间。

失效标志反映到本次输出。非活动、已完成及可查询的归档结果保持各自已有 flags 和数值。Deadline、First/Finish Tick、调度和驻留累计路径均沿用原实现。新增补偿只在诊断快照读取时执行。

## 测试与前后对照

| 检查 | 结果 |
|---|---|
| 新测试与旧生产代码 | 阻塞 Job 快照的失效位断言失败，退出 134。四组原 Job 检查仍通过。 |
| 应用历史 | 9/9，45 项断言 |
| RT 历史 | 9/9，45 项断言 |
| 原 B1 Job | 77 项通过。启用状态依次验证 FIRST_VALID、FINISH_VALID 和 BUDGET_VALID，时钟失效时显式验证 CYCLES_INVALID。 |
| 新 Host Job | 43 项覆盖阻塞、就绪、重新运行、DWT 恢复、Deadline 独立和有效归档。另 10 项覆盖快照单独观测时钟失效并延续到结算。 |
| 内核 CTest | 360/360，全新目录重新构建。 |

测试通过公开 API 及现有 Host Fake DWT。阻塞用例在恰好 1 Tick 时仍为 0 miss，跨到第 2 Tick 计 1 miss；第 10 Tick 醒来完成，仍为 1 miss 且无有效预算结果。另一任务先前完成的 20 周期样本仍有效。原测试现在要求三个有效位全部存在。

应用与 RT 入口分别是 `bash scripts/test/run_host.sh` 和 `bash scripts/test/run_rt_host.sh`。360 项使用独立内核工程的历史测试目标，通过 [CMake 注入文件](logs/legacy-overlay.cmake) 从修复 checkout 引用 task.c、diag_rt.c 及接口头。使用 `CMAKE_PROJECT_InsightRTOS_INCLUDE` 载入注入文件，`B1_SOURCE` 指向修复 checkout 的 Middlewares/InsightRTOS，启用 `INSIGHT_HOST_TESTS`、`INSIGHT_TRACE_ENABLED`、`INSIGHT_DIAG_EXPLAIN`、`INSIGHT_DIAG_DL`、`INSIGHT_DIAG_RT`、`INSIGHT_STAGE9_1_TESTS`、`INSIGHT_STAGE9_2_TESTS`，随后构建并执行 `ctest --output-on-failure -j6`。历史测试源码及断言未改动。

## ARM 审计

使用 `DISPLAY_RTOS_Debug_Trace` 与 `DISPLAY_RTOS_O2_Trace`，分别使用原预设及同配置 `-DINSIGHT_DIAG_RT=OFF` 进行干净构建。

| 配置 | Flash BIN / B | 静态 SRAM 含 MSP / B |
|---|---:|---:|
| Debug RT ON | 72,576 | 49,336 |
| Debug RT OFF | 68,764 | 48,008 |
| O2 RT ON | 51,172 | 49,344 |
| O2 RT OFF | 48,908 | 48,016 |
| O2 RT ON，保留完整 API | 52,780 | 49,352 |

ELF 与 MAP 验证 TCB 72 B、heap=0、用户栈 8×4,096 B、Idle 512 B、MSP 4,096 B，完整 API 的 RT 常驻内核状态为 1,024 B。RT OFF 镜像无 RT Job 符号、Hook 或相关状态；生产镜像不含测试入口。完整 API 对照增量 3,872 B，比 B1 历史报告的 3,816 B 多 56 B。Flash 收敛归 Issue #12。

受版本控制的生产改动仅在 diag_rt.c 的快照函数。TCB、SVC/PendSV 汇编、调度、优先级、队列和 Mutex PI 源码无改动。证据见 [ARM 符号与资源](logs/arm-audit.log)、[修复前](logs/rt-before.log)、[修复后](logs/rt-after.log) 和 [内核 360 项日志](logs/kernel-host.log)。
