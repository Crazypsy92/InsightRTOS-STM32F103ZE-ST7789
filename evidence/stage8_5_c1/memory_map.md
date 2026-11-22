# Debug/O2实际资源

| 资源（B，子项不重复相加） | Debug | O2 |
|---|---|---|
| flash | 76188 | 57988 |
| data | 144 | 144 |
| bss | 4772 | 4768 |
| user_stacks | 32768 | 32768 |
| active_user_stacks | 16384 | 16384 |
| reserved_unused_user_stacks | 16384 | 16384 |
| idle_stack_in_bss | 512 | 512 |
| msp | 4096 | 4096 |
| heap | 0 | 0 |
| ram_with_alignment | 41784 | 41776 |
| remaining_sram | 23752 | 23760 |

| 配置 | BIN长度 | 最大有效Flash地址 | 2KiB页数 | 页号 | 页范围末端exclusive |
|---|---|---|---|---|---|
| debug | 76188 | 0x0801299B | 38 | 0–37 | 0x08013000 |
| o2 | 57988 | 0x0800E283 | 29 | 0–28 | 0x0800E800 |

Flash总524288B、SRAM总65536B。RAM=DATA+BSS+32768用户栈+4096MSP+对齐；Idle/TCB/Queue/行缓冲/观察结构/UI缓存已在BSS，不重复相加。相对B1.4两配置静态RAM增加416B，主要文本/颜色缓存400B与四个应用计数16B。没有生产framebuffer；预览测试115200B像素数组只存在Host executable，ARM compile_commands无tests来源。

## debug

| 段 | VMA | Size | LMA/写入 |
|---|---|---|---|
| .isr_vector | 0x08000000 | 484 | 与VMA相同 |
| .text | 0x080001E4 | 44584 | 与VMA相同 |
| .rodata | 0x0800B00C | 30960 | 与VMA相同 |
| .ARM | 0x080128FC | 8 | 与VMA相同 |
| .init_array | 0x08012904 | 4 | 与VMA相同 |
| .fini_array | 0x08012908 | 4 | 与VMA相同 |
| .data | 0x20000000 | 144 | 0x0801290C |
| .bss | 0x20000090 | 4772 | NOBITS不写入 |
| .user_stacks | 0x20001338 | 32768 | NOBITS不写入 |
| .msp_stack | 0x2000F000 | 4096 | NOBITS不写入 |

## o2

| 段 | VMA | Size | LMA/写入 |
|---|---|---|---|
| .isr_vector | 0x08000000 | 484 | 与VMA相同 |
| .text | 0x080001E4 | 29024 | 与VMA相同 |
| .rodata | 0x08007344 | 28320 | 与VMA相同 |
| .ARM | 0x0800E1E4 | 8 | 与VMA相同 |
| .init_array | 0x0800E1EC | 4 | 与VMA相同 |
| .fini_array | 0x0800E1F0 | 4 | 与VMA相同 |
| .data | 0x20000000 | 144 | 0x0800E1F4 |
| .bss | 0x20000090 | 4768 | NOBITS不写入 |
| .user_stacks | 0x20001330 | 32768 | NOBITS不写入 |
| .msp_stack | 0x2000F000 | 4096 | NOBITS不写入 |


向量0x08000000，初始MSP0x20010000，MSP区0x2000F000–0x2000FFFF；PSP由每任务静态栈初始化。实际ELF PT_LOAD/VMA/LMA逐段无重叠/越界，.data Flash加载/SRAM运行，NOBITS不产生HEX SRAM写入。单一startup/Reset/SystemInit；SVC/PendSV/SysTick强符号唯一、TIM2唯一；Thumb入口与向量槽一致。VTOR代码沿用B1批准的0地址Flash别名口径，不伪称本轮寄存器测量。

Trace=OFF、RTOS_TESTING=0，软浮点Cortex-M3，无FPU/TCB ABI变化；heap=0，_sbrk始终ENOMEM。无malloc/free/calloc/realloc生产符号、semihost BKPT或Host测试入口。实际nm/readelf/map/编译命令/反汇编在static-debug、static-o2，包ELF/BIN/HEX从同一ELF导出并独立解析比较。
