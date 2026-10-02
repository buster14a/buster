"""Issue #2277: bounded Windows diagnostic; not a performance acceptance harness.

Entry main builds identical first-party C inputs, retains every process result,
then compares compile-only latency, COFF text-section bytes and QPC runtime.
No production flags or test/retirement authority are modified.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import struct
import subprocess
import time

KERNEL = r'''
#ifndef NEGATIVE_CONTROL
#define NEGATIVE_CONTROL 0
#endif
unsigned helper(unsigned x) { return (x * 33u) ^ (x >> 7u); }
unsigned sum_scale(const unsigned *a, const unsigned *b, unsigned n, unsigned salt)
{
    unsigned result = salt;
    (void)b;
    for (unsigned i = 0; i < n; i += 1) result += a[i] * 3u + 7u;
    return result + NEGATIVE_CONTROL;
}
unsigned divide7(const unsigned *a, const unsigned *b, unsigned n, unsigned salt)
{
    unsigned result = 0;
    (void)b;
    for (unsigned i = 0; i < n; i += 1) result += (a[i] + salt) / 7u;
    return result;
}
unsigned dot(const unsigned *a, const unsigned *b, unsigned n, unsigned salt)
{
    unsigned result = salt;
    for (unsigned i = 0; i < n; i += 1) result += a[i] * b[i];
    return result;
}
unsigned calls(const unsigned *a, const unsigned *b, unsigned n, unsigned salt)
{
    unsigned result = 0;
    (void)b;
    for (unsigned i = 0; i < n; i += 1) result += helper(a[i] + salt);
    return result;
}
'''

CALLER = r'''
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned (*Kernel)(const unsigned *, const unsigned *, unsigned, unsigned);
unsigned sum_scale(const unsigned *, const unsigned *, unsigned, unsigned);
unsigned divide7(const unsigned *, const unsigned *, unsigned, unsigned);
unsigned dot(const unsigned *, const unsigned *, unsigned, unsigned);
unsigned calls(const unsigned *, const unsigned *, unsigned, unsigned);
static unsigned reference(const unsigned *a, const unsigned *b, unsigned n, unsigned salt, unsigned kind)
{
    unsigned result = (kind == 0u || kind == 2u) ? salt : 0u;
    for (unsigned i = 0; i < n; i += 1)
    {
        unsigned x = a[i];
        if (kind == 0u) result += x + x + x + 7u;
        else if (kind == 1u) result += (x + salt) / 7u;
        else if (kind == 2u) result += x * b[i];
        else { x += salt; result += ((x << 5u) + x) ^ (x >> 7u); }
    }
    return result;
}
int main(int argc, char **argv)
{
    int result = 0;
    unsigned kind = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0u;
    unsigned rounds = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 32768u;
    unsigned a[1024], b[1024];
    Kernel functions[4] = {sum_scale, divide7, dot, calls};
    if (kind >= 4u) result = 2;
    for (unsigned i = 0; i < 1024u; i += 1)
    {
        a[i] = (i * 1664525u + 1013904223u) & 1023u;
        b[i] = (i * 1103515245u + 12345u) & 2047u;
    }
    if (result == 0)
    {
        Kernel function = functions[kind];
        for (unsigned salt = 0; salt < 8u; salt += 1)
        {
            unsigned actual = function(a, b, 1024u, salt);
            unsigned expected = reference(a, b, 1024u, salt, kind);
            if (actual != expected) { printf("MISMATCH kind=%u actual=%u expected=%u\n", kind, actual, expected); result = 3; }
        }
        if (result == 0)
        {
            LARGE_INTEGER begin, end, frequency;
            unsigned checksum = 0;
            if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&begin)) result = 4;
            if (result == 0)
            {
                for (unsigned r = 0; r < rounds; r += 1) checksum += function(a, b, 1024u, r);
                if (!QueryPerformanceCounter(&end)) result = 4;
                else printf("RESULT ticks=%lld frequency=%lld checksum=%u\n", end.QuadPart - begin.QuadPart, frequency.QuadPart, checksum);
            }
        }
    }
    return result;
}
'''

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def text_sections(path):
    data = Path(path).read_bytes()
    if len(data) < 20:
        raise ValueError('truncated COFF')
    machine, count = struct.unpack_from('<HH', data)
    if machine != 0x8664 or count == 0 or 20 + count * 40 > len(data):
        raise ValueError('not ordinary x64 COFF')
    text = bytearray()
    for index in range(count):
        offset = 20 + index * 40
        name = data[offset:offset + 8].split(b'\0')[0]
        size, start = struct.unpack_from('<II', data, offset + 16)
        if start + size > len(data):
            raise ValueError('COFF section outside file')
        if name.startswith(b'.text'):
            text.extend(data[start:start + size])
    if not text:
        raise ValueError('missing text sections')
    return {'text_section_bytes': len(text), 'text_sha256': hashlib.sha256(text).hexdigest(), 'object_bytes': len(data)}

def run(argv, root, records, timeout=120, required=True):
    sequence = len(records)
    log = root / ('process-%04d.log' % sequence)
    started = time.perf_counter_ns()
    timed_out = False
    with log.open('wb') as stream:
        process = subprocess.Popen([str(x) for x in argv], stdout=stream, stderr=subprocess.STDOUT)
        try:
            status = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(process.pid)], stdout=stream, stderr=stream, timeout=30, check=False)
            status = process.wait(timeout=30)
    record = {'sequence': sequence, 'argv': [str(x) for x in argv], 'wall_ns': time.perf_counter_ns() - started,
              'exit_code': status, 'timeout': timed_out, 'log': log.name, 'cpu_seconds': None,
              'peak_process_tree_rss_bytes': None}
    records.append(record)
    (root / 'processes.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    if required and (status != 0 or timed_out):
        raise RuntimeError('process failed: %s\n%s' % (argv, log.read_text(errors='replace')[-8000:]))
    return record, log.read_text(errors='replace')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ide', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    ide = args.ide.resolve()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    records = []
    identity = {'schema': 'BUSTER_MSVC_DIAGNOSTIC_V1', 'buster_source': os.environ['BUSTER_COMPARISON_BASE'],
                'buster_binary_sha256': sha(ide), 'platform': platform.platform(), 'cpu': platform.processor(),
                'logical_cpus': os.cpu_count(), 'image_version': os.environ.get('ImageVersion'),
                'image_os': os.environ.get('ImageOS'), 'runner_name': os.environ.get('RUNNER_NAME'),
                'cl_path': shutil.which('cl'), 'cl_sha256': sha(shutil.which('cl')),
                'workflow_sha': os.environ.get('GITHUB_SHA'), 'qualified_performance_acceptance': False,
                'memory_and_cpu_accounting': 'not measured: no complete child-tree accounting',
                'license_buster': 'First-party license grant missing/unresolved; LICENSES/README.md',
                'license_msvc': 'Proprietary Microsoft Visual Studio/Build Tools terms'}
    (root / 'identity.json').write_text(json.dumps(identity, indent=2), encoding='utf-8')
    sources = {'kernels': KERNEL, 'tiny': 'unsigned add(unsigned a, unsigned b) { return a + b; }\n',
               'functions256': '\n'.join('unsigned f%u(const unsigned *a, unsigned n, unsigned salt) { unsigned s = salt; for (unsigned i=0; i<n; i+=1) s += a[i] * %uu + %uu; return s; }' % (i, i % 31 + 1, i) for i in range(256))}
    for name, content in sources.items():
        (root / (name + '.c')).write_text(content, encoding='utf-8')
    (root / 'caller.c').write_text(CALLER, encoding='utf-8')
    run(['cl', '/nologo', '/Bv', '/c', '/TC', '/std:c17', '/J', '/Od', root / 'tiny.c', '/Fo' + str(root / 'identity.obj')], root, records)
    run(['clang', '--version'], root, records)
    variants = ['buster-fast', 'buster-quality', 'msvc-Od', 'msvc-O2']

    def command(variant, source, output):
        if variant.startswith('buster'):
            allocator = variant.split('-')[1]
            result = [ide, 'cc', '-target', 'x86_64-pc-windows-msvc', '-march=baseline', '-std=c17', '-funsigned-char', '-O2',
                      '-fregister-allocator=' + allocator, '-fno-machine-fallback', '-g0', '-c', source, '-o', output]
        else:
            result = ['cl', '/nologo', '/TC', '/std:c17', '/Zc:preprocessor', '/J', '/' + variant.split('-')[1], '/c', source, '/Fo' + str(output)]
        return result

    compile_rows = []
    for name in sources:
        for trial in range(8):
            order = variants if trial % 2 == 0 else list(reversed(variants))
            for variant in order:
                output = root / ('%s-%s-%u.obj' % (name, variant, trial))
                record, _ = run(command(variant, root / (name + '.c'), output), root, records)
                compile_rows.append({'workload': name, 'variant': variant, 'trial': trial, 'warmup': trial == 0,
                                     'source_bytes': (root / (name + '.c')).stat().st_size, 'source_sha256': sha(root / (name + '.c')),
                                     'process': record['sequence'], 'wall_ns': record['wall_ns'], **text_sections(output)})
                (root / 'compile.json').write_text(json.dumps(compile_rows, indent=2), encoding='utf-8')
    for name in sources:
        for variant in variants:
            hashes = {r['text_sha256'] for r in compile_rows if r['workload'] == name and r['variant'] == variant}
            if len(hashes) != 1:
                raise RuntimeError('unstable text bytes: %s %s' % (name, variant))
    # Confirm the native -O spelling boundary without calling it a speed result.
    for level in ['0', '3']:
        output = root / ('buster-O%s.obj' % level)
        run([ide, 'cc', '-target', 'x86_64-pc-windows-msvc', '-march=baseline', '-std=c17', '-funsigned-char', '-O' + level, '-g0', '-c', root / 'kernels.c', '-o', output], root, records)
    optimization_same = text_sections(root / 'buster-O0.obj')['text_sha256'] == text_sections(root / 'buster-O3.obj')['text_sha256']
    run(['cl', '/nologo', '/TC', '/std:c17', '/O2', '/J', '/c', root / 'caller.c', '/Fo' + str(root / 'caller.obj')], root, records)
    for variant in variants:
        run(['cl', '/nologo', root / 'caller.obj', root / ('kernels-' + variant + '-7.obj'), '/Fe' + str(root / (variant + '.exe'))], root, records)
    runtime_rows = []
    for kind in range(4):
        for trial in range(6):
            order = variants if trial % 2 == 0 else list(reversed(variants))
            for variant in order:
                record, output = run([root / (variant + '.exe'), str(kind), '32768'], root, records)
                match = re.search(r'^RESULT ticks=(\d+) frequency=(\d+) checksum=(\d+)$', output, re.M)
                if not match:
                    raise RuntimeError('missing QPC result')
                ticks, frequency, checksum = map(int, match.groups())
                if not ticks or not frequency:
                    raise RuntimeError('invalid QPC result')
                runtime_rows.append({'kind': ['sum_scale', 'divide7', 'dot', 'calls'][kind], 'variant': variant,
                                     'trial': trial, 'warmup': trial == 0, 'runtime_ns': ticks * 1e9 / frequency,
                                     'checksum': checksum, 'process': record['sequence'], 'ticks': ticks, 'frequency': frequency})
                (root / 'runtime.json').write_text(json.dumps(runtime_rows, indent=2), encoding='utf-8')
        if len({r['checksum'] for r in runtime_rows if r['kind'] == ['sum_scale', 'divide7', 'dot', 'calls'][kind]}) != 1:
            raise RuntimeError('runtime checksums disagree')
    run(['cl', '/nologo', '/TC', '/std:c17', '/J', '/O2', '/DNEGATIVE_CONTROL=1', '/c', root / 'kernels.c', '/Fo' + str(root / 'negative.obj')], root, records)
    run(['cl', '/nologo', root / 'caller.obj', root / 'negative.obj', '/Fe' + str(root / 'negative.exe')], root, records)
    negative, output = run([root / 'negative.exe', '0', '1'], root, records, required=False)
    if negative['exit_code'] != 3 or 'MISMATCH kind=0' not in output or negative['timeout']:
        raise RuntimeError('semantic negative control did not fail as expected')
    summary = {'identity': identity, 'compile': [], 'runtime': [], 'negative_control': 'rejected with observed value mismatch',
               'buster_O0_O3_text_identical': optimization_same, 'validation': '8 small-value checks before each runtime observation, cross-variant checksum equality, stable per-variant text hashes'}
    for name in sources:
        for variant in variants:
            rows = [r for r in compile_rows if r['workload'] == name and r['variant'] == variant and not r['warmup']]
            times = [r['wall_ns'] / 1e6 for r in rows]
            summary['compile'].append({'workload': name, 'variant': variant, 'source_bytes': rows[0]['source_bytes'],
                                       'median_ms': statistics.median(times), 'min_ms': min(times), 'max_ms': max(times),
                                       'text_section_bytes': rows[0]['text_section_bytes'], 'object_bytes': rows[0]['object_bytes']})
    for kind in ['sum_scale', 'divide7', 'dot', 'calls']:
        for variant in variants:
            times = [r['runtime_ns'] / 1e6 for r in runtime_rows if r['kind'] == kind and r['variant'] == variant and not r['warmup']]
            summary['runtime'].append({'kind': kind, 'variant': variant, 'median_ms': statistics.median(times), 'min_ms': min(times), 'max_ms': max(times)})
    (root / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    (root / 'sha256.json').write_text(json.dumps({p.name: sha(p) for p in sorted(root.iterdir()) if p.is_file()}, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2))

if __name__ == '__main__':
    main()
