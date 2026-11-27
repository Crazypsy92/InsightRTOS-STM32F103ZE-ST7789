# Stage 9.4-F Mutex 与 Queue 实机验收

**STATUS: PASS**。STM32F103ZET6 三个场景均通过，夹具从 Tick 0 执行到 8。原始夹具帧重复读取一致。四个用户栈及 Idle 的 guard 完整，CFSR、HFSR 和断言记录均为 0。

| 场景 | 实机证据 |
| --- | --- |
| A · Mutex 等待取消及 PI 撤销 | Tick 3 实测 High→Medium→Low，Low/Medium/High 有效优先级为 5/5/5。High 的 STOP 返回 OK (0)，仅剩 Medium→Low，优先级 4/4/5。Medium 的 cancel_wait 返回 OK，等待图归零，优先级 2/4/5。两个锁等待均在 Thread 恢复并返回 CANCELLED (10)，High 读到 sticky STOP。取消期间 A/B owner 始终为 Low/Medium。两位持有者各自 unlock 返回 OK；Tick 4 两锁的 owner、wait_head、owner_next 均为空。 |
| B · Queue Send 被 STOP 取消 | Tick 5 实测 BLOCKED/PENDING，待发源地址为 0x20000058。STOP 返回 OK，READY 时等待头和源指针均清零，count/head/tail 与队列内哨兵保持不变。Tick 6 Sender 返回 CANCELLED 并观察到 sticky STOP；源由 0x3210ABBA 改为 0x6543ABBA，仍只读出原哨兵 0x1234ABCD 一次。再次读返回 UNAVAILABLE (5)。 |
| C · Queue Receive 被 cancel_wait 取消 | Tick 7 实测 BLOCKED/PENDING，接收目标地址为 0x20000054。cancel_wait 返回 OK，READY 时等待头和目标指针均清零，count 保持 0。Tick 8 Receiver 返回 CANCELLED，输出保持 0xDEAD1978。随后 0x77AABBFF 正常发送、读回一次，再读返回 UNAVAILABLE (5)。 |

两条 Queue 路径均记录 BLOCKED→READY→RUNNING→TERMINATED，退出后无遗留等待指针或链表成员。Trace 以任务 generation 校验 BLOCK、CANCELLED、实际调度以及 STOP 提交。

## 软件和恢复

- Host 478/478，其中 140 项 Stage 7 生命周期与 IPC。ARM Debug/O2 夹具和常规 O2 构建通过，正式 O2 无夹具符号。生产内核、TCB、上下文及 UI 无改动。
- O2 占 Flash 35,200 B，Debug 占 Flash 49,040 B。静态 SRAM 均为 51,216 B，含 4 KiB MSP 和 8×4 KiB 用户栈，总余量 14,320 B。ELF/BIN/HEX、向量及加载段检查通过。Debug 仅做软件验证。
- 硬件身份、Option Bytes、保护状态和旧镜像前检通过。擦写仅在页 0–30；夹具和正式固件均完成独立读回、FF 尾部和 Option Bytes 检查。
- 已恢复冻结正式 O2。63,488 B 和运行夹具前逐字节一致，SHA256 `2f6cf7b5270afb5079925e4783d1f29d8ef1c7f80cdfc5dbec96d4ba3a28b1be`。复位后 Monitor 快照序号 4→8，Tick 203→536。

[取消规约](ipc_cancel_contract.md) · [实机快照和 Trace](logs/IpcCancel-fixture.json) · [正式固件恢复](logs/IntegratedProd-readback.json) · [Monitor 推进](logs/IntegratedProd-diag.json)
