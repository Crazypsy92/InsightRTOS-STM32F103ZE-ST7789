set pagination off
set confirm off
set remotetimeout 20
set print pretty on
set logging file evidence/stage9_1_h2/gdb_runtime.log
set logging overwrite on
set logging enabled on
file artifacts/stage9_1_h1/insightrtos_display_o2_trace.elf
target extended-remote localhost:3339
python
import gdb,json,struct,time,traceback,re
from pathlib import Path
E=Path('evidence/stage9_1_h2');out={'hardware':True,'checks':{},'contexts':[],'task_entries':[],'pend_sv_switches':[]}
def val(s):return int(gdb.parse_and_eval(s))
def reg(s):return val('$'+('xPSR' if s=='xpsr' else s))&0xffffffff
def sym(s):return val('&'+s)
def mem(a,n):return bytes(gdb.selected_inferior().read_memory(a,n))
def word(a):return int.from_bytes(mem(a,4),'little')
def check(n,ok,d=None):
 out['checks'][n]={'pass':bool(ok),'detail':d};print('HARDWARE_CHECK',n,'PASS' if ok else 'FAIL',d)
 if not ok:raise RuntimeError(n)
def snap(label):
 d={'label':label,'pc':reg('pc'),'lr':reg('lr'),'msp':reg('msp'),'psp':reg('psp'),'control':reg('control'),'xpsr':reg('xpsr'),'primask':reg('primask'),'r4_r11':[reg('r'+str(i)) for i in range(4,12)]};out['contexts'].append(d);return d
def until(expr):
 bp=gdb.Breakpoint(expr,type=gdb.BP_HARDWARE_BREAKPOINT,temporary=True);gdb.execute('continue');check('hit_'+expr,not bp.is_valid(),reg('pc'))
def current():return val('rtos_current_task')
def task(addr):return gdb.Value(addr).cast(gdb.lookup_type('rtos_task_t').pointer()).dereference()
try:
 gdb.execute('maintenance flush register-cache');c=snap('physical_BOARD_RESET_entry')
 check('reset_MSP_and_vector',word(0x08000000)==0x20010000 and word(0x08000004)==(sym('Reset_Handler')|1))
 check('initial_fault_status',word(0xe000ed28)==0 and word(0xe000ed2c)==0)
 gdb.execute('monitor cortex_m vector_catch hard_err bus_err')
 until('main');snap('Reset_SystemInit_to_main')
 until('rtos_start');snap('rtos_start_entry')
 clocks={n:word(a) for n,a in {'SystemCoreClock':sym('SystemCoreClock'),'RCC_CR':0x40021000,'RCC_CFGR':0x40021004,'TIM2_PSC':0x40000028,'TIM2_ARR':0x4000002c,'TIM2_CR1':0x40000000,'TIM2_DIER':0x4000000c}.items()};out['clocks']=clocks;cf=clocks['RCC_CFGR'];cr=clocks['RCC_CR']
 check('72MHz_HSE_PLL_tree',clocks['SystemCoreClock']==72000000 and cr&(1<<17) and cr&(1<<25) and (cf>>2)&3==2 and (cf>>4)&15==0 and (cf>>8)&7==4 and (cf>>11)&7==0 and (cf>>18)&15==7 and cf&(1<<16),clocks)
 check('TIM2_1ms_configuration',clocks['TIM2_PSC']==71 and clocks['TIM2_ARR']==999 and clocks['TIM2_CR1']&1 and clocks['TIM2_DIER']&1)
 until('SVC_Handler');s=snap('SVC_entry');check('SVC_exception',s['xpsr']&0x1ff==11)
 ins=gdb.selected_frame().architecture().disassemble(sym('SVC_Handler'),sym('SVC_Handler')+100)
 ret=next(i['addr'] for i in ins if re.match(r'bx\s+lr',i['asm']))
 until('*'+hex(ret));s=snap('SVC_before_exception_return');check('SVC_EXC_RETURN',s['lr']==0xfffffffd)
 stacks=sym('display_stacks');idle=sym('idle_stack')
 for i,name in enumerate(['key_task','controller_task','lcd_task','monitor_task']):
  until(name);c=snap(name+'_real_thread_entry');t=task(current())
  check(name+'_PSP_priority',c['xpsr']&0x1ff==0 and c['control']&2 and stacks+i*4096<c['psp']<=stacks+(i+1)*4096 and int(t['base_priority'])==[5,4,2,1][i],c)
  out['task_entries'].append({'task':name,'id':int(t['id']),'base_priority':int(t['base_priority']),'tcb':current(),'psp':c['psp']})
 until('idle_entry');c=snap('Idle_real_thread_entry');check('Idle_PSP',c['control']&2 and idle<c['psp']<=idle+512,c)
 priorities={n:mem(a,1)[0] for n,a in {'SVC':0xe000ed1f,'PendSV':0xe000ed22,'SysTick':0xe000ed23,'TIM2':0xe000e41c}.items()};out['priorities']=priorities
 check('interrupt_priorities',priorities=={'SVC':0,'PendSV':240,'SysTick':128,'TIM2':160},priorities)
 check('SysTick100Hz_configuration',word(0xe000e014)==719999 and word(0xe000e010)&7==7)
 for k in range(3):
  until('PendSV_Handler');old=current();before=snap('PendSV_entry_'+str(k))
  check('PendSV_exception_'+str(k),before['xpsr']&0x1ff==14)
  for step in range(3):gdb.execute('stepi')
  saved_sp=reg('r0');saved=list(struct.unpack('<8I',mem(saved_sp,32)))
  check('R4_R11_saved_'+str(k),saved==before['r4_r11'] and saved_sp==before['psp']-32)
  until('*rtos_port_pendsv_unmask');new=current();sp=int(task(new)['sp']);restored=snap('PendSV_restored_'+str(k));frame=list(struct.unpack('<8I',mem(sp,32)))
  check('R4_R11_restored_'+str(k),restored['r4_r11']==frame and restored['psp']==sp+32 and restored['lr']==0xfffffffd)
 out['status']='PASS'
except Exception as ex:
 out['status']='FAIL';out['error']=str(ex);print(traceback.format_exc())
finally:
 (E/'startup-context.json').write_text(json.dumps(out,indent=2)+'\n')
 if out.get('status')!='PASS':raise gdb.GdbError('Hardware startup/context failed')
end
continue
disconnect
quit
