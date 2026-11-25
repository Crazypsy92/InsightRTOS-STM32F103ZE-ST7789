# Stage 9.3-B2c MSP 测量方法

## 区间、启动与重新填充

- 链接脚本将 MSP 保留在 [0x2000F000, 0x20010000)，大小 4,096 B，8 B 对齐。向量首字为 0x20010000，O2 的 _ebss 位于 0x20003350，启动清零和任务栈不覆盖 MSP。.msp_stack 为 NOLOAD。
- 仅 INSIGHT_TEST_MSP 的 Reset_Handler 在第一个 BL SystemInit 之前核验向量提供的 SP 等于链接区间顶。填充只用寄存器，保存并恢复 PRIMASK，四个底部字写入不同 guard，余下写 0xA55A3CC3。反汇编显示该段无 push、pop、BL 或 SP 写入。随后按原顺序运行 SystemInit、复制 DATA、清 BSS、初始化构造器并进入 main。
- SVC 在调度启动时从 MSP Thread 进入。汇编设置 CONTROL.SPSEL，加载 PSP，再重设 MSP 为 _estack 后异常返回。SVC、PendSV 汇编和链接脚本均无修改。
- 稳态与受控负载分别重新填充。仅在 Controller Thread 中先保存/置位 PRIMASK，再核对 IPSR=0、CONTROL.SPSEL=1、MSP 等于区间顶，才扫描上一个窗口并重填下一窗口。该 C 调用链使用 PSP。标记完成后执行 DMB 再发布阶段号。保留底部原始 guard，不用新写入掩盖旧损坏。
- 全窗口由固件自主推进，常态 6,200 Tick，再切换到 10 Hz / 600 个 TIM2 短键刺激的 6,200 Tick 窗口。在线读取只轮询状态和健康。结束从 DWARF 匹配布局读取已封存的三个独立位图和入口记录，不暂停两个测量窗口。
- 固件检查 MSP 底部四字、入口取样范围、时钟和至少 512 B 防线，异常时停止压力请求。每次窗口关闭时把所有偏离填充的字记录成 1,024 位位图，离线重新确认最低变更地址。剩余量同时给出含和扣除 16 B guard 两种口径。

## 异常路径与解释边界

采样点在 rtos_port_tick_start、rtos_sched_switch、SysTick_Handler、TIM2_IRQHandler。以 IPSR 识别实际 SVC/PendSV/SysTick/TIM2，忽略 Thread 中第一次选任务。每个入口分别保存次数及所见最小 MSP。SysTick 读取 TIM2 NVIC active 位和 SHCSR.PENDSVACT，TIM2 读取 SHCSR.PENDSVACT，按实际低优先级 handler 仍 active 的情形累计嵌套。进程未强制触发抢占或更改优先级。尾链且低优先级 handler 已退出的情形不计为嵌套。

MSP 水位指可见的最低改写字，缺少写入的保留帧或恰巧写回哨兵可能导致低估。入口 MSP 是特定测点，不能覆盖更深的子调用。测试探针本身可能产生保存寄存器和调用帧；结果适用于本轮 O2 探针镜像及实际负载。最初填充完成前不可观测；未进行 NMI、HardFault 等故障深链注入。Debug 只构建并做静态分析。

## 静态调用链交叉核对

基于本轮相同配置 Cortex-M3 soft ABI 的 ELF/MAP、反汇编直接调用边及 .su 帧。保守累加尾调用的帧，排除断言失败的停机链；已注册的 MultiButton 间接调用按 key1_read_pin 和 key1_event_callback 核对。下表为单次入口成功路径的局部静态估计。通用调用图包括 Mutex PI 的共享分支，四任务实际 IPC 只配置队列。队列口径排除共享 wait 路径中仅 Mutex 所有者可达的优先级重算。

| 链 | O2 队列 / 通用 B | Debug 队列 / 通用 B |
|---|---:|---:|
| SVC tick 启动 C + 36 B 硬件帧 | 56 / 56 | 92 / 92 |
| PendSV 调度 C + 8 B 显式 MSP 压栈 | 112 / 112 | 208 / 208 |
| SysTick C | 152 / 240 | 196 / 312 |
| TIM2 C | 288 / 376 | 380 / 496 |
| SysTick 嵌套 TIM2，合计 +36 B 硬件帧 | 476 / 652 | 612 / 844 |

Cortex-M3 基本硬件帧 32 B，如需对齐另计 4 B。运行中首次从 Thread 进入异常时硬件帧在 PSP，嵌套异常从 Handler Mode 才在 MSP 入栈。PendSV 的 R4–R11 在 PSP，显式 MSP 只保存 r3/lr，共 8 B。启动 SVC 入栈前 Thread 仍用 MSP，以上 56/92 B 从当时 MSP 算起，启动调用者的既有深度另由启动水位覆盖。单个链估计相加不会考虑 PRIMASK 屏蔽段限制，不当作嵌套可达性的证明。PendSV C 主体屏蔽中断，只需关注屏蔽前和重启中断后的短区段。

[队列链](logs/stack-chains-queue-only.log) · [通用链](logs/stack-chains.log) · [O2 异常反汇编](logs/entries-O2.log) · [Debug 异常反汇编](logs/entries-Debug.log)

## 两轮观测结果

| 工况 | SVC / PendSV / SysTick / TIM2 次数 | 最低可见写入深度 | 最低采样入口 MSP |
|---|---|---:|---|
| 启动及预热 | 1 / 626 / 300 / 3,000 | 236 B | 0x2000FF78（SVC） |
| 常态 | 0 / 12,936 / 6,200 / 62,000 | 132 B | 0x2000FFD8（PendSV） |
| 压力 | 0 / 14,877 / 6,200 / 62,000 | 252 B | 0x2000FFD8（PendSV） |

SysTick 抢占 TIM2、SysTick 抢占 PendSV、TIM2 抢占 PendSV 均无观测，状态 NOT_TESTED。水位 252 B 来自已实际运行的路径，不能替代静态 476 B 的队列嵌套估算。静态估算又不能代替平台实际执行。

B2b 报告的参考周期超标与 ON/OFF Job 负载差异仍是性能观察限制。本轮未复测该指标。没有观察到栈底接近 512 B 防护阈值，也没有观察到异常入口失败。

## 复现

构建 DISPLAY_RTOS_O2_Msp、DISPLAY_RTOS_Debug_Msp 和正常 DISPLAY_RTOS_O2_Trace。正式 O2 探针镜像从同次 ELF 导出 BIN/HEX/MAP，经独立审计后在 tests/hardware/run_timing.sh 的 Msp 模式部署。传入已独立核验的旧镜像授权区间读回和空白暂存目录。程序只运行一次，完成后由 tests/hardware/read_msp.py 裁取封存位图、入口计数和健康数据。
