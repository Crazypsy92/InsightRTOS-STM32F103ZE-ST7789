#include <stddef.h>
#include "priority_internal.h"
#include "kernel_internal.h"
#include "port.h"
#include "rtos.h"

unsigned int rtos_priority_recompute_locked(void)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    rtos_task_t *tasks[RTOS_MAX_TASKS];
    uint8_t candidate[RTOS_MAX_TASKS], next[RTOS_MAX_TASKS];
    uint8_t owner_index[RTOS_MAX_TASKS];
    const size_t count = rtos_task_snapshot_locked(tasks);
    RTOS_ASSERT(count <= RTOS_MAX_TASKS);
    for (size_t i = 0U; i < count; ++i) {
        RTOS_ASSERT(tasks[i]->base_priority <= RTOS_MAX_PRIORITY);
        candidate[i] = tasks[i]->base_priority;
        owner_index[i] = (uint8_t)RTOS_MAX_TASKS;
#if RTOS_MUTEX_PI
        if (tasks[i]->state == RTOS_TASK_BLOCKED && tasks[i]->wait_reason == RTOS_WAIT_MUTEX) {
            RTOS_ASSERT(tasks[i]->wait_result == RTOS_WAIT_RESULT_PENDING &&
                        (tasks[i]->delay_queued || tasks[i]->delay_next == NULL));
            const rtos_mutex_t *const mutex = tasks[i]->wait_object;
            RTOS_ASSERT(mutex != NULL && mutex->initialized && mutex->owner != NULL);
            size_t owner = 0U;
            while (owner < count && tasks[owner] != mutex->owner) ++owner;
            RTOS_ASSERT(owner < count && owner != i);
            owner_index[i] = (uint8_t)owner;
        }
#endif
    }
    /* Synchronous max propagation is independent of node enumeration order.
     * A maximum travels at most N-1 simple edges; round N verifies stability.
     * Reinitializing from bases also removes boosts trapped in old cycles. */
    bool stable = count == 0U;
    unsigned int rounds = 0U;
    for (size_t round = 0U; round < count; ++round) {
        for (size_t i = 0U; i < count; ++i) next[i] = candidate[i];
        for (size_t i = 0U; i < count; ++i) {
            const size_t owner = owner_index[i];
            if (owner < count && next[owner] < candidate[i]) next[owner] = candidate[i];
        }
        stable = true; ++rounds;
        for (size_t i = 0U; i < count; ++i) {
            if (candidate[i] != next[i]) stable = false;
            candidate[i] = next[i];
        }
        if (stable) break;
    }
    RTOS_ASSERT(stable);
    for (size_t i = 0U; i < count; ++i) rtos_task_set_effective_locked(tasks[i], candidate[i]);
    return rounds;
}
