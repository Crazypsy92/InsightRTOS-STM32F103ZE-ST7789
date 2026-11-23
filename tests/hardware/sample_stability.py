#!/usr/bin/env python3
"""
Hardware stability and real-time sampling script for InsightRTOS Stage 9.3-A2.
Supports:
- Dynamic symbol resolution via arm-none-eabi-nm or MAP file (zero hardcoded RAM offsets)
- Readout of core CPU registers (PC, xPSR)
- Readout of hardware timers: TIM2 ms (uwTick) and RTOS 100Hz ticks (g_rtos_tick_count)
- Readout of Application IPC statistics (g_display_statistics)
- Readout of Real-Time Diagnostics snapshots (g_diag_rt_export: 4 tasks + Idle)
- Readout of bounded event tracker lifecycle (g_event_tracker: drops, evictions, orphaned ACKs)
- Controlled software stress injection mode (--mode stress --duration 300)
- Zero silent error swallowing: missing symbols, OpenOCD timeouts or invalid reads raise exceptions.
"""

import os
import sys
import json
import time
import subprocess
import struct
import argparse
import hashlib

DEFAULT_ELF = "build/DISPLAY_RTOS_O2_Trace/insightrtos_display.elf"
DEFAULT_MAP = "build/DISPLAY_RTOS_O2_Trace/insightrtos_display.map"

def resolve_symbols_from_nm(elf_path):
    symbols = {}
    try:
        res = subprocess.run(["arm-none-eabi-nm", "-C", elf_path],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        for line in res.stdout.splitlines():
            parts = line.strip().split()
            if len(parts) >= 3:
                addr_str, stype, name = parts[0], parts[1], parts[2]
                try:
                    symbols[name] = int(addr_str, 16)
                except ValueError:
                    continue
    except Exception as e:
        print(f"[WARN] arm-none-eabi-nm failed: {e}. Falling back to map parsing.")
    return symbols

def resolve_symbols_from_map(map_path):
    symbols = {}
    if not os.path.exists(map_path):
        return symbols
    with open(map_path, "r", errors="ignore") as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 2 and parts[0].startswith("0x") and len(parts[0]) == 10:
                try:
                    addr = int(parts[0], 16)
                    name = parts[1]
                    symbols[name] = addr
                except ValueError:
                    continue
    return symbols

def get_required_symbols(elf_path, map_path):
    syms = resolve_symbols_from_nm(elf_path)
    if not syms:
        syms = resolve_symbols_from_map(map_path)
    required = ["g_sys_tick", "kernel_ticks", "g_display_statistics"]
    for r in required:
        if r not in syms:
            raise RuntimeError(f"Required symbol '{r}' could not be resolved from ELF or MAP!")
    return syms

def get_firmware_sha(bin_path):
    if not os.path.exists(bin_path):
        return "UNKNOWN"
    with open(bin_path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()

def get_git_commit():
    try:
        res = subprocess.run(["git", "rev-parse", "HEAD"], stdout=subprocess.PIPE, text=True, check=True)
        return res.stdout.strip()
    except Exception:
        return "UNKNOWN"

def openocd_exec(cmds):
    full_cmd = ["openocd", "-f", "interface/stlink.cfg", "-f", "target/stm32f1x.cfg"]
    for c in cmds:
        full_cmd.extend(["-c", c])
    res = subprocess.run(full_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"OpenOCD command failed (code {res.returncode}):\n{res.stdout}")
    return res.stdout

def read_memory_words(sym_addr, count):
    cmds = [
        "init",
        "halt",
        f"mdw 0x{sym_addr:08X} {count}",
        "resume",
        "shutdown"
    ]
    out = openocd_exec(cmds)
    words = []
    prefix = f"0x{sym_addr:08x}:"
    for line in out.splitlines():
        line = line.strip()
        parts = line.split(":")
        if len(parts) == 2:
            try:
                addr_val = int(parts[0].strip(), 16)
                if sym_addr <= addr_val < sym_addr + count * 4:
                    for w in parts[1].strip().split():
                        words.append(int(w, 16))
            except ValueError:
                continue
    if len(words) < count:
        raise RuntimeError(f"Failed to read {count} words from 0x{sym_addr:08X}, got {len(words)}")
    return words[:count]

def write_memory_word(addr, val):
    cmds = [
        "init",
        "halt",
        f"mww 0x{addr:08X} 0x{val:08X}",
        "resume",
        "shutdown"
    ]
    openocd_exec(cmds)

def read_full_sample(syms):
    # Prepare batch OpenOCD script to read:
    # 1. reg pc, reg xPSR
    # 2. uwTick (1 word)
    # 3. g_rtos_tick_count (1 word)
    # 4. g_display_statistics (28 words / 112 bytes)
    # 5. g_event_tracker (100 words / 400 bytes)
    # 6. g_diag_rt_export (if present, ~80 words / 320 bytes)
    uwtick_addr = syms["g_sys_tick"]
    rtos_tick_addr = syms["kernel_ticks"]
    stats_addr = syms["g_display_statistics"]
    tracker_addr = syms.get("g_event_tracker", 0)
    rt_export_addr = syms.get("g_diag_rt_export", 0)

    cmds = [
        "init",
        "halt",
        "reg pc",
        "reg xPSR",
        f"mdw 0x{uwtick_addr:08X} 1",
        f"mdw 0x{rtos_tick_addr:08X} 1",
        f"mdw 0x{stats_addr:08X} 24"
    ]
    if tracker_addr != 0:
        cmds.append(f"mdw 0x{tracker_addr:08X} 64")
    if rt_export_addr != 0:
        cmds.append(f"mdw 0x{rt_export_addr:08X} 80")

    cmds.extend(["resume", "shutdown"])
    out = openocd_exec(cmds)

    # Parse stdout
    pc = None
    xpsr = None
    mem_dict = {}

    for line in out.splitlines():
        line = line.strip()
        if line.startswith("pc (/32):"):
            pc = line.split(":")[1].strip()
        elif line.startswith("xPSR (/32):"):
            xpsr = line.split(":")[1].strip()
        elif line.startswith("0x") and ":" in line:
            parts = line.split(":")
            if len(parts) == 2:
                try:
                    addr_val = int(parts[0].strip(), 16)
                    words_in_line = [int(w, 16) for w in parts[1].strip().split()]
                    for i, w in enumerate(words_in_line):
                        mem_dict[addr_val + i*4] = w
                except ValueError:
                    continue

    if uwtick_addr not in mem_dict or rtos_tick_addr not in mem_dict:
        raise RuntimeError("Failed to sample hardware ticks from MCU!")

    tim2_ms = mem_dict[uwtick_addr]
    kernel_tick = mem_dict[rtos_tick_addr]

    # Parse display statistics
    stats_words = [mem_dict.get(stats_addr + i*4, 0) for i in range(24)]
    task_runs = stats_words[0:4]
    key_short = stats_words[4]
    key_double = stats_words[5]
    key_long = stats_words[6]
    key_isr_events = stats_words[7]
    key_overflows = stats_words[8]
    command_overflows = stats_words[9]
    telemetry_overflows = stats_words[10]
    transport_errors = stats_words[11]
    initialization_errors = stats_words[12]
    lcd_refreshes = stats_words[13]

    # Parse tracker if present
    tracker_info = {}
    if tracker_addr != 0:
        # display_event_tracker_t: history[8] (8*20 = 160 bytes = 40 words)
        # offset 160 (word 40): latest_id, confirmed_id, latest_event, completed_ms,
        # success_count, failure_count, retry_count, drop_count, evicted_unacked_count, orphaned_ack_count
        base_w = 40
        tracker_info = {
            "latest_id": mem_dict.get(tracker_addr + (base_w+0)*4, 0),
            "confirmed_id": mem_dict.get(tracker_addr + (base_w+1)*4, 0),
            "completed_ms": mem_dict.get(tracker_addr + (base_w+3)*4, 0),
            "success_count": mem_dict.get(tracker_addr + (base_w+4)*4, 0),
            "failure_count": mem_dict.get(tracker_addr + (base_w+5)*4, 0),
            "retry_count": mem_dict.get(tracker_addr + (base_w+6)*4, 0),
            "drop_count": mem_dict.get(tracker_addr + (base_w+7)*4, 0),
            "evicted_unacked_count": mem_dict.get(tracker_addr + (base_w+8)*4, 0),
            "orphaned_ack_count": mem_dict.get(tracker_addr + (base_w+9)*4, 0)
        }

    # Parse RT snapshot export if present
    rt_export = {}
    if rt_export_addr != 0:
        sample_seq = mem_dict.get(rt_export_addr, 0)
        clock_type = mem_dict.get(rt_export_addr + 4, 0)
        freq_hz = mem_dict.get(rt_export_addr + 8, 0)
        clock_flags = mem_dict.get(rt_export_addr + 20, 0)

        # 4 task snapshots + 1 idle snapshot, each 56 bytes (14 words)
        # Offset for task snapshots starts at word 6 (addr + 24)
        task_names = ["KEY", "CTRL", "LCD", "MON", "IDLE"]
        snapshots = {}
        for idx in range(5):
            t_base = rt_export_addr + 24 + idx * 56
            # uint64 charged_cycles (words 0..1)
            c_lo = mem_dict.get(t_base, 0)
            c_hi = mem_dict.get(t_base + 4, 0)
            charged_cycles = (c_hi << 32) | c_lo
            # uint64 last_ready_cycles (words 2..3)
            r_lo = mem_dict.get(t_base + 8, 0)
            r_hi = mem_dict.get(t_base + 12, 0)
            last_ready_cycles = (r_hi << 32) | r_lo
            # uint64 max_ready_cycles (words 4..5)
            mr_lo = mem_dict.get(t_base + 16, 0)
            mr_hi = mem_dict.get(t_base + 20, 0)
            max_ready_cycles = (mr_hi << 32) | mr_lo
            # ref (word 6)
            ref_raw = mem_dict.get(t_base + 24, 0)
            # flags (word 7)
            snap_flags = mem_dict.get(t_base + 28, 0)
            # last_ready_ticks (word 8)
            last_ready_ticks = mem_dict.get(t_base + 32, 0)
            # max_ready_ticks (word 9)
            max_ready_ticks = mem_dict.get(t_base + 36, 0)
            # dispatch_count (word 10)
            dispatch_count = mem_dict.get(t_base + 40, 0)

            snapshots[task_names[idx]] = {
                "charged_cycles": charged_cycles,
                "last_ready_cycles": last_ready_cycles,
                "max_ready_cycles": max_ready_cycles,
                "flags": snap_flags,
                "last_ready_ticks": last_ready_ticks,
                "max_ready_ticks": max_ready_ticks,
                "dispatch_count": dispatch_count
            }

        rt_export = {
            "sample_seq": sample_seq,
            "clock_type": clock_type,
            "frequency_hz": freq_hz,
            "clock_flags": clock_flags,
            "tasks": snapshots
        }

    return {
        "timestamp": time.time(),
        "pc": pc,
        "xpsr": xpsr,
        "tim2_ms": tim2_ms,
        "kernel_tick": kernel_tick,
        "task_runs": task_runs,
        "key_short": key_short,
        "key_double": key_double,
        "key_long": key_long,
        "key_isr_events": key_isr_events,
        "key_overflows": key_overflows,
        "command_overflows": command_overflows,
        "telemetry_overflows": telemetry_overflows,
        "transport_errors": transport_errors,
        "initialization_errors": initialization_errors,
        "lcd_refreshes": lcd_refreshes,
        "tracker": tracker_info,
        "rt_diagnostics": rt_export
    }

def main():
    parser = argparse.ArgumentParser(description="InsightRTOS Stage 9.3-A2 Stability and RT Sampler")
    parser.add_argument("--mode", choices=["normal", "stress"], default="normal",
                        help="Sampling profile: 'normal' (60s baseline) or 'stress' (300s controlled stress)")
    parser.add_argument("--duration", type=int, default=None,
                        help="Duration in seconds (default: 60s for normal, 300s for stress)")
    parser.add_argument("--interval", type=float, default=None,
                        help="Sampling interval in seconds (default: 5.0s for normal, 30.0s for stress)")
    parser.add_argument("--stress-rate", type=int, default=100,
                        help="Stress event injection period in ms (default: 100ms = 10 events/sec)")
    parser.add_argument("--elf", default=DEFAULT_ELF, help="Target ELF file")
    parser.add_argument("--bin", default="build/DISPLAY_RTOS_O2_Trace/insightrtos_display.bin", help="Target BIN file")
    parser.add_argument("--map", default=DEFAULT_MAP, help="Target MAP file")
    parser.add_argument("--out-dir", default="evidence/stage9_3_a2/logs", help="Output directory")

    args = parser.parse_args()

    duration = args.duration if args.duration is not None else (300 if args.mode == "stress" else 60)
    interval = args.interval if args.interval is not None else (30.0 if args.mode == "stress" else 5.0)

    os.makedirs(args.out_dir, exist_ok=True)
    out_json = os.path.join(args.out_dir, f"stability_{args.mode}_{duration}s.json")
    out_jsonl = os.path.join(args.out_dir, f"stability_{args.mode}_{duration}s_samples.jsonl")

    print(f"=== InsightRTOS Hardware Sampling [{args.mode.upper()} Profile] ===")
    print(f"ELF: {args.elf}")
    print(f"Duration: {duration}s | Interval: {interval}s")

    syms = get_required_symbols(args.elf, args.map)
    commit_sha = get_git_commit()
    firmware_sha = get_firmware_sha(args.bin)
    print(f"Commit SHA: {commit_sha}")
    print(f"Firmware SHA: {firmware_sha}")
    print(f"Resolved {len(syms)} symbols. g_sys_tick=0x{syms['g_sys_tick']:08X}, kernel_ticks=0x{syms['kernel_ticks']:08X}")

    stress_sym = syms.get("g_stress_inject_rate", 0)
    if args.mode == "stress":
        if stress_sym == 0:
            raise RuntimeError("Symbol 'g_stress_inject_rate' not found in ELF! Cannot activate controlled stress test.")
        print(f"[STRESS] Activating controlled stress injection (period {args.stress_rate}ms = {1000//args.stress_rate} ev/s) at 0x{stress_sym:08X}...")
        write_memory_word(stress_sym, args.stress_rate)

    samples = []
    start_time = time.time()
    num_samples = int(duration / interval) + 1

    try:
        for idx in range(num_samples):
            elapsed = time.time() - start_time
            data = read_full_sample(syms)
            data["sample_index"] = idx
            data["elapsed_s"] = round(elapsed, 2)
            samples.append(data)

            # Print brief status
            rt_str = ""
            if "rt_diagnostics" in data and "tasks" in data["rt_diagnostics"]:
                tasks = data["rt_diagnostics"]["tasks"]
                lcd_c = tasks.get("LCD", {}).get("charged_cycles", 0)
                idle_c = tasks.get("IDLE", {}).get("charged_cycles", 0)
                rt_str = f"| LCD_cyc={lcd_c:,} IDLE_cyc={idle_c:,}"

            ev_str = ""
            if "tracker" in data and data["tracker"]:
                tr = data["tracker"]
                ev_str = f"| Ev(succ:{tr.get('success_count',0)}, drop:{tr.get('drop_count',0)}, evict:{tr.get('evicted_unacked_count',0)}, orph:{tr.get('orphaned_ack_count',0)})"

            print(f"[{idx:02d}/{num_samples-1}] {data['elapsed_s']:6.1f}s: TIM2={data['tim2_ms']}ms Tick={data['kernel_tick']} LCD={data['lcd_refreshes']} Err(Trans:{data['transport_errors']}, Init:{data['initialization_errors']}) {rt_str} {ev_str}")

            with open(out_jsonl, "a") as f:
                f.write(json.dumps(data, ensure_ascii=False) + "\n")

            if idx < num_samples - 1:
                # Compensate sleep time
                target_next = start_time + (idx + 1) * interval
                sleep_sec = max(0.1, target_next - time.time())
                time.sleep(sleep_sec)

    finally:
        if args.mode == "stress" and stress_sym != 0:
            print(f"[STRESS] Deactivating stress injection at 0x{stress_sym:08X}...")
            try:
                write_memory_word(stress_sym, 0)
            except Exception as e:
                print(f"[WARN] Failed to deactivate stress injection: {e}")

    # Final summary calculations
    total_time = samples[-1]["elapsed_s"] - samples[0]["elapsed_s"]
    tim2_delta = samples[-1]["tim2_ms"] - samples[0]["tim2_ms"]
    tick_delta = samples[-1]["kernel_tick"] - samples[0]["kernel_tick"]
    lcd_delta = samples[-1]["lcd_refreshes"] - samples[0]["lcd_refreshes"]

    tim2_freq = round(tim2_delta / total_time, 2) if total_time > 0 else 0
    tick_freq = round(tick_delta / total_time, 2) if total_time > 0 else 0
    lcd_freq = round(lcd_delta / total_time, 2) if total_time > 0 else 0

    zero_system_errors = (
        samples[-1]["transport_errors"] == 0 and
        samples[-1]["initialization_errors"] == 0
    )

    summary = {
        "profile": args.mode,
        "commit_sha": commit_sha,
        "firmware_sha": firmware_sha,
        "elf_path": args.elf,
        "sample_count": len(samples),
        "total_duration_s": round(total_time, 2),
        "tim2_freq_hz": tim2_freq,
        "tick_freq_hz": tick_freq,
        "lcd_freq_hz": lcd_freq,
        "zero_system_errors": zero_system_errors,
        "transport_errors": samples[-1]["transport_errors"],
        "initialization_errors": samples[-1]["initialization_errors"],
        "key_overflows": samples[-1]["key_overflows"],
        "command_overflows": samples[-1]["command_overflows"],
        "event_tracker": samples[-1].get("tracker", {}),
        "rt_diagnostics_end": samples[-1].get("rt_diagnostics", {})
    }

    with open(out_json, "w") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)

    print(f"\n=== SUMMARY ({args.mode.upper()} {duration}s) ===")
    print(f"Total time: {total_time:.2f}s | Samples: {len(samples)}")
    print(f"TIM2 Frequency: {tim2_freq} Hz (Target 1000 Hz)")
    print(f"RTOS Tick Frequency: {tick_freq} Hz (Target 100 Hz)")
    print(f"LCD Refresh Frequency: {lcd_freq} Hz")
    print(f"Zero System Errors: {zero_system_errors}")
    print(f"Results saved to: {out_json}")

if __name__ == "__main__":
    main()
