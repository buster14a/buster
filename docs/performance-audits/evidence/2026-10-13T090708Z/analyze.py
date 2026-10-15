import json, re, sys, glob, os, statistics as st
d = sys.argv[1]
def load(v, i):
    r = json.load(open(f"{d}/r-{v}-{i}.json"))
    r["wall_ms"] = r["wall_ns"] / 1e6; r["user_ms"] = r["utime"] * 1e3; r["sys_ms"] = r["stime"] * 1e3
    m = open(f"{d}/m-{v}-{i}.txt").read()
    for k, val in re.findall(r"\b(\w+_ns)=(\d+)", m.split("\n", 1)[1]):
        if k not in ("start_ns", "end_ns"): r[k[:-3] + "_ms"] = int(val) / 1e6
    vt = open(f"{d}/v-{v}-{i}.txt").read()
    for line in vt.splitlines():
        if line.startswith("DIAG_INL"):
            for k, val in re.findall(r"(\w+)=(\d+)", line):
                r["DIAG_INL." + (k[:-3] + "_ms" if k.endswith("_ns") else k)] = int(val) / 1e6 if k.endswith("_ns") else int(val)
        if line.startswith("DIAG_PREP") or line.startswith("IR_FAST ") or line.startswith("IR_FAST_PASS"):
            tag = line.split()[0] + ("." + re.search(r"name=(\w+)", line).group(1) if "PASS" in line else "")
            for k, val in re.findall(r"(\w*ns)=(\d+)", line):
                r[f"{tag}.{k[:-3]}_ms"] = int(val) / 1e6
    return r
pairs = sorted({int(f.split("-")[-1][:-5]) for f in glob.glob(f"{d}/r-a-*.json") if not f.endswith("warm.json")})
pairs = [i for i in pairs if os.path.exists(f"{d}/r-b-{i}.json")]
A = [load("a", i) for i in pairs]; B = [load("b", i) for i in pairs]
keys = [k for k in A[0] if k.endswith("_ms") or k in ("minflt", "maxrss_kb") or k.startswith("DIAG_INL")]
print(f"pairs={len(pairs)}")
print(f"{'metric':45s} {'A median':>12s} {'B median':>12s} {'med delta':>11s} {'delta IQR':>21s} {'med ratio':>10s}")
for k in keys:
    a = [x[k] for x in A]; b = [x.get(k, 0) for x in B]
    dl = sorted(y - x for x, y in zip(a, b)); q = st.quantiles(dl, n=4) if len(dl) > 1 else [dl[0]] * 3
    ra = [y / x for x, y in zip(a, b) if x]
    print(f"{k:45s} {st.median(a):12.1f} {st.median(b):12.1f} {st.median(dl):11.1f} [{q[0]:9.1f},{q[2]:9.1f}] {st.median(ra) if ra else 0:10.4f}")
