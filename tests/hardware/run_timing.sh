#!/usr/bin/env bash
set -euo pipefail

# Run at the application repository root. Previous must be an independently
# validated snapshot of the authorized pages, never a just-read substitute.
previous=$(realpath "$1")
scratch=$(realpath "$2")
mode=${3:-both}
evidence=evidence/stage9_3_b2b
case "$mode" in
    Msp) evidence=evidence/stage9_3_b2c ;;
    Integrated) evidence=evidence/stage9_4_a ;;
    Mutex) evidence=evidence/stage9_4_b ;;
    Backpressure) evidence=evidence/stage9_4_c ;;
    Semaphore) evidence=evidence/stage9_4_d ;;
esac
log="$evidence/logs"
if [[ $mode == Mutex ]]; then log="$log/hardware"; fi
log=$(realpath "$log")
cfg=tests/hardware/timing_openocd
if ps -eo stat=,comm= | awk '$1 !~ /^Z/ {print $2}' | rg -x 'openocd|st-util|pyocd|gdb-multiarch|stlink-server'; then
    exit 1
fi
exec 9>/tmp/insightrtos-zet6-stlink.lock
flock -n 9

case "$mode" in
    both) profiles=(Off On) ;;
    On) profiles=(On) ;;
    Msp) profiles=(Msp) ;;
    Integrated) profiles=(Integrated IntegratedProd) ;;
    Mutex) profiles=(Mutex IntegratedProd) ;;
    Backpressure) profiles=(Backpressure IntegratedProd) ;;
    Semaphore) profiles=(Semaphore IntegratedProd) ;;
    *) exit 2 ;;
esac

for name in "${profiles[@]}"; do
    profile=DISPLAY_RTOS_O2_Timing_$name
    case "$name" in
        Msp) profile=DISPLAY_RTOS_O2_Msp ;;
        Integrated) profile=DISPLAY_RTOS_O2_Integrated_Stress ;;
        IntegratedProd) profile=DISPLAY_RTOS_O2_Integrated ;;
        Mutex) profile=DISPLAY_RTOS_O2_Mutex ;;
        Backpressure) profile=DISPLAY_RTOS_O2_Backpressure ;;
        Semaphore) profile=DISPLAY_RTOS_O2_Semaphore ;;
    esac
    if [[ ( $mode == Mutex || $mode == Backpressure || $mode == Semaphore ) && $name == IntegratedProd ]]; then profile=Stage94A_Frozen_Production; fi
    f=$(realpath "build/$profile/insightrtos_display.bin")
    bin="${f%.bin}"
    proof="$log/firmware-audit.json"
    python3 - "$proof" "$f" "$profile" <<'PY'
import hashlib,json,sys
from pathlib import Path
proof,name,profile=sys.argv[1:]
p=json.loads(Path(proof).read_text())[profile]
b=Path(name).read_bytes()
assert 484 < len(b) <= 63488 and len(b)==p["flash_bytes"]
assert hashlib.sha256(b).hexdigest()==p["sha256"]["bin"]
PY
    prefix="$log/$name"
    tmp="$scratch/$name"
    test ! -e "$prefix-erase.log"
    # Halt before inspecting Flash. Live MEM-AP reads can return
    # inconsistent Flash words while the CPU and DMA are active.
    openocd -f "$cfg/flash-target.tcl" -f "$cfg/identity.tcl" \
      -c 'halt; wait_halt 3000' \
      -c "dump_image $tmp-before.bin 0x08000000 0xf800" \
      -c "dump_image $tmp-ob-before.bin 0x1ffff800 16; shutdown" > "$prefix-preflight.log" 2>&1
    rg -q '^IDENTITY_PASS' "$prefix-preflight.log"
    cmp "$previous" "$tmp-before.bin"
    openocd -f "$cfg/flash-target.tcl" -f "$cfg/flash-bank.tcl" \
      -c 'flash erase_sector 0 0 30' \
      -c 'for {set addr 0x08000000} {$addr < 0x0800f800} {incr addr 2048} {foreach word [read_memory $addr 32 512] {if {$word != 0xffffffff} {error "Blank check failed"}}}' \
      -c 'echo BLANK_VERIFIED_63488' \
      -c "flash write_bank 0 $f 0" -c 'echo WRITE_COMPLETE; shutdown' > "$prefix-erase.log" 2>&1
    rg -q '^BANK_GEOMETRY_PASS' "$prefix-erase.log"
    rg -q '^BLANK_VERIFIED_63488$' "$prefix-erase.log"
    rg -q '^WRITE_COMPLETE$' "$prefix-erase.log"
    openocd -f "$cfg/memap.cfg" -f "$cfg/identity.tcl" \
      -c "dump_image $tmp-after.bin 0x08000000 0xf800" \
      -c "dump_image $tmp-ob-after.bin 0x1ffff800 16; shutdown" > "$prefix-readback.log" 2>&1
    rg -q '^IDENTITY_PASS' "$prefix-readback.log"
    python3 - "$tmp" "$f" <<'PY' > "$prefix-readback.json"
import hashlib,json,sys
from pathlib import Path
tmp,f=sys.argv[1:]
b=Path(f).read_bytes()
obs=Path(tmp+"-after.bin").read_bytes()
options=Path(tmp+"-ob-before.bin").read_bytes()
assert obs==b.ljust(63488,b"\xff") and options==Path(tmp+"-ob-after.bin").read_bytes()
print(json.dumps({"status":"PASS","sha256":hashlib.sha256(b).hexdigest(),"bytes":len(b),
                  "tail_ff":63488-len(b),"option_bytes_equal":True}, indent=2))
PY
    printf '%s readback verified\n' "$name"
    openocd -f "$cfg/flash-target.tcl" -f "$cfg/identity.tcl" \
      -c 'cortex_m reset_config sysresetreq; reset run; sleep 125; echo RESET_COMMAND_COMPLETE; shutdown' \
      > "$prefix-reset.log" 2>&1
    rg -q '^RESET_COMMAND_COMPLETE$' "$prefix-reset.log"
    openocd -f "$cfg/live.cfg" > "$prefix-live.log" 2>&1 &
    pid=$!
    trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
    for _ in {1..25}; do
        if rg -q '^IDENTITY_PASS' "$prefix-live.log"; then break; fi
        kill -0 "$pid"
        sleep .2
    done
    rg -q '^IDENTITY_PASS' "$prefix-live.log"
    rt=0
    if [[ $name == On ]]; then rt=1; fi
    sleep 1
    if [[ $name == Msp ]]; then
        PYTHONDONTWRITEBYTECODE=1 python3 tests/hardware/read_msp.py --elf "$bin.elf" \
          --out "$prefix-watermark.json" | tee "$prefix-sampler.log"
    elif [[ $name == Mutex ]]; then
        PYTHONDONTWRITEBYTECODE=1 python3 tests/hardware/read_mutex.py --elf "$bin.elf" \
          --out "$prefix-fixture.json" | tee "$prefix-sampler.log"
    elif [[ $name == Backpressure ]]; then
        PYTHONDONTWRITEBYTECODE=1 python3 tests/hardware/read_backpressure.py --elf "$bin.elf" \
          --out "$prefix-fixture.json" | tee "$prefix-sampler.log"
    elif [[ $name == Semaphore ]]; then
        PYTHONDONTWRITEBYTECODE=1 python3 tests/hardware/read_semaphore.py --elf "$bin.elf" \
          --out "$prefix-fixture.json" | tee "$prefix-sampler.log"
    elif [[ $name == Integrated || $name == IntegratedProd ]]; then
        PYTHONDONTWRITEBYTECODE=1 python3 tests/hardware/read_integrated.py --elf "$bin.elf" \
          --profile "$name" --out "$prefix-diag.json" | tee "$prefix-sampler.log"
    else
        PYTHONDONTWRITEBYTECODE=1 python3 tests/hardware/read_timing.py --rt "$rt" --elf "$bin.elf" \
          --out "$prefix-timing.json" | tee "$prefix-sampler.log"
    fi
    kill "$pid"
    wait "$pid" || true
    trap - EXIT
    previous="$tmp-after.bin"
done
