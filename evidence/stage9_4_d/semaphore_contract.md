# Stage 9.4-D Semaphore 验证规约

## 配置与时序

独立 DISPLAY_RTOS_O2_Semaphore 和 DISPLAY_RTOS_Debug_Semaphore 档位复用 Cortex-M3 启动、72 MHz 时钟、100 Hz SysTick、1 kHz TIM2 与固定任务栈。测试入口替代正式应用入口，排除正式应用的重复静态栈。Low、High、Peer、Observer 优先级 2、5、5、6，数值大者先运行。Idle 保持。

Observer 用有限信号量门闩安排三个 worker。测试目标 Semaphore 的等待上限为 4 到 30 Tick，worker 接受命令的上限 500 Tick。TIM2 真正的更新事件在 IRQ 28（IPSR 44）时调用测试钩子。每轮只 arm 一次，并在调用现有 rtos_sem_give_from_isr 前清除请求。钩子只采集固定字段，返回后由正常 PendSV 完成调度。未显式设置 PSP 或向内核对象注入状态。完成后 worker 与 Observer 退出，Idle 可以继续运行。

## 证据与判定

| 路径 | 验证证据 |
|---|---|
| TIM2 Give | 给许可前 waiter identity、BLOCKED 和计数 0；ISR 返回时 waiter READY、计数仍为 0、PendSV pending，ISR 内恢复计数未增加；退出后实际 Thread 恢复，IPSR 0，take 成功。Trace CONTEXT_SWITCH 的槽位和 generation 匹配。每轮额外 NO_WAIT 证明无多余许可。 |
| 优先级与 FIFO | 依次让 Low、High、Peer 阻塞。Observer 每次从真实等待链记录头部并 Give，快照验证 READY，再由 Thread 与 Trace 记录 High → Peer → Low。 |
| 容量 | 计数型初值 1、上限 3。先消费初值，再 Give 三次，满值多一次返回 FULL，三次 Take 归零，空值 NO_WAIT 返回 UNAVAILABLE。二值超额 Give 返回 FULL。Thread 中误调用 FromISR 返回 CONTEXT。 |
| deadline 与恢复 | 一轮 real TIM2 Give 放在 deadline 前 1 Tick。另一次无 Give，真实 SysTick 唤醒并返回 TIMEOUT，随后 Give/Take 正常。另安排阻塞中的 Peer 取消并检查 CANCELLED。没有把一个实机时序外推到全部指令竞争变体。 |

所有现场结果经只读 MEM-AP，按 ELF DWARF 解码，完成态的完整结构独立读取两次并比较。此处无 LCD 路径，正式 O2 恢复后用原综合快照读者核对发布推进。维持既有 STM32F103ZET6 主 Flash 页 0–30、保护和 Option Bytes 限制。

## 复现

使用两个 Semaphore 预设和正式 DISPLAY_RTOS_O2_Integrated 构建。Host 针对既有 stage4_1 至 stage4_5 标签运行 CTest，并运行应用与 RT/Job 脚本。生成独立 ELF/BIN/HEX 审计后，tests/hardware/run_timing.sh 的 Semaphore 模式接收旧镜像授权区间的已验证读回。恢复镜像来自已冻结的 build/Stage94A_Frozen_Production。现场记录保存在 logs/，未开始擦写时也保留前检失败。

## 实机记录

| Give Tick | 截止 Tick | TIM2 ms | ISR 返回 | Thread 返回 | Trace 前一任务 |
|---:|---:|---:|---|---|---|
| 2 | 15 | 21 | OK | OK | Idle |
| 5 | 18 | 51 | OK | OK | Idle |
| 8 | 21 | 81 | OK | OK | Idle |
| 11 | 24 | 111 | OK | OK | Idle |
| 14 | 27 | 141 | OK | OK | Idle |
| 19 | 20 | 191 | OK | OK | Idle |

各行中等待者是 High slot 1 / generation 1。ISR 之后的对应 Trace 均为 idle_transition。所有 Thread take 的 IPSR 都是 0。普通 Thread Give 场景中两个高优先级同为 5，High 比 Peer 先登记，因此排序为 High、Peer、Low，正好验证优先级层级和 FIFO 两个规则。

上限测试观察到 RTOS_ERR_CONTEXT=4、RTOS_ERR_UNAVAILABLE=5、RTOS_ERR_FULL=6。未发生许可饱和越界。超时为 RTOS_ERR_TIMEOUT=7，取消为 RTOS_ERR_CANCELLED=10；两者都在无剩余 count 的情况下结束。夹具和恢复镜像均复用同一个已核验的硬件读回脚本。
