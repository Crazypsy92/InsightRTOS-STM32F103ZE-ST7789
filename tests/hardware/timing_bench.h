#ifndef INSIGHT_TIMING_BENCH_H
#define INSIGHT_TIMING_BENCH_H

#include "rtos.h"

enum bench_path {
    BENCH_EMPTY_READ,
    BENCH_EMPTY_CALL,
    BENCH_PENDSV,
    BENCH_SYSTICK,
    BENCH_RELEASE,
    BENCH_COMPLETE,
    BENCH_SNAPSHOT,
    BENCH_PATH_COUNT
};

typedef struct {
    uint64_t sum;
    uint32_t count, min, max;
    uint32_t reserved;
} timing_path_stats_t;

typedef struct {
    timing_path_stats_t paths[BENCH_PATH_COUNT];
    uint32_t state, start_tick, finish_tick, start_ms, finish_ms;
    uint32_t task_switch_mask, idle_dispatches, events;
    uint32_t completed_jobs, bad_job_flags, clock_hz, rt_enabled;
} timing_capture_t;

extern volatile timing_capture_t g_timing_bench;

static inline bool timing_bench_running(void)
{
    return g_timing_bench.state == 1U;
}

static inline uint32_t timing_bench_stamp(void)
{
    __asm volatile("" ::: "memory");
    const uint32_t cycles = *(volatile const uint32_t *)UINT32_C(0xe0001004);
    __asm volatile("" ::: "memory");
    return cycles;
}

void timing_bench_record(enum bench_path path, uint32_t cycles);
void timing_bench_record_switch(uint32_t cycles, uint32_t task_id);
void timing_bench_controller_step(rtos_task_ref_t ref, bool display_ready);

#endif
