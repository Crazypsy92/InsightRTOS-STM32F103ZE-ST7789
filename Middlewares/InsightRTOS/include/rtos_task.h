#ifndef INSIGHT_RTOS_TASK_H
#define INSIGHT_RTOS_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RTOS_MAX_TASKS 8U
#define RTOS_MIN_STACK_WORDS 128U
#define RTOS_STACK_ALIGNMENT 8U
#define RTOS_PRIORITY_LEVELS 32U
#define RTOS_MAX_PRIORITY (RTOS_PRIORITY_LEVELS - 1U)
#define RTOS_IDLE_TASK_ID UINT32_C(0)
#define RTOS_STACK_FILL UINT32_C(0xA5A5A5A5)
#define RTOS_STACK_GUARD UINT32_C(0xDEADC0DE)

typedef void (*rtos_task_entry_t)(void *arg);

typedef enum {
    RTOS_TASK_UNUSED = 0,
    RTOS_TASK_READY,
    RTOS_TASK_RUNNING,
    RTOS_TASK_TERMINATED,
    RTOS_TASK_BLOCKED
} rtos_task_state_t;

typedef enum {
    RTOS_OK = 0,
    RTOS_ERR_ARGUMENT,
    RTOS_ERR_STATE,
    RTOS_ERR_CAPACITY,
    RTOS_ERR_CONTEXT,
    RTOS_ERR_UNAVAILABLE,
    RTOS_ERR_FULL,
    RTOS_ERR_TIMEOUT,
    RTOS_ERR_NOT_OWNER,
    RTOS_ERR_STALE,
    RTOS_ERR_CANCELLED
} rtos_status_t;

typedef enum {
    RTOS_WAIT_NONE = 0,
    RTOS_WAIT_SEMAPHORE,
    RTOS_WAIT_MUTEX,
    RTOS_WAIT_QUEUE_SEND,
    RTOS_WAIT_QUEUE_RECEIVE
} rtos_wait_reason_t;

typedef enum {
    RTOS_WAIT_RESULT_NONE = 0,
    RTOS_WAIT_RESULT_PENDING,
    RTOS_WAIT_RESULT_GRANTED,
    RTOS_WAIT_RESULT_TIMEOUT,
    RTOS_WAIT_RESULT_CANCELLED
} rtos_wait_result_t;

/* Stable lifetime identity; generation zero is invalid. No pointer conversion. */
typedef struct { uint32_t slot; uint32_t generation; } rtos_task_ref_t;
typedef struct {
    rtos_task_ref_t identity;
    rtos_task_state_t state;
    uint8_t base_priority;
    uint8_t effective_priority;
} rtos_task_snapshot_t;
_Static_assert(sizeof(rtos_task_ref_t) == 8U, "Task reference ABI");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(rtos_task_snapshot_t) == 12U, "ARM snapshot ABI");
#endif

struct rtos_mutex;

typedef struct rtos_task {
    /* Assembly saves and restores this field at TCB offset zero. */
    uint32_t *sp;
    uint32_t id;
    const char *name;
    uint32_t *stack_base;
    size_t stack_words;
    /* C11 alias retains legacy diagnostic field and its byte offset.
     * All scheduling uses effective_priority; callers may only read fields. */
    union { uint8_t effective_priority; uint8_t priority; };
    rtos_task_state_t state;
    /* After state: occupies ARM ABI padding without moving the old state. */
    uint8_t base_priority;
    rtos_task_entry_t entry;
    void *arg;
    struct rtos_task *next;
    /* Remaining CPU slice; retained when a higher-priority task preempts. */
    uint32_t slice_remaining;
    /* Delay membership is independent of the ready queue's next field. */
    struct rtos_task *delay_next;
    uint32_t wake_tick;
    bool delay_queued;
    bool ready_queued;
    struct rtos_task *wait_next;
    void *wait_object;
    rtos_wait_reason_t wait_reason;
    rtos_wait_result_t wait_result;
    /* Intrusive owned-object list; includes ownership handed to READY tasks. */
    struct rtos_mutex *owned_mutexes;
    /* Active only for PENDING Queue wait; cleared before READY. Append-only:
     * sp and all existing field offsets stay unchanged. */
    union { const void *send; void *receive; } wait_buffer;
} rtos_task_t;

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(rtos_task_t) == 72U, "ARM Queue-wait TCB ABI");
_Static_assert(offsetof(rtos_task_t, wait_buffer) == 68U, "Append-only wait buffer");
#endif
_Static_assert(offsetof(rtos_task_t, sp) == 0U,
               "The saved stack pointer must be the first TCB member");

/*
 * Thread context only. The caller owns the static stack and name for the
 * task lifetime through its final context switch. Stack base is 8-byte aligned.
 * Safe terminated slots/stacks may be reused by a later create. Raw TCB pointers
 * then alias the new task and cannot identify the old lifetime (ABA).
 * A higher-priority new task may run before this function returns when the
 * scheduler is preemptive. Creation respects an enclosing PRIMASK critical section; BASEPRI/FAULTMASK
 * and handler contexts are rejected.
 */
rtos_status_t rtos_task_create(rtos_task_t **out, const char *name,
                               rtos_task_entry_t entry, void *arg,
                               uint32_t *stack, size_t words,
                               uint8_t priority);

/* Same creation transaction as legacy create, commits ref before READY.
 * Failure leaves *out unchanged. Thread only, enclosing PRIMASK permitted.
 * Output must be independent application storage; see task_public_handle.md. */
rtos_status_t rtos_task_create_with_ref(rtos_task_ref_t *out, const char *name,
    rtos_task_entry_t entry, void *arg, uint32_t *stack, size_t words,
    uint8_t priority);
/* Thread only; prestart/Idle/non-RUNNING => STATE. Failure preserves output. */
rtos_status_t rtos_task_self_ref(rtos_task_ref_t *out);
/* Thread only, including prestart/Idle; enclosing PRIMASK permitted. O(1).
 * ARGUMENT: malformed/unallocated; STALE: mismatched generation; STATE:
 * matching unpublished/invalid lifecycle. Failure preserves output.
 * Snapshot is coherent at one instant, not a live reference or state lock. */
rtos_status_t rtos_task_get_snapshot(rtos_task_ref_t ref, rtos_task_snapshot_t *out);

/* Ordinary RUNNING user Thread, started kernel, all IRQ masks clear.
 * Atomically cancel one PENDING IPC wait by generation identity. Pure delay,
 * unpublished/nonwaiting/already completed => STATE; stale => STALE.
 * OK commits CANCELLED+READY, not termination or immediate execution guarantee. */
rtos_status_t rtos_task_cancel_wait(rtos_task_ref_t target);

/* Cooperative, sticky request on this generation. Same caller context as cancel.
 * Active repeated request OK, terminated/unpublished STATE, old identity STALE.
 * Pending IPC is cancelled atomically; Delay and committed results are retained.
 * Never terminates, reclaims stacks or releases owned mutexes. */
rtos_status_t rtos_task_request_stop(rtos_task_ref_t target);
/* Current RUNNING user only; O(1), no scheduling/IPC/PI. Thread context as self_ref:
 * enclosing PRIMASK allowed, BASEPRI/FAULTMASK/Handlers rejected. Failure leaves
 * *out_requested unchanged; caller provides independent writable output storage. */
rtos_status_t rtos_task_stop_requested(bool *out_requested);

/* Read-only query; permitted in thread and handler context. */
rtos_task_t *rtos_task_current(void);

/* Thread context, running kernel, interrupts enabled; never returns.
 * All owned mutexes must first be unlocked; violation asserts before termination. */
_Noreturn void rtos_task_exit(void);

#endif
