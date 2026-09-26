#!/usr/bin/env python3
"""Hosted deterministic census of one possible scratch-watermark externality.

No timings or production-default edits. Builds serially in one configured root,
freezes the compilers, restores production source, and compares identical input
objects. A trace uses libc stderr, never Buster's scratch-backed formatter.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

PIN = 'ade6ac4b6ecb21f30b61b656439bac476c145e2f'
TREE = '4c5306221fdb22fccc929b55e333163742de17d0'
BLOB = 'ede2de412850975123da3f4a473f017591655a75'
root = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
records: list[dict] = []

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(argv: list[str], name: str, *, limit: int = 900,
        env: dict[str, str] | None = None, required: bool = True) -> dict:
    stem = out / name
    stem.parent.mkdir(parents=True, exist_ok=True)
    timed_out = False
    with Path(str(stem)+'.stdout').open('wb') as stdout, Path(str(stem)+'.stderr').open('wb') as stderr:
        try:
            process = subprocess.run(argv, cwd=root, env=env, stdout=stdout,
                                     stderr=stderr, timeout=limit, check=False)
            status = process.returncode
        except subprocess.TimeoutExpired:
            status, timed_out = None, True
    record = {'name': name, 'argv': argv, 'cwd': str(root),
              'returncode': status, 'timeout': timed_out}
    Path(str(stem)+'.json').write_text(json.dumps(record, indent=2)+'\n')
    records.append(record)
    with (out/'commands.jsonl').open('a') as stream:
        stream.write(json.dumps(record)+'\n')
    print(name, status, flush=True)
    if required and status != 0:
        raise RuntimeError(f'{name}: command failed; retained logs')
    return record

identity = subprocess.check_output(['git','rev-parse','HEAD','HEAD^{tree}'], cwd=root, text=True).splitlines()
assert identity == [PIN, TREE], identity
(out/'production.txt').write_text('\n'.join(identity)+'\n')
source = root/'src/buster/lib/compiler/frontend/c/c_gen.c'
original = source.read_bytes()
assert hashlib.sha1(b'blob '+str(len(original)).encode()+b'\0'+original).hexdigest() == BLOB
run(['git','archive','--format=tar','--output='+str(out/'production.tar'),PIN], 'archive')
for name, argv in [('clang',['clang','--version']),('gcc',['gcc','--version']),
                   ('kernel',['uname','-a']),('cpu',['lscpu'])]:
    run(argv,'environment/'+name)
shutil.copy2('/etc/os-release',out/'environment/os-release')
driver = out/'build-driver'
run(['clang','-Isrc','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable',
     '-fwrapv','-fno-strict-aliasing','-funsigned-char','build.c','-o',str(driver)],'bootstrap')
build_env = dict(os.environ, BUSTER_CC='clang')
run([str(driver),'generate','-DBUSTER_CI=ON','-DBUSTER_INCLUDE_TESTS=OFF',
     '-DBUSTER_BENCH_ALLOCATIONS=ON','-DBUSTER_LTO=OFF'], 'generate',env=build_env)
run([str(driver),'build','--config','Release','-t','ide'],'build-base',env=build_env)
base = out/'base-ide'; probe = out/'probe-ide'
shutil.copy2(root/'build/Release/ide',base)
text = original.decode()
anchor = '    CIrPromotedMemberWork* work = arena_allocate(arena, CIrPromotedMemberWork, capacity);'
assert text.count(anchor) == 1
replacement = '''    u64 scratch_probe_before = arena_dirty_position(arena);
    u64 scratch_probe_start = arena->position;
    CIrPromotedMemberWork* work = arena_allocate(arena, CIrPromotedMemberWork, capacity);
    fprintf(stderr, "SCRATCH_WATERMARK %llu %llu %llu %u %llu\\n",
            (unsigned long long)scratch_probe_before,
            (unsigned long long)scratch_probe_start,
            (unsigned long long)arena->position, capacity,
            (unsigned long long)(sizeof(*work) * capacity));'''
text = '#include <stdio.h> /* Disposable hosted observer, not a production include. */\n'+text.replace(anchor,replacement)
try:
    source.write_text(text)
    run(['git','diff','--','src/buster/lib/compiler/frontend/c/c_gen.c'],'probe-patch')
    shutil.copy2(source,out/'c_gen.observed.c')
    run([str(driver),'build','--config','Release','-t','ide'],'build-probe',env=build_env)
    shutil.copy2(root/'build/Release/ide',probe)
finally:
    source.write_bytes(original)
assert source.read_bytes() == original
for name in ['CMakeCache.txt','compile_commands.json']:
    shutil.copy2(root/'build'/name,out/name)
(out/'compilers.json').write_text(json.dumps({'base':sha(base),'probe':sha(probe)},indent=2)+'\n')

inputs = out/'inputs'; inputs.mkdir(exist_ok=True)
subjects: list[tuple[str,Path,list[str],bool]] = []
for types in [0,256,4096]:
    for sites in [1,64]:
        for breadth in [0,8]:
            name = f'T{types}-Q{sites}-K{breadth}'
            prefix = ''.join(f'struct U{i} {{ int unrelated_{i}; }};\n' for i in range(types))
            fields = ('int value;\n' if breadth == 0 else
                      ''.join(f'struct {{ int value_{i}; }};\n' for i in range(breadth)))
            member = 'value' if breadth == 0 else 'value_0'
            expr = '+'.join(f'sizeof(object.{member})' for _ in range(sites))
            body = prefix+'struct Root {\n'+fields+'};\nstruct Root object;\n'
            body += f'int main(void) {{ return ({expr}) != {sites*4}; }}\n'
            path = inputs/(name+'.c'); path.write_text(body)
            subjects.append((name,path,[],True))
for filename in ['basic_c_operations.c','basic_c_frontend_ssa.c',
                 'basic_c_sizeof_anonymous_aggregate.c','basic_c_unnamed_initializer_members.c',
                 'basic_c_packed_layout.c','basic_c_union_designator_merge.c']:
    subjects.append((Path(filename).stem,root/'tests'/filename,[],False))
subjects.append(('self-compile',root/'src/buster/apps/ide/ide.c',
                 ['-Isrc','-Ibuild/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0'],False))
rows = []
run_env = dict(os.environ,BUSTER_ALLOCATION_CENSUS='1')
trace_re = re.compile(r'^SCRATCH_WATERMARK (\d+) (\d+) (\d+) (\d+) (\d+)$',re.M)
common = ['cc','-target','x86_64-linux','-std=gnu17','-fwrapv','-fno-strict-aliasing',
          '-funsigned-char','-g0','-O0','-fverify-codegen','-fregister-allocator=fast','-fno-machine-fallback']
for name,path,extra,executable in subjects:
    d = out/'subjects'/name; d.mkdir(parents=True)
    object_path = out/'same-output.o'
    results = {}
    for label,compiler in [('base',base),('probe',probe)]:
        object_path.unlink(missing_ok=True)
        result = run([str(compiler),*common,*extra,str(path),'-c','-o',str(object_path)],
                     f'subjects/{name}/{label}',env=run_env,required=False)
        results[label] = result['returncode']
        if result['returncode'] == 0:
            shutil.copy2(object_path,d/(label+'.o'))
    identical = results['base']==results['probe']==0 and (d/'base.o').read_bytes()==(d/'probe.o').read_bytes()
    traces = [tuple(map(int,m)) for m in trace_re.findall((d/'probe.stderr').read_text(errors='replace'))]
    increases = [(before,start,end,capacity,requested) for before,start,end,capacity,requested in traces if end>before]
    row = {'subject':name,'input_sha256':sha(path),'compile':results,'identical_objects':identical,
           'query_count':len(traces),'dirty_raise_count':len(increases),
           'sum_dirty_raises':sum(end-before for before,start,end,capacity,requested in increases),
           'requested_bytes':sum(requested for before,start,end,capacity,requested in traces),
           'max_requested_bytes':max((requested for before,start,end,capacity,requested in traces),default=0),
           'dirty_raises':increases}
    # Only these simple ISO-C fixtures have an explicitly supplied runtime oracle.
    if executable and identical:
        for label in ['base','probe']:
            program = d/(label+'-program')
            run(['clang',str(d/(label+'.o')),'-o',str(program)],f'subjects/{name}/{label}-link')
            run([str(program)],f'subjects/{name}/{label}-run',limit=20)
        reference = d/'reference'
        run(['clang','-std=c11','-pedantic-errors',str(path),'-o',str(reference)],f'subjects/{name}/reference-build')
        run([str(reference)],f'subjects/{name}/reference-run',limit=20)
    rows.append(row)
    (out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
    print('CENSUS',json.dumps(row),flush=True)

(out/'interpretation.json').write_text(json.dumps({
    'hypothesis':'Unwritten all-type member-search capacity raises the scratch dirty watermark and forces avoidable later zeroing.',
    'first_falsifier':'If allocation end never exceeds incoming dirty high-water, this producer cannot raise that watermark on the observed workload.',
    'limits':'No timings, RSS, PMU, 9700X measurement, production optimization, full suite, sanitizer or self-host fixed-point gate. self-compile is an object compile of the exact original unity source.',
    'source_restored':source.read_bytes()==original,
    'passing_object_pairs':sum(row['identical_objects'] for row in rows),
    'subject_count':len(rows)},indent=2)+'\n')
manifest=[]
for path in sorted(out.rglob('*')):
    if path.is_file() and path.name!='SHA256SUMS':
        manifest.append(sha(path)+'  '+str(path.relative_to(out)))
(out/'SHA256SUMS').write_text('\n'.join(manifest)+'\n')
if not all(row['identical_objects'] for row in rows):
    raise RuntimeError('One or more source compilations or equivalence controls failed; see retained evidence.')
