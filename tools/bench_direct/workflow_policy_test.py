#!/usr/bin/env python3
"""Fail closed if Actions can reach the 9700X outside the owner's direct paths.

`.github/workflows/9700x-direct-bench.yml` (#2704) is the only workflow that
may select the benchmark runner. Its trigger, gates, checkouts and run scripts
are pinned here line for line, because the gates are the whole access control:
the host jobs compile and run explicitly requested owner pull-request code or
already-landed main code as the runner account. Routine comparisons are
post-merge (#3087), and RAD Debugger diagnostics are main-only (#3086).
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
DIRECT = WORKFLOWS / "9700x-direct-bench.yml"
DIRECT_REQUEST = WORKFLOWS / "9700x-direct-request.yml"
COMPILER_REQUEST = WORKFLOWS / "9700x-compiler-request.yml"
COMPILER_REPORT = WORKFLOWS / "9700x-compiler-report.yml"
LIFECYCLE = WORKFLOWS / "9700x-lifecycle.yml"
ACTIONLINT = ROOT / ".github" / "actionlint.yaml"
BENCHMARKING = ROOT / "docs" / "agents" / "benchmarking.md"
ADMISSION_GUIDE = ROOT / "benchmarks" / "9700x" / "ADMISSION.md"
RADDEBUGGER = WORKFLOWS / "raddebugger-compatibility.yml"

# Re-running failed or single jobs reuses earlier successful job outputs, so
# the host job is bound to an authorization produced in this same attempt.
ATTEMPT_BINDING = "needs.authorize.outputs.attempt == format('{0}', github.run_attempt)"
REQUEST_BINDING = "needs.authorize.outputs.request_head == github.event.workflow_run.head_sha"

# The direct workload path (#2704): main's definition, started by the request
# workflow's completion and run only for the owner's own same-repository pull
# requests. The gate reads the request run from the workflow_run payload; the
# authorize job re-reads it and the pull request author through the API.
DIRECT_TERMS = (
    "vars.BENCH_DIRECT_ENABLED == 'true'",
    "github.event_name == 'workflow_run'",
    "github.event.workflow_run.event == 'pull_request'",
    "github.event.workflow_run.path == '.github/workflows/9700x-direct-request.yml'",
    "github.event.workflow_run.conclusion == 'success'",
    "github.event.workflow_run.head_repository.full_name == github.repository",
    "github.event.workflow_run.actor.login == 'davidgmbb'",
    "github.event.workflow_run.actor.id == 39247043",
    "github.event.workflow_run.triggering_actor.login == 'davidgmbb'",
    "github.event.workflow_run.triggering_actor.id == 39247043",
    "(github.run_attempt == 1 || github.triggering_actor == 'davidgmbb')",
)
DIRECT_AUTHORIZE_IF = "    if: ${{ " + " && ".join(DIRECT_TERMS) + " }}"
DIRECT_RUN_IF = "    if: ${{ " + " && ".join((*DIRECT_TERMS, ATTEMPT_BINDING, REQUEST_BINDING, "needs.authorize.outputs.workloads == 'true'")) + " }}"
# The pull-request compiler comparison (#2769) shares the direct gate.
PULL_RUN_IF = "    if: ${{ " + " && ".join((*DIRECT_TERMS, ATTEMPT_BINDING, REQUEST_BINDING, "needs.authorize.outputs.compare == 'true'")) + " }}"
PULL_PUBLISH_IF = "    if: ${{ " + " && ".join(("always()", *DIRECT_TERMS, "needs.authorize.outputs.compare == 'true'")) + " }}"
DIRECT_TRIGGER = (
    "on:",
    "  workflow_run:",
    "    workflows: [9700X direct workload request, 9700X compiler benchmark request]",
    "    types: [completed]",
)
DIRECT_AUTHORIZE_BLOCKS = (
    ("    permissions:", "      contents: read", "      actions: read", "      pull-requests: read", "    timeout-minutes: 5"),
    ("    outputs:", "      attempt: ${{ steps.verify.outputs.attempt }}",
     "      base: ${{ steps.verify.outputs.base }}"),
    (
        "        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
        "        with:",
        "          ref: ${{ github.sha }}",
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
    'stream.write(f"attempt={attempt}\\nbase={base}\\npull={number}\\nworkloads={str(workloads).lower()}\\n"',
    'COMPARE_REQUEST = "benchmarks/9700x/compiler-compare.request"',
    "delta, problems = request_delta(head, request_commit, compared_parents)",
    "workloads, compare = workloads and fresh_workloads, compare and fresh_compare",
    'f"request_head={head}\\ncompare={str(compare).lower()}\\nsampling_requested={str(sampling_requested).lower()}\\npreparation_requested={str(preparation_requested).lower()}\\nmerge_base={extra[\'merge_base\']}\\n"',
    '("comparison merge base", isinstance(base_sha, str) and bool(COMMIT.fullmatch(base_sha)) and base_sha != head)',
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
    "      - 'benchmarks/9700x/*.data'",
    "      - 'benchmarks/9700x/compiler-compare.request'",
    "      - 'benchmarks/9700x/scaling.request'",
)

# The main compiler comparison (#2752): main's definition, started by the
# push-to-main request marker's completion, only while both variables are set.
# Main is trusted code, so the pusher is not the authority; the authorizer
# re-reads the commit, its first parent and main itself.
COMPILER_TERMS = (
    "vars.BENCH_DIRECT_ENABLED == 'true'",
    "vars.BENCH_COMPILER_ENABLED == 'true'",
    "github.event_name == 'workflow_run'",
    "github.event.workflow_run.event == 'push'",
    "github.event.workflow_run.head_branch == 'main'",
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
     "      checks: read", "    timeout-minutes: 5"),
    TRUSTED_TOOLS_CHECKOUT,
    (
        "        id: verify",
        "        shell: bash",
        "        env:",
        "          GH_TOKEN: ${{ github.token }}",
        "          BQ_REPOSITORY: ${{ github.repository }}",
        "          BQ_REQUEST_RUN_ID: ${{ github.event.workflow_run.id }}",
        "          BQ_HEAD_COMMIT: ${{ github.event.workflow_run.head_sha }}",
        "          BQ_RUN_ATTEMPT: ${{ github.run_attempt }}",
        "        run: python3 -B tools/bench_direct/authorize_compiler.py",
    ),
)
COMPILER_AUTHORIZER_MARKERS = (
    "from authorize import COMMIT, DECIMAL, REPOSITORY, fetch, full_name",
    'REQUEST_WORKFLOW = ".github/workflows/9700x-compiler-request.yml"',
    '("request event", run.get("event") == "push")',
    '("request branch", run.get("head_branch") == "main")',
    'on_main = fetch(f"{prefix}/compare/{head}...main", token)',
    'failures.append("commit is still on main")',
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
        "          fetch-depth: 0",
        "          filter: blob:none",
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
    "          [[ \"$BQ_PULL\" =~ ^[0-9]+$ ]]",
    "          [[ \"$(git -C candidate rev-parse HEAD)\" == \"$BQ_HEAD_COMMIT\" ]]",
    "          python3 -B trusted/tools/bench_direct/compiler_compare.py --mode main \\",
    "            --candidate candidate --lab trusted/tools/uarch_lab.py \\",
    "            --work \"$RUNNER_TEMP/compiler-bench/work\" --evidence \"$RUNNER_TEMP/compiler-bench/evidence\" \\",
    "            --summary \"$GITHUB_STEP_SUMMARY\" --repository \"$BQ_REPOSITORY\" --ref refs/heads/main \\",
    "            --pull \"$BQ_PULL\" --pull-head \"$BQ_PULL_HEAD\" --base \"$BQ_BASE_COMMIT\" --base-tree \"$BQ_BASE_TREE\" \\",
    "            --head \"$BQ_HEAD_COMMIT\" --head-tree \"$BQ_HEAD_TREE\" --trusted-revision \"$BQ_TRUSTED_REVISION\" \\",
    "            --request-run-id \"$BQ_REQUEST_RUN_ID\" --run-id \"$BQ_RUN_ID\" --run-attempt \"$BQ_RUN_ATTEMPT\"",
]
PULL_RUN_SCRIPT = [line.replace("--mode main", "--mode pull")
                   .replace("--ref refs/heads/main", "--ref \"refs/pull/$BQ_PULL/head\"")
                   .replace("^[0-9]+$", "^[1-9][0-9]*$")
                   .replace("--pull-head \"$BQ_PULL_HEAD\"", "--pull-head \"$BQ_HEAD_COMMIT\"")
                   .replace(" \"$BQ_PULL_HEAD\" \"$BQ_TRUSTED_REVISION\"", " \"$BQ_TRUSTED_REVISION\"")
                   for line in COMPILER_RUN_SCRIPT]
# A main measurement in progress is never cancelled; pull requests coalesce.
DIRECT_CONCURRENCY = (
    "concurrency:",
    "  group: buster-9700x-direct-bench-${{ github.event.workflow_run.event == 'push' && 'main-compare' || github.event.workflow_run.head_branch }}",
    "  cancel-in-progress: ${{ github.event.workflow_run.event != 'push' }}",
)
PULL_RUN_LINES = (
    "    needs: authorize",
    PULL_RUN_IF,
    "    runs-on:",
    "      group: buster-9700x-service-dispatch",
    "      labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]",
    "      BQ_BASE_COMMIT: ${{ needs.authorize.outputs.merge_base }}",
    "      BQ_HEAD_COMMIT: ${{ github.event.workflow_run.head_sha }}",
)
PULL_CHECKOUTS = (
    COMPILER_CHECKOUTS[0],
    (
        "        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
        "        with:",
        "          ref: ${{ github.event.workflow_run.head_sha }}",
        "          path: candidate",
        "          fetch-depth: 0",
        "          filter: blob:none",
        "          persist-credentials: false",
    ),
    COMPILER_CHECKOUTS[2],
)
PULL_PUBLISH_BLOCKS = (
    ("    needs: [authorize, compare-pull]", PULL_PUBLISH_IF, "    runs-on: ubuntu-24.04",
     "    permissions:", "      actions: read", "      checks: write", "    timeout-minutes: 10"),
    TRUSTED_TOOLS_CHECKOUT,
    ("          GH_TOKEN: ${{ github.token }}", "          BQ_MODE: pull"),
    ("          BQ_AUTHORIZE_RESULT: ${{ needs.authorize.result }}",
     "          BQ_AUTHORIZED_ATTEMPT: ${{ needs.authorize.outputs.attempt }}",
     "          BQ_COMPARE_RESULT: ${{ needs.compare-pull.result }}"),
    ("        run: python3 -B tools/bench_direct/compiler_publish.py",),
)
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
    "  push:",
    "    branches: [main]",
)

# Check and report visibility (#2803, #2804). The start jobs share the host
# job's gate, so they run only after this attempt's authorization; they and
# the request bridge hold checks: write and never touch the 9700X.
START_PULL_BLOCKS = (
    ("    needs: authorize", PULL_RUN_IF, "    runs-on: ubuntu-24.04", "    permissions:", "      actions: read",
     "      checks: write", "      pull-requests: read", "    timeout-minutes: 5"),
    TRUSTED_TOOLS_CHECKOUT,
    ("          GH_TOKEN: ${{ github.token }}", "          BQ_MODE: pull"),
    ("        run: python3 -B tools/bench_direct/compiler_github.py start",),
)
START_COMPILER_BLOCKS = (
    ("    needs: authorize-compiler", COMPILER_RUN_IF, "    runs-on: ubuntu-24.04", "    permissions:",
     "      actions: read", "      checks: write", "      contents: read", "    timeout-minutes: 5"),
    TRUSTED_TOOLS_CHECKOUT,
    ("          GH_TOKEN: ${{ github.token }}", "          BQ_MODE: main"),
    ("        run: python3 -B tools/bench_direct/compiler_github.py start",),
)
# The commit report: contents: write (the commit-comment API's permission) in
# its own job that downloads nothing and takes the publisher's entry as data.
COMMENT_IF = "    if: ${{ " + " && ".join((
    "always()", "needs.publish-compiler.result == 'success'", "needs.publish-compiler.outputs.comment != ''",
    *COMPILER_TERMS)) + " }}"
COMMENT_WRITER = (
    "        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683",
    "        with:",
    "          ref: ${{ github.sha }}",
    "          sparse-checkout: tools/bench_direct",
    "          persist-credentials: false",
    "      - name: Upsert the report comment on the main commit",
    "        shell: bash",
    "        env:",
    "          GH_TOKEN: ${{ github.token }}",
    "          BQ_REPOSITORY: ${{ github.repository }}",
)
COMMENT_RUN = (
    "          BQ_PUBLISHER_RUN_ID: ${{ github.run_id }}",
    "          BQ_PUBLISHER_ATTEMPT: ${{ github.run_attempt }}",
    "          BQ_TRUSTED_REVISION: ${{ github.sha }}",
    "        run: python3 -B tools/bench_direct/compiler_comment.py",
)
COMMENT_BLOCKS = (
    ("    needs: publish-compiler", COMMENT_IF, "    runs-on: ubuntu-24.04", "    permissions:",
     "      contents: write", "    timeout-minutes: 5", "    concurrency:",
     "      group: buster-9700x-compiler-comment-${{ needs.publish-compiler.outputs.head }}",
     "      cancel-in-progress: false"),
    COMMENT_WRITER,
    ("          BQ_COMMENT: ${{ needs.publish-compiler.outputs.comment }}", *COMMENT_RUN),
)
ANNOUNCE_BLOCKS = (
    ("    if: ${{ vars.BENCH_DIRECT_ENABLED == 'true' && vars.BENCH_COMPILER_ENABLED == 'true' }}",
     "    runs-on: ubuntu-24.04", "    permissions:", "      checks: write", "    timeout-minutes: 3",
     "    concurrency:", "      group: buster-9700x-check-writer", "      cancel-in-progress: false",
     "      queue: max", "    continue-on-error: true"),
    TRUSTED_TOOLS_CHECKOUT,
    ("          GH_TOKEN: ${{ github.token }}", "          BQ_REPOSITORY: ${{ github.repository }}",
     "          BQ_HEAD_COMMIT: ${{ github.sha }}", "          BQ_REQUEST_RUN_ID: ${{ github.run_id }}",
     "          BQ_REQUEST_ATTEMPT: ${{ github.run_attempt }}",
     "        run: python3 -B tools/bench_direct/compiler_github.py announce"),
)
REPORT_TRIGGER = (
    "on:",
    "  workflow_dispatch:",
    "    inputs:",
    "      run_id:",
    "        description: 9700X direct workload benchmark run ID",
    "        required: true",
    "        type: string",
    "      run_attempt:",
    "        description: Its measurement attempt",
    "        required: true",
    "        type: string",
)
REPORT_OWNER = "github.ref == 'refs/heads/main' && github.actor == 'davidgmbb' && github.triggering_actor == 'davidgmbb'"
REPORT_BLOCKS = {
    "validate": (
        ("    if: ${{ " + REPORT_OWNER + " }}", "    runs-on: ubuntu-24.04", "    permissions:", "      actions: read",
         "      checks: read", "      contents: read", "      pull-requests: read", "    timeout-minutes: 10"),
        TRUSTED_TOOLS_CHECKOUT,
        ("          BQ_RECOVER_RUN_ID: ${{ inputs.run_id }}", "          BQ_RECOVER_ATTEMPT: ${{ inputs.run_attempt }}",
         "          BQ_REGRESSION_POLICY: ${{ vars.BENCH_COMPILER_REGRESSION_POLICY }}",
         "        run: python3 -B tools/bench_direct/compiler_publish.py"),
    ),
    "comment": (
        ("    needs: validate", "    if: ${{ " + REPORT_OWNER + " && needs.validate.outputs.comment != '' }}",
         "    runs-on: ubuntu-24.04", "    permissions:", "      contents: write", "    timeout-minutes: 5",
         "    concurrency:", "      group: buster-9700x-compiler-comment-${{ needs.validate.outputs.head }}",
         "      cancel-in-progress: false"),
        COMMENT_WRITER,
        ("          BQ_COMMENT: ${{ needs.validate.outputs.comment }}", *COMMENT_RUN),
    ),
}
HOSTED_FORBIDDEN = ("buster-zen5", "ryzen-9700x", "self-hosted", "path: candidate", "pull-requests: write",
                    "actions: write", "statuses: write", "download-artifact", "upload-artifact", "secrets.")

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
        "## Scheduling policy",
        "relative to **every parent**",
        "## Administrator steps",
        ".github/workflows/9700x-direct-bench.yml@refs/heads/main",
        "must use **Re-run all jobs**",
        "## Main compiler comparison",
        "BENCH_COMPILER_ENABLED",
        "The performance verdict is report-only.",
        "## Check and commit report",
        ".github/workflows/9700x-compiler-report.yml",
    ),
}


LIFECYCLE_EXPECTED = """name: 9700X terminal lifecycle recovery
on:
  workflow_run:
    workflows: [9700X direct workload benchmark, 9700X compiler benchmark request]
    types: [completed]
  workflow_dispatch:
    inputs:
      run_id:
        description: Completed benchmark executor or failed main request run ID
        required: true
        type: string
      run_attempt:
        description: Exact completed run attempt
        required: true
        type: string
permissions: {}
concurrency:
  group: buster-9700x-terminal-${{ github.event.workflow_run.id || inputs.run_id }}-${{ github.event.workflow_run.run_attempt || inputs.run_attempt }}
  cancel-in-progress: false
jobs:
  reconcile:
    name: Reconcile the exact completed benchmark attempt
    if: ${{ github.repository == 'buster14a/buster' && ((github.event_name == 'workflow_run' && github.event.workflow_run.head_repository.full_name == github.repository && (github.event.workflow_run.path == '.github/workflows/9700x-direct-bench.yml' || (github.event.workflow_run.path == '.github/workflows/9700x-compiler-request.yml' && github.event.workflow_run.conclusion != 'success'))) || (github.event_name == 'workflow_dispatch' && github.ref == 'refs/heads/main' && github.actor == 'davidgmbb' && github.actor_id == '39247043' && github.triggering_actor == 'davidgmbb')) }}
    runs-on: ubuntu-24.04
    timeout-minutes: 5
    concurrency:
      group: buster-9700x-check-writer
      cancel-in-progress: false
      queue: max
    permissions:
      contents: read
      actions: read
      checks: write
    steps:
      - name: Machine specifications
        uses: buster14a/buster/.github/actions/machine-specifications@a36422384d0334a53d4be73bc306b97ccdba4768
        with:
          requested-runner: ubuntu-24.04
      - name: Check out the trusted lifecycle controller
        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683
        with:
          ref: ${{ github.sha }}
          persist-credentials: false
      - name: Compile the trusted native controller
        run: clang -std=c11 -Isrc -O2 -Wall -Wextra -Werror -Wno-unused-function -fwrapv -fno-strict-aliasing -funsigned-char tools/bench_direct/lifecycle.c -lm -o "$RUNNER_TEMP/9700x-lifecycle"
      - name: Reconcile terminal checks and record separate Actions costs
        env:
          GH_TOKEN: ${{ github.token }}
          LC_RUN_ID: ${{ github.event.workflow_run.id || inputs.run_id }}
          LC_ATTEMPT: ${{ github.event.workflow_run.run_attempt || inputs.run_attempt }}
        shell: bash
        run: |
          set -o pipefail
          "$RUNNER_TEMP/9700x-lifecycle" recover "$LC_RUN_ID" "$LC_ATTEMPT" | tee "$RUNNER_TEMP/9700x-lifecycle.jsonl"
      - name: Retain bounded lifecycle observations
        if: ${{ always() }}
        uses: actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02
        with:
          name: 9700x-lifecycle-${{ github.event.workflow_run.id || inputs.run_id }}-${{ github.event.workflow_run.run_attempt || inputs.run_attempt }}
          path: ${{ runner.temp }}/9700x-lifecycle.jsonl
          if-no-files-found: warn
          retention-days: 90"""


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
    actions = ROOT / ".github" / "actions"
    texts.update({path: path.read_text(encoding="utf-8")
                  for path in (*actions.rglob("*.yml"), *actions.rglob("*.yaml"))})
    check_runner_routes(errors, texts)
    check_lifecycle(errors, texts.get(LIFECYCLE, ""))
    check_postmerge_diagnostics(errors)
    check_premerge_checks(errors)
    check_direct_workflow(errors)
    check_sampling_path(errors)
    check_preparation_path(errors)
    check_compiler_path(errors)
    return report(errors)


def check_lifecycle(errors: list[str], workflow: str) -> None:
    """Only exact completion callbacks or owner/main hosted attempt replay."""
    active = "\n".join(line for line in workflow.splitlines()
                       if line.strip() and not line.lstrip().startswith("#"))
    if active != LIFECYCLE_EXPECTED:
        errors.append("terminal lifecycle recovery must match the exact reviewed completion/replay hosted workflow")


def trigger_block(workflow: str) -> tuple[str, ...]:
    """The entire event block, so an event after a blank line cannot hide."""
    result: list[str] = []
    active = False
    for line in workflow.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line == "on:":
            active = True
        elif active and not line.startswith(" "):
            break
        if active:
            result.append(line.rstrip())
    return tuple(result)


def check_runner_routes(errors: list[str], texts: dict[Path, str]) -> None:
    """Audit every workflow, reusable workflow and local composite action."""
    for path, text in texts.items():
        active = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))
        if path != DIRECT:
            for marker in ("buster-zen5", "ryzen-9700x", "buster-9700x-service-dispatch", "self-hosted"):
                if marker in active:
                    errors.append(f"unreviewed benchmark runner route {marker}: {path.name}")
            if path != LIFECYCLE and ".github/workflows/9700x-direct-bench.yml" in active:
                errors.append(f"direct benchmark cannot be called or dispatched indirectly: {path.name}")
        for name in ("9700X direct workload request", "9700X compiler benchmark request"):
            if path not in (DIRECT, DIRECT_REQUEST, COMPILER_REQUEST, LIFECYCLE) and name in active:
                errors.append(f"only the direct workflow may follow the request workflow: {path.name}")
        if "pull_request_target" in active:
            errors.append(f"pull_request_target is forbidden repository-wide: {path.name}")



def check_sampling_path(errors: list[str], direct: str | None = None) -> None:
    """Experimental admission precedes physical assignment; no ordinary short route."""
    direct = DIRECT.read_text(encoding="utf-8") if direct is None else direct
    jobs = job_blocks(direct)
    extra = (ATTEMPT_BINDING, REQUEST_BINDING, "github.run_attempt == 1",
             "github.event.workflow_run.run_attempt == 1", "needs.authorize.outputs.sampling_admitted == 'true'")
    condition = "    if: ${{ " + " && ".join((*DIRECT_TERMS, *extra)) + " }}"
    physical_condition = condition[:-3] + " && needs.sampling-queue.result == 'success' }}"
    publish_condition = condition.replace("${{ ", "${{ always() && ")
    for name, expected in (("sampling-queue", condition), ("sampling", physical_condition),
                           ("sampling-publish", publish_condition)):
        job = jobs.get(name, [])
        if not job or [line for line in job if line.startswith("    if:")] != [expected]:
            errors.append(f"{name} lacks exact owner, every-attempt and native frozen admission")
        if any("workflow_dispatch" in line or "secrets." in line or "environment:" in line for line in job):
            errors.append(f"{name} contains unreviewed authority")
    for name, command in (("sampling-queue", "sampling-queue"), ("sampling-publish", "sampling-publish")):
        job = jobs.get(name, [])
        if not contains_block(job, ("    concurrency:", "      group: buster-9700x-check-writer",
                                    "      cancel-in-progress: false", "      queue: max")):
            errors.append(f"{name} must retain the shared check writer queue")
        if not contains_block(job, ("    permissions:", "      contents: read", "      actions: read",
                                    "      pull-requests: read", "      checks: write")):
            errors.append(f"{name} must keep writes at the hosted check boundary")
        if "    runs-on: ubuntu-24.04" not in job or any("self-hosted" in line for line in job):
            errors.append(f"{name} must be hosted only")
        if f"        run: python3 -B tools/bench_direct/compiler_publish.py {command}" not in job:
            errors.append(f"{name} must use the existing trusted publisher")
        if "          ref: ${{ needs.authorize.outputs.sampling_trusted_revision }}" not in job or \
                "          persist-credentials: false" not in job:
            errors.append(f"{name} must pin its reviewed consumer implementation")
    physical = jobs.get("sampling", [])
    if "    needs: [authorize, sampling-queue]" not in physical or not contains_block(physical, (
            "    runs-on:", "      group: buster-9700x-service-dispatch",
            "      labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]")):
        errors.append("sampling physical assignment must follow the hosted queue/admission")
    if any(marker in line for line in physical for marker in (
            "GH_TOKEN", "github.token", "permissions:", "sudo", "api.github.com", "curl ", "wget ", "ssh ")):
        errors.append("sampling physical job carries a token, extra permissions or host mutation")
    expected_script = [
        "          set -euo pipefail",
        "          trusted/build.sh compiler_profile_qualification --execute \\",
        "            --phase \"$BQ_SAMPLING_PHASE\" --packet \"$BQ_SAMPLING_PACKET\" \\",
        "            --trusted-root \"$PWD/trusted\" --cleanup-root \"$RUNNER_TEMP\" \\",
        "            --evidence \"$RUNNER_TEMP/compiler-sampling-evidence\"",
    ]
    if run_scripts(physical) != [expected_script]:
        errors.append("sampling must execute only the bounded native controller")
    if "    timeout-minutes: ${{ fromJSON(needs.authorize.outputs.sampling_timeout_minutes) }}" not in physical:
        errors.append("sampling timeout must come from the immutable native reservation")
    if "          ref: ${{ needs.authorize.outputs.sampling_trusted_revision }}" not in physical:
        errors.append("sampling measurement implementation is not frozen")
    for key in ("request", "freeze", "parent_freeze", "acquisition_plan", "allowlist", "facts", "history"):
        expected = "      BQ_SAMPLING_" + key.upper() + "_DATA: ${{ needs.authorize.outputs.sampling_" + key + "_data }}"
        if expected not in physical:
            errors.append(f"sampling lacks bounded authenticated {key} data")
    if any("compiler_compare.py" in line or "compiler-compare-v1" in line for line in physical):
        errors.append("sampling cannot silently prepend an ordinary long comparison")


def check_preparation_path(errors: list[str], direct: str | None = None) -> None:
    """Experimental admission precedes physical assignment; no ordinary short route."""
    direct = DIRECT.read_text(encoding="utf-8") if direct is None else direct
    jobs = job_blocks(direct)
    extra = (ATTEMPT_BINDING, REQUEST_BINDING, "github.run_attempt == 1",
             "github.event.workflow_run.run_attempt == 1", "needs.authorize.outputs.preparation_admitted == 'true'")
    condition = "    if: ${{ " + " && ".join((*DIRECT_TERMS, *extra)) + " }}"
    physical_condition = condition[:-3] + " && needs.preparation-queue.result == 'success' }}"
    publish_condition = condition.replace("${{ ", "${{ always() && ")
    for name, expected in (("preparation-queue", condition), ("preparation", physical_condition),
                           ("preparation-publish", publish_condition)):
        job = jobs.get(name, [])
        if not job or [line for line in job if line.startswith("    if:")] != [expected]:
            errors.append(f"{name} lacks exact owner, every-attempt and native frozen admission")
        if any("workflow_dispatch" in line or "secrets." in line or "environment:" in line for line in job):
            errors.append(f"{name} contains unreviewed authority")
    for name, command in (("preparation-queue", "preparation-queue"), ("preparation-publish", "preparation-publish")):
        job = jobs.get(name, [])
        if not contains_block(job, ("    concurrency:", "      group: buster-9700x-check-writer",
                                    "      cancel-in-progress: false", "      queue: max")):
            errors.append(f"{name} must retain the shared check writer queue")
        if not contains_block(job, ("    permissions:", "      contents: read", "      actions: read",
                                    "      pull-requests: read", "      checks: write")):
            errors.append(f"{name} must keep writes at the hosted check boundary")
        if "    runs-on: ubuntu-24.04" not in job or any("self-hosted" in line for line in job):
            errors.append(f"{name} must be hosted only")
        if f"        run: python3 -B tools/bench_direct/compiler_publish.py {command}" not in job:
            errors.append(f"{name} must use the existing trusted publisher")
        if "          ref: ${{ needs.authorize.outputs.preparation_trusted_revision }}" not in job or \
                "          persist-credentials: false" not in job:
            errors.append(f"{name} must pin its reviewed consumer implementation")
    physical = jobs.get("preparation", [])
    if "    needs: [authorize, preparation-queue]" not in physical or not contains_block(physical, (
            "    runs-on:", "      group: buster-9700x-service-dispatch",
            "      labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]")):
        errors.append("preparation physical assignment must follow the hosted queue/admission")
    if any(marker in line for line in physical for marker in (
            "GH_TOKEN", "github.token", "permissions:", "sudo", "api.github.com", "curl ", "wget ", "ssh ")):
        errors.append("preparation physical job carries a token, extra permissions or host mutation")
    expected_script = [
        "          set -euo pipefail",
        "          trusted/build.sh compiler_profile_qualification --execute-preparation \\",
        "            --trusted-root \"$PWD/trusted\" --cleanup-root \"$RUNNER_TEMP\" \\",
        "            --evidence \"$RUNNER_TEMP/compiler-preparation-evidence\"",
    ]
    if run_scripts(physical) != [expected_script]:
        errors.append("preparation must execute only the bounded native controller")
    if "    timeout-minutes: ${{ fromJSON(needs.authorize.outputs.preparation_timeout_minutes) }}" not in physical:
        errors.append("preparation timeout must come from the immutable native reservation")
    if "          ref: ${{ needs.authorize.outputs.preparation_trusted_revision }}" not in physical:
        errors.append("preparation measurement implementation is not frozen")
    for key in ("request", "plan", "allowlist", "facts", "history"):
        expected = "      BQ_PREPARATION_" + key.upper() + "_DATA: ${{ needs.authorize.outputs.preparation_" + key + "_data }}"
        if expected not in physical:
            errors.append(f"preparation lacks bounded authenticated {key} data")
    if any("compiler_compare.py" in line or "compiler-compare-v1" in line for line in physical):
        errors.append("preparation cannot silently prepend an ordinary long comparison")

def check_postmerge_diagnostics(errors: list[str], text: str | None = None) -> None:
    """RAD Debugger tests every triggering main SHA without a pre-merge path."""
    text = RADDEBUGGER.read_text(encoding="utf-8") if text is None else text
    if trigger_block(text) != COMPILER_REQUEST_TRIGGER:
        errors.append("RAD Debugger must trigger only on unfiltered pushes to main")
    for marker in ("ref: ${{ github.sha }}", "group: raddebugger-${{ github.run_id }}",
                   "cancel-in-progress: false", "contents: read", "persist-credentials: false",
                   "ref: f6b4a38134652886239b91f940cd7a67fedf689d", "if: always()",
                   "test_raddebugger --self-test", "test_raddebugger --config Release",
                   "Retain exact-source diagnostics including failures"):
        if marker not in text:
            errors.append(f"RAD Debugger is missing post-merge evidence marker: {marker}")
    if "github.event.pull_request" in text or "contents: write" in text:
        errors.append("RAD Debugger must use the triggering main SHA with read-only contents")


def check_premerge_checks(errors: list[str], rules: dict | None = None, admission: str | None = None) -> None:
    """Post-merge diagnostics must never become merge admission dependencies."""
    rules = json.loads((ROOT / ".github/main-merge-queue.ruleset.json").read_text()) if rules is None else rules
    admission = (ROOT / "tools/merge_queue_admission.py").read_text() if admission is None else admission
    for rule in rules.get("rules", []):
        for check in rule.get("parameters", {}).get("required_status_checks", []):
            context = check.get("context", "")
            if context.startswith("9700X ") or context in ("RAD Debugger compatibility", "linux-x86-64"):
                errors.append(f"post-merge diagnostic is required before merging: {context}")
    for marker in ("raddebugger-compatibility.yml", "9700x-direct-bench.yml",
                   "9700x-direct-request.yml", "9700x-compiler-request.yml"):
        if marker in admission:
            errors.append(f"post-merge diagnostic participates in merge admission: {marker}")


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
    if list(jobs) != ["authorize", "preparation-queue", "preparation", "preparation-publish", "sampling-queue", "sampling", "sampling-publish", "bench", "compare-pull", "start-pull", "publish-pull", "authorize-compiler",
                      "start-compiler", "compare", "publish-compiler", "comment-compiler"]:
        errors.append(f"direct workflow jobs must be authorize, bench, compare-pull, start-pull, publish-pull, "
                      f"authorize-compiler, start-compiler, compare, publish-compiler, comment-compiler: {list(jobs)}")
    authorize, run = jobs.get("authorize", []), jobs.get("bench", [])

    # The trigger block is exact: no other event or workflow may start it.
    if trigger_block("\n".join(lines)) != DIRECT_TRIGGER:
        errors.append("direct workflow trigger must be exactly the reviewed workflow_run block")
    declarations = [line.rstrip() for line in lines if line.lstrip().startswith("permissions:")]
    if declarations != ["permissions: {}"] + ["    permissions:"] * 11:
        errors.append("direct workflow must grant GITHUB_TOKEN permissions only to its hosted authorize, "
                      "start, publish and comment jobs")

    if DIRECT_AUTHORIZE_IF not in authorize:
        errors.append("direct authorize job is missing its exact condition")
    for block in DIRECT_AUTHORIZE_BLOCKS:
        if not contains_block(authorize, block):
            errors.append(f"direct authorize job is missing exact block starting: {block[0].strip()}")
    if len([line for line in authorize if "uses:" in line]) != 2 or \
            len([line for line in authorize if "run:" in line]) != 4:
        errors.append("direct authorize job must contain only reviewed checkouts, the authorizer and native admission bootstrap")
    for marker in ("buster-zen5", "ryzen-9700x", "self-hosted", "workflow_run.head_branch", "path: candidate"):
        if any(marker in line for line in authorize):
            errors.append(f"direct authorize job must not use: {marker}")
    source = authorizer.read_text(encoding="utf-8")
    for marker in DIRECT_AUTHORIZER_MARKERS:
        if marker not in source:
            errors.append(f"direct authorizer is missing check: {marker}")
    if source.count("GITHUB_OUTPUT") != 1 or source.count("stream.write(") != 2:
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
                   "merge_group:", "workflow_call:",
                   "repository_dispatch:", "issue_comment:", "secrets.", "inputs.", "wget ", " ssh ",
                   "https://"):
        if marker in direct:
            errors.append(f"direct workflow contains forbidden path: {marker}")

    request = DIRECT_REQUEST.read_text(encoding="utf-8")
    request_lines = request.splitlines()
    if "name: 9700X direct workload request" not in request_lines:
        errors.append("request workflow name must match the direct workflow's trigger")
    if trigger_block("\n".join(request_lines)) != DIRECT_REQUEST_TRIGGER:
        errors.append("request workflow trigger must be exactly the reviewed pull_request block")
    # Every file the authorizer reads as a request must start a request run (#424):
    # a request the trigger ignores never reaches the 9700X.
    authorizer = (ROOT / "tools" / "bench_direct" / "authorize.py").read_text(encoding="utf-8")
    for request_file in re.findall(r'^[A-Z_]+_REQUEST = "(benchmarks/9700x/[^"]+)"$', authorizer, re.MULTILINE):
        if f"      - '{request_file}'" not in request_lines:
            errors.append(f"request workflow paths do not include the authorized request {request_file}")
    if [line.rstrip() for line in request_lines if line.lstrip().startswith("permissions:")] != ["permissions: {}"]:
        errors.append("request workflow must grant no GITHUB_TOKEN permissions")
    for marker in ("uses:", "self-hosted", "buster-zen5", "ryzen-9700x", "group: buster-9700x-service",
                   "secrets.", "github.token", "${{ github.event.pull_request.head", "environment:"):
        if marker in request:
            errors.append(f"request workflow must not use: {marker}")
    if expression_lines_in_scripts(request):
        errors.append("request workflow must not interpolate an expression inside a run script")


def check_compiler_path(errors: list[str]) -> None:
    """The landed-main comparison: trusted gate, unprivileged host, hosted publication."""
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

    compare_pull, publish_pull = jobs.get("compare-pull", []), jobs.get("publish-pull", [])
    for line in PULL_RUN_LINES:
        if line not in compare_pull:
            errors.append(f"pull compare job is missing exact line: {line.strip()}")
    if [line for line in compare_pull if line.startswith("    if:")] != [PULL_RUN_IF]:
        errors.append("pull compare job is missing its exact condition")
    for marker in ("GH_TOKEN", "github.token", "curl ", "api.github.com", "permissions:", "sudo",
                   "environment:", "buster-bench", "secrets.", "--sudo", "--profile-steps"):
        if any(marker in line for line in compare_pull):
            errors.append(f"pull compare job must not use: {marker}")
    if len([line for line in compare_pull if "uses:" in line]) != len(PULL_CHECKOUTS) or \
            not all(contains_block(compare_pull, block) for block in PULL_CHECKOUTS):
        errors.append("pull compare job must use exactly two credential-free checkouts and the evidence upload")
    if run_scripts(compare_pull) != [PULL_RUN_SCRIPT]:
        errors.append("pull compare job must run only main's harness with validated identities")
    for block in PULL_PUBLISH_BLOCKS:
        if not contains_block(publish_pull, block):
            errors.append(f"pull publish job is missing exact block starting: {block[0].strip()}")
    if len([line for line in publish_pull if "uses:" in line]) != 1 or \
            len([line for line in publish_pull if "run:" in line]) != 1:
        errors.append("pull publish job must be one trusted checkout and one publisher call")
    for marker in ("buster-zen5", "ryzen-9700x", "self-hosted", "path: candidate", "contents: write",
                   "pull-requests: write", "actions: write", "statuses: write"):
        if any(marker in line for line in publish_pull):
            errors.append(f"pull publish job must not use: {marker}")
    check_visibility(errors, jobs)

    request = COMPILER_REQUEST.read_text(encoding="utf-8")
    request_lines = request.splitlines()
    if "name: 9700X compiler benchmark request" not in request_lines:
        errors.append("compiler request workflow name must match the direct workflow's trigger")
    if trigger_block("\n".join(request_lines)) != COMPILER_REQUEST_TRIGGER:
        errors.append("compiler request workflow trigger must be exactly the reviewed push-to-main block")
    if not contains_block(DIRECT.read_text(encoding="utf-8").splitlines(), DIRECT_CONCURRENCY):
        errors.append("direct workflow concurrency must never cancel a main measurement in progress")
    if [line.rstrip() for line in request_lines if line.lstrip().startswith("permissions:")] != \
            ["permissions: {}", "    permissions:"]:
        errors.append("compiler request workflow must grant GITHUB_TOKEN permissions only to its announce job")
    for marker in ("self-hosted", "buster-zen5", "ryzen-9700x", "group: buster-9700x-service",
                   "secrets.", "environment:", "pull_request", "workflow_dispatch"):
        if marker in request:
            errors.append(f"compiler request workflow must not use: {marker}")
    request_jobs = job_blocks(request)
    if list(request_jobs) != ["request", "announce"]:
        errors.append(f"compiler request workflow jobs must be request, announce: {list(request_jobs)}")
    for marker in ("uses:", "github.token", "permissions:", "if:"):
        if any(marker in line for line in request_jobs.get("request", [])):
            errors.append(f"compiler request marker job must not use: {marker}")
    announce = request_jobs.get("announce", [])
    for block in ANNOUNCE_BLOCKS:
        if not contains_block(announce, block):
            errors.append(f"compiler announce job is missing exact block starting: {block[0].strip()}")
    if len([line for line in announce if "uses:" in line]) != 1 or len([line for line in announce if "run:" in line]) != 1:
        errors.append("compiler announce job must be one trusted checkout and one check-writer call")
    if expression_lines_in_scripts(request):
        errors.append("compiler request workflow must not interpolate an expression inside a run script")


def check_visibility(errors: list[str], jobs: dict[str, list[str]]) -> None:
    """Start, comment and recovery jobs (#2803, #2804): hosted, narrowly permitted, trusted code only."""
    for name, blocks in (("start-pull", START_PULL_BLOCKS), ("start-compiler", START_COMPILER_BLOCKS),
                         ("comment-compiler", COMMENT_BLOCKS)):
        job = jobs.get(name, [])
        for block in blocks:
            if not contains_block(job, block):
                errors.append(f"{name} job is missing exact block starting: {block[0].strip()}")
        if len([line for line in job if "uses:" in line]) != 1 or len([line for line in job if "run:" in line]) != 1:
            errors.append(f"{name} job must be one trusted checkout and one trusted script call")
        for marker in (*HOSTED_FORBIDDEN, *(("actions: read", "checks:", "pull-requests:")
                                            if name == "comment-compiler" else ("contents: write",))):
            if any(marker in line for line in job):
                errors.append(f"{name} job must not use: {marker}")
    # All hosted check writers share one short-lived retaining queue. This
    # includes cross-attempt orphan reconciliation, so fresh-read/PATCH cannot
    # race a newer start job. GitHub bounds queue:max at 100 pending jobs.
    lock = (
        "    concurrency:",
        "      group: buster-9700x-check-writer",
        "      cancel-in-progress: false",
        "      queue: max",
    )
    for name in ("start-pull", "start-compiler", "publish-pull", "publish-compiler"):
        if not contains_block(jobs.get(name, []), lock):
            errors.append(f"{name} must serialize all hosted check writers without pending replacement")
    request_jobs = job_blocks(COMPILER_REQUEST.read_text(encoding="utf-8"))
    if not contains_block(request_jobs.get("announce", []), lock):
        errors.append("request announce must share the hosted check writer retaining queue")
    recovery_jobs = job_blocks(LIFECYCLE.read_text(encoding="utf-8"))
    if not contains_block(recovery_jobs.get("reconcile", []), lock):
        errors.append("terminal recovery must share the hosted check writer retaining queue")
    writer = (ROOT / "tools" / "bench_direct" / "compiler_github.py").read_text(encoding="utf-8")
    if any(marker in writer for marker in ("wait_for_host", "START_SECONDS", "POLL_SECONDS")):
        errors.append("compiler check setup must not poll physical-runner scheduling")
    if [line for line in jobs.get("publish-compiler", []) if line.strip().startswith(("comment:", "head:"))] != \
            ["      comment: ${{ steps.publish.outputs.comment }}", "      head: ${{ steps.publish.outputs.head }}"]:
        errors.append("publish-compiler must expose exactly its comment entry and head outputs")
    writer = (ROOT / "tools" / "bench_direct" / "compiler_comment.py").read_text(encoding="utf-8")
    for marker in (".download(", "/artifacts", "/check-runs", "zipfile"):
        if marker in writer:
            errors.append(f"the comment writer must not read evidence or checks: {marker}")
    if not COMPILER_REPORT.is_file():
        errors.append(f"missing report recovery workflow: {COMPILER_REPORT.relative_to(ROOT)}")
        return
    report_text = COMPILER_REPORT.read_text(encoding="utf-8")
    lines = report_text.splitlines()
    if trigger_block(report_text) != REPORT_TRIGGER:
        errors.append("report recovery trigger must be exactly the reviewed workflow_dispatch block")
    if [line.rstrip() for line in lines if line.lstrip().startswith("permissions:")] != \
            ["permissions: {}", "    permissions:", "    permissions:"]:
        errors.append("report recovery must grant permissions only to its validate and comment jobs")
    report_jobs = job_blocks(report_text)
    if list(report_jobs) != list(REPORT_BLOCKS):
        errors.append(f"report recovery jobs must be validate, comment: {list(report_jobs)}")
    for name, blocks in REPORT_BLOCKS.items():
        job = report_jobs.get(name, [])
        for block in blocks:
            if not contains_block(job, block):
                errors.append(f"report recovery {name} job is missing exact block starting: {block[0].strip()}")
        if len([line for line in job if "uses:" in line]) != 1 or len([line for line in job if "run:" in line]) != 1:
            errors.append(f"report recovery {name} job must be one trusted checkout and one trusted script call")
        for marker in (*HOSTED_FORBIDDEN, *(("actions: read", "checks:", "pull-requests:") if name == "comment"
                                            else ("contents: write", "checks: write"))):
            if any(marker in line for line in job):
                errors.append(f"report recovery {name} job must not use: {marker}")
    if expression_lines_in_scripts(report_text):
        errors.append("report recovery must not interpolate an expression inside a run script")


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
