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
import subprocess
import sys
import time

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


def _native_inventory(api, mode: str, workflow: str, number: int,
                      selected: str, *, deadline: float | None = None, budget=None):
    """Transport-only compatibility adapter; native C owns query and verdict."""
    executable = Path(__file__).with_name("main_push_inventory")
    deadline = deadline if deadline is not None else time.monotonic() + 60
    evidence = {"phase": ("predecessor" if mode == "predecessors" else "successor") + "-discovery", "requests": 0, "pages": 0,
                "inventory_complete": False}
    budget = budget if budget is not None else {"requests": 0, "retained": [], "rejections": [], "calls": 0}
    budget.setdefault("started_at", time.monotonic())
    budget.setdefault("pages", 0)
    evidence["requests"] = budget["requests"]
    evidence["pages"] = budget["pages"]
    budget["calls"] += 1
    evidence["call"] = budget["calls"]
    child = None
    previous = getattr(api, "deadline", None)
    try:
        child = subprocess.Popen(
            [os.fspath(executable), mode, workflow, str(number), selected, str(budget["requests"])],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
        )
        child.stdin.write(json.dumps({"retained": budget["retained"] if mode == "predecessors" else []},
                                    separators=(",", ":")) + "\n")
        child.stdin.flush()
        while True:
            message = json.loads(child.stdout.readline())
            if "observation" in message:
                observation = dict(message["observation"], call=budget["calls"])
                budget["rejections"].append(observation)
                print("main-push discovery: " + json.dumps(observation, sort_keys=True), file=sys.stderr)
                delay = message["backoff_seconds"]
                if time.monotonic() + delay >= deadline and delay:
                    raise MaintenanceError("discovery deadline reached during native recovery")
                if delay:
                    time.sleep(delay)
                continue
            if "request" not in message:
                if time.monotonic() >= deadline:
                    raise MaintenanceError("discovery deadline reached")
                evidence.update(message)
                evidence["helper_pages"] = message["pages"]
                evidence["pages"] = budget["pages"]
                budget["inventory_complete"] = message.get("inventory_complete") is True
                evidence["rejections"] = list(budget["rejections"])
                evidence["calls"] = budget["calls"]
                evidence["elapsed_seconds"] = time.monotonic() - budget["started_at"]
                evidence["deadline_remaining_seconds"] = max(0, deadline - time.monotonic())
                budget["evidence"] = {key: value for key, value in evidence.items() if key not in ("result", "retained")}
                if mode == "predecessors":
                    budget["retained"] = message["retained"]
                status = child.wait(timeout=5)
                if status != 0 or message.get("inventory_complete") is not True:
                    raise MaintenanceError(message.get("error", "native discovery failed"))
                return message["result"]
            budget["requests"] += 1
            evidence["requests"] = budget["requests"]
            budget["pages"] += int("page" in message["query"])
            evidence["pages"] = budget["pages"]
            evidence["last_query"] = {"path": message["request"], **message["query"]}
            if time.monotonic() >= deadline:
                raise MaintenanceError("discovery deadline reached")
            if hasattr(api, "deadline"):
                api.deadline = min(previous, deadline) if previous is not None else deadline
            response = api.request(message["request"], **message["query"])
            if time.monotonic() >= deadline:
                raise MaintenanceError("discovery deadline reached after API read")
            child.stdin.write(json.dumps(response, separators=(",", ":")) + "\n")
            child.stdin.flush()
    except (MaintenanceError, OSError, ValueError, TypeError,
            subprocess.SubprocessError, preflight.PreflightError,
            retirement.AdmissionError) as error:
        failure = MaintenanceError(str(error) if isinstance(error, MaintenanceError)
                                   else "discovery transport/adapter failure: " + type(error).__name__)
        evidence["rejections"] = list(budget["rejections"])
        failure.discovery = evidence
        raise failure from None
    finally:
        if hasattr(api, "deadline"):
            api.deadline = previous
        if child is not None:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)
            child.stdin.close()
            child.stdout.close()


def _predecessors(api, workflow: str, number: int, target: str, *, deadline=None, budget=None) -> list[dict]:
    return _native_inventory(api, "predecessors", workflow,
                             _positive(number, "current run number"), target, deadline=deadline, budget=budget)


def wait_predecessors(api, workflow: str, number: int, target: str,
                      seconds: float, *, poll: float = POLL_SECONDS,
                      clear_reads: int = 2, sleep=time.sleep,
                      clock=time.monotonic, budget=None) -> list[dict]:
    if seconds < 0 or poll < 0 or clear_reads <= 0:
        raise MaintenanceError("invalid predecessor wait policy")
    deadline = clock() + seconds
    budget = budget if budget is not None else {"requests": 0, "retained": [], "rejections": [], "calls": 0}
    observed = {}
    clear = 0
    while True:
        blocked = _predecessors(api, workflow, number, target,
                                deadline=deadline if seconds > 0 else None, budget=budget)
        observed.update((row["id"], row) for row in blocked)
        clear = clear + 1 if not blocked else 0
        if clear >= clear_reads:
            return sorted(observed.values(), key=lambda row: (row["run_number"], row["id"]))
        remaining = deadline - clock()
        if remaining <= 0:
            ids = ", ".join(str(row["id"]) for row in blocked) or "unstable inventory"
            raise MaintenanceError("timed out waiting for predecessor runs: " + ids)
        sleep(min(poll, remaining))


def _successor(api, workflow: str, number: int, head: str, *, deadline=None, budget=None) -> dict | None:
    return _native_inventory(api, "successor", workflow,
                             _positive(number, "current run number"),
                             _sha(head, "successor head"), deadline=deadline, budget=budget)


def wait_successor(api, workflow: str, number: int, head: str,
                   seconds: float, *, poll: float = POLL_SECONDS,
                   sleep=time.sleep, clock=time.monotonic, budget=None) -> dict:
    deadline = clock() + seconds
    budget = budget if budget is not None else {"requests": 0, "retained": [], "rejections": [], "calls": 0}
    while True:
        result = _successor(api, workflow, _positive(number, "current run number"),
                            _sha(head, "successor head"),
                            deadline=deadline if seconds > 0 else None, budget=budget)
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
    phase = "predecessor-discovery"
    attempted = False
    waited = []
    budget = {"requests": 0, "retained": [], "rejections": [], "calls": 0}

    def invoke(head):
        nonlocal phase, attempted
        phase, attempted = "action", True
        return action(head)

    def successor(head):
        nonlocal phase
        phase = "successor-discovery"
        return wait_successor(api, workflow, number, head,
                              min(seconds, SUCCESSOR_WAIT_SECONDS), budget=budget)

    try:
        waited = wait_predecessors(api, workflow, number, target, seconds, budget=budget)
        phase = "main-reconciliation"
        status, report, result = reconcile(event_main, lambda: _main(api), invoke, successor)
    except (MaintenanceError, preflight.PreflightError, retirement.AdmissionError,
            OSError, TypeError, ValueError) as error:
        status, result = 2, None
        report = {
            "schema": SCHEMA, "status": "failed", "event_main": event_main,
            "current_main": None, "attempts": [], "failing_phase": phase,
            "action_attempted": attempted,
            "error": {"type": type(error).__name__,
                      "message": str(error) if isinstance(error, MaintenanceError)
                      else "maintenance API/action failure"},
        }
        if hasattr(error, "discovery"):
            report["discovery"] = error.discovery
    report.setdefault("discovery", budget.get("evidence", {
        "requests": budget["requests"], "calls": budget["calls"],
        "rejections": budget["rejections"], "inventory_complete": budget.get("inventory_complete", False)}))
    report["run_id"] = os.environ.get("GITHUB_RUN_ID")
    report["run_attempt"] = os.environ.get("GITHUB_RUN_ATTEMPT")
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
    report["action"] = action
    root.mkdir(parents=True, exist_ok=True)
    (root / "main-push-maintenance.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    discovery = report.get("discovery", {})
    details = json.dumps({"error": report.get("error"), "discovery": {
        key: discovery.get(key) for key in ("inventory_complete", "requests", "pages", "calls", "call",
                                            "attempts", "request_limit", "attempt_limit", "recovery_stop", "elapsed_seconds",
                                            "deadline_remaining_seconds", "helper_pages", "last_query", "rejections")
    }}, sort_keys=True)
    print("main-push maintenance: " + details, file=sys.stderr)
    if summary is not None:
        summary.parent.mkdir(parents=True, exist_ok=True)
        with summary.open("a", encoding="utf-8") as output:
            output.write(
                "\n### Main-push maintenance\n\n"
                f"- Action: `{action}`\n"
                f"- Event main: `{report['event_main']}`\n"
                f"- Current main: `{report['current_main']}`\n"
                f"- Result: **{report['status']}**\n"
                f"- Failing phase: `{report.get('failing_phase', 'none')}`\n"
                f"\n```json\n{details}\n```\n"
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
    report["action"] = "native-retirement-invalidation"
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
    invalidation.add_argument("--report-dir", type=Path, default=Path("evidence/native-retirement-invalidation"))
    invalidation.add_argument("--summary", type=Path, default=os.environ.get("GITHUB_STEP_SUMMARY"))
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
            _record(arguments.report_dir, arguments.summary, report["maintenance"],
                    "native-retirement-invalidation")
            print(json.dumps(report, indent=2, sort_keys=True))
        return status
    except (MaintenanceError, preflight.PreflightError, retirement.AdmissionError,
            OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        report = {
            "schema": SCHEMA, "status": "failed", "event_main": arguments.event_main,
            "current_main": None, "attempts": [], "action_attempted": None,
            "failing_phase": "initialization-or-reporting",
            "run_id": os.environ.get("GITHUB_RUN_ID"),
            "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT"),
            "ordering": {"workflow": arguments.workflow, "run_number": arguments.run_number},
            "error": {"type": type(error).__name__, "message": "maintenance initialization/reporting failed"},
        }
        try:
            _record(arguments.report_dir, arguments.summary, report, arguments.command)
        except OSError:
            print("main-push maintenance failure: could not retain failure report", file=sys.stderr)
        print("main-push maintenance failure: " + type(error).__name__, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
