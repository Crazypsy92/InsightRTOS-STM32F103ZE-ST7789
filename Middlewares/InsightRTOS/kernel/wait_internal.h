#ifndef INSIGHT_RTOS_WAIT_INTERNAL_H
#define INSIGHT_RTOS_WAIT_INTERNAL_H

#include <stdbool.h>
#include "rtos_task.h"

/* PRIMASK exclusion required. Complete one pending semaphore, mutex or queue operation, remove
 * both indices, publish one result and READY. Stale attempts return false and
 * change nothing. TIMEOUT requires an actually due registered deadline.
 * Mutex GRANTED transfers owner before READY; TIMEOUT preserves owner.
 * CANCELLED revokes either finite/infinite IPC wait without data, permits or
 * owner transfer; Mutex cancellation re-solves PI before READY.
 * Infinite Mutex waits reject TIMEOUT without changing state.
 * Queue GRANTED requires caller to finish full copy/ring commit BEFORE this
 * call; removes event/buffer/deadline and publishes READY. Queue TIMEOUT requires
 * a due registered finite deadline, revokes references without data copy or PI.
 * Ordinary delay-only tasks retain their existing result-free wake path. */
/* Call at the actual wait-registration boundary, AFTER immediate/NO_WAIT paths.
 * Requires current RUNNING user task and PRIMASK. No mutation or scheduling. */
bool rtos_wait_block_allowed_locked(rtos_task_t *task);

bool rtos_wait_complete_locked(rtos_task_t *task, rtos_wait_result_t result);

#endif
