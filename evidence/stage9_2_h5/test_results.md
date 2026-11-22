# Stage 9.2-H5: 软件测试与全量回归报告 (Test Results)

## 一、测试概要与判据

本阶段按照《InsightRTOS 应用层模块化与测试资产管理方案》（`docs/architecture/rtos_app_modularization.md`）要求，重构测试目录与执行规范，直接编译生产 `App/Src/app_events.c` 与 `App/Src/app_ipc.c` 源码，全面验证模块化后的功能等价性。

| 测试层级 | 测试套件 | 测试范围 / 目标 | 结果 | 判定 |
| :--- | :--- | :--- | :---: | :---: |
| **Host 单元与集成测试** | `scripts/test/run_host.sh` | 生产 App 模块、IPC 队列、事件状态机、环形历史、MultiButton、15行UI、LED极性 | 8/8 Suites (35 Assertions) | **PASS** |
| **历史 H4 兼容测试** | `tests/stage9_2_h4/run_tests.sh` | 历史基准行为比对（字模、事件ID、ACK闭环） | 9/9 Tests | **PASS** |
| **内核全量回归测试** | `ctest --test-dir build-host-stage9_2` | 调度器、优先级继承、互斥锁、死锁检测、Trace | 360/360 Tests | **PASS** |
| **QEMU 非硬件测试** | `scripts/test/run_qemu.sh` | Cortex-M3 无物理硬件验证 | 环境无 qemu-system-arm | **NOT_TESTED** |
| **多 Profile 构建验证** | CMake Presets (6 种) | 裸机/Smoke/Display profile 互不干扰与构建隔离 | 6/6 Presets | **PASS** |

---

## 二、Host 模块化单元与集成测试 (`tests/host/test_app_modular.c`)

通过统一执行脚本 `scripts/test/run_host.sh` 执行，产物输出至 `build/tests/test_app_modular`（符合测试可执行文件不进入 `tests/` 的要求）：

```text
====================================================
InsightRTOS Stage 9.2-H5 Modular Host Test Suite
====================================================
[TEST 1] Configuration and ABI verification... PASS
[TEST 2] Production App_IPC queues and depths... PASS
[TEST 3] Production App_Events allocation and stage transitions... PASS
[TEST 4] Bounded 8-slot ring history and overflow drop... PASS
[TEST 5] Closed-loop multi-task pipeline simulation... PASS
[TEST 6] MultiButton event state machine... PASS
[TEST 7] Chinese 16x16 glyph pack and UI screen bounds... PASS
[TEST 8] LED active-low polarity and deadlock diagnostic states... PASS
====================================================
ALL STAGE 9.2-H5 HOST TESTS PASSED (8/8 Suites, 35 Assertions)
====================================================
```

### 重点测试覆盖点：
1. **真实生产代码链接**：直接编译 `App/Src/app_events.c` 和 `App/Src/app_ipc.c`，彻底解决 H4 仅测试测试复制品的问题。
2. **IPC 队列深度与容量**：验证 5 条静态队列初始化、满队（32/4）返回 `RTOS_ERR_FULL`、只读深度快照 `App_IPC_GetQueueDepths()` 精确反映入队出队。
3. **事件状态跃迁**：验证 `IDENTIFIED -> KEY -> CTRL -> LCD -> ACKED` 单向跃迁，非预期状态严禁推进。
4. **反压与丢弃统计**：验证队列满时 `App_Events_RecordRetry()` 增加重试计数且不影响丢弃计数；ISR 满时真实丢弃增加 `drop_count`。
5. **8 槽位环形历史覆盖**：分配 12 个事件，验证最新 8 个事件可通过 `App_Events_FindTracked()` 正确检索，已淘汰的 4 个事件返回 `NULL`。

---

## 三、内核全量回归测试 (ctest 360/360)

通过 `scripts/test/run_regression.sh` 执行：
- 调度器、就绪队列、时间片轮转：PASS
- 优先级反转与互斥锁优先级继承（Transitive PI）：PASS
- 阻塞等待超时、取消等待、任务退出：PASS
- 死锁环路检测算法与有界快照：PASS
- 可解释调度（Explainable Scheduling）Trace：PASS
- **总计**：360/360 tests passed, 0 tests failed (耗时 0.96 秒)。

---

## 四、多 Profile 编译矩阵验证

| Preset 编译预设 | 目标可执行文件 | Flash 大小 | RAM 大小 | 状态 |
| :--- | :--- | :---: | :---: | :---: |
| `DISPLAY_RTOS_O2_Trace` | `insightrtos_display.elf` | 48,876 B | 64 KiB (100.0%) | **PASS** |
| `DISPLAY_RTOS_Debug_Trace`| `insightrtos_display.elf` | 60,864 B | 64 KiB (100.0%) | **PASS** |
| `DISPLAY_RTOS_O2` | `insightrtos_display.elf` | 44,792 B | 64 KiB (100.0%) | **PASS** |
| `DISPLAY_RTOS_Debug` | `insightrtos_display.elf` | 56,808 B | 64 KiB (100.0%) | **PASS** |
| `RTOS_Debug` (Smoke) | `insightrtos_rtos.elf` | 28,564 B | 64 KiB (100.0%) | **PASS** |
| `Debug` (Bare-metal) | `insightrtos.elf` | 41,964 B | 2,464 B (3.76%) | **PASS** |

所有 profile 独立构建无任何源码交叉污染，未跟踪文件保持纯净。
