# Stage 9.2-H1: 单屏诊断固件实机部署与验收最终报告 (Acceptance)

## 一、验收执行概要

- **工程路径**: `/home/mythepsy92/program/codex/InsightRTOS`
- **开源仓库**: `https://github.com/Crazypsy92/InsightRTOS-STM32F103ZE-ST7789`
- **目标硬件**: STM32F103ZET6 (512 KiB Flash, 64 KiB SRAM, ARM Cortex-M3 @ 72 MHz) + ST7789 240×240 IPS
- **烧录固件**: `artifacts/stage9_2/insightrtos_display_o2_trace.bin`
  - 固件大小: 43,620 字节
  - 固件 SHA256: `44a52a2bb7ed423ac8fc5ac09b7c95f08e5185f50d4c170dbf84d136458201c1`
- **烧录范围**: 主 Flash 扇区 0～30 (`[0x08000000, 0x0800F800)`，共 63,488 字节)
- **Option Bytes**: `0x03FFFFFC` (RDP=0, 读保护未启用，写保护无修改，烧录前后完全一致)

---

## 二、核心验收状态表 (Acceptance Verdict Table)

| 验收项 (Inspection Item) | 状态判定 (Status) | 核心证据与指标 (Key Metrics & Evidence) |
| :--- | :--- | :--- |
| **FLASH PROGRAMMING** | **PASS** | 扇区 0～30 精确擦除，写入 43,620 B，无 Mass Erase，Option Bytes 零变动 |
| **FLASH READBACK** | **PASS** | 读回 43,620 B SHA256 `44a52a2bb7...` 逐字节 100% 一致；尾部 19,868 B 全 `0xFF` |
| **RTOS SCHEDULING** | **PASS** | 4 业务任务 (KEY, Controller, LCD, Monitor) + Idle 抢占式调度正常，60s 调度 > 6,400 次 |
| **SINGLE SCREEN** | **待用户确认** | RAM 显存 `previous_cons` 15 行信息完整无截断；物理目视观感待用户最终确认 |
| **EXPLAIN TRACE** | **PASS** | 128 条记录 24B ABI 环形缓冲，60s 记录 51,229 次因果调度事件，覆盖统计正常 |
| **MUTEX DIAGNOSTICS** | **PASS** | 任务上下文图分析正常，`Status: NO DEADLOCK`，极短快照临界区 (< 3 µs)，无死锁 |
| **PHYSICAL DEADLOCK INJECTION** | **NOT TESTED** | 正常生产应用不故意制造物理死锁；保留 Stage 9.2 Host/QEMU 100% 模拟测试 PASS 证据 |
| **STABILITY 60s** | **PASS** | 连续运行 61.01s，`sys_tick` 同步增长 61,029 (1000.3 Hz)，错误数 0，断言失败数 0，复位自愈正常 |

---

## 三、显存实时排版对照 (Live RAM Buffer)

通过 OpenOCD DAP 接口直接自 STM32F103ZET6 RAM（`0x20000d50`）读出的 15 行实际显存文本：

```text
Row 00: [KERNEL]
Row 01: [Tick: 100Hz  TIM2: 1kHz]
Row 02: [TASK STATUS]
Row 03: [KEY         BLOCK (P5)]
Row 04: [Controller  BLOCK (P4)]
Row 05: [LCD         RUN (P2)]
Row 06: [Monitor     BLOCK (P1)]
Row 07: [MUTEX DIAGNOSTICS]
Row 08: [Status: NO DEADLOCK]
Row 09: [Owner: --]
Row 10: [Waiter: --]
Row 11: [Cycle: --]
Row 12: [PI: NORMAL]
Row 13: [Trace: 69500 Ovr:69372]
Row 14: [Errors: 0  Ref: 283]
```

---

## 四、证据文件索引

- 烧录日志: `evidence/stage9_2_h1/flash_programming.log`
- 固件校验: `evidence/stage9_2_h1/firmware_verification.json`
- 回读比对: `evidence/stage9_2_h1/readback_verification.json`
- 单屏界面测试: `evidence/stage9_2_h1/single_screen_test.md`
- 硬件死锁诊断: `evidence/stage9_2_h1/mutex_diag_hardware.md`
- 调度追踪运行时: `evidence/stage9_2_h1/trace_runtime.md`
- 60秒稳定性测试: `evidence/stage9_2_h1/stability_60s.md`
- 硬件复位测试: `evidence/stage9_2_h1/reset_test.md`
- 遗留问题与局限: `evidence/stage9_2_h1/remaining_issues.md`
