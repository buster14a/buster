#!/usr/bin/env python3
"""Diagnostic changes to exported IR only; not a production repair or acceptance run."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ide=Path(sys.argv[1]).resolve()
base=Path(sys.argv[2]).resolve()
out=base/'followup'
out.mkdir(exist_ok=True)
assert hashlib.sha256(ide.read_bytes()).hexdigest()=='57cc0f0ec6876bf8d591ea5eddb663da6cfbb4dc84da00e47bad686eceb20e80'
commands=[]
checks=[]
def write(name,text):
    p=out/name
    p.write_text(text)
    return p
def run(label,argv,expected=None):
    argv=[str(v) for v in argv]
    try:
        p=subprocess.run(argv,cwd=out,text=True,capture_output=True,timeout=60,check=False)
        r=dict(label=label,argv=argv,cwd=str(out),returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,timeout=False)
    except (subprocess.TimeoutExpired,OSError) as e:
        r=dict(label=label,argv=argv,cwd=str(out),returncode=None,stdout='',stderr=str(e),timeout=isinstance(e,subprocess.TimeoutExpired))
    passed=r['returncode']==0 and (expected is None or r['stdout'].strip()==expected)
    commands.append(r)
    checks.append(dict(label=label,passed=passed,expected_stdout=expected,actual_stdout=r['stdout'],returncode=r['returncode']))
    write('%03d-%s.json'%(len(commands),label),json.dumps(r,indent=2))
    print(label, 'rc=',r['returncode'],r['stdout'][:200],flush=True)
    return passed

def consume(stem,ll,main,extras,expected):
    for opt in ['0','2']:
        obj=out/(stem+'-O'+opt+'.o')
        exe=out/(stem+'-O'+opt)
        if run(stem+'-consume-O'+opt,['clang','-O'+opt,'-c',ll,'-o',obj]):
            if run(stem+'-link-O'+opt,['clang','-no-pie',obj,main,*extras,'-o',exe]):
                run(stem+'-execute-O'+opt,[exe],expected)

# Restore only the missing facts in an exported module, retaining raw originals.
for mode in ['ssa','memory']:
    text=(base/('abi-'+mode+'.ll')).read_text()
    for name,typ,attr in [('read_sc','i8','signext'),('read_ss','i16','signext'),('read_uc','i8','zeroext'),('read_us','i16','zeroext')]:
        pattern=r'(@'+name+r'\()'+typ+r'(?=[ )])'
        text,n=re.subn(pattern,lambda m:m.group(1)+typ+' '+attr,text)
        assert n==2,(name,n)
    text,n=re.subn(r'(call i32 %[A-Za-z0-9_.]+\()i8 ',r'\1i8 signext ',text)
    assert n==1,n
    fixed=write('abi-'+mode+'-attributes-restored.ll',text)
    consume('abi-'+mode+'-restored',fixed,base/'abi-main.o',[base/'abi-callee-clang.o'],'sc=-1 ss=-1 uc=255 us=65535 indirect=-1')

    text=(base/('ctor-llvm-'+mode+'.ll')).read_text()
    assert '@llvm.global_ctors' not in text
    text+='\n@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 101, ptr @initialize, ptr null }]\n'
    fixed=write('ctor-'+mode+'-registration-restored.ll',text)
    consume('ctor-'+mode+'-restored',fixed,base/'sources/ctor_main.c',[],'value=37')

    text=(base/('weak_import-llvm-'+mode+'.ll')).read_text()
    assert text.count('declare i32 @optional()')==1
    fixed=write('weak-import-'+mode+'-binding-restored.ll',text.replace('declare i32 @optional()','declare extern_weak i32 @optional()'))
    consume('weak-import-'+mode+'-restored',fixed,base/'sources/weak_import_main.c',[],'value=0')

    text=(base/('weak-llvm-'+mode+'.ll')).read_text()
    assert '@selected = global ' in text and 'define i32 @choose(' in text
    fixed=write('weak-def-'+mode+'-binding-restored.ll',text.replace('@selected = global ','@selected = weak global ').replace('define i32 @choose(','define weak i32 @choose('))
    consume('weak-def-'+mode+'-restored',fixed,base/'sources/weak_main.c',[base/'sources/strong.c'],'value=58')

    text=(base/('alias-llvm-'+mode+'.ll')).read_text()
    assert text.count('declare i32 @other()')==1
    fixed=write('alias-'+mode+'-identity-restored.ll',text.replace('declare i32 @other()','@other = alias i32 (), ptr @target'))
    consume('alias-'+mode+'-restored',fixed,base/'sources/alias_main.c',[],'value=37')

# A separately compiled observer keeps constructor/destructor effects visible.
library=write('lifecycle.c','''extern void record_exit(int);\nint initialized;\n__attribute__((constructor(101))) static void initialize(void) { initialized=37; }\n__attribute__((destructor(101))) static void finalize(void) { record_exit(43); }\nint probe(void) { return initialized; }\n''')
main=write('lifecycle_main.c','''extern int printf(const char *, ...);\nextern int probe(void);\nvoid record_exit(int x) { printf("destructor=%d\\n",x); }\nint main(void) { printf("constructor=%d\\n",probe()); return 0; }\n''')
flags=['-target','x86_64-linux','-std=gnu17','-g0','-fwrapv','-fno-strict-aliasing','-funsigned-char']
for kind in ['clang','gcc','native','llvm-ssa','llvm-memory']:
    for opt in ['0','2']:
        stem='lifecycle-'+kind+'-O'+opt
        obj=out/(stem+'.o')
        if kind in ['clang','gcc']:
            ok=run(stem+'-compile',[kind,'-std=gnu17','-O'+opt,'-c',library,'-o',obj])
        elif kind=='native':
            ok=run(stem+'-compile',[ide,'cc',*flags,'-fregister-allocator=fast','-c',library,'-o',obj])
        else:
            bc=out/(stem+'.bc')
            flag='-ffrontend-ssa' if kind=='llvm-ssa' else '-fno-frontend-ssa'
            ok=run(stem+'-export',[ide,'cc',*flags,flag,'-emit-llvm',library,'-o',bc])
            if ok:
                run(stem+'-decode',['clang','-S','-emit-llvm',bc,'-o',out/(stem+'.ll')])
                ok=run(stem+'-consume',['clang','-O'+opt,'-c',bc,'-o',obj])
        if ok:
            exe=out/stem
            if run(stem+'-link',['clang','-no-pie',obj,main,'-o',exe]):
                run(stem+'-execute',[exe],'constructor=37\ndestructor=43')

# Correct the initial scout's unsupported -unknown-unknown triple. These belong
# to existing #310/#1196 and #1332, not new claims or a replacement implementation.
for name,expected in [('wasm_pointer',1),('wasm_alignment',0),('wasm_dynamic_alignment',0)]:
    for mode,flag in [('ssa','-ffrontend-ssa'),('memory','-fno-frontend-ssa')]:
        stem=name+'-64-'+mode
        artifact=out/(stem+'.wasm')
        ok=run(stem+'-compile',[ide,'cc','-target','wasm64-unknown-freestanding','-std=gnu17','-g0',flag,base/('sources/'+name+'.c'),'-o',artifact])
        if ok:
            run(stem+'-execute',['node','--experimental-wasm-memory64',base/'wasm-run.js',artifact,str(expected)],'first='+str(expected)+' second='+str(expected))

write('commands.json',json.dumps(commands,indent=2))
write('checks.json',json.dumps(checks,indent=2))
failed=[c for c in checks if not c['passed']]
write('summary.json',json.dumps(dict(checks=len(checks),failed_checks=len(failed),failures=failed,note='Restoration changes only diagnostic .ll copies, not Buster. Native lifecycle source repeats are not optimization-matrix evidence. Wasm follow-ups belong to existing work.'),indent=2))
write('summary.md','# Follow-up controls\n\n'+str(len(checks))+' checks; '+str(len(failed))+' failed checks. Not a distinct-bug count.\n\n'+'\n'.join(c['label']+': '+('PASS' if c['passed'] else 'FAIL') for c in checks)+'\n')
manifest=[]
for p in sorted(out.rglob('*')):
    if p.is_file() and p.name!='SHA256SUMS':
        manifest.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(out).as_posix())
write('SHA256SUMS','\n'.join(manifest)+'\n')
raise SystemExit(1 if failed else 0)
