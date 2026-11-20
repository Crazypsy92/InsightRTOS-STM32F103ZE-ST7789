#ifndef INSIGHT_RTOS_MUTEX_INTERNAL_H
#define INSIGHT_RTOS_MUTEX_INTERNAL_H
#include "rtos_mutex.h"
/* PRIMASK required. Move object from old owner's intrusive list to new owner
 * (or NULL). Used only by lock/unlock and unified wait completion. */
/* Recompute the entire live wait graph, including dynamic upward/downward
 * propagation. Parameter verifies the initiating owner is a valid user task. */
void rtos_mutex_recalculate_locked(rtos_task_t *task);
void rtos_mutex_set_owner_locked(rtos_mutex_t *mutex, rtos_task_t *owner);
#endif
