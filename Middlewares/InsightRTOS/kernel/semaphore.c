#include <stddef.h>

#include "kernel_internal.h"
#include "port.h"
#include "rtos.h"
#include "semaphore_internal.h"
#include "delay_queue.h"
#include "wait_internal.h"

static rtos_status_t sem_context(bool require_running)
{
    if (rtos_port_in_handler() || rtos_port_in_unmaskable_handler() ||
        rtos_port_interrupts_masked()) {
        return RTOS_ERR_CONTEXT;
    }
    if (require_running || rtos_sched_started()) {
        const rtos_task_t *const task = rtos_task_current();
        if (!rtos_sched_started() || (task == NULL) ||
            (task->id == RTOS_IDLE_TASK_ID) ||
            (task->state != RTOS_TASK_RUNNING)) {
            return RTOS_ERR_STATE;
        }
    }
    return RTOS_OK;
}

static void sem_check(const rtos_semaphore_t *sem)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(sem != NULL && sem->initialized);
    RTOS_ASSERT(sem->max_count >= 1U);
    RTOS_ASSERT(sem->count <= sem->max_count);
    RTOS_ASSERT((sem->wait_head == NULL) || (sem->count == 0U));
}

rtos_status_t rtos_sem_init(rtos_semaphore_t *sem, uint32_t initial_count)
{
    return rtos_sem_init_counting(sem, initial_count, 1U);
}

rtos_status_t rtos_sem_init_counting(rtos_semaphore_t *sem,
                                    uint32_t initial_count, uint32_t max_count)
{
    const rtos_status_t context = sem_context(false);
    if (context != RTOS_OK) {
        return context;
    }
    if ((sem == NULL) || (max_count == 0U) || (initial_count > max_count)) {
        return RTOS_ERR_ARGUMENT;
    }
    const rtos_irq_state_t irq = rtos_critical_enter();
    if (sem->initialized) {
        rtos_critical_exit(irq);
        return RTOS_ERR_STATE;
    }
    sem->count = initial_count;
    sem->max_count = max_count;
    sem->wait_head = NULL;
    sem->initialized = true;
    rtos_critical_exit(irq);
    return RTOS_OK;
}

static void sem_enqueue(rtos_semaphore_t *sem, rtos_task_t *task, uint32_t timeout)
{
    sem_check(sem);
    RTOS_ASSERT(sem->count == 0U);
    RTOS_ASSERT(task->state == RTOS_TASK_RUNNING);
    RTOS_ASSERT(!task->ready_queued && !task->delay_queued);
    RTOS_ASSERT(task->next == NULL && task->delay_next == NULL);
    RTOS_ASSERT(task->wait_next == NULL && task->wait_object == NULL);
    RTOS_ASSERT(task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(task->wait_result == RTOS_WAIT_RESULT_NONE);

    rtos_task_t **link = &sem->wait_head;
    while (*link != NULL) {
        const rtos_task_t *const queued = *link;
        RTOS_ASSERT(queued != task);
        RTOS_ASSERT(queued->state == RTOS_TASK_BLOCKED);
        RTOS_ASSERT(queued->wait_object == sem);
        RTOS_ASSERT(queued->wait_reason == RTOS_WAIT_SEMAPHORE);
        RTOS_ASSERT(queued->wait_result == RTOS_WAIT_RESULT_PENDING);
        RTOS_ASSERT(!queued->ready_queued);
        if (queued->effective_priority < task->effective_priority) {
            break;
        }
        link = &(*link)->wait_next; /* Skip ties to preserve FIFO. */
    }
    task->wait_next = *link;
    task->wait_object = sem;
    task->wait_reason = RTOS_WAIT_SEMAPHORE;
    task->wait_result = RTOS_WAIT_RESULT_PENDING;
    task->slice_remaining = RTOS_TIME_SLICE_TICKS;
    task->state = RTOS_TASK_BLOCKED;
    *link = task;
    if (timeout != RTOS_WAIT_FOREVER) {
        const uint32_t now = rtos_tick_get();
        task->wake_tick = now + timeout;
        rtos_delay_queue_insert(task, now);
    }
    rtos_sched_request(RTOS_SCHED_REASON_BLOCK);
}

rtos_status_t rtos_sem_take(rtos_semaphore_t *sem, uint32_t timeout)
{
    const rtos_status_t context = sem_context(true);
    if (context != RTOS_OK) {
        return context;
    }
    if ((sem == NULL) || ((timeout > RTOS_DELAY_MAX_TICKS) &&
                          (timeout != RTOS_WAIT_FOREVER))) {
        return RTOS_ERR_ARGUMENT;
    }
    const rtos_irq_state_t irq = rtos_critical_enter();
    if (!sem->initialized) {
        rtos_critical_exit(irq);
        return RTOS_ERR_STATE;
    }
    sem_check(sem);
    if (sem->count > 0U) {
        --sem->count;
        rtos_critical_exit(irq);
        return RTOS_OK;
    }
    if (timeout == 0U) {
        rtos_critical_exit(irq);
        return RTOS_ERR_UNAVAILABLE;
    }
    rtos_task_t *const task = rtos_task_current();
    if (!rtos_wait_block_allowed_locked(task)) {
        rtos_critical_exit(irq); return RTOS_ERR_CANCELLED;
    }
    sem_enqueue(sem, task, timeout);
    rtos_critical_exit(irq);
    /* Resume with exactly one committed result; never retry shared count. */
    const rtos_irq_state_t resumed_irq = rtos_critical_enter();
    RTOS_ASSERT(rtos_task_current() == task && task->state == RTOS_TASK_RUNNING);
    RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL);
    RTOS_ASSERT(task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(!task->delay_queued && !task->ready_queued);
    RTOS_ASSERT((task->wait_result == RTOS_WAIT_RESULT_GRANTED) ||
                (task->wait_result == RTOS_WAIT_RESULT_TIMEOUT) ||
                (task->wait_result == RTOS_WAIT_RESULT_CANCELLED));
    const rtos_status_t result = task->wait_result == RTOS_WAIT_RESULT_GRANTED ?
                                 RTOS_OK :
            task->wait_result == RTOS_WAIT_RESULT_CANCELLED ? RTOS_ERR_CANCELLED : RTOS_ERR_TIMEOUT;
    task->wait_result = RTOS_WAIT_RESULT_NONE;
    rtos_critical_exit(resumed_irq);
    return result;
}

rtos_status_t rtos_sem_give_locked(rtos_semaphore_t *sem)
{
    sem_check(sem);
    rtos_task_t *const task = sem->wait_head;
    if (task == NULL) {
        if (sem->count == sem->max_count) {
            return RTOS_ERR_FULL;
        }
        /* Checked before addition, including max_count == UINT32_MAX. */
        ++sem->count;
        return RTOS_OK;
    }
    RTOS_ASSERT(task->wait_object == sem);
    RTOS_ASSERT(rtos_wait_complete_locked(task, RTOS_WAIT_RESULT_GRANTED));
    return RTOS_OK;
}

static rtos_status_t sem_give_checked(rtos_semaphore_t *sem)
{
    if (sem == NULL) {
        return RTOS_ERR_ARGUMENT;
    }
    const rtos_irq_state_t irq = rtos_critical_enter();
    if (!sem->initialized) {
        rtos_critical_exit(irq);
        return RTOS_ERR_STATE;
    }
    const rtos_status_t result = rtos_sem_give_locked(sem);
    rtos_critical_exit(irq);
    return result;
}

rtos_status_t rtos_sem_give(rtos_semaphore_t *sem)
{
    const rtos_status_t context = sem_context(true);
    return context == RTOS_OK ? sem_give_checked(sem) : context;
}

rtos_status_t rtos_sem_give_from_isr(rtos_semaphore_t *sem)
{
    if (!rtos_port_in_kernel_isr()) return RTOS_ERR_CONTEXT;
    /* An interrupted current task may already be BLOCKED or READY before
     * its first PendSV. Idle is also a valid interrupted context. */
    if (!rtos_sched_started() || rtos_task_current() == NULL) return RTOS_ERR_STATE;
    return sem_give_checked(sem);
}
