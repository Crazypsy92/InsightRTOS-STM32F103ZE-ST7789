# InsightRTOS 应用层模块化与测试资产管理方案

> 设计状态：**PROPOSED — 方案已入库，生产源码尚未重构**。先完成架构迁移及等价回归，再启动 Stage 9.3 实时性诊断。默认不启动 ChatGPT 云端定时接力。
>
> 参考工程：[STM32F103-FreeRTOS-MultiButton](https://github.com/Crazypsy92/STM32F103-FreeRTOS-MultiButton)；本工程：[InsightRTOS-STM32F103ZE-ST7789](https://github.com/Crazypsy92/InsightRTOS-STM32F103ZE-ST7789)。
>
> 当前冻结基线：Stage 9.2-H4，最新硬件提交 `7163f0c`，四任务 KEY/CTRL/LCD/MON，优先级 5/4/2/1，100 Hz SysTick + 1 kHz TIM2，ST7789 SPI3/DMA。H4 firmware 49,424 B，当前受控擦写区 `[0x08000000,0x0800F800)`。不要在架构改造中改变产品行为。

## 1. 对照现状和迁移原则

| 设计点 | FreeRTOS 参考仓库 | InsightRTOS 现状 | 本轮处理 |
|---|---|---|---|
| 任务入口 | `BSP/Src/freertos_task.c` 的 `freertos_start()`，集中定义任务参数、函数、句柄和创建入口 | `Core/display_app.c` 的 `display_app_start()` 创建四任务，同时容纳 IPC、事件追踪等 | 保留**一个任务入口/模板**，将 IPC 和事件跟踪移至独立模块 |
| 项目配置 | `BSP/Inc/FreeRTOSConfig.h` 配内核；`freertos_task.c` 局部宏配任务 | `Middlewares/InsightRTOS/include/rtos_config.h` + `CMakePresets.json`；任务优先级/队列容量散在 `display_app.c/.h` | 分清内核配置与应用任务配置，保留生产配置基线 |
| 主入口 | `Core/Src/main.c` 初始化后调用 `freertos_start()` | `Core/Src/main.c` 初始化后调用 `display_app_start()` | 保留对外入口函数名，避免干扰 CubeMX 与其他 profile |
| 驱动 | `BSP/` 驱动 GPIO/MultiButton | `BSP/` 驱动按键、LCD、SPI3/DMA | 不挪 BSP，不把驱动移进内核 |
| 内核 | `Middlewares/FreeRTOS/` | `Middlewares/InsightRTOS/` | **不替换 API、不搬运 FreeRTOS 实现、不重写调度器** |

参考代码仅借鉴**文件职责和任务模板风格**。本工程继续使用 `rtos_task_create_with_ref()`、静态栈、`rtos_queue_*`，绝不引入 `xTaskCreate`、`vTaskStartScheduler` 或 FreeRTOS heap。

## 2. 目标布局（最小可维护结构）

```text
App/
├── Inc/
│   ├── app_config.h       # 四任务优先级、栈、队列容量、事件历史容量
│   ├── app_tasks.h        # 唯一的任务启动/任务声明接口
│   ├── app_ipc.h          # KEY/CTRL/LCD/MON 所用消息结构与 IPC 接口
│   └── app_events.h       # 按键事件 ID、阶段、ACK 结果、只读统计接口
└── Src/
    ├── app_tasks.c       # 任务总模板：栈/句柄/四任务函数/创建并启动
    ├── app_ipc.c         # 五条队列、静态缓冲、初始化、受控入队/出队
    └── app_events.c      # 有界事件 ID 跟踪、阶段推进、成功/失败/重试计数

Core/
├── Src/main.c            # CubeMX 入口；保持原启动契约
├── display_app.c          # 兼容层：display_app_start() -> app_tasks_start()
├── display_app.h          # 兼容转发；迁移期保留旧 include 可用
├── display_ui.c           # UI 布局与只读展示
├── display_pages.c        # LCD 事务与 ACK
├── display_policy.c       # 刷新节奏
└── rtos_smoke_app.c       # 独立 smoke profile，不与正式任务表混用

BSP/{Inc,Src}/             # STM32 驱动和 MultiButton，保持原层级
Middlewares/InsightRTOS/  # 自研 RTOS 内核/诊断/port，保持原层级
cmake/insight_app.cmake    # 仅正式 DISPLAY_RTOS profile 的应用源清单
scripts/test/             # 统一测试入口脚本
tests/                    # 测试源码、桩、fixture（不得放测试可执行文件）
evidence/                 # 按 Stage 冻结的验收摘要与有界原始证据
build/                    # 所有可重建构建物，gitignore
```

> 如果迁移中发现拆开 `app_ipc.c` 会导致锁/并发语义改变，可先保留队列为 `app_tasks.c` 的私有静态对象，但必须将消息类型、容量和 API 边界清楚集中在相应头文件。**优先最小差异，禁止为目录好看牺牲真实性。**

### 2.1 app_config.h（应用配置，不复制内核配置）

- `APP_KEY_PRIORITY=5`、`APP_CTRL_PRIORITY=4`、`APP_LCD_PRIORITY=2`、`APP_MON_PRIORITY=1`。
- 四任务 `APP_TASK_STACK_WORDS=1024`，总保留 `RTOS_MAX_TASKS=8` 个独立静态栈槽，沿用 `.user_stacks` 段与对齐；不要简单理解为 4×4 KiB 后直接删掉其余保留槽。
- 队列容量保持 `KEY=32`、`COMMAND=32`、`ACK=4`、`METRIC=4`；8 槽事件历史 `DISPLAY_EVENT_HISTORY=8`。
- 系统级 `RTOS_TICK_HZ=100`、`RTOS_MAX_TASKS`、TCB ABI、诊断宏、ISR/优先级归属继续由内核头和 CMake 控制，**不在 app_config.h 中定义第二份同名值**。
- 增加编译期断言检查栈单位（uint32_t words ≠ bytes）、队列元素尺寸 `<=RTOS_QUEUE_MAX_ITEM_SIZE`、优先级有效性、资源与 ABI。

### 2.2 app_tasks.c（唯一任务模板）

- 放置四任务 `key_task/controller_task/lcd_task/monitor_task` 和唯一 `app_tasks_start()`，采用参考工程“配置→任务句柄→任务函数→统一启动”的清晰顺序；是否用静态任务描述表取决于与原创建顺序是否完全一致。
- 任务入口均为 `static void xxx_task(void *argument)`、各自 `for (;;)`。注释简洁标明“触发来源/等待对象/优先级/处理职责/允许阻塞 API/是否触碰 LCD”。源码里的硬件任务创建、顺序与周期必须等价。
- 统一创建 4 个任务并启动调度器；LCD Task 唯一拥有 SPI3/ST7789。不要创建 LED Task、测试 Task、诊断 Task 参与正式固件。
- `Core/display_app.c` 保留很薄的 `display_app_start()` 桥接，使 `Core/Src/main.c` 及其他旧引用不变。smoke 与 bare-metal profile 应独立构建。

### 2.3 app_ipc.c 与 app_events.c

- `app_ipc` 仅处理五条现有队列、静态存储及消息类型；队列所有权、ISR/Thread API、FULL/BLOCKED/ACK 规则在接口注明。禁止让 UI/驱动直接依赖队列内部 `count`，如需要使用**受保护只读快照接口**。
- `app_events` 维护 ID、8 槽历史、阶段跃迁、成功/失败/重试/丢弃计数。建立只读快照 API；真实 ACK 与 ID 一致后才增加成功数，`KEY_EVENT_NONE` 不计事件。保证 ISR O(1)、没有格式化和动态内存。
- H4 使用指针 `Display_FindTrackedEvent` 直接返回全局状态地址；迁移时要审计其使用者，逐步替换为有界更新接口，避免跨任务读写竞态或因历史槽位覆盖造成误计数。**迁移阶段不重定义事件语义，不做未经测试的业务改进**。
- `Core/display_ui.c` 只读统计/任务快照；诊断运行时数据不直接塞进 LCD，保留“系统状态、四任务、按键链路、LED、异常”界面风格。

### 2.4 后续 Stage 9.3 的接口边界（此轮只预留，不实现）

- 实时性诊断属于 **RTOS 内核诊断模块**，而不是 Display/BSP 模块。后续在 `Middlewares/InsightRTOS/include/rtos_diag_rt.h` 和 `kernel/diag_rt.c` 等可审计位置增加 API 与编译开关 `INSIGHT_DIAG_RT`，接口命名以 Stage 9.3 正式设计审查为准。
- 应用 `App/Src/` 只通过公开只读 API 采样，不自行维护第二份任务 CPU 时间、截止时间或 Trace。
- 禁止把 H5 当作提前完成 9.3；本轮不实施 DWT side table/job/deadline 逻辑。

## 3. 迁移顺序与安全门槛

| 子步骤 | 改动内容 | 通过判据 |
|---|---|---|
| H5.1 冻结现状 | 记录 H4 commit、CMake profile、任务/队列/栈/统计映射、ELF/MAP/SHA 和现有测试结果 | 基线可复现；列出适配点与调用方 |
| H5.2 配置与任务模板 | 引入 `app_config.h/app_tasks.h/app_tasks.c`、桥接旧入口；不变更四任务业务行为 | 四任务名称、优先级、栈和创建顺序相同；四种 RTOS Debug/O2 profile 编译 |
| H5.3 IPC/事件模块 | 按最小差异把消息类型、队列存储和事件跟踪移至 `app_ipc/app_events`，单一所有者，原 UI 不改 | 消息大小、状态与 ACK 行为一致；原 Host 回归全 PASS |
| H5.4 构建与验收 | 用 `cmake/insight_app.cmake` 管源、补直接测试真实生产代码、实机运行 60 秒及文档 | 构建配置不互相串源、H4 功能与烧录读回稳定，证据完整 |

不建议“按文件名一键全局搬家”；如阶段中断，应能回到 H4 已通过固件。未经审查不修改：
- `Middlewares/InsightRTOS/kernel/`、`arch/arm/`、`port/`、TCB 72 B、PendSV/SVC 调度入口；
- 100 Hz SysTick + 1 kHz TIM2；
- `BSP/` 的 SPI3/DMA、GPIO、MultiButton 初始化顺序；
- `CMakePresets.json` 的裸机/Smoke/Display profile 含义；
- `display_app_start()` 公开入口、用户体验和真实硬件观测语义。

## 4. 测试与文件归档规范（从 H5 开始新增项适用）

```text
tests/
├── host/                 # 面向真实生产模块源码的 unit/integration
├── qemu/                 # ARM Cortex-M3 无物理硬件验证
├── hardware/             # 受控 STM32 实机脚本、校验 fixture
├── fixtures/             # 精简输入与预期结果
└── stage9_2_h4/          # 历史保留，不挪移/重写

scripts/test/
├── run_host.sh
├── run_qemu.sh
├── run_hardware.sh
└── run_regression.sh

evidence/stage9_2_h5/
├── acceptance.md         # 唯一总验收：结论、测试来源、代码 SHA
├── test_results.md       # Host/QEMU/回归测试命令与真实结果
├── hardware.md           # 60 秒板上观测、烧录/读回、未测项
├── manifest.json         # 版本、工具链、固件 SHA、链接资源
└── logs/                 # 有界的必要原始可核验日志（只交文本/JSON）
```

- 源码与脚本保留在 `tests/`、`scripts/test/`；构建出的 ELF/BIN/HEX/MAP、可执行测试程序及临时生成文件只进 `build/`；仓库不得积累大量重复固件拷贝与大体积二进制日志。
- 规范由 **阶段号 + 稳定文件名** 区分，不再为每次更新造 `V1/V2/final/final2` 文件；`acceptance.md` 是该阶段唯一主报告，不同时建立多份同义报告。
- `README.md` 只讲最新结构、实际使用和一条最新验收入口。长测试矩阵集中在 `docs/`；阶段证据只给结果与证据链接。
- 历史 `evidence/stage9_2_h2/h3/h4`、旧脚本、日志不得为了“清爽”直接删除。确需后续整理，另做索引与迁移映射，保证 SHA、引用仍可追溯。
- 当前 `.gitignore` 有 `*.log` 规则。若确需上传原始文本日志，只对 `evidence/**/logs/` 做**精确例外规则**，不开放全仓日志，也不要强制提交密钥、UID、隐私或无界文件。
- H4 的 `tests/stage9_2_h4/test_stage9_2_h4.c` 主要自建结构体和模拟阶段转移。H5 新增 **直接编译生产 `App/Src/app_events.c`、`app_ipc.c`、任务初始化代码的 Host/mock 集成测试**，避免只测测试复制品；测试的编译结果不得在 `tests/` 内生成未跟踪二进制。
- 硬件测试和生成固件的授权遵守 `docs/remote_task_relay.md`：用户已持续允许同一 STM32F103ZET6 的常规烧录/复位/回读和非破坏性测试，但仍要核对目标及只擦写主 Flash 页 0～30；不能 mass erase、动 Option Bytes/RDP/WRP 或超出区域。
- 每项报告注明 `PASS/FAIL/NOT_TESTED/USER_CONFIRMATION_PENDING`；没有原始数据的“全通过”不得当真实验收。发现问题保留真实失败日志和修复前后差异。

## 5. H5 架构整理的量化门槛

1. 用户业务任务仍 4 个：KEY P5、CTRL P4、LCD P2、MON P1；IPC 队列容量/元素类型不变；0 新动态分配。
2. 单击/双击/长按、PC13 LED、15 行中文屏、事件 ID→ACK、Trace 与 Mutex 诊断均与 H4 相同；按键处理成功/失败/重试统计正确。
3. H4 Host 9/9、既有 360/360 原回归重跑且无新增失败；新增集成测试真实生产对象，需包含多事件交错、ACK 错误、queue FULL、ID/slot 覆盖和周期刷新。
4. Debug/O2 × Smoke/Display = 4 个关键构建均通过；原裸机 profile 同时不回归。如新增测试使用 QEMU，只把真正跑过的项目标 PASS。
5. TCB 72B、固定 SRAM 64KiB、栈预算、最大 64B queue item ABI 编译验证；Flash 不超 `[0x08000000,0x0800F800)` 已授权区，提交实际大小和 H4 对比，超限先停下评估。
6. 板上实际烧录需先核对 MCU UID/地址，回读 SHA256 一致；连续自由运行至少 60 秒、0 HardFault/Assert、0 新增 SPI/DMA/队列错误；人眼按键确认仍按真实状态填写。
7. 一个架构文档、一个阶段主报告、对应测试源码及必要原始证据即可；不生成大量形式化审计文档。GitHub Issue 回填 commit 与证据，状态置 REVIEW 后停止，**绝不自动开始 Stage 9.3**。

## 6. 后续阶段（H5 通过后，按需单独派发）

- **Stage 9.3-A：实时性诊断最小内核原型。** `INSIGHT_DIAG_RT` 独立关闭/开启、任务代数隔离、假时钟/计数回绕、任务运行时间与首次响应延迟、job release/complete 最小语义。先 Host + QEMU，板上再用 DWT 验证时钟能力与开销；不往 LCD 堆统计数字。保持 stage9.2_H5 的模块边界和证据规范。
- **Stage 9.3-B：预算和 Deadline Miss 检测、竞争及错误路径。** 对真实高负载任务验证开销与最坏时延，三诊断互不改写调度语义。
- **Stage 9.4：三大诊断模块组合及长稳负载。** 先现有 STM32 + ST7789 + KEY/LED、再 UART DMA 环回与 ADC DMA 并发，最终 1h/24h，明确零错/退化阈值。UART 回环用户以前已做、未开源；**不盲目新建替代仓库或重复制造历史项目**，待后续用户提供现有源码再复用。

> 当前文件本身不是功能验收。执行入口由单个 GitHub [TASK] Issue 驱动；按用户日常模式不启用自动定时接力。