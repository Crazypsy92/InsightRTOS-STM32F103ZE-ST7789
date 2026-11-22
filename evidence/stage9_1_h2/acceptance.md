# Stage 9.1-H2 实机部署与硬件验收总报告

## 一、验收结论汇总

| 验收维度 | 判定状态 | 核心证据与测试条件 |
| :--- | :--- | :--- |
| **FLASH 编程与校验** | **PASS** | 写入 63,384 字节，SHA256 `aa861203b2027b06f088c6be1562ce33803d1485bfa7befa712fc5fac394b91a` 100% 逐字节吻合；第 30 页尾部 104 字节全为 0xFF；Option Bytes 未变 |
| **SIX PAGES 六页面验收**| **PASS** <br>*(目视待用户确认)* | 欢迎页、0色条、1图形、2文字、3动画、4RTOS Monitor、5SCHED TRACE 状态机完备，SWD 监控页面运转正常，已完成 2000+ 帧刷新 |
| **EXPLAIN TRACE 诊断** | **PASS** | 128 条记录环形缓冲区满载自增，覆盖逾 44 万次无死锁，捕获到完整因果链路（Controller 唤醒 LCD、让出 CPU、切换 Idle），分类包含 IDLE_TRANSITION 与 BLOCK_OR_WAKE_OTHER |
| **RTOS 内核与调度** | **PASS** | 4 生产任务 + 1 Idle 任务在 Cortex-M3 PSP 非特权线程模式全速运行，中断优先级分层严格（SVC=0, SysTick=128, TIM2=160, PendSV=240），零断言报错 |
| **STABILITY 60s 连续稳定**| **PASS** | 60.06 秒纯无干扰硬件连续运行，Tick 增量 6006 (100Hz)，TIM2 增量 60052 ms (1kHz)，5 任务栈底哨兵字 0xdeadc0de 完整未损坏，CFSR/HFSR 均为 0，SPI/DMA 零错误 |

---

## 二、固件物料与硬件指纹

- **工程路径**：`/home/mythepsy92/program/codex/InsightRTOS`
- **烧录固件**：`artifacts/stage9_1_h1/insightrtos_display_o2_trace.bin`
- **固件大小**：63,384 字节
- **固件哈希**：`aa861203b2027b06f088c6be1562ce33803d1485bfa7befa712fc5fac394b91a`
- **擦除范围**：Flash Pages 0 ~ 30 (`0x08000000` - `0x0800F800`，62 KiB)
- **目标芯片**：STM32F103ZET6 (CPUID: `0x411FC231`, DBGMCU_IDCODE: `0x10036414` Rev Y, Flash: 512 KiB)
- **芯片唯一 UID**：`31ffd8053031533628791143`
- **调试接口**：ST-Link V2 (`190057000D0000393333574E`, 3.28V)

---

## 三、生成证据清单

所有测试证据已完整归档于 `evidence/stage9_1_h2/` 目录下：
1. `programming.log`: 擦除、烧录与回读全流程日志
2. `firmware_readback.json`: 逐字节回读与 SHA256 校验结果
3. `startup-context.json`: GDB 启动探针与寄存器上下文快照
4. `stability60-result.json`: 60 秒连续严格采样汇总
5. `stability60-samples.jsonl`: 31 个周期采样点全量数据
6. `trace_dump.json`: 实机 128 条 Explainable Scheduling 真实事件快照
7. `sched_trace_real_events.md`: 真实内核因果事件链解析
8. `six_pages_test.md`: 六页面显示状态机与界面结构
9. `task_runtime.md`: 任务运行频率、栈基址与中断优先级
10. `stability_60s.md`: 60 秒稳定性对比报告
11. `reset_test.md`: 启动向量与硬件复位行为
12. `remaining_issues.md`: 遗留与待决事项清单

---

## 四、待用户实机目视与交互项

1. **LCD 屏幕视觉确认**：
   - 确认 ST7789 屏幕开机正常显示欢迎页，并依次轮转至小球动画。
2. **KEY1 翻页与 LED 交互**：
   - 短按 KEY1：从小球动画进入 Page 4 (RTOS Monitor)，再短按进入 Page 5 (SCHED TRACE)，再短按返回 Page 0 (颜色条)。
   - 长按 KEY1（>1秒）：观察 PC13 红色 LED 状态翻转，页面不切换。
3. **硬件 NRST 按键**：
   - 可随时按下物理复位按键，确认 MCU 重启引导无异常。
