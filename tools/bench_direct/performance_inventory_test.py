#!/usr/bin/env python3
"""Every performance-validation entry point has a declared Zen 5 disposition (#2761).

`docs/performance-validation-v1.json` is the inventory. This test discovers
the entry points that can claim performance validation from their own
registrations, so a new or renamed one cannot be added without a row, and a
deleted one cannot leave a stale row behind:

    build.c build_command_names   bench/throughput/production-profile commands
    tools/uarch_lab.py            measuring subcommands (report only renders)
    src/buster/apps/ide           the `ide bench` command
    tools/*                       bench/benchmark/scaling/survey/performance tools
    .github/workflows             benchmark/throughput/profile/lab/perf workflows
    benchmarks/9700x/*.c          the direct workload path (one wildcard row)

Each row is `performance`, `diagnostic` or `policy`. A performance row either
names an existing Zen 5 route and the consumer that checks its evidence
(`covered`), or is explicitly `NOT VALIDATED` with a resolution. Diagnostic
and policy rows are `not applicable` with a reason. The summary line reports
how many performance entries are covered; the inventory never claims more.

Map: discover, check_rows, main.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / "docs" / "performance-validation-v1.json"
BENCH_WORKFLOW = ROOT / ".github" / "workflows" / "9700x-direct-bench.yml"
ROUTE_JOBS = {"main-compare": "  compare:", "pull-compare": "  compare-pull:", "direct-workload": "  bench:"}
VALIDATIONS = ("performance", "diagnostic", "policy")
BUILD_COMMAND = re.compile(r"bench|throughput|^production_profile$")
TOOL = re.compile(r"bench|benchmark|scaling|survey|performance")
WORKFLOW = re.compile(r"bench|throughput|profile|lab\b|perf|708", re.IGNORECASE)


def discover(root: Path) -> set[str]:
    """Ids of every entry point that can claim performance validation."""
    found = set()
    build = (root / "build.c").read_text(encoding="utf-8")
    table = build[build.index("build_command_names[] = {"):]
    table = table[:table.index("};")]
    for name in re.findall(r'S8_INITIALIZER\("([a-z0-9_]+)"\)', table):
        if BUILD_COMMAND.search(name) and not name.endswith("self_test"):
            found.add("build:" + name)
    lab = (root / "tools" / "uarch_lab.py").read_text(encoding="utf-8")
    # `report` only re-renders existing raw files; every other mode measures,
    # including any mode added later.
    for name in re.findall(r'add_parser\("([a-z_]+)"', lab):
        if name != "report":
            found.add("uarch_lab:" + name)
    ide = "".join(path.read_text(encoding="utf-8") for path in sorted((root / "src/buster/apps/ide").glob("*.c")))
    if 'S8("bench")' in ide:
        found.add("ide:bench")
    for path in sorted((root / "tools").iterdir()):
        if path.is_file() and path.suffix in (".py", ".c") and TOOL.search(path.name) and \
                not path.stem.endswith("_test") and not path.stem.startswith("test_"):
            found.add("tool:" + path.name)
    for path in sorted((root / ".github" / "workflows").glob("*.yml")):
        first = path.read_text(encoding="utf-8").splitlines()[0]
        if WORKFLOW.search(path.name) or WORKFLOW.search(first):
            found.add("workflow:" + path.name)
    found.add("benchmarks/9700x/*.c")
    return found


def check_rows(data: object, discovered: set[str], bench_workflow: str) -> tuple[list[str], dict]:
    """(errors, counts) for the inventory against the discovered entry points."""
    errors: list[str] = []
    entries = data.get("entries") if isinstance(data, dict) else None
    if not isinstance(data, dict) or data.get("schema") != "buster-performance-validation-v1" or not isinstance(entries, list):
        return ["inventory is not a buster-performance-validation-v1 document"], {}
    ids = [entry.get("id") for entry in entries if isinstance(entry, dict)]
    if len(ids) != len(entries) or len(set(ids)) != len(ids):
        errors.append("inventory rows must be objects with unique ids")
    # A variant row (`id@variant`) records a second route of a discovered entry.
    bases = {entry_id.split("@", 1)[0] for entry_id in ids if isinstance(entry_id, str)}
    for missing in sorted(discovered - bases):
        errors.append(f"unregistered performance entry point: {missing}")
    for stale in sorted(bases - discovered):
        errors.append(f"inventory row names no current entry point: {stale}")
    counts = {"covered": 0, "not_validated": 0, "not_applicable": 0}
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        name = entry.get("id")
        zen5 = entry.get("zen5") if isinstance(entry.get("zen5"), dict) else {}
        route, status = zen5.get("route"), zen5.get("status")
        if entry.get("validation") not in VALIDATIONS or not entry.get("measures"):
            errors.append(f"{name}: validation must be one of {VALIDATIONS} with a measures description")
        elif entry["validation"] != "performance":
            if (route, status) != ("none", "not applicable") or not zen5.get("reason"):
                errors.append(f"{name}: a {entry['validation']} row is not applicable with a reason and no route")
            counts["not_applicable"] += 1
        elif route == "none":
            if status != "NOT VALIDATED" or not zen5.get("resolution"):
                errors.append(f"{name}: a performance row without a route is NOT VALIDATED with a resolution")
            counts["not_validated"] += 1
        elif route not in ROUTE_JOBS or ROUTE_JOBS[route] + "\n" not in bench_workflow:
            errors.append(f"{name}: route {route!r} is not a job of the 9700X workflow")
        elif status != "covered" or not (isinstance(zen5.get("consumer"), str) and (ROOT / zen5["consumer"]).is_file()):
            errors.append(f"{name}: a routed row is covered and names an existing evidence consumer")
        else:
            counts["covered"] += 1
    return errors, counts


def main() -> int:
    errors: list[str] = []
    try:
        data = json.loads(INVENTORY.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        data = None
        errors.append(f"inventory unreadable: {error}")
    counts: dict = {}
    if data is not None:
        problems, counts = check_rows(data, discover(ROOT), BENCH_WORKFLOW.read_text(encoding="utf-8"))
        errors.extend(problems)
    # Self-checks of the rules on synthetic inventories.
    row = {"id": "tool:x.py", "validation": "performance", "measures": "m",
           "zen5": {"route": "pull-compare", "status": "covered", "consumer": "tools/bench_direct/compiler_publish.py"}}
    synthetic = {"schema": "buster-performance-validation-v1", "entries": [row]}
    bench = "  compare-pull:\n"
    for label, inventory, found, expected in (
        ("covered row passes", synthetic, {"tool:x.py", }, True),
        ("unregistered entry fails", synthetic, {"tool:x.py", "tool:y.py"}, False),
        ("stale row fails", synthetic, set(), False),
        ("route to a missing job fails", {**synthetic, "entries": [dict(row, zen5=dict(row["zen5"], route="main-compare"))]},
         {"tool:x.py"}, False),
        ("unrouted covered claim fails", {**synthetic, "entries": [dict(row, zen5={"route": "none", "status": "covered"})]},
         {"tool:x.py"}, False),
        ("NOT VALIDATED needs a resolution", {**synthetic, "entries": [dict(row, zen5={"route": "none", "status": "NOT VALIDATED"})]},
         {"tool:x.py"}, False),
    ):
        if (not check_rows(inventory, found, bench)[0]) != expected:
            errors.append("self-check failed: " + label)
    for error in errors:
        print("PERFORMANCE_INVENTORY_FAIL " + error, file=sys.stderr)
    if not errors:
        print("PERFORMANCE_INVENTORY_PASS covered={covered} not_validated={not_validated} "
              "not_applicable={not_applicable}; performance validation is complete only for covered entries".format(**counts))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
