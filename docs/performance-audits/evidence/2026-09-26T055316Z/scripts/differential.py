#!/usr/bin/env python3
# Disposable correctness differential: same input, same flags, every variant.
# Compares exit status, stderr text and artifact bytes against the base variant.
import hashlib, json, os, subprocess, sys, glob
from concurrent.futures import ThreadPoolExecutor
S = os.path.dirname(os.path.abspath(__file__))
REPO = '/home/user/buster'
cfg = sys.argv[1]
variants = os.environ.get('VARIANTS', 'base,a,b,ab').split(',')
options = {
    'g': ['-g'], 'g0': ['-g0'], 'nossa': ['-g', '-fno-frontend-ssa'], 'quality': ['-g', '-fregister-allocator=quality'],
    'noalloc': ['-g', '-fno-register-allocator'], 'a64': ['-g', '-target', 'aarch64-linux'], 'win': ['-g', '-target', 'x86_64-windows'],
    'nofast': ['-g', '-fno-canonical-fast'], 'nopromote': ['-g', '-fno-canonical-local-promotion'],
}
if len(sys.argv) > 2:
    options = {k: v for k, v in options.items() if k in sys.argv[2].split(',')}
inputs = sorted(glob.glob(os.path.join(REPO, 'tests', '*.c')))
work = os.path.join(S, 'diff-' + cfg)
os.makedirs(work, exist_ok=True)

def one(task):
    path, key = task
    name = os.path.basename(path)[:-2]
    row = {'input': os.path.relpath(path, REPO), 'options': key}
    for v in variants:
        out = os.path.join(work, f'{name}.{key}.{v}.o')
        p = subprocess.run([os.path.join(S, 'vbin', f'ide-{v}-{cfg}-native'), 'cc', '-c'] + options[key] + [os.path.relpath(path, REPO), '-o', out],
                           cwd=REPO, capture_output=True, timeout=600)
        digest = hashlib.sha256(open(out, 'rb').read()).hexdigest() if os.path.exists(out) else None
        if os.path.exists(out):
            os.unlink(out)
        row[v] = {'rc': p.returncode, 'stderr': hashlib.sha256(p.stderr).hexdigest(), 'object': digest}
    base = row['base']
    row['identical'] = all(row[v] == base for v in variants)
    return row

tasks = [(p, k) for p in inputs for k in options]
rows = []
with ThreadPoolExecutor(int(os.environ.get('JOBS', '2'))) as pool:
    for r in pool.map(one, tasks):
        rows.append(r)
with open(os.path.join(S, 'evidence', f'differential-{cfg}' + os.environ.get('TAG', '') + '.jsonl'), 'w') as f:
    for r in rows:
        f.write(json.dumps(r, sort_keys=True) + '\n')
bad = [r for r in rows if not r['identical']]
objects = sum(1 for r in rows if r['base']['object'])
print(f'config={cfg} invocations={len(rows) * len(variants)} rows={len(rows)} objects_compared={objects} mismatches={len(bad)} nonzero_exit_rows={sum(1 for r in rows if r["base"]["rc"])}')
for r in bad[:20]:
    print('MISMATCH', json.dumps(r))
