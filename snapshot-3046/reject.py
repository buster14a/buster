"""Compare a normal snapshot with the same binary forced through the unsupported-advice fallback."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys

root = Path(sys.argv[1])
directory = root / 'fallback-cache'
directory.mkdir()
out = root / 'qualification'
cpu = min(os.sched_getaffinity(0))
os.sched_setaffinity(0, {cpu})
for kind, filename, flags in (
    ('dynamic', 'runtime.snapshot', []),
    ('static', 'runtime-static.snapshot', ['-static']),
    ('static-pie', 'runtime-static-pie.snapshot', ['-static-pie']),
    ('no-pie', 'runtime-no-pie.snapshot', ['-no-pie']),
    ('shared', 'runtime-shared.snapshot', []),
):
    results = []
    for reject in (False, True):
        if (directory / 'buster').exists():
            shutil.rmtree(directory / 'buster')
        env = dict(os.environ, XDG_CACHE_HOME=str(directory))
        if reject:
            env['LD_PRELOAD'] = str(root / 'reject.so')
        cmd = [str(root / 'plain-head')]
        if kind == 'shared':
            cmd += ['benchmark-link', '--only', 'direct', '--kinds', 'shared', '--functions', '1',
                    '--runs', '1', '--cpus', str(cpu), '--cache', str(directory / 'buster'),
                    '--directory', str(root), '--keep']
        else:
            cmd += [*flags, 'tools/ci/qualification.c', '-o', str(out)]
        proc = subprocess.run(cmd, check=True, capture_output=True, env=env, text=True)
        assert proc.stderr.count('declined writable snapshot advice') == int(reject), (kind, reject, proc.stderr)
        if kind == 'shared':
            temp = Path(re.search(r'outputs in ([^;]+); the snapshots in ', proc.stdout)[1])
            assert temp.parent == root and temp.name.startswith('buster-link-benchmark-')
            shutil.copyfile(temp / 'direct', out)
            shutil.rmtree(temp)
        results.append(((directory / 'buster' / filename).read_bytes(), out.read_bytes()))
        warm = subprocess.run(cmd, check=True, capture_output=True, env=env, text=True)
        assert 'declined writable snapshot advice' not in warm.stderr
        if kind == 'shared':
            temp = Path(re.search(r'outputs in ([^;]+); the snapshots in ', warm.stdout)[1])
            assert temp.parent == root and temp.name.startswith('buster-link-benchmark-')
            shutil.rmtree(temp)
    assert results[0] == results[1], kind
    print('ADVICE_FALLBACK', kind, 'image_bytes', len(results[0][0]), 'equal_raw_images_and_outputs warm_bypassed', flush=True)
