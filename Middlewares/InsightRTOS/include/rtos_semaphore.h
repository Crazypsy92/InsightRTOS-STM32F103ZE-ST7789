#ifndef INSIGHT_RTOS_SEMAPHORE_H
#define INSIGHT_RTOS_SEMAPHORE_H

#include <stdbool.h>
#include <stdint.h>
#include "rtos_task.h"
#include "rtos_wait.h"


/* Static, initially zero-initialized storage; never copy or modify fields.
 * init succeeds once. Object and waiting task stacks outlive all operations.
 * Permits have no owner. There is no destruction/reinitialization API.
 */
typedef struct {
    uint32_t count;
    uint32_t max_count;
    rtos_task_t *wait_head;
    bool initialized;
} rtos_semaphore_t;

/* Thread only, all IRQ masks clear; allowed before scheduler start. */
rtos_status_t rtos_sem_init(rtos_semaphore_t *sem, uint32_t initial_count);
/* Same lifecycle/context rules; 1 <= max_count <= UINT32_MAX and
 * initial_count <= max_count. The binary initializer is exactly max_count=1.
 * max_count bounds stored count, not permits already directly granted. */
rtos_status_t rtos_sem_init_counting(rtos_semaphore_t *sem,
                                    uint32_t initial_count, uint32_t max_count);
/* Ordinary RUNNING thread, started kernel, all IRQ masks clear.
 * Timeout 0 polls; 1..INT32_MAX waits that many subsequent delivered ticks;
 * WAIT_FOREVER has no deadline. Other values return ERR_ARGUMENT.
 * Empty nonblocking take returns ERR_UNAVAILABLE; full give returns ERR_FULL.
 * A blocking take returns OK for its own grant, or ERR_TIMEOUT for expiry.
 * At a deadline, the first protected completion wins (grant or timeout).
 */
rtos_status_t rtos_sem_take(rtos_semaphore_t *sem, uint32_t timeout);
rtos_status_t rtos_sem_give(rtos_semaphore_t *sem);

/* SysTick or external IRQ only, after scheduler start. NMI, faults, SVC,
 * PendSV and thread mode return ERR_CONTEXT. Saves/restores entry PRIMASK;
 * inherited PRIMASK/BASEPRI/FAULTMASK are preserved. Never blocks or switches
 * task contexts directly; a higher-priority wake requests PendSV. */
rtos_status_t rtos_sem_give_from_isr(rtos_semaphore_t *sem);

/* A pending blocking wait may return ERR_CANCELLED after a successful
 * rtos_task_cancel_wait(ref) or request_stop(ref). A sticky STOP also rejects
 * a new blocking registration; immediate success and NO_WAIT are unchanged.
 * No message/permit is acquired or committed on CANCELLED. */
#endif
