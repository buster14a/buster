#!/usr/bin/env python3
"""Fail closed if Actions can reach the 9700X outside the owner's direct paths.

`.github/workflows/9700x-direct-bench.yml` (#2704) is the only workflow that
may select the benchmark runner. Its trigger, gates, checkouts and run scripts
are pinned here line for line, because the gates are the whole access control:
the host jobs compile and run pull-request or merge-group code as the runner
account. The merge-group compiler comparison (#2752) is pinned the same way.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
DIRECT = WORKFLOWS / "9700x-direct-bench.yml"
DIRECT_REQUEST = WORKFLOWS / "9700x-direct-request.yml"
COMPILER_REQUEST = WORKFLOWS / "9700x-compiler-request.yml"
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
    "    workflows: [9700X direct workload request, 9700X compiler benchmark request]",
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

# The merge-group compiler comparison (#2752): main's definition, started by
# the merge_group request marker's completion, only while both variables are
# set. GitHub starts queue events, so the actor is not the authority; the
# authorizer re-reads the queue ref, the group commit and its pull request.
COMPILER_TERMS = (
    "vars.BENCH_DIRECT_ENABLED == 'true'",
    "vars.BENCH_COMPILER_ENABLED == 'true'",
    "github.event_name == 'workflow_run'",
    "github.event.workflow_run.event == 'merge_group'",
    "github.event.workflow_run.path == '.github/workflows/9700x-compiler-request.yml'",
    "github.event.workflow_run.conclusion == 'success'",
    "github.event.workflow_run.head_repository.full_name == github.repository",
    "(github.run_attempt == 1 || github.triggering_actor == 'davidgmbb')",
)
COMPILER_AUTHORIZE_IF = "    if: ${{ " + " && ".join(COMPILER_TERMS) + " }}"
COMPILER_ATTEMPT = "needs.authorize-compiler.outputs.attempt == format('{0}', github.run_attempt)"
COMPILER_RUN_IF = "    if: ${{ " + " && ".join((*COMPILER_TERMS, COMPILER_ATTEMPT)) + " }}"
COMPILER_PUBLISH_IF = "    if: ${{ " + " && ".join(("always()", *COMPILER_TERMS)) + " }}"
TRUSTED_TOOLS_CHECKOUT = (
    "        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
    "        with:",
    "          ref: ${{ github.sha }}",
    "          sparse-checkout: tools/bench_direct",
    "          persist-credentials: false",
)
COMPILER_AUTHORIZE_BLOCKS = (
    ("    runs-on: ubuntu-24.04",),
    ("    permissions:", "      contents: read", "      pull-requests: read", "      actions: read",
     "    timeout-minutes: 5"),
    TRUSTED_TOOLS_CHECKOUT,
    (
        "        id: verify",
        "        shell: bash",
        "        env:",
        "          GH_TOKEN: ${{ github.token }}",
        "          BQ_REPOSITORY: ${{ github.repository }}",
        "          BQ_REQUEST_RUN_ID: ${{ github.event.workflow_run.id }}",
        "          BQ_HEAD_COMMIT: ${{ github.event.workflow_run.head_sha }}",
        "          BQ_HEAD_BRANCH: ${{ github.event.workflow_run.head_branch }}",
        "          BQ_RUN_ATTEMPT: ${{ github.run_attempt }}",
        "        run: python3 -B tools/bench_direct/authorize_compiler.py",
    ),
)
COMPILER_AUTHORIZER_MARKERS = (
    "from authorize import COMMIT, DECIMAL, MAINTAINER, REPOSITORY, fetch, full_name, identity",
    'REQUEST_WORKFLOW = ".github/workflows/9700x-compiler-request.yml"',
    '("request event", run.get("event") == "merge_group")',
    '("live queue ref names the head")',
    '("pull request author", identity(pull.get("user")) == MAINTAINER)',
    '("pull request head repository", full_name(pull_head_record.get("repo")) == repository)',
    '("pull request head is the second parent", bool(pull_head) and pull_head_record.get("sha") == pull_head)',
)
COMPILER_RUN_LINES = (
    "    needs: authorize-compiler",
    COMPILER_RUN_IF,
    "    runs-on:",
    "      group: buster-9700x-service-dispatch",
    "      labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]",
    "      BQ_HEAD_COMMIT: ${{ github.event.workflow_run.head_sha }}",
    "      BQ_TRUSTED_REVISION: ${{ github.sha }}",
)
COMPILER_CHECKOUTS = (
    (
        "        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
        "        with:",
        "          ref: ${{ github.sha }}",
        "          path: trusted",
        "          sparse-checkout: tools",
        "          persist-credentials: false",
    ),
    (
        "        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
        "        with:",
        "          ref: ${{ github.event.workflow_run.head_sha }}",
        "          path: candidate",
        "          fetch-depth: 2",
        "          persist-credentials: false",
    ),
    (
        "        if: ${{ always() }}",
        "        uses: actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02 # v4.6.2",
        "        with:",
        "          name: buster-9700x-compiler-${{ github.event.workflow_run.head_sha }}-${{ github.run_attempt }}",
        "          path: ${{ runner.temp }}/compiler-bench/evidence",
    ),
)
COMPILER_RUN_SCRIPT = [
    "          set -euo pipefail",
    "          for commit in \"$BQ_BASE_COMMIT\" \"$BQ_BASE_TREE\" \"$BQ_HEAD_COMMIT\" \"$BQ_HEAD_TREE\" \"$BQ_PULL_HEAD\" \"$BQ_TRUSTED_REVISION\"; do",
    "            [[ \"$commit\" =~ ^[0-9a-f]{40}$ ]]",
    "          done",
    "          [[ \"$BQ_PULL\" =~ ^[1-9][0-9]*$ ]]",
    "          [[ \"$(git -C candidate rev-parse HEAD)\" == \"$BQ_HEAD_COMMIT\" ]]",
    "          python3 -B trusted/tools/bench_direct/compiler_compare.py \\",
    "            --candidate candidate --lab trusted/tools/uarch_lab.py \\",
    "            --work \"$RUNNER_TEMP/compiler-bench/work\" --evidence \"$RUNNER_TEMP/compiler-bench/evidence\" \\",
    "            --summary \"$GITHUB_STEP_SUMMARY\" --repository \"$BQ_REPOSITORY\" --queue-branch \"$BQ_QUEUE_BRANCH\" \\",
    "            --pull \"$BQ_PULL\" --pull-head \"$BQ_PULL_HEAD\" --base \"$BQ_BASE_COMMIT\" --base-tree \"$BQ_BASE_TREE\" \\",
    "            --head \"$BQ_HEAD_COMMIT\" --head-tree \"$BQ_HEAD_TREE\" --trusted-revision \"$BQ_TRUSTED_REVISION\" \\",
    "            --request-run-id \"$BQ_REQUEST_RUN_ID\" --run-id \"$BQ_RUN_ID\" --run-attempt \"$BQ_RUN_ATTEMPT\"",
]
COMPILER_PUBLISH_BLOCKS = (
    ("    needs: [authorize-compiler, compare]", COMPILER_PUBLISH_IF, "    runs-on: ubuntu-24.04",
     "    permissions:", "      actions: read", "      checks: write", "    timeout-minutes: 10"),
    TRUSTED_TOOLS_CHECKOUT,
    ("          BQ_AUTHORIZE_RESULT: ${{ needs.authorize-compiler.result }}",
     "          BQ_AUTHORIZED_ATTEMPT: ${{ needs.authorize-compiler.outputs.attempt }}",
     "          BQ_COMPARE_RESULT: ${{ needs.compare.result }}"),
    ("        run: python3 -B tools/bench_direct/compiler_publish.py",),
)
COMPILER_REQUEST_TRIGGER = (
    "on:",
    "  merge_group:",
    "    types: [checks_requested]",
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
        "## Merge-group compiler comparison",
        "BENCH_COMPILER_ENABLED",
        "The performance verdict is report-only.",
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
        for name in ("9700X direct workload request", "9700X compiler benchmark request"):
            if path not in (DIRECT, DIRECT_REQUEST, COMPILER_REQUEST) and name in texts[path]:
                errors.append(f"only the direct workflow may follow the request workflow: {path.name}")
        if "pull_request_target" in texts[path]:
            errors.append(f"pull_request_target is forbidden repository-wide: {path.name}")
    check_direct_workflow(errors)
    check_compiler_path(errors)
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
    if list(jobs) != ["authorize", "bench", "authorize-compiler", "compare", "publish-compiler"]:
        errors.append(f"direct workflow jobs must be authorize, bench, authorize-compiler, compare, "
                      f"publish-compiler: {list(jobs)}")
    authorize, run = jobs.get("authorize", []), jobs.get("bench", [])

    # The trigger block is exact: no other event or workflow may start it.
    start = lines.index("on:") if "on:" in lines else len(lines)
    trigger = [line for line in lines[start:start + len(DIRECT_TRIGGER) + 1] if line.strip()]
    if tuple(trigger) != DIRECT_TRIGGER:
        errors.append("direct workflow trigger must be exactly the reviewed workflow_run block")
    declarations = [line.rstrip() for line in lines if line.lstrip().startswith("permissions:")]
    if declarations != ["permissions: {}", "    permissions:", "    permissions:", "    permissions:"]:
        errors.append("direct workflow must grant GITHUB_TOKEN permissions only to its hosted authorize "
                      "and publish jobs")

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


def check_compiler_path(errors: list[str]) -> None:
    """The merge-group comparison: main's gate, an unprivileged host job, hosted publication."""
    authorizer = ROOT / "tools" / "bench_direct" / "authorize_compiler.py"
    required = (DIRECT, COMPILER_REQUEST, authorizer, *(ROOT / "tools" / "bench_direct" / name for name in (
        "compiler_compare.py", "compiler_publish.py", "compiler_receipt.py")), ROOT / "tools" / "uarch_lab.py")
    for path in required:
        if not path.is_file():
            errors.append(f"missing compiler comparison file: {path.relative_to(ROOT)}")
    if any(not path.is_file() for path in required):
        return
    jobs = job_blocks(DIRECT.read_text(encoding="utf-8"))
    authorize, compare, publish = (jobs.get(name, []) for name in ("authorize-compiler", "compare", "publish-compiler"))
    for name, job, condition in (("authorize-compiler", authorize, COMPILER_AUTHORIZE_IF),
                                 ("compare", compare, COMPILER_RUN_IF), ("publish-compiler", publish, COMPILER_PUBLISH_IF)):
        if [line for line in job if line.startswith("    if:")] != [condition]:
            errors.append(f"compiler {name} job is missing its exact condition")
    for block in COMPILER_AUTHORIZE_BLOCKS:
        if not contains_block(authorize, block):
            errors.append(f"compiler authorize job is missing exact block starting: {block[0].strip()}")
    if len([line for line in authorize if "uses:" in line]) != 1 or \
            len([line for line in authorize if "run:" in line]) != 1:
        errors.append("compiler authorize job must be one trusted checkout and one authorizer call")
    source = authorizer.read_text(encoding="utf-8")
    for marker in COMPILER_AUTHORIZER_MARKERS:
        if marker not in source:
            errors.append(f"compiler authorizer is missing check: {marker}")
    if source.count("GITHUB_OUTPUT") != 1 or source.count("stream.write(") != 1:
        errors.append("compiler authorizer must write its outputs once, after every check")

    for line in COMPILER_RUN_LINES:
        if line not in compare:
            errors.append(f"compiler compare job is missing exact line: {line.strip()}")
    for marker in ("GH_TOKEN", "github.token", "curl ", "api.github.com", "permissions:", "sudo",
                   "environment:", "buster-bench", "secrets.", "--sudo", "--profile-steps"):
        if any(marker in line for line in compare):
            errors.append(f"compiler compare job must not use: {marker}")
    uses = [line for line in compare if "uses:" in line]
    if len(uses) != len(COMPILER_CHECKOUTS) or not all(contains_block(compare, block) for block in COMPILER_CHECKOUTS):
        errors.append("compiler compare job must use exactly two credential-free checkouts and the evidence upload")
    if run_scripts(compare) != [COMPILER_RUN_SCRIPT]:
        errors.append("compiler compare job must run only main's harness with validated identities")

    for block in COMPILER_PUBLISH_BLOCKS:
        if not contains_block(publish, block):
            errors.append(f"compiler publish job is missing exact block starting: {block[0].strip()}")
    if len([line for line in publish if "uses:" in line]) != 1 or \
            len([line for line in publish if "run:" in line]) != 1:
        errors.append("compiler publish job must be one trusted checkout and one publisher call")
    for name, job in (("authorize-compiler", authorize), ("publish-compiler", publish)):
        for marker in ("buster-zen5", "ryzen-9700x", "self-hosted", "path: candidate", "contents: write",
                       "pull-requests: write", "actions: write", "statuses: write"):
            if any(marker in line for line in job):
                errors.append(f"compiler {name} job must not use: {marker}")

    request = COMPILER_REQUEST.read_text(encoding="utf-8")
    request_lines = request.splitlines()
    if "name: 9700X compiler benchmark request" not in request_lines:
        errors.append("compiler request workflow name must match the direct workflow's trigger")
    start = request_lines.index("on:") if "on:" in request_lines else len(request_lines)
    trigger = [line for line in request_lines[start:start + len(COMPILER_REQUEST_TRIGGER) + 1] if line.strip()]
    if tuple(trigger) != COMPILER_REQUEST_TRIGGER:
        errors.append("compiler request workflow trigger must be exactly the reviewed merge_group block")
    if [line.rstrip() for line in request_lines if line.lstrip().startswith("permissions:")] != ["permissions: {}"]:
        errors.append("compiler request workflow must grant no GITHUB_TOKEN permissions")
    for marker in ("uses:", "self-hosted", "buster-zen5", "ryzen-9700x", "group: buster-9700x-service",
                   "secrets.", "github.token", "environment:", "pull_request", "workflow_dispatch"):
        if marker in request:
            errors.append(f"compiler request workflow must not use: {marker}")
    if expression_lines_in_scripts(request):
        errors.append("compiler request workflow must not interpolate an expression inside a run script")


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
    print("WORKFLOW_POLICY_PASS the owner direct paths are the only 9700X Actions paths")
    return 0


if __name__ == "__main__":
    sys.exit(main())
