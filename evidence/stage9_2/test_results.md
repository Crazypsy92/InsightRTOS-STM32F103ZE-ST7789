# Stage 9.2: 测试结果与回归报告 (Test Results)

## 一、Host 单元测试结果 (Host Unit Tests)

构建目标：`build-host-stage9_2`
执行命令：`ctest --test-dir build-host-stage9_2 -V -R stage9_2`
通过率：**11 / 11 (100% PASS)**

| 测试编号 | 测试用例名称 | 覆盖场景与断言重点 | 耗时 | 判定 |
|---|---|---|---|---|
| #350 | `stage9_2_host_budget` | 工作区结构体 384 B ABI、节点与头结构体尺寸 | 0.01 s | **PASS** |
| #351 | `stage9_2_host_arguments` | NULL 传参、未捕获调用、ISR 中断上下文非法调用拦截 | 0.00 s | **PASS** |
| #352 | `stage9_2_host_no_contention` | 无竞争场景，检测 0 节点 0 边，`deadlock_detected = false` | 0.00 s | **PASS** |
| #353 | `stage9_2_host_single_contention` | 单 Mutex 正常抢占竞争，出边指向 Owner，无死锁 | 0.00 s | **PASS** |
| #354 | `stage9_2_host_serial_chain` | 多任务串行等待依赖链（A->B->C），非环路验证 | 0.00 s | **PASS** |
| #355 | `stage9_2_host_multiple_waiters` | 多个等待者同时阻塞在同一互斥量（扇入图），无环路 | 0.00 s | **PASS** |
| #356 | `stage9_2_host_abba_deadlock` | 经典两任务 ABBA 锁交叉等待，检测 2-环路与超时标志 | 0.00 s | **PASS** |
| #357 | `stage9_2_host_circular_deadlock` | 三任务循环死锁 (A->C->B->A)，规范化旋转起始任务 | 0.00 s | **PASS** |
| #358 | `stage9_2_host_transitive_pi` | 传递性优先级继承与死锁并存，验证 PI 提升标记 | 0.00 s | **PASS** |
| #359 | `stage9_2_host_task_exit_stale` | 任务退出（TERMINATED）后校验，返回 `RTOS_ERR_STALE` | 0.00 s | **PASS** |
| #360 | `stage9_2_host_anomaly` | 异常与不完整图防护，绝不虚报 NO DEADLOCK | 0.00 s | **PASS** |

### 全内核回归结果
执行命令：`ctest --test-dir build-host-stage9_2 --output-on-failure`
测试统计：**360 / 360 测试用例全部 PASS (100%)**
证明 Stage 9.2 未对原有调度器、任务生命周期、队列、信号量、延时队列及 Stage 9.1 可解释调度功能造成任何破坏。

---

## 二、QEMU ARM Cortex-M3 实机场景测试 (QEMU Scenario Tests)

测试平台：`mps2-an385` (ARM Cortex-M3, Semihosting + UART)
测试执行器：`tests/stage9_2/run.sh`

### 1. Debug 构建场景验证 (`build-arm-stage9_2`)
- 编译输出文件：`stage9_2_scenario.elf` (Flash 31,812 B, RAM 17,328 B)
- 运行日志节选：
```text
InsightRTOS Stage 9.2 Deadlock Diagnostics Booting...
Supervisor started in Thread mode
Phase 0: No contention, deadlock=false PASS
Starting Phase 1: Normal Mutex Contention...
Task A: attempting to lock M1 (timeout 10)...
Phase 1: Contention correctly identified without false deadlock PASS
Task A: acquired M1
Starting Phase 2: ABBA Deadlock Test...
Task A: locking M1...
Task B: locking M2...
Task B: attempting to lock M1 (timeout 50)...
Task A: attempting to lock M2 (timeout 50)...
Phase 2: DEADLOCK DETECTED! Cycle length 2 PASS
Cycle tasks: [T0, T1]
Waiting for timeout to resolve cycle naturally...
Task B: lock M1 returned (timeout or ok)
Task A: lock M2 returned (timeout or ok)
Post-timeout: Deadlock cleanly resolved PASS
STAGE 9.2 QEMU ARM SCENARIO PASS
```
- 结果：**1 / 1 PASS (0.71 s)**

### 2. Release (O2) 构建场景验证 (`build-arm-stage9_2-o2`)
- 编译输出文件：`stage9_2_scenario.elf` (Flash 27,792 B, RAM 17,328 B)
- 运行日志一致，全部断言通过，无任何崩溃或超时。
- 结果：**1 / 1 PASS (0.70 s)**
