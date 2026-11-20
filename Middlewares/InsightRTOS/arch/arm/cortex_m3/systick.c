#include "port.h"
#include "rtos_assert.h"
#include "rtos_config.h"
#include "kernel_internal.h"

#define SYST_CSR (*(volatile uint32_t *)UINT32_C(0xe000e010))
#define SYST_RVR (*(volatile uint32_t *)UINT32_C(0xe000e014))
#define SYST_CVR (*(volatile uint32_t *)UINT32_C(0xe000e018))
#define SCB_ICSR (*(volatile uint32_t *)UINT32_C(0xe000ed04))

static bool systick_configured;

void rtos_port_tick_init(void)
{
    SYST_CSR = 0U;
    SCB_ICSR = (UINT32_C(1) << 25) | (UINT32_C(1) << 27);
    uint32_t reload = 0U;
    RTOS_ASSERT(rtos_port_systick_reload(rtos_port_cpu_hz(), RTOS_TICK_HZ, &reload));
    SYST_RVR = reload;
    SYST_CVR = 0U;
    systick_configured = true;
    __asm volatile("dsb\nisb" ::: "memory");
}

void rtos_port_tick_start(void)
{
    RTOS_ASSERT(systick_configured);
#if RTOS_PREEMPTIVE
    SYST_CVR = 0U;
    /* Processor clock, exception request enabled, counter enabled. */
    SYST_CSR = UINT32_C(7);
    __asm volatile("dsb\nisb" ::: "memory");
#endif
}

void SysTick_Handler(void)
{
    rtos_tick_handler();
}
