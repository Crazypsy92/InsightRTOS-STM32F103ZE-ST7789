# Stage 9.4-G SPI3 DMA 实机验收

**STATUS: PASS**。STM32F103ZET6 完成 Flash DMA 源探测、五种长度、真实超时和下一次 acquire 恢复。PD11 在 SPI3 启动前置高，所有记录点均为高。夹具使用 TX-only 调用，DMA2 Ch2 TX，Ch1 RX 和 RX DMA 请求未启用。

| 用例 | 长度 B | 分块 B | 每块 CMAR | 传输耗时 ms | 结果 |
| --- | ---: | --- | --- | ---: | --- |
| Flash 路径探测 | 16 | 16 | 0x08010000 | 0.032 | PASS |
| 轮询阈值以下 | 15 | 轮询 | — | 0.028 | PASS |
| DMA 下限 | 16 | 16 | 0x08010000 | 0.032 | PASS |
| 最大单块 | 65,535 | 65,535 | 0x08010000 | 29.151 | PASS |
| 跨块尾 1 B | 65,536 | 65,535 + 1 | 0x08010000, 0x0801FFFF | 29.176 | PASS |
| 最大双块 | 131,070 | 65,535 + 65,535 | 0x08010000, 0x0801FFFF | 58.300 | PASS |

每个正常 DMA 块的 CNDTR 初值等于块长，终值为 0，清标志前 TX TCIF=1、TEIF=0，排空时 TXE=1、BSY=0。排空快照记录到自然 RXNE/OVR，返回后两者均已清除，DMA 请求、通道 EN 和相关标志均为零。配置寄存器给出 PLL SYSCLK、HCLK 72 MHz、APB1 36 MHz、SPI BR=0（除以 2）、Mode 0。由配置推导 SCK 18 MHz，实际波形 **NOT MEASURED**。

## DMA 停止与恢复

在 8,192 B 事务中，TX 通道仍在运行时采样到 CNDTR=8,062、DMA ISR=0x00、SPI SR=0xC3。仅关闭 TX EN，CNDTR 停在 8,058，TCIF/TEIF 保持为 0。DWT 等待实测 1,767,926 周期，预算 1,767,680 周期，返回 TIMEOUT (-2)。LL 首次 recover 后寄存器清理通过，release 后 ERROR 与错误码保留，owner=NULL、depth=0。下一次 acquire 再次 recover，计数 0→1→2，随后 16 B DMA 成功，release 后 READY 和错误码 0。

## 软件与烧录边界

- Host 478/478。Debug/O2 与正式 O2 均构建通过，正式 ELF 无夹具或观测钩子符号。两套夹具镜像通过 ELF/BIN/HEX 一致性检查。O2 Flash 26,268 B，Debug Flash 40,904 B，静态 SRAM 均为 48,312 B。夹具与 Idle 的栈 guard 完整，故障和断言寄存器为 0。
- 首次烧录后 SWD 大块读取中断，流程在启动夹具前停止。用户重新接线后，100 kHz SWD 独立读回匹配才继续。写入限页 0–30，夹具和恢复固件均通过独立读回、FF 尾部和 Option Bytes 核验。
- 恢复后的首次运行态读回在 0x08027B34 得到一个异常零字。停核独立复读后，完整只读源区 131,070 B 与操作前逐字节一致，另用 MEM-AP 单独核对该字为 0xFFFFFFFF。源区内容全部为 FF。SHA256 `d5893ddd7278de70e9c508cd8e445887544162dcabada4cc5d913be883f8219d`。
- 冻结正式 O2 已恢复，63,488 B 授权范围复核与测试前逐字节一致。Monitor 快照序号 4→8，Tick 209→549。
- 物理 TEIF、永久 BSY 和不可清除的物理 OVR 为 **NOT TESTED**。正常 TX-only 自然产生的 RXNE/OVR 已实际观察和清理。

[驱动契约](spi_dma_contract.md) · [实机逐块证据](logs/SpiDma-fixture.json) · [恢复读回](logs/IntegratedProd-readback.json) · [复位后 Monitor](logs/IntegratedProd-diag.json)
