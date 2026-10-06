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
DIRECT_REQUEST = WORKFLOWS / "9700x-direct-request.yml"
ACTIONLINT = ROOT / ".github" / "actionlint.yaml"
BENCHMARKING = ROOT / "docs" / "agents" / "benchmarking.md"
ADMISSION_GUIDE = ROOT / "benchmarks" / "9700x" / "ADMISSION.md"

# Re-running failed or single jobs reuses earlier successful job outputs, so
# the host job is bound to an authorization produced in this same attempt.
ATTEMPT_BINDING = "needs.authorize.outputs.attempt == format('{0}', github.run_attempt)"

# The direct workload path (#2704): main's definition, started by the request
# workflow's completion and run only for the owner's own same-repository pull
# requests. The gate reads the request run from the workflow_run payload; the
# authorize job re-reads it and the pull request author through the API.
DIRECT_TERMS = (
    "vars.BENCH_DIRECT_ENABLED == 'true'",
    "github.event_name == 'workflow_run'",
    "github.event.workflow_run.event == 'pull_request'",
    "github.event.workflow_run.conclusion == 'success'",
    "github.event.workflow_run.head_repository.full_name == github.repository",
    "github.event.workflow_run.actor.login == 'davidgmbb'",
    "github.event.workflow_run.actor.id == 39247043",
    "github.event.workflow_run.triggering_actor.login == 'davidgmbb'",
    "github.event.workflow_run.triggering_actor.id == 39247043",
    "(github.run_attempt == 1 || github.triggering_actor == 'davidgmbb')",
)
DIRECT_AUTHORIZE_IF = "    if: ${{ " + " && ".join(DIRECT_TERMS) + " }}"
DIRECT_RUN_IF = "    if: ${{ " + " && ".join((*DIRECT_TERMS, ATTEMPT_BINDING)) + " }}"
DIRECT_TRIGGER = (
    "on:",
    "  workflow_run:",
    "    workflows: [9700X direct workload request]",
    "    types: [completed]",
)
DIRECT_AUTHORIZE_BLOCKS = (
    ("    permissions:", "      actions: read", "      pull-requests: read", "    timeout-minutes: 5"),
    ("    outputs:", "      attempt: ${{ steps.verify.outputs.attempt }}",
     "      base: ${{ steps.verify.outputs.base }}"),
    (
        "        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
        "        with:",
        "          ref: ${{ github.sha }}",
        "          sparse-checkout: tools/bench_direct",
        "          persist-credentials: false",
    ),
    (
        "        id: verify",
        "        shell: bash",
        "        env:",
        "          GH_TOKEN: ${{ github.token }}",
        "          BQ_REPOSITORY: ${{ github.repository }}",
        "          BQ_REQUEST_RUN_ID: ${{ github.event.workflow_run.id }}",
        "          BQ_HEAD_COMMIT: ${{ github.event.workflow_run.head_sha }}",
        "          BQ_RUN_ATTEMPT: ${{ github.run_attempt }}",
        "        run: python3 -B tools/bench_direct/authorize.py",
    ),
)
DIRECT_AUTHORIZER_MARKERS = (
    'MAINTAINER = {"login": "davidgmbb", "id": 39247043}',
    'REQUEST_WORKFLOW = ".github/workflows/9700x-direct-request.yml"',
    '("request actor", identity(run.get("actor")) == MAINTAINER)',
    '("request triggering actor", identity(run.get("triggering_actor")) == MAINTAINER)',
    '("request head repository", full_name(run.get("head_repository")) == repository)',
    '("pull request author", identity(pull.get("user")) == MAINTAINER)',
    '("pull request head repository", full_name(pull["head"].get("repo")) == repository)',
    'stream.write(f"attempt={attempt}\\nbase={base}\\n")',
)
DIRECT_RUN_LINES = (
    "    needs: authorize",
    DIRECT_RUN_IF,
    "    runs-on:",
    "      group: buster-9700x-service-dispatch",
    "      labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]",
    "      BQ_BASE_COMMIT: ${{ needs.authorize.outputs.base }}",
    "      BQ_HEAD_COMMIT: ${{ github.event.workflow_run.head_sha }}",
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
        "          ref: ${{ github.event.workflow_run.head_sha }}",
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
# The request workflow is a marker only: its completion is the trigger.
DIRECT_REQUEST_TRIGGER = (
    "on:",
    "  pull_request:",
    "    types: [opened, synchronize, reopened]",
    "    paths:",
    "      - 'benchmarks/9700x/*.c'",
)

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
        if path not in (DIRECT, DIRECT_REQUEST) and "9700X direct workload request" in texts[path]:
            errors.append(f"only the direct workflow may follow the request workflow: {path.name}")
        if "pull_request_target" in texts[path]:
            errors.append(f"pull_request_target is forbidden repository-wide: {path.name}")
    check_direct_workflow(errors)
    return report(errors)


def check_direct_workflow(errors: list[str]) -> None:
    """The direct path runs main's gate and harness for the owner's pull requests."""
    authorizer = ROOT / "tools" / "bench_direct" / "authorize.py"
    harness = ROOT / "tools" / "bench_direct" / "run_workloads.py"
    for path in (DIRECT, DIRECT_REQUEST, authorizer, harness):
        if not path.is_file():
            errors.append(f"missing direct workload file: {path.relative_to(ROOT)}")
    if any(not path.is_file() for path in (DIRECT, DIRECT_REQUEST, authorizer, harness)):
        return
    direct = DIRECT.read_text(encoding="utf-8")
    lines = direct.splitlines()
    jobs = job_blocks(direct)
    if list(jobs) != ["authorize", "bench"]:
        errors.append(f"direct workflow jobs must be authorize then bench: {list(jobs)}")
    authorize, run = jobs.get("authorize", []), jobs.get("bench", [])

    # The trigger block is exact: no other event or workflow may start it.
    start = lines.index("on:") if "on:" in lines else len(lines)
    trigger = [line for line in lines[start:start + len(DIRECT_TRIGGER) + 1] if line.strip()]
    if tuple(trigger) != DIRECT_TRIGGER:
        errors.append("direct workflow trigger must be exactly the reviewed workflow_run block")
    declarations = [line.rstrip() for line in lines if line.lstrip().startswith("permissions:")]
    if declarations != ["permissions: {}", "    permissions:"]:
        errors.append("direct workflow must grant no GITHUB_TOKEN permissions beyond authorize")

    if DIRECT_AUTHORIZE_IF not in authorize:
        errors.append("direct authorize job is missing its exact condition")
    for block in DIRECT_AUTHORIZE_BLOCKS:
        if not contains_block(authorize, block):
            errors.append(f"direct authorize job is missing exact block starting: {block[0].strip()}")
    if len([line for line in authorize if "uses:" in line]) != 1 or \
            len([line for line in authorize if "run:" in line]) != 1:
        errors.append("direct authorize job must be one trusted checkout and one authorizer call")
    for marker in ("buster-zen5", "ryzen-9700x", "self-hosted", "workflow_run.head_branch", "path: candidate"):
        if any(marker in line for line in authorize):
            errors.append(f"direct authorize job must not use: {marker}")
    source = authorizer.read_text(encoding="utf-8")
    for marker in DIRECT_AUTHORIZER_MARKERS:
        if marker not in source:
            errors.append(f"direct authorizer is missing check: {marker}")
    if source.count("GITHUB_OUTPUT") != 1 or source.count("stream.write(") != 1:
        errors.append("direct authorizer must write its outputs once, after every check")

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
    for marker in ("workflow_dispatch:", "pull_request:", "pull_request_target", "push:", "schedule:",
                   "repository_dispatch:", "issue_comment:", "secrets.", "inputs.", "wget ", " ssh ",
                   "https://"):
        if marker in direct:
            errors.append(f"direct workflow contains forbidden path: {marker}")

    request = DIRECT_REQUEST.read_text(encoding="utf-8")
    request_lines = request.splitlines()
    if "name: 9700X direct workload request" not in request_lines:
        errors.append("request workflow name must match the direct workflow's trigger")
    start = request_lines.index("on:") if "on:" in request_lines else len(request_lines)
    trigger = [line for line in request_lines[start:start + len(DIRECT_REQUEST_TRIGGER) + 1] if line.strip()]
    if tuple(trigger) != DIRECT_REQUEST_TRIGGER:
        errors.append("request workflow trigger must be exactly the reviewed pull_request block")
    if [line.rstrip() for line in request_lines if line.lstrip().startswith("permissions:")] != ["permissions: {}"]:
        errors.append("request workflow must grant no GITHUB_TOKEN permissions")
    for marker in ("uses:", "self-hosted", "buster-zen5", "ryzen-9700x", "group: buster-9700x-service",
                   "secrets.", "github.token", "${{ github.event.pull_request.head", "environment:"):
        if marker in request:
            errors.append(f"request workflow must not use: {marker}")
    if expression_lines_in_scripts(request):
        errors.append("request workflow must not interpolate an expression inside a run script")


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
