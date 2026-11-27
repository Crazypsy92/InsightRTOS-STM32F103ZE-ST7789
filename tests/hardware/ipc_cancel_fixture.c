#include "ipc_cancel_fixture.h"
#include "main.h"
#include "rtos.h"
#include "rtos_diag.h"
#include <stdalign.h>

enum { LOW, MEDIUM, HIGH, OBSERVER, TASKS };
enum { SEND, RECEIVE, QUEUES };
enum { STACK_WORDS = 4096U / sizeof(uint32_t), TRACE_CAP = 80U };
#define SENTINEL UINT32_C(0x1234abcd)
#define SOURCE UINT32_C(0x3210abba)
#define MUTATED UINT32_C(0x6543abba)
#define OUTPUT UINT32_C(0xdead1978)
#define REPLAY UINT32_C(0x77aabbff)

typedef struct {
    uint32_t tick, count, head, tail, stored, sender, receiver;
    uint32_t state, reason, result, buffer, links;
} queue_view_t;

typedef struct {
    uint32_t tick, owner[2], waiter[2], owner_next[2];
    rtos_task_snapshot_t tasks[3];
    rtos_diag_dl_workspace_t graph;
} mutex_view_t;

typedef struct {
    uint32_t count;
    rtos_trace_record_t records[TRACE_CAP];
} trace_window_t;

typedef struct {
    queue_view_t before, ready, final;
    uint32_t payload_before, payload_after, received;
    int32_t cancel, wait, pull, empty, replay_send;
    uint32_t visible_stop, running, ipsr;
} queue_case_t;

volatile struct {
    uint32_t state, start, end;
    rtos_task_ref_t refs[TASKS];
    mutex_view_t mutex[4];
    int32_t mutex_cancel[2], mutex_wait[2], unlock[2];
    uint32_t high_stop, mutex_running[2], mutex_ipsr[2];
    queue_case_t queue[QUEUES];
    trace_window_t trace[3];
} g_ipc_cancel_fixture;

static rtos_mutex_t a, b;
static rtos_queue_t queues[QUEUES];
static rtos_semaphore_t gate[TASKS - 1U], unlock_low, queue_start[QUEUES];
static uint32_t store[QUEUES], source = SOURCE, output = OUTPUT;
static rtos_task_ref_t refs[TASKS];
static const rtos_task_t *volatile live[TASKS - 1U];

__attribute__((section(".user_stacks"), used))
alignas(8) static uint32_t display_stacks[RTOS_MAX_TASKS][STACK_WORDS];

static void wait_gate(rtos_semaphore_t *sem)
{
    RTOS_ASSERT(rtos_sem_take(sem, 100U) == RTOS_OK);
}

static void step(rtos_semaphore_t *sem)
{
    RTOS_ASSERT(rtos_sem_give(sem) == RTOS_OK);
    RTOS_ASSERT(rtos_delay(1U) == RTOS_OK);
}

static uint32_t id(const rtos_task_t *task)
{
    return task == NULL ? 0U : task->id;
}

static rtos_task_snapshot_t snapshot(uint32_t who)
{
    rtos_task_snapshot_t result;
    RTOS_ASSERT(rtos_task_get_snapshot(refs[who], &result) == RTOS_OK);
    return result;
}

static bool stop_seen(void)
{
    bool value;
    RTOS_ASSERT(rtos_task_stop_requested(&value) == RTOS_OK);
    return value;
}

static void enter(uint32_t who)
{
    rtos_task_ref_t self;
    RTOS_ASSERT(rtos_task_self_ref(&self) == RTOS_OK && self.slot == refs[who].slot &&
                self.generation == refs[who].generation);
    live[who] = rtos_task_current();
    wait_gate(&gate[who]);
}

static void queue_worker(uint32_t which, uint32_t who)
{
    wait_gate(&queue_start[which]);
    volatile queue_case_t *const out = &g_ipc_cancel_fixture.queue[which];
    if (which == SEND) {
        out->wait = rtos_queue_send(&queues[SEND], &source, RTOS_WAIT_FOREVER);
        source = MUTATED;
    } else {
        out->wait = rtos_queue_receive(&queues[RECEIVE], &output, RTOS_WAIT_FOREVER);
    }
    out->visible_stop = stop_seen();
    out->running = snapshot(who).state;
    out->ipsr = __get_IPSR();
    RTOS_ASSERT(out->wait == RTOS_ERR_CANCELLED && out->visible_stop == (which == SEND) &&
                out->running == RTOS_TASK_RUNNING && out->ipsr == 0U);
    rtos_task_exit();
}

static void low(void *unused)
{
    (void)unused;
    enter(LOW);
    RTOS_ASSERT(rtos_mutex_lock(&a, RTOS_NO_WAIT) == RTOS_OK);
    wait_gate(&unlock_low);
    g_ipc_cancel_fixture.unlock[0] = rtos_mutex_unlock(&a);
    RTOS_ASSERT(g_ipc_cancel_fixture.unlock[0] == RTOS_OK);
    queue_worker(SEND, LOW);
}

static void medium(void *unused)
{
    (void)unused;
    enter(MEDIUM);
    RTOS_ASSERT(rtos_mutex_lock(&b, RTOS_NO_WAIT) == RTOS_OK);
    g_ipc_cancel_fixture.mutex_wait[1] = rtos_mutex_lock(&a, 100U);
    g_ipc_cancel_fixture.mutex_running[1] = snapshot(MEDIUM).state;
    g_ipc_cancel_fixture.mutex_ipsr[1] = __get_IPSR();
    RTOS_ASSERT(g_ipc_cancel_fixture.mutex_wait[1] == RTOS_ERR_CANCELLED && !stop_seen());
    g_ipc_cancel_fixture.unlock[1] = rtos_mutex_unlock(&b);
    RTOS_ASSERT(g_ipc_cancel_fixture.unlock[1] == RTOS_OK);
    queue_worker(RECEIVE, MEDIUM);
}

static void high(void *unused)
{
    (void)unused;
    enter(HIGH);
    g_ipc_cancel_fixture.mutex_wait[0] = rtos_mutex_lock(&b, 100U);
    g_ipc_cancel_fixture.mutex_running[0] = snapshot(HIGH).state;
    g_ipc_cancel_fixture.mutex_ipsr[0] = __get_IPSR();
    g_ipc_cancel_fixture.high_stop = stop_seen();
    RTOS_ASSERT(g_ipc_cancel_fixture.mutex_wait[0] == RTOS_ERR_CANCELLED &&
                g_ipc_cancel_fixture.high_stop);
    rtos_task_exit();
}

static void capture_mutex(uint32_t at, uint32_t low_prio, uint32_t medium_prio, uint32_t edges)
{
    volatile mutex_view_t *const out = &g_ipc_cancel_fixture.mutex[at];
    rtos_diag_dl_workspace_t graph;
    const rtos_irq_state_t irq = rtos_critical_enter();
    out->tick = rtos_tick_get();
    out->owner[0] = id(a.owner);
    out->owner[1] = id(b.owner);
    out->waiter[0] = id(a.wait_head);
    out->waiter[1] = id(b.wait_head);
    out->owner_next[0] = a.owner_next != NULL;
    out->owner_next[1] = b.owner_next != NULL;
    for (uint32_t i = LOW; i <= HIGH; ++i) out->tasks[i] = snapshot(i);
    rtos_critical_exit(irq);
    /* Observer remains higher priority than all fixture workers. */
    RTOS_ASSERT(rtos_diag_dl_capture(&graph) == RTOS_OK);
    out->graph = graph;
    RTOS_ASSERT(out->tasks[LOW].effective_priority == low_prio &&
                out->tasks[MEDIUM].effective_priority == medium_prio && graph.header.edge_count == edges &&
                out->waiter[0] == (edges >= 1U ? id(live[MEDIUM]) : 0U) &&
                out->waiter[1] == (edges == 2U ? id(live[HIGH]) : 0U));
    RTOS_ASSERT(out->owner[0] == (at < 3U ? id(live[LOW]) : 0U) &&
                out->owner[1] == (at < 3U ? id(live[MEDIUM]) : 0U) &&
                out->owner_next[0] == 0U && out->owner_next[1] == 0U);
}

static uint64_t cursor_now(void)
{
    rtos_diag_trace_info_t info;
    RTOS_ASSERT(rtos_diag_trace_info(&info) == RTOS_OK);
    return info.next_sequence;
}

static void trace_since(uint64_t cursor, uint32_t phase)
{
    volatile trace_window_t *const out = &g_ipc_cancel_fixture.trace[phase];
    const uint64_t end = cursor_now();
    RTOS_ASSERT(end > cursor && end - cursor <= RTOS_TRACE_CAPACITY);
    while (cursor < end) {
        rtos_trace_record_t batch[RTOS_TRACE_BATCH_MAX];
        rtos_diag_trace_batch_t meta;
        RTOS_ASSERT(rtos_diag_trace_read(cursor, batch, RTOS_TRACE_BATCH_MAX, &meta) == RTOS_OK &&
                    meta.copied > 0U && meta.oldest_sequence <= cursor && meta.producer_dropped == 0U);
        for (uint32_t i = 0; i < meta.copied; ++i) {
            if (batch[i].sequence >= end || batch[i].slot > HIGH) continue;
            const uint32_t index = out->count;
            RTOS_ASSERT(index < TRACE_CAP);
            out->records[index] = batch[i];
            out->count = index + 1U;
        }
        cursor += meta.copied;
    }
}

static queue_view_t capture_queue(uint32_t which, uint32_t worker)
{
    const rtos_irq_state_t irq = rtos_critical_enter();
    const rtos_queue_t *q = &queues[which];
    const rtos_task_t *t = live[worker];
    const uint32_t links = ((uint32_t)t->ready_queued << 0) | ((uint32_t)t->delay_queued << 1) |
        ((uint32_t)(t->next != NULL) << 2) | ((uint32_t)(t->delay_next != NULL) << 3) |
        ((uint32_t)(t->wait_next != NULL) << 4) | ((uint32_t)(t->wait_object != NULL) << 5) |
        ((uint32_t)(t->owned_mutexes != NULL) << 6);
    const queue_view_t result = {rtos_tick_get(), q->count, q->head, q->tail, store[which],
        id(q->send_wait_head), id(q->receive_wait_head), snapshot(worker).state,
        t->wait_reason, t->wait_result, (uintptr_t)t->wait_buffer.receive, links};
    rtos_critical_exit(irq);
    return result;
}

static void queue_case(uint32_t which, uint32_t worker)
{
    const uint64_t cursor = cursor_now();
    const bool send = which == SEND;
    rtos_queue_t *q = &queues[which];
    volatile queue_case_t *const out = &g_ipc_cancel_fixture.queue[which];
    const uint32_t sentinel = SENTINEL;
    if (send) RTOS_ASSERT(rtos_queue_send(q, &sentinel, RTOS_NO_WAIT) == RTOS_OK);
    out->payload_before = send ? source : output;
    step(&queue_start[which]);
    out->before = capture_queue(which, worker);
    RTOS_ASSERT(out->before.state == RTOS_TASK_BLOCKED &&
                out->before.reason == (send ? RTOS_WAIT_QUEUE_SEND : RTOS_WAIT_QUEUE_RECEIVE) &&
                out->before.result == RTOS_WAIT_RESULT_PENDING &&
                out->before.buffer == (uintptr_t)(send ? &source : &output) &&
                out->before.links == 32U && out->before.count == (uint32_t)send &&
                out->before.sender == (send ? id(live[worker]) : 0U) &&
                out->before.receiver == (send ? 0U : id(live[worker])));
    out->cancel = send ? rtos_task_request_stop(refs[worker]) : rtos_task_cancel_wait(refs[worker]);
    out->ready = capture_queue(which, worker);
    RTOS_ASSERT(out->cancel == RTOS_OK && out->ready.state == RTOS_TASK_READY &&
                out->ready.result == RTOS_WAIT_RESULT_CANCELLED && out->ready.reason == RTOS_WAIT_NONE &&
                out->ready.sender == 0U && out->ready.receiver == 0U && out->ready.buffer == 0U &&
                out->ready.count == out->before.count && out->ready.stored == out->before.stored &&
                out->ready.head == out->before.head && out->ready.tail == out->before.tail);
    RTOS_ASSERT(rtos_delay(1U) == RTOS_OK);
    out->final = capture_queue(which, worker);
    out->payload_after = send ? source : output;
    RTOS_ASSERT(out->final.state == RTOS_TASK_TERMINATED && out->final.reason == RTOS_WAIT_NONE &&
                out->final.result == RTOS_WAIT_RESULT_NONE && out->final.links == 0U &&
                out->final.buffer == 0U && out->final.count == out->before.count &&
                out->final.stored == out->before.stored &&
                out->payload_after == (send ? MUTATED : OUTPUT));
    const uint32_t replay = REPLAY;
    if (!send) out->replay_send = rtos_queue_send(q, &replay, RTOS_NO_WAIT);
    uint32_t pulled = 0U;
    out->pull = rtos_queue_receive(q, &pulled, RTOS_NO_WAIT);
    out->received = pulled;
    RTOS_ASSERT(out->pull == RTOS_OK && pulled == (send ? SENTINEL : REPLAY) &&
                (send || out->replay_send == RTOS_OK));
    out->empty = rtos_queue_receive(q, &pulled, RTOS_NO_WAIT);
    RTOS_ASSERT(out->empty == RTOS_ERR_UNAVAILABLE && q->count == 0U);
    trace_since(cursor, which + 1U);
}

static void observer(void *unused)
{
    (void)unused;
    g_ipc_cancel_fixture.start = rtos_tick_get();
    g_ipc_cancel_fixture.state = 1U;
    const uint64_t cursor = cursor_now();
    step(&gate[LOW]);
    step(&gate[MEDIUM]);
    step(&gate[HIGH]);
    capture_mutex(0U, 5U, 5U, 2U);
    g_ipc_cancel_fixture.mutex_cancel[0] = rtos_task_request_stop(refs[HIGH]);
    RTOS_ASSERT(g_ipc_cancel_fixture.mutex_cancel[0] == RTOS_OK);
    capture_mutex(1U, 4U, 4U, 1U);
    g_ipc_cancel_fixture.mutex_cancel[1] = rtos_task_cancel_wait(refs[MEDIUM]);
    RTOS_ASSERT(g_ipc_cancel_fixture.mutex_cancel[1] == RTOS_OK);
    capture_mutex(2U, 2U, 4U, 0U);
    step(&unlock_low);
    capture_mutex(3U, 2U, 4U, 0U);
    trace_since(cursor, 0U);
    queue_case(SEND, LOW);
    queue_case(RECEIVE, MEDIUM);
    g_ipc_cancel_fixture.end = rtos_tick_get();
    __DMB();
    g_ipc_cancel_fixture.state = 2U;
    rtos_task_exit();
}

_Noreturn void ipc_cancel_fixture_start(void)
{
    static const rtos_task_entry_t entry[] = {low, medium, high, observer};
    static const uint8_t priority[] = {2U, 4U, 5U, 6U};
    static const char *const names[] = {"Low", "Medium", "High", "Observer"};
    RTOS_ASSERT(rtos_mutex_init(&a) == RTOS_OK && rtos_mutex_init(&b) == RTOS_OK);
    RTOS_ASSERT(rtos_sem_init(&unlock_low, 0U) == RTOS_OK);
    for (uint32_t i = 0; i < QUEUES; ++i) {
        RTOS_ASSERT(rtos_queue_init(&queues[i], &store[i], sizeof(store[i]), sizeof(store[i]), 1U) == RTOS_OK);
        RTOS_ASSERT(rtos_sem_init(&queue_start[i], 0U) == RTOS_OK);
    }
    for (uint32_t i = 0; i < TASKS; ++i) {
        if (i != OBSERVER) RTOS_ASSERT(rtos_sem_init(&gate[i], 0U) == RTOS_OK);
        RTOS_ASSERT(rtos_task_create_with_ref(&refs[i], names[i], entry[i], NULL,
                    display_stacks[i], STACK_WORDS, priority[i]) == RTOS_OK);
        g_ipc_cancel_fixture.refs[i] = refs[i];
    }
    rtos_start();
}
