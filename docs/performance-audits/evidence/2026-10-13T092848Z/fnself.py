import sys,re,collections
# self Ir per function name from a callgrind out file
def load(path):
    names={}; fn=None; tot=collections.Counter(); incall=False
    for line in open(path):
        if line.startswith('fn='):
            m=re.match(r'fn=\((\d+)\)(?: (.*))?',line.strip())
            if m.group(2): names[m.group(1)]=m.group(2)
            fn=names[m.group(1)]
        elif line.startswith('cfn=') :
            m=re.match(r'cfn=\((\d+)\)(?: (.*))?',line.strip())
            if m.group(2): names[m.group(1)]=m.group(2)
        elif line.startswith('calls='): incall=True
        elif line and (line[0].isdigit() or line[0] in '+-*'):
            if incall: incall=False; continue
            p=line.split()
            if len(p)>1 and fn: tot[fn]+=int(p[1])
    return tot
a=load(sys.argv[1]); b=load(sys.argv[2])
print('total', (sum(b.values())-sum(a.values()))/1e6)
d=sorted(((b[k]-a[k]),k) for k in set(a)|set(b))
for v,k in d[:10]+d[-10:]: print(f"{v/1e6:9.3f} {k}")
