# InsightRTOS Stage8.5-C2 实机验收

仅使用C1 O2冻结包，长度57988B、SHA256 526890ff96594c65533c69e129a1a889ec2368768b4b4fca3a3c7052eac72e35。指定ST-Link 190057000D0000393333574E及ZET6身份/保护状态与B1.3一致。新增页20–28已只读确认全FF，实际29页×2KiB几何通过，逐页擦除0–28，无Mass Erase/解锁/Option Bytes修改。

| 项目 | 最终结果 |
|---|---|
| SOFTWARE PACKAGE | PASS |
| HARDWARE IDENTITY | PASS |
| FLASH ERASE | PASS pages0–28 only |
| FLASH PROGRAMMING | PASS C1 O2 57988B |
| FLASH READBACK | PASS independent reconnect + byte/SHA equal |
| OPTION BYTES | PASS unchanged |
| RTOS STARTUP | PASS hardware |
| LCD DISPLAY | PASS USER CONFIRMED |
| FIVE PAGES | PASS USER CONFIRMED + live counters |
| KEY INPUT | PASS USER CONFIRMED + FIFO counts |
| SPI/DMA | PASS observed UI paths; long-block risk OPEN/NOT TESTED |
| TASK SCHEDULING | PASS four task entries + Idle + real context switching |
| STABILITY | PASS >=30min actual free run |
| NRST RESET | PASS physical RESET + user confirmation |
| SPI3 PHYSICAL SCK | NOT MEASURED |
| MSP HIGH WATERMARK | NOT MEASURED |

独立读回57988B与BIN逐字节及SHA一致，0x0800E284–0x0800E7FF的1404B全FF，Option Bytes操作前/后/最终一致；运行后再次读取Flash仍与冻结BIN一致。仅保存计划范围快照，不伪称512KiB完整备份。

实际连续窗口1805.013秒，SysTick100.00262Hz、TIM21000.02451Hz；全部采样无已观察assert/HardFault/BusFault状态、guard损坏或SPI/DMA错误。LCD五页/欢迎/LED结果为用户确认，代理未自行取得视频/照片。详细计数、栈观测及原始日志见各报告。

首次诊断脚本Thumb指针比较误报已纠正，保留attempt1原始记录；固件、检查目标、内核/驱动均未改动，也未为误报重烧。原长块DMA预算问题仍OPEN，本轮当前UI路径正常不代表所有DMA路径通过。没有CPU使用率、全局切换或HardFault累计计数接口，不伪造数据。

完成后停止，不进入新UI开发、驱动修复或其他工程。

离线C1包验证日志中的HARDWARE NOT EXECUTED是原包验证器的固定范围说明，不表示C2没有执行硬件；C2实机证据以本目录原始记录为准。
