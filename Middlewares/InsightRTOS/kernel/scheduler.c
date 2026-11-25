#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "delay_queue.h"
#include "diag_rt_internal.h"
#include "kernel_internal.h"
#include "port.h"
#include "ready_queue.h"
#include "rtos.h"
#if defined(INSIGHT_TEST_MSP) && INSIGHT_TEST_MSP
#include "msp_bench.h"
#endif

rtos_task_t *rtos_current_task;

static bool scheduler_started;
static uint32_t pending_reasons;
static rtos_task_t idle_task;
alignas(RTOS_STACK_ALIGNMENT) static uint32_t idle_stack[RTOS_MIN_STACK_WORDS];

static void idle_entry(void *arg)
{
    (void)arg;
    rtos_port_idle();
}

bool rtos_sched_started(void)
{
    return scheduler_started;
}

bool rtos_sched_stack_overlaps(uintptr_t begin, uintptr_t end)
{
    const uintptr_t idle_begin = (uintptr_t)idle_stack;
    const uintptr_t idle_end = idle_begin + sizeof(idle_stack);
    return (begin < idle_end) && (idle_begin < end);
}

static void idle_prepare(void)
{
    /* Avoid aggregate initialization that may require a freestanding runtime
     * memset helper once the TCB grows beyond inline-copy thresholds. */
    idle_task.sp = rtos_port_stack_init(idle_stack, RTOS_MIN_STACK_WORDS,
                                      idle_entry, NULL);
    idle_task.id = RTOS_IDLE_TASK_ID;
    idle_task.name = "Idle";
    idle_task.stack_base = idle_stack;
    idle_task.stack_words = RTOS_MIN_STACK_WORDS;
    idle_task.base_priority = 0U;
    idle_task.effective_priority = 0U;
    idle_task.state = RTOS_TASK_READY;
    idle_task.entry = idle_entry;
    idle_task.arg = NULL;
    idle_task.next = NULL;
    idle_task.slice_remaining = RTOS_TIME_SLICE_TICKS;
    idle_task.delay_next = NULL;
    idle_task.wake_tick = 0U;
    idle_task.delay_queued = false;
    idle_task.ready_queued = false;
    idle_task.wait_next = NULL;
    idle_task.wait_object = NULL;
    idle_task.wait_reason = RTOS_WAIT_NONE;
    idle_task.wait_result = RTOS_WAIT_RESULT_NONE;
    idle_task.owned_mutexes = NULL;
    idle_task.wait_buffer.receive = NULL;
    RTOS_ASSERT(rtos_task_stack_valid(&idle_task));
}

#if defined(INSIGHT_TEST_TIMING)
#include "timing_bench.h"

static __attribute__((noinline)) void sched_switch_body(void);

void rtos_sched_switch(void)
{
    if (timing_bench_running() && rtos_port_in_pendsv()) {
        const uint32_t start = timing_bench_stamp();
        sched_switch_body();
        const uint32_t elapsed = timing_bench_stamp() - start;
        const uint32_t id = rtos_current_task->id;
        timing_bench_record_switch(elapsed, id == RTOS_IDLE_TASK_ID ? 0U : id);
        return;
    }
    sched_switch_body();
}

static void sched_switch_body(void)
#else
void rtos_sched_switch(void)
#endif
{
#if defined(INSIGHT_TEST_MSP) && INSIGHT_TEST_MSP
    msp_bench_irq_entry();
#endif
    RTOS_ASSERT(scheduler_started);
    RTOS_ASSERT(rtos_port_interrupts_masked());

    rtos_task_t *const previous = rtos_current_task;
    const bool prev_was_running = (previous != NULL && previous->state == RTOS_TASK_RUNNING);
    uint32_t reasons = pending_reasons;
    pending_reasons = 0U;
    if (previous == NULL) {
        reasons |= RTOS_SCHED_REASON_START;
    } else {
        RTOS_ASSERT(rtos_task_stack_valid(previous));
        RTOS_ASSERT((previous->state == RTOS_TASK_RUNNING) ||
                    (previous->state == RTOS_TASK_TERMINATED) ||
                    (previous->state == RTOS_TASK_BLOCKED) ||
                    (previous->state == RTOS_TASK_READY));
        if (previous->state == RTOS_TASK_TERMINATED) {
            RTOS_ASSERT(!previous->ready_queued && !previous->delay_queued);
            reasons |= RTOS_SCHED_REASON_EXIT;
        } else if (previous->state == RTOS_TASK_BLOCKED) {
            RTOS_ASSERT(previous != &idle_task);
            const bool object_wait = previous->wait_reason == RTOS_WAIT_SEMAPHORE ||
                                     previous->wait_reason == RTOS_WAIT_MUTEX ||
                                     previous->wait_reason == RTOS_WAIT_QUEUE_SEND ||
                                     previous->wait_reason == RTOS_WAIT_QUEUE_RECEIVE;
            RTOS_ASSERT(!previous->ready_queued);
            if (object_wait) {
                RTOS_ASSERT(previous->wait_object != NULL);
                RTOS_ASSERT(previous->wait_result == RTOS_WAIT_RESULT_PENDING);
                RTOS_ASSERT(previous->delay_queued || previous->delay_next == NULL);
                if (previous->wait_reason == RTOS_WAIT_QUEUE_SEND || previous->wait_reason == RTOS_WAIT_QUEUE_RECEIVE) {
                    RTOS_ASSERT(previous->wait_buffer.receive != NULL);
                    RTOS_ASSERT(previous->delay_queued || previous->delay_next == NULL);
                } else RTOS_ASSERT(previous->wait_buffer.receive == NULL);
            } else {
                RTOS_ASSERT(previous->delay_queued);
                RTOS_ASSERT(previous->wait_object == NULL);
                RTOS_ASSERT(previous->wait_reason == RTOS_WAIT_NONE);
                RTOS_ASSERT(previous->wait_next == NULL);
                RTOS_ASSERT(previous->wait_result == RTOS_WAIT_RESULT_NONE);
            }
            reasons |= RTOS_SCHED_REASON_BLOCK;
        } else if (previous->state == RTOS_TASK_READY) {
            /* A wake can publish READY before the first PendSV saves this
             * blocked task (e.g. a one-tick deadline). Do not insert its node
             * again; the authoritative ready queue also handles early grants. */
            RTOS_ASSERT(previous != &idle_task);
            RTOS_ASSERT(previous->ready_queued && !previous->delay_queued);
            RTOS_ASSERT((reasons & RTOS_SCHED_REASON_BLOCK) != 0U);
        } else {
            RTOS_ASSERT(!previous->ready_queued && !previous->delay_queued);
        }
#if RTOS_PREEMPTIVE
        if (reasons == 0U) {
            /* An already serviced/coalesced hardware PendSV is harmless. */
            return;
        }
#else
        if (reasons == 0U) {
            /* Keep the first-stage raw-PendSV register probe operational. */
            reasons = RTOS_SCHED_REASON_YIELD;
        }
#endif
        if (previous->state == RTOS_TASK_RUNNING) {
            previous->state = RTOS_TASK_READY; RTOS_TRACE(RTOS_TRACE_TASK_READY, previous, 0U, 0U); rtos_diag_rt_task_ready_hook(previous);
            if (previous != &idle_task) {
                if ((reasons & (RTOS_SCHED_REASON_YIELD |
                                RTOS_SCHED_REASON_SLICE)) != 0U) {
                    previous->slice_remaining = RTOS_TIME_SLICE_TICKS;
                    rtos_ready_queue_push(previous);
                } else {
                    /* A higher priority interrupts this task's current turn.
                     * Keep FIFO position and residual budget until it resumes. */
                    rtos_ready_queue_push_front(previous);
                }
            }
        }
    }

    rtos_task_t *next = rtos_ready_queue_pop();
    if (next == NULL) {
        next = &idle_task;
    }
    if ((previous == &idle_task) || (next == &idle_task)) {
        reasons |= RTOS_SCHED_REASON_IDLE;
    }
    RTOS_ASSERT(next->state == RTOS_TASK_READY);
    RTOS_ASSERT(!next->ready_queued && !next->delay_queued);
    RTOS_ASSERT((next->next == NULL) && (next->delay_next == NULL));
    RTOS_ASSERT(next->wait_object == NULL && next->wait_next == NULL);
    RTOS_ASSERT(next->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(next->wait_buffer.receive == NULL);
    RTOS_ASSERT(next->wait_result != RTOS_WAIT_RESULT_PENDING);
    RTOS_ASSERT(rtos_task_stack_valid(next));
    next->state = RTOS_TASK_RUNNING;
    rtos_current_task = next; RTOS_TRACE_SELECT(previous, next, reasons);
    RTOS_TRACE_CONTEXT_SWITCH(previous, next, reasons, prev_was_running);
    rtos_diag_rt_switch_hook(previous, next);
#if RTOS_TESTING
    if (previous != next) {
        rtos_test_switch_hook(previous, next, reasons, rtos_tick_get());
    }
#endif
}

void rtos_sched_request(uint32_t reasons)
{
    RTOS_ASSERT(scheduler_started);
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(reasons != 0U);
    if ((reasons & RTOS_SCHED_REASON_BLOCK) != 0U) { RTOS_TRACE(RTOS_TRACE_TASK_BLOCK, rtos_current_task, rtos_current_task->wait_reason, rtos_current_task->wake_tick); } pending_reasons |= reasons;
    /* This only requests an exception: actual switching waits for IRQ unlock. */
    rtos_port_yield();
}

void rtos_sched_task_ready(rtos_task_t *task)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(task != NULL);
    RTOS_ASSERT(task->state == RTOS_TASK_READY);
    RTOS_ASSERT(task->ready_queued && !task->delay_queued);
#if RTOS_PREEMPTIVE
    if (scheduler_started && (rtos_current_task != NULL) &&
        ((rtos_current_task == &idle_task) ||
         (rtos_current_task->state != RTOS_TASK_RUNNING) ||
         (task->effective_priority > rtos_current_task->effective_priority))) {
        rtos_sched_request(RTOS_SCHED_REASON_HIGHER);
    }
#endif
}

void rtos_sched_block_current(uint32_t deadline, uint32_t now)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(scheduler_started);
    rtos_task_t *const task = rtos_current_task;
    RTOS_ASSERT(task != NULL);
    RTOS_ASSERT(task != &idle_task);
    RTOS_ASSERT(task->state == RTOS_TASK_RUNNING);
    RTOS_ASSERT(!task->ready_queued && !task->delay_queued);
    RTOS_ASSERT((task->next == NULL) && (task->delay_next == NULL));
    RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL);
    RTOS_ASSERT(task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(task->wait_buffer.receive == NULL);
    RTOS_ASSERT(task->wait_result == RTOS_WAIT_RESULT_NONE);
    const uint32_t distance = deadline - now;
    RTOS_ASSERT((distance > 0U) && (distance <= RTOS_DELAY_MAX_TICKS));

    task->wake_tick = deadline;
    task->slice_remaining = RTOS_TIME_SLICE_TICKS;
    task->state = RTOS_TASK_BLOCKED;
    rtos_delay_queue_insert(task, now);
    rtos_sched_request(RTOS_SCHED_REASON_BLOCK);
}

void rtos_sched_wake(rtos_task_t *task)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(scheduler_started);
    RTOS_ASSERT(task != NULL);
    RTOS_ASSERT(task != &idle_task);
    RTOS_ASSERT(task->state == RTOS_TASK_BLOCKED);
    /* The wait node is removed before publishing READY. */
    RTOS_ASSERT(task->wait_result != RTOS_WAIT_RESULT_PENDING);
    RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL);
    RTOS_ASSERT(task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(task->wait_buffer.receive == NULL);
    RTOS_ASSERT(!task->delay_queued && !task->ready_queued);
    RTOS_ASSERT((task->next == NULL) && (task->delay_next == NULL));
    task->state = RTOS_TASK_READY; RTOS_TRACE(RTOS_TRACE_TASK_READY, task, 0U, 0U); rtos_diag_rt_task_ready_hook(task);
    rtos_ready_queue_push(task);

    RTOS_ASSERT(rtos_current_task != NULL);
    const bool needs_cpu = (rtos_current_task == &idle_task) ||
                          (rtos_current_task->state != RTOS_TASK_RUNNING);
#if RTOS_PREEMPTIVE
    if (needs_cpu || (task->effective_priority > rtos_current_task->effective_priority)) {
        rtos_sched_request(RTOS_SCHED_REASON_WAKE | RTOS_SCHED_REASON_HIGHER);
    }
#else
    if (needs_cpu) {
        rtos_sched_request(RTOS_SCHED_REASON_WAKE);
    }
#endif
}

void rtos_sched_on_tick(void)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    rtos_diag_rt_on_tick(rtos_tick_get());
#if RTOS_PREEMPTIVE
    rtos_task_t *const current = rtos_current_task;
    if ((current == NULL) || (current == &idle_task) ||
        (current->state != RTOS_TASK_RUNNING)) {
        return;
    }
    if (!rtos_ready_queue_has_priority(current->effective_priority)) {
        /* A sole runnable task at this priority needs no PendSV or self-switch. */
        current->slice_remaining = RTOS_TIME_SLICE_TICKS;
        return;
    }
    if (current->slice_remaining > 0U) {
        --current->slice_remaining;
    }
    if (current->slice_remaining == 0U) {
        rtos_sched_request(RTOS_SCHED_REASON_SLICE);
    }
#endif
}

_Noreturn void rtos_start(void)
{
    RTOS_ASSERT(!rtos_port_in_handler());
    RTOS_ASSERT(!rtos_port_interrupts_masked());
    RTOS_ASSERT(!scheduler_started);
    RTOS_ASSERT(rtos_current_task == NULL);

    const rtos_irq_state_t irq_state = rtos_critical_enter();
    idle_prepare();
    rtos_diag_rt_init();
    rtos_port_init();
    scheduler_started = true;
    rtos_sched_switch();
    rtos_critical_exit(irq_state);
    rtos_port_start();
}

void rtos_yield(void)
{
    RTOS_ASSERT(!rtos_port_in_handler());
    RTOS_ASSERT(!rtos_port_interrupts_masked());
    RTOS_ASSERT(scheduler_started);
    RTOS_ASSERT(rtos_current_task != NULL);
    RTOS_ASSERT(rtos_current_task->state == RTOS_TASK_RUNNING);
    const rtos_irq_state_t irq_state = rtos_critical_enter();
    RTOS_TRACE_YIELD(rtos_current_task);
    rtos_sched_request(RTOS_SCHED_REASON_YIELD);
    rtos_critical_exit(irq_state);
}

void rtos_sched_reconsider(void)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
#if RTOS_PREEMPTIVE
    if (!scheduler_started || rtos_current_task == NULL) return;
    const int highest = rtos_ready_queue_highest_priority();
    if (highest >= 0 && (rtos_current_task->state != RTOS_TASK_RUNNING ||
        rtos_current_task->id == RTOS_IDLE_TASK_ID || highest > (int)rtos_current_task->effective_priority)) {
        rtos_sched_request(RTOS_SCHED_REASON_HIGHER);
    }
#endif
}
