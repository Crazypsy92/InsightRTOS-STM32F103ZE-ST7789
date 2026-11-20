#include <stdint.h>

#include "delay_queue.h"
#include "kernel_internal.h"
#include "rtos_assert.h"
#include "rtos_critical.h"
#include "rtos_tick.h"

static uint32_t kernel_ticks;

uint32_t rtos_tick_get(void)
{
    const rtos_irq_state_t state = rtos_critical_enter();
    const uint32_t snapshot = kernel_ticks;
    rtos_critical_exit(state);
    return snapshot;
}

void rtos_tick_handler(void)
{
    const rtos_irq_state_t state = rtos_critical_enter();
    if (rtos_sched_started()) {
        /* uint32_t arithmetic deliberately wraps modulo 2^32. */
        ++kernel_ticks;
        /* Charge only peers that were runnable during this interval. Newly
         * awakened equal-priority tasks join its FIFO tail after accounting. */
        rtos_sched_on_tick();
        rtos_delay_process_expired(kernel_ticks);
    }
    rtos_critical_exit(state);
}

#if RTOS_TESTING
void rtos_tick_test_set(uint32_t tick)
{
    const rtos_irq_state_t state = rtos_critical_enter();
    /* Arbitrary jumps would invalidate the queue's modular ordering. */
    RTOS_ASSERT(rtos_delay_queue_empty());
    kernel_ticks = tick;
    rtos_critical_exit(state);
}

void rtos_tick_test_advance(uint32_t ticks)
{
    const rtos_irq_state_t state = rtos_critical_enter();
    RTOS_ASSERT(rtos_sched_started());
    RTOS_ASSERT(ticks <= RTOS_DELAY_MAX_TICKS);
    kernel_ticks += ticks;
    /* Test-only deadline acceleration, deliberately not synthetic CPU time. */
    rtos_delay_process_expired(kernel_ticks);
    rtos_critical_exit(state);
}
#endif

#if INSIGHT_TRACE_ENABLED
uint32_t rtos_trace_tick_locked(void) { return kernel_ticks; }
#endif
