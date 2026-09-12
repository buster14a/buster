#!/usr/bin/env python3
import json,pathlib,re,subprocess
out=pathlib.Path(__file__).resolve().parent
source=out/'addresses.c'
source.write_text('struct Inner { long long x[16]; }; struct Node { char pad[13]; struct Inner inner[3]; };\n'+''.join('long long address_%d(struct Node *p, short j) { long long r = 0;\n'%i+''.join('r += p->inner[(j + %d) %% 3].x[%d];\n'%(k,k) for k in range(16))+'return r; }\n' for i in range(256)))
rows=[]
for variant in ['baseline','candidate']:
 for target in ['x86_64-unknown-linux-gnu','aarch64-unknown-linux-gnu']:
  for workload,src in [('fixture','tests/basic_c_shared_address_facts.c'),('addresses',str(source))]:
   args=[str(out/(variant+'-probe')/'ide'),'cc','-target',target,'-fregister-allocator=fast','-fno-machine-fallback','-c',src,'-o',str(out/(variant+'-'+target+'-'+workload+'.o'))]
   r=subprocess.run(args,capture_output=True,text=True,timeout=60)
   stem=variant+'-'+target+'-'+workload;(out/(stem+'.stderr')).write_text(r.stderr);(out/(stem+'.stdout')).write_text(r.stdout)
   counts=[tuple(map(int,m)) for m in re.findall(r'queries=(\d+) definitions=(\d+) types=(\d+)',r.stderr)]
   row={'variant':variant,'target':target,'workload':workload,'exit':r.returncode,'functions':len(counts),'queries':sum(x[0] for x in counts),'definitions':sum(x[1] for x in counts),'types':sum(x[2] for x in counts),'command':args};rows.append(row);print(json.dumps(row),flush=True)
(out/'census-final.json').write_text(json.dumps(rows,indent=2))
