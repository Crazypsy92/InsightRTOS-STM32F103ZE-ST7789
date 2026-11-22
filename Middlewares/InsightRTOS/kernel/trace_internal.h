#ifndef INSIGHT_TRACE_INTERNAL_H
#define INSIGHT_TRACE_INTERNAL_H
#include "../include/rtos_trace.h"
#include "../include/rtos_diag.h"
#if INSIGHT_TRACE_ENABLED
/* Caller holds PRIMASK; never called from NMI/HardFault. No mask mutation. */
void rtos_trace_emit_locked(rtos_trace_event_t event, const rtos_task_t *task,
                            uint32_t arg0, uint32_t arg1);
void rtos_trace_select_locked(const rtos_task_t *previous, const rtos_task_t *next, uint32_t reasons);
uint32_t rtos_trace_generation_locked(const rtos_task_t *task);
uint32_t rtos_trace_tick_locked(void);
uint32_t rtos_task_slot_generation_locked(uint16_t slot);
#if INSIGHT_DIAG_EXPLAIN
void rtos_trace_context_switch_locked(const rtos_task_t *previous, const rtos_task_t *next,
                                      uint32_t reasons, bool prev_was_running);
#endif
#define RTOS_TRACE(event, task, a, b) rtos_trace_emit_locked((event), (task), (uint32_t)(a), (uint32_t)(b))
#define RTOS_TRACE_SELECT(previous, next, reasons) rtos_trace_select_locked((previous), (next), (reasons))
#define RTOS_TRACE_WAIT_SAVE(task) const uint32_t trace_wait_reason = (uint32_t)(task)->wait_reason
#if INSIGHT_DIAG_EXPLAIN
#define RTOS_TRACE_DELAY_EXPIRED(task) rtos_trace_emit_locked(RTOS_TRACE_DELAY_EXPIRED, (task), (task)->wake_tick, 0U)
#define RTOS_TRACE_YIELD(task) rtos_trace_emit_locked(RTOS_TRACE_TASK_YIELD, (task), RTOS_SCHED_REASON_YIELD, 0U)
#define RTOS_TRACE_CONTEXT_SWITCH(prev, next, reasons, prev_was_running) \
    do { \
        if ((prev) != NULL && (next) != NULL && (prev) != (next) && rtos_port_in_pendsv()) { \
            rtos_trace_context_switch_locked((prev), (next), (reasons), (prev_was_running)); \
        } \
    } while (0)
#else
#define RTOS_TRACE_DELAY_EXPIRED(task) ((void)0)
#define RTOS_TRACE_YIELD(task) ((void)0)
#define RTOS_TRACE_CONTEXT_SWITCH(prev, next, reasons, prev_was_running) ((void)(prev_was_running))
#endif
#if RTOS_TESTING
void rtos_trace_test_reset_locked(uint32_t next_sequence);
#endif
#else
#define RTOS_TRACE(event, task, a, b) ((void)0)
#define RTOS_TRACE_SELECT(previous, next, reasons) ((void)0)
#define RTOS_TRACE_WAIT_SAVE(task) ((void)0)
#define RTOS_TRACE_DELAY_EXPIRED(task) ((void)0)
#define RTOS_TRACE_YIELD(task) ((void)0)
#define RTOS_TRACE_CONTEXT_SWITCH(prev, next, reasons, prev_was_running) ((void)(prev_was_running))
#endif
#endif
