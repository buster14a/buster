#!/usr/bin/env python3
# Offline analysis of callgrind records written by cgrun.py. Groups records by
# (workload, config, LL) and reports per-variant counters, savings and the
# predeclared interaction I = M(A+B) - M(A) - M(B) + M(base).
import glob, json, os, re, sys
S = os.path.dirname(os.path.abspath(__file__))
recs = [json.load(open(p)) for p in sorted(glob.glob(os.path.join(S, 'cg', '*.json')))]
groups = {}
for r in recs:
    m = re.match(r'(.+)-(base|a|b|ab)-(ll\w+)$', r['label'])
    if not m:
        continue
    groups.setdefault((m.group(1), m.group(3)), {})[m.group(2)] = r
EV = 'Ir Dr Dw I1mr D1mr D1mw ILmr DLmr DLmw'.split()
def total(r, e): return r['totals'][EV.index(e)] if r['totals'] else 0
def prep(r, i): return r['inclusive'].get('ir_prepare_canonical_module', [0, 0, 0])[i]
out = []
for (work, ll), g in sorted(groups.items()):
    if set(g) != {'base', 'a', 'b', 'ab'}:
        out.append(f'## {work} {ll}: incomplete ({sorted(g)})')
        continue
    hashes = {v: g[v]['artifact_sha256'] for v in g}
    rcs = {v: g[v]['rc'] for v in g}
    ident = len(set(hashes.values())) == 1 and len(set(rcs.values())) == 1
    out.append(f'## {work} {ll} outputs_identical={ident} rc={rcs["base"]} out={str(hashes["base"])[:16]}')
    rows = [('prepare DLmr', lambda r: prep(r, 1)), ('prepare Ir', lambda r: prep(r, 0)), ('prepare DLmw', lambda r: prep(r, 2)),
            ('total DLmr', lambda r: total(r, 'DLmr')), ('total DLmw', lambda r: total(r, 'DLmw')), ('total Ir', lambda r: total(r, 'Ir')),
            ('total D1mr', lambda r: total(r, 'D1mr'))]
    out.append('| metric | base | A | B | A+B | A-base | B-base | A+B-base | I |')
    out.append('|---|---:|---:|---:|---:|---:|---:|---:|---:|')
    for name, f in rows:
        b, a, bb, ab = (f(g[v]) for v in ('base', 'a', 'b', 'ab'))
        inter = ab - a - bb + b
        out.append(f'| {name} | {b:,} | {a:,} | {bb:,} | {ab:,} | {a - b:+,} | {bb - b:+,} | {ab - b:+,} | {inter:+,} |')
print('\n'.join(out))
