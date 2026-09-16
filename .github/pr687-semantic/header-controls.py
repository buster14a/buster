import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

compiler = Path(sys.argv[1]).resolve()
headers = Path(sys.argv[2]).resolve()
out = Path(sys.argv[3]).resolve()
out.mkdir(parents=True, exist_ok=True)
expected = 'ea3ee83a8c95a6feba588d3d5de78f6dc24adbebd043f593efb974e93778b4e3'
actual = hashlib.sha256((headers / 'avx512bf16intrin.h').read_bytes()).hexdigest()
assert actual == expected, actual
(out / 'header-sha256.txt').write_text(actual + '\n')
proof = '#include <immintrin.h>\n_Static_assert(sizeof(__m512bh) == 64, "BF16 header reached");\n__bf16 header_proof = 1.5;\n'
cases = {
    'positive': proof,
    'arity': '#define __builtin_ia32_cvtsbf162ss_32(a) __builtin_ia32_cvtsbf162ss_32(a, a)\n' + proof,
    'type': '#define __builtin_ia32_cvtsbf162ss_32(a) __builtin_ia32_cvtsbf162ss_32((void*)0)\n' + proof,
}
features = ['__MMX__', '__SSE__', '__SSE2__', '__SSE3__', '__SSSE3__', '__SSE4_1__', '__SSE4_2__', '__AVX__', '__AVX2__', '__AVX512F__', '__AVX512BW__', '__AVX512VL__', '__AVX512BF16__', '__AVXNECONVERT__']
# A freestanding resource-header check needs no foreign SDK. Explicit ISA
# prerequisites ensure MSVC's feature guards cannot omit the tested header.
flags = ['-D__STDC_HOSTED__=0'] + ['-D' + feature + '=1' for feature in features]
records = []
for case, source in cases.items():
    src = out / (case + '.c')
    src.write_text(source)
    for target in ['x86_64-unknown-linux-gnu', 'x86_64-pc-windows-msvc', 'x86_64-apple-macos']:
        for form in ['ssa', 'memory', 'clang']:
            stem = case + '-' + target + '-' + form
            argv = ['clang'] if form == 'clang' else [str(compiler), 'cc']
            argv += ['-c', str(src), '-I' + str(headers), '--target=' + target, '-o', str(out / (stem + '.o'))] + flags
            if form == 'memory':
                argv.append('-fno-frontend-ssa')
            run = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
            log = run.stdout.decode('utf-8', errors='replace')
            (out / (stem + '.log')).write_text(log)
            okay = run.returncode == 0 if case == 'positive' else run.returncode != 0 and 'cvtsbf162ss_32' in log and 'argument' in log
            records.append({'case': case, 'target': target, 'form': form, 'status': run.returncode, 'pass': okay, 'argv': argv})
            print(stem, run.returncode, 'PASS' if okay else 'FAIL', flush=True)
(out / 'results.json').write_text(json.dumps(records, indent=2))
assert all(row['pass'] for row in records), [row for row in records if not row['pass']]
assert hashlib.sha256((headers / 'avx512bf16intrin.h').read_bytes()).hexdigest() == expected
