# InsightRTOS — STM32F103ZET6 + ST7789 全功能 RTOS 诊断与驱动平台

> **当前版本：v0.9.3-A2 (Stage 9.3-A2: Real-Time & Reliability Baseline Verification)**  
> 运行平台：真实硬件 STM32F103ZET6 + ST7789 (1.54" 240×240 IPS TFT)  
> 固件状态：具备**可解释调度**、**Mutex 死锁诊断**与**实时性观测（CPU 运行周期、唤醒延迟与时钟可信度）**三大核心诊断能力；实现 8 槽历史下的孤儿 ACK 准确结算与零丢失契约；通过 60 秒常态基线及 5 分钟受控高频压力测试（0 传输错误，0 HardFault）。
> 阶段验收：[Stage 9.3-A2 综合验收报告](evidence/stage9_3_a2/acceptance.md) · [时序可信度与安全性契约](evidence/stage9_3_a2/timing_safety.md) · [测试清单与压力日志](evidence/stage9_3_a2/test_results.md)

---

> **实时性与可靠性基线验收演进：** 依据 [Issue #6](https://github.com/Crazypsy92/InsightRTOS-STM32F103ZE-ST7789/issues/6)，全面加固测试基础设施真实性（移除 QEMU 假 PASS，重构全动态符号采样），在 Monitor Task 导出实时只读诊断快照；扩展 `App/Src/app_events.c` 生命周期计数，保证环形槽覆盖时的孤儿 ACK 仍准确结算；通过 STM32 实机 60 秒基线及 5 分钟（300 秒 / 10 events/s）连续高频压力测试。

> **实时性诊断核心机制：** 遵循诊断体系架构规范，新增 `Middlewares/InsightRTOS/include/rtos_diag_rt.h` 与 `kernel/diag_rt.c`，引入 `INSIGHT_DIAG_RT` 开关。实现 64 位单调周期时钟扩展、59 秒多回绕不确定性拦截、任务驻留周期累计（含在途补偿）、唤醒响应延迟测量（纳秒与节拍双维度），以及任务终止数据冻结与代数复用失效校验。执行任务闭环于 [Issue #5](https://github.com/Crazypsy92/InsightRTOS-STM32F103ZE-ST7789/issues/5)。

> **应用层模块化演进：** 参照 [STM32F103-FreeRTOS-MultiButton](https://github.com/Crazypsy92/STM32F103-FreeRTOS-MultiButton) 的清晰任务模板与配置分离原则，已将应用层完整重构至 `App/` 独立目录，实现应用配置、IPC 通信、事件跟踪与任务入口的高内聚低耦合解耦；统一 `tests/` 与 `scripts/test/` 自动化测试脚本资产体系。详细架构规范见 [模块化架构方案](docs/architecture/rtos_app_modularization.md)，执行任务已闭环于 [Issue #4](https://github.com/Crazypsy92/InsightRTOS-STM32F103ZE-ST7789/issues/4)。

> **按需开启远程自动接力：** 外出时可让 ChatGPT 审查 GitHub 验收报告并发布下一轮 Issue，由 Antigravity 后台领取；平时两端均保持暂停。完整开关流程、安全边界与 Antigravity 初始化指令见 [远程任务接力操作规程](docs/remote_task_relay.md)。

---

## 一、系统特性概览

1. **工业级 SPI/DMA 总线抽象**：
   - 具备事务排他占有、嵌套重入深度管理、状态机守卫与故障自恢复机制。
   - LL 后端支持 DMA2 自动分块（绕开 16 位 CNDTR ≤ 65535 字节硬件限制）、基于速率与有效载荷的有界动态超时预算，杜绝长块传输误报超时。
2. **多任务 RTOS 协同显示体系**：
   - **KEY Task (优先级 5)**: 扫描 KEY1 按键，事件防抖与状态机分发。
   - **Controller Task (优先级 4)**: 状态逻辑控制器与消息队列转发。
   - **LCD Task (优先级 2)**: 独占 SPI3 和 ST7789 控制权，执行微秒级差分刷新。
   - **Monitor Task (优先级 1)**: 系统健康度、运行计数与总线异常监控。
   - **Idle Task (优先级 0)**: 低功耗等待 (`__WFI`)。
3. **可解释调度 (Explainable Scheduling)**：
   - 128 条记录环形缓冲区、64 位序列号、防覆盖过期游标。
   - 实时解析任务切换因果：抢占（HIGHER_PREEMPT）、主动让出（YIELD）、时间片轮转（SLICE_ROTATION）、IPC/阻塞唤醒（BLOCK_OR_WAKE_OTHER）与空闲迁移（IDLE_TRANSITION）。
4. **Mutex 资源依赖与死锁诊断 (Deadlock Diagnostics)**：
   - 微秒级短临界区快照（关中断 < 3 µs），捕获活动任务、持有与等待依赖图。
   - 在任务线程上下文执行有界三色 DFS 环路搜索，支持 2 任务 ABBA、3 任务循环等待及多等待者扇入拓扑检测。
   - 规范化旋转环路输出，识别带超时边界的死锁路径；结合任务插槽世代号（Slot Generation）防范陈旧失效快照。
5. **240×240 单屏综合状态显示界面**：
   - 淘汰旧有多页面轮播与开机延迟，重构为单一固定的 15 行系统综合看板。
   - 字符级差分渲染：整屏无变动时总线开销为 0 µs，局部变更行重绘仅耗时约 0.25 ms，杜绝画面频闪与总线饥饿。

---

## 二、硬件接线与引脚分配

| 功能引脚 | STM32F103ZET6 引脚 | 硬件复用与配置说明 |
| --- | --- | --- |
| **SPI3_SCK** | **PB3** | 上电需调用 `LL_GPIO_AF_Remap_SWJ_NOJTAG()` 关闭 JTAG 复用 |
| **SPI3_MISO** | **PB4** | 需关闭 JTAG 复用；ST7789 为单向屏幕，可不连接 |
| **SPI3_MOSI** | **PB5** | 需关闭 JTAG 复用；连接 ST7789 SDA 数据引脚 |
| **LCD_CS** | **PD11** | 软件片选控制，低电平有效 |
| **LCD_DC** | **PD12** | 数据/命令控制（高电平数据，低电平命令） |
| **LCD_BL** | **PD13** | 背光控制，高电平点亮（支持 PWM 调光） |
| **LCD_RST** | — | 驱动内建软件软复位命令序列，无需占用外部 GPIO |
| **KEY1** | **PA15** | 输入上拉，按下为低电平（需 SWJ_NOJTAG 解除复用） |
| **LED** | **PC13** | 板载状态指示灯，低电平点亮 |

> **时钟与 DMA 配置**：
> - SPI3 挂载于 APB1 总线（36 MHz），默认通信波特率 18 MHz，模式 0（CPOL=0, CPHA=0）。
> - 发送通道：SPI3_TX → DMA2 通道 2；接收通道：SPI3_RX → DMA2 通道 1。
> - 独立硬件定时器 TIM2（1 kHz / 1 ms）作为系统高精度时间基准，SysTick 专属于 RTOS 内核调度。

---

## 三、软件架构分层 (Software Architecture)

```text
+-------------------------------------------------------------------------+
| 应用层 (App/ - 独立业务模块化)                                           |
|  - App/Inc/app_config.h     : 四任务优先级、栈深度、队列与事件容量配置    |
|  - App/Inc/app_ipc.h        : 5 条队列消息结构声明与受控 IPC API 接口    |
|  - App/Src/app_ipc.c        : 静态队列缓冲区定义与入队出队/深度快照实现  |
|  - App/Inc/app_events.h     : 唯一单调事件 ID 与 8 槽位环形历史追踪声明  |
|  - App/Src/app_events.c     : 事件状态机跃迁、闭环 ACK 与重试/丢弃统计   |
|  - App/Inc/app_tasks.h      : 统一任务启动接口与任务句柄声明             |
|  - App/Src/app_tasks.c      : KEY/CTRL/LCD/MON 任务模板与 app_tasks_start|
+-------------------------------------------------------------------------+
| 兼容与界面策略层 (Core/)                                                |
|  - Core/display_app.c       : 向后兼容桥接层 (display_app_start)         |
|  - Core/display_ui.c        : 15 行综合看板、字符级差分渲染 (零频闪)     |
|  - Core/display_pages.c     : 页面驱动接口与刷新调用                     |
|  - Core/display_policy.c    : 250 ms 周期刷新与总线协调策略              |
|  - Core/rtos_smoke_app.c    : 独立 RTOS 极简冒烟测试 Profile             |
+-------------------------------------------------------------------------+
| 中间件内核层 (Middlewares/InsightRTOS)                                  |
|  - kernel/scheduler.c      : 抢占式调度器、上下文切换                   |
|  - kernel/deadlock.c       : 资源依赖快照提取与三色 DFS 死锁环路分析    |
|  - kernel/trace.c          : 128 条记录环形缓冲与因果可解释调度日志     |
|  - kernel/mutex.c          : 优先级继承 (PI) 互斥量                     |
|  - kernel/task.c           : 任务生命周期与插槽世代号 (Slot Gen) 管理   |
+-------------------------------------------------------------------------+
| 设备协议与适配层 (Drivers & Ports)                                      |
|  - BSP/Src/st7789.c         : ST7789 寄存器配置、画点、矩形、字体渲染    |
|  - BSP/Src/st7789_port.c    : 片选、背光、延时板级适配                   |
|  - BSP/Src/key_port.c       : 按键防抖与 multi_button 移植               |
|  - BSP/Src/lcd_cn16.c       : 16x16 中文点阵字模包与中英混排引擎         |
+-------------------------------------------------------------------------+
| 总线抽象与底层硬件 (Bus & LL)                                           |
|  - BSP/Inc/spi_bus.h        : MCU 无关 SPI 总线所有权契约与状态机       |
|  - BSP/Src/spi_stm32_ll.c   : SPI3 + DMA2 自动分块传输与动态超时预算     |
|  - BSP/Src/bsp_spi.c        : 板级时钟、引脚映射与复用配置               |
+-------------------------------------------------------------------------+
| 统一测试资产体系 (tests/ & scripts/test/)                               |
|  - tests/host/             : 面向生产源码的单元/集成测试 (test_app_modular)|
|  - tests/hardware/         : 受控固件烧录核验与 60s 稳定性采样脚本      |
|  - tests/fixtures/         : 内存布局、Option Bytes 与系统配置期望文件  |
|  - scripts/test/run_host.sh: 编译并执行 Host 单元测试套件                |
|  - scripts/test/run_regression.sh: 全量回归测试脚本 (含内核 360 ctest)   |
|  - scripts/test/run_hardware.sh  : 实机自动化烧录与 60 秒稳定性采样     |
+-------------------------------------------------------------------------+
```

---

## 四、240×240 中文单屏看板与事件链路 (Stage 9.2-H3)

ST7789 屏幕被划分为 15 行等宽点阵区域（每行 16 像素高），采用 142 字模裁剪中文与 ASCII 混排显示：

```text
+----------------------------------------+
| Row 00: InsightRTOS       正常         | [青色] 系统全局状态指示
| Row 01: 按键: 单击        LED: 关      | [白色] 最近按键事件与 PC13 输出极性
| Row 02: 链路: 识别>KEY>CTRL>LCD        | [绿色/灰色] 事件处理链路 (~2s 非阻塞留痕)
| Row 03: 任务      状态     优先级      | [黄色] 任务状态表头
| Row 04: > KEY     阻塞     P5/5        | [黄色高亮] 单击循环选中的任务
| Row 05:   CTRL    阻塞     P4/4        | [白色] Controller 任务状态与优先级
| Row 06:   LCD     就绪     P2/2        | [白色] LCD 任务状态与优先级
| Row 07:   MON     就绪     P1/1        | [白色] Monitor 任务状态与优先级
| Row 08: 选中: KEY 接收按键事件         | [青色] 选中任务职责描述
| Row 09: 已处理:3  最近:1.2s            | [白色] 累计处理事件数与完成耗时
| Row 10: 死锁:无  错误:0                | [绿色/红色] 依赖图死锁检测与系统错误
| Row 11: 事件: 成功:3 失败:0            | [白色] 事件结果闭环统计 (成功/失败/重试)
| Row 12: 详情: 运行次数: 42             | [黄色] 双击切换任务详情显示 (或黑行)
| Row 13: 单击选  双击详情  长按LED      | [灰色] 用户交互提示
| Row 14:                                | [黑色] 底部防贴边隔离行
+----------------------------------------+
```

### 按键交互与事件链路
- **单按键 (KEY1 / PA15)**：
  - **单击**：依次循环选中 4 个任务（KEY → CTRL → LCD → MON → KEY），Row 08 动态显示该任务的中文字责描述。
  - **双击**：开启 / 关闭 Row 12 任务详情模式（显示该任务的运行次数）。
  - **长按**：由 Controller 任务翻转板载 PC13 LED 输出状态，Row 01 同步显示 `LED: 开` 或 `LED: 关`。
- **事件链路流水线与短时留痕**：
  - 链路全过程：`TIM2 1ms 采样` → `MultiButton 识别 (ID分配)` → `Display_KeyFromISR` → `raw_keys` → `KEY Task` → `controller_keys` → `Controller Task` → `lcd_commands` → `LCD Task` → `lcd_acks (ACK确认)`。
  - 处理过程中仅显示实际发生的阶段（如 `识别>KEY`）。
  - 渲染完成后在非阻塞 UI 采样中维持 **2000 ms 短时留痕**（显示 `识别>KEY>CTRL>LCD`），超时后平滑退回 `待机`。
  - KEY 任务在完成 IPC 转发后立即回到 `BLOCKED` 等待队列，属完全正常的稳态行为。

---

## 五、构建配置与预设矩阵 (CMake Presets)

本项目支持通过 CMake Presets 快速切换编译目标：

| 预设名称 (Preset) | 镜像文件目标 | 优化与定义 | 适用场景 |
|---|---|---|---|
| **`DISPLAY_RTOS_O2_Trace`** | `insightrtos_display.elf/.bin/.hex` | `-O2`, Trace=1, Diag=1 | **生产推荐固件（包含完整诊断能力）** |
| `DISPLAY_RTOS_Debug_Trace`| `insightrtos_display.elf/.bin/.hex` | `-g -O0`, Trace=1, Diag=1 | 调试版（包含完整诊断与调试符号） |
| `DISPLAY_RTOS_O2` | `insightrtos_display.elf/.bin/.hex` | `-O2`, Trace=0, Diag=0 | 生产基线版（关闭诊断与日志记录） |
| `DISPLAY_RTOS_Debug` | `insightrtos_display.elf/.bin/.hex` | `-g -O0`, Trace=0, Diag=0 | 调试基线版（关闭诊断） |
| `RTOS_O2` / `RTOS_Debug` | `insightrtos_rtos.elf/.bin/.hex` | `-O2` / `-g` | RTOS 双任务极简冒烟测试镜像 |
| `O2` / `Debug` / `Release`| `insightrtos.elf/.bin/.hex` | `-O2` / `-g` | 裸机 LCD 4 页面测试演示镜像 |

### 编译命令示例
```bash
# 配置并编译生产全功能诊断固件 (O2 Trace)
cmake --preset DISPLAY_RTOS_O2_Trace
cmake --build --preset DISPLAY_RTOS_O2_Trace

# 或编译裸机演示固件
cmake --preset O2
cmake --build --preset O2
```

构建完成后，固件二进制文件自动存放于 `build/<PresetName>/` 目录下，并自动导出 `.bin` 与 `.hex`。

---

## 六、硬件烧录与部署

使用 ST-Link 与 OpenOCD（或 pyocd / J-Link）将固件烧录至 `0x08000000`：

```bash
openocd -f interface/stlink-v2.cfg -f target/stm32f1x.cfg \
  -c "program build/DISPLAY_RTOS_O2_Trace/insightrtos_display.hex verify reset exit"
```

> **烧录安全说明**：
> 生产固件体积仅约 43.6 KiB（占用第 0 ~ 21 页 Flash），擦写操作严格限定在程序占用范围内，不影响芯片 Option Bytes 与后续扇区数据。

---

## 七、设计与验收文档索引 (Documentation & Evidence)

本工程包含完整的架构设计基准文档与全流程实机验收证据：

### 1. 核心诊断架构设计 (`docs/diagnostics_design/`)
- [系统总体诊断架构](docs/diagnostics_design/architecture.md)
- [可解释调度详细设计](docs/diagnostics_design/explainable_scheduler.md)
- [Mutex 资源依赖与死锁诊断设计](docs/diagnostics_design/deadlock_diagnostics.md)
- [内核事件模式与 ABI 契约](docs/diagnostics_design/event_schema.md)
- [公共诊断 API 规范](docs/diagnostics_design/public_api.md)
- [内存与耗时预算约束](docs/diagnostics_design/resource_budget.md)
- [诊断验证测试矩阵](docs/diagnostics_design/test_matrix.md)

### 2. 阶段验收与实机部署证据 (`evidence/`)
- [**Stage 9.2-H5: 参考 FreeRTOS 任务模板重构应用模块与测试目录综合验收报告**](evidence/stage9_2_h5/acceptance.md)
  - [软件测试与全量回归报告](evidence/stage9_2_h5/test_results.md)
  - [实机部署与 60 秒硬件稳定性验收报告](evidence/stage9_2_h5/hardware.md)
  - [固件与构建清单](evidence/stage9_2_h5/manifest.json)
  - [Flash 烧录原始日志](evidence/stage9_2_h5/logs/programming.log)
  - [逐字节回读与校验记录](evidence/stage9_2_h5/logs/readback_verification.json)
  - [60 秒实机稳定性连续采样数据](evidence/stage9_2_h5/logs/stability_60s.json)
  - [文件校验哈希汇总](evidence/stage9_2_h5/SHA256SUMS)
- [**Stage 9.2-H4: 精简中文单屏与事件 ACK 关联修正综合验收报告**](evidence/stage9_2_h4/acceptance.md)
  - [16×16 中文点阵字体集成与渲染架构报告](evidence/stage9_2_h2/chinese_font_report.md)
  - [字模点阵校验与 118 汉字覆盖报告](evidence/stage9_2_h2/font_glyph_verification.md)
  - [MultiButton 事件扩展与时序互斥测试报告](evidence/stage9_2_h2/multibutton_event_tests.md)
  - [中文单屏综合验收看板排版规范](evidence/stage9_2_h2/ui_layout.md)
  - [Host 软件测试矩阵与自动化验证报告](evidence/stage9_2_h2/host_test_results.md)
  - [60 秒持续运行稳定性验收报告](evidence/stage9_2_h2/hardware_60s.md)
  - [固件受控烧录与读回校验记录](evidence/stage9_2_h2/programming.log)
  - [遗留问题与已知边界报告](evidence/stage9_2_h2/remaining_issues.md)
- [**Stage 9.2-H1: 单屏诊断固件实机部署与 60s 稳定性验收报告**](evidence/stage9_2_h1/acceptance.md)
  - [单屏综合看板实机测试与显存核验报告](evidence/stage9_2_h1/single_screen_test.md)
  - [死锁诊断硬件实机验证报告](evidence/stage9_2_h1/mutex_diag_hardware.md)
  - [调度追踪与可解释性运行时报告](evidence/stage9_2_h1/trace_runtime.md)
  - [60 秒持续运行稳定性验收报告](evidence/stage9_2_h1/stability_60s.md)
  - [硬件复位与自愈测试报告](evidence/stage9_2_h1/reset_test.md)
  - [固件精确烧录与回读校验记录](evidence/stage9_2_h1/flash_programming.log)
- [**Stage 9.2: Mutex 资源依赖与死锁诊断验收报告**](evidence/stage9_2/acceptance.md)
  - [Mutex 依赖关系快照机制设计](evidence/stage9_2/mutex_dependency_design.md)
  - [三色 DFS 死锁环路检测算法分析](evidence/stage9_2/deadlock_detection.md)
  - [单元测试与 QEMU ARM 场景测试报告](evidence/stage9_2/test_results.md)
  - [内存预算与栈消耗审计报告](evidence/stage9_2/resource_report.md)
  - [综合看板界面验收报告](evidence/stage9_2/ui_acceptance.md)
- [**Stage 9.1-H2: 六页面调度追踪固件实机部署验收报告**](evidence/stage9_1_h2/acceptance.md)
- [**Stage 9.1-H1: 可解释调度硬件集成报告**](evidence/stage9_1_h1/acceptance.md)
- [**Stage 9.1: 可解释调度功能软件验证报告**](evidence/stage9_1/acceptance.md)
- [**Stage 8.5-C4-R1: 五页面硬件验收与基线固化报告**](evidence/stage8_5_c4_r1/acceptance.md)
- [**Stage 8.5-C3: SPI/DMA 可靠性修复与边界测试报告**](evidence/stage8_5_c3/acceptance.md)

---

## 八、开源许可证

本项目核心调度器与总线驱动遵循开源规范，第三方 HAL/CMSIS 驱动版权归 STMicroelectronics 及其授权方所有。详情参见各源码文件头声明。
