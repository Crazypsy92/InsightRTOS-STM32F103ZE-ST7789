# C2 板载RESET

PASS（实际计数复位/重新推进 + USER CONFIRMATION）。首次用户RESET由硬件断点捕获Reset_Handler，验证MSP与启动链；随后第二次无RESET捕获/无GDB停顿的板载RESET用于欢迎和五页观察。

30分钟和栈快照完成后解除halt、GDB断开，用户再次按板载RESET。observe_final_reset.py只读观察，未发送reset/halt/resume或写入；记录kernel_tick/TIM2从旧计数归零并再次增长、LCD ready恢复和guard有效。用户目视结果见user-reset-confirmations.json。OpenOCD保留只读观测连接，未宣称完全断电冷启动或拔除ST-Link启动；该两项NOT EXECUTED。

USER CONFIRMATION REQUIRED：已取得最终复位/屏幕确认，不将GDB continue称为物理RESET。
