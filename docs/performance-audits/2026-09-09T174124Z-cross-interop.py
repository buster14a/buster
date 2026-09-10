from pathlib import Path
import argparse, json, subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--ide', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--fixture', type=Path)
parser.add_argument('--host', type=Path)
opt = parser.parse_args()
repo = Path.cwd()
work = repo.parent
out = opt.out.resolve()
out.mkdir(exist_ok=False)
ide = opt.ide.resolve()
fixture = opt.fixture.resolve() if opt.fixture else work / '36-winind-fixture.c'
host_path = opt.host.resolve() if opt.host else work / '36-winind-host.c'
records = []
def run(command):
    command = [str(x) for x in command]
    r = subprocess.run(command, capture_output=True, text=True, timeout=30)
    records.append({'command': command, 'exit': r.returncode, 'stdout': r.stdout, 'stderr': r.stderr})
    (out / 'results.json').write_text(json.dumps(records, indent=2))
    assert r.returncode == 0, records[-1]

host = host_path.read_text().replace('#include <stdio.h>', '').replace('    printf("native-aggregate=%d\\n", bad);', '')
(out / 'host-aarch64.c').write_text(host)
(out / 'start.s').write_text('.text\n.global _start\n_start:\nbl main\nmov x8, #93\nsvc #0\n')
run(['clang', '-target', 'aarch64-linux-gnu', '-O2', '-ffreestanding', '-fno-stack-protector', '-c', out / 'host-aarch64.c', '-o', out / 'host-aarch64.o'])
run(['clang', '-target', 'aarch64-linux-gnu', '-c', out / 'start.s', '-o', out / 'start.o'])
run(['clang', '-O2', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-c', host_path, '-o', out / 'host-native.o'])
for target in ('x86_64-unknown-linux', 'aarch64-unknown-linux'):
    a64 = target.startswith('aarch64')
    subjects = [('clang-O0', ['clang', '-O0']), ('clang-O2', ['clang', '-O2'])]
    if a64:
        subjects = [(name, flags + ['-target', 'aarch64-linux-gnu', '-ffreestanding', '-fno-stack-protector']) for name, flags in subjects]
    for mode in ('none', 'mir-stack', 'fast', 'quality'):
        for ssa in ('-ffrontend-ssa', '-fno-frontend-ssa'):
            flags = [ide, 'cc', '-target', target, '-g0', '-fregister-allocator=' + mode, ssa, '-fverify-codegen']
            if mode != 'none':
                flags += ['-fno-machine-fallback']
            subjects.append((mode + ssa, flags))
    for name, flags in subjects:
        name = target + '-' + name
        run([*flags, '-c', fixture, '-o', out / (name + '.o')])
        if a64:
            run(['ld.lld', '-e', '_start', out / (name + '.o'), out / 'host-aarch64.o', out / 'start.o', '-o', out / name])
            run(['qemu-aarch64', out / name])
        else:
            run(['clang', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie', out / (name + '.o'), out / 'host-native.o', '-o', out / name])
            run([out / name])
        print(name + ' passed', flush=True)
