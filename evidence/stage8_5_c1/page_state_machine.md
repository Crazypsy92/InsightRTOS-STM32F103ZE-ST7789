# Stage C1 原状态机审计（修改前）

2026-11-20。实际B1.4唯一四任务源码为本仓库app/stm32f103ze_st7789；DISPLAY_RTOS_Debug/O2 → insightrtos_display目标。用户给出的Cube_MX路径为较早原应用，没有DISPLAY_RTOS构建入口，本轮只读核对，不修改该路径、不改双任务入口。初始B1.4独立校验通过，原包与完整源码冻结于baseline。

原app_lcd_test.c以s_page/s_auto_run/s_ready/s_page_start_ms及s_last_frame_ms管理状态。初始0，前三页各2000ms自动切换，到3动画后永久停留；动画每25ms阈值、200ms换色。短按0→1→2→3→0并禁用自动，长按只翻LED不切页。KEY_GetEvent只在LCD_Test_Task消费一次；RTOS key_port将该接口转交LCD专属staged_event，而ISR→KEY→Controller→LCD命令均为FIFO副本，不是额外业务消费路径。

Controller每10ms检查ACK和按键；ACK包含sequence/page/automatic/ready/page_started_ms/frame_ms/error/refreshes。仅需要切页/动画更新才发NONE命令；LCD收到命令后执行原LCD_Test_Task并delay(1)。初始化原来无限每秒重试，C1必须改为有限尝试并保留RAM诊断。

计划最小修改：同一s_page状态机在RTOS条件编译下PAGE_COUNT=5，加入case4及250ms刷新分支，自动规则仍page<3。原0–3绘制函数完整保持，裸机分支继续4页。欢迎页在成功PortInit之后/首次0页之前由LCD任务调用并rtos_delay约1.5秒，非第二页面状态机。统计用公开snapshot/代数句柄和Tick，并在短临界区复制应用计数；无CPU或上下文切换虚拟数据。

## C1 最终状态转换（已实现）

| 当前 | 触发 | 后续 | 自动模式 |
|---|---|---|---|
| 0/1/2 | 2000ms到期且auto=1 | 1/2/3 | 保持 |
| 3动画 | 无键、25ms阈值 | 原动画更新、仍3 | 不进入4 |
| 0/1/2/3 | 短按 | 1/2/3/4 | 置0 |
| 4 | 短按 | 0 | 保持0 |
| 4 | 无短按、250ms阈值 | 局部状态更新、仍4 | 保持 |
| 任意已就绪页 | 长按 | 原页面，不切换；LED翻转 | 原语义 |
| LCD未就绪 | 输入命令 | 唯一KEY_GetEvent路径消费后拒绝绘制，错误ACK | 不绘图 |

仅RTOS条件编译将PAGE_COUNT改5、增加case4/250ms分支和欢迎调用；原四绘制函数以及动画更新函数逐函数字节一致。裸机条件仍PAGE_COUNT=4，最终BIN与旧冻结裸机相同。s_page、s_auto_run、s_ready和原时间戳仍为唯一页面状态；复用s_last_frame_ms作为当前动态页面时基，不建第二个页面索引。

Controller→LCD命令仍sequence/event/at_ms，LCD→Controller ACK仍sequence/page/auto/ready/page_started/frame/error/refreshes，IPC布局不改。KEY_GetEvent仅LCD适配层消费，UI快照不调用按键接口；正常执行恰好一次消费校验。LCD Task每条命令之后delay1tick，Controller优先级4/KEY5可抢占LCD2。进入欢迎期间的事件不合并；队满时Controller保留pending_key并继续收ACK，序号只在发送成功后提交。

软件95秒模型在第一按键前明确检查自动0→1→2→3且没有进入4；100个独立短按包含4→0，20次进入4；两次长按不切页，LED总102翻转。慢LCD200秒模型触发2224次保留重试，102个事件全部递交；模拟时间不代表实机稳定性时长。
