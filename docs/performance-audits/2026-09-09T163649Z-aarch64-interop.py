from pathlib import Path
import argparse,json,subprocess
parser=argparse.ArgumentParser(); parser.add_argument('--ide',type=Path,required=True); parser.add_argument('--out',type=Path,required=True); opt=parser.parse_args()
repo=Path.cwd(); out=opt.out.resolve(); out.mkdir(exist_ok=False); ide=opt.ide.resolve(); records=[]
def run(command):
 command=[str(x) for x in command]; r=subprocess.run(command,capture_output=True,text=True,timeout=30)
 records.append({'command':command,'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr}); (out/'results.json').write_text(json.dumps(records,indent=2)); assert r.returncode==0,records[-1]
host=(repo/'tests/differential/native_variadic_host.c').read_text().replace('#include <stdarg.h>', 'typedef __builtin_va_list va_list;\n#define va_start __builtin_va_start\n#define va_end __builtin_va_end\n#define va_copy __builtin_va_copy\n#define va_arg __builtin_va_arg').replace('#include <stdio.h>','').replace('    printf("native-variadic=%d\\n", bad);', '')
(out/'host.c').write_text(host); (out/'start.s').write_text('.text\n.global _start\n_start:\nbl main\nmov x8, #93\nsvc #0\n')
run(['clang','-target','aarch64-linux-gnu','-O2','-ffreestanding','-fno-stack-protector','-c',out/'host.c','-o',out/'host.o'])
run(['clang','-target','aarch64-linux-gnu','-c',out/'start.s','-o',out/'start.o'])
subjects=[('clang-O0',['clang','-target','aarch64-linux-gnu','-O0','-ffreestanding','-fno-stack-protector']),('clang-O2',['clang','-target','aarch64-linux-gnu','-O2','-ffreestanding','-fno-stack-protector'])]
for mode in ['none','mir-stack','fast','quality']:
 for ssa in ['-ffrontend-ssa','-fno-frontend-ssa']:
  flags=[ide,'cc','-target','aarch64-unknown-linux','-g0','-fregister-allocator='+mode,ssa,'-fverify-codegen']
  if mode!='none': flags+=['-fno-machine-fallback']
  subjects.append((mode+ssa,flags))
for name,flags in subjects:
 run([*flags,'-c','tests/differential/native_variadic.c','-o',out/(name+'.o')])
 run(['ld.lld','-e','_start',out/(name+'.o'),out/'host.o',out/'start.o','-o',out/name])
 run(['qemu-aarch64',out/name]); print(name+' passed',flush=True)
