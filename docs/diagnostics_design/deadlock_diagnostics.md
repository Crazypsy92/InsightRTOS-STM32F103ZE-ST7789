# Mutex 等待图与死锁诊断

第一版只分析已提交的 Mutex 等待。当前 PI 求解由 priority_graph.c 从 rtos_task_snapshot_locked 得到活动任务，取 BLOCKED + WAIT_MUTEX 的 owner 边并迭代传播最大有效优先级。它允许循环拓扑内的有界收敛，释放或取消后从 base 重新求值。诊断复用同一权威输入，不调用 PI 求解，也不把优先级继承结果当作循环判据。[PI 求解](../../kernel/priority_graph.c) · [Mutex](../../kernel/mutex.c) · [等待完成](../../kernel/wait.c)

## 快照

- 最多 8 个已发布且未终止的用户节点，固定按槽索引，记录 generation、状态、base/effective priority。Idle、UNUSED 保留槽、TERMINATED 不入图。
- BLOCKED、WAIT_MUTEX、PENDING 同时成立才产生等待边 waiter→当前 owner。每节点至多一条出边。记录 delay_queued、wake_tick，以它们识别有限等待，避免用残留 wake_tick 判定。目标 owner 可以 READY/RUNNING/BLOCKED；owner 的代数从同批节点读取。
- 在一个 PRIMASK 区间捕获 kernel tick、节点、owner 对应槽和资源身份。用已有任务池迭代和 generation 验证；按 slot 校验 owner 指针只对照同批活动节点，未知、缺 owner 或自边作为图不变量异常。内核内存已损坏的情况超出此普通诊断契约。
- 资源键暂存为 uintptr_t，仅在调用方工作区内部使用。解锁后按其相等关系分配当前快照内 Mutex 序号，随后丢弃原键。结果不导出地址或可解引用的 owner/Mutex 指针。抓取边时不遍历 owned_mutexes 链表，也无 Mutex 注册表。

## 图搜索与判定

对每个未访问节点，在线程中迭代沿 owner 槽走，使用未见、当前路径、已完成三色和最多 8 项的路径。回到当前路径上的节点即一条环；旋转到最小槽开始，去重。每节点和边至多进入路径一次，搜索 O(N+E)，N≤8、E≤8，无递归。结果包含多个互不交叠循环、参与引用和资源序号，可另外指明链入环的等待者，但链入者不列为环成员。结果按捕获时间解释，不为后续操作授权。

| 观测 | 对外判定 |
|---|---|
| 等待链无循环 | 普通竞争。带期限的单边标成有限等待 |
| 至少一条有限边的环 | 当前循环风险，可由相应超时自动解除；等待到期还需内核处理。记录快照时最早 due 的有限边，不预测最终持有者 |
| 全部无限等待的环 | 当前死锁风险，无自动超时边；同代外部取消、协作及后续业务仍可能解环 |
| 自边、owner 无效、图不变量不符 | 快照异常。保留有效输入和异常位置，不输出“已确证死锁” |

30 多天等长期场景的优先排序采用已有模 2^32 期限语义和当前捕获 tick，半范围用于相对比较。到期但未处理的有限边仍为有限。由于在锁下捕获，不会看到 handoff 中新 owner 仍处于旧等待边的暂态。取消或超时移除 waiter，owner 保持；grant 原子转给选中的新 owner，剩余 waiters 据新 owner 重连。出边也会随任务取消、释放或退出消失。现有 Mutex 不可递归，同 owner 复锁返回 RTOS_ERR_STATE，不生成自边。NO_WAIT 错误同样不生成边。任务退出前必须释放 owned mutexes，诊断不改此约束。回收后新 generation 不会延续旧边。

第一版不增加 Mutex 写路径 Hook。唯一内核扩展是线程触发的只读图抓取入口及共享身份读取支持。Queue/Semaphore、IPC 和外设联合环不在此次图模型内。图有环只报告，禁止自动取消、强拆锁、重试或调整优先级。
