#include "msp_bench.h"
#include "delay.h"
#include "main.h"
#include "port.h"

#define MSP_BYTES 4096U
#define MSP_WORDS (MSP_BYTES / sizeof(uint32_t))
#define MSP_MIN_MARGIN 512U

enum msp_state { MSP_BOOT, MSP_NORMAL, MSP_LOAD, MSP_DONE, MSP_FAILED };
enum irq_slot { MSP_SVC, MSP_PENDSV, MSP_SYSTICK, MSP_TIM2, MSP_IRQ_COUNT };
enum nested_slot { MSP_SYSTICK_OVER_TIM2, MSP_SYSTICK_OVER_PENDSV,
                   MSP_TIM2_OVER_PENDSV, MSP_NESTED_COUNT };

typedef struct {
    uint32_t touched[MSP_WORDS / 32U];
    uint32_t guard[MSP_BENCH_GUARD_WORDS];
    uint32_t calls[MSP_IRQ_COUNT], entry_min[MSP_IRQ_COUNT], nested[MSP_NESTED_COUNT];
    uint32_t start_tick, finish_tick, start_ms, finish_ms;
    uint32_t lowest_touched;
} msp_phase_t;

typedef struct {
    uint32_t state, fault, bottom, top, clock_hz, pressure_events;
    msp_phase_t phases[3];
} msp_capture_t;

extern uint32_t __msp_stack_bottom__[MSP_WORDS], __msp_stack_top__[];
extern volatile uint32_t g_stress_request, g_stress_generated;

volatile msp_capture_t g_msp_bench;

void msp_bench_observe(uint32_t exception, uint32_t msp)
{
    const uint32_t state = g_msp_bench.state;
    if (state > MSP_LOAD) return;
    enum irq_slot slot;
    switch (exception) {
        case 11U: slot = MSP_SVC; break;
        case 14U: slot = MSP_PENDSV; break;
        case 15U: slot = MSP_SYSTICK; break;
        case 16U + (uint32_t)TIM2_IRQn: slot = MSP_TIM2; break;
        default: return;
    }

    volatile msp_phase_t *phase = &g_msp_bench.phases[state];
    ++phase->calls[slot];
    if (!phase->entry_min[slot] || msp < phase->entry_min[slot]) phase->entry_min[slot] = msp;
    if (msp <= (uintptr_t)__msp_stack_bottom__ + MSP_MIN_MARGIN ||
        msp > (uintptr_t)__msp_stack_top__ || (msp & 3U) != 0U) {
        g_msp_bench.fault = 1U;
    }

    /* A lower-priority exception remains ACTIVE until its return. */
    const bool pendsv_active = (SCB->SHCSR & SCB_SHCSR_PENDSVACT_Msk) != 0U;
    if (slot == MSP_SYSTICK) {
        if ((NVIC->IABR[0] & (UINT32_C(1) << TIM2_IRQn)) != 0U)
            ++phase->nested[MSP_SYSTICK_OVER_TIM2];
        if (pendsv_active) ++phase->nested[MSP_SYSTICK_OVER_PENDSV];
    }
    if (slot == MSP_TIM2 && pendsv_active) ++phase->nested[MSP_TIM2_OVER_PENDSV];
}

static bool guard_intact(const volatile uint32_t *stack)
{
    for (uint32_t i = 0U; i < MSP_BENCH_GUARD_WORDS; ++i) {
        if (stack[i] != MSP_BENCH_GUARD) return false;
    }
    return true;
}

static void close_phase(volatile msp_phase_t *phase, const volatile uint32_t *stack)
{
    phase->finish_ms = GetTick_ms();
    phase->finish_tick = rtos_tick_get();
    phase->lowest_touched = (uint32_t)(uintptr_t)__msp_stack_top__;
    for (uint32_t i = 0U; i < MSP_BENCH_GUARD_WORDS; ++i) phase->guard[i] = stack[i];
    for (uint32_t i = MSP_BENCH_GUARD_WORDS; i < MSP_WORDS; ++i) {
        if (stack[i] != MSP_BENCH_FILL) {
            if (phase->lowest_touched == (uintptr_t)__msp_stack_top__)
                phase->lowest_touched = (uint32_t)(uintptr_t)&stack[i];
            phase->touched[i / 32U] |= UINT32_C(1) << (i % 32U);
        }
    }
}

static void arm_phase(volatile msp_phase_t *phase, volatile uint32_t *stack)
{
    for (uint32_t i = MSP_BENCH_GUARD_WORDS; i < MSP_WORDS; ++i) stack[i] = MSP_BENCH_FILL;
    __DMB();
    phase->start_ms = GetTick_ms();
    phase->start_tick = rtos_tick_get();
}

void msp_bench_controller_step(bool display_ready)
{
    const uint32_t state = g_msp_bench.state;
    if (state > MSP_LOAD) return;
    const uintptr_t bottom = (uintptr_t)__msp_stack_bottom__;
    const uintptr_t top = (uintptr_t)__msp_stack_top__;
    volatile uint32_t *const stack = (volatile uint32_t *)bottom;
    const bool bounds_valid = top > bottom && top - bottom == MSP_BYTES && (bottom & 7U) == 0U;
    const bool healthy = !g_msp_bench.fault && bounds_valid && guard_intact(stack);
    volatile msp_phase_t *const phase = &g_msp_bench.phases[state];
    if (healthy) {
        if (state == MSP_BOOT) {
            if (!display_ready || rtos_tick_get() < 300U) return;
        } else if (rtos_tick_get() - phase->start_tick < 6200U) {
            return;
        }
    }

    const rtos_irq_state_t saved = rtos_critical_enter();
    uint32_t msp, ipsr, control;
    __asm volatile("mrs %0, msp\nmrs %1, ipsr\nmrs %2, control" : "=r"(msp), "=r"(ipsr), "=r"(control));
    const bool clean_msp = bounds_valid && ipsr == 0U && (control & 2U) != 0U && msp == top;
    if (clean_msp) close_phase(phase, stack);
    g_msp_bench.bottom = bottom;
    g_msp_bench.top = top;
    g_msp_bench.clock_hz = rtos_port_cpu_hz();
    if (!healthy || !clean_msp || !guard_intact(stack) || g_msp_bench.fault ||
        phase->lowest_touched <= bottom + MSP_MIN_MARGIN ||
        !rtos_port_clock_available() || g_msp_bench.clock_hz != 72000000U) {
        g_stress_request = 0U;
        g_msp_bench.fault = 1U;
        g_msp_bench.state = MSP_FAILED;
        rtos_critical_exit(saved);
        return;
    }
    if (state == MSP_LOAD) {
        g_msp_bench.pressure_events = g_stress_generated;
    } else {
        arm_phase(&g_msp_bench.phases[state + 1U], stack);
        if (state == MSP_NORMAL) g_stress_request = 1U;
    }
    __DMB();
    g_msp_bench.state = state + 1U;
    rtos_critical_exit(saved);
}
