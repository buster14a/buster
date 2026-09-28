#!/usr/bin/env python3
"""Refresh every open PR against one exact default-branch snapshot.

Pull-request and merge-group admission stay in merge_conflict_preflight.py.  This
module owns only the repository-wide push/workflow-dispatch sweep.  It replaces
per-PR before/after API reads and repeated default-branch fetches with bounded
inventory snapshots, batched immutable-head fetches, and delta reconciliation.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import sys
import time
from typing import Sequence

import merge_conflict_preflight as preflight


MAX_SNAPSHOT_PASSES = 3
HEAD_FETCH_BATCH_SIZE = 32
SNAPSHOT_MAIN_REF = "refs/merge-conflict-preflight/snapshot-main"
SNAPSHOT_HEAD_PREFIX = "refs/merge-conflict-preflight/snapshot-pr-"


@dataclass(frozen=True)
class PullIdentity:
    number: int
    head: str
    base: str

    @property
    def key(self) -> tuple[int, str]:
        return self.number, self.head


@dataclass(frozen=True)
class HeadResolution:
    resolved: dict[tuple[int, str], str]
    failed: dict[tuple[int, str], preflight.PreflightError]
    batch_fetches: int
    fallback_fetches: int


def _default_branch(event: dict) -> str:
    repository = event.get("repository")
    branch = repository.get("default_branch") if isinstance(repository, dict) else None
    if not isinstance(branch, str) or not branch:
        branch = "main"
    return branch


def _inventory(rows: Sequence[dict], default_branch: str) -> list[PullIdentity]:
    identities: list[PullIdentity] = []
    seen: set[int] = set()
    for row in rows:
        number, head, base = preflight._pull_identity(row)
        if number <= 0:
            raise preflight.PreflightError(f"listed pull request has invalid number {number}")
        if number in seen:
            raise preflight.PreflightError(f"open-pull inventory repeats pull request #{number}")
        if base != default_branch:
            raise preflight.PreflightError(
                f"listed pull request #{number} targets {base!r}, expected {default_branch!r}"
            )
        seen.add(number)
        identities.append(PullIdentity(number, head, base))
    return identities


def _remaining(deadline: float | None) -> float:
    return deadline - time.monotonic() if deadline is not None else float("inf")


def _require_budget(deadline: float | None) -> float:
    remaining = _remaining(deadline)
    if remaining <= 0:
        raise preflight.RefreshBudgetError("merge-conflict refresh time budget exhausted")
    return remaining


def _main_ref(default_branch: str) -> str:
    return f"refs/heads/{default_branch}"


def _head_ref(number: int) -> str:
    return f"{SNAPSHOT_HEAD_PREFIX}{number}"


def _fetch_main(repo: Path, default_branch: str, deadline: float | None) -> str:
    return preflight._fetch_ref(repo, _main_ref(default_branch), SNAPSHOT_MAIN_REF, deadline)


def _fetch_head_batch(repo: Path, identities: Sequence[PullIdentity],
                      deadline: float | None) -> dict[tuple[int, str], str]:
    remaining = _require_budget(deadline)
    refspecs = [
        f"+refs/pull/{identity.number}/head:{_head_ref(identity.number)}"
        for identity in identities
    ]
    preflight._git(
        repo, "fetch", "--no-tags", "--force", "origin", *refspecs,
        timeout=min(30, remaining),
    )
    return {
        identity.key: preflight._commit(repo, _head_ref(identity.number))
        for identity in identities
    }


def _fetch_heads(repo: Path, identities: Sequence[PullIdentity],
                 deadline: float | None) -> HeadResolution:
    resolved: dict[tuple[int, str], str] = {}
    failed: dict[tuple[int, str], preflight.PreflightError] = {}
    batch_fetches = 0
    fallback_fetches = 0
    for start in range(0, len(identities), HEAD_FETCH_BATCH_SIZE):
        batch = identities[start:start + HEAD_FETCH_BATCH_SIZE]
        try:
            batch_fetches += 1
            resolved.update(_fetch_head_batch(repo, batch, deadline))
        except preflight.RefreshBudgetError as error:
            for identity in batch:
                failed[identity.key] = error
            for identity in identities[start + len(batch):]:
                failed[identity.key] = error
            break
        except preflight.PreflightError:
            # A moved/closed PR ref can reject a multi-ref fetch.  Isolate that
            # uncommon case without returning to the normal one-fetch-per-PR path.
            for identity in batch:
                try:
                    fallback_fetches += 1
                    resolved[identity.key] = preflight._fetch_ref(
                        repo,
                        f"refs/pull/{identity.number}/head",
                        _head_ref(identity.number),
                        deadline,
                    )
                except preflight.PreflightError as error:
                    failed[identity.key] = error
    return HeadResolution(resolved, failed, batch_fetches, fallback_fetches)


def _analyze_identity(repo: Path, api: preflight.GitHubApi, main: str,
                      identity: PullIdentity, context: str) -> dict:
    del context
    # The sweep publishes a new authoritative result for every exact head, so a
    # combined-status lookup for the previous result is unnecessary remote work.
    # Retirement candidates still obtain the live status evidence required by
    # the trusted generated-state gate.
    report = preflight.analyze(repo, main, identity.head)
    if report["candidate_changes"]["generated_or_integration_owned_retirement_paths"]:
        report = preflight.analyze(
            repo, main, identity.head, None, api.retirement_status(identity.head)
        )
    return report


def _failure(error: preflight.PreflightError, stage: str,
             identity: PullIdentity | None = None, scope: str = "pull_request") -> dict:
    record = preflight._refresh_failure(
        error,
        scope,
        stage,
        identity.number if identity is not None else None,
        identity.head if identity is not None else None,
    )
    if identity is not None:
        record["identity"] = {"number": identity.number, "head": identity.head,
                              "base": identity.base}
    return record


def _write_failure(report_dir: Path, record: dict, suffix: str) -> None:
    preflight._write_report(record, report_dir / suffix, None)


def _current_entries(snapshot: Sequence[PullIdentity],
                     published: dict[tuple[int, str], dict]) -> tuple[list[dict], list[dict]]:
    current_keys = {identity.key for identity in snapshot}
    completed = [published[identity.key] for identity in snapshot if identity.key in published]
    superseded = [entry for key, entry in published.items() if key not in current_keys]
    return completed, superseded


def _active_failures(snapshot: Sequence[PullIdentity],
                     failures: dict[tuple[int, str], dict],
                     global_failures: Sequence[dict]) -> tuple[list[dict], list[dict]]:
    current_keys = {identity.key for identity in snapshot}
    active = list(global_failures)
    active.extend(failures[key] for key in current_keys if key in failures)
    superseded = [record for key, record in failures.items() if key not in current_keys]
    return active, superseded


def _finish(refresh: dict, snapshot: Sequence[PullIdentity],
            published: dict[tuple[int, str], dict],
            failures: dict[tuple[int, str], dict],
            global_failures: Sequence[dict], report_dir: Path,
            summary: Path | None, pending: Sequence[PullIdentity]) -> int:
    completed, superseded = _current_entries(snapshot, published)
    active_failures, superseded_failures = _active_failures(
        snapshot, failures, global_failures)
    pending_numbers = []
    seen_pending: set[int] = set()
    for identity in pending:
        if (identity.key not in published and identity.key not in failures and
                identity.number not in seen_pending):
            pending_numbers.append(identity.number)
            seen_pending.add(identity.number)
    refresh["listed_pull_requests"] = len(snapshot)
    refresh["completed"] = completed
    refresh["superseded"] = superseded
    refresh["failed"] = active_failures
    refresh["superseded_failures"] = superseded_failures
    refresh["not_attempted"] = pending_numbers
    refresh["blocking_count"] = sum(int(entry["blocking"]) for entry in completed)
    return preflight._finish_refresh(refresh, report_dir, summary)


def refresh_event(repo: Path, api: preflight.GitHubApi, event: dict,
                  report_dir: Path, summary: Path | None, context: str) -> int:
    default_branch = _default_branch(event)
    api.deadline = time.monotonic() + preflight.REFRESH_BUDGET_SECONDS
    refresh = {
        "schema": preflight.REFRESH_SCHEMA,
        "strategy": "pinned-main-batched-head-snapshot-v1",
        "default_branch": default_branch,
        "budget_seconds": preflight.REFRESH_BUDGET_SECONDS,
        "inventory_complete": False,
        "listed_pull_requests": None,
        "snapshot_passes": 0,
        "inventory_reads": 0,
        "main_fetches": 0,
        "head_batch_fetches": 0,
        "head_fallback_fetches": 0,
        "main": None,
        "completed": [],
        "superseded": [],
        "failed": [],
        "superseded_failures": [],
        "not_attempted": [],
        "blocking_count": 0,
    }
    snapshot: list[PullIdentity] = []
    published: dict[tuple[int, str], dict] = {}
    failures: dict[tuple[int, str], dict] = {}
    global_failures: list[dict] = []
    pending: list[PullIdentity] = []
    try:
        refresh["inventory_reads"] += 1
        snapshot = _inventory(api.open_pull_requests(default_branch), default_branch)
        refresh["inventory_complete"] = True
    except preflight.PreflightError as error:
        refresh["inventory_complete"] = False
        record = _failure(error, "list", scope="inventory")
        global_failures.append(record)
        _write_failure(report_dir, record, "inventory-error.json")
        return _finish(refresh, snapshot, published, failures, global_failures,
                       report_dir, summary, pending)
    try:
        refresh["main_fetches"] += 1
        main = _fetch_main(repo, default_branch, api.deadline)
        refresh["main"] = main
    except preflight.PreflightError as error:
        record = _failure(error, "resolve", scope="default_branch")
        global_failures.append(record)
        _write_failure(report_dir, record, "default-branch-error.json")
        pending = list(snapshot)
        return _finish(refresh, snapshot, published, failures, global_failures,
                       report_dir, summary, pending)
    pending = list(snapshot)
    for pass_number in range(1, MAX_SNAPSHOT_PASSES + 1):
        refresh["snapshot_passes"] = pass_number
        if _remaining(api.deadline) <= 0:
            error = preflight.RefreshBudgetError(
                "merge-conflict refresh time budget exhausted")
            record = _failure(error, "start-pass", scope="refresh")
            global_failures.append(record)
            _write_failure(report_dir, record, "refresh-budget-error.json")
            break
        resolution = _fetch_heads(repo, pending, api.deadline)
        refresh["head_batch_fetches"] += resolution.batch_fetches
        refresh["head_fallback_fetches"] += resolution.fallback_fetches
        unstable: set[tuple[int, str]] = set()
        stop_refresh = False
        for identity in pending:
            actual = resolution.resolved.get(identity.key)
            fetch_error = resolution.failed.get(identity.key)
            if fetch_error is not None:
                if isinstance(fetch_error, preflight.RefreshBudgetError):
                    record = _failure(fetch_error, "fetch", scope="refresh")
                    global_failures.append(record)
                    _write_failure(report_dir, record, "refresh-budget-error.json")
                    stop_refresh = True
                    break
                record = _failure(fetch_error, "fetch", identity)
                failures[identity.key] = record
                _write_failure(report_dir, record,
                               f"pr-{identity.number}-{identity.head}-error.json")
                continue
            if actual != identity.head:
                unstable.add(identity.key)
                continue
            stage = "analyze"
            try:
                report = _analyze_identity(repo, api, main, identity, context)
                preflight._write_report(
                    report,
                    report_dir / f"pr-{identity.number}-{identity.head}.json",
                    summary,
                    f"PR #{identity.number}",
                )
                stage = "publish"
                api.publish_status(identity.head, report, context, preflight._target_url())
                published[identity.key] = {
                    "pull_request": identity.number,
                    "head": identity.head,
                    "main": main,
                    "blocking": report["outcome"]["blocking"],
                }
            except preflight.PreflightError as error:
                record = _failure(error, stage, identity)
                failures[identity.key] = record
                _write_failure(report_dir, record,
                               f"pr-{identity.number}-{identity.head}-error.json")
                if isinstance(error, preflight.ApiRequestError) and error.systemic:
                    stop_refresh = True
                    break
            if _remaining(api.deadline) <= 0:
                error = preflight.RefreshBudgetError(
                    "merge-conflict refresh time budget exhausted")
                record = _failure(error, "analyze", scope="refresh")
                global_failures.append(record)
                _write_failure(report_dir, record, "refresh-budget-error.json")
                stop_refresh = True
                break
        if stop_refresh:
            pending = [
                identity for identity in pending
                if identity.key not in published and identity.key not in failures
            ]
            break
        try:
            refresh["main_fetches"] += 1
            current_main = _fetch_main(repo, default_branch, api.deadline)
        except preflight.PreflightError as error:
            record = _failure(error, "recheck", scope="default_branch")
            global_failures.append(record)
            _write_failure(report_dir, record, "default-branch-recheck-error.json")
            pending = [
                identity for identity in pending
                if identity.key not in published and identity.key not in failures
            ]
            break
        if current_main != main:
            error = preflight.PreflightError(
                f"default branch moved during snapshot refresh: {main} -> {current_main}"
            )
            record = _failure(error, "recheck", scope="default_branch")
            record["previous_main"] = main
            record["current_main"] = current_main
            global_failures.append(record)
            _write_failure(report_dir, record, "default-branch-moved.json")
            refresh["superseded_main_results"] = list(published.values())
            refresh["superseded_main_failures"] = list(failures.values())
            published.clear()
            failures.clear()
            pending = list(snapshot)
            break
        try:
            refresh["inventory_reads"] += 1
            current_snapshot = _inventory(
                api.open_pull_requests(default_branch), default_branch)
            refresh["inventory_complete"] = True
        except preflight.PreflightError as error:
            refresh["inventory_complete"] = False
            record = _failure(error, "recheck", scope="inventory")
            global_failures.append(record)
            _write_failure(report_dir, record, "inventory-recheck-error.json")
            pending = [
                identity for identity in pending
                if identity.key not in published and identity.key not in failures
            ]
            break
        snapshot = current_snapshot
        pending = [
            identity for identity in snapshot
            if identity.key not in published and identity.key not in failures
        ]
        if not pending:
            break
        if pass_number == MAX_SNAPSHOT_PASSES:
            for identity in pending:
                error = preflight.PreflightError(
                    f"pull request #{identity.number} did not stabilize in "
                    f"{MAX_SNAPSHOT_PASSES} inventory passes"
                )
                record = _failure(error, "stabilize", identity)
                failures[identity.key] = record
                _write_failure(report_dir, record,
                               f"pr-{identity.number}-{identity.head}-unstable.json")
            pending = []
        elif unstable:
            # The next inventory snapshot supplies the only identities eligible
            # for another attempt; never retry a stale listed head directly.
            pending = [
                identity for identity in pending
                if identity.key in unstable or identity.key not in resolution.resolved
            ]
    return _finish(refresh, snapshot, published, failures, global_failures,
                   report_dir, summary, pending)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--event-path", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--context", default=preflight.STATUS_CONTEXT)
    parser.add_argument(
        "--api-url",
        default=os.environ.get("GITHUB_API_URL", "https://api.github.com"),
    )
    return parser


def main() -> int:
    arguments = _parser().parse_args()
    status = 2
    try:
        event_name = os.environ.get("GITHUB_EVENT_NAME", "")
        if event_name not in ("push", "workflow_dispatch"):
            raise preflight.PreflightError(
                f"snapshot refresh does not support GitHub event {event_name!r}"
            )
        api = preflight.GitHubApi(
            arguments.repository,
            os.environ.get("GITHUB_TOKEN", ""),
            arguments.api_url,
        )
        status = refresh_event(
            arguments.repo,
            api,
            preflight._event(arguments.event_path),
            arguments.report_dir,
            arguments.summary,
            arguments.context,
        )
    except preflight.PreflightError as error:
        print(f"merge-conflict-preflight-refresh: error: {error}", file=sys.stderr)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
