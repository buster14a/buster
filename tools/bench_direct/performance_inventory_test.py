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
(`covered`), or is explicitly `NOT VALIDATED` with a resolution and a
`claim_route`: the existing 9700X route a performance claim about it must use
instead, or `none` when no Zen 5 route exists yet (a visible gap, counted as
`no_route`). A claim route must be a job of the 9700X workflow, and a tool
that includes `<buster/...>` headers cannot claim the direct-workload route,
which compiles each workload alone against system headers. A covered
row's consumer must itself refuse evidence from any host but the approved
Zen 5 host (it uses compiler_receipt's APPROVED_HOST or host_problem), so a
route cannot count as covered through a consumer that never checks hardware. Diagnostic
and policy rows are `not applicable` with a reason. The summary line reports
how many performance entries are covered; the inventory never claims more.

Native macOS and Windows timing, which no Zen 5 route can measure, may instead
be `native-informative` on route `hosted-native` (maintainer decision on
#2761, 2026-10-11). Such a row is never covered and is counted separately. It
must be a macOS or Windows workflow whose named `job` runs on a literal
GitHub-hosted `macos-*` or `windows-*` label matching its `platform`, so a
Linux, matrix-selected or self-hosted timing job cannot use it to avoid the
9700X rule. The native `consumer` must report in a read-only job with
`--annotate`, no step may continue on error, and `issues: write` belongs only
to a separate job that publishes through the consumer and is gated to this
repository's `main`. Its `alert` must link an existing documented heading.

Each owner-requested fixed profile on the pull-compare route (PROFILE_ROWS)
needs its own covered `<entry>@<profile>` row (`uarch_lab:compare@...`, or
`build:bench_throughput@...` for a harness profile, or the entry's own row
when the profile is its only Zen 5 route, as for `build:production_profile`,
the two compiler-internal probes of compiler-probes-v1, the three
frontend-selector probes, `tool:wild_linker_bench.c` and `tool:link_tls_bench.py`) whose consumer
validates that exact frozen profile, so a flag-pair profile cannot be added,
or its validation dropped, without the inventory noticing.

Every covered row on the compare routes must be reachable from a receipt leg
in compiler_receipt.INVENTORY_LEGS for that route's mode, and every row the
mapping names must be a covered row of that route, so the published check's
discharged rows (covered_inventory_rows) can neither silently lose a row when
a leg or row is renamed or deleted nor discharge a row the inventory does not
cover there.

Evidence is chosen by mechanism and claim (#4013). `evidence_classes` names
the physical `zen5-native` class (whose routes are 9700X jobs) and the
alternative `work-count` and `native-host` classes, each with the consumer
that produces and checks it; `claims` maps each claim to its class and tools
and must equal the `pe_claims` table of that consumer, so the policy and the
tool cannot drift. A row may carry an `alternative` block naming the
non-Zen-5 classes it supports through that consumer; it is counted
separately and never as covered. An alternative consumer must state that it
is not Zen 5 evidence, never check the approved host, and never be the
consumer of a covered Zen 5 row, so a hosted or counted result can never be
written as `zen5: covered`.

Map: discover, check_rows, check_native_row, check_classes, check_profile_rows, check_leg_rows, main.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compiler_receipt import INVENTORY_LEGS, INVENTORY_ROUTES  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / "docs" / "performance-validation-v1.json"
BENCH_WORKFLOW = ROOT / ".github" / "workflows" / "9700x-direct-bench.yml"
ROUTE_JOBS = {"main-compare": "  compare:", "pull-compare": "  compare-pull:", "direct-workload": "  bench:"}
VALIDATIONS = ("performance", "diagnostic", "policy")
PHYSICAL_CLASS = "zen5-native"
ALTERNATIVE_CLASSES = ("work-count", "native-host")
ALTERNATIVE_MARKER = "not Zen 5 evidence"
# One pe_claims row: {"name", "class", "tool,tool", ...
CLAIM_TABLE = re.compile(r'^\s*\{"([a-z0-9-]+)", "([a-z0-9-]+)", "([a-z,-]*)"', re.MULTILINE)
BUILD_COMMAND = re.compile(r"bench|throughput|^production_profile$|^compiler_profile_qualification$")
TOOL = re.compile(r"bench|benchmark|scaling|survey|performance|^compiler_profile_qualification\.c$")
BUSTER_INCLUDE = re.compile(r"^\s*#\s*include\s*<buster/", re.MULTILINE)
HOST_CHECK = re.compile(r"\b(?:APPROVED_HOST|host_problem)\b")
WORKFLOW = re.compile(r"bench|throughput|profile|lab\b|perf|708|^external-projects\.yml$", re.IGNORECASE)
RECEIPT = ROOT / "tools" / "bench_direct" / "compiler_receipt.py"
NATIVE_ROUTE, NATIVE_STATUS = "hosted-native", "native-informative"
NATIVE_CONSUMER = "tools/bench_direct/native_timing.c"
NATIVE_RUNNERS = {"macos": re.compile(r"macos-[0-9a-z.-]+"), "windows": re.compile(r"windows-[0-9a-z.-]+")}
NATIVE_ALERT_GATE = ("github.repository == 'buster14a/buster'", "github.ref == 'refs/heads/main'")
# Inventory row -> the compiler_receipt profile constant its consumer must validate.
PROFILE_ROWS = {
    "uarch_lab:compare@canonical-inline-self-host": "INLINE_ACCEPTANCE_PROFILE",
    "uarch_lab:compare@ast-pilot-acceptance": "AST_PILOT_ACCEPTANCE_PROFILE",
    "uarch_lab:compare@ast-retirement-acceptance": "AST_RETIREMENT_PROFILE",
    "uarch_lab:compare@constant-reuse-acceptance": "CONSTANT_REUSE_ACCEPTANCE_PROFILE",
    "uarch_lab:compare@process-floor": "PROCESS_FLOOR_PROFILE",
    "build:bench_throughput@throughput-real-source": "THROUGHPUT_REAL_SOURCE_PROFILE",
    "build:production_profile": "PRODUCTION_PROFILE_PROFILE",
    "tool:fast_ra_scaling.c": "COMPILER_PROBES_PROFILE",
    "tool:machine_line_mark_scaling.c": "COMPILER_PROBES_PROFILE",
    "tool:benchmark_frontend_scaling.py": "FRONTEND_SELECTOR_PROFILE",
    "tool:benchmark_frontend_scaling.py@frontend-nesting": "FRONTEND_NESTING_PROFILE",
    "tool:selection_benchmark.py": "FRONTEND_SELECTOR_PROFILE",
    "ide:bench": "FRONTEND_SELECTOR_PROFILE",
    "tool:wild_linker_bench.c": "WILD_LINKER_PROFILE",
    "tool:link_tls_bench.py": "LINK_TLS_PROFILE",
    "uarch_lab:compare@self-host-inline-compare": "SELFHOST_INLINE_PROFILE",
}


# A minimal native hosted timing workflow for the self-checks.
NATIVE_WORKFLOW = """jobs:
  timing:
    runs-on: macos-26
    steps:
      - run: clang tools/bench_direct/native_timing.c -lm -o "$RUNNER_TEMP/native-timing"
      - run: '"$RUNNER_TEMP/native-timing" macos --annotate'
  alert:
    needs: timing
    if: ${{ needs.timing.outputs.verdict == 'regression' && github.repository == 'buster14a/buster' && github.ref == 'refs/heads/main' }}
    runs-on: ubuntu-26.04
    permissions:
      contents: read
      issues: write
    steps:
      - run: clang tools/bench_direct/native_timing.c -lm -o "$RUNNER_TEMP/native-timing"
      - run: '"$RUNNER_TEMP/native-timing" macos --publish-issue'
"""


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


def read_tool(name: object) -> str:
    """Source of a `tool:` row's file, or empty for any other row."""
    path = ROOT / "tools" / name[len("tool:"):] if isinstance(name, str) and name.startswith("tool:") else None
    return path.read_text(encoding="utf-8") if path is not None and path.is_file() else ""


def read_root(path: str) -> str:
    """A repository file's text, or empty when it does not exist."""
    file = ROOT / path
    return file.read_text(encoding="utf-8") if file.is_file() else ""


def job_blocks(workflow: str) -> dict[str, list[str]]:
    """Map each top-level job to its lines (as in workflow_policy_test.job_blocks)."""
    lines = workflow.splitlines()
    start = lines.index("jobs:") + 1 if "jobs:" in lines else len(lines)
    jobs: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in lines[start:]:
        header = re.fullmatch(r"  ([A-Za-z0-9_-]+):", line)
        if header:
            current = jobs.setdefault(header.group(1), [])
        elif line.strip() and not line.startswith("   "):
            current = None
        elif current is not None:
            current.append(line)
    return jobs


def heading_anchors(markdown: str) -> set[str]:
    """GitHub-style anchors of a document's headings."""
    return {re.sub(r"[^a-z0-9 _-]", "", line.lstrip("#").strip().lower()).replace(" ", "-")
            for line in markdown.splitlines() if re.match(r"#{1,6} ", line)}


def check_native_row(name: str, zen5: dict, read) -> list[str]:
    """A native-informative row names a hosted macOS/Windows job and a main-only documented alert."""
    errors: list[str] = []
    platform, job_name, alert = zen5.get("platform"), zen5.get("job"), zen5.get("alert")
    workflow = read(".github/workflows/" + name[len("workflow:"):]) if isinstance(name, str) and name.startswith("workflow:") else ""
    jobs = job_blocks(workflow)
    job = jobs.get(job_name, []) if isinstance(job_name, str) else []
    runs_on = [line.split(":", 1)[1].strip() for line in job if line.startswith("    runs-on:")]
    command = f'"$RUNNER_TEMP/native-timing" {platform}'
    alerts = [lines for lines in jobs.values() if "      issues: write" in lines]
    reporters = [lines for lines in jobs.values() if lines not in alerts and
                 command in "\n".join(lines) and "--annotate" in "\n".join(lines)]
    anchor = alert.split("#", 1) if isinstance(alert, str) and alert.count("#") == 1 else ["", ""]
    if zen5.get("route") != NATIVE_ROUTE or zen5.get("status") != NATIVE_STATUS or not zen5.get("resolution"):
        errors.append(f"{name}: a native hosted row is {NATIVE_STATUS} on {NATIVE_ROUTE} with a resolution")
    elif platform not in NATIVE_RUNNERS or not workflow:
        errors.append(f"{name}: only an existing macOS or Windows workflow can be {NATIVE_STATUS}")
    elif not job or len(runs_on) != 1 or not NATIVE_RUNNERS[platform].fullmatch(runs_on[0]):
        errors.append(f"{name}: job {job_name!r} must run on one literal GitHub-hosted {platform} label")
    elif zen5.get("consumer") != NATIVE_CONSUMER or not read(NATIVE_CONSUMER) or NATIVE_CONSUMER not in workflow or \
            not reporters or "continue-on-error" in workflow:
        errors.append(f"{name}: a read-only job must report through {NATIVE_CONSUMER} ({command} --annotate), "
                      "and no step may continue on error")
    elif len(alerts) != 1 or alerts[0] is job or NATIVE_CONSUMER not in "\n".join(alerts[0]) or \
            command not in "\n".join(alerts[0]) or "--publish-issue" not in "\n".join(alerts[0]) or \
            not all(any(line.startswith("    if:") and gate in line for line in alerts[0]) for gate in NATIVE_ALERT_GATE):
        errors.append(f"{name}: issues: write belongs only to one separate main-gated job publishing through {command}")
    elif not anchor[1] or anchor[1] not in heading_anchors(read(anchor[0])):
        errors.append(f"{name}: alert {alert!r} is not a documented heading")
    return errors


def check_rows(data: object, discovered: set[str], bench_workflow: str, read=read_root) -> tuple[list[str], dict]:
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
    counts = {"covered": 0, "not_validated": 0, "no_route": 0, "native_informative": 0, "not_applicable": 0, "alternative": 0}
    classes = data.get("evidence_classes") if isinstance(data.get("evidence_classes"), dict) else {}
    alternative_consumers = {spec.get("consumer") for key, spec in classes.items()
                             if key in ALTERNATIVE_CLASSES and isinstance(spec, dict)}
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        name = entry.get("id")
        zen5 = entry.get("zen5") if isinstance(entry.get("zen5"), dict) else {}
        route, status = zen5.get("route"), zen5.get("status")
        if "alternative" in entry:
            alternative = entry["alternative"] if isinstance(entry["alternative"], dict) else {}
            named = alternative.get("classes")
            if entry.get("validation") != "performance" or alternative.get("status") != "supported" or \
                    not isinstance(named, list) or not named or len(set(named)) != len(named) or \
                    not set(named) <= set(ALTERNATIVE_CLASSES) or \
                    any(not isinstance(classes.get(key), dict) or alternative.get("consumer") != classes[key].get("consumer")
                        for key in named):
                errors.append(f"{name}: an alternative block is a supported performance row naming only "
                              f"{ALTERNATIVE_CLASSES} classes through their declared consumer")
            else:
                counts["alternative"] += 1
        if status == "covered" and zen5.get("consumer") in alternative_consumers:
            errors.append(f"{name}: alternative consumer {zen5.get('consumer')} can never discharge a zen5 row")
        if entry.get("validation") not in VALIDATIONS or not entry.get("measures"):
            errors.append(f"{name}: validation must be one of {VALIDATIONS} with a measures description")
        elif entry["validation"] != "performance":
            if (route, status) != ("none", "not applicable") or not zen5.get("reason"):
                errors.append(f"{name}: a {entry['validation']} row is not applicable with a reason and no route")
            counts["not_applicable"] += 1
        elif route == NATIVE_ROUTE or status == NATIVE_STATUS:
            problems = check_native_row(name, zen5, read)
            errors.extend(problems)
            counts["native_informative"] += not problems
        elif route == "none":
            claim = zen5.get("claim_route")
            if status != "NOT VALIDATED" or not zen5.get("resolution"):
                errors.append(f"{name}: a performance row without a route is NOT VALIDATED with a resolution")
            elif claim != "none" and (claim not in ROUTE_JOBS or ROUTE_JOBS[claim] + "\n" not in bench_workflow):
                errors.append(f"{name}: claim_route {claim!r} is neither none nor a job of the 9700X workflow")
            elif claim == "direct-workload" and BUSTER_INCLUDE.search(read_tool(name)):
                errors.append(f"{name}: includes <buster/...> headers, so it cannot be a standalone direct workload")
            counts["not_validated"] += 1
            counts["no_route"] += claim == "none"
        elif route not in ROUTE_JOBS or ROUTE_JOBS[route] + "\n" not in bench_workflow:
            errors.append(f"{name}: route {route!r} is not a job of the 9700X workflow")
        elif status != "covered" or not (isinstance(zen5.get("consumer"), str) and (ROOT / zen5["consumer"]).is_file()):
            errors.append(f"{name}: a routed row is covered and names an existing evidence consumer")
        elif not HOST_CHECK.search((ROOT / zen5["consumer"]).read_text(encoding="utf-8")):
            errors.append(f"{name}: consumer {zen5['consumer']} never checks the observed host against APPROVED_HOST")
        else:
            counts["covered"] += 1
    return errors, counts


def check_classes(data: object, read=read_root) -> list[str]:
    """The evidence classes, the claim map, and their agreement with the alternative consumer's pe_claims table."""
    errors: list[str] = []
    classes = data.get("evidence_classes") if isinstance(data, dict) else None
    claims = data.get("claims") if isinstance(data, dict) else None
    if not isinstance(classes, dict) or set(classes) != {PHYSICAL_CLASS, *ALTERNATIVE_CLASSES} or \
            not all(isinstance(spec, dict) and spec.get("establishes") and spec.get("does_not_establish") for spec in classes.values()):
        return [f"evidence_classes must define {PHYSICAL_CLASS} and {ALTERNATIVE_CLASSES}, each with what it establishes and does not"]
    routes = classes[PHYSICAL_CLASS].get("routes")
    if "consumer" in classes[PHYSICAL_CLASS] or not isinstance(routes, list) or not routes or \
            not set(routes) <= set(ROUTE_JOBS):
        errors.append(f"{PHYSICAL_CLASS} names only 9700X workflow routes {sorted(ROUTE_JOBS)} and no alternative consumer")
    consumers = set()
    for key in ALTERNATIVE_CLASSES:
        consumer, tools = classes[key].get("consumer"), classes[key].get("tools")
        source = read(consumer) if isinstance(consumer, str) else ""
        consumers.add(consumer)
        if "routes" in classes[key] or not source or not isinstance(tools, list) or not tools:
            errors.append(f"{key}: an alternative class names an existing consumer and its tools, and no 9700X route")
        elif ALTERNATIVE_MARKER not in source or HOST_CHECK.search(source):
            errors.append(f"{key}: consumer {consumer} must state it is {ALTERNATIVE_MARKER!r} and never check the approved host")
    table = {}
    for consumer in sorted(c for c in consumers if isinstance(c, str)):
        for claim, kind, tools in CLAIM_TABLE.findall(read(consumer)):
            table[claim] = {"class": kind, "tools": [tool for tool in tools.split(",") if tool]}
    if not isinstance(claims, dict) or not claims:
        errors.append("claims must map each claim to its evidence class and tools")
    else:
        for claim, spec in claims.items():
            kind = spec.get("class") if isinstance(spec, dict) else None
            tools = spec.get("tools") if isinstance(spec, dict) else None
            allowed = classes.get(kind, {}).get("tools", []) if isinstance(classes.get(kind), dict) else []
            if kind not in classes or not isinstance(tools, list) or (kind == PHYSICAL_CLASS) != (not tools) or \
                    not set(tools) <= set(allowed):
                errors.append(f"claim {claim}: names an evidence class and only that class's tools; a {PHYSICAL_CLASS} claim has none")
        if claims != table:
            errors.append(f"claims {sorted(claims)} disagree with the consumer's pe_claims table {sorted(table)}")
    return errors


def check_profile_rows(data: object, receipt: str, read_consumer) -> list[str]:
    """Each PROFILE_ROWS profile is defined, and its row is covered on pull-compare by a consumer naming it."""
    errors: list[str] = []
    entries = data.get("entries") if isinstance(data, dict) else None
    rows = {entry.get("id"): entry for entry in entries if isinstance(entry, dict)} if isinstance(entries, list) else {}
    for name, constant in PROFILE_ROWS.items():
        zen5 = rows.get(name, {}).get("zen5") if isinstance(rows.get(name, {}).get("zen5"), dict) else {}
        consumer = zen5.get("consumer") if isinstance(zen5.get("consumer"), str) else ""
        if not re.search(rf"^{constant} = ", receipt, re.MULTILINE):
            errors.append(f"{name}: compiler_receipt defines no {constant}")
        elif name not in rows or zen5.get("route") != "pull-compare" or zen5.get("status") != "covered":
            errors.append(f"{name}: the fixed {constant} profile needs a covered pull-compare row")
        elif not consumer or not re.search(rf"\b{constant}\b", read_consumer(consumer)):
            errors.append(f"{name}: consumer {consumer or '(none)'} never validates {constant}")
    return errors


def check_leg_rows(data: object, legs: dict, routes: dict) -> list[str]:
    """Each covered compare-route row is reachable from a receipt leg, and each mapped row is covered there."""
    errors: list[str] = []
    entries = data.get("entries") if isinstance(data, dict) else None
    rows = {entry.get("id"): entry for entry in entries if isinstance(entry, dict)} if isinstance(entries, list) else {}
    if set(legs) != set(routes):
        errors.append(f"receipt leg modes {sorted(legs)} are not the compare modes {sorted(routes)}")
    for mode, route in routes.items():
        mapped = {row for names in legs.get(mode, {}).values() for row in names}
        for name, entry in rows.items():
            zen5 = entry.get("zen5") if isinstance(entry.get("zen5"), dict) else {}
            if zen5.get("route") == route and zen5.get("status") == "covered" and name not in mapped:
                errors.append(f"{name}: covered on {route} but no {mode} receipt leg discharges it")
        for leg, names in legs.get(mode, {}).items():
            if not names:
                errors.append(f"{mode} receipt leg {leg} discharges no inventory row")
            for name in names:
                zen5 = rows.get(name, {}).get("zen5") if isinstance(rows.get(name, {}).get("zen5"), dict) else {}
                if name not in rows:
                    errors.append(f"{mode} receipt leg {leg} names {name}, which is not an inventory row")
                elif (zen5.get("route"), zen5.get("status")) != (route, "covered"):
                    errors.append(f"{mode} receipt leg {leg} names {name}, which is not covered on {route}")
    return errors


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
        errors.extend(check_classes(data))
        errors.extend(check_profile_rows(data, RECEIPT.read_text(encoding="utf-8"),
                                         lambda path: (ROOT / path).read_text(encoding="utf-8") if (ROOT / path).is_file() else ""))
        errors.extend(check_leg_rows(data, INVENTORY_LEGS, INVENTORY_ROUTES))
    # Self-checks of the rules on synthetic inventories.
    row = {"id": "tool:x.py", "validation": "performance", "measures": "m",
           "zen5": {"route": "pull-compare", "status": "covered", "consumer": "tools/bench_direct/compiler_publish.py"}}
    synthetic = {"schema": "buster-performance-validation-v1", "entries": [row]}
    bench = "  compare-pull:\n  bench:\n"
    gap = {"route": "none", "status": "NOT VALIDATED", "resolution": "r", "claim_route": "pull-compare"}
    probe = "tool:fast_ra_scaling.c"
    for label, inventory, found, expected in (
        ("covered row passes", synthetic, {"tool:x.py", }, True),
        ("unregistered entry fails", synthetic, {"tool:x.py", "tool:y.py"}, False),
        ("stale row fails", synthetic, set(), False),
        ("route to a missing job fails", {**synthetic, "entries": [dict(row, zen5=dict(row["zen5"], route="main-compare"))]},
         {"tool:x.py"}, False),
        ("unrouted covered claim fails", {**synthetic, "entries": [dict(row, zen5={"route": "none", "status": "covered"})]},
         {"tool:x.py"}, False),
        ("consumer without a host check fails",
         {**synthetic, "entries": [dict(row, zen5=dict(row["zen5"], consumer="tools/bench_direct/authorize.py"))]},
         {"tool:x.py"}, False),
        ("NOT VALIDATED needs a resolution", {**synthetic, "entries": [dict(row, zen5={"route": "none", "status": "NOT VALIDATED"})]},
         {"tool:x.py"}, False),
        ("NOT VALIDATED with a claim route passes", {**synthetic, "entries": [dict(row, zen5=gap)]}, {"tool:x.py"}, True),
        ("NOT VALIDATED with no route passes", {**synthetic, "entries": [dict(row, zen5=dict(gap, claim_route="none"))]},
         {"tool:x.py"}, True),
        ("NOT VALIDATED needs a claim route", {**synthetic, "entries": [dict(row, zen5=dict(gap, claim_route=None))]},
         {"tool:x.py"}, False),
        ("a claim route to a missing job fails",
         {**synthetic, "entries": [dict(row, zen5=dict(gap, claim_route="main-compare"))]}, {"tool:x.py"}, False),
        ("a compiler-internal probe cannot claim a direct workload",
         {**synthetic, "entries": [dict(row, id=probe, zen5=dict(gap, claim_route="direct-workload"))]},
         {probe}, False),
    ):
        if (not check_rows(inventory, found, bench)[0]) != expected:
            errors.append("self-check failed: " + label)
    native_files = {
        ".github/workflows/native.yml": NATIVE_WORKFLOW,
        ".github/workflows/linux.yml": NATIVE_WORKFLOW.replace("runs-on: macos-26", "runs-on: ubuntu-26.04"),
        ".github/workflows/matrix.yml": NATIVE_WORKFLOW.replace("runs-on: macos-26", "runs-on: ${{ matrix.os }}"),
        ".github/workflows/selfhosted.yml": NATIVE_WORKFLOW.replace("runs-on: macos-26", "runs-on: [self-hosted, macos-26]"),
        ".github/workflows/open-alert.yml": NATIVE_WORKFLOW.replace(" && github.ref == 'refs/heads/main'", ""),
        ".github/workflows/writer.yml": NATIVE_WORKFLOW.replace("    runs-on: macos-26\n",
                                                                 "    runs-on: macos-26\n    permissions:\n      issues: write\n"),
        ".github/workflows/silent.yml": NATIVE_WORKFLOW.replace(" --publish-issue", ""),
        ".github/workflows/unreported.yml": NATIVE_WORKFLOW.replace(" macos --annotate", " macos"),
        ".github/workflows/lenient.yml": NATIVE_WORKFLOW.replace("    steps:\n", "    continue-on-error: true\n    steps:\n", 1),
        NATIVE_CONSUMER: "consumer", "docs/guide.md": "# Guide\n\n## Native hosted timing\n",
    }
    native = {"route": NATIVE_ROUTE, "status": NATIVE_STATUS, "platform": "macos", "job": "timing",
              "consumer": NATIVE_CONSUMER, "alert": "docs/guide.md#native-hosted-timing", "resolution": "r"}
    workflow_row = {"id": "workflow:native.yml", "validation": "performance", "measures": "m", "zen5": native}
    for label, zen5, workflow, expected in (
        ("a native hosted macOS row passes", native, "native.yml", True),
        ("a Linux timing job cannot be native-informative", native, "linux.yml", False),
        ("a matrix-selected runner cannot be native-informative", native, "matrix.yml", False),
        ("a self-hosted runner cannot be native-informative", native, "selfhosted.yml", False),
        ("a Linux platform cannot be native-informative", dict(native, platform="linux"), "native.yml", False),
        ("the platform must match the runner", dict(native, platform="windows"), "native.yml", False),
        ("a missing job fails", dict(native, job="absent"), "native.yml", False),
        ("a missing workflow fails", native, "absent.yml", False),
        ("an alert job not gated to main fails", native, "open-alert.yml", False),
        ("issues: write in the timing job fails", native, "writer.yml", False),
        ("an alert job that publishes nothing fails", native, "silent.yml", False),
        ("a workflow without an annotating report fails", native, "unreported.yml", False),
        ("a workflow that continues on error fails", native, "lenient.yml", False),
        ("an undocumented alert fails", dict(native, alert="docs/guide.md#missing"), "native.yml", False),
        ("another consumer fails", dict(native, consumer="tools/bench_direct/authorize.py"), "native.yml", False),
        ("the native status needs its route", dict(native, route="none"), "native.yml", False),
        ("the native route needs its status", dict(native, status="covered"), "native.yml", False),
    ):
        inventory = {"schema": "buster-performance-validation-v1",
                     "entries": [dict(workflow_row, id="workflow:" + workflow, zen5=zen5)]}
        problems, tallies = check_rows(inventory, {"workflow:" + workflow}, bench, lambda path: native_files.get(path, ""))
        if (not problems) != expected or (expected and (tallies["covered"], tallies["native_informative"]) != (0, 1)):
            errors.append("self-check failed: " + label)
    profile_rows = [{"id": name, "validation": "performance", "measures": "m",
                     "zen5": {"route": "pull-compare", "status": "covered", "consumer": "publisher.py"}}
                    for name in PROFILE_ROWS]
    constants = "".join(f"{constant} = {{}}\n" for constant in PROFILE_ROWS.values())
    publisher = " ".join(PROFILE_ROWS.values())
    ast_row = "uarch_lab:compare@ast-pilot-acceptance"
    for label, rows, receipt, consumer, expected in (
        ("every fixed profile row passes", profile_rows, constants, publisher, True),
        ("a missing profile row fails", [row for row in profile_rows if row["id"] != ast_row], constants, publisher, False),
        ("a profile row on another route fails",
         [dict(row, zen5=dict(row["zen5"], route="main-compare")) if row["id"] == ast_row else row for row in profile_rows],
         constants, publisher, False),
        ("a NOT VALIDATED profile row fails",
         [dict(row, zen5={"route": "none", "status": "NOT VALIDATED"}) if row["id"] == ast_row else row for row in profile_rows],
         constants, publisher, False),
        ("a consumer that ignores the profile fails", profile_rows, constants, "INLINE_ACCEPTANCE_PROFILE", False),
        ("an undefined profile constant fails", profile_rows, "INLINE_ACCEPTANCE_PROFILE = {}\n", publisher, False),
    ):
        inventory = {"schema": "buster-performance-validation-v1", "entries": rows}
        if (not check_profile_rows(inventory, receipt, lambda path: consumer)) != expected:
            errors.append("self-check failed: " + label)
    covered = [{"id": name, "validation": "performance", "measures": "m",
                "zen5": {"route": route, "status": "covered", "consumer": "publisher.py"}}
               for name, route in (("lab@main", "main-compare"), ("lab", "pull-compare"), ("profile", "pull-compare"))]
    legs = {"main": {"self-host": ("lab@main",)}, "pull": {"self-host": ("lab",), "opt-in": ("profile",)}}
    routes = {"main": "main-compare", "pull": "pull-compare"}
    for label, rows, mapping, expected in (
        ("every covered compare row is reachable", covered, legs, True),
        ("a deleted leg leaves its row unreachable", covered,
         {"main": legs["main"], "pull": {"self-host": ("lab",)}}, False),
        ("a renamed row leaves the mapping stale",
         [dict(row, id="profile@v2") if row["id"] == "profile" else row for row in covered], legs, False),
        ("a deleted row leaves the mapping stale", [row for row in covered if row["id"] != "profile"],
         {"main": legs["main"], "pull": {"self-host": ("lab",), "opt-in": ("profile",)}}, False),
        ("a leg naming a row of the other route fails", covered,
         {"main": {"self-host": ("lab@main", "lab")}, "pull": legs["pull"]}, False),
        ("a leg naming a NOT VALIDATED row fails",
         [dict(row, zen5={"route": "none", "status": "NOT VALIDATED"}) if row["id"] == "profile" else row
          for row in covered], legs, False),
        ("a mode without legs fails", covered, {"pull": legs["pull"]}, False),
        ("a leg without rows fails", covered, {"main": legs["main"], "pull": dict(legs["pull"], empty=())}, False),
    ):
        inventory = {"schema": "buster-performance-validation-v1", "entries": rows}
        if (not check_leg_rows(inventory, mapping, routes)) != expected:
            errors.append("self-check failed: " + label)
    consumer = "tools/x_evidence.c"
    source = ('"not Zen 5 evidence"\n{"work-reduction", "work-count", "callgrind", "hosted",\n'
              '{"native-runtime", "native-host", "time", "hosted",\n{"uarch", "zen5-native", "", "pull-compare",\n')
    classes = {PHYSICAL_CLASS: {"routes": ["pull-compare"], "establishes": "e", "does_not_establish": "d"},
               "work-count": {"consumer": consumer, "tools": ["callgrind"], "establishes": "e", "does_not_establish": "d"},
               "native-host": {"consumer": consumer, "tools": ["time"], "establishes": "e", "does_not_establish": "d"}}
    claims = {"work-reduction": {"class": "work-count", "tools": ["callgrind"]},
              "native-runtime": {"class": "native-host", "tools": ["time"]}, "uarch": {"class": PHYSICAL_CLASS, "tools": []}}
    policy = {"evidence_classes": classes, "claims": claims}
    for label, inventory, text, expected in (
        ("matching classes and claims pass", policy, source, True),
        ("a claim table drift fails", policy, source.replace('"time"', '"perf-stat"'), False),
        ("a uarch claim cannot name a hosted tool",
         dict(policy, claims=dict(claims, uarch={"class": PHYSICAL_CLASS, "tools": ["time"]})),
         source.replace('"zen5-native", ""', '"zen5-native", "time"'), False),
        ("a claim cannot borrow another class's tool",
         dict(policy, claims=dict(claims, **{"native-runtime": {"class": "native-host", "tools": ["callgrind"]}})),
         source.replace('"native-host", "time"', '"native-host", "callgrind"'), False),
        ("an alternative consumer that checks the approved host fails", policy, source + "APPROVED_HOST\n", False),
        ("an alternative consumer must disclaim Zen 5", policy, source.replace("not Zen 5 evidence", ""), False),
        ("the physical class cannot use a hosted route",
         dict(policy, evidence_classes=dict(classes, **{PHYSICAL_CLASS: dict(classes[PHYSICAL_CLASS], routes=["hosted"])})),
         source, False),
        ("the physical class cannot name an alternative consumer",
         dict(policy, evidence_classes=dict(classes, **{PHYSICAL_CLASS: dict(classes[PHYSICAL_CLASS], consumer=consumer)})),
         source, False),
        ("a missing alternative class fails",
         dict(policy, evidence_classes={k: v for k, v in classes.items() if k != "native-host"}), source, False),
    ):
        if (not check_classes(inventory, lambda path, text=text: text if path == consumer else "")) != expected:
            errors.append("self-check failed: " + label)
    alternative = {"status": "supported", "classes": ["work-count"], "consumer": consumer}
    for label, entry, expected in (
        ("a supported alternative row passes", dict(row, zen5=gap, alternative=alternative), True),
        ("an alternative cannot name the physical class",
         dict(row, zen5=gap, alternative=dict(alternative, classes=[PHYSICAL_CLASS])), False),
        ("an alternative must use its class's consumer",
         dict(row, zen5=gap, alternative=dict(alternative, consumer="tools/other.c")), False),
        ("an alternative needs a class", dict(row, zen5=gap, alternative=dict(alternative, classes=[])), False),
        ("a diagnostic row cannot carry an alternative",
         dict(row, validation="diagnostic", zen5={"route": "none", "status": "not applicable", "reason": "r"},
              alternative=alternative), False),
        ("an alternative consumer cannot discharge a covered zen5 row",
         dict(row, zen5=dict(row["zen5"], consumer=consumer)), False),
    ):
        inventory = {"schema": "buster-performance-validation-v1", "evidence_classes": classes, "entries": [entry]}
        problems, tallies = check_rows(inventory, {"tool:x.py"}, bench)
        if (not problems) != expected or (expected and tallies["alternative"] != 1):
            errors.append("self-check failed: " + label)
    for error in errors:
        print("PERFORMANCE_INVENTORY_FAIL " + error, file=sys.stderr)
    if not errors:
        print("PERFORMANCE_INVENTORY_PASS covered={covered} not_validated={not_validated} no_route={no_route} "
              "native_informative={native_informative} not_applicable={not_applicable} alternative={alternative}; Zen 5 "
              "validation is complete only for covered entries; native-informative timing and alternative evidence "
              "are never Zen 5 evidence".format(**counts))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
