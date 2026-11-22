# Stage 9.1-H1 实机部署与硬件验收计划 (Hardware Test Plan)

## 一、授权与安全前提 (Prerequisites & Authorization Rules)

> [!IMPORTANT]
> **守则重申**：本阶段（Stage 9.1-H1）严格限定为软件验证与新版固件打包。**未经用户针对本固件版本、目标设备型号与精确擦除范围的明确授权，严禁擅自执行 OpenOCD / J-Link 擦除、烧录或硬件复位。**

---

## 二、部署目标与固件规格 (Target Specifications)

- **目标芯片**: STM32F103ZET6 (High-Density, 512 KB Flash, 64 KB SRAM)
- **显示外设**: 2.0 寸 ST7789V LCD (240×240 RGB565, SPI3 + DMA2 Channel 2)
- **推荐部署固件**:
  - 文件: `artifacts/stage9_1_h1/insightrtos_display_o2_trace.bin`
  - 大小: 63,384 字节 (31 页)
  - 烧录起始地址: `0x08000000`
  - 精确擦除范围: `0x08000000` - `0x0800F800` (Pages 0..30)
  - SHA256 校验码: `aa861203b2027b06f088c6be1562ce33803d1485bfa7befa712fc5fac394b91a`

---

## 三、部署流程与命令序列 (Deployment Procedure)

取得明确用户授权后，执行标准烧录步骤：

1. **连接与身份预检 (Preflight Check)**：
   - 检查 OpenOCD ST-Link 连接状态。
   - 读取 Core ID 与 Device ID (0x10016414)。
   - 检查 Option Bytes（防写保护与读保护状态）。
2. **有界扇区擦除 (Bounded Sector Erase)**：
   - 仅擦除 Page 0 至 Page 30（共 31 页，62 KB），保留 Flash 高地址历史数据与配置。
3. **固件写入 (Programming)**：
   - 将 `insightrtos_display_o2_trace.bin` 写入 `0x08000000`。
4. **逐字节回读核验 (Readback Verification)**：
   - 读回 `0x08000000` 至 `0x0800F798` 的 63,384 字节数据，对比 SHA256，确保 Flash 编程 100% 一致。
5. **系统软复位与启动监测 (Reset & Boot)**：
   - 执行 `reset run`，进入硬件验收环节。

---

## 四、实机功能验收测试用例 (Hardware Acceptance Checklist)

| 序号 | 验证项目 (Item) | 预期现象与验收指标 (Expectation) | 判定方法 |
|---|---|---|---|
| **HW-01** | **冷启动与欢迎页** | 目标板复位后，ST7789 背光点亮，显示 Welcome 欢迎页（包含 InsightRTOS 标识），停留 1.5 秒后自动切入 Page 0（Color Bar） | 目测 + 串口/定时日志 |
| **HW-02** | **原前五页面巡回** | 自动依次播放 Page 0（颜色条）、Page 1（图形）、Page 2（字体文本），进入 Page 3（动画）后停留在动画帧 | 目测无花屏、白屏或撕裂 |
| **HW-03** | **KEY1 翻页流转** | 短按 KEY1 按键，页面依次切换：动画 -> RTOS Tasks (页 4) -> SCHED TRACE (页 5) -> Color (页 0)，完成 6 页面循环闭环 | 物理按键触发，100% 响应 |
| **HW-04** | **第 6 页诊断展示** | 进入 Page 5 (SCHED TRACE)：<br>1. Row 0 显示绿色 `DIAG: ACTIVE (128 REC)`<br>2. Row 1 `Seq` 随调度持续递增，`Ovr` 显示覆盖数<br>3. Row 3 准确显示 `Class`（`HIGHER_PREEMPT` 亮青色，`YIELD` 黄色等）<br>4. Row 4/5 准确解析 `KEY`、`Controller`、`LCD`、`Monitor` 任务名称与代次<br>5. Row 7/8 明确显示免责警示行 | 目测屏幕排版、字迹清晰、色彩准确 |
| **HW-05** | **250ms 定期刷新** | 停留在 Page 5 时，画面以约 4 Hz 频率动态更新最新事件，画面平稳流畅，无闪烁、无总线挂死 | 持续观察 30 秒 |
| **HW-06** | **多任务并发协同** | 在 LCD 处于第 6 页动态刷新期间，KEY 任务按键仍然秒级响应，Controller 与 Monitor 任务持续累加，无调度饥饿 | 按键测试 + Monitor 计数检查 |
| **HW-07** | **60 秒稳定性自由运行** | 设备保持运行 60 秒以上，零 HardFault，零断言报错，主栈与任务栈未触碰水位线 | GDB 挂接探查或运行时监控 |
| **HW-08** | **板载 RESET 按钮复位** | 按下开发板上的红色物理 RESET 键，MCU 瞬间复位并重新成功走通 HW-01 至 HW-04 | 物理按键实测 |
