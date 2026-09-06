"""Opt-in Linux AVX-512 kernel differential and timing harness.

The canonical Debug non-unity build supplies the production lexer objects;
no compiler build policy or dependencies are replicated here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--out', type=Path, required=True, help='Private output directory; corpus and binaries stay here.')
parser.add_argument('--clang', default='clang')
parser.add_argument('--time', action='store_true', help='Run seven alternating scalar/SIMD pairs after correctness passes.')
parser.add_argument('--regimes', action='store_true', help='Also time small/dense synthetic populations.')
parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
args = parser.parse_args()
repo = args.repo.resolve()
out = args.out.resolve()
source = Path(__file__).resolve().parent
out.mkdir(parents=True, exist_ok=True)
if platform.system() != 'Linux' or platform.machine() != 'x86_64':
    parser.error('The opt-in harness requires Linux x86-64.')
if args.cpu not in os.sched_getaffinity(0):
    parser.error('Requested CPU is outside this process affinity mask.')
if args.regimes and not args.time:
    parser.error('--regimes also requires --time.')

def run(command, log=None):
    if log:
        with log.open('w') as stream:
            subprocess.run(command, check=True, stdout=stream, stderr=subprocess.STDOUT)
    else:
        subprocess.run(command, check=True)

macros = subprocess.check_output([args.clang, '-march=native', '-dM', '-E', '-x', 'c', '/dev/null'], text=True)
for feature in ('__AVX512F__', '__AVX512BW__', '__AVX512VBMI__', '__AVX512VBMI2__'):
    if feature not in macros:
        parser.error('Host compiler does not expose ' + feature + ' with -march=native.')

object_root = repo / 'build/CMakeFiles/ide.dir/Debug/src/buster/lib'
objects = sorted(path for path in object_root.rglob('*.o') if path.name != 'entry_point.c.o')
if not (object_root / 'compiler/frontend/c/c_source.c.o').exists():
    parser.error('First build Debug non-unity ide through ./build.sh; production lexer objects are required.')
flags = ['-std=gnu17', '-O3', '-g', '-march=native', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function']
run([args.clang, *flags, '-DBUSTER_UNITY_BUILD=0', '-DBUSTER_INCLUDE_TESTS=1', '-I' + str(repo / 'src'), '-I' + str(repo / 'build/generated'), str(source / 'dump_corpus.c'), *map(str, objects), '-lm', '-o', str(out / 'dump_corpus')], out / 'dump-build.log')
run([sys.executable, str(source / 'prepare.py'), '--repo', str(repo), '--out', str(out), '--buster-lexer', str(out / 'dump_corpus'), '--all-library', '--clang', args.clang], out / 'prepare.log')
command = [args.clang, *flags, '-I' + str(repo / 'src'), '-I' + str(out), str(source / 'benchmark.c'), '-o', str(out / 'benchmark')]
run(command, out / 'benchmark-build.log')
run([str(out / 'benchmark'), str(out / 'corpus.bin'), 'check'], out / 'check.log')
print((out / 'check.log').read_text().strip())
symbols = ['buster_x86_metadata_decode_base64_chunk_avx512', 'c_ir_decode_quoted', 'c_symbols_intern_tokens']
for symbol in symbols:
    run(['objdump', '-d', '-Mintel', '--disassemble=' + symbol, str(out / 'benchmark')], out / (symbol + '.asm'))

provenance = dict(source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(), compiler=subprocess.check_output([args.clang, '--version'], text=True), compile_command=command, cpu=args.cpu, cpuinfo=Path('/proc/cpuinfo').read_text().split('\n\n')[0], binary_sha256=hashlib.sha256((out / 'benchmark').read_bytes()).hexdigest(), dump_binary_sha256=hashlib.sha256((out / 'dump_corpus').read_bytes()).hexdigest(), corpus_sha256=hashlib.sha256((out / 'corpus.bin').read_bytes()).hexdigest())
(out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
logs = []
if args.time:
    log = out / 'timing.log'
    run(['taskset', '-c', str(args.cpu), str(out / 'benchmark'), str(out / 'corpus.bin'), 'timing'], log)
    logs.append(log)
if args.regimes:
    regimes = out / 'regimes'
    run([sys.executable, str(source / 'regimes.py'), '--out', str(regimes)])
    for corpus in sorted(regimes.glob('*.bin')):
        run([str(out / 'benchmark'), str(corpus), 'check'], corpus.with_suffix('.check.log'))
        log = corpus.with_suffix('.log')
        run(['taskset', '-c', str(args.cpu), str(out / 'benchmark'), str(corpus), 'timing'], log)
        logs.append(log)

results = []
for log in logs:
    kernels = {}
    sums = {}
    for line in log.read_text().splitlines():
        match = re.fullmatch(r'TIMING (\w+) pair=(\d+) vector=(\d+) ns=(\d+) units=(\d+) sum=(\d+)', line)
        if match:
            kernel, pair, vector, ns, units, checksum = match.groups()
            if int(units):
                kernels.setdefault(kernel, {}).setdefault(int(vector), []).append(int(ns) / int(units))
                sums.setdefault(kernel, set()).add(int(checksum))
    if not kernels:
        raise RuntimeError('Empty timing output: ' + str(log))
    for kernel, versions in kernels.items():
        if set(versions) != {0, 1} or len(sums[kernel]) != 1 or any(len(values) != 7 for values in versions.values()):
            raise RuntimeError('Incomplete or inconsistent timing output: ' + str(log))
        scalar, vector = (statistics.median(versions[variant]) for variant in (0, 1))
        row = dict(corpus=log.stem, kernel=kernel, scalar_ns_per_unit=scalar, vector_ns_per_unit=vector, scalar_over_vector=scalar/vector, scalar_min=min(versions[0]), scalar_max=max(versions[0]), vector_min=min(versions[1]), vector_max=max(versions[1]))
        results.append(row)
        print(f'{log.stem}: {kernel}: scalar={scalar:.5f}, SIMD={vector:.5f} ns/unit, ratio={scalar/vector:.3f}x')
if results:
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
