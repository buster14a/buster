import hashlib,json,subprocess,sys
from pathlib import Path

repo,binary,output=map(lambda s:Path(s).resolve(),sys.argv[1:4])
output.mkdir(exist_ok=False)
cases=[]
for shape in ['aggregate','aggregate-reversed','indirect','indirect-reversed','cast','cast-reversed','splice']:
    for n in [256,512,1024,2048,4096]:
        raw=subprocess.check_output([str(binary),shape,str(n)],cwd=repo,text=True)
        (output/f'{shape}-{n}.stdout').write_text(raw)
        observation,work=map(json.loads,raw.splitlines())
        assert observation['ok'],observation
        assert work['field_visits']<=2*n
        assert work['label_set_work']<=150*n+4096
        assert work['swept_values']==0
        if shape=='splice':
            assert work['marker_bytes']==1 and work['row_checks']==1 and work['index_work']<=64 and work['parameter_blocks']==0
        cases.append(dict(observation,work=work))
for shape in ['aggregate-bad','indirect-bad']:
    raw=subprocess.check_output([str(binary),shape,'17'],cwd=repo,text=True)
    (output/f'{shape}-17.stdout').write_text(raw)
    observation,work=map(json.loads,raw.splitlines())
    assert observation['ok'] and observation['error']!=0,observation
    cases.append(dict(observation,work=work))
source_paths=['src/buster/lib/compiler/ir/ir.c','src/buster/lib/compiler/ir/ir_inline.c','src/buster/lib/compiler/ir/ir_internal.h',
              'src/buster/lib/compiler/ir/ir_construction.h','src/buster/tests/compiler/ir/ir_validate_scaling_test.c']
summary={'revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
         'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
         'source_sha256':{p:hashlib.sha256((repo/p).read_bytes()).hexdigest() for p in source_paths},
         'scope':'exact production event counters; instrumented ns values are diagnostics only','cases':cases}
(output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('37 cases passed')
