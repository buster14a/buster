# WIP (#1834), not for merging (see gen.py). Batched sweep runner:
#   python3 -I batch.py clang TARGET CASEDIR|intrin.txt OUT
#   SWEEP_ONLY=clang.TARGET.txt python3 -I batch.py buster:build/compiler TARGET CASEDIR|intrin.txt OUT [extra flags]
# Concatenates cases with renaming macros, compiles once and attributes errors by line; splits a batch whose failure
# has no attributable error (a crash). buster stops at its first error, so its failing case is dropped and the rest
# rerun. The clang mode ran (seconds per target); the buster mode is untested: the container's builds were OOM-killed
# before build/compiler existed, so its flags (-w, --lanes 1) need checking.
# Batched sweep runner: python3 -I batch.py COMPILER_KIND TARGET CASEDIR OUT
#   COMPILER_KIND: clang | buster:<path>
# Concatenates cases with renaming macros, compiles once, attributes errors by line; splits a batch whose failure
# has no attributable error (a crash). For buster, which stops at the first error, removes the failing case and retries.
import os, sys, subprocess, re, tempfile
kind, target, casedir, out = sys.argv[1:5]
prefix = ''
if casedir.endswith('.txt'):
    srcs = {}
    for line in open(casedir):
        n, code = line.rstrip('\n').split('\t', 1)
        srcs[n] = code + '\n'
    names = list(srcs)
    only = os.environ.get('SWEEP_ONLY')
    if only:
        keep = set(l.split()[0] for l in open(only) if ' ok' in l)
        names = [n for n in names if n in keep]
    prefix = '#include <immintrin.h>\n#include <x86intrin.h>\n'
    macros = ['g']
else:
    names = sorted(n[:-2] for n in os.listdir(casedir) if n.endswith('.c'))
    srcs = {n: open(os.path.join(casedir, n + '.c')).read() for n in names}
    only = os.environ.get('SWEEP_ONLY')
    if only:
        keep = set(l.split()[0] for l in open(only) if ' ok' in l)
        names = [n for n in names if n in keep]
    macros = ['V', 'M', 'X', 'W', 'H', 'R', 'B', 'I', 'f']
work = tempfile.mkdtemp(prefix='sweep-', dir=os.path.dirname(os.path.abspath(out)))
def build(batch):
    lines = prefix.rstrip('\n').split('\n') if prefix else []
    owner = [None] * len(lines)
    for k, n in enumerate(batch):
        for m in macros:
            lines.append(f'#define {m} {m}_{k}'); owner.append(None)
        for l in srcs[n].rstrip('\n').split('\n'):
            lines.append(l); owner.append(n)
        for m in macros:
            lines.append(f'#undef {m}'); owner.append(None)
    return '\n'.join(lines) + '\n', owner
def run(batch):
    text, owner = build(batch)
    path = os.path.join(work, 'b.c')
    with open(path, 'w') as fh:
        fh.write(text)
    if kind == 'clang':
        cmd = ['clang', '-O0', '-march=' + target, '-c', '-o', '/dev/null', '-ferror-limit=0', '-w', path]
    else:
        cmd = [kind.split(':', 1)[1], '-c', '-march=' + target, '--lanes', '1', '-w', '-o', os.path.join(work, 'b.o'), path]
        cmd += sys.argv[5:]
    p = subprocess.run(cmd, capture_output=True, text=True)
    errs = {}
    for line in p.stderr.split('\n'):
        m = re.match(r'^' + re.escape(path) + r':(\d+):\d+: (?:fatal )?error: (.*)$', line)
        if m:
            ln = int(m.group(1)) - 1
            n = owner[ln] if 0 <= ln < len(owner) else None
            if n is not None and n not in errs:
                errs[n] = m.group(2)
    return p.returncode, errs, p.stderr
result = {}
def solve(batch):
    if not batch:
        return
    rc, errs, stderr = run(batch)
    if rc == 0:
        for n in batch:
            result[n] = 'ok'
        return
    if errs and kind == 'clang':
        for n in batch:
            if n in errs:
                result[n] = 'fail ' + errs[n]
        rest = [n for n in batch if n not in errs]
        solve(rest)
        return
    if errs:
        # buster stops at its first error: record it and go on with the others.
        first = next(iter(errs))
        result[first] = 'fail ' + errs[first]
        solve([n for n in batch if n != first])
        return
    if len(batch) == 1:
        tail = [l for l in stderr.split('\n') if l.strip()]
        result[batch[0]] = 'fail ?? ' + (tail[0][:200] if tail else 'rc=%d' % rc)
        return
    h = len(batch) // 2
    solve(batch[:h]); solve(batch[h:])
chunk = int(os.environ.get('SWEEP_CHUNK', '64'))
order = names
for i in range(0, len(order), chunk):
    solve(order[i:i + chunk])
with open(out, 'w') as fh:
    for n in names:
        fh.write(f'{n} {result.get(n, "missing")}\n')
