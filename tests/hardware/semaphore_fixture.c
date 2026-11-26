#include "semaphore_fixture.h"
#include "main.h"
#include "rtos.h"
#include "rtos_diag.h"
#include <stdalign.h>
extern volatile uint32_t g_sys_tick;

enum worker { LOW, HIGH, PEER, OBSERVER, TASKS };
enum { IRQ_ROUNDS = 6, CHECK_CAPACITY = 24, STOP = UINT32_MAX };

typedef struct {
    uint32_t ipsr, tim2_ms, tick, due_tick, count_before, count_after;
    uint32_t waiter_before, state_before, state_after, waiter_after, pendsv_pending;
    uint32_t take_before, take_after, step_snapshot;
    int32_t give;
} irq_observation_t;

typedef struct {
    uint32_t worker, tick, ipsr, irq_samples, count;
    int32_t take;
} take_observation_t;

typedef struct {
    uint32_t code, count;
} count_observation_t;

typedef struct {
    uint32_t state, tick_start, tick_end, irq_samples, waiting_count;
    uint32_t priorities[TASKS], irq_take_index[IRQ_ROUNDS], fifo_first, fifo_head_before[3];
    rtos_task_ref_t refs[TASKS];
    irq_observation_t irq[IRQ_ROUNDS];
    rtos_trace_record_t irq_dispatch[IRQ_ROUNDS], fifo_dispatch[3];
    rtos_diag_explain_t irq_explain[IRQ_ROUNDS], fifo_explain[3];
    take_observation_t takes[16];
    uint32_t take_count, count_checks, timeout_index, timeout_due, cancelled_index;
    count_observation_t counts[CHECK_CAPACITY];
} semaphore_report_t;

volatile semaphore_report_t g_semaphore_fixture;
static rtos_semaphore_t tested, counting, gates[OBSERVER];
static rtos_task_ref_t refs[TASKS];
static const uint8_t priorities[TASKS] = {2U, 5U, 5U, 6U};
static const uint32_t worker_index[OBSERVER] = {LOW, HIGH, PEER};
static uint32_t requests[OBSERVER];
static volatile uint32_t irq_armed, irq_fire_tick;
static volatile uint32_t fixture_step;

__attribute__((section(".user_stacks"), used))
alignas(8) static uint32_t display_stacks[RTOS_MAX_TASKS][4096U / sizeof(uint32_t)];

static uint32_t waiter_id(const rtos_task_t *task)
{
    return task == NULL ? 0U : task->id;
}

void semaphore_fixture_tim2(void)
{
    if (!irq_armed) return;
    const uint32_t tick = rtos_tick_get();
    if (tick < irq_fire_tick) return;

    const uint32_t i = g_semaphore_fixture.irq_samples;
    irq_armed = 0U;
    RTOS_ASSERT(i < IRQ_ROUNDS && tested.wait_head != NULL);
    const rtos_task_t *const waiter = tested.wait_head;
    volatile irq_observation_t *const report = &g_semaphore_fixture.irq[i];
    report->ipsr = __get_IPSR();
    report->tick = tick;
    report->tim2_ms = g_sys_tick;
    report->due_tick = waiter->wake_tick;
    report->waiter_before = waiter_id(waiter);
    report->state_before = waiter->state;
    report->count_before = tested.count;
    report->take_before = g_semaphore_fixture.take_count;
    report->step_snapshot = fixture_step;
    report->give = rtos_sem_give_from_isr(&tested);
    report->take_after = g_semaphore_fixture.take_count;
    report->count_after = tested.count;
    report->state_after = waiter->state;
    report->waiter_after = waiter_id(tested.wait_head);
    report->pendsv_pending = !!(SCB->ICSR & SCB_ICSR_PENDSVSET_Msk);
    __DMB();
    g_semaphore_fixture.irq_samples = i + 1U;
}

static void worker_task(void *argument)
{
    const uint32_t who = *(const uint32_t *)argument;
    for (;;) {
        RTOS_ASSERT(rtos_sem_take(&gates[who], 500U) == RTOS_OK);
        const uint32_t timeout = requests[who];
        if (timeout == STOP) rtos_task_exit();
        const rtos_status_t result = rtos_sem_take(&tested, timeout);
        const uint32_t tick = rtos_tick_get();
        const rtos_irq_state_t saved = rtos_critical_enter();
        const uint32_t at = g_semaphore_fixture.take_count;
        RTOS_ASSERT(at < 16U);
        g_semaphore_fixture.takes[at] = (take_observation_t){
            who, tick, __get_IPSR(), g_semaphore_fixture.irq_samples, tested.count, result
        };
        requests[who] = 0U;
        g_semaphore_fixture.take_count = at + 1U;
        rtos_critical_exit(saved);
    }
}

static void advance(uint32_t ticks)
{
    RTOS_ASSERT(rtos_delay(ticks) == RTOS_OK);
}

static uint32_t block(enum worker who, uint32_t timeout)
{
    RTOS_ASSERT(requests[who] == 0U);
    requests[who] = timeout;
    RTOS_ASSERT(rtos_sem_give(&gates[who]) == RTOS_OK);
    advance(1U);
    const rtos_task_t *task = tested.wait_head;
    while (task != NULL && task->id != refs[who].slot + 1U) task = task->wait_next;
    RTOS_ASSERT(task != NULL && task->state == RTOS_TASK_BLOCKED &&
                task->wait_reason == RTOS_WAIT_SEMAPHORE && task->delay_queued);
    return task->wake_tick;
}

static uint64_t trace_cursor(void)
{
    rtos_diag_trace_info_t info;
    RTOS_ASSERT(rtos_diag_trace_info(&info) == RTOS_OK);
    return info.next_sequence;
}

static void dispatch_since(uint64_t cursor, enum worker who,
                           volatile rtos_trace_record_t *record,
                           volatile rtos_diag_explain_t *explanation)
{
    const uint64_t end = trace_cursor();
    RTOS_ASSERT(end - cursor <= RTOS_TRACE_CAPACITY);
    while (cursor < end) {
        rtos_trace_record_t batch[RTOS_TRACE_BATCH_MAX];
        rtos_diag_trace_batch_t meta;
        RTOS_ASSERT(rtos_diag_trace_read(cursor, batch, RTOS_TRACE_BATCH_MAX, &meta) == RTOS_OK &&
                    meta.copied > 0U);
        for (uint32_t i = 0U; i < meta.copied; ++i) {
            if (batch[i].event_type != RTOS_TRACE_CONTEXT_SWITCH || batch[i].slot != refs[who].slot)
                continue;
            rtos_diag_explain_t why;
            RTOS_ASSERT(rtos_diag_explain_record(&batch[i], &why) == RTOS_OK &&
                        !why.primary_stale && why.primary_generation == refs[who].generation);
            *record = batch[i];
            *explanation = why;
            return;
        }
        cursor += meta.copied;
    }
    RTOS_ASSERT(false);
}

static void take_result(uint32_t index, enum worker who, rtos_status_t expected)
{
    RTOS_ASSERT(g_semaphore_fixture.take_count == index + 1U);
    const volatile take_observation_t *take = &g_semaphore_fixture.takes[index];
    RTOS_ASSERT(take->worker == who && take->take == expected && take->ipsr == 0U);
}

static void counts(rtos_semaphore_t *sem, rtos_status_t actual, rtos_status_t expected, uint32_t count)
{
    const uint32_t i = g_semaphore_fixture.count_checks;
    RTOS_ASSERT(i < CHECK_CAPACITY && actual == expected && sem->count == count && sem->wait_head == NULL);
    g_semaphore_fixture.counts[i] = (count_observation_t){actual, count};
    g_semaphore_fixture.count_checks = i + 1U;
}

static void check_counts(void)
{
    counts(&counting, rtos_sem_take(&counting, RTOS_NO_WAIT), RTOS_OK, 0U);
    for (uint32_t n = 1U; n <= 3U; ++n) counts(&counting, rtos_sem_give(&counting), RTOS_OK, n);
    counts(&counting, rtos_sem_give(&counting), RTOS_ERR_FULL, 3U);
    for (uint32_t n = 3U; n > 0U; --n)
        counts(&counting, rtos_sem_take(&counting, RTOS_NO_WAIT), RTOS_OK, n - 1U);
    counts(&counting, rtos_sem_take(&counting, RTOS_NO_WAIT), RTOS_ERR_UNAVAILABLE, 0U);
    counts(&tested, rtos_sem_give_from_isr(&tested), RTOS_ERR_CONTEXT, 0U);
    counts(&tested, rtos_sem_give(&tested), RTOS_OK, 1U);
    counts(&tested, rtos_sem_give(&tested), RTOS_ERR_FULL, 1U);
    counts(&tested, rtos_sem_take(&tested, RTOS_NO_WAIT), RTOS_OK, 0U);
}

static void irq_round(uint32_t i)
{
    const uint32_t due = block(HIGH, i == IRQ_ROUNDS - 1U ? 5U : 15U);
    const uint32_t index = g_semaphore_fixture.take_count;
    g_semaphore_fixture.irq_take_index[i] = index;
    const uint64_t cursor = trace_cursor();
    irq_fire_tick = i == IRQ_ROUNDS - 1U ? due - 1U : rtos_tick_get() + 1U;
    __DMB();
    irq_armed = 1U;
    advance(irq_fire_tick - rtos_tick_get() + 1U);
    fixture_step = i + 1U;
    take_result(index, HIGH, RTOS_OK);
    dispatch_since(cursor, HIGH, &g_semaphore_fixture.irq_dispatch[i], &g_semaphore_fixture.irq_explain[i]);
    const volatile irq_observation_t *ob = &g_semaphore_fixture.irq[i];
    RTOS_ASSERT(g_semaphore_fixture.irq_samples == i + 1U && ob->ipsr == 16U + TIM2_IRQn &&
                ob->give == RTOS_OK && ob->tick == irq_fire_tick && ob->tick < due &&
                ob->due_tick == due && ob->pendsv_pending &&
                ob->state_before == RTOS_TASK_BLOCKED && ob->state_after == RTOS_TASK_READY &&
                ob->waiter_before == refs[HIGH].slot + 1U && ob->waiter_after == 0U &&
                ob->count_before == 0U && ob->count_after == 0U &&
                ob->take_before == index && ob->take_after == index &&
                g_semaphore_fixture.takes[index].irq_samples == i + 1U &&
                g_semaphore_fixture.irq_dispatch[i].tick == ob->tick &&
                g_semaphore_fixture.takes[index].tick == ob->tick);
    counts(&tested, rtos_sem_take(&tested, RTOS_NO_WAIT), RTOS_ERR_UNAVAILABLE, 0U);
}

static void check_fifo(void)
{
    (void)block(LOW, 30U);
    (void)block(HIGH, 30U);
    (void)block(PEER, 30U);
    const uint32_t first = g_semaphore_fixture.take_count;
    g_semaphore_fixture.fifo_first = first;
    const enum worker order[] = {HIGH, PEER, LOW};
    for (uint32_t i = 0U; i < 3U; ++i) {
        g_semaphore_fixture.fifo_head_before[i] = waiter_id(tested.wait_head);
        RTOS_ASSERT(g_semaphore_fixture.fifo_head_before[i] == refs[order[i]].slot + 1U);
        const uint64_t cursor = trace_cursor();
        RTOS_ASSERT(rtos_sem_give(&tested) == RTOS_OK && tested.count == 0U);
        rtos_task_snapshot_t snap;
        RTOS_ASSERT(rtos_task_get_snapshot(refs[order[i]], &snap) == RTOS_OK &&
                    snap.state == RTOS_TASK_READY && snap.effective_priority == priorities[order[i]]);
        advance(1U);
        take_result(first + i, order[i], RTOS_OK);
        dispatch_since(cursor, order[i], &g_semaphore_fixture.fifo_dispatch[i], &g_semaphore_fixture.fifo_explain[i]);
    }
    RTOS_ASSERT(tested.wait_head == NULL);
}

static void check_termination(void)
{
    uint32_t due = block(HIGH, 4U);
    uint32_t index = g_semaphore_fixture.take_count;
    g_semaphore_fixture.timeout_index = index;
    g_semaphore_fixture.timeout_due = due;
    advance(due - rtos_tick_get() + 1U);
    take_result(index, HIGH, RTOS_ERR_TIMEOUT);
    RTOS_ASSERT(g_semaphore_fixture.takes[index].tick == due && tested.wait_head == NULL);
    counts(&tested, rtos_sem_give(&tested), RTOS_OK, 1U);
    counts(&tested, rtos_sem_take(&tested, RTOS_NO_WAIT), RTOS_OK, 0U);

    (void)block(PEER, 20U);
    index = g_semaphore_fixture.take_count;
    g_semaphore_fixture.cancelled_index = index;
    RTOS_ASSERT(rtos_task_cancel_wait(refs[PEER]) == RTOS_OK);
    advance(1U);
    take_result(index, PEER, RTOS_ERR_CANCELLED);
    counts(&tested, rtos_sem_take(&tested, RTOS_NO_WAIT), RTOS_ERR_UNAVAILABLE, 0U);
}

static void observer_task(void *unused)
{
    (void)unused;
    g_semaphore_fixture.tick_start = rtos_tick_get();
    g_semaphore_fixture.state = 1U;
    check_counts();
    for (uint32_t i = 0U; i < IRQ_ROUNDS; ++i) irq_round(i);
    check_fifo();
    check_termination();
    RTOS_ASSERT(tested.wait_head == NULL && tested.count == 0U);
    g_semaphore_fixture.waiting_count = tested.count;
    for (uint32_t i = 0U; i < OBSERVER; ++i) {
        requests[i] = STOP;
        RTOS_ASSERT(rtos_sem_give(&gates[i]) == RTOS_OK);
    }
    advance(1U);
    g_semaphore_fixture.tick_end = rtos_tick_get();
    __DMB();
    g_semaphore_fixture.state = 2U;
    rtos_task_exit();
}

_Noreturn void semaphore_fixture_start(void)
{
    RTOS_ASSERT(rtos_sem_init(&tested, 0U) == RTOS_OK);
    RTOS_ASSERT(rtos_sem_init_counting(&counting, 1U, 3U) == RTOS_OK);
    static const char *const names[TASKS] = {"Low", "High", "Peer", "Observer"};
    for (uint32_t i = 0U; i < TASKS; ++i) {
        if (i < OBSERVER) RTOS_ASSERT(rtos_sem_init(&gates[i], 0U) == RTOS_OK);
        RTOS_ASSERT(rtos_task_create_with_ref(&refs[i], names[i],
                    i == OBSERVER ? observer_task : worker_task,
                    i == OBSERVER ? NULL : (void *)&worker_index[i],
                    display_stacks[i], 4096U / sizeof(uint32_t), priorities[i]) == RTOS_OK);
        g_semaphore_fixture.refs[i] = refs[i];
        g_semaphore_fixture.priorities[i] = priorities[i];
    }
    rtos_start();
}
