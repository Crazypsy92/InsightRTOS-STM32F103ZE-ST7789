# InsightRTOS Stage 9.1: Host Test Results

## 一、Host 测试套件概述

Host 单元与功能验证环境构建配置：
- `CMAKE_BUILD_TYPE=Debug`
- `-DINSIGHT_TRACE_ENABLED=ON`
- `-DINSIGHT_DIAG_EXPLAIN=ON`
- `-DINSIGHT_STAGE9_1_TESTS=ON`

执行命令：
```bash
ctest --test-dir build-host-stage9_1 -V
```

结果统计：
- **总测试数**：349 项
- **通过数**：349 项
- **失败数**：0 项
- **通过率**：**100%**

---

## 二、Stage 9.1 专项 Host 测试详单 (11 项全部通过)

| 测试用例 ID | 目标行为与覆盖点 | 预期原因码 / 提交类 | 实际执行结果 |
|---|---|---|---|
| `stage9_1_host_preemption` | 高优先级任务从唤醒进入就绪触发直接抢占 | `RTOS_COMMIT_HIGHER_PREEMPT` (1) | **PASS** (0.00s) |
| `stage9_1_host_fifo` | 相同优先级任务轮转，就绪队列头尾交替 | `RTOS_COMMIT_SLICE_ROTATION` (3) | **PASS** (0.00s) |
| `stage9_1_host_explicit_yield` | 任务显式调用 `rtos_yield()` 让出 CPU | `RTOS_COMMIT_YIELD` (2), 原因码 0x02 | **PASS** (0.00s) |
| `stage9_1_host_delay_expired` | 任务延时到期，`delay_process_expired` 唤醒并就绪 | `RTOS_TRACE_DELAY_EXPIRED` (10) | **PASS** (0.00s) |
| `stage9_1_host_ipc` | 信号量 Post/Wait 正常唤醒、超时唤醒及等待取消 | `IPC_COMPLETE` / `IPC_TIMEOUT` / 取消唤醒 | **PASS** (0.00s) |
| `stage9_1_host_mutex_pi` | 互斥量优先级继承导致低优先级任务被调度，释放后恢复 | `RTOS_TRACE_MUTEX_PI_APPLY` / `RESTORE` | **PASS** (0.00s) |
| `stage9_1_host_exit` | 任务正常执行结束，进入终止状态由调度器回收 | `RTOS_COMMIT_EXIT` (5) | **PASS** (0.00s) |
| `stage9_1_host_slot_reuse` | 任务槽位被新任务复用，generation 递增，旧引用判定失效 | `is_stale == true`，正确识别失效 | **PASS** (0.00s) |
| `stage9_1_cursor_ring_wrap` | 环形缓冲区写入 150 条记录 (>128)，检验覆盖与回绕 | `overwritten_records == 22`，游标正确前进 | **PASS** (0.00s) |
| `stage9_1_cursor_64bit_wrap` | 64 位单调序列跨越 `UINT32_MAX` 边界 | 跨界单调递增，无符号翻转错误 | **PASS** (0.00s) |
| `stage9_1_cursor_arguments` | 游标过期检测、非法入参防护、中断/上下文检查 | 返回 `RTOS_ERR_STALE` / `RTOS_ERR_PARAM` | **PASS** (0.00s) |

---

## 三、历史功能与测试集回归

Stage 1 至 Stage 8 的所有既有 Host 测试集（338 项）完全通过，证明新增诊断事件与元数据未对 RTOS 内核就绪队列、时间片轮转、互斥量继承算法或 IPC 唤醒逻辑产生任何副作用。
