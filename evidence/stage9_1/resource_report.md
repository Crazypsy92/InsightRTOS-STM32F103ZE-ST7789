# InsightRTOS Stage 9.1: Resource and Budget Compliance Report

## 一、STM32F103ZET6 物理平台测量依据

- **物理参数**：Flash 512 KiB (0x08000000 - 0x08080000)，SRAM 64 KiB (0x20000000 - 0x20010000)。
- **编译器与构建类型**：`arm-none-eabi-gcc 14.2.1`，支持 Debug (`-O0 -g`) 与 O2 (`-O2 -g -DNDEBUG` 即 RelWithDebInfo)。
- **硬件配置规范**：独立运行模式 (`stm32f103ze_standalone`)，MSP 独立保留 4,096 B (位于 0x2000F000~0x20010000)，8 个用户任务栈共 32,768 B。

---

## 二、真实 ELF 与 MAP 测量数据对比

对编译生成的独立运行固件通过 `arm-none-eabi-size` 进行物理切片度量：

| 构建配置 | Text (代码区) | Data (已初始化) | BSS (零初始化) | 总静态 RAM (Data+BSS) | 较基线 RAM 增量 |
|---|---:|---:|---:|---:|---:|
| **Debug: Trace OFF / Explain OFF** | 24,360 B | 8 B | 37,960 B | 37,968 B | 基准 (0 B) |
| **Debug: Trace ON / Explain OFF** | 25,636 B | 8 B | 41,080 B | 41,088 B | +3,120 B |
| **Debug: Trace ON / Explain ON** | **26,240 B** | **8 B** | **41,088 B** | **41,096 B** | **+3,128 B** |
| **O2: Trace OFF / Explain OFF** | 13,104 B | 8 B | 37,960 B | 37,968 B | 基准 (0 B) |
| **O2: Trace ON / Explain OFF** | 14,264 B | 8 B | 41,080 B | 41,088 B | +3,120 B |
| **O2: Trace ON / Explain ON** | **14,564 B** | **8 B** | **41,088 B** | **41,096 B** | **+3,128 B** |

---

## 三、静态内存 (SRAM) 增量核查

### 1. 目标预算对照
在 Stage 9.0 架构设计中（`docs/diagnostics_design/resource_budget.md` 第 9 行）：
> Explain 单开：128×24 + 16 现有 Trace，外加 seq 高字与状态 2×4 = 3,096 B

### 2. 实际分配核查
在 `build/stage9_1-standalone-o2-explain/libinsight_kernel.a` 中对 `trace.c.obj` 实际检查：
```
text: 5687 B
data: 0 B
bss:  3096 B (trace_records 3072 B + 4个旧元数据 16 B + 2个新元数据 8 B)
```
- `trace_records[128]`：$128 \times 24 = 3,072$ B
- `trace_write_index`, `trace_count`, `trace_next_sequence`, `trace_overwritten`：$4 \times 4 = 16$ B
- `trace_next_sequence_high`, `trace_status`：$2 \times 4 = 8$ B
- **内核 Trace 模块总静态占用恰好为 3,096 B，单字不差！**
- 相对于已开启 Trace 的系统，Explain 模块仅增加 **8 字节** 全局变量。

---

## 四、代码区 (Flash) 增量核查

### 1. 目标预算对照
- Stage 9.0 规定的 Explain 模块 Flash 新增上限目标：**≤ 2,048 B**。

### 2. 实测增量 (相对于 Trace ON / Explain OFF)
- **O2 生产优化构建**：
  $$14,564\text{ B} - 14,264\text{ B} = \mathbf{+300\text{ B}}$$
  （仅消耗预算的 14.6%，余量 1,748 B）
- **Debug 调试构建**：
  $$26,240\text{ B} - 25,636\text{ B} = \mathbf{+604\text{ B}}$$
  （仅消耗预算的 29.5%，余量 1,444 B）

Flash 增量远低于 2,048 B 硬指标，完全合格。

---

## 五、栈与动态内存预算

1. **动态内存 (Heap)**：`heap = 0`，未引入任何 `malloc`、`free` 或 `_sbrk`。
2. **任务栈容量增量**：0 B。各任务控制块 TCB 维持 72 B ABI。
3. **关键路径锁持有与中断屏蔽**：
   - `rtos_trace_context_switch_locked()` 仅执行几条移位与槽位写入，耗时 ≤ 35 cycle (~0.48 µs @ 72MHz)。
   - `rtos_diag_trace_read()` 每次批处理读取最多 8 条记录，有界临界区内仅执行内存 memcpy，耗时 ≤ 85 cycle (~1.18 µs @ 72MHz)。
   - 字符串格式化全部在临界区外、非中断上下文中执行。
