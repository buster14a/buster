# Compile every tests/*.c fixture with two compilers and record the CODEGEN frame statistics.
import subprocess, sys, os, re, json, hashlib, concurrent.futures
base, cand, out = sys.argv[1], sys.argv[2], sys.argv[3]
alloc = os.environ.get("ALLOC", "fast")
root = "/home/user/buster"
files = sorted(f for f in os.listdir(f"{root}/tests") if f.endswith(".c"))
os.makedirs(out, exist_ok=True)
def run(compiler, tag, f):
    obj = f"{out}/{tag}_{f}.o"
    p = subprocess.run([compiler, "cc", "-c", "-g0", "-v", f"-fregister-allocator={alloc}", f"tests/{f}", "-o", obj],
                       cwd=root, capture_output=True, text=True, timeout=600)
    stats = {}
    for line in (p.stdout + "\n" + p.stderr).split("\n"):
        if line.startswith("CODEGEN "):
            for k, v in re.findall(r"(\w+)=(\d+)", line):
                stats[k] = int(v)
    h = hashlib.sha256(open(obj, "rb").read()).hexdigest() if p.returncode == 0 and os.path.exists(obj) else None
    return {"rc": p.returncode, "frame": stats.get("stack_frame_bytes"), "max_frame": stats.get("max_stack_frame_bytes"),
            "value_bytes": stats.get("stack_value_bytes"), "functions": stats.get("functions"), "sha": h}
def both(f):
    return f, run(base, "base", f), run(cand, "cand", f)
rows = []
with concurrent.futures.ThreadPoolExecutor(4) as ex:
    for f, a, b in ex.map(both, files):
        rows.append({"file": f, "base": a, "cand": b})
json.dump(rows, open(f"{out}/census_{alloc}.json", "w"), indent=1)
ok = [r for r in rows if r["base"]["rc"] == 0 and r["cand"]["rc"] == 0 and r["base"]["frame"] is not None]
rc_diff = [r["file"] for r in rows if (r["base"]["rc"] == 0) != (r["cand"]["rc"] == 0)]
tb = sum(r["base"]["frame"] for r in ok); tc = sum(r["cand"]["frame"] for r in ok)
smaller = sum(1 for r in ok if r["cand"]["frame"] < r["base"]["frame"]); larger = sum(1 for r in ok if r["cand"]["frame"] > r["base"]["frame"])
same_hash = sum(1 for r in ok if r["cand"]["sha"] == r["base"]["sha"])
print(f"alloc={alloc} files={len(rows)} compiled_both={len(ok)} exit_status_differs={rc_diff}")
print(f"sum stack_frame_bytes base={tb} cand={tc} ratio={tc/max(tb,1):.3f}; smaller={smaller} larger={larger} identical_objects={same_hash}")
top = sorted(ok, key=lambda r: r["base"]["frame"] - r["cand"]["frame"], reverse=True)[:8]
for r in top:
    print(f"  {r['file']}: {r['base']['frame']} -> {r['cand']['frame']} (max {r['base']['max_frame']} -> {r['cand']['max_frame']})")
for r in ok:
    if r["cand"]["frame"] > r["base"]["frame"]:
        print(f"  LARGER {r['file']}: {r['base']['frame']} -> {r['cand']['frame']}")
