#ifndef INSIGHT_RTOS_SCHED_H
#define INSIGHT_RTOS_SCHED_H

/* Both APIs require thread context with interrupts enabled. */
_Noreturn void rtos_start(void);
void rtos_yield(void);

#endif
