source [find interface/stlink-dap.cfg]
transport select dapdirect_swd
adapter serial 190057000D0000393333574E
adapter speed 1000
reset_config none
source [find target/swj-dp.tcl]
swj_newdap zet6 cpu -irlen 4 -expected-id 0x1ba01477
dap create zet6.dap -chain-position zet6.cpu
target create zet6.memory mem_ap -dap zet6.dap -ap-num 0
gdb_port disabled
tcl_port disabled
telnet_port disabled
init
