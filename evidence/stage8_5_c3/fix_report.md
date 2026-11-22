# 驱动更改与 RXNE 独立审查

生产代码仅改 `app/stm32f103ze_st7789/BSP/Src/spi_stm32_ll.c`。调用者、SPI bus API/错误码、LCD 页面、KEY、时钟、内核/TCB/ARM 汇编均保持原样。旧、新 SHA256 和差异见 `source-integrity.json`、`spi-stm32-ll.diff`。

- 长块改用实际 BR/HCLK/PCLK 推导的逐块期限。双工把 TX、RX 的 TE 和 TC 合并到同一个状态快照和期限，RX TE 可在 TX 未完成时立即终止。仅缺 RX TC 时不重启超时窗口。TXE/BSY 尾部仍受各自上限保护。
- 成功或异常的 DMA 清理先关闭 SPI 请求，再禁用通道，随后以一次 IFCR 写清参与 SPI 的两组通道标志。异常恢复先做上述清理，确认 TXE=1、BSY=0 后再检查 RX。保留原 SPI disable/enable 回退；再次检查仍不空闲时上报 `SPI_BUS_ERR_RECOVERY`。
- 传输失败仍返回原始 `-2 TIMEOUT` 或 `-3 DMA`，总线进入 ERROR。释放只交还 owner/深度。释放后的 recover 或下一次 acquire 才能恢复 READY；恢复失败显式返回 `-5 RECOVERY`。配置时外设已忙仍返回 `-4 BUSY`，不获得 owner。驱动不管理设备 CS。
- RXNE 清理以前使用可无限重复读 DR 的循环。STM32F1 主机处于空闲时，接收缓冲与 OVR 按 LL 的 DR→SR 顺序清理，再读取 SR 验证 RXNE/OVR 已清。配置后、每块起始、TX-only 完成后、恢复后都在无后续 SCK 的位置检查。TX-only 全双工产生的预期 OVR 在完成后清除；卡住的 RXNE 在 Host 模型中有界报错且不会假报恢复。电气干扰、硅片异常或真实 RXNE/OVR 竞态仍为 **OPEN / HARDWARE NOT EXECUTED**，保留板上首字节、OVR 和重复故障专项。 [STM32F1 RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f103xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)

边界测试直接编译本驱动，外围为 Host 寄存器模型。覆盖 TX/RX 缓冲内容、DMA 地址偏移、CNDTR 写入、清理后请求/通道关闭、故障时状态/错误码及下次 acquire。Host Context 用 ucontext 执行原 C 内核的高低优先级任务，低优先级任务在驱动中等待时测得高优先级任务各运行两次，成功与超时恢复均覆盖。测试没有模拟低电平接线、真实 DMA 寄存器时序或 ARM 上下文保存。

```sh
cmake -S tests/stage8_5_c3 -B /tmp/c3-dma-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c3-dma-debug
ctest --test-dir /tmp/c3-dma-debug -V
cmake -S tests/stage8_5_c3 -B /tmp/c3-dma-o2 -DCMAKE_BUILD_TYPE=RelWithDebInfo '-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -g'
cmake --build /tmp/c3-dma-o2
ctest --test-dir /tmp/c3-dma-o2 -V
```
