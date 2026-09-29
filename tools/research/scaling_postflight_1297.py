#!/usr/bin/env python3
"""Reuse exact diagnostic binaries; never interpret hosted samples as acceptance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import statistics
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument('--prior', type=Path, required=True)
ap.add_argument('--out', type=Path, required=True)
a = ap.parse_args()
prior = a.prior.resolve()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
metadata = json.loads((prior/'metadata.json').read_text())
plain, census = prior/'plain-ide', prior/'census-ide'
for name, path in [('plain', plain), ('census', census)]:
    assert hashlib.sha256(path.read_bytes()).hexdigest() == metadata['binary_sha256'][name]
    path.chmod(0o755)
old_flags = set(re.search(r'^Flags:\s+(.*)$', metadata['lscpu'], re.M).group(1).split())
lscpu = subprocess.check_output(['lscpu'], text=True)
new_flags = set(re.search(r'^Flags:\s+(.*)$', lscpu, re.M).group(1).split())
assert old_flags <= new_flags, ('native build host flags unavailable', sorted(old_flags-new_flags))
meta = {'prior': metadata, 'lscpu': lscpu, 'uname': list(os.uname()),
        'harness_revision': subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        'timing_class': 'uninstrumented hosted diagnostic, not 9700X acceptance',
        'original_wall_screen': 'invalid for fine latency: Python timeout wait polling quantizes completion; retained, not fitted'}
(out/'metadata.json').write_text(json.dumps(meta, indent=2))
timer = out/'native-clock'
subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','tools/research/scaling_clock_1297.c','-o',str(timer)],check=True)
# Timer checks are recorded, outside compiler observations.
for i, command in enumerate([['/bin/true'], ['/bin/sleep','0.1'], ['/bin/false']]):
    status = subprocess.run([str(timer),str(out/f'timer-check-{i}.json'),*command]).returncode
    assert status == (1 if i == 2 else 0)
    print('TIMER_CHECK', command, (out/f'timer-check-{i}.json').read_text().strip(),flush=True)

# Preserve pinned source for independent local inspection without a network clone.
source_out = out/'source'
source_out.mkdir()
for name in ['c.h','c_internal.h','c.c','c_parse.c','c_gen.c']:
    path = Path('src/buster/lib/compiler/frontend/c')/name
    if path.exists(): shutil.copy2(path,source_out/name)
for name in ['results.jsonl','builds.txt']:
    path = Path('docs/performance-audits/evidence/2026-09-26T161851Z')/name
    if path.exists(): shutil.copy2(path,out/('historical-'+name))

cases = json.loads((prior/'manifest.json').read_text())
counts = [json.loads(line) for line in (prior/'counts.jsonl').read_text().splitlines()]
assert len(counts) == len(cases), 'do not time an incomplete census'
shutil.copy2(prior/'counts.jsonl',out/'counts.jsonl')
inputs = out/'inputs'
inputs.mkdir()
for case in cases:
    source = prior/'inputs'/Path(case['path']).name
    assert hashlib.sha256(source.read_bytes()).hexdigest() == case['sha256']
    shutil.copy2(source,inputs/source.name)
    case['path'] = str(inputs/source.name)
empty = inputs/'minimal-valid.c'
empty.write_text('extern unsigned startup_control;\n')
cases.append({'name':'minimal-valid','family':'startup-control','path':str(empty)})
object_path = out/'subject.o'

def invoke(binary, path, stem, extra=()):
    command = [str(binary),'cc','-g0','-c','-march=baseline',*extra,str(path),'-o',str(object_path)]
    timing = out/(stem+'.json')
    with (out/(stem+'.stdout')).open('w') as stdout, (out/(stem+'.stderr')).open('w') as stderr:
        code = subprocess.run([str(timer),str(timing),*command],stdout=stdout,stderr=stderr).returncode
    result = json.loads(timing.read_text())
    result['command'] = command
    result['launcher_exit'] = code
    if code == 0:
        result['object_sha256'] = hashlib.sha256(object_path.read_bytes()).hexdigest()
        result['object_bytes'] = object_path.stat().st_size
    timing.write_text(json.dumps(result))
    return result

# Independent real-source census after reconstructing generated configuration
# via the same existing build driver. This does NOT rebuild either timed binary.
driver = prior/'build-driver'
driver.chmod(0o755)
command = [str(driver),'generate','--cc','clang','--config','Release','--linker','DEFAULT','--','-DBUSTER_DEBUG_INFO=OFF']
with (out/'configure.log').open('w') as log:
    generated = subprocess.run(command,stdout=log,stderr=subprocess.STDOUT).returncode
print('GENERATE_FOR_REAL_SOURCE',generated,flush=True)
pattern = re.compile(r'SCALE_PREDECL start=(\d+) end=(\d+) entities=(\d+) visits=(\d+) locals=(\d+) selected=(\d+) entity_size=(\d+)')
real_records = []
if generated == 0:
    source = Path('src/buster/apps/ide/ide.c')
    extra = ['-Isrc','-Ibuild/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0']
    for label,binary in [('plain',plain),('census',census)]:
        record = invoke(binary,source,'real-unity-'+label,extra)
        rows = pattern.findall((out/('real-unity-'+label+'.stderr')).read_text())
        record.update(label=label,calls=len(rows),visits=sum(int(r[3]) for r in rows),selected=sum(int(r[5]) for r in rows))
        real_records.append(record)
        print('REAL_UNITY',json.dumps(record),flush=True)
    if all(r['exit_code'] == 0 for r in real_records):
        assert real_records[0]['object_sha256'] == real_records[1]['object_sha256']
(out/'real-source.json').write_text(json.dumps(real_records,indent=2))

# Fixed five randomized repetitions. Native CLOCK_MONOTONIC/wait4 owns wall/RSS;
# Python waits outside that clock, so timeout polling cannot quantize these data.
for i,case in enumerate(cases):
    record = invoke(plain,Path(case['path']),f'warmup-{i:03}')
    assert record['exit_code'] == 0 and record['launcher_exit'] == 0, record
schedule = [(rep,case) for rep in range(5) for case in cases]
random.Random(1297002).shuffle(schedule)
results = []
for i,(rep,case) in enumerate(schedule):
    record = invoke(plain,Path(case['path']),f'timed-{i:04}')
    record.update(name=case['name'],repetition=rep)
    with (out/'timings.jsonl').open('a') as f: f.write(json.dumps(record)+'\n')
    assert record['exit_code'] == 0 and record['launcher_exit'] == 0, record
    results.append(record)
for case in cases:
    rows = [r for r in results if r['name'] == case['name']]
    print('SCREEN',case['name'],'wall_median',statistics.median(r['wall_seconds'] for r in rows),
          'wall_range',min(r['wall_seconds'] for r in rows),max(r['wall_seconds'] for r in rows),
          'rss_kib_median',statistics.median(r['peak_rss_kib'] for r in rows),
          'minor_faults_median',statistics.median(r['minor_faults'] for r in rows),flush=True)
print('POSTFLIGHT_COMPLETE',len(results),'native-clock samples; no accepted speedup',flush=True)
