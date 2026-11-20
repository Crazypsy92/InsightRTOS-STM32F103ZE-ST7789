#include <stddef.h>
#include "kernel_internal.h"
#include "mutex_internal.h"
#include "port.h"
#include "rtos.h"
#include "wait_internal.h"
#include "priority_internal.h"
#include "delay_queue.h"

static rtos_status_t mutex_context(bool running)
{
    if (rtos_port_in_handler() || rtos_port_in_unmaskable_handler() || rtos_port_interrupts_masked()) return RTOS_ERR_CONTEXT;
    if (running || rtos_sched_started()) {
        const rtos_task_t *const task = rtos_task_current();
        if (!rtos_sched_started() || task == NULL || task->id == RTOS_IDLE_TASK_ID ||
            task->state != RTOS_TASK_RUNNING) return RTOS_ERR_STATE;
    }
    return RTOS_OK;
}
void rtos_mutex_set_owner_locked(rtos_mutex_t *mutex, rtos_task_t *owner)
{
    RTOS_ASSERT(rtos_port_interrupts_masked() && mutex != NULL && mutex->initialized);
    rtos_task_t *const previous = mutex->owner;
    RTOS_ASSERT(previous != owner);
    if (previous != NULL) {
        RTOS_ASSERT(previous == rtos_task_current() && previous->state == RTOS_TASK_RUNNING);
        rtos_mutex_t **link = &previous->owned_mutexes;
        while (*link != mutex) { RTOS_ASSERT(*link != NULL); link = &(*link)->owner_next; }
        *link = mutex->owner_next;
    } else RTOS_ASSERT(mutex->owner_next == NULL);
    mutex->owner_next = NULL;
    if (owner != NULL) {
        RTOS_ASSERT(owner->id != RTOS_IDLE_TASK_ID);
        RTOS_ASSERT(owner->state == RTOS_TASK_RUNNING || owner->state == RTOS_TASK_BLOCKED);
        for (const rtos_mutex_t *m = owner->owned_mutexes; m != NULL; m = m->owner_next) {
            RTOS_ASSERT(m != mutex && m->owner == owner);
        }
        mutex->owner_next = owner->owned_mutexes;
        owner->owned_mutexes = mutex;
    }
    mutex->owner = owner;
}
rtos_status_t rtos_mutex_init(rtos_mutex_t *mutex)
{
    const rtos_status_t context = mutex_context(false);
    if (context != RTOS_OK) return context;
    if (mutex == NULL) return RTOS_ERR_ARGUMENT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    if (mutex->initialized) { rtos_critical_exit(irq); return RTOS_ERR_STATE; }
    mutex->owner = NULL; mutex->wait_head = NULL; mutex->owner_next = NULL; mutex->initialized = true;
    rtos_critical_exit(irq); return RTOS_OK;
}
rtos_status_t rtos_mutex_lock(rtos_mutex_t *mutex, uint32_t timeout_ticks)
{
    const rtos_status_t context = mutex_context(true);
    if (context != RTOS_OK) return context;
    if (mutex == NULL || (timeout_ticks > RTOS_DELAY_MAX_TICKS && timeout_ticks != RTOS_WAIT_FOREVER)) return RTOS_ERR_ARGUMENT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    if (!mutex->initialized) { rtos_critical_exit(irq); return RTOS_ERR_STATE; }
    rtos_task_t *const task = rtos_task_current();
    if (mutex->owner == task) { rtos_critical_exit(irq); return RTOS_ERR_STATE; }
    if (mutex->owner == NULL) {
        RTOS_ASSERT(mutex->wait_head == NULL);
        rtos_mutex_set_owner_locked(mutex, task);
        rtos_critical_exit(irq); return RTOS_OK;
    }
    if (timeout_ticks == RTOS_NO_WAIT) { rtos_critical_exit(irq); return RTOS_ERR_UNAVAILABLE; }
    if (!rtos_wait_block_allowed_locked(task)) {
        rtos_critical_exit(irq); return RTOS_ERR_CANCELLED;
    }
    RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL && task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(task->wait_result == RTOS_WAIT_RESULT_NONE && !task->ready_queued && !task->delay_queued);
    RTOS_ASSERT(task->next == NULL && task->delay_next == NULL);
    rtos_task_t **link = &mutex->wait_head;
    while (*link != NULL) {
        const rtos_task_t *const waiter = *link;
        RTOS_ASSERT(waiter != task && waiter->state == RTOS_TASK_BLOCKED);
        RTOS_ASSERT(waiter->wait_object == mutex && waiter->wait_reason == RTOS_WAIT_MUTEX);
        RTOS_ASSERT(waiter->wait_result == RTOS_WAIT_RESULT_PENDING && !waiter->ready_queued && (waiter->delay_queued || waiter->delay_next == NULL));
        if (waiter->effective_priority < task->effective_priority) break;
        link = &(*link)->wait_next;
    }
    task->wait_next = *link; task->wait_object = mutex; task->wait_reason = RTOS_WAIT_MUTEX;
    task->wait_result = RTOS_WAIT_RESULT_PENDING; task->slice_remaining = RTOS_TIME_SLICE_TICKS;
    task->state = RTOS_TASK_BLOCKED; *link = task;
    if (timeout_ticks != RTOS_WAIT_FOREVER) {
        const uint32_t now = rtos_tick_get();
        task->wake_tick = now + timeout_ticks;
        rtos_delay_queue_insert(task, now);
    }
    rtos_mutex_recalculate_locked(mutex->owner);
    rtos_sched_request(RTOS_SCHED_REASON_BLOCK);
    rtos_critical_exit(irq);
    const rtos_irq_state_t resumed = rtos_critical_enter();
    RTOS_ASSERT(rtos_task_current() == task && task->state == RTOS_TASK_RUNNING);
    RTOS_ASSERT(task->wait_result == RTOS_WAIT_RESULT_GRANTED || task->wait_result == RTOS_WAIT_RESULT_TIMEOUT || task->wait_result == RTOS_WAIT_RESULT_CANCELLED);
    const bool granted = task->wait_result == RTOS_WAIT_RESULT_GRANTED;
    RTOS_ASSERT(granted ? mutex->owner == task : mutex->owner != task);
    RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL && task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(!task->ready_queued && !task->delay_queued && task->delay_next == NULL);
    const rtos_status_t result = granted ? RTOS_OK :
        task->wait_result == RTOS_WAIT_RESULT_CANCELLED ? RTOS_ERR_CANCELLED : RTOS_ERR_TIMEOUT;
    task->wait_result = RTOS_WAIT_RESULT_NONE;
    rtos_critical_exit(resumed); return result;
}
rtos_status_t rtos_mutex_unlock(rtos_mutex_t *mutex)
{
    const rtos_status_t context = mutex_context(true);
    if (context != RTOS_OK) return context;
    if (mutex == NULL) return RTOS_ERR_ARGUMENT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    if (!mutex->initialized) { rtos_critical_exit(irq); return RTOS_ERR_STATE; }
    if (mutex->owner != rtos_task_current()) { rtos_critical_exit(irq); return RTOS_ERR_NOT_OWNER; }
    if (mutex->wait_head != NULL) {
        RTOS_ASSERT(rtos_wait_complete_locked(mutex->wait_head, RTOS_WAIT_RESULT_GRANTED));
    } else {
        rtos_task_t *const previous = mutex->owner;
        rtos_mutex_set_owner_locked(mutex, NULL);
        rtos_mutex_recalculate_locked(previous);
    }
    rtos_sched_reconsider();
    rtos_critical_exit(irq); return RTOS_OK;
}

void rtos_mutex_recalculate_locked(rtos_task_t *task)
{
    RTOS_ASSERT(rtos_port_interrupts_masked() && task != NULL);
    RTOS_ASSERT(task->id != RTOS_IDLE_TASK_ID);
    (void)rtos_priority_recompute_locked();
}
