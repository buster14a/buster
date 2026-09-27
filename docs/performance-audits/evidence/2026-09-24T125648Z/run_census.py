#!/usr/bin/env python3
"""Run the #531 provenance census over frozen inputs with a baseline and a probe compiler.

usage: run_census.py --baseline IDE --census IDE --repo ROOT --out DIR [--sqlite DIR] [--populations DIR]

For every input the object written by the probe binary must be byte-identical
to the one written by the baseline binary (the probe only observes). The
provenance rows land in <out>/<case>.tsv; identities in <out>/inputs.sha256.
Counts only: no timing is recorded or reported by this script.
"""
import argparse
import filecmp
import hashlib
import os
import subprocess
import sys


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--census", required=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--sqlite", default=None, help="extracted sqlite-amalgamation-3530400 directory")
    parser.add_argument("--populations", default=None, help="directory holding tiny.c/many_small.c/few_large.c")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    repo = os.path.abspath(args.repo)
    generated = os.path.join(repo, "build", "generated")
    cases = [
        ("stage1", os.path.join(repo, "src/buster/apps/ide/ide.c"),
         ["-I" + os.path.join(repo, "src"), "-I" + generated, "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-g"]),
        ("cfuncs", os.path.join(repo, "tests/c_abi_cfuncs.c"), ["-g"]),
        ("operations", os.path.join(repo, "tests/basic_c_operations.c"), ["-g"]),
    ]
    if args.populations:
        for name in ("tiny", "many_small", "few_large"):
            cases.append((name, os.path.join(args.populations, name + ".c"), ["-g"]))
    if args.sqlite:
        cases.append(("sqlite3", os.path.join(args.sqlite, "sqlite3.c"),
                      ["-g0", "-O2", "-I" + args.sqlite, "-DSQLITE_THREADSAFE=1", "-DSQLITE_ENABLE_MATH_FUNCTIONS",
                       "-DSQLITE_ENABLE_COLUMN_METADATA"]))
    identities = []
    failures = 0
    for name, source, flags in cases:
        if not os.path.exists(source):
            print(f"{name}: missing input {source}", file=sys.stderr)
            failures += 1
            continue
        identities.append(f"{sha256(source)}  {os.path.relpath(source, repo) if source.startswith(repo) else source}")
        base_object = os.path.join(args.out, name + ".base.o")
        census_object = os.path.join(args.out, name + ".census.o")
        tsv = os.path.join(args.out, name + ".tsv")
        if os.path.exists(tsv):
            os.remove(tsv)
        base = subprocess.run([args.baseline, "cc", *flags, "-c", source, "-o", base_object], cwd=repo, capture_output=True, text=True)
        env = dict(os.environ, BUSTER_LOWER_PROVENANCE_PATH=tsv)
        probe = subprocess.run([args.census, "cc", *flags, "-c", source, "-o", census_object], cwd=repo, capture_output=True, text=True, env=env)
        identical = os.path.exists(base_object) and os.path.exists(census_object) and filecmp.cmp(base_object, census_object, shallow=False)
        rows = sum(1 for line in open(tsv, encoding="utf-8") if line.startswith("F\t")) if os.path.exists(tsv) else 0
        print(f"{name}: baseline exit {base.returncode}, probe exit {probe.returncode}, objects identical={identical}, F rows={rows}")
        if base.stderr.strip():
            print(base.stderr[:2000], file=sys.stderr)
        if probe.stderr.strip() and probe.stderr != base.stderr:
            print(probe.stderr[:2000], file=sys.stderr)
        if base.returncode != 0 or probe.returncode != 0 or not identical or rows == 0:
            failures += 1
        for path in (base_object, census_object):
            if os.path.exists(path):
                identities.append(f"{sha256(path)}  {os.path.relpath(path, args.out)}")
    with open(os.path.join(args.out, "inputs.sha256"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(identities) + "\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
