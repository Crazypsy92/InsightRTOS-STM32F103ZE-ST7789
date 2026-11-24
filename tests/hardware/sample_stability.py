#!/usr/bin/env python3
"""Fail-closed SWD reader for an already programmed A3 target (OpenOCD Tcl)."""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import time

TRAFFIC = ("key_attempts", "key_sent_ok", "key_queue_full", "key_wrong_context",
           "key_invalid_arg", "key_other_errors")
COUNTERS = ("success_count", "failure_count", "pending_count", "drop_count")

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def field(node, name):
    child = node["fields"][name]
    require(0 <= child["offset"] <= node["size"] - child["size"], f"{name}: ABI bounds")
    return child

def verify_layout(nodes):
    tracker = nodes["g_event_tracker"]
    history = field(tracker, "history")
    require(history["count"] == 8 and history["element"]["size"] == 24 and
            field(tracker, "latest_id")["offset"] == history["size"], "tracker ABI")
    export = nodes["g_diag_rt_export"]
    snapshots = field(export, "task_snapshots")
    snap = snapshots["element"]
    require(snapshots["count"] == 4 and snap["size"] == 56 and
            field(snap, "ref")["size"] == 8 and field(snap, "flags")["offset"] == 32, "RT ABI")
    require(field(export, "idle_snapshot")["offset"] == snapshots["offset"] + snapshots["size"],
            "RT array extent")
    for n in TRAFFIC:
        require(field(nodes["g_display_statistics"], n)["size"] == 4, "statistics ABI")
    for n in COUNTERS:
        require(field(tracker, n)["size"] == 4, "tracker counter ABI")

def read_layout(elf):
    command = ["gdb-multiarch", "-nx", "-batch", str(elf), "-x",
               str(Path(__file__).with_name("elf_layout.py"))]
    result = subprocess.check_output(command, text=True)
    rows = [l.removeprefix("A3_LAYOUT ") for l in result.splitlines() if l.startswith("A3_LAYOUT ")]
    require(len(rows) == 1, "ELF DWARF metadata missing")
    nodes = json.loads(rows[0])
    verify_layout(nodes)
    return nodes

def decode(data, node):
    require(len(data) == node["size"], "short/long target read")
    if "fields" in node:
        return {name: decode(data[f["offset"]: f["offset"] + f["size"]], f)
                for name in node["fields"] for f in [field(node, name)]}
    if "element" in node:
        element = node["element"]
        require(element["size"] * node["count"] == len(data), "array ABI")
        return [decode(data[i:i + element["size"]], element)
                for i in range(0, len(data), element["size"])]
    return int.from_bytes(data, "little", signed=node["signed"])

class Tcl:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=45)
    def cmd(self, text):
        self.socket.sendall(text.encode() + b"\x1a")
        answer = b""
        while b"\x1a" not in answer:
            piece = self.socket.recv(65536)
            require(piece, "Tcl closed")
            answer += piece
        result, tail = answer.split(b"\x1a", 1)
        require(not tail, "unexpected Tcl reply")
        return result.decode().strip()
    def words(self, addr, count=1):
        result = [int(x, 0) for x in self.cmd(f"read_memory {addr:#x} 32 {count}").split()]
        require(len(result) == count, "target word count")
        return result
    def data(self, addr, size):
        require(size % 4 == 0, "unaligned structure length")
        return b"".join(x.to_bytes(4, "little") for x in self.words(addr, size // 4))

def sample(target, nodes):
    def get(name):
        n = nodes[name]
        return decode(target.data(n["address"], n["size"]), n)
    export = nodes["g_diag_rt_export"]
    begin_address = export["address"] + field(export, "seq_begin")["offset"]
    for _ in range(5):
        seq = target.words(begin_address)[0]
        frame = get("g_diag_rt_export")
        if seq and not seq & 1 and seq == frame["seq_begin"] == frame["seq_end"] == target.words(begin_address)[0]:
            break
    else:
        return {"quality": "INCOMPLETE", "monotonic": time.monotonic()}
    tick = get("kernel_ticks")
    stats = get("g_display_statistics")
    tracker = get("g_event_tracker")
    refs = get("g_display_task_refs")
    tasks = frame["task_snapshots"] + [frame["idle_snapshot"]]
    clock = frame["clock_cap"]
    status = target.words(0xe000edf0)[0]
    faults = target.words(0xe000ed28, 2)
    guards = [target.words(nodes["display_stacks"]["address"] + 4096*i)[0] for i in range(4)]
    guards += target.words(nodes["idle_stack"]["address"])
    require(not status & ((1 << 17) | (1 << 19)) and faults == [0, 0], "halt/lockup/fault")
    require(get("g_rtos_assert_line") == 0 and all(g == 0xdeadc0de for g in guards), "assert or stack guard")
    require(stats["initialization_errors"] == stats["transport_errors"] == stats["display_fault"] == 0
            and stats["ready"] == 1, "LCD not healthy")
    require(all(stats[n] == 0 for n in TRAFFIC[3:]), "non-FULL ingress error")
    require(clock["flags"] == 1 and clock["frequency_hz"] == 72000000 and
            clock["tick_hz"] == 100 and all(t["flags"] == 1 for t in tasks), "RT clock flags unusable")
    require([t["ref"] for t in tasks] == refs + [{"slot": 8, "generation": 1}], "RT task ref")
    require([t["base_priority"] for t in tasks] == [5, 4, 2, 1, 0], "RT priorities")
    output = {"quality": "VALID" if (tick-frame["published_tick"]) % 2**32 <= 220 else "STALE",
              "monotonic": time.monotonic(), "kernel_ticks": tick, "tim2_ms": get("g_sys_tick"),
              "rt": frame, "stats": stats, "tracker": tracker, "guards": guards, "faults": faults}
    if "g_stress_request" in nodes:
        output["stress"] = {k: get("g_stress_" + k) for k in ("request", "remaining", "generated")}
    return output

def flow(start, end):
    a, b = (v["stats"] for v in (start, end))
    x, y = (v["tracker"] for v in (start, end))
    d = {k: b[k] - a[k] for k in TRAFFIC}
    d.update({k: y[k]-x[k] for k in COUNTERS})
    require(d["key_attempts"] == sum(d[k] for k in TRAFFIC[1:]), "ingress conservation")
    require(d["key_sent_ok"] == sum(d[k] for k in COUNTERS[:3]), "ACK conservation")
    require(d["drop_count"] == sum(d[k] for k in TRAFFIC[2:]), "drop conservation")
    require(y["pending_count"] == x["pending_count"] == 0, "endpoint not drained")
    return d

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", type=Path, required=True)
    ap.add_argument("--bin", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--mode", choices=["normal", "stress", "burst"], required=True)
    ap.add_argument("--port", type=int, default=6673)
    args = ap.parse_args()
    nodes = read_layout(args.elf)
    require((args.mode != "normal") == ("g_stress_request" in nodes), "wrong firmware profile")
    out = args.out_dir / args.mode
    out.mkdir(exist_ok=False)
    (out/"layout.json").write_text(json.dumps(nodes, indent=2) + "\n")
    t = Tcl(args.port)
    digest = hashlib.sha256(args.bin.read_bytes()).hexdigest()
    before = out / "flash-verify.bin"
    t.cmd(f"dump_image {before.resolve()} 0x08000000 {args.bin.stat().st_size}")
    require(hashlib.sha256(before.read_bytes()).hexdigest() == digest, "on-board image SHA mismatch")
    addr = nodes["g_stress_request"]["address"] if args.mode != "normal" else None
    duration = {"normal": 60, "stress": 300, "burst": 12}[args.mode]
    result = {"status": "FAIL", "mode": args.mode, "sha256": digest}
    try:
        with (out/"samples.jsonl").open("x", buffering=1) as log:
            start = sample(t, nodes)
            require(start["quality"] == "VALID", "invalid initial sample")
            log.write(json.dumps(start) + "\n")
            if addr is not None:
                require(start["stress"]["request"] == start["stress"]["remaining"] == 0, "stress already running")
                require(t.cmd(f"mww {addr:#x} {1 if args.mode == 'stress' else 2}") == "", "stress command failed")
            prev, valid, incomplete = start, 1, 0
            while True:
                time.sleep(5)
                current = sample(t, nodes)
                log.write(json.dumps(current) + "\n")
                if current["quality"] != "VALID":
                    incomplete += 1
                    require(incomplete <= 3, "too many invalid frames")
                    continue
                valid += 1
                for k in ("kernel_ticks", "tim2_ms"):
                    require(current[k] >= prev[k], f"{k} went backward")
                for a, b in zip(current["rt"]["task_snapshots"] + [current["rt"]["idle_snapshot"]],
                                prev["rt"]["task_snapshots"] + [prev["rt"]["idle_snapshot"]]):
                    require(a["charged_cycles"] >= b["charged_cycles"] and
                            a["dispatch_count"] >= b["dispatch_count"], "RT counter went backward")
                for k in TRAFFIC:
                    require(current["stats"][k] >= prev["stats"][k], f"{k} went backward")
                prev = current
                elapsed = current["monotonic"]-start["monotonic"]
                print(f"{args.mode}: {elapsed:.1f}s, attempts={current['stats']['key_attempts']}, "
                      f"settled={current['tracker']['success_count']}, full={current['stats']['key_queue_full']}", flush=True)
                if elapsed >= duration and (addr is None or current["stress"]["request"] == 0
                                             and current["tracker"]["pending_count"] == 0):
                    break
                require(elapsed <= duration+20, "stress timeout or backlog did not drain")
            counters = flow(start, current)
            if addr is not None:
                generated = current["stress"]["generated"]-start["stress"]["generated"]
                require(generated == (3000 if args.mode == "stress" else 34), "stress generator count")
                require(counters["key_attempts"] == generated, "external or missing injection during fixture")
            if args.mode == "burst":
                require(counters["key_queue_full"] > 0 and counters["key_sent_ok"] > 0, "burst did not exercise FULL/recovery")
            tick_hz = (current["kernel_ticks"]-start["kernel_ticks"])/elapsed
            tim2_hz = (current["tim2_ms"]-start["tim2_ms"])/elapsed
            require(95 < tick_hz < 105 and 950 < tim2_hz < 1050, "clock rates")
            result.update(status="PASS", elapsed_s=elapsed, valid=valid, incomplete=incomplete,
                          tick_hz=tick_hz, tim2_hz=tim2_hz, flow=counters,
                          start=start, end=current)
    except Exception as exc:
        result["error"] = str(exc)
        raise
    finally:
        try:
            if addr is not None:
                t.cmd(f"mww {addr:#x} 0")
                deadline = time.monotonic() + 2
                remaining = nodes["g_stress_remaining"]["address"]
                while t.words(addr)[0] or t.words(remaining)[0]:
                    require(time.monotonic() < deadline, "could not clear stress")
                    time.sleep(0.02)
                result["stress_disabled"] = True
        except Exception:
            result["status"] = "BLOCKED"
            raise
        finally:
            (out/"summary.json").write_text(json.dumps(result, indent=2)+"\n")
            t.socket.close()

if __name__ == "__main__":
    main()
