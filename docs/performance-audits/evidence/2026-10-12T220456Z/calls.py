import sys,re,collections
# sums call counts per (caller, callee) for callees matching a regex
f=open(sys.argv[1]); pat=re.compile(sys.argv[2])
names={}; fn=None; cfn=None; calls=collections.Counter(); incl=collections.Counter()
def name(kind,v):
    m=re.match(r'\((\d+)\)(?: (.*))?',v)
    if m:
        if m.group(2): names[(kind,m.group(1))]=m.group(2)
        return names.get((kind,m.group(1)),m.group(1))
    return v
pending=None
for line in f:
    line=line.rstrip('\n')
    if line.startswith('fn='): fn=name('fn',line[3:])
    elif line.startswith('cfn='): cfn=name('fn',line[4:])
    elif line.startswith('calls='): pending=int(line[6:].split()[0])
    elif pending is not None and line and (line[0].isdigit() or line[0] in '+-*'):
        parts=line.split()
        if pat.search(cfn):
            calls[(fn,cfn)]+=pending; incl[(fn,cfn)]+=int(parts[1]) if len(parts)>1 else 0
        pending=None
for k,v in sorted(calls.items(), key=lambda x:-incl[x[0]]):
    print(f"{v:>10} calls {incl[k]:>14,} Ir  {k[0]} -> {k[1]}")
