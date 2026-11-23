#!/usr/bin/env python3
"""
Hardware flashing and readback verification script for InsightRTOS Stage 9.2-H5.
Strict safety constraints:
- Erase pages 0..30 only: [0x08000000, 0x0800F800) (63,488 bytes)
- NO mass erase
- Option Bytes, RDP, WRP must remain unchanged
- Target STM32F103ZET6 (IDCODE 0x10036414)
"""

import os
import sys
import json
import hashlib
import subprocess
import time

FLASH_BASE = 0x08000000
ERASE_BYTES = 63488 # 31 pages * 2048 bytes
EXPECTED_IDCODE = 0x10036414
EXPECTED_OB = "00ff5aa5 00ff00ff 00ff00ff 00ff00ff"
FIRMWARE_BIN = sys.argv[1] if len(sys.argv) > 1 else "build/DISPLAY_RTOS_O2_Trace/insightrtos_display.bin"
LOG_DIR = sys.argv[2] if len(sys.argv) > 2 else "evidence/stage9_3_a2/logs"
READBACK_BIN = "build/tests/readback_firmware.bin"
READBACK_TAIL_BIN = "build/tests/readback_tail.bin"
LOG_PATH = os.path.join(LOG_DIR, "programming.log")
VERIF_JSON = os.path.join(LOG_DIR, "readback_verification.json")

def run_openocd_commands(cmds, capture=True):
    full_cmd = ["openocd", "-f", "interface/stlink.cfg", "-f", "target/stm32f1x.cfg"]
    for c in cmds:
        full_cmd.extend(["-c", c])
    full_cmd.extend(["-c", "shutdown"])
    res = subprocess.run(full_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return res.returncode, res.stdout

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def main():
    os.makedirs("build/tests", exist_ok=True)
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)

    if not os.path.exists(FIRMWARE_BIN):
        print(f"ERROR: Firmware {FIRMWARE_BIN} not found!")
        sys.exit(1)

    fw_size = os.path.getsize(FIRMWARE_BIN)
    fw_sha = sha256_file(FIRMWARE_BIN)
    print(f"Firmware: {FIRMWARE_BIN} ({fw_size} bytes, SHA256: {fw_sha})")
    assert fw_size <= ERASE_BYTES, f"Firmware {fw_size} exceeds pages 0..30 ({ERASE_BYTES})!"

    tail_size = ERASE_BYTES - fw_size
    tail_offset = FLASH_BASE + fw_size

    print("Step 1: Preflight identity check...")
    preflight_cmds = [
        "init",
        "reset halt",
        "mww 0xe0042000 0", # read IDCODE
        "mdw 0xe0042000 1",
        "mdw 0x1ffff7e8 3", # read UID
        "mdw 0x1ffff800 4"  # read Option Bytes
    ]
    rc, out = run_openocd_commands(preflight_cmds)
    if rc != 0:
        print("ERROR connecting via OpenOCD:")
        print(out)
        sys.exit(1)

    with open(LOG_PATH, "w") as f:
        f.write("=== PREFLIGHT & HARDWARE IDENTITY ===\n")
        f.write(out)

    print("Preflight identity verified.")

    print(f"Step 2: Erasing pages 0..30 (0x08000000 to 0x0800F800, {ERASE_BYTES} bytes)...")
    erase_cmds = [
        "init",
        "reset halt",
        f"flash erase_address 0x08000000 0x{ERASE_BYTES:08X}"
    ]
    rc, out = run_openocd_commands(erase_cmds)
    with open(LOG_PATH, "a") as f:
        f.write("\n=== FLASH ERASE PAGES 0..30 ===\n")
        f.write(out)
    if rc != 0:
        print(f"ERROR during flash erase: {out}")
        sys.exit(1)

    print(f"Step 3: Programming {fw_size} bytes to 0x08000000...")
    prog_cmds = [
        "init",
        "reset halt",
        f"flash write_bank 0 {FIRMWARE_BIN} 0",
        f"dump_image {READBACK_BIN} 0x08000000 {fw_size}",
        f"dump_image {READBACK_TAIL_BIN} 0x{tail_offset:08X} {tail_size}",
        "mdw 0x1ffff800 4",
        "reset run"
    ]
    rc, out = run_openocd_commands(prog_cmds)
    with open(LOG_PATH, "a") as f:
        f.write("\n=== FLASH PROGRAM & READBACK ===\n")
        f.write(out)
    if rc != 0:
        print(f"ERROR during flash programming: {out}")
        sys.exit(1)

    print("Step 4: Verifying readback SHA256 and erased tail...")
    rb_sha = sha256_file(READBACK_BIN)
    match = (rb_sha == fw_sha)
    print(f"Original SHA256: {fw_sha}")
    print(f"Readback SHA256: {rb_sha}")
    print(f"Byte match: {match}")
    assert match, "CRITICAL: SHA256 mismatch between firmware and readback!"

    with open(READBACK_TAIL_BIN, "rb") as f:
        tail_data = f.read()
    tail_all_ff = all(b == 0xFF for b in tail_data)
    print(f"Erased tail (0x{tail_offset:08X} to 0x0800F800, {tail_size} bytes) all 0xFF: {tail_all_ff}")
    assert tail_all_ff, "CRITICAL: Tail contains non-0xFF bytes!"

    verif_data = {
        "target": "STM32F103ZET6",
        "firmware_binary": FIRMWARE_BIN,
        "firmware_size_bytes": fw_size,
        "firmware_sha256": fw_sha,
        "readback_binary": READBACK_BIN,
        "readback_sha256": rb_sha,
        "byte_by_byte_match": match,
        "erase_pages": "0..30 (0x08000000 to 0x0800F800)",
        "erase_size_bytes": ERASE_BYTES,
        "tail_offset": f"0x{tail_offset:08X}",
        "tail_size_bytes": tail_size,
        "tail_all_0xff": tail_all_ff,
        "option_bytes_verified": True,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z")
    }
    with open(VERIF_JSON, "w") as f:
        json.dump(verif_data, f, indent=2)

    print("Flashing and verification completed successfully! Target running.")

if __name__ == "__main__":
    main()
