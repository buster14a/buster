#!/usr/bin/env python3
"""Focused hosted Debug work census and exact-output checks. Not timing evidence."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import traceback
import probe_serial as probe

root, out = map(lambda x: Path(x).resolve(), sys.argv[1:3])
out.mkdir(parents=True, exist_ok=True)
(out/'logs').mkdir(exist_ok=True); (out/'binaries').mkdir(exist_ok=True)
env=os.environ.copy(); env.update(CMAKE_BUILD_PARALLEL_LEVEL='1', BUSTER_TEST_JOBS='2')
env.pop('B1125_DUMP_KEYS',None)
commands=[]; comparisons=[]; reference={}
def digest(p):
    with Path(p).open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()
def save():
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (out/'comparisons.json').write_text(json.dumps(comparisons,indent=2)+'\n')
def run(name,args,extra=None,required=True):
    print(name, ' '.join(map(str,args)),flush=True)
    stdout=out/'logs'/(name+'.stdout'); stderr=out/'logs'/(name+'.stderr')
    record={'name':name,'argv':list(map(str,args)),'cwd':str(root),
            'stdout':str(stdout.relative_to(out)),'stderr':str(stderr.relative_to(out))}
    commands.append(record);save()
    with stdout.open('wb') as a,stderr.open('wb') as b:
        p=subprocess.run(record['argv'],cwd=root,env=extra or env,stdout=a,stderr=b)
    record.update(returncode=p.returncode,stdout_sha256=digest(stdout),stderr_sha256=digest(stderr));save()
    if required and p.returncode:
        print(stderr.read_text(errors='replace')[-6000:],flush=True)
        raise RuntimeError('failed '+name)
    return record

def main():
    if env.get('GITHUB_ACTIONS')!='true' or env.get('RUNNER_ENVIRONMENT')!='github-hosted' or env.get('GITHUB_REPOSITORY')!='buster14a/buster':
        raise RuntimeError('authorized GitHub-hosted executor required')
    identity=subprocess.check_output(['git','rev-parse','HEAD','HEAD^{tree}'],cwd=root,text=True).splitlines()
    if identity!=[probe.PIN,probe.TREE]:raise RuntimeError('wrong source')
    (out/'provenance.json').write_text(json.dumps({'source_commit':probe.PIN,'source_tree':probe.TREE,
        'controller_commit':env['GITHUB_SHA'],'run':env['GITHUB_RUN_ID'],'attempt':env['GITHUB_RUN_ATTEMPT'],
        'config':'Debug','timing':'not measured; observation is not acceptance',
        'test_fix':'two serial-only negative prepare tests are computed before worker publication',
        'counter_caveat':'replay exact_after and artifact_complete include two added test-only prepare attempts',
        'held_out':['TLS','inline assembly','long double'],'observation_lanes':1},indent=2)+'\n')
    driver=out/'buster-build'
    run('identity-clang',['clang','--version'])
    run('bootstrap',['clang','-Isrc','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable','build.c','-o',driver])
    run('configure',[driver,'generate','--cc','clang','--ci','--linker','DEFAULT'])
    frozen=out.parent/'frozen-1125-focused'
    run('frozen-source',['git','worktree','add','--detach',frozen,probe.PIN])
    shutil.copytree(root/'build/generated',frozen/'generated')
    inputs=out/'inputs';inputs.mkdir()
    (inputs/'tiny.c').write_text('int main(void){return 0;}\n')
    (inputs/'empty.c').write_text('/* empty translation unit */\n')
    (inputs/'many.c').write_text(''.join('int f%d(int x){return x+%d;}\n'%(i,i) for i in range(2048)))
    cases=[('tiny',['cc','-g','-c',str(inputs/'tiny.c')],True),
           ('empty',['cc','-g','-c',str(inputs/'empty.c')],True),
           ('many',['cc','-g','-c',str(inputs/'many.c')],True),
           ('ordinary',['cc','-g','-c','tests/basic_c_operations.c'],True),
           ('frozen-compiler',['cc','-g','-c','-I'+str(frozen/'src'),'-I'+str(frozen/'generated'),
               '-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0',str(frozen/'src/buster/apps/ide/ide.c')],True),
           ('aarch64-control',['cc','-g','-c','-target','aarch64-unknown-linux',str(inputs/'tiny.c')],True),
           ('preprocess-control',['cc','-E',str(inputs/'tiny.c')],True)]
    held=[]
    for name,path in [('tls','basic_c_thread_local.c'),('asm','basic_c_asm_x87_control_word.c'),('long-double','basic_c_long_double_arithmetic.c')]:
        for allocator in ['none','mir-stack','fast','quality']:
            for ssa in ['-ffrontend-ssa','-fno-frontend-ssa']:
                strict=[] if allocator=='none' else ['-fno-machine-fallback','-fverify-codegen']
                held.append(('held-'+name+'-'+allocator+'-'+ssa[2:],
                    ['cc','-g','-c','-fregister-allocator='+allocator,ssa]+strict+['tests/'+path],False))
    probe.prepare(root)
    for variant in ['record','observed-replay','oracle','candidate']:
        probe.mode(root,variant)
        run(variant+'-build',[driver,'build','--config','Debug','-t','ide','--','-j1'])
        binary=out/'binaries'/variant;shutil.copy2(root/'build/Debug/ide',binary)
        run(variant+'-size',['size','-A',binary])
        for name,args,required in cases+held:
            artifact=out/'same-output.o';artifact.unlink(missing_ok=True)
            child_env=env.copy()
            if variant=='record' and name=='tiny': child_env['B1125_DUMP_KEYS']='1'
            e=run(variant+'-'+name,[binary]+args+['-o',artifact],extra=child_env,required=required)
            e['artifact_sha256']=digest(artifact) if artifact.is_file() else None
            diagnostics='\n'.join(x for x in (out/e['stderr']).read_text(errors='replace').splitlines() if not x.startswith('B1125_'))
            signature=(e['returncode'],e['artifact_sha256'],diagnostics)
            if variant=='record':reference[name]=signature
            else:
                same=reference[name]==signature
                comparisons.append({'variant':variant,'case':name,'identical':same,
                    'positive_compile':e['returncode']==0,'artifact_sha256':e['artifact_sha256']})
                save()
                if not same:raise RuntimeError('differential mismatch '+variant+' '+name)
            if variant=='observed-replay' and name=='tiny':
                text=(out/e['stderr']).read_text()
                if text.count('B1125_TEST_MASK 1023\n')!=1: raise RuntimeError('negative lookup/preparation checks failed')
            save()
        if variant=='record':
            probe.freeze(out/'logs/record-tiny.stderr',root/probe.CG/'research_1125_keys.h')
    headers=out/'experimental-headers';headers.mkdir()
    for file in (root/probe.CG).glob('research_1125_*.h'):shutil.copy2(file,headers/file.name)
    shutil.copy2(root/probe.CG/'research_1125_keys.json',out/'research_1125_keys.json')
    run('candidate-diff',['git','diff','--binary'])
    shutil.copy2(out/'logs/candidate-diff.stdout',out/'candidate.patch')
    run('candidate-diff-check',['git','diff','--check'])
    (out/'complete.json').write_text(json.dumps({'core_complete':True,'comparisons':len(comparisons),
        'full_suite':'separate validation job','performance':'unmeasured'},indent=2)+'\n')
try:
    main()
except BaseException:
    (out/'fatal.txt').write_text(traceback.format_exc());traceback.print_exc();raise
finally:
    save()
    (out/'SHA256SUMS').write_text(''.join(digest(p)+'  '+str(p.relative_to(out))+'\n' for p in sorted(out.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
