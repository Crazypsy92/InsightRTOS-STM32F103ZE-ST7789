#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "rtos.h"
#include <stdint.h>
#include <stdalign.h>

/* Task Priorities (5/4/2/1) */
#define APP_KEY_PRIORITY       5U
#define APP_CTRL_PRIORITY      4U
#define APP_LCD_PRIORITY       2U
#define APP_MON_PRIORITY       1U

/* Stack Allocation (Words) */
#define APP_TASK_STACK_WORDS   1024U

/* Queue Capacities */
#define APP_KEY_QUEUE_CAPACITY     32U
#define APP_COMMAND_QUEUE_CAPACITY 32U
#define APP_ACK_QUEUE_CAPACITY     4U
#define APP_METRIC_QUEUE_CAPACITY  4U

/* Event History Capacity */
#define APP_EVENT_HISTORY_CAPACITY 8U

/* Static Compile-Time Assertions */
_Static_assert(RTOS_MAX_TASKS == 8U && RTOS_TICK_HZ == 100U, "Audited profile");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(rtos_task_t) == 72U, "TCB 72B ABI");
#endif
_Static_assert(APP_KEY_PRIORITY <= RTOS_MAX_PRIORITY, "Valid KEY priority");
_Static_assert(APP_CTRL_PRIORITY <= RTOS_MAX_PRIORITY, "Valid CTRL priority");
_Static_assert(APP_LCD_PRIORITY <= RTOS_MAX_PRIORITY, "Valid LCD priority");
_Static_assert(APP_MON_PRIORITY <= RTOS_MAX_PRIORITY, "Valid MON priority");

#endif /* APP_CONFIG_H */
