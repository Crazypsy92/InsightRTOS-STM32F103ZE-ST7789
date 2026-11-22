# C4 实机身份核验

**HARDWARE VERIFIED**。两次独立 MEM-AP 连接均使用 ST-Link `190057000D0000393333574E`，固件 V2J45S7，VID:PID 0483:3748。目标电压分别为 3.274900 V 和 3.281275 V。

| 项目 | 两次读值 |
|---|---|
| MCU IDCODE | `0x10036414` |
| CPUID | `0x411FC231` |
| UID 原始字节序 | `31ffd8053031533628791143` |
| Flash 容量 | 512 KiB |
| Option Bytes | `a55aff00ff00ff00ff00ff00ff00ff00` |
| OBR / WRPR | `0x03FFFFFC` / `0xFFFFFFFF` |

芯片身份和保护状态与 C2 一致。两次均未停止或复位目标。C4 未执行用于擦除前准入的 Flash 驱动页表复核，因为固件内容已触发停止条件。原始连接日志及寄存器和 Option Bytes 字节快照同目录保存。
