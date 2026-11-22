# InsightRTOS Stage 9.1: Cursor, Wrap, and Overwrite Verification

## 一、环形缓冲区覆盖机制

Trace 环形缓冲区维持 128 条记录容量不变。当记录数超过 128 时：
- 最旧的记录被新记录无缝覆盖。
- 内部变量 `trace_overwritten` 递增，记录被丢弃的事件总数。
- 全局 64 位单调序列号持续增加。

### 1. 回绕测试验证 (`test_ring_wrap`)
- **注入数据**：连续写入 150 条记录。
- **状态检查**：
  - `info.capacity == 128`
  - `info.count == 128`
  - `info.overwritten_records == 22`
  - `info.oldest_sequence == 22`
  - `info.next_sequence == 150`
- **读取验证**：
  从 oldest sequence (22) 读取 8 条记录，返回的记录序列号精准对齐为 22 至 29，未发生数据错位。

---

## 二、64 位序列号跨界测试 (`test_64bit_sequence_wrap`)

为了杜绝在长生命周期运行中 32 位整型溢出回绕导致的比较混乱，Stage 9.1 设计了 64 位单调序列：
- 低 32 位：由 `trace_next_sequence` 承载。
- 高 32 位：由 `trace_next_sequence_high` 承载，当低 32 位从 `0xFFFFFFFF` 溢出回绕至 0 时原子自增。

### 1. 跨越边界测试设计
测试用例模拟 `next_sequence = 0xFFFFFFFF`，并触发记录写入：
- 写入前：`next_sequence = 0x00000000FFFFFFFFULL`。
- 写入后：`next_sequence = 0x0000000100000000ULL`。
- 校验：
  - `info.next_sequence == 0x100000000ULL`
  - 读取游标比较逻辑仍然保持严格单调，无符号溢出未导致误判为“过期”。

---

## 三、游标过期与安全保护 (`test_argument_and_context_checks`)

`rtos_diag_trace_read()` 提供了严格的有界与防挂死保护：
1. **过期游标检测**：
   若调用方传入的游标 `cursor < info.oldest_sequence`（即该记录已被覆盖丢失），API 立即返回 `RTOS_ERR_STALE`，并在 batch 元数据中置位 `RTOS_DIAG_FLAG_LOST_EVENTS`。
2. **前瞻游标检测**：
   若传入的游标 `cursor > info.next_sequence`，API 返回 `RTOS_ERR_PARAM`。
3. **空指针与零长度保护**：
   对于非法指针或读取上限为 0，均返回 `RTOS_ERR_PARAM`。
4. **中断临界区保护**：
   读取操作限制每次批处理最多复制指定的有界数量（如 8 条），临界区耗时有界（≤1.2 µs @ 72MHz），绝不长时间关中断。
