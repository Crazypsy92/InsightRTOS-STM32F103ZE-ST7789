# C1 长块 DMA 超时复现

冻结 C1 文件 `BSP/Src/spi_stm32_ll.c` 的 SHA256 为 `051f8ac830bd502f8abdec0e8e1783b81a17522ceaf7fda338e6ad35613de8ac`，与 C1 源码清单一致。修改前复制该文件，直接编译到 Host 寄存器模型。测试通过实际 `spi_stm32_ll_init → spi_bus_acquire → spi_bus_transfer → spi_bus_release` 进入 LL，并在错误后验证下次 acquire 和发收。原文件可从 C1 的 B1.4 源码归档提取，并以清单校验。

当 HCLK=72 MHz、PCLK1=36 MHz、SPI3 BR=0 时，SCK=18 MHz。每 8 位帧占 32 个 HCLK 周期。旧式计算在同一配置只计 16 周期/B，外加 1,000,000 周期。无间隙理想线时与旧预算的交点为 62,500 B。

| 输入 | 旧预算（周期） | 理想线时（周期） | C1 模型结果 | C3 模型结果 |
|---|---:|---:|---|---|
| 62,500 B | 2,000,000 | 2,000,000 | OK，27.779 ms | 对照点由旧版测试保留 |
| 65,535 B | 2,048,560 | 2,097,120 | TIMEOUT (-2)，28.453 ms | OK，29.129 ms |

Fake DMA 以实际 BR 和 APB/HCLK 模拟总线推进。65,535 B 在 TC 到来前 48,560 个周期就已用完旧预算，即使 DMA 没有仲裁间隙。模型以整块比特结束时刻发布 TC，两者可能相差硬件发送流水线末尾的少量帧周期，远小于缺口。测试用 32 周期采样步长，记录耗时包括相应软件等待和模型内恢复开销。LCD 常规填充使用 240×2 B 行缓冲，每行最长 480 B，C2 五页通过与本缺陷可以同时成立。大块 `LCD_ShowPicture` 或直接 SPI 总线调用可传更大长度。

复现命令（`old_ll.c` 为上述 SHA256 的旧文件）

```sh
tar -xOzf evidence/stage8_5_c1/baseline/application-b1_4.tar.gz BSP/Src/spi_stm32_ll.c > /tmp/old_ll.c
sha256sum /tmp/old_ll.c
cmake -S tests/stage8_5_c3 -B /tmp/c3-before -DCMAKE_BUILD_TYPE=Debug -DSPI_SOURCE=/tmp/old_ll.c
cmake --build /tmp/c3-before
timeout 15 /tmp/c3-before/spi_dma_boundary before
```

原始复现保存在 `before-build.log` 和 `before-reproduction.csv`。这是生产驱动在 Host 寄存器模型中的可重复失败，未进行物理 SPI 传输。
