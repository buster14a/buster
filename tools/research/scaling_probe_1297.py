#!/usr/bin/env python3
"""Disposable #1297 census; production edits exist only in the runner worktree.
Hosted time/RSS are descriptive, never 9700X performance acceptance.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import time

PIN = '8f67df736f13d4edc055110a7a6d619a00a22eaf'
BLOB = '60dfd0e1c6174088326976954e618f6435e9f92a'
SOURCE = Path('src/buster/lib/compiler/frontend/c/c_gen.c')
FUNC = 'BUSTER_C_INTERNAL bool c_ir_predeclare_labeled_automatic_locals(CIntegerIrBuilder* builder, u32 start, u32 end)\n{\n'


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_logged(cmd: list[str], log: Path, timeout: int = 900) -> None:
    print('COMMAND', json.dumps(cmd), flush=True)
    with log.open('w') as stream:
        p = subprocess.run(cmd, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    if p.returncode:
        print(log.read_text()[-16000:], flush=True)
        raise RuntimeError(f'command failed ({p.returncode}): {cmd}')


def generate(padding: int, functions: int, labeled: int, locals_: int = 1, refs: int = 0) -> str:
    lines = [f'extern unsigned pad{i:08d};' for i in range(padding)]
    for i in range(functions):
        lines.append(f'unsigned f{i:08d}(void) {{')
        for j in range(locals_):
            lines.append(f'  unsigned x{j:08d} = {j + 1}u;')
        for _ in range(refs):
            lines.append('  x00000000 ^= 1u;')
        # Equal byte lengths. Entity counts are observed, not assumed.
        lines.append('  goto done; done: ;' if i < labeled else '  ;    ;    ;    ; ;')
        lines.append('  return x00000000; }')
    return '\n'.join(lines) + '\n'


def patch() -> bytes:
    raw = SOURCE.read_bytes()
    text = raw.decode()
    assert text.count(FUNC) == 1, 'function anchor changed'
    begin = text.index(FUNC)
    end = text.index('\n}\n', begin) + 3
    body = text[begin:end]
    body = body.replace(FUNC, FUNC + '    u64 census_visits = 0, census_locals = 0, census_selected = 0;\n')
    anchor = '        CEntity* value = builder->parse.entities + entity.value;\n'
    assert body.count(anchor) == 1
    body = body.replace(anchor, anchor + '        census_visits += 1;\n        census_locals += value->kind == C_ENTITY_LOCAL;\n')
    anchor = '        u32 alignment = local_type_value->layout.alignment;\n'
    assert body.count(anchor) == 1
    body = body.replace(anchor, '        census_selected += 1;\n' + anchor)
    anchor = '    return true;\n'
    assert body.count(anchor) == 1
    report = '''    fprintf(stderr, "SCALE_PREDECL start=%u end=%u entities=%u visits=%llu locals=%llu selected=%llu entity_size=%llu\\n",
            start, end, builder->parse.entity_count, (unsigned long long)census_visits,
            (unsigned long long)census_locals, (unsigned long long)census_selected,
            (unsigned long long)sizeof(CEntity));
'''
    body = body.replace(anchor, report + anchor)
    SOURCE.write_text('#include <stdio.h> /* Disposable census only. */\n' + text[:begin] + body + text[end:])
    return raw


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    assert subprocess.check_output(['git', 'hash-object', str(SOURCE)], text=True).strip() == BLOB
    changed = subprocess.check_output(['git', 'diff', '--name-only', PIN, 'HEAD'], text=True).splitlines()
    assert all(x in {'.github/workflows/research-1297-scaling.yml', 'tools/research/scaling_probe_1297.py'} for x in changed), changed
    metadata = {'subject_revision': PIN, 'source_blob': BLOB,
                'harness_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                'environment': 'GitHub-hosted diagnostic, not 9700X performance acceptance',
                'platform': list(os.uname()),
                'clang': subprocess.check_output(['clang', '--version'], text=True),
                'lscpu': subprocess.check_output(['lscpu'], text=True)}
    (out/'metadata.json').write_text(json.dumps(metadata, indent=2))
    driver = str(out/'build-driver')
    run_logged(['clang', '-Isrc', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-g', 'build.c', '-o', driver], out/'bootstrap.log')
    run_logged([driver, 'generate', '--cc', 'clang', '--config', 'Release', '--linker', 'DEFAULT', '--', '-DBUSTER_DEBUG_INFO=OFF'], out/'configure.log')
    run_logged([driver, 'build', '--config', 'Release', '-t', 'ide'], out/'plain-build.log')
    ide = Path('build/Release/ide')
    assert ide.is_file(), 'documented ide path missing'
    plain = out/'plain-ide'
    shutil.copy2(ide, plain)
    run_logged([driver, 'test_self_host', '--config', 'Release'], out/'self-host.log')
    original = patch()
    (out/'instrumentation.diff').write_text(subprocess.check_output(['git', 'diff', '--', str(SOURCE)], text=True))
    try:
        run_logged([driver, 'build', '--config', 'Release', '-t', 'ide'], out/'census-build.log')
        census = out/'census-ide'
        shutil.copy2(ide, census)
    finally:
        SOURCE.write_bytes(original)
    metadata['binary_sha256'] = {'plain': digest(plain), 'census': digest(census)}
    (out/'metadata.json').write_text(json.dumps(metadata, indent=2))
    print('BUILDS_COMPLETE', json.dumps(metadata['binary_sha256']), flush=True)
    inputs = out/'inputs'
    inputs.mkdir()
    cases: list[dict] = []
    def add(family: str, p: int, f: int, q: int, l: int = 1, r: int = 0) -> None:
        name = f'{family}-p{p}-f{f}-q{q}-l{l}-r{r}'
        path = inputs/(name+'.c')
        path.write_text(generate(p, f, q, l, r))
        cases.append(dict(name=name, family=family, padding=p, functions=f, labeled=q, locals_per_function=l, references=r, path=str(path), source_bytes=path.stat().st_size, sha256=digest(path)))
    for p, f, q in [(0,1,0),(0,1,1),(16,1,1),(16,2,2)]:
        add('smoke', p,f,q)
    for p in [0,16,64,256,1024,4096,16384]:
        add('padding',p,32,16)
        add('padding-control',p,32,0)
    for q in [0,1,2,4,8,16,32]:
        add('activation',4096,32,q)
    for n in [1,4,16,64,256,1024,2048]:
        add('diagonal',0,n,n)
        add('diagonal-control',0,n,0)
    for n in [1,4,16,64,256,1024]:
        add('local-count',0,1,1,n)
    for n in [0,4,16,64,256,1024]:
        add('references',256,1,1,1,n)
    (out/'manifest.json').write_text(json.dumps(cases, indent=2))
    pattern = re.compile(r'SCALE_PREDECL start=(\d+) end=(\d+) entities=(\d+) visits=(\d+) locals=(\d+) selected=(\d+) entity_size=(\d+)')
    object_path = out/'subject.o'
    def compile_one(binary: Path, path: Path, stem: str, strict: bool = False) -> tuple[dict, str]:
        command = [str(binary), 'cc', '-g0', '-c', '-march=baseline', str(path), '-o', str(object_path)]
        if strict:
            command += ['-fverify-codegen']
        stderr_path = out/(stem+'.stderr')
        timing_path = out/(stem+'.time')
        started = time.perf_counter_ns()
        with stderr_path.open('w') as error:
            completed = subprocess.run(['/usr/bin/time', '-f', '%U %S %M %F %R', '-o', str(timing_path), *command], stdout=subprocess.DEVNULL, stderr=error, timeout=120)
        elapsed = (time.perf_counter_ns()-started)/1e9
        timing = timing_path.read_text().strip().splitlines()[-1].split()
        record = dict(command=command, exit_code=completed.returncode, wall_seconds=elapsed,
                      user_seconds=float(timing[0]), system_seconds=float(timing[1]),
                      peak_rss_kib=int(timing[2]), major_faults=int(timing[3]), minor_faults=int(timing[4]))
        if completed.returncode:
            print('COMPILE_FAILURE', json.dumps(record), stderr_path.read_text()[-4000:], flush=True)
            raise RuntimeError(stem)
        record['object_sha256'] = digest(object_path)
        record['object_bytes'] = object_path.stat().st_size
        return record, stderr_path.read_text()
    for i, case in enumerate(cases):
        path = Path(case['path'])
        run_logged(['clang', '-std=c11', '-pedantic-errors', '-fsyntax-only', str(path)], out/(case['name']+'.clang.log'), timeout=120)
        baseline, _ = compile_one(plain, path, case['name']+'.plain')
        counted, stderr = compile_one(census, path, case['name']+'.census')
        rows = [dict(zip(['start','end','entities','visits','locals','selected','entity_size'], map(int, m))) for m in pattern.findall(stderr)]
        assert len(rows) == case['labeled'], (case,rows)
        assert all(row['visits'] == row['entities'] for row in rows), rows
        assert sum(row['selected'] for row in rows) == case['labeled'] * case['locals_per_function'], (case,rows)
        assert baseline['object_sha256'] == counted['object_sha256'], case
        compile_one(census, path, case['name']+'.strict', strict=True)
        summary = dict(case=case, plain=baseline, counted=counted, calls=len(rows),
                       entity_counts=sorted({r['entities'] for r in rows}),
                       visits=sum(r['visits'] for r in rows), selected=sum(r['selected'] for r in rows),
                       entity_size=rows[0]['entity_size'] if rows else None)
        with (out/'counts.jsonl').open('a') as f:
            f.write(json.dumps(summary)+'\n')
        print('COUNT', case['name'], 'calls',summary['calls'],'E',summary['entity_counts'],'visits',summary['visits'],'selected',summary['selected'],'bytes',baseline['object_bytes'], flush=True)
        if i == 3:
            print('SMOKE_PASS: four valid cases, actual loop counts, byte-identical objects; expanding predeclared grid',flush=True)
    schedule = [(rep, case) for rep in range(5) for case in cases]
    random.Random(1297).shuffle(schedule)
    for j, (rep, case) in enumerate(schedule):
        record, _ = compile_one(plain, Path(case['path']), f'timing-{j:04}')
        record.update(name=case['name'], repetition=rep)
        with (out/'timings.jsonl').open('a') as f:
            f.write(json.dumps(record)+'\n')
    print('TIMING_SCREEN_COMPLETE',len(schedule),'serial uninstrumented hosted compiles; NOT accepted performance',flush=True)
    for relative in ['tests/basic_c_frontend_ssa.c', 'tests/basic_c_statement_expression_value.c']:
        path = Path(relative)
        if not path.exists():
            print('REAL_SOURCE_MISSING',relative,flush=True)
            continue
        record, stderr = compile_one(census, path, path.stem+'.real', strict=True)
        rows = pattern.findall(stderr)
        print('REAL_SOURCE', relative, 'calls',len(rows),'visits',sum(int(r[3]) for r in rows), 'selected',sum(int(r[5]) for r in rows), 'rss_kib',record['peak_rss_kib'],flush=True)
    print('SUCCESS', len(cases), 'valid families; all census/plain objects identical and strict verification passed',flush=True)

if __name__ == '__main__':
    main()
