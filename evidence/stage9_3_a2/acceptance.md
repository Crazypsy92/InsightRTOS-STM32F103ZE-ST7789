# InsightRTOS Stage 9.3-A2 主验收报告

## 一、阶段定位与执行边界

- **项目**：`Crazypsy92/InsightRTOS-STM32F103ZE-ST7789`
- **对应任务**：GitHub Issue #6（Stage 9.3-A2 实时性与可靠性基线验收）
- **基线 Commit**：`3005e24391263d915998f497475d681cbb4c1a74` (Stage 9.3-A)
- **目标宗旨**：本轮为 Stage 9.3-A 完成后的**基线加固与实机验真**。重点解决测试基础设施真实性（彻底消除 QEMU 假 PASS 与硬件硬编码读取）、事件流水线在 8 槽历史下的 ACK 韧性与生命周期统计、受控烧录、60 秒基线采样以及 5 分钟受控高频压力测试。
- **阶段边界**：
  - **不**进入 Stage 9.3-B（未实现 Job Release/Complete 周期统计与 Deadline Miss 判定）。
  - **不**修改 240×240 中文 LCD 验收页面。
  - **不**增设无关硬件外设或修改 SVC/PendSV 汇编 ABI。
  - **不**执行 Flash 全片 Mass Erase。

---

## 二、六大检查点完成与核验矩阵

| 序号 | 检查项 / 验收标准 | 实施方案 / 实际执行情况 | 状态判定 |
| :---: | :--- | :--- | :---: |
| 1 | **测试基础设施真实性** | 修正 `run_rt_qemu.sh` / `run_qemu.sh`，剔除 `|| true` 与 Host 替代，因无 `qemu-system-arm` 真实输出 `NOT_TESTED`；重构 `sample_stability.py`，全动态符号寻址，禁止回落假 0 错误。 | **PASS** |
| 2 | **真实 RT 只读快照导出** | 在 Monitor Task 中周期导出 `g_diag_rt_export`（含时钟能力、4 任务与 Idle 周期快照）；Python 脚本直接解析内存。 | **PASS** |
| 3 | **事件队列有界历史与 ACK 韧性** | 扩展 `app_events.c/.h`，增加 `evicted_unacked_count` 与 `orphaned_ack_count`；槽位被环形覆盖的事件在延迟 ACK 到达时仍准确计入 `success_count`，**零静默丢失**。 | **PASS** |
| 4 | **资源核验与编译对比** | 编译 `DISPLAY_RTOS_O2` (RT OFF) 与 `DISPLAY_RTOS_O2_Trace` (RT ON)；Flash 占用从 46,556 B 增至 50,604 B（增量 4,048 B）；静态侧表 SRAM 占用 568 B（预算上限 1024 B，余量 456 B）；栈开销 $\le 64$ B。 | **PASS** |
| 5 | **受控硬件烧录与回读** | 严格仅擦除主 Flash 第 0～30 页 `[0x08000000, 0x0800F800)`（63,488 字节）；写入 50,604 字节；回读 SHA256 100% 匹配；尾部 12,884 字节全为 `0xFF`；Option Bytes 前后完全一致。 | **PASS** |
| 6 | **60s 基线 + 5min 压力实测** | 60 秒基线采样：13 组样本，0 错误，TIM2 994.32 Hz，Tick 99.42 Hz；5 分钟（300 秒）压力测试：注入 10 events/s，11 组样本，574 次受控丢弃，0 传输错误，0 内存越界，0 HardFault。 | **PASS** |

---

## 三、实机实测时序与周期分布

在 STM32F103ZET6（72 MHz Cortex-M3）硬件上，通过 DWT CYCCNT 实际采样的 4 个生产任务及 Idle 周期与唤醒就绪延迟如下：

```text
+----------+--------------------+---------------------+--------------------+--------------------+
| 任务名称 | 60s 基线累计周期   | 300s 压力累计周期   | 最新就绪延迟 (Cycles)| 历史最大就绪延迟   |
+----------+--------------------+---------------------+--------------------+--------------------+
| KEY      | 3,830              | 3,830               | 29,751 (~413 us)   | 29,751 (~413 us)   |
| CTRL     | 26,830,304         | 153,781,449         | 2,109 (~29 us)     | 24,707 (~343 us)   |
| LCD      | 36,714,475         | 413,078,801         | 7,105 (~98 us)     | 19,887 (~276 us)   |
| MON      | 886,465            | 5,167,699           | 8,556 (~118 us)    | 1,073,110 (~14.9ms)|
| IDLE     | 4,615,585,135      | 26,283,988,661      | 0                  | 0                  |
+----------+--------------------+---------------------+--------------------+--------------------+
```

- **高优先级保障**：KEY (P5)、CTRL (P4)、LCD (P2) 就绪延迟均在微秒级（29～413 $\mu$s），满足抢占式实时响应。
- **低优先级可抢占**：MON (P1) 历史最大延迟为 1,073,110 cycles（约 14.9 ms），为正常被高优先级任务（LCD 绘制与 CTRL 事件流）抢占造成的合理调度延期。

---

## 四、结论分级声明

依据 Issue #6 规范，本阶段验收结论严格采用四级分级声明：

1. **`FUNCTIONAL_PASS`（功能验证通过）**：
   - 模块化应用 Host 测试 9/9（45 断言）全部通过。
   - 实时诊断 Host 测试 9/9（45 断言）全部通过。
   - 内核历史回归测试 360/360 ctest 全部通过。
   - 硬件受控烧录回读 SHA256 100% 匹配，Option Bytes 未受改动。
   - 60 秒常态与 5 分钟受控压力运行通过，0 系统传输错误，0 HardFault，0 Assert。
2. **`MEASURED_LATENCY`（实测时延表征）**：
   - 给出真实硬件 DWT 计量的 CPU Occupancy 周期分布与 Ready-to-Run Latency 分布。
3. **`BOUNDARY_UNVERIFIED`（未验证硬边界声明）**：
   - 外部真实 GPIO 物理电平到中断进栈延迟未接物理示波器/逻辑分析仪，标记为未测。
   - 调试器同时冻结 SysTick 与 CYCCNT 时的外部物理时间流逝无法被 MCU 内部自主感知，作为已知时钟边界限制记录于 [`timing_safety.md`](./timing_safety.md)。
   - 任务级 Deadline 违约判定与执行预算超限熔断尚未实现（留待 Stage 9.3-B）。
4. **`SAFETY_UNCERTIFIED`（非功能安全认证声明）**：
   - 本阶段测试属于科研与开源工业内核的工程基线验收，**不**构成 IEC 61508、ISO 26262 或 DO-178C 等功能安全认证。

---

## 五、交付证据清单

- 主验收报告：[`evidence/stage9_3_a2/acceptance.md`](file:///home/mythepsy92/program/Cube_MX/Cmake/STM32F103ZET6/insightrtos/insightrtos/evidence/stage9_3_a2/acceptance.md)
- 时序可信度与安全性契约：[`evidence/stage9_3_a2/timing_safety.md`](file:///home/mythepsy92/program/Cube_MX/Cmake/STM32F103ZET6/insightrtos/insightrtos/evidence/stage9_3_a2/timing_safety.md)
- 完整测试结果与命令：[`evidence/stage9_3_a2/test_results.md`](file:///home/mythepsy92/program/Cube_MX/Cmake/STM32F103ZET6/insightrtos/insightrtos/evidence/stage9_3_a2/test_results.md)
- 固件与资源清单：[`evidence/stage9_3_a2/manifest.json`](file:///home/mythepsy92/program/Cube_MX/Cmake/STM32F103ZET6/insightrtos/insightrtos/evidence/stage9_3_a2/manifest.json)
- 实机日志目录：[`evidence/stage9_3_a2/logs/`](file:///home/mythepsy92/program/Cube_MX/Cmake/STM32F103ZET6/insightrtos/insightrtos/evidence/stage9_3_a2/logs/)
  - `programming.log`
  - `readback_verification.json`
  - `stability_normal_60s.json`
  - `stability_normal_60s_samples.jsonl`
  - `stability_stress_300s.json`
  - `stability_stress_300s_samples.jsonl`
