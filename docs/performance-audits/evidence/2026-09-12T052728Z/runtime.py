import hashlib,json,struct,subprocess,time,sys
from pathlib import Path
root=Path.cwd();out=root/'build/many-native-arguments';out.mkdir(parents=True,exist_ok=True);compiler=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'build/Debug/ide'
qemu=sys.argv[2] if len(sys.argv)>2 else 'qemu-aarch64'
rows=[]
def run(cmd):
 t=time.monotonic();r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=60)
 row=dict(argv=list(map(str,cmd)),exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr,seconds=time.monotonic()-t)
 if r.returncode:raise RuntimeError(json.dumps(row))
 return row
def text_bytes(p):
 b=p.read_bytes();base=20+struct.unpack_from('<H',b,16)[0]
 for i in range(struct.unpack_from('<H',b,2)[0]):
  n=base+40*i
  if b[n:n+8].rstrip(b'\0')==b'.text':
   size,start=struct.unpack_from('<II',b,n+16);return b[start:start+size]
 raise RuntimeError(str(p))
for arch in ['x86_64','aarch64']:
 for os in ['linux','macos','windows']:
  target=arch+'-'+os;clangtarget={'linux':arch+'-linux-gnu','macos':('arm64' if arch=='aarch64' else arch)+'-apple-macos11','windows':arch+'-pc-windows-msvc'}[os]
  host=out/(target+'-host.o')
  run(['clang','-target',clangtarget,'-O2','-fno-tree-vectorize','-fno-tree-slp-vectorize','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-ffreestanding','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-c','tests/differential/many_native_arguments_host.c','-o',host])
  for mode in ['mir-stack','fast','quality']:
   for ssa in ['-fno-frontend-ssa','-ffrontend-ssa']:
    for mixed in [False,True]:
     name=target+'-'+mode+('-memory' if 'no-' in ssa else '-ssa')+('-mixed' if mixed else '-self');obj=out/(name+'.o');exe=out/name
     row=dict(target=target,mode=mode,ssa=ssa,mixed=mixed)
     try:
      row['compile']=run([compiler,'cc','-target',target,'-fregister-allocator='+mode,ssa,'-fno-machine-fallback','-fverify-codegen','-g0','-c','tests/differential/many_native_arguments.c' if mixed else 'tests/basic_c_many_native_arguments.c','-o',obj])
      assert 'mir=7' in row['compile']['stdout']+row['compile']['stderr']
      if os in ['windows','macos']:
       native=out/(name+'-native')
       row['link']=run([compiler,'cc','-target',target,obj]+([host] if mixed else [])+['-o',native])
       wrapper=Path(__file__).resolve().parent/('pe-code-wrapper.py' if os=='windows' else 'macho-code-wrapper.py')
       row['wrapper']=run(['python3',wrapper,native]+([obj] if os=='windows' else [])+[exe])
       row['linked_image_code_preserved']=True
      else:
       row['link']=run([compiler,'cc','-target',arch+'-linux',obj]+([host] if mixed else [])+['-o',exe])
      row['execute']=run(([qemu] if arch=='aarch64' else [])+[exe]);row['passed']=True
     except Exception as error:row['passed']=False;row['error']=str(error)
     rows.append(row);print(name,'PASS' if row['passed'] else row['error'],flush=True)
     out.joinpath('runtime.json').write_text(json.dumps(rows,indent=2)+'\n')
print(sum(r['passed'] for r in rows),'/',len(rows))
raise SystemExit(any(not r['passed'] for r in rows))
