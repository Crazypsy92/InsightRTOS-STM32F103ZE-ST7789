#include <stddef.h>
#include <stdint.h>

#include "delay_queue.h"
#include "kernel_internal.h"
#include "port.h"
#include "rtos.h"
#include "wait_internal.h"

static rtos_status_t delay_call_context(void)
{
    if (rtos_port_in_handler() || rtos_port_in_unmaskable_handler() ||
        rtos_port_interrupts_masked()) {
        return RTOS_ERR_CONTEXT;
    }
    if (!rtos_sched_started() || (rtos_current_task == NULL) ||
        (rtos_current_task->id == RTOS_IDLE_TASK_ID) ||
        (rtos_current_task->state != RTOS_TASK_RUNNING)) {
        return RTOS_ERR_STATE;
    }
    return RTOS_OK;
}

rtos_status_t rtos_delay(uint32_t ticks)
{
    const rtos_status_t context = delay_call_context();
    if (context != RTOS_OK) {
        return context;
    }
    if (ticks > RTOS_DELAY_MAX_TICKS) {
        return RTOS_ERR_ARGUMENT;
    }
    if (ticks == 0U) {
        return RTOS_OK;
    }

    const rtos_irq_state_t state = rtos_critical_enter();
    const uint32_t now = rtos_tick_get();
    rtos_sched_block_current(now + ticks, now);
    rtos_critical_exit(state);
    /* The real port resumes this C stack only after its task is selected. */
    return RTOS_OK;
}

rtos_status_t rtos_delay_until(uint32_t *previous_release_tick,
                              uint32_t period_ticks)
{
    const rtos_status_t context = delay_call_context();
    if (context != RTOS_OK) {
        return context;
    }
    if ((previous_release_tick == NULL) || (period_ticks == 0U) ||
        (period_ticks > RTOS_DELAY_MAX_TICKS)) {
        return RTOS_ERR_ARGUMENT;
    }

    const rtos_irq_state_t state = rtos_critical_enter();
    const uint32_t now = rtos_tick_get();
    const uint32_t previous = *previous_release_tick;
    const uint32_t elapsed = now - previous;
    if (elapsed > RTOS_DELAY_MAX_TICKS) {
        rtos_critical_exit(state);
        return RTOS_ERR_ARGUMENT;
    }

    const uint32_t next = previous + period_ticks;
    /* Publish only after validating every argument, even for a missed release. */
    *previous_release_tick = next;
    if (period_ticks > elapsed) {
        rtos_sched_block_current(next, now);
    }
    rtos_critical_exit(state);
    return RTOS_OK;
}

void rtos_delay_process_expired(uint32_t now)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    rtos_task_t *task = rtos_delay_queue_peek_due(now);
    while (task != NULL) {
        if (task->wait_reason == RTOS_WAIT_SEMAPHORE || task->wait_reason == RTOS_WAIT_MUTEX ||
            task->wait_reason == RTOS_WAIT_QUEUE_SEND || task->wait_reason == RTOS_WAIT_QUEUE_RECEIVE) {
            RTOS_ASSERT(rtos_wait_complete_locked(task, RTOS_WAIT_RESULT_TIMEOUT));
        } else {
            RTOS_ASSERT(rtos_delay_queue_pop_due(now) == task);
            RTOS_TRACE_DELAY_EXPIRED(task);
            rtos_sched_wake(task);
        }
        task = rtos_delay_queue_peek_due(now);
    }
}
