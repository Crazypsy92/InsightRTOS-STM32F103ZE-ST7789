#ifndef INSIGHT_RTOS_CRITICAL_H
#define INSIGHT_RTOS_CRITICAL_H

#include <stdint.h>

typedef uint32_t rtos_irq_state_t;

/*
 * Thread mode or configurable-priority exception handlers only, never NMI or
 * HardFault. Tokens must be paired in last-in-first-out order in the same
 * context. An inner exit restores the masked state inherited from its parent.
 * Keep sections short: pending SysTick events can coalesce while IRQs are masked.
 */
rtos_irq_state_t rtos_critical_enter(void);
void rtos_critical_exit(rtos_irq_state_t state);

#endif
