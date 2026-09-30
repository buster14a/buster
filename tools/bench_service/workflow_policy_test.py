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
ADMISSION_GUIDE = SERVICE / "deploy" / "GITHUB_ADMISSION.md"
OPERATOR_PACKET = SERVICE / "deploy" / "ISSUE_880_OPERATOR_PACKET.md"
BROKER = SERVICE / "systemd_broker.c"
RECIPE_TEST = SERVICE / "dispatch_recipe_test.py"

# The reviewed dispatch allowlist (#2071): recipe -> service runtime budget in
# seconds. The broker enforces one fixed RuntimeMaxSec for every recipe, so
# each budget must equal it; a shorter wait would give up on a job the service
# still runs. A recipe that needs a different budget needs a reviewed broker
# change first. The installed service still refuses any recipe its compiled
# registry does not serve.
REVIEWED_RECIPES = (("validate-buster-v1", 3600), ("zen5-calibration-v1", 3600))
FINALIZATION_ALLOWANCE = 600
JOB_MARGIN = 300
JOB_TIMEOUT_MINUTES = 120
BUDGET_STEP = "      - name: Select the reviewed recipe and its result-wait budget"
SUBMIT_COMMAND = (
    'receipt="$(/usr/bin/sudo -n -u buster-bench -- /usr/local/libexec/buster-bench-service '
    "gateway submit-recipe \\",
    '"$BQ_RECIPE" "$BQ_IDEMPOTENCY_KEY" "$BQ_BASE_COMMIT" "$BQ_CANDIDATE_COMMIT" 2>&1)"',
)

# Cheap context checks shared by both jobs. The authorize job then proves the
# same identity, by login and numeric ID, from GitHub's record of the attempt.
MAINTAINER_TERMS = (
    "github.ref == 'refs/heads/main'",
    "vars.BENCH_SERVICE_DISPATCH_ENABLED == 'true'",
    "github.event_name == 'workflow_dispatch'",
    "github.actor == 'davidgmbb'",
    "github.actor_id == '39247043'",
    "github.triggering_actor == 'davidgmbb'",
)
# Re-running failed or single jobs reuses earlier successful job outputs, so
# submission is bound to an authorization produced in this same attempt.
ATTEMPT_BINDING = "needs.authorize.outputs.attempt == format('{0}', github.run_attempt)"
AUTHORIZE_IF = "    if: ${{ " + " && ".join(MAINTAINER_TERMS) + " }}"
SUBMIT_IF = "    if: ${{ " + " && ".join((*MAINTAINER_TERMS, ATTEMPT_BINDING)) + " }}"
AUTHORIZE_LINES = (
    AUTHORIZE_IF,
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
    "              (\"event\", run.get(\"event\") == \"workflow_dispatch\"),",
    "              (\"head branch\", run.get(\"head_branch\") == \"main\"),",
    "              (\"actor\", identity(\"actor\") == maintainer),",
    "              (\"triggering actor\", identity(\"triggering_actor\") == maintainer),",
    "              (\"run attempt\", type(run.get(\"run_attempt\")) is int and",
    "               run.get(\"run_attempt\") == run_attempt),",
    "              sys.exit(\"BENCH_DISPATCH_UNAUTHORIZED \" + \", \".join(failures))",
)
AUTHORIZE_OUTPUT = "          printf 'attempt=%s\\n' \"$BQ_RUN_ATTEMPT\" >> \"$GITHUB_OUTPUT\""
SUBMIT_LINES = (
    "    needs: authorize",
    SUBMIT_IF,
    "    environment: benchmark-9700x",
    "    runs-on:",
    "      group: buster-9700x-service-dispatch",
    "      labels: [self-hosted, Linux, X64, buster-zen5, ryzen-9700x]",
)

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
    ADMISSION_GUIDE: (
        "### Dispatch gate",
        "### Settings transition",
        "must use **Re-run all jobs**",
    ),
    OPERATOR_PACKET: (
        "**Step 1:**",
        "**Step 2:**",
    ),
}

# The superseded contract required davidgmbb's approval with self-review
# prevention, which blocked the maintainer's own dispatches.
SUPERSEDED_CONTRACT = ("self-review", "prevent_self_review", "pending environment")


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

    for path in (ADMISSION_GUIDE, OPERATOR_PACKET, BENCHMARKING, DEPLOYMENT):
        if path.is_file():
            text = path.read_text(encoding="utf-8")
            for phrase in SUPERSEDED_CONTRACT:
                if phrase in text:
                    errors.append(
                        f"{path.relative_to(ROOT)} restates the manual-approval contract: {phrase}")

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
                          {"id": 158946652, "type": "IntegrationInstallation"},
                          {"id": 159756060, "type": "IntegrationInstallation"},
                          {"id": 161964061, "type": "IntegrationInstallation"}]:
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
            "python3 -B tools/bench_service/dispatch_recipe_test.py",
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
    jobs = job_blocks(dispatch)
    if list(jobs) != ["authorize", "submit"]:
        errors.append(f"dispatch workflow jobs must be authorize then submit: {list(jobs)}")
    authorize = jobs.get("authorize", [])
    submit = jobs.get("submit", [])

    permission_declarations = [
        line.rstrip()
        for line in dispatch.splitlines()
        if line.lstrip().startswith("permissions:")
    ]
    if permission_declarations != ["permissions: {}", "    permissions:"]:
        errors.append("dispatch workflow must grant no GITHUB_TOKEN permissions beyond authorize")
    authorize_permissions = ("    permissions:", "      actions: read", "    timeout-minutes: 5")
    if not contains_block(authorize, authorize_permissions):
        errors.append("authorize job must be granted exactly actions: read")

    for line in AUTHORIZE_LINES:
        if line not in authorize:
            errors.append(f"authorize job is missing exact line: {line.strip()}")
    scripts = run_scripts(authorize)
    if len(scripts) != 1 or not scripts[0] or scripts[0][-1] != AUTHORIZE_OUTPUT:
        errors.append("authorize job must emit its attempt only after every verification passes")
    elif sum("GITHUB_OUTPUT" in line for line in scripts[0]) != 1:
        errors.append("authorize job must write exactly one output")
    for marker in ("environment:", "buster-zen5", "ryzen-9700x", "self-hosted", "inputs."):
        if any(marker in line for line in authorize):
            errors.append(f"authorize job must not use: {marker}")

    for line in SUBMIT_LINES:
        if line not in submit:
            errors.append(f"submit job is missing exact line: {line.strip()}")
    submit_if = [line for line in submit if line.startswith("    if:")]
    for term in (*MAINTAINER_TERMS, ATTEMPT_BINDING):
        if len(submit_if) != 1 or term not in submit_if[0]:
            errors.append(f"submit job condition is missing: {term}")
    for marker in ("GH_TOKEN", "github.token", "curl ", "api.github.com", "permissions:"):
        if any(marker in line for line in submit):
            errors.append(f"submit job must not use: {marker}")

    for number, line in expression_lines_in_scripts(dispatch):
        errors.append(f"line {number} interpolates an expression inside a run script")

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
        "wget ",
        "uses:",
        "secrets.",
        "./build",
        "cmake ",
        "ninja ",
    )
    for marker in forbidden:
        if marker in dispatch:
            errors.append(f"dispatch workflow contains forbidden execution path: {marker}")
    curl_lines = [line for line in dispatch.splitlines() if "curl " in line]
    if len(curl_lines) != 1 or curl_lines[0] not in authorize or \
            sum("https://" in line for line in dispatch.splitlines()) != 1:
        errors.append("only authorize may fetch, once, from its fixed run-attempt API URL")

    input_line = re.compile(r"^\s+BQ_(IDEMPOTENCY_KEY|BASE_COMMIT|CANDIDATE_COMMIT|RECIPE): \$\{\{ inputs\.")
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

    check_recipe_selection(dispatch, submit, errors)
    return report(errors)


def check_recipe_selection(dispatch: str, submit: list[str], errors: list[str]) -> None:
    """Recipe choice comes from the reviewed allowlist; the wait from its budget."""
    names = [name for name, _ in REVIEWED_RECIPES]
    if not RECIPE_TEST.is_file():
        errors.append("missing executable dispatch recipe test")
    if names[0] != "validate-buster-v1" or len(set(names)) != len(names):
        errors.append("the reviewed allowlist must start with validate-buster-v1 and be unique")

    recipe_input = input_block(dispatch, "recipe")
    expected_input = [
        "        description: Installed service recipe from the reviewed allowlist",
        "        required: true",
        "        type: choice",
        "        default: validate-buster-v1",
        "        options:",
        *[f"          - {name}" for name in names],
    ]
    if recipe_input != expected_input:
        errors.append(f"recipe input must be a required choice of exactly the allowlist: {recipe_input}")

    broker = BROKER.read_text(encoding="utf-8") if BROKER.is_file() else ""
    runtime = re.findall(r'"--property=RuntimeMaxSec=([1-9][0-9]*)us"', broker)
    if len(runtime) != 1 or int(runtime[0]) % 1000000:
        errors.append("the broker must define one whole-second RuntimeMaxSec")
    else:
        for name, budget in REVIEWED_RECIPES:
            if budget != int(runtime[0]) // 1000000:
                errors.append(f"{name} budget must equal the broker RuntimeMaxSec")

    timeout = f"    timeout-minutes: {JOB_TIMEOUT_MINUTES}"
    if [line for line in submit if line.startswith("    timeout-minutes:")] != [timeout]:
        errors.append(f"submit job must keep {timeout.strip()}")
    if f"      BQ_JOB_TIMEOUT_SECONDS: {JOB_TIMEOUT_MINUTES * 60}" not in submit:
        errors.append("BQ_JOB_TIMEOUT_SECONDS must equal the submit job timeout")
    if "      BQ_RECIPE: ${{ inputs.recipe }}" not in submit:
        errors.append("submit job must receive the recipe only through its validated environment")
    for _, budget in REVIEWED_RECIPES:
        if budget + FINALIZATION_ALLOWANCE + JOB_MARGIN >= JOB_TIMEOUT_MINUTES * 60:
            errors.append("a reviewed recipe budget leaves no admission window in the job timeout")

    steps = [line for line in submit if line.startswith("      - name: ")]
    if not steps or steps[0] != BUDGET_STEP:
        errors.append("the recipe/budget step must run before any gateway command")
    scripts = run_scripts(submit)
    budget = [line.strip() for line in scripts[0]] if scripts else []
    arms = [line for line in budget if re.fullmatch(r"[a-z0-9-]+\) runtime_budget=[0-9]+ ;;", line)]
    if arms != [f"{name}) runtime_budget={value} ;;" for name, value in REVIEWED_RECIPES]:
        errors.append(f"budget step allowlist differs from the reviewed allowlist: {arms}")
    refusal = ("*)", "echo 'BENCH_DISPATCH_RECIPE_REFUSED recipe is not in the reviewed allowlist' >&2",
               "exit 1", ";;", "esac")
    if not contains_block(budget, refusal):
        errors.append("budget step must refuse every recipe outside the allowlist")
    for marker in (
        f"finalization_allowance={FINALIZATION_ALLOWANCE}",
        f"job_margin={JOB_MARGIN}",
        "wait_seconds=$((runtime_budget + finalization_allowance))",
        "job_deadline=$((started + BQ_JOB_TIMEOUT_SECONDS - job_margin))",
        "admission_deadline=$((job_deadline - wait_seconds))",
        "if ((admission_deadline <= started)); then",
    ):
        if marker not in budget:
            errors.append(f"budget step is missing: {marker}")
    if budget[:2] != ["set -euo pipefail", 'started="$(date +%s)"']:
        errors.append("budget step must read the clock before any other check")

    flat = [line.strip() for script in scripts for line in script]
    for block in (
        ('if (($(date +%s) >= BQ_ADMISSION_DEADLINE)); then', "break"),
        ("deadline=$(($(date +%s) + BQ_WAIT_SECONDS))", "if ((deadline > BQ_JOB_DEADLINE)); then",
         "deadline=$BQ_JOB_DEADLINE", "fi"),
        ("while (($(date +%s) < deadline)); do",),
        SUBMIT_COMMAND,
    ):
        if not contains_block(flat, block):
            errors.append(f"submit job is missing deadline or selection control: {block[0]}")
    # The admission attempt cap stays; the fixed recipe and poll-count wait go.
    if dispatch.count("for ((attempt = 0; attempt < 180; attempt += 1)); do") != 1:
        errors.append("admission must keep its 180-attempt cap")
    for marker in ("service-recipes=validate-buster-v1", "attempt < 360"):
        if marker in dispatch:
            errors.append(f"dispatch workflow keeps a superseded fixed control: {marker}")


def input_block(workflow: str, name: str) -> list[str]:
    """Lines of one workflow_dispatch input, excluding its header and comments."""
    lines = workflow.splitlines()
    header = f"      {name}:"
    block: list[str] = []
    if header in lines:
        for line in lines[lines.index(header) + 1:]:
            if not line.startswith("        "):
                break
            block.append(line)
    return block


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
    print("WORKFLOW_POLICY_PASS fixed gateway is the sole 9700X Actions path")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
