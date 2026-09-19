#!/usr/bin/env python3
"""Fail closed if Actions can execute repository code on the benchmark host."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
DISPATCH = WORKFLOWS / "9700x-service-dispatch.yml"
OLD_AUDIT = WORKFLOWS / "zen5-audit.yml"


def main() -> int:
    errors: list[str] = []
    if OLD_AUDIT.exists():
        errors.append("zen5-audit.yml still permits checked-out code on the benchmark host")
    if not DISPATCH.is_file():
        errors.append("missing 9700x-service-dispatch.yml")
        return report(errors)

    dispatch = DISPATCH.read_text(encoding="utf-8")
    workflows = sorted((*WORKFLOWS.glob("*.yml"), *WORKFLOWS.glob("*.yaml")))
    benchmark_users = [
        path.name
        for path in workflows
        if "buster-zen5" in path.read_text(encoding="utf-8")
        or "ryzen-9700x" in path.read_text(encoding="utf-8")
    ]
    if benchmark_users != [DISPATCH.name]:
        errors.append(f"benchmark labels are not exclusive to the service workflow: {benchmark_users}")

    required = (
        "workflow_dispatch:",
        "environment: benchmark-9700x",
        "vars.BENCH_SERVICE_DISPATCH_ENABLED == 'true'",
        "runs-on: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]",
        "group: buster-9700x-service-dispatch",
        "/usr/bin/sudo -n -u buster-bench -- /usr/local/libexec/buster-bench-service gateway submit",
        "/usr/bin/sudo -n -u buster-bench -- /usr/local/libexec/buster-bench-service gateway result",
        "^[A-Za-z0-9._-]{1,64}$",
        "^([0-9a-f]{40}|[0-9a-f]{64})$",
    )
    for marker in required:
        if marker not in dispatch:
            errors.append(f"dispatch workflow is missing required policy marker: {marker}")

    forbidden = (
        "actions/checkout@",
        "pull_request:",
        "pull_request_target:",
        "schedule:",
        "repository_dispatch:",
        "push:",
        " git ",
        " ssh ",
        "curl ",
        "wget ",
        "./build",
        "cmake ",
        "ninja ",
    )
    for marker in forbidden:
        if marker in dispatch:
            errors.append(f"dispatch workflow contains forbidden execution path: {marker}")

    input_line = re.compile(r"^\s+BQ_(IDEMPOTENCY_KEY|BASE_COMMIT|CANDIDATE_COMMIT): \$\{\{ inputs\.")
    for number, line in enumerate(dispatch.splitlines(), 1):
        if "${{ inputs." in line and not input_line.match(line):
            errors.append(f"line {number} interpolates an input outside the validated environment")

    gateway_submitters = [
        path.name
        for path in workflows
        if "gateway submit" in path.read_text(encoding="utf-8")
    ]
    if gateway_submitters != [DISPATCH.name]:
        errors.append(f"service submission is not unique: {gateway_submitters}")

    submit_lines = [line.strip() for line in dispatch.splitlines() if " gateway submit" in line]
    if len(submit_lines) != 1 or not submit_lines[0].startswith(
        'receipt="$(/usr/bin/sudo -n -u buster-bench -- '
        "/usr/local/libexec/buster-bench-service gateway submit"
    ):
        errors.append("submission must use exactly one literal installed-gateway command")

    return report(errors)


def report(errors: list[str]) -> int:
    if errors:
        for error in errors:
            print(f"WORKFLOW_POLICY_FAIL {error}", file=sys.stderr)
        return 1
    print("WORKFLOW_POLICY_PASS fixed gateway is the sole 9700X Actions path")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
