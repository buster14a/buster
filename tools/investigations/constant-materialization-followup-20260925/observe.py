#!/usr/bin/env python3
"""Hosted-only adversarial follow-up using frozen matched compiler binaries.
No performance claim; source and binaries are inherited, execution host is new.
Usage: observe.py EVIDENCE_ZIP OUTPUT EXPECTED_ZIP_SHA256
"""
from pathlib import Path
import hashlib,json,os,signal,subprocess,sys,zipfile
archive=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def put(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n')
assert sha(archive)==sys.argv[3]
z=zipfile.ZipFile(archive)
manifest={p:h for h,p in (line.split('  ',1) for line in z.read('SHA256SUMS').decode().splitlines())}
for name in ['compilers.json','identity.json','patches/structural.c']+[f'compilers/{v}/ide' for v in ['baseline','local','structural']]:
 data=z.read(name);assert hashlib.sha256(data).hexdigest()==manifest[name]
 dest=out/'inherited'/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
 if dest.name=='ide':dest.chmod(0o755)
ident=json.loads(z.read('compilers.json'))
rows=[]
def run(key,args,cwd=out,timeout=60):
 d=out/'attempts'/key;d.mkdir(parents=True,exist_ok=False)
 row={'key':key,'argv':[str(a) for a in args],'cwd':str(cwd),'exit':None,'timeout':False}
 with (d/'stdout').open('wb') as so,(d/'stderr').open('wb') as se:
  p=subprocess.Popen(row['argv'],cwd=cwd,stdout=so,stderr=se,start_new_session=True)
  try:row['exit']=p.wait(timeout=timeout)
  except subprocess.TimeoutExpired:
   row['timeout']=True;os.killpg(p.pid,signal.SIGKILL);row['exit']=p.wait(timeout=30)
 put(d/'result.json',row);rows.append(row);put(out/'attempts.json',rows);print(json.dumps(row),flush=True)
 return row
cases={
'int_add':'static volatile int x; int main(void){return __builtin_constant_p(x+1);}',
'int_equal':'static volatile int x; int main(void){return __builtin_constant_p(x==0);}',
'int_less':'static volatile int x; int main(void){return __builtin_constant_p(x<1);}',
'int_right':'static volatile int x; int main(void){return __builtin_constant_p(1+x);}',
'int_mul':'static volatile int x; int main(void){return __builtin_constant_p(x*0);}',
'int_shift':'static volatile unsigned x; int main(void){return __builtin_constant_p(x<<1);}',
'i128_add':'static volatile unsigned __int128 x; int main(void){return __builtin_constant_p(x+1);}',
'float_equal':'static volatile double x; int main(void){return __builtin_constant_p(x==0.0);}',
'float_left':'static volatile double x; int main(void){return __builtin_constant_p(x+1.0);}',
'float_right':'static volatile double x; int main(void){return __builtin_constant_p(1.0+x);}',
'wide_float_right':'static volatile long double x; int main(void){return __builtin_constant_p(1.0L+x);}',
'known_arithmetic':'static const int z=0;static int a=z+1;int main(void){return a!=1;}',
'direct_volatile':'static volatile int x;int main(void){return __builtin_constant_p(x);}',
'runtime_arithmetic':'static volatile int x=3;int main(void){return x+1!=4;}',
}
common=['-std=gnu17','-fwrapv','-fno-strict-aliasing','-funsigned-char','-g0']
try:
 put(out/'provenance.json',{'inherited_archive_sha256':sha(archive),'source':json.loads(z.read('identity.json')),'compilers':ident,'fresh_build':False,'performance_evidence':False})
 run('host/uname',['uname','-a']);run('host/lscpu',['lscpu']);run('host/clang',['clang','--version']);run('host/gcc',['gcc','--version'])
 for name,s in cases.items():
  src=out/'sources'/(name+'.c');src.parent.mkdir(parents=True,exist_ok=True);src.write_text(s+'\n')
  for cc in ['clang','gcc']:
   for opt in ['0','2']:
    exe=out/(name+'-'+cc+'-'+opt)
    c=run(f'ref/{cc}/{opt}/{name}/compile',[cc,*common,'-O'+opt,src,'-o',exe])
    if c['exit']==0:run(f'ref/{cc}/{opt}/{name}/execute',[exe],timeout=10)
  for variant in ['baseline','local','structural']:
   compiler=out/'inherited/compilers'/variant/'ide';assert sha(compiler)==ident[variant]['compiler_sha256']
   for front in ['ssa','memory']:
    for mode in ['fast','quality']:
     dest=out/'programs'/variant/front/mode/name;dest.parent.mkdir(parents=True,exist_ok=True)
     key=f'{variant}/{front}/{mode}/{name}'
     c=run(key+'/compile',[compiler,'cc','-target','x86_64-linux',*common,'-O0','-fverify-codegen','-fno-machine-fallback','-fregister-allocator='+mode,'-ffrontend-ssa' if front=='ssa' else '-fno-frontend-ssa',src,'-o',dest])
     if c['exit']==0:run(key+'/execute',[dest],timeout=10)
   if name in ['int_add','float_equal','float_right','float_left','known_arithmetic']:
    bc=out/'ir'/variant/(name+'.bc');bc.parent.mkdir(parents=True,exist_ok=True)
    key=f'ir/{variant}/{name}'
    c=run(key+'/export',[compiler,'cc','-target','x86_64-linux',*common,'-O0','-emit-llvm',src,'-o',bc])
    if c['exit']==0:
     run(key+'/decode',['clang','-S','-emit-llvm','-x','ir',bc,'-o',bc.with_suffix('.ll')])
     c=run(key+'/link',['clang',bc,'-o',bc.with_suffix('.exe')])
     if c['exit']==0:run(key+'/execute',[bc.with_suffix('.exe')],timeout=10)
 text=z.read('patches/structural.c').decode()
 start=text.index('typedef enum CIrConstantTruthKind');end=text.index('BUSTER_C_INTERNAL CIrConstantTruth c_ir_constant_truth_query',start)
 contract=text[start:end]
 for kind,body in [('invalid_use','return !value;'),('explicit_use','return value.kind != C_IR_TRUTH_FALSE;')]:
  src=out/'contract'/(kind+'.c');src.parent.mkdir(parents=True,exist_ok=True)
  src.write_text(contract+'\nint main(void){CIrConstantTruth value={.kind=C_IR_TRUTH_FALSE};'+body+'}\n')
  for cc in ['clang','gcc']:run('contract/'+cc+'/'+kind,[cc,'-std=c11','-Wall','-Wextra','-Werror','-fsyntax-only',src])
 put(out/'completion.json',{'complete':True})
finally:
 with (out/'SHA256SUMS').open('w') as f:
  for p in sorted(out.rglob('*')):
   if p.is_file() and p.name!='SHA256SUMS':f.write(f'{sha(p)}  {p.relative_to(out)}\n')
