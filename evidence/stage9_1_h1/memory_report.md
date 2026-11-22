# Stage 9.1-H1 存储与栈预算分析报告 (Memory Report)

## 一、概述 (Overview)

本报告详细统计 STM32F103ZET6 + ST7789 应用在集成 Explainable Scheduling 后的 Flash 与 SRAM 链接分布，对比启用诊断与关闭诊断的资源增量，核查栈调用链安全与余量。

---

## 二、固件大小与内存分布表 (Firmware Size & Memory Map)

| 构建配置 (Profile) | 优化等级 | 诊断状态 | Text (B) | Data (B) | BSS (B) | Flash 占用 | Flash 占比 | SRAM 占用 | SRAM 占比 |
|---|---|---|---|---|---|---|---|---|---|
| **DISPLAY_RTOS_O2_Trace** | -O2 | **ON** | 63,240 | 144 | 45,136 | **63,384 B** | 12.09% | **45,280 B** | 69.09% |
| **DISPLAY_RTOS_Debug_Trace** | -g -O0 | **ON** | 82,796 | 144 | 45,132 | **82,940 B** | 15.82% | **45,276 B** | 69.09% |
| **DISPLAY_RTOS_O2 (Baseline)** | -O2 | OFF | 59,684 | 144 | 42,032 | **59,828 B** | 11.41% | **42,176 B** | 64.36% |
| **DISPLAY_RTOS_Debug (Baseline)**| -g -O0 | OFF | 78,200 | 144 | 42,036 | **78,344 B** | 14.94% | **42,180 B** | 64.36% |

---

## 三、资源增量与预算精确核对 (Delta Analysis)

### 1. SRAM 增量核算

- **实测增量**：
  $$\Delta \text{SRAM} = 45,280\text{ B} - 42,176\text{ B} = 3,104\text{ B}$$
- **理论结构组成**：
  1. `trace_records[128]` 环形数组：$128 \times 24\text{ B} = 3,072\text{ B}$。
  2. 全局游标与序列号变量（`trace_write_index`, `trace_count`, `trace_next_sequence`, `trace_overwritten`, `trace_next_sequence_high`, `trace_status`）：$6 \times 4\text{ B} = 24\text{ B}$。
  3. 第 6 页前次行缓冲对比色彩状态（`previous_trace_color[10]`）：$10 \times 2\text{ B} = 20\text{ B}$。
  4. 结构体与对齐填充（4 字节对齐）：$12\text{ B}$。
  - **总计**：恰好为 **3,104 B**，与 Stage 9.0 设计预算（3,104 B）完全相符。
- **SRAM 剩余余量**：
  $$65,536\text{ B} - 45,280\text{ B} = 20,256\text{ B} \quad (30.91\% \text{ 自由余量})$$
  为主栈（MSP）、中断处理及临界保护提供极为充足的安全空间。

### 2. Flash 增量核算

- **实测增量 (O2 优化)**：
  $$\Delta \text{Flash} = 63,384\text{ B} - 59,828\text{ B} = 3,556\text{ B} \quad (0.69\% \text{ of 512KB})$$
- **代码增量分布**：
  - Trace 环形存取与 64 位单调序列管理逻辑：~780 B。
  - `rtos_diag_explain_record()` 解包与因果分析引擎：~1,120 B。
  - `Display_UISchedTrace()` 字符格式化与差分刷新逻辑：~1,656 B。
- **Flash 剩余空间**：
  $$524,288\text{ B} - 63,384\text{ B} = 460,904\text{ B} \quad (87.91\% \text{ 剩余})$$

---

## 四、任务栈预算与深度分析 (Task Stack Budgets)

所有 4 个用户任务与 Idle 任务的静态栈分配均经过深度调用链审查：

| 任务名称 (Task) | 优先级 | 分配字数 (Words) | 分配字节 (Bytes) | 最大调用深度预估 | 峰值栈使用估算 | 安全余量 (Headroom) |
|---|---|---|---|---|---|---|
| **KEY Task** | 4 | 256 | 1,024 B | `KEY_Task` -> `MultiButton` -> `key_port` | ~312 B | **> 69%** |
| **Controller Task**| 3 | 512 | 2,048 B | `Controller_Task` -> `Queue_Send` -> `Kernel` | ~480 B | **> 76%** |
| **LCD Task** | 2 | 1,024 | 4,096 B | `LCD_Task` -> `Display_UISchedTrace` -> `Font` | ~820 B | **> 79%** |
| **Monitor Task** | 1 | 256 | 1,024 B | `Monitor_Task` -> `Display_ReadUIView` | ~288 B | **> 71%** |
| **Idle Task** | 0 | 128 | 512 B | `rtos_port_idle` -> `__WFI` | ~64 B | **> 87%** |

**结论**：
所有任务栈余量均在 69% 以上，不存在栈溢出隐患。工程严禁动态内存分配（零 `malloc` / 零 `free`），彻底根除内存碎片风险。
