import subprocess,json,re
from pathlib import Path
rows=[]
for variant in ['baseline','patch']:
    for shape in ['comma','parentheses','prefix','mixed','postfix']:
        for depth in ([64,256,1024,4096] if variant=='baseline' else [64,256,1024,4096,16384]):
            cmd=[f'../typeof-diagnostic/{variant}-ide','cc','-g0','-std=gnu23','-fsyntax-only',f'../typeof-inputs/{shape}-{depth}.c']
            r=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            fields={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',r.stdout)}
            rows.append({'variant':variant,'shape':shape,'depth':depth,'argv':cmd,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr,**fields})
            Path('../typeof-work-final.json').write_text(json.dumps(rows,indent=2))
            if r.returncode: raise RuntimeError(rows[-1])
            print(variant,shape,depth,fields,flush=True)
