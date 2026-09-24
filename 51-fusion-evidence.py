#!/usr/bin/env python3
"""Branch-only experiment transformations; never part of the product PR.

The narrow competitor changes only eligibility. The preprocessing adapter
reuses the native harness's generation, paired scheduling, process collector,
output identity checks, raw schema and statistical replay. No timing observer
is added to either compiler. The adapter is not an admitted production mode.
"""
from pathlib import Path
import argparse
import hashlib

parser = argparse.ArgumentParser()
parser.add_argument("action", choices=("narrow", "preprocess"))
args = parser.parse_args()
path = Path("src/buster/lib/compiler/frontend/c/c_source.c" if args.action == "narrow" else "tools/throughput/throughput.c")
text = path.read_text()
changes = []
if args.action == "narrow":
    old = "if (!macro->builtin && !macro->definition.pragma_like && !macro->definition.has_paste && !macro->definition.has_stringify)"
    changes.append((old, old[:-1] + " && !macro->definition.parameter_count)"))
else:
    changes = [
        ('config->assembly = !strcmp(value, "assembly");', 'config->assembly = !strcmp(value, "assembly") ? 1 : !strcmp(value, "preprocess") ? 2 : 0;'),
        ('return job->stage ? ".exe" : job->assembly ? ".s" : ".o";', 'return job->stage ? ".exe" : job->assembly == 2 ? ".i" : job->assembly ? ".s" : ".o";'),
        ('args[argc++] = job->assembly ? "-S" : "-c";', 'args[argc++] = job->assembly == 2 ? "-E" : job->assembly ? "-S" : "-c";'),
        ('jobs[i].stage ? "executable" : jobs[i].assembly ? "assembly" : "object"', 'jobs[i].stage ? "executable" : jobs[i].assembly == 2 ? "preprocess" : jobs[i].assembly ? "assembly" : "object"'),
        ('!jobs[i].stage && jobs[i].assembly ? "/assembly" : ""', '!jobs[i].stage && jobs[i].assembly == 2 ? "/preprocess" : !jobs[i].stage && jobs[i].assembly ? "/assembly" : ""'),
        ('--artifact object|assembly (ordinary jobs only;', '--artifact object|assembly|preprocess (branch-only diagnostic; ordinary jobs only;'),
    ]
for old, new in changes:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"refusing changed anchor: {old!r}: {count} occurrences")
    text = text.replace(old, new, 1)
path.write_text(text)
print(f"{args.action}: {hashlib.sha256(path.read_bytes()).hexdigest()} {path}")
