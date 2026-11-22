# SPI3 DMA 时钟与等待预算

SPI3 走 APB1（36 MHz）。ST7789 请求 18 MHz，实际 BR=/2。SPI2 同属 APB1，SPI1 走 72 MHz APB2。驱动使用已配置 CR1.BR、板级 PCLK 和由时钟初始化维护的 SystemCoreClock（HCLK）。TIM2 的 72 MHz 定时器输入、1 ms 计数和 RTOS 的 100 Hz SysTick 均不参与 SPI 超时。裸机 Delay_Init 和 RTOS Timebase_Init 都先使能 DWT CYCCNT。SPI 等待使用该时钟和无符号差值，调度期间经过的 CPU 周期计入预算。

每次 API 调用按 65,535 B 切分，最初总长度小于 16 B 时轮询，其余走 DMA。大传输余下 1 B 仍用 DMA，offset 同时推进 TX/RX 指针且任何 CNDTR 写入均为 1–65,535。需遵守传入缓冲区至少覆盖 len B 的既有接口契约。LCD port 在整次 acquire/transfer/release 持有片选，DMA 分块不拉高 CS。TX-only 只配 TX，双工先配并开启 RX。

```text
divider      = 2 << CR1.BR
wire_cycles  = ceil(chunk * 8 * divider * HCLK / PCLK)
budget       = wire_cycles + ceil(wire_cycles / 4) + ceil(HCLK * 20 / 1000)
```

64 位中间量避免长块和慢速配置溢出。无效时钟或预算超过半个 32 位计数周期时，在启动 DMA 前返回参数错误。以 72 MHz 为例，启用等待期间的计数器整圈约 59.65 s，支持跨一次边界的正常比较；线程长时间得不到 CPU 或调试器停止期间的墙钟无法由该循环约束。

| 实际 SCK | 长度 | 理想线时 | 共用 DMA 预算 |
|---:|---:|---:|---:|
| 18 MHz | 255 B | 0.113 ms | 20.142 ms |
| 18 MHz | 65,535 B | 29.127 ms | 56.408 ms |
| 9 MHz（请求 17 MHz） | 65,535 B | 58.253 ms | 92.817 ms |
| 140,625 Hz（BR=/256） | 65,535 B | 3,728.213 ms | 4,680.267 ms |

25% 为 DMA/APB 服务间隙余量，额外 20 ms 覆盖启动与抢占观测（在当前 RTOS 为两个 tick 周期的量级）。这是明确的工程预算，尚无实机最坏延迟保证。每个块在开启通道前只采一次起点。单次 DMA_ISR 寄存器快照先查所有参与通道的 TEIF，再查所需 TCIF，最后判断仍未完成的等待是否超时。采样时钟先于读状态，所以因抢占晚回来时，已锁存完成的事务仍可成功；缺标志则超时。软件轮询无法判定锁存发生在截止点之前还是之后。

TC 后依次等待 TXE=1 和 BSY=0。二者各有 500,000 周期（当前约 6.944 ms）的独立上限。轮询路径的 TXE、RXNE 和末尾 BSY 沿用相同上限。恢复还有单独的 1,000,000 周期空闲预算，约 13.889 ms。无高优先级饥饿情况下，65,535 B 在 18 MHz 下单块失败预算可按 56.408 + 6.944 + 6.944 + 13.889 ms 保守估计，另有配置和函数开销，多块按各块预算累加。驱动同步等待，无 DMA 完成 ISR、无 RTOS API、无屏蔽中断、无主动 yield；高优先级任务可抢占，低优先级任务需等待更高任务阻塞或退出当前调用。

硬件语义对照 ST RM0008 的 DMA 通道 CNDTR/IFCR 及 SPI DMA 流程（TC 后 TXE、BSY）和状态标志。 [STM32F1 RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f103xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
