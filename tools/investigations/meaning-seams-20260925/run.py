#!/usr/bin/env python3
"""Disposable correctness observer. No production edits or timing conclusions."""
import hashlib, json, os, pathlib, subprocess, sys
source = pathlib.Path(sys.argv[1]).resolve()
compiler = pathlib.Path(sys.argv[2]).resolve()
out = pathlib.Path(sys.argv[3]).resolve()
out.mkdir(parents=True, exist_ok=True)
common = ['-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-g0']
profiles = [
    ('clang-O0', ['clang', '--target=x86_64-linux-gnu', *common, '-O0']),
    ('gcc-O0', ['gcc', '-m64', *common, '-O0']),
    ('buster-ssa-O0', [str(compiler), 'cc', '-target', 'x86_64-linux', *common, '-O0', '-fverify-codegen', '-fregister-allocator=fast', '-fno-machine-fallback', '-ffrontend-ssa']),
    ('buster-memory-O0', [str(compiler), 'cc', '-target', 'x86_64-linux', *common, '-O0', '-fverify-codegen', '-fregister-allocator=fast', '-fno-machine-fallback', '-fno-frontend-ssa']),
]
rows=[]
def invoke(argv, stem, limit):
    timed_out=False
    try:
        p=subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=limit, check=False)
        code, stdout, stderr = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        code, stdout, stderr, timed_out = None, e.stdout or b'', e.stderr or b'', True
    pathlib.Path(str(stem)+'.stdout').write_bytes(stdout)
    pathlib.Path(str(stem)+'.stderr').write_bytes(stderr)
    data={'argv':argv,'returncode':code,'timeout':timed_out}
    pathlib.Path(str(stem)+'.json').write_text(json.dumps(data,indent=2)+'\n')
    return data
for case in range(1,25):
    for name, prefix in profiles:
        d=out/f'{case:02d}-{name}'; d.mkdir()
        binary=d/'program'
        built=invoke([*prefix,f'-DCASE={case}',str(source),'-o',str(binary)],d/'compile',120)
        ran=invoke([str(binary)],d/'run',15) if built['returncode']==0 else None
        rows.append({'case':case,'profile':name,'compile':built,'run':ran})
        with (out/'results.jsonl').open('a') as f: f.write(json.dumps(rows[-1])+'\n')
        print(case,name,built['returncode'],None if ran is None else ran['returncode'],flush=True)
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
failures=sum(r['compile']['returncode']!=0 or r['run']['returncode']!=0 for r in rows)
print('observed_nonpassing_rows', failures, 'total',len(rows),flush=True)
# A completed observer is not a passing semantic result. Preserve every row.
sys.exit(1 if failures else 0)
