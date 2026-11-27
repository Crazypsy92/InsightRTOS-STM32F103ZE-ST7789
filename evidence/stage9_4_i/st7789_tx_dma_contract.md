# ST7789 实际面板链路

诊断沿用现有 LCD Task（slot 2，优先级 2）和生产 ST7789 初始化、绘图及总线 API。Controller 仍通过原命令和 ACK 队列驱动刷新。诊断从 LCD Task 画纯色、四象限、横竖条、边框和十字线，间隔更新 ASCII 帧号。结束后要求原 RTOS 信息页完整重绘，恢复普通调度与显示流程。

PD11 是低有效 CS。Port 先 acquire，再拉低 CS，结束 release 成功且 depth 归零后拉高。PD12 低为命令、高为数据。1 字节命令走轮询；低于 16 B 的数据也走轮询。真实 480 B 像素行走 SPI3 TX-only DMA，DMA2 Ch2 的 CMAR 指向现有静态行缓冲，CNDTR 从 480 降至 0。Ch1 和 RX DMA 请求保持关闭。

DMA 观测点在启动前、TC 等待返回后、TXE/BSY 排空后与写返回。TX-only 传输可短暂产生 RXNE/OVR；在 CS 仍有效时录得排空状态，随后通过既有收尾关闭请求和通道、清标志并清 RXNE/OVR。写返回仍由 LCD 持有总线，Port 释放后应为 READY、owner=NULL、depth=0。每次交易累加不变量检查，发布最后一个完整像素行的五阶段快照、最后 release、长度直方图、轮询与 DMA 计数。采集器读取序号一致的快照，测试期间无需停止 MCU。

生产 ELF 无任何诊断或观测符号。功能运行频率 18 MHz 来自 RCC/SPI 配置，逻辑分析仪或示波器波形需要现场测量。
