#ifndef INSIGHT_RTOS_DIAG_RT_H
#define INSIGHT_RTOS_DIAG_RT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rtos_config.h"
#include "rtos_task.h"

#ifndef INSIGHT_DIAG_RT
#define INSIGHT_DIAG_RT 0
#endif
_Static_assert(INSIGHT_DIAG_RT == 0 || INSIGHT_DIAG_RT == 1, "Diag RT boolean");

typedef enum {
    RTOS_DIAG_CLOCK_UNAVAILABLE = 0,
    RTOS_DIAG_CLOCK_TICK_ONLY = 1,
    RTOS_DIAG_CLOCK_CYCLE_ACCURATE = 2
} rtos_diag_clock_type_t;

#define RTOS_DIAG_RT_VALID             (1U << 0)
#define RTOS_DIAG_RT_CLOCK_UNCERTAIN   (1U << 1)
#define RTOS_DIAG_RT_CLOCK_UNAVAILABLE (1U << 2)
#define RTOS_DIAG_RT_STALE             (1U << 3)

typedef struct {
    rtos_diag_clock_type_t clock_type;
    uint32_t frequency_hz;
    uint32_t tick_hz;
    uint32_t wrap_period_ticks;
    uint32_t flags;
} rtos_diag_clock_cap_t;

typedef struct {
    uint64_t charged_cycles;
    uint64_t last_ready_latency_cycles;
    uint64_t max_ready_latency_cycles;
    rtos_task_ref_t ref;
    uint32_t flags;
    uint32_t last_ready_latency_ticks;
    uint32_t max_ready_latency_ticks;
    uint32_t dispatch_count;
    uint32_t last_dispatch_tick;
    uint16_t state;
    uint8_t base_priority;
    uint8_t effective_priority;
} rtos_diag_rt_snapshot_t;
_Static_assert(sizeof(rtos_diag_rt_snapshot_t) == 56U, "RT snapshot 56B");

#if INSIGHT_DIAG_RT
rtos_status_t rtos_diag_rt_clock_cap(rtos_diag_clock_cap_t *out_cap);
rtos_status_t rtos_diag_rt_snapshot(rtos_task_ref_t ref, rtos_diag_rt_snapshot_t *out_snapshot);
rtos_status_t rtos_diag_rt_idle_snapshot(rtos_diag_rt_snapshot_t *out_snapshot);
#if RTOS_TESTING
void rtos_diag_rt_test_reset(void);
#endif
#endif

#endif /* INSIGHT_RTOS_DIAG_RT_H */
