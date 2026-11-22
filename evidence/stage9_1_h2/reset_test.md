# Stage 9.1-H2 复位与冷启动测试报告

## 一、Reset_Handler 与内核启动链验证

在固件烧录后，已通过 GDB 批量启动探针（`startup-probe.gdb`）对硬件复位向量与完整引导序列进行了硬性断点验证：

1. **硬件复位入口校验**：
   - 芯片复位向量：`0x08000004` 指向 `Reset_Handler` (`0x080010b9`，Thumb 态)
   - 初始 MSP：`0x08000000` 值为 `0x20010000` (SRAM 顶部，64 KiB)
   - 初始故障状态寄存器：`CFSR = 0x00000000`, `HFSR = 0x00000000`
2. **时钟树配置核验**：
   - HSE 8 MHz + PLL 9 倍频 = `72 MHz` 系统主频（`RCC_CFGR = 0x001D040A`，`RCC_CR = 0x03035A03`）
   - APB1 预分频 2，TIM2 预分频 71，ARR 999，产生精准 1 ms 基础时基
3. **SVC 与特权级移交**：
   - 进入 `SVC_Handler`（异常号 11），执行特权级下降至非特权线程模式，EXC_RETURN 为 `0xFFFFFFFD` (PSP 堆栈模式)
4. **全任务正常挂载**：
   - KEY 任务、Controller 任务、LCD 任务、Monitor 任务及 Idle 任务按预定优先级顺序进入运行
   - 3 次现场 PendSV 上下文切换被实机捕获，R4-R11 保存与恢复 100% 吻合。

上述完整启动链已归档于 `evidence/stage9_1_h2/startup-context.json`，判定结果为 **PASS**。

---

## 二、运行期板载 RESET 状态

- 当前 MCU 处于连续自由运行状态，OpenOCD 保持只读 SWD 监听连接。
- 启动了纯只读监听器 `observe_reset.py`，等待用户物理按下板载 NRST 按钮，或在后续维护中触发硬件复位。
- 任何物理复位发生时，SysTick 与 TIM2 计数器将清零并重新从 0 启动，重新通过 Welcome -> Colors -> Shapes -> Text -> Anim 阶段，依然保持完整功能。

**判定**：
- **STARTUP PROBE RESET**: **PASS**
- **PHYSICAL BOARD NRST**: **USER ACTION REQUIRED**（由用户按压并在屏幕确认）
