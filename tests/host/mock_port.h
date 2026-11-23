#ifndef INSIGHT_RTOS_HOST_MOCK_PORT_H
#define INSIGHT_RTOS_HOST_MOCK_PORT_H

#include <setjmp.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MOCK_EVENT_START = 1,
    MOCK_EVENT_EXIT,
    MOCK_EVENT_IDLE,
    MOCK_EVENT_ASSERT
} mock_event_t;

extern jmp_buf mock_boundary;
extern bool mock_boundary_armed;
extern bool mock_in_handler;
extern uint32_t mock_exception;
extern bool mock_unmaskable_handler;
extern bool mock_stop_after_yield;
extern bool mock_pendsv_pending;
extern uint32_t mock_primask;
extern uint32_t mock_basepri;
extern uint32_t mock_faultmask;
extern unsigned int mock_init_count;
extern unsigned int mock_start_count;
extern unsigned int mock_assert_count;
extern unsigned int mock_pend_count;
extern unsigned int mock_dispatch_count;
extern const char *mock_last_assertion;
extern void (*mock_stack_init_hook)(uint32_t *base, size_t words);
/* One-shot tests inject a pending SysTick at the IRQ-unmask/PendSV boundary. */
extern void (*mock_before_irq_unmask_hook)(void);

/* Deliver only after leaving handler mode and restoring PRIMASK to zero. */
void mock_dispatch_pending(void);
void mock_tick(void);
bool rtos_port_in_pendsv(void);

void mock_set_clock_available(bool avail);
void mock_set_cycles_32(uint32_t cycles);
void mock_advance_cycles_32(uint32_t delta);

#endif
