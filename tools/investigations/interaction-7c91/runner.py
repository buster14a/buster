"""Bounded hosted-only research runner. No clocks or performance conclusions.
The native repository driver remains the build-policy authority. This script
only arranges disposable overlays, frozen inputs, oracle checks and evidence.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import urllib.request

BASE = 'ade6ac4b6ecb21f30b61b656439bac476c145e2f'
TREE = '4c5306221fdb22fccc929b55e333163742de17d0'
if os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('RUNNER_ENVIRONMENT') != 'github-hosted':
    raise SystemExit('This packet requires a GitHub-hosted executor; no local execution.')
repo = Path.cwd()
research = repo / 'tools/investigations/interaction-7c91'
evidence = Path(os.environ['RUNNER_TEMP']) / 'interaction-7c91-result'
work = Path(os.environ['RUNNER_TEMP']) / 'interaction-7c91-work'
evidence.mkdir(exist_ok=True)
for name in ['logs', 'bin', 'inputs', 'outputs', 'cjson']:
    (evidence / name).mkdir(exist_ok=True)
commands = []
rows = []
failures = []
env = dict(os.environ, LC_ALL='C', BUSTER_TEST_JOBS='2')

def run(tag, argv, cwd=work, extra=None, timeout=180, required=True):
    argv = list(map(str, argv))
    log = evidence / 'logs' / (tag + '.log')
    with log.open('wb') as output:
        child = subprocess.Popen(argv, cwd=cwd, env=dict(env, **(extra or {})),
                                 stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            rc = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()
            rc = 124
    commands.append({'tag': tag, 'argv': argv, 'cwd': str(cwd), 'rc': rc,
                     'log': str(log.relative_to(evidence)), 'extra_env': extra or {}})
    (evidence / 'commands.json').write_text(json.dumps(commands, indent=2))
    print(f'R7_COMMAND {tag} rc={rc}', flush=True)
    if required and rc:
        print(log.read_text(errors='replace')[-12000:], flush=True)
        raise RuntimeError(f'{tag} failed: {rc}')
    return rc, log.read_text(errors='replace')

def save_rows():
    (evidence / 'rows.json').write_text(json.dumps(rows, indent=2))
    (evidence / 'failures.json').write_text(json.dumps(failures, indent=2))

try:
    run('source-tree', ['git', 'rev-parse', BASE + '^{tree}'], cwd=repo)
    if (evidence/'logs/source-tree.log').read_text().strip() != TREE:
        raise RuntimeError('base tree mismatch')
    run('worktree', ['git', 'worktree', 'add', '--detach', work, BASE], cwd=repo)
    run('source-clean', ['git', 'diff', '--exit-code'])
    run('compiler-version', ['clang', '--version'])
    run('gcc-version', ['gcc', '--version'])
    (evidence / 'identity.json').write_text(json.dumps({
        'base': BASE, 'tree': TREE, 'experiment': os.environ['GITHUB_SHA'],
        'run': os.environ['GITHUB_RUN_ID'], 'runner': os.environ.get('RUNNER_NAME'),
        'environment': os.environ.get('RUNNER_ENVIRONMENT'), 'timing': False}, indent=2))
    driver = evidence / 'bin/build-driver'
    run('bootstrap', ['clang', '-Isrc', '-Wall', '-Werror', '-Wno-unused-function',
                      '-Wno-unused-variable', 'build.c', '-o', driver], timeout=300)
    run('configure', [driver, 'generate', '--ci', '--cc', 'clang', '--linker', 'DEFAULT',
                      '--', '-DBUSTER_UNITY_BUILD=OFF'], timeout=300)
    for variant, trace, ordered in [('pristine', 0, 0), ('baseline', 1, 0),
                                   ('candidate', 1, 1), ('clean', 0, 1)]:
        if variant == 'baseline':
            run('overlay', [sys.executable, research/'overlay.py', work, research])
        if variant != 'pristine':
            config = work / 'src/buster/lib/compiler/llvm/research_config.h'
            config.write_text(f'#define R7_TRACE {trace}\n#define R7_ORDERED {ordered}\n')
        run('build-' + variant, [driver, 'build', '--config', 'Release', '-t', 'ide'], timeout=900)
        shutil.copy2(work/'build/Release/ide', evidence/'bin'/variant)
        if variant == 'pristine':
            common = ['cc', '-Isrc', '-Ibuild/generated', '-DBUSTER_UNITY_BUILD=1',
                      '-DBUSTER_INCLUDE_TESTS=0', '-g', 'src/buster/apps/ide/ide.c', '-lm']
            stage1, stage2 = evidence/'bin/stage1', evidence/'bin/stage2'
            rc, _ = run('selfhost-stage1', [evidence/'bin/pristine', *common, '-o', stage1],
                        timeout=300, required=False)
            if not rc:
                rc, _ = run('selfhost-stage2', [stage1, *common, '-o', stage2], timeout=300, required=False)
                if not rc:
                    identical = stage1.read_bytes() == stage2.read_bytes()
                    (evidence/'selfhost.json').write_text(json.dumps({'equal': identical}))
                    if not identical:
                        failures.append('baseline self-host fixed point differs')
            else:
                failures.append('baseline self-host stage1 unavailable; see log')
    run('overlay-diff', ['git', 'diff', '--', 'src/buster/lib/compiler/llvm/bitcode.c'])
    run('generator-clang', ['clang', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                           research/'generator.c', '-o', evidence/'bin/generator'])
    run('generator-gcc', ['gcc', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                         research/'generator.c', '-o', evidence/'bin/generator-gcc'])
    observer = evidence / 'observer.c'
    observer.write_text('extern int interaction_probe(void);\nint main(void) { return interaction_probe() != (int)sizeof(int); }\n')
    current = evidence / 'current.c'
    flags = ['cc', '-std=c17', '-target', 'x86_64-linux', '-O0', '-g0', '-fwrapv',
             '-fno-strict-aliasing', '-funsigned-char', '-ffrontend-ssa', '-fverify-codegen', '-emit-llvm']
    grid = [(u, d, order) for u in [0,128,512] for d in [1,8,32,128] for order in ['root','leaf']]
    grid += [(0,d,order) for d in [2,3,4] for order in ['root','leaf']]
    for u, d, order in grid:
        tag = f'u{u}-d{d}-{order}'
        _, source = run('generate-' + tag, [evidence/'bin/generator', u, d, order])
        _, independent = run('generate-gcc-' + tag, [evidence/'bin/generator-gcc', u, d, order])
        if source != independent:
            raise RuntimeError('generator host-compiler disagreement')
        current.write_text(source)
        (evidence/'inputs'/(tag+'.c')).write_text(source)
        run('valid-c-' + tag, ['clang', '-std=c17', '-pedantic-errors', '-fsyntax-only', current])
        outputs = []
        for variant in ['pristine', 'baseline', 'candidate', 'clean']:
            output = evidence/'outputs'/(tag+'-'+variant+'.bc')
            rc, log = run(tag+'-'+variant, [evidence/'bin'/variant, *flags, current, '-o', output],
                          extra={'BUSTER_R7_GRAPH':'1'}, required=False)
            row = {'case':tag, 'U':u, 'D':d, 'order':order, 'variant':variant, 'rc':rc,
                   'source_bytes':len(source.encode()), 'output_bytes':output.stat().st_size if output.exists() else 0}
            summaries = re.findall(r'^R7_SUMMARY (.*)$', log, re.M)
            if len(summaries) == 1:
                row.update({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', summaries[0])})
            rows.append(row)
            outputs.append(output.read_bytes() if not rc and output.exists() else None)
            save_rows()
        if outputs[0] is None or any(o != outputs[0] for o in outputs):
            failures.append(tag + ': emission failure or nonidentical bitcode')
        if outputs[2] is not None:
            for opt in ['-O0','-O2']:
                executable = evidence/'outputs'/(tag+opt)
                rc, _ = run('consume-'+tag+opt, ['clang', opt, evidence/'outputs'/(tag+'-candidate.bc'),
                                                observer, '-o', executable], required=False)
                if not rc:
                    rc, _ = run('execute-'+tag+opt, [executable], required=False)
                if rc:
                    failures.append(tag+opt+': independent consumer failed')
        save_rows()
    regressions = {
        'opaque-pointer-cycle':'struct A; struct B { struct A *a; }; struct A { struct B *b; }; int interaction_probe(void) { return 4; }',
        'atomic-padding':'struct Three { char x[3]; }; _Atomic(struct Three) a; int interaction_probe(void) { return (int)sizeof a; }',
        'duplicate-edges':'struct L { int x; }; struct P { struct L a, b; }; struct P f(struct P x, struct P y) { return x; } int interaction_probe(void) { return 4; }',
        'union-enum-array':'enum E { V=7 }; union U { int x; double y; }; struct S { enum E e; union U u; int a[3]; }; int interaction_probe(void) { return 4; }',
    }
    representative = [(name, source, []) for name,source in regressions.items()]
    descriptor = (work/'tools/throughput/workloads/cjson-1.7.19.workload').read_text()
    for name in ['cJSON.c','cJSON.h','cJSON_Utils.c','cJSON_Utils.h']:
        destination = evidence/'cjson'/name
        expected = re.search(r'input=\w+\t'+re.escape(name)+r'\t([0-9a-f]{64})\t', descriptor).group(1)
        data = urllib.request.urlopen('https://raw.githubusercontent.com/DaveGamble/cJSON/c859b25da02955fef659d658b8f324b5cde87be3/'+name, timeout=60).read()
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError('workload hash mismatch: '+name)
        destination.write_bytes(data)
    representative += [(name, (evidence/'cjson'/name).read_text(), ['-I'+str(evidence/'cjson')])
                       for name in ['cJSON.c','cJSON_Utils.c']]
    representative += [('repository-bitcode', (work/'src/buster/lib/compiler/llvm/bitcode.c').read_text(),
                        ['-Isrc','-Ibuild/generated','-I'+str(work/'src/buster/lib/compiler/llvm')])]
    for fixture in sorted((work/'src/buster/tests/compiler/llvm/fixtures').glob('*.c')):
        representative.append((fixture.stem, fixture.read_text(), []))
    for name, source, extra in representative:
        current.write_text(source)
        (evidence/'inputs'/(name+'.c')).write_text(source)
        outputs = []
        for variant in ['baseline','candidate']:
            output = evidence/'outputs'/(name+'-'+variant+'.bc')
            rc, log = run(name+'-'+variant, [evidence/'bin'/variant, *flags, *extra, current, '-o',output],
                          timeout=180, required=False)
            row = {'case':name, 'variant':variant, 'rc':rc,
                   'source_bytes':len(source.encode()), 'output_bytes':output.stat().st_size if output.exists() else 0}
            summaries = re.findall(r'^R7_SUMMARY (.*)$', log, re.M)
            if len(summaries)==1:
                row.update({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',summaries[0])})
            rows.append(row)
            outputs.append(output.read_bytes() if not rc and output.exists() else None)
        if outputs[0] != outputs[1]:
            failures.append(name+': differential emission mismatch')
        if outputs[1] is not None:
            run('consume-'+name, ['clang','-O0','-c',evidence/'outputs'/(name+'-candidate.bc'),
                                 '-o',evidence/'outputs'/(name+'.o')], required=False)
        save_rows()
    for variant in ['pristine','clean']:
        rc, _ = run('suite-'+variant,[evidence/'bin'/variant,'test','--verbose=1','--ci=1'],
                    timeout=600, required=False)
        if rc:
            failures.append('suite-'+variant+': see exact failure log')
    save_rows()
    print('R7_RESULT rows='+str(len(rows))+' differences_or_unavailable='+str(len(failures)),flush=True)
finally:
    save_rows()
    manifest=[]
    for path in sorted(evidence.rglob('*')):
        if path.is_file() and path.name!='SHA256SUMS':
            manifest.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+str(path.relative_to(evidence)))
    (evidence/'SHA256SUMS').write_text('\n'.join(manifest)+'\n')
