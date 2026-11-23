#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include "app_config.h"
#include "key_port.h"
#include <stdint.h>
#include <stdbool.h>

#define DISPLAY_EVENT_HISTORY APP_EVENT_HISTORY_CAPACITY

typedef enum {
    EVENT_STAGE_IDLE = 0,
    EVENT_STAGE_IDENTIFIED, /* in ISR: MultiButton identified event */
    EVENT_STAGE_KEY,        /* in KEY task: received from raw_keys */
    EVENT_STAGE_CTRL,       /* in Controller task: received from controller_keys */
    EVENT_STAGE_LCD,        /* in LCD task: rendered on screen */
    EVENT_STAGE_ACKED,      /* in Controller task: verified via matching ACK */
    EVENT_STAGE_FAILED      /* error during execution or queue overflow drop */
} display_event_stage_t;

typedef struct {
    uint32_t id;
    key_event_t event;
    display_event_stage_t stage;
    uint32_t timestamp_ms;
    uint32_t ack_ms;
    int32_t error;
} display_tracked_event_t;

typedef struct {
    display_tracked_event_t history[APP_EVENT_HISTORY_CAPACITY];
    uint32_t latest_id;
    uint32_t confirmed_id;
    key_event_t latest_event;
    uint32_t completed_ms;
    uint32_t success_count;
    uint32_t failure_count;
    uint32_t retry_count;
    uint32_t drop_count;
    uint32_t evicted_unacked_count; /* Evicted from bounded history before ACK */
    uint32_t orphaned_ack_count;    /* ACK processed after event slot eviction */
} display_event_tracker_t;

extern volatile display_event_tracker_t g_event_tracker;

uint32_t App_Events_AllocateId(key_event_t event, uint32_t now_ms, bool is_dropped);
void App_Events_UpdateStage(uint32_t id, display_event_stage_t expected_stage, display_event_stage_t new_stage);
void App_Events_RecordAck(uint32_t id, int32_t error, uint32_t now_ms);
void App_Events_RecordRetry(void);
display_tracked_event_t *App_Events_FindTracked(uint32_t id);
void App_Events_GetSnapshot(display_event_tracker_t *out_snapshot);

#endif /* APP_EVENTS_H */
