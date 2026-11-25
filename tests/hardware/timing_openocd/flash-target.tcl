source [find interface/stlink-dap.cfg]
transport select dapdirect_swd
adapter serial 190057000D0000393333574E
source [find target/stm32f1x.cfg]
stm32f1x.cpu configure -event examine-end {}
adapter speed 500
reset_config none
gdb_port disabled
tcl_port disabled
telnet_port disabled
init
