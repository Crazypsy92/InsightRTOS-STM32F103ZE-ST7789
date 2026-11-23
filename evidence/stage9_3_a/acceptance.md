# InsightRTOS Stage 9.3-A: 实时性诊断核心基础设施验收报告 (Acceptance Report)

## 一、阶段概述与交付目标

本报告对应 GitHub Issue [#5](https://github.com/Crazypsy92/InsightRTOS-STM32F103ZE-ST7789/issues/5)：**`[TASK] Stage 9.3-A 实时性诊断：时钟能力、CPU 运行周期与唤醒延迟`**。

在基线提交 `ddac72ea991b7975acc33410f53446cc864a2825`（Stage 9.2-H5）基础上，本阶段完成了 InsightRTOS 第三大核心诊断能力（Real-Time Diagnostics）的第一轮工程落地，实现了高精度、可验证的实时性观测基础设施。

### 本阶段回答的三个核心问题：
1. **每个任务在测量窗口内实际被计量的 Thread CPU 执行周期是多少？**
   - 通过在调度器切出/切入点 `rtos_sched_switch()` 记录 64 位单调 DWT 周期差，准确累加各任务的 `charged_cycles`；
   - 处于 `RTOS_TASK_BLOCKED`（等待信号量、互斥锁、队列、延时）期间的时间严格隔离，不摊派 CPU 周期；
   - 空闲时间独立计入 `s_idle_record`，不计入业务任务；
   - 支持动态在途查询（In-Flight Query），对当前正处于 `RUNNING` 的任务动态补偿当前段驻留周期，保证单调递增；
   - 明确遵循中立约定：ARM Cortex-M3 无每个 ISR 的硬件周期扣除，快照声明为 **"Thread Occupancy Cycles (含内嵌 IRQ 开销)"**。
2. **从任务进入 READY 到首次被调度运行的响应延迟是多少？**
   - 在任务发布、阻塞唤醒以及抢占让出（READY 点）记录 `ready_cycles` 与 `ready_tick`；
   - 在首派发（RUNNING 点）计算 `latency_cycles`（72 MHz 纳秒级）与 `latency_ticks`（100 Hz 节拍级），并跟踪历史极值 `max_ready_latency_cycles` / `max_ready_latency_ticks`。
3. **测量时间源是否可信？**
   - 区分 `RTOS_DIAG_CLOCK_UNAVAILABLE (0)`、`RTOS_DIAG_CLOCK_TICK_ONLY (1)` 与 `RTOS_DIAG_CLOCK_CYCLE_ACCURATE (2)`；
   - 通过 10 ms SysTick 心跳采样实现 32 位 DWT CYCCNT 到 64 位单调时间的可靠扩展（比率 5965:1）；
   - 设置 59.0 秒（5900 ticks）多次回绕不确定性硬边界，遇长暂停或丢时钟自动置位 `RTOS_DIAG_RT_CLOCK_UNCERTAIN`，绝不报告看似合理实则错误的伪造数据。

---

## 二、架构设计与资源预算核验

### 1. 独立开关与零开销契约 (`INSIGHT_DIAG_RT`)
- 编译开关：`INSIGHT_DIAG_RT`（0 或 1，默认 0），与 `INSIGHT_TRACE_ENABLED`、`INSIGHT_DIAG_EXPLAIN`、`INSIGHT_DIAG_DL` 完全正交；
- **RT 关闭时**：所有 Hook 宏展开为 `((void)0)`，不分配任何 side table，无运行时指令开销，不增加任何 RAM。

### 2. 静态侧表与 1024 B 内存预算核验
- 不修改 TCB 结构体及其 72 B ABI 冻结契约；
- 不修改 SVC / PendSV 汇编及上下文切换 ABI；
- 0 堆内存分配（`heap = 0`）；
- 静态分配实测（通过 `arm-none-eabi-nm -S --size-sort` 提取）：
  - `s_rt_tasks[8]`：512 字节（8 × 64 字节）
  - `s_rt_global`：32 字节
  - `s_idle_record`：24 字节
  - **总静态 SRAM 占用：568 字节** $\le 1024$ 字节预算（余量 456 字节，占用率仅 55.5%）。

### 3. 栈调用深度预算 (`.su` 反汇编实测)
- `rtos_diag_rt_snapshot`：64 字节（预算 $\le 96$ B）
- `rtos_diag_rt_switch_hook`：32 字节（预算 $\le 64$ B）
- `rtos_diag_rt_task_ready_hook`：16 字节
- `rtos_diag_rt_on_tick`：8 字节

---

## 三、测试矩阵与验收结果

### 1. 自动化测试结果

| 测试套件 | 验证范围 | 结果 | 耗时 |
| :--- | :--- | :---: | :---: |
| **`scripts/test/run_rt_host.sh`** | 9 个 RT 诊断主机套件（45 个断言）：尺寸预算、时钟能力、CPU 周期累加、阻塞隔离、在途查询、唤醒延迟、32 位回绕与 59s 不确定性检测、代数复用失效（`RTOS_ERR_STALE`）、上下文校验 | **9/9 PASS (100%)** | 0.42 s |
| **`scripts/test/run_host.sh`** | Stage 9.2-H5 应用模块化测试（35 个断言）：IPC 队列、8 槽历史环、闭环调度、MultiButton 状态机、中文字模 | **8/8 PASS (100%)** | 0.28 s |
| **`scripts/test/run_regression.sh`** | 内核历史与回归全集测试（360 个测试用例）：信号量、队列、互斥量 PI、死锁诊断 | **360/360 PASS (100%)** | 0.94 s |
| **`scripts/test/run_rt_qemu.sh`** | QEMU ARM 目标运行脚本 | **NOT_TESTED** | N/A |

> [!NOTE]
> **真实性声明**：本地环境未安装 `qemu-system-arm` 二进制文件。根据 Issue #5 要求，“真正执行 ARM ELF 才能写 PASS；只检测安装/跑 Host 为 NOT_TESTED”，本报告如实记录 QEMU 状态为 `NOT_TESTED`，严禁通过 Host 测试冒充 QEMU 通过。

---

## 四、STM32 硬件实机受控烧录与 60 秒稳定性验收

### 1. 硬件预检与身份核验
- **目标芯片**：STM32F103ZET6（512 KiB Flash, 64 KiB SRAM）
- **ST-Link 探针**：VID:PID `0483:3748`，供电电压 `3.27 V`
- **IDCODE**：`0x10036414`
- **UID**：`0x05d8ff31 36533130 43117928`
- **Option Bytes**：`0x00ff5aa5 0x00ff00ff 0x00ff00ff 0x00ff00ff`（读保护解除，写保护未启用）

### 2. 受控 Flash 烧录与回读校验
- **固件镜像**：`artifacts/stage9_3_a/insightrtos_display_o2_trace.bin`
- **固件大小**：49,588 字节（Flash 配额上限 63,488 字节，余量 13,900 字节）
- **擦除范围**：严格限制于第 0～30 页 `[0x08000000, 0x0800F800)`（共 63,488 字节），**严禁全片擦除**；
- **尾部校验**：`[0x0800C1B4, 0x0800F800)` 共 13,900 字节**100% 均为 0xFF**；
- **回读对比**：独立重连后读回主 Flash，SHA256 对比：
  - 固件原始 SHA256：`a3cdc8d487a49f5467f23680e2a8f811179ac115f5fcae3543484a428291df57`
  - 硬件回读 SHA256：`a3cdc8d487a49f5467f23680e2a8f811179ac115f5fcae3543484a428291df57`
  - 逐字节完全一致（`Byte match: True`）。
- **Option Bytes 检查**：前后一致，无越界写。

### 3. 60 秒硬件稳定性连续采样 (13 次采样)
运行 `tests/hardware/sample_stability.py` 对真实运行的 STM32 芯片进行 62.06 秒（13 次独立采样）不间断在线探测：
- **采样总时长**：62.06 秒
- **TIM2 硬件基准频率**：993.17 Hz（标称 1,000 Hz，受 OpenOCD SWD 调试采样暂停微扰影响，完全正常）
- **Kernel SysTick 频率**：99.31 Hz（标称 100 Hz，严格保持 10:1 节拍比）
- **LCD 实际刷新率**：3.9 Hz（标称 ~3.8 Hz，独占 SPI3/DMA 稳定刷新）
- **错误计数**：
  - 传输错误（Transport Errors）：`0`
  - 初始化错误（Init Errors）：`0`
  - 按键队列溢出（Key Overflows）：`0`
  - 控制命令溢出（Command Overflows）：`0`
- **屏幕显示**：单屏 240×240 中文显示界面完全正常无乱码，KEY、CTRL、LCD、MON 任务状态与优先级动态刷新，死锁诊断显示“无死锁”，无新增 cycle 显示行。

---

## 五、遗留问题与下一阶段边界 (Stage 9.3-B 待办)

1. **本阶段终止点**：本阶段（Stage 9.3-A）任务至此全部验收合格，立即进入 `STOP / REVIEW`，**不自动开启 Stage 9.3-B**；
2. **Stage 9.3-B 规划工作**：
   - 实现 `rtos_diag_job_release()` 与 `rtos_diag_job_complete()` 语义；
   - 任务预算超限（Budget Overrun）与相对截止时间（Deadline Miss）判定；
   - 复杂周期性释放任务的高负载与抖动测试。
