#ifndef INSIGHT_RTOS_TRACE_H
#define INSIGHT_RTOS_TRACE_H
#include "rtos_config.h"
#include "rtos_task.h"

#ifndef INSIGHT_TRACE_ENABLED
#define INSIGHT_TRACE_ENABLED 0
#endif
_Static_assert(INSIGHT_TRACE_ENABLED == 0 || INSIGHT_TRACE_ENABLED == 1, "Trace boolean");
#define RTOS_TRACE_SCHEMA_VERSION 1U
#define RTOS_TRACE_CAPACITY 128U
#define RTOS_TRACE_BATCH_MAX 8U
#define RTOS_TRACE_IDLE_SLOT UINT16_MAX
#define RTOS_TRACE_UNKNOWN_SLOT (UINT16_MAX - 1U)
typedef enum {
    RTOS_TRACE_TASK_CREATE = 1, RTOS_TRACE_TASK_READY, RTOS_TRACE_TASK_BLOCK,
    RTOS_TRACE_SCHED_SELECT, RTOS_TRACE_WAIT_COMPLETE, RTOS_TRACE_PRIORITY_CHANGE,
    RTOS_TRACE_STOP_REQUEST, RTOS_TRACE_TASK_TERMINATED, RTOS_TRACE_TASK_RECLAIM
} rtos_trace_event_t;
typedef struct {
    uint32_t sequence, tick, generation, arg0, arg1;
    uint16_t slot, event_type;
} rtos_trace_record_t;
typedef struct {
    uint32_t count, capacity, oldest_sequence, next_sequence, overwritten;
} rtos_trace_stats_t;
_Static_assert(sizeof(rtos_trace_record_t) == 24U, "Trace schema size");
_Static_assert(_Alignof(rtos_trace_record_t) == 4U, "Trace schema alignment");
_Static_assert(RTOS_TRACE_CAPACITY == 128U && RTOS_TRACE_BATCH_MAX <= RTOS_TRACE_CAPACITY, "Bounded trace storage/copy");

#if INSIGHT_TRACE_ENABLED
/* Read-only, non-consuming. Thread including prestart/Idle; outer PRIMASK is
 * retained. BASEPRI/FAULTMASK/Handlers rejected. Caller provides valid writable,
 * independent nonoverlapping outputs. Stats/copy each observe one instant.
 * Cursor must be in current window or next_sequence; stale failure leaves all
 * outputs unchanged. Max 8 records, uint32 sequences/ticks wrap modulo 2^32.
 * No init/reset dependency: BSS starts empty. Not a complete history if covered.
 */
rtos_status_t rtos_trace_get_stats(rtos_trace_stats_t *out);
rtos_status_t rtos_trace_read(uint32_t first_sequence, rtos_trace_record_t *out,
                             uint32_t capacity, uint32_t *copied);
#endif
#endif
