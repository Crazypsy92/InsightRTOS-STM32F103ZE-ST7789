# Stage 9.4-F 等待取消与资源清理规约

夹具使用四个静态任务，Low / Medium / High / Observer 的优先级分别为 2 / 4 / 5 / 6。Observer 按 Tick 门控工作线程，每次取消前后观察公共 Task Snapshot、真实资源链接和 Trace。Low 与 Medium 释放自己的 Mutex 后分别执行 Sender 与 Receiver。Queue 测试使用公开 WAIT_FOREVER 语义，由 Observer 在下一次取得 CPU 后取消；Observer 的采集端另有超时。

| 路径 | 取消前 | 取消提交 | 任务恢复及资源处置 |
| --- | --- | --- | --- |
| Mutex High | High 等 B、Medium 等 A，Medium 和 Low 被提升至 5 | STOP 取消 High 的 B 等待。A/B owner 仍为 Low/Medium，Low 与 Medium 降至 4 | High 的 lock 返回 CANCELLED，STOP 为 true，随后自行退出 |
| Mutex Medium | Medium 阻塞，仍持有 B；Low 优先级 4 | cancel_wait 取消 A 等待，Low 回到 2；A/B owner 保持 | Medium 的 lock 返回 CANCELLED，持有者自行释放 B；Low 收到 gate 后自行释放 A |
| Queue Send | 容量 1，哨兵已入队、静态源被引用 | STOP 摘除发送等待节点和源指针，保留 count/head/tail/存储 | Send 返回 CANCELLED 后才改写源；Observer 消费原哨兵一次 |
| Queue Receive | Queue 为空，静态目标持有哨兵 | cancel_wait 摘除接收等待节点和目标指针，目标保持原值 | Receive 返回 CANCELLED；后续可以写入并读回探针值一次 |

Trace 按 generation 核对 BLOCK、CANCELLED 和实际调度的顺序。板级采集将状态帧读两遍并对比，同时检查故障寄存器、断言和四个用户栈及 Idle 栈 guard。
