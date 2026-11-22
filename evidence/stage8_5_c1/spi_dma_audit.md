# SPI/DMA 契约与长块风险

原spi_stm32_ll.c、spi_bus.h、bsp_spi.c、st7789.c、st7789_port.c和字体SHA保持；只改集成应用和条件页面分支。LCD Task唯一入口，欢迎/五页调用前有LCD身份断言，KEY/Controller/Monitor不调用屏幕。无其他SPI3用户，不新增无业务意义Mutex；同步DMA返回前所有缓冲区仍有效。

## 总线和故障

LCD → 原ST7789协议 → lcd_cs_low/Port_Cs acquire → spi_bus状态机 → Port_Write/ll_transfer → DMA2 TX_CH2/RX_CH1 → TC/TE/尾部TXE/BSY检查 → 关闭请求/通道清标志 → 释放depth，最终CS高。错误不绕过原恢复；release保留ERROR，下一次acquire自动recover。DMA为轮询同步完成，没有虚构异步RTOS等待或IRQ通知；TIM2无SPI/阻塞API。

新Host契约测试链接实际原st7789.c及实际spi_bus.h，mock Port/总线底层，在100个连续写调用位置分别注入TIMEOUT，验证owner=NULL/depth=0/CS释放、ERROR与错误码保留，下一acquire恰好一次recover再成功。此测试验证协议/状态机调用，不等同于实际LL DMA/电气测试；Port_Cs硬件实现则以原SHA和代码审计保持。

原LCD_ShowString逐字逐行发送，字节行长为12/16/20/24/32（最大32B）；原全屏/形状填充按480B行缓冲发送。新欢迎/状态页只用原Fill及ShowString，当前生产UI数据块≤480B，栈上临时行字符串在同步显示调用返回前存活，字体常驻Flash，静态s_line_buf只被LCD写，抢占不释放或覆盖其指针。没有新65535B图像传输或整帧DMA。

## 已确认的长块预算缺陷：OPEN

实际原表达式timeout=chunk*8*(PCLK/requested_SPI)+1000000，用DWT CPU周期计时，却以36MHz PCLK而不是72MHz CPU换算。当前分频/2、实际SPI目标18MHz时，旧预算=16×chunk+1000000，理想线速需要32×chunk。

65535B：旧2048560周期≈28.452ms，理想2097120周期≈29.127ms，预算少48560周期≈0.674ms；大于62500B已在没有任何额外负载的理想条件下不足。480B：预算1007680周期，理想15360周期≈0.213ms，当前UI行路径没有同一数学不足。测试脚本dma_budget_audit.py直接检查当前源表达式并导出dma-budget.csv，未用理论值冒充实测。

建议后续独立驱动变更采用64位向上取整ceil(chunk*8*CPU_Hz/actual_SPI_Hz)+有依据余量；actual_SPI须来自真正BR分频，不能用未取整请求频率，限制最大预算/32位DWT回绕窗口，仍检查TX/RX TC、TE和最终BSY。必须配套独立LL寄存器时钟模型、超时前后对照、分块/错误恢复及硬件测试。本轮未取得实际LL/DMA时序证据，不扩展UI任务为驱动修复，按用户允许保留OPEN，**不宣称所有DMA路径通过**。

继承RXNE drain对永久异常硬件标志仍无独立软件期限；正常读DR会清标志。未关闭超时、未改成无限等待、未忽略错误。实际SPI3 18MHz及硬件故障恢复NOT MEASURED。
