import sys,math,statistics
names={0:'prepass walk',1:'prepass index+value liveness',2:'setup+loop floors',3:'block scan',4:'edit merge',5:'home census',6:'home block close+color',7:'home linear close',8:'slot row census+certify',9:'slot close (liveness)',10:'slot color',11:'home color',12:'layout+predicate tail',14:'predicate inspect'}
def load(p):
    out=[]
    for line in open(p):
        d={}
        for kv in line.split()[1:]:
            k,v=kv.split('='); d[k]=int(v)
        out.append(d)
    return out
for wl in sys.argv[1:]:
    runs=[load(f'/tmp/claude-0/probe/{wl}.{i}.txt') for i in (1,2,3)]
    n=len(runs[0])
    fs=[]
    for j in range(n):
        d=dict(runs[0][j])
        for k in list(d):
            if k.startswith('t') or k=='total':
                d[k]=statistics.median(r[j].get(k,0) for r in runs)
        fs.append(d)
    tot=sum(f['total'] for f in fs)
    rows=sum(f['rows'] for f in fs)
    print(f'== {wl}: functions={n} rows={rows} blocks={sum(f["blocks"] for f in fs)} vregs={sum(f["vregs"] for f in fs)} total_cycles={tot/1e6:.1f}M cyc/row={tot/rows:.0f}')
    for i in sorted(names):
        t=sum(f.get(f't{i}',0) for f in fs)
        print(f'  t{i:<2} {names[i]:32s} {t/1e6:8.1f}M {100*t/tot:5.1f}%  {t/rows:6.0f} cyc/row')
    for i in range(16):
        w=sum(f.get(f'w{i}',0) for f in fs); p=sum(f.get(f'p{i}',0) for f in fs)
        if w: print(f'  liveness phase {i}: word updates {w:,} pops {p:,}')
    print(f'  close_bits {sum(f["close_bits"] for f in fs):,} edits {sum(f["edits"] for f in fs):,} retro {sum(f["retro"] for f in fs):,} homes {sum(f["homes"] for f in fs):,} shareable {sum(f["shareable"] for f in fs):,}')
    # size buckets
    print('  size bucket (rows)   fns   rows%   cyc%   cyc/row  [per-phase cyc/row t0 t1 t3 t6 t9 t10 t11]')
    b=[0,64,256,1024,4096,16384,1<<30]
    for lo,hi in zip(b,b[1:]):
        g=[f for f in fs if lo<=f['rows']<hi]
        if not g: continue
        r=sum(f['rows'] for f in g); t=sum(f['total'] for f in g)
        ph=' '.join(f'{sum(f.get("t%d"%i,0) for f in g)/r:5.0f}' for i in (0,1,3,6,9,10,11))
        print(f'  [{lo:>5},{hi:>10}) {len(g):5d} {100*r/rows:6.1f} {100*t/tot:6.1f} {t/r:8.0f}   {ph}')
    # top functions
    print('  top functions by cycles:')
    for f in sorted(fs,key=lambda f:-f['total'])[:8]:
        top=sorted(((f.get(f't{i}',0),i) for i in names),reverse=True)[:4]
        print(f'   rows={f["rows"]} blocks={f["blocks"]} vregs={f["vregs"]} slots={f["slots"]} words={f["tracked_words"]} homes={f["homes"]} shar={f["shareable"]} total={f["total"]/1e6:.1f}M ({100*f["total"]/tot:.1f}%) cyc/row={f["total"]/f["rows"]:.0f} ' + ' '.join(f't{i}={t/1e6:.1f}M' for t,i in top) + ' ' + ' '.join(f'w{i}={f[f"w{i}"]:,}' for i in range(16) if f'w{i}' in f))
    # log-log slope per phase for functions >=256 rows
    print('  log-log slope of phase cycles vs rows (fns with rows>=128):')
    g=[f for f in fs if f['rows']>=128]
    for i in sorted(names):
        xs=[];ys=[]
        for f in g:
            t=f.get(f't{i}',0)
            if t>0: xs.append(math.log(f['rows'])); ys.append(math.log(t))
        if len(xs)>10:
            mx=sum(xs)/len(xs); my=sum(ys)/len(ys)
            sl=sum((x-mx)*(y-my) for x,y in zip(xs,ys))/sum((x-mx)**2 for x in xs)
            print(f'    t{i:<2} {names[i]:32s} slope {sl:.2f} (n={len(xs)})')
