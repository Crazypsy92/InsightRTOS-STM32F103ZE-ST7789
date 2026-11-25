# Stage 9.4-B ZET6 Mutex 实机验收

**PASS**。基线 `e4d1679286a355c3445e4b39ace967e68508f60f`。独立 O2 测试镜像在 STM32F103ZET6 上完成 202 轮场景，耗时 838 Tick（约 8.38 秒），没有断言、HardFault、残留所有权、等待者或残留优先级继承。六个任务栈 guard（四 worker、Observer、Idle）完整。工作区原始字节与 ELF DWARF 解码均保存。

| 场景 | 通过轮数 | 实机证据 |
|---|---:|---|
| 所有权、优先级和同级 FIFO 交接 | 100/100 | Medium 最早等待，授予顺序却为 High → Peer → Medium。High 在仍为 READY 时已成为 owner。 |
| 经典 PI | 100/100 | Low 的有效优先级从 2 升至 5，Medium READY/P4。Trace 与实际执行顺序均为 Low → High → Medium，最后 Low 回到 P2。 |
| 传递性 PI | 1/1 | High → Medium → Low 的两条真实等待边；Low 从 2 升至 4 再到 5，Medium 从 4 升至 5，解链后都回到原优先级。 |
| 有限 ABBA | 1/1 | Tick 809 捕获两个有限边，最早到期 837。Low 在 837 返回超时并解锁，Medium 同 Tick 直接接手，早于自己的期限 868。 |

基础返回码来自实机：首次 lock 与 unlock 为 RTOS_OK；重复 lock 为 RTOS_ERR_STATE（2）；非 owner unlock 为 RTOS_ERR_NOT_OWNER（8）；竞争时 NO_WAIT 为 RTOS_ERR_UNAVAILABLE（5）。测试直接调用公开 Mutex、任务快照、Trace/Explain 和 DL API。没有申请永久 Mutex 等待。有限环的报告是当前循环风险，随后已自动解除。

Host 历史内核构建复跑 360/360、应用 9/9、RT 9/9、Job/DWT 77+53 项通过。ARM Debug/O2 测试镜像与同源码生产 O2 构建通过。未改动内核生产逻辑、SVC/PendSV、优先级实现、栈容量或显示页面。Debug 只构建，实机使用 O2。本轮未重跑 QEMU。

板卡 512 KiB、IDCODE 0x10036414、UID 与前轮一致。两次操作只涉及页 0–30，经旧镜像全区、空白、独立读回及前后 Option Bytes 对照。测试后恢复 **Stage 9.4-A 冻结生产 O2**，其 63,488 B 读回与测试前独立备份完全相同，复位后综合快照发布序号从 4 前进至 8。

[测试设计](test_design.md) · [解码实测](logs/hardware/Mutex-fixture.json) · [原始工作区字节](logs/hardware/Mutex-fixture.raw.hex) · [生产镜像恢复读回](logs/hardware/IntegratedProd-readback.json)
