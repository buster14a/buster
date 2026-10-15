from pathlib import Path
import hashlib,json,statistics,subprocess

w=Path(__file__).resolve().parent
out=w/'fix-evidence'/'frozen-comparison-final'
out.mkdir(exist_ok=False)
rows=[]
for mode in ['default','inline']:
    flags=[] if mode=='default' else ['-fcanonical-inline']
    for sample in range(-1,6):
        order=['a','b'] if sample%2==0 else ['b','a']
        pair=[]
        for arm in order:
            stem=out/f'{mode}-{sample}-{arm}'
            command=['taskset','-c','14','perf','stat','-x,','-e','instructions:u','-o',str(stem)+'.csv','--',str(w/f'subject-{arm}'),
                     'cc','-Isrc','-Igenerated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0','-g','-march=native',*flags,
                     'src/buster/apps/ide/ide.c','-lm','-o',str(w/'frozen-result')]
            p=subprocess.run(command,cwd=w/'frozen',capture_output=True,text=True)
            Path(str(stem)+'.stdout').write_text(p.stdout)
            Path(str(stem)+'.stderr').write_text(p.stderr)
            assert p.returncode==0,(command,p.returncode,p.stderr[-1000:])
            observed=[]
            for line in Path(str(stem)+'.csv').read_text().splitlines():
                fields=line.split(',')
                if len(fields)>2 and fields[2]=='instructions:u': observed.append(int(fields[0]))
            assert len(observed)==1,observed
            row={'mode':mode,'sample':sample,'arm':arm,'instructions_u':observed[0],
                 'output_sha256':hashlib.sha256((w/'frozen-result').read_bytes()).hexdigest(),'command':command}
            rows.append(row);pair.append(row)
        assert pair[0]['output_sha256']==pair[1]['output_sha256']
        print(mode,sample,'outputs identical',flush=True)
summary={'baseline_sha256':hashlib.sha256((w/'subject-a').read_bytes()).hexdigest(),
         'candidate_sha256':hashlib.sha256((w/'subject-b').read_bytes()).hexdigest(),
         'scope':'diagnostic paired native user instruction counts; no elapsed-time or Zen 5 claim','runs':rows,'medians':{}}
for mode in ['default','inline']:
    a=[r['instructions_u'] for r in rows if r['mode']==mode and r['arm']=='a' and r['sample']>=0]
    b=[r['instructions_u'] for r in rows if r['mode']==mode and r['arm']=='b' and r['sample']>=0]
    summary['medians'][mode]={'baseline':statistics.median(a),'candidate':statistics.median(b),'ratio':statistics.median(b)/statistics.median(a)}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary['medians']),flush=True)
