#!/usr/bin/env python3
"""Fresh same-root matched diagnostic builds, never acceptance timing.
Usage: diagnose.py CHECKOUT PARENT_ZIP OUTPUT
"""
from pathlib import Path
import collections,hashlib,json,os,signal,subprocess,sys,zipfile
root=Path(sys.argv[1]).resolve();z=zipfile.ZipFile(sys.argv[2]);out=Path(sys.argv[3]).resolve();out.mkdir(parents=True,exist_ok=True)
pin='ade6ac4b6ecb21f30b61b656439bac476c145e2f';tree='4c5306221fdb22fccc929b55e333163742de17d0';cg='src/buster/lib/compiler/frontend/c/c_gen.c'
work=root.parent/'truth-diagnostic-work';env=dict(os.environ,CMAKE_BUILD_PARALLEL_LEVEL='2',LC_ALL='C',TZ='UTC');rows=[]
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def put(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n')
def run(key,args,cwd=work,timeout=180):
 d=out/'attempts'/key;d.mkdir(parents=True,exist_ok=False)
 row={'key':key,'argv':[str(a) for a in args],'cwd':str(cwd),'exit':None,'timed_out':False}
 with (d/'stdout').open('wb') as so,(d/'stderr').open('wb') as se:
  p=subprocess.Popen(row['argv'],cwd=cwd,env=env,stdout=so,stderr=se,start_new_session=True)
  try:row['exit']=p.wait(timeout=timeout)
  except subprocess.TimeoutExpired:
   row['timed_out']=True;os.killpg(p.pid,signal.SIGKILL);row['exit']=p.wait(timeout=30)
 put(d/'result.json',row);rows.append(row);put(out/'attempts.json',rows);print(json.dumps(row),flush=True);return row
def require(key,args,cwd=work,timeout=180):
 if run(key,args,cwd,timeout)['exit']!=0:raise RuntimeError(key)
manifest={p:h for h,p in (line.split('  ',1) for line in z.read('SHA256SUMS').decode().splitlines())}
def inherited(name):
 data=z.read(name);assert hashlib.sha256(data).hexdigest()==manifest[name];return data
names=['bool_fractions','integer_widths','wide_float_truth','conditional_common_type','array_bool','readonly_bool','short_and','short_or']
try:
 require('worktree',['git','worktree','add','--detach',work,pin],root)
 actual=subprocess.check_output(['git','rev-parse','HEAD^{tree}'],cwd=work,text=True).strip();assert actual==tree
 base=(work/cg).read_text();structural=inherited('patches/structural.c').decode()
 (work/'diagnostic-inputs').mkdir()
 for name in names:
  data=inherited('sources/'+name+'.c');(work/'diagnostic-inputs'/f'{name}.c').write_bytes(data)
  dest=out/'sources'/f'{name}.c';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
 driver=work/'.cache/diagnostic-build';driver.parent.mkdir(exist_ok=True)
 require('bootstrap',['clang','-Isrc','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable','-fwrapv','-fno-strict-aliasing','-funsigned-char','-g','build.c','-o',driver])
 require('generate',[driver,'generate','--ci','--linker','DEFAULT','--build-directory','build-diagnostic','--','-DBUSTER_INCLUDE_TESTS=OFF','-DBUSTER_UNITY_BUILD=OFF'])
 summary={}
 for variant in ['baseline-control','baseline-diagnostic','structural-diagnostic']:
  text=structural if variant.startswith('structural') else base
  if variant!='baseline-control':
   include='#include "c_internal.h"\n';assert text.count(include)==1
   text=text.replace(include,include+'#include <stdio.h> // Disposable observer only.\n')
   anchor='BUSTER_C_INTERNAL bool c_ir_constant_normalize(CIntegerIrBuilder* builder, CIrConstantValue* value)\n{\n';assert text.count(anchor)==1
   text=text.replace(anchor,anchor+'    fprintf(stderr, "TRUTH_NORMALIZE %u\\n", (u32)value->kind);\n')
   if variant.startswith('structural'):anchor='BUSTER_C_INTERNAL CIrConstantTruth c_ir_constant_truth_query(CIntegerIrBuilder* builder, const CIrConstantValue* value_input)\n{\n'
   else:anchor='BUSTER_C_INTERNAL bool c_ir_constant_truth(CIntegerIrBuilder* builder, const CIrConstantValue* value_input)\n{\n'
   assert text.count(anchor)==1
   text=text.replace(anchor,anchor+'    fprintf(stderr, "TRUTH_QUERY %u\\n", (u32)value_input->kind);\n')
  (work/cg).write_text(text)
  dest=out/'source'/f'{variant}.c';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(text)
  require(variant+'/build',[driver,'build','--config','Release','--build-directory','build-diagnostic','-t','ide'],timeout=720)
  compiler=work/'build-diagnostic/Release/ide';summary[variant]={'compiler_sha256':sha(compiler),'source_sha256':sha(work/cg),'cases':{}}
  for name in names:
   obj=out/'objects'/variant/f'{name}.o';obj.parent.mkdir(parents=True,exist_ok=True)
   key=variant+'/'+name
   r=run(key,[compiler,'cc','-std=gnu17','-fwrapv','-fno-strict-aliasing','-funsigned-char','-g0','-O0','-c',work/'diagnostic-inputs'/f'{name}.c','-o',obj],timeout=60)
   lines=(out/'attempts'/key/'stderr').read_text(errors='replace').splitlines()
   summary[variant]['cases'][name]={'exit':r['exit'],'object_sha256':sha(obj) if obj.exists() else None,'events':dict(collections.Counter(s for s in lines if s.startswith('TRUTH_')))}
  put(out/'summary.json',summary)
 put(out/'completion.json',{'complete':True,'instrumentation_not_acceptance':True,'production_pin':pin,'production_tree':tree})
finally:
 with (out/'SHA256SUMS').open('w') as f:
  for p in sorted(out.rglob('*')):
   if p.is_file() and p.name!='SHA256SUMS':f.write(f'{sha(p)}  {p.relative_to(out)}\n')
