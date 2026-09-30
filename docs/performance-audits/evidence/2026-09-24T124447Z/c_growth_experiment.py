#!/usr/bin/env python3
"""Disposable, pinned-source discovery experiment; not a timing acceptance gate.

Build through the existing driver, sequentially in one configured root. All
source changes are counters in the diagnostic binary and restored afterwards.
No deployment, credentials, settings, retirement policy or production changes.
"""
import difflib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import urllib.request

BASE = '2e942e80a87666409cf29d3e24a68d322b9e71fd'
ROOT = Path.cwd()
OUT = Path(os.environ['RUNNER_TEMP']) / 'c-growth-evidence'
OUT.mkdir(parents=True, exist_ok=True)
SOURCE = Path('src/buster/lib/compiler/frontend/c/c_gen.c')
FIELDS = ['qualified_calls', 'qualified_visits', 'qualified_hits',
          'constant_calls', 'entity_visits', 'global_visits',
          'runtime_calls', 'symbol_visits']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(name, argv, timeout=120, cwd=ROOT, required=True):
    start = time.time()
    record = {'name': name, 'argv': [str(x) for x in argv], 'cwd': str(cwd),
              'started': start, 'evidence_class': 'diagnostic-not-acceptance'}
    try:
        result = subprocess.run(record['argv'], cwd=cwd, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=timeout, env={**os.environ, 'LC_ALL': 'C', 'LANG': 'C'})
        record.update(status=result.returncode, stdout=result.stdout, stderr=result.stderr)
    except subprocess.TimeoutExpired as error:
        record.update(status='TIMEOUT', stdout=str(error.stdout), stderr=str(error.stderr))
    record['elapsed_seconds'] = time.time() - start
    with (OUT / 'runs.jsonl').open('a') as stream:
        stream.write(json.dumps(record, sort_keys=True) + '\n')
    print(name, record['status'], round(record['elapsed_seconds'], 3), flush=True)
    if required and record['status'] != 0:
        raise RuntimeError(f'{name} failed: {record["stderr"][-5000:]}')
    return record


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f'expected one source anchor: {old[:120]}')
    return text.replace(old, new, 1)


def instrumentation(original):
    fields = ', '.join(FIELDS)
    text = replace_once(original, '#include "c_internal.h"',
                        '#include "c_internal.h"\n#include <stdio.h>\n'
                        'static _Thread_local struct { unsigned long long ' + fields + '; } c_growth_probe;')
    specs = [
        ('BUSTER_C_INTERNAL IrTypeId c_ir_add_qualified_type(', '// GNU `__atomic_*`',
         [('    IrType* base =', '    c_growth_probe.qualified_calls += 1;\n    IrType* base ='),
          ('        IrType* type = program->types.types + type_index;',
           '        c_growth_probe.qualified_visits += 1;\n        IrType* type = program->types.types + type_index;'),
          ('            return type->id;', '            c_growth_probe.qualified_hits += 1;\n            return type->id;')]),
        ('BUSTER_C_INTERNAL bool c_ir_constant_from_global(', 'BUSTER_C_INTERNAL bool c_ir_constant_identifier(',
         [('    if (symbol.value', '    c_growth_probe.constant_calls += 1;\n    if (symbol.value'),
          ('            CEntity* entity =', '            c_growth_probe.entity_visits += 1;\n            CEntity* entity ='),
          ('            IrGlobal* global =', '            c_growth_probe.global_visits += 1;\n            IrGlobal* global =')]),
    ]
    # The runtime helper has a forward declaration. Select the definition.
    start = text.index('// Emit one compiler-runtime call whose ABI is fixed')
    end = text.index('BUSTER_C_INTERNAL bool c_ir_emit_runtime_call(', start)
    part = text[start:end]
    part = replace_once(part, '    IrTypeId function_type =',
                        '    c_growth_probe.runtime_calls += 1;\n    IrTypeId function_type =')
    part = replace_once(part, '        IrSymbol* candidate =',
                        '        c_growth_probe.symbol_visits += 1;\n        IrSymbol* candidate =')
    text = text[:start] + part + text[end:]
    for begin, finish, edits in specs:
        start = text.index(begin)
        end = text.index(finish, start)
        part = text[start:end]
        for old, new in edits:
            part = replace_once(part, old, new)
        text = text[:start] + part + text[end:]
    begin = text.index('CIRLowerResult c_lower_to_ir_with_options(')
    part = text[begin:]
    part = replace_once(part, '    CIRLowerResult result = {0};',
                        '    memset(&c_growth_probe, 0, sizeof(c_growth_probe));\n    CIRLowerResult result = {0};')
    fmt = 'C_GROWTH_V1 ' + ' '.join(key + '=%llu' for key in FIELDS)
    fmt += ' c_types=%u entities=%u ir_types=%u ir_symbols=%u functions=%u globals=%u diagnostics=%u\\n'
    values = ', '.join('c_growth_probe.' + key for key in FIELDS)
    report = ('    fprintf(stderr, "' + fmt + '", ' + values +
              ', parse.type_count, parse.entity_count, program->types.count, program->symbols.count,'
              ' module->function_count, module->global_count, result.diagnostic_count);\n')
    part = replace_once(part, '    arena_destroy(lowering_arena, 1);', report + '    arena_destroy(lowering_arena, 1);')
    return text[:begin] + part


def inputs(frozen):
    cases = []
    directory = OUT / 'inputs'
    directory.mkdir(exist_ok=True)
    for family in ('qualified', 'constant', 'runtime'):
        for n in (0, 64, 256, 1024):
            for q in (1, 16, 64, 256):
                if family == 'qualified':
                    text = ''.join(f'struct Pad{i:04d} {{ int x; }};\n' for i in range(n))
                    text += ''.join(f'int f{i:04d}(int *p) {{ return __atomic_load_n(p, 0); }}\n' for i in range(q))
                elif family == 'constant':
                    text = ''.join(f'int pad{i:04d};\n' for i in range(n))
                    text += 'const int needle = 7;\n'
                    text += ''.join(f'int f{i:04d}(void) {{ return __builtin_constant_p(needle + 1); }}\n' for i in range(q))
                else:
                    text = ''.join(f'int pad{i:04d};\n' for i in range(n))
                    text += ''.join(f'__int128 f{i:04d}(__int128 a, __int128 b) {{ return a / b; }}\n' for i in range(q))
                name = f'{family}-n{n}-q{q}'
                path = directory / (name + '.c')
                path.write_text(text)
                cases.append((name, path, []))
    for name, text in [
        ('tiny', 'int f(void) { return 7; }\n'),
        ('qualified-early-control', '_Atomic(int) early;\n' + ''.join(f'struct Pad{i:04d} {{ int x; }};\n' for i in range(1024)) + ''.join(f'int f{i:04d}(int *p) {{ return __atomic_load_n(p, 0); }}\n' for i in range(256))),
        ('ordinary-load-control', ''.join(f'int pad{i:04d};\n' for i in range(1024)) + ''.join(f'int f{i:04d}(int *p) {{ return *p; }}\n' for i in range(256))),
        ('inline-division-control', ''.join(f'int pad{i:04d};\n' for i in range(1024)) + ''.join(f'long f{i:04d}(long a, long b) {{ return a / b; }}\n' for i in range(256))),
    ]:
        path = directory / (name + '.c')
        path.write_text(text)
        cases.append((name, path, []))
    for name, path, _ in cases:
        run('clang-valid-' + name, ['clang', '-std=gnu17', '-fsyntax-only', path])
    cases.append(('basic-operations', frozen / 'tests/basic_c_operations.c', []))
    cases.append(('unity', frozen / 'src/buster/apps/ide/ide.c',
                  ['-g', '-I' + str(frozen / 'src'), '-I' + str(frozen / 'build/generated'),
                   '-DBUSTER_UNITY_BUILD=1', '-DBUSTER_INCLUDE_TESTS=0']))
    cjson = directory / 'cjson'
    cjson.mkdir(exist_ok=True)
    descriptor = (ROOT / 'tools/throughput/workloads/cjson-1.7.19.workload').read_text()
    for line in descriptor.splitlines():
        if line.startswith('input=source\t') or line.startswith('input=header\t'):
            _, name, expected, size = line.split('\t')
            data = urllib.request.urlopen('https://raw.githubusercontent.com/DaveGamble/cJSON/c859b25da02955fef659d658b8f324b5cde87be3/' + name, timeout=30).read()
            if len(data) != int(size) or hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError('cJSON descriptor identity mismatch: ' + name)
            (cjson / name).write_bytes(data)
    for name in ('cJSON.c', 'cJSON_Utils.c'):
        cases.append((name, cjson / name, ['-O2', '-I' + str(cjson)]))
    (OUT / 'inputs.json').write_text(json.dumps([{'name': n, 'path': str(p), 'sha256': digest(p), 'bytes': p.stat().st_size, 'flags': f} for n, p, f in cases], indent=2))
    return cases


def main():
    run('identity', ['git', 'rev-parse', 'HEAD', 'HEAD^{tree}', BASE + '^{tree}'])
    run('host', ['lscpu', '--json'])
    run('clang', ['clang', '--version'])
    run('kernel', ['uname', '-a'])
    frozen = OUT / 'frozen'
    frozen.mkdir(exist_ok=True)
    archive = OUT / 'source.tar'
    run('archive', ['git', 'archive', '--format=tar', '-o', archive, BASE])
    with tarfile.open(archive) as tar:
        tar.extractall(frozen, filter='data')
    archive.unlink()
    original = SOURCE.read_text()
    pinned = subprocess.check_output(['git', 'show', BASE + ':' + str(SOURCE)], text=True)
    if original != pinned:
        raise RuntimeError('working compiler source differs from pinned base')
    instrumented = instrumentation(original)
    (OUT / 'instrumentation.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True), instrumented.splitlines(True), fromfile='a/' + str(SOURCE), tofile='b/' + str(SOURCE))))
    driver = OUT / 'build-driver'
    run('bootstrap', ['clang', '-Isrc', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', 'build.c', '-o', driver])
    run('configure', [driver, 'generate', '--cc', 'clang', '--config', 'Release', '--no-sanitize', '--no-fuzz', '--no-lto', '--linker', 'DEFAULT', '--', '-DBUSTER_INCLUDE_TESTS=OFF', '-DBUSTER_DEBUG_INFO=OFF'], 120)
    run('build-base', [driver, 'build', '--config', 'Release', '-t', 'ide'], 900)
    base_binary = OUT / 'ide-base'
    shutil.copy2('build/Release/ide', base_binary)
    shutil.copytree('build/generated', frozen / 'build/generated', dirs_exist_ok=True)
    shutil.copytree('build/generated', OUT / 'generated', dirs_exist_ok=True)
    run('baseline-self-host', [driver, 'test_self_host', '--config', 'Release'], 600, required=False)
    cases = inputs(frozen)
    binaries = [('base', base_binary)]
    try:
        SOURCE.write_text(instrumented)
        run('build-probe', [driver, 'build', '--config', 'Release', '-t', 'ide'], 900)
        probe_binary = OUT / 'ide-probe'
        shutil.copy2('build/Release/ide', probe_binary)
        binaries.append(('probe', probe_binary))
    finally:
        SOURCE.write_text(original)
    outputs = OUT / 'output'
    outputs.mkdir(exist_ok=True)
    rows = []
    for label, binary in binaries:
        for name, source, flags in cases:
            output = outputs / 'out.o'
            output.unlink(missing_ok=True)
            metrics = outputs / f'{label}-{name}.metrics'
            argv = [binary, 'cc', '-g0', '-O0', *flags, '-fregister-allocator=fast', '-fsource-metrics=' + str(metrics), '-c', source, '-o', output]
            record = run(label + '-' + name, argv, 120, required=False)
            counters = [line for line in record['stderr'].splitlines() if line.startswith('C_GROWTH_V1 ')]
            rows.append({'binary': label, 'binary_sha256': digest(binary), 'case': name, 'status': record['status'], 'output_sha256': digest(output) if output.exists() else None, 'output_bytes': output.stat().st_size if output.exists() else None, 'counters': counters})
    (OUT / 'census.json').write_text(json.dumps(rows, indent=2))
    print(json.dumps(rows, indent=2), flush=True)
    # Retain source identities and reproducible generated inputs, not huge objects.
    for path in outputs.glob('*.o'):
        path.unlink()
    shutil.rmtree(frozen)
    (OUT / 'SHA256SUMS').write_text(''.join(digest(path) + '  ' + str(path.relative_to(OUT)) + '\n' for path in sorted(OUT.rglob('*')) if path.is_file() and path.name != 'SHA256SUMS'))
    failed = [r for r in rows if r['status'] != 0 or (r['binary'] == 'probe' and len(r['counters']) != 1)]
    equivalent = all(rows[i]['output_sha256'] == rows[i + len(cases)]['output_sha256'] for i in range(len(cases)))
    if failed or not equivalent:
        raise RuntimeError(f'incomplete census: failed={len(failed)} equivalent={equivalent}; retain all evidence')


if __name__ == '__main__':
    main()
