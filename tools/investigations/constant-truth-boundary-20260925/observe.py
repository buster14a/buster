#!/usr/bin/env python3
"""Hosted-only failure-retaining observer. No timing or performance inference.
Usage: python3 observe.py CHECKOUT OUTPUT
Build orchestration stays in build.c; production prototypes are C-only.
"""
from __future__ import annotations
import collections
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys

PIN='ade6ac4b6ecb21f30b61b656439bac476c145e2f'
TREE='4c5306221fdb22fccc929b55e333163742de17d0'
CG='src/buster/lib/compiler/frontend/c/c_gen.c'
HERE=Path(__file__).resolve().parent
ROOT=Path(sys.argv[1]).resolve()
OUT=Path(sys.argv[2]).resolve()
OUT.mkdir(parents=True,exist_ok=True)
ENV=dict(os.environ,CMAKE_BUILD_PARALLEL_LEVEL='2',LC_ALL='C',TZ='UTC')
ATTEMPTS=[]
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()
def dump(p,obj):
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(obj,indent=2)+'\n')
def run(key,argv,cwd=ROOT,timeout=180):
    folder=OUT/'attempts'/key
    folder.mkdir(parents=True,exist_ok=False)
    command=[str(a) for a in argv]
    row={'key':key,'argv':command,'cwd':str(cwd),'timeout_seconds':timeout,'exit':None,'timed_out':False}
    with (folder/'stdout').open('wb') as so,(folder/'stderr').open('wb') as se:
        try:
            proc=subprocess.Popen(command,cwd=cwd,env=ENV,stdout=so,stderr=se,start_new_session=True)
            try:row['exit']=proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                row['timed_out']=True
                os.killpg(proc.pid,signal.SIGKILL)
                row['exit']=proc.wait(timeout=30)
        except OSError as exc:row['setup_error']=str(exc)
    dump(folder/'result.json',row)
    ATTEMPTS.append(row)
    dump(OUT/'attempts.json',ATTEMPTS)
    print(json.dumps(row),flush=True)
    return row
def good(row):return row['exit']==0 and not row['timed_out']
def require(key,argv,cwd=ROOT,timeout=180):
    if not good(run(key,argv,cwd,timeout)):raise RuntimeError('Prerequisite failed: '+key)
def git_text(*args):
    return subprocess.check_output(['git',*args],cwd=ROOT,env=ENV,text=True).strip()

# Complete C translation units check results and any effect counts. Holdouts
# deliberately do not pre-claim coverage by the proposed boundary.
CASES={}
def case(name,source,family='control'):CASES[name]=(source+'\n',family)
case('readonly_zero_cond','static const int zero=0; static int x=zero?3:4; int main(void){return x!=4;}','1225')
case('volatile_not','static volatile int x; int main(void){return __builtin_constant_p(!x);}','1225')
case('volatile_double_not','static volatile int x; int main(void){return __builtin_constant_p(!!x);}','1225')
case('volatile_cond','static volatile int x; int main(void){return __builtin_constant_p(x?7:9);}','1225')
case('volatile_and','static volatile int x; int main(void){return __builtin_constant_p(x&&0);}','1224')
case('volatile_or','static volatile int x; int main(void){return __builtin_constant_p(x||1);}','1224')
case('call_and','static volatile int n; static int mark(void){n++;return 0;} int main(void){int r=__builtin_constant_p(mark()&&0);return r || n;}','1224')
case('call_or','static volatile int n; static int mark(void){n++;return 0;} int main(void){int r=__builtin_constant_p(mark()||1);return r || n;}','1224')
case('macro_fallback','#define PICK(e) (__builtin_constant_p(e)?0:((void)(e),1))\nstatic volatile int n; static int mark(void){n++;return 0;} int main(void){int r=PICK(mark()&&0);return r!=1 || n!=1;}','1224')
case('choose_fallback','static volatile int n; static int mark(void){n++;return 0;} int main(void){int r=__builtin_choose_expr(__builtin_constant_p(mark()||1),0,((void)(mark()||1),1));return r!=1 || n!=1;}','1224')
case('array_not','static int a[3]; static int x=!a; int main(void){return x!=0;}','new-counterexample')
case('array_and','static int a[3]; static int x=a&&1; int main(void){return x!=1;}','new-counterexample')
case('array_cond','static int a[3]; static int x=a?3:4; int main(void){return x!=3;}')
case('array_bool','static int a[3]; static _Bool x=(_Bool)a; int main(void){return x!=1;}')
case('readonly_float_cond','static const double zero=-0.0; static int x=zero?3:4; int main(void){return x!=4;}','new-counterexample')
case('readonly_wide_zero','static const long double z=-0.0L; static int x=z?3:4; int main(void){return x!=4;}','new-counterexample')
case('readonly_bool','static const int z=0; static _Bool b=(_Bool)z; int main(void){return b!=0;}')
case('bool_fractions','static _Bool a=0.5,b=-0.5,c=-0.0; int main(void){return a!=1 || b!=1 || c!=0;}')
case('integer_widths','static _Bool a=(unsigned __int128)1<<100; static _Bool b=256; static int x=!((unsigned __int128)1<<100); int main(void){return a!=1 || b!=1 || x!=0;}')
case('wide_float_truth','static int a=!0.25L,b=!-0.0L; static _Bool c=-0.25L; int main(void){return a!=0 || b!=1 || c!=1;}')
case('function_truth','static int f(void){return 1;} static int a=!f,b=f?3:4; int main(void){return a!=0 || b!=3;}')
case('address_and_null','static int g; static _Bool a=&g,b=(void*)0; static int c=!(void*)0; int main(void){return a!=1 || b!=0 || c!=1;}')
case('conditional_common_type','static long long a=1?-1:1U; static double b=1?0.5:1; int main(void){return a!=4294967295LL || b!=0.5;}')
case('nested_common_type','static long long a=(1?(0?-1:1U):-2); int main(void){return a!=1;}')
case('static_asserts','_Static_assert(!0,"zero"); _Static_assert(1?1U:-1,"select"); int main(void){_Static_assert((256?1:0),"local");return 0;}')
case('direct_predicate','static volatile int n; static int mark(void){n++;return 0;} int main(void){int r=__builtin_constant_p(mark());return r || n;}')
case('comma_predicate','static volatile int n; static int mark(void){n++;return 0;} int main(void){int r=__builtin_constant_p((mark(),7));return r || n;}')
case('short_and','static volatile int n; static int mark(void){n++;return 0;} int main(void){int r=__builtin_constant_p(0&&mark());return r!=1 || n;}')
case('short_or','static volatile int n; static int mark(void){n++;return 0;} int main(void){int r=__builtin_constant_p(1||mark());return r!=1 || n;}')
case('runtime_effects','static volatile int n; static int mark(void){n++;return 0;} int main(void){int a=mark()&&0;int b=mark()||1;int c=!mark();return a || b!=1 || c!=1 || n!=3;}')
case('unknown_left_bool_cast','static volatile int x; int main(void){return __builtin_constant_p((_Bool)x);}')
case('unknown_condition_same_arms','static volatile int x; int main(void){return __builtin_constant_p(x?1:1);}')
case('const_null_pointer','static int *const p=0; static int x=p?3:4; static _Bool b=(_Bool)p; int main(void){return x!=4 || b!=0;}','holdout')
case('const_nonnull_pointer','static int a;static int *const p=&a;static int x=p?3:4;int main(void){return x!=3;}','holdout')
case('preparation_intersection','static volatile int n;static int mark(void){n++;return 0;}int main(void){int r=__builtin_constant_p(!(mark()&&0));return r || n;}','separate-181')
NEGATIVE={
 'false_assert':'_Static_assert(0,"intentional false");int main(void){return 0;}\n',
 'record_condition':'struct S{int n;};int main(void){struct S s={0};return s?1:0;}\n',
 'record_not':'struct S{int n;};int main(void){struct S s={0};return !s;}\n',
}
MODES=['none','mir-stack','fast','quality']
FRONTS=['ssa','memory']
OPTS=['0','2']
COMMON=['-std=gnu17','-fwrapv','-fno-strict-aliasing','-funsigned-char','-g0']
NATIVE=[]
REFERENCES=[]
EXPORTS=[]
IDENTITIES={}

def matrix(variant,compiler,work):
    for name,(source,family) in CASES.items():
        src=work/'truth-inputs'/f'{name}.c'
        for mode in MODES:
            for front in FRONTS:
                for opt in OPTS:
                    cell=f'{mode}-{front}-O{opt}'
                    dest=OUT/'programs'/variant/name/cell
                    dest.parent.mkdir(parents=True,exist_ok=True)
                    flags=['-target','x86_64-linux',*COMMON,f'-O{opt}','-fverify-codegen',f'-fregister-allocator={mode}',
                           '-ffrontend-ssa' if front=='ssa' else '-fno-frontend-ssa']
                    if mode!='none':flags.append('-fno-machine-fallback')
                    build=run(f'{variant}/{name}/{cell}/compile',[compiler,'cc',*flags,src,'-o',dest],work,60)
                    execute=run(f'{variant}/{name}/{cell}/execute',[dest],work,15) if good(build) else None
                    NATIVE.append({'variant':variant,'case':name,'family':family,'cell':cell,'compile':build,'execute':execute,
                                   'binary_sha256':digest(dest) if dest.exists() else None})
    dump(OUT/'native.json',NATIVE)
    for name,src in NEGATIVE.items():
        for front in FRONTS:
            row=run(f'{variant}/negative/{name}/{front}',[compiler,'cc',*COMMON,
                '-ffrontend-ssa' if front=='ssa' else '-fno-frontend-ssa',work/'truth-inputs'/f'{name}.c','-c','-o',OUT/'negative.o'],work,60)
            dump(OUT/'negative'/variant/f'{name}-{front}.json',row)

def exports(variant,compiler,work):
    names=['readonly_zero_cond','volatile_not','volatile_and','macro_fallback','array_not',
           'conditional_common_type','bool_fractions','wide_float_truth','runtime_effects','const_null_pointer']
    for name in names:
        for front in FRONTS:
            prefix=OUT/'ir'/variant/name/front
            prefix.mkdir(parents=True,exist_ok=True)
            bc=prefix/'module.bc';ll=prefix/'module.ll';exe=prefix/'program'
            row=run(f'{variant}/ir/{name}/{front}/export',[compiler,'cc','-target','x86_64-linux',*COMMON,'-O0','-emit-llvm',
                '-ffrontend-ssa' if front=='ssa' else '-fno-frontend-ssa',work/'truth-inputs'/f'{name}.c','-o',bc],work,60)
            result={'variant':variant,'case':name,'front':front,'export':row}
            if good(row):
                result['decode']=run(f'{variant}/ir/{name}/{front}/decode',['clang','-S','-emit-llvm','-x','ir',bc,'-o',ll],work,60)
                result['link']=run(f'{variant}/ir/{name}/{front}/link',['clang','-O0',bc,'-o',exe],work,60)
                if good(result['link']):result['execute']=run(f'{variant}/ir/{name}/{front}/execute',[exe],work,15)
                result['bitcode_sha256']=digest(bc)
            EXPORTS.append(result)
    dump(OUT/'exports.json',EXPORTS)

def self_host(variant,compiler,work):
    dest=OUT/'selfhost'/variant
    dest.mkdir(parents=True,exist_ok=True)
    args=['cc','-Isrc','-Ibuild-proof/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0',
          '-g','-v','src/buster/apps/ide/ide.c','-lm']
    first=run(f'{variant}/selfhost/stage1',[compiler,*args,'-o',dest/'stage1'],work,420)
    second=run(f'{variant}/selfhost/stage2',[dest/'stage1',*args,'-o',dest/'stage2'],work,420) if good(first) else None
    result={'stage1':first,'stage2':second,'benchmark_run':False,'full_test_self_host_target':False}
    if second and good(second):
        result.update(stage1_sha256=digest(dest/'stage1'),stage2_sha256=digest(dest/'stage2'))
        result['fixed_point']=result['stage1_sha256']==result['stage2_sha256']
    else:result['fixed_point']=False
    dump(dest/'result.json',result)
    return result['fixed_point']

def main():
    if git_text('rev-parse',PIN+'^{tree}')!=TREE:raise RuntimeError('Source tree pin mismatch')
    work=ROOT.parent/'truth-boundary-work'
    require('worktree',['git','worktree','add','--detach',work,PIN])
    (work/'truth-inputs').mkdir()
    for name,(source,family) in CASES.items():(work/'truth-inputs'/f'{name}.c').write_text(source)
    for name,source in NEGATIVE.items():(work/'truth-inputs'/f'{name}.c').write_text(source)
    shutil.copytree(work/'truth-inputs',OUT/'sources')
    dump(OUT/'cases.json',{n:{'source':s,'family':f} for n,(s,f) in CASES.items()})
    require('patches',[sys.executable,HERE/'make_patches.py',work,OUT/'patches'])
    for tool in ['clang','gcc','cmake','ninja']:run('environment/'+tool,[tool,'--version'])
    run('environment/uname',['uname','-a'])
    dump(OUT/'identity.json',{'production_commit':PIN,'production_tree':TREE,
         'transport_commit':git_text('rev-parse','HEAD'),'transport_tree':git_text('rev-parse','HEAD^{tree}'),
         'runner_os':ENV.get('RUNNER_OS'),'runner_arch':ENV.get('RUNNER_ARCH'),
         'run_id':ENV.get('GITHUB_RUN_ID'),'run_attempt':ENV.get('GITHUB_RUN_ATTEMPT'),
         'build_parallel_level':ENV['CMAKE_BUILD_PARALLEL_LEVEL'],'acceptance_timing':False})
    (work/'.cache').mkdir(exist_ok=True)
    driver=work/'.cache'/'build-proof'
    require('bootstrap',['clang','-Isrc','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable',
         '-fwrapv','-fno-strict-aliasing','-funsigned-char','-g','build.c','-o',driver],work)
    require('generate',[driver,'generate','--ci','--linker','DEFAULT','--build-directory','build-proof','--',
         '-DBUSTER_INCLUDE_TESTS=ON','-DBUSTER_UNITY_BUILD=OFF'],work)
    # Reference observations supplement the contract, not majority voting.
    for cc in ['clang','gcc']:
        for opt,extra in [('O0',[]),('O2',[]),('O1san',['-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
            for name,(source,family) in CASES.items():
                dest=OUT/'references'/cc/opt/name
                dest.parent.mkdir(parents=True,exist_ok=True)
                flags=['-O1' if opt=='O1san' else '-'+opt,*COMMON,*extra]
                compile_row=run(f'reference/{cc}/{opt}/{name}/compile',[cc,*flags,work/'truth-inputs'/f'{name}.c','-o',dest],work,60)
                execute=run(f'reference/{cc}/{opt}/{name}/execute',[dest],work,15) if good(compile_row) else None
                REFERENCES.append({'compiler':cc,'opt':opt,'case':name,'family':family,'compile':compile_row,'execute':execute})
    dump(OUT/'references.json',REFERENCES)
    for variant in ['baseline','local','structural']:
        require(variant+'/restore',['git','restore','--source',PIN,'--staged','--worktree','--',CG],work)
        if variant!='baseline':
            require(variant+'/apply',['git','apply','--check',OUT/'patches'/f'{variant}.patch'],work)
            require(variant+'/apply-write',['git','apply',OUT/'patches'/f'{variant}.patch'],work)
        require(variant+'/stage',['git','add','--',CG],work)
        tree=subprocess.check_output(['git','write-tree'],cwd=work,text=True).strip()
        require(variant+'/build',[driver,'build','--config','Release','--build-directory','build-proof','-t','ide'],work,720)
        binary=OUT/'compilers'/variant/'ide';binary.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(work/'build-proof/Release/ide',binary)
        IDENTITIES[variant]={'tree':tree,'compiler_sha256':digest(binary),'c_gen_sha256':digest(work/CG)}
        dump(OUT/'compilers.json',IDENTITIES)
        fixed=self_host(variant,binary,work)
        if variant=='baseline' and not fixed:raise RuntimeError('Baseline fixed-point prerequisite failed; no candidate applied')
        matrix(variant,binary,work)
        exports(variant,binary,work)
        if variant in ['baseline','structural']:run(variant+'/test_all',[binary,'test','--verbose=1','--ci=1'],work,600)
    # Separate diagnostic builds: no global counter, timer or acceptance.
    for variant in ['baseline','structural']:
        require(variant+'/diagnostic/restore',['git','restore','--source',PIN,'--staged','--worktree','--',CG],work)
        if variant!='baseline':require(variant+'/diagnostic/apply',['git','apply',OUT/'patches'/f'{variant}.patch'],work)
        text=(work/CG).read_text()
        anchor='BUSTER_C_INTERNAL bool c_ir_constant_normalize(CIntegerIrBuilder* builder, CIrConstantValue* value)\n{\n'
        assert text.count(anchor)==1
        text=text.replace(anchor,anchor+'    fprintf(stderr, "TRUTH_NORMALIZE %u\\n", (u32)value->kind);\n')
        if variant=='baseline':anchor='BUSTER_C_INTERNAL bool c_ir_constant_truth(CIntegerIrBuilder* builder, const CIrConstantValue* value_input)\n{\n'
        else:anchor='BUSTER_C_INTERNAL CIrConstantTruth c_ir_constant_truth_query(CIntegerIrBuilder* builder, const CIrConstantValue* value_input)\n{\n'
        assert text.count(anchor)==1
        text=text.replace(anchor,anchor+'    fprintf(stderr, "TRUTH_QUERY %u\\n", (u32)value_input->kind);\n')
        (work/CG).write_text(text)
        diff=subprocess.check_output(['git','diff',PIN,'--',CG],cwd=work)
        (OUT/'patches'/f'{variant}-diagnostic.patch').write_bytes(diff)
        require(variant+'/diagnostic/build',[driver,'build','--config','Release','--build-directory','build-proof','-t','ide'],work,720)
        binary=OUT/'compilers'/f'{variant}-diagnostic'/'ide';binary.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(work/'build-proof/Release/ide',binary)
        counts={}
        for name in ['bool_fractions','integer_widths','wide_float_truth','conditional_common_type','array_bool','readonly_bool']:
            key=variant+'/diagnostic/'+name
            dest=OUT/'diagnostic'/variant/(name+'.o');dest.parent.mkdir(parents=True,exist_ok=True)
            result=run(key,[binary,'cc',*COMMON,'-O0','-c',work/'truth-inputs'/f'{name}.c','-o',dest],work,60)
            lines=(OUT/'attempts'/key/'stderr').read_text(errors='replace').splitlines()
            counts[name]={'attempt':result,'events':dict(collections.Counter(x for x in lines if x.startswith('TRUTH_'))),
                          'object_sha256':digest(dest) if dest.exists() else None}
        dump(OUT/'diagnostic'/variant/'counts.json',{'compiler_sha256':digest(binary),'cases':counts})
    dump(OUT/'completion.json',{'complete':True,'acceptance_timing':False,'benchmark_run':False})
try:main()
except Exception as exc:
    dump(OUT/'failure.json',{'error':repr(exc)})
    raise
finally:
    with (OUT/'SHA256SUMS').open('w') as f:
        for p in sorted(OUT.rglob('*')):
            if p.is_file() and p.name!='SHA256SUMS':f.write(f'{digest(p)}  {p.relative_to(OUT)}\n')
