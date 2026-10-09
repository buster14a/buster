#!/usr/bin/env python3
"""Authorize one direct 9700X workload run from GitHub's own records (#2704).

Run from trusted `main` by the hosted `authorize` job of
`.github/workflows/9700x-direct-bench.yml`. That workflow starts on
`workflow_run`, so its event payload describes the request workflow's run.
This re-reads that run and the pull request for its head commit through the
API and fails closed unless both name the owner. It emits the current run
attempt and the pull request's base commit only after every check holds.

It also reads the pull request's changed files (plan) and says what was
requested: workloads (the selection contract of workload_selection.py) and a
compiler comparison (an added or modified COMPARE_REQUEST, #2769, or
SCALING_REQUEST, #424, whose scaling leg runs inside the comparison). For a
comparison it resolves the merge base with the base branch and both trees from
GitHub's records, so the host job and the publisher bind the same identities.
"""

from __future__ import annotations

import base64
import json
import os
import re
import sys
import urllib.parse
import urllib.request
import urllib.error
from datetime import datetime
from pathlib import Path

from workload_selection import api_changes, select

MAINTAINER = {"login": "davidgmbb", "id": 39247043}
REQUEST_WORKFLOW = ".github/workflows/9700x-direct-request.yml"
COMMIT = re.compile(r"[0-9a-f]{40}")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
DECIMAL = re.compile(r"[1-9][0-9]*")
API = "https://api.github.com"
COMPARE_REQUEST = "benchmarks/9700x/compiler-compare.request"
# Kept equal to compiler_receipt.SCALING_REQUEST; this file imports nothing local.
SCALING_REQUEST = "benchmarks/9700x/scaling.request"
FILE_PAGES = 30
FILE_PAGE_SIZE = 100
# GitHub's compare endpoint never returns more than 300 files.
COMPARE_FILE_LIMIT = 300


def identity(record: object) -> dict | None:
    """Login and numeric ID of an API user object, or None when malformed."""
    result = None
    if isinstance(record, dict) and type(record.get("id")) is int:
        result = {"login": record.get("login"), "id": record.get("id")}
    return result


def full_name(record: object) -> object:
    return record.get("full_name") if isinstance(record, dict) else None


def plan(files: object) -> tuple[bool, bool, list[str]]:
    """(workloads, compare, refusals) requested by the changed files.

    Workload selection is shared with the executor (workload_selection, #2924);
    any refusal means no host work may start.
    """
    rows = [row for row in (files if isinstance(files, list) else [])
            if isinstance(row, dict) and isinstance(row.get("filename"), str) and isinstance(row.get("status"), str)]
    workloads, problems = select(api_changes(rows))
    compare = any(row["filename"] in (COMPARE_REQUEST, SCALING_REQUEST) and row["status"] != "removed"
                  for row in rows)
    return bool(workloads), compare, problems


def inventory(fetch_page, expected: object) -> tuple[list[dict], list[str]]:
    """The pull request's complete changed-file rows, or why they are incomplete (#2939).

    `fetch_page(n)` returns page n. Complete means every page and row was well
    formed, no file repeated, and the row count equals the pull request's own
    `changed_files`; a short or empty page, an exhausted page budget and a
    count mismatch are all failures, never an empty or partial plan.
    """
    files: list[dict] = []
    failures: list[str] = []
    seen: set[str] = set()
    if type(expected) is not int or expected < 0:
        failures.append("changed-file count of the pull request")
    for page in range(1, FILE_PAGES + 1) if not failures else ():
        rows = fetch_page(page)
        if not isinstance(rows, list):
            failures.append(f"changed-file page {page} is malformed")
            break
        for row in rows:
            name = row.get("filename") if isinstance(row, dict) else None
            status = row.get("status") if isinstance(row, dict) else None
            previous = row.get("previous_filename") if isinstance(row, dict) else None
            if not isinstance(name, str) or not name or not isinstance(status, str) or \
                    (previous is not None and not isinstance(previous, str)) or \
                    (status == "renamed" and not previous):
                failures.append(f"changed-file page {page} has a malformed row")
                break
            if name in seen:
                failures.append(f"changed-file inventory repeats {name}")
                break
            seen.add(name)
            files.append(row)
        if failures or len(files) >= expected or len(rows) < FILE_PAGE_SIZE:
            break
    if not failures and len(files) != expected:
        failures.append(f"changed-file inventory has {len(files)} of {expected} files "
                        f"(page budget {FILE_PAGES} x {FILE_PAGE_SIZE})")
    return files, failures


def request_delta(head: str, commit: object, comparisons: object) -> tuple[list[dict], list[str]]:
    """Fresh changed files at head, relative to every parent (#3087).

    A merged request inherited unchanged from either parent is not an
    affirmative request for this candidate. The API's capped diff cannot
    prove a complete plan at its limit, so that case fails closed.
    """
    failures: list[str] = []
    result: list[dict] = []
    parents = commit.get("parents") if isinstance(commit, dict) else None
    parents = [row.get("sha") for row in parents if isinstance(row, dict)] if isinstance(parents, list) else []
    if not (isinstance(commit, dict) and commit.get("sha") == head and 1 <= len(parents) <= 2
            and len(parents) == len(commit["parents"])
            and all(isinstance(sha, str) and COMMIT.fullmatch(sha) and sha != head for sha in parents)
            and len(set(parents)) == len(parents)):
        failures.append("request head and parent identities")
    if not isinstance(comparisons, list) or len(comparisons) != len(parents):
        failures.append("request head-parent comparisons")
    common: set[str] | None = None
    for parent, compared in zip(parents, comparisons if isinstance(comparisons, list) else []) if not failures else ():
        if not (isinstance(compared, dict) and compared.get("status") == "ahead"
                and isinstance(compared.get("base_commit"), dict) and compared["base_commit"].get("sha") == parent
                and isinstance(compared.get("merge_base_commit"), dict)
                and compared["merge_base_commit"].get("sha") == parent):
            failures.append("request comparison parent identity")
            break
        rows = compared.get("files")
        if not isinstance(rows, list) or len(rows) >= COMPARE_FILE_LIMIT:
            failures.append("request comparison file inventory is malformed or capped")
            break
        files, problems = inventory(
            lambda page: rows[(page - 1) * FILE_PAGE_SIZE:page * FILE_PAGE_SIZE], len(rows))
        failures.extend(problems)
        if failures:
            break
        names = {row["filename"] for row in files}
        if common is None:
            result, common = files, names
        else:
            common &= names
    result = [row for row in result if row["filename"] in common] if not failures and common is not None else []
    requested: list[dict] = []
    for row in result:
        fresh = True
        if row["filename"] in (COMPARE_REQUEST, SCALING_REQUEST):
            additions: set[str] | None = None
            for compared in comparisons:
                changed = next(item for item in compared["files"] if item["filename"] == row["filename"])
                patch = changed.get("patch")
                if not isinstance(patch, str):
                    failures.append("request marker patch is unavailable")
                    fresh = False
                    break
                lines = patch.splitlines()
                added = {line[1:] for line in lines if line.startswith("+") and not line.startswith("+++")}
                removed = {line[1:] for line in lines if line.startswith("-") and not line.startswith("---")}
                # A union of earlier parent requests has no new common line;
                # a reordered line is not a renewed experiment either.
                added -= removed
                additions = added if additions is None else additions & added
            fresh = fresh and bool(additions)
        if fresh:
            requested.append(row)
    result = requested if not failures else []
    return result, failures


def comparison(head: str, compared: object, head_commit: object) -> tuple[list[str], dict]:
    """The merge base and both trees of a requested compiler comparison."""
    failures: list[str] = []
    base = compared.get("merge_base_commit") if isinstance(compared, dict) else None
    base_sha = base.get("sha") if isinstance(base, dict) else None
    base_tree = base.get("commit", {}).get("tree", {}).get("sha") if isinstance(base, dict) else None
    head_tree = head_commit.get("commit", {}).get("tree", {}).get("sha") if isinstance(head_commit, dict) else None
    for name, holds in (
        ("comparison merge base", isinstance(base_sha, str) and bool(COMMIT.fullmatch(base_sha)) and base_sha != head),
        ("comparison base tree", isinstance(base_tree, str) and bool(COMMIT.fullmatch(base_tree))),
        ("comparison head commit", isinstance(head_commit, dict) and head_commit.get("sha") == head),
        ("comparison head tree", isinstance(head_tree, str) and bool(COMMIT.fullmatch(head_tree))),
    ):
        if not holds:
            failures.append(name)
    return failures, ({} if failures else {"merge_base": base_sha, "merge_base_tree": base_tree, "head_tree": head_tree})


def verify(repository: str, run_id: int, head: str, run: object, pulls: object) -> tuple[list[str], str]:
    """Return the failed checks and the pull request's base commit."""
    failures: list[str] = []
    base = ""
    if not isinstance(run, dict):
        run = {}
        failures.append("request run record")
    for name, holds in (
        ("request run id", type(run.get("id")) is int and run.get("id") == run_id),
        ("request workflow", run.get("path") == REQUEST_WORKFLOW),
        ("request event", run.get("event") == "pull_request"),
        ("request conclusion", run.get("status") == "completed" and run.get("conclusion") == "success"),
        ("request head commit", run.get("head_sha") == head),
        ("request repository", full_name(run.get("repository")) == repository),
        ("request head repository", full_name(run.get("head_repository")) == repository),
        ("request actor", identity(run.get("actor")) == MAINTAINER),
        ("request triggering actor", identity(run.get("triggering_actor")) == MAINTAINER),
    ):
        if not holds:
            failures.append(name)
    matching = [pull for pull in (pulls if isinstance(pulls, list) else [])
                if isinstance(pull, dict) and pull.get("state") == "open"
                and isinstance(pull.get("head"), dict) and pull["head"].get("sha") == head]
    if len(matching) != 1:
        failures.append("exactly one open pull request for the head commit")
    else:
        pull = matching[0]
        base_record = pull.get("base") if isinstance(pull.get("base"), dict) else {}
        candidate = base_record.get("sha")
        for name, holds in (
            ("pull request author", identity(pull.get("user")) == MAINTAINER),
            ("pull request head repository", full_name(pull["head"].get("repo")) == repository),
            ("pull request base repository", full_name(base_record.get("repo")) == repository),
            ("pull request base commit", isinstance(candidate, str) and bool(COMMIT.fullmatch(candidate))),
        ):
            if not holds:
                failures.append(name)
        if not failures:
            base = candidate
    return failures, base


def fetch(path: str, token: str) -> object:
    request = urllib.request.Request(API + path, headers={
        "Authorization": "Bearer " + token,
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
    })
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


# Data transport for the native qualification admission command. This adapter
# performs no campaign selection, budget decision, retry or physical work.
SAMPLING_PREFIX = "profile: compiler-main-sampling-"
SAMPLING_FREEZE = "docs/compiler-main-sampling-freeze-v1.tsv"
SAMPLING_ALLOWLIST = "docs/compiler-main-sampling-admission-v1.tsv"
SAMPLING_CHECK = "9700X compiler sampling research"
PREPARATION_PREFIX = "profile: compiler-baseline-closure-qualification-"
PREPARATION_PLAN = "docs/compiler-preparation-plan-v1.tsv"
PREPARATION_ALLOWLIST = "docs/compiler-preparation-admission-v1.tsv"
PREPARATION_CHECK = "9700X compiler preparation research"
PREPARATION_MARKER = re.compile(r"profile: compiler-baseline-closure-qualification-v1 packet: 0 freeze: ([0-9a-f]{40})")
SAMPLING_MARKER = re.compile(
    r"profile: compiler-main-sampling-(acquire|pilot|confirm)-v1 packet: (0|[1-9][0-9]*) freeze: ([0-9a-f]{40})")
SAMPLING_HISTORY_HEADER = (
    "phase", "packet", "request_run_id", "request_run_attempt", "executor_run_id", "executor_run_attempt",
    "state", "physical_wall_us", "campaign", "freeze_revision", "actor_login", "actor_id",
    "triggering_login", "triggering_id", "pull_author_login", "pull_author_id")


def sampling_content(repository: str, path: str, revision: str, token: str) -> str:
    """A bounded UTF-8 GitHub contents record, consumed only as data."""
    row = fetch(f"/repos/{repository}/contents/{path}?ref={urllib.parse.quote(revision)}", token)
    if not isinstance(row, dict) or row.get("type") != "file" or row.get("encoding") != "base64" \
            or type(row.get("size")) is not int or not 0 <= row["size"] <= 128 * 1024:
        raise ValueError("sampling input is missing or exceeds the data bound")
    text = base64.b64decode(row.get("content", ""), validate=False).decode("utf-8")
    if len(text.encode("utf-8")) != row["size"]:
        raise ValueError("sampling content size differs from GitHub's record")
    return text


def sampling_selector(text: str) -> tuple[str, str, str, str] | None:
    """Extract data fields only; the native command independently validates it."""
    lines = [line for line in text.splitlines() if line.startswith(SAMPLING_PREFIX)]
    if not lines:
        return None
    if len(lines) != 1 or not (match := SAMPLING_MARKER.fullmatch(lines[0])):
        raise ValueError("sampling selector is malformed or repeated")
    return lines[0], match[1], match[2], match[3]


def preparation_selector(text: str) -> tuple[str, str, str, str] | None:
    """Read the separately versioned preparation selector as data."""
    lines = [line for line in text.splitlines() if line.startswith(PREPARATION_PREFIX)]
    if not lines:
        return None
    if len(lines) != 1 or not (match := PREPARATION_MARKER.fullmatch(lines[0])):
        raise ValueError("preparation selector is malformed or repeated")
    return lines[0], "qualify", "0", match[1]


def preparation_fresh_selector(text: str, compared_parents: list) -> tuple[str, str, str, str] | None:
    lines = [line for line in text.splitlines() if line.startswith(PREPARATION_PREFIX)]
    fresh = [line for line in lines if compared_parents and all(sampling_added(row, line) == line for row in compared_parents)]
    if not fresh:
        return None
    if any(line.startswith(SAMPLING_PREFIX) for line in text.splitlines()):
        raise ValueError("replace the historical sampling selector before requesting preparation")
    return preparation_selector(text)


def sampling_added(compared: object, selector: str) -> str:
    """The exact selector if GitHub's parent patch adds, rather than moves, it."""
    rows = compared.get("files") if isinstance(compared, dict) else None
    row = next((row for row in rows if isinstance(row, dict) and row.get("filename") == COMPARE_REQUEST), {}) \
        if isinstance(rows, list) else {}
    patch = row.get("patch")
    lines = patch.splitlines() if isinstance(patch, str) else []
    added = {line[1:] for line in lines if line.startswith("+") and not line.startswith("+++")}
    removed = {line[1:] for line in lines if line.startswith("-") and not line.startswith("---")}
    return selector if selector in added - removed else "-"


def preparation_data(repository: str, token: str, run: dict, pull: dict, head: str, attempt: str,
                     marker: str, compared_parents: list, directory: Path) -> bool:
    """Observe five bounded records for the distinct native preparation policy."""
    selected = preparation_fresh_selector(marker, compared_parents)
    if selected is None:
        return False
    directory.mkdir(parents=True, exist_ok=False)
    line, _, _, revision = selected
    policy_revision = os.environ.get("GITHUB_SHA", "")
    if not COMMIT.fullmatch(policy_revision):
        raise ValueError("preparation policy lacks the current trusted workflow revision")
    allowlist_text = sampling_content(repository, PREPARATION_ALLOWLIST, policy_revision, token)
    plan_text = sampling_content(repository, PREPARATION_PLAN, revision, token)
    allowlist = dict(row.split("\t") for row in allowlist_text.splitlines() if "\t" in row)
    history = sampling_attempt_history(repository, token, str(run["id"]), allowlist.get("history_since", "-"),
                                      revision, allowlist.get("freeze_sha256", "-"), "-", "-", preparation=True)
    facts = qualification_facts(repository, run, pull, head, attempt, line, compared_parents)
    for name, text in (("request.txt", line + "\n"), ("plan.tsv", plan_text), ("allowlist.tsv", allowlist_text),
                       ("facts.tsv", "".join(f"{key}\t{value}\n" for key, value in facts.items())),
                       ("history.tsv", "\t".join(SAMPLING_HISTORY_HEADER) + "\n" +
                        "".join("\t".join(row) + "\n" for row in history))):
        (directory / name).write_text(text, encoding="utf-8")
    return True


def sampling_attempt_history(repository: str, token: str, current: str, since: str,
                             freeze_revision: str, campaign: str, parent_revision: str,
                             parent_campaign: str, ancestor_revision: str = "-", ancestor_campaign: str = "-",
                             preparation: bool = False) -> list[list[str]]:
    """Complete bounded GitHub request/executor records, including hostless attempts."""
    if since == "-":  # Disabled configuration supplies no admission history window.
        return []
    rows = []
    total = None
    requests = []
    query = urllib.parse.urlencode({"event": "pull_request", "created": ">=" + since, "per_page": 100})
    for page in range(1, 11):
        listed = fetch(f"/repos/{repository}/actions/workflows/9700x-direct-request.yml/runs?{query}&page={page}", token)
        values = listed.get("workflow_runs") if isinstance(listed, dict) else None
        if not isinstance(values, list) or type(listed.get("total_count")) is not int:
            raise ValueError("sampling request history is unavailable")
        if total is None:
            total = listed["total_count"]
        if listed["total_count"] != total or total > 1000:
            raise ValueError("sampling request history is oversized or changed during reading")
        requests.extend(values)
        if len(requests) >= total:
            break
        if len(values) != 100:
            raise ValueError("sampling request history is incomplete")
    if len(requests) != total or len({row.get("id") for row in requests if isinstance(row, dict)}) != total:
        raise ValueError("sampling request history is incomplete or duplicated")
    for request in sorted(requests, key=lambda row: (row.get("created_at", ""), row.get("id", 0))):
        if str(request.get("id")) == current:
            continue
        sha = request.get("head_sha")
        if not isinstance(sha, str) or not COMMIT.fullmatch(sha):
            raise ValueError("sampling history request has no exact commit")
        # A missing marker on an unrelated request is ordinary history.
        try:
            marker_text = sampling_content(repository, COMPARE_REQUEST, sha, token)
            marker = preparation_selector(marker_text) if preparation else sampling_selector(marker_text)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                continue
            raise
        if marker is None or marker[3] not in (freeze_revision, parent_revision, ancestor_revision):
            continue
        # A later ordinary synchronize can inherit the old selector. Only the
        # immutable every-parent delta establishes a new declared attempt.
        source_commit = fetch(f"/repos/{repository}/commits/{sha}", token)
        parents = source_commit.get("parents") if isinstance(source_commit, dict) else None
        parents = [row.get("sha") for row in parents if isinstance(row, dict)] if isinstance(parents, list) else []
        parent_deltas = [fetch(f"/repos/{repository}/compare/{parent}...{sha}", token)
                         for parent in parents[:2] if isinstance(parent, str) and COMMIT.fullmatch(parent)]
        _, delta_problems = request_delta(sha, source_commit, parent_deltas)
        if delta_problems:
            raise ValueError("sampling historical every-parent delta is incomplete: " + "; ".join(delta_problems))
        marker = preparation_fresh_selector(marker_text, parent_deltas) if preparation else sampling_fresh_selector(marker_text, parent_deltas)
        if marker is None:
            continue
        selector, phase, packet, revision = marker
        history_campaign = campaign if revision == freeze_revision else parent_campaign if revision == parent_revision else ancestor_campaign
        associated = fetch(f"/repos/{repository}/commits/{sha}/pulls?per_page=100", token)
        # The associated-commit endpoint proves historical membership. A PR's
        # live head advances between packets; it is not the old request's head.
        snapshot = request.get("pull_requests", [])
        if not isinstance(snapshot, list) or len(snapshot) > 1:
            raise ValueError("sampling history request has ambiguous pull provenance")
        number = snapshot[0].get("number") if snapshot and isinstance(snapshot[0], dict) else None
        if snapshot and (type(number) is not int or number <= 0):
            raise ValueError("sampling history request has no canonical pull number")
        matches = [pull for pull in associated if isinstance(pull, dict) and
                   (number is None or pull.get("number") == number)] if isinstance(associated, list) else []
        if len(matches) != 1:
            raise ValueError("sampling history request has no unique owning pull")
        pull = matches[0]
        head_record = pull.get("head") if isinstance(pull.get("head"), dict) else {}
        base_record = pull.get("base") if isinstance(pull.get("base"), dict) else {}
        if type(request.get("id")) is not int or request["id"] <= 0 or request.get("run_attempt") != 1 or \
                request.get("path") != REQUEST_WORKFLOW or request.get("event") != "pull_request" or \
                full_name(request.get("repository")) != repository or full_name(request.get("head_repository")) != repository or \
                identity(request.get("actor")) != MAINTAINER or identity(request.get("triggering_actor")) != MAINTAINER or \
                identity(pull.get("user")) != MAINTAINER or type(pull.get("number")) is not int or pull["number"] <= 0 or \
                full_name(head_record.get("repo")) != repository or full_name(base_record.get("repo")) != repository or \
                not isinstance(head_record.get("sha"), str) or not COMMIT.fullmatch(head_record["sha"]):
            raise ValueError("sampling history owner/request provenance is invalid")
        user = pull.get("user") if isinstance(pull.get("user"), dict) else {}
        actor = request.get("actor") if isinstance(request.get("actor"), dict) else {}
        triggering = request.get("triggering_actor") if isinstance(request.get("triggering_actor"), dict) else {}
        external = re.compile((r"buster-compiler-preparation-v1:" if preparation else r"buster-main-sampling-v1:") + re.escape(history_campaign) + ":" +
                              re.escape(phase) + ":" + re.escape(packet) + ":" +
                              str(request["id"]) + r":([1-9][0-9]*):1\Z")
        wanted_check = PREPARATION_CHECK if preparation else SAMPLING_CHECK
        checks = fetch(f"/repos/{repository}/commits/{sha}/check-runs?check_name={urllib.parse.quote(wanted_check)}&filter=all&per_page=100", token)
        checks = checks.get("check_runs") if isinstance(checks, dict) else None
        if not isinstance(checks, list) or len(checks) >= 100:
            raise ValueError("sampling executor check history is unavailable or capped")
        matching = [check for check in checks if isinstance(check, dict) and
                    check.get("name") == wanted_check and check.get("head_sha") == sha and
                    isinstance(check.get("app"), dict) and check["app"].get("id") == 15368 and
                    isinstance(check.get("external_id"), str) and external.fullmatch(check["external_id"])]
        request_state = request.get("conclusion")
        executor_id, executor_attempt, state, physical = "-", "-", request_state if request_state in ("failed", "failure", "cancelled") else "not_run", "-"
        if state == "failure":
            state = "failed"
        if len(matching) > 1:
            raise ValueError("sampling request has duplicate executor checks")
        if matching:
            check = matching[0]
            executor_id = external.fullmatch(check["external_id"])[1]
            execution = fetch(f"/repos/{repository}/actions/runs/{executor_id}", token)
            jobs = fetch(f"/repos/{repository}/actions/runs/{executor_id}/attempts/1/jobs?per_page=100", token)
            jobs = jobs.get("jobs") if isinstance(jobs, dict) else None
            if not isinstance(execution, dict) or execution.get("id") != int(executor_id) or \
                    execution.get("path") != ".github/workflows/9700x-direct-bench.yml" or \
                    execution.get("event") != "workflow_run" or execution.get("head_branch") != "main" or execution.get("run_attempt") != 1 or \
                    execution.get("display_title") != f"9700X request {request['id']}.1 head {sha}" or \
                    full_name(execution.get("repository")) != repository or full_name(execution.get("head_repository")) != repository or \
                    identity(execution.get("actor")) != MAINTAINER or identity(execution.get("triggering_actor")) != MAINTAINER or \
                    not isinstance(execution.get("head_sha"), str) or not COMMIT.fullmatch(execution["head_sha"]) or \
                    not isinstance(jobs, list) or len(jobs) >= 100:
                raise ValueError("sampling executor provenance is unavailable")
            executor_attempt = str(execution.get("run_attempt"))
            host_name = "Compiler preparation qualification" if preparation else "Sampling qualification packet"
            published_name = "Validate compiler preparation evidence" if preparation else "Validate sampling packet evidence"
            host = [job for job in jobs if isinstance(job, dict) and job.get("name") == host_name]
            published = [job for job in jobs if isinstance(job, dict) and job.get("name") == published_name]
            state = "cancelled" if execution.get("conclusion") == "cancelled" else "failed" if execution.get("conclusion") == "failure" else "incomplete"
            if len(host) == 1:
                start, end = host[0].get("started_at"), host[0].get("completed_at")
                if isinstance(start, str) and isinstance(end, str):
                    started = datetime.fromisoformat(start.replace("Z", "+00:00"))
                    completed = datetime.fromisoformat(end.replace("Z", "+00:00"))
                    if started.tzinfo is None or completed.tzinfo is None or \
                            started.utcoffset() is None or completed.utcoffset() is None:
                        raise ValueError("sampling physical occupancy timezone is unavailable")
                    duration = (completed - started).total_seconds()
                    if duration <= 0:
                        raise ValueError("sampling physical occupancy timestamps are invalid")
                    physical = str(round(duration * 1000000) + 2000000)
            if check.get("status") == "completed" and execution.get("status") == "completed" and execution.get("conclusion") == "success":
                state = "complete" if check.get("conclusion") == "success" and \
                    isinstance(check.get("output"), dict) and check["output"].get("title") == ("Valid unqualified preparation packet" if preparation else "Valid unqualified sampling packet") and \
                    physical != "-" and len(host) == len(published) == 1 and host[0].get("conclusion") == published[0].get("conclusion") == "success" \
                    else "invalid"
        rows.append([phase, packet, str(request["id"]), str(request.get("run_attempt")), executor_id,
                     executor_attempt, state, physical, history_campaign, revision,
                     str(actor.get("login", "-")), str(actor.get("id", "-")),
                     str(triggering.get("login", "-")), str(triggering.get("id", "-")),
                     str(user.get("login", "-")), str(user.get("id", "-"))])
    return rows


def sampling_fresh_selector(text: str, compared_parents: list) -> tuple[str, str, str, str] | None:
    lines = [line for line in text.splitlines() if line.startswith(SAMPLING_PREFIX)]
    fresh = [line for line in lines if compared_parents and all(sampling_added(row, line) == line for row in compared_parents)]
    if not fresh:
        return None
    if any(line.startswith(PREPARATION_PREFIX) for line in text.splitlines()):
        raise ValueError("replace the historical preparation selector before requesting sampling")
    return sampling_selector(text)


def sampling_patch_requested(compared_parents: list, prefix: str = SAMPLING_PREFIX) -> bool:
    common = None
    for compared in compared_parents:
        rows = compared.get("files") if isinstance(compared, dict) else []
        row = next((row for row in rows if isinstance(row, dict) and row.get("filename") == COMPARE_REQUEST), {})
        patch = row.get("patch")
        lines = patch.splitlines() if isinstance(patch, str) else []
        added = {line[1:] for line in lines if line.startswith("+") and not line.startswith("+++")}
        removed = {line[1:] for line in lines if line.startswith("-") and not line.startswith("---")}
        added = {line for line in added - removed if line.startswith(prefix)}
        common = added if common is None else common & added
    return bool(common)


def qualification_facts(repository: str, run: dict, pull: dict, head: str, attempt: str,
                        line: str, compared_parents: list) -> dict:
    """Observe shared exact-request facts; native policies decide admission."""
    actor, triggering = run.get("actor", {}), run.get("triggering_actor", {})
    return {
        "schema": "buster-main-sampling-github-facts-v1", "repository": repository,
        "request_run_id": str(run["id"]), "request_run_attempt": str(run.get("run_attempt")),
        "executor_run_id": os.environ.get("GITHUB_RUN_ID", "-"), "executor_run_attempt": attempt,
        "request_head": head, "trusted_revision": os.environ.get("GITHUB_SHA", "-"),
        "owner_login": MAINTAINER["login"], "owner_id": str(MAINTAINER["id"]),
        "actor_login": str(actor.get("login")), "actor_id": str(actor.get("id")),
        "triggering_login": str(triggering.get("login")), "triggering_id": str(triggering.get("id")),
        "pull_author_login": str(pull.get("user", {}).get("login")), "pull_author_id": str(pull.get("user", {}).get("id")),
        "request_repository": str(full_name(run.get("repository"))),
        "request_head_repository": str(full_name(run.get("head_repository"))),
        "pull_repository": str(full_name(pull.get("head", {}).get("repo"))), "pull_state": str(pull.get("state")),
        "parent_count": str(len(compared_parents)),
        "fresh_parent_0": sampling_added(compared_parents[0], line) if compared_parents else "-",
        "fresh_parent_1": sampling_added(compared_parents[1], line) if len(compared_parents) == 2 else "-",
    }


def sampling_data(repository: str, token: str, run: dict, pull: dict, head: str, attempt: str,
                  marker: str, compared_parents: list, directory: Path) -> bool:
    """Write API records for native admission; no emitted fact authorizes host work."""
    selected = sampling_selector(marker)
    if selected is None:
        return False
    directory.mkdir(parents=True, exist_ok=False)
    line, phase, packet, revision = selected
    policy_revision = os.environ.get("GITHUB_SHA", "")
    if not COMMIT.fullmatch(policy_revision):
        raise ValueError("sampling policy lacks the current trusted workflow revision")
    allowlist_text = sampling_content(repository, SAMPLING_ALLOWLIST, policy_revision, token)
    allowlist = dict(row.split("\t") for row in allowlist_text.splitlines() if "\t" in row)
    freeze_text = sampling_content(repository, SAMPLING_FREEZE, revision, token)
    parent_revision = allowlist.get("parent_freeze_revision", "-")
    parent_text = "" if parent_revision == "-" else sampling_content(repository, SAMPLING_FREEZE, parent_revision, token)
    # Transport lineage values only. The native policy parses the exact bounded
    # parent bytes, checks all identities and authenticates each hash.
    parent_fields = dict(row.split("\t") for row in parent_text.splitlines() if "\t" in row)
    ancestor_revision = parent_fields.get("campaign_parent_revision", "-") if phase == "confirm" else "-"
    ancestor_campaign = parent_fields.get("campaign_parent", "-") if phase == "confirm" else "-"
    acquisition_text = freeze_text if phase == "acquire" else parent_text if phase == "pilot" else (
        sampling_content(repository, SAMPLING_FREEZE, ancestor_revision, token)
        if COMMIT.fullmatch(ancestor_revision) else "")
    history = sampling_attempt_history(
        repository, token, str(run["id"]), allowlist.get("history_since", "-"), revision,
        allowlist.get("freeze_sha256", "-"), allowlist.get("parent_freeze_revision", "-"),
        allowlist.get("campaign_parent", "-"), ancestor_revision, ancestor_campaign)
    facts = qualification_facts(repository, run, pull, head, attempt, line, compared_parents)
    for name, text in (("request.txt", line + "\n"), ("freeze.tsv", freeze_text), ("parent-freeze.tsv", parent_text),
                       ("acquisition-plan.tsv", acquisition_text), ("allowlist.tsv", allowlist_text),
                       ("facts.tsv", "".join(f"{key}\t{value}\n" for key, value in facts.items())),
                       ("history.tsv", "\t".join(SAMPLING_HISTORY_HEADER) + "\n" +
                        "".join("\t".join(row) + "\n" for row in history))):
        (directory / name).write_text(text, encoding="utf-8")
    return True


def sampling_transport(directory: Path, preparation: bool = False) -> dict[str, str]:
    """Bounded data-only output for a tokenless native physical executor."""
    fields = {"request": "request.txt", "freeze": "freeze.tsv", "parent_freeze": "parent-freeze.tsv",
              "acquisition_plan": "acquisition-plan.tsv", "allowlist": "allowlist.tsv",
              "facts": "facts.tsv", "history": "history.tsv"}
    if preparation:
        fields = {"request": "request.txt", "plan": "plan.tsv", "allowlist": "allowlist.tsv", "facts": "facts.tsv", "history": "history.tsv"}
    values = {}
    total = 0
    for key, name in fields.items():
        payload = (directory / name).read_bytes()
        encoded = base64.b64encode(payload).decode("ascii")
        total += len(encoded)
        if len(encoded) > 65536 or total > 262144:
            raise ValueError("sampling transport exceeds its native data bound")
        values[f"{'preparation' if preparation else 'sampling'}_{key}_data"] = encoded
    return values


def main() -> int:
    environment = os.environ
    repository = environment.get("BQ_REPOSITORY", "")
    run_id = environment.get("BQ_REQUEST_RUN_ID", "")
    head = environment.get("BQ_HEAD_COMMIT", "")
    attempt = environment.get("BQ_RUN_ATTEMPT", "")
    token = environment.get("GH_TOKEN", "")
    output = environment.get("GITHUB_OUTPUT", "")
    failures: list[str] = []
    base = ""
    if not (REPOSITORY.fullmatch(repository) and DECIMAL.fullmatch(run_id) and COMMIT.fullmatch(head)
            and DECIMAL.fullmatch(attempt) and token and output):
        failures.append("workflow inputs")
    else:
        run = fetch(f"/repos/{repository}/actions/runs/{run_id}", token)
        pulls = fetch(f"/repos/{repository}/commits/{head}/pulls?per_page=100", token)
        failures, base = verify(repository, int(run_id), head, run, pulls)
    number = next(pull["number"] for pull in pulls if isinstance(pull, dict) and pull.get("state") == "open"
                  and isinstance(pull.get("head"), dict) and pull["head"].get("sha") == head) if not failures else 0
    files: list = []
    if not failures:
        detail = fetch(f"/repos/{repository}/pulls/{number}", token)
        if not (isinstance(detail, dict) and detail.get("number") == number
                and isinstance(detail.get("head"), dict) and detail["head"].get("sha") == head):
            failures.append("pull request record for the head commit")
        else:
            files, problems = inventory(
                lambda page: fetch(f"/repos/{repository}/pulls/{number}/files?per_page={FILE_PAGE_SIZE}&page={page}",
                                   token), detail.get("changed_files"))
            failures.extend(problems)
    workloads, compare, problems = plan(files if not failures else [])
    failures.extend(problems)
    request_files: list[dict] = []
    compared_parents: list = []
    sampling_requested = False
    preparation_requested = False
    transported = {}
    if not failures and (workloads or compare):
        request_commit = fetch(f"/repos/{repository}/commits/{head}", token)
        parents = request_commit.get("parents", []) if isinstance(request_commit, dict) else []
        parents = [row.get("sha") for row in parents if isinstance(row, dict)] if isinstance(parents, list) else []
        compared_parents = [
            fetch(f"/repos/{repository}/compare/{parent}...{head}", token)
            for parent in parents[:2] if isinstance(parent, str) and COMMIT.fullmatch(parent)]
        delta, problems = request_delta(head, request_commit, compared_parents)
        failures.extend(problems)
        changed = {row["filename"] for row in delta}
        request_files = [row for row in files if row["filename"] in changed]
        fresh_workloads, fresh_compare, problems = plan(request_files)
        failures.extend(problems)
        workloads, compare = workloads and fresh_workloads, compare and fresh_compare
    extra = {"merge_base": "", "merge_base_tree": "", "head_tree": ""}
    if not failures and compare:
        compared = fetch(f"/repos/{repository}/compare/{urllib.parse.quote(base)}...{head}", token)
        problems, extra = comparison(head, compared, fetch(f"/repos/{repository}/commits/{head}", token))
        failures.extend(problems)
    if not failures and compare and sampling_patch_requested(compared_parents, PREPARATION_PREFIX):
        marker = sampling_content(repository, COMPARE_REQUEST, head, token)
        selected = preparation_fresh_selector(marker, compared_parents)
        if selected is not None:
            pull = next(row for row in pulls if row.get("number") == number)
            directory = Path(environment["RUNNER_TEMP"]) / "compiler-preparation-admission"
            preparation_requested = preparation_data(repository, token, run, pull, head, attempt, marker, compared_parents, directory)
            transported = sampling_transport(directory, preparation=True)
            compare = False
            workloads = False
    if not failures and compare and sampling_patch_requested(compared_parents):
        marker = sampling_content(repository, COMPARE_REQUEST, head, token)
        selected = sampling_fresh_selector(marker, compared_parents)
        if selected is not None:
            pull = next(row for row in pulls if row.get("number") == number)
            sampling_requested = sampling_data(repository, token, run, pull, head, attempt, marker, compared_parents,
                                                Path(environment["RUNNER_TEMP"]) / "compiler-sampling-admission")
            transported = sampling_transport(Path(environment["RUNNER_TEMP"]) / "compiler-sampling-admission")
            compare = False
            workloads = False
    if failures:
        print("BENCH_DIRECT_UNAUTHORIZED " + ", ".join(failures), file=sys.stderr)
    else:
        paths = [row["filename"] for row in request_files
                 if row["filename"] in (COMPARE_REQUEST, SCALING_REQUEST)
                 or re.fullmatch(r"benchmarks/9700x/[^/]+\.(c|data)", row["filename"])]
        print(f"BENCH_DIRECT_REQUEST head={head} files={json.dumps(paths)} "
              f"workloads={str(workloads).lower()} compare={str(compare).lower()}")
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"attempt={attempt}\nbase={base}\npull={number}\nworkloads={str(workloads).lower()}\n"
                         f"request_head={head}\ncompare={str(compare).lower()}\nsampling_requested={str(sampling_requested).lower()}\npreparation_requested={str(preparation_requested).lower()}\nmerge_base={extra['merge_base']}\n"
                         f"merge_base_tree={extra['merge_base_tree']}\nhead_tree={extra['head_tree']}\n")
            for key, value in transported.items():
                stream.write(f"{key}={value}\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
