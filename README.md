# insightrtos — STM32F103ZET6 ST7789 屏幕驱动示例

> **状态：v0.1.0-alpha（早期版本）**
> 已在真实硬件（STM32F103ZET6 + ST7789 1.54" 240×240）上完成上电运行、4 页面显示与按键翻页验证。
> 内核仍在持续完善，**不是工业级成熟 RTOS**，请勿直接用于安全关键场景。

基于 STM32F103ZET6 + ST7789（1.54" 240×240）的裸机工程。核心是一套 **MCU 无关的 SPI 抽象总线**：同一物理 SPI 外设可被多个设备按事务独占使用，并具备状态机、统一错误码与故障自恢复能力。当前示例设备为 ST7789 TFT，按键切换测试页面；`Middlewares/InsightRTOS` 内置自研 Cortex-M3 内核（v0.7.5 源码快照，来源与 SHA256 见 [`VERSION_MANIFEST.md`](Middlewares/InsightRTOS/VERSION_MANIFEST.md)），可通过构建开关编译成双任务冒烟镜像。

## 硬件与接线

| 功能 | 引脚 | 说明 |
| --- | --- | --- |
| SPI3_SCK | PB3 | 需关闭 JTAG（SWJ_NOJTAG）后复用 |
| SPI3_MISO | PB4 | ST7789 只发不收，可不接 |
| SPI3_MOSI | PB5 | 需关闭 JTAG 后复用 |
| LCD_CS | PD11 | 软件片选，低有效 |
| LCD_DC | PD12 | 数据/命令选择 |
| LCD_BL | PD13 | 背光，高电平点亮 |
| LCD_RST | 无 | 驱动使用软件复位命令 |
| KEY1 | PA15 | 上拉输入，按下为低；需 SWJ_NOJTAG |
| LED | PC13 | 低电平点亮 |

SPI3 挂在 APB1（36 MHz），默认 18 MHz / Mode 0 / 启用 DMA。SPI3_TX → DMA2 通道 2，SPI3_RX → DMA2 通道 1。

> PB3/PB4/PB5 与 JTAG 复用，工程在 `main()` 中先调用 `LL_GPIO_AF_Remap_SWJ_NOJTAG()`，保留 SWD 调试。

## 软件架构

自下而上分层，换 MCU 只需替换最底层的实现文件：

```
应用层      app_lcd_test.c        4 个测试页面 / 按键翻页
协议层      st7789.c             ST7789 命令、初始化、绘图、字符
移植层      st7789_port.c        SPI3 + PD11/PD12/PD13 的板级适配
总线抽象    spi_bus.h            所有权 / 嵌套 / 状态机 / 错误码（MCU 无关）
LL 实现     spi_stm32_ll.c       GPIO 复用、SPI/DMA 配置与阻塞传输
板级        bsp_spi.c            SPI1 / SPI2 / SPI3 的引脚与时钟表
```

驱动与移植层只依赖 `spi_bus.h`；`spi_stm32_ll.*` 是全工程唯一产生 SPI 时序的地方。片选（CS）由 port 层管理，总线层不操作任何 CS。

## SPI 总线契约

一次事务必须成对调用：

```c
spi_bus_acquire(bus, owner, &cfg);   /* 切换速率/模式并占用总线 */
/* ... spi_bus_transfer(bus, owner, tx, rx, len) ... */
spi_bus_release(bus, owner);         /* 返回 1 表示完全释放，可拉高 CS */
```

- **独占**：`acquire..release` 之间总线只属于一个 owner；同 owner 可嵌套，嵌套层级由 `depth` 记录。
- **ISR 拒绝**：中断上下文中 `acquire` 返回 `SPI_BUS_ERR_ISR`，防止打断主循环的 DMA 事务。
- **配置一致**：嵌套事务的 `clock_hz/mode/use_dma` 必须与首次一致，否则返回 `SPI_BUS_ERR_CONFIG`。
- **阻塞传输**：DMA 也等待 TC/BSY 后才返回；传输失败进入 ERROR，下一次 `acquire` 自动尝试 `recover`。

状态机：

```
UNINIT --configure OK--> READY --acquire--> BUSY
BUSY   --release(depth==0)--> READY
BUSY   --transfer 失败--> ERROR
ERROR  --recover OK--> READY
ERROR  --下一次 acquire--> 自动 recover
```

统一错误码 `spi_bus_status_t`：

| 值 | 含义 |
| --- | --- |
| `SPI_BUS_OK` = 0 | 成功 |
| `SPI_BUS_ERR_PARAM` = -1 | 参数非法 |
| `SPI_BUS_ERR_TIMEOUT` = -2 | TXE/RXNE/BSY/DMA 等待超时 |
| `SPI_BUS_ERR_DMA` = -3 | DMA 传输错误（TEIF） |
| `SPI_BUS_ERR_BUSY` = -4 | 外设忙 / 总线被其它设备占用 |
| `SPI_BUS_ERR_RECOVERY` = -5 | 异常恢复失败 |
| `SPI_BUS_ERR_ISR` = -6 | 中断上下文中占用总线 |
| `SPI_BUS_ERR_STATE` = -7 | 状态机不允许该操作 |
| `SPI_BUS_ERR_CONFIG` = -8 | 嵌套事务配置不一致 |
| `SPI_BUS_ERR_UNSUPPORTED` = -9 | 实现方未提供该操作 |

调试接口：`spi_bus_get_state()`、`spi_bus_get_last_error()`、`spi_bus_clear_error()`、`spi_bus_recover()`。

## 屏幕测试程序

上电初始化后自动播放 4 个页面：

| 页面 | 内容 |
| --- | --- |
| 0 | 颜色条 |
| 1 | 图形（线、矩形、圆） |
| 2 | 文字（ASCII 12/16/20/24/32 字体） |
| 3 | 动画（小球边界反弹，25 ms 一帧） |

前 3 页每 2 秒自动切换；短按 KEY1 手动翻页并停止自动播放，到动画页后持续运行动画。全程不依赖串口，仅靠屏幕与 PC13 LED 反馈。

ST7789 初始化最后默认调用 `LCD_SetRotation(dev, 180U)`；若实际显示方向不符，调整该调用的角度或 180° 下的显存偏移即可。

## 目录结构

```
BSP/
  Inc/  spi_bus.h  spi_stm32_ll.h  bsp_spi.h  st7789.h  st7789_port.h
        app_lcd_test.h  key_port.h  multi_button.h  lcd_fonts.h  delay.h
  Src/  spi_stm32_ll.c  bsp_spi.c  st7789.c  st7789_port.c
        app_lcd_test.c  key_port.c  multi_button.c  lcd_fonts.c  delay.c
Core/   main.c  gpio.c  ...
cmake/  stm32cubemx/  gcc-arm-none-eabi.cmake
```

## 构建与烧录

依赖：CMake ≥ 3.22、Ninja、`arm-none-eabi-gcc`（`arm-none-eabi-` 前缀需在 PATH 中）。

工程有两套镜像，由 `INSIGHT_RTOS_ENABLED` 切换：

| 预设 | 镜像 | 说明 |
| --- | --- | --- |
| `Debug` / `Release` / `O2` | `insightrtos` | 裸机 LCD 演示（4 个测试页面） |
| `RTOS_Debug` / `RTOS_O2` | `insightrtos_rtos` | RTOS 双任务冒烟镜像（`Core/rtos_smoke_app.c`） |

```bash
cmake --preset Debug          # RTOS 镜像用 RTOS_Debug / RTOS_O2
cmake --build --preset Debug
```

生成物：`build/<preset>/insightrtos.elf`（RTOS 镜像为 `insightrtos_rtos.elf`），以及 `.hex/.bin`。

> 烧录会覆盖目标板 Flash 中的原有固件与数据，请先确认板内无需要保留的内容。

ST-Link + OpenOCD 烧录（按实际镜像名替换）：

```bash
openocd -f interface/stlink-v2.cfg -f target/stm32f1x.cfg \
  -c "program build/Debug/insightrtos.elf verify reset exit"
```

## 注意事项与已知限制

- **SPI/DMA 长块传输超时预算（优先修复）**：单次传输的等待超时是固定预算，未随数据量放大，大块传输存在被误判为 `SPI_BUS_ERR_TIMEOUT` 的风险；后续按分块大小动态计算超时。
- **单 owner 非阻塞不支持**：当前 `release` 必须在 `transfer` 返回后立即执行；若改成异步 DMA，需要把释放移入完成回调并增加超时语义。
- **ISR 不可直接访问 SPI**：请把请求排队到主循环执行。
- **recover 要求总线空闲**：`owner != NULL` 时调用返回 `SPI_BUS_ERR_BUSY`；失败会保持 ERROR 状态。
- **DMA 分块上限**：CNDTR 为 16 位，单次 ≤ 65535 字节，驱动自动分块。
- **无显存**：逐块/逐行发送，RAM 占用低；大区域填充耗时与 SPI 速率直接相关。
- **中文字库**：`lcd_fonts.c` 内含 GB2312 点阵，Flash 占用较大；不需要时可裁剪。
- **PB3/PB4/PB5**：必须关闭 JTAG，否则 SPI3 无法工作。

## 许可证

本仓库暂未添加 `LICENSE` 文件。STM32 驱动库（HAL/LL）、CMSIS、字库等第三方文件的授权仍在审计中，审计完成前代码仅供学习与参考，不构成开源授权。
