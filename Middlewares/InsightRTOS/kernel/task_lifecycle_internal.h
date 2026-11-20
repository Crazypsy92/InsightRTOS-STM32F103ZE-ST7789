#ifndef INSIGHT_TASK_LIFECYCLE_INTERNAL_H
#define INSIGHT_TASK_LIFECYCLE_INTERNAL_H
#include "rtos_config.h"
#include "rtos_task.h"
/* Internal identity foundation, by value. All calls require IRQ exclusion.
 * Capture before reuse; capturing an old pointer afterwards identifies NEW task.
 * Resolution itself does not mutate state. Generation never wraps. */
bool rtos_task_reclaimable_locked(const rtos_task_t *task);
bool rtos_task_ref_capture_locked(const rtos_task_t *task, rtos_task_ref_t *ref);
rtos_task_t *rtos_task_ref_resolve_locked(rtos_task_ref_t ref);
/* PRIMASK; live pool TCB only, O(1), does not consume STOP. */
bool rtos_task_stop_pending_locked(const rtos_task_t *task);
#if RTOS_TESTING
/* Saturation fault injection only, on a safe terminated task. */
void rtos_task_test_generation_locked(rtos_task_t *task, uint32_t generation);
#endif
#endif
