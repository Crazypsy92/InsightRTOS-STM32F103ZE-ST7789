# InsightRTOS — STM32F103ZET6 + ST7789 全功能 RTOS 诊断与驱动平台

> **当前版本：v0.9.2 (Stage 9.2: Explainable Scheduling + Mutex Deadlock Diagnostics)**  
> 运行平台：真实硬件 STM32F103ZET6 + ST7789 (1.54" 240×240 IPS TFT)  
> 固件状态：四任务生产应用已实机部署并固化；具备**可解释调度**与 **Mutex 死锁诊断**双核心能力；零动态内存分配，严格 72 字节 TCB ABI 与 384 字节诊断工作区预算。

---

## 一、系统特性概览

1. **工业级 SPI/DMA 总线抽象**：
   - 具备事务排他占有、嵌套重入深度管理、状态机守卫与故障自恢复机制。
   - LL 后端支持 DMA2 自动分块（绕开 16 位 CNDTR ≤ 65535 字节硬件限制）、基于速率与有效载荷的有界动态超时预算，杜绝长块传输误报超时。
2. **多任务 RTOS 协同显示体系**：
   - **KEY Task (优先级 4)**: 扫描 KEY1 按键，事件防抖与状态机分发。
   - **Controller Task (优先级 3)**: 状态逻辑控制器与消息队列转发。
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
| 应用层 (Application)                                                    |
|  - Core/display_app.c       : 四任务应用入口、通信队列与任务初始化       |
|  - BSP/Src/app_lcd_test.c   : 裸机 4 页面回退演示程序                   |
+-------------------------------------------------------------------------+
| 界面与策略层 (UI & Policy)                                              |
|  - Core/display_ui.c        : 15 行综合看板、字符级差分渲染 (零频闪)    |
|  - Core/display_pages.c     : 页面驱动接口与刷新调用                    |
|  - Core/display_policy.c    : 250 ms 周期刷新与总线协调策略             |
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
+-------------------------------------------------------------------------+
| 总线抽象与底层硬件 (Bus & LL)                                           |
|  - BSP/Inc/spi_bus.h        : MCU 无关 SPI 总线所有权契约与状态机       |
|  - BSP/Src/spi_stm32_ll.c   : SPI3 + DMA2 自动分块传输与动态超时预算     |
|  - BSP/Src/bsp_spi.c        : 板级时钟、引脚映射与复用配置               |
+-------------------------------------------------------------------------+
```

---

## 四、240×240 综合状态看板

ST7789 屏幕被划分为 15 行等宽点阵区域，显示系统运行与诊断全貌：

```text
+----------------------------------------+
| 00: InsightRTOS SYSTEM RUNNING         | [青色] 运行状态正常
| 01: ---------------------------------- |
| 02: KERNEL: 100Hz TIM2:1kHz TICK:12480 | [白色] 系统心跳与时基
| 03: ---------------------------------- |
| 04: TASK STATUS:                       | [黄色] 任务运行状态
| 05:  KEY : RUN  PRIO:4(4) RUNS: 1248   |
| 06:  CTRL: BLK  PRIO:3(3) RUNS: 1248   |
| 07:  LCD : RUN  PRIO:2(2) RUNS:  499   |
| 08:  MON : BLK  PRIO:1(1) RUNS:  124   |
| 09: ---------------------------------- |
| 10: MUTEX DIAGNOSTICS:                 | [黄色] 死锁诊断指标
| 11:  STATUS: ACTIVE (NO DEADLOCK)      | [绿色/红色] 死锁告警
| 12:  CYCLE : NONE      PI BOOST: 0     | 环路任务序列与 PI 计数
| 13:  NODES : 4 (0 EDGES)               | 依赖图节点与出边数
| 14:  TRACE : 128 REC   ERRORS: 0       | 调度追踪记录与错误数
+----------------------------------------+
```

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
