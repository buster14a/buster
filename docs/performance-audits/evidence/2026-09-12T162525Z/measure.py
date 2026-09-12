#!/usr/bin/env python3
"""Same-checkout Release test RSS replay; analysis tool, not a CI matrix gate."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def fields(line):
    return dict(token.split('=', 1) for token in line.split()[1:])


def parse_log(path):
    modules, arenas = [], []
    for line in path.read_text().splitlines():
        if line.startswith('TEST_MODULE_TIMING '):
            row = fields(line)
            modules.append({key: value for key, value in row.items() if key != 'duration_ns'})
        elif line.startswith('TEST_ARENA_V1 '):
            row = fields(line)
            for key in ['start', 'end', 'retained_bytes', 'high_water', 'peak_bytes', 'after', 'live_bytes', 'rewind']:
                row[key] = int(row[key])
            assert row['retained_bytes'] == row['end'] - row['start'] >= 0, row
            assert row['high_water'] >= row['end'] >= row['start'], row
            assert row['peak_bytes'] == row['high_water'] - row['start'], row
            assert row['live_bytes'] == row['after'] - row['start'] >= 0, row
            assert row['after'] == (row['start'] if row['rewind'] else row['end']), row
            arenas.append(row)
    assert modules and all(row['status'] == 'pass' and row['failed'] == '0' for row in modules)
    return modules, arenas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--pairs', type=int, default=3)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--rss-budget-mib', type=int, default=640)
    args = parser.parse_args()
    assert args.pairs > 0 and args.jobs > 0
    args.output.mkdir(parents=True, exist_ok=False)
    temporary = (args.output / 'temporary').resolve()
    temporary.mkdir()
    binaries = {'base': args.base.resolve(), 'candidate': args.candidate.resolve()}
    hashes = {key: hashlib.sha256(path.read_bytes()).hexdigest() for key, path in binaries.items()}
    (args.output / 'binaries.json').write_text(json.dumps(hashes, indent=2) + '\n')
    env = dict(os.environ, BUSTER_TEST_JOBS=str(args.jobs), BUSTER_TEST_TEMPORARY_BASE=str(temporary))
    rows, expected_modules = [], None
    for pair in range(args.pairs):
        for variant in (['base', 'candidate'] if pair % 2 == 0 else ['candidate', 'base']):
            label = str(pair) + '-' + variant
            path = args.output / (label + '.log')
            with path.open('w') as log:
                start = time.monotonic()
                child = subprocess.Popen([str(binaries[variant]), 'test', '--verbose=1', '--ci=1'], stdout=log, stderr=subprocess.STDOUT, env=env)
                _, status, usage = os.wait4(child.pid, 0)
                code = os.waitstatus_to_exitcode(status)
                child.returncode = code
                row = dict(pair=pair, variant=variant, jobs=args.jobs, exit_code=code,
                           peak_rss_kib=usage.ru_maxrss, wall_seconds=time.monotonic()-start,
                           user_seconds=usage.ru_utime, system_seconds=usage.ru_stime)
            rows.append(row)
            with (args.output / 'raw.csv').open('w', newline='') as raw:
                writer = csv.DictWriter(raw, fieldnames=list(row), lineterminator="\n")
                writer.writeheader()
                writer.writerows(rows)
            print(json.dumps(row), flush=True)
            assert code == 0, path
            modules, arenas = parse_log(path)
            if expected_modules is None: expected_modules = modules
            assert modules == expected_modules, (label, 'module/assertion results changed')
            (args.output / (label + '-modules.json')).write_text(json.dumps(modules, indent=2) + '\n')
            (args.output / (label + '-arenas.json')).write_text(json.dumps(arenas, indent=2) + '\n')
            if variant == 'candidate':
                assert usage.ru_maxrss <= args.rss_budget_mib * 1024, row
                budgets = {'compiler_driver_tests': 64, 'c_frontend_tests': 40, 'machine_tests': 80}
                seen = set()
                for arena in arenas:
                    if arena['kind'] == 'module' and arena.get('arena_slot', '0') == '0' and arena['module'] in budgets:
                        seen.add(arena['module'])
                        assert arena['peak_bytes'] <= budgets[arena['module']] * 1048576, arena
                assert seen == set(budgets), 'missing primary module memory report'
    temporary.rmdir()


if __name__ == '__main__':
    main()
