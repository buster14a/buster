#!/usr/bin/env python3
"""Fail closed if Actions can execute repository code on the benchmark host."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
SERVICE = ROOT / "tools" / "bench_service"
DISPATCH = WORKFLOWS / "9700x-service-dispatch.yml"
POLICY = WORKFLOWS / "bench-service-policy.yml"
EXCLUSIVE_TEST = SERVICE / "exclusive_admission_test.c"
OLD_AUDIT = WORKFLOWS / "zen5-audit.yml"
TRANSIENT_REPAIR = WORKFLOWS / "pr843-doc-fix.yml"
ACTIONLINT = ROOT / ".github" / "actionlint.yaml"
BENCHMARKING = ROOT / "docs" / "agents" / "benchmarking.md"
DEPLOYMENT = SERVICE / "deploy" / "VALIDATE_BUSTER_V1.md"
ADMISSION_INSTALLER = SERVICE / "deploy" / "configure_github_admission.sh"
BENCHMARK_RULESET = ROOT / ".github" / "rulesets" / "benchmark-main.json"
MAIN_QUEUE_RULESET = ROOT / ".github" / "main-merge-queue.ruleset.json"
MAIN_QUEUE_GATE = ROOT / "tools" / "merge_queue_admission.py"

SOURCE_REQUIREMENTS = {
    "exclusive_admission.c": (
        "BqError bq_submit_exclusive",
        "queue->needs_reconciliation",
        "bq_pending(&queue->state)",
        "bq_append(queue, BQ_SUBMIT",
    ),
    "main.c": (
        '#include "exclusive_admission.c"',
        "operation = BQ_OP_SUBMIT_EXCLUSIVE",
    ),
    "protocol.c": (
        "BQ_OP_SUBMIT_EXCLUSIVE",
        "bq_submit_exclusive(queue",
    ),
    "transport.c": (
        "operation == BQ_OP_SUBMIT_EXCLUSIVE",
    ),
}

DOCUMENTATION_REQUIREMENTS = {
    ACTIONLINT: (
        ".github/workflows/9700x-service-dispatch.yml",
        "only by the fixed service dispatch workflow",
    ),
    BENCHMARKING: (
        "The dedicated Ryzen 7 9700X is no longer a general GitHub Actions",
        ".github/workflows/9700x-service-dispatch.yml",
        "native-retirement-performance-v1` remains blocked",
    ),
    DEPLOYMENT: (
        ".github/workflows/9700x-service-dispatch.yml` is the reviewed smoke",
        "The retired audit workflow must not be restored",
        "BENCH_SERVICE_DISPATCH_ENABLED` is exactly `true",
    ),
}


def main() -> int:
    errors: list[str] = []
    if OLD_AUDIT.exists():
        errors.append("zen5-audit.yml still permits checked-out code on the benchmark host")
    if TRANSIENT_REPAIR.exists():
        errors.append("transient PR repair workflow remains in the repository tree")

    for path, markers in DOCUMENTATION_REQUIREMENTS.items():
        if not path.is_file():
            errors.append(f"missing benchmark policy document: {path.relative_to(ROOT)}")
            continue
        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in text:
                errors.append(f"{path.relative_to(ROOT)} is missing policy marker: {marker}")

    if BENCHMARKING.is_file():
        text = BENCHMARKING.read_text(encoding="utf-8")
        if "gh workflow run zen5-audit.yml" in text:
            errors.append("benchmarking guide still instructs operators to run retired zen5-audit.yml")
    if DEPLOYMENT.is_file():
        text = DEPLOYMENT.read_text(encoding="utf-8")
        if "There is no admitted smoke dispatch workflow" in text:
            errors.append("deployment guide still claims the fixed smoke workflow is missing")

    if not ADMISSION_INSTALLER.is_file():
        errors.append("missing configure_github_admission.sh")
    else:
        installer = ADMISSION_INSTALLER.read_text(encoding="utf-8")
        disable_command = 'gh variable set BENCH_SERVICE_DISPATCH_ENABLED --body false --repo "$repo"'
        disable_count = installer.count(disable_command)
        mutation_positions = [
            position
            for marker in ("gh api --method PUT", "gh api --method POST")
            if (position := installer.find(marker)) >= 0
        ]
        if disable_count != 1:
            errors.append("admission installer must disable dispatch exactly once")
        elif not mutation_positions:
            errors.append("admission installer is missing repository policy mutations")
        elif installer.find(disable_command) > min(mutation_positions):
            errors.append("admission installer must disable dispatch before its first policy mutation")
        elif not 0 <= installer.find("verify_github_queue.py") < min(mutation_positions):
            errors.append("admission installer must verify the main queue before policy mutation")
        if MAIN_QUEUE_GATE.is_file():
            gate_id = re.search(
                r"(?m)^RULESET_ID = ([1-9][0-9]*)$",
                MAIN_QUEUE_GATE.read_text(encoding="utf-8"),
            )
            installer_id = re.search(r"(?m)^main_ruleset_id=([1-9][0-9]*)$", installer)
            if not gate_id or not installer_id or gate_id.group(1) != installer_id.group(1):
                errors.append("admission installer and merge-queue gate must bind the same ruleset ID")
        else:
            errors.append("missing main merge-queue admission gate")

    if not BENCHMARK_RULESET.is_file() or not MAIN_QUEUE_RULESET.is_file():
        errors.append("missing benchmark or main queue ruleset")
    else:
        benchmark = json.loads(BENCHMARK_RULESET.read_text(encoding="utf-8"))
        queue = json.loads(MAIN_QUEUE_RULESET.read_text(encoding="utf-8"))
        benchmark_checks = [
            rule for rule in benchmark["rules"] if rule["type"] == "required_status_checks"
        ]
        queue_checks = [
            rule for rule in queue["rules"] if rule["type"] == "required_status_checks"
        ]
        if len(benchmark_checks) != 1 or len(queue_checks) != 1:
            errors.append("benchmark and main queue must each define one status-check rule")
        else:
            benchmark_params = benchmark_checks[0]["parameters"]
            queue_params = queue_checks[0]["parameters"]
            if benchmark_params["strict_required_status_checks_policy"] is not False:
                errors.append("benchmark checks must preserve non-strict merge-queue admission")
            if queue_params["strict_required_status_checks_policy"] is not False:
                errors.append("main queue checks must remain non-strict")
            benchmark_contexts = {
                check["context"] for check in benchmark_params["required_status_checks"]
            }
            queue_contexts = {check["context"] for check in queue_params["required_status_checks"]}
            if not benchmark_contexts <= queue_contexts:
                errors.append("benchmark checks must be covered by the main queue")
        if benchmark["bypass_actors"] or queue["bypass_actors"]:
            errors.append("benchmark and main queue rulesets must not allow bypass")
        benchmark_reviews = [
            rule for rule in benchmark["rules"] if rule["type"] == "pull_request"
        ]
        if len(benchmark_reviews) != 1:
            errors.append("benchmark ruleset must require pull-request review")
        else:
            review_params = benchmark_reviews[0]["parameters"]
            if (
                review_params["required_approving_review_count"] < 1
                or not review_params["dismiss_stale_reviews_on_push"]
                or not review_params["require_last_push_approval"]
            ):
                errors.append("benchmark ruleset must require fresh independent review")

    if not POLICY.is_file():
        errors.append("missing bench-service-policy.yml")
    else:
        policy = POLICY.read_text(encoding="utf-8")
        if re.search(r"(?m)^\s+(?:paths|paths-ignore):\s*$", policy):
            errors.append("required workflow-policy check must run for every pull request")
        for marker in (
            "pull_request:",
            "merge_group:",
            "types: [checks_requested]",
            "tools/bench_service/exclusive_admission_test.c",
            '"$RUNNER_TEMP/exclusive-admission-test"',
        ):
            if marker not in policy:
                errors.append(f"workflow-policy check is missing marker: {marker}")

    if not EXCLUSIVE_TEST.is_file():
        errors.append("missing exclusive_admission_test.c")
    else:
        test = EXCLUSIVE_TEST.read_text(encoding="utf-8")
        for marker in (
            "bq_submit_exclusive",
            "BQ_UNSUPPORTED",
            "BQ_CONFLICT",
            "BQ_BUSY",
            "BQ_RECONCILIATION_REQUIRED",
        ):
            if marker not in test:
                errors.append(f"exclusive-admission regression test is missing marker: {marker}")

    for name, markers in SOURCE_REQUIREMENTS.items():
        path = SERVICE / name
        if not path.is_file():
            errors.append(f"missing service source: {name}")
            continue
        source = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in source:
                errors.append(f"{name} is missing exclusive-admission marker: {marker}")

    if not DISPATCH.is_file():
        errors.append("missing 9700x-service-dispatch.yml")
        return report(errors)

    dispatch = DISPATCH.read_text(encoding="utf-8")
    permission_declarations = [
        line.strip()
        for line in dispatch.splitlines()
        if line.lstrip().startswith("permissions:")
    ]
    if permission_declarations != ["permissions: {}"]:
        errors.append("dispatch workflow must grant no GITHUB_TOKEN permissions")

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
        "github.ref == 'refs/heads/main'",
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
