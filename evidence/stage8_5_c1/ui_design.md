# InsightRTOS 240×240 UI

2026-11-20。本仓库app/stm32f103ze_st7789为唯一四任务修改目标；Cube_MX旧路径没有DISPLAY_RTOS入口，不修改。保留裸机Debug/O2四页、双任务RTOS_Debug/O2；四任务DISPLAY_RTOS_Debug/O2输出新版C1包。没有LVGL、图片资源或生产framebuffer。

## 欢迎页

成功ST7789_PortInit之后、调度器已运行的LCD任务内显示InsightRTOS / STM32F103ZET6 / RTOS STARTING...，随后四行显示公开API读到的任务状态和基础/有效优先级。rtos_delay(150tick)约1.5秒后进入原颜色页，其他任务与TIM2/SysTick继续运行。KEY事件在FIFO中保留，欢迎页不额外消费事件。

初始化最多3次、失败间隔100tick，成功后只显示一次欢迎。最后仍失败则display_fault=1、initialization_errors=3；不再重试，不绘图，LCD继续阻塞接收并消费/拒绝显示请求，返回ready=0和错误ACK，KEY/LED/Controller/Monitor继续推进。此时无可用屏幕，诊断保留在RAM/GDB，不伪称显示ERROR画面。欢迎绘制本身的总线错误也归入有限初始化重试。

## 第五页面板

顶部27px深蓝条：InsightRTOS（原ASCII_Font20）。正文ASCII_Font12，y=34+20×row，共10行，37字符上限，末端y226以内，x8至230以内。依次显示RUNNING/ERROR、Kernel Tick、用户任务数量、KEY/Controller/LCD/Monitor状态与基础/有效优先级、Qerr/Back、Page refresh、SPI/DMA err。

Qerr=关键输入队列失败+非关键遥测丢样；Back为保留事件的背压重试，不是丢键。Page refresh为应用ACK的逻辑绘制尝试数，可能包括失败尝试，不是硬件帧计数；SPI/DMA err为失败显示命令计数，不是虚构DMA IRQ次数。状态查询失败显示UNKNOWN、ERROR，不伪造任务状态/数量。计数为uint32自然回绕。没有CPU占用率、虚构切换次数、虚构HardFault计数。

首次进入完整绘制背景与标签；250ms检查一次，只重绘文本或颜色变化的行，用不透明字符及空格尾填充清除旧位数。静态缓存10×38B文本+10×2B颜色=400B，非像素framebuffer。ERROR/数据显示来自公开API/实际计数，上一命令错误已ACK后才清LCD层历史错误；任何本次绘制错误会使行缓存失效，下一周期重试，不把失败像素当作已显示。

Controller仍每tick服务，原25ms动画阈值与前三页2000ms规则保留。250ms仅为状态页检查阈值，实际延迟受10ms tick、队列背压和绘图耗时影响，不是已测硬实时刷新保证。无状态页全屏周期擦除，无长临界区绘图。

## 软件预览与限制

previews/welcome-software.png、monitor-running-software.png、monitor-error-software.png使用原字库位图生成，仅为布局测试fixture，**不是STM32截图或真实任务运行证据**。fixture刻意提供不同基础/有效优先级及非法句柄，以证明UI没有写死状态；生产默认优先级仍5/4/2/1。边界、UINT32_MAX格式化、相同数据不重画、单行状态变化、错误缓存失效均有原UI C测试。真实颜色、旋转、闪烁、亮度、帧率NOT MEASURED。
