#ifndef INSIGHT_DIAG_RT_INTERNAL_H
#define INSIGHT_DIAG_RT_INTERNAL_H

#include "rtos_diag_rt.h"
#include "rtos_task.h"

#if INSIGHT_DIAG_RT

void rtos_diag_rt_init(void);
void rtos_diag_rt_task_init_hook(uint32_t slot, uint32_t generation);
void rtos_diag_rt_task_ready_hook(rtos_task_t *task);
void rtos_diag_rt_switch_hook(rtos_task_t *previous, rtos_task_t *next);
void rtos_diag_rt_task_exit_hook(const rtos_task_t *task);
void rtos_diag_rt_on_tick(uint32_t tick);

#else

#define rtos_diag_rt_init() ((void)0)
#define rtos_diag_rt_task_init_hook(slot, gen) ((void)0)
#define rtos_diag_rt_task_ready_hook(task) ((void)0)
#define rtos_diag_rt_switch_hook(prev, next) ((void)0)
#define rtos_diag_rt_task_exit_hook(task) ((void)0)
#define rtos_diag_rt_on_tick(tick) ((void)0)

#endif /* INSIGHT_DIAG_RT */

#endif /* INSIGHT_DIAG_RT_INTERNAL_H */
