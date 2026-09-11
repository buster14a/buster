#!/usr/bin/env python3
"""Paired wall-clock diagnostics for the fixed alias fixture; no runtime claims."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('baseline')
parser.add_argument('candidate')
parser.add_argument('source')
parser.add_argument('output')
args = parser.parse_args()
out = Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=False)
compilers = {'baseline': str(Path(args.baseline).resolve()), 'candidate': str(Path(args.candidate).resolve())}
source = Path(args.source).resolve()
cpu = min(os.sched_getaffinity(0))
os.sched_setaffinity(0, {cpu})
rows = []
commands = []
for mode in ('fast', 'quality'):
    artifact = out / (mode + '.o')
    for pair in range(14):
        order = ('baseline', 'candidate') if pair % 2 == 0 else ('candidate', 'baseline')
        for variant in order:
            command = [compilers[variant], 'cc', '-c', '-g0', '-O0', '-fregister-allocator=' + mode,
                       '-fno-machine-fallback', str(source), '-o', str(artifact)]
            start = time.perf_counter_ns()
            subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=30, check=True)
            elapsed = (time.perf_counter_ns() - start) / 1e9
            if pair >= 2:
                rows.append({'mode': mode, 'pair': pair - 2, 'variant': variant, 'wall_s': elapsed,
                    'output_bytes': artifact.stat().st_size, 'output_sha256': hashlib.sha256(artifact.read_bytes()).hexdigest()})
                commands.append(command)
summary = {}
rng = random.Random(41)
for mode in ('fast', 'quality'):
    selected = [row for row in rows if row['mode'] == mode]
    before = [row for row in selected if row['variant'] == 'baseline']
    after = [row for row in selected if row['variant'] == 'candidate']
    ratios = [a['wall_s'] / b['wall_s'] for b, a in zip(before, after)]
    bootstrap = sorted(statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(10000))
    summary[mode] = {'baseline_median_wall_s': statistics.median(row['wall_s'] for row in before),
        'candidate_median_wall_s': statistics.median(row['wall_s'] for row in after),
        'median_paired_ratio': statistics.median(ratios), 'paired_bootstrap_95': [bootstrap[249], bootstrap[9749]],
        'baseline_output_bytes': before[0]['output_bytes'], 'candidate_output_bytes': after[0]['output_bytes'],
        'output_identical': len({row['output_sha256'] for row in selected}) == 1}
    for variant in compilers:
        assert len({row['output_sha256'] for row in selected if row['variant'] == variant}) == 1
assert summary['fast']['output_identical']
result = {'cpu': cpu, 'host_qualification': 'shared container; diagnostic only', 'compilers': compilers,
    'compiler_sha256': {name: hashlib.sha256(Path(path).read_bytes()).hexdigest() for name, path in compilers.items()},
    'source': str(source), 'source_bytes': source.stat().st_size, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'summary': summary, 'samples': rows, 'commands': commands}
(out / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(summary, indent=2))
