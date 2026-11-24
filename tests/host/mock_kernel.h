#ifndef MOCK_KERNEL_H
#define MOCK_KERNEL_H

#include "rtos.h"
#include <stdint.h>
#include <stdbool.h>

void mock_kernel_set_isr_result(rtos_status_t result);
void mock_kernel_reset(void);
void mock_kernel_set_tick(uint32_t tick);
void mock_kernel_advance_ms(uint32_t ms);

#endif /* MOCK_KERNEL_H */
