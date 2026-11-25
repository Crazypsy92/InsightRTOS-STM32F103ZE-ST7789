#include "mutex_fixture.h"
#include "main.h"
#include "rtos.h"
#include "rtos_diag.h"
#include <stdalign.h>

enum worker { LOW, MEDIUM, HIGH, PEER, OBSERVER, TASKS };
enum scenario { HANDOFF, CLASSIC_PI, TRANSITIVE_PI, ABBA, SCENARIOS };
enum operation {
    NONE, HOLD_A, HOLD_B, REJECT_A, RELEASE_A, MARK_MEDIUM, TAKE_A, TAKE_B,
    TAKE_A_RELEASE_B, TIMEOUT_B_RELEASE_A, TAKE_A_RELEASE_HELD_B
};

#define WAIT_TICKS 100U
#define REPEATS 100U
#define TRACE_SWITCHES 16U
#define ORDER_CAPACITY 16U

typedef struct {
    uint32_t operation, worker, tick;
} checkpoint_t;

typedef struct {
    uint32_t tick_resume, owner_when_granted, release_count;
    int32_t initial, repeated, not_owner, no_wait, wait, release;
} worker_result_t;

typedef struct {
    rtos_diag_dl_workspace_t graph;
    rtos_task_snapshot_t ready[4], final[4];
    rtos_trace_record_t switches[TRACE_SWITCHES];
    rtos_diag_explain_t explanations[TRACE_SWITCHES];
    checkpoint_t order[ORDER_CAPACITY];
    worker_result_t workers[4];
    uint32_t switches_count, order_count, pass_rounds;
    uint32_t owner_a, owner_b, waiter_a, waiter_b, timeout_a, timeout_b;
    uint32_t direct_owner, direct_state;
    int32_t owner_lock, owner_repeat, owner_unlock;
} case_report_t;

typedef struct {
    uint32_t state, scenario, round, start_tick, end_tick;
    uint32_t priorities[TASKS];
    rtos_task_ref_t refs[TASKS];
    case_report_t cases[SCENARIOS];
} fixture_report_t;

volatile fixture_report_t g_mutex_fixture;

static rtos_mutex_t mutex_a, mutex_b;
static rtos_semaphore_t gates[4];
static enum operation actions[4];
static rtos_task_ref_t refs[TASKS];

static const uint32_t worker_indices[4] = {LOW, MEDIUM, HIGH, PEER};
static const uint8_t priorities[TASKS] = {2, 4, 5, 5, 6};
__attribute__((section(".user_stacks"), used))
alignas(8) static uint32_t display_stacks[RTOS_MAX_TASKS][4096U / sizeof(uint32_t)];

static uint32_t owner_id(const rtos_mutex_t *mutex)
{
    return mutex->owner == NULL ? 0U : mutex->owner->id;
}

static void checkpoint(volatile case_report_t *report, enum operation op, uint32_t index)
{
    const rtos_irq_state_t saved = rtos_critical_enter();
    const uint32_t slot = report->order_count;
    RTOS_ASSERT(slot < ORDER_CAPACITY);
    report->order[slot] = (checkpoint_t){op, index, rtos_tick_get()};
    report->order_count = slot + 1U;
    rtos_critical_exit(saved);
}

static void release(volatile worker_result_t *result, rtos_mutex_t *mutex)
{
    result->release = rtos_mutex_unlock(mutex);
    RTOS_ASSERT(result->release == RTOS_OK);
    ++result->release_count;
}

static void take(volatile worker_result_t *result, rtos_mutex_t *mutex,
                 uint32_t index, uint32_t timeout, rtos_status_t expect)
{
    result->wait = rtos_mutex_lock(mutex, timeout);
    result->tick_resume = rtos_tick_get();
    RTOS_ASSERT(result->wait == expect);
    if (expect == RTOS_OK) {
        result->owner_when_granted = owner_id(mutex);
        RTOS_ASSERT(result->owner_when_granted == refs[index].slot + 1U);
    }
}

static void worker_task(void *argument)
{
    const uint32_t index = *(const uint32_t *)argument;
    for (;;) {
        RTOS_ASSERT(rtos_sem_take(&gates[index], RTOS_WAIT_FOREVER) == RTOS_OK);
        const enum operation op = actions[index];
        volatile case_report_t *const report = &g_mutex_fixture.cases[g_mutex_fixture.scenario];
        volatile worker_result_t *const result = &report->workers[index];
        switch (op) {
        case HOLD_A:
            result->initial = rtos_mutex_lock(&mutex_a, RTOS_NO_WAIT);
            RTOS_ASSERT(result->initial == RTOS_OK && owner_id(&mutex_a) == refs[index].slot + 1U);
            result->repeated = rtos_mutex_lock(&mutex_a, RTOS_NO_WAIT);
            RTOS_ASSERT(result->repeated == RTOS_ERR_STATE);
            break;
        case HOLD_B:
            result->initial = rtos_mutex_lock(&mutex_b, RTOS_NO_WAIT);
            RTOS_ASSERT(result->initial == RTOS_OK && owner_id(&mutex_b) == refs[index].slot + 1U);
            break;
        case REJECT_A:
            result->not_owner = rtos_mutex_unlock(&mutex_a);
            result->no_wait = rtos_mutex_lock(&mutex_a, RTOS_NO_WAIT);
            RTOS_ASSERT(result->not_owner == RTOS_ERR_NOT_OWNER &&
                        result->no_wait == RTOS_ERR_UNAVAILABLE);
            break;
        case RELEASE_A:
            checkpoint(report, op, index);
            release(result, &mutex_a);
            break;
        case MARK_MEDIUM:
            checkpoint(report, op, index);
            break;
        case TAKE_A:
            take(result, &mutex_a, index, WAIT_TICKS, RTOS_OK);
            checkpoint(report, op, index);
            release(result, &mutex_a);
            break;
        case TAKE_B:
            take(result, &mutex_b, index, WAIT_TICKS, RTOS_OK);
            checkpoint(report, op, index);
            release(result, &mutex_b);
            break;
        case TAKE_A_RELEASE_B:
        case TAKE_A_RELEASE_HELD_B:
            take(result, &mutex_a, index, op == TAKE_A_RELEASE_B ? WAIT_TICKS : 60U, RTOS_OK);
            checkpoint(report, op, index);
            release(result, &mutex_a);
            release(result, &mutex_b);
            break;
        case TIMEOUT_B_RELEASE_A:
            take(result, &mutex_b, index, 30U, RTOS_ERR_TIMEOUT);
            checkpoint(report, op, index);
            release(result, &mutex_a);
            break;
        default:
            RTOS_ASSERT(false);
            break;
        }
        actions[index] = NONE;
    }
}

static void step(void)
{
    RTOS_ASSERT(rtos_delay(1U) == RTOS_OK);
}

static void start_worker(enum worker who, enum operation op)
{
    RTOS_ASSERT(who < OBSERVER && actions[who] == NONE);
    actions[who] = op;
    RTOS_ASSERT(rtos_sem_give(&gates[who]) == RTOS_OK);
}

static void perform(enum worker who, enum operation op)
{
    start_worker(who, op);
    step();
    RTOS_ASSERT(actions[who] == NONE);
}

static void block_worker(enum worker who, enum operation op, rtos_mutex_t *mutex)
{
    start_worker(who, op);
    step();
    for (const rtos_task_t *waiter = mutex->wait_head; waiter; waiter = waiter->wait_next) {
        if (waiter->id == refs[who].slot + 1U) {
            RTOS_ASSERT(waiter->state == RTOS_TASK_BLOCKED && waiter->delay_queued);
            return;
        }
    }
    RTOS_ASSERT(false);
}

static void prepare(volatile case_report_t *report, enum scenario scenario, uint32_t round)
{
    RTOS_ASSERT(mutex_a.owner == NULL && mutex_b.owner == NULL &&
                mutex_a.wait_head == NULL && mutex_b.wait_head == NULL &&
                mutex_a.owner_next == NULL && mutex_b.owner_next == NULL);
    g_mutex_fixture.scenario = scenario;
    g_mutex_fixture.round = round;
    report->order_count = 0U;
    report->switches_count = 0U;
    for (uint32_t i = 0; i < OBSERVER; ++i) {
        RTOS_ASSERT(actions[i] == NONE);
        report->workers[i] = (worker_result_t){0};
    }
}

static void capture(volatile case_report_t *report, uint32_t edges)
{
    static rtos_diag_dl_workspace_t work;
    RTOS_ASSERT(rtos_diag_dl_capture(&work) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_dl_analyze(&work) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_dl_verify_current(&work) == RTOS_OK);
    RTOS_ASSERT(work.header.node_count == TASKS && work.header.edge_count == edges &&
                work.header.flags == 0U && work.header.captured && work.header.analyzed);
    report->graph = work;
}

static void check_edge(const volatile case_report_t *report, enum worker from, enum worker to)
{
    const volatile rtos_diag_dl_node_t *node = &report->graph.nodes[from];
    RTOS_ASSERT(node->slot == refs[from].slot && node->generation == refs[from].generation &&
                node->owner_slot == refs[to].slot && node->owner_generation == refs[to].generation &&
                node->state == RTOS_TASK_BLOCKED && (node->edge_flags & RTOS_DIAG_DL_EDGE_FINITE) != 0U &&
                node->resource_id != 0U);
}

static uint64_t trace_cursor(void)
{
    rtos_diag_trace_info_t info;
    RTOS_ASSERT(rtos_diag_trace_info(&info) == RTOS_OK);
    return info.next_sequence;
}

static void switches(volatile case_report_t *report, uint64_t cursor)
{
    const uint64_t stop = trace_cursor();
    RTOS_ASSERT(stop - cursor <= RTOS_TRACE_CAPACITY);
    while (cursor < stop) {
        rtos_trace_record_t records[RTOS_TRACE_BATCH_MAX];
        rtos_diag_trace_batch_t batch;
        RTOS_ASSERT(rtos_diag_trace_read(cursor, records, RTOS_TRACE_BATCH_MAX, &batch) == RTOS_OK);
        RTOS_ASSERT(batch.copied != 0U);
        for (uint32_t i = 0; i < batch.copied; ++i) {
            if (records[i].event_type != RTOS_TRACE_CONTEXT_SWITCH) continue;
            const uint32_t index = report->switches_count;
            RTOS_ASSERT(index < TRACE_SWITCHES);
            rtos_diag_explain_t why;
            RTOS_ASSERT(rtos_diag_explain_record(&records[i], &why) == RTOS_OK);
            report->switches[index] = records[i];
            report->explanations[index] = why;
            report->switches_count = index + 1U;
        }
        cursor += batch.copied;
    }
}

static void finish(volatile case_report_t *report)
{
    RTOS_ASSERT(mutex_a.owner == NULL && mutex_a.wait_head == NULL && mutex_a.owner_next == NULL &&
                mutex_b.owner == NULL && mutex_b.wait_head == NULL && mutex_b.owner_next == NULL);
    report->owner_a = owner_id(&mutex_a);
    report->owner_b = owner_id(&mutex_b);
    report->waiter_a = mutex_a.wait_head == NULL ? 0U : mutex_a.wait_head->id;
    report->waiter_b = mutex_b.wait_head == NULL ? 0U : mutex_b.wait_head->id;
    for (uint32_t i = 0; i < OBSERVER; ++i) {
        rtos_task_snapshot_t snap;
        RTOS_ASSERT(actions[i] == NONE && rtos_task_get_snapshot(refs[i], &snap) == RTOS_OK);
        RTOS_ASSERT(snap.base_priority == priorities[i] && snap.effective_priority == priorities[i]);
        report->final[i] = snap;
    }
    ++report->pass_rounds;
}

static void run_handoff(uint32_t round)
{
    volatile case_report_t *report = &g_mutex_fixture.cases[HANDOFF];
    prepare(report, HANDOFF, round);
    report->owner_lock = rtos_mutex_lock(&mutex_a, RTOS_NO_WAIT);
    report->owner_repeat = rtos_mutex_lock(&mutex_a, RTOS_NO_WAIT);
    RTOS_ASSERT(report->owner_lock == RTOS_OK && report->owner_repeat == RTOS_ERR_STATE);
    perform(HIGH, REJECT_A);
    RTOS_ASSERT(owner_id(&mutex_a) == refs[OBSERVER].slot + 1U);
    block_worker(MEDIUM, TAKE_A, &mutex_a);
    block_worker(HIGH, TAKE_A, &mutex_a);
    block_worker(PEER, TAKE_A, &mutex_a);
    capture(report, 3U);
    check_edge(report, HIGH, OBSERVER);
    check_edge(report, PEER, OBSERVER);
    check_edge(report, MEDIUM, OBSERVER);
    const uint64_t cursor = trace_cursor();
    report->owner_unlock = rtos_mutex_unlock(&mutex_a);
    report->direct_owner = owner_id(&mutex_a);
    rtos_task_snapshot_t successor;
    RTOS_ASSERT(rtos_task_get_snapshot(refs[HIGH], &successor) == RTOS_OK);
    report->direct_state = successor.state;
    RTOS_ASSERT(report->owner_unlock == RTOS_OK &&
                report->direct_owner == refs[HIGH].slot + 1U && successor.state == RTOS_TASK_READY);
    step();
    switches(report, cursor);
    RTOS_ASSERT(report->order_count == 3U && report->order[0].worker == HIGH &&
                report->order[1].worker == PEER && report->order[2].worker == MEDIUM);
    finish(report);
}

static uint32_t first_switch(const volatile case_report_t *report, enum worker who)
{
    for (uint32_t i = 0; i < report->switches_count; ++i)
        if (report->switches[i].slot == refs[who].slot) return i;
    return UINT32_MAX;
}

static void run_classic(uint32_t round)
{
    volatile case_report_t *report = &g_mutex_fixture.cases[CLASSIC_PI];
    prepare(report, CLASSIC_PI, round);
    perform(LOW, HOLD_A);
    block_worker(HIGH, TAKE_A, &mutex_a);
    capture(report, 1U);
    check_edge(report, HIGH, LOW);
    RTOS_ASSERT(report->graph.nodes[LOW].base_priority == priorities[LOW] &&
                report->graph.nodes[LOW].effective_priority == priorities[HIGH] &&
                (report->graph.nodes[LOW].edge_flags & RTOS_DIAG_DL_EDGE_PI_BOOST) != 0U);
    start_worker(MEDIUM, MARK_MEDIUM);
    start_worker(LOW, RELEASE_A);
    for (uint32_t i = 0; i < 2U; ++i) {
        rtos_task_snapshot_t snap;
        RTOS_ASSERT(rtos_task_get_snapshot(refs[i], &snap) == RTOS_OK && snap.state == RTOS_TASK_READY);
        report->ready[i] = snap;
    }
    const uint64_t cursor = trace_cursor();
    step();
    switches(report, cursor);
    RTOS_ASSERT(report->order_count == 3U && report->order[0].worker == LOW &&
                report->order[1].worker == HIGH && report->order[2].worker == MEDIUM);
    RTOS_ASSERT(first_switch(report, LOW) < first_switch(report, MEDIUM) &&
                first_switch(report, MEDIUM) != UINT32_MAX);
    finish(report);
}

static void run_transitive(void)
{
    volatile case_report_t *report = &g_mutex_fixture.cases[TRANSITIVE_PI];
    prepare(report, TRANSITIVE_PI, 1U);
    perform(LOW, HOLD_A);
    perform(MEDIUM, HOLD_B);
    block_worker(MEDIUM, TAKE_A_RELEASE_B, &mutex_a);
    rtos_task_snapshot_t before;
    RTOS_ASSERT(rtos_task_get_snapshot(refs[LOW], &before) == RTOS_OK &&
                before.effective_priority == priorities[MEDIUM]);
    report->ready[LOW] = before;
    block_worker(HIGH, TAKE_B, &mutex_b);
    capture(report, 2U);
    check_edge(report, MEDIUM, LOW);
    check_edge(report, HIGH, MEDIUM);
    RTOS_ASSERT(report->graph.nodes[LOW].effective_priority == priorities[HIGH] &&
                report->graph.nodes[MEDIUM].effective_priority == priorities[HIGH] &&
                (report->graph.nodes[LOW].edge_flags & RTOS_DIAG_DL_EDGE_PI_BOOST) != 0U &&
                (report->graph.nodes[MEDIUM].edge_flags & RTOS_DIAG_DL_EDGE_PI_BOOST) != 0U);
    const uint64_t cursor = trace_cursor();
    perform(LOW, RELEASE_A);
    switches(report, cursor);
    RTOS_ASSERT(report->order_count == 3U && report->order[1].worker == MEDIUM &&
                report->order[2].worker == HIGH);
    finish(report);
}

static void run_abba(void)
{
    volatile case_report_t *report = &g_mutex_fixture.cases[ABBA];
    prepare(report, ABBA, 1U);
    perform(LOW, HOLD_A);
    perform(MEDIUM, HOLD_B);
    block_worker(LOW, TIMEOUT_B_RELEASE_A, &mutex_b);
    block_worker(MEDIUM, TAKE_A_RELEASE_HELD_B, &mutex_a);
    report->timeout_a = mutex_b.wait_head->wake_tick; /* Low waits for B. */
    report->timeout_b = mutex_a.wait_head->wake_tick; /* Medium waits for A. */
    capture(report, 2U);
    check_edge(report, LOW, MEDIUM);
    check_edge(report, MEDIUM, LOW);
    const volatile rtos_diag_dl_result_t *result = &report->graph.result;
    RTOS_ASSERT(result->deadlock_detected && result->cycle_length == 2U &&
                result->finite_edge_count == 2U &&
                result->flags == (RTOS_DIAG_DL_RES_DEADLOCK | RTOS_DIAG_DL_RES_HAS_TIMEOUT) &&
                result->cycle_tasks[0] == refs[LOW].slot && result->cycle_tasks[1] == refs[MEDIUM].slot &&
                result->cycle_mutexes[0] == report->graph.nodes[LOW].resource_id &&
                result->cycle_mutexes[1] == report->graph.nodes[MEDIUM].resource_id &&
                result->earliest_due_tick == report->timeout_a &&
                report->graph.nodes[LOW].wake_tick == report->timeout_a &&
                report->graph.nodes[MEDIUM].wake_tick == report->timeout_b &&
                result->capture_tick < report->timeout_a && report->timeout_a < report->timeout_b);
    const uint64_t cursor = trace_cursor();
    RTOS_ASSERT(rtos_delay(report->timeout_a - rtos_tick_get()) == RTOS_OK);
    step();
    switches(report, cursor);
    RTOS_ASSERT(report->order_count == 2U && report->order[0].worker == LOW &&
                report->order[1].worker == MEDIUM &&
                report->workers[LOW].tick_resume == report->timeout_a &&
                report->workers[MEDIUM].tick_resume < report->timeout_b);
    finish(report);
}

static void observer_task(void *unused)
{
    (void)unused;
    g_mutex_fixture.start_tick = rtos_tick_get();
    g_mutex_fixture.state = 1U;
    for (uint32_t i = 1U; i <= REPEATS; ++i) {
        run_handoff(i);
        run_classic(i);
    }
    run_transitive();
    run_abba();
    g_mutex_fixture.end_tick = rtos_tick_get();
    __DMB();
    g_mutex_fixture.state = 2U;
    for (;;) RTOS_ASSERT(rtos_delay(100U) == RTOS_OK);
}

_Noreturn void mutex_fixture_start(void)
{
    RTOS_ASSERT(rtos_mutex_init(&mutex_a) == RTOS_OK && rtos_mutex_init(&mutex_b) == RTOS_OK);
    static const char *const names[TASKS] = {"Low", "Medium", "High", "Peer", "Observer"};
    for (uint32_t i = 0; i < TASKS; ++i) {
        if (i < OBSERVER) RTOS_ASSERT(rtos_sem_init(&gates[i], 0U) == RTOS_OK);
        /* The worker index belongs to immutable fixture data. */
        RTOS_ASSERT(rtos_task_create_with_ref(&refs[i], names[i],
                    i == OBSERVER ? observer_task : worker_task,
                    i == OBSERVER ? NULL : (void *)&worker_indices[i],
                    display_stacks[i], 4096U / sizeof(uint32_t), priorities[i]) == RTOS_OK);
        g_mutex_fixture.refs[i] = refs[i];
        g_mutex_fixture.priorities[i] = priorities[i];
        RTOS_ASSERT(refs[i].slot == i);
    }
    rtos_start();
}
