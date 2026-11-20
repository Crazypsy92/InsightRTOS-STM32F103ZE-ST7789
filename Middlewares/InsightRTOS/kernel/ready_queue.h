#ifndef INSIGHT_RTOS_READY_QUEUE_H
#define INSIGHT_RTOS_READY_QUEUE_H

#include <stdbool.h>

#include "rtos_task.h"
#include "rtos_config.h"

/* Kernel-internal interfaces; the caller must hold the IRQ exclusion lock. */
void rtos_ready_queue_push(rtos_task_t *task);
void rtos_ready_queue_push_front(rtos_task_t *task);
rtos_task_t *rtos_ready_queue_pop(void);
bool rtos_ready_queue_has_priority(uint8_t priority);
/* Remove before changing effective priority. No budget reset. */
void rtos_ready_queue_remove(rtos_task_t *task);
int rtos_ready_queue_highest_priority(void);

#if RTOS_TESTING
/* Read-only roots; caller holds PRIMASK and inspects a stable transaction. */
void rtos_ready_queue_test_roots(rtos_task_t **heads, rtos_task_t **tails);
#endif

#endif
