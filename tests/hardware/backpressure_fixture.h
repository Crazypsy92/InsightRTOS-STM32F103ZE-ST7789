#ifndef INSIGHT_BACKPRESSURE_FIXTURE_H
#define INSIGHT_BACKPRESSURE_FIXTURE_H

#include "app_tasks.h"

#define BP_EVENTS 48U

void BP_GateConsumer(void);
void BP_CommandAttempt(const display_command_t *command, rtos_status_t status, uint32_t committed);
void BP_Executed(const display_command_t *command);
void BP_Ack(const display_ack_t *ack);

#endif
