# InsightRTOS Stage 9.1: QEMU ARM Cortex-M3 Verification

## 一、真实 ARM 执行环境验证

在真实 ARM Cortex-M3 (QEMU MPS2 AN385) 目标架构下，对 Explainable Scheduling 进行了双轨验证：
1. **真实硬件探针与全流程场景** (`stage9_1_qemu_scenario`)
2. **GDB 指令级断点与寄存器状态校验** (`stage9_1_gdb_explain`)

构建目录：`build-arm-stage9_1`
工具链：`arm-none-eabi-gcc 14.2.1` / `gdb-multiarch` / `qemu-system-arm`

---

## 二、测试执行记录

### 1. `stage9_1_qemu_scenario` 执行输出
```
InsightRTOS Stage 9.1 Explainable Scheduler Booting...
Controller started in Thread mode with PSP
Creating High priority task...
Creating FIFO tasks...
Creating Exiting task...
--- EXPLAIN TRACE DUMP ---
Task created [slot 0, gen 1]
Task became READY [slot 0, gen 1]
Scheduler select: next [slot 0, gen 1] from [slot 65534, gen 0] reasons=0x1
Task created [slot 1, gen 1]
Task became READY [slot 1, gen 1]
Task became READY [slot 0, gen 1]
Scheduler select: next [slot 1, gen 1] from [slot 0, gen 1] reasons=0x8
Context switch commit to [slot 1, gen 1, prio 6] from [slot 0, gen 1, prio 4] class=HIGHER_PREEMPT
Task BLOCKED [slot 1, gen 1] reason=0 wake_tick=2
Scheduler select: next [slot 0, gen 1] from [slot 1, gen 1] reasons=0x40
Context switch commit to [slot 0, gen 1, prio 4] from [slot 1, gen 1, prio 6] class=BLOCK_OR_WAKE_OTHER
Task BLOCKED [slot 0, gen 1] reason=0 wake_tick=3
Scheduler select: next [slot 65535, gen 0] from [slot 0, gen 1] reasons=0x60
Context switch commit to [slot 65535, gen 0, prio 0] from [slot 0, gen 1, prio 4] class=IDLE_TRANSITION
Delay expired [slot 1, gen 1] wake_tick=2
Task became READY [slot 1, gen 1]
Task became READY [slot 65535, gen 0]
Scheduler select: next [slot 1, gen 1] from [slot 65535, gen 0] reasons=0xa8
Context switch commit to [slot 1, gen 1, prio 6] from [slot 65535, gen 0, prio 0] class=IDLE_TRANSITION
Task terminated [slot 1, gen 1]
Scheduler select: next [slot 65535, gen 0] from [slot 1, gen 1] reasons=0x30
Context switch commit to [slot 65535, gen 0, prio 0] from [slot 1, gen 1, prio 6] class=IDLE_TRANSITION
Delay expired [slot 0, gen 1] wake_tick=3
Task became READY [slot 0, gen 1]
Task became READY [slot 65535, gen 0]
Scheduler select: next [slot 0, gen 1] from [slot 65535, gen 0] reasons=0xa8
Context switch commit to [slot 0, gen 1, prio 4] from [slot 65535, gen 0, prio 0] class=IDLE_TRANSITION
Task created [slot 2, gen 1]
Task became READY [slot 2, gen 1]
Task created [slot 3, gen 1]
Task became READY [slot 3, gen 1]
Task BLOCKED [slot 0, gen 1] reason=0 wake_tick=6
Scheduler select: next [slot 2, gen 1] from [slot 0, gen 1] reasons=0x40
Context switch commit to [slot 2, gen 1, prio 2] from [slot 0, gen 1, prio 4] class=BLOCK_OR_WAKE_OTHER
Explicit yield [slot 2, gen 1] reason=0x2
Task became READY [slot 2, gen 1]
Scheduler select: next [slot 3, gen 1] from [slot 2, gen 1] reasons=0x2
Context switch commit to [slot 3, gen 1, prio 2] from [slot 2, gen 1, prio 2] class=YIELD
Explicit yield [slot 3, gen 1] reason=0x2
Task became READY [slot 3, gen 1]
Scheduler select: next [slot 2, gen 1] from [slot 3, gen 1] reasons=0x2
Context switch commit to [slot 2, gen 1, prio 2] from [slot 3, gen 1, prio 2] class=YIELD
Task terminated [slot 2, gen 1]
Scheduler select: next [slot 3, gen 1] from [slot 2, gen 1] reasons=0x10
Context switch commit to [slot 3, gen 1, prio 2] from [slot 2, gen 1, prio 2] class=EXIT
Task terminated [slot 3, gen 1]
Scheduler select: next [slot 65535, gen 0] from [slot 3, gen 1] reasons=0x30
Context switch commit to [slot 65535, gen 0, prio 0] from [slot 3, gen 1, prio 2] class=IDLE_TRANSITION
Delay expired [slot 0, gen 1] wake_tick=6
Task became READY [slot 0, gen 1]
Task became READY [slot 65535, gen 0]
Scheduler select: next [slot 0, gen 1] from [slot 65535, gen 0] reasons=0xa8
Context switch commit to [slot 0, gen 1, prio 4] from [slot 65535, gen 0, prio 0] class=IDLE_TRANSITION
Task created [slot 4, gen 1]
Task became READY [slot 4, gen 1]
Task BLOCKED [slot 0, gen 1] reason=0 wake_tick=8
Scheduler select: next [slot 4, gen 1] from [slot 0, gen 1] reasons=0x40
Context switch commit to [slot 4, gen 1, prio 2] from [slot 0, gen 1, prio 4] class=BLOCK_OR_WAKE_OTHER
Task terminated [slot 4, gen 1]
Scheduler select: next [slot 65535, gen 0] from [slot 4, gen 1] reasons=0x30
Context switch commit to [slot 65535, gen 0, prio 0] from [slot 4, gen 1, prio 2] class=IDLE_TRANSITION
Delay expired [slot 0, gen 1] wake_tick=8
Task became READY [slot 0, gen 1]
Task became READY [slot 65535, gen 0]
Scheduler select: next [slot 0, gen 1] from [slot 65535, gen 0] reasons=0xa8
Context switch commit to [slot 0, gen 1, prio 4] from [slot 65535, gen 0, prio 0] class=IDLE_TRANSITION
--- END TRACE DUMP ---
STAGE 9.1 QEMU ARM SCENARIO PASS
```

### 2. GDB 断点与寄存器断言 (`stage9_1_gdb_explain`)
- **断点 1 (`stage9_1_svc_probe`)**:
  - `PSP = 0x20000fe0`
  - `MSP = 0x20400000`
  - `CONTROL = 2` (Thread 模式使用 PSP)
  - `IPSR = 0` (非异常模式)
  - 证明：SVC 启动成功，首个任务已恢复真实 PSP 上下文并开始执行。
- **断点 2 (`stage9_1_high_exec_probe`)**:
  - `PSP = 0x20001ff0` (High 任务独立栈)
  - `CONTROL = 2`
  - 证明：发生高优先级就绪抢占时，PendSV 成功将 CPU 控制权切换至高优先级任务，并实际恢复了其硬件寄存器。
- **断点 3 (`stage9_1_checkpoint`)**:
  - 证明：全部调度场景及因果解释格式化在 ARM 裸机环境中完整执行完毕。

---

## 三、三级执行证据链区分

本测试在真实指令层严格证明了以下三个阶段的边界独立性：
1. **`RTOS_TRACE_SCHED_SELECT`**：内核调度器选定下一个候选任务（纯 C 决策阶段）。
2. **`RTOS_TRACE_CONTEXT_SWITCH` (C Commit)**：内核更新当前任务指针，提交切换意图，标记提交类别（C 移交阶段）。
3. **真实 PSP 上下文恢复与任务入口执行**：PendSV 汇编压栈/出栈完成，任务从自身 entry 执行第一条指令（真实执行阶段，由汇编探针与 GDB 断点确证）。
