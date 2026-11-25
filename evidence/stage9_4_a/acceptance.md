# Stage 9.4-A 实机验收

**PASS**。基线 `21477bb20eb9bb74ddfeb74af8337ec58ec78b95`。在 STM32F103ZET6 上实测四任务、Idle、真实 TIM2 ISR 到队列、LCD 和 ACK，综合快照内的 Trace/Explain、RT 和 Mutex 图均能读取并分别核验。原 LCD 绘制、SPI3/DMA、调度器和内核 ABI 保持。现场未进行 LCD 目视复测。

## 工况

| 工况 | 实际窗口 | ISR 尝试 | 入队成功 | ACK 成功 / 失败 | Queue FULL / retry / 待 ACK |
|---|---:|---:|---:|---:|---:|
| 自然运行 | 60.818 s | 0 | 0 | 0 / 0 | 0 / 0 / 0 |
| 10 Hz 有界流 | 66.047 s | 600 | 600 | 600 / 0 | 0 / 0 / 0 |
| 单轮真实 ISR 突发 | 13.581 s | 34 | 33 | 33 / 0 | 1 / 0 / 0 |

压力流以 TIM2 100 ms 节拍产生 600 次，窗口余下时间用于结算并等 Monitor 发布。突发实测 33 次成功、1 个 FULL，与当时等待接收的 KEY 直接接收一笔加上 32 槽队列的行为相符。该丢弃有真实返回码与账本计数。Controller 命令队列 retry 本轮未触发。压力流最后识别/已确认 ID 均为 600，末笔有效 ACK 延迟 73 ms。突发最后识别 ID 634，最后确认 ID 633，其有效 ACK 延迟 2,334 ms。26 笔在途事件在 8 项历史中被覆盖，原 pending 账本仍匹配和结算了这 26 笔 ACK；这部分延迟无历史配对。三阶段均无重复或拒绝 ACK。

三阶段 Trace 均为 128 项窗口，最多读取 8 条。常态、压力流、突发结束的累计覆盖分别为 53,010、117,155、128,201。每个结束帧均可解释一条真实上下文切换，所选记录的两端任务代均有效。RT CLOCK flags 有效，四任务和 Idle 使用原优先级和 generation；常态无手动按键，KEY 保持原等待行为。图分析各阶段都为 4 节点、0 Mutex 边，结论是 NO_DEADLOCK_OBSERVED。

600 次压力流和 33 个突发入队各完成一个 Controller 单次发送 Job，共 633 个；无 miss、overrun、abort、重复或无效 Job 结算。最大测到的 Job DWT 驻留为 2,609 周期，业务端到端延迟另用真实 ACK TIM2 时间戳。原业务 LED/LCD 的现场可见效果仅保留为未目视复测。

## 回归与硬件

- Host 应用与历史 RT 各 9/9，既有 77+53 项 Job/DWT 检查通过，新增 ACK 时钟回绕、迟到/重复、覆盖、失败和时间戳不匹配断言通过。没有修改生产内核逻辑，本轮未重复运行 360 项内核历史用例。
- 生产与压力 O2 都构建、独立解析 ELF/BIN/HEX、检查软浮点/TCB 72 B/4 KiB MSP/静态栈/heap 0。压力 53,760 B，生产 53,676 B；两次只擦写页 0–30，原镜像比对、FF 空白/尾部、独立读回和 Option Bytes 对照通过。
- 实测样本中四个用户栈与 Idle guard 完整，未发现 MCU fault、断言或 LCD/SPI/DMA 传输错误。结束后已恢复 **DISPLAY_RTOS_O2_Integrated** 生产配置，复位后检查 Monitor 的发布序号推进，镜像不含压力 mailbox。
- 保留 B2a Flash 超目标 176 B、B2b 参考周期未达及 ON/OFF Job 负载差异、B2c 三种异常嵌套未覆盖等限制。本轮未测物理 IRQ 时延、实际极限栈深度或 WCET，也未注入实际死锁。

[集成协议与样例](integration_contract.md) · [三工况离线摘要](logs/Integrated-summary.json) · [正式镜像读回](logs/IntegratedProd-readback.json)
