# Stage 9.4-E Task 生命周期规约

Observer 使用优先级 6，Worker 使用优先级 5。每次运行只创建这两个用户任务，保留内核 Idle。Worker 每代使用同一块 4096 B 静态栈。每次创建先记录 READY；Worker 校验 `self_ref`、RUNNING、Thread 上下文和 PSP，再执行有限场景。Observer 每次结束先记录同代 TERMINATED、队列字段、信号量计数及栈 guard，之后清空夹具保存的只读 TCB 指针，再创建下一代。

| 场景 | 提交顺序 | 预期结果 |
| --- | --- | --- |
| A | Semaphore Take 进入 BLOCKED/PENDING → STOP → CANCELLED/READY | Take 返回 CANCELLED (10)，Thread 读到 sticky STOP 后调用 exit |
| B | Semaphore Take 进入 BLOCKED/PENDING → Give → GRANTED/READY → STOP | Observer 保持 RUNNING 直到两个 API 返回。Take 恢复一次并返回 OK (0)，Worker 读到 STOP 后调用 exit |
| C | 三次自然完成并退出，随后再次在同一栈创建 | 槽不变、generation 每次加一；旧 snapshot 与 STOP 都返回 STALE (9)，新代初始 STOP 为 false |
| D | 纯 Delay 进入 BLOCKED → STOP → 原 deadline | 纯 Delay 保持 BLOCKED，不产生 IPC 完成；在原到期 Tick 恢复并观察 sticky STOP |

任何场景退出后，同代 snapshot 返回 OK 并报告 TERMINATED。同代再次 STOP 返回 STATE (2)。创建可回收任务时，Trace 先标记旧 generation 的 RECLAIM，再标记新 generation 的 CREATE。Observer 根据当前 generation 的快照检查优先级，检查 ready/delay 标志、ready/delay/wait 链、等待对象、Mutex 持有链以及等待缓冲区清空。残余许可用 NO_WAIT Take 验证返回 UNAVAILABLE (5)。

Trace 通过公开接口分批读取，由独立采集器检查 generation、提交顺序、实际调度与生命周期顺序。夹具报告在结束态固定，采集器重复读两遍，保存原始字节和解码结果，并检查断言、Fault 寄存器、halt/lockup 以及两个用户栈和 Idle guard。所有测试等待有明确 Tick 上限。报告记录的是具体受控顺序，覆盖范围限于夹具执行的场景。
