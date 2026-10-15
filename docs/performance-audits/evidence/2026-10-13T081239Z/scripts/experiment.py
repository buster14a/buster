import hashlib,json,math,os,pathlib,resource,statistics,subprocess,sys,time
R=pathlib.Path(__file__).resolve().parent
CELLS=[(t,m) for t in ('x86_64','aarch64') for m in ('fast','quality')]
def command(binary,t,m,out,verbose=True):
    target=['-march=skylake-avx512'] if t=='x86_64' else ['-target','aarch64-linux','-isystem',str(R/'cross-headers/usr/aarch64-linux-gnu/include')]
    return [str(R/('ide-'+binary)),'cc','-Isrc','-Igenerated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0','-g0',*target,'-fregister-allocator='+m,'-c','src/buster/apps/ide/ide.c','-o',str(out),*(['-v'] if verbose else [])]
def run(binary,t,m,out,log,verbose=True):
    cmd=command(binary,t,m,out,verbose)
    before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic()
    with open(log,'w') as f: p=subprocess.run(cmd,cwd=R/'frozen',stdout=f,stderr=subprocess.STDOUT)
    after=resource.getrusage(resource.RUSAGE_CHILDREN)
    assert p.returncode==0,(cmd,log,p.returncode)
    return {'cpu':after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,'wall':time.monotonic()-start,'command':cmd}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def counters(log):
    result={}
    for s in log.read_text().splitlines():
        if s.startswith(('CODEGEN ','CODEGEN_ALLOCATOR ')):
            result[s.split()[0]]=dict(x.split('=',1) for x in s.split()[1:])
    return result
out=R/os.environ.get('BUSTER_3933_OUTPUT','results');out.mkdir(exist_ok=True)
if sys.argv[1]=='census':
    import analyze_trace
    rows={}
    for t,m in CELLS:
        for b in ('base','census'):
            key=f'{t}-{m}-{b}';obj=out/(key+'.o');log=out/(key+'.log')
            measurement=({'command':command(b,t,m,obj),'reused_capture':True} if '--reuse' in sys.argv and obj.exists() and log.exists() else run(b,t,m,obj,log))
            rows[key]={**measurement,'sha256':digest(obj),'counters':counters(log)}
            if b=='census':
                rows[key]['attribution']=analyze_trace.main(log)
                assert rows[key]['sha256']==rows[f'{t}-{m}-base']['sha256']
                assert rows[key]['counters']==rows[f'{t}-{m}-base']['counters']
                print(key,json.dumps(rows[key]['attribution']),flush=True)
        (out/'census.json').write_text(json.dumps(rows,indent=2))
elif sys.argv[1]=='counts':
    rows={}
    for t,m in CELLS:
        key=f'{t}-{m}-candidate';obj=out/(key+'.o');log=out/(key+'.log')
        rows[key]={**run('candidate',t,m,obj,log),'sha256':digest(obj),'counters':counters(log)}
        print(key,json.dumps(rows[key]['counters']),flush=True)
    (out/'candidate-counts.json').write_text(json.dumps(rows,indent=2))
elif sys.argv[1]=='timing':
    n=int(sys.argv[2]);rows={}
    for t,m in CELLS:
        cell=f'{t}-{m}';samples={'base':[],'candidate':[]}
        for b in samples:run(b,t,m,'/dev/null',out/'timing.log',False)
        for i in range(n):
            for b in (('base','candidate') if i%2==0 else ('candidate','base')):
                samples[b].append(run(b,t,m,'/dev/null',out/'timing.log',False))
        ratios=[math.log(c['cpu']/b['cpu']) for b,c in zip(samples['base'],samples['candidate'])]
        mu=statistics.mean(ratios);se=statistics.stdev(ratios)/math.sqrt(n)
        rows[cell]={'samples':samples,'ratio':math.exp(mu),'ci95':[math.exp(mu-2.262*se),math.exp(mu+2.262*se)]}
        print(cell,{k:v for k,v in rows[cell].items() if k!='samples'},flush=True)
        (out/'timing.json').write_text(json.dumps(rows,indent=2))
