#!/usr/bin/env python3
"""Read and validate the completed TIM2/Semaphore fixture on ZET6."""
import argparse
import json
from pathlib import Path
import time

from read_timing import layout, record
from sample_stability import Tcl, decode, require


def check_dispatch(trace, why, ref):
    require(trace["event_type"] == why["kind"] == 12 and
            trace["slot"] == why["primary_slot"] == ref["slot"] and
            trace["generation"] == why["primary_generation"] == ref["generation"] and
            not why["primary_stale"], "Trace dispatch identity")


def validate(frame):
    require(frame["state"] == 2 and frame["priorities"] == [2, 5, 5, 6] and
            [ref["slot"] for ref in frame["refs"]] == list(range(4)) and
            frame["waiting_count"] == 0 and frame["take_count"] == 11, "fixture state")
    require(frame["irq_samples"] == 6 and frame["fifo_first"] == 6, "scenario accounting")
    high = frame["refs"][1]
    for i, irq in enumerate(frame["irq"]):
        index = frame["irq_take_index"][i]
        take = frame["takes"][index]
        trace = frame["irq_dispatch"][i]
        require(index == i and irq["ipsr"] == 44 and irq["give"] == 0 and irq["pendsv_pending"] == 1,
                f"IRQ {i}: source/return/PendSV")
        require(irq["waiter_before"] == high["slot"] + 1 and irq["waiter_after"] == 0 and
                irq["state_before"] == 4 and irq["state_after"] == 1 and
                irq["count_before"] == irq["count_after"] == 0, f"IRQ {i}: direct grant")
        require(irq["take_before"] == irq["take_after"] == index and
                take["irq_samples"] == i + 1 and take["ipsr"] == 0 and take["take"] == 0 and
                take["worker"] == high["slot"] and take["count"] == 0,
                f"IRQ {i}: ISR/Thread order or phantom permit")
        require(irq["tick"] == take["tick"] == trace["tick"] < irq["due_tick"], f"IRQ {i}: time")
        check_dispatch(trace, frame["irq_explain"][i], high)
    near = frame["irq"][-1]
    require(near["due_tick"] - near["tick"] == 1, "give was not near deadline")
    for j, worker in enumerate((1, 2, 0)):
        take = frame["takes"][frame["fifo_first"] + j]
        trace = frame["fifo_dispatch"][j]
        require(frame["fifo_head_before"][j] == worker + 1 and
                take["worker"] == worker and take["take"] == take["count"] == take["ipsr"] == 0 and
                trace["tick"] <= take["tick"], "priority/FIFO result")
        check_dispatch(trace, frame["fifo_explain"][j], frame["refs"][worker])
    timeout = frame["takes"][frame["timeout_index"]]
    cancelled = frame["takes"][frame["cancelled_index"]]
    require(frame["timeout_index"] == 9 and timeout["worker"] == 1 and timeout["take"] == 7 and
            timeout["tick"] == frame["timeout_due"] and timeout["count"] == timeout["ipsr"] == 0,
            "timeout outcome")
    require(frame["cancelled_index"] == 10 and cancelled["take"] == 10 and
            cancelled["worker"] == 2 and cancelled["count"] == cancelled["ipsr"] == 0,
            "cancellation outcome")
    expected = [(0, 0), (0, 1), (0, 2), (0, 3), (6, 3), (0, 2), (0, 1), (0, 0),
                (5, 0), (4, 0), (0, 1), (6, 1), (0, 0)]
    expected += [(5, 0)] * 6 + [(0, 1), (0, 0), (5, 0)]
    require(frame["count_checks"] == len(expected) and
            [(v["code"], v["count"]) for v in frame["counts"][:len(expected)]] == expected,
            "semaphore count conservation")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    node = nodes["g_semaphore_fixture"]
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
        print("STM32 Semaphore PASS", frame["irq_samples"], "real TIM2 grants")
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
