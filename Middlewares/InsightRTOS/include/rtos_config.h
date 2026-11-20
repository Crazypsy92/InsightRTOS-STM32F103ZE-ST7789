#ifndef INSIGHT_RTOS_CONFIG_H
#define INSIGHT_RTOS_CONFIG_H

#include <stdint.h>

#ifndef RTOS_QUEUE_MAX_ITEM_SIZE
#define RTOS_QUEUE_MAX_ITEM_SIZE 64U
#endif
_Static_assert(RTOS_QUEUE_MAX_ITEM_SIZE > 0U && RTOS_QUEUE_MAX_ITEM_SIZE <= UINT32_MAX,
               "Queue item limit must be positive and fit uint32_t");

#ifndef RTOS_PREEMPTIVE
#define RTOS_PREEMPTIVE 1
#endif

#ifndef RTOS_MUTEX_PI
#define RTOS_MUTEX_PI 1
#endif

#ifndef RTOS_TICK_HZ
#define RTOS_TICK_HZ 100U
#endif

#ifndef RTOS_TIME_SLICE_TICKS
#define RTOS_TIME_SLICE_TICKS 1U
#endif

#ifndef RTOS_TESTING
#define RTOS_TESTING 0
#endif

_Static_assert((RTOS_MUTEX_PI == 0) || (RTOS_MUTEX_PI == 1), "PI must be zero or one");
_Static_assert(RTOS_TESTING || RTOS_MUTEX_PI, "Disabling PI is test-only");

_Static_assert((RTOS_PREEMPTIVE == 0) || (RTOS_PREEMPTIVE == 1),
               "RTOS_PREEMPTIVE must be zero or one");
_Static_assert((RTOS_TESTING == 0) || (RTOS_TESTING == 1),
               "RTOS_TESTING must be zero or one");
_Static_assert((RTOS_TICK_HZ > 0) && (RTOS_TICK_HZ <= UINT32_MAX),
               "The tick frequency must fit a nonzero uint32_t");
_Static_assert((RTOS_TIME_SLICE_TICKS > 0) &&
               (RTOS_TIME_SLICE_TICKS <= UINT32_MAX),
               "A time slice must fit a nonzero uint32_t");

#endif
