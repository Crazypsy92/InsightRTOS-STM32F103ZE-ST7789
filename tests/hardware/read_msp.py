#!/usr/bin/env python3
"""Read closed MSP phase records and runtime health over non-halting MEM-AP."""
import argparse
import json
from pathlib import Path
import time

from read_timing import health, layout, record
from sample_stability import Tcl, field, flow, require


PHASES = ("boot", "normal", "load")
HANDLERS = ("svc", "pendsv", "systick", "tim2")
NESTING = ("systick_over_tim2", "systick_over_pendsv", "tim2_over_pendsv")


def summarize(label, phase, bottom, top):
    require(len(phase["touched"]) == 32 and len(phase["guard"]) == 4, "MSP DWARF layout")
    require(phase["guard"] == [0xe25a5ada] * 4, f"{label}: MSP guard")
    require(not phase["touched"][0] & 0xf, "guard words marked as watermark")
    indices = [i * 32 + b for i, word in enumerate(phase["touched"])
               for b in range(32) if word & (1 << b)]
    require(indices, f"{label}: no MSP writes found")
    lowest = bottom + 4 * min(indices)
    require(lowest == phase["lowest_touched"] and lowest > bottom + 512,
            f"{label}: waterline disagreement or minimum margin")
    calls = dict(zip(HANDLERS, phase["calls"], strict=True))
    entry = dict(zip(HANDLERS, phase["entry_min"], strict=True))
    require(all((v == 0 if not calls[k] else bottom + 512 < v <= top and v % 4 == 0)
                for k, v in entry.items()), f"{label}: MSP entry range")
    require(all(calls[k] > 0 for k in ("pendsv", "systick", "tim2")), f"{label}: missing handler")
    require(calls["svc"] == (1 if label == "boot" else 0), f"{label}: SVC count")
    duration_ticks = phase["finish_tick"] - phase["start_tick"]
    duration_ms = phase["finish_ms"] - phase["start_ms"]
    if label != "boot":
        require(duration_ticks >= 6200 and duration_ms >= 60000, f"{label}: short phase")
    return {"lowest_touched": hex(lowest), "observed_bytes": top - lowest,
            "margin_including_guard": lowest - bottom,
            "margin_outside_guard": lowest - (bottom + 16),
            "handler_calls": calls, "recorded_entry_min": {k: hex(v) for k, v in entry.items()},
            "nested": dict(zip(NESTING, phase["nested"], strict=True)),
            "ticks": duration_ticks, "ms": duration_ms, "guard": phase["guard"],
            "touch_bitmap": phase["touched"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    capture = nodes["g_msp_bench"]
    state_address = capture["address"] + field(capture, "state")["offset"]
    fault_address = capture["address"] + field(capture, "fault")["offset"]
    target = Tcl(6673)
    try:
        deadline = time.monotonic() + 165
        snapshots = []
        with args.out.with_suffix(".jsonl").open("x", buffering=1) as log:
            while True:
                state = target.words(state_address)[0]
                fault = target.words(fault_address)[0]
                if fault or state == 4:
                    log.write(json.dumps({"state": state, "msp_failure": record(target, capture)}) + "\n")
                    raise RuntimeError("MSP guard, minimum margin, stack bounds, or clock failure")
                require(state in (0, 1, 2, 3), "unexpected MSP fixture state")
                sample = {"state": state, "host_monotonic": time.monotonic(),
                          **health(target, nodes, state > 0)}
                log.write(json.dumps(sample) + "\n")
                snapshots.append(sample)
                if state == 3:
                    break
                require(time.monotonic() < deadline, "MSP fixture timed out")
                time.sleep(4)
        frame = record(target, capture)
        require(frame["state"] == target.words(state_address)[0] == 3 and not frame["fault"],
                "MSP fixture unreadable or failed")
        bottom, top = frame["bottom"], frame["top"]
        require((bottom, top, frame["clock_hz"]) == (0x2000f000, 0x20010000, 72000000),
                "MSP reserve/clock")
        require(frame["pressure_events"] == 600 and
                not record(target, nodes["g_stress_request"]) and
                not record(target, nodes["g_stress_remaining"]), "bounded TIM2 stimulus")
        normal = [s for s in snapshots if s["state"] == 1]
        load = [s for s in snapshots if s["state"] == 2]
        require(len(normal) >= 12 and len(load) >= 12 and
                all(s["stats"]["key_attempts"] == 0 for s in normal),
                "missing phase coverage or normal window was stimulated")
        phase_results = {label: summarize(label, phase, bottom, top)
                         for label, phase in zip(PHASES, frame["phases"], strict=True)}
        flows = flow(snapshots[0], snapshots[-1])
        require(flows["key_attempts"] == 600 and
                all(n > 0 for n in snapshots[-1]["stats"]["task_runs"]), "events/tasks")
        result = {"status": "HARDWARE_MEASURED", "clock_hz": frame["clock_hz"],
                  "bottom": hex(bottom), "top_exclusive": hex(top), "phases": phase_results,
                  "health_samples": len(snapshots), "normal_health_samples": len(normal),
                  "load_health_samples": len(load), "flow": flows}
        args.out.write_text(json.dumps(result, indent=2) + "\n")
        print("MSP snapshot PASS", {k: v["observed_bytes"] for k, v in phase_results.items()})
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
