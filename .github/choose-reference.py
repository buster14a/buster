#!/usr/bin/env python3
"""Bounded correctness controls; no performance measurement."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ide = str(Path(sys.argv[1]).resolve())
fixture = Path(sys.argv[2]).read_text()
out = Path(sys.argv[3]).resolve()
out.mkdir(parents=True, exist_ok=True)
cases = re.findall(r'\{S8\("([^"\n]+)"\), (\d+), (\d+), (\d+), (\d+)\}', fixture)
assert len(cases) == 20
prelude = ('static volatile unsigned hits;\n'
           'static int mark(void) { hits += 1; return 7; }\n'
           'static int other(void) { hits += 100; return 9; }\n'
           'static int identity(int value) { return value; }\n')
functions = []
checks = []
for index, (expression, calls, stores, effects, value) in enumerate(cases):
    value, effects = int(value), int(effects)
    bodies = [f'int value = ({expression}); return value;',
              f'return identity({expression});', f'return ({expression});',
              f'int value = {value + 1}; if (({expression}) == {value}) value = {value}; return value;',
              f'(void)({expression}); return {value};',
              f'return ({{ int inner = ({expression}); inner; }});']
    for context, body in enumerate(bodies):
        probe = 6 * index + context
        functions.append(f'int probe_{probe}(int flag) {{ {body} }}\n')
        checks.append(f'hits = 0; value = probe_{probe}(1); failed |= value != {value}; failed |= hits != {effects}u;\n')
program = prelude + ''.join(functions) + 'int main(void) { int failed = 0; int value;\n' + ''.join(checks) + 'return failed; }\n'
minimal = ('static volatile unsigned hits;\nstatic int mark(void) { hits += 1; return 7; }\n'
           'int main(void) { int value = __builtin_choose_expr(1, 7, mark()); return (value != 7) | ((hits != 0) << 1); }\n')
sources = {'family120': program, 'reduced': minimal}
common = ['-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char']
configs = [(name + opt, command + [opt])
           for name, command in [('clang', ['clang', '--target=x86_64-linux-gnu']), ('gcc', ['gcc', '-m64'])]
           for opt in ['-O0', '-O2']]
configs += [(name + '_asan_ubsan', command + ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
            for name, command in [('clang', ['clang', '--target=x86_64-linux-gnu']), ('gcc', ['gcc', '-m64'])]]
configs += [('buster_' + mode + '_' + form, [ide, 'cc', '-target', 'x86_64-linux', '-fregister-allocator=' + mode,
             '-ffrontend-ssa' if form == 'ssa' else '-fno-frontend-ssa', '-fverify-codegen'])
            for mode in ['none', 'mir-stack', 'fast', 'quality'] for form in ['ssa', 'memory']]
records = []


def run(command, name):
    try:
        result = subprocess.run(command, text=True, capture_output=True, timeout=60)
        data = {'argv': command, 'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
    except subprocess.TimeoutExpired as error:
        data = {'argv': command, 'returncode': 'timeout', 'stdout': str(error.stdout), 'stderr': str(error.stderr)}
    (out / (name + '.json')).write_text(json.dumps(data, indent=2) + '\n')
    return data['returncode']


for compiler in ['clang', 'gcc']:
    run([compiler, '--version'], compiler + '-version')
for name, source in sources.items():
    path = out / (name + '.c')
    path.write_text(source)
    for config, prefix in configs:
        binary = out / (name + '-' + config)
        status = run(prefix + common + [str(path), '-o', str(binary)], name + '-' + config + '-compile')
        execution = run([str(binary)], name + '-' + config + '-run') if status == 0 else None
        record = {'name': name, 'config': config, 'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
                  'compile': status, 'run': execution}
        records.append(record)
        print('CHOOSE_RESULT', json.dumps(record), flush=True)
(out / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
print('CHOOSE_PASS_COUNT', sum(r['compile'] == 0 and r['run'] == 0 for r in records), 'of', len(records), flush=True)
# This is a capture on both baseline and candidate. Acceptance is explicit in
# the workflow rather than turning expected baseline discrepancies into passes.
