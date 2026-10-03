#!/usr/bin/env python3
"""Turn work-ledger corpus results into mechanism ledgers and comparisons.

Input: one results.json per subject from run_corpus.py, given as
`label=path`. The first subject is the baseline. Output: Markdown on stdout
(or --output), plus a JSON summary with --json.

For every subject the report sums the exact `work.*` counters, the
`ir_construction.*` counters and the per-phase fault/arena rows over each
corpus family and over the whole corpus, groups counters by mechanism (the
key's middle component) and, for each later subject, prints the per-counter
change against the baseline together with the callgrind instruction change
and whether every unit's plain artifact is byte-identical to the baseline's.
Counts are summed, never averaged, so moved work stays visible.
"""

import argparse
import json
import sys
from collections import OrderedDict, defaultdict
from pathlib import Path


def load(spec: str) -> tuple[str, dict]:
    label, _, path = spec.partition("=")
    return label, json.loads(Path(path).read_text())


def family_totals(results: dict) -> OrderedDict:
    totals = OrderedDict()
    for unit in results["units"]:
        family = totals.setdefault(unit["family"], defaultdict(int))
        family["units"] += 1
        family["compiled"] += 1 if unit.get("compiled", unit["plain_run"]["status"] == 0) else 0
        family["identical"] += 1 if unit.get("identical") else 0
        if unit.get("callgrind_ir") is not None:
            family["callgrind_units"] += 1
            family["callgrind_ir"] += unit["callgrind_ir"]
        family["plain_minor_faults"] += unit["plain_run"].get("minor_faults", 0)
        for key, value in unit.get("metrics", {}).items():
            if key.startswith("work.") and not key.endswith(".version") and not key.endswith(".overflowed"):
                family[key] += value
            elif key.startswith("ir_construction.") and not key.endswith(".version"):
                family[key] += value
            elif key in ("preprocessed.tokens", "lexed.translated_bytes"):
                family[key] += value
    return totals


def corpus_total(totals: OrderedDict) -> defaultdict:
    total = defaultdict(int)
    for family in totals.values():
        for key, value in family.items():
            total[key] += value
    return total


def fmt(value: int) -> str:
    return f"{value:,}"


def pct(new: int, old: int) -> str:
    if old == 0:
        return "n/a" if new == 0 else "new"
    return f"{100.0 * (new - old) / old:+.2f}%"


def mechanism_rows(total: dict) -> OrderedDict:
    groups = OrderedDict()
    for key in sorted(total):
        if key.startswith("work.phase."):
            continue
        if key.startswith("work."):
            parts = key.split(".")
            groups.setdefault(parts[1], []).append(key)
        elif key.startswith("ir_construction."):
            groups.setdefault("ir_construction", []).append(key)
    return groups


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("subjects", nargs="+", help="label=results.json; the first is the baseline")
    parser.add_argument("--output")
    parser.add_argument("--json")
    parser.add_argument("--all-counters", action="store_true", help="also list counters that are zero everywhere")
    args = parser.parse_args()

    subjects = [load(spec) for spec in args.subjects]
    lines = []
    summary = {"subjects": []}
    base_label, base = subjects[0]
    base_families = family_totals(base)
    base_total = corpus_total(base_families)
    families = list(base_families)

    lines.append(f"# Work ledger: baseline `{base_label}`\n")
    lines.append("| Family | Units | Compiled | Identical ledger/plain | Callgrind units | Callgrind Ir | Preprocessed tokens | Plain minor faults |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for name, family in list(base_families.items()) + [("**corpus**", base_total)]:
        lines.append(f"| {name} | {fmt(family['units'])} | {fmt(family['compiled'])} | {fmt(family['identical'])} | {fmt(family['callgrind_units'])} | "
                     f"{fmt(family['callgrind_ir'])} | {fmt(family['preprocessed.tokens'])} | {fmt(family['plain_minor_faults'])} |")
    lines.append("")
    lines.append("## Phase attribution (ledger build)\n")
    phases = sorted({key.split(".")[2] for key in base_total if key.startswith("work.phase.")},
                    key=lambda phase: ["startup", "preprocess", "parse", "semantic", "lower", "prepare", "target_prewarm",
                                       "codegen", "object", "output"].index(phase) if phase in
                    ["startup", "preprocess", "parse", "semantic", "lower", "prepare", "target_prewarm", "codegen", "object", "output"] else 99)
    header = "| Phase | " + " | ".join(f"{family} faults" for family in families) + " | corpus faults | corpus arena calls |"
    lines.append(header)
    lines.append("|---|" + "---:|" * (len(families) + 2))
    for phase in phases:
        cells = [fmt(base_families[family].get(f"work.phase.{phase}.minor_faults", 0)) for family in families]
        lines.append(f"| {phase} | " + " | ".join(cells) + f" | {fmt(base_total.get(f'work.phase.{phase}.minor_faults', 0))} | "
                     f"{fmt(base_total.get(f'work.phase.{phase}.arena_calls', 0))} |")
    lines.append("")
    lines.append("## Mechanism ledger (exact counts, summed over units)\n")
    groups = mechanism_rows(base_total)
    for group, keys in groups.items():
        lines.append(f"### {group}\n")
        lines.append("| Counter | " + " | ".join(families) + " | corpus |")
        lines.append("|---|" + "---:|" * (len(families) + 1))
        for key in keys:
            if not args.all_counters and base_total.get(key, 0) == 0:
                continue
            cells = [fmt(base_families[family].get(key, 0)) for family in families]
            lines.append(f"| `{key}` | " + " | ".join(cells) + f" | {fmt(base_total.get(key, 0))} |")
        lines.append("")

    base_hashes = {(unit["family"], unit["name"]): unit.get("plain_sha256") for unit in base["units"]}
    for label, results in subjects[1:]:
        families_new = family_totals(results)
        total_new = corpus_total(families_new)
        mismatched = [f"{unit['family']}/{unit['name']}" for unit in results["units"]
                      if base_hashes.get((unit["family"], unit["name"])) != unit.get("plain_sha256")]
        lines.append(f"# `{label}` against `{base_label}`\n")
        lines.append(f"Plain artifacts byte-identical to the baseline: {len(results['units']) - len(mismatched)}/{len(results['units'])}"
                     + (f" (differ: {', '.join(mismatched[:20])})" if mismatched else ""))
        lines.append("")
        lines.append("| Family | Baseline Ir | Candidate Ir | Change | Baseline faults | Candidate faults | Change |")
        lines.append("|---|---:|---:|---:|---:|---:|---:|")
        for name in families + ["**corpus**"]:
            old = base_total if name == "**corpus**" else base_families.get(name, {})
            new = total_new if name == "**corpus**" else families_new.get(name, {})
            lines.append(f"| {name} | {fmt(old.get('callgrind_ir', 0))} | {fmt(new.get('callgrind_ir', 0))} | "
                         f"{pct(new.get('callgrind_ir', 0), old.get('callgrind_ir', 0))} | {fmt(old.get('plain_minor_faults', 0))} | "
                         f"{fmt(new.get('plain_minor_faults', 0))} | {pct(new.get('plain_minor_faults', 0), old.get('plain_minor_faults', 0))} |")
        lines.append("")
        lines.append("| Counter (corpus) | Baseline | Candidate | Change |")
        lines.append("|---|---:|---:|---:|")
        changed = sorted(set(base_total) | set(total_new))
        for key in changed:
            if not (key.startswith("work.") or key.startswith("ir_construction.")):
                continue
            old = base_total.get(key, 0)
            new = total_new.get(key, 0)
            if old != new:
                lines.append(f"| `{key}` | {fmt(old)} | {fmt(new)} | {pct(new, old)} |")
        lines.append("")
        summary["subjects"].append({"label": label, "mismatched": mismatched, "total": dict(total_new),
                                    "families": {name: dict(values) for name, values in families_new.items()}})
    summary["baseline"] = {"label": base_label, "total": dict(base_total),
                           "families": {name: dict(values) for name, values in base_families.items()}}
    text = "\n".join(lines) + "\n"
    if args.output:
        Path(args.output).write_text(text)
    else:
        sys.stdout.write(text)
    if args.json:
        Path(args.json).write_text(json.dumps(summary, indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
