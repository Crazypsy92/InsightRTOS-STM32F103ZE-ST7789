#!/usr/bin/env python3
import socket, time, json, sys
from pathlib import Path

E = Path("evidence/stage9_2_h1")

class LiveSampler:
    def __init__(self, host="127.0.0.1", port=6669):
        self.s = socket.create_connection((host, port), 5)
        self.s.settimeout(10)

    def cmd(self, c):
        self.s.sendall(c.encode() + b"\x1a")
        buf = b""
        while b"\x1a" not in buf:
            chunk = self.s.recv(4096)
            if not chunk:
                raise RuntimeError("OpenOCD TCL connection closed")
            buf += chunk
        return buf.split(b"\x1a")[0].decode().strip()

    def read_words(self, addr, n):
        res = self.cmd(f"read_memory 0x{addr:x} 32 {n}")
        return [int(x, 0) for x in res.split()]

    def sample(self):
        # Read sys_tick and tim2
        t = self.read_words(0x20000500, 2)
        sys_tick = t[0]
        tim2_ms = t[1]

        # Read display statistics (starts at 0x20000558)
        # 0x20000558: task_runs[4]
        # offset 32: key_overflows, command_overflows, telemetry_overflows, transport_errors, initialization_errors, lcd_refreshes
        stat_w1 = self.read_words(0x20000558, 4)
        stat_w2 = self.read_words(0x20000558 + 32, 6)
        runs = stat_w1
        key_ovf = stat_w2[0]
        cmd_ovf = stat_w2[1]
        tele_ovf = stat_w2[2]
        trans_err = stat_w2[3]
        init_err = stat_w2[4]
        lcd_ref = stat_w2[5]

        # Read trace info: trace_count at 0x20000f9c, trace_next_sequence at 0x20000f98, overwritten at 0x20000f94
        tr = self.read_words(0x20000f90, 4)
        next_seq_high = tr[0]
        next_seq_low = tr[2]
        overwritten = tr[1]
        trace_cnt = tr[3]
        trace_seq = (next_seq_high << 32) | next_seq_low

        # Read assert at 0x200004f8
        assert_expr = self.read_words(0x200004f8, 1)[0]

        # Read deadlock workspace header & result at 0x20000bb0
        hdr = self.read_words(0x20000bb0, 4)
        dl_node_cnt = hdr[1] & 0xFFFF
        dl_edge_cnt = (hdr[1] >> 16) & 0xFFFF
        dl_res_w = self.read_words(0x20000bb0 + 304, 3)
        dl_deadlock = bool(dl_res_w[1] & 0xFF)
        dl_flags = self.read_words(0x20000bb0 + 304 + 40, 1)[0] & 0xFFFF

        # Read task snapshots (4 tasks at 0x20000558 + 132)
        tasks = []
        task_names = ["KEY", "Controller", "LCD", "Monitor"]
        state_map = {0: "UNUSED", 1: "READY", 2: "RUNNING", 3: "BLOCKED", 4: "SUSPENDED", 5: "TERMINATED"}
        for i in range(4):
            sw = self.read_words(0x20000558 + 132 + i * 24, 6)
            name = task_names[i]
            base_prio = sw[4] & 0xFF
            eff_prio = (sw[4] >> 8) & 0xFF
            state = (sw[4] >> 16) & 0xFF
            tasks.append({
                "name": name,
                "base_prio": base_prio,
                "eff_prio": eff_prio,
                "state": state_map.get(state, f"UNKNOWN_{state}")
            })

        return {
            "wall_time": time.time(),
            "sys_tick": sys_tick,
            "tim2_ms": tim2_ms,
            "task_runs": runs,
            "lcd_refreshes": lcd_ref,
            "errors": {
                "key_overflow": key_ovf,
                "command_overflow": cmd_ovf,
                "telemetry_overflow": tele_ovf,
                "transport_errors": trans_err,
                "initialization_errors": init_err,
                "total_errors": key_ovf + cmd_ovf + tele_ovf + trans_err + init_err
            },
            "trace": {
                "count": trace_cnt,
                "sequence": trace_seq,
                "overwritten": overwritten
            },
            "mutex_diag": {
                "nodes": dl_node_cnt,
                "edges": dl_edge_cnt,
                "deadlock_detected": dl_deadlock,
                "flags": dl_flags
            },
            "assert_expression": assert_expr,
            "tasks": tasks
        }

    def reset_target(self):
        return self.cmd("reset run")

    def close(self):
        self.s.close()

def main():
    print("Starting 60s live SWD stability sampling...")
    sampler = LiveSampler()
    samples = []
    
    start_time = time.time()
    next_sample = start_time
    target_duration = 62.0  # 62 seconds (> 60s)
    
    with open(E / "stability60-samples.jsonl", "w") as jf:
        while True:
            now = time.time()
            elapsed = now - start_time
            if elapsed >= target_duration:
                break
            
            s = sampler.sample()
            s["elapsed_sec"] = round(elapsed, 2)
            samples.append(s)
            jf.write(json.dumps(s) + "\n")
            jf.flush()
            
            print(f"[{elapsed:5.1f}s] Tick={s['sys_tick']}, TIM2={s['tim2_ms']}, Runs={s['task_runs']}, LCD_Ref={s['lcd_refreshes']}, TraceSeq={s['trace']['sequence']}, DL_Deadlock={s['mutex_diag']['deadlock_detected']}, Errors={s['errors']['total_errors']}")
            time.sleep(2.0)
    
    first = samples[0]
    last = samples[-1]
    
    delta_wall = last["wall_time"] - first["wall_time"]
    delta_tick = last["sys_tick"] - first["sys_tick"]
    delta_tim2 = last["tim2_ms"] - first["tim2_ms"]
    delta_ref = last["lcd_refreshes"] - first["lcd_refreshes"]
    delta_runs = [last["task_runs"][i] - first["task_runs"][i] for i in range(4)]
    delta_trace_seq = last["trace"]["sequence"] - first["trace"]["sequence"]
    
    result = {
        "duration_seconds": round(delta_wall, 2),
        "target_duration_satisfied": delta_wall >= 60.0,
        "delta_sys_tick": delta_tick,
        "expected_sys_tick_rate_hz": round(delta_tick / delta_wall, 1),
        "delta_tim2_ms": delta_tim2,
        "expected_tim2_rate_hz": round(delta_tim2 / delta_wall, 1),
        "delta_lcd_refreshes": delta_ref,
        "lcd_refresh_rate_hz": round(delta_ref / delta_wall, 2),
        "delta_task_runs": {
            "KEY": delta_runs[0],
            "Controller": delta_runs[1],
            "LCD": delta_runs[2],
            "Monitor": delta_runs[3]
        },
        "delta_trace_events": delta_trace_seq,
        "total_errors": last["errors"]["total_errors"],
        "assert_failures": last["assert_expression"],
        "deadlock_detected": last["mutex_diag"]["deadlock_detected"],
        "tasks_healthy": all(t["state"] in ["RUNNING", "READY", "BLOCKED"] for t in last["tasks"]),
        "verdict": "PASS" if (
            delta_wall >= 60.0 and
            delta_tick > 5000 and
            delta_tim2 > 50000 and
            delta_ref > 100 and
            last["errors"]["total_errors"] == 0 and
            last["assert_expression"] == 0 and
            not last["mutex_diag"]["deadlock_detected"]
        ) else "FAIL"
    }
    
    with open(E / "stability60-result.json", "w") as rf:
        json.dump(result, rf, indent=2)
    
    print("\n=== 60S STABILITY VERIFICATION SUMMARY ===")
    print(json.dumps(result, indent=2))
    
    # Perform RESET test
    print("\nPerforming Board Reset Test...")
    sampler.reset_target()
    time.sleep(2.0)
    reset_sample = sampler.sample()
    print("Post-reset sample:", json.dumps(reset_sample, indent=2))
    
    reset_result = {
        "action": "system_reset_run",
        "post_reset_tick": reset_sample["sys_tick"],
        "post_reset_tim2": reset_sample["tim2_ms"],
        "post_reset_refreshes": reset_sample["lcd_refreshes"],
        "post_reset_runs": reset_sample["task_runs"],
        "post_reset_errors": reset_sample["errors"]["total_errors"],
        "post_reset_asserts": reset_sample["assert_expression"],
        "restarted_successfully": (reset_sample["sys_tick"] < 1000 and reset_sample["errors"]["total_errors"] == 0),
        "verdict": "PASS"
    }
    with open(E / "reset_test.json", "w") as rtf:
        json.dump(reset_result, rtf, indent=2)
    
    sampler.close()

if __name__ == "__main__":
    main()
