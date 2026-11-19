"""Hosted A/B on real system runtimes. Usage: qualify.py plain|trace directory."""
from hashlib import sha256
from pathlib import Path
from statistics import median
import os
import random
import re
import shutil
import subprocess
import sys
import time

stage, path = sys.argv[1:]
root = Path(path)
kinds = {
    'dynamic': ([], 'runtime.snapshot'),
    'static': (['-static'], 'runtime-static.snapshot'),
    'static-pie': (['-static-pie'], 'runtime-static-pie.snapshot'),
    'shared': (['-shared', '-fPIC'], 'runtime-shared.snapshot'),
    'no-pie': (['-no-pie'], 'runtime-no-pie.snapshot'),
}
src = 'tools/ci/qualification.c'
variants = ('base', 'head')
pinned = min(os.sched_getaffinity(0))
os.sched_setaffinity(0, {pinned})
out = root / 'qualification'
print('PLACEMENT', stage, 'cpu', pinned, 'source', src, sha256(Path(src).read_bytes()).hexdigest(), flush=True)

def driver(v, kind, directory):
    cmd = [str(root / (stage + '-' + v)), *kinds[kind][0], src, '-o', str(out)]
    env = dict(os.environ, XDG_CACHE_HOME=str(directory))
    begin = time.monotonic_ns()
    run = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    elapsed = time.monotonic_ns() - begin
    if run.returncode:
        raise RuntimeError((cmd, run.returncode, run.stderr))
    return elapsed, run.stderr

def stats(p):
    s = p.stat()
    return s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns

def percentile(data, fraction):
    return sorted(data)[round(fraction * (len(data) - 1))]

def outcome(tag, kind, vals):
    a, b = vals['base'], vals['head']
    delta = [100 * (x-y) / x for x,y in zip(a,b)]
    rng = random.Random(3046)
    boots = [median(rng.choices(delta, k=len(delta))) for _ in range(3500)]
    print('SAMPLES',tag,kind,'base_us',','.join('%.1f'%x for x in a),'head_us',','.join('%.1f'%x for x in b), flush=True)
    print('TIME',tag,kind,'pairs',len(a),'base_us',round(median(a),2),'head_us',round(median(b),2),
          'paired_gain_pct',round(median(delta),2),'boot95',round(percentile(boots,.025),2),round(percentile(boots,.975),2), flush=True)

for kind, (_, filename) in kinds.items():
    objs = []
    images = []
    for v in variants:
        directory = root / (stage + '-' + v + '-' + kind)
        directory.mkdir()
        # Check normal creation; subsequent compiles use the same directory and same driver executable.
        _, log = driver(v, kind, directory)
        f = directory / 'buster' / filename
        blob = f.read_bytes()
        before = stats(f)
        exec_digest = sha256(out.read_bytes()).hexdigest()
        objs.append(exec_digest)
        if stage == 'trace':
            images.append(blob)
            line = next(l for l in log.splitlines() if l.startswith('TRACE '))
            desc = next(l for l in log.splitlines() if l.startswith('HASH '))
            piece_off, piece_len, digest = re.search(r'pieces_offset=(\d+) pieces_bytes=(\d+) directory=(\w+)',desc).groups()
            print(v, line, flush=True)
            print('IMAGE', v, kind, 'size', len(blob), 'sha256', sha256(blob).hexdigest(), 'dir', digest,
                  'piece_hashes_sha256', sha256(blob[int(piece_off):int(piece_off)+int(piece_len)]).hexdigest(), flush=True)
        _, warm_log = driver(v, kind, directory)
        assert 'TRACE kind=' not in warm_log and stats(f) == before
        assert out.is_file() and sha256(out.read_bytes()).hexdigest() == exec_digest
        with f.open('r+b') as damaged:
            damaged.write(bytes([blob[0] ^ 0xff]))
        _, rebuilt_log = driver(v, kind, directory)
        assert f.stat().st_ino != before[1] and f.read_bytes() == blob
        if stage == 'trace':
            assert 'TRACE kind=' in rebuilt_log
        print('DRIVER', stage, v, kind, 'cold warm corruption-rebuild passed', 'output_sha256', exec_digest, flush=True)
        if kind == 'dynamic' and stage == 'plain':
            subprocess.run([str(out),'--self-test'],check=True)
    assert objs[0] == objs[1], ('different linked outputs',kind,objs)
    if stage == 'trace':
        assert images[0] == images[1], ('different snapshot bytes',kind,
            [(i,x,y) for i,(x,y) in enumerate(zip(*images)) if x!=y][:30])
        continue
    for tag in ('cold','warm'):
        vals = {v:[] for v in variants}
        for n in range(42):
            for v in variants if n%2==0 else reversed(variants):
                directory = root / (stage+'-'+v+'-'+kind)
                if tag=='cold':
                    shutil.rmtree(directory/'buster')
                elapsed, _ = driver(v,kind,directory)
                if n >= 2:
                    vals[v].append(elapsed/1000)
        outcome(tag,kind,vals)
    vals = {v:[] for v in variants}
    for n in range(8):
        for v in variants if n%2==0 else reversed(variants):
            report = subprocess.check_output([str(root / (stage+'-'+v)), 'benchmark-snapshot', '--only', kind,
                                               '--runs','15','--directory',str(root)], text=True)
            val = float(re.search(r'(?m)^  snapshot_write\s+([\d.]+) us',report)[1])
            vals[v].append(val)
    outcome('writer',kind,vals)
