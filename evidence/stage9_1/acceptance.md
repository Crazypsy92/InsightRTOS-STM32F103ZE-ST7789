# InsightRTOS Stage 9.1: Explainable Scheduling Software Acceptance

## 一、验收结果总结 (Final Verdict Summary)

| 检查项 (Item) | 状态 (Status) | 说明 (Notes) |
|---|---|---|
| **EXPLAIN MODULE** | **PASS** | 完整实现 Stage 9.0 设计规范，支持显式 Yield、Delay 到期、PendSV C Commit 诊断事件及可解释输出 |
| **HOST TEST** | **PASS** | 11 项 Stage 9.1 专属 Host 场景测试全部通过 (8 项解释逻辑 + 3 项游标回绕)；历史 338 项 Host 测试 100% 通过 |
| **QEMU ARM TEST** | **PASS** | ARM Cortex-M3 (MPS2 AN385) 真实指令集测试，通过 SVC/PendSV 汇编探针与 GDB 断点验证真实 PSP 恢复与抢占执行 |
| **ORIGINAL REGRESSION** | **PASS** | 完整回归 2,155 次历史测试执行，涵盖 Host/ARM MPS2 (Debug/O2)、STM32 自检负载与独立预设，全部 100% 通过 |
| **TRACE ABI** | **PASS** | 严格保持 128 槽容量与 24 字节记录对齐，ABI 冻结，向后兼容 Stage 8.2 既有读取接口 |
| **PRODUCTION OFF COMPATIBILITY** | **PASS** | TRACE OFF / EXPLAIN OFF 保持零开销宏消解，无代码污染；TRACE ON / EXPLAIN OFF 保持原 16 字节元数据 |
| **RESOURCE BUDGET** | **PASS** | STM32F103ZET6 静态 SRAM 增量恰为 +8 B (预算 3,096 B，实测 3,096 B)；Flash 增量 O2 为 +300 B / Debug 为 +604 B (预算 ≤ 2,048 B) |
| **HARDWARE** | **NOT EXECUTED** | 遵守工程保护守则：本阶段仅完成软件实现、Host/QEMU 验证及资源测量，未对真实 MCU 进行烧录、擦除或复位 |

---

## 二、核心任务完成情况

### 任务 A：扩展现有 Trace
- **24 字节 ABI 保持**：`rtos_trace_record_t` 大小保持 24 字节不变，事件版本升级为 `RTOS_TRACE_EVENT_SET_VERSION_2`。
- **64 位单调序列与游标**：新增高 32 位扩展序列号，支持检测环形缓冲区回绕、覆盖与游标过期 (`RTOS_ERR_STALE`)。
- **配置矩阵验证**：
  1. `TRACE OFF / EXPLAIN OFF`：零宏消解，静态 RAM 增量 0 B，代码完全消除。
  2. `TRACE ON / EXPLAIN OFF`：保留 16 B 原元数据，与 Stage 8.2 完全二进制一致。
  3. `TRACE ON / EXPLAIN ON`：新增 8 B 全局元数据 (`trace_next_sequence_high`, `trace_status`)，支持可解释诊断。
  - CMakeLists.txt 强制依赖校验：`INSIGHT_DIAG_EXPLAIN` 开启时若 `INSIGHT_TRACE_ENABLED` 未开启则直接报错。

### 任务 B：新增三个事件源点
1. **显式 Yield (`RTOS_TRACE_TASK_YIELD`)**：
   - 插入位置：`rtos_yield()` 事务边界。
   - 仅在主动调用 yield 且被调度器接受时记录，绝不将普通时间片轮转混淆为 yield。
2. **普通 Delay 到期 (`RTOS_TRACE_DELAY_EXPIRED`)**：
   - 插入位置：`kernel/delay.c` 中 `rtos_delay_process_expired()`。
   - 区分纯 delay 到期与 IPC 等待超时，避免重复无意义插入。
3. **PendSV C Commit (`RTOS_TRACE_CONTEXT_SWITCH`)**：
   - 插入位置：`kernel/scheduler.c` 中 `rtos_sched_switch()`。
   - 通过 `rtos_port_in_pendsv()` (ARM 下核查 `IPSR == 14`) 准确判定当前上下文切换执行环境。
   - 细分 7 种提交类别：`HIGHER_PREEMPT` (1), `YIELD` (2), `SLICE_ROTATION` (3), `BLOCK_OR_WAKE_OTHER` (4), `EXIT` (5), `IDLE_TRANSITION` (6), `MIXED` (7)。

### 任务 C：可解释事件读取接口
- 头文件 `include/rtos_diag.h` 提供公开诊断 API：
  - `rtos_diag_trace_info()`: 获取缓冲区边界、丢失数与全局状态。
  - `rtos_diag_trace_read()`: 有界读取最多 N 条记录，检测过期游标。
  - `rtos_diag_trace_read_latest()`: 快速拉取最新批次。
  - `rtos_diag_explain_record()`: 结合任务 generation 输出结构化因果解释。
  - `rtos_diag_format_explanation()`: 独立安全格式化为文本。
- **Freestanding 无 libc 依赖设计**：ARM Cortex-M3 裸机环境下不依赖 `snprintf`，使用自研有界整数/字符串格式化工具，零外部依赖，极低 Flash 占用。

---

## 三、工程约束与保护核查

1. **TCB ABI**：未修改 TCB 结构，保持 72 字节。
2. **汇编接口**：未修改 `portasm.S`、SVC / PendSV 汇编或上下文切换寄存器布局。
3. **调度机制**：就绪队列、多优先级就绪位图、时间片轮转机制完全保持。
4. **硬件固件保护**：ST7789 驱动、SPI3/DMA、硬件工程 `Middlewares` 快照与 Stage 8.5-C4-R1 固件完全未被修改或覆盖。
5. **硬件操作**：未执行任何 STM32 Flash 编程、擦除或复位。
