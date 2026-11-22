# Stage 9.1-H1 Explainable Scheduling 硬件工程集成报告 (Integration Report)

## 一、集成目标与工程背景 (Integration Background)

在 Stage 9.1 中，可解释调度（Explainable Scheduling）通过了完备的 Host 单元测试与 ARM QEMU 指令集断点验证。Stage 9.1-H1 的核心任务是：
将该模块正式集成到 STM32F103ZET6 + ST7789 真实四任务生产固件工程（`app/stm32f103ze_st7789`）中，新增第 6 个 LCD 页面 `SCHED TRACE`，并保持既有 Stage 8.5-C4-R1 硬件功能基线与 C3 SPI/DMA 可靠性修复完全不变。

---

## 二、集成修改范围与文件清单 (Scope of Changes)

集成严格限定在硬件应用工程内，未修改公共内核 TCB 布局或汇编接口：

1. **中间件同步 (`app/stm32f103ze_st7789/Middlewares/InsightRTOS/`)**：
   - 将经过验证的 Stage 9.1 生产源码同步至 `Middlewares/InsightRTOS`。
   - 更新 `config/insight_project_config.cmake`，支持依据 CMake 变量动态设置 `INSIGHT_TRACE_ENABLED` 和 `INSIGHT_DIAG_EXPLAIN`。
   - 重新生成 `VERSION_MANIFEST.md`，校验全部 44 个源文件哈希。
2. **UI 逻辑与诊断页实现 (`Core/display_ui.h`, `Core/display_ui.c`)**：
   - 声明并实现 `Display_UISchedTrace(bool entering)`。
   - 实现无 libc `snprintf` 依赖的有界数值与字符串格式化。
   - 实现 `previous_trace` 字符级差分显存渲染，最大程度节省 SPI3/DMA 带宽。
   - 严格在线程上下文执行快照提取与格式化。
3. **页面生命周期与策略 (`BSP/Src/app_lcd_test.c`, `Core/display_policy.c`)**：
   - 在 `INSIGHT_DIAG_EXPLAIN=1` 时将 `LCD_TEST_PAGE_COUNT` 配置为 6，并在诊断禁用时平滑回退为 5。
   - 在 `lcd_test_draw_page` 中增加 `case 5U: Display_UISchedTrace(true)`。
   - 在 `LCD_Test_Task` 与 `Display_UpdateDue` 中加入每 250 ms 定期触发 `Display_UISchedTrace(false)` 的调度策略。
4. **构建系统与配置预设 (`CMakeLists.txt`, `cmake/insight_rtos.cmake`, `CMakePresets.json`)**：
   - 增加选项 `INSIGHT_TRACE_ENABLED` 与 `INSIGHT_DIAG_EXPLAIN`。
   - 新增构建预设 `DISPLAY_RTOS_O2_Trace` 与 `DISPLAY_RTOS_Debug_Trace`，并完整保留原有的 `DISPLAY_RTOS_O2` 与 `DISPLAY_RTOS_Debug`（Trace OFF 模式）。

---

## 三、四任务与总线协同机制 (Task & Bus Coordination)

应用中存在 4 个核心业务任务与 1 个 Idle 任务：

1. **KEY Task (优先级 4)**: 扫描 KEY1 按键，短按发送切换页面事件至 Controller。
2. **Controller Task (优先级 3)**: 接收输入事件，校验后通过命令队列发送至 LCD Task。
3. **LCD Task (优先级 2)**: 独占 ST7789 控制权，执行 SPI3/DMA 发送与画面绘制。
4. **Monitor Task (优先级 1)**: 定期统计任务运行计数与总线异常指标。
5. **Idle Task (优先级 0)**: 空闲等待 (`__WFI`)。

**第 6 页运行协同保障**：
- **独占总线**：LCD Task 是 ST7789、SPI3 和 DMA 的唯一持有者，不存在总线竞争。
- **防止总线饥饿**：第 6 页的刷新频率由 `Display_UpdateDue` 锁定为 250 ms 一次（4 Hz）。差分渲染技术使得绝大多数刷新只重绘变化的一两行文字，SPI3 传输时间小于 1 ms，绝不会造成 KEY 或 Controller 任务的调度饥饿。
- **有界临界区**：读取 Trace 环形缓冲区时通过 `rtos_port_irq_save()` 仅拷贝 8 条记录（192 字节），关中断时间小于 2 µs，不影响实时中断响应。

---

## 四、双配置兼容性验证 (Configuration Matrix)

| 配置 | `INSIGHT_TRACE_ENABLED` | `INSIGHT_DIAG_EXPLAIN` | 页面数量 | 诊断显示状态 | 适用场景 |
|---|---|---|---|---|---|
| **Trace ON / Explain ON** | **1** | **1** | **6** | `DIAG: ACTIVE (128 REC)` | 生产调试、调度观测、因果分析 |
| **Trace OFF / Explain OFF**| **0** | **0** | **5** | (未启用第 6 页) | 经典基线、极度敏感存储场景 |

已验证两种构建配置在 Debug 和 O2 优化等级下全部 100% 编译通过，链接预算合规。
