#!/usr/bin/env python3
"""Observe the user's board RESET only. Never issues reset/halt/resume or writes."""
from pathlib import Path
import importlib.util,sys,json,time,traceback
sys.dont_write_bytecode=True
E=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('c2_live_monitor',E/'runtime_monitor.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
t=m.Tcl();out={'hardware':True,'source':'user physical board RESET','automatic_reset_commands':False,'samples':[],'transient_read_errors':[]}
try:
 current=m.sample(t);m.safe(current,True);out['observer_start']=current
 first=json.loads((E/'final-reset-reference.json').read_text()) if (E/'final-reset-reference.json').exists() else current
 out['before']=first
 (E/'final-reset-ready.json').write_text(json.dumps({'ready':True,'baseline_tick':first['kernel_ticks'],'baseline_tim2':first['tim2_ms'],'monotonic':first['monotonic']},indent=2)+'\n')
 deadline=time.monotonic()+600;seen=None
 with (E/'final-reset-samples.jsonl').open('w') as log:
  while time.monotonic()<deadline:
   time.sleep(.5)
   try:d=m.sample(t)
   except Exception as ex:
    out['transient_read_errors'].append({'monotonic':time.monotonic(),'error':str(ex)});continue
   out['samples'].append(d);log.write(json.dumps(d)+'\n');log.flush()
   if d['kernel_ticks']<first['kernel_ticks'] and d['tim2_ms']<first['tim2_ms'] and seen is None:seen=d['monotonic'];out['reset_observed_monotonic']=seen
   if d['state'] in ['reset','unknown']:continue
   m.safe(d,d['ready']!=0)
   if seen is not None and d['monotonic']-seen>=30 and d['ready']==1:
    out['after']=d;out['status']='PASS';break
  else:raise RuntimeError('No completed physical RESET observation within600s; no automatic recovery')
except Exception as ex:
 out['status']='NOT COMPLETED';out['error']=str(ex);out['traceback']=traceback.format_exc()
finally:
 (E/'final-reset-observation.json').write_text(json.dumps(out,indent=2)+'\n');t.close()
print(json.dumps({k:v for k,v in out.items() if k not in ['samples','before','after']},indent=2),flush=True)
if out['status']!='PASS':raise SystemExit(1)
