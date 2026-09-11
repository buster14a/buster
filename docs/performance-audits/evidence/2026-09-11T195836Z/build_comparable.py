#!/usr/bin/env python3
"""Compile the changed machine.h dependency closure with frozen baseline flags."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('baseline')
parser.add_argument('candidate')
parser.add_argument('output')
args = parser.parse_args()
base = Path(args.baseline).resolve()
candidate = Path(args.candidate).resolve()
out = Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=True)
commands = json.loads((base / 'build/compile_commands.json').read_text())
entries = {row['output']: row for row in commands if '/Release/' in row['output']}
raw_deps = subprocess.check_output(['ninja', '-f', 'build-Release.ninja', '-t', 'deps'], cwd=base / 'build', text=True)
dependents = []
header = str(base / 'src/buster/lib/compiler/codegen/machine.h')
for block in raw_deps.split('\n\n'):
    lines = block.splitlines()
    if lines and header in [line.strip() for line in lines[1:]]:
        obj = lines[0].split(': #deps ', 1)[0]
        if obj in entries:
            dependents.append(obj)
assert any(obj.endswith('/codegen/machine.c.o') for obj in dependents)
assert any(obj.endswith('/codegen/codegen.c.o') for obj in dependents)
assert any(obj.endswith('/codegen/bootstrap_trace.c.o') for obj in dependents)
replacements = {}
compiles = []
for obj in sorted(dependents):
    entry = entries[obj]
    source = str(candidate / Path(entry['file']).relative_to(base))
    command = [part.replace('\\"', '"') for part in shlex.split(entry['command'])]
    command[1:1] = ['-I' + str(candidate / 'src')]
    command = [source if part == entry['file'] else part for part in command]
    destination = out / obj
    destination.parent.mkdir(parents=True, exist_ok=True)
    command[command.index('-o') + 1] = str(destination)
    subprocess.run(command, cwd=entry['directory'], check=True)
    replacements[obj] = str(destination)
    compiles.append({'source': source, 'output': str(destination), 'command': command})
    print(obj, flush=True)
link_commands = subprocess.check_output(['ninja', '-f', 'build-Release.ninja', '-t', 'commands', 'Release/ide'], cwd=base / 'build', text=True).splitlines()
link = shlex.split(next(line for line in reversed(link_commands) if ' -o Release/ide' in line))
if link[:2] == [':', '&&']:
    link = link[2:]
if link[-2:] == ['&&', ':']:
    link = link[:-2]
link[link.index('-o') + 1] = str(out / 'ide')
link = [replacements.get(part, part) for part in link]
subprocess.run(link, cwd=base / 'build', check=True)
(out / 'build.json').write_text(json.dumps({'baseline': str(base), 'candidate': str(candidate), 'dependency_header': header,
    'compile': compiles, 'link': link}, indent=2) + '\n')
