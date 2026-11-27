# SPI3 同步传输与恢复规约

夹具只使用生产 `BSP_SPI3_GetBus`、`spi_bus_acquire`、`spi_bus_transfer` 和 `spi_bus_release`，每笔事务使用 Mode 0、DMA 启用配置和空 RX 缓冲区。PD11 在 SPI3 初始化前预置高。验证使用 PB3/PB4/PB5，记录 PD11 输入电平作为片选信号证据。

15 B 请求进入轮询，16 B 请求进入 DMA。启动 DMA 后，单个请求以 65,535 B 为块上限，DMA 尾块继续走 DMA。因此 65,536 B 为 65,535 + 1，131,070 B 为两个完整块。TX 使用 DMA2 Ch2，RX 请求和 Ch1 保持关闭。每块在通道启动前采集 CMAR/CNDTR，DMA 等待返回后且清除标志前采集 TCIF/TEIF/CNDTR/SR，等 TXE=1、BSY=0 后再采集一次。同步返回后核验 DMA 请求、通道、标志以及 RXNE/OVR 清理。

Flash 源区为 `[0x08010000,0x0802FFFE)`。夹具先做 16 B Flash DMA 探测，只有成功且 TC/CNDTR 证据正确才进入长块用例。[ST RM0008 §13.2](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)

当前 DMA 每块 DWT 预算为线速耗时加 25% 和 20 ms。以 HCLK 72 MHz、SCK 配置推导 18 MHz，预算为每字节 40 周期加 1,440,000 周期。故障场景等 Ch2 已传输至少约 128 B、仍有待传字节时，仅将其 EN 清零；其后由原 dma_wait 等到真实 DWT 超时。第一轮恢复由 LL 失败路径调用，release 归还 owner/depth 并保持 ERROR。下一次 acquire 触发第二轮 recover，短 DMA 成功后 release 回到 READY。寄存器状态在同一线程有界采集，生产配置预处理剔除所有夹具钩子。
