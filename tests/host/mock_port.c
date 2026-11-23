#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernel_internal.h"
#include "mock_port.h"
#include "port.h"
#include "rtos_assert.h"

/* These stubs test C decisions. They never execute ARM task contexts. */
jmp_buf mock_boundary;
bool mock_boundary_armed;
bool mock_in_handler;
uint32_t mock_exception;
bool mock_unmaskable_handler;
bool mock_stop_after_yield;
bool mock_pendsv_pending;
uint32_t mock_primask;
uint32_t mock_basepri;
uint32_t mock_faultmask;
unsigned int mock_init_count;
unsigned int mock_start_count;
unsigned int mock_assert_count;
unsigned int mock_pend_count;
unsigned int mock_dispatch_count;
const char *mock_last_assertion;
void (*mock_stack_init_hook)(uint32_t *base, size_t words);
void (*mock_before_irq_unmask_hook)(void);
static bool mock_delivering;

static _Noreturn void mock_escape(mock_event_t event)
{
    if (!mock_boundary_armed) {
        (void)fprintf(stderr, "Unexpected host mock boundary %d\n", (int)event);
        exit(EXIT_FAILURE);
    }
    /* An expected assertion aborts the simulated exception, not a real CPU. */
    mock_delivering = false;
    longjmp(mock_boundary, (int)event);
}

uint32_t *rtos_port_stack_init(uint32_t *base, size_t words,
                               rtos_task_entry_t entry, void *arg)
{
    (void)entry;
    (void)arg;
    if (mock_stack_init_hook != NULL) {
        mock_stack_init_hook(base, words);
    }
    for (size_t index = 0U; index < words; ++index) {
        base[index] = RTOS_STACK_FILL;
    }
    base[0] = RTOS_STACK_GUARD;
    const uintptr_t top = (uintptr_t)(base + words) &
                          ~((uintptr_t)RTOS_STACK_ALIGNMENT - 1U);
    return (uint32_t *)top - 16;
}

bool rtos_port_context_valid(const rtos_task_t *task)
{
    /* This mock models a 16-word opaque context, not ARM exception behavior. */
    const uintptr_t end = (uintptr_t)(task->stack_base + task->stack_words);
    return end - (uintptr_t)task->sp >= 16U * sizeof(uint32_t);
}

void rtos_port_init(void)
{
    ++mock_init_count;
}

_Noreturn void rtos_port_start(void)
{
    ++mock_start_count;
    mock_escape(MOCK_EVENT_START);
}

void mock_dispatch_pending(void)
{
    if (mock_delivering || mock_in_handler || mock_exception != 0U ||
        ((mock_primask | mock_basepri | mock_faultmask) != 0U)) {
        return;
    }
    while (mock_pendsv_pending) {
        mock_pendsv_pending = false;
        mock_delivering = true;
        const uint32_t saved = rtos_port_irq_save();
        ++mock_dispatch_count;
        rtos_sched_switch();
        rtos_port_irq_restore(saved);
        mock_delivering = false;
        if (mock_stop_after_yield) {
            mock_escape(MOCK_EVENT_EXIT);
        }
    }
}

void rtos_port_yield(void)
{
    ++mock_pend_count;
    mock_pendsv_pending = true;
    mock_dispatch_pending();
}

bool rtos_port_in_pendsv(void)
{
    return mock_delivering;
}

bool rtos_port_in_handler(void)
{
    return mock_in_handler || mock_exception != 0U;
}

bool rtos_port_in_kernel_isr(void)
{
    return mock_exception == 15U || mock_exception >= 16U;
}

bool rtos_port_in_unmaskable_handler(void)
{
    return mock_unmaskable_handler || mock_exception == 2U || mock_exception == 3U;
}

bool rtos_port_interrupts_masked(void)
{
    return (mock_primask | mock_basepri | mock_faultmask) != 0U;
}

uint32_t rtos_port_irq_save(void)
{
    const uint32_t saved = mock_primask;
    mock_primask = 1U;
    return saved;
}

void rtos_port_irq_restore(uint32_t saved)
{
    mock_primask = saved;
    if ((saved == 0U) && (mock_before_irq_unmask_hook != NULL)) {
        mock_before_irq_unmask_hook();
    }
    mock_dispatch_pending();
}

void mock_tick(void)
{
    const bool previous_handler = mock_in_handler;
    mock_in_handler = true;
    rtos_tick_handler();
    mock_in_handler = previous_handler;
    mock_dispatch_pending();
}

_Noreturn void rtos_port_idle(void)
{
    mock_escape(MOCK_EVENT_IDLE);
}

_Noreturn void rtos_assert_fail(const char *expression, const char *file,
                               unsigned int line)
{
    (void)file;
    (void)line;
    ++mock_assert_count;
    mock_last_assertion = expression;
    mock_escape(MOCK_EVENT_ASSERT);
}

bool rtos_port_task_create_context(void)
{
    return !mock_in_handler && mock_exception == 0U && !mock_unmaskable_handler &&
        mock_basepri == 0U && mock_faultmask == 0U;
}

static bool s_mock_clock_available = true;
static uint32_t s_mock_cycles_32 = 0U;

bool rtos_port_clock_available(void)
{
    return s_mock_clock_available;
}

uint32_t rtos_port_clock_cycles_32(void)
{
    return s_mock_cycles_32;
}

uint32_t rtos_port_cpu_hz(void)
{
    return 72000000U;
}

void mock_set_clock_available(bool avail)
{
    s_mock_clock_available = avail;
}

void mock_set_cycles_32(uint32_t cycles)
{
    s_mock_cycles_32 = cycles;
}

void mock_advance_cycles_32(uint32_t delta)
{
    s_mock_cycles_32 += delta;
}
