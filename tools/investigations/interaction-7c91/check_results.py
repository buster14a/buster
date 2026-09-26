"""Independent analysis of retained counts, graph traces and bytes; no timing.
May be run on downloaded evidence; it never executes a compiler or generated C.
"""
from pathlib import Path
import hashlib
import json
import re
import sys

root = Path(sys.argv[1])
rows = json.loads((root/'rows.json').read_text())
checks = []

def check(name, value):
    checks.append({'check': name, 'pass': bool(value)})

def graph(variant, case):
    text = (root/'logs'/(case+'-'+variant+'.log')).read_text()
    nodes = [(int(i),int(k),int(n)) for i,k,n in re.findall(r'^R7_NODE (\d+) (\d+) (\d+)$',text,re.M)]
    edges = [(int(v),int(d)) for v,d in re.findall(r'^R7_EDGE (\d+) (\d+)$',text,re.M)]
    emitted = [(int(i),int(p)) for i,p in re.findall(r'^R7_TYPE (\d+) (\d+)$',text,re.M)]
    return nodes,edges,emitted

cases = sorted({r['case'] for r in rows if 'U' in r})
check('thirty predeclared cases', len(cases)==30)
primary = {}
for case in cases:
    by = {r['variant']:r for r in rows if r['case']==case}
    check(case+' four successful emissions', len(by)==4 and all(r['rc']==0 for r in by.values()))
    if any('types' not in by.get(v,{}) for v in ['baseline','candidate']):
        check(case+' trace completeness',False)
        continue
    old,new = by['baseline'],by['candidate']
    nodes,edges,emitted = graph('baseline',case)
    new_nodes,new_edges,new_emitted = graph('candidate',case)
    check(case+' same graph and emission sequence', (nodes,edges,emitted)==(new_nodes,new_edges,new_emitted))
    count = old['types']
    deps = [[] for _ in range(count)]
    for node,dependency in edges:
        deps[node].append(dependency)
    done=set()
    sequence=[]
    attempts=0
    passes=0
    while len(done)<count and passes<=count:
        passes+=1
        progress=0
        for node in range(count):
            if node not in done:
                attempts+=1
                if all(dependency in done for dependency in deps[node]):
                    done.add(node)
                    sequence.append((node,passes))
                    progress+=1
        if not progress:
            break
    check(case+' independent sweep simulation', sequence==emitted and attempts==old['attempts'] and passes==old['passes'])
    check(case+' observed table identity',old['slots']==count*passes)
    check(case+' predeclared sweep law',passes==(old['D'] if old['order']=='root' else 1))
    check(case+' bounded plan work',new['plan_nodes']==4*count and new['plan_edges']==2*len(edges) and
          new['delivered']==len(edges) and new['buckets']==3*count and new['slots']==count and
          new['attempts']==count and new['fallback']==0)
    check(case+' requested scratch bytes',new['setup_bytes']==32*count+12+4*max(1,len(edges)))
    check(case+' unchanged interner work',old['intern_rows']==new['intern_rows'] and old['intern_operands']==new['intern_operands'])
    output=[(root/'outputs'/(case+'-'+v+'.bc')).read_bytes() for v in ['pristine','baseline','candidate','clean']]
    check(case+' four byte-identical artifacts',all(b==output[0] for b in output))
    if old['order']=='root':
        primary[(old['U'],old['D'])]=old
if (0,1) in primary:
    H=primary[(0,1)]['types']-1
    for (u,d),r in primary.items():
        check(r['case']+' predeclared type law',r['types']==H+u+d)
    for u in [128,512]:
        for d in [8,32,128]:
            residual=primary[(u,d)]['slots']-primary[(u,1)]['slots']-primary[(0,d)]['slots']+primary[(0,1)]['slots']
            check(f'U={u},D={d} predeclared interaction residual',residual==u*(d-1))
    for d in [1,8,32,128]:
        check(f'D={d} unrelated population does not grow required output',
              len({primary[(u,d)]['output_bytes'] for u in [0,128,512]})==1 and
              len({primary[(u,d)]['records'] for u in [0,128,512]})==1)
else:
    H=None
result={'checks':checks,'passed':sum(c['pass'] for c in checks),'total':len(checks),'H':H,
        'failures':[c['check'] for c in checks if not c['pass']]}
print(json.dumps(result,indent=2))
sys.exit(bool(result['failures']))
