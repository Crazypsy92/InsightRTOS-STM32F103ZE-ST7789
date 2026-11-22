# Stage 9.1-H1 ARM QEMU 指令集与硬件级仿真测试报告 (QEMU Test Results)

## 一、概述 (Overview)

为确保 Explainable Scheduling 在真实的 ARMv7-M (Cortex-M3) 架构与 Thumb-2 指令集下具备 100% 的寄存器安全与中断行为正确性，在 QEMU 模拟器 (`qemu-system-arm` -machine mps2-an385 -cpu cortex-m3) 环境下执行了完整指令集仿真与 GDB 汇编断点分析。

- **仿真工具**: `qemu-system-arm` v10.2.1
- **调试工具**: `arm-none-eabi-gdb`
- **执行时间**: 2026-11-22
- **测试结果**: **202 / 202 全部通过 (100% PASS)**

---

## 二、Stage 9.1 专项 QEMU 测试分析 (Dedicated Stage 9.1 QEMU Tests)

### 1. `stage9_1_qemu_scenario` (场景端到端真实执行)

- **测试目标**：
  在 ARM MPS2-AN385 开发板模型上真实运行 Cortex-M3 固件。测试任务生命周期创建、高优先级抢占、`rtos_yield()` 主动让步、`rtos_delay()` 延时队列到期唤醒以及 PendSV C 移交提交点。
- **执行过程**：
  固件真实初始化 SysTick、SVC 与 PendSV 中断向量。内核触发多任务并发调度并记录 Trace。
- **输出断言**：
  检查场景输出日志，成功验证所有 7 类调度提交均被准确捕获并解析，单调序列号无缝递增，无任何 Fault 或内核死锁。
- **结果**：**Passed (0.14s)**。

### 2. `stage9_1_gdb_explain` (GDB 汇编寄存器与中断级别精确探查)

- **测试目标**：
  通过 GDB 脚本挂接 QEMU 仿真器，在 `rtos_sched_switch()` 和 `PendSV_Handler` 汇编入口打断点，物理核查 CPU 寄存器与硬件状态：
  1. **IPSR 寄存器**：验证在 PendSV 提交发生时，CPU 硬件的 `IPSR` 寄存器值恰好为 **14**（表示处于 PendSV ISR 异常上下文）。
  2. **PSP 栈指针**：核查当前任务的私有栈指针 `PSP` 在进入 C 移交点前后保持合法有效，未被破坏或跨越界限。
  3. **R4-R11 寄存器保护**：确认 C 层提交仅更新任务元数据与 `rtos_sched_switch` 返回值，不会擅自修改 ARM ABI 规定的 Callee-Saved 寄存器。
- **结果**：**Passed (0.20s)**。

---

## 三、全内核历史 QEMU 测试回归总结 (Full QEMU Regression)

测试工程包含 202 项测试实例：

| 阶段模块 | 测试项数 | 状态 | 覆盖特性说明 |
|---|---|---|---|
| **Stage 2** | 10 | **PASS** | 基础时间片、优先级就绪队列、Idle 空闲任务返回 |
| **Stage 3** | 13 | **PASS** | 同优先级 FIFO、Tick 回绕边界、Idle WFI 低功耗 |
| **Stage 4.1 - 4.5** | 47 | **PASS** | 同步信号量、资源互斥量、超时机制、死锁检测 |
| **Stage 5.1 - 5.5** | 51 | **PASS** | 优先级继承 (PI)、传递继承、复杂分支竞态与超时 |
| **Stage 6.1 - 6.5** | 79 | **PASS** | 环形消息队列 (IPC)、嵌套中断安全、生产级压力测试 |
| **Stage 9.1** | 2 | **PASS** | Explainable Scheduling 真实硬件场景与 GDB 探针 |
| **总计** | **202** | **100% PASS** | **耗时 26.56s，零失败，零回归缺陷** |
