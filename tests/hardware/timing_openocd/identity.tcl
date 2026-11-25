set id [lindex [read_memory 0xE0042000 32 1] 0]
set cpuid [lindex [read_memory 0xE000ED00 32 1] 0]
set capacity [lindex [read_memory 0x1FFFF7E0 16 1] 0]
if {$id != 0x10036414 || $cpuid != 0x411fc231 || $capacity != 512} {error "Identity/capacity mismatch"}
foreach actual [read_memory 0x1FFFF7E8 32 3] expected {0x05d8ff31 0x36533130 0x43117928} {
    if {$actual != $expected} {error "UID mismatch"}
}
set obr [lindex [read_memory 0x4002201C 32 1] 0]
set wrpr [lindex [read_memory 0x40022020 32 1] 0]
if {$obr != 0x03fffffc || $wrpr != 0xffffffff} {error "Protection mismatch"}
foreach actual [read_memory 0x1FFFF800 16 8] expected {0x5aa5 0x00ff 0x00ff 0x00ff 0x00ff 0x00ff 0x00ff 0x00ff} {
    if {$actual != $expected} {error "Option Bytes differ from recorded baseline"}
}
echo [format "IDENTITY_PASS IDCODE=0x%08X CPUID=0x%08X FLASH_KIB=%d UID=31ffd8053031533628791143 OBR=0x%08X WRPR=0x%08X" $id $cpuid $capacity $obr $wrpr]
