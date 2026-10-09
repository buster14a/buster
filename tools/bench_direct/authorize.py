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


def verify(repository: str, run_id: int, head: str, run: object, pulls: object,
           expected_run_attempt: int | None = None) -> tuple[list[str], str]:
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
    if expected_run_attempt is not None and (type(expected_run_attempt) is not int or expected_run_attempt <= 0 or
            type(run.get("run_attempt")) is not int or run.get("run_attempt") != expected_run_attempt):
        failures.append("request run attempt")
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
UTILITY_PREFIX = "profile: compiler-baseline-closure-utility-"
UTILITY_PLAN = "docs/compiler-closure-utility-plan-v1.tsv"
UTILITY_ALLOWLIST = "docs/compiler-closure-utility-admission-v1.tsv"
UTILITY_CHECK = "9700X compiler closure utility research"
UTILITY_MARKER = re.compile(r"profile: compiler-baseline-closure-utility-v1 packet: 0 freeze: ([0-9a-f]{40})")
PREPARATION_MARKER = re.compile(r"profile: compiler-baseline-closure-qualification-v1 packet: 0 freeze: ([0-9a-f]{40})")
SAMPLING_MARKER = re.compile(
    r"profile: compiler-main-sampling-(acquire|pilot|confirm)-v1 packet: (0|[1-9][0-9]*) freeze: ([0-9a-f]{40})")
SAMPLING_HISTORY_HEADER = (
    "phase", "packet", "request_run_id", "request_run_attempt", "executor_run_id", "executor_run_attempt",
    "state", "physical_wall_us", "campaign", "freeze_revision", "actor_login", "actor_id",
    "triggering_login", "triggering_id", "pull_author_login", "pull_author_id")


def sampling_content(repository: str, path: str, revision: str, token: str, *, api=None) -> str:
    """A bounded UTF-8 GitHub contents record, consumed only as data."""
    endpoint = f"/contents/{path}?ref={urllib.parse.quote(revision)}"
    row = api.request(endpoint) if api is not None else fetch(f"/repos/{repository}" + endpoint, token)
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
    if any(line.startswith((SAMPLING_PREFIX, UTILITY_PREFIX)) for line in text.splitlines()):
        raise ValueError("replace the historical sampling selector before requesting preparation")
    return preparation_selector(text)


def utility_selector(text: str) -> tuple[str, str, str, str] | None:
    """Read the separately versioned utility selector as data."""
    lines = [line for line in text.splitlines() if line.startswith(UTILITY_PREFIX)]
    if not lines:
        return None
    if len(lines) != 1 or not (match := UTILITY_MARKER.fullmatch(lines[0])):
        raise ValueError("utility selector is malformed or repeated")
    return lines[0], "utility", "0", match[1]


def utility_fresh_selector(text: str, compared_parents: list) -> tuple[str, str, str, str] | None:
    lines = [line for line in text.splitlines() if line.startswith(UTILITY_PREFIX)]
    fresh = [line for line in lines if compared_parents and all(sampling_added(row, line) == line for row in compared_parents)]
    if not fresh:
        return None
    if any(line.startswith((SAMPLING_PREFIX, PREPARATION_PREFIX)) for line in text.splitlines()):
        raise ValueError("replace historical experimental selectors before requesting utility")
    return utility_selector(text)


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


def utility_data(repository: str, token: str, run: dict, pull: dict, head: str, attempt: str,
                     marker: str, compared_parents: list, directory: Path) -> bool:
    """Observe five bounded records for the distinct native utility policy."""
    selected = utility_fresh_selector(marker, compared_parents)
    if selected is None:
        return False
    directory.mkdir(parents=True, exist_ok=False)
    line, _, _, revision = selected
    policy_revision = os.environ.get("GITHUB_SHA", "")
    if not COMMIT.fullmatch(policy_revision):
        raise ValueError("utility policy lacks the current trusted workflow revision")
    allowlist_text = sampling_content(repository, UTILITY_ALLOWLIST, policy_revision, token)
    plan_text = sampling_content(repository, UTILITY_PLAN, revision, token)
    allowlist = dict(row.split("\t") for row in allowlist_text.splitlines() if "\t" in row)
    history = sampling_attempt_history(repository, token, str(run["id"]), allowlist.get("history_since", "-"),
                                      revision, allowlist.get("freeze_sha256", "-"), "-", "-", utility=True)
    facts = qualification_facts(repository, run, pull, head, attempt, line, compared_parents)
    for name, text in (("request.txt", line + "\n"), ("plan.tsv", plan_text), ("allowlist.tsv", allowlist_text),
                       ("facts.tsv", "".join(f"{key}\t{value}\n" for key, value in facts.items())),
                       ("history.tsv", "\t".join(SAMPLING_HISTORY_HEADER) + "\n" +
                        "".join("\t".join(row) + "\n" for row in history))):
        (directory / name).write_text(text, encoding="utf-8")
    return True


def sampling_executor_inventory(repository: str, token: str, since: str, *, api=None) -> list[dict]:
    """Bounded original executor inventory; a missing check does not erase an attempt."""
    if since == "-":
        return []
    values, total = [], None
    query = urllib.parse.urlencode({"event": "workflow_run", "created": ">=" + since, "per_page": 100})
    for page in range(1, 11):
        path = f"/actions/workflows/9700x-direct-bench.yml/runs?{query}&page={page}"
        listed = api.request(path) if api is not None else fetch(f"/repos/{repository}" + path, token)
        rows = listed.get("workflow_runs") if isinstance(listed, dict) else None
        observed = listed.get("total_count") if isinstance(listed, dict) else None
        if not isinstance(rows, list) or type(observed) is not int or not 0 <= observed <= 1000:
            raise ValueError("original experimental executor inventory is unavailable or capped")
        if total is None:
            total = observed
        if total != observed:
            raise ValueError("original experimental executor inventory changed during reading")
        values.extend(rows)
        if len(values) >= total:
            break
        if len(rows) != 100:
            raise ValueError("original experimental executor inventory is incomplete")
    if len(values) != total or any(not isinstance(row, dict) or type(row.get("id")) is not int or
                                  row["id"] <= 0 for row in values) or \
            len({row["id"] for row in values}) != total:
        raise ValueError("original experimental executor inventory is incomplete or duplicated")
    return values


def sampling_attempt_history(repository: str, token: str, current: str, since: str,
                             freeze_revision: str, campaign: str, parent_revision: str,
                             parent_campaign: str, ancestor_revision: str = "-", ancestor_campaign: str = "-",
                             preparation: bool = False, utility: bool = False, *, api=None,
                             historical: bool = False, before_created: str | None = None) -> list[list[str]]:
    """Complete bounded GitHub request/executor records, including hostless attempts."""
    if preparation and utility:
        raise ValueError("experimental history kind is ambiguous")
    if since == "-":  # Disabled configuration supplies no admission history window.
        return []
    cutoff = None
    if historical:
        if not isinstance(before_created, str) or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", before_created):
            raise ValueError("historical sampling original creation timestamp is unavailable")
        cutoff = datetime.fromisoformat(before_created.replace("Z", "+00:00"))
    def read(path):
        prefix = f"/repos/{repository}"
        if api is not None:
            if not path.startswith(prefix + "/"):
                raise ValueError("historical API data path is foreign")
            return api.request(path[len(prefix):])
        return fetch(path, token)
    rows = []
    total = None
    requests = []
    executor_inventory = sampling_executor_inventory(repository, token, since, api=api)
    query = urllib.parse.urlencode({"event": "pull_request", "created": ">=" + since, "per_page": 100})
    for page in range(1, 11):
        listed = read(f"/repos/{repository}/actions/workflows/9700x-direct-request.yml/runs?{query}&page={page}")
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
    if historical:
        for listed_request in requests:
            created_text = listed_request.get("created_at") if isinstance(listed_request, dict) else None
            if not isinstance(listed_request, dict) or type(listed_request.get("id")) is not int or listed_request["id"] <= 0 or \
                    not isinstance(created_text, str) or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", created_text):
                raise ValueError("historical listed request has no exact chronological identity")
            datetime.fromisoformat(created_text.replace("Z", "+00:00"))
    for request in sorted(requests, key=lambda row: (row.get("created_at", ""), row.get("id", 0))):
        if str(request.get("id")) == current:
            continue
        if historical:
            if type(request.get("id")) is not int or request["id"] <= 0:
                raise ValueError("historical request has no typed original ID")
            latest = request
            listed_created = datetime.fromisoformat(latest["created_at"].replace("Z", "+00:00"))
            if listed_created > cutoff or listed_created == cutoff and latest["id"] >= int(current):
                continue
            request = read(f"/repos/{repository}/actions/runs/{latest['id']}/attempts/1")
            if not isinstance(request, dict) or type(request.get("id")) is not int or request["id"] != latest["id"] or type(request.get("run_attempt")) is not int or request["run_attempt"] != 1:
                raise ValueError("historical request original first attempt is unavailable")
            created_text = request.get("created_at")
            if not isinstance(created_text, str) or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", created_text):
                raise ValueError("historical sampling prefix creation timestamp is unavailable")
            created = datetime.fromisoformat(created_text.replace("Z", "+00:00"))
            if created != listed_created:
                raise ValueError("historical request listing relabeled its original creation time")
            if created > cutoff or created == cutoff and request["id"] >= int(current):
                raise ValueError("historical prefix changed its original chronological boundary")
        sha = request.get("head_sha")
        if not isinstance(sha, str) or not COMMIT.fullmatch(sha):
            raise ValueError("sampling history request has no exact commit")
        # A missing marker on an unrelated request is ordinary history.
        try:
            marker_text = sampling_content(repository, COMPARE_REQUEST, sha, token, api=api)
            marker = utility_selector(marker_text) if utility else preparation_selector(marker_text) if preparation else sampling_selector(marker_text)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                continue
            raise
        if marker is None or marker[3] not in (freeze_revision, parent_revision, ancestor_revision):
            continue
        # A later ordinary synchronize can inherit the old selector. Only the
        # immutable every-parent delta establishes a new declared attempt.
        source_commit = read(f"/repos/{repository}/commits/{sha}")
        parents = source_commit.get("parents") if isinstance(source_commit, dict) else None
        parents = [row.get("sha") for row in parents if isinstance(row, dict)] if isinstance(parents, list) else []
        parent_deltas = [read(f"/repos/{repository}/compare/{parent}...{sha}")
                         for parent in parents[:2] if isinstance(parent, str) and COMMIT.fullmatch(parent)]
        _, delta_problems = request_delta(sha, source_commit, parent_deltas)
        if delta_problems:
            raise ValueError("sampling historical every-parent delta is incomplete: " + "; ".join(delta_problems))
        marker = utility_fresh_selector(marker_text, parent_deltas) if utility else preparation_fresh_selector(marker_text, parent_deltas) if preparation else sampling_fresh_selector(marker_text, parent_deltas)
        if marker is None:
            continue
        if historical and (type(latest.get("run_attempt")) is not int or latest["run_attempt"] != 1):
            raise ValueError("historical research request was rerun")
        selector, phase, packet, revision = marker
        history_campaign = campaign if revision == freeze_revision else parent_campaign if revision == parent_revision else ancestor_campaign
        associated = read(f"/repos/{repository}/commits/{sha}/pulls?per_page=100")
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
        external = re.compile((r"buster-compiler-closure-utility-v1:" if utility else r"buster-compiler-preparation-v1:" if preparation else r"buster-main-sampling-v1:") + re.escape(history_campaign) + ":" +
                              re.escape(phase) + ":" + re.escape(packet) + ":" +
                              str(request["id"]) + r":([1-9][0-9]*):1\Z")
        wanted_check = UTILITY_CHECK if utility else PREPARATION_CHECK if preparation else SAMPLING_CHECK
        checks = read(f"/repos/{repository}/commits/{sha}/check-runs?check_name={urllib.parse.quote(wanted_check)}&filter=all&per_page=100")
        checks = checks.get("check_runs") if isinstance(checks, dict) else None
        if not isinstance(checks, list) or len(checks) >= 100:
            raise ValueError("sampling executor check history is unavailable or capped")
        matching = [check for check in checks if isinstance(check, dict) and
                    check.get("name") == wanted_check and check.get("head_sha") == sha and
                    isinstance(check.get("app"), dict) and check["app"].get("id") == 15368 and
                    isinstance(check.get("external_id"), str) and external.fullmatch(check["external_id"])]
        expected_title = f"9700X request {request['id']}.1 head {sha}"
        title_prefix = f"9700X request {request['id']}.1 "
        candidates = [row for row in executor_inventory if isinstance(row.get("display_title"), str) and
                      row["display_title"].startswith(title_prefix)]
        if len(candidates) > 1 or candidates and candidates[0]["display_title"] != expected_title:
            raise ValueError("sampling original executor title inventory is ambiguous or relabeled")
        request_state = request.get("conclusion")
        executor_id, executor_attempt, state, physical = "-", "-", request_state if request_state in ("failed", "failure", "cancelled") else "hostless", "-"
        if state == "failure":
            state = "failed"
        if len(matching) > 1:
            raise ValueError("sampling request has duplicate executor checks")
        if matching and (not candidates or str(candidates[0]["id"]) != external.fullmatch(matching[0]["external_id"])[1]):
            raise ValueError("sampling retained check contradicts complete original executor inventory")
        if matching or candidates:
            check = matching[0] if matching else None
            executor_id = str(candidates[0]["id"])
            latest_execution = read(f"/repos/{repository}/actions/runs/{executor_id}")
            execution = read(f"/repos/{repository}/actions/runs/{executor_id}/attempts/1") if historical else latest_execution
            if historical and (not isinstance(latest_execution, dict) or type(latest_execution.get("id")) is not int or
                               latest_execution["id"] != int(executor_id) or type(latest_execution.get("run_attempt")) is not int or latest_execution["run_attempt"] != 1):
                raise ValueError("historical research executor was rerun")
            jobs = read(f"/repos/{repository}/actions/runs/{executor_id}/attempts/1/jobs?per_page=100")
            jobs = jobs.get("jobs") if isinstance(jobs, dict) else None
            if not isinstance(execution, dict) or type(execution.get("id")) is not int or execution["id"] != int(executor_id) or \
                    execution.get("path") != ".github/workflows/9700x-direct-bench.yml" or \
                    execution.get("event") != "workflow_run" or execution.get("head_branch") != "main" or type(execution.get("run_attempt")) is not int or execution["run_attempt"] != 1 or \
                    execution.get("display_title") != f"9700X request {request['id']}.1 head {sha}" or \
                    full_name(execution.get("repository")) != repository or full_name(execution.get("head_repository")) != repository or \
                    identity(execution.get("actor")) != MAINTAINER or identity(execution.get("triggering_actor")) != MAINTAINER or \
                    not isinstance(execution.get("head_sha"), str) or not COMMIT.fullmatch(execution["head_sha"]) or \
                    not isinstance(jobs, list) or len(jobs) >= 100:
                raise ValueError("sampling executor provenance is unavailable")
            executor_attempt = str(execution.get("run_attempt"))
            host_name = "Compiler closure utility" if utility else "Compiler preparation qualification" if preparation else "Sampling qualification packet"
            published_name = "Validate compiler closure utility evidence" if utility else "Validate compiler preparation evidence" if preparation else "Validate sampling packet evidence"
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
            if check is not None and check.get("status") == "completed" and execution.get("status") == "completed" and execution.get("conclusion") == "success":
                state = "complete" if check.get("conclusion") == "success" and \
                    isinstance(check.get("output"), dict) and check["output"].get("title") == ("Valid unqualified utility packet" if utility else "Valid unqualified preparation packet" if preparation else "Valid unqualified sampling packet") and \
                    physical != "-" and len(host) == len(published) == 1 and host[0].get("conclusion") == published[0].get("conclusion") == "success" \
                    else "invalid"
        rows.append([phase, packet, str(request["id"]), str(request.get("run_attempt")), executor_id,
                     executor_attempt, state, physical, history_campaign, revision,
                     str(actor.get("login", "-")), str(actor.get("id", "-")),
                     str(triggering.get("login", "-")), str(triggering.get("id", "-")),
                     str(user.get("login", "-")), str(user.get("id", "-"))])
    return rows



def sampling_review_record(text: str) -> dict[str, str]:
    """Bounded data mapping; the distinct native reader owns historical validity."""
    result = {}
    if not isinstance(text, str) or not 0 < len(text.encode("utf-8")) <= 128 * 1024:
        raise ValueError("historical sampling record is unavailable or oversized")
    for row in text.splitlines(keepends=True):
        if not row.endswith("\n"):
            raise ValueError("historical sampling record is partial")
        columns = row[:-1].split("\t")
        if len(columns) != 2 or not all(columns) or columns[0] in result:
            raise ValueError("historical sampling record is ambiguous")
        result[columns[0]] = columns[1]
    return result


def sampling_review_native(records: dict[str, str]) -> dict[str, str]:
    """One fixed hosted native DATA validation; this output cannot admit execution."""
    import subprocess
    import tempfile
    root = Path(__file__).resolve().parents[2]
    order = ("allowlist", "request", "facts", "history", "freeze", "parent", "acquisition", "api")
    with tempfile.TemporaryDirectory(prefix="sampling-historical-review-") as temporary:
        directory = Path(temporary)
        for name in order:
            (directory / (name + ".tsv")).write_text(records[name], encoding="utf-8")
        output = directory / "review.txt"
        command = [str(root / "build.sh"), "compiler_profile_qualification", "--validate-historical-sampling",
                   *(str(directory / (name + ".tsv")) for name in order), str(output)]
        with (directory / "native.log").open("xb") as log:
            completed = subprocess.run(command, cwd=root, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT, timeout=120, check=False)
        if completed.returncode != 0 or not output.is_file() or output.stat().st_size > 16384:
            raise ValueError("native historical sampling evidence validation refused the attempt")
        result = {}
        for row in output.read_text(encoding="ascii").splitlines():
            key, separator, value = row.partition("=")
            if not separator or not key.startswith("sampling_") or key in result or not value:
                raise ValueError("native historical sampling output is ambiguous")
            result[key] = value
        if result.get("sampling_historical_valid") != "true" or "sampling_admitted" in result or \
                result.get("sampling_historical_execution_authority") != "false" or \
                result.get("sampling_historical_qualification") != "unqualified":
            raise ValueError("historical sampling validation changed its data-only boundary")
    return result


def sampling_review_compare_head(compared: dict, head: str) -> str:
    """Observe the last commit of a complete GitHub compare response, not a requested label."""
    commits = compared.get("commits") if isinstance(compared, dict) else None
    total = compared.get("total_commits") if isinstance(compared, dict) else None
    if not isinstance(commits, list) or type(total) is not int or not 0 < total <= 250 or \
            len(commits) != total or compared.get("status") != "ahead" or \
            any(not isinstance(row, dict) or not isinstance(row.get("sha"), str) or
                not COMMIT.fullmatch(row["sha"]) for row in commits):
        raise ValueError("historical compare commit inventory is incomplete")
    observed = commits[-1]["sha"]
    optional = compared.get("head_commit")
    if observed != head or optional is not None and (
            not isinstance(optional, dict) or optional.get("sha") != observed):
        raise ValueError("historical compare endpoint contradicts its observed head")
    return observed


def _review_sampling_attempt(api, repository: str, original_executor_attempt: dict,
                             original_request_attempt: dict, prefix_attempts=None) -> dict:
    """Re-select immutable original API records and committed P_i/F_i; never current OPEN authority."""
    import hashlib
    from types import SimpleNamespace
    if repository != "buster14a/buster":
        raise ValueError("historical sampling repository is foreign")
    observations = {}
    def read(path):
        if path not in observations:
            observations[path] = api.request(path)
        return observations[path]
    observed_api = SimpleNamespace(request=read)
    pairs = []
    for supplied in (original_executor_attempt, original_request_attempt):
        if not isinstance(supplied, dict) or type(supplied.get("id")) is not int or supplied["id"] <= 0 or \
                type(supplied.get("run_attempt")) is not int or supplied["run_attempt"] != 1:
            raise ValueError("historical sampling caller lacks a typed original first attempt")
        run_id = str(supplied["id"])
        actual = read(f"/actions/runs/{run_id}/attempts/1")
        latest = read(f"/actions/runs/{run_id}")
        if not isinstance(actual, dict) or type(actual.get("id")) is not int or actual["id"] != supplied["id"] or \
                type(actual.get("run_attempt")) is not int or actual["run_attempt"] != 1 or \
                not isinstance(latest, dict) or type(latest.get("id")) is not int or latest["id"] != actual["id"] or \
                type(latest.get("run_attempt")) is not int or latest["run_attempt"] != 1:
            raise ValueError("historical sampling original attempt is unavailable or was rerun")
        for key in ("id", "run_attempt", "head_sha", "path", "event", "repository", "head_repository", "display_title"):
            if supplied.get(key) != actual.get(key):
                raise ValueError("historical sampling caller relabeled an original API attempt")
        pairs.append((actual, latest))
    (execution, latest_execution), (request, latest_request) = pairs
    head, policy_revision = request.get("head_sha"), execution.get("head_sha")
    if not isinstance(head, str) or not COMMIT.fullmatch(head) or \
            not isinstance(policy_revision, str) or not COMMIT.fullmatch(policy_revision):
        raise ValueError("historical sampling lacks immutable source/workflow revisions")
    request_id, run_id = str(request["id"]), str(execution["id"])
    if execution.get("path") != ".github/workflows/9700x-direct-bench.yml" or \
            execution.get("event") != "workflow_run" or execution.get("head_branch") != "main" or \
            execution.get("display_title") != f"9700X request {request_id}.1 head {head}" or \
            request.get("path") != REQUEST_WORKFLOW or request.get("event") != "pull_request" or \
            request.get("status") != "completed" or request.get("conclusion") != "success" or \
            any(full_name(row.get(key)) != repository for row in (execution, request)
                for key in ("repository", "head_repository")) or \
            any(identity(row.get(key)) != MAINTAINER for row in (execution, request)
                for key in ("actor", "triggering_actor")):
        raise ValueError("historical sampling original workflow/request provenance is foreign")
    on_main = read(f"/compare/{policy_revision}...main")
    relation = on_main.get("status") if isinstance(on_main, dict) else None
    if relation not in ("ahead", "identical"):
        raise ValueError("historical sampling original policy is outside protected main")
    source_commit = read(f"/commits/{head}")
    parents = source_commit.get("parents") if isinstance(source_commit, dict) else None
    if not isinstance(parents, list) or not 1 <= len(parents) <= 2 or any(
            not isinstance(row, dict) or not isinstance(row.get("sha"), str) or
            not COMMIT.fullmatch(row["sha"]) for row in parents):
        raise ValueError("historical sampling source parent inventory is unavailable")
    comparisons = [read(f"/compare/{row['sha']}...{head}") for row in parents]
    compared_heads = [sampling_review_compare_head(row, head) for row in comparisons]
    unused_files, problems = request_delta(head, source_commit, comparisons)
    if problems:
        raise ValueError("historical sampling every-parent source proof failed: " + "; ".join(problems))
    marker_text = sampling_content(repository, COMPARE_REQUEST, head, "", api=observed_api)
    selected = sampling_fresh_selector(marker_text, comparisons)
    if selected is None:
        raise ValueError("historical sampling selector was inherited, moved or not fresh")
    line, phase, packet, revision = selected
    associated = read(f"/commits/{head}/pulls?per_page=100")
    snapshot = request.get("pull_requests", [])
    if not isinstance(snapshot, list) or len(snapshot) > 1:
        raise ValueError("historical sampling request has ambiguous pull membership")
    number = snapshot[0].get("number") if snapshot and isinstance(snapshot[0], dict) else None
    if snapshot and (type(number) is not int or number <= 0):
        raise ValueError("historical sampling pull snapshot has no typed number")
    matches = [row for row in associated if isinstance(row, dict) and
               (number is None or row.get("number") == number)] if isinstance(associated, list) and len(associated) < 100 else []
    if len(matches) != 1:
        raise ValueError("historical sampling commit has no unique original owning pull")
    pull = matches[0]
    if type(pull.get("number")) is not int or pull["number"] <= 0 or identity(pull.get("user")) != MAINTAINER or \
            pull.get("state") not in ("open", "closed") or \
            not isinstance(pull.get("head"), dict) or not isinstance(pull["head"].get("sha"), str) or \
            not COMMIT.fullmatch(pull["head"]["sha"]) or \
            full_name(pull["head"].get("repo")) != repository or \
            not isinstance(pull.get("base"), dict) or full_name(pull["base"].get("repo")) != repository:
        raise ValueError("historical sampling observed pull membership/owner is foreign")
    allowlist_text = sampling_content(repository, SAMPLING_ALLOWLIST, policy_revision, "", api=observed_api)
    allowlist = sampling_review_record(allowlist_text)
    freeze_text = sampling_content(repository, SAMPLING_FREEZE, revision, "", api=observed_api)
    frozen = sampling_review_record(freeze_text)
    parent_revision = allowlist.get("parent_freeze_revision", "-")
    parent_text = "" if parent_revision == "-" else sampling_content(
        repository, SAMPLING_FREEZE, parent_revision, "", api=observed_api)
    parent = sampling_review_record(parent_text) if parent_text else {}
    ancestor_revision = parent.get("campaign_parent_revision", "-") if phase == "confirm" else "-"
    ancestor_campaign = parent.get("campaign_parent", "-") if phase == "confirm" else "-"
    acquisition_text = freeze_text if phase == "acquire" else parent_text if phase == "pilot" else \
        sampling_content(repository, SAMPLING_FREEZE, ancestor_revision, "", api=observed_api)
    acquisition = sampling_review_record(acquisition_text)
    for reference in {revision, parent_revision, ancestor_revision, acquisition.get("trusted_revision", "-")} - {"-"}:
        if not COMMIT.fullmatch(reference):
            raise ValueError("historical sampling immutable reference is malformed")
        lineage = read(f"/compare/{reference}...{policy_revision}")
        if not isinstance(lineage, dict) or lineage.get("status") not in ("ahead", "identical"):
            raise ValueError("historical sampling frozen source is outside original reviewed policy ancestry")
    executors = sampling_executor_inventory(repository, "", allowlist.get("history_since", "-"), api=observed_api)
    selected_executors = [row for row in executors if isinstance(row.get("display_title"), str) and
                          row["display_title"].startswith(f"9700X request {request_id}.1 ")]
    if len(selected_executors) != 1 or selected_executors[0]["id"] != execution["id"] or \
            selected_executors[0]["display_title"] != execution["display_title"]:
        raise ValueError("historical original executor is not the unique complete inventory member")
    history = sampling_attempt_history(repository, "", request_id, allowlist.get("history_since", "-"),
        revision, allowlist.get("freeze_sha256", "-"), parent_revision, allowlist.get("campaign_parent", "-"),
        ancestor_revision, ancestor_campaign, api=observed_api, historical=True, before_created=request.get("created_at"))
    if prefix_attempts is not None and prefix_attempts != history:
        raise ValueError("historical sampling supplied prefix differs from complete original API history")
    facts = {
        "schema": "buster-main-sampling-github-facts-v1", "repository": repository,
        "request_run_id": request_id, "request_run_attempt": "1", "executor_run_id": run_id,
        "executor_run_attempt": "1", "request_head": head, "trusted_revision": policy_revision,
        "owner_login": MAINTAINER["login"], "owner_id": str(MAINTAINER["id"]),
        "actor_login": request["actor"]["login"], "actor_id": str(request["actor"]["id"]),
        "triggering_login": request["triggering_actor"]["login"], "triggering_id": str(request["triggering_actor"]["id"]),
        "pull_author_login": pull["user"]["login"], "pull_author_id": str(pull["user"]["id"]),
        "request_repository": full_name(request["repository"]), "request_head_repository": full_name(request["head_repository"]),
        "pull_repository": full_name(pull["head"]["repo"]), "pull_state": pull["state"],
        "parent_count": str(len(parents)), "fresh_parent_0": sampling_added(comparisons[0], line),
        "fresh_parent_1": sampling_added(comparisons[1], line) if len(parents) == 2 else "-"}
    facts_text = "".join(f"{key}\t{value}\n" for key, value in facts.items())
    history_text = "\t".join(SAMPLING_HISTORY_HEADER) + "\n" + "".join("\t".join(row) + "\n" for row in history)
    freeze_sha = hashlib.sha256(freeze_text.encode()).hexdigest()
    check_external = f"buster-main-sampling-v1:{freeze_sha}:{phase}:{packet}:{request_id}:{run_id}:1"
    listed = read(f"/commits/{head}/check-runs?check_name={urllib.parse.quote(SAMPLING_CHECK)}&filter=all&per_page=100")
    checks = listed.get("check_runs") if isinstance(listed, dict) else None
    own = [row for row in checks if isinstance(row, dict) and row.get("name") == SAMPLING_CHECK and
           row.get("head_sha") == head and isinstance(row.get("app"), dict) and row["app"].get("id") == 15368 and
           row.get("external_id") == check_external] if isinstance(checks, list) and len(checks) < 100 else []
    if len(own) != 1:
        raise ValueError("historical sampling has no unique retained exact native-admitted check")
    check = own[0]
    def digest(text):
        return hashlib.sha256(text.encode("utf-8")).hexdigest()
    proof = {
        "schema": "buster-main-sampling-historical-api-v1", "repository": repository,
        "policy_revision": policy_revision, "policy_main_relation": relation,
        "request_run_id": request_id, "request_run_attempt": "1", "request_latest_attempt": str(latest_request["run_attempt"]),
        "request_workflow": request["path"], "request_event": request["event"], "request_status": request["status"],
        "request_conclusion": request["conclusion"], "request_head": head, "source_commit": source_commit["sha"],
        "first_parent": parents[0]["sha"], "second_parent": parents[1]["sha"] if len(parents) == 2 else "-",
        "compare_parent_0": comparisons[0]["base_commit"]["sha"], "compare_head_0": compared_heads[0],
        "compare_parent_1": comparisons[1]["base_commit"]["sha"] if len(parents) == 2 else "-",
        "compare_head_1": compared_heads[1] if len(parents) == 2 else "-",
        "executor_run_id": run_id, "executor_run_attempt": "1", "executor_latest_attempt": str(latest_execution["run_attempt"]),
        "executor_workflow": execution["path"], "executor_event": execution["event"], "executor_branch": execution["head_branch"],
        "executor_head": execution["head_sha"], "executor_title": execution["display_title"],
        "executor_status": execution.get("status", "-"), "executor_conclusion": execution.get("conclusion", "-"),
        "executor_actor_login": execution["actor"]["login"], "executor_actor_id": str(execution["actor"]["id"]),
        "executor_triggering_login": execution["triggering_actor"]["login"], "executor_triggering_id": str(execution["triggering_actor"]["id"]),
        "pull_number": str(pull["number"]), "associated_pull_number": str(pull["number"]), "associated_commit": head,
        "pull_state": pull["state"], "pull_current_head": pull["head"]["sha"],
        "allowlist_sha256": digest(allowlist_text), "facts_sha256": digest(facts_text), "history_sha256": digest(history_text),
        "freeze_sha256": freeze_sha, "parent_freeze_sha256": digest(parent_text) if parent_text else "-",
        "acquisition_sha256": digest(acquisition_text), "check_name": check["name"], "check_app_id": str(check["app"]["id"]),
        "check_head": check["head_sha"], "check_external_id": check["external_id"], "check_status": check.get("status", "-"),
        "check_conclusion": check.get("conclusion", "-"),
        "check_title": check.get("output", {}).get("title", "-") if isinstance(check.get("output"), dict) else "-"}
    records = {"allowlist": allowlist_text, "request": line + "\n", "facts": facts_text, "history": history_text,
               "freeze": freeze_text, "parent": parent_text, "acquisition": acquisition_text,
               "api": "".join(f"{key}\t{value}\n" for key, value in proof.items())}
    admitted = sampling_review_native(records)
    envelope = json.dumps({"schema": "buster-main-sampling-original-api-envelope-v1",
                          "repository": repository, "request_run": request_id, "executor_run": run_id,
                          "api_observations": observations, "native_api_proof": proof},
                         sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    if len(envelope) > 8 * 1024 * 1024:
        raise ValueError("historical sampling API envelope exceeds the bounded archive size")
    return {"admitted": admitted, "freeze": frozen, "freeze_bytes": freeze_text.encode("utf-8"),
            "parent_freeze": parent, "acquisition_plan": acquisition, "acquisition_plan_bytes": acquisition_text.encode("utf-8"),
            "facts": facts, "history": [dict(zip(SAMPLING_HISTORY_HEADER, row)) for row in history],
            "request_line": line, "request": request, "executor": execution, "repository": repository,
            "head": head, "request_id": request_id, "run_id": run_id, "historical_review": True,
            "terminal_api_envelope": envelope, "terminal_api_sha256": hashlib.sha256(envelope).hexdigest()}


def review_sampling_authority(api, repository: str, original_executor_attempt: dict,
                              original_request_attempt: dict, prefix_attempts=None) -> dict:
    """Read-only original attempt review. A distinct original acquisition is rebound, not borrowed."""
    authority = _review_sampling_attempt(api, repository, original_executor_attempt, original_request_attempt, prefix_attempts)
    if authority["admitted"]["sampling_phase"] != "acquire":
        acquisitions = [row for row in authority["history"] if row["phase"] == "acquire"]
        if len(acquisitions) != 1:
            raise ValueError("historical sampling prefix lacks its one original acquisition")
        original = acquisitions[0]
        request = api.request(f"/actions/runs/{original['request_run_id']}/attempts/1")
        execution = api.request(f"/actions/runs/{original['executor_run_id']}/attempts/1")
        acquisition = _review_sampling_attempt(api, repository, execution, request, [])
        if acquisition["admitted"]["sampling_phase"] != "acquire" or acquisition["history"] or \
                acquisition["freeze_bytes"] != authority["acquisition_plan_bytes"]:
            raise ValueError("historical sampling acquisition was relabeled from the current attempt")
        authority["historical_acquisition"] = acquisition
    return authority

def sampling_fresh_selector(text: str, compared_parents: list) -> tuple[str, str, str, str] | None:
    lines = [line for line in text.splitlines() if line.startswith(SAMPLING_PREFIX)]
    fresh = [line for line in lines if compared_parents and all(sampling_added(row, line) == line for row in compared_parents)]
    if not fresh:
        return None
    if any(line.startswith((PREPARATION_PREFIX, UTILITY_PREFIX)) for line in text.splitlines()):
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
    if type(run.get("run_attempt")) is not int or run["run_attempt"] != 1 or attempt != "1":
        raise ValueError("experimental facts require actual API request attempt 1 and executor attempt 1")
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


def sampling_transport(directory: Path, preparation: bool = False, utility: bool = False) -> dict[str, str]:
    """Bounded data-only output for a tokenless native physical executor."""
    fields = {"request": "request.txt", "freeze": "freeze.tsv", "parent_freeze": "parent-freeze.tsv",
              "acquisition_plan": "acquisition-plan.tsv", "allowlist": "allowlist.tsv",
              "facts": "facts.tsv", "history": "history.tsv"}
    if preparation and utility:
        raise ValueError("experimental transport kind is ambiguous")
    if preparation or utility:
        fields = {"request": "request.txt", "plan": "plan.tsv", "allowlist": "allowlist.tsv", "facts": "facts.tsv", "history": "history.tsv"}
    values = {}
    total = 0
    for key, name in fields.items():
        payload = (directory / name).read_bytes()
        encoded = base64.b64encode(payload).decode("ascii")
        total += len(encoded)
        if len(encoded) > 65536 or total > 262144:
            raise ValueError("sampling transport exceeds its native data bound")
        values[f"{'utility' if utility else 'preparation' if preparation else 'sampling'}_{key}_data"] = encoded
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
    utility_requested = False
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
    if not failures and compare and sampling_patch_requested(compared_parents, UTILITY_PREFIX):
        marker = sampling_content(repository, COMPARE_REQUEST, head, token)
        selected = utility_fresh_selector(marker, compared_parents)
        if selected is not None:
            pull = next(row for row in pulls if row.get("number") == number)
            directory = Path(environment["RUNNER_TEMP"]) / "compiler-utility-admission"
            utility_requested = utility_data(repository, token, run, pull, head, attempt, marker, compared_parents, directory)
            transported = sampling_transport(directory, utility=True)
            compare = False
            workloads = False
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
                         f"request_head={head}\ncompare={str(compare).lower()}\nsampling_requested={str(sampling_requested).lower()}\npreparation_requested={str(preparation_requested).lower()}\nutility_requested={str(utility_requested).lower()}\nmerge_base={extra['merge_base']}\n"
                         f"merge_base_tree={extra['merge_base_tree']}\nhead_tree={extra['head_tree']}\n")
            for key, value in transported.items():
                stream.write(f"{key}={value}\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
