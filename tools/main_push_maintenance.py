#!/usr/bin/env python3
"""Order and neutralize superseded push-to-main maintenance."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import sys
import time
import urllib.parse

import merge_conflict_preflight as preflight
import merge_conflict_preflight_refresh as refresh
import native_retirement_merge_gate as retirement


SCHEMA = "buster-main-push-maintenance-v1"
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
MAX_PAGES = 10
POLL_SECONDS = 2.0
SUCCESSOR_WAIT_SECONDS = 120.0


class MaintenanceError(Exception):
    """A push-maintenance result cannot be classified safely."""


class MainMoved(MaintenanceError):
    def __init__(self, expected: str, observed: str):
        super().__init__(f"main moved before publication: {expected} -> {observed}")
        self.observed = observed


@dataclass(frozen=True)
class Action:
    status: int
    main: str
    movement_only: bool
    payload: dict
    directory: Path | None = None
    summary: Path | None = None


class GuardedApi:
    """Delegate reads; prove the expected main before every mutation."""

    def __init__(self, api, expected: str):
        self.api = api
        self.repository = api.repository
        self.expected = _sha(expected, "guarded main")
        self.posts = []

    def all(self, path: str, **query):
        return self.api.all(path, **query)

    def request(self, path: str, *, method: str = "GET", body=None, **query):
        if method != "GET":
            observed = _main(self.api)
            if observed != self.expected:
                raise MainMoved(self.expected, observed)
        result = self.api.request(path, method=method, body=body, **query)
        if method != "GET":
            self.posts.append({"path": path, "method": method, "body": body})
        return result


def _sha(value, label: str) -> str:
    if not isinstance(value, str) or HEX40.fullmatch(value) is None:
        raise MaintenanceError(f"{label} is not a commit SHA: {value!r}")
    return value


def _positive(value, label: str) -> int:
    if type(value) is not int or value <= 0:
        raise MaintenanceError(f"{label} is not positive: {value!r}")
    return value


def _main(api) -> str:
    value = api.request("git/ref/heads/main")
    obj = value.get("object") if isinstance(value, dict) else None
    return _sha(obj.get("sha") if isinstance(obj, dict) else None, "live main")


def _pages(api, path: str, key: str, **query) -> list[dict]:
    rows = []
    for page in range(1, MAX_PAGES + 1):
        value = api.request(path, per_page=100, page=page, **query)
        batch = value.get(key) if isinstance(value, dict) else None
        if not isinstance(batch, list) or not all(isinstance(row, dict) for row in batch):
            raise MaintenanceError(f"{key} inventory is malformed")
        rows.extend(batch)
        if len(batch) < 100:
            return rows
    raise MaintenanceError(f"{key} pagination limit reached")


def _runs(api, workflow: str) -> list[dict]:
    if not workflow or "/" in workflow:
        raise MaintenanceError(f"workflow must be a file name: {workflow!r}")
    name = urllib.parse.quote(workflow, safe="")
    return _pages(
        api, f"actions/workflows/{name}/runs", "workflow_runs",
        event="push", branch="main",
    )


def _jobs(api, run_id: int) -> list[dict]:
    return _pages(
        api, f"actions/runs/{_positive(run_id, 'run id')}/jobs", "jobs",
        filter="all",
    )


def _identity(run: dict) -> tuple[int, int, str, str, str]:
    values = (
        _positive(run.get("id"), "run id"),
        _positive(run.get("run_number"), "run number"),
        run.get("event"),
        run.get("head_branch"),
        run.get("status"),
    )
    if not all(isinstance(value, str) for value in values[2:]):
        raise MaintenanceError("workflow run omits event, branch, or status")
    return values


def _job_pending(api, run: dict, target: str) -> bool:
    run_id, _number, _event, _branch, status = _identity(run)
    if status == "completed":
        return False
    matches = [job for job in _jobs(api, run_id) if job.get("name") == target]
    if not matches:
        return True
    for job in matches:
        status = job.get("status")
        if not isinstance(status, str):
            raise MaintenanceError("target job omits status")
        if status != "completed":
            return True
    return False


def _predecessors(api, workflow: str, number: int, target: str) -> list[dict]:
    number = _positive(number, "current run number")
    blocked = []
    for run in _runs(api, workflow):
        run_id, run_number, event, branch, _status = _identity(run)
        if event != "push" or branch != "main" or run_number >= number:
            continue
        if _job_pending(api, run, target):
            blocked.append({
                "id": run_id,
                "run_number": run_number,
                "head_sha": _sha(run.get("head_sha"), "predecessor head"),
            })
    return sorted(blocked, key=lambda row: (row["run_number"], row["id"]))


def wait_predecessors(api, workflow: str, number: int, target: str,
                      seconds: float, *, poll: float = POLL_SECONDS,
                      clear_reads: int = 2, sleep=time.sleep,
                      clock=time.monotonic) -> list[dict]:
    if seconds < 0 or poll < 0 or clear_reads <= 0:
        raise MaintenanceError("invalid predecessor wait policy")
    deadline = clock() + seconds
    observed = {}
    clear = 0
    while True:
        blocked = _predecessors(api, workflow, number, target)
        observed.update((row["id"], row) for row in blocked)
        clear = clear + 1 if not blocked else 0
        if clear >= clear_reads:
            return sorted(observed.values(), key=lambda row: (row["run_number"], row["id"]))
        remaining = deadline - clock()
        if remaining <= 0:
            ids = ", ".join(str(row["id"]) for row in blocked) or "unstable inventory"
            raise MaintenanceError("timed out waiting for predecessor runs: " + ids)
        sleep(min(poll, remaining))


def _successor(api, workflow: str, number: int, head: str) -> dict | None:
    matches = []
    for run in _runs(api, workflow):
        run_id, run_number, event, branch, status = _identity(run)
        if (event == "push" and branch == "main" and run_number > number and
                run.get("head_sha") == head):
            matches.append({
                "id": run_id, "run_number": run_number,
                "status": status, "head_sha": head,
            })
    return min(matches, key=lambda row: (row["run_number"], row["id"])) if matches else None


def wait_successor(api, workflow: str, number: int, head: str,
                   seconds: float, *, poll: float = POLL_SECONDS,
                   sleep=time.sleep, clock=time.monotonic) -> dict:
    deadline = clock() + seconds
    while True:
        result = _successor(api, workflow, _positive(number, "current run number"),
                            _sha(head, "successor head"))
        if result is not None:
            return result
        remaining = deadline - clock()
        if remaining <= 0:
            raise MaintenanceError("main advanced without a successor run for " + head)
        sleep(min(poll, remaining))


def _movement_only(report: dict) -> bool:
    failures = report.get("failed")
    if not isinstance(failures, list) or not failures:
        return False
    for failure in failures:
        error = failure.get("error") if isinstance(failure, dict) else None
        message = error.get("message") if isinstance(error, dict) else None
        if (failure.get("scope") != "default_branch" or
                failure.get("stage") != "recheck" or
                not isinstance(message, str) or
                "default branch moved during snapshot refresh" not in message):
            return False
    return True


def reconcile(event_main: str, read_main, action, successor) -> tuple[int, dict, Action | None]:
    event_main = _sha(event_main, "push event main")
    current = _sha(read_main(), "initial live main")
    if current != event_main:
        report = {
            "schema": SCHEMA, "status": "superseded-before-action",
            "event_main": event_main, "current_main": current,
            "attempts": [], "successor": successor(current),
        }
        return 0, report, None

    result = action(event_main)
    observed = _sha(read_main(), "post-action live main")
    next_run = successor(observed) if observed != event_main else None
    if result.status == 0:
        status = 0
        outcome = "current" if observed == event_main else "superseded-after-action"
    elif observed != result.main and result.movement_only:
        status, outcome = 0, "superseded-after-action"
    else:
        status, outcome = result.status, "failed"
    report = {
        "schema": SCHEMA, "status": outcome,
        "event_main": event_main, "current_main": observed,
        "attempts": [{
            "action_main": result.main,
            "observed_main": observed,
            "action_status": result.status,
            "movement_only": result.movement_only,
        }],
    }
    if next_run is not None:
        report["successor"] = next_run
    return status, report, result


def _ordered(api, workflow: str, number: int, target: str, seconds: float,
             event_main: str, action) -> tuple[int, dict, Action | None]:
    waited = wait_predecessors(api, workflow, number, target, seconds)
    successor = lambda head: wait_successor(
        api, workflow, number, head, min(seconds, SUCCESSOR_WAIT_SECONDS)
    )
    status, report, result = reconcile(event_main, lambda: _main(api), action, successor)
    report["ordering"] = {
        "workflow": workflow, "run_number": number, "target_job": target,
        "predecessors_waited": waited,
    }
    return status, report, result


def _attempt(root: Path) -> tuple[Path, Path]:
    directory = root / "attempt"
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    return directory, directory / "summary.md"


def _promote(result: Action | None, report: dict, root: Path,
             summary: Path | None) -> None:
    if (result is None or result.directory is None or
            report["status"] not in ("current", "failed")):
        return
    for path in result.directory.iterdir():
        if path.is_file() and path != result.summary:
            shutil.copy2(path, root / path.name)
    if summary is not None and result.summary is not None and result.summary.exists():
        summary.parent.mkdir(parents=True, exist_ok=True)
        with summary.open("a", encoding="utf-8") as output:
            output.write(result.summary.read_text(encoding="utf-8"))


def _record(root: Path, summary: Path | None, report: dict, action: str) -> None:
    root.mkdir(parents=True, exist_ok=True)
    (root / "main-push-maintenance.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    if summary is not None:
        with summary.open("a", encoding="utf-8") as output:
            output.write(
                "\n### Main-push maintenance\n\n"
                f"- Action: `{action}`\n"
                f"- Event main: `{report['event_main']}`\n"
                f"- Current main: `{report['current_main']}`\n"
                f"- Result: **{report['status']}**\n"
            )


def refresh_push(repo: Path, api, event: dict, event_main: str, root: Path,
                 summary: Path | None, context: str, workflow: str,
                 number: int, target: str, seconds: float) -> int:
    root.mkdir(parents=True, exist_ok=True)

    def action(requested: str) -> Action:
        directory, attempt_summary = _attempt(root)
        status = refresh.refresh_event(
            repo, api, event, directory, attempt_summary, context
        )
        path = directory / "refresh.json"
        if not path.exists():
            raise MaintenanceError("snapshot refresh produced no refresh.json")
        payload = json.loads(path.read_text(encoding="utf-8"))
        main = payload.get("main")
        if not isinstance(main, str) or HEX40.fullmatch(main) is None:
            main = requested
        return Action(status, main, status != 0 and _movement_only(payload),
                      payload, directory, attempt_summary)

    status, report, result = _ordered(
        api, workflow, number, target, seconds, event_main, action
    )
    _promote(result, report, root, summary)
    _record(root, summary, report, "merge-conflict-preflight-refresh")
    return status


def invalidate_push(api, event_main: str, details_url: str, workflow: str,
                    number: int, target: str, seconds: float) -> tuple[int, dict]:
    def action(main: str) -> Action:
        guarded = GuardedApi(api, main)
        try:
            payload = retirement.invalidate_stale(guarded, main, details_url)
            return Action(0, main, False, payload)
        except MainMoved as moved:
            return Action(2, main, True, {
                "schema": retirement.SCHEMA,
                "status": "superseded-during-invalidation",
                "main": main, "observed_main": moved.observed,
                "pull_requests": [], "partial_writes": guarded.posts,
            })

    status, report, result = _ordered(
        api, workflow, number, target, seconds, event_main, action
    )
    if result is None:
        payload = {
            "schema": retirement.SCHEMA, "status": report["status"],
            "main": report["current_main"], "pull_requests": [],
        }
    else:
        payload = dict(result.payload)
        if report["status"] != "current":
            payload["status"] = report["status"]
    payload["maintenance"] = report
    return status, payload


def _order_args(parser) -> None:
    parser.add_argument("--workflow", required=True)
    parser.add_argument("--run-number", type=int, required=True)
    parser.add_argument("--predecessor-job", required=True)
    parser.add_argument("--wait-seconds", type=float, required=True)


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--api-url", default=os.environ.get("GITHUB_API_URL", "https://api.github.com")
    )
    commands = parser.add_subparsers(dest="command", required=True)
    snapshot = commands.add_parser("refresh-merge-conflicts")
    snapshot.add_argument("--repo", type=Path, default=Path("."))
    snapshot.add_argument("--event-path", type=Path, required=True)
    snapshot.add_argument("--repository", required=True)
    snapshot.add_argument("--event-main", required=True)
    snapshot.add_argument("--report-dir", type=Path, required=True)
    snapshot.add_argument("--summary", type=Path)
    snapshot.add_argument("--context", default=preflight.STATUS_CONTEXT)
    _order_args(snapshot)
    invalidation = commands.add_parser("invalidate-native-retirement")
    invalidation.add_argument("--repository", required=True)
    invalidation.add_argument("--event-main", required=True)
    invalidation.add_argument("--details-url", required=True)
    _order_args(invalidation)
    return parser


def main(argv=None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        api = preflight.GitHubApi(
            arguments.repository, os.environ.get("GITHUB_TOKEN", ""),
            arguments.api_url,
        )
        if arguments.command == "refresh-merge-conflicts":
            status = refresh_push(
                arguments.repo, api, preflight._event(arguments.event_path),
                arguments.event_main, arguments.report_dir, arguments.summary,
                arguments.context, arguments.workflow, arguments.run_number,
                arguments.predecessor_job, arguments.wait_seconds,
            )
        else:
            status, report = invalidate_push(
                api, arguments.event_main, arguments.details_url,
                arguments.workflow, arguments.run_number,
                arguments.predecessor_job, arguments.wait_seconds,
            )
            print(json.dumps(report, indent=2, sort_keys=True))
        return status
    except (MaintenanceError, preflight.PreflightError, retirement.AdmissionError,
            OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print("main-push maintenance failure: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
