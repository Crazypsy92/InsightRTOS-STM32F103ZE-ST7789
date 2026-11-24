#include "rtos_diag_rt.h"
#include "diag_rt_internal.h"
#include "kernel_internal.h"
#include "port.h"
#include "rtos.h"
#include "rtos_critical.h"
#include "task_lifecycle_internal.h"

#if INSIGHT_DIAG_RT

#define RTOS_DIAG_RT_MAX_TASKS RTOS_MAX_TASKS /* 8 */
#define RTOS_DIAG_RT_SLOT_NONE 0xFFFFU
#define RTOS_DIAG_RT_SLOT_IDLE 0xFFFEU

typedef struct {
    uint64_t release_charge;
    uint32_t flags, budget_cycles, deadline_ticks;
    uint32_t id, release_tick;
    uint32_t last_first, max_first, last_finish, max_finish;
    uint32_t last_budget, max_budget;
    uint16_t completed, misses, overruns, aborted, rejected, invalid;
} rt_job_t;

typedef struct {
    uint64_t charged_cycles;
    uint64_t last_ready_cycles;
    uint64_t last_ready_latency_cycles;
    uint64_t max_ready_latency_cycles;
    uint32_t generation;
    uint32_t last_ready_tick;
    uint32_t last_ready_latency_ticks;
    uint32_t max_ready_latency_ticks;
    uint32_t dispatch_count;
    uint32_t last_dispatch_tick;
    rt_job_t job;
} rtos_diag_rt_task_record_t;

typedef struct {
    uint64_t charged_cycles;
    uint32_t dispatch_count;
    uint32_t last_dispatch_tick;
    uint32_t reserved;
} rtos_diag_rt_idle_record_t;

typedef struct {
    uint64_t cycle_epoch;
    uint64_t running_dispatch_cycles;
    uint32_t last_cycle32;
    uint32_t last_sample_tick;
    uint16_t running_slot;
    bool clock_uncertain;
    bool last_sample_tick_valid;
    bool initialized;
} rtos_diag_rt_global_t;

static rtos_diag_rt_task_record_t s_rt_tasks[RTOS_DIAG_RT_MAX_TASKS];
static rtos_diag_rt_idle_record_t s_idle_record;
static rtos_diag_rt_global_t s_rt_global;
static uint32_t s_stale_requests, s_exhausted_ids;

_Static_assert(sizeof(s_rt_tasks) + sizeof(s_idle_record) + sizeof(s_rt_global) +
               sizeof(s_stale_requests) + sizeof(s_exhausted_ids) <= 1024U,
               "RT side table and state within 1024B SRAM budget");

static uint64_t get_cycles_64_locked(uint32_t current_tick)
{
    if (!rtos_port_clock_available()) {
        if (s_rt_global.initialized) s_rt_global.clock_uncertain = true;
        return 0ULL;
    }
    const uint32_t current32 = rtos_port_clock_cycles_32();
    if (!s_rt_global.initialized) {
        s_rt_global.last_cycle32 = current32;
        s_rt_global.last_sample_tick = current_tick;
        s_rt_global.last_sample_tick_valid = true;
        s_rt_global.initialized = true;
        return (uint64_t)current32;
    }

    if (current32 < s_rt_global.last_cycle32) {
        s_rt_global.cycle_epoch += (UINT64_C(1) << 32);
    }
    s_rt_global.last_cycle32 = current32;

    if (s_rt_global.last_sample_tick_valid) {
        const uint32_t elapsed_ticks = current_tick - s_rt_global.last_sample_tick;
        /* ~59.0s boundary at 100 Hz = 5900 ticks */
        if (elapsed_ticks >= 5900U) {
            s_rt_global.clock_uncertain = true;
        }
    }
    s_rt_global.last_sample_tick = current_tick;
    s_rt_global.last_sample_tick_valid = true;

    return s_rt_global.cycle_epoch | (uint64_t)current32;
}

void rtos_diag_rt_init(void)
{
    s_rt_global.running_slot = RTOS_DIAG_RT_SLOT_NONE;
    s_rt_global.cycle_epoch = 0ULL;
    s_rt_global.running_dispatch_cycles = 0ULL;
    s_rt_global.last_cycle32 = 0U;
    s_rt_global.last_sample_tick = 0U;
    s_rt_global.clock_uncertain = false;
    s_rt_global.last_sample_tick_valid = false;
    s_rt_global.initialized = false;
    s_idle_record.charged_cycles = 0ULL;
    s_idle_record.dispatch_count = 0U;
    s_idle_record.last_dispatch_tick = 0U;
    s_idle_record.reserved = 0U;

    if (rtos_port_clock_available()) {
        s_rt_global.last_cycle32 = rtos_port_clock_cycles_32();
        s_rt_global.last_sample_tick = rtos_tick_get();
        s_rt_global.last_sample_tick_valid = true;
        s_rt_global.initialized = true;
    }
}

void rtos_diag_rt_task_init_hook(uint32_t slot, uint32_t generation)
{
    if (slot < RTOS_DIAG_RT_MAX_TASKS) {
        rtos_diag_rt_task_record_t *rec = &s_rt_tasks[slot];
        rec->charged_cycles = 0ULL;
        rec->last_ready_cycles = 0ULL;
        rec->last_ready_latency_cycles = 0ULL;
        rec->max_ready_latency_cycles = 0ULL;
        rec->generation = generation;
            rec->last_ready_tick = 0U;
        rec->last_ready_latency_ticks = 0U;
        rec->max_ready_latency_ticks = 0U;
        rec->dispatch_count = 0U;
        rec->last_dispatch_tick = 0U;
        rec->job = (rt_job_t){0};
    }
}

void rtos_diag_rt_task_ready_hook(rtos_task_t *task)
{
    if (task != NULL && task->id != RTOS_IDLE_TASK_ID && task->id <= RTOS_DIAG_RT_MAX_TASKS) {
        const uint32_t slot = task->id - 1U;
        const uint32_t tick = rtos_tick_get();
        s_rt_tasks[slot].last_ready_tick = tick;
        s_rt_tasks[slot].last_ready_cycles = get_cycles_64_locked(tick);
    }
}

static void job_count(rtos_diag_rt_task_record_t *rec, uint16_t *counter)
{
    if (*counter != UINT16_MAX) ++*counter;
    else rec->job.flags |= RTOS_DIAG_JOB_COUNTER_SATURATED;
}

static uint64_t task_charge(uint32_t slot, uint64_t cycles)
{
    uint64_t charged = s_rt_tasks[slot].charged_cycles;
    if (s_rt_global.running_slot == slot && cycles >= s_rt_global.running_dispatch_cycles)
        charged += cycles - s_rt_global.running_dispatch_cycles;
    return charged;
}

static bool clock_valid(void)
{
    return rtos_port_clock_available() && !s_rt_global.clock_uncertain;
}

static void job_check_deadline(rtos_diag_rt_task_record_t *rec, uint32_t elapsed)
{
    if (rec->job.deadline_ticks != 0U && elapsed > rec->job.deadline_ticks &&
        elapsed <= RTOS_DELAY_MAX_TICKS + 1U &&
        (rec->job.flags & (RTOS_DIAG_JOB_MISSED | RTOS_DIAG_JOB_TICK_INVALID)) == 0U) {
        rec->job.flags |= RTOS_DIAG_JOB_MISSED;
        job_count(rec, &rec->job.misses);
    }
    if (elapsed > RTOS_DELAY_MAX_TICKS) rec->job.flags |= RTOS_DIAG_JOB_TICK_INVALID;
}

static void job_check_budget(rtos_diag_rt_task_record_t *rec, uint64_t charge, bool finish)
{
    if (!clock_valid() || charge < rec->job.release_charge)
        rec->job.flags |= RTOS_DIAG_JOB_CYCLES_INVALID;
    if ((rec->job.flags & RTOS_DIAG_JOB_CYCLES_INVALID) != 0U) return;

    const uint64_t use = charge - rec->job.release_charge;
    if (rec->job.budget_cycles != 0U && use > rec->job.budget_cycles &&
        (rec->job.flags & RTOS_DIAG_JOB_OVERRUN) == 0U) {
        rec->job.flags |= RTOS_DIAG_JOB_OVERRUN;
        job_count(rec, &rec->job.overruns);
    }
    if (finish) {
        const uint32_t measured = use > UINT32_MAX ? UINT32_MAX : (uint32_t)use;
        if (use > UINT32_MAX) rec->job.flags |= RTOS_DIAG_JOB_BUDGET_SATURATED;
        rec->job.last_budget = measured;
        if (measured > rec->job.max_budget) rec->job.max_budget = measured;
        rec->job.flags |= RTOS_DIAG_JOB_BUDGET_VALID;
    }
}

static void job_first_dispatch(rtos_diag_rt_task_record_t *rec, uint32_t tick)
{
    if ((rec->job.flags & (RTOS_DIAG_JOB_ACTIVE | RTOS_DIAG_JOB_FIRST_VALID)) != RTOS_DIAG_JOB_ACTIVE)
        return;
    const uint32_t elapsed = tick - rec->job.release_tick;
    if (elapsed > RTOS_DELAY_MAX_TICKS || (rec->job.flags & RTOS_DIAG_JOB_TICK_INVALID) != 0U) {
        rec->job.flags |= RTOS_DIAG_JOB_TICK_INVALID;
        return;
    }
    rec->job.last_first = elapsed;
    if (elapsed > rec->job.max_first) rec->job.max_first = elapsed;
    rec->job.flags |= RTOS_DIAG_JOB_FIRST_VALID;
}

static void job_abort(rtos_diag_rt_task_record_t *rec)
{
    if ((rec->job.flags & RTOS_DIAG_JOB_ACTIVE) != 0U) {
        rec->job.flags &= ~RTOS_DIAG_JOB_ACTIVE;
        job_count(rec, &rec->job.aborted);
    }
}

void rtos_diag_rt_task_exit_hook(const rtos_task_t *task)
{
    job_abort(&s_rt_tasks[task->id - 1U]);
}

void rtos_diag_rt_switch_hook(rtos_task_t *previous, rtos_task_t *next)
{
    (void)previous;
    const uint32_t now_tick = rtos_tick_get();
    const uint64_t now_cycles = get_cycles_64_locked(now_tick);

    /* 1. Account CPU time for previous running task */
    if (s_rt_global.running_slot != RTOS_DIAG_RT_SLOT_NONE) {
        uint64_t delta = 0ULL;
        if (now_cycles >= s_rt_global.running_dispatch_cycles) {
            delta = now_cycles - s_rt_global.running_dispatch_cycles;
        }
        if (s_rt_global.running_slot == RTOS_DIAG_RT_SLOT_IDLE) {
            s_idle_record.charged_cycles += delta;
        } else if (s_rt_global.running_slot < RTOS_DIAG_RT_MAX_TASKS) {
            rtos_diag_rt_task_record_t *rec = &s_rt_tasks[s_rt_global.running_slot];
            rec->charged_cycles += delta;
            if ((rec->job.flags & RTOS_DIAG_JOB_ACTIVE) != 0U)
                job_check_budget(rec, rec->charged_cycles, false);
        }
    }

    /* 2. Dispatch next running task */
    if (next == NULL || next->id == RTOS_IDLE_TASK_ID) {
        s_rt_global.running_slot = RTOS_DIAG_RT_SLOT_IDLE;
        s_rt_global.running_dispatch_cycles = now_cycles;
        s_idle_record.dispatch_count++;
        s_idle_record.last_dispatch_tick = now_tick;
    } else {
        const uint32_t slot = next->id - 1U;
        if (slot < RTOS_DIAG_RT_MAX_TASKS) {
            rtos_diag_rt_task_record_t *rec = &s_rt_tasks[slot];
            s_rt_global.running_slot = (uint16_t)slot;
            s_rt_global.running_dispatch_cycles = now_cycles;
            rec->dispatch_count++;
            rec->last_dispatch_tick = now_tick;
            job_first_dispatch(rec, now_tick);

            if (rec->last_ready_cycles != 0ULL && now_cycles >= rec->last_ready_cycles) {
                const uint64_t lat_cycles = now_cycles - rec->last_ready_cycles;
                rec->last_ready_latency_cycles = lat_cycles;
                if (lat_cycles > rec->max_ready_latency_cycles) {
                    rec->max_ready_latency_cycles = lat_cycles;
                }
                rec->last_ready_cycles = 0ULL;
            }
            if (now_tick >= rec->last_ready_tick) {
                const uint32_t lat_ticks = now_tick - rec->last_ready_tick;
                rec->last_ready_latency_ticks = lat_ticks;
                if (lat_ticks > rec->max_ready_latency_ticks) {
                    rec->max_ready_latency_ticks = lat_ticks;
                }
            }
        }
    }
}

void rtos_diag_rt_on_tick(uint32_t tick)
{
    const uint64_t cycles = get_cycles_64_locked(tick);
    for (uint32_t i = 0U; i < RTOS_DIAG_RT_MAX_TASKS; ++i) {
        rtos_diag_rt_task_record_t *rec = &s_rt_tasks[i];
        if ((rec->job.flags & RTOS_DIAG_JOB_ACTIVE) != 0U) {
            job_check_deadline(rec, tick - rec->job.release_tick);
            if (s_rt_global.running_slot == i) job_check_budget(rec, task_charge(i, cycles), false);
        }
    }
}

static bool job_api_thread(void)
{
    return rtos_port_task_create_context() && !rtos_port_interrupts_masked();
}

static bool job_api_running(void)
{
    return rtos_sched_started() && rtos_current_task != NULL &&
           rtos_current_task->id != RTOS_IDLE_TASK_ID &&
           rtos_current_task->state == RTOS_TASK_RUNNING;
}

static void increment_u32(uint32_t *counter)
{
    if (*counter != UINT32_MAX) ++*counter;
}

static rtos_status_t job_ref_locked(rtos_task_ref_t ref, bool allow_terminated)
{
    if (ref.slot >= RTOS_DIAG_RT_MAX_TASKS || ref.generation == 0U) return RTOS_ERR_ARGUMENT;
    const rtos_task_t *task = rtos_task_ref_resolve_locked(ref);
    if (task == NULL || s_rt_tasks[ref.slot].generation != ref.generation) {
        increment_u32(&s_stale_requests);
        return RTOS_ERR_STALE;
    }
    if (!allow_terminated && task->state == RTOS_TASK_TERMINATED) return RTOS_ERR_STATE;
    return RTOS_OK;
}

rtos_status_t rtos_diag_rt_configure(rtos_task_ref_t ref, const rtos_diag_rt_config_t *cfg)
{
    if (cfg == NULL || (cfg->flags & ~RTOS_DIAG_JOB_ENABLED) != 0U ||
        cfg->period_ticks > RTOS_DELAY_MAX_TICKS || cfg->deadline_ticks > RTOS_DELAY_MAX_TICKS)
        return RTOS_ERR_ARGUMENT;
    if (!job_api_thread()) return RTOS_ERR_CONTEXT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    rtos_status_t status = job_ref_locked(ref, false);
    if (status == RTOS_OK) {
        rtos_diag_rt_task_record_t *rec = &s_rt_tasks[ref.slot];
        if ((cfg->flags & RTOS_DIAG_JOB_ENABLED) == 0U) {
            job_abort(rec);
            rec->job.flags &= ~RTOS_DIAG_JOB_ENABLED;
        } else if ((rec->job.flags & RTOS_DIAG_JOB_ACTIVE) != 0U) {
            status = RTOS_ERR_STATE;
        } else if (cfg->budget_cycles != 0U && !clock_valid()) {
            status = RTOS_ERR_UNAVAILABLE;
        } else {
            rec->job.deadline_ticks = cfg->deadline_ticks;
            rec->job.budget_cycles = cfg->budget_cycles;
            rec->job.flags |= RTOS_DIAG_JOB_ENABLED;
        }
    }
    rtos_critical_exit(irq);
    return status;
}

rtos_status_t rtos_diag_rt_job_release(rtos_task_ref_t ref, uint32_t *out_job_id)
{
    if (out_job_id == NULL) return RTOS_ERR_ARGUMENT;
    if (!job_api_thread()) return RTOS_ERR_CONTEXT;
    if (!job_api_running()) return RTOS_ERR_STATE;
    const rtos_irq_state_t irq = rtos_critical_enter();
    rtos_status_t status = job_ref_locked(ref, false);
    if (status == RTOS_OK) {
        rtos_diag_rt_task_record_t *rec = &s_rt_tasks[ref.slot];
        if ((rec->job.flags & RTOS_DIAG_JOB_ENABLED) == 0U || (rec->job.flags & RTOS_DIAG_JOB_ACTIVE) != 0U) {
            status = RTOS_ERR_STATE;
            job_count(rec, &rec->job.rejected);
        } else if (rec->job.id == UINT32_MAX) {
            status = RTOS_ERR_CAPACITY;
            increment_u32(&s_exhausted_ids);
        } else {
            const uint32_t tick = rtos_tick_get();
            const uint64_t cycles = get_cycles_64_locked(tick);
            rec->job.flags = (rec->job.flags & RTOS_DIAG_JOB_COUNTER_SATURATED) |
                RTOS_DIAG_JOB_ENABLED | RTOS_DIAG_JOB_ACTIVE;
            if (!clock_valid()) rec->job.flags |= RTOS_DIAG_JOB_CYCLES_INVALID;
            rec->job.release_tick = tick;
            rec->job.release_charge = task_charge(ref.slot, cycles);
            *out_job_id = ++rec->job.id;
            if (s_rt_global.running_slot == ref.slot) job_first_dispatch(rec, tick);
        }
    }
    rtos_critical_exit(irq);
    return status;
}

static rtos_status_t job_end(uint32_t id, bool abort)
{
    if (!job_api_thread()) return RTOS_ERR_CONTEXT;
    if (!job_api_running()) return RTOS_ERR_STATE;
    const rtos_irq_state_t irq = rtos_critical_enter();
    const uint32_t slot = rtos_current_task->id - 1U;
    rtos_diag_rt_task_record_t *rec = &s_rt_tasks[slot];
    rtos_status_t status = RTOS_OK;
    if (id == 0U || rec->job.id != id || (rec->job.flags & RTOS_DIAG_JOB_ACTIVE) == 0U) {
        job_count(rec, &rec->job.invalid);
        status = id == 0U ? RTOS_ERR_ARGUMENT : RTOS_ERR_STATE;
    } else if (abort) {
        job_abort(rec);
    } else {
        const uint32_t tick = rtos_tick_get();
        const uint32_t elapsed = tick - rec->job.release_tick;
        const uint64_t cycles = get_cycles_64_locked(tick);
        job_check_deadline(rec, elapsed);
        job_check_budget(rec, task_charge(slot, cycles), true);
        if ((rec->job.flags & RTOS_DIAG_JOB_TICK_INVALID) == 0U) {
            rec->job.last_finish = elapsed;
            if (elapsed > rec->job.max_finish) rec->job.max_finish = elapsed;
            rec->job.flags |= RTOS_DIAG_JOB_FINISH_VALID;
        }
        rec->job.flags &= ~RTOS_DIAG_JOB_ACTIVE;
        job_count(rec, &rec->job.completed);
    }
    rtos_critical_exit(irq);
    return status;
}

rtos_status_t rtos_diag_rt_job_complete(uint32_t id) { return job_end(id, false); }
rtos_status_t rtos_diag_rt_job_abort(uint32_t id) { return job_end(id, true); }

rtos_status_t rtos_diag_rt_job_snapshot(rtos_task_ref_t ref, rtos_diag_rt_job_snapshot_t *out)
{
    if (out == NULL) return RTOS_ERR_ARGUMENT;
    if (!job_api_thread()) return RTOS_ERR_CONTEXT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    const rtos_status_t status = job_ref_locked(ref, true);
    if (status == RTOS_OK) {
        const rtos_diag_rt_task_record_t *rec = &s_rt_tasks[ref.slot];
        const rt_job_t *job = &rec->job;
        *out = (rtos_diag_rt_job_snapshot_t){
            .flags = rec->job.flags,
            .active_job_id = (rec->job.flags & RTOS_DIAG_JOB_ACTIVE) != 0U ? job->id : 0U,
            .release_tick = job->release_tick,
            .last_first_dispatch_ticks = job->last_first, .max_first_dispatch_ticks = job->max_first,
            .last_finish_ticks = job->last_finish, .max_finish_ticks = job->max_finish,
            .last_budget_cycles = job->last_budget, .max_budget_cycles = job->max_budget,
            .completed_jobs = job->completed, .deadline_misses = job->misses,
            .budget_overruns = job->overruns, .aborted_jobs = job->aborted,
            .rejected_releases = job->rejected, .invalid_jobs = job->invalid,
            .stale_requests = s_stale_requests, .exhausted_ids = s_exhausted_ids
        };
    }
    rtos_critical_exit(irq);
    return status;
}

rtos_status_t rtos_diag_rt_clock_cap(rtos_diag_clock_cap_t *out_cap)
{
    if (out_cap == NULL) {
        return RTOS_ERR_ARGUMENT;
    }
    if (rtos_port_in_handler() || rtos_port_interrupts_masked()) {
        return RTOS_ERR_CONTEXT;
    }

    const rtos_irq_state_t state = rtos_critical_enter();
    const bool clock_avail = rtos_port_clock_available();
    if (!clock_avail) {
        out_cap->clock_type = RTOS_DIAG_CLOCK_UNAVAILABLE;
        out_cap->frequency_hz = 0U;
        out_cap->tick_hz = RTOS_TICK_HZ;
        out_cap->wrap_period_ticks = 0U;
        out_cap->flags = RTOS_DIAG_RT_CLOCK_UNAVAILABLE;
    } else {
        out_cap->clock_type = RTOS_DIAG_CLOCK_CYCLE_ACCURATE;
        out_cap->frequency_hz = rtos_port_cpu_hz();
        out_cap->tick_hz = RTOS_TICK_HZ;
        if (out_cap->frequency_hz > 0U) {
            out_cap->wrap_period_ticks = (uint32_t)((UINT64_C(1) << 32) * RTOS_TICK_HZ / out_cap->frequency_hz);
        } else {
            out_cap->wrap_period_ticks = 0U;
        }
        out_cap->flags = RTOS_DIAG_RT_VALID;
        if (s_rt_global.clock_uncertain) {
            out_cap->flags |= RTOS_DIAG_RT_CLOCK_UNCERTAIN;
        }
    }
    rtos_critical_exit(state);
    return RTOS_OK;
}

rtos_status_t rtos_diag_rt_snapshot(rtos_task_ref_t ref, rtos_diag_rt_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL || ref.slot >= RTOS_DIAG_RT_MAX_TASKS || ref.generation == 0U) {
        return RTOS_ERR_ARGUMENT;
    }
    if (rtos_port_in_handler() || rtos_port_interrupts_masked()) {
        return RTOS_ERR_CONTEXT;
    }

    const rtos_irq_state_t state = rtos_critical_enter();
    rtos_diag_rt_task_record_t *rec = &s_rt_tasks[ref.slot];
    if (rec->generation == 0U) {
        rtos_critical_exit(state);
        return RTOS_ERR_ARGUMENT;
    }
    if (rec->generation != ref.generation) {
        rtos_critical_exit(state);
        return RTOS_ERR_STALE;
    }

    const uint32_t now_tick = rtos_tick_get();
    const uint64_t now_cycles = get_cycles_64_locked(now_tick);

    out_snapshot->ref = ref;
    out_snapshot->last_ready_latency_cycles = rec->last_ready_latency_cycles;
    out_snapshot->max_ready_latency_cycles = rec->max_ready_latency_cycles;
    out_snapshot->last_ready_latency_ticks = rec->last_ready_latency_ticks;
    out_snapshot->max_ready_latency_ticks = rec->max_ready_latency_ticks;
    out_snapshot->dispatch_count = rec->dispatch_count;
    out_snapshot->last_dispatch_tick = rec->last_dispatch_tick;

    /* Compute charged cycles including in-flight execution if currently running */
    uint64_t in_flight = 0ULL;
    if (s_rt_global.running_slot == (uint16_t)ref.slot && now_cycles >= s_rt_global.running_dispatch_cycles) {
        in_flight = now_cycles - s_rt_global.running_dispatch_cycles;
    }
    out_snapshot->charged_cycles = rec->charged_cycles + in_flight;

    /* Query task state from kernel */
    rtos_task_t *task = rtos_task_ref_resolve_locked(ref);
    if (task != NULL) {
        out_snapshot->state = (uint16_t)task->state;
        out_snapshot->base_priority = task->base_priority;
        out_snapshot->effective_priority = task->effective_priority;
    } else {
        out_snapshot->state = (uint16_t)RTOS_TASK_TERMINATED;
        out_snapshot->base_priority = 0U;
        out_snapshot->effective_priority = 0U;
    }

    /* Set status flags */
    out_snapshot->flags = RTOS_DIAG_RT_VALID;
    if (!rtos_port_clock_available()) {
        out_snapshot->flags |= RTOS_DIAG_RT_CLOCK_UNAVAILABLE;
    }
    if (s_rt_global.clock_uncertain) {
        out_snapshot->flags |= RTOS_DIAG_RT_CLOCK_UNCERTAIN;
    }

    rtos_critical_exit(state);
    return RTOS_OK;
}

rtos_status_t rtos_diag_rt_idle_snapshot(rtos_diag_rt_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return RTOS_ERR_ARGUMENT;
    }
    if (rtos_port_in_handler() || rtos_port_interrupts_masked()) {
        return RTOS_ERR_CONTEXT;
    }

    const rtos_irq_state_t state = rtos_critical_enter();
    const uint32_t now_tick = rtos_tick_get();
    const uint64_t now_cycles = get_cycles_64_locked(now_tick);

    out_snapshot->ref.slot = RTOS_DIAG_RT_MAX_TASKS;
    out_snapshot->ref.generation = 1U;
    out_snapshot->last_ready_latency_cycles = 0ULL;
    out_snapshot->max_ready_latency_cycles = 0ULL;
    out_snapshot->last_ready_latency_ticks = 0U;
    out_snapshot->max_ready_latency_ticks = 0U;
    out_snapshot->dispatch_count = s_idle_record.dispatch_count;
    out_snapshot->last_dispatch_tick = s_idle_record.last_dispatch_tick;
    out_snapshot->base_priority = 0U;
    out_snapshot->effective_priority = 0U;

    uint64_t in_flight = 0ULL;
    if (s_rt_global.running_slot == RTOS_DIAG_RT_SLOT_IDLE) {
        out_snapshot->state = (uint16_t)RTOS_TASK_RUNNING;
        if (now_cycles >= s_rt_global.running_dispatch_cycles) {
            in_flight = now_cycles - s_rt_global.running_dispatch_cycles;
        }
    } else {
        out_snapshot->state = (uint16_t)RTOS_TASK_READY;
    }
    out_snapshot->charged_cycles = s_idle_record.charged_cycles + in_flight;

    out_snapshot->flags = RTOS_DIAG_RT_VALID;
    if (!rtos_port_clock_available()) {
        out_snapshot->flags |= RTOS_DIAG_RT_CLOCK_UNAVAILABLE;
    }
    if (s_rt_global.clock_uncertain) {
        out_snapshot->flags |= RTOS_DIAG_RT_CLOCK_UNCERTAIN;
    }

    rtos_critical_exit(state);
    return RTOS_OK;
}

#if RTOS_TESTING
void rtos_diag_rt_test_set_job_id(uint32_t slot, uint32_t id)
{
    RTOS_ASSERT(slot < RTOS_DIAG_RT_MAX_TASKS);
    s_rt_tasks[slot].job.id = id;
}

void rtos_diag_rt_test_reset(void)
{
    const rtos_irq_state_t state = rtos_critical_enter();
    rtos_diag_rt_init();
    s_stale_requests = s_exhausted_ids = 0U;
    for (size_t i = 0U; i < RTOS_DIAG_RT_MAX_TASKS; ++i) {
        s_rt_tasks[i].charged_cycles = 0ULL;
        s_rt_tasks[i].last_ready_cycles = 0ULL;
        s_rt_tasks[i].last_ready_latency_cycles = 0ULL;
        s_rt_tasks[i].max_ready_latency_cycles = 0ULL;
        s_rt_tasks[i].generation = 0U;
        s_rt_tasks[i].last_ready_tick = 0U;
        s_rt_tasks[i].last_ready_latency_ticks = 0U;
        s_rt_tasks[i].max_ready_latency_ticks = 0U;
        s_rt_tasks[i].dispatch_count = 0U;
        s_rt_tasks[i].last_dispatch_tick = 0U;
        s_rt_tasks[i].job = (rt_job_t){0};
    }
    rtos_critical_exit(state);
}
#endif

#endif /* INSIGHT_DIAG_RT */
