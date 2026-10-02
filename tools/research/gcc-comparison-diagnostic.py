"""Frozen hosted diagnostic for #2274; not a performance acceptance harness."""
import hashlib
import json
import math
import os
import itertools
import signal
from pathlib import Path
import statistics
import subprocess
import threading
import time

ROOT = Path('gcc-comparison-evidence')
ROOT.mkdir(exist_ok=True)
SAMPLES = ROOT / 'samples.jsonl'
SAMPLES.write_text('')
BASE = ['-std=gnu11', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-g0', '-fno-pic', '-fno-pie']
TOOLS = {
    'buster-fast': ['./build/Release/ide', 'cc', '-target', 'x86_64-unknown-linux', '-march=baseline', '-O0', '-fregister-allocator=fast'] + BASE,
    'gcc-O0': ['gcc', '-march=x86-64', '-mtune=generic', '-O0'] + BASE,
    'gcc-O2': ['gcc', '-march=x86-64', '-mtune=generic', '-O2'] + BASE,
}

def capture(cmd):
    p = subprocess.run(cmd, text=True, capture_output=True, timeout=60)
    return {'argv': cmd, 'exit': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

metadata = {
    'diagnostic_only': True, 'compile_triplets': 12, 'runtime_triplets': 12,
    'baseline': capture(['git', 'rev-parse', 'HEAD', 'HEAD^{tree}']),
    'gcc': capture(['gcc', '--version']), 'clang': capture(['clang', '--version']),
    'cpu': capture(['lscpu']), 'kernel': capture(['uname', '-a']),
    'compiler_sha256': digest('build/Release/ide'),
    'compiler_bytes': Path('build/Release/ide').stat().st_size,
    'gcc_sha256': digest('/usr/bin/gcc'),
    'configs': TOOLS,
    'resource_contract': 'GNU time accounts waited descendants; maxrss is maximum child peak, not simultaneous process-tree RSS. Wall is monotonic wrapper-to-completion including identical GNU time launch overhead.',
}
(ROOT / 'metadata.json').write_text(json.dumps(metadata, indent=2))

def measured(cmd, key, artifact=None, expected_digest=None):
    stamp = ROOT / (key + '.time')
    out = ROOT / (key + '.stdout')
    err = ROOT / (key + '.stderr')
    if artifact is not None:
        artifact.unlink(missing_ok=True)
    start = time.perf_counter()
    timed_out = False
    with out.open('w') as fo, err.open('w') as fe:
        p = subprocess.Popen(['/usr/bin/time', '-f', '%U,%S,%M', '-o', str(stamp), '--'] + cmd,
                             stdout=fo, stderr=fe, start_new_session=True)
        def terminate_group():
            nonlocal timed_out
            timed_out = True
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        # wait(timeout=...) polls at exponentially spaced sleeps on POSIX,
        # quantizing small durations. A separate watchdog preserves blocking wait.
        watchdog = threading.Timer(120, terminate_group)
        watchdog.start()
        p.wait()
        watchdog.cancel()
        watchdog.join()
    elapsed = time.perf_counter() - start
    fields = stamp.read_text().strip().splitlines()[-1].split(',') if stamp.exists() and not timed_out else ['0','0','0']
    row = {'key': key, 'argv': cmd, 'exit': p.returncode, 'timed_out': timed_out, 'wall_s': elapsed,
           'cpu_s': float(fields[0]) + float(fields[1]), 'maxrss_kib': int(fields[2]),
           'stdout': out.read_text(), 'stderr_path': str(err)}
    if artifact is not None:
        row['artifact_valid'] = p.returncode == 0 and artifact.exists() and artifact.stat().st_size > 0
        row['output_sha256'] = digest(artifact) if row['artifact_valid'] else None
        if expected_digest is not None:
            row['artifact_valid'] = row['artifact_valid'] and row['output_sha256'] == expected_digest
    with SAMPLES.open('a') as f:
        f.write(json.dumps(row) + '\n')
    return row

prefix = 'extern int printf(const char *, ...);\n'
sources = {'tiny': ('int main(void) { return 0; }\n', None)}
mix = 'unsigned int x=1; for (unsigned int i=0;i<50000000u;i++) { x^=x<<13; x^=x>>17; x^=x<<5; }'
x = 1
for _ in range(50000000):
    x ^= (x << 13) & 0xffffffff
    x ^= x >> 17
    x ^= (x << 5) & 0xffffffff
sources['integer_mix'] = (prefix + 'int main(void) {' + mix + 'printf("%u\\n",x); return 0;}\n', str(x) + '\n')
array = list(range(1024))
for r in range(100000):
    array = [((a + r) * 1664525 + 1013904223) & 0xffffffff for a in array]
expected = sum(array) & 0xffffffff
sources['array_loop'] = (prefix + 'int main(void) { unsigned int a[1024]; for(unsigned int i=0;i<1024;i++)a[i]=i; for(unsigned int r=0;r<100000;r++)for(unsigned int i=0;i<1024;i++)a[i]=(a[i]+r)*1664525u+1013904223u; unsigned int s=0;for(unsigned int i=0;i<1024;i++)s+=a[i];printf("%u\\n",s);return 0;}\n', str(expected)+'\n')
funcs = ''.join('static __attribute__((noinline)) unsigned int f%d(unsigned int x){return x*%du+%du;}\n' % (i, 2*i+1, i) for i in range(256))
calls = ''.join('s+=f%d(r);' % i for i in range(256))
total = (sum(2*i+1 for i in range(256))*sum(range(100000)) + sum(range(256))*100000) & 0xffffffff
sources['many_functions'] = (prefix + funcs + 'int main(void){unsigned int s=0;for(unsigned int r=0;r<100000;r++){' + calls + '}printf("%u\\n",s);return 0;}\n', str(total)+'\n')
fp = 0.25
for _ in range(50000000):
    fp = fp * 0.999999 + 0.000001
sources['scalar_float'] = (prefix + 'volatile double seed=0.25; int main(void){double x=seed;for(unsigned int i=0;i<50000000;i++)x=x*0.999999+0.000001;printf("%.17g\\n",x);return 0;}\n', format(fp, '.17g')+'\n')
sources['basic_c_operations'] = (Path('tests/basic_c_operations.c').read_text(), '')

for round_id in range(12):
    measured(['/usr/bin/true'], 'wrapper-floor-'+str(round_id))

probes = {
 'unused-Werror': ('int main(void){int unused=7;return 0;}\n', ['-Wall','-Wextra','-Werror']),
 'unknown-warning': ('int main(void){return 0;}\n', ['-Wbuster-option-does-not-exist']),
 'depfile': ('int main(void){return 0;}\n', ['-MMD','-MF',str(ROOT/'probe.d')]),
 'asan': ('int main(void){return 0;}\n', ['-fsanitize=address']),
 'lto': ('int main(void){return 0;}\n', ['-flto']),
 'pgo': ('int main(void){return 0;}\n', ['-fprofile-generate']),
 'analyzer': ('int main(void){return 0;}\n', ['-fanalyzer']),
 'undeclared': ('int f(void){return missing_symbol;}\n', []),
 'missing-semicolon': ('int f(void){return 0}\n', []),
 'negative-array': ('int a[-1];\n', []),
 'duplicate-definition': ('int x=1; int x=2;\n', []),
 'explicit-error': ('#error deliberate-error\nint x;\n', []),
 'explicit-warning': ('#warning deliberate-warning\nint x;\n', []),
 'missing-include': ('#include "missing-comparison-header.h"\nint x;\n', []),
}
probe_rows = []
for name, (source, flags) in probes.items():
    path = ROOT/('probe-'+name+'.c')
    path.write_text(source)
    for tool in ('buster-fast','gcc-O2'):
        for operation in ('-fsyntax-only','-c'):
            obj = ROOT/('probe-'+name+'-'+tool+'.o')
            obj.unlink(missing_ok=True)
            row = capture(TOOLS[tool]+flags+[operation,str(path),'-o',str(obj)])
            row.update({'probe':name,'tool':tool,'operation':operation,'artifact_exists':obj.exists()})
            probe_rows.append(row)
(ROOT/'probes.json').write_text(json.dumps(probe_rows,indent=2))
orders = list(itertools.permutations(TOOLS))

coverage = []
for workload, (source, expected_stdout) in sources.items():
    src = ROOT / (workload + '.c')
    src.write_text(source)
    (ROOT / (workload + '.sha256')).write_text(digest(src) + '\n')
    if expected_stdout is not None:
        (ROOT / (workload + '.expected')).write_text(expected_stdout)
    for tool, base in TOOLS.items():
        obj = ROOT / (workload + '-' + tool + '.o')
        check = measured(base + ['-c', str(src), '-o', str(obj)], workload+'-'+tool+'-preflight', artifact=obj)
        accepted = check['artifact_valid']
        entry = {'workload': workload, 'tool': tool, 'accepted': accepted, 'input_sha256': digest(src)}
        if accepted:
            entry['object_bytes'] = obj.stat().st_size
            entry['output_sha256'] = digest(obj)
            entry['sections'] = capture(['size', '-A', str(obj)])
            text_bytes = 0
            found_text = False
            for line in entry['sections']['stdout'].splitlines():
                cells = line.split()
                if len(cells) >= 2 and (cells[0] == '.text' or cells[0].startswith('.text.')):
                    found_text = True
                    text_bytes += int(cells[1])
            entry['text_bytes'] = text_bytes if found_text and entry['sections']['exit']==0 else None
        coverage.append(entry)
        if not accepted:
            continue
        exe = ROOT / (workload + '-' + tool)
        # Identical GCC driver/linker/CRT for all produced objects; not end-to-end linker timing.
        link = capture(['gcc', '-no-pie', str(obj), '-o', str(exe)])
        entry['link'] = link
        if link['exit'] == 0:
            executed = capture([str(exe.resolve())])
            entry['validation'] = executed
            entry['correct'] = executed['exit'] == 0 and (expected_stdout is None or executed['stdout'] == expected_stdout)
        else:
            entry['correct'] = False
    (ROOT / 'coverage.json').write_text(json.dumps(coverage, indent=2))
    for pair in range(12):
        order = orders[pair % len(orders)]
        for tool in order:
            entry = next(v for v in coverage if v['tool']==tool and v['workload']==workload)
            if not entry.get('correct'):
                continue
            obj = ROOT / (workload + '-' + tool + '.o')
            measured(TOOLS[tool] + ['-c', str(src), '-o', str(obj)], workload+'-'+tool+'-compile-'+str(pair), artifact=obj, expected_digest=entry['output_sha256'])
    if workload not in ('tiny', 'basic_c_operations'):
        for pair in range(12):
            order = orders[pair % len(orders)]
            for tool in order:
                entry = next(v for v in coverage if v['tool']==tool and v['workload']==workload)
                if entry.get('correct'):
                    row = measured([str((ROOT/(workload+'-'+tool)).resolve())], workload+'-'+tool+'-runtime-'+str(pair))
                    if row['exit'] or row['stdout'] != expected_stdout:
                        raise RuntimeError('runtime oracle disagreement: '+row['key'])

rows = [json.loads(line) for line in SAMPLES.read_text().splitlines()]
summary = []
for workload in sources:
    for tool in TOOLS:
        entry = dict(next(v for v in coverage if v['tool']==tool and v['workload']==workload))
        for phase in ('compile','runtime'):
            group = [r for r in rows if r['key'].startswith(workload+'-'+tool+'-'+phase+'-')]
            valid = len(group)==12 and all(r['exit']==0 and not r['timed_out'] and (phase!='compile' or r['artifact_valid']) for r in group)
            entry[phase+'_valid'] = valid
            if valid:
                entry[phase] = {'n':len(group), **{metric:{'median':statistics.median([r[metric] for r in group]),'min':min(r[metric] for r in group),'max':max(r[metric] for r in group)} for metric in ('wall_s','cpu_s','maxrss_kib')}}
        summary.append(entry)
(ROOT / 'summary.json').write_text(json.dumps(summary, indent=2))
print('GCC_COMPARISON_COMPLETE', flush=True)
print(json.dumps(summary, indent=2), flush=True)
