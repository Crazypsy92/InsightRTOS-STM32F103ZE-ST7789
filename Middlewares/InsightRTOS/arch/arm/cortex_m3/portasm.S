.syntax unified
.cpu cortex-m3
.thumb

.extern rtos_current_task
.extern rtos_sched_switch
.extern rtos_port_tick_start
.extern _estack

.section .text.SVC_Handler,"ax",%progbits
.global SVC_Handler
.type SVC_Handler,%function
.thumb_func
SVC_Handler:
    cpsid i
    /* The selected TCB exists; SysTick remains masked until PSP is valid. */
    bl rtos_port_tick_start
    ldr r3, =rtos_current_task
    ldr r2, [r3]
    ldr r0, [r2]                 /* TCB.sp at offset zero */
    ldmia r0!, {r4-r11}
    msr psp, r0
    movs r0, #2                 /* privileged thread mode, PSP */
    msr control, r0
    isb
    ldr r0, =_estack             /* abandon the bootstrap MSP frame */
    msr msp, r0
    ldr lr, =0xfffffffd          /* thread mode, PSP, basic frame */
    cpsie i
    bx lr
.size SVC_Handler, .-SVC_Handler

.section .text.PendSV_Handler,"ax",%progbits
.global PendSV_Handler
.type PendSV_Handler,%function
.thumb_func
PendSV_Handler:
    cpsid i
    mrs r0, psp                 /* hardware has saved R0-R3,R12,LR,PC,xPSR */
    stmdb r0!, {r4-r11}
    ldr r3, =rtos_current_task
    ldr r2, [r3]
    str r0, [r2]
    push {r3, lr}               /* keep MSP 8-byte aligned across C call */
    bl rtos_sched_switch
    pop {r3, lr}
    ldr r2, [r3]
    ldr r0, [r2]
    ldmia r0!, {r4-r11}
    msr psp, r0
    isb
.global rtos_port_pendsv_unmask
rtos_port_pendsv_unmask:
    cpsie i
    bx lr
.size PendSV_Handler, .-PendSV_Handler

.section .note.GNU-stack,"",%progbits
