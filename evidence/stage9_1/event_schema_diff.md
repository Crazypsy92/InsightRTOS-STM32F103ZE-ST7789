# InsightRTOS Stage 9.1: Event Schema Diff and ABI Analysis

## 一、记录结构体 ABI 检查

`rtos_trace_record_t` 的定义完全保持不变，确保了二进制兼容性：

```c
typedef struct {
    uint32_t timestamp;      /* 内核 tick 计数 (4 字节) */
    uint16_t event_type;     /* 事件类型标识 (2 字节) */
    uint16_t sequence;       /* 缓冲区内循环序列号低 16 位 (2 字节) */
    uint32_t task_ref;       /* 任务引用 (槽位 + generation) (4 字节) */
    uint32_t reason;         /* 事件原因/状态码/commit_class (4 字节) */
    uint32_t payload;        /* 附加载荷 (如 wake_tick / related_task) (4 字节) */
    uint32_t reserved;       /* 保持 24 字节对齐的预留字段 (4 字节) */
} rtos_trace_record_t;
```

**结构体大小断言**：`sizeof(rtos_trace_record_t) == 24`。
128 条记录占用静态内存恰好为 $128 \times 24 = 3072$ 字节。

---

## 二、事件类型比对与扩展

| 事件枚举常量 | 数值 | 引入阶段 | 描述 |
|---|---|---|---|
| `RTOS_TRACE_TASK_CREATE` | 1 | Stage 8.2 | 任务创建成功 |
| `RTOS_TRACE_TASK_TERMINATE` | 2 | Stage 8.2 | 任务自然退出或终止 |
| `RTOS_TRACE_TASK_READY` | 3 | Stage 8.2 | 任务加入就绪队列 |
| `RTOS_TRACE_TASK_BLOCKED` | 4 | Stage 8.2 | 任务进入阻塞等待 |
| `RTOS_TRACE_SCHED_SELECT` | 5 | Stage 8.2 | 调度器选定下一个任务 |
| `RTOS_TRACE_MUTEX_PI_APPLY` | 6 | Stage 8.2 | 互斥量优先级继承生效 |
| `RTOS_TRACE_MUTEX_PI_RESTORE`| 7 | Stage 8.2 | 互斥量优先级继承恢复 |
| `RTOS_TRACE_IPC_COMPLETE` | 8 | Stage 8.2 | IPC 操作完成唤醒 |
| `RTOS_TRACE_IPC_TIMEOUT` | 9 | Stage 8.2 | IPC 等待超时 |
| **`RTOS_TRACE_DELAY_EXPIRED`** | **10** | **Stage 9.1** | **普通延时到期** |
| **`RTOS_TRACE_TASK_YIELD`** | **11** | **Stage 9.1** | **显式主动让出 CPU** |
| **`RTOS_TRACE_CONTEXT_SWITCH`** | **12** | **Stage 9.1** | **C 提交上下文切换与因果归因** |

---

## 三、版本与宏定义

- 原版本宏：`RTOS_TRACE_EVENT_SET_VERSION_1 (1U)`
- 新版本宏：`RTOS_TRACE_EVENT_SET_VERSION_2 (2U)`
- 默认版本：当开启 `INSIGHT_DIAG_EXPLAIN` 时，默认事件集版本定义为 `2U`；当仅开启历史 Trace 时，仍可选用 `1U`。

---

## 四、字段编码与载荷规范

对于新增的 3 类事件，其字段编码如下：

### 1. `RTOS_TRACE_DELAY_EXPIRED` (10)
- `task_ref`: 到期任务句柄。
- `reason`: 0 (保留)。
- `payload`: 到期的目标 tick 计数值。

### 2. `RTOS_TRACE_TASK_YIELD` (11)
- `task_ref`: 发起 yield 的当前任务句柄。
- `reason`: `RTOS_SCHED_REASON_YIELD` (0x02)。
- `payload`: 0 (保留)。

### 3. `RTOS_TRACE_CONTEXT_SWITCH` (12)
- `task_ref`: 切换目标任务句柄 (`next`)。
- `reason`: 高 16 位为调度原因掩码 (`reasons`)，低 16 位为提交分类 (`rtos_commit_class_t`)。
- `payload`: 被切出前一任务句柄 (`previous`)。
