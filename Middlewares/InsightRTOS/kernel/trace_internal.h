#ifndef INSIGHT_TRACE_INTERNAL_H
#define INSIGHT_TRACE_INTERNAL_H
#include "../include/rtos_trace.h"
#if INSIGHT_TRACE_ENABLED
/* Caller holds PRIMASK; never called from NMI/HardFault. No mask mutation. */
void rtos_trace_emit_locked(rtos_trace_event_t event, const rtos_task_t *task,
                            uint32_t arg0, uint32_t arg1);
void rtos_trace_select_locked(const rtos_task_t *previous, const rtos_task_t *next, uint32_t reasons);
uint32_t rtos_trace_generation_locked(const rtos_task_t *task);
uint32_t rtos_trace_tick_locked(void);
#define RTOS_TRACE(event, task, a, b) rtos_trace_emit_locked((event), (task), (uint32_t)(a), (uint32_t)(b))
#define RTOS_TRACE_SELECT(previous, next, reasons) rtos_trace_select_locked((previous), (next), (reasons))
#define RTOS_TRACE_WAIT_SAVE(task) const uint32_t trace_wait_reason = (uint32_t)(task)->wait_reason
#if RTOS_TESTING
void rtos_trace_test_reset_locked(uint32_t next_sequence);
#endif
#else
#define RTOS_TRACE(event, task, a, b) ((void)0)
#define RTOS_TRACE_SELECT(previous, next, reasons) ((void)0)
#define RTOS_TRACE_WAIT_SAVE(task) ((void)0)
#endif
#endif
