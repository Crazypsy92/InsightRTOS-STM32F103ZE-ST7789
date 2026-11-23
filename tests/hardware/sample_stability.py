#!/usr/bin/env python3
"""
60-second hardware stability sampling script for InsightRTOS Stage 9.2-H5.
Collects 13 samples at 5-second intervals over 60 seconds:
- TIM2 1000 Hz millisecond tick counter
- RTOS 100 Hz kernel tick counter
- Task run counters (KEY, CTRL, LCD, MON)
- LCD refresh counter (~3.8 Hz)
- Zero error verification (transport, init, overflows)
- PC / xPSR registers
- RAM console buffer (15 rows text)
"""

import os
import sys
import json
import time
import subprocess
import struct

STAT_ADDR = 0x20000728
TIM2_TICK_ADDR = 0x200006f4
KERNEL_TICK_ADDR = 0x2000296c
PREV_CONS_ADDR = 0x20001198
OUTPUT_JSON = "evidence/stage9_3_a/logs/stability_60s.json"
SAMPLES_JSONL = "evidence/stage9_3_a/logs/stability_60s_samples.jsonl"

def sample_target():
    # Read core registers, TIM2 tick, kernel tick, stats, and console buffer
    cmds = [
        "init",
        "halt",
        "reg pc",
        "reg xPSR",
        f"mdw 0x{TIM2_TICK_ADDR:08X} 1",
        f"mdw 0x{KERNEL_TICK_ADDR:08X} 1",
        f"mdw 0x{STAT_ADDR:08X} 16", # first 64 bytes of stats
        f"dump_image build/tests/screen_ram.bin 0x{PREV_CONS_ADDR:08X} 960",
        "resume",
        "shutdown"
    ]
    full_cmd = ["openocd", "-f", "interface/stlink.cfg", "-f", "target/stm32f1x.cfg"]
    for c in cmds:
        full_cmd.extend(["-c", c])
    res = subprocess.run(full_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return res.returncode, res.stdout

def parse_openocd_output(out):
    pc = None
    xpsr = None
    tim2 = None
    kernel_tick = None
    stats_words = []

    for line in out.splitlines():
        line = line.strip()
        if line.startswith("pc (/32):"):
            pc = line.split(":")[1].strip()
        elif line.startswith("xPSR (/32):"):
            xpsr = line.split(":")[1].strip()
        elif f"0x{TIM2_TICK_ADDR:08x}:" in line.lower():
            parts = line.split(":")
            if len(parts) > 1:
                tim2 = int(parts[1].strip().split()[0], 16)
        elif f"0x{KERNEL_TICK_ADDR:08x}:" in line.lower():
            parts = line.split(":")
            if len(parts) > 1:
                kernel_tick = int(parts[1].strip().split()[0], 16)
        elif f"0x{STAT_ADDR:08x}:" in line.lower():
            parts = line.split(":")
            if len(parts) > 1:
                for w in parts[1].strip().split():
                    stats_words.append(int(w, 16))
        elif stats_words and len(stats_words) < 16 and (f"0x{STAT_ADDR+16:08x}:" in line.lower() or
                                                        f"0x{STAT_ADDR+32:08x}:" in line.lower() or
                                                        f"0x{STAT_ADDR+48:08x}:" in line.lower()):
            parts = line.split(":")
            if len(parts) > 1:
                for w in parts[1].strip().split():
                    stats_words.append(int(w, 16))

    screen_text = []
    if os.path.exists("build/tests/screen_ram.bin"):
        with open("build/tests/screen_ram.bin", "rb") as f:
            data = f.read(960)
            for row in range(15):
                row_bytes = data[row*64 : (row+1)*64]
                try:
                    s = row_bytes.decode('utf-8', errors='replace').rstrip('\x00')
                except Exception:
                    s = str(row_bytes)
                screen_text.append(s)

    return {
        "pc": pc,
        "xpsr": xpsr,
        "tim2_ms": tim2,
        "kernel_tick": kernel_tick,
        "task_runs": stats_words[0:4] if len(stats_words) >= 4 else [],
        "key_overflows": stats_words[8] if len(stats_words) > 8 else 0,
        "command_overflows": stats_words[9] if len(stats_words) > 9 else 0,
        "transport_errors": stats_words[11] if len(stats_words) > 11 else 0,
        "initialization_errors": stats_words[12] if len(stats_words) > 12 else 0,
        "lcd_refreshes": stats_words[13] if len(stats_words) > 13 else 0,
        "screen_text": screen_text
    }

def main():
    os.makedirs("build/tests", exist_ok=True)
    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)

    print("=== Starting 60-second hardware stability measurement ===")
    samples = []
    start_time = time.time()

    for sample_idx in range(13): # 0, 1, 2, ..., 12 = 13 samples
        elapsed = time.time() - start_time
        rc, out = sample_target()
        if rc != 0:
            print(f"Sample #{sample_idx} error:\n{out}")
            sys.exit(1)

        data = parse_openocd_output(out)
        data["sample_index"] = sample_idx
        data["elapsed_s"] = round(elapsed, 2)
        samples.append(data)

        print(f"Sample #{sample_idx:02d} ({data['elapsed_s']:5.1f}s): TIM2={data['tim2_ms']}ms, Tick={data['kernel_tick']}, LCD={data['lcd_refreshes']}, Errs=(Trans:{data['transport_errors']}, Init:{data['initialization_errors']})")

        with open(SAMPLES_JSONL, "a") as f:
            f.write(json.dumps(data, ensure_ascii=False) + "\n")

        if sample_idx < 12:
            time.sleep(5.0)

    total_time = samples[-1]["elapsed_s"] - samples[0]["elapsed_s"]
    tim2_delta = samples[-1]["tim2_ms"] - samples[0]["tim2_ms"]
    tick_delta = samples[-1]["kernel_tick"] - samples[0]["kernel_tick"]
    lcd_delta = samples[-1]["lcd_refreshes"] - samples[0]["lcd_refreshes"]

    tim2_freq = round(tim2_delta / total_time, 2)
    tick_freq = round(tick_delta / total_time, 2)
    lcd_freq = round(lcd_delta / total_time, 2)

    zero_errors = (
        samples[-1]["transport_errors"] == 0 and
        samples[-1]["initialization_errors"] == 0 and
        samples[-1]["key_overflows"] == 0 and
        samples[-1]["command_overflows"] == 0
    )

    print("\n--- Summary ---")
    print(f"Total time: {total_time:.2f} s")
    print(f"TIM2 frequency: {tim2_freq} Hz (expected ~1000 Hz)")
    print(f"Tick frequency: {tick_freq} Hz (expected ~100 Hz)")
    print(f"LCD refresh rate: {lcd_freq} Hz (expected ~3.8 Hz)")
    print(f"Zero errors verified: {zero_errors}")

    summary = {
        "status": "PASS" if zero_errors else "FAIL",
        "duration_seconds": total_time,
        "sample_count": len(samples),
        "tim2_frequency_hz": tim2_freq,
        "kernel_tick_frequency_hz": tick_freq,
        "lcd_refresh_frequency_hz": lcd_freq,
        "zero_errors_verified": zero_errors,
        "samples": samples
    }

    with open(OUTPUT_JSON, "w") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)

    print(f"Stability data saved to {OUTPUT_JSON}")

if __name__ == "__main__":
    main()
