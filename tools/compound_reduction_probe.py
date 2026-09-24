"""Reduced spellings and exactly-once neighbors; source is defined GNU17."""
import json
import os
from pathlib import Path
import subprocess

out = Path(os.environ['RUNNER_TEMP']) / 'compound-validation'
label = os.environ['PROBE_LABEL']
cases = {
    'reduced_local': 'int main(void) { int value = 5; value *= 1.5; return value != 7; }\n',
    'reduced_result': 'int main(void) { int value = 5; int result = (value *= 1.5); return result != 7 || value != 7; }\n',
    'reduced_expanded': 'int main(void) { int value = 5; value = value * 1.5; return value != 7; }\n',
    'once_index': 'int main(void) { int values[2] = {5, 99}; unsigned index = 0; double rhs = 1.5; int result = (values[index++] *= rhs); return result != 7 || index != 1 || values[0] != 7 || values[1] != 99; }\n',
    'pointer_atomic_shift_controls': 'int main(void) { int objects[4] = {0}; int *p = objects; p += 2; p -= 1; _Atomic unsigned a = 1; unsigned r = (a += 2u); unsigned long long w = 0x100000000ULL; w >>= 32; return p != objects + 1 || a != 3 || r != 3 || w != 1; }\n',
}
common = ['-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char']
configs = [(cc+'_'+opt, [cc, '-m64', '-'+opt]) for cc in ['clang','gcc'] for opt in ['O0','O2']]
configs += [(cc+'_sanitize', [cc,'-m64','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all']) for cc in ['clang','gcc']]
for mode in ['none','mir-stack','fast','quality']:
    for form in ['ssa','memory']:
        configs.append(('buster_'+mode+'_'+form, ['build/Release/ide','cc','-target','x86_64-linux','-O0','-g0','-fregister-allocator='+mode,'-fverify-codegen','-ffrontend-ssa' if form=='ssa' else '-fno-frontend-ssa']))
records=[]
for name, source in cases.items():
    path=out/(name+'.c');path.write_text(source)
    for config,compiler in configs:
        exe=out/(name+'-'+config+'.exe')
        argv=compiler+common+[str(path),'-o',str(exe)]
        c=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
        r=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30) if c.returncode==0 else None
        records.append(dict(case=name,config=config,argv=argv,compile=c.returncode,run=r.returncode if r else None,compile_stdout=c.stdout.decode(errors='replace'),compile_stderr=c.stderr.decode(errors='replace'),run_stderr=r.stderr.decode(errors='replace') if r else None))
        exe.unlink(missing_ok=True)
(out/(label+'-reductions.json')).write_text(json.dumps(records,indent=2))
print(label, json.dumps([{k:r[k] for k in ['case','config','compile','run']} for r in records],indent=2))
assert all(r['compile']==0 and r['run']==0 for r in records if not r['config'].startswith('buster_'))
if label=='candidate':
    assert all(r['compile']==0 and r['run']==0 for r in records)
