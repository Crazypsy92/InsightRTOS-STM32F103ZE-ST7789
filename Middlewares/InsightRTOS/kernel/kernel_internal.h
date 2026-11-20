#ifndef INSIGHT_RTOS_KERNEL_INTERNAL_H
#define INSIGHT_RTOS_KERNEL_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "rtos_config.h"
#include "rtos_task.h"
#include "trace_internal.h"

#define RTOS_SCHED_REASON_START (UINT32_C(1) << 0)
#define RTOS_SCHED_REASON_YIELD (UINT32_C(1) << 1)
#define RTOS_SCHED_REASON_SLICE (UINT32_C(1) << 2)
#define RTOS_SCHED_REASON_HIGHER (UINT32_C(1) << 3)
#define RTOS_SCHED_REASON_EXIT (UINT32_C(1) << 4)
#define RTOS_SCHED_REASON_IDLE (UINT32_C(1) << 5)
#define RTOS_SCHED_REASON_BLOCK (UINT32_C(1) << 6)
#define RTOS_SCHED_REASON_WAKE (UINT32_C(1) << 7)

/* PendSV reads this symbol and the sp member at TCB offset zero. */
extern rtos_task_t *rtos_current_task;

/* Called with interrupts masked, after the outgoing context has been saved. */
void rtos_sched_switch(void);
bool rtos_sched_started(void);
bool rtos_sched_stack_overlaps(uintptr_t begin, uintptr_t end);
bool rtos_task_stack_valid(const rtos_task_t *task);
/* Live published user tasks in slot/ID order, masked; no Idle/reservations. */
size_t rtos_task_snapshot_locked(rtos_task_t **tasks);

/* All three require interrupts masked; request flags and PendSV coalesce. */
void rtos_sched_request(uint32_t reasons);
void rtos_sched_on_tick(void);
/* Reconsider after a complete priority/ownership transaction. Masked. */
void rtos_sched_reconsider(void);
void rtos_sched_task_ready(rtos_task_t *task);
/* Internal state transitions, caller masked; Idle can neither block nor wake. */
void rtos_sched_block_current(uint32_t deadline, uint32_t now);
void rtos_sched_wake(rtos_task_t *task);
void rtos_delay_process_expired(uint32_t now);

/* SysTick glue calls this; no exception frame manipulation occurs here. */
void rtos_tick_handler(void);

#if RTOS_TESTING
void rtos_tick_test_set(uint32_t tick);
/* Deadline-only logical acceleration, not time-slice/CPU execution simulation.
 * Started kernel; delta 0..INT32_MAX, expirations processed at the final tick.
 */
void rtos_tick_test_advance(uint32_t ticks);
/* Test builds supply this bounded recording hook; never perform UART I/O here. */
void rtos_test_switch_hook(const rtos_task_t *previous, const rtos_task_t *next,
                           uint32_t reasons, uint32_t tick);
#endif

#endif
