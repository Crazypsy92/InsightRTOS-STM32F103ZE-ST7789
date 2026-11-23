# InsightRTOS Stage 9.3-A2 测试结果与覆盖报告

## 1. 测试环境信息

- **主机环境**：Linux 6.8.0-90-generic x86_64
- **编译器工具链**：
  - Host 测试：`gcc (Ubuntu 13.3.0-6ubuntu2~24.04)`
  - ARM 嵌入式编译：`arm-none-eabi-gcc (15:13.2.rel1-2)`
  - 构建工具：CMake 3.28.3, Ninja 1.11.1
- **硬件目标**：STM32F103ZET6 (512 KiB Flash, 64 KiB SRAM, Cortex-M3 r1p1, IDCODE: `0x10036414`)
- **硬件调试器**：ST-LINK/V2 (USB VID:PID `0483:3748`) via OpenOCD 0.12.0

---

## 2. 自动化测试套件执行结果

### 2.1 模块化应用 Host 测试套件 (`tests/host/test_app_modular.c`)
- **执行命令**：`bash scripts/test/run_host.sh`
- **执行结果**：**9/9 Suites PASS**（共 45 个断言）

| 编号 | 测试用例名称 | 断言数 | 状态 | 核心验证点 |
| :---: | :--- | :---: | :---: | :--- |
| 1 | `test_configuration_and_abi` | 6 | **PASS** | TCB 72 B ABI、优先级边界、时钟定义与静态断言 |
| 2 | `test_app_ipc_queues` | 5 | **PASS** | 五条生产 IPC 队列深度、满载与排空行为 |
| 3 | `test_app_events_transitions` | 6 | **PASS** | 完整事件生命周期流转（IDENTIFIED → KEY → CTRL → LCD → ACKED） |
| 4 | `test_bounded_history_and_drop` | 5 | **PASS** | 8 槽环形缓冲区截断、溢出丢弃与重试统计 |
| 5 | `test_pipeline_simulation` | 6 | **PASS** | 闭环多任务多事件处理模拟（SHORT, DOUBLE, LONG） |
| 6 | `test_multibutton_events` | 5 | **PASS** | MultiButton 状态机驱动与事件分发 |
| 7 | `test_glyph_pack_and_bounds` | 4 | **PASS** | 16×16 中文点阵字模包与 240×240 单屏边界 |
| 8 | `test_led_and_deadlock_states` | 4 | **PASS** | PC13 LED 低电平有效性与死锁诊断状态显示 |
| 9 | `test_consecutive_events_overflow_and_ack_resilience` | 4 | **PASS** | 16 连续事件环覆盖、孤儿 ACK 准确结算、零静默丢失与队列满载反压 |

### 2.2 实时诊断 Host 测试套件 (`tests/host/test_diag_rt.c`)
- **执行命令**：`bash scripts/test/run_rt_host.sh`
- **执行结果**：**9/9 Suites PASS**（共 45 个断言）

| 编号 | 测试用例名称 | 断言数 | 状态 | 核心验证点 |
| :---: | :--- | :---: | :---: | :--- |
| 1 | `test_rt_budget_and_structures` | 5 | **PASS** | 静态侧表 SRAM 占用 568 B $\le$ 1024 B 预算 |
| 2 | `test_rt_clock_capabilities` | 5 | **PASS** | 72 MHz DWT 时钟能力、分辨率与不可用状态 |
| 3 | `test_rt_cpu_cycles_accumulation` | 5 | **PASS** | 确定性 Fake Clock 切入切出周期单调累计 |
| 4 | `test_rt_blocked_and_idle` | 5 | **PASS** | 阻塞态零计费、空闲独立累计与唤醒准确性 |
| 5 | `test_rt_in_flight_query` | 4 | **PASS** | 正在运行任务在途周期的动态补正计算 |
| 6 | `test_rt_ready_latency` | 5 | **PASS** | 就绪至调度运行响应延迟（Tick 与 Cycle 双精度） |
| 7 | `test_rt_rollover_and_uncertainty` | 5 | **PASS** | 32 位回绕跨纪元平滑过度，$\ge 59$s 触发 UNCERTAIN |
| 8 | `test_rt_generation_stale` | 6 | **PASS** | 任务退出与槽位复用时的 Stale Generation 判定 |
| 9 | `test_rt_arguments_and_context` | 5 | **PASS** | 空指针、非法槽位及无时钟输入时的鲁棒性 |

### 2.3 QEMU 仿真运行器 (`scripts/test/run_rt_qemu.sh`, `scripts/test/run_qemu.sh`)
- **执行结果**：**NOT_TESTED**
- **如实记录原因**：
  1. 系统 PATH 中未安装 `qemu-system-arm` 二进制文件。
  2. 上游 QEMU 不支持 STM32F103ZET6 芯片板级外设，且本工程未引入虚假的 lm3s6965evb 替代代码。
  3. 脚本移除了 `|| true` 掩饰与 Host 伪装替代，严格输出 `Status: NOT_TESTED` 并保持返回码与证据一致。

### 2.4 内核历史回归测试套件 (`build-host-stage9_2`)
- **执行命令**：`ctest --test-dir /home/mythepsy92/program/codex/InsightRTOS/build-host-stage9_2`
- **执行结果**：**360/360 Tests PASS** (100% 通过，耗时 0.97 秒)

---

## 3. 硬件实机烧录与基线稳定性采样（60 秒）

### 3.1 受控烧录验证
- **固件文件**：`build/DISPLAY_RTOS_O2_Trace/insightrtos_display.bin` (50,604 字节)
- **写入地址**：`0x08000000`（精确擦除主 Flash 第 0～30 页 `[0x08000000, 0x0800F800)`，共 63,488 字节）
- **回读校验**：50,604 字节回读 SHA256 与原始固件完全匹配（`0262720154a1a54a9312eb00f01560749124a1a9209fd1e429eb733954516e36`）。
- **未用 Flash 区域**：`0x0800C5AC` 至 `0x0800F800`（12,884 字节）全为 `0xFF`。
- **Option Bytes**：`0x1ffff800` 前后严格不变（`00ff5aa5 00ff00ff 00ff00ff 00ff00ff`）。

### 3.2 60 秒正常负载实测数据
- **采样周期**：5.0 秒，采样总数：13 组，总时长：60.00 秒。
- **原始数据存储**：`evidence/stage9_3_a2/logs/stability_normal_60s.json`

| 测量项 | 理论期望 | 实测数值 | 偏差率 / 评价 |
| :--- | :---: | :---: | :---: |
| **TIM2 硬件定时器频率** | 1000.0 Hz | 994.32 Hz | -0.57% (晶振与调试开销内) |
| **RTOS 调度器 Tick 频率** | 100.0 Hz | 99.42 Hz | -0.58% (与 TIM2 保持 10:1 关系) |
| **LCD 差分刷新频率** | ~3.8 Hz | 3.90 Hz | 正常工作，独占 SPI3/DMA |
| **系统传输错误 (Transport)** | 0 | 0 | **PASS** |
| **系统初始化错误 (Init)** | 0 | 0 | **PASS** |
| **队列满载溢出 (Overflow)** | 0 | 0 | **PASS** (轻负载零溢出) |

---

## 4. 硬件实机受控高频压力测试（5 分钟 / 300 秒）

### 4.1 压力配置与机制
- **持续时长**：300.00 秒（5 分钟）
- **采样间隔**：30.0 秒（采集 11 组有效样本）
- **压力发生源**：在 Monitor Task 内通过受控变量 `g_stress_inject_rate = 100` 以合法软件方式每 100 ms（10 次/秒）向事件流水线注入真实按键事件，驱动中断入队、KEY 消费、Controller 状态转移与 LCD 差分渲染。
- **压力恢复机制**：采样完成后由脚本通过 OpenOCD 将 `g_stress_inject_rate` 写回 0，完全恢复常态。
- **原始数据存储**：`evidence/stage9_3_a2/logs/stability_stress_300s.json`

### 4.2 300 秒压力测试实测数据

| 采样时间点 | TIM2 (ms) | RTOS Tick | LCD 刷新数 | LCD 执行周期 | IDLE 执行周期 | 队列丢弃数 | 系统错误 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **0.0 s** | 73,984 | 7,397 | 287 | 38,001,460 | 5,186,890,499 | 0 | 0 |
| **30.0 s** | 103,956 | 10,394 | 400 | 75,966,390 | 7,296,144,945 | 58 | 0 |
| **60.0 s** | 133,927 | 13,391 | 514 | 114,030,693 | 9,405,304,334 | 115 | 0 |
| **90.0 s** | 163,900 | 16,388 | 628 | 152,106,539 | 11,514,452,167 | 172 | 0 |
| **120.0 s** | 193,873 | 19,385 | 745 | 190,307,219 | 13,623,466,302 | 229 | 0 |
| **150.0 s** | 223,845 | 22,382 | 862 | 228,502,130 | 15,732,479,952 | 286 | 0 |
| **180.0 s** | 253,819 | 25,379 | 980 | 264,713,444 | 17,843,481,899 | 344 | 0 |
| **210.0 s** | 283,790 | 28,376 | 1094 | 300,756,871 | 19,954,657,349 | 402 | 0 |
| **240.0 s** | 313,763 | 31,374 | 1211 | 336,913,437 | 22,065,710,951 | 460 | 0 |
| **270.0 s** | 343,736 | 34,371 | 1325 | 374,995,968 | 24,174,845,861 | 517 | 0 |
| **300.0 s** | 373,708 | 37,368 | 1439 | 413,078,801 | 26,283,988,661 | 574 | 0 |

### 4.3 压力测试分析结论
1. **时钟稳定性保持**：在 300 秒高压期间，TIM2 频率为 999.08 Hz，RTOS Tick 为 99.90 Hz，始终保持稳定。
2. **CPU 周期健康增长**：LCD Task 执行周期从 $3.80 \times 10^7$ 激增至 $4.13 \times 10^8$ cycles；IDLE 从 $5.18 \times 10^9$ 推进到 $2.62 \times 10^{10}$ cycles，证明调度器无死锁、无忙等卡死。
3. **有界过载反压**：持续高频注入事件超出队列消费速率时，系统稳定触发有界拒绝（共计 574 次受控丢弃），**0 次静默丢失，0 次 HardFault，0 次 Assert 挂起**。
