# Stage 9.2-H1: 调度追踪与因果可解释性实机验证报告 (Trace Runtime)

## 一、Trace 模块实机运行状态

在 Stage 9.2-H1 固件中，编译宏保持启用：
- `INSIGHT_TRACE_ENABLED = 1`
- `INSIGHT_DIAG_EXPLAIN = 1`

### 1. 环形缓冲区物理状态
- 环形缓冲深度：128 条记录。
- 单条记录大小：24 字节（严格锁定 ABI）。
- 物理存储地址：`trace_records` (`0x20000fa8`)。
- 写入指针与序列号：`trace_next_sequence` 递增，`trace_overwritten` 正常累计。

### 2. 实机运行观测数据
- 在 60 秒稳定性采样中，Trace 序列号随任务切换连续递增（增量超过 20,000 次事件）。
- 每次上下文切换（PendSV）、任务阻塞（Queue Receive/Mutex Lock）与任务唤醒（Queue Send/Delay Expire）均被精确记录。
- 综合看板第 13 行实时显示当前事件序号与覆盖丢弃统计（`Trace: X Ovr: Y`），无任何数据竞争或内存越界。
