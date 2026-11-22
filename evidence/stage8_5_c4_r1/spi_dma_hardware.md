# SPI3/DMA C3 实机结果

**普通 LCD 路径 PASS**。启动探针在 LCD Task 持有 SPI3 总线（BUSY 且 depth > 0）时捕获 480 B 行传输。DMA2 TX Channel 2 的 CNDTR 为 0、TCIF2 为 1、TEIF2 为 0；SPI3 SR 采样为 0x43，TXE=1、BSY=0。停止 DMA 例程返回时，SPI TX/RX DMA 请求、TX 通道使能和相关 DMA 标志均已清除。启动中的 LCD 初始化和整屏行传输，以及自由运行中的 Monitor 局部刷新均有实机证据。

SPI3 CR1 的 BR=/2。HSE/PLL、HCLK 72 MHz、PCLK1 36 MHz 已从运行配置核对，得到 SPI3 配置值 18 MHz；SCK 波形频率 **NOT MEASURED**。没有读取 SPI DR 或注入 SPI 数据。全双工 SPI 配置下的 TX-only 行传输在清理入口观察到 OVR=1，与 RX 无 DMA 的路径相符。60 秒内初始化、传输、LL 错误和恢复计数均为 0。

实际 SVC、PendSV、四任务、Idle、PSP 及 R4–R11 保存恢复均通过板上探针。60 秒内任务和显示并行推进。另进行了 1,313 次非停机 TCB 读取以尝试捕获恰在 DMA 等待里的 LCD 被高优先级任务抢占，未取得同一事务的相关快照，故严格的单事务时序为 **NOT TESTED**。

冻结固件没有安全的 65,535 B 或跨块诊断入口。真实长块 **NOT TESTED**，也没有注入 DMA 错误、卡 BSY 或 RXNE/OVR 异常。故障后的 acquire/recover 真机路径 **NOT TESTED**，RXNE/OVR 物理故障恢复 **OPEN**。C3 的长块修复保持软件验证通过。
