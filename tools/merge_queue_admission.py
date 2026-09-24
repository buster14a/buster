#!/usr/bin/env python3
"""Read-only exact-merge-group admission (#867, #1122).

GitHub owns ordering and rebuilding; this is not another queue or publisher.
The six existing gates remain independently required. Their latest workflow
attempts are resolved by path, event and exact group SHA, never by a same-name
commit status or a historical PR-head result. Native-retirement admission is
executed only from an independently trusted main revision and must fail closed.
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

SCHEMA = "buster-merge-queue-admission-v1"
CONTEXT = "Main integration admission"
RETIREMENT_CONTEXT = "Native retirement merge admission"
RULESET_ID = 22537199
BYPASS_ACTORS = [
    {"actor_id": 5, "actor_type": "RepositoryRole", "bypass_mode": "always"},
    {"actor_id": 39247043, "actor_type": "User", "bypass_mode": "always"},
]
CHECKS = {
    "ci.yml": "CI complete",
    "self-host-audit.yml": "Linux x86-64 bootstrap evidence",
    "tcc-bootstrap.yml": "Canonical TCC bootstrap",
    "gpu-toolchains.yml": "GPU Linux consumers",
    "bench-service-policy.yml": "Benchmark service workflow policy",
    "api-migration-policy.yml": "API migration policy",
}
QUEUE = {
    "check_response_timeout_minutes": 360,
    "grouping_strategy": "ALLGREEN",
    "max_entries_to_build": 20,
    "max_entries_to_merge": 1,
    "merge_method": "MERGE",
    "min_entries_to_merge": 1,
    "min_entries_to_merge_wait_minutes": 0,
}
SHA = re.compile(r"[0-9a-f]{40}\Z")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\Z")
MAX_PAGES = 20
POLICY_PATHS = (
    "tools/merge_queue_admission.py",
    "tools/native_retirement_merge_gate.py",
    "tools/native_retirement_integration.py",
    ".github/workflows/merge-queue-admission.yml",
    ".github/workflows/api-migration-policy.yml",
    ".github/main-merge-queue.ruleset.json",
    "docs/native-retirement-dependencies-v1.json",
)


class AdmissionError(Exception):
    """A candidate or configuration cannot authorize main admission."""


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


def latest_runs(rows: list, candidate: dict) -> dict:
    selected = {}
    for row in rows:
        if not isinstance(row, dict):
            raise AdmissionError("malformed workflow-run row")
        path = row.get("path")
        if path not in {".github/workflows/" + name for name in CHECKS}:
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


def check_results(runs: dict, jobs: dict, candidate: dict) -> tuple[list, list]:
    evidence, pending = [], []
    for filename, context in CHECKS.items():
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
        require(job.get("status") == "completed" and job.get("conclusion") == "success",
                context + ": required job did not succeed")
        evidence.append({"context": context, "workflow": filename, "run_id": run["id"],
                         "run_attempt": run["run_attempt"], "job_id": job["id"]})
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
        require(data["current_user_can_bypass"] == "never",
                "the admission reader must not have bypass authority")
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
    expected = set(CHECKS.values()) | {CONTEXT, RETIREMENT_CONTEXT}
    require(len(actual) == len(expected) and {item.get("context") for item in actual} == expected,
            "all six original checks, retirement admission and exact-group admission must remain required")
    require(all(item.get("integration_id") == 15368 for item in actual),
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
    return {"id": RULESET_ID, "bypass_inventory": visibility}


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
        with urllib.request.urlopen(request, timeout=30) as response:
            result = json.load(response)
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


def live_identity(api: GitHub, candidate: dict) -> bool:
    main = api.get("git/ref/heads/main")["object"]["sha"]
    ref = candidate["head_ref"].removeprefix("refs/")
    head = api.get("git/ref/" + urllib.parse.quote(ref, safe="/"))["object"]["sha"]
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


def collect(api: GitHub, candidate: dict) -> tuple[list, list]:
    rows = api.pages("actions/runs", "workflow_runs", event="merge_group", head_sha=candidate["head"])
    runs = latest_runs(rows, candidate)
    jobs = {}
    for run in runs.values():
        if run.get("status") == "completed":
            key = (run["id"], run["run_attempt"])
            path = f"actions/runs/{key[0]}/attempts/{key[1]}/jobs"
            jobs[key] = api.pages(path, "jobs")
    return check_results(runs, jobs, candidate)


def retirement_admission(arguments, candidate: dict) -> dict:
    native_gate = arguments.repo_root / "tools/native_retirement_merge_gate.py"
    require(native_gate.is_file(), "#925/#927 native-retirement admission must land before queue activation")
    # The trusted gate resolves the PR's writer attestation and proves full-tree
    # equality. A PR-head status alone never authorizes this synthetic SHA.
    result = subprocess.run([
        sys.executable, "-B", str(native_gate), "check", "--repo-root", str(arguments.repo_root),
        "--base", candidate["base"], "--head", candidate["head"], "--current-main", candidate["base"],
        "--event", "merge_group", "--repository", arguments.repository,
    ], capture_output=True, text=True, check=False)
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
            evidence, pending = collect(api, candidate)
        else:
            pending = ["queued predecessor has not landed"]
        if not pending:
            # Re-read completed attempts as well as refs. An older successful
            # attempt must not hide a rerun that started during collection.
            repeated, pending = collect(api, candidate)
            if not pending and repeated == evidence:
                require(retirement_admission(arguments, candidate) == retirement,
                        "trusted publication changed during combined-head CI; rebuild admission")
                require(live_identity(api, candidate),
                        "predecessor no longer equals main before final admission")
                verify_trusted_policy(candidate, arguments.repo_root)
                ruleset_after = live_ruleset(api, arguments.repository)
                break
        require(time.monotonic() < deadline, "timed out waiting for exact-group gates: " + ", ".join(pending))
        time.sleep(min(30, max(0, deadline - time.monotonic())))
    return dict(candidate, status="admitted", checks=evidence, retirement=retirement,
                ruleset_reads=[ruleset_before, ruleset_after])


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
    arguments = parser.parse_args(argv)
    code = 0
    try:
        if arguments.command == "check-ruleset":
            validate_ruleset(json.loads(arguments.file.read_text()))
            print("merge-queue ruleset contract passed")
        elif arguments.command == "audit-workflows":
            audit_workflows(arguments.root)
            print("required workflow event inventory passed")
        elif arguments.command == "wait-base":
            require(0 <= arguments.wait_seconds <= 18000, "wait must be bounded to five hours")
            print(json.dumps(wait_base(arguments), sort_keys=True))
        else:
            require(0 <= arguments.wait_seconds <= 18000, "wait must be bounded to five hours")
            report = run_gate(arguments)
            arguments.output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
            print(json.dumps(report, sort_keys=True))
    except (AdmissionError, OSError, KeyError, TypeError, ValueError) as error:
        print("merge-queue admission failed: " + str(error), file=sys.stderr)
        code = 1
    return code


if __name__ == "__main__":
    raise SystemExit(main())
