#!/usr/bin/env python3
"""Compile every tests/*.c fixture with two compilers and compare outputs byte-for-byte.

Usage: compare.py BASE CANDIDATE OUTDIR [extra flags...]
Runs from the repository root given by REPO (default /home/user/buster).
A fixture counts only when BOTH compilers agree on success/failure; outputs,
stdout and stderr are compared. Results go to OUTDIR/results.tsv.
"""
import hashlib, os, subprocess, sys, concurrent.futures as cf
base, cand, out = sys.argv[1], sys.argv[2], sys.argv[3]
extra = sys.argv[4:]
repo = os.environ.get("REPO", "/home/user/buster")
os.makedirs(out, exist_ok=True)
files = sorted(f for f in os.listdir(os.path.join(repo, "tests")) if f.endswith(".c"))
def run(compiler, tag, name):
    obj = os.path.join(out, f"{name}.{tag}.o")
    cmd = [compiler, "cc", *extra, "-c", os.path.join("tests", name), "-o", obj]
    p = subprocess.run(cmd, cwd=repo, capture_output=True, timeout=600)
    data = open(obj, "rb").read() if os.path.exists(obj) else b""
    return p.returncode, hashlib.sha256(data).hexdigest(), len(data), p.stdout, p.stderr
def one(name):
    b = run(base, "base", name); c = run(cand, "cand", name)
    same = b[0] == c[0] and b[1] == c[1] and b[3] == c[3] and b[4] == c[4]
    return name, b[0], c[0], b[1], c[1], b[2], same
rows = []
with cf.ThreadPoolExecutor(max_workers=int(os.environ.get("JOBS", "3"))) as ex:
    for r in ex.map(one, files):
        rows.append(r)
with open(os.path.join(out, "results.tsv"), "w") as f:
    f.write("fixture\tbase_rc\tcand_rc\tbase_sha256\tcand_sha256\tbytes\tidentical\n")
    for r in rows:
        f.write("\t".join(str(x) for x in r) + "\n")
ok = sum(1 for r in rows if r[6]); built = sum(1 for r in rows if r[1] == 0)
print(f"fixtures={len(rows)} identical={ok} differing={len(rows)-ok} base_success={built}")
for r in rows:
    if not r[6]: print("DIFF", r[0], r[1], r[2])
