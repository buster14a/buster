#!/usr/bin/env python3
"""Hosted IR-only follow-up. Preserve rejected earlier attempts separately."""
import hashlib
import json
import pathlib
import subprocess
import sys
compiler=pathlib.Path(sys.argv[1]).resolve()
sources=pathlib.Path(sys.argv[2]).resolve()
out=pathlib.Path(sys.argv[3]).resolve(); out.mkdir(parents=True,exist_ok=True)
common=['-std=gnu17','-fwrapv','-fno-strict-aliasing','-funsigned-char','-g0','-O0']
def invoke(argv,stem,limit=120):
    timed_out=False
    try:
        p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=limit,check=False)
        code,stdout,stderr=p.returncode,p.stdout,p.stderr
    except subprocess.TimeoutExpired as exc:
        code,stdout,stderr,timed_out=None,exc.stdout or b'',exc.stderr or b'',True
    pathlib.Path(str(stem)+'.stdout').write_bytes(stdout)
    pathlib.Path(str(stem)+'.stderr').write_bytes(stderr)
    data={'argv':argv,'returncode':code,'timeout':timed_out}
    pathlib.Path(str(stem)+'.json').write_text(json.dumps(data,indent=2)+'\n')
    return data
rows=[]
for source in sorted(sources.glob('*.c')):
    for frontend in ['ssa','memory']:
        d=out/source.stem/frontend; d.mkdir(parents=True)
        bc=d/'module.bc'; ll=d/'module.ll'; binary=d/'llvm-program'
        args=[str(compiler),'cc','-target','x86_64-linux',*common,
              '-ffrontend-ssa' if frontend=='ssa' else '-fno-frontend-ssa']
        # -fverify-codegen is native-only and MUST NOT accompany -emit-llvm.
        exported=invoke([*args,'-emit-llvm',str(source),'-o',str(bc)],d/'export')
        decoded=invoke(['clang','-S','-emit-llvm','-x','ir',str(bc),'-o',str(ll)],d/'decode') if exported['returncode']==0 else None
        linked=invoke(['clang','-O0',str(bc),'-o',str(binary)],d/'link') if exported['returncode']==0 else None
        ran=invoke([str(binary)],d/'run',15) if linked and linked['returncode']==0 else None
        pp=invoke([*args,'-E',str(source),'-o',str(d/'source.i')],d/'preprocess')
        row={'case':source.stem,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'frontend':frontend,
             'export':exported,'decode':decoded,'link':linked,'run':ran,'preprocess':pp}
        rows.append(row)
        with (out/'results.jsonl').open('a') as f: f.write(json.dumps(row)+'\n')
        print(source.stem,frontend,'export',exported['returncode'],'decode',decoded and decoded['returncode'],
              'link',linked and linked['returncode'],'run',ran and ran['returncode'],flush=True)
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
failed=any(any(row[key] is None or row[key]['returncode']!=0 for key in ['export','decode','link','run','preprocess']) for row in rows)
sys.exit(1 if failed else 0)
