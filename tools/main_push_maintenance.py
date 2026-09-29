#!/usr/bin/env python3
"""Keep default-branch maintenance green and converged across rapid main pushes.

GitHub concurrency can cancel both the running invocation and an older pending
invocation.  The workflows therefore key push runs by exact SHA and use this
module to arbitrate shared maintenance state.  A push that is already obsolete
is a successful no-op.  A run that observes main move after it started repeats
only the same fixed maintenance action against the newer exact revision.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import sys
import urllib.parse

import merge_conflict_preflight as preflight
import merge_conflict_preflight_refresh as refresh
import native_retirement_merge_gate as retirement


SCHEMA = "buster-main-push-maintenance-v1"
MAX_RECONCILE_ATTEMPTS = 4
HEX40 = re.compile(r"[0-9a-f]{40}\Z")


class MaintenanceError(Exception):
    """A main-push maintenance invocation cannot be classified safely."""


@dataclass(frozen=True)
class ActionResult:
    status: int
    main: str
    retryable_if_moved: bool
    payload: dict
    evidence_dir: Path | None = None
    summary_path: Path | None = None


@dataclass(frozen=True)
class ReconcileResult:
    status: int
    report: dict
    action: ActionResult | None


def _sha(value: object, label: str) -> str:
    if not isinstance(value, str) or HEX40.fullmatch(value) is None:
        raise MaintenanceError(f"{label} is not a canonical commit SHA: {value!r}")
    return value


def _main_head(api: preflight.GitHubApi, branch: str = "main") -> str:
    encoded = urllib.parse.quote(branch, safe="")
    value = api.request("git/ref/heads/" + encoded)
    object_value = value.get("object") if isinstance(value, dict) else None
    sha = object_value.get("sha") if isinstance(object_value, dict) else None
    return _sha(sha, "live default branch")


def _attempt_record(index: int, requested: str, result: ActionResult,
                    observed: str) -> dict:
    return {
        "attempt": index,
        "requested_main": requested,
        "action_main": result.main,
        "observed_main": observed,
        "action_status": result.status,
        "retryable_if_moved": result.retryable_if_moved,
    }


def reconcile(event_main: str, read_main, action,
              max_attempts: int = MAX_RECONCILE_ATTEMPTS) -> ReconcileResult:
    """Run one maintenance action until its published revision is still current."""
    event_main = _sha(event_main, "push event main")
    current = _sha(read_main(), "initial live main")
    attempts: list[dict] = []
    if current != event_main:
        report = {
            "schema": SCHEMA,
            "status": "superseded",
            "event_main": event_main,
            "current_main": current,
            "attempts": attempts,
        }
        return ReconcileResult(0, report, None)

    last: ActionResult | None = None
    status = 2
    for index in range(1, max_attempts + 1):
        requested = current
        last = action(requested, index)
        observed = _sha(read_main(), "post-action live main")
        attempts.append(_attempt_record(index, requested, last, observed))
        if last.status != 0:
            if last.retryable_if_moved and observed != last.main:
                current = observed
                continue
            status = last.status
            current = observed
            break
        if observed == last.main:
            status = 0
            current = observed
            break
        current = observed
    else:
        status = 2

    if status == 0:
        outcome = "current"
    elif len(attempts) >= max_attempts and attempts[-1]["observed_main"] != attempts[-1]["action_main"]:
        outcome = "unstable"
    else:
        outcome = "failed"
    report = {
        "schema": SCHEMA,
        "status": outcome,
        "event_main": event_main,
        "current_main": current,
        "attempts": attempts,
    }
    return ReconcileResult(status, report, last)


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


def _prepare_attempt(root: Path, index: int) -> tuple[Path, Path]:
    attempt = root / f"attempt-{index}"
    if attempt.exists():
        shutil.rmtree(attempt)
    attempt.mkdir(parents=True)
    return attempt, attempt / "summary.md"


def _promote_attempt(action: ActionResult | None, report_dir: Path,
                     summary: Path | None) -> None:
    if action is None or action.evidence_dir is None:
        return
    for path in action.evidence_dir.iterdir():
        if path.is_file() and path != action.summary_path:
            shutil.copy2(path, report_dir / path.name)
    if summary is not None and action.summary_path is not None and action.summary_path.exists():
        summary.parent.mkdir(parents=True, exist_ok=True)
        with summary.open("a", encoding="utf-8") as destination:
            destination.write(action.summary_path.read_text(encoding="utf-8"))


def _write_maintenance_report(report_dir: Path, report: dict,
                              summary: Path | None, action: str) -> None:
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / "main-push-maintenance.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    if summary is not None:
        summary.parent.mkdir(parents=True, exist_ok=True)
        with summary.open("a", encoding="utf-8") as output:
            output.write("\n### Main-push maintenance\n\n")
            output.write(f"- Action: `{action}`\n")
            output.write(f"- Event main: `{report['event_main']}`\n")
            output.write(f"- Current main: `{report['current_main']}`\n")
            output.write(f"- Result: **{report['status']}**\n")
            output.write(f"- Attempts: {len(report['attempts'])}\n")


def refresh_main_push(repo: Path, api: preflight.GitHubApi, event: dict,
                      event_main: str, report_dir: Path, summary: Path | None,
                      context: str) -> int:
    report_dir.mkdir(parents=True, exist_ok=True)

    def action(requested_main: str, index: int) -> ActionResult:
        attempt_dir, attempt_summary = _prepare_attempt(report_dir, index)
        status = refresh.refresh_event(
            repo, api, event, attempt_dir, attempt_summary, context
        )
        refresh_path = attempt_dir / "refresh.json"
        if not refresh_path.exists():
            raise MaintenanceError("merge-conflict refresh produced no refresh.json")
        payload = json.loads(refresh_path.read_text(encoding="utf-8"))
        action_main = payload.get("main")
        if not isinstance(action_main, str) or HEX40.fullmatch(action_main) is None:
            action_main = requested_main
        return ActionResult(
            status, action_main, status != 0 and _movement_only(payload), payload,
            attempt_dir, attempt_summary,
        )

    result = reconcile(event_main, lambda: _main_head(api), action)
    _promote_attempt(result.action, report_dir, summary)
    _write_maintenance_report(
        report_dir, result.report, summary, "merge-conflict-preflight-refresh"
    )
    return result.status


def invalidate_main_push(api: preflight.GitHubApi, event_main: str,
                         details_url: str) -> tuple[int, dict]:
    def action(main: str, _index: int) -> ActionResult:
        payload = retirement.invalidate_stale(api, main, details_url)
        return ActionResult(0, main, False, payload)

    result = reconcile(event_main, lambda: _main_head(api), action)
    if result.action is None:
        payload = {
            "schema": retirement.SCHEMA,
            "status": "superseded",
            "main": result.report["current_main"],
            "pull_requests": [],
        }
    else:
        payload = dict(result.action.payload)
        if result.status != 0:
            payload["status"] = "maintenance-" + result.report["status"]
    payload["maintenance"] = result.report
    return result.status, payload


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--api-url", default=os.environ.get("GITHUB_API_URL", "https://api.github.com")
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    refresh_parser = subparsers.add_parser("refresh-merge-conflicts")
    refresh_parser.add_argument("--repo", type=Path, default=Path("."))
    refresh_parser.add_argument("--event-path", type=Path, required=True)
    refresh_parser.add_argument("--repository", required=True)
    refresh_parser.add_argument("--event-main", required=True)
    refresh_parser.add_argument("--report-dir", type=Path, required=True)
    refresh_parser.add_argument("--summary", type=Path)
    refresh_parser.add_argument("--context", default=preflight.STATUS_CONTEXT)

    invalidate_parser = subparsers.add_parser("invalidate-native-retirement")
    invalidate_parser.add_argument("--repository", required=True)
    invalidate_parser.add_argument("--event-main", required=True)
    invalidate_parser.add_argument("--details-url", required=True)
    return parser


def main(argv=None) -> int:
    arguments = _parser().parse_args(argv)
    status = 2
    try:
        api = preflight.GitHubApi(
            arguments.repository,
            os.environ.get("GITHUB_TOKEN", ""),
            arguments.api_url,
        )
        if arguments.command == "refresh-merge-conflicts":
            event = preflight._event(arguments.event_path)
            status = refresh_main_push(
                arguments.repo, api, event, arguments.event_main,
                arguments.report_dir, arguments.summary, arguments.context,
            )
        else:
            status, report = invalidate_main_push(
                api, arguments.event_main, arguments.details_url
            )
            print(json.dumps(report, indent=2, sort_keys=True))
    except (MaintenanceError, preflight.PreflightError, retirement.AdmissionError,
            OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print("main-push maintenance failure: " + str(error), file=sys.stderr)
        status = 2
    return status


if __name__ == "__main__":
    raise SystemExit(main())
