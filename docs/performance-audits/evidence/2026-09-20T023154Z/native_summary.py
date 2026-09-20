import csv, math, statistics, sys
rows=list(csv.DictReader(open(sys.argv[1])))
for j,name in enumerate(['tiny_startup','large_function','many_functions','symbol_table','control_flow','backend_pressure']):
 for rnd in (0,1):
  pairs={}
  for r in rows:
   if int(r['job'])==j and int(r['round'])==rnd: pairs.setdefault(int(r['pair']),{})[int(r['variant'])]=float(r['wall_seconds'])
  if len(pairs)!=40: continue
  blocks=sorted(math.sqrt(pairs[i][1]/pairs[i][0]*pairs[i+1][1]/pairs[i+1][0]) for i in range(0,40,2))
  print(f'{name:18} {rnd+1} {100*(statistics.median(blocks)-1):+.3f}% [{100*(blocks[2]-1):+.3f}%,{100*(blocks[-3]-1):+.3f}%]')
