#ifndef INSIGHT_RTOS_SEMAPHORE_INTERNAL_H
#define INSIGHT_RTOS_SEMAPHORE_INTERNAL_H

#include "rtos_semaphore.h"

/* Internal atomic give transition. IRQ exclusion required, initialized object.
 * Shared by validated thread/ISR APIs and controlled race tests. */
rtos_status_t rtos_sem_give_locked(rtos_semaphore_t *sem);

#endif
