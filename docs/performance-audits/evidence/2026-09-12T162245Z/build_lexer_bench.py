"""Isolate c_source.c: reuse the configured Release objects and compiler flags."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path.cwd()
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, TMPDIR=str(out))
source = (root / 'src/buster/apps/ide/ide.c').read_text()
begin = source.index('BUSTER_GLOBAL_LOCAL ProcessResult compiler_run_benchmarks(void)')
end = source.index('#include <buster/apps/ide/selection_benchmark.h>', begin)
(out / 'bench-app.c').write_text(source[:begin] + Path(__file__).with_name('lexer_benchmark.c').read_text() + '\n' + source[end:])
(out / 'baseline-source.c').write_bytes(subprocess.check_output(['git', 'show', '8c5a9bd:src/buster/lib/compiler/frontend/c/c_source.c']))
commands = json.loads((root / 'build/compile_commands.json').read_text())
records = []
for suffix, replacement, output in [('/apps/ide/ide.c', out / 'bench-app.c', out / 'bench-app.o'),
                                    ('/frontend/c/c_source.c', out / 'baseline-source.c', out / 'baseline-source.o')]:
    command = next(c for c in commands if c['file'].endswith(suffix) and '-O3' in c['command'])
    argv = shlex.split(command['command'])
    argv += ['-I' + str(Path(command['file']).parent)]
    argv[argv.index('-o') + 1] = str(output)
    argv[argv.index('-c') + 1] = str(replacement)
    subprocess.run(argv, cwd=command['directory'], env=env, check=True)
    records.append(argv)
link = subprocess.check_output(['ninja', '-C', 'build', '-f', 'build-Release.ninja', '-t', 'commands', 'Release/ide'], text=True).splitlines()[-1]
argv = shlex.split(link)
argv = argv[2:argv.index('&&', 2)]
for variant in ('baseline', 'candidate'):
    variant_argv = [str(out / 'bench-app.o') if a.endswith('/apps/ide/ide.c.o') else
                    str(out / 'baseline-source.o') if variant == 'baseline' and a.endswith('/frontend/c/c_source.c.o') else a for a in argv]
    variant_argv[variant_argv.index('-o') + 1] = str(out / ('bench-' + variant))
    subprocess.run(variant_argv, cwd=root / 'build', env=env, check=True)
    records.append(variant_argv)
(out / 'lexer-benchmark-build.json').write_text(json.dumps(records, indent=2) + '\n')
