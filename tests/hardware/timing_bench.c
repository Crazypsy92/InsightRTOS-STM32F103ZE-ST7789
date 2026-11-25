#include "timing_bench.h"
#include "delay.h"
#include "port.h"
#include "rtos_diag_rt.h"

extern volatile uint32_t g_stress_request;
extern volatile uint32_t g_stress_generated;

volatile timing_capture_t g_timing_bench;

void timing_bench_record(enum bench_path path, uint32_t cycles)
{
    /* Each path has a single writer. The capture is read after state becomes 2. */
    volatile timing_path_stats_t *stats = &g_timing_bench.paths[path];
    if (stats->count == 0U || cycles < stats->min) stats->min = cycles;
    if (cycles > stats->max) stats->max = cycles;
    stats->sum += cycles;
    ++stats->count;
}

void timing_bench_record_switch(uint32_t cycles, uint32_t task_id)
{
    timing_bench_record(BENCH_PENDSV, cycles);
    if (task_id == 0U) {
        ++g_timing_bench.idle_dispatches;
    } else {
        g_timing_bench.task_switch_mask |= UINT32_C(1) << task_id;
    }
}

static __attribute__((noinline)) void empty_call(void)
{
    __asm volatile("" ::: "memory");
}

static void calibrate(void)
{
    for (uint32_t i = 0; i < 64U; ++i) {
        const rtos_irq_state_t irq = rtos_critical_enter();
        uint32_t start = timing_bench_stamp();
        const uint32_t read_cycles = timing_bench_stamp() - start;
        start = timing_bench_stamp();
        empty_call();
        const uint32_t call_cycles = timing_bench_stamp() - start;
        rtos_critical_exit(irq);
        timing_bench_record(BENCH_EMPTY_READ, read_cycles);
        timing_bench_record(BENCH_EMPTY_CALL, call_cycles);
    }
}

void timing_bench_controller_step(rtos_task_ref_t ref, bool display_ready)
{
#if INSIGHT_DIAG_RT
    static uint32_t job_id;
#else
    (void)ref;
#endif
    if (g_timing_bench.state == 2U) return;
    const uint32_t tick = rtos_tick_get();
    if (g_timing_bench.state == 0U) {
        if (tick < 300U || !display_ready) return;
        RTOS_ASSERT(rtos_port_clock_available() && rtos_port_cpu_hz() == 72000000U);
        calibrate();
#if INSIGHT_DIAG_RT
        const rtos_diag_rt_config_t cfg = {RTOS_DIAG_JOB_ENABLED, 1U, 0U, 3U};
        RTOS_ASSERT(rtos_diag_rt_configure(ref, &cfg) == RTOS_OK);
#endif
        g_timing_bench.clock_hz = rtos_port_cpu_hz();
        g_timing_bench.rt_enabled = INSIGHT_DIAG_RT;
        g_timing_bench.start_ms = GetTick_ms();
        g_timing_bench.start_tick = rtos_tick_get();
        g_stress_request = 1U;
        g_timing_bench.state = 1U;
        return;
    }
#if INSIGHT_DIAG_RT
    if (job_id != 0U) {
        const uint32_t start = timing_bench_stamp();
        const rtos_status_t status = rtos_diag_rt_job_complete(job_id);
        timing_bench_record(BENCH_COMPLETE, timing_bench_stamp() - start);
        RTOS_ASSERT(status == RTOS_OK);
    }
#endif
    if (tick - g_timing_bench.start_tick >= 6200U) {
#if INSIGHT_DIAG_RT
        rtos_diag_rt_job_snapshot_t snapshot;
        RTOS_ASSERT(rtos_diag_rt_job_snapshot(ref, &snapshot) == RTOS_OK);
        g_timing_bench.completed_jobs = snapshot.completed_jobs;
        g_timing_bench.bad_job_flags = snapshot.flags &
            (RTOS_DIAG_JOB_CYCLES_INVALID | RTOS_DIAG_JOB_TICK_INVALID |
             RTOS_DIAG_JOB_MISSED | RTOS_DIAG_JOB_OVERRUN);
        RTOS_ASSERT(snapshot.active_job_id == 0U && snapshot.invalid_jobs == 0U &&
                    snapshot.deadline_misses == 0U && snapshot.budget_overruns == 0U);
#endif
        g_timing_bench.events = g_stress_generated;
        g_timing_bench.finish_ms = GetTick_ms();
        g_timing_bench.finish_tick = rtos_tick_get();
        __asm volatile("dmb" ::: "memory");
        g_timing_bench.state = 2U;
        return;
    }
#if INSIGHT_DIAG_RT
    uint32_t start = timing_bench_stamp();
    const rtos_status_t release = rtos_diag_rt_job_release(ref, &job_id);
    timing_bench_record(BENCH_RELEASE, timing_bench_stamp() - start);
    RTOS_ASSERT(release == RTOS_OK);

    rtos_diag_rt_job_snapshot_t snapshot;
    start = timing_bench_stamp();
    const rtos_status_t status = rtos_diag_rt_job_snapshot(ref, &snapshot);
    timing_bench_record(BENCH_SNAPSHOT, timing_bench_stamp() - start);
    RTOS_ASSERT(status == RTOS_OK && snapshot.active_job_id == job_id);
#endif
}
