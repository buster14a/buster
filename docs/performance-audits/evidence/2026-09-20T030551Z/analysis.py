import csv,hashlib,json,math,statistics
from pathlib import Path
e=Path(__file__).resolve().parent
lines=[]
for case in ['standard','aggregate-856','aggregate-854','AA']:
 names={int(a):b for a,b in (x.split('\t') for x in (e/case/'jobs.tsv').read_text().splitlines())}
 rows=list(csv.DictReader((e/case/'samples.csv').open()))
 for j,name in names.items():
  for rnd in (0,1):
   sel=[x for x in rows if int(x['job'])==j and int(x['round'])==rnd]; pairs={}
   for x in sel:pairs.setdefault(int(x['pair']),{})[int(x['variant'])]=float(x['wall_seconds'])
   assert len(pairs)==40
   b=sorted(math.sqrt(pairs[i][1]/pairs[i][0]*pairs[i+1][1]/pairs[i+1][0]) for i in range(0,40,2))
   m=[statistics.median(float(x['wall_seconds']) for x in sel if int(x['variant'])==v)*1000 for v in (0,1)]
   lines.append(f'| {case} | {name} | {rnd+1} | {m[0]:.3f} | {m[1]:.3f} | {100*(statistics.median(b)-1):+.3f}% | [{100*(b[2]-1):+.3f}%, {100*(b[-3]-1):+.3f}%] |')
u=json.loads((e/'unity.json').read_text()); ul=[]
for case in ['AA','856']:
 for rnd in (0,1):
  p={}
  for x in u['records']:
   if x['case']==case and x['round']==rnd:p.setdefault(x['pair'],{})[x['leg']]=x['wall_ns']
  b=sorted(math.sqrt(p[i]['candidate']/p[i]['base']*p[i+1]['candidate']/p[i+1]['base']) for i in range(0,40,2))
  ul.append(f'| {case} | {rnd+1} | {100*(statistics.median(b)-1):+.3f}% | [{100*(b[2]-1):+.3f}%, {100*(b[-3]-1):+.3f}%] |')
print('\n'.join(lines+ul))
