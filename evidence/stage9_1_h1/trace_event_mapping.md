# Stage 9.1-H1 Trace 事件到 LCD 显示映射说明 (Trace Event Mapping)

## 一、24 字节记录二进制结构 (Trace Record Layout)

Trace 环形缓冲区中每条记录占用 24 字节，遵循严格对齐的 ABI 规范：

```text
+-----------------------+-----------------------+  Byte Offset
| sequence (uint32_t)   | tick (uint32_t)       |  0x00 - 0x07
+-----------------------+-----------------------+
| generation (uint32_t) | arg0 (uint32_t)       |  0x08 - 0x0F
+-----------------------+-----------------------+
| arg1 (uint32_t)       | slot (uint16_t)       |  0x10 - 0x15
+-----------------------+-----------------------+
| event_type (uint16_t) | (4-byte aligned)      |  0x16 - 0x17
+-----------------------+-----------------------+
```

---

## 二、字段解包与解释结构体映射 (Unpack & Explain Mapping)

### 1. `RTOS_TRACE_CONTEXT_SWITCH` (事件类型 12)

当调度器通过 `rtos_sched_switch()` 进行任务切换并触发 PendSV C 移交时，记录解包公式如下：

- **切入任务（Target Task）**：
  - `exp.primary_slot = record->slot`
  - `exp.primary_generation = record->generation`
- **切出任务（Previous Task）**：
  - `exp.related_slot = (uint16_t)(record->arg1 & 0xFFFFU)`
  - `exp.related_generation = record->arg0`
- **优先级与分类位图**：
  - `exp.previous_priority = (uint8_t)((record->arg1 >> 16) & 0x1FU)`
  - `exp.next_priority = (uint8_t)((record->arg1 >> 21) & 0x1FU)`
  - `exp.commit_class = (rtos_commit_class_t)((record->arg1 >> 26) & 0x0FU)`
- **原因码与辅助参数**：
  - `exp.reason_flags = record->arg1`

---

## 三、代次有效性校验与陈旧标识 (Stale Generation Detection)

由于任务可能会被销毁（`rtos_task_exit` / `rtos_task_terminate`）并在相同的 TCB 槽位重建，如果不对代次（Generation）进行比对，诊断界面可能会用新任务的名称错误解释几分钟前的历史事件。

**判陈旧逻辑 (`is_stale`)**：
```c
if (exp.primary_slot == RTOS_TRACE_IDLE_SLOT) {
    exp.primary_stale = false;
} else {
    const uint32_t cur_gen = rtos_task_slot_generation_locked(exp.primary_slot);
    exp.primary_stale = (cur_gen != exp.primary_generation);
}
```

**界面渲染行为**：
- 若 `is_stale == false`：正常显示任务名称与代次，如 `KEY [s0 g10]`。
- 若 `is_stale == true`：在任务名称末尾附加 `?` 警示符，如 `KEY [s0 g1]?`，明确向用户指示该槽位的历史任务已消亡并被重用。

---

## 四、任务名称映射规则 (Task Name Resolution)

在四任务应用中，LCD Task 启动时保存了 4 个主要任务的 `rtos_task_ref_t`：

1. `g_display_task_refs[0]` -> **"KEY"** (按键扫描与状态机任务)
2. `g_display_task_refs[1]` -> **"Controller"** (业务逻辑与命令分发任务)
3. `g_display_task_refs[2]` -> **"LCD"** (ST7789 驱动与页面渲染任务)
4. `g_display_task_refs[3]` -> **"Monitor"** (系统运行指标统计任务)
5. `slot == 0xFFFFU` -> **"Idle"** (内核空闲任务)
6. 未知槽位 -> **"Task#<slot>"**

---

## 五、因果分类与界面文字映射表 (Commit Class Mapping Table)

| `commit_class` 枚举值 | 文本输出 | 字体颜色 | 触发条件 |
|---|---|---|---|
| `RTOS_COMMIT_HIGHER_PREEMPT` (1) | `HIGHER_PREEMPT` | 青色 (CYAN) | 高优先级任务就绪打断当前正在执行的低优先级任务 |
| `RTOS_COMMIT_YIELD` (2) | `YIELD` | 黄色 (YELLOW) | 任务显式调用 `rtos_yield()` 主动让出 CPU |
| `RTOS_COMMIT_SLICE_ROTATION` (3) | `SLICE_ROTATION` | 白色 (WHITE) | 同优先级时间片耗尽触发的就绪队列轮转 |
| `RTOS_COMMIT_BLOCK_OR_WAKE_OTHER` (4) | `BLOCK_OR_WAKE` | 洋红 (MAGENTA) | 当前任务因等待 IPC/延时阻塞，或唤醒其他任务 |
| `RTOS_COMMIT_EXIT` (5) | `EXIT` | 红色 (RED) | 任务主动终止或退出执行 |
| `RTOS_COMMIT_IDLE_TRANSITION` (6) | `IDLE_TRANSITION` | 绿色 (GREEN) | 切换至 Idle 或从 Idle 被中断/延时唤醒 |
| `RTOS_COMMIT_MIXED` (7) | `MIXED` | 黄色 (YELLOW) | 复合原因（如抢占与时间片同时满足） |
| `RTOS_COMMIT_OTHER` / 默认 | `UNKNOWN` | 灰色 (GRAY) | 无法完全归类的其他上下文切换 |
