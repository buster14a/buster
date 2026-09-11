#!/usr/bin/env python3
"""Attribute a clang -ftime-trace(-verbose) JSON to Buster functions.

Usage: analyze_trace.py <trace.json> [hot_function]

Per-function cost is the interval union of every backend event whose detail
names the function (plain, parenthesized, or "loop ... in function NAME"), so
nested pass-manager scopes are not double-counted. Frontend parse/IRGen events
are excluded; module-level scopes carry no function detail and drop out.
"""
import collections
import json
import re
import sys

path = sys.argv[1]
hot = sys.argv[2] if len(sys.argv) > 2 else 'codegen_generate_canonical_module_attempt'
events = [e for e in json.load(open(path))['traceEvents'] if e.get('ph') == 'X']

totals = {}
for e in json.load(open(path))['traceEvents']:
    if e.get('name', '').startswith('Total ') and 'args' in e:
        totals[e['name'][6:]] = e['args'].get('avg ms', 0) * e['args'].get('count', 1) / 1e3

execute = next(e['dur'] for e in events if e['name'] == 'ExecuteCompiler') / 1e6
print(f'ExecuteCompiler {execute:.2f}s')
for name in ('Frontend', 'Backend', 'Optimizer', 'CodeGenPasses'):
    if name in totals:
        print(f'  Total {name:14s} {totals[name]:8.2f}s  {totals[name] / execute * 100:5.1f}%')

ident = re.compile(r'^\(?([A-Za-z_][A-Za-z0-9_]*)\)?$')
loop_fn = re.compile(r'in function ([A-Za-z_][A-Za-z0-9_]*)')
frontend = {'ParseFunctionDefinition', 'ParseDeclarationOrFunctionDefinition', 'CodeGen Function', 'EvaluateAsRValue'}
intervals = collections.defaultdict(list)
for e in events:
    if e['name'] in frontend:
        continue
    detail = e.get('args', {}).get('detail', '')
    match = ident.match(detail) or loop_fn.search(detail)
    if match:
        intervals[match.group(1)].append((e['ts'], e['ts'] + e['dur']))


def union(spans):
    spans.sort()
    total = 0
    start = end = None
    for s, t in spans:
        if start is None:
            start, end = s, t
        elif s <= end:
            end = max(end, t)
        else:
            total += end - start
            start, end = s, t
    return total + (end - start if start is not None else 0)


rank = sorted(((union(v) / 1e6, k) for k, v in intervals.items()), reverse=True)
attributed = sum(t for t, _ in rank)
print(f'\n{len(rank)} functions, attributed backend union {attributed:.2f}s')
for i, (t, k) in enumerate(rank[:12]):
    print(f'{i + 1:3d} {t:7.3f}s {t / execute * 100:5.2f}%  {k}')
for n in (10, 100, 1000):
    print(f'  top {n}: {sum(t for t, _ in rank[:n]) / attributed * 100:.1f}% of attributed')

# Codegen passes run as RunPass events nested in the function's OptFunction.
codegen = [e for e in events if e['name'] == 'OptFunction' and e.get('args', {}).get('detail') == hot]
per_pass_tu = collections.defaultdict(float)
for e in events:
    if e['name'] == 'RunPass':
        per_pass_tu[e.get('args', {}).get('detail', '?')] += e['dur'] / 1e6
if codegen:
    window = codegen[0]
    lo, hi = window['ts'], window['ts'] + window['dur']
    per_pass = collections.defaultdict(float)
    for e in events:
        if e['name'] == 'RunPass' and e['tid'] == window['tid'] and lo <= e['ts'] and e['ts'] + e['dur'] <= hi:
            per_pass[e.get('args', {}).get('detail', '?')] += e['dur'] / 1e6
    print(f'\n{hot}: codegen OptFunction {window["dur"] / 1e6:.3f}s; top passes (share of that pass across the TU):')
    for name, t in sorted(per_pass.items(), key=lambda kv: -kv[1])[:8]:
        print(f'  {t:7.3f}s  {t / per_pass_tu[name] * 100:5.1f}% of TU  {name}')
