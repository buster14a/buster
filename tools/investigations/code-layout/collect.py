#!/usr/bin/env python3
"""Collect actual Buster output on the pinned, untouched production checkout."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--production', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
production = pathlib.Path(args.production).resolve()
output = pathlib.Path(args.output).resolve()
diagnostic = pathlib.Path(__file__).resolve().parent
output.mkdir(parents=True, exist_ok=True)
revision = '8f67df736f13d4edc055110a7a6d619a00a22eaf'
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=production, text=True).strip() == revision
commands = []

def run(command, name, cwd=production):
    with (output / (name + '.log')).open('w') as log:
        result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
    commands.append({'command': [str(x) for x in command], 'cwd': str(cwd), 'exit': result.returncode, 'log': name + '.log'})
    (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    if result.returncode:
        print((output / (name + '.log')).read_text())
        raise RuntimeError('command failed: ' + name)

sources = (diagnostic / 'fixture_sources.txt').read_text().splitlines()
fixture_objects = []
for index, source in enumerate(sources):
    data = (production / source).read_bytes()
    assert not re.search(rb'\b(?:__asm__|__asm|asm)\b', data), source
    object_path = output / (pathlib.Path(source).stem + '.o')
    run(['build/Release/ide', 'cc', '-target', 'x86_64-unknown-linux-gnu', '-fregister-allocator=fast', '-fno-machine-fallback', '-fverify-codegen', '-g', '-c', source, '-o', str(object_path)], 'fixture-' + str(index))
    fixture_objects.append(str(object_path))
(output / 'fixture-source-identities.json').write_text(json.dumps([
    {'source': source, 'sha256': hashlib.sha256((production / source).read_bytes()).hexdigest(), 'inline_asm_marker': False} for source in sources], indent=2) + '\n')
run([sys.executable, str(diagnostic / 'layout_census.py'), '--revision', revision, '--output', str(output / 'fixtures.json'), '--proven-no-inline-asm', *fixture_objects], 'fixture-census')
run(['build/Release/ide', 'cc', '-target', 'x86_64-unknown-linux-gnu', '-Isrc', '-Ibuild/generated', '-DBUSTER_UNITY_BUILD=1', '-DBUSTER_INCLUDE_TESTS=0', '-g0', '-v', '-fregister-allocator=fast', '-fno-machine-fallback', '-fverify-codegen', '-c', 'src/buster/apps/ide/ide.c', '-o', str(output / 'unity.o')], 'unity-compile')
run([sys.executable, str(diagnostic / 'layout_census.py'), '--revision', revision, '--output', str(output / 'unity.json'), str(output / 'unity.o')], 'unity-census')
for name in ('fixtures', 'unity'):
    report = json.loads((output / (name + '.json')).read_text())
    print(name + ': ' + json.dumps(report['summary'], sort_keys=True))
