#ifndef INSIGHT_RTOS_TICK_H
#define INSIGHT_RTOS_TICK_H

#include <stdint.h>

/* A protected snapshot, callable from threads or configurable-priority ISRs.
 * Counts delivered ticks since start, modulo 2^32; it is not wall-clock time.
 */
uint32_t rtos_tick_get(void);

#endif
