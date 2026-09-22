#!/usr/bin/env python3
"""Build + differential checks only. No timing."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
os.chdir(root)
subprocess.run(['python3', 'generate_unroll.py'], check=True)
common = ['gcc', '-std=c11', '-g', '-fno-omit-frame-pointer', '-fno-tree-vectorize',
          '-fno-tree-slp-vectorize', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char',
          '-Wall', '-Wextra', '-Werror', '-Wpedantic']
commands = [common + ['-O3', 'switch_overlap.c', '-o', 'switch_overlap'],
            common + ['-O1', '-fsanitize=address,undefined', 'switch_overlap.c', '-o', 'switch_overlap_sanitize']]
results = []
for command in commands:
    subprocess.run(command, check=True)
    binary = command[-1]
    env = dict(os.environ)
    if 'sanitize' in binary:
        env['ASAN_OPTIONS'] = 'detect_leaks=0'
    done = subprocess.run([f'./{binary}', '--test'], env=env, text=True, capture_output=True, check=False)
    result = {'build_command': command, 'test_command': [f'./{binary}', '--test'],
              'test_environment': {'ASAN_OPTIONS': env.get('ASAN_OPTIONS', '')},
              'returncode': done.returncode, 'stdout': done.stdout, 'stderr': done.stderr,
              'binary_sha256': hashlib.sha256(Path(binary).read_bytes()).hexdigest()}
    results.append(result)
    (root / 'correctness.json').write_text(json.dumps(results, indent=2) + '\n')
    done.check_returncode()
with open('assembly.txt', 'w') as out:
    subprocess.run(['objdump', '-d', '-Mintel', 'switch_overlap'], stdout=out, check=True)
manifest = {'scope': 'isolated correctness builds, not trusted compiler acceptance',
            'compiler': subprocess.check_output(['gcc', '--version'], text=True),
            'commands': commands,
            'source_sha256': {name: hashlib.sha256(Path(name).read_bytes()).hexdigest()
                              for name in ('switch_overlap.c', 'unroll.inc', 'generate_unroll.py', 'run_pairs.py', 'build.py')}}
Path('build_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({'builds': len(results), 'tests': [json.loads(r['stdout']) for r in results], 'timed_runs': 0}))
