#ifndef INSIGHT_RTOS_PORT_H
#define INSIGHT_RTOS_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rtos_task.h"

uint32_t *rtos_port_stack_init(uint32_t *base, size_t words,
                               rtos_task_entry_t entry, void *arg);
/* Called only after the kernel has checked guard, SP alignment and bounds. */
bool rtos_port_context_valid(const rtos_task_t *task);
void rtos_port_init(void);
/* Clock frequency is supplied by the BSP; no board addresses enter the kernel. */
uint32_t rtos_port_cpu_hz(void);
bool rtos_port_systick_reload(uint32_t cpu_hz, uint32_t tick_hz, uint32_t *reload);
void rtos_port_tick_init(void);
void rtos_port_tick_start(void);
void SysTick_Handler(void);
_Noreturn void rtos_port_start(void);
void rtos_port_yield(void);
bool rtos_port_in_pendsv(void);
bool rtos_port_in_handler(void);
/* Thread only; existing enclosing PRIMASK is supported, BASEPRI/FAULTMASK are not. */
bool rtos_port_task_create_context(void);
/* Only SysTick and external IRQs may use kernel ISR APIs. */
bool rtos_port_in_kernel_isr(void);
bool rtos_port_in_unmaskable_handler(void);
bool rtos_port_interrupts_masked(void);
uint32_t rtos_port_irq_save(void);
void rtos_port_irq_restore(uint32_t saved);
_Noreturn void rtos_port_idle(void);

#endif
