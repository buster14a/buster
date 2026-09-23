#!/usr/bin/env python3
"""Branch-only M3 diagnostic: no timing or production policy changes.

Extract the actual scalar reference, compare bounded reduction kernels, and
instrument only the numeric asm-label count query in an isolated hosted tree.
All production builds remain build.c-owned. Do not use this on a live tree.
"""
from __future__ import annotations
import collections
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

SOURCE = Path('src/buster/lib/compiler/ir/ir.c')
SYMBOL = 'ir_inline_assembly_label_operand_base'
BASE = 'bb96b5d08e570aff3cbd473441d71ab0dc691d67'
OUT = Path(os.environ['M3_EVIDENCE'])
OUT.mkdir(parents=True, exist_ok=True)


def run(argv: list[str], name: str) -> None:
    with (OUT / 'commands.jsonl').open('a') as ledger:
        ledger.write(json.dumps({'argv': argv, 'log': name}) + '\n')
    print('+ ' + shlex.join(argv), flush=True)
    with (OUT / name).open('wb') as log:
        result = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT, check=False)
    print((OUT / name).read_text(errors='replace'), flush=True)
    with (OUT / 'commands.jsonl').open('a') as ledger:
        ledger.write(json.dumps({'argv': argv, 'exit': result.returncode}) + '\n')
    if result.returncode:
        raise SystemExit(result.returncode)


def function_text(text: str) -> str:
    start = text.index('u32 ' + SYMBOL + '(IrInstruction* instruction)\n{')
    end = text.index('\nbool ir_inline_assembly_jump_target(', start)
    result = text[start:end].rstrip()
    if result.count('return result > UINT32_MAX ? UINT32_MAX : (u32)result;') != 1:
        raise RuntimeError('source precondition changed')
    return result


KERNELS = r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <immintrin.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct IrInstruction IrInstruction;
struct IrInstruction { u32 operand_count; u32 immediate_count; u64 *immediates; };
#define IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT ((u64)1 << 8)
#define IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE ((u64)1 << 9)
#define NOINLINE __attribute__((noinline))
#define T256 __attribute__((noinline, target("avx2,popcnt")))
#define T512 __attribute__((noinline, target("avx512f,popcnt")))
/* ACTUAL_REFERENCE */

NOINLINE u64 scalar_bits(u64 const *x, size_t n)
{
    u64 count = 0;
#pragma clang loop vectorize(disable) interleave(disable)
    for (size_t i = 0; i < n; i += 1)
    {
        count += (x[i] >> 8) & (x[i] >> 9) & 1;
    }
    return count;
}

NOINLINE u64 auto_native(u64 const *x, size_t n)
{
    u64 count = 0;
    for (size_t i = 0; i < n; i += 1)
    {
        count += (x[i] & 768) == 768;
    }
    return count;
}

T256 u64 auto256(u64 const *x, size_t n)
{
    u64 count = 0;
    for (size_t i = 0; i < n; i += 1)
    {
        count += (x[i] & 768) == 768;
    }
    return count;
}

T512 u64 auto512(u64 const *x, size_t n)
{
    u64 count = 0;
    for (size_t i = 0; i < n; i += 1)
    {
        count += (x[i] & 768) == 768;
    }
    return count;
}

T256 u64 mask256(u64 const *x, size_t n)
{
    u64 count = 0;
    size_t i = 0;
    __m256i bits = _mm256_set1_epi64x(768);
    for (; n - i >= 4; i += 4)
    {
        __m256i values = _mm256_loadu_si256((__m256i const *)(x + i));
        __m256i equal = _mm256_cmpeq_epi64(_mm256_and_si256(values, bits), bits);
        unsigned mask = (unsigned)_mm256_movemask_pd(_mm256_castsi256_pd(equal));
        count += (unsigned)__builtin_popcount(mask);
    }
    for (; i < n; i += 1)
    {
        count += (x[i] & 768) == 768;
    }
    return count;
}

T512 u64 mask512(u64 const *x, size_t n)
{
    u64 count = 0;
    size_t i = 0;
    __m512i bits = _mm512_set1_epi64(768);
    for (; n - i >= 8; i += 8)
    {
        __m512i values = _mm512_loadu_si512(x + i);
        unsigned mask = _mm512_cmpeq_epi64_mask(_mm512_and_si512(values, bits), bits);
        count += (unsigned)__builtin_popcount(mask);
    }
    for (; i < n; i += 1)
    {
        count += (x[i] & 768) == 768;
    }
    return count;
}

T512 u64 mask512_four(u64 const *x, size_t n)
{
    u64 count = 0;
    size_t i = 0;
    __m512i bits = _mm512_set1_epi64(768);
    for (; n - i >= 32; i += 32)
    {
        __m512i a = _mm512_loadu_si512(x + i);
        __m512i b = _mm512_loadu_si512(x + i + 8);
        __m512i c = _mm512_loadu_si512(x + i + 16);
        __m512i d = _mm512_loadu_si512(x + i + 24);
        unsigned ma = _mm512_cmpeq_epi64_mask(_mm512_and_si512(a, bits), bits);
        unsigned mb = _mm512_cmpeq_epi64_mask(_mm512_and_si512(b, bits), bits);
        unsigned mc = _mm512_cmpeq_epi64_mask(_mm512_and_si512(c, bits), bits);
        unsigned md = _mm512_cmpeq_epi64_mask(_mm512_and_si512(d, bits), bits);
        count += (unsigned)__builtin_popcount(ma | (mb << 8) | (mc << 16) | (md << 24));
    }
    for (; i < n; i += 1)
    {
        count += (x[i] & 768) == 768;
    }
    return count;
}

NOINLINE u64 oracle(u64 const *x, size_t n)
{
    u64 count = 0;
#pragma clang loop vectorize(disable) interleave(disable)
    for (size_t i = 0; i < n; i += 1)
    {
        if ((x[i] & 256) != 0)
        {
            if ((x[i] & 512) != 0)
            {
                count += 1;
            }
        }
    }
    return count;
}

int main(void)
{
    int failed = 0;
    int has256 = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("popcnt");
    int has512 = __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("popcnt");
    long page_query = sysconf(_SC_PAGESIZE);
    size_t page = page_query > 0 ? (size_t)page_query : 0;
    unsigned char *storage = page ? mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    u64 cases = 0;
    if (storage == MAP_FAILED || page < 4096 || mprotect(storage + page, page, PROT_NONE) != 0)
    {
        failed = 1;
    }
    else
    {
        IrInstruction empty = {0};
        IrInstruction invalid = {1, 0, 0};
        IrInstruction missing = {1, 1, 0};
        failed |= ir_inline_assembly_label_operand_base(0) != UINT32_MAX;
        failed |= ir_inline_assembly_label_operand_base(&invalid) != UINT32_MAX;
        failed |= ir_inline_assembly_label_operand_base(&missing) != UINT32_MAX;
        failed |= ir_inline_assembly_label_operand_base(&empty) != 0;
        for (size_t n = 0; n <= 257; n += 1)
        {
            /* Every array ends exactly on a PROT_NONE page; zero-length is not readable. */
            u64 *x = (u64 *)(storage + page - n * sizeof(u64));
            for (unsigned pattern = 0; pattern < 12; pattern += 1)
            {
                for (size_t i = 0; i < n; i += 1)
                {
                    unsigned cls = pattern < 4 ? pattern : pattern == 4 ? (unsigned)(i & 3) :
                        pattern == 5 ? (i == 0 ? 3u : 0u) : pattern == 6 ? (i + 1 == n ? 3u : 0u) :
                        (unsigned)((i * 1103515245u + pattern * 12345u) >> (pattern - 7)) & 3;
                    x[i] = ((UINT64_C(0x9e3779b97f4a7c15) * (i + pattern)) & ~UINT64_C(768)) | ((u64)cls << 8);
                }
                u64 expected = oracle(x, n);
                IrInstruction instruction = {(u32)n, (u32)n, x};
                failed |= ir_inline_assembly_label_operand_base(&instruction) != n + expected;
                failed |= scalar_bits(x, n) != expected;
                failed |= auto_native(x, n) != expected;
                if (has256)
                {
                    failed |= auto256(x, n) != expected;
                    failed |= mask256(x, n) != expected;
                }
                if (has512)
                {
                    failed |= auto512(x, n) != expected;
                    failed |= mask512(x, n) != expected;
                    failed |= mask512_four(x, n) != expected;
                }
                cases += 1;
            }
        }
        failed |= munmap(storage, 2 * page) != 0;
    }
    printf("M3_KERNEL cases=%" PRIu64 " descriptor_controls=4 avx2=%s avx512=%s failed=%d\n", cases,
           has256 ? "executed" : "NOT_EXECUTED", has512 ? "executed" : "NOT_EXECUTED", failed);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
'''


def kernels() -> None:
    text = SOURCE.read_text()
    reference = function_text(text)
    (OUT / 'reference.c').write_text(reference + '\n')
    source = OUT / 'kernels.c'
    source.write_text(KERNELS.replace('/* ACTUAL_REFERENCE */', reference))
    run(['clang', '--version'], 'clang.txt')
    run(['lscpu'], 'lscpu.txt')
    flags = ['-std=c11', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-Wall', '-Wextra', '-Werror']
    # Every target variant is emitted; execution is guarded by the hosting CPU.
    for producer, extra in [('generic', ['-march=x86-64']), ('native', ['-march=native'])]:
        binary = OUT / ('kernels-' + producer)
        run(['clang', '-O3', *flags, *extra, str(source), '-o', str(binary)], producer + '-build.log')
        run([str(binary)], producer + '-correctness.log')
        run(['objdump', '-d', '-Mintel', str(binary)], producer + '.asm')
    binary = OUT / 'kernels-sanitize'
    run(['clang', '-O1', *flags, '-march=x86-64', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(source), '-o', str(binary)], 'sanitize-build.log')
    run([str(binary)], 'sanitize-correctness.log')
    (OUT / 'kernel-identity.json').write_text(json.dumps({'base': BASE, 'source_sha256': hashlib.sha256(text.encode()).hexdigest(),
        'reference_sha256': hashlib.sha256(reference.encode()).hexdigest(), 'kernels_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'timing': 'NOT_RUN', 'prototype_revision': 1}, indent=2) + '\n')


def instrument() -> None:
    before = SOURCE.read_text()
    reference = function_text(before)
    changed = reference.replace('    u64 result = instruction->operand_count;', '    u64 m3_classes[4] = {0};\n    u64 result = instruction->operand_count;')
    changed = changed.replace('        u64 constraint = instruction->immediates[index];', '        u64 constraint = instruction->immediates[index];\n        m3_classes[(constraint >> 8) & 3] += 1;')
    changed = changed.replace('    return result > UINT32_MAX ? UINT32_MAX : (u32)result;',
        '    string_print(S8("M3_COUNT n={u32} k={u64} c0={u64} c1={u64} c2={u64} c3={u64}\\n"), instruction->operand_count, result - instruction->operand_count, m3_classes[0], m3_classes[1], m3_classes[2], m3_classes[3]);\n    return result > UINT32_MAX ? UINT32_MAX : (u32)result;')
    if changed == reference or 'M3_COUNT' not in changed or (OUT / 'ir.c.before').exists():
        raise RuntimeError('instrumentation precondition failed')
    after = before.replace(reference, changed, 1)
    (OUT / 'ir.c.before').write_text(before)
    (OUT / 'instrumentation.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True), fromfile=str(SOURCE), tofile=str(SOURCE))))
    SOURCE.write_text(after)
    (OUT / 'instrumentation-sha256.txt').write_text(hashlib.sha256(after.encode()).hexdigest() + '\n')


def restore() -> None:
    SOURCE.write_bytes((OUT / 'ir.c.before').read_bytes())
    run(['git', 'diff', '--exit-code', '--', str(SOURCE)], 'restored-source.log')


def summarize() -> None:
    workloads = {}
    for path in sorted(OUT.glob('workload-*.log')):
        rows = [tuple(map(int, match)) for match in re.findall(r'M3_COUNT n=(\d+) k=(\d+) c0=(\d+) c1=(\d+) c2=(\d+) c3=(\d+)', path.read_text(errors='replace'))]
        if any(sum(row[2:]) != row[0] or row[1] != row[5] for row in rows):
            raise RuntimeError('census conservation failure')
        status_path = path.with_suffix('.status')
        workloads[path.stem] = {'status': status_path.read_text().strip() if status_path.exists() else 'MISSING', 'queries': len(rows),
            'records': sum(row[0] for row in rows), 'selected': sum(row[1] for row in rows),
            'max_length': max((row[0] for row in rows), default=0),
            'length_count_histogram': dict(collections.Counter(f'{row[0]},{row[1]}' for row in rows)),
            'classes_neither_output_rw_both': [sum(row[i] for row in rows) for i in range(2, 6)]}
    result = {'base': BASE, 'workloads': workloads, 'timings': 'NOT_RUN; instrumented census only', 'benchmark_cohorts': 0}
    (OUT / 'census.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: m3_mask_probe.py kernels|instrument|restore|summarize')
    mode = sys.argv[1]
    if mode == 'kernels':
        kernels()
    elif mode == 'instrument':
        instrument()
    elif mode == 'restore':
        restore()
    elif mode == 'summarize':
        summarize()
    else:
        raise SystemExit('unknown mode')
