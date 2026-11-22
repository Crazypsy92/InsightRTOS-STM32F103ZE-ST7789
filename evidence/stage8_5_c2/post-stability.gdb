set pagination off
set confirm off
set remotetimeout 20
set logging file evidence/stage8_5_c2/post-stability-gdb.log
set logging overwrite on
set logging enabled on
file artifacts/stage8_5_c1/o2/insightrtos_zet6_c1_o2.elf
target extended-remote localhost:3339
python
import gdb,json,struct,time,traceback
from pathlib import Path
E=Path('evidence/stage8_5_c2');S=json.loads((E/'runtime-schema.json').read_text());out={'hardware':True,'scope':'halt after completed continuous30min window','stacks':{},'tasks':[]}
def val(x):return int(gdb.parse_and_eval(x))
def mem(a,n):return bytes(gdb.selected_inferior().read_memory(a,n))
def word(a):return int.from_bytes(mem(a,4),'little')
try:
 gdb.execute('maintenance flush register-cache')
 out['registers']={n:val('$'+n)&0xffffffff for n in ['pc','lr','psp','msp','control','xPSR','primask']}
 for name,v in S['stacks'].items():
  blob=mem(v['address'],v['bytes']);(E/(name+'-stack.bin')).write_bytes(blob);words=list(struct.unpack('<'+'I'*(len(blob)//4),blob));first=1
  while first<len(words) and words[first]==0xa5a5a5a5:first+=1
  out['stacks'][name]={'base':v['address'],'bytes':len(blob),'guard':words[0],'guard_ok':words[0]==0xdeadc0de,'observed_used_from_fill_bytes':len(blob)-first*4,'untouched_pattern_bytes_excluding_guard':max(0,first*4-4)}
  if words[0]!=0xdeadc0de:raise RuntimeError(name+' guard damaged')
 for i in range(4):
  ref=gdb.parse_and_eval('g_display_task_refs['+str(i)+']');slot=int(ref['slot']);t=gdb.parse_and_eval('task_pool['+str(slot)+']')
  out['tasks'].append({k:int(t[k]) for k in ['id','sp','stack_base','stack_words','state','base_priority','effective_priority']})
 out['MSP_bounds_ok']=0x2000f000<=out['registers']['msp']<=0x20010000
 if not out['MSP_bounds_ok']:raise RuntimeError('MSP outside audited reserve')
 out['MSP_high_watermark']='NOT MEASURED: MSP reserve was not prefilled by frozen firmware'
 out['faults']={'CFSR':word(0xe000ed28),'HFSR':word(0xe000ed2c),'assert_line':word(S['symbols']['g_rtos_assert_line'])}
 if any(out['faults'].values()):raise RuntimeError('fault or assert recorded')
 out['status']='PASS'
except Exception as ex:
 out['status']='FAIL';out['error']=str(ex);print(traceback.format_exc())
finally:
 (E/'post-stability.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
 if out.get('status')!='PASS':raise gdb.GdbError('Post-stability check failed; preserve halted target')
end
disconnect
quit
