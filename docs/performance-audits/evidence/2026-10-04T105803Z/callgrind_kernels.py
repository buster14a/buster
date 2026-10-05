import re,sys
def parse(path):
    events=None; names={}; fn=None; self_={}; incl={}
    pending_call=False
    for raw in open(path):
        line=raw.rstrip('\n')
        if line.startswith('events:'):
            events=line.split()[1:]; continue
        m=re.match(r'^c?fn=\((\d+)\)(?: (.*))?$',line)
        if m:
            i=m.group(1)
            if m.group(2): names[i]=m.group(2)
            if line.startswith('fn='):
                fn=names.get(i,i); self_.setdefault(fn,[0]*len(events)); incl.setdefault(fn,[0]*len(events)); pending_call=False
            continue
        if line.startswith(('fl=','fi=','fe=','ob=','cob=','cfi=','cfl=')):
            continue
        if line.startswith('calls='):
            pending_call=True; continue
        if fn is not None and re.match(r'^[+-]?\d|\*',line):
            parts=line.split()
            vals=[int(v) for v in parts[1:1+len(events)]]
            vals+=[0]*(len(events)-len(vals))
            if pending_call:
                incl[fn]=[a+b for a,b in zip(incl[fn],vals)]; pending_call=False
            else:
                self_[fn]=[a+b for a,b in zip(self_[fn],vals)]
    return events,{f:[a+b for a,b in zip(self_[f],incl[f])] for f in self_}
ev,big=parse(sys.argv[1]); ev2,small=parse(sys.argv[2])  # usage: callgrind_kernels.py callgrind-object.out callgrind-object-ll2m.out
ix={e:i for i,e in enumerate(ev)}
print("| kernel | Ir | Dr | Dw | D1mr | D1mw | DLmr @2 MiB | DLmw @2 MiB |")
print("|---|---:|---:|---:|---:|---:|---:|---:|")
for k in sorted([f for f in big if f.startswith('lab_kernel_')], key=lambda n:(n.split('_')[2], n)):
    b=big[k]; s=small.get(k,[0]*9)
    print(f"| `{k}` | {b[ix['Ir']]:,} | {b[ix['Dr']]:,} | {b[ix['Dw']]:,} | {b[ix['D1mr']]:,} | {b[ix['D1mw']]:,} | {s[ix['DLmr']]:,} | {s[ix['DLmw']]:,} |")
