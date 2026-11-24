# Stage 9.3-B1 验收

**FUNCTIONAL_PASS**。显式 Job、相对 Deadline、DWT 执行预算、Abort 和 generation 隔离通过生产代码测试。应用历史 9/9、RT 历史 9/9、内核 360/360；B1 新增 77 项 Job 检查。ARM Debug/O2 构建通过。TCB 每项 72 B，SVC/PendSV 汇编、调度行为、业务任务优先级及显示页面源码无改动。

A3 先通过门禁并经 PR #9 合入 main，merge commit 为 `7a054e3854a5ce1caa29b78f0bdc86b6a6c82de5`。A3 的代码和证据提交均在其祖先链内。B1 代码为 `e1728d1b2a3d2b035ee7c0e8e36ada9087681990`。

## MEASURED_LATENCY

STM32F103ZET6 的独立 O2 Job 配置读回与 54,700 B 输入一致，授权范围内剩余 8,788 B 为 FF，Option Bytes 前后一致。独立静态 JobTest 任务发布了以下结果。

| 场景 | 板上结果 |
|---|---|
| 1 Tick 的 Deadline | 恰好 1 Tick 完成，0 次 miss |
| 相同 Deadline，晚 1 Tick | 2 Tick 完成，累计恰好 1 次 miss |
| 100,000 周期预算 | 计费 150,658 周期，累计恰好 1 次 overrun |
| 显式 Abort 后退出 | 累计 1 次 abort，0 个活动 Job，0 次无效结算，guard 完整 |

连续自由运行 62.205 s，13/13 样本有效。Tick 1,709 → 7,930，TIM2 17,220 → 79,426 ms，LCD 刷新 64 → 302。DWT 标记有效，四业务任务和 Idle 的栈 guard 完整。Controller、LCD、Monitor 和 Idle 持续被调度，KEY 在无输入窗口内处于等待。未观察到断言、硬件错误状态、LCD 初始化或传输错误。原始证据见 [Job 快照](logs/job-fixture.json) 和 [连续运行](logs/normal/summary.json)。

## BOUNDARY_UNVERIFIED

- QEMU 标记 NOT_TESTED，当前运行环境缺少 qemu-system-arm，本轮无 STM32F103ZET6 板级仿真结果。
- 自由运行窗口没有人工按键或 LCD 目视确认。传输及调度状态来自 SWD 记录。
- RT 常驻内核状态为 1,024 B；.su 和反汇编审核显示调度路径局部栈增量在目标内。O2 正常链接对同配置 RT OFF 增加 2,264 B，保留全部公开 Job API 时增加 3,816 B，超过 3,072 B 目标 744 B。完整 API Flash 预算仍待收敛。具体归因见 [测试与资源记录](test_results.md)。
- DWT 的任务驻留计费可含该窗口内中断开销。实测值只适用于本次配置和负载。关键路径 WCET、MSP 实际峰值和有效时间源同时停走均未验收。

**SAFETY_UNCERTIFIED**。此结果属于工程功能和观测验收。
