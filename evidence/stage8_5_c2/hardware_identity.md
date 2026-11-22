# C2 实机身份（实际只读采集）

ST-Link 190057000D0000393333574E，固件V2J45S7 (API v2) VID:PID 0483:3748，供电3.272727 V。IDCODE=0x10036414，CPUID=0x411fc231，UID(raw)=31ffd8053031533628791143，Flash=512KiB。与B1.3设备记录一致。Option Bytes=a55aff00ff00ff00ff00ff00ff00ff00，RDP未启用/WRP无保护，OBR/WRPR原始值见identity.log。

计划范围[0x08000000,0x0800E800)已完整只读取得59392B，不是512KiB全片备份；新增页20–28全FF。实际flash probe重新确认256×2KiB页几何，29页精确擦除并全FF检查通过。没有Mass Erase/解锁/Option Bytes修改。
