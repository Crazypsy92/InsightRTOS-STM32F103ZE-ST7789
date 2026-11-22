# 剩余边界

- **NOT TESTED**：65,535 B 和跨块真实 DMA。冻结五页应用缺安全诊断入口。以后可独立批准带 Flash 静态测试数据的诊断固件，使用回环或参考从机采集 SCK、CS、CNDTR、TC/TE 及恢复结果。
- **OPEN**：真实 RXNE/OVR 故障、DMA TE/BSY 注入、故障后的 acquire/recover。普通路径未观察到传输错误或恢复调用。
- **NOT TESTED**：同一 DMA 事务正在等待时恰被高优先级任务抢占的相关时序。已验证真实调度及显示负载下的任务推进。
- **NOT MEASURED**：实体 SCK 和 MSP 高水位。18 MHz 是寄存器推导值。
- **OPEN**：初始 950 kHz SWD 读取曾失败，原因未确定。日志保留；改用 100 kHz 纯 MEM-AP 后，独立 60 秒窗口和后续读数通过。
