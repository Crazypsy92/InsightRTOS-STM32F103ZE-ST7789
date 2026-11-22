# 后续分阶段实现计划

## 9.1 可解释调度及共用接口

1. 扩展 Trace 事件版本和游标统计，保留唯一 128 条环、原 24 B ABI 和旧 API。新增开关 INSIGHT_DIAG_EXPLAIN，依赖 INSIGHT_TRACE_ENABLED。先测 Trace-only 历史解码、OFF 代码生成及新 reader 的边界。
2. 在原 C 事务中增加三处源点：显式 yield 接受，delay.c 纯 delay 到期，scheduler.c 的 PendSV C commit。Host 模拟与 Cortex-M3 数据来源标识清楚。IPC 等候完成、任务 READY/BLOCK/EXIT、PI 和 SELECT 沿用旧事件。保留同优先级 FIFO、早唤醒以及合并请求的测试。
3. Host 后 QEMU。用真实 ARM 断点对照 C 提交、PSP 恢复和任务入口。证据区分三者。STM32 仅在获新固件部署授权后测临界区开销，保持五页 UI。

## 9.2 Mutex 死锁诊断

先加入按需读图入口和一个静态 workspace，启用 INSIGHT_DIAG_DL。优先复用 task.c 的活动任务遍历和现有代数检查。将受保护的 bounded copy 与锁外无递归搜索分别验证。无 Mutex 变更 Hook，图不参与 PI。Host 测异常数据和穷举小图（含槽位复用），QEMU 测真实 grant/cancel/timeout 和循环。最后在独立板级测试任务上试验有限等待及监督取消。

## 9.3 实时性诊断

加 INSIGHT_DIAG_RT、可选板级时钟能力与按代的 side table。时钟模拟测试优先覆盖回绕、时代失效和时间语义，随后加入发布、block/wake、切换提交、退出/代数发布及物理 SysTick 心跳的最小 Hook。任务 API 明确 job release/complete。板级验证配对 DWT 与外部波形，给出 ISR 归属误差，再判定增量时延。QEMU 无可信周期源时，验证 fake clock/逻辑 Tick 和不可用状态。

## 组合与门槛

每阶段先保证原内核回归和生产 OFF 兼容；三模块均通过后再组合构建，核对 64/512 KiB 链接预算、静态栈以及板上 60 秒五页实际交互。验证上游后以明确 SHA 清单迁移到 Middlewares 快照，阶段包独立输出。默认不接入新的 Monitor 画面或自动诊断任务。后续如有 UI 需求，单独审查只读摘要发布与消费的刷新节奏，保持 KEY/Controller/LCD 的队列、LCD 独占 SPI3/DMA、原 Tick/TIM2 语义。

Stage 9.0 完成后等待实现授权。冻结 C3 固件可继续作为实机基线，Stage 9 的资源、误差、WCET、IRQ、故障和新版实机结果均待实现验收。
