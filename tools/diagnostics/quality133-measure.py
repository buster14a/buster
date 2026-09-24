#!/usr/bin/env python3
"""Temporary #133 hosted screening, not a production tool or acceptance policy."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

BASE = '2e942e80a87666409cf29d3e24a68d322b9e71fd'
CANDIDATE = '3704003f742b5c8a26e8f60e86bfe1663d2f4f99'
EVIDENCE = Path(os.environ['RUNNER_TEMP']) / 'quality133-screening'
EVIDENCE.mkdir()
(EVIDENCE / 'bin').mkdir()
(EVIDENCE / 'builds').mkdir()

def run(args, log):
    print('COMMAND', json.dumps([str(arg) for arg in args]), flush=True)
    with open(log, 'w') as stream:
        subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT, check=True)

def sha(data):
    return hashlib.sha256(data).hexdigest()

for label, command in [('clang', ['clang', '--version']), ('cmake', ['cmake', '--version']), ('ninja', ['ninja', '--version']), ('uname', ['uname', '-a']), ('cpu', ['lscpu'])]:
    run(command, EVIDENCE / (label + '.txt'))
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip() == CANDIDATE
assert subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], text=True).strip() == '5b0b268022bc8c6b17b49df239e6b7a0df415376'
driver = EVIDENCE / 'build-driver'
run(['clang', '-Isrc', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', 'build.c', '-o', str(driver)], EVIDENCE / 'driver-build.log')
run([str(driver), 'bench_throughput', 'help'], EVIDENCE / 'harness-build.log')
harness = EVIDENCE / 'bin' / 'throughput'
shutil.copy2('build/throughput-tools/throughput', harness)
records = []
for label, revision in [('base', BASE), ('base-rebuild', BASE), ('simple', BASE), ('projected', CANDIDATE)]:
    subprocess.run(['git', 'checkout', '--detach', revision], check=True)
    if label == 'simple':
        path = Path('src/buster/lib/compiler/codegen/machine_schedule.c')
        source = path.read_text()
        old = '            growth -= (role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) && demanded;\n            growth += (role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) && !demanded;'
        new = '            if (demanded)\n            {\n                growth -= role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE;\n            }\n            else\n            {\n                growth += role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE;\n            }'
        assert source.count(old) == 1
        path.write_text(source.replace(old, new))
        (EVIDENCE / 'simple.patch').write_bytes(subprocess.check_output(['git', 'diff', '--binary']))
    out = EVIDENCE / 'builds' / label
    out.mkdir()
    patch = subprocess.check_output(['git', 'diff', '--binary'])
    (out / 'source.patch').write_bytes(patch)
    config = [str(driver), 'generate', '--ci', '--cc', 'clang', '--', '-DBUSTER_INCLUDE_TESTS=OFF', '-DBUSTER_UNITY_BUILD=OFF', '-DBUSTER_BENCH_ALLOCATIONS=OFF', '-DCMAKE_LINKER_TYPE=DEFAULT']
    build = [str(driver), 'build', '--config', 'Release', '-t', 'ide', '--', '-j2']
    run(config, out / 'configure.log')
    run(build, out / 'build.log')
    binary = Path('build/Release/ide').read_bytes()
    frozen = EVIDENCE / 'bin' / label
    frozen.write_bytes(binary)
    frozen.chmod(0o500)
    for name in ['CMakeCache.txt', 'compile_commands.json']:
        shutil.copy2(Path('build') / name, out / name)
    run(['objdump', '-h', str(frozen)], out / 'sections.txt')
    run(['nm', '-a', str(frozen)], out / 'symbols.txt')
    symbols = (out / 'symbols.txt').read_text()
    for forbidden in ['machine_schedule_queue_growth_descriptor', 'machine_schedule_function_trace', 'machine_schedule_trace_write']:
        assert forbidden not in symbols
    records.append({'label': label, 'revision': revision, 'source_patch_sha256': sha(patch), 'binary_sha256': sha(binary), 'binary_bytes': len(binary), 'cwd': os.getcwd(), 'configure': config, 'build': build})
    (EVIDENCE / 'build-identities.json').write_text(json.dumps(records, indent=2) + '\n')
    if label == 'simple':
        subprocess.run(['git', 'restore', 'src/buster/lib/compiler/codegen/machine_schedule.c'], check=True)
print(json.dumps(records, indent=2), flush=True)
# Every compiler has finished building before any measured launch.
for name, left, right, right_id in [('same-root-control', 'base', 'base-rebuild', BASE), ('simple', 'base', 'simple', BASE + '+simple.patch'), ('projected', 'base', 'projected', CANDIDATE)]:
    argv = [str(harness), 'run', '--baseline', str(EVIDENCE / 'bin' / left), '--candidate', str(EVIDENCE / 'bin' / right), '--baseline-id', BASE, '--candidate-id', right_id, '--output', str(EVIDENCE / name), '--profile', 'ci', '--mode', 'quality', '--pairs', '20', '--warmups', '2', '--timeout', '120', '--cpu', 'auto', '--require-identical-output', '--no-guard']
    run(argv, EVIDENCE / (name + '.log'))
print('QUALITY133_HOSTED_SCREENING_COMPLETE', flush=True)
