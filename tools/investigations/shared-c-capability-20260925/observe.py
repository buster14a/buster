#!/usr/bin/env python3
"""Functional evidence only. Exit status does not substitute for row results."""
import argparse
import importlib.util
import itertools
import json
import pathlib
import subprocess
import hashlib

parser = argparse.ArgumentParser()
parser.add_argument('compiler', type=pathlib.Path)
parser.add_argument('output', type=pathlib.Path)
parser.add_argument('--references', action='store_true')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('prepare', pathlib.Path(__file__).with_name('prepare.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = args.output / 'family.c'
source.write_text(module.make_source())
common = ['-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-g0']
profiles = []
if args.references:
    for compiler in ['clang', 'gcc']:
        for label, flags in [('O0', ['-O0']), ('O2', ['-O2']), ('sanitized', ['-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]:
            profiles.append((compiler + '-' + label, [compiler] + common + flags))
for allocator, form, optimization in itertools.product(['none', 'mir-stack', 'fast', 'quality'], ['-ffrontend-ssa', '-fno-frontend-ssa'], ['-O0', '-O2']):
    cmd = [str(args.compiler.resolve()), 'cc', '-target', 'x86_64-linux'] + common + [optimization, '-fverify-codegen', '-fregister-allocator=' + allocator, form]
    if allocator != 'none':
        cmd += ['-fno-machine-fallback']
    profiles.append((allocator + form + optimization, cmd))
rows = []
for label, command in profiles:
    out = args.output / label
    out.mkdir(exist_ok=True)
    argv = command + [str(source.resolve()), '-o', str((out / 'program').resolve())]
    row = {'profile': label, 'argv': argv, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'checks_per_program': len(module.CASES) * 6, 'compile_exit': None, 'run_exit': None}
    try:
        compile_result = subprocess.run(argv, capture_output=True, timeout=120)
        row['compile_exit'] = compile_result.returncode
        (out / 'compile.stdout').write_bytes(compile_result.stdout)
        (out / 'compile.stderr').write_bytes(compile_result.stderr)
        if compile_result.returncode == 0:
            run = subprocess.run([str((out / 'program').resolve())], capture_output=True, timeout=30)
            row['run_exit'] = run.returncode
            (out / 'run.stdout').write_bytes(run.stdout)
            (out / 'run.stderr').write_bytes(run.stderr)
    except subprocess.TimeoutExpired as error:
        row['timeout'] = error.timeout
    (out / 'result.json').write_text(json.dumps(row, indent=2))
    rows.append(row)
    print(json.dumps(row), flush=True)
(args.output / 'results.json').write_text(json.dumps(rows, indent=2))
