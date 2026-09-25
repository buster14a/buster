#!/usr/bin/env python3
"""Hosted correctness/work-count experiment, deliberately not a timing harness."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import traceback
import probe

ROOT = Path(sys.argv[1]).resolve()
OUT = Path(sys.argv[2]).resolve()
OUT.mkdir(parents=True, exist_ok=True)
LOGS = OUT/'logs'; LOGS.mkdir(exist_ok=True)
BIN = OUT/'binaries'; BIN.mkdir(exist_ok=True)
REFERENCES = OUT/'reference-artifacts'; REFERENCES.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(CMAKE_BUILD_PARALLEL_LEVEL='1', BUSTER_TEST_JOBS='2', BUSTER_MATRIX_THREADS='2')
ENV.pop('B1125_DUMP_KEYS', None)
records = []
comparisons = []

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''): h.update(block)
    return h.hexdigest()

def save():
    (OUT/'commands.json').write_text(json.dumps(records, indent=2)+'\n')
    (OUT/'comparisons.json').write_text(json.dumps(comparisons, indent=2)+'\n')

def run(name, argv, *, required=True, env=None, cwd=ROOT, timeout=1200):
    args = list(map(str, argv))
    print(name, ' '.join(args), flush=True)
    stdout, stderr = LOGS/(name+'.stdout'), LOGS/(name+'.stderr')
    entry = dict(name=name, argv=args, cwd=str(cwd), timeout_seconds=timeout,
                 stdout=str(stdout.relative_to(OUT)), stderr=str(stderr.relative_to(OUT)))
    records.append(entry); save()
    with stdout.open('wb') as out, stderr.open('wb') as err:
        try:
            p = subprocess.run(args, cwd=cwd, env=env or ENV, stdout=out, stderr=err, timeout=timeout)
            entry['returncode'] = p.returncode
        except subprocess.TimeoutExpired:
            entry.update(returncode=None, timed_out=True)
        except Exception as error:
            entry.update(returncode=None, launch_error=str(error))
    entry.update(stdout_sha256=sha(stdout), stderr_sha256=sha(stderr)); save()
    if entry['returncode'] != 0:
        print(stderr.read_text(errors='replace')[-7000:], flush=True)
        if required: raise RuntimeError('failed required step '+name)
    return entry

DRIVER = OUT/'buster-build'
def build(name):
    run(name+'-build', [DRIVER,'build','--config','Release','-t','ide','--','-j1'])
    dst = BIN/name
    shutil.copy2(ROOT/'build/Release/ide', dst)
    run(name+'-size', ['size','-A',dst])
    return dst

cases = []
case_results = {}
def population():
    inputs = OUT/'inputs'; inputs.mkdir(exist_ok=True)
    (inputs/'tiny.c').write_text('int main(void) { return 0; }\n')
    (inputs/'empty.c').write_text('/* empty translation unit */\n')
    (inputs/'many.c').write_text(''.join('int f%d(int x) { return x + %d; }\n' % (i,i) for i in range(2048)))
    (inputs/'bad.c').write_text('int main(void) { return missing_name; }\n')
    common = ['cc','-g','-c']
    for name, file in [('tiny',inputs/'tiny.c'),('empty',inputs/'empty.c'),('many',inputs/'many.c'),
                       ('ordinary',ROOT/'tests/basic_c_operations.c')]:
        cases.append((name, common+[str(file)], True))
    frozen = OUT.parent/'frozen-1125'
    run('frozen-worktree', ['git','worktree','add','--detach',frozen,probe.PIN])
    shutil.copytree(ROOT/'build/generated', frozen/'generated')
    cases.append(('frozen-compiler',common+['-I'+str(frozen/'src'),'-I'+str(frozen/'generated'),
        '-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0',str(frozen/'src/buster/apps/ide/ide.c')],True))
    cases.append(('aarch64-control', common+['-target','aarch64-unknown-linux',str(inputs/'tiny.c')],True))
    cases.append(('preprocess-control', ['cc','-E',str(inputs/'tiny.c')],True))
    return inputs

def compile_cases(label, binary, selected=None, *, record=False):
    for name, args, required in selected or cases:
        output = OUT/'same-output.o'
        output.unlink(missing_ok=True)
        env = ENV.copy()
        if record and name == 'tiny': env['B1125_DUMP_KEYS']='1'
        entry = run(label+'-'+name, [binary]+args+['-o',str(output)], env=env, required=required)
        artifact = sha(output) if output.is_file() else None
        entry['artifact_sha256']=artifact
        entry['artifact_bytes']=output.stat().st_size if output.is_file() else None
        case_results[label,name]=entry
        if label=='base':
            if output.exists(): shutil.copy2(output, REFERENCES/(name+'.out'))
        else:
            original=case_results['base',name]
            same = original['returncode']==entry['returncode'] and original['artifact_sha256']==artifact
            # Observation lines are never compared as compiler diagnostics.
            def diagnostics(e):
                return '\n'.join(x for x in (OUT/e['stderr']).read_text(errors='replace').splitlines() if not x.startswith('B1125_'))
            same_diag = diagnostics(original)==diagnostics(entry)
            comparisons.append(dict(variant=label,case=name,artifact_equal=same,diagnostics_equal=same_diag,
                baseline_sha256=original['artifact_sha256'],candidate_sha256=artifact))
            if not same or not same_diag:
                if output.exists(): shutil.copy2(output, OUT/(label+'-'+name+'-different.out'))
                save(); raise RuntimeError('differential mismatch: '+label+' '+name)
        save()

def main():
    if os.environ.get('GITHUB_ACTIONS')!='true' or os.environ.get('RUNNER_ENVIRONMENT')!='github-hosted':
        raise RuntimeError('This experiment may execute compiler workloads only on GitHub-hosted executors')
    if os.environ.get('GITHUB_REPOSITORY') != 'buster14a/buster':
        raise RuntimeError('wrong repository')
    actual = subprocess.check_output(['git','rev-parse','HEAD','HEAD^{tree}'],cwd=ROOT,text=True).splitlines()
    if actual != [probe.PIN,probe.TREE]: raise RuntimeError('source identity mismatch')
    (OUT/'provenance.json').write_text(json.dumps(dict(source_commit=probe.PIN,source_tree=probe.TREE,
        workflow_sha=os.environ.get('GITHUB_SHA'),run_id=os.environ.get('GITHUB_RUN_ID'),
        run_attempt=os.environ.get('GITHUB_RUN_ATTEMPT'),runner=os.environ.get('RUNNER_NAME'),
        evidence_class='hosted correctness and deterministic work counts; no acceptance timing',
        held_out_families=['TLS','inline assembly','long double'],
        observation_policy='one compile lane only; no instrumented suite or parallel cohort',
        baseline_fixed_point_run=36182710724),indent=2)+'\n')
    for tool in ['clang','gcc','cmake','ninja']:
        run('identity-'+tool,[tool,'--version'])
    run('bootstrap',['clang','-Isrc','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable','-g','build.c','-o',DRIVER])
    run('configure',[DRIVER,'generate','--cc','clang','--ci','--linker','DEFAULT'])
    base=build('base')
    run('base-self-host',[DRIVER,'test_self_host','--config','Release'])
    run('base-tests',[base,'test','--verbose=0','--ci=1'],required=False)
    inputs=population()
    compile_cases('base',base)
    probe.prepare(ROOT)
    record=build('record')
    compile_cases('record',record,record=True)
    probe.freeze(LOGS/'record-tiny.stderr',ROOT/probe.CG/'research_1125_keys.h')
    shutil.copy2(ROOT/probe.CG/'research_1125_keys.h',OUT/'research_1125_keys.h')
    shutil.copy2(ROOT/probe.CG/'research_1125_keys.json',OUT/'research_1125_keys.json')
    probe.mode(ROOT,'observed-replay')
    observed=build('observed-replay')
    compile_cases('observed-replay',observed)
    probe.mode(ROOT,'oracle')
    oracle=build('oracle')
    compile_cases('oracle',oracle)
    run('oracle-tests',[oracle,'test','--verbose=0','--ci=1'],required=False)
    probe.mode(ROOT,'candidate')
    candidate=build('candidate')
    compile_cases('candidate',candidate)
    run('candidate-tests',[candidate,'test','--verbose=0','--ci=1'],required=False)
    run('candidate-mode-matrix',[DRIVER,'test_mode_matrix','--config','Release'],required=False)
    run('candidate-self-host',[DRIVER,'test_self_host','--config','Release'],required=False)
    held=[]
    for name, path in [('held-tls','basic_c_thread_local.c'),('held-asm','basic_c_asm_x87_control_word.c'),
                       ('held-long-double','basic_c_long_double_arithmetic.c')]:
        for mode in ['none','mir-stack','fast','quality']:
            for ssa in ['-ffrontend-ssa','-fno-frontend-ssa']:
                strict=[] if mode=='none' else ['-fno-machine-fallback','-fverify-codegen']
                held.append((name+'-'+mode+'-'+ssa[2:],['cc','-g','-c','-fregister-allocator='+mode,ssa]+strict+[str(ROOT/'tests'/path)],True))
    for jobs in [1,2,4]:
        held.append(('multi-link-'+str(jobs), ['cc','-g','-fcompile-jobs='+str(jobs),
            'tests/basic_c_constructor_order.c','tests/basic_c_constructor_order_second.c'],True))
    compile_cases('base',base,held)
    compile_cases('candidate',candidate,held)
    for label,binary in [('base',base),('candidate',candidate)]:
        run(label+'-bad-diagnostic',[binary,'cc','-c',inputs/'bad.c','-o',OUT/'bad.o'],required=False)
    # Retain exact transformed source and both observation and plain-C variants.
    (OUT/'prototype-sources').mkdir(exist_ok=True)
    for path in probe.PINS:
        dst=OUT/'prototype-sources'/path; dst.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(ROOT/path,dst)
    for path in (ROOT/probe.CG).glob('research_1125_*.h'):
        shutil.copy2(path,OUT/'prototype-sources'/path.name)
    run('candidate-diff',['git','diff','--no-ext-diff'])
    run('candidate-diff-check',['git','diff','--check'])
    run('candidate-source-status',['git','status','--short'])
    # Debug + sanitizers have a separate generated tree, never delete Release.
    sanitize=OUT.parent/'sanitize-1125'
    configured=run('sanitizer-configure',[DRIVER,'generate','--cc','clang','--ci','--linker','DEFAULT','--sanitize','--build-dir',sanitize],required=False)
    if configured['returncode']==0:
        built=run('sanitizer-build',[DRIVER,'build','--config','Debug','--build-dir',sanitize,'-t','ide','--','-j1'],required=False)
        if built['returncode']==0:
            clean=ENV.copy()
            clean.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            run('sanitizer-tests',[sanitize/'Debug/ide','test','--verbose=0','--ci=1'],env=clean,required=False)
    bad_a=next(x for x in records if x['name']=='base-bad-diagnostic')
    bad_b=next(x for x in records if x['name']=='candidate-bad-diagnostic')
    comparisons.append(dict(case='bad-diagnostic',variant='candidate',
        identical_nonzero_exit=bad_a['returncode']==bad_b['returncode'] and bool(bad_a['returncode']),
        diagnostics_equal=bad_a['stderr_sha256']==bad_b['stderr_sha256']))
    save()

try:
    main()
except Exception:
    (OUT/'fatal.txt').write_text(traceback.format_exc())
    traceback.print_exc()
    sys.exit(1)
finally:
    save()
    hashes=[]
    for path in sorted(OUT.rglob('*')):
        if path.is_file() and path.name!='SHA256SUMS': hashes.append(sha(path)+'  '+str(path.relative_to(OUT)))
    (OUT/'SHA256SUMS').write_text('\n'.join(hashes)+'\n')
