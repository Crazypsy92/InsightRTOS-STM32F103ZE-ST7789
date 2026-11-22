#!/usr/bin/env python3
"""Read-only live SWD sampling. No reset, load, register injection or Flash writes."""
from pathlib import Path
import socket,time,json,struct,sys,traceback
E=Path(__file__).resolve().parent
S=json.loads((E/'runtime-schema.json').read_text())
class Tcl:
 def __init__(self):self.s=socket.create_connection(('127.0.0.1',6669),10);self.s.settimeout(20)
 def cmd(self,c):
  self.s.sendall(c.encode()+b'\x1a');b=b''
  while b'\x1a' not in b:
   v=self.s.recv(65536)
   if not v:raise RuntimeError('Tcl closed')
   b+=v
  v=b.split(b'\x1a')[0].decode().strip()
  if any(x in v.lower() for x in ['error:', 'invalid command','failed to read']):raise RuntimeError(v)
  return v
 def rd(self,a,n=1):return [int(v,0) for v in self.cmd(f'read_memory 0x{a:x} 32 {n}').split()]
 def word(self,a):return self.rd(a)[0]
 def close(self):self.s.close()
def require(ok,message):
 if not ok:raise RuntimeError(message)
def sample(t):
 begin=time.monotonic();raw=b''.join(struct.pack('<I',n) for n in t.rd(S['statistics']['address'],S['statistics']['bytes']//4))
 d={}
 for n,v in S['statistics']['fields'].items():
  data=raw[v['offset']:v['offset']+v['bytes']]
  if n=='snapshots':
   d[n]=[]
   for i in range(4):
    item=data[i*S['snapshot_bytes']:(i+1)*S['snapshot_bytes']]
    obj={k:int.from_bytes(item[x['offset']:x['offset']+x['bytes']],'little') for k,x in S['snapshot_layout'].items()}
    obj.update(slot=int.from_bytes(item[:4],'little'),generation=int.from_bytes(item[4:8],'little'));d[n].append(obj)
  elif v['bytes']==4:d[n]=int.from_bytes(data,'little',signed=n in ['last_lcd_error','ll_last_error'])
  else:d[n]=list(struct.unpack('<'+'I'*(len(data)//4),data))
 d['kernel_ticks']=t.word(S['symbols']['kernel_ticks']);d['tim2_ms']=t.word(S['symbols']['g_tim2_ms_count'])
 d['current_tcb']=t.word(S['symbols']['rtos_current_task']);d['assert_line']=t.word(S['symbols']['g_rtos_assert_line']);faults=t.rd(0xe000ed28,2);d['CFSR'],d['HFSR']=faults
 d['guards']={n:t.word(v['address']) for n,v in S['stacks'].items()}
 d['ll_direct_recoveries']=t.word(S['symbols']['g_ll_recover_count']);d['ll_direct_last_error']=t.word(S['symbols']['g_ll_last_error'])
 dhcsr=t.word(0xe000edf0)
 d['DHCSR']=dhcsr;d['state']='halted' if dhcsr&(1<<17) else 'lockup' if dhcsr&(1<<19) else 'running';d['PC13_ODR']=t.word(0x4001100c)&8192
 d['monotonic']=time.monotonic();d['sample_read_seconds']=d['monotonic']-begin;d['utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
 return d
def safe(d,strict):
 require(d['state']=='running','Unexpected target halt')
 require(d['assert_line']==0 and d['CFSR']==0 and d['HFSR']==0,'Observed assert or fault')
 require(d['display_fault']==0,'LCD terminal initialization fault')
 if strict or d['ready']!=0:require(all(v==0xdeadc0de for v in d['guards'].values()),'Stack guard damaged')
def main():
 label=sys.argv[1];duration=float(sys.argv[2]);strict=len(sys.argv)>3 and sys.argv[3]=='strict'
 t=Tcl();out={'label':label,'hardware':True,'status':'RUNNING','strict_uninterrupted_window':strict,'debug_halts_during_window':False,'samples':[],'context_switch_total':'NOT IMPLEMENTED','hardfault_total_counter':'NOT IMPLEMENTED'}
 try:
  first=sample(t);safe(first,strict);out['samples'].append(first);begin=first['monotonic'];deadline=begin+duration;previous=first;last_print=begin
  with (E/(label+'-samples.jsonl')).open('w') as f:
   f.write(json.dumps(first)+'\n');f.flush()
   while time.monotonic()<deadline:
    time.sleep(min(5,max(0,deadline-time.monotonic())))
    d=sample(t);out['samples'].append(d);f.write(json.dumps(d)+'\n');f.flush();safe(d,strict)
    if strict:
     elapsed=d['monotonic']-first['monotonic'];dk=(d['kernel_ticks']-first['kernel_ticks'])&0xffffffff;dm=(d['tim2_ms']-first['tim2_ms'])&0xffffffff
     require(dk>0 and dm>0,'Timebase not advancing')
     require(d['task_runs'][1]>first['task_runs'][1] and d['task_runs'][3]>first['task_runs'][3],'Controller/Monitor stopped')
     require(d['key_overflows']==0,'Critical queue overflow')
     require(d['transport_errors']==first['transport_errors']==0,'SPI transport errors')
     require(d['ll_direct_recoveries']==first['ll_direct_recoveries']==0,'SPI recovery unexpectedly invoked')
     require(d['initialization_errors']==0,'LCD initialization errors')
     if elapsed>=20:require(95<=dk/elapsed<=105 and 950<=dm/elapsed<=1050,'Timebase rate out of tolerance/reset detected')
     if d['page']==4 and previous['page']==4:require(d['monitor_refreshes']>previous['monitor_refreshes'],'Monitor page stopped refreshing')
    previous=d
    if d['monotonic']-last_print>=55:
     print(json.dumps({'elapsed_s':round(d['monotonic']-begin,1),'page':d['page'],'tick':d['kernel_ticks'],'runs':d['task_runs'],'refresh':d['lcd_refreshes'],'SPI_errors':d['transport_errors'],'recoveries':d['ll_direct_recoveries'],'guards':'PASS'}),flush=True);last_print=d['monotonic']
  last=out['samples'][-1];out['elapsed_s']=last['monotonic']-begin
  out['deltas']={n:(last[n]-first[n])&0xffffffff for n in ['kernel_ticks','tim2_ms','lcd_refreshes','monitor_refreshes','key_isr_events','key_short','key_long','transport_errors','ll_direct_recoveries']}
  out['task_run_deltas']=[(a-b)&0xffffffff for a,b in zip(last['task_runs'],first['task_runs'])]
  out['tick_hz']=out['deltas']['kernel_ticks']/out['elapsed_s'] if out['elapsed_s'] else None
  out['TIM2_hz']=out['deltas']['tim2_ms']/out['elapsed_s'] if out['elapsed_s'] else None
  out['status']='PASS'
  print(label,'PASS',json.dumps({k:out[k] for k in ['elapsed_s','deltas','task_run_deltas','tick_hz','TIM2_hz']}),flush=True)
 except Exception as ex:
  out['status']='FAIL';out['error']=str(ex);out['traceback']=traceback.format_exc();print(out['traceback'],flush=True)
  try:t.cmd('halt');out['halted_on_failure']=True
  except Exception:pass
 finally:
  (E/(label+'-result.json')).write_text(json.dumps(out,indent=2)+'\n');t.close()
 if out['status']!='PASS':raise SystemExit(1)
if __name__=='__main__':main()
