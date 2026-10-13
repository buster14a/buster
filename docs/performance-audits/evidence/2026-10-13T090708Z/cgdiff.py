import re, sys
def load(p):
    rows = {}
    for line in open(p):
        m = re.match(r"\s*([\d,]+) \([^)]*\)\s+(.*)$", line)
        if not m: continue
        parts = re.findall(r"([\d,]+|\.)\s+(?:\([^)]*\)\s+)?", line)
        nums = re.findall(r"(?:^|\s)([\d,]+|\.)(?:\s+\(\s*[\d.]+%\))?(?=\s)", line)
        name = line.rstrip().split()[-2] if line.rstrip().endswith("]") else line.rstrip().split()[-1]
        vals = [0 if n == "." else int(n.replace(",", "")) for n in nums[:9]]
        if len(vals) == 9: rows.setdefault(name, vals)
    return rows
a, b = load(sys.argv[1]), load(sys.argv[2])
cols = ["Ir", "Dr", "Dw", "I1mr", "D1mr", "D1mw", "ILmr", "DLmr", "DLmw"]
want = sys.argv[3:]
print(f"{'function':80s} {'dIr':>12s} {'Ir B/A':>8s} {'dD1m':>10s} {'dLLd(r+w)':>10s} {'LLd B/A':>8s}")
for w in want:
    for name in a:
        if name.endswith(w):
            x, y = a[name], b.get(name, [0]*9)
            dll = (y[7]+y[8]) - (x[7]+x[8]); ll = (x[7]+x[8])
            print(f"{name[-80:]:80s} {y[0]-x[0]:12,d} {y[0]/x[0] if x[0] else 0:8.4f} {(y[4]+y[5])-(x[4]+x[5]):10,d} {dll:10,d} {(y[7]+y[8])/ll if ll else 0:8.4f}")
