#!/usr/bin/env python3
"""Read-only exact-merge-group admission (#867, #1122).

GitHub owns ordering and rebuilding; this is not another queue or publisher.
The five admission gates remain independently required. Their latest workflow
attempts are resolved by path, event and exact group SHA, never by a same-name
commit status or a historical PR-head result. Native-retirement admission is
executed only from an independently trusted main revision and must fail closed.
Groups that land admitted sources without an exact-tree writer attestation
also require the ephemeral reconstruction job (required_checks, #1893).

Map: identity/check_current bind one group; collect/check_results read the five
gates; run_gate is the legacy runner-held merge_group loop and wait_base the
predecessor wait still used by the native-retirement workflows. reconcile is
the event-driven replacement (#1807): one bounded pass from trusted main, no
sleeping, publishing CONTEXT through CheckWriter only for groups whose own
workflow no longer produces it (group_owner).
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request

from native_retirement_integration import APIReadError, github_read_json

SCHEMA = "buster-merge-queue-admission-v1"
CONTEXT = "Main integration admission"
RETIREMENT_CONTEXT = "Native retirement merge admission"
RETIREMENT_MARKER = "buster-native-retirement-admission-v1"
RETIREMENT_WORKFLOW = ".github/workflows/api-migration-policy.yml"
RULESET_ID = 22537199
GITHUB_ACTIONS_APP_ID = 15368
QUEUE_REF_PREFIX = "refs/heads/gh-readonly-queue/main/"
ADMISSION_WORKFLOW = ".github/workflows/merge-queue-admission.yml"
# Twice the build limit: replaced groups can briefly coexist with new ones.
MAX_QUEUE_REFS = 40
BYPASS_ACTORS = [
    {"actor_id": 5, "actor_type": "RepositoryRole", "bypass_mode": "always"},
    {"actor_id": 39247043, "actor_type": "User", "bypass_mode": "always"},
]
CHECKS = {
    "ci.yml": "CI complete",
    "tcc-bootstrap.yml": "Canonical TCC bootstrap",
    "gpu-toolchains.yml": "GPU Linux consumers",
    "bench-service-policy.yml": "Benchmark service workflow policy",
    "api-migration-policy.yml": "API migration policy",
}
# #3045 rollout: the exact live ruleset selects either the existing admission
# policy or the reviewed main-only audit policy. Every other gate stays required.
POST_MERGE_CHECKS = CHECKS
LEGACY_CHECKS = {**CHECKS, "self-host-audit.yml": "Linux x86-64 bootstrap evidence"}
# Retirement groups whose generated state is not attested for their exact
# tree additionally need the read-only ephemeral reconstruction (#1893). It is
# collected here, not made ruleset-required, because it is path-filtered on PRs.
RECONSTRUCTION_CHECK = {"native-retirement-rebind.yml": "Reconstruct candidate closure ephemerally"}
RECONSTRUCTION_MODES = frozenset(("ordinary-bound-merge-group", "trusted-integration-merge-group"))
QUEUE = {
    "check_response_timeout_minutes": 360,
    "grouping_strategy": "ALLGREEN",
    "max_entries_to_build": 6,
    "max_entries_to_merge": 1,
    "merge_method": "MERGE",
    "min_entries_to_merge": 1,
    "min_entries_to_merge_wait_minutes": 0,
}
SHA = re.compile(r"[0-9a-f]{40}\Z")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\Z")
MAX_PAGES = 20
POLICY_PATHS = (
    "docs/current-native-object-census-v1.json",
    "tools/merge_queue_admission.py",
    "tools/ci_no_code.c",
    ".github/workflows/ci-no-code-plan.yml",
    "tools/native_retirement_merge_gate.py",
    "tools/native_retirement_integration.py",
    "tools/native_retirement_contract.py",
    "tools/native_retirement_dependency_binding.py",
    "tools/native_retirement_external.py",
    "tools/native_retirement_materializer.py",
    "tools/native_retirement_rebind.py",
    "tools/native_retirement_rebind_contract.py",
    "tools/native_retirement_sdks.py",
    ".github/workflows/merge-queue-admission.yml",
    ".github/workflows/merge-queue-reconcile.yml",
    ".github/workflows/ci-merge-group-watch.yml",
    ".github/workflows/ci-recovery.yml",
    ".github/scripts/recover-ci.py",
    ".github/workflows/api-migration-policy.yml",
    ".github/workflows/native-retirement-admission.yml",
    ".github/workflows/native-retirement-rebind.yml",
    ".github/main-merge-queue.ruleset.json",
    "docs/native-retirement-dependencies-v1.json",
    "docs/native-retirement-sdks-v1.json",
    "docs/native-retirement-support-v1.tsv",
)


class AdmissionError(Exception):
    """A candidate or configuration cannot authorize main admission."""


class GroupRetired(Exception):
    """A successful ref read proves this exact group no longer exists."""


class DelegatedReadError(OSError):
    """The trusted native gate exhausted its bounded API read recovery."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AdmissionError(message)


def digest(value: str, label: str) -> str:
    require(isinstance(value, str) and SHA.fullmatch(value) is not None,
            label + " must be an exact lowercase 40-hex SHA")
    return value


def git(repo: Path, *arguments: str) -> str:
    result = subprocess.run(["git", "-C", str(repo), *arguments],
                            capture_output=True, text=True, check=False)
    require(result.returncode == 0, "git failed: " + result.stderr.strip())
    return result.stdout.strip()


def identity(event: dict, repository: str, expected_sha: str, repo: Path,
             trusted_sha: str | None = None) -> dict:
    require(event.get("action") == "checks_requested", "not a checks_requested event")
    require(event.get("repository", {}).get("full_name") == repository,
            "merge-group repository mismatch")
    group = event.get("merge_group", {})
    require(group.get("base_ref") == "refs/heads/main", "queue must target main")
    ref = group.get("head_ref", "")
    require(isinstance(ref, str) and ref.startswith("refs/heads/gh-readonly-queue/main/"),
            "not a main merge-queue ref")
    base = digest(group.get("base_sha"), "base")
    head = digest(group.get("head_sha"), "head")
    require(head == digest(expected_sha, "GITHUB_SHA"), "event/group SHA mismatch")
    policy_sha = digest(trusted_sha if trusted_sha is not None else git(repo, "rev-parse", "HEAD"),
                        "trusted policy revision")
    # GitHub builds later entries on the preceding synthetic merge. Never run
    # policy from that base until it has actually become main.
    chain = git(repo, "rev-list", "--first-parent", "--max-count=21", base).splitlines()
    require(policy_sha in chain,
            "trusted main is not within the bounded first-parent chain of the group base")
    git(repo, "merge-base", "--is-ancestor", base, head)
    parents = git(repo, "rev-list", "--parents", "-n", "1", head).split()
    require(len(parents) == 3 and parents[0] == head and parents[1] == base,
            "group head must have the event base as its first parent and one PR as its second")
    return {"schema": SCHEMA, "repository": repository, "base": base,
            "base_tree": git(repo, "rev-parse", base + "^{tree}"), "head": head,
            "final_tree": git(repo, "rev-parse", head + "^{tree}"), "head_ref": ref,
            "policy_sha": policy_sha, "base_first_parents": chain}


def check_current(candidate: dict, current_main: str, queue_head: str) -> bool:
    main = digest(current_main, "current main")
    require(digest(queue_head, "current queue head") == candidate["head"],
            f"merge group replaced: main={main} base={candidate['base']} "
            f"head={candidate['head']} current_head={queue_head}; do not reuse this result")
    chain = candidate["base_first_parents"]
    require(candidate["policy_sha"] in chain and main in chain and
            chain.index(main) <= chain.index(candidate["policy_sha"]),
            f"main diverged from queued predecessor: main={main} "
            f"base={candidate['base']} head={candidate['head']}; rebuild this group")
    return main == candidate["base"]


def verify_trusted_policy(candidate: dict, repo: Path) -> None:
    # A predecessor may change policy while this workflow waits. Old code is
    # independently trusted, but must not authorize a tree under new policy.
    changed = git(repo, "diff", "--name-only", candidate["policy_sha"],
                  candidate["base"], "--", *POLICY_PATHS)
    require(not changed,
            f"queued predecessor changed admission policy ({changed.replace(chr(10), ', ')}): "
            f"main={candidate['base']} head={candidate['head']}; rebuild this group")


def required_checks(retirement: dict | None, ruleset: dict | None = None) -> dict:
    checks = dict(LEGACY_CHECKS if ruleset is not None and
                  ruleset.get("self_host_admission") is True else CHECKS)
    if retirement is not None and retirement.get("mode") in RECONSTRUCTION_MODES:
        checks.update(RECONSTRUCTION_CHECK)
    return checks


def latest_runs(rows: list, candidate: dict, checks: dict = CHECKS) -> dict:
    selected = {}
    for row in rows:
        if not isinstance(row, dict):
            raise AdmissionError("malformed workflow-run row")
        path = row.get("path")
        if path not in {".github/workflows/" + name for name in checks}:
            continue
        require(row.get("head_sha") == candidate["head"], "workflow SHA mismatch")
        require(row.get("event") == "merge_group", "workflow is not merge-group evidence")
        require(row.get("repository", {}).get("full_name") == candidate["repository"],
                "workflow repository mismatch")
        require(row.get("head_branch") == candidate["head_ref"].removeprefix("refs/heads/"),
                "workflow queue-ref mismatch")
        require(type(row.get("id")) is int and row["id"] > 0, "invalid workflow run ID")
        require(type(row.get("run_attempt")) is int and row["run_attempt"] > 0,
                "invalid workflow attempt")
        old = selected.get(path)
        if old is None or (row["id"], row["run_attempt"]) > (old["id"], old["run_attempt"]):
            selected[path] = row
    return selected


def check_results(runs: dict, jobs: dict, candidate: dict,
                  checks: dict = CHECKS) -> tuple[list, list]:
    evidence, pending = [], []
    for filename, context in checks.items():
        run = runs.get(".github/workflows/" + filename)
        if run is None:
            pending.append(context + ": missing workflow")
            continue
        if run.get("status") != "completed":
            pending.append(context + ": workflow still running")
            continue
        # A non-required optional job may fail (for example GPU Metal). The
        # required job, not the whole workflow conclusion, owns that contract.
        require(run.get("conclusion") in ("success", "failure"),
                context + ": cancelled, skipped, neutral or invalid workflow")
        key = (run["id"], run["run_attempt"])
        matches = [job for job in jobs.get(key, []) if job.get("name") == context]
        require(len(matches) == 1, context + ": missing or ambiguous required job")
        job = matches[0]
        require(type(job.get("id")) is int and job["id"] > 0, context + ": invalid job ID")
        require(job.get("head_sha") == candidate["head"], context + ": job SHA mismatch")
        require(job.get("run_id") == run["id"] and
                job.get("run_attempt") == run["run_attempt"], context + ": job attempt mismatch")
        no_code = candidate.get("no_code") is True
        plans = [row for row in jobs.get(key, [])
                 if row.get("name") == "No-code plan / Classify no-code changes"]
        omitted = (no_code and bool(plans) and context != "CI complete" and
                   job.get("conclusion") == "skipped")
        expected = "skipped" if omitted else "success"
        require(job.get("status") == "completed" and job.get("conclusion") == expected,
                context + ": required job did not succeed" if not no_code else
                context + ": no-code disposition differs from trusted classification")
        if no_code and plans:
            require(len(plans) == 1 and plans[0].get("status") == "completed" and
                    plans[0].get("conclusion") == "success" and
                    plans[0].get("head_sha") == candidate["head"] and
                    plans[0].get("run_id") == run["id"] and
                    plans[0].get("run_attempt") == run["run_attempt"],
                    context + ": missing exact-attempt classification")
            if expected == "skipped":
                for row in jobs.get(key, []):
                    if row.get("name") != "No-code plan / Classify no-code changes":
                        require(row.get("status") == "completed" and row.get("conclusion") == "skipped" and
                                not row.get("runner_id") and not row.get("steps"),
                                context + ": no-code workload was selected or allocated a runner")
        evidence.append({"context": context, "workflow": filename, "run_id": run["id"],
                         "run_attempt": run["run_attempt"], "job_id": job["id"],
                         **({"disposition": "not-applicable-no-code" if omitted else "executed"} if no_code else {})})
    return evidence, pending


def validate_ruleset(data: dict, *, read_only_response: bool = False) -> None:
    require(data.get("target") == "branch" and data.get("enforcement") == "active",
            "queue ruleset must be active and target branches")
    require(data.get("conditions", {}).get("ref_name") ==
            {"include": ["refs/heads/main"], "exclude": []}, "ruleset scope must be exact main")
    # GitHub omits this administrator-only field from GITHUB_TOKEN reads.
    # Absence is unknown, not evidence of either a bypass or an empty list.
    # Deployment audits must still supply a complete administrator response.
    if not read_only_response or "bypass_actors" in data:
        require("bypass_actors" in data,
                "bypass inventory is hidden: check-ruleset needs an administrator response")
        require(data["bypass_actors"] == BYPASS_ACTORS,
                "bypass actors differ from reviewed main ruleset")
    if "current_user_can_bypass" in data:
        if read_only_response:
            require(data["current_user_can_bypass"] == "never",
                    "the admission reader must not have bypass authority")
        else:
            # The complete reviewed inventory above scopes the offline audit.
            # A saved response reports capability, not authenticated reader identity.
            require(data["current_user_can_bypass"] in ("never", "always", "pull_requests_only"),
                    "administrator reader bypass capability is invalid")
    rules = data.get("rules", [])
    require(isinstance(rules, list), "rules must be a list")
    by_type = {rule["type"]: rule for rule in rules}
    require(len(by_type) == len(rules), "duplicate rule types")
    queue = by_type.get("merge_queue", {}).get("parameters")
    if queue != QUEUE:
        if isinstance(queue, dict):
            differences = [f"{key}: expected {QUEUE.get(key, '<absent>')!r}, "
                           f"got {queue.get(key, '<absent>')!r}"
                           for key in sorted(QUEUE.keys() | queue.keys())
                           if key not in QUEUE or key not in queue or QUEUE[key] != queue[key]]
            detail = "; ".join(differences)
        else:
            detail = f"expected {QUEUE!r}, got {queue!r}"
        raise AdmissionError("queue parameters differ from repository contract: " + detail)
    checks = by_type.get("required_status_checks", {}).get("parameters", {})
    require(checks.get("strict_required_status_checks_policy") is False,
            "do not require feature-branch updates")
    require(checks.get("do_not_enforce_on_create") is False, "checks must apply on creation")
    actual = checks.get("required_status_checks", [])
    expected = set(LEGACY_CHECKS.values()) | {CONTEXT, RETIREMENT_CONTEXT}
    post_merge = set(CHECKS.values()) | {CONTEXT, RETIREMENT_CONTEXT}
    names = {item.get("context") for item in actual}
    require(len(actual) == len(names) and names in (expected, post_merge),
            "required checks must match the reviewed admission or main-only self-host policy")
    require(all(item.get("integration_id") == GITHUB_ACTIONS_APP_ID for item in actual),
            "required check source must be GitHub Actions")
    require("deletion" in by_type and "non_fast_forward" in by_type,
            "retain deletion and non-fast-forward protection")
    review = by_type.get("pull_request", {}).get("parameters", {})
    require(review.get("required_approving_review_count") == 0 and
            review.get("require_code_owner_review") is False and
            review.get("require_last_push_approval") is False and
            review.get("dismiss_stale_reviews_on_push") is False and
            review.get("required_review_thread_resolution") is True,
            "retain solo-maintainer review policy and resolved threads")


def live_ruleset(api: GitHub, repository: str) -> dict:
    data = api.get(f"rulesets/{RULESET_ID}")
    require(type(data.get("id")) is int and data["id"] == RULESET_ID and
            data.get("source_type") == "Repository" and data.get("source") == repository,
            "live ruleset identity mismatch")
    validate_ruleset(data, read_only_response=True)
    visibility = "verified-expected" if "bypass_actors" in data else "hidden"
    if visibility == "hidden":
        print("ruleset bypass inventory is hidden from the read-only token; "
              "the two reviewed bypass actors require a separate administrator audit", file=sys.stderr)
    checks = next(rule["parameters"]["required_status_checks"] for rule in data["rules"]
                  if rule["type"] == "required_status_checks")
    return {"id": RULESET_ID, "bypass_inventory": visibility,
            "self_host_admission": any(row["context"] == LEGACY_CHECKS["self-host-audit.yml"]
                                       for row in checks)}


def audit_workflows(root: Path) -> None:
    # Deliberately checks the small existing declarative event contract without
    # adding a YAML dependency. actionlint remains the YAML/expression parser.
    for filename, context in CHECKS.items():
        text = (root / ".github/workflows" / filename).read_text()
        require("\non:\n" in text and "\npermissions:" in text,
                filename + ": missing explicit events/permissions")
        events = text.split("\non:\n", 1)[1].split("\npermissions:", 1)[0]
        require("  pull_request:\n" in events and
                "  merge_group:\n    types: [checks_requested]" in events,
                filename + ": required pull-request/merge-group trigger missing")
        require(re.search(r"^    (?:paths|paths-ignore):", events, re.MULTILINE) is None,
                filename + ": required checks must not be path-filtered")
        require("    name: " + context + "\n" in text, filename + ": required job renamed")


class GitHub:
    """GET-only reader; no ref, check, workflow or ruleset mutation capability."""

    def __init__(self, repository: str, token: str):
        require(REPOSITORY.fullmatch(repository) is not None, "invalid repository")
        require(bool(token), "GH_TOKEN is required")
        self.prefix = "https://api.github.com/repos/" + repository + "/"
        self.token = token

    def get(self, path: str, **query):
        url = self.prefix + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        request = urllib.request.Request(url, headers={
            "Authorization": "Bearer " + self.token,
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        })
        result = github_read_json(request, path, retry_404=path.startswith("git/ref/heads/gh-readonly-queue/main/"))
        return result

    def pages(self, path: str, field: str, **query) -> list:
        result = []
        complete = False
        for page in range(1, MAX_PAGES + 1):
            response = self.get(path, per_page=100, page=page, **query)
            rows = response.get(field)
            require(isinstance(rows, list), "malformed paged API response")
            result.extend(rows)
            if len(rows) < 100:
                require(response.get("total_count") == len(result),
                        "API response is truncated or changed during pagination")
                complete = True
                break
        require(complete, "API pagination limit reached; refusing partial evidence")
        return result


def current_queue_head(api: GitHub, candidate: dict) -> str:
    """Confirm the exact queue ref, independently of evidence collection."""
    ref = candidate["head_ref"].removeprefix("refs/")
    try:
        head = api.get("git/ref/" + urllib.parse.quote(ref, safe="/"))["object"]["sha"]
    except APIReadError as error:
        if error.status == 404:
            # A 404 alone could be a permission/service fault. A successful
            # collection read must independently prove exact-ref retirement.
            rows = api.get("git/matching-refs/" + urllib.parse.quote(ref, safe="/"))
            require(isinstance(rows, list) and len(rows) <= MAX_QUEUE_REFS and
                    all(isinstance(row, dict) and isinstance(row.get("ref"), str) and
                        row["ref"].startswith(QUEUE_REF_PREFIX) and isinstance(row.get("object"), dict)
                        for row in rows),
                    "malformed queue-ref confirmation response")
            for row in rows:
                digest(row["object"].get("sha"), "confirmed queue head")
            matches = [row for row in rows if row.get("ref") == candidate["head_ref"]]
            require(len(matches) <= 1, "ambiguous queue-ref confirmation")
            if not matches or digest(matches[0].get("object", {}).get("sha"), "confirmed queue head") != candidate["head"]:
                raise GroupRetired(f"exact merge group retired: ref={candidate['head_ref']} head={candidate['head']}; "
                                   "no admission issued; GitHub owns rebuilding") from None
        raise
    if digest(head, "current queue head") != candidate["head"]:
        raise GroupRetired(f"exact merge group replaced: ref={candidate['head_ref']} head={candidate['head']} "
                           f"current_head={head}; no admission issued; GitHub owns rebuilding")
    return head


def live_identity(api: GitHub, candidate: dict) -> bool:
    main = api.get("git/ref/heads/main")["object"]["sha"]
    head = current_queue_head(api, candidate)
    ready = check_current(candidate, main, head)
    if not ready:
        print(f"waiting for queued predecessor: main={main} base={candidate['base']} "
              f"head={candidate['head']} predecessor=ahead-of-main", file=sys.stderr)
    return ready


def wait_base(arguments) -> dict:
    event = json.loads(arguments.event.read_text())
    policy_sha = git(arguments.trusted_root, "rev-parse", "HEAD")
    candidate = identity(event, arguments.repository, arguments.sha, arguments.repo_root,
                         trusted_sha=policy_sha)
    api = GitHub(arguments.repository, os.environ.get("GH_TOKEN", ""))
    deadline = time.monotonic() + arguments.wait_seconds
    while True:
        if live_identity(api, candidate):
            verify_trusted_policy(candidate, arguments.repo_root)
            require(live_identity(api, candidate), "group changed after predecessor landed")
            return {"schema": SCHEMA, "status": "base-landed", "base": candidate["base"],
                    "head": candidate["head"], "policy_sha": candidate["policy_sha"]}
        require(time.monotonic() < deadline,
                f"timed out waiting for predecessor: base={candidate['base']} "
                f"head={candidate['head']}; no admission was issued")
        time.sleep(min(30, max(0, deadline - time.monotonic())))


def collect(api: GitHub, candidate: dict, checks: dict = CHECKS) -> tuple[list, list]:
    rows = api.pages("actions/runs", "workflow_runs", event="merge_group", head_sha=candidate["head"])
    runs = latest_runs(rows, candidate, checks)
    jobs = {}
    for run in runs.values():
        if run.get("status") == "completed":
            key = (run["id"], run["run_attempt"])
            path = f"actions/runs/{key[0]}/attempts/{key[1]}/jobs"
            jobs[key] = api.pages(path, "jobs")
    return check_results(runs, jobs, candidate, checks)


def trusted_no_code(arguments, candidate: dict) -> dict | None:
    """Native classification from independently checked-out main; never candidate Python."""
    driver = os.environ.get("BUSTER_CI_NO_CODE_DRIVER", "")
    report = None
    if driver:
        result = subprocess.run([
            driver, "ci_no_code", "--repo", str(arguments.repo_root),
            "--base", candidate["base"], "--head", candidate["head"],
            "--tested", candidate["head"], "--policy", candidate["policy_sha"],
            "--event", "merge_group",
        ], capture_output=True, text=True, check=False, timeout=240,
            env={key: value for key, value in os.environ.items() if key != "GITHUB_OUTPUT"})
        require(result.returncode == 0, "trusted no-code classifier failed; not admitted")
        plan = json.loads(result.stdout)
        require(plan.get("schema") == "buster-ci-no-code-v1" and
                plan.get("base") == candidate["base"] and plan.get("head") == candidate["head"] and
                plan.get("tested") == candidate["head"] and plan.get("policy") == candidate["policy_sha"] and
                type(plan.get("no_code")) is bool, "stale or malformed no-code plan")
        if plan["no_code"]:
            require(plan.get("profile") == "no-code" and plan.get("reason") == "reviewed-prose-only",
                    "invalid no-code reason")
            report = dict(plan, mode="no-code", status="admitted")
    return report


def retirement_admission(arguments, candidate: dict) -> dict:
    no_code = trusted_no_code(arguments, candidate)
    if no_code is not None:
        return no_code
    native_gate = arguments.repo_root / "tools/native_retirement_merge_gate.py"
    require(native_gate.is_file(), "#925/#927 native-retirement admission must land before queue activation")
    # The trusted gate resolves the PR's writer attestation and proves full-tree
    # equality. A PR-head status alone never authorizes this synthetic SHA.
    result = subprocess.run([
        sys.executable, "-B", str(native_gate), "check", "--repo-root", str(arguments.repo_root),
        "--base", candidate["base"], "--head", candidate["head"], "--current-main", candidate["base"],
        "--event", "merge_group", "--repository", arguments.repository,
    ], capture_output=True, text=True, check=False)
    if result.returncode == 75:
        raise DelegatedReadError("trusted retirement admission requires API retry: " + result.stderr.strip())
    require(result.returncode == 0, "trusted retirement admission rejected group: " + result.stderr.strip())
    retirement = json.loads(result.stdout)
    require(retirement.get("status") == "admitted", "retirement gate did not admit this group")
    require(retirement.get("base") == candidate["base"] and retirement.get("head") == candidate["head"],
            "retirement admission identity mismatch")
    return retirement


def run_gate(arguments) -> dict:
    event = json.loads(arguments.event.read_text())
    candidate = identity(event, arguments.repository, arguments.sha, arguments.repo_root)
    api = GitHub(arguments.repository, os.environ.get("GH_TOKEN", ""))
    live_identity(api, candidate)
    ruleset_before = live_ruleset(api, arguments.repository)
    deadline = time.monotonic() + arguments.wait_seconds
    retirement = None
    while True:
        if live_identity(api, candidate):
            verify_trusted_policy(candidate, arguments.repo_root)
            retirement = retirement_admission(arguments, candidate)
            evidence, pending = collect(api, candidate, required_checks(retirement, ruleset_before))
        else:
            pending = ["queued predecessor has not landed"]
        if not pending:
            # Re-read completed attempts as well as refs. An older successful
            # attempt must not hide a rerun that started during collection.
            repeated, pending = collect(api, candidate, required_checks(retirement, ruleset_before))
            if not pending and repeated == evidence:
                require(retirement_admission(arguments, candidate) == retirement,
                        "trusted publication changed during combined-head CI; rebuild admission")
                require(live_identity(api, candidate),
                        "predecessor no longer equals main before final admission")
                verify_trusted_policy(candidate, arguments.repo_root)
                ruleset_after = live_ruleset(api, arguments.repository)
                require(ruleset_before == ruleset_after,
                        "ruleset policy changed during collection; rebuild admission")
                break
        require(time.monotonic() < deadline, "timed out waiting for exact-group gates: " + ", ".join(pending))
        time.sleep(min(30, max(0, deadline - time.monotonic())))
    return dict(candidate, status="admitted", checks=evidence, retirement=retirement,
                ruleset_reads=[ruleset_before, ruleset_after])


def ancestor(repo: Path, older: str, newer: str) -> bool:
    result = subprocess.run(["git", "-C", str(repo), "merge-base", "--is-ancestor", older, newer],
                            capture_output=True, text=True, check=False)
    require(result.returncode in (0, 1), "git merge-base failed: " + result.stderr.strip())
    return result.returncode == 0


def check_marker(head: str) -> str:
    # Binds a reconciler-published check run to one exact group head. The CI
    # fail-fast watcher recognizes the same marker (.github/scripts/recover-ci.py).
    return SCHEMA + ":" + digest(head, "group head")


def group_owner(repo: Path, head: str) -> str:
    # Exactly one producer per group. A group whose own admission workflow still
    # declares merge_group runs the legacy runner-held job; the reconciler then
    # only shadows it. The group's bytes are read as data, never executed.
    git(repo, "cat-file", "-e", head + "^{commit}")
    listed = git(repo, "ls-tree", "--name-only", head, "--", ADMISSION_WORKFLOW)
    text = git(repo, "show", head + ":" + ADMISSION_WORKFLOW) if listed else ""
    return "legacy" if "\n  merge_group:\n" in text else "reconciler"


def queue_refs(repo: Path) -> tuple[str, dict]:
    refs = {}
    for line in git(repo, "ls-remote", "origin", "refs/heads/main", QUEUE_REF_PREFIX + "*").splitlines():
        sha, ref = line.split("\t")
        require(ref == "refs/heads/main" or ref.startswith(QUEUE_REF_PREFIX), "unexpected ref " + ref)
        refs[ref] = digest(sha, ref)
    main = refs.pop("refs/heads/main", None)
    require(main is not None, "main is missing from the remote")
    require(len(refs) <= MAX_QUEUE_REFS, f"{len(refs)} queue refs exceed the bound of {MAX_QUEUE_REFS}")
    git(repo, "fetch", "--no-tags", "--quiet", "origin", main, *sorted(set(refs.values())))
    return main, refs


def published_checks(api: GitHub, head: str, context: str = CONTEXT,
                     marker: str | None = None) -> tuple[dict | None, list]:
    marker = check_marker(head) if marker is None else marker
    rows = api.pages(f"commits/{head}/check-runs", "check_runs", check_name=context,
                     filter="all", app_id=GITHUB_ACTIONS_APP_ID)
    ours, foreign = [], []
    for row in rows:
        require(isinstance(row, dict) and row.get("name") == context and row.get("head_sha") == head and
                row.get("app", {}).get("id") == GITHUB_ACTIONS_APP_ID, "malformed check-run row")
        (ours if row.get("external_id") == marker else foreign).append(row)
    require(len(ours) <= 1, "duplicate reconciler check runs on " + head)
    return (ours[0] if ours else None), [row.get("id") for row in foreign]


def evaluate(api: GitHub, arguments, candidate: dict) -> tuple[str, object]:
    """One bounded pass: ("pending", reasons) or ("admitted", report); rejects raise."""
    state, detail = "pending", ["queued predecessor has not landed"]
    # Requiring the trusted checkout to be the landed base is stronger than
    # verify_trusted_policy alone: a predecessor policy change is never judged
    # by older authority. A stale checkout waits for the base's own main push.
    if not live_identity(api, candidate):
        pass
    elif candidate["policy_sha"] != candidate["base"]:
        detail = ["reconciler checkout predates the landed base; its main push reconciles"]
    else:
        verify_trusted_policy(candidate, arguments.repo_root)
        retirement = retirement_admission(arguments, candidate)
        # Same inventory as run_gate: unattested retirement groups also need
        # the ephemeral reconstruction job (#1893).
        ruleset_before = live_ruleset(api, arguments.repository)
        checks = required_checks(retirement, ruleset_before)
        evidence_candidate = dict(candidate, no_code=retirement.get("mode") == "no-code")
        evidence, detail = collect(api, evidence_candidate, checks)
        if not detail:
            # An older successful attempt must not hide a rerun started during
            # collection; that rerun's own completion event reconciles again.
            repeated, detail = collect(api, evidence_candidate, checks)
            if not detail and repeated != evidence:
                detail = ["required workflow attempts changed during collection"]
            if not detail:
                require(retirement_admission(arguments, candidate) == retirement,
                        "trusted publication changed during combined-head CI; rebuild admission")
                require(live_identity(api, candidate),
                        "predecessor no longer equals main before final admission")
                ruleset_after = live_ruleset(api, arguments.repository)
                require(ruleset_before == ruleset_after,
                        "ruleset policy changed during collection; rebuild admission")
                state = "admitted"
                detail = dict(candidate, status="admitted", checks=evidence, retirement=retirement,
                              ruleset_reads=[ruleset_before, ruleset_after])
    return state, detail


class CheckWriter:
    """The reconciler's only write: its two exact-group admission checks."""

    def __init__(self, repository: str, token: str):
        require(REPOSITORY.fullmatch(repository) is not None, "invalid repository")
        require(bool(token), "GH_TOKEN is required")
        self.prefix = "https://api.github.com/repos/" + repository + "/check-runs"
        self.token = token

    def send(self, existing: dict | None, body: dict) -> dict:
        head = body.get("head_sha") if existing is None else existing.get("head_sha")
        require(body.get("name") in (CONTEXT, RETIREMENT_CONTEXT) and
                digest(head, "check head") == head and
                body.get("external_id") == (check_marker(head) if body["name"] == CONTEXT
                                             else native_marker(head)),
                "check writer accepts only reviewed exact-head admission contexts")
        url = self.prefix if existing is None else self.prefix + "/" + str(existing["id"])
        request = urllib.request.Request(url, data=json.dumps(body).encode(),
                                         method="POST" if existing is None else "PATCH", headers={
            "Authorization": "Bearer " + self.token,
            "Accept": "application/vnd.github+json",
            "Content-Type": "application/json",
            "X-GitHub-Api-Version": "2022-11-28",
        })
        with urllib.request.urlopen(request, timeout=30) as response:
            result = json.load(response)
        return result


def check_body(head: str, state: str, detail, details_url: str) -> dict:
    title = {"pending": "Waiting for exact-group prerequisites",
             "admitted": "Exact group admitted", "rejected": "Exact group rejected"}[state]
    if state == "pending":
        summary = "Not admitted. Pending: " + "; ".join(detail)
    elif state == "admitted":
        summary = (f"Admitted {head} on base {detail['base']} under trusted policy "
                   f"{detail['policy_sha']}.")
    else:
        summary = "Not admitted: " + detail
    body = {"name": CONTEXT, "head_sha": head, "external_id": check_marker(head),
            "details_url": details_url, "status": "in_progress" if state == "pending" else "completed",
            "output": {"title": title, "summary": summary[:60000]}}
    if state != "pending":
        body["conclusion"] = "success" if state == "admitted" else "failure"
    if state == "admitted":
        body["output"]["text"] = "```json\n" + json.dumps(detail, sort_keys=True, indent=2)[:60000] + "\n```"
    return body


def reconcile_group(api: GitHub, writer: CheckWriter, arguments, policy: str, main: str,
                    ref: str, head: str) -> dict:
    repo = arguments.repo_root
    result = {"ref": ref, "head": head, "published": False}
    base = None if ancestor(repo, head, main) else git(repo, "rev-parse", head + "^1")
    if base is None:
        result["state"] = "landed"
    elif base != main and ancestor(repo, main, base):
        # Nothing can change until main advances; that push reconciles again.
        result.update(state="pending", detail=["queued predecessor has not landed"])
    else:
        owner = group_owner(repo, head)
        existing, foreign = published_checks(api, head) if owner == "reconciler" else (None, [])
        result["owner"] = owner
        if existing is not None and existing.get("status") == "completed":
            # Terminal: never retry a correctness failure or re-issue a success.
            result.update(state="published", conclusion=existing.get("conclusion"))
        else:
            event = {"action": "checks_requested", "repository": {"full_name": arguments.repository},
                     "merge_group": {"base_ref": "refs/heads/main", "head_ref": ref,
                                     "base_sha": base, "head_sha": head}}
            try:
                # Another producer of this name is never authority; refuse to race it.
                require(not foreign, f"unexpected second producer of {CONTEXT}: check runs {foreign}")
                candidate = identity(event, arguments.repository, head, repo, trusted_sha=policy)
                state, detail = evaluate(api, arguments, candidate)
            except AdmissionError as error:
                state, detail = "rejected", str(error)
            body = check_body(head, state, detail, arguments.details_url)
            unchanged = (existing is not None and state == "pending" and
                         existing.get("output", {}).get("summary") == body["output"]["summary"])
            if owner == "reconciler" and not unchanged:
                if existing is not None:
                    del body["head_sha"]
                writer.send(existing, body)
            result.update(state=state, detail=detail, published=owner == "reconciler")
    return result


def native_owner(repo: Path, head: str) -> str:
    # The API compatibility job remains in this workflow after activation. Only
    # the native-admission job moves, so the workflow's merge_group event alone
    # cannot determine ownership. Candidate YAML is inspected as inert data.
    listed = git(repo, "ls-tree", "--name-only", head, "--", RETIREMENT_WORKFLOW)
    if not listed:
        return "missing"
    source = git(repo, "show", head + ":" + RETIREMENT_WORKFLOW)
    return "legacy" if "\n  native-retirement-admission:\n" in source else "reconciler"


def native_marker(head: str) -> str:
    return RETIREMENT_MARKER + ":" + digest(head, "group head")


def native_evaluate(api: GitHub, arguments, candidate: dict) -> tuple[str, object]:
    state, detail = "pending", ["queued predecessor has not landed"]
    if not live_identity(api, candidate):
        pass
    elif candidate["policy_sha"] != candidate["base"]:
        detail = ["trusted checkout predates the landed base; its main push reconciles"]
    else:
        verify_trusted_policy(candidate, arguments.repo_root)
        before = live_ruleset(api, arguments.repository)
        retirement = retirement_admission(arguments, candidate)
        require(retirement_admission(arguments, candidate) == retirement,
                "trusted retirement publication changed during validation")
        require(live_identity(api, candidate), "group changed before native admission")
        verify_trusted_policy(candidate, arguments.repo_root)
        after = live_ruleset(api, arguments.repository)
        state = "admitted"
        detail = dict(candidate, status="admitted", retirement=retirement,
                      ruleset_reads=[before, after])
    return state, detail


def reconcile_native_group(api: GitHub, writer: CheckWriter, arguments, policy: str,
                           main: str, ref: str, head: str) -> dict:
    result = {"ref": ref, "head": head, "published": False}
    base = None if ancestor(arguments.repo_root, head, main) else git(
        arguments.repo_root, "rev-parse", head + "^1")
    if base is None:
        result["state"] = "landed"
    elif base != main and ancestor(arguments.repo_root, main, base):
        result.update(state="pending", detail=["queued predecessor has not landed"])
    else:
        owner = native_owner(arguments.repo_root, head)
        result["owner"] = owner
        if owner == "missing":
            result.update(state="rejected", detail="native admission workflow is absent")
        else:
            existing, foreign = published_checks(api, head, RETIREMENT_CONTEXT,
                                                 native_marker(head)) if owner == "reconciler" else (None, [])
            if existing is not None and existing.get("status") == "completed":
                result.update(state="published", conclusion=existing.get("conclusion"))
            else:
                event = {"action": "checks_requested", "repository": {"full_name": arguments.repository},
                         "merge_group": {"base_ref": "refs/heads/main", "head_ref": ref,
                                         "base_sha": base, "head_sha": head}}
                try:
                    require(not foreign, "unexpected second native admission producer: " + str(foreign))
                    candidate = identity(event, arguments.repository, head,
                                         arguments.repo_root, trusted_sha=policy)
                    state, detail = native_evaluate(api, arguments, candidate)
                except AdmissionError as error:
                    state, detail = "rejected", str(error)
                body = check_body(head, state, detail, arguments.details_url)
                body["name"] = RETIREMENT_CONTEXT
                body["external_id"] = native_marker(head)
                unchanged = (existing is not None and state == "pending" and
                             existing.get("output", {}).get("summary") == body["output"]["summary"])
                if owner == "reconciler" and not unchanged:
                    if existing is not None:
                        del body["head_sha"]
                    writer.send(existing, body)
                result.update(state=state, detail=detail, published=owner == "reconciler")
    return result


def reconcile(arguments) -> dict:
    policy = git(arguments.repo_root, "rev-parse", "HEAD")
    main, refs = queue_refs(arguments.repo_root)
    token = os.environ.get("GH_TOKEN", "")
    api = GitHub(arguments.repository, token)
    writer = CheckWriter(arguments.repository, token)
    groups = []
    for ref, head in sorted(refs.items()):
        try:
            group = reconcile_group(api, writer, arguments, policy, main, ref, head)
            group["native"] = reconcile_native_group(api, writer, arguments, policy, main, ref, head)
            groups.append(group)
        except GroupRetired as error:
            groups.append({"ref": ref, "head": head, "state": "retired", "published": False,
                           "detail": str(error)})
        except APIReadError as error:
            state, detail = "retry", str(error)
            if error.status == 404:
                # A checks/evidence read may race retirement before identity
                # evaluation. Confirm it; other 404s remain unresolved faults.
                try:
                    current_queue_head(api, {"head_ref": ref, "head": head})
                except GroupRetired as retired:
                    state, detail = "retired", str(retired)
                except (AdmissionError, OSError, KeyError, TypeError, ValueError) as confirmation:
                    detail += "; ref confirmation failed: " + str(confirmation)
            groups.append({"ref": ref, "head": head, "state": state, "detail": detail})
        except (OSError, KeyError, TypeError, ValueError) as error:
            # Transport or response failure: publish nothing; the next event or
            # the scheduled sweep retries. Correctness failures never land here.
            groups.append({"ref": ref, "head": head, "state": "retry", "detail": str(error)})
    return {"schema": SCHEMA, "mode": "reconcile", "repository": arguments.repository,
            "policy_sha": policy, "main": main, "groups": groups}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    ruleset = commands.add_parser("check-ruleset")
    ruleset.add_argument("file", type=Path)
    audit = commands.add_parser("audit-workflows")
    audit.add_argument("root", type=Path)
    check = commands.add_parser("check-group")
    check.add_argument("--repo-root", type=Path, required=True)
    check.add_argument("--event", type=Path, required=True)
    check.add_argument("--repository", required=True)
    check.add_argument("--sha", required=True)
    check.add_argument("--wait-seconds", type=int, default=18000)
    check.add_argument("--output", type=Path, required=True)
    waiting = commands.add_parser("wait-base")
    waiting.add_argument("--repo-root", type=Path, required=True)
    waiting.add_argument("--trusted-root", type=Path, required=True)
    waiting.add_argument("--event", type=Path, required=True)
    waiting.add_argument("--repository", required=True)
    waiting.add_argument("--sha", required=True)
    waiting.add_argument("--wait-seconds", type=int, default=18000)
    sweep = commands.add_parser("reconcile")
    sweep.add_argument("--repo-root", type=Path, required=True)
    sweep.add_argument("--repository", required=True)
    sweep.add_argument("--details-url", required=True)
    sweep.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args(argv)
    code = 0
    try:
        if arguments.command == "check-ruleset":
            validate_ruleset(json.loads(arguments.file.read_text()))
            print("merge-queue ruleset contract passed")
        elif arguments.command == "audit-workflows":
            audit_workflows(arguments.root)
            print("required workflow event inventory passed")
        elif arguments.command == "reconcile":
            report = reconcile(arguments)
            arguments.output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
            print(json.dumps(report, sort_keys=True))
            if any(group["state"] == "retry" for group in report["groups"]):
                code = 1
        elif arguments.command == "wait-base":
            require(0 <= arguments.wait_seconds <= 18000, "wait must be bounded to five hours")
            print(json.dumps(wait_base(arguments), sort_keys=True))
        else:
            require(0 <= arguments.wait_seconds <= 18000, "wait must be bounded to five hours")
            report = run_gate(arguments)
            arguments.output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
            print(json.dumps(report, sort_keys=True))
    except GroupRetired as error:
        report = {"schema": SCHEMA, "mode": arguments.command, "status": "retry",
                  "reason": "group-retired", "detail": str(error)}
        if hasattr(arguments, "output"):
            arguments.output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
        print(json.dumps(report, sort_keys=True))
        # Legacy required jobs must not turn obsolete validation green. This
        # distinct retry outcome terminates their wait without a policy denial.
        code = 75
    except (APIReadError, DelegatedReadError) as error:
        report = {"schema": SCHEMA, "mode": arguments.command, "status": "retry",
                  "reason": "api-read", "detail": str(error)}
        if hasattr(arguments, "output"):
            arguments.output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
        print(json.dumps(report, sort_keys=True))
        code = 75
    except (AdmissionError, OSError, KeyError, TypeError, ValueError) as error:
        print("merge-queue admission failed: " + str(error), file=sys.stderr)
        code = 1
    return code


if __name__ == "__main__":
    raise SystemExit(main())
