import collections
import json
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1])
destination = pathlib.Path(sys.argv[2])
names = {kind: {} for kind in ('fn', 'fl', 'ob')}
self_cost = collections.defaultdict(lambda: [0] * 13)
line_cost = collections.defaultdict(lambda: [0] * 13)
edges = collections.defaultdict(lambda: [0] * 14)
function_files = {}
fn = fl = ob = cfn = cfl = cob = None
line_number = 0
pending = None
events = []
totals = []
for raw in source.read_text().splitlines():
    if raw.startswith('events: '):
        events = raw.split()[1:]
    elif raw.startswith('totals: '):
        totals = list(map(int, raw.split()[1:]))
    elif '=' in raw and raw.split('=', 1)[0] in ('fn', 'cfn', 'fl', 'fi', 'fe', 'cfl', 'cfi', 'ob', 'cob'):
        tag, value = raw.split('=', 1)
        match = re.match(r'\((\d+)\)(?: (.*))?$', value)
        category = 'fn' if tag in ('fn', 'cfn') else 'ob' if tag in ('ob', 'cob') else 'fl'
        key = match.group(1) if match else value
        if match and match.group(2) is not None:
            names[category][key] = match.group(2)
        elif not match:
            names[category][key] = value
        if tag == 'fn':
            fn = key
            function_files.setdefault(fn, fl)
        elif tag == 'cfn':
            cfn = key
        elif tag in ('fl', 'fi', 'fe'):
            fl = key
        elif tag == 'ob':
            ob = key
        elif tag == 'cob':
            cob = key
        else:
            cfl = key
    elif raw.startswith('calls='):
        pending = int(raw.split('=', 1)[1].split()[0])
    elif raw and raw[0] in '0123456789+-*':
        fields = raw.split()
        pos = fields[0]
        if pos != '*':
            line_number = line_number + int(pos) if pos[0] in '+-' else int(pos, 0) if pos.startswith('0x') else int(pos)
        costs = list(map(int, fields[1:]))
        costs += [0] * (len(events) - len(costs))
        if pending is not None:
            target = edges[(fn, cfn)]
            target[0] += pending
            for i, cost in enumerate(costs):
                target[i + 1] += cost
            pending = None
        else:
            for target in (self_cost[fn], line_cost[(fn, fl, line_number)]):
                for i, cost in enumerate(costs):
                    target[i] += cost

assert [sum(v[i] for v in self_cost.values()) for i in range(len(events))] == totals
functions = []
for key, costs in self_cost.items():
    incoming = [v for (caller, callee), v in edges.items() if callee == key]
    outgoing = [v for (caller, callee), v in edges.items() if caller == key]
    functions.append({'id': key, 'name': names['fn'].get(key, key), 'file': names['fl'].get(function_files.get(key)),
                      'calls': sum(v[0] for v in incoming), 'self': dict(zip(events, costs)),
                      'inclusive': {event: costs[i] + sum(v[i+1] for v in outgoing) for i, event in enumerate(events)}})
result = {'profile': str(source), 'events': events, 'totals': dict(zip(events, totals)), 'functions': functions,
          'edges': [{'caller': names['fn'].get(caller, caller), 'callee': names['fn'].get(callee, callee), 'calls': v[0],
                     'cost': dict(zip(events, v[1:]))} for (caller, callee), v in edges.items()],
          'lines': [{'function': names['fn'].get(fn, fn), 'file': names['fl'].get(fl, fl), 'line': line,
                     'cost': dict(zip(events, v))} for (fn, fl, line), v in line_cost.items()]}
destination.write_text(json.dumps(result, indent=2))
print(json.dumps({'totals': result['totals'], 'functions': len(functions), 'edges': len(edges)}, indent=2))
for event in ('Ir', 'D1mr', 'I1mr', 'Bcm', 'Bim'):
    print('\nTOP SELF', event)
    for fn in sorted(functions, key=lambda row: row['self'][event], reverse=True)[:12]:
        print(f"{fn['self'][event]:>13,} {fn['self'][event]/totals[events.index(event)]:>7.2%} {fn['calls']:>12,} {fn['name']}")
print('\nTOP INCLUSIVE Ir')
for fn in sorted(functions, key=lambda row: row['inclusive']['Ir'], reverse=True)[:40]:
    print(f"{fn['inclusive']['Ir']:>13,} {fn['inclusive']['Ir']/totals[0]:>7.2%} {fn['calls']:>12,} {fn['name']}")
