# Stage 9.4-H SPI3 全双工 DMA 实机验收

**STATUS: PASS**。STM32F103ZET6 通过 PB5→PB4 实体回环，21 笔正常事务逐字节相同。PD11 采样保持高电平。SPI3 寄存器显示 Mode 0，APB1 36 MHz、BR=0，推导 SCK 18 MHz，实际波形 **NOT MEASURED**。

| 用例 | 实测次数 | 路径 | 一致的 TX / RX CRC32 | 不匹配字节 |
| --- | ---: | --- | --- | ---: |
| 15 B · 递增 | 1 | 轮询 | A06C675E | 0 |
| 257 B · 伪随机 | 1 | DMA | 60A2C4E6 | 0 |
| 16 B · 递增 | 3 | DMA | CECEE288 | 0 |
| 16 B · 55/AA 交替 | 3 | DMA | AD9261BE | 0 |
| 16 B · 固定种子伪随机 | 3 | DMA | 1A683BFF | 0 |
| 2,048 B · 递增 | 3 | DMA | 9F5EDD58 | 0 |
| 2,048 B · 55/AA 交替 | 3 | DMA | 3140E0E4 | 0 |
| 2,048 B · 固定种子伪随机 | 3 | DMA | 988A1EE9 | 0 |
| 恢复后 16 B · 伪随机 | 1 | DMA | 1A683BFF | 0 |

每个正常 DMA 事务先观测到 RX 请求和 Ch1 已启用、TX 请求和 Ch2 尚未启用；两路 CNDTR 均从请求长度降为 0，清除前两个 TCIF 均置位且两个 TEIF 均为 0。排空时 TXE=1、BSY=0、RXNE=0、OVR=0。同步返回后两路 DMA 请求、通道和标志均清零，release 后 READY、owner=NULL、depth=0。TX/RX 的 guard 和短包缓冲尾部哨兵完整。

## RX 停止与恢复

- 2,048 B 故障事务中，关闭 RX 前 TX/RX CNDTR 分别为 1,916/1,918，两通道均运行。仅关闭 RX Ch1 EN 后，RX CNDTR 固定为 1,913。
- TX 继续至 CNDTR 0，出现 TX TCIF，RX TCIF 缺失。等待结束时 SPI SR=0x43，RXNE/OVR 均置位。实际等待 1,522,148 DWT 周期，预算 1,521,920 周期，返回 TIMEOUT (-2)。
- RX 首个未写入位置为索引 135，其后 1,913 B 与原 RX 哨兵精确一致，主机独立重建已接收前缀与未写入尾部 CRC，结果吻合。
- LL 恢复后两路请求和通道、DMA 标志、RXNE/OVR 清零，TXE=1、BSY=0。release 归还占用，保留 ERROR 与 -2。下一次 acquire 自动恢复，计数 0→1→2；后续 16 B 回环成功，总线 READY、错误清零。

## 软件、资源和正式固件恢复

- Host 478/478，加上 12 例 C 缓冲/CRC 独立校验、实机帧离线回放和三个损坏结果拒绝用例。ARM Debug/O2 及正式构建通过，正式 ELF 无夹具、注入或钩子符号。
- Debug/O2 Flash 分别 41,564 B、26,708 B，静态 SRAM 均为 59,480 B（含 4 KiB MSP 和 32 KiB 用户栈预留），剩余 6,056 B。TX/RX 各 2,048 B，连同 guard 共 4,112 B。夹具及 Idle 栈 guard 完整，实机观察无故障或断言。
- 仅擦写页 0–30。冻结正式 O2 已恢复，授权区 63,488 B 逐字节一致，Option Bytes 保持。Monitor 复位后快照序号 4→8，Tick 209→549。
- **NOT TESTED**：Debug 夹具实机、物理 TEIF、永久 BSY、无法清除的物理 OVR。

[全双工规约](spi_duplex_contract.md) · [实机逐笔证据](logs/SpiDuplex-fixture.json) · [正式镜像读回](logs/IntegratedProd-readback.json) · [Monitor 推进](logs/IntegratedProd-diag.json)
