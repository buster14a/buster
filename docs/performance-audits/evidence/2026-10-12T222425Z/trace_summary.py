#!/usr/bin/env python3
"""Summarize ide-time-trace.json: phase totals, top OptFunction details, top CodeGenPasses passes."""
import collections
import json
import sys

trace = json.load(open(sys.argv[1]))
events = [e for e in trace["traceEvents"] if e.get("ph") == "X"]
print("# phase totals (seconds; nested, not additive)")
for name in ("ExecuteCompiler", "Frontend", "Backend", "Optimizer", "CodeGenPasses"):
    print(f"{sum(e['dur'] for e in events if e['name'] == name) / 1e6:10.3f} {name}")
print("# Total events (seconds; nested, not additive)")
totals = {e["name"]: e["dur"] / 1e6 for e in events if e["name"].startswith("Total ")}
for name, seconds in sorted(totals.items(), key=lambda item: -item[1])[:25]:
    print(f"{seconds:10.3f} {name}")
functions = collections.Counter()
for e in events:
    if e["name"] == "OptFunction":
        functions[e.get("args", {}).get("detail", "")] += e["dur"] / 1e6
print(f"# OptFunction: {len(functions)} functions recorded, {sum(functions.values()):.3f} s; top 30")
for name, seconds in functions.most_common(30):
    print(f"{seconds:10.3f} {name}")
codegen = [e for e in events if e["name"] == "CodeGenPasses"][0]
start, end = codegen["ts"], codegen["ts"] + codegen["dur"]
passes = collections.Counter()
for e in events:
    if start <= e["ts"] < end and e["name"] == "RunPass":
        passes[e.get("args", {}).get("detail", "")] += e["dur"] / 1e6
print("# RunPass inside CodeGenPasses; top 20")
for name, seconds in passes.most_common(20):
    print(f"{seconds:10.3f} {name}")
