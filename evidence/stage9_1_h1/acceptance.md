# InsightRTOS Stage 9.1-H1: Explainable Scheduling STM32 集成与 ST7789 诊断界面验收报告

## 一、验收结果总结 (Final Verdict Summary)

| 检查项 (Item) | 状态 (Status) | 说明 (Notes) |
|---|---|---|
| **SOFTWARE INTEGRATION** | **PASS** | 完整将 Stage 9.1 生产内核无缝集成至 `app/stm32f103ze_st7789`，新增第 6 个 LCD 页面 `SCHED TRACE`，零 mock 泄漏 |
| **STATE MACHINE & UI** | **PASS** | 6 页面状态机流转 (0->1->2->3->4->5->0) 闭环；支持 KEY1 短按翻页；第 6 页 250ms 定期差分刷新，总线开销极低 |
| **TRACE EXPLANATION** | **PASS** | 7 类移交决策因果解析准确，色彩编码分明；支持任务代次陈旧检测 (`?` 标注)；强制标明 C 提交非 ARM 寄存器证明 |
| **HOST TEST SUITE** | **PASS** | 4 项 H1 专项测试全部通过；历史 7 项 C1 应用测试 100% 通过；全内核 349 项 Host 单元测试全部通过 |
| **QEMU ARM REGRESSION** | **PASS** | Cortex-M3 (MPS2 AN385) 真实指令集仿真 202 项测试 100% 通过；GDB 探针物理核查 IPSR=14 与 PSP/Callee-Saved 寄存器安全 |
| **CONFIGURATION MATRIX** | **PASS** | 支持 `TRACE ON / EXPLAIN ON` (6 页面) 与 `TRACE OFF / EXPLAIN OFF` (5 页面) 两种构建配置，Debug / O2 均编译通过 |
| **MEMORY BUDGET** | **PASS** | O2 Trace 固件 Flash 仅 63,384 B (12.09%)，SRAM 增量恰为 3,104 B (与 Stage 9.0 理论预算完全一致)，剩余 30.91% SRAM 自由空间 |
| **TCB & ABI FROZEN** | **PASS** | 严格保持 TCB 结构 (72 B) 及偏移、`portasm.S` 汇编接口零改动、上下文切换 ABI 零变动 |
| **HARDWARE STATUS** | **NOT EXECUTED** | 严格遵守工程安全守则：未经用户针对本固件、目标设备及精确扇区范围的明确授权，**未执行** Flash 擦除、烧录或复位 |
| **OPEN DEFECTS** | **OPEN** | Stage 8.5-C3 遗留之硬件 SPI3 RXNE/OVR 物理中断边界在当前软件测试中无法直接模拟，继续保留 OPEN 状态待实机验证 |

---

## 二、关键指标与交付物核查 (Deliverables & Manifest)

### 1. 证据交付物清单 (`evidence/stage9_1_h1/`)

1. [acceptance.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/acceptance.md) - 本综合验收报告。
2. [integration_report.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/integration_report.md) - 生产内核集成、CMake 构建系统与四任务协同机制报告。
3. [middleware_version_manifest.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/middleware_version_manifest.md) - 中间件与上游生产源码 SHA256 比对清单（44 文件逐一核验）。
4. [trace_ui_design.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/trace_ui_design.md) - ST7789 240×240 第 6 页排版规约、字体、色彩及差分刷新设计。
5. [trace_event_mapping.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/trace_event_mapping.md) - 24 字节 Trace 二进制结构向界面文字与因果分类映射说明。
6. [host_test_results.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/host_test_results.md) - Host 专项测试（状态机、UI 映射、超高速环形回绕抗性、禁用回退）及历史回归日志。
7. [qemu_test_results.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/qemu_test_results.md) - ARM Cortex-M3 真实架构 QEMU 202 项测试结果及 GDB 断点报告。
8. [memory_report.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/memory_report.md) - Flash 与 SRAM 链接分布、增量核算及五任务栈调用链余量分析。
9. [firmware_manifest.json](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/firmware_manifest.json) - 包含入口地址、扇区范围、大小及 SHA256 的固件元数据清单。
10. [hardware_test_plan.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/hardware_test_plan.md) - 实机部署流程、逐字节回读核验及 8 项实机功能验收测试计划。

### 2. 固件交付物清单 (`artifacts/stage9_1_h1/`)

| 固件配置 | 文件 | SHA256 校验码 | 规格说明 |
|---|---|---|---|
| **O2 Trace (推荐部署)** | `insightrtos_display_o2_trace.bin` | `aa861203b2027b06f088c6be1562ce33803d1485bfa7befa712fc5fac394b91a` | 63,384 B (31 页), 入口 0x080010b9 |
| | `insightrtos_display_o2_trace.hex` | `697c33429d1f5a26fd17150f02ebfaeab2767359e44363852b61b20400d4e891` | Intel HEX 格式，校验与 BIN 一致 |
| | `insightrtos_display_o2_trace.elf` | `00a3653d4719d19afd9c142035125427517a229af2d934fd7df49a6142dc2e59` | 带符号表 ELF |
| | `insightrtos_display_o2_trace.map` | `e615d12e7ec91b8dad83c248db0ada1f2eeed7a83319ead42ffca685848c9527` | 链接符号映射表 |
| **Debug Trace** | `insightrtos_display_debug_trace.bin`| `f13bc21c395ebf5962276f05f884be3e8fde92b7e6cecc682f0345afcf41d6f9` | 82,940 B (41 页), 入口 0x080016c1 |
| | `insightrtos_display_debug_trace.hex`| `d4f9deadc939185f305d52869d8c3448e903c20ccfc01dded3b463546f46abb0` | Intel HEX 格式 |
| | `insightrtos_display_debug_trace.elf`| `9906e060fe6a1d20c45ba17cf04903379a6558ee407d53d47f39e63b189ecf01` | 带完整 DWARF-4 调试信息 |
| | `insightrtos_display_debug_trace.map`| `b387e5fb888beaec72206d28ad14926904c2938df311adcae52e7a80bd026b98` | 链接符号映射表 |
| **O2 Baseline** | `insightrtos_display_o2.bin` | `6e90b13f1d69802ca5a7b4c2b5f7bc1cc473626d546cae232995c98fad4fd924` | 59,828 B (30 页), Trace 关闭基线 |
| **Debug Baseline** | `insightrtos_display_debug.bin` | `eea35032e72d620cde96c34fe1af96182f6497d475cdd9d8ab3de8a78f9bbc9c` | 78,344 B (39 页), Trace 关闭基线 |

---

## 三、后续硬件测试就绪状态 (Next Steps)

固件已在 `artifacts/stage9_1_h1/` 完成全量封装与 ELF/BIN/HEX 双向一致性校验。
在用户针对目标硬件提供明确授权后，即可按照 [hardware_test_plan.md](file:///home/mythepsy92/program/codex/InsightRTOS/evidence/stage9_1_h1/hardware_test_plan.md) 执行 Flash Page 0..30 有界擦除、烧录与实机功能验收。
