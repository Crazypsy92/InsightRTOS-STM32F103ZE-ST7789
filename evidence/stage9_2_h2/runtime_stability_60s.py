import socket
import time
import json

def read_mem(s, addr, count):
    cmd = f'read_memory {addr} 32 {count}\x1a'
    s.sendall(cmd.encode('utf-8'))
    data = b''
    while True:
        chunk = s.recv(4096)
        data += chunk
        if b'\x1a' in chunk:
            break
    resp = data.decode('utf-8', errors='replace').rstrip('\x1a').strip()
    return [int(x, 16) for x in resp.split()]

def main():
    s = socket.socket()
    s.connect(('127.0.0.1', 6669))
    print("Connected to OpenOCD TCL server on port 6669.")

    # Symbol addresses
    # kernel_ticks: 0x200024e4 (1 word)
    # g_tim2_ms_count: 0x200006f4 (1 word)
    # g_display_statistics: 0x20000748 (45 words)
    # dl_work.0: 0x20000da0 -> dl_res at offset 304: 0x20000ed0

    samples = []
    start_time = time.time()
    next_sample = start_time

    print("Beginning 60-second continuous stability monitoring...")

    while True:
        now = time.time()
        elapsed = now - start_time
        if elapsed >= 61.0: # >= 60s
            break

        if now >= next_sample:
            kt = read_mem(s, '0x200024e4', 1)[0]
            tim2 = read_mem(s, '0x200006f4', 1)[0]
            stats = read_mem(s, '0x20000748', 20)
            dl_res = read_mem(s, '0x20000ed0', 2)

            # dl_res[0]: byte 0 is deadlock_detected, byte 1 is cycle_length
            dl_deadlock = bool((dl_res[0] & 0xFF) != 0)

            sample = {
                'elapsed_s': round(elapsed, 2),
                'kernel_ticks': kt,
                'tim2_ms': tim2,
                'task_runs': {
                    'KEY': stats[0],
                    'Controller': stats[1],
                    'LCD': stats[2],
                    'Monitor': stats[3]
                },
                'lcd_refreshes': stats[13],
                'transport_errors': stats[11],
                'init_errors': stats[12],
                'deadlock_detected': dl_deadlock
            }
            samples.append(sample)
            print(f"[{sample['elapsed_s']:5.1f}s] RTOS_tick={kt:6d} TIM2_ms={tim2:7d} LCD_ref={sample['lcd_refreshes']:4d} CTRL_runs={sample['task_runs']['Controller']:6d} Errs={sample['transport_errors']} DL={sample['deadlock_detected']}")
            next_sample += 2.0

        time.sleep(0.1)

    s.close()
    duration = samples[-1]['elapsed_s'] - samples[0]['elapsed_s']
    d_kt = samples[-1]['kernel_ticks'] - samples[0]['kernel_ticks']
    d_tim2 = samples[-1]['tim2_ms'] - samples[0]['tim2_ms']
    d_ref = samples[-1]['lcd_refreshes'] - samples[0]['lcd_refreshes']
    d_ctrl = samples[-1]['task_runs']['Controller'] - samples[0]['task_runs']['Controller']
    d_lcd = samples[-1]['task_runs']['LCD'] - samples[0]['task_runs']['LCD']
    d_mon = samples[-1]['task_runs']['Monitor'] - samples[0]['task_runs']['Monitor']

    summary = {
        'duration_seconds': round(duration, 2),
        'target_duration_satisfied': duration >= 60.0,
        'delta_rtos_kernel_ticks': d_kt,
        'rtos_kernel_tick_rate_hz': round(d_kt / duration, 2),
        'delta_tim2_ms': d_tim2,
        'tim2_ms_rate_hz': round(d_tim2 / duration, 2),
        'delta_lcd_refreshes': d_ref,
        'lcd_refresh_rate_hz': round(d_ref / duration, 2),
        'delta_task_runs': {
            'KEY': samples[-1]['task_runs']['KEY'] - samples[0]['task_runs']['KEY'],
            'Controller': d_ctrl,
            'LCD': d_lcd,
            'Monitor': d_mon
        },
        'total_transport_errors': samples[-1]['transport_errors'],
        'total_init_errors': samples[-1]['init_errors'],
        'deadlock_detected': samples[-1]['deadlock_detected'],
        'verdict': 'PASS' if (duration >= 60.0 and samples[-1]['transport_errors'] == 0 and not samples[-1]['deadlock_detected']) else 'FAIL'
    }

    with open('evidence/stage9_2_h2/hardware_60s.json', 'w') as f:
        json.dump(summary, f, indent=2)

    with open('evidence/stage9_2_h2/hardware_60s_samples.jsonl', 'w') as f:
        for samp in samples:
            f.write(json.dumps(samp) + '\n')

    print("\n=== 60-SECOND STABILITY SUMMARY ===")
    print(json.dumps(summary, indent=2))

if __name__ == '__main__':
    main()
