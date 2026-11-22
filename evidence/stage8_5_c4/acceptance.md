# Stage 8.5-C4 准入结论

**BLOCKED — 当前 Flash 内容与 C2 基线不符。** 芯片和保护状态相符，但在两个独立的 ST-Link 会话中，`[0x08000000, 0x0800E800)` 的 59,392 B 读值逐字节一致，与冻结 C2 在页 0–14 有 29,815 B 不同，首末差异为 `0x08000001` 和 `0x080077FF`。当前读回 SHA256 为 `ba776449afbe93fa463b5efd01fa39e8e223b97d513e954d087e1a706c4ce38e`。也未匹配冻结 C3。C2 历史证据全部通过 SHA256 复核。

| 项目 | 结论 |
|---|---|
| C3 固件包 | **SOFTWARE VERIFIED**。O2 58,804 B，ELF/BIN/HEX/MAP 与冻结清单一致 |
| 实机身份及 Option Bytes | **HARDWARE VERIFIED**。同一 UID，保护状态匹配两次只读核验 |
| 当前固件准入 | **BLOCKED**。两次独立只读捕获确认内容不符 |
| 擦除、写入及写后读回 | **NOT EXECUTED** |
| 五页面、按键、LED、任务、时基、复位、30 分钟 | **NOT TESTED** |
| 实体 SPI3 SCK、栈水位 | **NOT MEASURED** |
| 长块 DMA | **SOFTWARE VERIFIED / HARDWARE NOT TESTED** |
| RXNE/OVR 物理故障恢复 | **OPEN** |
| C3 作为 alpha 实机基线 | **BLOCKED** |

原计划在写入前停止。未复位、停止或写入 MCU，未修改 Option Bytes，未自动回滚。读回覆盖计划页区间，未读取全片。当前目标内容保留待对账，C2 冻结包保留为人工恢复候选。
