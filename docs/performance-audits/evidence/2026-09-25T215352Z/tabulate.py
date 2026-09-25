import glob, os, re, subprocess, filecmp, json
def total(path):
    for line in open(path):
        if line.startswith('summary:') or line.startswith('totals:'):
            return int(line.split()[1])
def resolved(path):
    # count calls of machine_x64_metadata_shape_resolve recorded in the callgrind file
    out = subprocess.run(['callgrind_annotate','--inclusive=yes','--tree=caller',path],capture_output=True,text=True).stdout
    m = re.findall(r'=> .*machine_x64_metadata_shape_resolve \((\d+)x\)', out)
    return int(m[0]) if m else 0
rows=[]
for b in sorted(glob.glob('*.base.cg')):
    n=b[:-len('.base.cg')]; p=n+'.proto.cg'
    bo, po = n+'.base.out', n+'.proto.out'
    ok = os.path.exists(bo) and os.path.exists(po)
    same = ok and filecmp.cmp(bo, po, shallow=False)
    rows.append(dict(workload=n, base=total(b), proto=total(p), shapes=resolved(p), compiled=ok, identical=same))
json.dump(rows, open('summary.json','w'), indent=1)
print(f"{'workload':24s} {'base Ir':>13s} {'proto Ir':>13s} {'delta':>12s} {'%':>7s} {'shapes':>6s} output")
tb=tp=cnt=0
for r in rows:
    d=r['proto']-r['base']
    out = 'identical' if r['identical'] else ('both-failed' if not r['compiled'] else 'DIFFER')
    print(f"{r['workload']:24s} {r['base']:13,d} {r['proto']:13,d} {d:12,d} {100*d/r['base']:6.1f}% {r['shapes']:6d} {out}")
    if r['workload'].startswith('H1') and r['compiled']: tb+=r['base'];tp+=r['proto'];cnt+=1
print(f"H1 held-out compiled TUs={cnt}: base {tb:,} proto {tp:,} delta {tp-tb:,} ({100*(tp-tb)/tb:.1f}%)")
