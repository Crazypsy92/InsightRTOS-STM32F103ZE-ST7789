#!/usr/bin/env python3
"""Validate native LCD command backpressure evidence without halting the MCU."""
import argparse
import json
from pathlib import Path
import time

from read_integrated import observe
from read_timing import layout, record
from sample_stability import Tcl, field, require

RTOS_ERR_FULL = 6


def validate(fixture, sample, injected):
    events = fixture["events"]
    count = len(events)
    require(injected == count == 48, "incorrect stimulus count")
    require(fixture["gate_end"] >= fixture["gate_deadline"] == fixture["gate_start"] + 150 and
            fixture["gate_end"] <= fixture["gate_deadline"] + 30, "gate did not release on time")
    require(fixture["command_max_depth"] == 32, "lcd_commands did not reach capacity")
    require(fixture["sent"] == fixture["executed"] == fixture["acked"] == count,
            "incomplete command/LCD/ACK chain")
    stats = sample["stats"]
    tracker = sample["diagnostics"]["events"]
    require(stats["key_attempts"] == stats["key_sent_ok"] == count and stats["key_queue_full"] == 0,
            "input accounting")
    require(stats["controller_events"] == stats["lcd_events"] == count, "application event accounting")
    require(stats["command_overflows"] == tracker["retry_count"] == fixture["full_total"] ==
            sum(e["full_count"] for e in events) > 0, "FULL/retry mismatch")
    require(tracker["latest_id"] == tracker["confirmed_id"] == tracker["success_count"] == count and
            all(tracker[k] == 0 for k in ("pending_count", "drop_count", "failure_count",
                                         "duplicate_ack_count", "rejected_ack_count")), "ACK accounting")
    seq = events[0]["sent_sequence"]
    retry_chain = None
    for index, event in enumerate(events, start=1):
        require(event["sent_sequence"] == event["lcd_sequence"] == event["ack_sequence"] == seq and
                event["ack_error"] == 0, f"event {index}: sequence/ACK")
        require(event["sent_tick"] <= event["lcd_tick"] <= event["ack_tick"] and
                event["lcd_tick"] >= fixture["gate_end"], f"event {index}: operation/gate order")
        if event["full_count"]:
            require(event["full_status"] == RTOS_ERR_FULL and event["full_depth"] == 32 and
                    event["full_sequence"] == seq == event["full_committed"] + 1 and
                    event["full_tick"] < event["sent_tick"], f"event {index}: FULL evidence")
            if retry_chain is None:
                retry_chain = {"event_id": index, **event}
        seq += 1
    require(retry_chain is not None, "missing FULL → retry → execute → ACK chain")
    return retry_chain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    fixture_node = nodes["g_backpressure"]
    complete_addr = fixture_node["address"] + field(fixture_node, "complete")["offset"]
    target = Tcl(6673)
    try:
        deadline = time.monotonic() + 45
        with args.out.with_suffix(".jsonl").open("x", buffering=1) as log:
            while time.monotonic() < deadline:
                sample = observe(target, nodes)
                if sample:
                    log.write(json.dumps(sample) + "\n")
                    frames = sample["diagnostics"]
                    if (target.words(complete_addr)[0] == 1 and frames["events"]["confirmed_id"] == 48 and
                            frames["queue_depths"][:4] == [0] * 4 and frames["has_switch"] and
                            frames["job_pair_valid"]):
                        fixture = record(target, fixture_node)
                        require(fixture["complete"] == 1 and fixture == record(target, fixture_node),
                                "fixture report changed after completion")
                        result = {"fixture": fixture, "sample": sample}
                        args.out.write_text(json.dumps(result, indent=2) + "\n")
                        result["retry_chain"] = validate(fixture, sample, record(target, nodes["g_stress_generated"]))
                        result["status"] = "PASS"
                        args.out.write_text(json.dumps(result, indent=2) + "\n")
                        print("LCD backpressure hardware PASS", fixture["full_total"])
                        return
                time.sleep(1)
        raise RuntimeError("bounded recovery/queue drain not confirmed before deadline")
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
