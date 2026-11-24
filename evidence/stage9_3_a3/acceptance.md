# Stage 9.3-A3 验收

**SOFTWARE PASS · HARDWARE PASS（本次测量范围）· REVIEW**

接手时重新探测了指定 STM32F103ZET6。页 0–30 的完整读回已匹配交接时生成的常态固件，故无需沿用旧连接恢复脚本。经预检分别烧录常态与压力配置，只擦除页 0–30。两个配置均独立重连，逐字节及 SHA256 与对应 BIN 一致，区间尾部为 FF，Option Bytes 保持一致。

| 核验项 | 实际结果 |
|---|---|
| 常态 | 62.288 s 自由运行，13 份有效 RT 样本 |
| 定速压力 | 300 秒由 TIM2 ISR 发出 3,000 次，303.678 s 观测窗口，61 份有效快照；3,000 接受及成功 ACK，0 FULL、0 CONTEXT、0 错误 ACK、0 pending |
| 突发 | 34 次尝试、33 接受并成功 ACK、1 次真实 FULL，0 CONTEXT、0 pending；压力控制字与剩余次数读回为 0 |
| 板上故障与栈 | 采样中未观察到断言、HardFault 或锁死。五栈 guard 完整；填充估计 KEY/CTRL/LCD/MON/Idle 为 192/388/816/552/72 B |
| 软件回归 | 内核 360/360、应用 Host 9/9、RT Host 9/9、ARM ABI 负例通过 |
| 剩余边界 | QEMU **NOT_TESTED**；物理 LCD 目视待确认；MSP 高水位、严格 WCET 与精确净线程 CPU 时间未测 |

用真实 App_IPC 与 queue.c 复现了 A2 的 Monitor Thread 调用 FromISR 返回 ERR_CONTEXT。旧 574 次没有保存分项返回码，不能据此认定为 Queue FULL，也无法回溯每次实际结果。A3 仅以本轮独立有界突发证明了真实 FULL 和后续排空。现有内核、TCB、SVC/PendSV、四任务优先级和 LCD 绘制代码均未改动。未进入 9.3-B。

代码提交为 bb6c0f6ba1ad14bcc5920edd7646892ae2a88cf8。原交接证据保留，首次只读测试曾因 100 kHz 完整镜像读回耗时 11.59 s 而超出客户端 10 s 超时；后续把读取时限调为 45 s 并在 480 kHz 完成。板上保留压力固件并处于注入关闭状态。仅通过采样观察，不作硬实时或功能安全认证。

详见 [测试与资源结果](test_results.md)、[测量契约](measurement_contract.md)、[固件清单](manifest.json) 和 logs/ 原始证据。
