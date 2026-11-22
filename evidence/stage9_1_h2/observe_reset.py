#!/usr/bin/env python3
"""Observe board RESET. Pure read-only monitoring; never issues reset/halt/resume."""
from pathlib import Path
import importlib.util, sys, json, time, traceback

sys.dont_write_bytecode = True
E = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('h2_live_monitor', E / 'runtime_monitor.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

t = m.Tcl()
out = {
    'hardware': True,
    'source': 'board RESET',
    'samples': [],
    'transient_read_errors': []
}

try:
    first = m.sample(t)
    out['before'] = first
    print(f"RESET OBSERVER READY. Current tick={first['kernel_ticks']}, tim2={first['tim2_ms']}", flush=True)
    deadline = time.monotonic() + 300
    seen = None
    with (E / 'reset-samples.jsonl').open('w') as log:
        while time.monotonic() < deadline:
            time.sleep(0.5)
            try:
                d = m.sample(t)
            except Exception as ex:
                out['transient_read_errors'].append({'monotonic': time.monotonic(), 'error': str(ex)})
                continue
            out['samples'].append(d)
            log.write(json.dumps(d) + '\n')
            log.flush()
            if d['kernel_ticks'] < first['kernel_ticks'] and d['tim2_ms'] < first['tim2_ms'] and seen is None:
                seen = d['monotonic']
                out['reset_observed_monotonic'] = seen
                print(f"RESET DETECTED at monotonic={seen}! New tick={d['kernel_ticks']}", flush=True)
            if seen is not None and (d['monotonic'] - seen >= 5) and d['ready'] == 1:
                out['after'] = d
                out['status'] = 'PASS'
                print(f"RESET RECOVERY CONFIRMED: tick={d['kernel_ticks']}, tim2={d['tim2_ms']}, ready={d['ready']}", flush=True)
                break
        else:
            out['status'] = 'WAITING_FOR_USER_RESET'
except Exception as ex:
    out['status'] = 'ERROR'
    out['error'] = str(ex)
    out['traceback'] = traceback.format_exc()
finally:
    (E / 'reset-observation.json').write_text(json.dumps(out, indent=2) + '\n')
    t.close()

if __name__ == '__main__':
    pass
