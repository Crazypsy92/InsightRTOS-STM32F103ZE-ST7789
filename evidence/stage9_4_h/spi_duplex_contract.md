# SPI3 全双工 DMA 数据与恢复契约

ZET6 回环跳线接 PB5→PB4。PD11 先预置为高，夹具只访问生产 SPI 总线接口。SPI3 配置 Mode 0，APB1 36 MHz、BR=0 对应配置 SCK 18 MHz。波形频率需另行测量。

15 B 走轮询，大于等于 16 B 走 DMA。DMA2 Ch1 将 SPI DR 写入 RX，Ch2 从 TX 写入 SPI DR。两路 CNDTR 均等于长度，内存地址递增且外设地址不递增，宽度为字节。先启用 RX 请求及 Ch1，再启用 TX 请求及 Ch2。等待两个 TCIF，随后等 TXE 并确认 BSY 清零，清理请求、通道、DMA 标志；全双工收尾应无 RXNE 或 OVR。[ST RM0008 §25.3.9](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)

对齐且不重叠的 TX/RX 静态缓冲分别为 2,048 B，各有两端 guard。RX 在每次传输前填非零且与对应 TX 字节不同的哨兵，全量逐字节比较，保存首次不符位置、总数及双方 IEEE CRC32。主机脚本另外从模式和固定种子重建字节并核对 CRC。2048 B 例使用 guard、短包同时检查缓冲尾部。

故障只关闭活跃 RX Ch1 的 EN，原 DMA 等待逻辑继续运行。TX 应可完成，缺失 RX TCIF 触发 DWT TIMEOUT；检测尾部停留哨兵及 OVR。LL 失败路径先恢复一次，release 清除占用保留 ERROR。下一次 acquire 自动恢复第二次，随后 16 B 回环成功、回到 READY。测试宏之外无观测钩子或故障注入代码。
