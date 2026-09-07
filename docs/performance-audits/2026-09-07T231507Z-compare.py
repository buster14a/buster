# Paired same-source compiler measurements for #39; standard library only.
# wait4 RSS can include inherited parent memory, particularly for tiny children.
import hashlib,json,os,statistics,subprocess,sys,time
from pathlib import Path
if len(sys.argv) != 5:
 raise SystemExit('usage: compare.py BASELINE_IDE CANDIDATE_IDE FROZEN_SOURCE_ROOT OUTPUT_DIRECTORY')
root=Path(sys.argv[3]).resolve()
out=Path(sys.argv[4]).resolve();out.mkdir(parents=True,exist_ok=True)
compilers={'before':str(Path(sys.argv[1]).resolve()),'after':str(Path(sys.argv[2]).resolve())}
stress=out/'many-types.c'
stress.write_text(''.join(f'struct S{i} {{ long x; double y; }};\n' for i in range(5000))+'int main(void) { return 0; }\n')
cases={'unity':['-Isrc','-Ibuild/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0','-g','src/buster/apps/ide/ide.c','-lm'], 'many_types':[str(stress)], 'small':['tests/basic_c_operations.c']}
rows=[]
for case,args in cases.items():
 n=6 if case=='unity' else 10
 for index in range(n+1):
  labels=['before','after'] if index%2==0 else ['after','before']
  for label in labels:
   output=out/(case+'-'+label)
   cmd=[compilers[label],'cc',*args,'-o',str(output)]
   with (out/'last.log').open('wb') as log:
    start=time.perf_counter_ns()
    proc=subprocess.Popen(cmd,cwd=root,stdout=log,stderr=log)
    _,status,usage=os.wait4(proc.pid,0)
    proc.returncode=os.waitstatus_to_exitcode(status)
    wall=(time.perf_counter_ns()-start)/1e9
   if proc.returncode: raise RuntimeError((case,label,(out/'last.log').read_text()))
   digest=hashlib.sha256(output.read_bytes()).hexdigest()
   if index:
    rows.append({'case':case,'compiler':label,'sample':index,'wall_s':wall,'user_s':usage.ru_utime,'sys_s':usage.ru_stime,'peak_rss_kib':usage.ru_maxrss,'bytes':output.stat().st_size,'sha256':digest})
summary={}
for case in cases:
 summary[case]={}
 for label in compilers:
  data=[r for r in rows if r['case']==case and r['compiler']==label]
  summary[case][label]={'median_wall_s':statistics.median(r['wall_s'] for r in data),'min_wall_s':min(r['wall_s'] for r in data),'max_wall_s':max(r['wall_s'] for r in data),'median_peak_rss_kib':statistics.median(r['peak_rss_kib'] for r in data),'bytes':data[0]['bytes'],'hashes':sorted(set(r['sha256'] for r in data))}
 summary[case]['identical_output']=len(set(r['sha256'] for r in rows if r['case']==case))==1
(out/'results.json').write_text(json.dumps({'summary':summary,'samples':rows},indent=2)+'\n')
print(json.dumps(summary,indent=2))
