#include "app_diagnostics.h"
#include "delay.h"
#include "main.h"

app_diag_snapshot_t g_app_diag;
static rtos_diag_dl_workspace_t s_graph;
static volatile uint32_t s_attempt_seq;
static volatile app_diag_job_attempt_t s_attempt;

void App_Diag_ConfigureController(rtos_task_ref_t ref)
{
    const rtos_diag_rt_config_t config = {RTOS_DIAG_JOB_ENABLED, 0U, 72000U, 1U};
    s_attempt.configured = rtos_diag_rt_configure(ref, &config);
}

uint32_t App_Diag_BeginCommand(uint32_t event_id)
{
    ++s_attempt_seq;
    __DMB();
    uint32_t id = 0U;
    s_attempt.event_id = event_id;
    s_attempt.release = s_attempt.configured == RTOS_OK ?
        rtos_diag_rt_job_release(g_display_task_refs[1], &id) : s_attempt.configured;
    s_attempt.job_id = id;
    return id;
}

void App_Diag_EndCommand(uint32_t job_id, bool sent)
{
    s_attempt.command_sent = sent ? 1U : 0U;
    s_attempt.complete = job_id != 0U ? rtos_diag_rt_job_complete(job_id) : RTOS_ERR_STATE;
    __DMB();
    ++s_attempt_seq;
}

static void capture_trace(app_diag_snapshot_t *out)
{
    rtos_trace_record_t records[RTOS_DIAG_TRACE_BATCH_MAX];
    out->trace_tick = rtos_tick_get();
    out->trace_capacity = RTOS_TRACE_CAPACITY;
    out->trace_schema = RTOS_TRACE_SCHEMA_VERSION;
    out->trace_event_set = RTOS_TRACE_EVENT_SET_VERSION;
    out->trace_status = rtos_diag_trace_read_latest(records, RTOS_DIAG_TRACE_BATCH_MAX, &out->trace);
    out->has_switch = 0U;
    out->explain_status = RTOS_ERR_UNAVAILABLE;
    if (out->trace_status != RTOS_OK) return;

    for (uint32_t i = out->trace.copied; i > 0U; --i) {
        const rtos_trace_record_t *record = &records[i - 1U];
        if (record->event_type == RTOS_TRACE_CONTEXT_SWITCH) {
            out->switch_record = *record;
            out->explain_status = rtos_diag_explain_record(record, &out->switch_reason);
            out->has_switch = out->explain_status == RTOS_OK ? 1U : 0U;
            break;
        }
    }
}

static void capture_rt(app_diag_snapshot_t *out)
{
    out->rt_begin_tick = rtos_tick_get();
    out->clock_status = rtos_diag_rt_clock_cap(&out->clock);
    out->rt_read_mask = 0U;
    for (uint32_t i = 0U; i < 4U; ++i) {
        if (rtos_diag_rt_snapshot(g_display_task_refs[i], &out->tasks[i]) == RTOS_OK)
            out->rt_read_mask |= UINT32_C(1) << i;
    }
    if (rtos_diag_rt_idle_snapshot(&out->idle) == RTOS_OK) out->rt_read_mask |= UINT32_C(1) << 4;
    const uint32_t before = s_attempt_seq;
    __DMB();
    out->attempt = s_attempt;
    out->job_status = rtos_diag_rt_job_snapshot(g_display_task_refs[1], &out->job);
    __DMB();
    out->job_pair_valid = (before == s_attempt_seq && before != 0U && (before & 1U) == 0U &&
        out->attempt.release == RTOS_OK && out->attempt.complete == RTOS_OK &&
        out->job_status == RTOS_OK && out->job.active_job_id == 0U) ? 1U : 0U;
    out->rt_end_tick = rtos_tick_get();
}

static void capture_dl(app_diag_snapshot_t *out)
{
    out->dl_capture_status = rtos_diag_dl_capture(&s_graph);
    out->dl_analyze_status = RTOS_ERR_STATE;
    out->dl_observation = APP_DL_UNAVAILABLE;
    if (out->dl_capture_status != RTOS_OK) return;

    out->dl_analyze_status = rtos_diag_dl_analyze(&s_graph);
    out->dl_header = s_graph.header;
    if (out->dl_analyze_status != RTOS_OK) return;

    out->dl = s_graph.result;
    if (out->dl.flags & (RTOS_DIAG_DL_RES_INCOMPLETE | RTOS_DIAG_DL_RES_ANOMALY)) {
        out->dl_observation = APP_DL_INCOMPLETE;
    } else {
        out->dl_observation = out->dl.deadlock_detected ?
            APP_DL_CYCLE_OBSERVED : APP_DL_NO_DEADLOCK_OBSERVED;
    }
}

void App_Diag_Publish(void)
{
    app_diag_snapshot_t *const out = &g_app_diag;
    const uint32_t now = rtos_tick_get();
    if (out->seq_begin != 0U && now - out->begin_tick < 100U) return;

    uint32_t seq = out->seq_begin + 2U;
    if (seq == 0U) seq = 2U;
    out->seq_begin = seq - 1U;
    __DMB();

    out->schema_version = APP_DIAG_SCHEMA_VERSION;
    out->bytes = sizeof(*out);
    out->begin_tick = now;
    App_Events_GetSnapshot(&out->events);
    out->events_ms = GetTick_ms();
    out->ack = App_Events_AckTiming(&out->events);
    App_IPC_GetQueueDepths(out->queue_depths);
    out->queues_ms = GetTick_ms();
    capture_trace(out);
    capture_rt(out);
    capture_dl(out);
    out->end_tick = rtos_tick_get();
    out->seq_end = seq;
    __DMB();
    out->seq_begin = seq;
}
