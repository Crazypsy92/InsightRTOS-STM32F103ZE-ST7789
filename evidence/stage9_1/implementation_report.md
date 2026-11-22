# InsightRTOS Stage 9.1: Implementation Report

## 一、模块架构概述

Explainable Scheduling（可解释调度）基于 Stage 8.2 已稳定的 Trace 环形缓冲区机制扩展而来。在完全保持 128 条记录容量和 24 字节 ABI 的前提下，引入了单调递增的 64 位序列号、游标回绕及丢失检测机制，并为调度内核补充了三个缺失的因果事件源点。

```
+-------------------------------------------------------------+
|                      rtos_diag.h API                        |
|  - rtos_diag_trace_info()        - rtos_diag_trace_read()   |
|  - rtos_diag_explain_record()    - rtos_diag_format_exp()   |
+-------------------------------------------------------------+
                              |
+-----------------------------v-------------------------------+
|                      kernel/trace.c                         |
|  - 128 槽 24 B 环形缓冲区 (trace_records[128])              |
|  - 64 位序列号扩展 (trace_next_sequence_high)                |
|  - 独立安全字符串/数值格式化器 (Freestanding, no libc)       |
+-------------------------------------------------------------+
       ^                         ^                      ^
       |                         |                      |
[rtos_yield()]        [delay_process_expired]    [rtos_sched_switch]
 (TASK_YIELD)             (DELAY_EXPIRED)          (CONTEXT_SWITCH)
```

---

## 二、三个新增事件源点实现

### 1. 显式 Yield (`RTOS_TRACE_TASK_YIELD`)
- **位置**：`kernel/scheduler.c` 中的 `rtos_yield()`。
- **语义**：当运行任务主动让出 CPU 时，在更新就绪队列和请求 PendSV 前记录：
  ```c
  RTOS_TRACE_YIELD(rtos_current_task);
  ```
- **记录内容**：
  - `task_ref`：发起 yield 的任务引用。
  - `reason`：`RTOS_SCHED_REASON_YIELD` (0x02)。
  - `related_ref`：无 (0)。
  - `tick`：当前内核时间戳。

### 2. 普通 Delay 到期 (`RTOS_TRACE_DELAY_EXPIRED`)
- **位置**：`kernel/delay.c` 中的 `rtos_delay_process_expired()`。
- **语义**：在系统 tick 中唤醒延时到期任务时，针对纯延时任务记录到期事件：
  ```c
  if (task->wait_reason == RTOS_WAIT_REASON_DELAY) {
      RTOS_TRACE_DELAY_EXPIRED(task);
  }
  ```
- **区分机制**：对于 IPC 阻塞超时唤醒的任务，由各自 IPC 模块记录超时完成状态，此处不重复记录。

### 3. PendSV C Commit (`RTOS_TRACE_CONTEXT_SWITCH`)
- **位置**：`kernel/scheduler.c` 中的 `rtos_sched_switch()`。
- **环境判别**：
  在 `arch/arm/cortex_m3/port.c` 中实现 `rtos_port_in_pendsv()`：
  ```c
  bool rtos_port_in_pendsv(void)
  {
      return (__get_IPSR() == 14U);
  }
  ```
  在 Host Mock 中通过 `mock_delivering` 实现相同逻辑。
- **因果分类与属性映射** (`rtos_commit_class_t`)：
  - `RTOS_COMMIT_IDLE_TRANSITION` (6)：切换涉及 Idle 任务。
  - `RTOS_COMMIT_EXIT` (5)：前一任务已退出 (`previous->state == RTOS_TASK_TERMINATED`)。
  - `RTOS_COMMIT_BLOCK_OR_WAKE_OTHER` (4)：前一任务被阻塞 (`previous->state == RTOS_TASK_BLOCKED`)。
  - `RTOS_COMMIT_HIGHER_PREEMPT` (1)：前一任务仍处于就绪状态，且调度原因为高优先级任务被唤醒就绪 (`RTOS_SCHED_REASON_WAKE`)。
  - `RTOS_COMMIT_YIELD` (2)：前一任务显式发起 yield。
  - `RTOS_COMMIT_SLICE_ROTATION` (3)：同优先级时间片用尽轮转。
  - `RTOS_COMMIT_MIXED` (7)：多种并发原因。

---

## 三、诊断与解释引擎 (`rtos_diag.h`)

### 1. 结构化诊断输出
`rtos_diag_explain_record()` 解析原始 24 字节 trace 记录，填充 `rtos_diag_explain_t`：
- 事件分类 (`kind`)：TASK_LIFECYCLE, TASK_READY, TASK_BLOCK, SCHED_DECISION, CONTEXT_COMMIT, DELAY_EVENT, EXPLICIT_YIELD, IPC_EVENT, MUTEX_INHERIT。
- 主任务与相关任务有效性及失效状态检测（通过比较记录时的 generation 与槽位当前 generation）。
- 提交分类 (`commit_class`) 与因果原因码。

### 2. Freestanding 字符串格式化器
为消除对 Newlib C 库 `snprintf` 的依赖，在 `kernel/trace.c` 中内置了紧凑、有界的安全格式化函数：
- `append_str(char *buf, size_t cap, size_t *pos, const char *s)`
- `append_u32(char *buf, size_t cap, size_t *pos, uint32_t val)`
- `append_hex(char *buf, size_t cap, size_t *pos, uint32_t val)`
不仅避免了 ARM GCC 裸机链接时的多架构重定位冲突，还将格式化代码的 Flash 占用控制在 300 字节以内。
