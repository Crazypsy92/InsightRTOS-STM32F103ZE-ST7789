#ifndef INSIGHT_RTOS_DELAY_QUEUE_H
#define INSIGHT_RTOS_DELAY_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include "rtos_delay.h"
#include "rtos_config.h"
#include "rtos_task.h"

/* All operations require the IRQ exclusion lock. Insert future deadlines only,
 * 1..MAX ahead of now. Process expirations before advancing another half range.
 */
void rtos_delay_queue_insert(rtos_task_t *task, uint32_t now);
rtos_task_t *rtos_delay_queue_peek_due(uint32_t now);
rtos_task_t *rtos_delay_queue_pop_due(uint32_t now);
/* Remove a known member by identity (including a middle/tail deadline). */
void rtos_delay_queue_remove(rtos_task_t *task);
bool rtos_delay_queue_empty(void);

static inline bool rtos_tick_deadline_due(uint32_t now, uint32_t deadline)
{
    /* The supported horizon excludes the ambiguous half-circle distance. */
    return (uint32_t)(now - deadline) <= RTOS_DELAY_MAX_TICKS;
}

#if RTOS_TESTING
/* Read-only test root, IRQ exclusion required. */
rtos_task_t *rtos_delay_queue_test_head(void);
#endif

#endif
