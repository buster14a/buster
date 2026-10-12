# Usage: python3 -I summarize.py RAW_CSV. Prints the audit table from measured rows only;
# any nonzero returncode invalidates its cell.
import csv, statistics, sys
rows = [r for r in csv.DictReader(open(sys.argv[1])) if r["phase"] == "measured"]
cells = list(dict.fromkeys(r["cell"] for r in rows))
print("| cell | pairs | baseline median ms | candidate median ms | median paired ratio B/A | min ratio | max ratio |")
print("| --- | --- | --- | --- | --- | --- | --- |")
for c in cells:
    rs = [r for r in rows if r["cell"] == c]
    if any(r["returncode"] != "0" for r in rs):
        print(f"| {c} | INVALID: nonzero returncode | | | | | |"); continue
    a = {r["pair"]: int(r["wall_ns"]) for r in rs if r["binary"] == "baseline"}
    b = {r["pair"]: int(r["wall_ns"]) for r in rs if r["binary"] == "candidate"}
    ratio = [b[p] / a[p] for p in a]
    print(f"| {c} | {len(a)} | {statistics.median(a.values())/1e6:.3f} | {statistics.median(b.values())/1e6:.3f} | "
          f"{statistics.median(ratio):.3f} | {min(ratio):.3f} | {max(ratio):.3f} |")
