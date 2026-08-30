#!/usr/bin/env python3
"""Summarize differential-harness divergence reports for triage.

Reads build/differential-c/divergences/*.report.txt and groups the per-unit
findings by family and reject detail, so hundreds of reports collapse into a
handful of candidate root causes.  Behavior divergences cannot be grouped by
message (they all read "exit/stdout differ"), so those are listed per family
with their seed and unit for manual reduction.
"""

import collections
import os
import re
import sys

DIVERGENCES = os.path.join("build", "differential-c", "divergences")


def normalize_reject(detail):
    # Strip paths, line/column numbers, and generated identifiers so the same
    # diagnostic groups across seeds.
    detail = re.sub(r"/[^ ]*\.c:\d+:\d+:", "<loc>:", detail)
    detail = re.sub(r"'unit_\d+'", "'unit'", detail)
    detail = re.sub(r"'[a-z]+\d+_[A-Za-z0-9_]+'", "'<generated>'", detail)
    detail = re.sub(r"\(previous type \d+, new type \d+\)", "(previous/new type)", detail)
    return detail


def main():
    grouped = collections.defaultdict(list)   # (family, normalized detail) -> [(seed, unit)]
    behaviors = collections.defaultdict(list)  # family -> [(seed, unit)]
    for name in sorted(os.listdir(DIVERGENCES)):
        if not name.endswith(".report.txt"):
            continue
        with open(os.path.join(DIVERGENCES, name)) as report_file:
            text = report_file.read()
        header = re.search(r"family=(\S+) seed=(\d+) category=(\S+)", text)
        if not header:
            continue
        family, seed = header.group(1), int(header.group(2))
        unit_findings = re.findall(r"  unit (\d+): (\S+) \((.*)\)", text)
        if not unit_findings:
            unit_findings = [("-", header.group(3), re.search(r"detail: (.*)", text).group(1))]
        for unit, category, detail in unit_findings:
            if category in ("behavior", "run-crash"):
                behaviors[family].append((seed, unit, category))
            else:
                grouped[(family, category, normalize_reject(detail))].append((seed, unit))

    print("== grouped rejects/crashes ==")
    for (family, category, detail), hits in sorted(grouped.items(), key=lambda item: -len(item[1])):
        seeds = ", ".join("%d:u%s" % (seed, unit) for seed, unit in hits[:6])
        print("%4d  %-14s %-9s %s" % (len(hits), family, category, detail))
        print("      e.g. %s" % seeds)
    print()
    print("== behavior divergences (reduce by hand) ==")
    for family, hits in sorted(behaviors.items(), key=lambda item: -len(item[1])):
        seeds = " ".join("%d:u%s%s" % (seed, unit, "!" if category == "run-crash" else "")
                         for seed, unit, category in hits[:30])
        print("%4d  %-14s %s%s" % (len(hits), family, seeds, " ..." if len(hits) > 30 else ""))


if __name__ == "__main__":
    sys.exit(main())
