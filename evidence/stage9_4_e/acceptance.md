# Stage 9.4-E 任务生命周期验收

**STATUS: PASS**。在 STM32F103ZET6 上执行 A–D，六代 Worker 全部通过。夹具从 Tick 0 运行到 16。结束态报告两次独立读取一致，PSP 在 Worker 的静态栈内，IPSR=0、CONTROL=2，Observer、Worker、Idle 的 guard 完整，CFSR、HFSR 和断言行均为 0。

| 场景 | 实测结果 |
| --- | --- |
| A · STOP 取消 Semaphore 等待 | 槽 1 / 代 1。Tick 0 进入 BLOCKED/PENDING。Tick 1 STOP 返回 OK (0)，Trace 16→17 提交 STOP→CANCELLED。Worker 恢复一次，Take 返回 CANCELLED (10)，读到 STOP 后自行退出。 |
| B · 保留已授予许可 | 槽 1 / 代 2。Tick 2 进入 BLOCKED。Tick 3 Give 与 STOP 均返回 OK；Trace 44→46→49 对应 GRANTED→STOP→实际调度。Observer 未让位前状态始终为 GRANTED。Worker 的 Take 返回 OK 一次，随后读到 STOP 并退出。 |
| C · 同栈同槽复用 | 槽 1 / 代 3、4、5，分别在 Tick 4、5、6 自行退出。各轮记录 READY→RUNNING→TERMINATED，栈地址均为 0x20003788。随后创建均递增 generation，旧快照和旧 STOP 返回 STALE (9)。Trace 先记录旧代 RECLAIM，再记录新代 CREATE。 |
| D · STOP 时保留纯 Delay | 槽 1 / 代 6。Tick 7 调用 Delay，Tick 8 STOP 返回 OK。仍保持 BLOCKED，在原截止 Tick 15 恢复，看到 STOP 后自行退出。Trace 无 IPC 完成。 |

六代均确认同代 TERMINATED 快照有效，终止后的 STOP 返回 STATE (2)，ready/delay/wait 链和等待缓冲区均已清空。Semaphore 的 count 与 wait_head 全部归零，NO_WAIT Take 返回 UNAVAILABLE (5)。

## 软件与正式固件恢复

- Host **478/478**，含 Stage 7.1–7.5 生命周期和 IPC **140 项**。ARM Debug、O2 及普通正式 O2 构建通过，正式 O2 无生命周期夹具符号。公共内核、TCB 和上下文汇编无改动。
- O2 Flash 28,376 B，Debug Flash 41,852 B，两种配置静态 SRAM 46,984 B（含 4 KiB MSP），剩余 18,552 B。两套夹具及冻结正式镜像通过 ELF、BIN、HEX、向量和加载段审计。
- ST-Link 重新枚举后完成身份及保护核验。第一次 Flash 前检读回不一致，流程在擦除前停止。独立复读及下一次正式前检均与历史冻结区间一致，随后仅擦写页 0–30。两次烧录均经独立读回、FF 尾部和 Option Bytes 检查。
- 冻结正式 O2 已恢复。全部 63,488 B 读回与烧录前逐字节一致，SHA256 为 `2f6cf7b5270afb5079925e4783d1f29d8ef1c7f80cdfc5dbec96d4ba3a28b1be`。Monitor 综合快照序号 4→8，Tick 204→536。
- Debug 仅执行软件构建。真实硬件验证覆盖上述受控顺序。

[生命周期规约](lifecycle_contract.md) · [板上数据与 Trace](logs/Lifecycle-fixture.json) · [正式固件恢复](logs/IntegratedProd-readback.json) · [Monitor 推进](logs/IntegratedProd-diag.json)
