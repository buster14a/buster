import csv,math,statistics,sys,json
from pathlib import Path
root=Path(sys.argv[1]) if len(sys.argv)>1 and not sys.argv[1].startswith("--") else Path(__file__).parent; result=[]
for folder in sorted(root.iterdir()):
 if not (folder/'samples.csv').is_file():continue
 names=dict(x.split('\t') for x in (folder/'jobs.tsv').read_text().splitlines());rows=list(csv.DictReader((folder/'samples.csv').open()))
 for job,name in names.items():
  for rnd in (0,1):
   selected=[x for x in rows if x['job']==job and int(x['round'])==rnd];pairs={}
   for x in selected:pairs.setdefault(int(x['pair']),{})[int(x['variant'])]=float(x['wall_seconds'])
   assert len(pairs)==40,(folder,job,rnd,len(pairs))
   blocks=sorted(math.sqrt(pairs[i][1]/pairs[i][0]*pairs[i+1][1]/pairs[i+1][0]) for i in range(0,40,2))
   med=[1000*statistics.median(float(x['wall_seconds']) for x in selected if int(x['variant'])==v) for v in (0,1)]
   first=selected[0]
   result.append(dict(experiment=folder.name,workload=name,round=rnd+1,base_ms=med[0],candidate_ms=med[1],change=100*(statistics.median(blocks)-1),lower=100*(blocks[2]-1),upper=100*(blocks[-3]-1),source_bytes=int(first['source_bytes']),source_lines=int(first['source_lines']),source_functions=int(first['source_functions']),output_bytes=int(first['output_bytes']),output_sha256=first['output_sha256']))
if '--json' in sys.argv:print(json.dumps(result,indent=2))
else:
 for x in result:print('| {experiment} | {workload} | {round} | {base_ms:.3f} | {candidate_ms:.3f} | {change:+.3f}% | [{lower:+.3f}%, {upper:+.3f}%] |'.format(**x))
