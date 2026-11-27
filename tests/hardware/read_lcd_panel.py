#!/usr/bin/env python3
"""Observe the LCD Task and rendered-panel DMA without halting the target."""
import argparse
import json
from pathlib import Path
import time

from read_integrated import observe
from read_timing import layout, record
from sample_stability import Tcl, field, require


def panel_snapshot(target, node):
    addr = node["address"] + field(node, "seq_begin")["offset"]
    for _ in range(10):
        first = target.words(addr)[0]
        if first and not first & 1:
            report = record(target, node)
            if first == report["seq_begin"] == report["seq_end"] == target.words(addr)[0]:
                return report
        time.sleep(0.04)
    return None


def clean(r):
    return r["sr"] & 0xc3 == 2 and r["cr2"] & 3 == 0 and \
        (r["ccr"] | r["rx_ccr"]) & 1 == 0 and r["isr"] & 0xff == 0


def bus_release(r):
    require(clean(r) and r["owner"] == r["depth"] == r["recover"] == r["last_error"] == 0 and
            r["state"] == 1 and r["pins"] & 0x800, "released bus not idle, clean and unowned")


def validate_panel(f):
    require(f["state"] in (1, 2) and not f["lcd_error"] and f["preinit_pins"] & 0x800,
            "panel state or CS pre-init")
    require(f["hclk"] == 72_000_000 and (f["rcc_cfgr"] >> 2) & 3 == 2 and
            (f["rcc_cfgr"] >> 4) & 15 == 0 and (f["rcc_cfgr"] >> 8) & 7 == 4,
            "configured PLL, HCLK, APB1")
    b = f["bus"]
    require(b["invariants"] == b["failures"] == 0 and b["selects"] == b["releases"] and
            b["returns"] == b["dma"] + b["poll"] == b["command_writes"] + b["data_writes"] and
            b["dma"] == sum(b["dma_buckets"]) and b["dma_buckets"][4] > 0 and
            b["command_writes"] > 0 and b["poll"] > 0, "LCD transaction accounting")
    require(b["maximum_transaction_cycles"] < 14_400_000, "SPI held more than 200 ms")
    row = b["row"]
    arm, wait, tail, ret, released = (row[k] for k in ("armed", "waited", "drained", "returned", "released"))
    require(row["length"] == arm["cndtr"] == 480 and 0x20000000 <= arm["cmar"] <= 0x20010000 - 480 and
            arm["ccr"] & 0xfd1 == 0x90 and arm["cr1"] & 0x3b == 0 and
            (arm["cr2"] & 3) == 0 and arm["isr"] & 0xff == 0 and
            arm["owner"] != 0 and all(r["owner"] == arm["owner"] and r["depth"] == 1 and
                r["state"] == 2 and r["pins"] == 0x1000 for r in (arm, wait, tail, ret)),
            "pixel row DMA setup or CS/DC")
    for r in (wait, tail):
        require(r["cndtr"] == 0 and r["isr"] & 0xaa == 0x20 and
                r["cr2"] & 3 == 2 and r["rx_ccr"] & 1 == 0, "DMA completion evidence")
    require(tail["sr"] & 0x82 == 2 and clean(ret), "SPI drain or write return")
    bus_release(released)
    bus_release(b["last_release"])
    require(sum(f["pattern_counts"]) == f["full_frames"] and f["local_updates"] <= f["full_frames"],
            "pattern / local text accounting")


def validate_series(samples):
    first, last = samples[0], samples[-1]
    require(len(samples) >= 11 and all(b["monotonic"] - a["monotonic"] < 72 for a, b in zip(samples, samples[1:])),
            "missing minute sample")
    for a, b in zip(samples, samples[1:]):
        require(b["diagnostics"]["seq_end"] > a["diagnostics"]["seq_end"] and
                b["end_read_tick"] > a["end_read_tick"] and b["tim2_ms"] > a["tim2_ms"] and
                all(y > x for x, y in zip(a["stats"]["task_runs"][1:], b["stats"]["task_runs"][1:])) and
                b["stats"]["task_runs"][0] >= a["stats"]["task_runs"][0] and
                b["panel"]["bus"]["dma"] > a["panel"]["bus"]["dma"] and
                b["stats"]["lcd_refreshes"] > a["stats"]["lcd_refreshes"] and
                b["stats"]["ll_last_error"] == b["stats"]["ll_recover_count"] == 0,
                "task, LCD or Monitor did not advance")
    for s in samples:
        stats = s["stats"]
        if stats["key_task_events"] == 0:
            require(stats["task_runs"][0] == 0 and stats["snapshots"][0]["state"] == 4,
                    "idle KEY task did not block on input")
        require(max(s["diagnostics"]["queue_depths"]) <= 1 and
                all(s["stats"][k] == 0 for k in ("command_overflows", "telemetry_overflows",
                    "transport_errors", "initialization_errors", "ll_last_error", "ll_recover_count")),
                "queue accumulation or display errors")
    p = last["panel"]
    elapsed = last["monotonic"] - first["monotonic"]
    require(elapsed >= 600 and p["state"] == 2 and p["end_ms"] - p["start_ms"] >= 600_000 and
            p["full_frames"] - first["panel"]["full_frames"] >= 300 and
            all(n > 0 for n in p["pattern_counts"]) and
            p["local_updates"] - first["panel"]["local_updates"] >= 300,
            "duration, complete frames, partial updates or restored application page")
    return elapsed


def validate_key(last, sample):
    require(sample["panel"]["state"] == 2, "KEY must be observed after stress")
    previous, now = last["stats"], sample["stats"]
    count = now["key_isr_events"] - previous["key_isr_events"]
    ack = sample["diagnostics"]["ack"]
    ledger = sample["tracker"]
    require(count > 0 and all(now[k] - previous[k] == count for k in
                ("key_task_events", "controller_events", "lcd_events")) and
            now["task_runs"][0] - previous["task_runs"][0] == count and
            now["key_short"] > previous["key_short"] and
            ledger["success_count"] - last["tracker"]["success_count"] == count and
            ledger["pending_count"] == ledger["failure_count"] == ledger["drop_count"] == 0 and
            ack["valid"] and ack["error"] == 0 and ack["id"] == ledger["confirmed_id"],
            "physical KEY -> Controller -> LCD -> ACK")
    return count


def observe_panel(target, nodes):
    app = observe(target, nodes)
    panel = panel_snapshot(target, nodes["g_lcd_panel_diag"])
    if app is None or panel is None:
        return None
    return {**app, "panel": panel}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    target = Tcl(6673)
    try:
        with args.out.with_suffix(".jsonl").open("x", buffering=1) as raw:
            samples = []
            next_sample = time.monotonic()
            start_wait = next_sample
            while time.monotonic() < start_wait + 660:
                time.sleep(max(0, next_sample - time.monotonic()))
                s = observe_panel(target, nodes)
                if not s or not s["stats"]["ready"]:
                    require(not samples, "missing minute publication")
                    next_sample = time.monotonic() + 0.5
                    continue
                raw.write(json.dumps(s) + "\n")
                validate_panel(s["panel"])
                samples.append(s)
                print(f"minute {len(samples)-1} | frames {s['panel']['full_frames']} | "
                      f"DMA {s['panel']['bus']['dma']} | monitor {s['diagnostics']['seq_end']}", flush=True)
                if len(samples) >= 11 and s["panel"]["state"] == 2:
                    break
                next_sample = samples[0]["monotonic"] + min(len(samples) * 60, 630)
            elapsed = validate_series(samples)
        print("10-minute panel capture complete; observe a physical KEY1 input", flush=True)
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            key = observe_panel(target, nodes)
            if (key and key["stats"]["key_short"] > samples[-1]["stats"]["key_short"] and
                    key["tracker"]["pending_count"] == 0 and key["diagnostics"]["ack"]["valid"]):
                validate_key(samples[-1], key)
                break
            time.sleep(2)
        else:
            raise RuntimeError("no completed physical KEY1 event after panel run")
        args.out.write_text(json.dumps({"status": "PASS", "elapsed_s": elapsed,
            "samples": samples, "key": key}, indent=2) + "\n")
        print("ST7789 electrical and scheduling acceptance PASS", flush=True)
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
