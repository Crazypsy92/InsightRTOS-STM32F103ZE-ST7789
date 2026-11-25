#ifndef APP_DIAGNOSTICS_H
#define APP_DIAGNOSTICS_H

#include "app_tasks.h"
#include "rtos_diag.h"

#define APP_DIAG_SCHEMA_VERSION 1U

enum app_dl_observation {
    APP_DL_UNAVAILABLE,
    APP_DL_NO_DEADLOCK_OBSERVED,
    APP_DL_CYCLE_OBSERVED,
    APP_DL_INCOMPLETE
};

typedef struct {
    uint32_t event_id, job_id, command_sent;
    int32_t configured, release, complete;
} app_diag_job_attempt_t;

typedef struct {
    volatile uint32_t seq_begin;
    uint32_t schema_version, bytes, begin_tick, end_tick;
    uint32_t events_ms, queues_ms, queue_depths[5];
    display_event_tracker_t events;
    app_event_ack_timing_t ack;
    uint32_t trace_tick, trace_capacity, trace_schema, trace_event_set, has_switch;
    int32_t trace_status, explain_status;
    rtos_diag_trace_batch_t trace;
    rtos_trace_record_t switch_record;
    rtos_diag_explain_t switch_reason;
    uint32_t rt_begin_tick, rt_end_tick, rt_read_mask;
    int32_t clock_status, job_status;
    rtos_diag_clock_cap_t clock;
    rtos_diag_rt_snapshot_t tasks[4], idle;
    rtos_diag_rt_job_snapshot_t job;
    app_diag_job_attempt_t attempt;
    uint32_t job_pair_valid;
    int32_t dl_capture_status, dl_analyze_status;
    enum app_dl_observation dl_observation;
    rtos_diag_dl_header_t dl_header;
    rtos_diag_dl_result_t dl;
    volatile uint32_t seq_end;
} app_diag_snapshot_t;

extern app_diag_snapshot_t g_app_diag;

void App_Diag_ConfigureController(rtos_task_ref_t ref);
uint32_t App_Diag_BeginCommand(uint32_t event_id);
void App_Diag_EndCommand(uint32_t job_id, bool sent);
void App_Diag_Publish(void);

#endif
