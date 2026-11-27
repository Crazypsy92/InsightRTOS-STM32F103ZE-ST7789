#!/usr/bin/env python3
"""Read and validate the completed IPC cancellation fixture on ZET6."""
import argparse
import json
from pathlib import Path
import time

from read_timing import layout, record
from sample_stability import Tcl, decode, require


def events(window, ref):
    return [r for r in window["records"][:window["count"]]
            if (r["slot"], r["generation"]) == (ref["slot"], ref["generation"])]


def cancellation(window, ref, reason, sticky):
    trace = events(window, ref)
    cancelled = [r for r in trace if r["event_type"] == 5 and
                 (r["arg0"], r["arg1"]) == (4, reason)]
    require(len(cancelled) == 1, f"unique cancellation trace for {ref}")
    sequence = cancelled[0]["sequence"]
    require(any(r["event_type"] == 3 and r["sequence"] < sequence for r in trace) and
            any(r["event_type"] == 12 and r["sequence"] > sequence for r in trace),
            f"blocked -> cancelled -> dispatched for {ref}")
    stops = [r for r in trace if r["event_type"] == 7]
    require(len(stops) == int(sticky) and (not sticky or stops[0]["sequence"] < sequence),
            f"STOP trace for {ref}")


def validate(frame):
    require(frame["state"] == 2 and frame["end"] > frame["start"], "fixture state")
    require(frame["mutex_cancel"] == [0, 0] and frame["mutex_wait"] == [10, 10] and
            frame["unlock"] == [0, 0] and frame["high_stop"] == 1 and
            frame["mutex_running"] == [2, 2] and frame["mutex_ipsr"] == [0, 0],
            "Mutex returns and Thread resume")
    priorities = [[5, 5, 5], [4, 4, 5], [2, 4, 5], [2, 4, 5]]
    for i, view in enumerate(frame["mutex"]):
        require([s["effective_priority"] for s in view["tasks"]] == priorities[i] and
                view["owner"] == ([1, 2] if i < 3 else [0, 0]) and
                view["waiter"] == [[2, 3], [2, 0], [0, 0], [0, 0]][i] and
                view["owner_next"] == [0, 0], f"Mutex state {i}")
        graph = view["graph"]
        nodes = graph["nodes"][:graph["header"]["node_count"]]
        edges = {(n["slot"], n["owner_slot"]) for n in nodes if n["edge_flags"] & 1}
        require(edges == [{(2, 1), (1, 0)}, {(1, 0)}, set(), set()][i] and
                graph["header"]["edge_count"] == len(edges), f"PI graph {i}")
        for node in nodes:
            require(node["generation"] == frame["refs"][node["slot"]]["generation"] and
                    (not node["edge_flags"] & 1 or node["owner_generation"] ==
                     frame["refs"][node["owner_slot"]]["generation"]), f"PI graph identity {i}")
    require([[s["state"] for s in v["tasks"]][1:] for v in frame["mutex"]] ==
            [[4, 4], [4, 1], [1, 1], [4, 3]], "Mutex waiter states")
    cancellation(frame["trace"][0], frame["refs"][2], 2, True)
    cancellation(frame["trace"][0], frame["refs"][1], 2, False)
    for i, case in enumerate(frame["queue"]):
        before, ready, final = (case[k] for k in ("before", "ready", "final"))
        require((before["state"], ready["state"], case["running"], final["state"]) == (4, 1, 2, 3) and
                before["reason"] == 3 + i and before["result"] == 1 and before["buffer"] and
                ready["result"] == 4 and final["result"] == 0, f"Queue states {i}")
        require(all(before[k] == ready[k] == final[k] for k in ("count", "head", "tail", "stored")) and
                before["count"] == 1 - i and
                all(ready[k] == final[k] == 0 for k in ("buffer", "sender", "receiver", "reason")) and
                final["links"] == 0, f"Queue cleanup {i}")
        require(case["cancel"] == case["pull"] == case["ipsr"] == 0 and case["wait"] == 10 and
                case["empty"] == 5 and case["visible_stop"] == 1 - i and
                (case["payload_before"], case["payload_after"], case["received"]) ==
                [(0x3210abba, 0x6543abba, 0x1234abcd),
                 (0xdead1978, 0xdead1978, 0x77aabbff)][i], f"Queue payload {i}")
        cancellation(frame["trace"][i + 1], frame["refs"][i], 3 + i, i == 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    node = nodes["g_ipc_cancel_fixture"]
    address = node["address"]
    target = Tcl(6673)
    try:
        deadline = time.monotonic() + 20
        with args.out.with_suffix(".jsonl").open("x", buffering=1) as log:
            while time.monotonic() < deadline:
                state = target.words(address)[0]
                debug = target.words(0xe000edf0)[0]
                guards = [target.words(nodes["display_stacks"]["address"] + i * 4096)[0]
                          for i in range(4)] + target.words(nodes["idle_stack"]["address"])
                health = {"state": state, "tick": record(target, nodes["kernel_ticks"]),
                          "tim2_ms": record(target, nodes["g_sys_tick"]),
                          "assert_line": record(target, nodes["g_rtos_assert_line"]),
                          "faults": target.words(0xe000ed28, 2), "guards": guards}
                log.write(json.dumps(health) + "\n")
                require(health["assert_line"] == 0 and health["faults"] == [0, 0] and
                        not debug & ((1 << 17) | (1 << 19)) and guards == [0xdeadc0de] * 5,
                        f"fixture halted or faulted: {health}")
                if state == 2:
                    break
                time.sleep(0.5)
            else:
                raise RuntimeError("fixture did not finish before deadline")
        raw = target.data(address, node["size"])
        frame = decode(raw, node)
        require(raw == target.data(address, node["size"]), "unstable fixture export")
        args.out.with_suffix(".raw.hex").write_text(raw.hex() + "\n")
        result = {"report": frame, "health": health}
        args.out.write_text(json.dumps(result, indent=2) + "\n")
        validate(frame)
        result["status"] = "PASS"
        args.out.write_text(json.dumps(result, indent=2) + "\n")
        print("STM32 IPC cancellation PASS: Mutex, Queue Send and Queue Receive")
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
