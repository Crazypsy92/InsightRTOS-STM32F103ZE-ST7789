#ifndef INSIGHT_RTOS_QUEUE_H
#define INSIGHT_RTOS_QUEUE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rtos_config.h"
#include "rtos_task.h"
#include "rtos_wait.h"

/* Static zero-initialized object; initialize once, never copy/modify fields.
 * Object and entire supplied storage must remain valid, disjoint and reserved
 * for the queue. No destroy/reinitialize. No alignment requirement on storage. */
typedef struct rtos_queue {
    uint8_t *buffer;
    size_t buffer_bytes;
    size_t item_size;
    uint32_t capacity;
    uint32_t count;
    uint32_t head;
    uint32_t tail;
    bool initialized;
    rtos_task_t *send_wait_head;
    rtos_task_t *receive_wait_head;
} rtos_queue_t;
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(rtos_queue_t) == 40U, "ARM blocking Queue ABI");
/* A pending blocking wait may return ERR_CANCELLED after a successful
 * rtos_task_cancel_wait(ref) or request_stop(ref). A sticky STOP also rejects
 * a new blocking registration; immediate success and NO_WAIT are unchanged.
 * No message/permit is acquired or committed on CANCELLED. */
#endif

/* Thread, all IRQ masks clear. Also allowed before scheduler start.
 * capacity>0, 1<=item_size<=RTOS_QUEUE_MAX_ITEM_SIZE, sufficient bytes.
 * Rejects multiplication/address overflow and object/storage overlap. */
rtos_status_t rtos_queue_init(rtos_queue_t *queue, void *buffer,
    size_t buffer_bytes, size_t item_size, uint32_t capacity);
/* Ordinary RUNNING task, started kernel, all IRQ masks clear. NO_WAIT, 1..INT32_MAX, or WAIT_FOREVER.
 * Other values return ERR_ARGUMENT, even when immediately available.
 * Finite waits return ERR_TIMEOUT if expiry commits first, without copying.
 * FULL/UNAVAILABLE leave object, storage and receive output unchanged.
 * Exactly item_size bytes are copied. Item must be valid and disjoint from
 * both object and entire supplied storage; no pointer lifetime management.
 * On blocking SEND the source remains valid AND stable until completion;
 * on blocking RECEIVE the writable target remains valid until completion.
 * Also reject overlap with ANY pending buffer on this queue (even send/send).
 * A message containing pointers copies pointer values, not pointed-to data. */
rtos_status_t rtos_queue_send(rtos_queue_t *queue, const void *item,
    uint32_t timeout_ticks);
rtos_status_t rtos_queue_receive(rtos_queue_t *queue, void *item,
    uint32_t timeout_ticks);
/* SysTick (IPSR15) or external IRQ (IPSR>=16), started kernel/current valid.
 * Thread, faults, NMI, SVC and PendSV return ERR_CONTEXT; not started ERR_STATE.
 * One nonblocking attempt; full returns ERR_FULL with no state change.
 * Same copy/alias contract as send; source may be reused on return.
 * Restores entry PRIMASK, preserves BASEPRI/FAULTMASK; never loads a PSP.
 * Receiver handoff/finite deadline cancellation use the shared wait protocol. */
rtos_status_t rtos_queue_send_from_isr(rtos_queue_t *queue, const void *item);
/* A pending blocking wait may return ERR_CANCELLED after a successful
 * rtos_task_cancel_wait(ref) or request_stop(ref). A sticky STOP also rejects
 * a new blocking registration; immediate success and NO_WAIT are unchanged.
 * No message/permit is acquired or committed on CANCELLED. */
#endif
