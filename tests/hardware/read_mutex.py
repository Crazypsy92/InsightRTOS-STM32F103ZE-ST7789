#!/usr/bin/env python3
"""Verify frozen Mutex validation data from a running ZET6 over MEM-AP."""
import argparse
import json
from pathlib import Path
import time

from read_timing import layout, record
from sample_stability import Tcl, decode, field, require

LOW, MEDIUM, HIGH, PEER, OBSERVER = range(5)
CASE_NAMES = ("handoff", "classic_pi", "transitive_pi", "finite_abba")
PRIORITIES = [2, 4, 5, 5, 6]
# rtos_status_t, rtos_task_state_t and DL v1 encodings.
STATE, UNAVAILABLE, TIMEOUT, NOT_OWNER = 2, 5, 7, 8
READY, BLOCKED = 1, 4


def edge(case, frame, source, owner):
    node = case["graph"]["nodes"][source]
    src, dst = frame["refs"][source], frame["refs"][owner]
    require((node["slot"], node["generation"]) == (src["slot"], src["generation"]) and
            (node["owner_slot"], node["owner_generation"]) == (dst["slot"], dst["generation"]),
            "bad wait-for identity/generation")
    require(node["state"] == BLOCKED and node["resource_id"] and node["edge_flags"] & 1,
            "wait-for edge must be finite and blocked")
    return node


def validate(frame):
    require(frame["state"] == 2 and frame["priorities"] == PRIORITIES and
            [ref["slot"] for ref in frame["refs"]] == list(range(5)) and
            all(ref["generation"] > 0 for ref in frame["refs"]), "report/task identity")
    require(frame["end_tick"] > frame["start_tick"], "fixture did not execute")
    reports = dict(zip(CASE_NAMES, frame["cases"], strict=True))
    rounds = (100, 100, 1, 1)
    orders = ([HIGH, PEER, MEDIUM], [LOW, HIGH, MEDIUM], [LOW, MEDIUM, HIGH], [LOW, MEDIUM])
    for (name, case), expected_rounds, want_order in zip(reports.items(), rounds, orders, strict=True):
        require(case["pass_rounds"] == expected_rounds and
                [c["worker"] for c in case["order"][:case["order_count"]]] == want_order, name)
        require(not any(case[k] for k in ("owner_a", "owner_b", "waiter_a", "waiter_b")),
                f"{name}: mutex state not drained")
        require([x["effective_priority"] for x in case["final"]] == PRIORITIES[:4] and
                all(x["state"] == BLOCKED for x in case["final"]), f"{name}: workers did not recover")
        graph = case["graph"]
        header = graph["header"]
        require(header["node_count"] == 5 and header["flags"] == 0 and
                header["captured"] == header["analyzed"] == 1 and
                graph["result"]["capture_tick"] == header["capture_tick"], f"{name}: invalid graph")
        require(0 < case["switches_count"] <= 16, f"{name}: no trace evidence")
        for r, e in zip(case["switches"][:case["switches_count"]], case["explanations"], strict=False):
            require(r["event_type"] == e["kind"] == 12 and
                    r["slot"] == e["primary_slot"] and not e["primary_stale"] and not e["related_stale"],
                    f"{name}: invalid trace")
        if name != "finite_abba":
            require(not graph["result"]["deadlock_detected"], f"{name}: unexpected cycle")
    handoff, classic, transitive, abba = reports.values()
    require(handoff["owner_lock"] == handoff["owner_unlock"] == 0 and
            handoff["owner_repeat"] == STATE and handoff["direct_owner"] == HIGH + 1 and
            handoff["direct_state"] == READY, "handoff before waiter resumed")
    require(handoff["graph"]["header"]["edge_count"] == 3 and
            handoff["workers"][HIGH]["not_owner"] == NOT_OWNER and
            handoff["workers"][HIGH]["no_wait"] == UNAVAILABLE, "Mutex rejection")
    for task in (HIGH, PEER, MEDIUM):
        edge(handoff, frame, task, OBSERVER)
        require(handoff["workers"][task]["owner_when_granted"] == task + 1, "grant owner")
    edge(classic, frame, HIGH, LOW)
    low, medium = classic["ready"][:2]
    require(low["state"] == medium["state"] == READY and low["base_priority"] == 2 and
            low["effective_priority"] == 5 and medium["effective_priority"] == 4 and
            classic["graph"]["nodes"][LOW]["edge_flags"] & 2, "classic PI Ready comparison")
    slots = [r["slot"] for r in classic["switches"][:classic["switches_count"]]]
    require(LOW in slots and MEDIUM in slots and slots.index(LOW) < slots.index(MEDIUM), "classic Trace")
    edge(transitive, frame, HIGH, MEDIUM)
    edge(transitive, frame, MEDIUM, LOW)
    require(transitive["ready"][LOW]["effective_priority"] == 4 and
            all(transitive["graph"]["nodes"][i]["effective_priority"] == 5 and
                transitive["graph"]["nodes"][i]["edge_flags"] & 2 for i in (LOW, MEDIUM)), "transitive PI")
    n_low = edge(abba, frame, LOW, MEDIUM)
    n_med = edge(abba, frame, MEDIUM, LOW)
    require(abba["graph"]["header"]["edge_count"] == abba["graph"]["header"]["mutex_count"] == 2,
            "ABBA resources")
    result = abba["graph"]["result"]
    require(result["deadlock_detected"] and result["cycle_length"] == result["finite_edge_count"] == 2 and
            result["flags"] == 3 and result["cycle_tasks"][:2] == [LOW, MEDIUM] and
            result["cycle_mutexes"][:2] == [n_low["resource_id"], n_med["resource_id"]] and
            result["capture_tick"] < result["earliest_due_tick"] == n_low["wake_tick"] ==
            abba["timeout_a"] < abba["timeout_b"] == n_med["wake_tick"], "finite ABBA diagnosis")
    low, med = abba["workers"][:2]
    require(low["wait"] == TIMEOUT and low["tick_resume"] == abba["timeout_a"] and
            low["release"] == med["wait"] == med["release"] == 0 and
            med["owner_when_granted"] == MEDIUM + 1 and med["tick_resume"] < abba["timeout_b"],
            "timeout recovery/direct transfer")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    node = nodes["g_mutex_fixture"]
    addr = node["address"]
    state_addr = addr + field(node, "state")["offset"]
    target = Tcl(6673)
    try:
        deadline = time.monotonic() + 40
        with args.out.with_suffix(".jsonl").open("x", buffering=1) as log:
            while True:
                state, scenario, round_no = target.words(addr, 3)
                tick = record(target, nodes["kernel_ticks"])
                fault = target.words(0xe000ed28, 2)
                assert_line = record(target, nodes["g_rtos_assert_line"])
                status = target.words(0xe000edf0)[0]
                guards = [target.words(nodes["display_stacks"]["address"] + i * 4096)[0]
                          for i in range(5)] + target.words(nodes["idle_stack"]["address"])
                health = {"state": state, "scenario": scenario, "round": round_no, "tick": tick,
                          "tim2_ms": record(target, nodes["g_sys_tick"]), "assert_line": assert_line,
                          "faults": fault, "guards": guards}
                log.write(json.dumps(health) + "\n")
                require(assert_line == 0 and fault == [0, 0] and not status & ((1 << 17) | (1 << 19)),
                        f"assert/fault/halt: {health}")
                require(guards == [0xdeadc0de] * 6, "fixture stack guard")
                if state == 2:
                    break
                require(time.monotonic() < deadline, "fixture did not complete")
                time.sleep(1)
        raw = target.data(addr, node["size"])
        frame = decode(raw, node)
        require(target.words(state_addr)[0] == 2 and target.data(addr, node["size"]) == raw,
                "unstable frozen report")
        args.out.with_suffix(".raw.hex").write_text(raw.hex() + "\n")
        args.out.write_text(json.dumps({"report": frame, "health": health}, indent=2) + "\n")
        validate(frame)
        args.out.write_text(json.dumps({"status": "PASS", "report": frame, "health": health}, indent=2) + "\n")
        print("STM32 Mutex fixture PASS", frame["end_tick"] - frame["start_tick"])
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
