"""Complete the historical read-only replay; no compiler or test execution."""
from pathlib import Path
import csv
import hashlib
import json
import os
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
PHASES = ('configure', 'build', 'test', 'post_test', 'self_host', 'evidence')

def main():
    if sys.flags.optimize:
        raise ValueError('Run without -O: the preserved historical helpers use assertions')
    env = dict(os.environ)
    env.pop('PYTHONOPTIMIZE', None)
    for script in ('verify_artifacts.py', 'analyze.py'):
        subprocess.run([sys.executable, '-B', str(ROOT / script)], check=True, env=env,
                       stdout=subprocess.DEVNULL, timeout=120)
    out = ROOT / 'derived'
    lanes = json.loads((out / 'lane_phases.json').read_text())
    totals = []
    for run in sorted({r['lane'].split(':')[0] for r in lanes}):
        selected = [r for r in lanes if r['lane'].startswith(run + ':')]
        seconds = {phase: sum(r[phase] for r in selected) for phase in PHASES}
        all_six = sum(seconds.values())
        primary = sum(seconds[p] for p in PHASES[:3])
        for phase in PHASES:
            value = seconds[phase]
            totals.append(dict(run_id=run, phase=phase, seconds=value, minutes=value / 60,
                               percent_all_six_phases=value / all_six * 100,
                               percent_primary_three=value / primary * 100 if phase in PHASES[:3] else ''))
    (out / 'phase_totals.json').write_text(json.dumps(totals, indent=2))
    with (out / 'phase_totals.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(totals[0]))
        writer.writeheader()
        writer.writerows(totals)
    with zipfile.ZipFile(ROOT / 'raw/clang-analyzer-36996168725-1.zip') as z:
        for name in ('aggregate.log', 'full.log', 'comparison-selection.txt'):
            (out / name).write_bytes(z.read('_temp/buster-analyzer/' + name))
    manifest = json.loads((ROOT / 'expected-original.json').read_text())
    failed = []
    for name, expected in manifest.items():
        p = ROOT / name
        if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != expected:
            failed.append(name)
    if failed:
        raise ValueError('Original-content mismatch: ' + ', '.join(failed))
    print(f'Verified all {len(manifest)} original bundle members byte-for-byte')

if __name__ == '__main__':
    main()
