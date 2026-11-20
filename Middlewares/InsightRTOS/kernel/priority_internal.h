#ifndef INSIGHT_RTOS_PRIORITY_INTERNAL_H
#define INSIGHT_RTOS_PRIORITY_INTERNAL_H
#include "rtos_task.h"
/* Global least fixed point from base priorities. Bounded by live task count;
 * returns propagation rounds for verification. Caller holds PRIMASK and only
 * requests scheduling after its complete ownership/wait transaction. */
unsigned int rtos_priority_recompute_locked(void);
/* Masked internal operation, no context switch or budget reset. Actual changes
 * rejoin the effective-priority FIFO tail; unchanged priorities retain order. */
void rtos_task_set_effective_locked(rtos_task_t *task, uint8_t priority);
#endif
