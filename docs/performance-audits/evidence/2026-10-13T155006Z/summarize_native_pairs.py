#!/usr/bin/env python3
"""Summarize all six pairs without dropping runs or replacing raw receipts."""
from pathlib import Path
import hashlib
import json
import math
import re
import statistics

root = Path(__file__).resolve().parent
directory = root / 'ab-suite-final'
observations = {}
for arm in 'AB':
    observations[arm] = []
    for pair in range(6):
        stem = directory / f'run-{arm}-{pair:02d}'
        observation = json.loads(stem.with_suffix('.json').read_text())
        assert observation['exit_code'] == 0 and not observation['timed_out']
        stdout = stem.with_suffix('.stdout').read_text(errors='replace')
        units = re.findall(r'\[(\d+)/(\d+)\] Unit tests', stdout)[-1]
        modules = re.findall(r'\[(\d+)/(\d+)\] Module tests', stdout)[-1]
        assert units[0] == units[1] and modules == ('63', '63')
        observation['unit_assertions'] = int(units[0])
        observations[arm].append(observation)

def sign_test(baseline, candidate):
    lower = sum(b < a for a, b in zip(baseline, candidate))
    higher = sum(b > a for a, b in zip(baseline, candidate))
    n = lower + higher
    return min(1.0, 2 * sum(math.comb(n, k) for k in range(min(lower, higher) + 1)) / 2**n)

assert sign_test([2] * 6, [1] * 6) == 0.03125
assert sign_test([1] * 6, [1] * 6) == 1.0
assert sign_test([2] * 6, [1] * 5 + [3]) == 0.21875

metrics = []
for name in ('wall_seconds', 'user_seconds', 'system_seconds', 'max_rss_kib',
             'minor_faults', 'major_faults', 'voluntary_switches', 'involuntary_switches'):
    a = [r[name] for r in observations['A']]
    b = [r[name] for r in observations['B']]
    median_a, median_b = statistics.median(a), statistics.median(b)
    change = median_b / median_a - 1 if median_a else (0 if median_b == 0 else None)
    p = sign_test(a, b)
    if a == b:
        verdict = 'within-effect'
    elif p > 0.05 or change is None:
        verdict = 'inconclusive'
    elif abs(change) < 0.01:
        verdict = 'within-effect'
    else:
        verdict = 'improvement' if change < 0 else 'regression'
    metrics.append({'name': name, 'baseline': a, 'candidate': b,
                    'baseline_median': median_a, 'candidate_median': median_b,
                    'relative_change': change, 'sign_test_p': p, 'verdict': verdict})

manifest = json.loads((root / 'experiment-manifest.json').read_text())
summary = {
    'schema': 'buster-native-pair-summary-v1', 'complete': True,
    'claim': 'native-runtime', 'evidence_class': 'native-host',
    'method': 'exact-child wait4 and monotonic clock; 50 ms poll; two-sided paired sign test excluding ties; ratio of medians; 1 percent minimum effect',
    'collector': 'run_native_pair.py',
    'collector_sha256': hashlib.sha256((root / 'run_native_pair.py').read_bytes()).hexdigest(),
    'summary_script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'host': {'cpu': manifest['cpu'], 'uname': manifest['uname']},
    'baseline_revision': manifest['baseline_revision'],
    'candidate_revision': manifest['candidate_revision'],
    'baseline_binary_sha256': manifest['baseline_binary_sha256'],
    'candidate_binary_sha256': manifest['candidate_binary_sha256'],
    'arguments': ['test', '--verbose=0'], 'pairs': 6, 'additional_warmups': 0,
    'order': 'A0 B0 B1 A1 A2 B2 B3 A3 A4 B4 B5 A5',
    'metrics': metrics,
    'unit_assertions': {arm: [x['unit_assertions'] for x in rows] for arm, rows in observations.items()},
    'limits': 'Only this native host, workload, configuration, and process population; no Zen 5 or other-platform claim. A/A control covered os_tests, not the complete suite.',
}
(directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
for metric in metrics:
    change = metric['relative_change']
    print(f"{metric['name']}: {metric['baseline_median']:.6g} -> {metric['candidate_median']:.6g}, "
          f"change={100*change if change is not None else 'unavailable'}%, p={metric['sign_test_p']}, {metric['verdict']}")
