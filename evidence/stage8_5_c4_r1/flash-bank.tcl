source evidence/stage8_5_c4_r1/identity.tcl
halt
wait_halt 3000
flash probe 0
set banks [flash list]
echo "BANK_LIST $banks"
if {[llength $banks] != 1} {error "Expected exactly one bank"}
set bank [lindex $banks 0]
if {[dict get $bank base] != 0x08000000 || [dict get $bank size] != 524288 || [dict get $bank driver] ne "stm32f1x"} {
    error "Bank does not exactly cover authorized STM32F1 main flash"
}
set info [flash info 0 sectors]
set geometry [regexp -all -inline {#\s*([0-9]+):\s*0x([0-9a-f]+)\s*\(0x([0-9a-f]+)} $info]
if {[llength $geometry] != 1024} {error "Expected exactly 256 erase sectors"}
set index 0
foreach {whole n off size} $geometry {
    if {$n != $index || [expr 0x$off] != $index*2048 || [expr 0x$size] != 2048} {error "Unexpected page geometry"}
    incr index
}
echo "BANK_GEOMETRY_PASS bank=0 start=0x08000000 end_exclusive=0x08080000 pages=0-255 page_bytes=2048"
