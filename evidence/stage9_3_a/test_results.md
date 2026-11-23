# InsightRTOS Stage 9.3-A: 测试套件执行记录与断言清单 (Test Results)

## 一、概述

本文件记录 InsightRTOS Stage 9.3-A（实时性诊断：时钟能力、CPU 运行周期与唤醒延迟）在主机、回归套件以及真实 STM32F103ZET6 硬件上的全部量化测试结果。

---

## 二、测试套件汇总 (Test Summary)

| 层次 | 测试套件 / 脚本 | 用例数 / 断言数 | 结果 | 耗时 | 备注 |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **RT 诊断主机测试** | `scripts/test/run_rt_host.sh` | 9 套件 / 45 断言 | **PASS (100%)** | 0.42 s | 独立子进程隔离执行，确定性 Mock 时钟 |
| **应用模块化测试** | `scripts/test/run_host.sh` | 8 套件 / 35 断言 | **PASS (100%)** | 0.28 s | 继承 Stage 9.2-H5 架构与闭环事件流验证 |
| **内核全量回归** | `scripts/test/run_regression.sh` | 360 / 360 用例 | **PASS (100%)** | 0.94 s | 涵盖 IPC、互斥锁 PI、队列、死锁诊断全集 |
| **QEMU 目标执行** | `scripts/test/run_rt_qemu.sh` | 0 | **NOT_TESTED** | N/A | 系统无 `qemu-system-arm`，如实声明，不伪造通过 |
| **硬件受控烧录回读** | `tests/hardware/flash_firmware.py` | 4 项物理检查 | **PASS (100%)** | 4.8 s | 页 0..30，SHA256 100% 匹配，尾部全 0xFF |
| **60 秒硬件稳定性** | `tests/hardware/sample_stability.py` | 13 次动态采样 | **PASS (100%)** | 62.1 s | 0 总线错误，0 队列故障，TIM2/SysTick 比值正常 |

---

## 三、RT 诊断主机测试清单 (`tests/host/test_diag_rt.c`)

1. **`test_rt_budget_and_structures`**:
   - `sizeof(rtos_diag_rt_snapshot_t) == 56U` (零跨平台填充).
   - Cortex-M3 32 位目标 `sizeof(rtos_task_t) == 72U` (ABI 冻结).
   - 断言数：2.
2. **`test_rt_clock_capabilities`**:
   - `mock_set_clock_available(false)` -> `RTOS_DIAG_CLOCK_UNAVAILABLE`，`flags & CLOCK_UNAVAILABLE`.
   - `mock_set_clock_available(true)` -> `RTOS_DIAG_CLOCK_CYCLE_ACCURATE`，`freq=72MHz`，`tick=100Hz`，`wrap_period=5965`.
   - 断言数：8.
3. **`test_rt_cpu_cycles_accumulation`**:
   - TaskA 运行 15,000 cycles -> 切到 TaskB 运行 25,000 cycles -> 切回 TaskA 运行 10,000 cycles.
   - 断言 TaskA charged_cycles == 25,000 (15,000 + 10,000 在途).
   - 断言 TaskB charged_cycles == 25,000.
   - 断言 Idle charged_cycles == 0.
   - 断言数：7.
4. **`test_rt_blocked_and_idle`**:
   - TaskA delay 5 ticks，TaskB 累加 30,000 cycles 后 delay 10 ticks.
   - 两任务均进入 `RTOS_TASK_BLOCKED`，Idle 任务运行 50,000 cycles.
   - 断言 TaskA、TaskB 处于 BLOCKED 态期间其 charged_cycles 严格不增加.
   - 断言 Idle snapshot 准确采集 50,000 cycles，调度次数累加 1.
   - 步进 5 ticks 唤醒 TaskA，断言 TaskA 重新切为 RUNNING 态.
   - 断言数：8.
5. **`test_rt_in_flight_query`**:
   - Task 在 RUNNING 态下保持执行，动态读取快照.
   - 步进 8,123 cycles，快照精确同步增加 8,123.
   - 再次步进 4,000 cycles，快照再增 4,000 cycles.
   - 断言数：4.
6. **`test_rt_ready_latency`**:
   - TaskLow 处于 READY，高优先级 TaskHigh 占用 CPU 80,000 cycles 与 2 个 ticks.
   - TaskHigh 阻塞，TaskLow 首派发.
   - 断言 `last_ready_latency_cycles == 80,000`.
   - 断言 `last_ready_latency_ticks == 2`.
   - 断言 `max_ready_latency_cycles == 80,000`.
   - 断言数：5.
7. **`test_rt_rollover_and_uncertainty`**:
   - 32 位 DWT 时钟置于回绕前夕 `0xFFFFFFC0`，跨越 0 至 `0x00000024`.
   - 断言 64 位单调拓展平滑衔接，增量为 100 cycles，无时间倒退，无不确定性.
   - 跨越 59.0 秒长间隔（`rtos_tick_test_advance(5905U)`，$\ge 5900$ ticks 阈值）.
   - 断言快照置位 `RTOS_DIAG_RT_CLOCK_UNCERTAIN`.
   - 断言时钟能力报告同步置位 `RTOS_DIAG_RT_CLOCK_UNCERTAIN`.
   - 断言数：5.
8. **`test_rt_generation_stale`**:
   - Task1 退出终止后，以原 `ref_old` 读取其冻结历史，断言成功.
   - 插槽被 Task2 复用（generation 递增），以旧 `ref_old` 查询返回 `RTOS_ERR_STALE`.
   - 以新 `ref_new` 查询返回全新初始统计.
   - 断言数：4.
9. **`test_rt_arguments_and_context`**:
   - 校验非法插槽号、空指针、代数 0 返回 `RTOS_ERR_ARGUMENT`.
   - 校验中断上下文（Handler Mode）调用所有诊断 API 均返回 `RTOS_ERR_CONTEXT`.
   - 断言数：6.

---

## 四、禁用状态零开销测试 (`INSIGHT_DIAG_RT=0`)

- 编译指令：带 `-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes -Wundef` 编译内核所有源文件。
- 结论：
  - 静态编译零错误、零告警；
  - 所有 Hook 展开为 `((void)0)`；
  - 未生成任何 `s_rt_tasks`、`s_rt_global`、`s_idle_record` 变量；
  - 内核调度关键路径汇编指令与 Stage 9.2 基线完全一致。
