#include "backpressure_fixture.h"

#define GATE_TICKS 150U

typedef struct {
    uint32_t full_count, full_tick, full_depth, full_sequence, full_committed;
    int32_t full_status;
    uint32_t sent_sequence, sent_tick, lcd_sequence, lcd_tick, ack_sequence, ack_tick;
    int32_t ack_error;
} bp_event_t;

typedef struct {
    uint32_t gate_start, gate_deadline, gate_end, complete, command_max_depth;
    uint32_t full_total, sent, executed, acked;
    bp_event_t events[BP_EVENTS];
} bp_capture_t;

volatile bp_capture_t g_backpressure;
extern volatile uint32_t g_stress_request;

void BP_GateConsumer(void)
{
    g_backpressure.gate_start = rtos_tick_get();
    g_backpressure.gate_deadline = g_backpressure.gate_start + GATE_TICKS;
    g_stress_request = 3U;
    RTOS_ASSERT(rtos_delay(GATE_TICKS) == RTOS_OK);
    g_backpressure.gate_end = rtos_tick_get();
}

static volatile bp_event_t *event_at(uint32_t id)
{
    RTOS_ASSERT(id > 0U && id <= BP_EVENTS);
    return &g_backpressure.events[id - 1U];
}

void BP_CommandAttempt(const display_command_t *command, rtos_status_t status, uint32_t committed)
{
    if (command->id == 0U || g_backpressure.complete) return;
    volatile bp_event_t *event = event_at(command->id);
    uint32_t depths[5];
    App_IPC_GetQueueDepths(depths);
    if (depths[2] > g_backpressure.command_max_depth) g_backpressure.command_max_depth = depths[2];
    RTOS_ASSERT(!event->sent_sequence);
    if (status == RTOS_ERR_FULL) {
        RTOS_ASSERT(depths[2] == APP_COMMAND_QUEUE_CAPACITY &&
                    committed + 1U == command->sequence);
        if (event->full_count == 0U) {
            event->full_tick = rtos_tick_get();
            event->full_depth = depths[2];
            event->full_sequence = command->sequence;
            event->full_committed = committed;
            event->full_status = status;
        }
        RTOS_ASSERT(event->full_sequence == command->sequence && event->full_committed == committed);
        ++event->full_count;
        ++g_backpressure.full_total;
    } else {
        RTOS_ASSERT(status == RTOS_OK && committed == command->sequence);
        event->sent_tick = rtos_tick_get();
        event->sent_sequence = command->sequence;
        ++g_backpressure.sent;
    }
}

void BP_Executed(const display_command_t *command)
{
    if (command->id == 0U || g_backpressure.complete) return;
    volatile bp_event_t *event = event_at(command->id);
    RTOS_ASSERT(event->sent_sequence == command->sequence && !event->lcd_sequence);
    event->lcd_tick = rtos_tick_get();
    event->lcd_sequence = command->sequence;
    ++g_backpressure.executed;
}

void BP_Ack(const display_ack_t *ack)
{
    if (ack->id == 0U || g_backpressure.complete) return;
    volatile bp_event_t *event = event_at(ack->id);
    RTOS_ASSERT(event->lcd_sequence == ack->sequence && !event->ack_sequence);
    event->ack_tick = rtos_tick_get();
    event->ack_sequence = ack->sequence;
    event->ack_error = ack->error;
    if (++g_backpressure.acked == BP_EVENTS) {
        __asm volatile("dmb" ::: "memory");
        g_backpressure.complete = 1U;
    }
}
