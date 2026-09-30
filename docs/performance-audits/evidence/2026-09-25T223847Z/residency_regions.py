import sys, collections
path = sys.argv[1]
sites = {}; snap = collections.defaultdict(list); order = []
for line in open(path):
    f = line.rstrip("\n").split("\t")
    if f[0] == "R":
        snap[f[1]].append((int(f[2]), int(f[3], 16), int(f[4]), int(f[5])))
        if f[1] not in order: order.append(f[1])
    elif f[0] == "D":
        sites[int(f[1])] = f[2]
# Region clustering uses the final snapshot's ranges (superset).
final = sorted(snap[order[-1]], key=lambda r: r[1])
regions = []  # [start, end, [idx...]]
for idx, addr, size, res in final:
    if regions and addr < regions[-1][1]:
        regions[-1][1] = max(regions[-1][1], addr + size); regions[-1][2].append(idx)
    else:
        regions.append([addr, addr + size, [idx]])
region_of = {}
for ri, r in enumerate(regions):
    for idx in r[2]: region_of[idx] = ri
# Unique residency per region per snapshot needs page-level data; approximate by the max resident of any
# single range in the region (lower bound) and min(region span, sum) (upper bound). Report both.
for label in order:
    rows = snap[label]
    per_region = collections.defaultdict(list)
    for idx, addr, size, res in rows: per_region[region_of[idx]].append((idx, addr, size, res))
    mono = []; scratch = []
    for ri, rs in per_region.items():
        if len(rs) == 1: mono.append(rs[0])
        else: scratch.append((ri, rs))
    mono_res = sum(r[3] for r in mono)
    lb = sum(max(r[3] for r in rs) for ri, rs in scratch)
    span = sum(min(regions[ri][1] - regions[ri][0], sum(r[3] for r in rs)) for ri, rs in scratch)
    print(f"### {label}: monotonic_ranges={len(mono)} resident={mono_res/1e6:.1f}MB | scratch_regions={len(scratch)} resident_lb={lb/1e6:.1f}MB span_ub={span/1e6:.1f}MB")
    by = collections.defaultdict(lambda: [0, 0, 0])
    for idx, addr, size, res in mono:
        b = by[sites[idx]]; b[0] += size; b[1] += res; b[2] += 1
    for site, (size, res, n) in sorted(by.items(), key=lambda kv: -kv[1][1])[:int(sys.argv[2])]:
        print(f"  mono res={res/1e6:8.2f}MB handed={size/1e6:9.1f}MB n={n:5d} {site}")
    for ri, rs in sorted(scratch, key=lambda x: -(regions[x[0]][1]-regions[x[0]][0]))[:6]:
        top = collections.Counter()
        for idx, addr, size, res in rs: top[sites[idx]] += 1
        print(f"  scratch region {ri}: span={(regions[ri][1]-regions[ri][0])/1e6:.1f}MB max_single_res={max(r[3] for r in rs)/1e6:.1f}MB ranges={len(rs)} sites={top.most_common(4)}")
