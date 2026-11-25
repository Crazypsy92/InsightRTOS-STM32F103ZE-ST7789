# Stage 9.3-B2a Flash 验收

**PASS_WITH_BUDGET_EXCEPTION**。完整 Job API 的同配置 O2 Flash 增量从 **3,872 B 降至 3,248 B**，减少 **624 B**，仍超出 3,072 B 目标 **176 B**。完整 API 保留，RT OFF 镜像前后逐字节一致。

基线为 B1 合入 main 的 `c98cf82af6451c8fe441696e79e80f7dc2751e59`。生产改动仅在 diag_rt.c，共用周期驻留、上下文检查、计数饱和及 Job 结束路径，减少初始化和快照中的重复代码。公开 API、结构布局、调度和上下文 ABI 保持。DWT 失效、历史归档、Tick Deadline、Budget 和 generation 继续使用原判定。

| 验证 | 结果 | 日志 |
|---|---|---|
| 应用 Host | 9/9，退出 0 | [应用](logs/app-host.log) |
| 历史 RT、B1 Job 和 DWT 失效 | 9/9、77 项和 53 项，退出 0 | [RT/Job](logs/rt-host.log) |
| 内核 CTest | 干净重建，360/360，退出 0 | [内核](logs/kernel-host.log) |
| ARM Debug/O2 RT ON/OFF | 四套严格构建通过 | [构建资源](logs/resources.json) |
| API 与 OFF 消除 | 六个指定 API 全部保留；OFF 无 RT Hook、API 和状态 | [符号与资源](logs/resources.json) |

Host 入口为 `bash scripts/test/run_host.sh` 和 `bash scripts/test/run_rt_host.sh`。360 项使用独立内核测试目标及 B1 时钟修复阶段已有的 legacy-overlay.cmake，通过 CMAKE_PROJECT_InsightRTOS_INCLUDE 引用本分支的 task.c、diag_rt.c 和接口头。启用 Host、Trace、Explain、DL、RT、Stage 9.1、Stage 9.2，在全新目录构建并执行 `ctest --output-on-failure -j6`。历史测试与断言保持。

TCB 72 B、完整 API 的 RT 内核状态 1,016 B、heap 0。用户任务 8×4,096 B、Idle 512 B、MSP 4,096 B。本轮未连接或烧录开发板，未测关键路径周期或 MSP 水位。有 [Debug 栈帧](logs/Debug-on-stack.log) 和 [O2 栈帧](logs/O2-on-stack.log) 静态记录。
