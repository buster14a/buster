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
ACTOR_POLICY = ROOT / ".github" / "benchmark-actions-policy.json"
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
        variable_read = installer.find(
            '"repos/$repo/actions/variables/BENCH_SERVICE_DISPATCH_ENABLED"')
        variable_guard = installer.find('variable.get("value") != "false"')
        if "gh variable set BENCH_SERVICE_DISPATCH_ENABLED" in installer:
            errors.append("admission installer must leave the dispatch variable unchanged")
        elif "gh api --method" in installer:
            errors.append("admission preflight must not mutate repository policies")
        elif not 0 <= variable_read < variable_guard < installer.find("verify_github_queue.py"):
            errors.append("admission preflight must verify disabled dispatch before reading main policy")
        elif 'variable.get("name") != "BENCH_SERVICE_DISPATCH_ENABLED"' not in installer:
            errors.append("admission installer must check the dispatch variable identity")
        for marker in (
            "orgs/$owner/actions/runner-groups",
            "selected_workflows",
            "repo-runners.json",
            "repos/$repo/actions/policies",
            "policy_matches",
            "verify_github_actor_policy.py",
            "verify_github_admission.py",
            "obsolete benchmark branch ruleset must be removed",
        ):
            if marker not in installer:
                errors.append(f"admission installer is missing control: {marker}")
        policy_readback = installer.find('"repos/$repo/actions/policies/$policy_id"')
        permission_readback = installer.find('"repos/$repo/collaborators/davidgmbb/permission"')
        environment_readback = installer.find('"repos/$repo/environments/benchmark-9700x"')
        if not (0 <= policy_readback < permission_readback < environment_readback <
                installer.find("verify_github_admission.py")):
            errors.append("requester policy and administrator permission must precede environment readback")
        if "--input \"$policy\"" in installer:
            errors.append("admission installer must never replace the existing Actions policy")
        if ACTOR_POLICY.is_file():
            actor_policy = json.loads(ACTOR_POLICY.read_text(encoding="utf-8"))
            if actor_policy["id"] != 5417 or actor_policy["enforcement"] != "active" or \
                    actor_policy["conditions"] != {"workflow_path": {"include": [
                        ".github/workflows/9700x-service-dispatch.yml"], "exclude": []}} or \
                    actor_policy["rules"][1]["parameters"] != {
                        "allowed_events": ["workflow_dispatch"]}:
                errors.append("benchmark requester policy identity, scope or event differs")
            actors = actor_policy["rules"][0]["parameters"]["allowed_actors"]
            if actors != [{"id": 5, "type": "RepositoryRole"},
                          {"id": 39247043, "type": "User"},
                          {"id": 1144995, "type": "App"},
                          {"id": 1236702, "type": "App"},
                          {"id": 811515, "type": "App"}]:
                errors.append("benchmark requester allowlist differs")
        else:
            errors.append("missing reviewed benchmark Actions requester policy")
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

    if BENCHMARK_RULESET.exists():
        errors.append("redundant benchmark branch ruleset policy must be removed")
    if not MAIN_QUEUE_RULESET.is_file():
        errors.append("missing main queue ruleset")
    else:
        queue = json.loads(MAIN_QUEUE_RULESET.read_text(encoding="utf-8"))
        queue_checks = [
            rule for rule in queue["rules"] if rule["type"] == "required_status_checks"
        ]
        if len(queue_checks) != 1:
            errors.append("main queue must define one status-check rule")
        else:
            queue_params = queue_checks[0]["parameters"]
            if queue_params["strict_required_status_checks_policy"] is not False:
                errors.append("main queue checks must remain non-strict")
            queue_contexts = {check["context"] for check in queue_params["required_status_checks"]}
            if not {"CI complete", "Benchmark service workflow policy"} <= queue_contexts:
                errors.append("main queue must include the benchmark workflow policy check")
        if queue["bypass_actors"] != [
            {"actor_id": 5, "actor_type": "RepositoryRole", "bypass_mode": "always"},
            {"actor_id": 39247043, "actor_type": "User", "bypass_mode": "always"},
        ]:
            errors.append("main queue bypass actors differ from the reviewed contract")

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
        "runs-on:",
        "group: buster-9700x-service-dispatch",
        "labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]",
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
