import statistics,math
def load(p):
    out=[]
    for line in open(p):
        d={}
        for kv in line.split()[1:]:
            k,v=kv.split('='); d[k]=int(v)
        out.append(d)
    return out
import sys
wl=sys.argv[1]
runs=[load(f'/tmp/claude-0/probe/{wl}.{i}.txt') for i in (1,2,3)]
fs=[]
for j in range(len(runs[0])):
    d=dict(runs[0][j])
    for k in ('t8','t9','total'): d[k]=statistics.median(r[j][k] for r in runs)
    fs.append(d)
for name,g in (('shareable>0',[f for f in fs if f['shareable']>0]),('shareable=0',[f for f in fs if f['shareable']==0])):
    r=sum(f['rows'] for f in g)
    print(name,len(g),'fns rows',r,'t8',sum(f['t8'] for f in g)/1e6,'t9',sum(f['t9'] for f in g)/1e6, 't9/row', sum(f['t9'] for f in g)/max(r,1))
g=[f for f in fs if f['shareable']>0 and f['rows']>=128]
xs=[math.log(f['rows']) for f in g]; ys=[math.log(f['t9']) for f in g]
mx=sum(xs)/len(xs); my=sum(ys)/len(ys)
print('slope t9 among shareable>0', sum((x-mx)*(y-my) for x,y in zip(xs,ys))/sum((x-mx)**2 for x in xs), len(g))
b=[0,256,1024,4096,1<<30]
for lo,hi in zip(b,b[1:]):
    h=[f for f in g if lo<=f['rows']<hi]
    if h: print(lo,hi,len(h),'t9/row',sum(f['t9'] for f in h)/sum(f['rows'] for f in h),'t8/row',sum(f['t8'] for f in h)/sum(f['rows'] for f in h),'shar/fn',sum(f['shareable'] for f in h)/len(h),'blocks/fn',sum(f['blocks'] for f in h)/len(h))
print('---')
for name,g in (('slots=0',[f for f in fs if f['slots']==0]),('slots>0 shar=0',[f for f in fs if f['slots']>0 and f['shareable']==0]),('shar>0',[f for f in fs if f['shareable']>0])):
    r=sum(f['rows'] for f in g)
    print(name,len(g),'rows',r,'t8 M',round(sum(f['t8'] for f in g)/1e6,2),'t9 M',round(sum(f['t9'] for f in g)/1e6,2),'total M',round(sum(f['total'] for f in g)/1e6,2))
print('all total', sum(f['total'] for f in fs)/1e6)
