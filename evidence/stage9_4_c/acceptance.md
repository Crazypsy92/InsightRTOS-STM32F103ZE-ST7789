# Stage 9.4-C ZET6 下游命令队列验收

**PASS**。基线为 `ad5f271d4a5ab5992ba1d13964f9d333d04aec21`。STM32F103ZET6 的独立测试镜像通过真实 TIM2 ISR、KEY、Controller、LCD 和 ACK 业务路径触发了下游命令队列满载。门控在 Tick 62 开始，按预定时点于 Tick 212 自动解除。

| 项目 | 实测结果 |
|---|---:|
| TIM2 注入 / 入口接受 / 入口 FULL | 48 / 48 / 0 |
| lcd_commands 最大深度 | 32/32 |
| 命令 FULL / Controller retry | 223 / 223 |
| 成功提交 / LCD 执行 / 唯一成功 ACK | 48 / 48 / 48 |
| 失败 / 重复 ACK / 拒绝 ACK / Pending | 0 / 0 / 0 / 0 |
| 输入或下游实际丢弃 | 0 |
| 断言 / HardFault 标志 / LCD 传输错误 | 0 / 0 / 0 |

事件 33 的 117 次 FULL 中，首次在 Tick 96，返回码为 RTOS_ERR_FULL（6）、命令队列深度 32、序列请求 33、已提交序列仍为 32。门控解除后，同一事件和序列在 Tick 213 成功提交，Tick 444 完成 LCD 执行，Tick 445 收到唯一 ACK。总计 48 个事件的提交、执行和 ACK 序列连续，采样结束的四条业务队列全空。Monitor 的周期遥测队列在最终采样有 1 项，仍按既有方式消费。

40 项未 ACK 记录从 8 槽历史淘汰，原 pending 账本仍匹配并结算全部 40 个 ACK。末笔事件 48 保留有效 TIM2 延迟 4,390 ms。其余被淘汰项没有补算跨阶段延迟。Controller 单次发送 Job 累计 271（48 次成功与 223 次 FULL 尝试），无 miss、overrun 或 active Job 遗留。Trace 为 128 项窗口，累计覆盖 5,374 条；四任务及 Idle 的 RT 状态有效，4 个 Mutex 图节点零边。五个栈 guard 完整。

## 构建和部署

- 应用 Host 9/9、历史 RT 9/9、Job 77+53 项通过。离线再次校验同一实机帧，并确认伪造 FULL、ACK 序列和提前提交序列的三个改动样本均被拒绝。未改内核，未重复 360 项历史测试。
- O2/Debug 测试配置、正式 O2 集成配置均构建成功。O2 压力镜像 54,812 B；Debug 77,408 B，仅用于软件构建。生产构建的 ELF 未保留门控/压力邮箱符号。板上恢复使用上轮已冻结并验收的 53,676 B 正式文件。
- 擦写前重新核对 UID、512 KiB、页几何、保护与旧固件全区。两次均仅涉及页 0–30。写入后独立读回 SHA、FF 尾部与 Option Bytes 对照通过。恢复后全区与测试前保持逐字节相同，复位后正式综合快照发布周期推进（序号 4 → 8）。
- 未执行 LCD 目视复验、长稳、多轮压力、QEMU 或 Debug 实机。

[恢复规约](backpressure_contract.md) · [逐笔实机证据](logs/Backpressure-fixture.json) · [正式固件读回](logs/IntegratedProd-readback.json)
