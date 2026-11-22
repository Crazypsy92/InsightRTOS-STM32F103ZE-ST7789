# SPI/DMA 实机验证

C3 长块超时修复为 **SOFTWARE VERIFIED**。本轮未部署该固件，LCD 初始化、局部/整屏刷新、DMA TC 后 TXE/BSY、错误计数和错误后的 acquire/recover 实机行为均 **NOT TESTED**。真实 ≥65,535 B 传输 **NOT TESTED**，SPI3 SCK **NOT MEASURED**，RXNE/OVR 真机故障恢复仍 **OPEN**。

冻结的五页面固件不提供安全的超长块和故障注入入口。后续可独立批准诊断镜像，在隔离的参考从机或回环夹具上以 Flash 中的静态图案执行 65,535 B、65,536 B 和多块 TX，记录 SCK/CS、DMA CNDTR、末尾 TXE/BSY、错误码及下一次 acquire。双工回环按板上 SRAM 容量设定长度。本轮没有构建或运行该镜像。
