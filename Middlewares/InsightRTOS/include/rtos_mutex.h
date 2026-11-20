#ifndef INSIGHT_RTOS_MUTEX_H
#define INSIGHT_RTOS_MUTEX_H
#include <stdbool.h>
#include <stdint.h>
#include "rtos_task.h"
#include "rtos_wait.h"
/* Static zero-initialized, init once; never copy/modify or destroy a live
 * object. Owner is a task identity, including a READY task awaiting resumption.
 * Transitive PI propagates effective demands through the whole wait graph.
 * Bounded recomputation also revokes expired/cancelled donations. No recursive
 * locking or ISR operation. Default production policy enables PI. */
typedef struct rtos_mutex {
    rtos_task_t *owner;
    rtos_task_t *wait_head;
    bool initialized;
    struct rtos_mutex *owner_next;
} rtos_mutex_t;
/* Thread with all IRQ masks clear; init also allowed before scheduler start. */
rtos_status_t rtos_mutex_init(rtos_mutex_t *mutex);
/* Ordinary RUNNING task only. Accepts NO_WAIT, 1..INT32_MAX or WAIT_FOREVER;
 * other values ERR_ARGUMENT even when unlocked. Finite contention may return
 * ERR_TIMEOUT or ERR_CANCELLED without changing the owner. Self-lock returns ERR_STATE unchanged.
 * Contended NO_WAIT returns ERR_UNAVAILABLE without PendSV. Sticky STOP rejects
 * a new contended blocking wait with ERR_CANCELLED; unlock still requires owner. */
rtos_status_t rtos_mutex_lock(rtos_mutex_t *mutex, uint32_t timeout_ticks);
/* Only owner may unlock; otherwise ERR_NOT_OWNER, including unlocked objects.
 * Direct handoff selects highest priority/FIFO and assigns ownership before
 * READY publication. Exiting with any owned mutex is a fatal API violation. */
rtos_status_t rtos_mutex_unlock(rtos_mutex_t *mutex);
#endif
