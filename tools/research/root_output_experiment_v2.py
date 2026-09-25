#!/usr/bin/env python3
"""Bounded second run, retaining the first experiment exactly.

The 65,537th macro invocation exceeds Buster's existing 65,536 expansion
limit. Both first-run variants rejected it identically; that was an incorrect
fixture expectation, not a prototype regression. --clean additionally removes
all disposable observer instrumentation from both compiler variants.
"""
from pathlib import Path
import hashlib
import subprocess
import sys

args = sys.argv[1:]
clean = '--clean' in args
if clean:
    args.remove('--clean')
assert '--out' in args
out = Path(args[args.index('--out') + 1]).resolve()
out.mkdir(parents=True, exist_ok=True)
source_path = Path(__file__).with_name('root_output_experiment.py')
raw = source_path.read_bytes()
assert hashlib.sha256(raw).hexdigest() == '5e55387722ca65391f14b92a0272b3c04c8ae8d593d0df59558ba374668bec93'
s = raw.decode()
old = 'cases.append((name, n <= 4097, 0))'
assert s.count(old) == 1
s = s.replace(old, 'cases.append((name, n <= 4097, 1 if n == 65537 else 0))')
if clean:
    old = 'observer = instrument(work/CFILE, variant)'
    assert s.count(old) == 1
    s = s.replace(old, 'observer = ""  # Exact clean baseline/candidate; no observer applied.')
    old = "instrumentation='invocation-local counters; reporting enabled only for named census launches'"
    assert s.count(old) == 1
    s = s.replace(old, "instrumentation='NONE: exact clean baseline and prototype sources'")
path = out / 'executed-experiment-v2.py'
path.write_text(s)
(out / 'executed-experiment-v2.sha256').write_text(hashlib.sha256(s.encode()).hexdigest() + '\n')
print('VALIDATION_MODE=' + ('clean' if clean else 'census'), flush=True)
raise SystemExit(subprocess.run([sys.executable, str(path), *args]).returncode)
