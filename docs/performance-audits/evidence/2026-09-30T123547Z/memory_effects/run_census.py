"""Branch-only untimed research orchestration, never a production gate."""
import csv
import json
import pathlib
import subprocess

root = pathlib.Path.cwd()
out = root / "memory-evidence"
out.mkdir(exist_ok=True)
driver = root / "src/buster/lib/compiler/driver/driver.c"
original_source = driver.read_text()
source = original_source
include = '#include <buster/lib/compiler/driver/driver.h>\n'
assert source.count(include) == 1
source = source.replace(include, include + '#include "../../../../../tools/research/memory_effects/memory_census.h"\n')
seam = '    result.local_promotion = module->local_promotion;\n'
assert source.count(seam) == 1
source = source.replace(seam, '    if (validation.error == IR_VALIDATION_NONE)\n    {\n        buster_research_memory_census(lowered.program, module);\n    }\n' + seam)
driver.write_text(source)
subprocess.run(['clang', '-Isrc', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', 'build.c', '-o', '/tmp/buster-memory-build'], check=True)
subprocess.run(['/tmp/buster-memory-build', 'generate', '--cc', 'clang', '--linker', 'DEFAULT', '--no-include-tests'], check=True)
subprocess.run(['/tmp/buster-memory-build', 'build', '--config', 'Release', '-t', 'ide'], check=True)
driver.write_text(original_source)
inputs = [
    'tools/research/memory_effects/memory_cases.c',
    'tests/basic_c_operations.c',
    'tests/basic_c_local_aggregate_copy.c',
    'tests/basic_c_conditional_array_branch.c',
    'tests/basic_c_pointer_to_array_place.c',
    'src/buster/apps/ide/ide.c',
]
fields = ['row_visits', 'operand_visits', 'values', 'address_steps', 'loads', 'stores',
          'eligible_loads', 'eligible_stores', 'unknown_accesses', 'barriers',
          'comparisons', 'invalidations', 'flushed_slots', 'evictions', 'RLE', 'SFL', 'DSE', 'scratch_bytes', 'cfg_target_visits']
commands = []
summaries = []
for subject, path in enumerate(inputs):
    for mode in ['default', 'memory']:
        label = f'{subject}-{mode}'
        command = ['build/Release/ide', 'cc', '-Isrc', '-Ibuild/generated', '-DBUSTER_UNITY_BUILD=1',
                   '-DBUSTER_INCLUDE_TESTS=0', '-target', 'x86_64-unknown-linux', '-march=baseline',
                   '-g0', '-fregister-allocator=fast', '-fverify-codegen', '-fno-machine-fallback', '-c', path, '-o', str(out / (label + '.o'))]
        if mode == 'memory':
            command.append('-fno-frontend-ssa')
        with (out / (label + '.stdout')).open('w') as stdout, (out / (label + '.stderr')).open('w') as stderr:
            run = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=240)
        commands.append({'subject': path, 'mode': mode, 'argv': command, 'returncode': run.returncode})
        models = {0: [], 1: []}
        for line in (out / (label + '.stderr')).read_text().splitlines():
            if line.startswith('MEMORY_CENSUS,'):
                row = next(csv.reader([line]))
                assert len(row) == 22, (len(row), row)
                models[int(row[2])].append(dict(zip(fields, map(int, row[3:]))))
        for model, rows in models.items():
            summary = {'subject': path, 'mode': mode, 'model': model, 'exit': run.returncode, 'functions': len(rows)}
            for field in fields:
                summary[field] = max((r[field] for r in rows), default=0) if field == 'scratch_bytes' else sum(r[field] for r in rows)
            summaries.append(summary)
            print(json.dumps(summary), flush=True)
    (out / 'commands.json').write_text(json.dumps(commands, indent=2))
    (out / 'summary.json').write_text(json.dumps(summaries, indent=2))
assert all(row['returncode'] == 0 for row in commands), 'Retained failed compilation; inspect commands.json and stderr'
