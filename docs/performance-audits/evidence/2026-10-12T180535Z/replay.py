from pathlib import Path
import subprocess, hashlib, json, re, collections
import argparse
parser=argparse.ArgumentParser(description="Replay the #3741 preprocessing census on pristine local inputs")
parser.add_argument('--repo-root', type=Path, required=True, help='Instrumented source/build tree')
parser.add_argument('--workload-root', type=Path, required=True, help='Unmodified checkout at the evidence revision')
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--probe', type=Path, required=True)
parser.add_argument('--inputs', type=Path, required=True, help='Parent of lua-5.4.8, sqlite-amalgamation-3530400, quickjs')
parser.add_argument('--output', type=Path, required=True)
args=parser.parse_args()
root=args.repo_root.resolve()
workload=args.workload_root.resolve()
inputs=args.inputs.resolve()
out=args.output.resolve(); out.mkdir(exist_ok=False)
base=args.baseline.resolve()
probe=args.probe.resolve()
units=[]
units.append(('buster-unity',workload/'src/buster/apps/ide/ide.c',['-I'+str(workload/'src'),'-Ibuild/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0']))
units.append(('buster-unity-tests',workload/'src/buster/apps/ide/ide.c',['-I'+str(workload/'src'),'-Ibuild/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=1']))
for p in sorted((inputs/'lua-5.4.8/src').glob('*.c')):
    units.append(('lua-'+p.stem,p,['-DLUA_USE_LINUX']))
for name in ('sqlite3','shell'):
    units.append(('sqlite-'+name,inputs/f'sqlite-amalgamation-3530400/{name}.c',['-DSQLITE_ENABLE_MATH_FUNCTIONS','-DSQLITE_ENABLE_COLUMN_METADATA']))
for name in ('quickjs','dtoa','libregexp','libunicode','cutils','quickjs-libc','qjs','qjsc','run-test262'):
    units.append(('quickjs-'+name,inputs/f'quickjs/{name}.c',['-D_GNU_SOURCE','-DCONFIG_VERSION="2026-06-04"','-DCONFIG_CC="clang"','-DCONFIG_PREFIX="/usr/local"']))
for p in sorted(set((root/'tests').glob('*macro*.c')) | set((root/'tests').glob('*paste*.c'))):
    units.append(('control-'+p.stem,p,['-Isrc','-Ibuild/generated']))
for n in (1000,2000,4000,8000,16000,32000):
    p=out/f'paste-{n}.c'; p.write_text('#define X '+'##'.join(['a']*n)+'\nX\n')
    units.append((f'synthetic-{n}',p,[]))
results=[]
for name,p,flags in units:
    row={'name':name,'input':str(p),'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'flags':flags}
    for label,exe in [('base',base),('probe',probe)]:
        target=out/f'{name}.{label}.i'; metrics=out/f'{name}.{label}.metrics'; log=out/f'{name}.{label}.log'
        cmd=[str(exe),'cc','-std=c17' if name.startswith('synthetic-') else '-std=gnu17','-E',*flags,'-fsource-metrics='+str(metrics),str(p),'-o',str(target)]
        with log.open('wb') as stderr:
            proc=subprocess.run(cmd,cwd=root,stdout=subprocess.DEVNULL,stderr=stderr,timeout=120)
        row[label]={'command':cmd,'exit':proc.returncode,'output_sha256':hashlib.sha256(target.read_bytes()).hexdigest() if target.exists() else None,'log':str(log)}
        if label=='probe':
            text=log.read_text()
            chains=[{k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)} for line in text.splitlines() if line.startswith('PASTE_CHAIN')]
            row['paste']={'calls':text.count('PASTE_CALL '),'chains':len(chains),'joins':sum(c['joins'] for c in chains),'retained_joined_bytes':sum(c['bytes'] for c in chains),'final_bytes':sum(c['final'] for c in chains),'max_joins':max((c['joins'] for c in chains),default=0),'max_joined_length':max((c['max'] for c in chains),default=0),'histogram':dict(sorted(collections.Counter(c['joins'] for c in chains).items())),'failed_chains':sum(not c['ok'] for c in chains)}
    row['paste']['intermediate_bytes']=row['paste']['retained_joined_bytes']-row['paste']['final_bytes']-row['paste']['chains']
    row['outputs_identical']=row['base']['output_sha256']==row['probe']['output_sha256'] and row['base']['exit']==row['probe']['exit']==0
    if name.startswith('synthetic-') and row['outputs_identical']:
        n=int(name.split('-')[1])
        output=''.join(line.strip() for line in (out/f'{name}.base.i').read_text().splitlines() if not line.startswith('#'))
        assert output=='a'*n, name
        assert row['paste']['joins']==n-1, name
        assert row['paste']['retained_joined_bytes']==n*(n+1)//2+n-2, name
    metrics=dict(line.split('=',1) for line in (out/f'{name}.base.metrics').read_text().splitlines() if '=' in line)
    row['metrics']={key:int(value) for key,value in metrics.items() if key in ('preprocessed.spelling_bytes','preprocessed.tokens','lexed.bytes','unique.bytes')}
    row['intermediate_fraction']=row['paste']['intermediate_bytes']/row['metrics']['preprocessed.spelling_bytes']
    results.append(row)
    (out/'summary.json').write_text(json.dumps({'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'binaries':{k:hashlib.sha256(p.read_bytes()).hexdigest() for k,p in [('base',base),('probe',probe)]},'units':results},indent=2)+'\n')
    print(name,row['base']['exit'],row['probe']['exit'],row['outputs_identical'],row['paste'],flush=True)

if not all(row['outputs_identical'] for row in results):
    raise SystemExit('Preprocessing failed or baseline/probe outputs differ; inspect retained records')
