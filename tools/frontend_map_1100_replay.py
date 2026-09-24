#!/usr/bin/env python3
"""Disposable hosted correctness/work attribution, not a timing or RSS benchmark."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
evidence = Path(sys.argv[2]).resolve()
variant = sys.argv[3]
compiler = Path(sys.argv[4]).resolve()
evidence.mkdir(parents=True, exist_ok=True)
inputs = evidence / 'inputs'
inputs.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location('census', root / 'tools/allocation_census.py')
census = importlib.util.module_from_spec(spec)
spec.loader.exec_module(census)

sources = {
    'empty': '',
    'tiny': 'int f(void) { return 7; }\n',
    'warning': '#line 73 "logical.c"\n#warning retained message\nint f(void) { return 7; }\n',
    'malformed': 'int broken(\n',
    'error': '#line 91 "bad.c"\n#error retained error\nint x;\n',
    'aliases': 'bool b; alignas(16) int x; thread_local int t; static_assert(alignof(int) >= 1, "ok");\n',
    'alias_growth': ''.join('bool value_%d;\n' % i for i in range(4096)),
    'macro_growth': '#define DECL(n) int value_##n = n;\n' + ''.join('DECL(%d)\n' % i for i in range(32768)),
}
for name, text in sources.items():
    path = inputs / (name + '.c')
    if path.exists():
        assert path.read_text() == text
    else:
        path.write_text(text)

cases = []
for name in sources:
    flags = ['-std=c23'] if name.startswith('alias') else ['-std=c17']
    cases.append((name, [*flags, '-g', '-c', str(inputs / (name + '.c'))], name not in ('malformed', 'error')))
cases.append(('own_source', ['-g', '-Isrc', '-Ibuild/generated', '-DBUSTER_UNITY_BUILD=1', '-DBUSTER_INCLUDE_TESTS=0', '-c', 'src/buster/apps/ide/ide.c'], True))
records = []
for name, args, success in cases:
    output = evidence / 'same-output.o'
    output.unlink(missing_ok=True)
    argv = [str(compiler), 'cc', *args, '-o', str(output)]
    env = dict(os.environ, BUSTER_ALLOCATION_CENSUS='1')
    completed = subprocess.run(argv, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
    prefix = evidence / (variant + '-' + name)
    Path(str(prefix) + '.stdout').write_bytes(completed.stdout)
    Path(str(prefix) + '.stderr').write_bytes(completed.stderr)
    raw = completed.stdout + b'\n' + completed.stderr
    Path(str(prefix) + '.raw.log').write_bytes(raw)
    report = census.parse_census(raw.decode('utf-8', errors='strict'))
    Path(str(prefix) + '.allocation.json').write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    def without_census(data):
        return b''.join(line for line in data.splitlines(keepends=True) if not line.startswith(b'BUSTER_ALLOC_'))
    object_bytes = output.read_bytes() if output.exists() else b''
    digest = lambda data: hashlib.sha256(data).hexdigest()
    map_rows = [row for row in report['sites'] if row['kind'] == 'arena' and row['function'] == 'c_source_map_publish']
    record = {
        'case': name, 'variant': variant, 'returncode': completed.returncode,
        'expected_success': success, 'argv': argv,
        'compiler_sha256': digest(compiler.read_bytes()),
        'raw_sha256': digest(raw), 'object_bytes': len(object_bytes),
        'object_sha256': digest(object_bytes),
        'stdout_sha256': digest(without_census(completed.stdout)),
        'stderr_sha256': digest(without_census(completed.stderr)),
        'map_publication': map_rows, 'totals': report['totals'],
        'interpretation': 'cumulative allocation work; no timing, live-payload, RSS or speedup verdict',
    }
    records.append(record)
    print(json.dumps(record, sort_keys=True), flush=True)
Path(evidence, variant + '-records.json').write_text(json.dumps(records, indent=2, sort_keys=True) + '\n')
assert all((r['returncode'] == 0) == r['expected_success'] for r in records), 'unexpected compilation result'
if variant == 'candidate':
    baseline = json.loads(Path(evidence, 'baseline-records.json').read_text())
    comparisons = []
    for before, after in zip(baseline, records, strict=True):
        assert before['case'] == after['case']
        equal = all(before[key] == after[key] for key in ('returncode', 'object_bytes', 'object_sha256', 'stdout_sha256', 'stderr_sha256'))
        row = {'case': after['case'], 'output_and_diagnostics_identical': equal,
               'baseline_map': before['map_publication'], 'candidate_map': after['map_publication'],
               'baseline_totals': before['totals'], 'candidate_totals': after['totals']}
        comparisons.append(row)
    Path(evidence, 'comparison.json').write_text(json.dumps(comparisons, indent=2, sort_keys=True) + '\n')
    assert all(row['output_and_diagnostics_identical'] for row in comparisons), 'output or diagnostic divergence'
    print('WORK_ATTRIBUTION_COMPLETE cases=%d identical=1 timing=unmeasured rss=unmeasured' % len(comparisons), flush=True)
