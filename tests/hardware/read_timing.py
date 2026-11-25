#!/usr/bin/env python3
"""Read a complete timing fixture without halting the MCU."""
import argparse
import json
from pathlib import Path
import subprocess
import time

from sample_stability import Tcl, decode, field, flow, require

PATHS = ("empty_read", "empty_call", "pendsv", "systick", "release", "complete", "snapshot")


def layout(elf):
    result = subprocess.check_output(
        ["gdb-multiarch", "-nx", "-batch", str(elf), "-x",
         str(Path(__file__).with_name("elf_layout.py"))], text=True)
    rows = [line.removeprefix("A3_LAYOUT ") for line in result.splitlines()
            if line.startswith("A3_LAYOUT ")]
    require(len(rows) == 1, "missing DWARF layout")
    return json.loads(rows[0])


def record(target, node):
    return decode(target.data(node["address"], node["size"]), node)


def health(target, nodes, rt):
    stats = record(target, nodes["g_display_statistics"])
    faults = target.words(0xe000ed28, 2)
    status = target.words(0xe000edf0)[0]
    assert_line = record(target, nodes["g_rtos_assert_line"])
    guards = [target.words(nodes["display_stacks"]["address"] + index * 4096)[0]
              for index in range(4)] + target.words(nodes["idle_stack"]["address"])
    require(faults == [0, 0] and not status & ((1 << 17) | (1 << 19)) and not assert_line,
            f"halt/lockup/fault/assert: {status:#x}, {faults}, {assert_line}")
    require(guards == [0xdeadc0de] * 5, "stack guard")
    require(stats["initialization_errors"] == stats["transport_errors"] == stats["display_fault"] == 0,
            "LCD errors")
    require(stats["key_wrong_context"] == stats["key_invalid_arg"] == stats["key_other_errors"] == 0,
            "key ingress errors")
    require(target.words(0xe000e014)[0] == 719999, "SysTick reload")
    require(target.words(0xe0001000)[0] & 1 and target.words(0xe000edfc)[0] & (1 << 24),
            "DWT not enabled")
    tick = record(target, nodes["kernel_ticks"])
    payload = {"tick": tick, "tim2_ms": record(target, nodes["g_sys_tick"]), "stats": stats,
               "tracker": record(target, nodes["g_event_tracker"]), "faults": faults, "guards": guards,
               "assert_line": assert_line}
    if rt:
        export = nodes["g_diag_rt_export"]
        seq_addr = export["address"] + field(export, "seq_begin")["offset"]
        for _ in range(8):
            seq = target.words(seq_addr)[0]
            frame = record(target, export)
            if seq and not seq & 1 and seq == frame["seq_begin"] == frame["seq_end"] == target.words(seq_addr)[0]:
                break
        else:
            raise RuntimeError("RT export not published consistently")
        require(frame["clock_cap"]["flags"] == 1 and frame["clock_cap"]["frequency_hz"] == 72000000 and
                frame["clock_cap"]["tick_hz"] == 100, "RT clock validity")
        require(all(task["flags"] == 1 for task in frame["task_snapshots"] + [frame["idle_snapshot"]]),
                "RT task flags")
        tick = record(target, nodes["kernel_ticks"])
        require((tick - frame["published_tick"]) % 2**32 <= 220, "RT export stale")
        payload["rt"] = frame
    return payload


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--rt", type=int, choices=(0, 1), required=True)
    args = ap.parse_args()
    nodes = layout(args.elf)
    capture = nodes["g_timing_bench"]
    state_addr = capture["address"] + field(capture, "state")["offset"]
    target = Tcl(6673)
    try:
        # Wait for the first app export before requiring the RT seqlock.
        deadline = time.monotonic() + 100
        snapshots = []
        with args.out.with_suffix(".jsonl").open("x", buffering=1) as raw:
            while True:
                state = target.words(state_addr)[0]
                require(state in (0, 1, 2), "unexpected fixture state")
                current = health(target, nodes, args.rt and state != 0)
                log = {"host_monotonic": time.monotonic(), "state": state, **current}
                raw.write(json.dumps(log) + "\n")
                snapshots.append(log)
                if state == 2:
                    break
                require(time.monotonic() < deadline, "benchmark did not complete")
                time.sleep(5)
        require(any(s["state"] == 1 for s in snapshots), "no in-window sample")
        frame = record(target, capture)
        require(frame["state"] == target.words(state_addr)[0] == 2, "unfrozen capture")
        require(frame["rt_enabled"] == args.rt and frame["clock_hz"] == 72000000, "build/clock profile")
        require(frame["finish_tick"] - frame["start_tick"] >= 6200 and
                frame["finish_ms"] - frame["start_ms"] >= 60000, "short timing window")
        require(frame["task_switch_mask"] == 0x1e and frame["idle_dispatches"] > 0, "dispatch coverage")
        require(frame["events"] == 600, "finite TIM2 stimulus incomplete")
        require(current["stats"]["ready"] == 1 and current["stats"]["lcd_refreshes"] > 0, "LCD not ready")
        require(all(n > 0 for n in current["stats"]["task_runs"]), "missing task activity")
        require(not record(target, nodes["g_stress_request"]) and
                not record(target, nodes["g_stress_remaining"]), "stimulus remains active")
        flows = flow(snapshots[0], snapshots[-1])
        require(flows["key_attempts"] == 600, f"expected 600 attempts, got {flows}")
        require(frame["bad_job_flags"] == 0, "Job clock or Deadline invalid")
        counts = dict(zip(PATHS, frame.pop("paths"), strict=True))
        require(counts["empty_read"]["count"] == counts["empty_call"]["count"] == 64 and
                counts["empty_read"]["min"] > 0, "bad calibration")
        require(counts["systick"]["count"] >= 6200 and counts["pendsv"]["count"] >= 6200,
                "insufficient C-path samples")
        job_counts = [counts[p]["count"] for p in PATHS[4:]]
        require((min(job_counts) > 0 and len(set(job_counts)) == 1 and
                 job_counts[0] == frame["completed_jobs"]) if args.rt else max(job_counts) == 0,
                "Job sample consistency")
        overhead = counts["empty_call"]["min"]
        for name, row in counts.items():
            if not row["count"]:
                continue
            n = row["count"]
            require(row["min"] * n <= row["sum"] <= row["max"] * n, f"invalid {name} accumulated cycles")
            row["mean_cycles"] = row["sum"] / n
            if name not in PATHS[:2]:
                row["normalized_cycles"] = {key: max(0, value - overhead) for key, value in
                                            (("min", row["min"]), ("mean", row["mean_cycles"]),
                                             ("max", row["max"]))}
        frame.update(paths=counts, flow=flows, health_samples=len(snapshots))
        args.out.write_text(json.dumps(frame, indent=2) + "\n")
        print("Hardware timing capture PASS", args.rt, frame["finish_tick"] - frame["start_tick"])
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
