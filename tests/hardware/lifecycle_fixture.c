#include "lifecycle_fixture.h"
#include "main.h"
#include "rtos.h"
#include "rtos_diag.h"
#include <stdalign.h>

enum { CANCEL, GRANT, NATURAL_FIRST, NATURAL_SECOND, NATURAL_THIRD, DELAY, CASES };
enum { STACK_WORDS = 4096U / sizeof(uint32_t), EVENTS = 18U };

typedef struct {
    uint32_t state, reason, result, links, wake, count, waiter;
} observation_t;

typedef struct {
    rtos_task_ref_t ref, self;
    uint32_t stack, psp, control, ipsr, running, started, resumes, resumed, stop_initial, stop_seen;
    uint32_t ready, terminated, guard, trace_count;
    int32_t take, give, stop, stop_after_exit, old_snapshot, old_stop, no_permit;
    observation_t before, granted, after, before_due, clean;
    rtos_trace_record_t trace[EVENTS];
} case_report_t;

volatile struct {
    uint32_t state, start, end, completed;
    case_report_t cases[CASES];
} g_lifecycle_fixture;

static rtos_semaphore_t gate;
static const uint32_t scripts[CASES] = {CANCEL, GRANT, NATURAL_FIRST, NATURAL_SECOND, NATURAL_THIRD, DELAY};
/* Published by Worker and cleared by Observer before creating the next lifetime. */
static const rtos_task_t *volatile live_worker;

__attribute__((section(".user_stacks"), used))
alignas(8) static uint32_t display_stacks[RTOS_MAX_TASKS][STACK_WORDS];

static void pause_ticks(uint32_t ticks)
{
    RTOS_ASSERT(rtos_delay(ticks) == RTOS_OK);
}

static rtos_task_snapshot_t snapshot(rtos_task_ref_t ref)
{
    rtos_task_snapshot_t result;
    RTOS_ASSERT(rtos_task_get_snapshot(ref, &result) == RTOS_OK &&
                result.identity.slot == ref.slot && result.identity.generation == ref.generation &&
                result.base_priority == 5U && result.effective_priority == 5U);
    return result;
}

static observation_t observe(void)
{
    const rtos_irq_state_t irq = rtos_critical_enter();
    const rtos_task_t *const t = live_worker;
    RTOS_ASSERT(t != NULL);
    const uint32_t links =
        ((uint32_t)t->ready_queued << 0) | ((uint32_t)t->delay_queued << 1) |
        ((uint32_t)(t->next != NULL) << 2) | ((uint32_t)(t->delay_next != NULL) << 3) |
        ((uint32_t)(t->wait_next != NULL) << 4) | ((uint32_t)(t->wait_object != NULL) << 5) |
        ((uint32_t)(t->owned_mutexes != NULL) << 6) | ((uint32_t)(t->wait_buffer.receive != NULL) << 7);
    const observation_t result = {t->state, t->wait_reason, t->wait_result, links,
        t->wake_tick, gate.count, gate.wait_head != NULL ? gate.wait_head->id : 0U};
    rtos_critical_exit(irq);
    return result;
}

static void worker(void *argument)
{
    const uint32_t scenario = *(const uint32_t *)argument;
    volatile case_report_t *const out = &g_lifecycle_fixture.cases[scenario];
    rtos_task_ref_t self;
    RTOS_ASSERT(rtos_task_self_ref(&self) == RTOS_OK &&
                self.slot == out->ref.slot && self.generation == out->ref.generation);
    live_worker = rtos_task_current();
    out->self = self;
    out->stack = (uintptr_t)live_worker->stack_base;
    out->psp = __get_PSP();
    out->control = __get_CONTROL();
    out->ipsr = __get_IPSR();
    RTOS_ASSERT(out->ipsr == 0U && (out->control & 2U) != 0U &&
                out->psp > out->stack && out->psp <= out->stack + 4096U);
    out->running = snapshot(self).state;
    out->started = rtos_tick_get();
    bool stop;
    RTOS_ASSERT(rtos_task_stop_requested(&stop) == RTOS_OK && !stop);
    out->stop_initial = stop;
    if (scenario <= GRANT) {
        out->take = rtos_sem_take(&gate, 20U);
        RTOS_ASSERT(out->take == (scenario == GRANT ? RTOS_OK : RTOS_ERR_CANCELLED));
    } else if (scenario == DELAY) {
        pause_ticks(8U);
    }
    ++out->resumes;
    out->resumed = rtos_tick_get();
    RTOS_ASSERT(rtos_task_stop_requested(&stop) == RTOS_OK);
    out->stop_seen = stop;
    RTOS_ASSERT(stop == (scenario <= GRANT || scenario == DELAY));
    rtos_task_exit();
}

static uint64_t cursor_now(void)
{
    rtos_diag_trace_info_t info;
    RTOS_ASSERT(rtos_diag_trace_info(&info) == RTOS_OK);
    return info.next_sequence;
}

static void collect(uint64_t cursor, rtos_task_ref_t ref, volatile case_report_t *out)
{
    const uint64_t end = cursor_now();
    RTOS_ASSERT(end > cursor && end - cursor <= RTOS_TRACE_CAPACITY);
    while (cursor < end) {
        rtos_trace_record_t batch[RTOS_TRACE_BATCH_MAX];
        rtos_diag_trace_batch_t meta;
        RTOS_ASSERT(rtos_diag_trace_read(cursor, batch, RTOS_TRACE_BATCH_MAX, &meta) == RTOS_OK &&
                    meta.copied > 0U && meta.producer_dropped == 0U && meta.oldest_sequence <= cursor);
        for (uint32_t i = 0U; i < meta.copied; ++i) {
            if (batch[i].slot != ref.slot || batch[i].sequence >= end) continue;
            const uint32_t at = out->trace_count;
            RTOS_ASSERT(at < EVENTS);
            out->trace[at] = batch[i];
            out->trace_count = at + 1U;
        }
        cursor += meta.copied;
    }
}

static void stop_case(uint32_t scenario, rtos_task_ref_t ref, volatile case_report_t *out)
{
    RTOS_ASSERT(snapshot(ref).state == RTOS_TASK_BLOCKED);
    out->before = observe();
    RTOS_ASSERT(out->before.state == RTOS_TASK_BLOCKED && out->resumes == 0U &&
                (out->before.links & 2U) != 0U && out->before.count == 0U);
    if (scenario <= GRANT) {
        RTOS_ASSERT(out->before.reason == RTOS_WAIT_SEMAPHORE &&
                    out->before.result == RTOS_WAIT_RESULT_PENDING &&
                    out->before.waiter == ref.slot + 1U);
    }
    if (scenario == GRANT) {
        out->give = rtos_sem_give(&gate);
        out->granted = observe();
        RTOS_ASSERT(out->give == RTOS_OK && out->granted.state == RTOS_TASK_READY &&
                    out->granted.result == RTOS_WAIT_RESULT_GRANTED && out->resumes == 0U);
    }
    out->stop = rtos_task_request_stop(ref);
    out->after = observe();
    RTOS_ASSERT(out->stop == RTOS_OK && out->resumes == 0U && out->after.waiter == 0U &&
                out->after.count == 0U);
    if (scenario == DELAY) {
        RTOS_ASSERT(out->after.state == RTOS_TASK_BLOCKED && out->after.wake == out->before.wake &&
                    out->after.result == RTOS_WAIT_RESULT_NONE);
        pause_ticks(2U);
        out->before_due = observe();
        RTOS_ASSERT(out->before_due.state == RTOS_TASK_BLOCKED && out->resumes == 0U);
        const uint32_t now = rtos_tick_get();
        RTOS_ASSERT(now < out->before.wake);
        pause_ticks(out->before.wake - now + 1U);
        RTOS_ASSERT(out->resumed == out->before.wake);
    } else {
        RTOS_ASSERT(out->after.state == RTOS_TASK_READY && out->after.reason == RTOS_WAIT_NONE &&
                    out->after.result == (scenario == GRANT ? RTOS_WAIT_RESULT_GRANTED :
                                                            RTOS_WAIT_RESULT_CANCELLED));
        pause_ticks(1U);
    }
}

static void observer(void *unused)
{
    (void)unused;
    rtos_task_ref_t previous = {0};
    g_lifecycle_fixture.start = rtos_tick_get();
    g_lifecycle_fixture.state = 1U;
    for (uint32_t i = 0U; i < CASES; ++i) {
        volatile case_report_t *const out = &g_lifecycle_fixture.cases[i];
        const uint64_t cursor = cursor_now();
        rtos_task_ref_t ref;
        RTOS_ASSERT(live_worker == NULL && rtos_task_create_with_ref(&ref, "Worker", worker,
                    (void *)&scripts[i], display_stacks[1], STACK_WORDS, 5U) == RTOS_OK);
        out->ref = ref;
        out->ready = snapshot(ref).state;
        RTOS_ASSERT(out->ready == RTOS_TASK_READY);
        if (previous.generation != 0U) {
            rtos_task_snapshot_t unchanged = {.identity = previous, .state = RTOS_TASK_UNUSED};
            RTOS_ASSERT(ref.slot == previous.slot && ref.generation == previous.generation + 1U);
            out->old_snapshot = rtos_task_get_snapshot(previous, &unchanged);
            out->old_stop = rtos_task_request_stop(previous);
            RTOS_ASSERT(out->old_snapshot == RTOS_ERR_STALE && out->old_stop == RTOS_ERR_STALE &&
                        unchanged.state == RTOS_TASK_UNUSED &&
                        unchanged.identity.slot == previous.slot &&
                        unchanged.identity.generation == previous.generation);
        }
        pause_ticks(1U);
        if (i <= GRANT || i == DELAY) stop_case(i, ref, out);
        out->terminated = snapshot(ref).state;
        RTOS_ASSERT(out->terminated == RTOS_TASK_TERMINATED && out->resumes == 1U);
        out->stop_after_exit = rtos_task_request_stop(ref);
        out->clean = observe();
        out->guard = display_stacks[1][0];
        out->no_permit = rtos_sem_take(&gate, RTOS_NO_WAIT);
        RTOS_ASSERT(out->stop_after_exit == RTOS_ERR_STATE && out->guard == RTOS_STACK_GUARD &&
                    out->clean.state == RTOS_TASK_TERMINATED && out->clean.links == 0U &&
                    out->clean.reason == RTOS_WAIT_NONE && out->clean.result == RTOS_WAIT_RESULT_NONE &&
                    out->clean.count == 0U && out->clean.waiter == 0U && out->no_permit == RTOS_ERR_UNAVAILABLE);
        collect(cursor, ref, out);
        live_worker = NULL;
        previous = ref;
        g_lifecycle_fixture.completed = i + 1U;
    }
    g_lifecycle_fixture.end = rtos_tick_get();
    __DMB();
    g_lifecycle_fixture.state = 2U;
    rtos_task_exit();
}

_Noreturn void lifecycle_fixture_start(void)
{
    RTOS_ASSERT(rtos_sem_init(&gate, 0U) == RTOS_OK);
    rtos_task_ref_t ref;
    RTOS_ASSERT(rtos_task_create_with_ref(&ref, "Observer", observer, NULL,
                display_stacks[0], STACK_WORDS, 6U) == RTOS_OK);
    rtos_start();
}
