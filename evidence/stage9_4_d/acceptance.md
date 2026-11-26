# Stage 9.4-D ZET6 Semaphore 实机验收

**PASS**。从 main 基线 `c0bba0bb660eeea1f95aeafa75b012e30a99cab1` 构建的独立 O2 夹具，在既有 STM32F103ZET6 上执行全部场景。板上运行从 Tick 0 到 34（约 340 ms）；最终条件及全部状态由主机独立读取两遍并复核。在结束态取得一次健康快照，五个栈 guard 完整，CFSR/HFSR、断言行与停机状态无异常。

| 场景 | 结果和板上证据 |
|---|---|
| 真实 TIM2 → PendSV → 高优先级 Thread | 6/6。IRQ IPSR 44，Give 均为 RTOS_OK。每次 High 从 BLOCKED 转 READY，等待头由 High 清零、count 0→0、PendSV 置 pending。ISR 内 take 次数不变，之后 High 在 IPSR=0 的 Thread 中成功返回，Trace 与真实 Tick 对应。 |
| 多等待者优先级与同级 FIFO | Low P2、High P5、Peer P5 依次入队。实际取出为 High → Peer → Low，Tick 23 / 24 / 25；Trace 槽 1 / 2 / 0。最终等待链清空。 |
| 计数与二值边界 | 计数初值 1 可消费，归零后连续 Give 到 3。再 Give 返回 FULL 且 count 保持 3，三次 Take 恢复为 0。二值第二个 Give 在 count=1 时返回 FULL。Thread 误用 FromISR 返回 CONTEXT。 |
| deadline、超时和恢复 | 第六次 TIM2 Give 在 Tick 19、期限 20，High 当 Tick 获得许可，后续无多余许可。独立超时在精确 Tick 30 返回 TIMEOUT，无残留等待；后续 Give/Take 为 0→1→0。Peer 取消后在 Tick 32 返回 CANCELLED。 |

22 条返回值与计数检查通过。各 Give 只有一次许可转移。最终 Semaphore count=0、wait_head=NULL，未观察到重复或遗失许可。完整原始帧保存为十六进制字节和解码 JSON。

## 软件及恢复

- 相关 Host Semaphore/IPC 用例 85/85，应用和 RT 各 9/9，历史 Job 77 项和 DWT 53 项通过。未修改生产内核，未重复 360 项全量回归。
- ARM Debug/O2 夹具及正式 O2 均构建成功。测试 O2 27,960 B、Debug 41,316 B；两者静态 SRAM 44,576 B（含 4 KiB MSP）。TCB 72 B、heap 0、原用户栈容量保持。正式构建无 Semaphore 测试入口或状态。
- 最初 SWD 握手失败时没有擦写。重新插紧 SWD 排线后，身份、512 KiB Flash 几何、旧镜像和保护检查全部通过。仅操作主 Flash 页 0–30，两套镜像均独立读回，FF 尾部与 Option Bytes 前后一致。
- 已恢复冻结正式 DISPLAY_RTOS_O2_Integrated（53,676 B），63,488 B 授权区间独立读回与测试前逐字节一致。复位后正式 Monitor 的综合快照序号 4→8，生产健康检查通过。
- 未执行 Debug 实机、指令级全部截止竞争、物理 IRQ 延迟或长稳测试。

[验证规约](semaphore_contract.md) · [实机解码](logs/Semaphore-fixture.json) · [原始结构](logs/Semaphore-fixture.raw.hex) · [正式镜像读回](logs/IntegratedProd-readback.json)
