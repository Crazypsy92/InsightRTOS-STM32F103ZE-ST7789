#include <assert.h>
#include <setjmp.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

#include "app_ipc.h"
#include "kernel_internal.h"
#include "mock_port.h"
#include "port.h"
#include "ready_queue.h"
#include "rtos.h"
#include "rtos_critical.h"
#include "rtos_delay.h"
#include "rtos_diag.h"
#include "rtos_diag_rt.h"
#include "task_lifecycle_internal.h"

#define EXPECT_EVENT(expected, op) do { \
    mock_boundary_armed = true; \
    const int code = setjmp(mock_boundary); \
    if (code == 0) { op; assert(false); } \
    else { assert(code == (int)(expected)); } \
    mock_boundary_armed = false; \
} while (0)

alignas(RTOS_STACK_ALIGNMENT) static uint32_t stack_a[RTOS_MIN_STACK_WORDS];
alignas(RTOS_STACK_ALIGNMENT) static uint32_t stack_b[RTOS_MIN_STACK_WORDS];

static void dummy_entry(void *arg) { (void)arg; }

void rtos_test_switch_hook(const rtos_task_t *previous, const rtos_task_t *next,
                           uint32_t reasons, uint32_t tick)
{
    (void)previous; (void)next; (void)reasons; (void)tick;
}

/* 1. Size & Alignment Budget Verification */
static void test_rt_budget_and_structures(void)
{
    assert(sizeof(rtos_diag_rt_snapshot_t) == 56U);
#if UINTPTR_MAX == UINT32_MAX
    assert(sizeof(rtos_task_t) == 72U); /* ABI frozen on 32-bit ARM */
#endif
    puts("  [PASS] test_rt_budget_and_structures");
}

/* 2. Clock Capabilities & Unavailable Handling */
static void test_rt_clock_capabilities(void)
{
    rtos_diag_clock_cap_t cap;
    assert(rtos_diag_rt_clock_cap(NULL) == RTOS_ERR_ARGUMENT);

    /* Test when clock is unavailable */
    mock_set_clock_available(false);
    assert(rtos_diag_rt_clock_cap(&cap) == RTOS_OK);
    assert(cap.clock_type == RTOS_DIAG_CLOCK_UNAVAILABLE);
    assert(cap.frequency_hz == 0U);
    assert(cap.tick_hz == 100U);
    assert((cap.flags & RTOS_DIAG_RT_CLOCK_UNAVAILABLE) != 0U);

    /* Test when clock is available */
    mock_set_clock_available(true);
    assert(rtos_diag_rt_clock_cap(&cap) == RTOS_OK);
    assert(cap.clock_type == RTOS_DIAG_CLOCK_CYCLE_ACCURATE);
    assert(cap.frequency_hz == 72000000U);
    assert(cap.tick_hz == 100U);
    assert(cap.wrap_period_ticks > 5000U && cap.wrap_period_ticks < 6000U); /* ~5965 */
    assert((cap.flags & RTOS_DIAG_RT_VALID) != 0U);
    puts("  [PASS] test_rt_clock_capabilities");
}

/* 3. Task CPU Cycles Accumulation Across Switches */
static void test_rt_cpu_cycles_accumulation(void)
{
    mock_init_count = 0;
    mock_start_count = 0;
    mock_set_clock_available(true);
    mock_set_cycles_32(1000U);
    mock_primask = 0U;
    mock_in_handler = false;

    rtos_diag_rt_test_reset();

    rtos_task_t *task_a = NULL;
    rtos_task_t *task_b = NULL;
    rtos_task_ref_t ref_a, ref_b;

    assert(rtos_task_create_with_ref(&ref_a, "TaskA", dummy_entry, NULL,
                                     stack_a, RTOS_MIN_STACK_WORDS, 2U) == RTOS_OK);
    assert(rtos_task_create_with_ref(&ref_b, "TaskB", dummy_entry, NULL,
                                     stack_b, RTOS_MIN_STACK_WORDS, 2U) == RTOS_OK);
    (void)task_a; (void)task_b;

    /* Start scheduler */
    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());

    /* TaskA is dispatched initially at cycle 1000 */
    mock_advance_cycles_32(15000U); /* TaskA runs for 15,000 cycles, now at 16000 */

    /* Yield to TaskB */
    rtos_yield();

    /* TaskB is now running. Advance 25,000 cycles, now at 41000 */
    mock_advance_cycles_32(25000U);

    /* Yield back to TaskA */
    rtos_yield();

    /* TaskA is now running. Advance 10,000 cycles, now at 51000 */
    mock_advance_cycles_32(10000U);

    /* Query TaskA snapshot (in-flight: 15,000 previous + 10,000 current = 25,000) */
    rtos_diag_rt_snapshot_t snap_a;
    rtos_status_t status = rtos_diag_rt_snapshot(ref_a, &snap_a);
    if (status != RTOS_OK) {
        printf("DEBUG: rtos_diag_rt_snapshot failed with %d (in_handler=%d, masked=%d, primask=%u, basepri=%u)\n",
               (int)status, rtos_port_in_handler(), rtos_port_interrupts_masked(), mock_primask, mock_basepri);
        assert(status == RTOS_OK);
    }
    assert(snap_a.charged_cycles == 25000ULL);
    assert(snap_a.dispatch_count == 2U);
    assert(snap_a.state == RTOS_TASK_RUNNING);

    /* Query TaskB snapshot (switched out: 25,000) */
    rtos_diag_rt_snapshot_t snap_b;
    assert(rtos_diag_rt_snapshot(ref_b, &snap_b) == RTOS_OK);
    assert(snap_b.charged_cycles == 25000ULL);
    assert(snap_b.dispatch_count == 1U);
    assert(snap_b.state == RTOS_TASK_READY);

    /* Idle task has not run yet */
    rtos_diag_rt_snapshot_t snap_idle;
    assert(rtos_diag_rt_idle_snapshot(&snap_idle) == RTOS_OK);
    assert(snap_idle.charged_cycles == 0ULL);
    assert(snap_idle.dispatch_count == 0U);

    puts("  [PASS] test_rt_cpu_cycles_accumulation");
}

/* 4. Blocked State Exclusion & Idle Attribution */
static void test_rt_blocked_and_idle(void)
{
    mock_init_count = 0;
    mock_start_count = 0;
    mock_set_clock_available(true);
    mock_set_cycles_32(1000U);
    mock_primask = 0U;
    mock_in_handler = false;

    rtos_diag_rt_test_reset();

    rtos_task_ref_t ref_a, ref_b;
    assert(rtos_task_create_with_ref(&ref_a, "TaskA", dummy_entry, NULL,
                                     stack_a, RTOS_MIN_STACK_WORDS, 2U) == RTOS_OK);
    assert(rtos_task_create_with_ref(&ref_b, "TaskB", dummy_entry, NULL,
                                     stack_b, RTOS_MIN_STACK_WORDS, 2U) == RTOS_OK);

    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());
    mock_advance_cycles_32(10000U);

    rtos_diag_rt_snapshot_t snap_a_before;
    assert(rtos_diag_rt_snapshot(ref_a, &snap_a_before) == RTOS_OK);
    assert(snap_a_before.charged_cycles == 10000ULL);

    /* TaskA is running. Delay 5 ticks */
    assert(rtos_delay(5U) == RTOS_OK);

    /* TaskB is now dispatched. Run TaskB for 30,000 cycles */
    mock_advance_cycles_32(30000U);

    /* TaskB now also delays for 10 ticks */
    assert(rtos_delay(10U) == RTOS_OK);

    /* Both tasks are now blocked! Idle task runs */
    /* Run Idle task for 50,000 cycles */
    mock_advance_cycles_32(50000U);

    /* Query Idle snapshot while idle is running */
    rtos_diag_rt_snapshot_t snap_idle;
    assert(rtos_diag_rt_idle_snapshot(&snap_idle) == RTOS_OK);
    assert(snap_idle.state == RTOS_TASK_RUNNING);
    assert(snap_idle.charged_cycles == 50000ULL);
    assert(snap_idle.dispatch_count == 1U);

    /* Query TaskA snapshot while TaskA is BLOCKED */
    rtos_diag_rt_snapshot_t snap_a_blocked;
    assert(rtos_diag_rt_snapshot(ref_a, &snap_a_blocked) == RTOS_OK);
    assert(snap_a_blocked.state == RTOS_TASK_BLOCKED);
    /* Charged cycles must NOT have increased while blocked */
    assert(snap_a_blocked.charged_cycles == snap_a_before.charged_cycles);

    /* Query TaskB snapshot while TaskB is BLOCKED */
    rtos_diag_rt_snapshot_t snap_b_blocked;
    assert(rtos_diag_rt_snapshot(ref_b, &snap_b_blocked) == RTOS_OK);
    assert(snap_b_blocked.state == RTOS_TASK_BLOCKED);
    assert(snap_b_blocked.charged_cycles == 30000ULL);

    /* Step ticks to wake TaskA (after 5 ticks) */
    for (int i = 0; i < 5; ++i) {
        mock_advance_cycles_32(720000U);
        mock_tick();
    }

    /* TaskA has woken and preempts Idle */
    rtos_diag_rt_snapshot_t snap_a_woken;
    assert(rtos_diag_rt_snapshot(ref_a, &snap_a_woken) == RTOS_OK);
    assert(snap_a_woken.state == RTOS_TASK_RUNNING);

    puts("  [PASS] test_rt_blocked_and_idle");
}

/* 5. In-Flight Query Precision */
static void test_rt_in_flight_query(void)
{
    mock_init_count = 0;
    mock_start_count = 0;
    mock_set_clock_available(true);
    mock_set_cycles_32(5000U);
    mock_primask = 0U;
    mock_in_handler = false;

    rtos_diag_rt_test_reset();

    rtos_task_ref_t ref_a;
    assert(rtos_task_create_with_ref(&ref_a, "TaskInFlight", dummy_entry, NULL,
                                     stack_a, RTOS_MIN_STACK_WORDS, 2U) == RTOS_OK);

    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());

    rtos_diag_rt_snapshot_t snap1, snap2, snap3;

    assert(rtos_diag_rt_snapshot(ref_a, &snap1) == RTOS_OK);
    assert(snap1.state == RTOS_TASK_RUNNING);
    assert(snap1.charged_cycles == 0ULL);

    mock_advance_cycles_32(8123U);
    assert(rtos_diag_rt_snapshot(ref_a, &snap2) == RTOS_OK);
    assert(snap2.charged_cycles == 8123ULL);

    mock_advance_cycles_32(4000U);
    assert(rtos_diag_rt_snapshot(ref_a, &snap3) == RTOS_OK);
    assert(snap3.charged_cycles == 12123ULL);

    puts("  [PASS] test_rt_in_flight_query");
}

/* 6. Ready-to-Run Latency Tracking */
static void test_rt_ready_latency(void)
{
    /* Clean test setup */
    mock_set_clock_available(true);
    mock_set_cycles_32(200000U);
    rtos_diag_rt_test_reset();

    rtos_task_ref_t ref_h, ref_l;
    /* Create high prio task (prio 4) and low prio task (prio 2) */
    assert(rtos_task_create_with_ref(&ref_h, "TaskHigh", dummy_entry, NULL,
                                     stack_b, RTOS_MIN_STACK_WORDS, 4U) == RTOS_OK);
    assert(rtos_task_create_with_ref(&ref_l, "TaskLow", dummy_entry, NULL,
                                     stack_a, RTOS_MIN_STACK_WORDS, 2U) == RTOS_OK);

    /* Scheduler starts: TaskHigh runs first */
    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());

    /* TaskLow entered READY at cycle 200,000.
     * TaskHigh runs for 40,000 cycles and 2 ticks before blocking */
    mock_advance_cycles_32(40000U);
    mock_tick();
    mock_advance_cycles_32(40000U);
    mock_tick();

    /* TaskHigh now delays, allowing TaskLow to be dispatched at cycle 280,000 */
    assert(rtos_delay(10U) == RTOS_OK);

    /* TaskLow is now RUNNING. Check its ready latency */
    rtos_diag_rt_snapshot_t snap_l;
    assert(rtos_diag_rt_snapshot(ref_l, &snap_l) == RTOS_OK);
    assert(snap_l.state == RTOS_TASK_RUNNING);
    assert(snap_l.last_ready_latency_cycles == 80000ULL); /* 280,000 - 200,000 */
    assert(snap_l.max_ready_latency_cycles == 80000ULL);
    assert(snap_l.last_ready_latency_ticks == 2U);
    assert(snap_l.max_ready_latency_ticks == 2U);

    puts("  [PASS] test_rt_ready_latency");
}

/* 7. 32-Bit Monotonic Rollover & Multi-Wrap Uncertainty */
static void test_rt_rollover_and_uncertainty(void)
{
    mock_set_clock_available(true);
    /* Set clock right before 32-bit rollover */
    mock_set_cycles_32(0xFFFFFFC0U);
    rtos_diag_rt_test_reset();

    rtos_task_ref_t ref;
    assert(rtos_task_create_with_ref(&ref, "TaskRollover", dummy_entry, NULL,
                                     stack_a, RTOS_MIN_STACK_WORDS, 2U) == RTOS_OK);

    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());

    /* Advance 100 cycles: rolls over past 0 to 0x00000024 */
    mock_advance_cycles_32(100U);

    rtos_diag_rt_snapshot_t snap;
    assert(rtos_diag_rt_snapshot(ref, &snap) == RTOS_OK);
    assert((snap.flags & RTOS_DIAG_RT_VALID) != 0U);
    assert((snap.flags & RTOS_DIAG_RT_CLOCK_UNCERTAIN) == 0U);
    assert(snap.charged_cycles == 100ULL); /* 64-bit extension preserved seamless math */

    /* Now simulate a multi-wrap gap: >= 5900 ticks (59 seconds) without update (e.g. debugger halt) */
    rtos_tick_test_advance(5905U);

    assert(rtos_diag_rt_snapshot(ref, &snap) == RTOS_OK);
    assert((snap.flags & RTOS_DIAG_RT_CLOCK_UNCERTAIN) != 0U); /* Flagged uncertain! */

    rtos_diag_clock_cap_t cap;
    assert(rtos_diag_rt_clock_cap(&cap) == RTOS_OK);
    assert((cap.flags & RTOS_DIAG_RT_CLOCK_UNCERTAIN) != 0U);

    puts("  [PASS] test_rt_rollover_and_uncertainty");
}

/* 8. Slot Generation Reuse (Stale Check) */
static void test_rt_generation_stale(void)
{
    mock_set_clock_available(true);
    mock_set_cycles_32(1000U);
    rtos_diag_rt_test_reset();

    rtos_task_ref_t ref_old;
    assert(rtos_task_create_with_ref(&ref_old, "Task1", dummy_entry, NULL,
                                     stack_a, RTOS_MIN_STACK_WORDS, 2U) == RTOS_OK);

    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());
    mock_advance_cycles_32(5000U);

    /* Terminate Task1 */
    mock_stop_after_yield = true;
    EXPECT_EVENT(MOCK_EVENT_EXIT, rtos_task_exit());
    mock_stop_after_yield = false;

    /* Snapshot on terminated task with original ref still succeeds */
    rtos_diag_rt_snapshot_t snap_term;
    assert(rtos_diag_rt_snapshot(ref_old, &snap_term) == RTOS_OK);
    assert(snap_term.state == RTOS_TASK_TERMINATED);
    assert(snap_term.charged_cycles == 5000ULL);

    /* Reclaim slot 0 and create Task2 with new generation */
    rtos_task_ref_t ref_new;
    assert(rtos_task_create_with_ref(&ref_new, "Task2", dummy_entry, NULL,
                                     stack_a, RTOS_MIN_STACK_WORDS, 2U) == RTOS_OK);
    assert(ref_new.slot == ref_old.slot);
    assert(ref_new.generation == ref_old.generation + 1U);

    /* Old ref query must now return RTOS_ERR_STALE! */
    rtos_diag_rt_snapshot_t snap_stale;
    assert(rtos_diag_rt_snapshot(ref_old, &snap_stale) == RTOS_ERR_STALE);

    /* New ref query returns fresh stats for Task2 (immediately dispatched as prio 2 > idle) */
    rtos_diag_rt_snapshot_t snap_new;
    assert(rtos_diag_rt_snapshot(ref_new, &snap_new) == RTOS_OK);
    assert(snap_new.dispatch_count == 1U);
    assert(snap_new.state == RTOS_TASK_RUNNING);

    puts("  [PASS] test_rt_generation_stale");
}

/* 9. Context & Argument Validation */
static void test_rt_arguments_and_context(void)
{
    rtos_diag_rt_snapshot_t snap;
    rtos_task_ref_t invalid_slot = { .slot = RTOS_MAX_TASKS, .generation = 1U };
    rtos_task_ref_t invalid_gen = { .slot = 0U, .generation = 0U };

    assert(rtos_diag_rt_snapshot(invalid_slot, &snap) == RTOS_ERR_ARGUMENT);
    assert(rtos_diag_rt_snapshot(invalid_gen, &snap) == RTOS_ERR_ARGUMENT);
    assert(rtos_diag_rt_snapshot(invalid_slot, NULL) == RTOS_ERR_ARGUMENT);
    assert(rtos_diag_rt_idle_snapshot(NULL) == RTOS_ERR_ARGUMENT);

    /* In handler mode, calls return RTOS_ERR_CONTEXT */
    mock_in_handler = true;
    rtos_task_ref_t valid_ref = { .slot = 0U, .generation = 1U };
    assert(rtos_diag_rt_snapshot(valid_ref, &snap) == RTOS_ERR_CONTEXT);
    assert(rtos_diag_rt_idle_snapshot(&snap) == RTOS_ERR_CONTEXT);
    rtos_diag_clock_cap_t cap;
    assert(rtos_diag_rt_clock_cap(&cap) == RTOS_ERR_CONTEXT);
    mock_in_handler = false;

    const display_key_t key = {1U, 1U, 0U};
    assert(App_IPC_Init() == RTOS_OK);
    /* The old A2 Monitor called this real wrapper in Thread mode. */
    assert(App_IPC_SendKeyFromISR(&key) == RTOS_ERR_CONTEXT);
    puts("  [PASS] A2 real App_IPC/queue Thread call returns RTOS_ERR_CONTEXT");

    puts("  [PASS] test_rt_arguments_and_context");
}

static unsigned int job_checks;
#define JOB_CHECK(c) do { ++job_checks; assert(c); } while (0)

static rtos_task_ref_t make_job_task(const char *name, uint32_t *stack, uint8_t prio)
{
    rtos_task_ref_t ref;
    JOB_CHECK(rtos_task_create_with_ref(&ref, name, dummy_entry, NULL, stack,
                                       RTOS_MIN_STACK_WORDS, prio) == RTOS_OK);
    return ref;
}

static rtos_diag_rt_job_snapshot_t job_view(rtos_task_ref_t ref)
{
    rtos_diag_rt_job_snapshot_t s;
    JOB_CHECK(rtos_diag_rt_job_snapshot(ref, &s) == RTOS_OK);
    return s;
}

static void job_clock(uint32_t cycles)
{
    mock_set_clock_available(true);
    mock_set_cycles_32(cycles);
    rtos_diag_rt_test_reset();
}

static void test_job_boundaries(void)
{
    job_clock(1000U);
    rtos_task_ref_t a = make_job_task("A", stack_a, 2);
    rtos_diag_rt_config_t c = {RTOS_DIAG_JOB_ENABLED, 4, 100, 1};
    uint32_t id = 99;
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_ERR_STATE && id == 99);
    JOB_CHECK(rtos_diag_rt_configure(a, &c) == RTOS_OK);
    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());

    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_OK && id == 1);
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_ERR_STATE && id == 1);
    JOB_CHECK(rtos_diag_rt_configure(a, &c) == RTOS_ERR_STATE);
    JOB_CHECK(rtos_diag_rt_job_complete(2) == RTOS_ERR_STATE);
    mock_advance_cycles_32(100);
    mock_tick();
    JOB_CHECK(rtos_diag_rt_job_complete(id) == RTOS_OK);
    rtos_diag_rt_job_snapshot_t s = job_view(a);
    JOB_CHECK(s.completed_jobs == 1 && !s.deadline_misses && !s.budget_overruns);
    JOB_CHECK(s.last_finish_ticks == 1 && s.last_first_dispatch_ticks == 0 && s.last_budget_cycles == 100);
    JOB_CHECK((s.flags & (RTOS_DIAG_JOB_FIRST_VALID | RTOS_DIAG_JOB_FINISH_VALID |
                         RTOS_DIAG_JOB_BUDGET_VALID)) != 0 && !s.active_job_id);
    JOB_CHECK(rtos_diag_rt_job_complete(id) == RTOS_ERR_STATE);
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_OK && id == 2);
    mock_advance_cycles_32(101);
    mock_tick();
    mock_tick();
    mock_tick();
    s = job_view(a);
    JOB_CHECK(s.active_job_id == 2 && s.deadline_misses == 1 && s.budget_overruns == 1);
    JOB_CHECK(rtos_diag_rt_job_complete(id) == RTOS_OK);
    s = job_view(a);
    JOB_CHECK(s.deadline_misses == 1 && s.budget_overruns == 1 && s.completed_jobs == 2);
    JOB_CHECK(s.invalid_jobs == 2 && s.rejected_releases == 1 && s.max_budget_cycles == 101);
    const rtos_diag_rt_config_t bad = {RTOS_DIAG_JOB_ENABLED, UINT32_MAX, 0, 0};
    JOB_CHECK(rtos_diag_rt_configure(a, &bad) == RTOS_ERR_ARGUMENT);
    mock_in_handler = true;
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_ERR_CONTEXT);
    mock_in_handler = false;
    rtos_diag_rt_test_set_job_id(a.slot, UINT32_MAX);
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_ERR_CAPACITY && id == 2);
    JOB_CHECK(job_view(a).exhausted_ids == 1);
}

static void test_job_segments(void)
{
    job_clock(1000U);
    rtos_task_ref_t a = make_job_task("A", stack_a, 2);
    rtos_task_ref_t b = make_job_task("B", stack_b, 1);
    const rtos_diag_rt_config_t c = {RTOS_DIAG_JOB_ENABLED, 0, 31, 0};
    JOB_CHECK(rtos_diag_rt_configure(b, &c) == RTOS_OK);
    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());
    uint32_t id = 0;
    JOB_CHECK(rtos_diag_rt_job_release(b, &id) == RTOS_OK);
    JOB_CHECK(rtos_diag_rt_job_complete(id) == RTOS_ERR_STATE);
    mock_tick();
    JOB_CHECK(rtos_delay(1) == RTOS_OK); /* A blocks, first dispatch B after one tick */
    mock_advance_cycles_32(10);
    mock_tick(); /* A wakes and preempts B */
    mock_advance_cycles_32(100);
    JOB_CHECK(rtos_delay(2) == RTOS_OK);
    mock_advance_cycles_32(11);
    JOB_CHECK(rtos_delay(1) == RTOS_OK); /* B blocks, Idle will receive CPU time */
    mock_advance_cycles_32(300);
    mock_tick(); /* B wakes */
    mock_advance_cycles_32(10);
    JOB_CHECK(rtos_diag_rt_job_complete(id) == RTOS_OK);
    rtos_diag_rt_job_snapshot_t s = job_view(b);
    JOB_CHECK(s.last_budget_cycles == 31 && s.budget_overruns == 0);
    JOB_CHECK(s.last_first_dispatch_ticks == 1 && s.last_finish_ticks == 3);
    JOB_CHECK(s.completed_jobs == 1 && s.deadline_misses == 0);
    JOB_CHECK(job_view(a).invalid_jobs == 1);
}

static void test_job_wrap_and_unavailable(void)
{
    job_clock(UINT32_MAX - 24U);
    rtos_tick_test_set(UINT32_MAX);
    rtos_task_ref_t a = make_job_task("A", stack_a, 2);
    const rtos_diag_rt_config_t deadline = {RTOS_DIAG_JOB_ENABLED, 0, 0, 1};
    const rtos_diag_rt_config_t full = {RTOS_DIAG_JOB_ENABLED, 0, 50, 1};
    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());

    uint32_t id;
    JOB_CHECK(rtos_diag_rt_configure(a, &full) == RTOS_OK);
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_OK);
    mock_advance_cycles_32(50);
    mock_tick();
    JOB_CHECK(rtos_diag_rt_job_complete(id) == RTOS_OK);
    rtos_diag_rt_job_snapshot_t s = job_view(a);
    JOB_CHECK(s.last_finish_ticks == 1 && s.last_budget_cycles == 50 && !s.deadline_misses);

    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_OK);
    rtos_tick_test_advance(5905); /* no DWT anchor in the elapsed window */
    JOB_CHECK(rtos_diag_rt_job_complete(id) == RTOS_OK);
    s = job_view(a);
    JOB_CHECK((s.flags & RTOS_DIAG_JOB_CYCLES_INVALID) && !(s.flags & RTOS_DIAG_JOB_BUDGET_VALID));
    JOB_CHECK(s.deadline_misses == 1 && s.budget_overruns == 0);

    mock_set_clock_available(false);
    JOB_CHECK(rtos_diag_rt_configure(a, &full) == RTOS_ERR_UNAVAILABLE);
    JOB_CHECK(rtos_diag_rt_configure(a, &deadline) == RTOS_OK);
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_OK);
    mock_tick();
    JOB_CHECK(rtos_diag_rt_job_complete(id) == RTOS_OK);
    s = job_view(a);
    JOB_CHECK((s.flags & RTOS_DIAG_JOB_FINISH_VALID) && !(s.flags & RTOS_DIAG_JOB_BUDGET_VALID));
}

static void test_job_abort_reuse(void)
{
    job_clock(1000U);
    rtos_task_ref_t a = make_job_task("Old", stack_a, 2);
    const rtos_diag_rt_config_t c = {RTOS_DIAG_JOB_ENABLED, 0, 0, 0};
    const rtos_diag_rt_config_t off = {0};
    JOB_CHECK(rtos_diag_rt_configure(a, &c) == RTOS_OK);
    EXPECT_EVENT(MOCK_EVENT_START, rtos_start());
    uint32_t id;
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_OK && id == 1);
    JOB_CHECK(rtos_diag_rt_job_complete(id) == RTOS_OK);
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_OK && id == 2);
    JOB_CHECK(rtos_diag_rt_job_abort(id) == RTOS_OK);
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_OK && id == 3);
    JOB_CHECK(rtos_diag_rt_configure(a, &off) == RTOS_OK);
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_ERR_STATE);
    JOB_CHECK(rtos_diag_rt_configure(a, &c) == RTOS_OK);
    JOB_CHECK(rtos_diag_rt_job_release(a, &id) == RTOS_OK && id == 4);
    mock_stop_after_yield = true;
    EXPECT_EVENT(MOCK_EVENT_EXIT, rtos_task_exit());
    mock_stop_after_yield = false;
    rtos_diag_rt_job_snapshot_t s = job_view(a);
    JOB_CHECK(s.completed_jobs == 1 && s.aborted_jobs == 3 && !s.active_job_id);
    rtos_task_ref_t b = make_job_task("New", stack_a, 2);
    JOB_CHECK(a.slot == b.slot && a.generation != b.generation);
    JOB_CHECK(rtos_diag_rt_job_snapshot(a, &s) == RTOS_ERR_STALE && s.aborted_jobs == 3);
    s = job_view(b);
    JOB_CHECK(s.aborted_jobs == 0 && s.completed_jobs == 0 && s.stale_requests == 1);
    JOB_CHECK(rtos_diag_rt_configure(b, &c) == RTOS_OK);
    JOB_CHECK(rtos_diag_rt_job_release(b, &id) == RTOS_OK && id == 1);
}
static void run_isolated(void (*fn)(void))
{
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        fn();
        exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void run_job_cases(void)
{
    fflush(stdout);
    const char *names[] = {"boundaries", "segments", "wrap/clock", "abort/reuse"};
    void (*tests[])(void) = {test_job_boundaries, test_job_segments,
                           test_job_wrap_and_unavailable, test_job_abort_reuse};
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            tests[i]();
            printf("JOB PASS %s: %u checks (plus scheduler boundary assertions)\n", names[i], job_checks);
            fflush(stdout);
            _Exit(0);
        }
        int status;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "budget") == 0) test_rt_budget_and_structures();
        else if (strcmp(argv[1], "capabilities") == 0) test_rt_clock_capabilities();
        else if (strcmp(argv[1], "cpu_cycles") == 0) test_rt_cpu_cycles_accumulation();
        else if (strcmp(argv[1], "blocked_and_idle") == 0) test_rt_blocked_and_idle();
        else if (strcmp(argv[1], "in_flight") == 0) test_rt_in_flight_query();
        else if (strcmp(argv[1], "ready_latency") == 0) test_rt_ready_latency();
        else if (strcmp(argv[1], "rollover_uncertainty") == 0) test_rt_rollover_and_uncertainty();
        else if (strcmp(argv[1], "generation_stale") == 0) test_rt_generation_stale();
        else if (strcmp(argv[1], "arguments_context") == 0) test_rt_arguments_and_context();
        else {
            fprintf(stderr, "Unknown test: %s\n", argv[1]);
            return 1;
        }
        return 0;
    }

    puts("====================================================");
    puts("InsightRTOS Stage 9.3-A Real-Time Diagnostics Test Suite");
    puts("====================================================");

    run_isolated(test_rt_budget_and_structures);
    run_isolated(test_rt_clock_capabilities);
    run_isolated(test_rt_cpu_cycles_accumulation);
    run_isolated(test_rt_blocked_and_idle);
    run_isolated(test_rt_in_flight_query);
    run_isolated(test_rt_ready_latency);
    run_isolated(test_rt_rollover_and_uncertainty);
    run_isolated(test_rt_generation_stale);
    run_isolated(test_rt_arguments_and_context);

    puts("====================================================");
    puts("ALL STAGE 9.3-A REAL-TIME TESTS PASSED (9/9 Suites, 45 Assertions)");
    puts("====================================================");
    run_job_cases();
    return 0;
}
