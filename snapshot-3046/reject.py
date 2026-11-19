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
        trace = root / 'advice.trace'
        cmd = ['strace', '-f', '-qq', '-e', 'trace=madvise', '-o', str(trace), '--']
        if reject:
            cmd += [str(root / 'reject-advice'), str(size)]
        cmd.append(str(root / 'plain-head'))
        if kind == 'shared':
            cmd += ['benchmark-link', '--only', 'direct', '--kinds', 'shared', '--functions', '1',
                    '--runs', '1', '--cpus', str(cpu), '--cache', str(directory / 'buster'),
                    '--directory', str(root), '--keep']
        else:
            cmd += [*flags, 'tools/ci/qualification.c', '-o', str(out)]
        proc = subprocess.run(cmd, check=True, capture_output=True, env=env, text=True)
        image = directory / 'buster' / filename
        size = image.stat().st_size
        marker = f', {size}, MADV_POPULATE_WRITE)'
        calls = [l for l in trace.read_text().splitlines() if marker in l]
        assert len(calls) == 1, (kind, reject, calls)
        assert ('= -1 EINVAL' in calls[0]) if reject else calls[0].rstrip().endswith('= 0'), calls
        print('ADVICE_CALL', kind, 'reject', int(reject), calls[0], flush=True)
        if kind == 'shared':
            temp = Path(re.search(r'outputs in ([^;]+); the snapshots in ', proc.stdout)[1])
            assert temp.parent == root and temp.name.startswith('buster-link-benchmark-')
            shutil.copyfile(temp / 'direct', out)
            shutil.rmtree(temp)
        results.append(((directory / 'buster' / filename).read_bytes(), out.read_bytes()))
        warm = subprocess.run(cmd, check=True, capture_output=True, env=env, text=True)
        assert marker not in trace.read_text()
        if kind == 'shared':
            temp = Path(re.search(r'outputs in ([^;]+); the snapshots in ', warm.stdout)[1])
            assert temp.parent == root and temp.name.startswith('buster-link-benchmark-')
            shutil.rmtree(temp)
    assert results[0] == results[1], kind
    print('ADVICE_FALLBACK', kind, 'image_bytes', len(results[0][0]), 'equal_raw_images_and_outputs warm_bypassed', flush=True)
