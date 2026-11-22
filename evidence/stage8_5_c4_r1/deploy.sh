#!/usr/bin/env bash
set -euo pipefail
ev=evidence/stage8_5_c4_r1
bin=artifacts/stage8_5_c3/o2/insightrtos_zet6_c3_o2.bin
test ! -e "$ev/deployment-started"
exec 9>/tmp/insightrtos-zet6-stlink.lock
flock -n 9
check_probe_ownership() {
    if ps -eo comm= | rg -x 'openocd|st-util|stlink-server|gdb-multiarch|STM32_Programmer|STM32_Programme' ; then
        echo 'Unexpected debug process'
        exit 1
    fi
}
run() {
    local step=$1
    check_probe_ownership
    echo "=== $step ==="
    if ! timeout 150 openocd -f "$ev/$step.cfg" > "$ev/$step.log" 2>&1; then
        tail -n 28 "$ev/$step.log"
        exit 1
    fi
    tail -n 18 "$ev/$step.log"
    if rg '^(Error|Runtime Error):' "$ev/$step.log"; then exit 1; fi
    rg -q 'IDENTITY_PASS' "$ev/$step.log"
    rg -q 'shutdown command invoked' "$ev/$step.log"
}
test "$(wc -c < "$bin")" -eq 58804
printf '%s  %s\n' bf85e29f09ae20a2fec28fc106e1a8cc6614b47af379702369595c0789e460dc "$bin" | sha256sum -c
run preflight
awk '/Target voltage:/ {if ($NF>=3.0 && $NF<=3.6) ok=1} END{exit !ok}' "$ev/preflight.log"
cmp "$ev/option_bytes_before.bin" evidence/stage8_5_c4/option_bytes_before.bin
date -u +%FT%TZ > "$ev/deployment-started"
run erase
rg -q 'ERASE_COMMAND_PASS' "$ev/erase.log"
run blank-check
cmp "$ev/option_bytes_before.bin" "$ev/option_bytes_after_erase.bin"
python3 - "$ev" <<'PY'
import hashlib,sys
from pathlib import Path
d=Path(sys.argv[1])/'blank_full_flash.bin'
b=d.read_bytes()
if b!=b'\xff'*524288:
    raise SystemExit(f'Independent main Flash blank check failed, length={len(b)}')
print('BLANK_CHECK_PASS',len(b),hashlib.sha256(b).hexdigest())
PY
printf '%s  %s\n' bf85e29f09ae20a2fec28fc106e1a8cc6614b47af379702369595c0789e460dc "$bin" | sha256sum -c
run program
rg -q 'PROGRAM_COMMAND_PASS' "$ev/program.log"
run readback
python3 - "$ev" "$bin" <<'PY'
import hashlib,json,sys
from pathlib import Path
e=Path(sys.argv[1]); b=Path(sys.argv[2]).read_bytes()
before=(e/'option_bytes_before.bin').read_bytes()
after=(e/'option_bytes_after.bin').read_bytes()
blank=(e/'blank_full_flash.bin').read_bytes()
actual=(e/'flash_after.bin').read_bytes()
sha=lambda x:hashlib.sha256(x).hexdigest()
status={
 'status':'PASS' if len(actual)==524288 and actual[:58804]==b and actual[58804:]==b'\xff'*(524288-58804) and before==after and len(before)==16 else 'FAIL',
 'main_flash_range':['0x08000000','0x08080000'],'erase_pages':[0,255],
 'blank_main_flash_bytes':len(blank),'blank_main_flash_sha256':sha(blank),
 'blank_full_flash_all_ff':blank==b'\xff'*524288,'programmed_bin_bytes':len(b),
 'expected_bin_sha256':sha(b),'readback_bin_sha256':sha(actual[:58804]),
 'readback_bin_equal':actual[:58804]==b,'full_readback_bytes':len(actual),
 'full_readback_sha256':sha(actual),'tail_start':'0x0800E5B4','tail_bytes':len(actual[58804:]),
 'tail_all_ff':actual[58804:]==b'\xff'*(524288-58804),
 'option_bytes_before':before.hex(),'option_bytes_after':after.hex(),'option_bytes_equal':before==after,
 'prior_firmware_backup':False}
(e/'readback_verification.json').write_text(json.dumps(status,indent=2)+'\n')
print(json.dumps(status,indent=2))
if status['status']!='PASS': raise SystemExit(1)
PY
echo "DEPLOY_PASS"
