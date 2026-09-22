#!/usr/bin/env python3
"""Explicitly opt-in, serial paired timing. Not Buster host-service admission.

Raw endpoint construction is outside timing. Every C sample charges per-call
allocation, normalization, all kernel work, optional sort scratch, and free.
No automatic sample exclusions. Non-9700X execution requires an explicit label.
"""
import argparse
import csv
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
VARIANTS = ('scalar', 'hull', 'certificate', 'avx2', 'evex256', 'avx512',
            'avx512_u2', 'avx512_u4', 'avx512_u8')
PATTERNS = ('ordered', 'reverse', 'outward', 'permuted', 'ranges',
            'overlap_early', 'overlap_late', 'random')


def read_optional(path):
    try:
        return Path(path).read_text()
    except OSError as exc:
        return f'UNAVAILABLE: {exc}'


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def capture(command):
    try:
        done = subprocess.run(command, text=True, capture_output=True, check=False)
        return {'command': command, 'returncode': done.returncode,
                'stdout': done.stdout, 'stderr': done.stderr}
    except OSError as exc:
        return {'command': command, 'error': str(exc)}


def metadata(args):
    sysfiles = {
        'cpuinfo': '/proc/cpuinfo', 'kernel_version': '/proc/version',
        'smt_active': '/sys/devices/system/cpu/smt/active',
        'boost': '/sys/devices/system/cpu/cpufreq/boost',
        'amd_pstate_status': '/sys/devices/system/cpu/amd_pstate/status',
        'microcode': f'/sys/devices/system/cpu/cpu{args.cpu}/microcode/version',
        'thread_siblings': f'/sys/devices/system/cpu/cpu{args.cpu}/topology/thread_siblings_list',
        'governor': f'/sys/devices/system/cpu/cpu{args.cpu}/cpufreq/scaling_governor',
        'scaling_min_freq': f'/sys/devices/system/cpu/cpu{args.cpu}/cpufreq/scaling_min_freq',
        'scaling_max_freq': f'/sys/devices/system/cpu/cpu{args.cpu}/cpufreq/scaling_max_freq',
        'scaling_driver': f'/sys/devices/system/cpu/cpu{args.cpu}/cpufreq/scaling_driver',
    }
    files = {name: read_optional(path) for name, path in sysfiles.items()}
    context = json.loads(args.context.read_text()) if args.context else {
        'dimm_configuration': 'UNRECORDED', 'other_host_activity': 'UNRECORDED',
        'smt_sibling_condition': 'UNRECORDED', 'host_lease': 'UNRECORDED',
        'thermal_condition': 'UNRECORDED', 'power_policy_notes': 'UNRECORDED',
    }
    return {
        'created_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'argv': sys.argv, 'python': sys.version, 'platform': platform.platform(),
        'environment': files, 'operator_context': context,
        'lscpu': capture(['lscpu', '--json']), 'compiler': capture(['gcc', '--version']),
        'build_manifest': read_optional(ROOT / 'build_manifest.json'),
        'sha256': {name: sha256(ROOT / name) for name in
                   ('switch_overlap.c', 'unroll.inc', 'generate_unroll.py', 'run_pairs.py', 'switch_overlap')},
        'clock': 'C CLOCK_MONOTONIC_RAW where available, elapsed nanoseconds; no core-cycle or TSC claim',
        'scope': 'isolated endpoint pipeline; not a compiler or admitted performance-service run',
        'exclusions': 'None. A failed or mismatching pair aborts; previous samples retained.',
        'cache_protocol': 'one pool warmup per process; then seeded random visits; pool size does not prove cache coldness',
        'affinity_before': sorted(os.sched_getaffinity(0)),
        'dedicated_target': 'AMD Ryzen 7 9700X' in files['cpuinfo'],
        'machine_policy_changes': 'None; only child-inherited process affinity set to requested CPU',
    }


def parse_csv(text, allowed=None):
    result = text.split(',')
    if allowed and any(item not in allowed for item in result):
        raise argparse.ArgumentTypeError(f'allowed values: {allowed}')
    return result


def make_schedule(args):
    rng = random.Random(args.seed)
    plan = []
    for n in args.lengths:
        for pattern in args.patterns:
            pool = max(1, args.pool_mib * 1024 * 1024 // max(16 * n, 16)) if args.pool_mib else args.pool
            for phase, repetitions, candidates in (
                ('AA', args.aa_repetitions, ['scalar']), ('AB', args.repetitions, args.variants)):
                for rep in range(repetitions):
                    order = list(candidates)
                    rng.shuffle(order)
                    for candidate in order:
                        pair_seed = rng.randrange(1, 1 << 63)
                        sides = [('A', 'scalar'), ('B', candidate)]
                        rng.shuffle(sides)
                        plan.append({'phase': phase, 'n': n, 'pattern': pattern,
                                     'repetition': rep, 'candidate': candidate, 'seed': pair_seed,
                                     'pool': pool, 'sides': sides})
    return plan


def summarize(raw):
    groups = {}
    for pair in raw:
        a, b = pair['A']['elapsed_ns'], pair['B']['elapsed_ns']
        key = (pair['phase'], pair['n'], pair['pattern'], pair['candidate'])
        groups.setdefault(key, []).append(math.log(b / a))
    result = []
    rng = random.Random(0xC0FFEE)
    for (phase, n, pattern, variant), ratios in groups.items():
        boots = sorted(statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(4000))
        q1, q3 = (ratios[0], ratios[0]) if len(ratios) < 2 else (
            statistics.quantiles(ratios, n=4, method='inclusive')[0],
            statistics.quantiles(ratios, n=4, method='inclusive')[2])
        result.append({'phase': phase, 'n': n, 'pattern': pattern, 'variant': variant,
                       'pairs': len(ratios), 'median_B_over_A': math.exp(statistics.median(ratios)),
                       'ratio_q1': math.exp(q1), 'ratio_q3': math.exp(q3),
                       'median_ratio_bootstrap95': [math.exp(boots[100]), math.exp(boots[3899])],
                       'uncertainty_caveat': 'Exploratory paired bootstrap; correlated host drift and multiple selection remain; confirm held-out.'})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--bench', action='store_true')
    mode.add_argument('--dry-run', action='store_true')
    parser.add_argument('--cpu', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--variants', type=lambda s: parse_csv(s, VARIANTS), default=['hull', 'certificate', 'avx2', 'evex256', 'avx512'])
    parser.add_argument('--lengths', type=lambda s: [int(x) for x in s.split(',')], default=[4, 8, 10, 16, 27, 48, 130, 156])
    parser.add_argument('--patterns', type=lambda s: parse_csv(s, PATTERNS), default=['ordered', 'permuted'])
    parser.add_argument('--calls', type=int, default=20000)
    parser.add_argument('--pool', type=int, default=64)
    parser.add_argument('--pool-mib', type=int, default=0)
    parser.add_argument('--repetitions', type=int, default=21)
    parser.add_argument('--aa-repetitions', type=int, default=9)
    parser.add_argument('--seed', type=int, default=20260922)
    parser.add_argument('--context', type=Path)
    parser.add_argument('--diagnostic-non-9700x', action='store_true')
    args = parser.parse_args()
    if args.calls <= 0 or args.pool <= 0 or args.pool_mib < 0 or min(args.lengths) < 0 or max(args.lengths) > 1000000:
        parser.error('invalid size or calls')
    if args.repetitions < 5 or args.aa_repetitions < 5:
        parser.error('at least five independent pairs required in both phases')
    if args.cpu not in os.sched_getaffinity(0):
        parser.error('requested CPU is not in this process affinity set')
    info = metadata(args)
    if args.bench and not info['dedicated_target'] and not args.diagnostic_non_9700x:
        parser.error('9700X target not verified; --diagnostic-non-9700x labels another CPU explicitly')
    args.out.mkdir(parents=True, exist_ok=False)
    plan = make_schedule(args)
    (args.out / 'metadata.json').write_text(json.dumps(info, indent=2) + '\n')
    (args.out / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    if args.dry_run:
        print(json.dumps({'mode': 'dry-run', 'pairs': len(plan), 'commands': 2 * len(plan), 'timed_runs': 0}))
        return
    # This lock serializes this harness only; operator must establish host lease.
    with open(ROOT / '.paired-run.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        os.sched_setaffinity(0, {args.cpu})
        (args.out / 'affinity.json').write_text(json.dumps(sorted(os.sched_getaffinity(0))))
        raw = []
        with (args.out / 'samples.jsonl').open('w') as jf, (args.out / 'samples.csv').open('w', newline='') as cf:
            fields = ['pair', 'phase', 'n', 'pattern', 'candidate', 'repetition', 'side', 'variant', 'seed', 'pool', 'calls', 'elapsed_ns', 'input_hash', 'output_hash', 'process_peak_rss_kib']
            writer = csv.DictWriter(cf, fieldnames=fields)
            writer.writeheader()
            for pair_id, item in enumerate(plan):
                pair = dict(item)
                for side, variant in item['sides']:
                    command = [str(ROOT / 'switch_overlap'), '--bench', variant, item['pattern'], str(item['n']), str(args.calls), str(item['seed']), str(item['pool'])]
                    done = capture(command)
                    (args.out / f'command-{pair_id:05d}-{side}.json').write_text(json.dumps(done, indent=2) + '\n')
                    if done.get('returncode') != 0:
                        raise RuntimeError(f'failed sample {pair_id} {side}; raw output retained')
                    sample = json.loads(done['stdout'])
                    pair[side] = sample
                    record = {k: item[k] for k in ('phase', 'n', 'pattern', 'candidate', 'repetition', 'seed', 'pool')}
                    record.update({k: sample[k] for k in ('variant', 'calls', 'elapsed_ns', 'input_hash', 'output_hash', 'process_peak_rss_kib')})
                    record.update(pair=pair_id, side=side)
                    jf.write(json.dumps(record) + '\n'); jf.flush()
                    writer.writerow(record); cf.flush()
                if any(pair['A'][key] != pair['B'][key] for key in ('input_hash', 'output_hash', 'calls', 'pool')):
                    raise RuntimeError(f'pair {pair_id} identity/output mismatch; retained, no exclusions')
                raw.append(pair)
        (args.out / 'summary.json').write_text(json.dumps(summarize(raw), indent=2) + '\n')
        print(json.dumps({'mode': 'complete', 'pairs': len(raw), 'out': str(args.out)}))


if __name__ == '__main__':
    main()
