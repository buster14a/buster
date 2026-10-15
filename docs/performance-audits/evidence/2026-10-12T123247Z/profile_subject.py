import concurrent.futures, hashlib, json, os, pathlib, subprocess, sys
root = pathlib.Path(__file__).resolve().parent
subject = sys.argv[1]
binary = root / 'subjects' / (subject + '-profile')
loader = root / 'glibc/usr/lib/ld-linux-x86-64.so.2'
library_path = root / 'glibc/usr/lib'
os.environ['GLIBC_TUNABLES'] = 'glibc.cpu.hwcaps=-AVX512F,-AVX512VL'
cwd = root / 'frozen'
profiles = root / 'profiles'
common = [str(loader), '--library-path', str(library_path), str(binary), 'cc', '-Isrc', '-Igenerated', '-DBUSTER_UNITY_BUILD=1', '-DBUSTER_INCLUDE_TESTS=0', '-target', 'x86_64-unknown-linux', '-march=znver5', '-c']
records = []
def run(mode):
    opts = ['-fc-ast-pilot'] if mode == 'pilot' else []
    for debug in ['-g0', '-g']:
        output = profiles / f'{subject}-{mode}-{debug[1:]}.o'
        argv = common + opts + [debug, 'src/buster/apps/ide/ide.c', '-o', str(output)]
        with (profiles / f'{subject}-{mode}-{debug[1:]}.log').open('w') as log:
            subprocess.run(argv, cwd=cwd, stdout=log, stderr=log, check=True)
        records.append(dict(subject=subject, mode=mode, debug=debug, sha256=hashlib.sha256(output.read_bytes()).hexdigest(), size=output.stat().st_size, argv=argv))
    cg = profiles / f'{subject}-{mode}.cg'
    output = profiles / f'{subject}-{mode}-profile.o'
    argv = ['valgrind', '--tool=callgrind', '--cache-sim=no', '--branch-sim=no', f'--callgrind-out-file={cg}'] + common + opts + ['-g0', 'src/buster/apps/ide/ide.c', '-o', str(output)]
    with (profiles / f'{subject}-{mode}-callgrind.log').open('w') as log:
        subprocess.run(argv, cwd=cwd, stdout=log, stderr=log, check=True)
    total = [line for line in cg.read_text().splitlines() if line.startswith('summary:')][-1]
    records.append(dict(subject=subject, mode=mode, callgrind_summary=total, argv=argv, output_sha256=hashlib.sha256(output.read_bytes()).hexdigest()))
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    list(pool.map(run, ['default'] if subject == 'gate' else ['default', 'pilot']))
(root / 'profiles' / f'{subject}.json').write_text(json.dumps(records, indent=2) + '\n')
print(json.dumps(records, indent=2))
