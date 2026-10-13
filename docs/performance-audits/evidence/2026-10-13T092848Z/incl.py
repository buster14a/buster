import sys,re,collections
# inclusive Ir and call count of callees matching a regex, summed over callers (self calls excluded)
path,pat=sys.argv[1],re.compile(sys.argv[2])
names={}; fn=None; cfn=None; pending=None
calls=collections.Counter(); incl=collections.Counter(); selfc=collections.Counter()
for line in open(path):
    if line.startswith('fn='):
        m=re.match(r'fn=\((\d+)\)(?: (.*))?',line.strip())
        if m.group(2): names[m.group(1)]=m.group(2)
        fn=names[m.group(1)]
    elif line.startswith('cfn='):
        m=re.match(r'cfn=\((\d+)\)(?: (.*))?',line.strip())
        if m.group(2): names[m.group(1)]=m.group(2)
        cfn=names[m.group(1)]
    elif line.startswith('calls='): pending=int(line[6:].split()[0])
    elif line and (line[0].isdigit() or line[0] in '+-*'):
        p=line.split()
        if pending is not None:
            if pat.search(cfn) and cfn!=fn: calls[cfn]+=pending; incl[cfn]+=int(p[1])
            pending=None
        elif len(p)>1 and pat.search(fn): selfc[fn]+=int(p[1])
for k in sorted(set(calls)|set(selfc)): print(f"{k}: calls {calls[k]:,} inclusive {incl[k]/1e6:.2f} M self {selfc[k]/1e6:.2f} M")
