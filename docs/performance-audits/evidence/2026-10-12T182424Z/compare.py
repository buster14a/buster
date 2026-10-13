import statistics, sys
def load(p):
    out=[]
    for line in open(p):
        out.append({k:int(v) for k,v in (kv.split('=') for kv in line.split()[1:])})
    return out
def med(wl, n=5):
    runs=[load(f'/tmp/claude-0/probe/{wl}.{i}.txt') for i in range(1,n+1)]
    fs=[]
    for j in range(len(runs[0])):
        d=dict(runs[0][j])
        for k in list(d):
            if k.startswith('t') or k=='total': d[k]=statistics.median(r[j].get(k,0) for r in runs)
        fs.append(d)
    return fs
b=med('sqlitebase'); c=med('sqlitecand')
assert len(b)==len(c) and all(x['rows']==y['rows'] for x,y in zip(b,c))
groups={'slots=0':lambda f:f['slots']==0,'slots>0, nothing shareable':lambda f:f['slots']>0 and f['shareable']==0,'shareable>0':lambda f:f['shareable']>0}
print(f"{'population':28s} {'fns':>5s} {'rows':>7s} {'t8 base':>9s} {'t8 cand':>9s} {'total base':>11s} {'total cand':>11s}")
for name,p in groups.items():
    ib=[i for i,f in enumerate(b) if p(f)]
    s=lambda fs,k: sum(fs[i][k] for i in ib)/1e6
    print(f"{name:28s} {len(ib):5d} {sum(b[i]['rows'] for i in ib):7d} {s(b,'t8'):8.2f}M {s(c,'t8'):8.2f}M {s(b,'total'):10.2f}M {s(c,'total'):10.2f}M")
print(f"{'all':28s} {len(b):5d} {sum(f['rows'] for f in b):7d} {sum(f['t8'] for f in b)/1e6:8.2f}M {sum(f['t8'] for f in c)/1e6:8.2f}M {sum(f['total'] for f in b)/1e6:10.2f}M {sum(f['total'] for f in c)/1e6:10.2f}M")
