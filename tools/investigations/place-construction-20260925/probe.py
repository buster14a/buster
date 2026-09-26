#!/usr/bin/env python3
"""Hosted-only exact-once place probes; expected observations are literal goldens."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

CASES = [
    ("post_group", "r = (*((p++)))++;", [11,20,30,10,1,0]),
    ("pre_group", "r = ++(*((p++)));", [11,20,30,11,1,0]),
    ("post_decrement", "p = a + 1; r = (*((p--)))--;", [10,19,30,20,0,0]),
    ("indirect_source", "int **pp = &p; r = (*((*pp)++))++;", [11,20,30,10,1,0]),
    ("member_source", "struct Box { int *q; }; struct Box box = {p}; r = (*((box.q++)))++; p = box.q;", [11,20,30,10,1,0]),
    ("arithmetic_pointer", "r = (*(p + 1))++;", [10,21,30,20,0,0]),
    ("computed_arrow", "struct M { int m; }; struct M b[3] = {{10},{20},{30}}; struct M *q = b; r = (q + 1)->m++; a[0] = b[0].m; a[1] = b[1].m; a[2] = b[2].m;", [10,21,30,20,0,0]),
    ("unwrapped_post", "r = (*p++)++;", [11,20,30,10,1,0]),
    ("cast_arithmetic", "r = ++*(int *)(p + 1);", [10,21,30,21,0,0]),
    ("prefix_control", "r = ++*p++;", [11,20,30,11,1,0]),
    ("plain_control", "r = (*p)++;", [11,20,30,10,0,0]),
    ("double_dereference", "int *ptrs[2] = {a, a + 1}; int **pp = ptrs; r = (**(pp++))++; p = *pp;", [11,20,30,10,1,0]),
    ("cast_post", "r = (*((int *)(p++)))++;", [11,20,30,10,1,0]),
    ("group_subscript", "r = ((p++))[0]++;", [11,20,30,10,1,0]),
    ("call_pointer", "r = (*(next_pointer(p)))++;", [11,20,30,10,0,1]),
    ("subscript_control", "r = (p[0])++;", [11,20,30,10,0,0]),
    ("compound_control", "r = ((*((p++))) += 3);", [13,20,30,13,1,0]),
    ("assignment_control", "r = ((*((p++))) = 13);", [13,20,30,13,1,0]),
]


def main():
    ide = str(Path(sys.argv[1]).resolve())
    out = Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=False)
    rows = []

    def run(argv, prefix):
        prefix.with_suffix('.argv.json').write_text(json.dumps(argv))
        try:
            p = subprocess.run(argv, capture_output=True, timeout=180)
            status = p.returncode
            stdout, stderr = p.stdout, p.stderr
        except subprocess.TimeoutExpired as e:
            status = 124
            stdout, stderr = e.stdout or b'', e.stderr or b''
        prefix.with_suffix('.stdout').write_bytes(stdout)
        prefix.with_suffix('.stderr').write_bytes(stderr)
        prefix.with_suffix('.status').write_text(str(status) + '\n')
        return status, stdout

    run(['clang', '--version'], out / 'clang-version')
    run(['gcc', '--version'], out / 'gcc-version')
    (out / 'compiler.sha256').write_text(hashlib.sha256(Path(ide).read_bytes()).hexdigest() + '\n')
    observer = out / 'observer.c'
    observer.write_text('#include <stdio.h>\nvoid subject(int *out);\nint main(void) { int out[6] = {0}; subject(out); for (int i = 0; i < 6; ++i) printf("%d%c", out[i], i == 5 ? 10 : 32); return 0; }\n')
    profiles = [('clang-O0', ['clang','-O0']), ('clang-O2', ['clang','-O2']),
                ('gcc-O0', ['gcc','-O0']), ('gcc-O2', ['gcc','-O2'])]
    for mode in ['none', 'mir-stack', 'fast', 'quality']:
        for ssa in ['-ffrontend-ssa','-fno-frontend-ssa']:
            args = [ide, 'cc', '-target', 'x86_64-linux', '-O0', '-g0', '-fverify-codegen', '-fregister-allocator=' + mode, ssa]
            if mode != 'none':
                args += ['-fno-machine-fallback']
            profiles.append(('buster-' + mode + '-' + ssa[2:], args))
    for name, body, expected in CASES:
        folder = out / name
        folder.mkdir()
        source = folder / 'subject.c'
        source.write_text('static int hits;\nint *next_pointer(int *p) { hits++; return p; }\nvoid subject(int *out) { int a[3] = {10,20,30}; int *p = a; int r; hits = 0;\n' + body + '\nout[0]=a[0]; out[1]=a[1]; out[2]=a[2]; out[3]=r; out[4]=(int)(p-a); out[5]=hits; }\n')
        (folder / 'expected.json').write_text(json.dumps(expected))
        for profile, compiler in profiles:
            prefix = folder / profile
            obj = folder / (profile + '.o')
            exe = folder / (profile + '.exe')
            cc, _ = run(compiler + ['-std=c17', '-c', str(source), '-o', str(obj)], prefix.with_name(profile + '-compile'))
            link = execution = None
            observed = None
            if cc == 0:
                link, _ = run(['clang', '-std=c17', '-O0', str(observer), str(obj), '-o', str(exe)], prefix.with_name(profile + '-link'))
                if link == 0:
                    execution, text = run([str(exe)], prefix.with_name(profile + '-run'))
                    try:
                        observed = list(map(int, text.split()))
                    except ValueError:
                        pass
            verdict = 'PASS' if cc == 0 and link == 0 and execution == 0 and observed == expected else 'FAIL'
            row = dict(case=name, profile=profile, compile=cc, link=link, run=execution, observed=observed, expected=expected, verdict=verdict)
            rows.append(row)
            print(json.dumps(row), flush=True)
    (out / 'results.json').write_text(json.dumps(rows, indent=2))
    manifest = []
    for path in sorted(out.rglob('*')):
        if path.is_file():
            manifest.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + str(path.relative_to(out)))
    (out / 'SHA256SUMS').write_text('\n'.join(manifest) + '\n')
    return int(any(row['verdict'] != 'PASS' for row in rows))


if __name__ == '__main__':
    sys.exit(main())
