from pathlib import Path
import subprocess,os,csv,time,resource,statistics,json
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('--repo',type=Path,default=Path.cwd())
parser.add_argument('--work',type=Path,required=True)
options=parser.parse_args()
repo=options.repo.resolve()
work=options.work.resolve();out=work/'35-bench';out.mkdir(exist_ok=True)
clean=out/'clean.c';clean.write_text(''.join(f'int function_{i}(int value) {{ return value * {i+1} + {i}; }}\n' for i in range(1000)))
error=out/'diagnostics.c';error.write_text(''.join(f'#warning retained warning {i}\n' for i in range(1000))+'#error final error\n')
base=work/'35-ide-before';candidate=work/'35-ide-candidate';probe=work/'35-diagnostic-probe'
common=['cc','-target','x86_64-unknown-linux','-march=native','-g0','-fregister-allocator=none','-c']
large=common+['-Isrc','-Ibuild/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0','src/buster/apps/ide/ide.c','-o',str(out/'compiler.o')]
small=common+['tests/basic_c_operations.c','-o',str(out/'operations.o')]
samples=[];commands=[]
cases=[('compiler',base,candidate,large,0,7),('operations',base,candidate,small,0,17),
       ('clean-records',probe,probe,['cc','-fsyntax-only',str(clean)],0,17),
       ('diagnostic-records',probe,probe,['cc','-fsyntax-only',str(error)],1,17)]
for name,before,after,args,expected,count in cases:
    for variant in ('before','after'):
        env=dict(os.environ)
        if 'records' in name:env.update(BUSTER_DIAGNOSTIC_PROBE_SUPPRESS='1' if variant=='before' else '0',BUSTER_DIAGNOSTIC_PROBE_REPORT='1')
        command=[str(before if variant=='before' else after),*args]
        r=subprocess.run(command,cwd=repo,env=env,text=True,capture_output=True)
        assert r.returncode==expected,(name,variant,r.returncode,r.stdout[-400:],r.stderr[-400:])
        if 'records' in name:
            expected_records=0 if variant=='before' or name=='clean-records' else 1001
            assert f'DIAGNOSTIC_PROBE records={expected_records}\n' in r.stdout,(name,variant,r.stdout[:200])
        commands.append({'case':name,'variant':variant,'command':command,'expected_exit':expected,'record_probe':r.stdout.splitlines()[:1] if 'records' in name else []})
    for sample in range(-2,count):
        for variant in (('before','after') if sample%2==0 else ('after','before')):
            env=dict(os.environ)
            if 'records' in name:env['BUSTER_DIAGNOSTIC_PROBE_SUPPRESS']='1' if variant=='before' else '0'
            stat=out/'perf.csv'
            command=['perf','stat','-x,','-e','instructions:u','-o',str(stat),'--','taskset','-c','2',str(before if variant=='before' else after),*args]
            usage_before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter_ns()
            r=subprocess.run(command,cwd=repo,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
            elapsed=time.perf_counter_ns()-start;usage_after=resource.getrusage(resource.RUSAGE_CHILDREN)
            assert r.returncode==expected,(name,variant,r.returncode,r.stderr)
            line=next(x for x in stat.read_text().splitlines() if ',instructions:u,' in x);parts=line.split(',')
            assert parts[0].isdigit(),line
            if sample>=0:samples.append({'case':name,'variant':variant,'sample':sample,'wall_ns':elapsed,'cpu_seconds':usage_after.ru_utime+usage_after.ru_stime-usage_before.ru_utime-usage_before.ru_stime,'instructions':int(parts[0]),'counter_running_percent':parts[4]})
    print(name,'complete',flush=True)
with (out/'samples.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=samples[0].keys(),lineterminator='\n');w.writeheader();w.writerows(samples)
summary={}
for name,*_ in cases:
    summary[name]={}
    for metric in ('wall_ns','cpu_seconds','instructions'):
        a=statistics.median(x[metric] for x in samples if x['case']==name and x['variant']=='before')
        b=statistics.median(x[metric] for x in samples if x['case']==name and x['variant']=='after')
        summary[name][metric]={'before':a,'after':b,'percent':100*(b/a-1)}
(out/'summary.json').write_text(json.dumps(summary,indent=2));(out/'commands.json').write_text(json.dumps(commands,indent=2));print(json.dumps(summary,indent=2))
