#!/usr/bin/env python3
"""Markdown summary of a census results.jsonl for the job summary."""
import json
import sys


def main():
    rows = [json.loads(line) for line in open(sys.argv[1])]
    print("## Scaling census\n")
    for row in rows:
        if row["kind"] == "host":
            print(f"Host: {row['cpu']} x{row['cpus']}, {row['clang']}, census head `{row['census_head']}`\n")
    print("| build | commit | tree | binary sha256 |\n|---|---|---|---|")
    for row in rows:
        if row["kind"] == "build":
            print(f"| {row['name']}-{row['variant']} | `{row['commit'][:12]}` | `{row['tree'][:12]}` | `{row['binary_sha256'][:16]}` |")
    print("\n| experiment | row | flags | ref | status | object | counters (nonzero census/validation) | plain seconds |\n|---|---|---|---|---|---|---|---|")
    for row in rows:
        if row["kind"] == "experiment":
            counters = row["count"]["counters"] or {}
            shown = {k: v for k, v in counters.items() if v and (k.startswith("census_") or k.startswith("validation_global"))}
            seconds = ", ".join(f"{t['seconds']:.3f}" for t in row["plain"])
            print(f"| {row['experiment']} | {row['row']} | {' '.join(row['flags'])} | {row['ref']} | {row['count']['status']} | `{(row['count']['object_sha256'] or '-')[:12]}` | {shown} | {seconds} |")
    print("\n| workload | ref | variant | status | object | nonzero census/validation counters | plain seconds |\n|---|---|---|---|---|---|---|")
    for row in rows:
        if row["kind"] == "workload":
            counters = row["count"]["counters"] or {}
            shown = {k: v for k, v in counters.items() if v and (k.startswith("census_") or k.startswith("validation_global"))}
            seconds = ", ".join(f"{t['seconds']:.3f}" for t in row["plain"])
            print(f"| {row['workload']} | {row['ref']} | {row['variant']} | {row['count']['status']} | `{(row['count']['object_sha256'] or '-')[:12]}` | {shown} | {seconds} |")


if __name__ == "__main__":
    main()
