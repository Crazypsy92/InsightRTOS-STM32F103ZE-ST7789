#include "port.h"
#include "rtos_assert.h"
#include "rtos_critical.h"

rtos_irq_state_t rtos_critical_enter(void)
{
    RTOS_ASSERT(!rtos_port_in_unmaskable_handler());
    /* The port supplies compiler and processor ordering around PRIMASK. */
    return rtos_port_irq_save();
}

void rtos_critical_exit(rtos_irq_state_t state)
{
    RTOS_ASSERT(!rtos_port_in_unmaskable_handler());
    RTOS_ASSERT(state <= UINT32_C(1));
    rtos_port_irq_restore(state);
}
