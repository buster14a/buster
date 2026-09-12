from pathlib import Path
import subprocess,os,signal,time,json,hashlib,statistics
root=Path.cwd(); out=root.parent/'typeof-paired';out.mkdir(exist_ok=True)
compilers={'baseline':root.parent/'baseline-ide','patch':root/'build/Release/ide'}
jobs=[(shape,depth,root.parent/'typeof-inputs'/f'{shape}-{depth}.c') for shape in ['comma','parentheses','prefix','mixed','postfix'] for depth in [64,256,1024,4096]]
jobs.append(('comma',16384,root.parent/'typeof-inputs/comma-16384.c'))
tiny=out/'tiny.c';tiny.write_text('int object;\n')
jobs.extend([('tiny',0,tiny),('operations',0,root/'tests/basic_c_operations.c')])
records=[]
def expired(signum,frame): raise TimeoutError()
signal.signal(signal.SIGALRM,expired)
def measure(variant,shape,depth,source,pair,warmup=False):
 args=[str(compilers[variant]),'cc','-g0','-std=gnu23','-fsyntax-only',str(source)]
 log=out/f'{shape}-{depth}-{variant}-{pair}-{int(warmup)}.log'
 with log.open('wb') as stream:
  signal.setitimer(signal.ITIMER_REAL,20)
  start=time.perf_counter_ns();p=subprocess.Popen(args,stdout=stream,stderr=subprocess.STDOUT)
  timeout=False
  try: _,status,usage=os.wait4(p.pid,0)
  except TimeoutError:
   timeout=True;p.kill();_,status,usage=os.wait4(p.pid,0)
  finally: signal.setitimer(signal.ITIMER_REAL,0)
  elapsed=time.perf_counter_ns()-start;p.returncode=os.waitstatus_to_exitcode(status)
 row={'variant':variant,'shape':shape,'depth':depth,'pair':pair,'warmup':warmup,'argv':args,'wall_ns':elapsed,'cpu_seconds':usage.ru_utime+usage.ru_stime,'max_rss_kib':usage.ru_maxrss,'returncode':p.returncode,'timeout':timeout,'log':log.name,'output_sha256':hashlib.sha256(log.read_bytes()).hexdigest()}
 records.append(row);(out/'samples.json').write_text(json.dumps(records,indent=2))
 if p.returncode!=0 or timeout: raise RuntimeError(row)
for shape,depth,source in jobs:
 for variant in compilers:measure(variant,shape,depth,source,-1,True)
 for pair in range(8):
  for variant in (['baseline','patch'] if pair%2==0 else ['patch','baseline']):measure(variant,shape,depth,source,pair)
 med={variant:statistics.median(r['wall_ns']/1e6 for r in records if r['variant']==variant and r['shape']==shape and r['depth']==depth and not r['warmup']) for variant in compilers}
 print(shape,depth,med,flush=True)
for shape in ['parentheses','prefix','mixed','postfix']:
 for pair in range(8):measure('patch',shape,16384,root.parent/'typeof-inputs'/f'{shape}-16384.c',pair)
all_inputs={(r['shape'],r['depth'],r['argv'][-1]) for r in records}
provenance={'base_commit':'719ba64e3022a0ae1db078ea97cb951a70ac02a0','patch_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'patch_diff_sha256':hashlib.sha256(subprocess.check_output(['git','diff'])).hexdigest(),'binaries':{name:{'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for name,path in compilers.items()},'inputs':[{'shape':shape,'depth':depth,'path':str(path),'bytes':Path(path).stat().st_size,'sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest()} for shape,depth,path in sorted(all_inputs)],'compiler':subprocess.check_output(['clang','--version'],text=True),'uname':os.uname()._asdict() if hasattr(os.uname(),'_asdict') else list(os.uname()),'cpuinfo':next(line.strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name'))}
(out/'provenance.json').write_text(json.dumps(provenance,indent=2))
