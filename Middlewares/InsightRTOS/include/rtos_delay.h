#ifndef INSIGHT_RTOS_DELAY_H
#define INSIGHT_RTOS_DELAY_H

#include <stdint.h>

#include "rtos_task.h"

#define RTOS_DELAY_MAX_TICKS ((uint32_t)INT32_MAX)

/*
 * Ordinary RUNNING task only, kernel started and all IRQ masks clear.
 * Zero succeeds without blocking or yielding; 1..MAX waits at least that many
 * subsequent delivered ticks before becoming READY. CPU execution may be later.
 * Invalid context/state is rejected even when ticks is zero.
 */
rtos_status_t rtos_delay(uint32_t ticks);

/*
 * Advance the planned release by exactly one period (1..MAX). The previous
 * release must be past/equal to now and no older than MAX ticks, using modular
 * uint32_t arithmetic. Future/ambiguous bases are rejected unchanged.
 * Wait only if that next release is future; missed releases return immediately
 * and retain the planned baseline, allowing one-period-at-a-time catch-up.
 * All errors leave *previous_release_tick unchanged.
 */
rtos_status_t rtos_delay_until(uint32_t *previous_release_tick,
                              uint32_t period_ticks);

#endif
