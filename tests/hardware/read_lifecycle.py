#!/usr/bin/env python3
"""Read and validate the completed task lifecycle fixture on ZET6."""
import argparse
import json
from pathlib import Path
import time

from read_timing import layout, record
from sample_stability import Tcl, decode, require


def sequence(events, kind, arg=None):
    matches = [e["sequence"] for e in events if e["event_type"] == kind and
               (arg is None or e["arg0"] == arg)]
    require(len(matches) == 1, f"Trace event {kind}/{arg}: {matches}")
    return matches[0]


def validate(frame, stack):
    require(frame["state"] == 2 and frame["completed"] == 6 and frame["end"] > frame["start"],
            "fixture state")
    previous = None
    for i, case in enumerate(frame["cases"]):
        ref = case["ref"]
        require(ref == case["self"] and ref["slot"] == 1 and ref["generation"] > 0 and
                case["stack"] == stack and case["ipsr"] == 0 and
                case["control"] & 2 and stack < case["psp"] <= stack + 4096,
                f"identity and PSP stack {i}")
        require((case["ready"], case["running"], case["terminated"]) == (1, 2, 3) and
                case["resumes"] == 1 and case["stop_initial"] == 0 and
                case["stop_after_exit"] == 2 and case["guard"] == 0xdeadc0de and
                case["no_permit"] == 5, f"Worker lifecycle {i}")
        clean = case["clean"]
        require(clean["state"] == 3 and
                all(clean[key] == 0 for key in ("reason", "result", "links", "count", "waiter")),
                f"reclaimable links {i}")
        records = case["trace"][:case["trace_count"]]
        events = [r for r in records if r["generation"] == ref["generation"]]
        require(all(r["slot"] == ref["slot"] for r in records) and
                all(a["sequence"] < b["sequence"] for a, b in zip(records, records[1:])),
                f"Trace order {i}")
        created = sequence(events, 1)
        terminated = sequence(events, 8)
        dispatches = [e["sequence"] for e in events if e["event_type"] == 12]
        require(bool(dispatches) and created < dispatches[0] <= dispatches[-1] < terminated,
                f"create/dispatch/exit {i}")
        if previous is not None:
            require(ref["generation"] == previous["generation"] + 1 and
                    case["old_snapshot"] == case["old_stop"] == 9, f"stale generation {i}")
            older = [r for r in records if r["generation"] == previous["generation"]]
            require(len(older) == 1 and sequence(older, 9) < created, f"reclaim old generation {i}")
        previous = ref
        before, after = case["before"], case["after"]
        if i < 2:
            require((before["state"], before["reason"], before["result"], before["waiter"]) == (4, 1, 1, 2)
                    and before["links"] & 2 and before["count"] == 0 and
                    after["state"] == 1 and after["reason"] == after["count"] == after["waiter"] == 0 and
                    case["stop"] == 0 and case["stop_seen"] == 1 and len(dispatches) >= 2,
                    f"Semaphore blocking and wake {i}")
            stop = sequence(events, 7)
            completed = sequence(events, 5, 4 if i == 0 else 2)
            require(dispatches[0] < min(stop, completed) < max(stop, completed) < dispatches[-1],
                    f"commits precede dispatch {i}")
            if i == 0:
                require(stop < completed and after["result"] == 4 and case["take"] == 10,
                        "STOP cancelled a PENDING Take")
            else:
                grant = case["granted"]
                require(completed < stop and grant == after and after["result"] == 2 and
                        case["give"] == case["take"] == 0, "Grant survived STOP")
        elif i == 5:
            require(before["state"] == after["state"] == case["before_due"]["state"] == 4 and
                    before["result"] == after["result"] == before["reason"] == 0 and
                    before["wake"] == after["wake"] == case["resumed"] and
                    case["stop"] == 0 and case["stop_seen"] == 1 and len(dispatches) >= 2 and
                    sequence(events, 7) < sequence(events, 10, before["wake"]) < dispatches[-1] and
                    all(e["event_type"] != 5 for e in events), "Delay kept original deadline")
        else:
            require(case["stop_seen"] == 0 and all(e["event_type"] != 7 for e in events),
                    f"natural exit {i}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    node = nodes["g_lifecycle_fixture"]
    address = node["address"]
    target = Tcl(6673)
    try:
        deadline = time.monotonic() + 20
        with args.out.with_suffix(".jsonl").open("x", buffering=1) as log:
            while time.monotonic() < deadline:
                state = target.words(address)[0]
                debug = target.words(0xe000edf0)[0]
                guards = [target.words(nodes["display_stacks"]["address"] + i * 4096)[0]
                          for i in range(2)] + target.words(nodes["idle_stack"]["address"])
                health = {"state": state, "tick": record(target, nodes["kernel_ticks"]),
                          "tim2_ms": record(target, nodes["g_sys_tick"]),
                          "assert_line": record(target, nodes["g_rtos_assert_line"]),
                          "faults": target.words(0xe000ed28, 2), "guards": guards}
                log.write(json.dumps(health) + "\n")
                require(health["assert_line"] == 0 and health["faults"] == [0, 0] and
                        not debug & ((1 << 17) | (1 << 19)) and guards == [0xdeadc0de] * 3,
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
        validate(frame, nodes["display_stacks"]["address"] + 4096)
        result["status"] = "PASS"
        args.out.write_text(json.dumps(result, indent=2) + "\n")
        print("STM32 lifecycle PASS", frame["completed"], "Worker lifetimes")
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
