#!/usr/bin/env python3
"""Pinned diagnostic follow-up, never a production cache or timing gate.

All queries still execute the original implementation. The one-entry scalar
alternative is replayed against the actual successful query stream and checked
against each returned ID. Its saved visits are a counterfactual work count,
not a speedup. Per-query timestamps include instrumentation and are not used
for compiler performance acceptance. Uninstrumented A/A observations and
base/probe pairs are retained separately, without trimming or reruns.
"""
import difflib
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

import c_growth_experiment as initial

ROOT = Path.cwd()
BASE = initial.BASE
OUT = Path(os.environ['RUNNER_TEMP']) / 'c-growth-followup'
OUT.mkdir(parents=True, exist_ok=True)
initial.OUT = OUT
run = initial.run
digest = initial.digest
replace_once = initial.replace_once
SOURCE = initial.SOURCE


WRAPPER = r'''
// Disposable observer: this is NOT a production cache or a fast path.
// Every call below still executes the original query and returns its result.
static _Thread_local struct
{
    IrTypeId key;
    IrTypeId result;
    bool is_atomic;
    bool is_volatile;
    bool valid;
} c_growth_last;

BUSTER_C_INTERNAL IrTypeId c_ir_add_qualified_type(IrProgram* program, IrTypeId unqualified, bool is_atomic, bool is_volatile)
{
    unsigned long long before = c_growth_probe.qualified_visits;
    TimeDataType start = timestamp_take();
    IrTypeId result = c_ir_growth_qualified_original(program, unqualified, is_atomic, is_volatile);
    TimeDataType end = timestamp_take();
    unsigned long long visits = c_growth_probe.qualified_visits - before;
    bool valid = result.value != IR_ID_UNDERLYING_INVALID;
    bool hit = valid && c_growth_last.valid && c_growth_last.key.value == unqualified.value &&
               c_growth_last.is_atomic == is_atomic && c_growth_last.is_volatile == is_volatile;
    bool mismatch = hit && c_growth_last.result.value != result.value;
    fprintf(stderr, "C_GROWTH_QUERY key=%u atomic=%u volatile=%u result=%u valid=%u visits=%llu ns=%llu cache_hit=%u mismatch=%u table=%u\n",
            unqualified.value, (u32)is_atomic, (u32)is_volatile, result.value, (u32)valid,
            visits, (unsigned long long)timestamp_ns_between(start, end), (u32)hit, (u32)mismatch, program->types.count);
    if (valid)
    {
        c_growth_last.key = unqualified;
        c_growth_last.result = result;
        c_growth_last.is_atomic = is_atomic;
        c_growth_last.is_volatile = is_volatile;
        c_growth_last.valid = true;
    }
    return result;
}

'''


def instrumentation(original):
    text = initial.instrumentation(original)
    text = replace_once(text, '#include <stdio.h>', '#include <stdio.h>\n#include <buster/lib/time.h>')
    text = replace_once(text,
                        'BUSTER_C_INTERNAL IrTypeId c_ir_add_qualified_type(',
                        'BUSTER_C_INTERNAL IrTypeId c_ir_growth_qualified_original(')
    text = replace_once(text, '// GNU `__atomic_*` accepts an ordinary pointer,',
                        WRAPPER + '// GNU `__atomic_*` accepts an ordinary pointer,')
    text = replace_once(text, '    memset(&c_growth_probe, 0, sizeof(c_growth_probe));',
                        '    memset(&c_growth_probe, 0, sizeof(c_growth_probe));\n'
                        '    memset(&c_growth_last, 0, sizeof(c_growth_last));')
    return text


def extra_inputs():
    directory = OUT / 'independent-inputs'
    directory.mkdir(exist_ok=True)
    cases = []
    for n in (0, 64, 256, 1024):
        for q in (1, 16, 64, 256):
            # One function: Q does not add Q function types to the table.
            text = ''.join(f'struct Pad{i:04d} {{ int x; }};\n' for i in range(n))
            text += 'unsigned f(unsigned *p) { unsigned x = 0;\n'
            text += 'x += __atomic_load_n(p, 0);\n' * q
            text += 'return x; }\n'
            cases.append((f'one-body-n{n}-q{q}', text, []))
            # Actual frontend runtime helper route, unlike ordinary i128 divide.
            text = ''.join(f'int pad{i:04d};\n' for i in range(n))
            text += 'typedef unsigned __int128 wide;\n'
            text += 'wide f(_Atomic(wide) *p) { wide x = 0;\n'
            text += 'x += __c11_atomic_load(p, __ATOMIC_RELAXED);\n' * q
            text += 'return x; }\n'
            cases.append((f'atomic-runtime-n{n}-q{q}', text, ['-mcpu=baseline', '-mattr=-cx16']))
    padding = ''.join(f'struct Pad{i:04d} {{ int x; }};\n' for i in range(1024))
    body = 'unsigned f(unsigned *p) { unsigned x = 0;\n' + 'x += __atomic_load_n(p, 0);\n' * 256 + 'return x; }\n'
    cases.append(('one-body-early', '_Atomic(unsigned) early;\n' + padding + body, []))
    body = ('unsigned f(unsigned *p, unsigned long *q) { unsigned x = 0;\n' +
            'x += __atomic_load_n(p, 0); x += (unsigned)__atomic_load_n(q, 0);\n' * 128 + 'return x; }\n')
    cases.append(('one-body-alternating', padding + body, []))
    text = ('typedef unsigned __int128 wide;\nwide f(_Atomic(wide) *p) { wide x = 0;\n' +
            'x += __c11_atomic_load(p, __ATOMIC_RELAXED);\n' * 256 + 'return x; }\n')
    cases.append(('atomic-runtime-native-control', text, ['-mcpu=baseline', '-mattr=+cx16']))
    cases.append(('constant-early', 'const int needle = 7;\n' + ''.join(f'int pad{i:04d};\n' for i in range(1024)) +
                  ''.join(f'int f{i:04d}(void) {{ return __builtin_constant_p(needle + 1); }}\n' for i in range(256)), []))
    result = []
    for name, text, flags in cases:
        path = directory / (name + '.c')
        path.write_text(text)
        run('clang-valid-' + name, ['clang', '-std=gnu17', '-fsyntax-only', path])
        result.append((name, path, flags))
    url = 'https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip'
    data = urllib.request.urlopen(url, timeout=60).read()
    expected_zip = '1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d'
    if hashlib.sha256(data).hexdigest() != expected_zip:
        raise RuntimeError('SQLite pinned archive mismatch')
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        # Read only descriptor-named regular inputs, never archive paths.
        sql = directory / 'sqlite'
        sql.mkdir(exist_ok=True)
        for line in (ROOT / 'tools/throughput/workloads/sqlite-3.53.4.workload').read_text().splitlines():
            if line.startswith('input='):
                _, name, expected, size = line.split('\t')
                content = archive.read('sqlite-amalgamation-3530400/' + name)
                if len(content) != int(size) or hashlib.sha256(content).hexdigest() != expected:
                    raise RuntimeError('SQLite descriptor mismatch: ' + name)
                (sql / name).write_bytes(content)
        result.append(('sqlite3.c', sql / 'sqlite3.c', ['-O2', '-I' + str(sql), '-mcpu=baseline',
                       '-DSQLITE_THREADSAFE=1', '-DSQLITE_ENABLE_MATH_FUNCTIONS', '-DSQLITE_ENABLE_COLUMN_METADATA']))
    return result


def main():
    run('identity', ['git', 'rev-parse', 'HEAD', 'HEAD^{tree}', BASE + '^{tree}'])
    run('host', ['lscpu', '--json'])
    run('clang', ['clang', '--version'])
    run('kernel', ['uname', '-a'])
    run('clock-source', ['cat', 'src/buster/lib/time.h', 'src/buster/lib/time.c'])
    frozen = OUT / 'frozen'
    frozen.mkdir(exist_ok=True)
    archive = OUT / 'source.tar'
    run('archive', ['git', 'archive', '--format=tar', '-o', archive, BASE])
    with tarfile.open(archive) as tar:
        tar.extractall(frozen, filter='data')
    archive.unlink()
    original = SOURCE.read_text()
    if original != subprocess.check_output(['git', 'show', BASE + ':' + str(SOURCE)], text=True):
        raise RuntimeError('source is not pinned base')
    observer = instrumentation(original)
    (OUT / 'instrumentation.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True), observer.splitlines(True), fromfile='a/' + str(SOURCE), tofile='b/' + str(SOURCE))))
    driver = OUT / 'build-driver'
    run('bootstrap', ['clang', '-Isrc', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', 'build.c', '-o', driver])
    run('configure', [driver, 'generate', '--cc', 'clang', '--config', 'Release', '--no-sanitize', '--no-fuzz', '--no-lto', '--linker', 'DEFAULT', '--', '-DBUSTER_INCLUDE_TESTS=OFF', '-DBUSTER_DEBUG_INFO=OFF'])
    run('build-base', [driver, 'build', '--config', 'Release', '-t', 'ide'], 900)
    base_binary = OUT / 'ide-base'
    shutil.copy2('build/Release/ide', base_binary)
    shutil.copytree('build/generated', frozen / 'build/generated', dirs_exist_ok=True)
    shutil.copytree('build/generated', OUT / 'generated', dirs_exist_ok=True)
    cases = initial.inputs(frozen) + extra_inputs()
    (OUT / 'inputs.json').write_text(json.dumps([{'name': n, 'path': str(p), 'sha256': digest(p), 'bytes': p.stat().st_size, 'flags': f} for n, p, f in cases], indent=2))
    try:
        SOURCE.write_text(observer)
        run('build-probe', [driver, 'build', '--config', 'Release', '-t', 'ide'], 900)
        probe_binary = OUT / 'ide-probe'
        shutil.copy2('build/Release/ide', probe_binary)
    finally:
        SOURCE.write_text(original)
    outputs = OUT / 'output'
    outputs.mkdir(exist_ok=True)
    rows = []

    def compile_case(label, binary, case, observation):
        name, source, flags = case
        output = outputs / 'out.o'
        output.unlink(missing_ok=True)
        metrics = outputs / f'{label}-{name}-{observation}.metrics'
        argv = [binary, 'cc', '-g0', '-O0', *flags, '-fregister-allocator=fast', '-fsource-metrics=' + str(metrics), '-c', source, '-o', output]
        record = run(f'{label}-{name}-{observation}', argv, 120, required=False)
        traces = [line for line in record['stderr'].splitlines() if line.startswith('C_GROWTH_')]
        row = {'binary': label, 'binary_sha256': digest(binary), 'case': name, 'observation': observation,
               'status': record['status'], 'elapsed_seconds': record['elapsed_seconds'],
               'output_sha256': digest(output) if output.exists() else None,
               'output_bytes': output.stat().st_size if output.exists() else None, 'traces': traces}
        rows.append(row)
        with (OUT / 'observations.jsonl').open('a') as stream:
            stream.write(json.dumps(row, sort_keys=True) + '\n')
        return row

    for case in cases:
        compile_case('base', base_binary, case, 'census')
        compile_case('probe', probe_binary, case, 'census')
    # Fixed diagnostic family; timestamps are NOT an optimization verdict.
    timed = [case for case in cases if case[0] in ('tiny', 'unity', 'cJSON.c', 'sqlite3.c', 'one-body-n1024-q256', 'one-body-early')]
    for case in timed:
        for index in range(2):
            compile_case('base', base_binary, case, f'warmup-{index}')
            compile_case('probe', probe_binary, case, f'warmup-{index}')
        for index in range(7):
            # A/A calibration immediately adjacent to each base/probe pair.
            compile_case('base', base_binary, case, f'aa-first-{index}')
            compile_case('base', base_binary, case, f'aa-second-{index}')
            pair = [('base', base_binary), ('probe', probe_binary)]
            if index % 2:
                pair.reverse()
            for label, binary in pair:
                compile_case(label, binary, case, f'paired-{index}')
    failures = [r for r in rows if r['status'] != 0]
    census = [r for r in rows if r['observation'] == 'census']
    equal = all(census[i]['output_sha256'] == census[i + 1]['output_sha256'] for i in range(0, len(census), 2))
    mismatches = [r for r in rows if any('mismatch=1' in t for t in r['traces'])]
    (OUT / 'summary.json').write_text(json.dumps({'observations': len(rows), 'cases': len(cases), 'failures': len(failures), 'census_outputs_equal': equal, 'scalar_mismatches': len(mismatches)}, indent=2))
    shutil.copy2(__file__, OUT / 'c_growth_followup.py')
    shutil.copy2(initial.__file__, OUT / 'c_growth_experiment.py')
    output = outputs / 'out.o'
    output.unlink(missing_ok=True)
    shutil.rmtree(frozen)
    # experiment.log is still being appended by tee; hash only immutable files.
    (OUT / 'SHA256SUMS').write_text(''.join(digest(path) + '  ' + str(path.relative_to(OUT)) + '\n' for path in sorted(OUT.rglob('*')) if path.is_file() and path.name not in ('SHA256SUMS', 'experiment.log')))
    print((OUT / 'summary.json').read_text(), flush=True)
    if failures or not equal or mismatches:
        raise RuntimeError('follow-up incomplete; retain failed observations without reruns')


if __name__ == '__main__':
    main()
