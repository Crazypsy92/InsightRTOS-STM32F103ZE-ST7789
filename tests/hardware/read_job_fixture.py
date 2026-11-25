#!/usr/bin/env python3
"""Validate the B1 Job fixture published by the running target."""
import argparse
import json
from pathlib import Path

from sample_stability import Tcl, decode, field, read_layout, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    nodes = read_layout(args.elf)
    fixture = nodes["g_job_fixture"]
    addr = fixture["address"]
    begin = addr + field(fixture, "seq_begin")["offset"]
    target = Tcl(6673)
    try:
        before = target.words(begin)[0]
        report = decode(target.data(addr, fixture["size"]), fixture)
        require(before == report["seq_begin"] == report["seq_end"] ==
                target.words(begin)[0] == 2, "fixture publication incomplete")
        require(report["ref"]["slot"] == 4 and report["ref"]["generation"] > 0,
                "fixture task identity")
        on_time, late = report["on_time"], report["late"]
        require((on_time["completed_jobs"], on_time["deadline_misses"],
                 on_time["last_finish_ticks"]) == (1, 0, 1), "exact deadline")
        require((late["completed_jobs"], late["deadline_misses"],
                 late["last_finish_ticks"]) == (2, 1, 2), "deadline miss")
        budget = report["over_budget"]
        require(budget["completed_jobs"] == 3 and budget["budget_overruns"] == 1 and
                budget["deadline_misses"] == 1 and budget["flags"] & (1 << 4) and
                budget["last_budget_cycles"] > report["budget_cycles"] == 100000,
                "cycle overrun")
        aborted = report["aborted"]
        require(aborted["aborted_jobs"] == 1 and aborted["active_job_id"] == 0 and
                aborted["invalid_jobs"] == 0, "job abort")
        report["guard"] = target.words(nodes["display_stacks"]["address"] + 4 * 4096)[0]
        require(report["guard"] == 0xdeadc0de, "fixture stack guard")
        report["status"] = "PASS"
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print("STM32 Job fixture PASS")
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
