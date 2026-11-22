# Stage 8.5-C3 可靠性修复验收

| 项目 | 结论 | 实际依据 |
|---|---|---|
| 长块超时缺陷 | **FIXED — SOFTWARE VERIFIED** | 原 C1 65,535 B 在 28.453 ms 返回 -2，C3 在同模型中 29.129 ms 完成，CNDTR 保持 16 位分块 |
| C3 SPI/DMA 边界 | **PASS** | Debug 和 O2 各 26 条，O2 UBSan 复核通过；包含全双工分块、DMA TE、缺 TC、TC 后 TXE/BSY、RXNE、错误恢复、计数回绕和 RTOS 抢占 |
| 原四任务/五页面 | **PASS** | B1.4 历史四任务应用快照 3/3×2；当前 C3 五页面应用 7/7×2，原绘制、四任务、按键及队列源码与 C1 清单一致 |
| 原完整内核回归 | **PASS** | 2,155/2,155，六组原测试 ID 保持；128 项实际负面链接拒绝，冻结历史固件内容一致 |
| ARM 构建和固件校验 | **PASS** | 五页面 Debug/O2 与共享驱动裸机 Debug/O2 严格构建通过，独立检查 ELF/BIN/HEX/MAP，SHA256 核验通过 |
| 真机、示波器、擦写及复位 | **HARDWARE NOT EXECUTED** | 本轮仅构建与软件验证，C2 历史包和 93 项证据 SHA256 核验通过 |

**总评 SOFTWARE PASS。** 唯一生产修改为 SPI STM32 LL。公共内核、TCB、上下文汇编、SPI 总线公开接口、ST7789/页面以及 RTOS 任务逻辑均保持原版本。修复后的 DMA 预算取实际配置分频和 HCLK/PCLK，TX/RX 共用截止条件。状态机、错误路径与片选归属继续适用。RXNE/OVR 清理的死循环已替换为有界检查，物理板级 RXNE/OVR 异常仍 OPEN。

软件边界记录在 `boundary_test_results.csv`，时间由 72 MHz 寄存器模型导出。旧驱动复现和原始断言的产物独立保留。原内核回归在临时目录用未改动入口 `bash scripts/test_stage8_4a.sh` 执行。首次复制缺历史 fixture，修复后出现路径相关 `__FILE__` 字符串差异。用于历史 BIN 比对的构建通过 `-fmacro-prefix-map` 还原原路径，冻结镜像/断言保持原值。最后补齐原报告的文档依赖，早期失败日志保留。

五页面 Debug/O2 资源分别为 Flash **77,416 B / 58,804 B**，SRAM **41,784 B / 41,776 B**（含 MSP），详见 `memory_report.md`。O2 仅计划页 **0–28**。新版目录 `artifacts/stage8_5_c3/`，旧 C1/C2 保持。

待实测项包括实际 18 MHz SCK、长块物理传输/CS、真实 RXNE/OVR 与 DMA/BSY 故障注入、板上抢占时序、各栈高水位和 30 分钟连续运行。测试仅验证一次 DWT 回绕；长于整圈且无法持续执行的情况无法由当前计数接口精确计时。实际 RX 缓冲受 64 KiB SRAM 限制，超过 65,535 B 的全双工数据已在 Host 验证。后续硬件验收与下一阶段写入授权见 `hardware_test_plan.md`。
