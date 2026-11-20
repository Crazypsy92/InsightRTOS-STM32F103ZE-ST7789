#ifndef RTOS_SMOKE_APP_H
#define RTOS_SMOKE_APP_H
#include <stdint.h>
extern volatile uint32_t g_task_a_runs;
extern volatile uint32_t g_task_b_runs;
/* Public tick snapshot sampled by A, not an ISR-maintained second kernel tick. */
extern volatile uint32_t g_rtos_tick_count;
extern volatile uint32_t g_task_b_runs_while_a_delayed;
extern volatile uint32_t g_task_a_delay_pending;
_Noreturn void rtos_smoke_start(void);
#endif
