# Stage 9.2: 资源预算与消耗审计报告 (Resource Report)

## 一、诊断工作区内存预算 (Workspace Memory Budget)

根据 Stage 9.0 设计基准文档 `docs/diagnostics_design/resource_budget.md`，死锁诊断工作区结构体在 32-bit ARM Cortex-M 平台上的内存占用预算上限为 **384 字节**。

在 `include/rtos_diag.h` 与 `tests/stage9_2/test_deadlock_host.c` 中通过 `_Static_assert` 强制校验了各组成字段大小：

```c
_Static_assert(sizeof(rtos_diag_dl_header_t) == 24U, "Header size mismatch");
_Static_assert(sizeof(rtos_diag_dl_node_t) == 24U, "Node size mismatch");
_Static_assert(sizeof(rtos_diag_dl_result_t) == 48U, "Result size mismatch");
#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(rtos_diag_dl_workspace_t) == 384U, "Workspace size must be 384 bytes on 32-bit");
#endif
```

### 内存布局明细 (32-bit ARM)
| 结构成员 | 类型与维度 | 单项大小 | 成员总大小 | 累计偏移 |
|---|---|---|---|---|
| `header` | `rtos_diag_dl_header_t` | 24 B | 24 B | 0 B |
| `nodes` | `rtos_diag_dl_node_t[8]` | 24 B | 192 B | 24 B |
| `resource_keys` | `uintptr_t[8]` | 4 B | 32 B | 216 B |
| `task_slots` | `uint16_t[8]` + 对齐 | 2 B | 32 B | 248 B |
| `scratch` | `uint8_t[24]` | 1 B | 24 B | 280 B |
| `result` | `rtos_diag_dl_result_t` | 48 B | 48 B | 304 B |
| `reserved` | `uint8_t[32]` | 1 B | 32 B | 352 B |
| **总计** | `rtos_diag_dl_workspace_t` | — | **384 B** | **384 B** |

---

## 二、代码体积审计 (Code Size & Symbol Analysis)

在生产固件 `insightrtos_display_o2_trace.elf` 中提取符号大小（`arm-none-eabi-nm --size-sort -C`）：

| 诊断 API 符号 | 职责说明 | 符号大小 (Bytes) |
|---|---|---|
| `rtos_diag_dl_result` | 提取分析结果指针与就绪判断 | 20 B |
| `rtos_diag_dl_capture` | 极短临界区依赖图快照构建 | 480 B |
| `rtos_diag_dl_analyze` | 三色 DFS 环路搜索与规范化旋转 | 712 B |
| `rtos_diag_dl_verify_current` | 实时插槽世代与生命周期校验 | 44 B |
| **死锁诊断核心代码总和** | | **1,256 B (~1.2 KiB)** |

---

## 三、栈消耗审计 (Stack Usage Analysis)

通过 `-fstack-usage` 编译器生成的 `deadlock.c.su` 静态审计数据：

| 函数名称 | 静态局部栈消耗 | 栈类型 | 预算合规性 |
|---|---|---|---|
| `rtos_diag_dl_capture` | **88 Bytes** | static | 远低于任务栈警戒线 (1024 B) |
| `rtos_diag_dl_analyze` | **72 Bytes** | static | 远低于任务栈警戒线 (1024 B) |
| `rtos_diag_dl_verify_current` | **24 Bytes** | static | 极轻量 |
| `rtos_diag_dl_result` | **0 Bytes** | static | 纯内联返回，零额外栈开销 |

---

## 四、STM32 固件制品尺寸对比 (Firmware Footprint Comparison)

| 固件预设配置 | Flash 占用 (BIN 字节) | Flash 占用率 (512 KiB) | RAM 占用 (BSS+DATA) | 变动说明 |
|---|---|---|---|---|
| `DISPLAY_RTOS_Debug` (Trace OFF) | 57,400 B | 10.95% | 45,308 B | 调试版基线 |
| `DISPLAY_RTOS_O2` (Trace OFF) | 39,468 B | 7.53% | 45,304 B | 生产版基线 |
| `DISPLAY_RTOS_Debug_Trace` (Trace+DL ON) | 62,884 B | 11.99% | 45,428 B | 调试版完整诊断 |
| `DISPLAY_RTOS_O2_Trace` (Trace+DL ON) | **43,620 B** | **8.32%** | **45,428 B** | **生产推荐固件** |

> **关键优化成果**：
> 相比 Stage 9.1-H1 固件（63,384 B），在新增 Mutex 死锁诊断功能的同时，通过 UI 页面架构整合，**Flash 净减少 19,764 字节 (-31.2%)**，彻底消除冗余测试图片与字模。
