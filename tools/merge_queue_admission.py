#!/usr/bin/env python3
"""Read-only exact-merge-group admission (#867).

GitHub owns ordering and rebuilding; this is not another queue or publisher.
The six existing gates remain independently required. Their latest workflow
attempts are resolved by path, event and exact group SHA, never by a same-name
commit status or a historical PR-head result. Native-retirement admission is
executed only from the immutable trusted base and must fail closed.
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
    "max_entries_to_build": 1,
    "max_entries_to_merge": 1,
    "merge_method": "MERGE",
    "min_entries_to_merge": 1,
    "min_entries_to_merge_wait_minutes": 0,
}
SHA = re.compile(r"[0-9a-f]{40}\Z")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\Z")
MAX_PAGES = 20


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


def identity(event: dict, repository: str, expected_sha: str, repo: Path) -> dict:
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
    require(git(repo, "rev-parse", "HEAD") == base,
            "admission must execute from the immutable trusted base")
    git(repo, "merge-base", "--is-ancestor", base, head)
    return {"schema": SCHEMA, "repository": repository, "base": base,
            "base_tree": git(repo, "rev-parse", base + "^{tree}"), "head": head,
            "final_tree": git(repo, "rev-parse", head + "^{tree}"), "head_ref": ref}


def check_current(candidate: dict, current_main: str, queue_head: str) -> None:
    require(digest(current_main, "current main") == candidate["base"],
            "main advanced: discard this group and rebuild against current main")
    require(digest(queue_head, "current queue head") == candidate["head"],
            "merge group was replaced: never reuse this result for its successor")


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


def validate_ruleset(data: dict) -> None:
    require(data.get("target") == "branch" and data.get("enforcement") == "active",
            "queue ruleset must be active and target branches")
    require(data.get("conditions", {}).get("ref_name") ==
            {"include": ["refs/heads/main"], "exclude": []}, "ruleset scope must be exact main")
    require(data.get("bypass_actors") == [], "standing bypasses are not admitted")
    rules = data.get("rules", [])
    require(isinstance(rules, list), "rules must be a list")
    by_type = {rule["type"]: rule for rule in rules}
    require(len(by_type) == len(rules), "duplicate rule types")
    require(by_type.get("merge_queue", {}).get("parameters") == QUEUE,
            "queue must serialize builds and merges with ALLGREEN and MERGE")
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


def live_identity(api: GitHub, candidate: dict) -> None:
    main = api.get("git/ref/heads/main")["object"]["sha"]
    ref = candidate["head_ref"].removeprefix("refs/")
    head = api.get("git/ref/" + urllib.parse.quote(ref, safe="/"))["object"]["sha"]
    check_current(candidate, main, head)


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
    validate_ruleset(api.get(f"rulesets/{RULESET_ID}"))
    retirement = retirement_admission(arguments, candidate)
    deadline = time.monotonic() + arguments.wait_seconds
    while True:
        live_identity(api, candidate)
        evidence, pending = collect(api, candidate)
        if not pending:
            # Re-read completed attempts as well as refs. An older successful
            # attempt must not hide a rerun that started during collection.
            repeated, pending = collect(api, candidate)
            if not pending and repeated == evidence:
                require(retirement_admission(arguments, candidate) == retirement,
                        "trusted publication changed during combined-head CI; rebuild admission")
                live_identity(api, candidate)
                validate_ruleset(api.get(f"rulesets/{RULESET_ID}"))
                break
        require(time.monotonic() < deadline, "timed out waiting for exact-group gates: " + ", ".join(pending))
        time.sleep(min(30, max(0, deadline - time.monotonic())))
    return dict(candidate, status="admitted", checks=evidence, retirement=retirement)


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
    arguments = parser.parse_args(argv)
    code = 0
    try:
        if arguments.command == "check-ruleset":
            validate_ruleset(json.loads(arguments.file.read_text()))
            print("merge-queue ruleset contract passed")
        elif arguments.command == "audit-workflows":
            audit_workflows(arguments.root)
            print("required workflow event inventory passed")
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
