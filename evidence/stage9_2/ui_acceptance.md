# Stage 9.2: 综合状态显示界面设计与验收报告 (UI Acceptance)

## 一、UI 精简与综合页面重构目标

根据 Stage 9.2 规范与设计约束，移除了原有的 6 个测试与演示页面（开机欢迎、RGB 颜色条、几何图形、字体展示、小球动画、调度详情分立页），将其精简替换为一个**单一固定 240×240 综合状态验收页面**：
- **消除闪烁与开机延迟**：取消一切开机阻塞延迟与全屏频繁清屏。
- **纯字符级差分刷新**：每次刷新仅重绘内容发生变化的文字区域，最大程度降低 SPI3 和 DMA 总线占用。
- **高实时与信息完备**：在单屏 15 行文字中完整呈现系统内核心跳、所有活动任务运行状态与 Mutex 死锁诊断实时指标。

---

## 二、240×240 单屏布局设计 (Screen Layout)

ST7789 显示驱动分辨率为 240×240，采用 16×16 等宽点阵字体，整屏划分 15 行（每行行高 16 像素）：

```text
+----------------------------------------+
| 00: InsightRTOS SYSTEM RUNNING         | [青色 Cyan]
| 01: ---------------------------------- | [深灰 Gray]
| 02: KERNEL: 100Hz TIM2:1kHz TICK:12480 | [白色 White]
| 03: ---------------------------------- |
| 04: TASK STATUS:                       | [黄色 Yellow]
| 05:  KEY : RUN  PRIO:4(4) RUNS: 1248   |
| 06:  CTRL: BLK  PRIO:3(3) RUNS: 1248   |
| 07:  LCD : RUN  PRIO:2(2) RUNS:  499   |
| 08:  MON : BLK  PRIO:1(1) RUNS:  124   |
| 09: ---------------------------------- |
| 10: MUTEX DIAGNOSTICS:                 | [黄色 Yellow]
| 11:  STATUS: ACTIVE (NO DEADLOCK)      | [绿色 Green / 红色 Red]
| 12:  CYCLE : NONE      PI BOOST: 0     |
| 13:  NODES : 4 (0 EDGES)               |
| 14:  TRACE : 128 REC   ERRORS: 0       | [白色 White]
+----------------------------------------+
```

### 15 行字段含义定义
1. **Row 00 (标题栏)**: `InsightRTOS SYSTEM RUNNING`（全局状态）。
2. **Row 01 (分隔线)**: 水平视觉分割。
3. **Row 02 (内核心跳)**: 显示 RTOS 系统 Tick 频率 (100 Hz)、硬件 TIM2 高精度时基 (1 kHz) 及当前运行 Tick 计数值。
4. **Row 03 (分隔线)**: 水平视觉分割。
5. **Row 04 (任务分栏头)**: `TASK STATUS:`。
6. **Row 05 (KEY 任务)**: 状态（RUN/READY/BLK）、基础优先级、有效优先级、调度运行计数。
7. **Row 06 (Controller 任务)**: 状态、基础/有效优先级、运行计数。
8. **Row 07 (LCD 任务)**: 状态、基础/有效优先级、运行计数。
9. **Row 08 (Monitor 任务)**: 状态、基础/有效优先级、运行计数。
10. **Row 09 (分隔线)**: 水平视觉分割。
11. **Row 10 (诊断分栏头)**: `MUTEX DIAGNOSTICS:`。
12. **Row 11 (死锁检测状态)**:
    - 正常无死锁：`STATUS: ACTIVE (NO DEADLOCK)`（绿色高亮）。
    - 检出死锁环路：`STATUS: DEADLOCK DETECTED!`（红色高亮闪烁警示）。
    - 诊断未激活：`STATUS: DIAG DISABLED`。
13. **Row 12 (环路路径与 PI 状态)**:
    - `CYCLE: NONE` 或 `CYCLE: T1->T2->T1`（标明构成死锁环的具体任务序列）。
    - `PI BOOST`: 统计当前处于优先级提升状态的任务数。
14. **Row 13 (依赖图拓扑)**: 显示快照中的活动节点数与互斥等待边数（`NODES: X (Y EDGES)`）。
15. **Row 14 (追踪与健康状态)**: 显示当前 Trace 环形缓冲区有效记录数及系统错误计数。

---

## 三、差分渲染机制与性能保障 (Differential Refresh)

- **显存与带宽**：在 `display_ui.c` 中维护 15 行 `last_lines[15][32]` 字符缓存。
- **逐行比对**：每次 250 ms 周期调用 `Display_UIConsolidated(false)` 时，格式化生成新文本并使用 `strncmp` 快速比对。
- **无变动不重绘**：若该行字符与上一帧完全相同，跳过 SPI3/DMA 发送；若只有某几行（如 Tick 计数或运行次数）发生变动，仅重绘对应行矩形区域（`ST7789_FillRect` + `ST7789_DrawString`）。
- **总线耗时**：单行字符重绘通过 SPI3 DMA 仅耗时约 0.25 ms，整屏无变动时总线开销为 **0 µs**，彻底杜绝 UI 任务对按键与监控任务的抢占延迟。
