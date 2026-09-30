#!/usr/bin/env python3
"""Differential corpus: compile every tests/*.c fixture with a baseline and a
candidate compiler under several configurations; compare exit status, stderr
and output SHA-256. usage: corpus_diff.py <baseline> <candidate> <out-dir> [jobs]"""
import hashlib, os, subprocess, sys, concurrent.futures as cf
base, cand, out = sys.argv[1], sys.argv[2], sys.argv[3]
jobs = int(sys.argv[4]) if len(sys.argv) > 4 else 4
# The pinned main checkout whose tests/*.c fixtures are compiled.
root = os.environ.get('CORPUS_ROOT', os.getcwd())
configs = {
    'x64-elf-g0': ['-target', 'x86_64-unknown-linux-gnu', '-g0', '-c'],
    'x64-elf-g': ['-target', 'x86_64-unknown-linux-gnu', '-g', '-c'],
    'x64-elf-g-nossa': ['-target', 'x86_64-unknown-linux-gnu', '-g', '-fno-frontend-ssa', '-c'],
    'a64-elf-g': ['-target', 'aarch64-unknown-linux-gnu', '-g', '-c'],
    'x64-coff-g': ['-target', 'x86_64-windows', '-g', '-c'],
    'a64-macho-g': ['-target', 'aarch64-apple-darwin', '-g', '-c'],
    'wasm64': ['-target', 'wasm64', '-c'],
    'x64-llvm': ['-target', 'x86_64-unknown-linux-gnu', '-emit-llvm', '-c'],
}
os.makedirs(out, exist_ok=True)
fixtures = sorted(f for f in os.listdir(os.path.join(root, 'tests')) if f.endswith('.c'))
def run(compiler, tag, config, fixture):
    o = os.path.join(out, f'{tag}-{config}-{fixture}.o')
    try:
        os.remove(o)
    except FileNotFoundError:
        pass
    p = subprocess.run([compiler, 'cc'] + configs[config] + [os.path.join('tests', fixture), '-o', o], cwd=root, capture_output=True, timeout=600)
    digest = hashlib.sha256(open(o, 'rb').read()).hexdigest() if os.path.exists(o) else '-'
    if os.path.exists(o):
        os.remove(o)
    return p.returncode, p.stderr.replace(base.encode(), b'<cc>').replace(cand.encode(), b'<cc>'), digest
def compare(item):
    config, fixture = item
    a = run(base, 'a', config, fixture)
    b = run(cand, 'b', config, fixture)
    return config, fixture, a, b
items = [(c, f) for c in configs for f in fixtures]
same = differ = produced = 0
with cf.ThreadPoolExecutor(jobs) as pool:
    for config, fixture, a, b in pool.map(compare, items):
        if a == b:
            same += 1
            produced += a[2] != '-'
        else:
            differ += 1
            print(f'DIFF {config} {fixture}: base rc={a[0]} sha={a[2][:12]} | cand rc={b[0]} sha={b[2][:12]}')
            if a[1] != b[1]:
                print('  stderr base:', a[1][:300])
                print('  stderr cand:', b[1][:300])
print(f'SUMMARY compiles={len(items)*2} pairs={len(items)} identical={same} differ={differ} outputs={produced}')
