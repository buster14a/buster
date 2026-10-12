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
when the profile is its only Zen 5 route, as for `build:production_profile`
and the two compiler-internal probes of compiler-probes-v1) whose consumer
validates that exact frozen profile, so a flag-pair profile cannot be added,
or its validation dropped, without the inventory noticing.

Map: discover, check_rows, check_native_row, check_profile_rows, main.
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
    "build:bench_throughput@throughput-real-source": "THROUGHPUT_REAL_SOURCE_PROFILE",
    "build:production_profile": "PRODUCTION_PROFILE_PROFILE",
    "tool:fast_ra_scaling.c": "COMPILER_PROBES_PROFILE",
    "tool:machine_line_mark_scaling.c": "COMPILER_PROBES_PROFILE",
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
    counts = {"covered": 0, "not_validated": 0, "no_route": 0, "native_informative": 0, "not_applicable": 0}
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
        errors.extend(check_profile_rows(data, RECEIPT.read_text(encoding="utf-8"),
                                         lambda path: (ROOT / path).read_text(encoding="utf-8") if (ROOT / path).is_file() else ""))
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
    for error in errors:
        print("PERFORMANCE_INVENTORY_FAIL " + error, file=sys.stderr)
    if not errors:
        print("PERFORMANCE_INVENTORY_PASS covered={covered} not_validated={not_validated} no_route={no_route} "
              "native_informative={native_informative} not_applicable={not_applicable}; performance validation is "
              "complete only for covered entries, and native-informative timing is never Zen 5 evidence".format(**counts))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
