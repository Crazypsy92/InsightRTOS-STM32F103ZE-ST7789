# Stage 9.1-H2 60 秒实机连续稳定性运行报告

## 一、测试概要

- **测试时长**：60.06 秒（连续不间断 SWD DAP 采样）
- **采样点数**：31 个周期采样点（采样间隔约 2.0 秒）
- **采样模式**：严格运行期模式（`strict_uninterrupted_window=True`，无 GDB 断点暂停，MCU 自由全速运行）
- **内核配置**：InsightRTOS v1.0.0 (O2 Optimization + Full Explainable Diagnostics)

---

## 二、关键指标与稳定性数据对比

| 监测项 | 采样起始值 (T=0s) | 采样结束值 (T=60s) | 增量 (Delta) | 理论预期 | 判定 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Monotonic Time** | 0.00 s | 60.06 s | 60.06 s | 60.0 s | **PASS** |
| **Kernel Ticks** | 33,009 | 39,015 | +6,006 | ~6,006 (100 Hz) | **PASS** |
| **TIM2 1ms 时基** | 330,114 ms | 390,166 ms | +60,052 ms | ~60,060 ms (1 kHz) | **PASS** |
| **Trace 全局序列** | 373,009 | 441,377 | +68,368 事件 | 持续环形覆盖 | **PASS** |
| **Controller 运行次数**| 32,971 | 38,975 | +6,004 次 | ~100 Hz 定时驱动 | **PASS** |
| **LCD 绘制刷新次数** | 10,724 | 12,726 | +2,002 帧 | ~33.3 Hz (动画/刷新) | **PASS** |
| **Monitor 监控次数** | 660 | 780 | +120 次 | ~2.0 Hz (500ms) | **PASS** |
| **SPI/DMA 传输错误** | 0 | 0 | 0 | 0 | **PASS** |
| **SPI3 驱动恢复次数** | 0 | 0 | 0 | 0 | **PASS** |
| **RTOS 断言报错行** | 0 | 0 | 0 | 0 (`g_rtos_assert_line=0`) | **PASS** |
| **ARM Fault 寄存器** | CFSR=0, HFSR=0 | CFSR=0, HFSR=0 | 0 | 无硬件异常 | **PASS** |

---

## 三、栈内存哨兵检测（Stack Sentinels）

全部任务栈底哨兵字（`0xdeadc0de` / 3735929054）在 60 秒连续高负荷运行前后进行连续监控：

- **KEY Task** (`0x200020e0`): `0xdeadc0de`（完整未受破坏）
- **Controller Task** (`0x200030e0`): `0xdeadc0de`（完整未受破坏）
- **LCD Task** (`0x200040e0`): `0xdeadc0de`（完整未受破坏）
- **Monitor Task** (`0x200050e0`): `0xdeadc0de`（完整未受破坏）
- **Idle Task** (`0x20001d80`): `0xdeadc0de`（完整未受破坏）

**判定**：无任何栈下溢或内存越界行为，系统稳定可靠。
