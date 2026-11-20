#include <stddef.h>

#include "delay_queue.h"
#include "kernel_internal.h"
#include "port.h"
#include "queue_internal.h"
#include "mutex_internal.h"
#include "rtos.h"
#include "wait_internal.h"
#include "task_lifecycle_internal.h"

bool rtos_wait_complete_locked(rtos_task_t *task, rtos_wait_result_t result)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(task != NULL);
    RTOS_ASSERT((result == RTOS_WAIT_RESULT_GRANTED) ||
                (result == RTOS_WAIT_RESULT_TIMEOUT) ||
                (result == RTOS_WAIT_RESULT_CANCELLED));
    if ((task->state != RTOS_TASK_BLOCKED) ||
        ((task->wait_reason != RTOS_WAIT_SEMAPHORE) && (task->wait_reason != RTOS_WAIT_MUTEX) &&
         (task->wait_reason != RTOS_WAIT_QUEUE_SEND) && (task->wait_reason != RTOS_WAIT_QUEUE_RECEIVE)) ||
        (task->wait_result != RTOS_WAIT_RESULT_PENDING)) {
        return false;
    }
    RTOS_ASSERT(!task->ready_queued && task->next == NULL);
    rtos_task_t **head; RTOS_TRACE_WAIT_SAVE(task);
    rtos_task_t *old_owner = NULL;
    if (task->wait_reason == RTOS_WAIT_MUTEX) {
        rtos_mutex_t *const mutex = task->wait_object;
        RTOS_ASSERT(mutex != NULL && mutex->initialized && mutex->owner != NULL);
        old_owner = mutex->owner;
        if (result == RTOS_WAIT_RESULT_GRANTED) {
            RTOS_ASSERT(mutex->wait_head == task);
            rtos_mutex_set_owner_locked(mutex, task);
        } else if (result == RTOS_WAIT_RESULT_TIMEOUT) {
            /* An infinite wait has no timeout authority. A finite timeout
             * only removes the dependency; ownership must remain untouched. */
            if (!task->delay_queued) return false;
            RTOS_ASSERT(rtos_tick_deadline_due(rtos_tick_get(), task->wake_tick));
        }
        head = &mutex->wait_head;
    } else if (task->wait_reason == RTOS_WAIT_QUEUE_SEND || task->wait_reason == RTOS_WAIT_QUEUE_RECEIVE) {
        /* GRANTED caller has committed the copy/ring update. TIMEOUT only
         * revokes a due finite wait; no data copy and no Mutex graph change. */
        if (result == RTOS_WAIT_RESULT_TIMEOUT) {
            if (!task->delay_queued) return false;
            RTOS_ASSERT(rtos_tick_deadline_due(rtos_tick_get(), task->wake_tick));
        }
        RTOS_ASSERT(task->delay_queued || task->delay_next == NULL);
        RTOS_ASSERT(task->wait_buffer.receive != NULL);
        head = rtos_queue_wait_head_locked(task);
        if (result == RTOS_WAIT_RESULT_GRANTED) RTOS_ASSERT(*head == task);
    } else {
        rtos_semaphore_t *const sem = task->wait_object;
        RTOS_ASSERT(sem != NULL && sem->initialized);
        RTOS_ASSERT(sem->max_count > 0U && sem->count == 0U);
        if (result == RTOS_WAIT_RESULT_TIMEOUT) {
            RTOS_ASSERT(task->delay_queued);
            RTOS_ASSERT(rtos_tick_deadline_due(rtos_tick_get(), task->wake_tick));
        }
        head = &sem->wait_head;
    }

    /* All IPC object types join the same atomic unlink/result/READY commit. */
    rtos_task_t **link = head;
    while (*link != task) {
        RTOS_ASSERT(*link != NULL);
        link = &(*link)->wait_next;
    }
    *link = task->wait_next;
    task->wait_next = NULL;
    task->wait_object = NULL;
    task->wait_buffer.receive = NULL;
    task->wait_reason = RTOS_WAIT_NONE;
    if (task->delay_queued) rtos_delay_queue_remove(task);
    RTOS_ASSERT(task->delay_next == NULL);
    if (old_owner != NULL) {
        /* Unlink selected waiter before recomputing the new owner, or the
         * target would incorrectly donate to itself. Still one transaction. */
        rtos_mutex_recalculate_locked(old_owner);
        /* One global solve also covers the new owner and all upstream nodes. */
    }
    task->wait_result = result; RTOS_TRACE(RTOS_TRACE_WAIT_COMPLETE, task, result, trace_wait_reason);
    rtos_sched_wake(task);
    if (old_owner != NULL) rtos_sched_reconsider();
    return true;
}

bool rtos_wait_block_allowed_locked(rtos_task_t *task)
{
    RTOS_ASSERT(rtos_port_interrupts_masked() && task == rtos_current_task);
    RTOS_ASSERT(task != NULL && task->state == RTOS_TASK_RUNNING);
    return !rtos_task_stop_pending_locked(task);
}
