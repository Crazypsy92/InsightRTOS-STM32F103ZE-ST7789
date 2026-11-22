# C3 O2 连续自由运行 60 秒

**PASS**。通过 100 kHz 纯 MEM-AP 采集 13 个样本，跨度 **60.096 s**。期间没有停止、单步或复位命令。每个样本的 DHCSR 均显示目标未停机、未锁死。状态页保持 page 4，LCD 与 Monitor 刷新计数持续前进。

| 指标 | 起点 | 终点 | 增量 |
|---|---:|---:|---:|
| Kernel Tick | 13,775 | 19,785 | 6,010（100.006 Hz） |
| TIM2 ms | 137,772 | 197,872 | 60,100（1,000.059 Hz） |
| KEY | 7 | 8 | 1 |
| Controller | 13,771 | 19,781 | 6,010 |
| LCD | 930 | 1,141 | 211 |
| Monitor | 222 | 312 | 90 |
| LCD 页面刷新 | 929 | 1,139 | 210 |
| Monitor 刷新 | 378 | 588 | 210 |
| 短按 / 长按 | 6 / 1 | 6 / 2 | 0 / 1 |
| SPI/DMA 传输错误 / LL 恢复 | 0 / 0 | 0 / 0 | 0 / 0 |

Idle 在启动探针和自由运行 TCB 样本中均实际出现。KEY、命令及遥测 Queue 溢出为 0。长按入队、KEY、Controller、LCD 分别各消费 1 个事件。PC13 ODR 从 0 切换到 0x2000，页号保持 4。用户回复「正常」。

五个活动栈 guard 持续为 0xDEADC0DE。窗口后只读估算栈填充使用量为 KEY 176 B、Controller 328 B、LCD 696 B、Monitor 248 B、Idle 72 B。MSP 边界在启动时验证，MSP 高水位 **NOT MEASURED**。所有窗口样本 CFSR、HFSR 和断言行号为 0，无已观察故障或断言。

早期显示观察的 950 kHz SWD 会话发生读取中断，保留原始日志。100 kHz 重连后才建立上述 60 秒独立窗口。窗口内未发生相同读取错误。原始样本见 `stability60-samples.jsonl`，起止及增量见 `stability60-result.json`。
