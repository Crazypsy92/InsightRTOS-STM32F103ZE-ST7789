#include "app_tasks.h"
#include "job_fixture.h"
#include "main.h"

typedef struct {
    uint32_t seq_begin;
    rtos_task_ref_t ref;
    rtos_diag_rt_job_snapshot_t on_time, late, over_budget, aborted;
    uint32_t tick, budget_cycles;
    uint32_t seq_end;
} job_fixture_t;

volatile job_fixture_t g_job_fixture;

void insight_job_fixture(void *argument)
{
    (void)argument;
    rtos_task_ref_t self;
    uint32_t id;
    rtos_diag_rt_job_snapshot_t snap;
    RTOS_ASSERT(rtos_task_self_ref(&self) == RTOS_OK);
    RTOS_ASSERT(rtos_delay(300U) == RTOS_OK);
    g_job_fixture.seq_begin = 1U;
    __DMB();
    g_job_fixture.ref = self;

    const rtos_diag_rt_config_t timed = {RTOS_DIAG_JOB_ENABLED, 0U, 0U, 1U};
    RTOS_ASSERT(rtos_diag_rt_configure(self, &timed) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_rt_job_release(self, &id) == RTOS_OK);
    RTOS_ASSERT(rtos_delay(1U) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_rt_job_complete(id) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_rt_job_snapshot(self, &snap) == RTOS_OK);
    RTOS_ASSERT(snap.completed_jobs == 1U && snap.deadline_misses == 0U && snap.last_finish_ticks == 1U);
    g_job_fixture.on_time = snap;

    RTOS_ASSERT(rtos_diag_rt_job_release(self, &id) == RTOS_OK);
    RTOS_ASSERT(rtos_delay(2U) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_rt_job_complete(id) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_rt_job_snapshot(self, &snap) == RTOS_OK);
    RTOS_ASSERT(snap.completed_jobs == 2U && snap.deadline_misses == 1U && snap.last_finish_ticks == 2U);
    g_job_fixture.late = snap;

    const rtos_diag_rt_config_t budget = {RTOS_DIAG_JOB_ENABLED, 0U, 100000U, 0U};
    g_job_fixture.budget_cycles = budget.budget_cycles;
    RTOS_ASSERT(rtos_diag_rt_configure(self, &budget) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_rt_job_release(self, &id) == RTOS_OK);
    const uint32_t start = DWT->CYCCNT;
    for (uint32_t n = 0U; n < 200000U && (uint32_t)(DWT->CYCCNT - start) < 150000U; ++n) {
        __NOP();
    }
    RTOS_ASSERT((uint32_t)(DWT->CYCCNT - start) >= 150000U);
    RTOS_ASSERT(rtos_diag_rt_job_complete(id) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_rt_job_snapshot(self, &snap) == RTOS_OK);
    RTOS_ASSERT(snap.completed_jobs == 3U && snap.deadline_misses == 1U && snap.budget_overruns == 1U);
    RTOS_ASSERT((snap.flags & RTOS_DIAG_JOB_BUDGET_VALID) != 0U);
    g_job_fixture.over_budget = snap;

    RTOS_ASSERT(rtos_diag_rt_job_release(self, &id) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_rt_job_abort(id) == RTOS_OK);
    RTOS_ASSERT(rtos_diag_rt_job_snapshot(self, &snap) == RTOS_OK);
    RTOS_ASSERT(!snap.active_job_id && snap.aborted_jobs == 1U && snap.invalid_jobs == 0U);
    g_job_fixture.aborted = snap;
    g_job_fixture.tick = rtos_tick_get();
    g_job_fixture.seq_end = 2U;
    __DMB();
    g_job_fixture.seq_begin = 2U;
    rtos_task_exit();
}
