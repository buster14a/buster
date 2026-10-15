import sys, re, glob, statistics as st
R = sys.argv[1]
def load(p):
    d = {}
    if p.endswith(".csv"):
        for line in open(p):
            f = line.strip().split(",")
            if len(f) > 2 and f[0] and f[0][0].isdigit(): d[f[2]] = float(f[0])
    else:
        t = open(p).read()
        d["wall_ms"] = int(re.search(r"wall_ns=(\d+)", t).group(1)) / 1e6
        d["peak_rss_mb"] = int(re.search(r"peak_rss_bytes=(\d+)", t).group(1)) / 1e6
        for k, v in re.findall(r"\b(read|preprocess|parse|analysis|ir|codegen|object|emit|total)_ns=(\d+)", t.split("\n", 1)[1]): d[k + "_ms"] = int(v) / 1e6
    return d
pairs = sorted({re.match(r"(\d+)-", p.split("/")[-1]).group(1) for p in glob.glob(f"{R}/pairs/*-a.csv")})
rows = []
for i in pairs:
    a = {**load(f"{R}/pairs/{i}-a.csv"), **load(f"{R}/pairs/{i}-a.ccmetrics")}
    b = {**load(f"{R}/pairs/{i}-b.csv"), **load(f"{R}/pairs/{i}-b.ccmetrics")}
    rows.append((a, b))
keys = list(rows[0][0].keys())
print(f"pairs={len(rows)}")
print(f"{'metric':22s} {'A median':>14s} {'B median':>14s} {'med delta':>12s} {'min delta':>12s} {'max delta':>12s} {'med ratio':>10s}")
for k in keys:
    a = [x[k] for x, _ in rows]; b = [y[k] for _, y in rows]; d = sorted(y - x for x, y in zip(a, b))
    q = [y / x for x, y in zip(a, b) if x]; r = st.median(q) if q else 0
    print(f"{k:22s} {st.median(a):14.2f} {st.median(b):14.2f} {st.median(d):12.2f} {d[0]:12.2f} {d[-1]:12.2f} {r:10.4f}")
