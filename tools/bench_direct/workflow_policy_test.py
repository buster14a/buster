#!/usr/bin/env python3
"""Fail closed if Actions can reach the 9700X outside the owner's direct path.

`.github/workflows/9700x-direct-bench.yml` (#2704) is the only workflow that
may select the benchmark runner. Its trigger, gate, checkouts and run script
are pinned here line for line, because the gate is the whole access control:
the job compiles and runs pull-request code as the runner account.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
DIRECT = WORKFLOWS / "9700x-direct-bench.yml"
HARNESS = ROOT / "tools" / "bench_direct" / "run_workloads.py"
ACTIONLINT = ROOT / ".github" / "actionlint.yaml"
BENCHMARKING = ROOT / "docs" / "agents" / "benchmarking.md"
ADMISSION_GUIDE = ROOT / "benchmarks" / "9700x" / "ADMISSION.md"

# Re-running failed or single jobs reuses earlier successful job outputs, so
# the host job is bound to an authorization produced in this same attempt.
ATTEMPT_BINDING = "needs.authorize.outputs.attempt == format('{0}', github.run_attempt)"
AUTHORIZE_OUTPUT = "          printf 'attempt=%s\\n' \"$BQ_RUN_ATTEMPT\" >> \"$GITHUB_OUTPUT\""

# The direct workload path (#2704): main's definition, run for the owner's own
# same-repository pull requests only. The pull request author is part of the
# gate because pull_request_target runs for every pull request.
DIRECT_TERMS = (
    "vars.BENCH_DIRECT_ENABLED == 'true'",
    "github.event_name == 'pull_request_target'",
    "github.event.pull_request.user.login == 'davidgmbb'",
    "github.event.pull_request.user.id == 39247043",
    "github.event.pull_request.head.repo.full_name == github.repository",
    "github.actor == 'davidgmbb'",
    "github.actor_id == '39247043'",
    "github.triggering_actor == 'davidgmbb'",
)
DIRECT_AUTHORIZE_IF = "    if: ${{ " + " && ".join(DIRECT_TERMS) + " }}"
DIRECT_RUN_IF = "    if: ${{ " + " && ".join((*DIRECT_TERMS, ATTEMPT_BINDING)) + " }}"
DIRECT_TRIGGER = (
    "on:",
    "  pull_request_target:",
    "    types: [opened, synchronize, reopened]",
    "    paths:",
    "      - 'benchmarks/9700x/*.c'",
)
DIRECT_AUTHORIZE_LINES = (
    DIRECT_AUTHORIZE_IF,
    "    runs-on: ubuntu-24.04",
    "    outputs:",
    "      attempt: ${{ steps.verify.outputs.attempt }}",
    "        id: verify",
    "          GH_TOKEN: ${{ github.token }}",
    "          BQ_REPOSITORY: ${{ github.repository }}",
    "          BQ_RUN_ID: ${{ github.run_id }}",
    "          BQ_RUN_ATTEMPT: ${{ github.run_attempt }}",
    "          [[ \"$BQ_REPOSITORY\" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]",
    "          [[ \"$BQ_RUN_ID\" =~ ^[1-9][0-9]*$ ]]",
    "          [[ \"$BQ_RUN_ATTEMPT\" =~ ^[1-9][0-9]*$ ]]",
    "          printf 'Authorization: Bearer %s\\n' \"$GH_TOKEN\" |",
    "            curl --fail --silent --show-error --proto '=https' --max-time 30 --retry 3 \\",
    "              \"https://api.github.com/repos/$BQ_REPOSITORY/actions/runs/"
    "$BQ_RUN_ID/attempts/$BQ_RUN_ATTEMPT\"",
    "          maintainer = {\"login\": \"davidgmbb\", \"id\": 39247043}",
    "              (\"run id\", type(run.get(\"id\")) is int and run.get(\"id\") == run_id),",
    "              (\"event\", run.get(\"event\") == \"pull_request_target\"),",
    "              (\"actor\", identity(\"actor\") == maintainer),",
    "              (\"triggering actor\", identity(\"triggering_actor\") == maintainer),",
    "              (\"run attempt\", type(run.get(\"run_attempt\")) is int and",
    "               run.get(\"run_attempt\") == run_attempt),",
    "              sys.exit(\"BENCH_DIRECT_UNAUTHORIZED \" + \", \".join(failures))",
)
DIRECT_RUN_LINES = (
    "    needs: authorize",
    DIRECT_RUN_IF,
    "    runs-on:",
    "      group: buster-9700x-service-dispatch",
    "      labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]",
    "      BQ_BASE_COMMIT: ${{ github.event.pull_request.base.sha }}",
    "      BQ_HEAD_COMMIT: ${{ github.event.pull_request.head.sha }}",
)
DIRECT_CHECKOUTS = (
    (
        "        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
        "        with:",
        "          ref: ${{ github.sha }}",
        "          path: trusted",
        "          sparse-checkout: tools/bench_direct",
        "          persist-credentials: false",
    ),
    (
        "        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
        "        with:",
        "          ref: ${{ github.event.pull_request.head.sha }}",
        "          path: candidate",
        "          fetch-depth: 0",
        "          filter: blob:none",
        "          sparse-checkout: benchmarks/9700x",
        "          persist-credentials: false",
    ),
)
DIRECT_RUN_SCRIPT = [
    "          set -euo pipefail",
    "          [[ \"$BQ_BASE_COMMIT\" =~ ^[0-9a-f]{40}$ ]]",
    "          [[ \"$BQ_HEAD_COMMIT\" =~ ^[0-9a-f]{40}$ ]]",
    "          [[ \"$(git -C candidate rev-parse HEAD)\" == \"$BQ_HEAD_COMMIT\" ]]",
    "          python3 -B trusted/tools/bench_direct/run_workloads.py \\",
    "            --candidate candidate --base \"$BQ_BASE_COMMIT\" --head \"$BQ_HEAD_COMMIT\" \\",
    "            --work \"$RUNNER_TEMP/direct-bench\" --summary \"$GITHUB_STEP_SUMMARY\"",
]

DOCUMENTATION_REQUIREMENTS = {
    ACTIONLINT: (
        ".github/workflows/9700x-direct-bench.yml",
        "only by the direct 9700X workload workflow",
    ),
    BENCHMARKING: (
        "The dedicated Ryzen 7 9700X is not a general GitHub Actions",
        ".github/workflows/9700x-direct-bench.yml",
    ),
    ADMISSION_GUIDE: (
        "## Gate",
        "## Administrator steps",
        ".github/workflows/9700x-direct-bench.yml@refs/heads/main",
        "must use **Re-run all jobs**",
    ),
}


def main() -> int:
    errors: list[str] = []
    for path, markers in DOCUMENTATION_REQUIREMENTS.items():
        if not path.is_file():
            errors.append(f"missing policy document: {path.relative_to(ROOT)}")
            continue
        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in text:
                errors.append(f"{path.relative_to(ROOT)} is missing policy marker: {marker}")

    workflows = sorted((*WORKFLOWS.glob("*.yml"), *WORKFLOWS.glob("*.yaml")))
    texts = {path: path.read_text(encoding="utf-8") for path in workflows}
    benchmark_users = [path.name for path in workflows
                       if "buster-zen5" in texts[path] or "ryzen-9700x" in texts[path]]
    if benchmark_users != [DIRECT.name]:
        errors.append(f"benchmark labels are not exclusive to the direct workflow: {benchmark_users}")
    for path in workflows:
        if path != DIRECT and "pull_request_target" in texts[path] and "self-hosted" in texts[path]:
            errors.append(f"only the direct workflow may pair pull_request_target with self-hosted: {path.name}")
    check_direct_workflow(errors)
    return report(errors)


def check_direct_workflow(errors: list[str]) -> None:
    """The direct path runs main's harness for the owner's own pull requests."""
    if not DIRECT.is_file():
        errors.append("missing 9700x-direct-bench.yml")
        return
    direct = DIRECT.read_text(encoding="utf-8")
    lines = direct.splitlines()
    jobs = job_blocks(direct)
    if list(jobs) != ["authorize", "bench"]:
        errors.append(f"direct workflow jobs must be authorize then bench: {list(jobs)}")
    authorize, run = jobs.get("authorize", []), jobs.get("bench", [])

    # The trigger block is exact: no other event, type or path may start it.
    start = lines.index("on:") if "on:" in lines else len(lines)
    trigger = [line for line in lines[start:start + len(DIRECT_TRIGGER) + 1] if line.strip()]
    if tuple(trigger) != DIRECT_TRIGGER:
        errors.append("direct workflow trigger must be exactly the reviewed pull_request_target block")
    declarations = [line.rstrip() for line in lines if line.lstrip().startswith("permissions:")]
    if declarations != ["permissions: {}", "    permissions:"]:
        errors.append("direct workflow must grant no GITHUB_TOKEN permissions beyond authorize")
    if not contains_block(authorize, ("    permissions:", "      actions: read", "    timeout-minutes: 5")):
        errors.append("direct authorize job must be granted exactly actions: read")

    for line in DIRECT_AUTHORIZE_LINES:
        if line not in authorize:
            errors.append(f"direct authorize job is missing exact line: {line.strip()}")
    scripts = run_scripts(authorize)
    if len(scripts) != 1 or not scripts[0] or scripts[0][-1] != AUTHORIZE_OUTPUT or \
            sum("GITHUB_OUTPUT" in line for line in scripts[0]) != 1:
        errors.append("direct authorize job must emit its attempt once, after every verification passes")
    for marker in ("buster-zen5", "ryzen-9700x", "self-hosted", "uses:", "pull_request.head"):
        if any(marker in line for line in authorize if not line.startswith("    if:")):
            errors.append(f"direct authorize job must not use: {marker}")

    for line in DIRECT_RUN_LINES:
        if line not in run:
            errors.append(f"direct bench job is missing exact line: {line.strip()}")
    for name, job in (("authorize", authorize), ("bench", run)):
        if len([line for line in job if line.startswith("    if:")]) != 1:
            errors.append(f"direct {name} job must have exactly one condition")
    for marker in ("GH_TOKEN", "github.token", "curl ", "api.github.com", "permissions:", "sudo",
                   "environment:", "buster-bench"):
        if any(marker in line for line in run):
            errors.append(f"direct bench job must not use: {marker}")
    uses = [line for line in run if "uses:" in line]
    if len(uses) != len(DIRECT_CHECKOUTS) or not all(contains_block(run, block) for block in DIRECT_CHECKOUTS):
        errors.append("direct bench job must use exactly the two reviewed credential-free checkouts")
    if run_scripts(run) != [DIRECT_RUN_SCRIPT]:
        errors.append("direct bench job must run only main's harness with validated commit IDs")

    for number, line in expression_lines_in_scripts(direct):
        errors.append(f"direct workflow line {number} interpolates an expression inside a run script")
    for marker in ("workflow_dispatch:", "pull_request:", "push:", "schedule:", "repository_dispatch:",
                   "workflow_run:", "issue_comment:", "secrets.", "inputs.", "wget ", " ssh "):
        if marker in direct:
            errors.append(f"direct workflow contains forbidden path: {marker}")
    if sum("https://" in line for line in lines) != 1:
        errors.append("direct workflow may fetch only authorize's fixed run-attempt API URL")
    if not HARNESS.is_file():
        errors.append("missing direct workload harness")


def job_blocks(workflow: str) -> dict[str, list[str]]:
    """Map each top-level job to its lines, excluding comments between jobs."""
    lines = workflow.splitlines()
    start = lines.index("jobs:") + 1 if "jobs:" in lines else len(lines)
    jobs: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in lines[start:]:
        header = re.fullmatch(r"  ([A-Za-z0-9_-]+):", line)
        if header:
            current = jobs.setdefault(header.group(1), [])
        elif line.strip() and not line.startswith("   "):
            # A comment between jobs or a later top-level key ends the job.
            current = None
        elif current is not None:
            current.append(line)
    return jobs


def contains_block(lines: list[str], block: tuple[str, ...]) -> bool:
    return any(tuple(lines[index:index + len(block)]) == block for index in range(len(lines)))


def run_scripts(lines: list[str]) -> list[list[str]]:
    """Return the non-empty lines of every literal `run: |` script."""
    scripts: list[list[str]] = []
    indent = None
    for line in lines:
        if indent is not None:
            if not line.strip() or len(line) - len(line.lstrip()) > indent:
                if line.strip():
                    scripts[-1].append(line)
                continue
            indent = None
        match = re.fullmatch(r"( *)(?:- )?run: \|", line)
        if match:
            indent = len(match.group(1))
            scripts.append([])
    return scripts


def expression_lines_in_scripts(workflow: str) -> list[tuple[int, str]]:
    """Find `${{ }}` inside run scripts, including a one-line `run:` value."""
    found: list[tuple[int, str]] = []
    indent = None
    for number, line in enumerate(workflow.splitlines(), 1):
        if indent is not None:
            if not line.strip() or len(line) - len(line.lstrip()) > indent:
                if "${{" in line:
                    found.append((number, line))
                continue
            indent = None
        match = re.fullmatch(r"( *)(?:- )?run:(.*)", line)
        if match:
            if match.group(2).strip() in ("|", ">", "|-", ">-", "|+", ">+", ""):
                indent = len(match.group(1))
            elif "${{" in match.group(2):
                found.append((number, line))
    return found


def report(errors: list[str]) -> int:
    if errors:
        for error in errors:
            print(f"WORKFLOW_POLICY_FAIL {error}", file=sys.stderr)
        return 1
    print("WORKFLOW_POLICY_PASS the owner direct path is the only 9700X Actions path")
    return 0


if __name__ == "__main__":
    sys.exit(main())
