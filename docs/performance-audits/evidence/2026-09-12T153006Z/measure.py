import csv,hashlib,json,os,pathlib,statistics,subprocess,time,sys
baseline,candidate,compiler,directory=sys.argv[1:]
out=pathlib.Path(directory); out.mkdir(parents=True,exist_ok=False)
harnesses={'baseline':baseline,'shared':candidate}
records=[]
for pair in range(9):
 for variant in (['baseline','shared'] if pair%2==0 else ['shared','baseline']):
  directory=out/f'{pair}-{variant}'
  args=[harnesses[variant],'run','--baseline',compiler,'--candidate',compiler,'--output',str(directory),'--profile','smoke','--workload','tiny_startup','--mode','none','--pairs','4','--warmups','1','--no-guard','--require-identical-output']
  start=time.perf_counter_ns()
  with open(out/f'{pair}-{variant}.log','wb') as log:
   proc=subprocess.Popen(args,stdout=log,stderr=log)
   pid,status,usage=os.wait4(proc.pid,0);proc.returncode=os.waitstatus_to_exitcode(status)
  elapsed=(time.perf_counter_ns()-start)/1e9
  if proc.returncode:raise RuntimeError((variant,pair,proc.returncode))
  samples=list(csv.DictReader(open(directory/'samples.csv')))
  assert len(samples)==16
  assert len({r['output_sha256'] for r in samples})==1
  child_wall=sum(float(r['wall_seconds']) for r in samples)
  records.append({'pair':pair,'variant':variant,'elapsed_seconds':elapsed,'child_wall_seconds':child_wall,'outside_plus_warmups_seconds':elapsed-child_wall,'user_seconds':usage.ru_utime,'system_seconds':usage.ru_stime,'maxrss_kib':usage.ru_maxrss,'samples':len(samples),'artifact_sha256':samples[0]['output_sha256']})
with open(out/'raw.csv','w') as f:
 w=csv.DictWriter(f,records[0]);w.writeheader();w.writerows(records)
assert len({r['artifact_sha256'] for r in records})==1
summary={v:{k:statistics.median(r[k] for r in records if r['variant']==v and r['pair']>0) for k in ['elapsed_seconds','child_wall_seconds','outside_plus_warmups_seconds','user_seconds','system_seconds','maxrss_kib']} for v in harnesses}
summary['binaries']={v:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() for v,p in {**harnesses,'compiler':compiler}.items()}
summary['platform']=os.uname()._asdict() if hasattr(os.uname(),'_asdict') else list(os.uname())
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
