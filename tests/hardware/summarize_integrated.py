#!/usr/bin/env python3
"""Summarize a saved Stage 9.4-A capture without hardware access."""
import argparse
import json
from pathlib import Path

from read_integrated import COMMIT_CLASSES


def summarize(frame):
    trace = frame["trace"]
    switch = frame["switch_reason"]
    ack = frame["ack"]
    return {
        "publication_ticks": [frame["begin_tick"], frame["end_tick"]],
        "queue_depths": frame["queue_depths"],
        "latest_event_id": frame["events"]["latest_id"],
        "confirmed_id": frame["events"]["confirmed_id"],
        "ack": ack if ack["valid"] else None,
        "trace": {
            "half_open_window": [trace["oldest_sequence"], trace["next_sequence"]],
            "count": trace["count"], "copied": trace["copied"],
            "overwritten_total": trace["overwritten"], "producer_dropped": trace["producer_dropped"],
            "status": trace["status"],
            "switch_reason": COMMIT_CLASSES[switch["commit_class"]] if frame["has_switch"] else None,
            "switch_reference_stale": bool(switch["primary_stale"] or switch["related_stale"])
            if frame["has_switch"] else None,
        },
        "clock": frame["clock"],
        "rt_tasks": [{
            "slot": task["ref"]["slot"], "generation": task["ref"]["generation"],
            "dispatch_count": task["dispatch_count"], "charged_cycles": task["charged_cycles"],
            "max_ready_latency_cycles": task["max_ready_latency_cycles"], "flags": task["flags"]
        } for task in [*frame["tasks"], frame["idle"]]],
        "job": {"completed": frame["job"]["completed_jobs"],
                "misses": frame["job"]["deadline_misses"], "overruns": frame["job"]["budget_overruns"],
                "last_attempt": frame["attempt"] if frame["job_pair_valid"] else None},
        "dl": {"observation": ("UNAVAILABLE", "NO_DEADLOCK_OBSERVED", "CYCLE_OBSERVED", "INCOMPLETE")
                [frame["dl_observation"]],
                "capture_tick": frame["dl_header"]["capture_tick"],
                "nodes": frame["dl_header"]["node_count"], "edges": frame["dl_header"]["edge_count"]},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    raw = json.loads(args.capture.read_text())
    if raw["status"] != "PASS":
        raise ValueError("capture did not pass validation")
    result = {}
    for name in ("normal", "stream", "burst"):
        if name in raw:
            phase = raw[name]
            result[name] = {"samples": phase["samples"], "flow": phase["flow"],
                            "elapsed_s": phase["elapsed_s"],
                            **summarize(phase["end"]["diagnostics"])}
    if "final" in raw:
        result["production"] = summarize(raw["final"]["diagnostics"])
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
