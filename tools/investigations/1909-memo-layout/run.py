"""Hosted, fixed-budget correctness/census driver; no timing verdict."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path.cwd()
OUT = ROOT / 'memo-evidence'
OUT.mkdir(exist_ok=True)
HERE = pathlib.Path(__file__).resolve().parent
records = []

def command(name, args, env=None, required=True):
    result = subprocess.run(args, cwd=ROOT, env=dict(os.environ, **(env or {})), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (OUT / (name + '.stdout')).write_bytes(result.stdout)
    (OUT / (name + '.stderr')).write_bytes(result.stderr)
    records.append(dict(name=name, argv=list(map(str, args)), status=result.returncode, env=env or {}))
    (OUT / 'commands.json').write_text(json.dumps(records, indent=2))
    print(name, result.returncode, flush=True)
    if required and result.returncode:
        raise RuntimeError(name)
    return result

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def restore():
    for path, data in originals.items():
        path.write_bytes(data)

originals = {ROOT/p: (ROOT/p).read_bytes() for p in ['src/buster/lib/compiler/frontend/c/c_parse.c', 'src/buster/lib/compiler/frontend/c/c_internal.h']}
identities = dict(source_commit='8f67df736f13d4edc055110a7a6d619a00a22eaf', source_tree='8810c6603e47e5eecf26e1d73633619a347c70d8', original_sources={str(p.relative_to(ROOT)):sha(p) for p in originals})
command('host', ['bash','-c','uname -a; lscpu; clang --version'])
driver = str(OUT/'build-driver')
command('driver-build', ['clang','-Isrc','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable','-g','build.c','-o',driver])
command('configure', [driver,'generate','--cc','clang','--ci','--linker','DEFAULT'])
command('baseline-build', [driver,'build','--config','Release','-t','ide'])
command('baseline-fixedpoint', [driver,'test_self_host','--config','Release'], required=False)
baseline = OUT/'baseline-ide'
shutil.copy2(ROOT/'build/Release/ide', baseline)
identities['baseline_binary'] = sha(baseline)
command('baseline-focused', [str(baseline),'test','--module=c_frontend_tests,compiler_driver_tests','--ci=1','--verbose=0'], required=False)
fixtures = OUT/'inputs'
fixtures.mkdir(exist_ok=True)
(fixtures/'tiny.c').write_text('int empty(void) { return 0; }\nint one(int x) { return x + 1; }\n')
(fixtures/'mixed.c').write_text('struct S { int x; double y; };\nint mix(int x) { struct S s = {x, 2.0}; if (sizeof(s.x) == sizeof(int)) { int x = s.x; x += sizeof((x + 1)); s.x = x; } return _Generic(s.x, int:s.x, default:0) + (x ? x : s.x); }\n')
(fixtures/'small-populations.c').write_text('\n'.join(f'int f{i}(int x) {{ return x + {i}; }}' for i in range(128)))
(fixtures/'large-body.c').write_text('int many(int x) { int s=0;\n' + 's += x;\n'*16384 + 'return s; }\n')
(fixtures/'invalid.c').write_text('int bad(int x) { const int y=1; y=x; return sizeof(*x); }\n')
inputs = [('unity','src/buster/apps/ide/ide.c'), ('operations','tests/basic_c_operations.c')] + [(p.stem,str(p)) for p in sorted(fixtures.glob('*.c'))]
common = ['cc','-Isrc','-Ibuild/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0','-g0','-O0']
try:
    command('observer-overlay', ['python3',str(HERE/'observe.py'), str(ROOT/'src/buster/lib/compiler/frontend/c/c_parse.c')])
    identities['observer_sources'] = {str(p.relative_to(ROOT)):sha(p) for p in originals}
    command('observer-build', [driver,'build','--config','Release','-t','ide'])
    observer = OUT/'observer-ide'
    shutil.copy2(ROOT/'build/Release/ide', observer)
    identities['observer_binary'] = sha(observer)
    restore()
    for name, path in inputs:
        args = common + ['-c',path,'-o',str(OUT/'output.o')]
        b = command(name+'-baseline', [str(baseline)]+args, required=False)
        if (OUT/'output.o').exists(): shutil.copy2(OUT/'output.o',OUT/(name+'-baseline.o')); (OUT/'output.o').unlink()
        o = command(name+'-observer', [str(observer)]+args, env={'BUSTER_MEMO_TRACE':str(OUT/(name+'.trace'))}, required=False)
        if (OUT/'output.o').exists(): shutil.copy2(OUT/'output.o',OUT/(name+'-observer.o')); (OUT/'output.o').unlink()
        objects_equal = not (OUT/(name+'-baseline.o')).exists() or ((OUT/(name+'-observer.o')).exists() and sha(OUT/(name+'-baseline.o'))==sha(OUT/(name+'-observer.o')))
        assert b.returncode==o.returncode and b.stdout==o.stdout and b.stderr==o.stderr and objects_equal, name
    command('replay-build', ['clang','-std=c11','-O2','-Wall','-Wextra','-Werror',str(HERE/'replay.c'),'-o',str(OUT/'replay')])
    command('replay-selftest', [str(OUT/'replay'),'--self-test'])
    command('replay-sanitized-build', ['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-Wall','-Wextra','-Werror',str(HERE/'replay.c'),'-o',str(OUT/'replay-sanitized')])
    command('replay-sanitized-selftest', [str(OUT/'replay-sanitized'),'--self-test'])
    for name, path in inputs:
        command(name+'-replay', [str(OUT/'replay'),str(OUT/(name+'.trace'))])
    command('candidate-overlay', ['python3',str(HERE/'hotcold.py'), str(ROOT),'--poison'])
    identities['candidate_sources'] = {str(p.relative_to(ROOT)):sha(p) for p in originals}
    command('candidate-build', [driver,'build','--config','Release','-t','ide'])
    candidate = OUT/'candidate-ide'
    shutil.copy2(ROOT/'build/Release/ide', candidate)
    identities['candidate_binary'] = sha(candidate)
    command('candidate-focused', [str(candidate),'test','--module=c_frontend_tests,compiler_driver_tests','--ci=1','--verbose=0'], required=False)
    command('candidate-fixedpoint', [driver,'test_self_host','--config','Release'], required=False)
    restore()
    for name, path in inputs:
        c = command(name+'-candidate', [str(candidate)]+common+['-c',path,'-o',str(OUT/'output.o')], required=False)
        if (OUT/'output.o').exists(): shutil.copy2(OUT/'output.o',OUT/(name+'-candidate.o')); (OUT/'output.o').unlink()
        bstatus = next(r['status'] for r in records if r['name']==name+'-baseline')
        bexists = (OUT/(name+'-baseline.o')).exists()
        assert c.returncode==bstatus and c.stdout==(OUT/(name+'-baseline.stdout')).read_bytes() and c.stderr==(OUT/(name+'-baseline.stderr')).read_bytes(), name
        assert not bexists or ((OUT/(name+'-candidate.o')).exists() and sha(OUT/(name+'-baseline.o'))==sha(OUT/(name+'-candidate.o'))), name
finally:
    restore()
    (OUT/'identities.json').write_text(json.dumps(identities, indent=2))
    manifest = {str(p.relative_to(OUT)):sha(p) for p in OUT.rglob('*') if p.is_file()}
    (OUT/'manifest.json').write_text(json.dumps(manifest, indent=2))
