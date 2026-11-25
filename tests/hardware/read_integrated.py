#!/usr/bin/env python3
"""Read the v1 composite export and verify the live event ledger over MEM-AP."""
import argparse
import json
from pathlib import Path
import time

from read_timing import health, layout, record
from sample_stability import Tcl, field, flow, require

COMMIT_CLASSES = ("other", "higher_preempt", "yield", "slice_rotation", "block_or_wake",
                  "exit", "idle_transition", "mixed")


def read_snapshot(target, nodes):
    node = nodes["g_app_diag"]
    addr = node["address"] + field(node, "seq_begin")["offset"]
    for _ in range(8):
        before = target.words(addr)[0]
        if before and not before & 1:
            frame = record(target, node)
            if before == frame["seq_begin"] == frame["seq_end"] == target.words(addr)[0]:
                return frame
        time.sleep(0.08)
    return None


def validate(frame, tick, refs, size):
    require(frame["schema_version"] == 1 and frame["bytes"] == size, "composite schema")
    require((tick - frame["end_tick"]) % 2**32 <= 230, "stale composite publication")
    require(frame["clock_status"] == 0 and frame["clock"]["flags"] == 1 and
            frame["clock"]["frequency_hz"] == 72000000 and frame["clock"]["tick_hz"] == 100,
            "DWT capability")
    require(frame["rt_read_mask"] == 31 and frame["job_status"] == 0 and
            frame["attempt"]["configured"] == 0, "RT/Job status")
    tasks = frame["tasks"] + [frame["idle"]]
    require([t["ref"] for t in tasks] == refs + [{"slot": 8, "generation": 1}] and
            all(t["flags"] == 1 for t in tasks), "RT generation/validity")
    require([t["base_priority"] for t in tasks] == [5, 4, 2, 1, 0], "RT priorities")
    if frame["job_pair_valid"]:
        a = frame["attempt"]
        require(a["event_id"] and a["job_id"] and a["release"] == a["complete"] == 0 and
                frame["job"]["active_job_id"] == 0 and frame["job"]["completed_jobs"] > 0,
                "invalid attempt association")
    trace = frame["trace"]
    require(frame["trace_status"] == 0 and frame["trace_schema"] == 1 and
            frame["trace_event_set"] == 2 and frame["trace_capacity"] == 128, "Trace acquisition")
    require(0 < trace["copied"] <= 8 and
            trace["next_sequence"] - trace["oldest_sequence"] == trace["count"] <= 128,
            "Trace window")
    if frame["has_switch"]:
        switch, reason = frame["switch_record"], frame["switch_reason"]
        require(frame["explain_status"] == 0 and reason["kind"] == switch["event_type"] == 12 and
                0 <= reason["commit_class"] < len(COMMIT_CLASSES) and
                (switch["sequence"] - trace["oldest_sequence"]) % 2**32 < trace["count"],
                "Trace switch explanation")
    require(frame["dl_capture_status"] == frame["dl_analyze_status"] == 0 and
            frame["dl_observation"] == 1 and frame["dl_header"]["node_count"] == 4 and
            frame["dl_header"]["edge_count"] == frame["dl_header"]["mutex_count"] == 0 and
            frame["dl_header"]["captured"] == frame["dl_header"]["analyzed"] == 1 and
            frame["dl"]["deadlock_detected"] == 0 and
            frame["dl"]["capture_tick"] == frame["dl_header"]["capture_tick"], "DL graph observation")
    ack = frame["ack"]
    if ack["valid"]:
        events = frame["events"]
        retained = events["history"][ack["id"] % len(events["history"])]
        require(ack["id"] == events["confirmed_id"] == retained["id"] and
                retained["stage"] in (5, 6) and
                ack["ack_ms"] == events["completed_ms"] == retained["ack_ms"] and
                ack["timestamp_ms"] == retained["timestamp_ms"] and ack["error"] == retained["error"] and
                ack["latency_ms"] == (ack["ack_ms"] - ack["timestamp_ms"]) % 2**32,
                "inconsistent event/ACK latency")


def observe(target, nodes):
    app = health(target, nodes, False)
    frame = read_snapshot(target, nodes)
    if frame is None:
        return None
    tick = record(target, nodes["kernel_ticks"])
    validate(frame, tick, record(target, nodes["g_display_task_refs"]), nodes["g_app_diag"]["size"])
    return {**app, "end_read_tick": tick, "monotonic": time.monotonic(), "diagnostics": frame}


def phase(target, nodes, raw, name, start, seconds, expected=0, request=0):
    if request:
        mailbox = nodes["g_stress_request"]["address"]
        require(not record(target, nodes["g_stress_request"]), "stress already active")
        require(target.cmd(f"mww {mailbox:#x} {request}") == "", "TIM2 stimulus command")
    valid = 0
    elapsed = 0.0
    deadline = time.monotonic() + seconds + 26
    while time.monotonic() < deadline:
        time.sleep(2 if name == "burst" else 4)
        end = observe(target, nodes)
        if end is None:
            raw.write(json.dumps({"phase": name, "quality": "INCOMPLETE"}) + "\n")
            continue
        raw.write(json.dumps({"phase": name, "quality": "VALID", **end}) + "\n")
        valid += 1
        elapsed = end["monotonic"] - start["monotonic"]
        count = end["stats"]["key_attempts"] - start["stats"]["key_attempts"]
        require(count <= expected, f"{name}: unexpected physical events")
        events = end["diagnostics"]["events"]
        if (elapsed >= seconds and count == expected and
                end["tracker"]["pending_count"] == 0 and
                not record(target, nodes["g_stress_request"]) and
                events["latest_id"] == end["tracker"]["latest_id"] and
                events["pending_count"] == 0):
            break
    else:
        raise RuntimeError(f"{name}: stimulus/capture did not reach settled endpoint")

    counts = flow(start, end)
    require(counts["key_attempts"] == expected, "unexpected flow")
    require(end["tracker"]["duplicate_ack_count"] == end["tracker"]["rejected_ack_count"] == 0,
            "disordered, duplicate or unexpected ACK")
    for k in ("success_count", "failure_count", "drop_count", "retry_count", "pending_count", "confirmed_id"):
        require(events[k] == end["tracker"][k], f"{name}: composite ledger is not current for {k}")
    require(counts["failure_count"] == 0, f"{name}: LCD failed")
    if request:
        require(end["diagnostics"]["ack"]["valid"] == 1 and
                end["diagnostics"]["ack"]["id"] == end["tracker"]["confirmed_id"],
                f"{name}: final observable ACK missing")
        require(end["diagnostics"]["job_pair_valid"] == 1 and
                end["diagnostics"]["attempt"]["event_id"] == end["diagnostics"]["ack"]["id"],
                f"{name}: final job/attempt does not match settled tail")
    if name == "burst":
        require(counts["key_queue_full"] > 0 and counts["key_sent_ok"] > 0, "burst lacked backpressure")
    if name == "stream":
        require(counts["key_sent_ok"] > 0 and end["stats"]["task_runs"][0] > 0, "KEY did not run")
    require(end["diagnostics"]["trace"]["overwritten"] > start["diagnostics"]["trace"]["overwritten"] and
            end["diagnostics"]["has_switch"] == 1, "Trace not advancing")
    ticks = end["end_read_tick"] - start["end_read_tick"]
    milliseconds = end["tim2_ms"] - start["tim2_ms"]
    require(95 < ticks / elapsed < 105 and 950 < milliseconds / elapsed < 1050, "timebase rates")
    return {"elapsed_s": elapsed, "ticks": ticks, "tim2_ms": milliseconds, "samples": valid,
            "flow": counts, "start": start, "end": end}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--profile", choices=("Integrated", "IntegratedProd"), required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    has_stress = "g_stress_request" in nodes
    require(has_stress == (args.profile == "Integrated"), "wrong target profile")
    target = Tcl(6673)
    try:
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            start = observe(target, nodes)
            if start and start["stats"]["ready"]:
                break
            time.sleep(0.5)
        else:
            raise RuntimeError("no usable Monitor publication")
        with args.out.with_suffix(".jsonl").open("x", buffering=1) as raw:
            raw.write(json.dumps({"phase": "startup", "quality": "VALID", **start}) + "\n")
            result = {"profile": args.profile, "startup": start}
            if has_stress:
                result["normal"] = phase(target, nodes, raw, "normal", start, 60)
                result["stream"] = phase(target, nodes, raw, "stream", result["normal"]["end"], 62, 600, 1)
                result["burst"] = phase(target, nodes, raw, "burst", result["stream"]["end"], 12, 34, 2)
            else:
                time.sleep(3)
                final = observe(target, nodes)
                require(final and final["diagnostics"]["seq_begin"] > start["diagnostics"]["seq_begin"],
                        "production publication stopped")
                raw.write(json.dumps({"phase": "production", "quality": "VALID", **final}) + "\n")
                result["final"] = final
        result["status"] = "PASS"
        args.out.write_text(json.dumps(result, indent=2) + "\n")
        print("Integrated diagnostics hardware PASS", args.profile)
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
