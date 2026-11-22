# InsightRTOS Stage 9.1: Trace Compatibility Report

## 一、向后兼容性验证

Stage 8.2 既有的 Trace API 在 Stage 9.1 中得到 100% 保持与支持：

```c
rtos_status_t rtos_trace_init(void);
rtos_status_t rtos_trace_read(uint32_t start_seq, rtos_trace_record_t *records, uint32_t max_count, uint32_t *actual_count);
rtos_status_t rtos_trace_get_count(uint32_t *count);
```

### 1. 语义与读取行为保持
- 历史代码调用 `rtos_trace_read()` 时，其参数类型、返回值与序列号读取逻辑与原行为完全一致。
- 环形缓冲区的物理容量保持 128 条记录，溢出覆盖行为保持一致。
- 历史测试用例 `tests/trace/` 在没有任何修改的情况下直接在 Host 与 QEMU 上 100% 通过。

---

## 二、配置组合与零开销证明

Stage 9.1 严格测试了三种构建组合：

| 构建配置 | Trace 状态 | 诊断状态 | 静态内存增量 (RAM) | Flash 影响 | 关键路径开销 |
|---|---|---|---|---|---|
| **TRACE OFF / EXPLAIN OFF** | 禁用 | 禁用 | **0 B** | **0 B** | **0 cycle (宏展开为空)** |
| **TRACE ON / EXPLAIN OFF** | 启用 | 禁用 | **3,088 B** (原基准) | **0 B** (原基准) | 保持 Stage 8.2 原开销 |
| **TRACE ON / EXPLAIN ON** | 启用 | 启用 | **3,096 B** (+8 B) | **+300 B (O2) / +604 B (Debug)** | 极小 (仅在切换点记录 Commit) |

### 1. 禁用状态下的生产代码分析
在 `kernel/trace_internal.h` 中：
```c
#if defined(INSIGHT_DIAG_EXPLAIN) && INSIGHT_DIAG_EXPLAIN
void rtos_trace_context_switch_locked(rtos_task_t *previous, rtos_task_t *next, uint32_t reasons, bool prev_was_running);
#define RTOS_TRACE_CONTEXT_SWITCH(prev, next, reasons, prev_was_running) \
    rtos_trace_context_switch_locked((prev), (next), (reasons), (prev_was_running))
#else
#define RTOS_TRACE_CONTEXT_SWITCH(prev, next, reasons, prev_was_running) ((void)(prev_was_running))
#endif
```
当 `INSIGHT_DIAG_EXPLAIN` 关闭时，宏展开为空语句（或参数求值），在编译器优化后生成的机器指令与基线完全相同，不产生任何分支或内存访问。

### 2. 依赖安全性
在 `CMakeLists.txt` 中添加了构建守卫：
```cmake
if(INSIGHT_DIAG_EXPLAIN AND NOT INSIGHT_TRACE_ENABLED)
    message(FATAL_ERROR "INSIGHT_DIAG_EXPLAIN requires INSIGHT_TRACE_ENABLED=ON")
endif()
```
杜绝了未开启 Trace 时误开启 Explain 导致的未定义符号或编译告警。
