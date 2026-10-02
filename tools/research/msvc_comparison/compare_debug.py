"""Issue #2277: matched CodeView debug/debug and separate optimization gap.

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
        unsigned lengths[9] = {0u, 1u, 3u, 4u, 7u, 16u, 31u, 257u, 1024u};
        unsigned salts[6] = {0u, 1u, 7u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
        for (unsigned pattern = 0; pattern < 2u; pattern += 1)
        {
            for (unsigned i = 0; i < 1024u; i += 1)
            {
                a[i] = (i * 1664525u + 1013904223u) & (pattern == 0u ? 1023u : 0xffffffffu);
                b[i] = (i * 1103515245u + 12345u) & (pattern == 0u ? 2047u : 0xffffffffu);
            }
            for (unsigned length_index = 0; length_index < 9u; length_index += 1)
            {
                for (unsigned salt_index = 0; salt_index < 6u; salt_index += 1)
                {
                    unsigned actual = function(a, b, lengths[length_index], salts[salt_index]);
                    unsigned expected = reference(a, b, lengths[length_index], salts[salt_index], kind);
                    if (actual != expected) { printf("MISMATCH kind=%u n=%u salt=%u actual=%u expected=%u\n", kind, lengths[length_index], salts[salt_index], actual, expected); result = 3; }
                }
            }
        }
        for (unsigned i = 0; i < 1024u; i += 1)
        {
            a[i] = (i * 1664525u + 1013904223u) & 1023u;
            b[i] = (i * 1103515245u + 12345u) & 2047u;
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
    sections = []
    debug_bytes = 0
    unwind_bytes = 0
    for index in range(count):
        offset = 20 + index * 40
        name = data[offset:offset + 8].split(b'\0')[0]
        size, start = struct.unpack_from('<II', data, offset + 16)
        if start + size > len(data):
            raise ValueError('COFF section outside file')
        if name.startswith(b'.text'):
            text.extend(data[start:start + size])
        if name.startswith(b'.debug'):
            debug_bytes += size
        if name in (b'.pdata', b'.xdata'):
            unwind_bytes += size
        sections.append({'name': name.decode('ascii', errors='replace'), 'raw_bytes': size})
    if not text:
        raise ValueError('missing text sections')
    return {'text_section_bytes': len(text), 'text_sha256': hashlib.sha256(text).hexdigest(), 'debug_section_bytes': debug_bytes,
            'unwind_section_bytes': unwind_bytes, 'object_bytes': len(data), 'sections': sections}

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
    identity = {'schema': 'BUSTER_MSVC_DEBUG_DIAGNOSTIC_V2', 'buster_source': os.environ['BUSTER_COMPARISON_BASE'],
                'buster_binary_sha256': sha(ide), 'platform': platform.platform(), 'cpu': platform.processor(),
                'logical_cpus': os.cpu_count(), 'image_version': os.environ.get('ImageVersion'),
                'image_os': os.environ.get('ImageOS'), 'runner_name': os.environ.get('RUNNER_NAME'),
                'cl_path': shutil.which('cl'), 'cl_sha256': sha(shutil.which('cl')),
                'workflow_sha': os.environ.get('GITHUB_SHA'), 'qualified_performance_acceptance': False,
                'producer_configuration': 'trusted Clang-built Release; distinct from subject-program Debug',
                'primary_comparison': 'buster-debug versus msvc-debug, both with embedded CodeView',
                'secondary_gap': 'same buster-debug versus msvc-optimized-debug, both with embedded CodeView',
                'sensitivity_control': 'buster-transforms-off disables optional transforms without changing backend',
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
    variants = ['buster-debug', 'buster-transforms-off', 'msvc-debug', 'msvc-optimized-debug']

    def command(variant, source, output):
        if variant.startswith('buster'):
            result = [ide, 'cc', '-target', 'x86_64-pc-windows-msvc', '-march=baseline', '-std=c17', '-funsigned-char', '-O0',
                      '-fregister-allocator=fast', '-fno-machine-fallback', '-g', '-c', source, '-o', output]
            if variant == 'buster-transforms-off':
                result.extend(['-fno-canonical-fast', '-fno-frontend-ssa', '-fno-canonical-local-promotion', '-fno-target-local-promotion'])
        else:
            options = ['/Od', '/Ob0'] if variant == 'msvc-debug' else ['/O2', '/Ob2']
            result = ['cl', '/nologo', '/TC', '/std:c17', '/Zc:preprocessor', '/J', '/arch:SSE2', '/GS-', '/Z7', *options, '/c', source, '/Fo' + str(output)]
        return result

    compile_rows = []
    # Williams balanced orders: each measured compiler occupies each position twice.
    schedules = [(0, 1, 3, 2), (1, 2, 0, 3), (2, 3, 1, 0), (3, 0, 2, 1)]
    measured_trials = 8
    for name in sources:
        for trial in range(measured_trials + 1):
            order = variants if trial == 0 else [variants[i] for i in schedules[(trial - 1) % 4]]
            for variant in order:
                output = root / ('%s-%s-%u.obj' % (name, variant, trial))
                record, _ = run(command(variant, root / (name + '.c'), output), root, records)
                info = text_sections(output)
                section_names = {s['name'] for s in info['sections']}
                if not info['debug_section_bytes'] or not {'.debug$S', '.debug$T'} <= section_names:
                    raise RuntimeError('requested CodeView debug sections missing: %s %s' % (name, variant))
                compile_rows.append({'workload': name, 'variant': variant, 'trial': trial, 'warmup': trial == 0,
                                     'source_bytes': (root / (name + '.c')).stat().st_size, 'source_sha256': sha(root / (name + '.c')),
                                     'process': record['sequence'], 'wall_ns': record['wall_ns'], **info})
                (root / 'compile.json').write_text(json.dumps(compile_rows, indent=2), encoding='utf-8')
    for name in sources:
        for variant in variants:
            hashes = {r['text_sha256'] for r in compile_rows if r['workload'] == name and r['variant'] == variant}
            if len(hashes) != 1:
                raise RuntimeError('unstable text bytes: %s %s' % (name, variant))
    (root / 'effective-options.json').write_text(json.dumps({v: [str(x) for x in command(v, 'SOURCE', 'OUTPUT')] for v in variants}, indent=2), encoding='utf-8')
    # Independent binary consumer receipts are outside every timing boundary.
    for variant in variants:
        run(['llvm-objdump', '-dr', root / ('kernels-' + variant + '-8.obj')], root, records)
        run(['llvm-readobj', '--codeview', root / ('kernels-' + variant + '-8.obj')], root, records)
    run(['cl', '/nologo', '/TC', '/std:c17', '/O2', '/J', '/c', root / 'caller.c', '/Fo' + str(root / 'caller.obj')], root, records)
    linked_artifacts = []
    for variant in variants:
        run(['cl', '/nologo', root / 'caller.obj', root / ('kernels-' + variant + '-8.obj'), '/Fe' + str(root / (variant + '.exe')), '/link', '/DEBUG:FULL', '/INCREMENTAL:NO', '/OPT:NOREF', '/OPT:NOICF', '/PDB:' + str(root / (variant + '.pdb'))], root, records)
        linked_artifacts.append({'variant': variant, 'executable_bytes': (root / (variant + '.exe')).stat().st_size,
                                 'pdb_bytes': (root / (variant + '.pdb')).stat().st_size,
                                 'executable_sha256': sha(root / (variant + '.exe')), 'pdb_sha256': sha(root / (variant + '.pdb'))})
    runtime_rows = []
    for kind in range(4):
        for trial in range(measured_trials + 1):
            order = variants if trial == 0 else [variants[i] for i in schedules[(trial - 1) % 4]]
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
    summary = {'identity': identity, 'compile': [], 'runtime': [], 'linked_artifacts': linked_artifacts,
               'measured_trials_per_cell': measured_trials, 'ordering': 'Williams balanced blocks, two complete four-order blocks',
               'negative_control': 'rejected with observed value mismatch',
               'validation': '108 unsigned edge/length checks before each runtime observation, cross-variant checksum equality, stable per-variant text hashes, CodeView sections present, llvm-readobj decoding; debugger usability not executed'}
    for name in sources:
        for variant in variants:
            rows = [r for r in compile_rows if r['workload'] == name and r['variant'] == variant and not r['warmup']]
            times = [r['wall_ns'] / 1e6 for r in rows]
            summary['compile'].append({'workload': name, 'variant': variant, 'source_bytes': rows[0]['source_bytes'],
                                       'median_ms': statistics.median(times), 'min_ms': min(times), 'max_ms': max(times),
                                       'text_section_bytes': rows[0]['text_section_bytes'], 'debug_section_bytes': rows[0]['debug_section_bytes'],
                                       'unwind_section_bytes': rows[0]['unwind_section_bytes'], 'object_bytes': rows[0]['object_bytes']})
    for kind in ['sum_scale', 'divide7', 'dot', 'calls']:
        for variant in variants:
            times = [r['runtime_ns'] / 1e6 for r in runtime_rows if r['kind'] == kind and r['variant'] == variant and not r['warmup']]
            summary['runtime'].append({'kind': kind, 'variant': variant, 'median_ms': statistics.median(times), 'min_ms': min(times), 'max_ms': max(times)})
    (root / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    (root / 'sha256.json').write_text(json.dumps({p.name: sha(p) for p in sorted(root.iterdir()) if p.is_file()}, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2))

if __name__ == '__main__':
    main()
