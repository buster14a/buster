"""Bounded compiler-driver correctness experiment; no timings or performance claims."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path.cwd()
out = Path(os.environ['PROBE_OUT']).resolve()
out.mkdir(parents=True, exist_ok=True)
records = []


def run(name, argv, cwd=root):
    try:
        result = subprocess.run([str(a) for a in argv], cwd=cwd, capture_output=True, timeout=45)
        code, stdout, stderr = result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as exc:
        code, stdout, stderr = 124, exc.stdout or b'', exc.stderr or b''
    (out / (name + '.stdout')).write_bytes(stdout)
    (out / (name + '.stderr')).write_bytes(stderr)
    row = dict(name=name, argv=[str(a) for a in argv], cwd=str(cwd), exit=code)
    records.append(row)
    print(json.dumps(row), flush=True)
    (out / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
    return code


run('source-id', ['git', 'rev-parse', 'HEAD', 'HEAD^{tree}'])
run('source-clean', ['git', 'status', '--porcelain'])
run('clang-version', ['clang', '--version'])
run('readelf-version', ['readelf', '--version'])
run('newest-audit', ['python3', 'tools/new_audit.py', '--newest'])
inputs = out / 'inputs'
inputs.mkdir(exist_ok=True)
(inputs/'provider.c').write_text('int shared;\nint get(void) { return shared; }\n')
(inputs/'consumer.c').write_text('int shared;\nint get(void);\nint main(void) { shared=7; return get()!=7; }\n')
(inputs/'extern.c').write_text('extern int shared;\nint get(void);\nint main(void) { shared=7; return get()!=7; }\n')
(inputs/'initialized.c').write_text('int shared=3;\nint get(void) { return shared; }\n')
options = {'default': [], 'common': ['-fcommon'], 'no-common': ['-fno-common'],
           'last-common': ['-fno-common', '-fcommon'], 'last-no-common': ['-fcommon', '-fno-common']}
buster = [root/'build/Release/ide', 'cc']
for compiler, prefix in [('clang', ['clang']), ('buster', buster)]:
    for mode, flags in options.items():
        folder = out / (compiler + '-' + mode)
        folder.mkdir(exist_ok=True)
        for case in ('provider', 'consumer', 'extern', 'initialized'):
            obj = folder/(case+'.o')
            status = run(f'{compiler}-{mode}-{case}-compile', [*prefix, '-g0', *flags, '-c', inputs/(case+'.c'), '-o', obj])
            if status == 0 and obj.exists():
                run(f'{compiler}-{mode}-{case}-symbols', ['readelf', '-Ws', obj])
        for control, left, right in [('tentative', 'provider', 'consumer'), ('extern', 'provider', 'extern'), ('initialized', 'initialized', 'consumer')]:
            objects = [folder/(left+'.o'), folder/(right+'.o')]
            if all(p.exists() for p in objects):
                exe = folder/control
                status = run(f'{compiler}-{mode}-{control}-link', ['clang', *objects, '-o', exe])
                if status == 0:
                    run(f'{compiler}-{mode}-{control}-execute', [exe])
for mode in ('common', 'no-common'):
    for jobs in (1, 2):
        for control, left, right in [('tentative', 'provider', 'consumer'), ('extern', 'provider', 'extern'), ('initialized', 'initialized', 'consumer')]:
            exe = out/f'buster-{mode}-{jobs}-{control}'
            status = run(f'buster-{mode}-{jobs}-{control}-source-link', [*buster, '-g0', *options[mode], f'-fcompile-jobs={jobs}', inputs/(left+'.c'), inputs/(right+'.c'), '-o', exe])
            if status == 0:
                run(f'buster-{mode}-{jobs}-{control}-source-execute', [exe])
run('source-symbol-sites', ['git', 'grep', '-n', '-E', 'tentative|SHN_COMMON|common_symbol|is_common|compatible_codegen_option|CompilerDriverInvocation|IrSymbol|ObjectSymbol', '--', 'src/buster/lib/compiler'])
manifest = []
for p in sorted(out.rglob('*')):
    if p.is_file():
        manifest.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(out)))
(out/'SHA256SUMS').write_text('\n'.join(manifest)+'\n')
