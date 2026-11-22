# InsightRTOS Stage 8.5-C4-R1 实机验收

**PASS — C3 O2 已部署，五页面与 60 秒实机验收通过。**

ST-Link `190057000D0000393333574E`，MCU IDCODE `0x10036414`，CPUID `0x411FC231`，UID `31ffd8053031533628791143`，Flash 512 KiB。实际检测到一个 STM32F1 主 Flash bank，256 个 2 KiB 扇区。按本轮授权擦除页 0–255。独立读回全片 524,288 B 为 FF 后，写入既有 O2 目录中的冻结 C3 BIN 58,804 B。写入后独立重连，全片读回符合预期，剩余 465,484 B 为 FF，Option Bytes 在擦除前、擦除后、写入后及最终复位后均一致。没有备份旧程序、重编固件或修改生产源码。

| 项目 | 结论 |
|---|---|
| 冻结固件、主 Flash 擦除、写入及独立读回 | **PASS** |
| 欢迎页、四个原页面及 RTOS Monitor | **PASS**，用户目视反馈与运行计数 |
| KEY1 短按、长按、LED | **PASS**，用户反馈及 FIFO/GPIO 采样 |
| 四任务、Idle、SVC/PendSV、PSP 与栈 guard | **PASS**，实际入口、上下文与 guard 已采集 |
| 60 秒自由运行 | **PASS**，60.096 s，约 100.006 Hz SysTick、1,000.059 Hz TIM2 |
| 常规 LCD SPI/DMA 路径 | **PASS**，实捕 480 B TX 完成与尾部清理，窗口内 SPI/DMA 传输错误和 LL 恢复为 0 |
| 最终板载 RESET | **PASS**，实捕复位状态位、Tick/TIM2 重新增长、欢迎和动画恢复，用户确认 |
| 65535 B / 跨块、错误注入、单事务抢占时序 | **NOT TESTED** |
| 实体 SPI3 SCK、MSP 水位 | **NOT MEASURED** |
| 物理 RXNE/OVR 故障恢复 | **OPEN** |

启动期间的高频 SWD 读取中断已留档，降至 100 kHz 的纯 MEM-AP 采样完成独立自由运行窗口。最后复位的首个样本跨过复位边界，后续时基、栈和用户确认已单独保存。60 秒没有使用周期性暂停。C3 长块修复保留软件验证结论，当前实机验收范围为现有五页面与普通 SPI/DMA 负载。
