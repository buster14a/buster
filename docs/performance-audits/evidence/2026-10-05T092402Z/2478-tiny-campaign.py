#!/usr/bin/env python3
"""Fixed six-pair observations of the actual uninstrumented tiny-search cells."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--candidate', type=Path, required=True)
p.add_argument('--repo-root', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cpu', type=int, default=2)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
sources = {'baseline': a.baseline.resolve(), 'candidate': a.candidate.resolve()}
spec = {'pairs': 6, 'warmups_per_variant': 1, 'order': ['AB', 'BA', 'AB', 'BA', 'AB', 'BA'],
        'cpu': a.cpu, 'cells': {'n': 256, 'm': [1, 2, 3, 7, 15, 31, 32], 'shape': [0, 1], 'calls': 1048576},
        'source_binary_sha256': {k: hashlib.sha256(v.read_bytes()).hexdigest() for k, v in sources.items()},
        'interval': 'pointwise min/max of six paired ratios (96.875% sign-order coverage under independent continuous pairs); not simultaneous across 14 cells',
        'limits': 'shared host; no dedicated-host admission, speed/equivalence claim, adaptive extension or timing gate'}
(a.output/'prespecification.json').write_text(json.dumps(spec, indent=2)+'\n')
pattern = re.compile(r'BENCH_STRING_SEQUENCE n=(\d+) m=(\d+) shape=(\d+) calls=(\d+) elapsed_ns=(\d+) checksum=(\d+)')
records = []
environment = dict(os.environ, BUSTER_STRING_SEQUENCE_BENCH='1', BUSTER_TEST_JOBS='2')

def capture(label, variant):
    directory = a.output/label
    directory.mkdir()
    binary = directory/'ide'
    shutil.copy2(sources[variant], binary)
    command = ['taskset', '-c', str(a.cpu), str(binary.resolve()), 'test', '--module=string_tests', '--ci=1']
    start = time.time_ns()
    run = subprocess.run(command, cwd=a.repo_root, env=environment, capture_output=True, text=True)
    (directory/'stdout.log').write_text(run.stdout)
    (directory/'stderr.log').write_text(run.stderr)
    cells = [dict(zip(['n','m','shape','calls','elapsed_ns','checksum'], map(int, match))) for match in pattern.findall(run.stdout)]
    expected = {(256,m,s) for m in spec['cells']['m'] for s in [0,1]}
    observed = {(c['n'],c['m'],c['shape']) for c in cells}
    if run.returncode or len(cells) != 14 or observed != expected or any(c['elapsed_ns'] <= 0 or c['calls'] != 1048576 or c['checksum'] != (0 if c['shape'] == 0 else (1<<64)-1048576) for c in cells):
        raise SystemExit(f'capture failed: {label}, status={run.returncode}, cells={len(cells)}')
    record = {'label': label, 'variant': variant, 'start_unix_ns': start, 'command': command, 'returncode': run.returncode, 'cells': cells}
    records.append(record)
    (a.output/'runs.json').write_text(json.dumps(records, indent=2)+'\n')
    print(f'{label}: pass', flush=True)
    return {(c['m'], c['shape']): c['elapsed_ns'] for c in cells}

capture('warmup-baseline', 'baseline')
capture('warmup-candidate', 'candidate')
pairs=[]
for pair, order in enumerate(spec['order']):
    values={}
    for variant in (['baseline','candidate'] if order == 'AB' else ['candidate','baseline']):
        values[variant]=capture(f'pair-{pair}-{variant}',variant)
    pairs.append(values)
report=[]
for m in spec['cells']['m']:
    for shape in [0,1]:
        baseline=[pair['baseline'][m,shape] for pair in pairs]
        candidate=[pair['candidate'][m,shape] for pair in pairs]
        ratios=[b/a for a,b in zip(baseline,candidate)]
        report.append({'n':256, 'm':m, 'shape':shape,
                       'baseline_median_ns_per_call':statistics.median(baseline)/1048576,
                       'candidate_median_ns_per_call':statistics.median(candidate)/1048576,
                       'paired_ratio_median':statistics.median(ratios),
                       'pointwise_ratio_interval':[min(ratios),max(ratios)],
                       'paired_ratios':ratios})
(a.output/'summary.json').write_text(json.dumps({'specification':spec,'cells':report},indent=2)+'\n')
with (a.output/'summary.csv').open('w') as file:
    writer=csv.writer(file)
    writer.writerow(['n','m','shape','baseline_ns_per_call','candidate_ns_per_call','paired_ratio_median','pointwise_low','pointwise_high'])
    for row in report:
        writer.writerow([row['n'],row['m'],row['shape'],row['baseline_median_ns_per_call'],row['candidate_median_ns_per_call'],row['paired_ratio_median'],*row['pointwise_ratio_interval']])
print(json.dumps(report,indent=2))
