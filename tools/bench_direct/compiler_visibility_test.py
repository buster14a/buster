#!/usr/bin/env python3
"""Offline checks of the compiler benchmark's check lifecycle and commit report (#2803, #2804).

An in-memory GitHub (FakeGitHub) serves check runs, commit comments, commit
listings and job metadata through the real Api.pages, with injected lost
responses, so ownership, ordering, retries and reconciliation are exercised
without the network.
"""

from __future__ import annotations

import base64
import copy
import io
import json
import itertools
import os
import sys
import tempfile
import unittest
import urllib.error
import urllib.parse
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compiler_comment  # noqa: E402
import compiler_github  # noqa: E402
import compiler_publish  # noqa: E402
import compiler_receipt  # noqa: E402
from compiler_test import BINARIES, EXPECTED, archive, corpus, machine_bytes, receipt, summary  # noqa: E402

REPO = "buster14a/buster"
HEAD, TRUSTED, PARENT, OLDER = "a" * 40, "9" * 40, "b" * 40, "c" * 40
BOT = {"login": "github-actions[bot]", "id": 41898282, "type": "Bot"}
HUMAN = {"login": "davidgmbb", "id": 39247043, "type": "User"}
# compiler_publish.main dispatches on sys.argv[1:]; the default publish command
# takes no arguments, so a focused test run's own arguments must not leak in (#3680).
PUBLISH_ARGV = [compiler_publish.__file__]


def corpus_members() -> dict:
    """The throughput corpus documents a complete evidence artifact carries (#2761)."""
    documents = corpus()
    return {"throughput/summary.json": json.dumps(documents["summary"]),
            "throughput/metadata.json": json.dumps(documents["metadata"])}


class ArtifactDownloadBoundsTest(unittest.TestCase):
    def download(self, payload, **limits):
        redirected = urllib.error.HTTPError("https://api.github.com/archive", 302, "redirect",
                                            {"Location": "https://storage.invalid/archive"}, None)
        opener = mock.Mock()
        opener.open.side_effect = redirected
        response = io.BytesIO(payload)
        with mock.patch.object(compiler_github.urllib.request, "build_opener", return_value=opener), \
                mock.patch.object(compiler_github.urllib.request, "urlopen", return_value=response) as storage:
            result = compiler_github.Api(REPO, "test-token").download("https://api.github.com/archive", **limits)
            self.assertNotIn("Authorization", storage.call_args.args[0].headers)
        return result

    def test_bounded_preparation_download_reuses_tokenless_redirect(self):
        self.assertEqual(self.download(b"1234", max_bytes=4), b"1234")
        with self.assertRaises(OSError):
            self.download(b"12345", max_bytes=4)

    def test_invalid_new_bound_is_rejected_before_request(self):
        with mock.patch.object(compiler_github.urllib.request, "build_opener") as request:
            for limit in (True, 0, -1, (2 << 30) + 1, "4"):
                with self.subTest(limit=limit), self.assertRaises(ValueError):
                    compiler_github.Api(REPO, "test-token").download("https://api.github.com/archive", max_bytes=limit)
            request.assert_not_called()


class FakeGitHub(compiler_github.Api):
    def __init__(self):
        super().__init__(REPO, "token")
        self.checks: list[dict] = []
        self.comments: list[dict] = []
        self.commits: list[dict] = []
        self.pull_commits: list[str] = []
        self.jobs: list[list[dict]] = []
        self.lose: set[str] = set()
        self.writes: list[tuple[str, str]] = []
        self.next_id = 1000

    def new_id(self) -> int:
        self.next_id += 1
        return self.next_id

    def add_check(self, head: str, external_id: str, status: str = "queued", app: int = 15368,
                  name: str = "9700X compiler benchmark") -> dict:
        row = {"id": self.new_id(), "name": name, "head_sha": head, "external_id": external_id, "status": status,
               "conclusion": None, "app": {"id": app}, "html_url": f"https://github.com/{REPO}/runs/{self.next_id}"}
        self.checks.append(row)
        return row

    def add_comment(self, head: str, body: str, user: dict = BOT) -> dict:
        row = {"id": self.new_id(), "commit_id": head, "body": body, "user": dict(user), "path": None,
               "position": None, "html_url": f"https://github.com/{REPO}/commit/{head}#r{self.next_id}"}
        self.comments.append(row)
        return row

    def request(self, path: str, data: dict | None = None, method: str = "") -> object:
        method = method or ("GET" if data is None else "POST")
        parsed = urllib.parse.urlsplit(path)
        query = dict(urllib.parse.parse_qsl(parsed.query))
        parts = parsed.path.strip("/").split("/")
        page, per = int(query.get("page", 1)), int(query.get("per_page", 100))
        window = slice((page - 1) * per, page * per)
        result: object = None
        if method != "GET":
            self.writes.append((method, parsed.path))
        if method == "GET" and parts[0] == "commits" and parts[-1] == "check-runs":
            rows = [copy.deepcopy(row) for row in self.checks if row["head_sha"] == parts[1] and
                    row["name"] == query.get("check_name", row["name"]) and
                    str(row["app"]["id"]) == query.get("app_id", str(row["app"]["id"]))]
            result = {"total_count": len(rows), "check_runs": rows[window]}
        elif parts == ["check-runs"] and method == "POST":
            row = self.add_check(data["head_sha"], data["external_id"], data.get("status", "queued"),
                                 name=data["name"])
            row.update({key: value for key, value in data.items() if key not in ("head_sha", "name")})
            result = copy.deepcopy(row)
        elif parts[0] == "check-runs" and method == "PATCH":
            row = next(row for row in self.checks if row["id"] == int(parts[1]))
            row.update(data)
            result = copy.deepcopy(row)
        elif parts == ["commits"]:
            result = copy.deepcopy(self.commits)
        elif parts[0] == "pulls":
            result = [{"sha": sha} for sha in self.pull_commits]
        elif parts[0] == "actions" and parts[-1] == "jobs":
            result = {"jobs": self.jobs.pop(0) if len(self.jobs) > 1 else self.jobs[0]}
        elif parts[0] == "commits" and parts[-1] == "comments" and method == "GET":
            result = [copy.deepcopy(row) for row in self.comments if row["commit_id"] == parts[1]][window]
        elif parts[0] == "commits" and parts[-1] == "comments" and method == "POST":
            result = copy.deepcopy(self.add_comment(parts[1], data["body"]))
        elif parts[0] == "comments" and method == "PATCH":
            row = next(row for row in self.comments if row["id"] == int(parts[1]))
            row["body"] = data["body"]
            result = copy.deepcopy(row)
        elif parts[0] == "comments" and method == "DELETE":
            self.comments = [row for row in self.comments if row["id"] != int(parts[1])]
        else:
            raise AssertionError(f"unexpected {method} {path}")
        if method + " " + parts[0] in self.lose:
            self.lose.discard(method + " " + parts[0])
            raise urllib.error.URLError("response lost after the write")
        return result


def marker(head: str = HEAD, mode: str = "main", request: str = "91", request_attempt: str = "1",
           attempt: str = "1") -> str:
    return compiler_receipt.attempt_marker(head, mode, request, request_attempt, attempt)


def environment(mode: str = "main", attempt: str = "1") -> dict:
    return {"BQ_MODE": mode, "BQ_REPOSITORY": REPO, "BQ_HEAD_COMMIT": HEAD, "BQ_BASE_COMMIT": PARENT,
            "BQ_PULL": "7", "BQ_REQUEST_RUN_ID": "91", "BQ_REQUEST_ATTEMPT": "1", "BQ_RUN_ID": "92",
            "BQ_RUN_ATTEMPT": attempt, "BQ_TRUSTED_REVISION": TRUSTED}


def compare_job(status: str, conclusion: str | None = None, mode: str = "main") -> list[dict]:
    job = {"name": compiler_github.COMPARE_JOBS[mode], "status": status, "conclusion": conclusion,
           "html_url": f"https://github.com/{REPO}/actions/runs/92/job/5", "runner_name": "buster-zen5-9700x"}
    if status != "queued":
        job["started_at"] = "2026-10-06T15:33:00Z"
    return [job]


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class CheckLifecycleTest(unittest.TestCase):
    def test_markers_separate_modes_attempts_and_reject_non_decimal(self) -> None:
        main, pull = marker(), marker(mode="pull")
        self.assertNotEqual(main, pull)
        self.assertTrue(main.startswith(compiler_receipt.check_marker(HEAD, "main") + ":"))
        self.assertNotEqual(marker(attempt="2"), main)
        self.assertNotEqual(marker(request_attempt="2"), main)
        with self.assertRaises(ValueError):
            compiler_receipt.attempt_marker(HEAD, "main", "91", "0", "1")

    def test_announce_queues_on_the_measured_head_and_retries_find_it(self) -> None:
        api = FakeGitHub()
        api.lose.add("POST check-runs")
        env = {"BQ_HEAD_COMMIT": HEAD, "BQ_REQUEST_RUN_ID": "91", "BQ_REQUEST_ATTEMPT": "1"}
        rows = compiler_github.announce(api, env)
        self.assertEqual([(row["head_sha"], row["status"], row["external_id"]) for row in api.checks],
                         [(HEAD, "queued", marker())])
        # Repeated delivery adopts the same check.
        self.assertEqual([row["id"] for row in compiler_github.announce(api, env)], [rows[0]["id"]])
        self.assertEqual(len(api.checks), 1)
        self.assertIn(compiler_github.NATIVE_LIFECYCLE, api.checks[0]["output"]["summary"].splitlines())

    def test_start_adopts_once_without_reading_physical_scheduling(self) -> None:
        api = FakeGitHub()
        bridge = api.add_check(HEAD, marker())
        api.jobs = [compare_job("queued"), compare_job("in_progress")]
        rows = compiler_github.start(api, environment())
        self.assertEqual([row["id"] for row in rows], [bridge["id"]])
        self.assertEqual(bridge["status"], "queued")
        self.assertIn("Native Actions job state", bridge["output"]["summary"])
        self.assertIn("does not claim measurement", bridge["output"]["summary"])
        self.assertIn(compiler_github.NATIVE_LIFECYCLE, bridge["output"]["summary"].splitlines())
        self.assertEqual(len(api.jobs), 2)
        self.assertFalse([row for row in api.checks if row["head_sha"] == TRUSTED])

    def test_wait_longer_than_twenty_minutes_needs_no_controller(self) -> None:
        api = FakeGitHub()
        api.jobs = [compare_job("queued")]
        rows = compiler_github.start(api, environment())
        self.assertEqual(rows[0]["status"], "queued")
        self.assertEqual(len(api.jobs), 1)
        done = {"status": "completed", "conclusion": "success", "output": {"title": "Measured", "summary": ""}}
        compiler_github.complete_check(api, HEAD, "main", marker(), done)
        compiler_github.start(api, environment())
        self.assertEqual(api.checks[0]["status"], "completed")
        self.assertEqual(api.checks[0]["output"]["title"], "Measured")

    def test_terminal_before_setup_is_final_for_both_modes(self) -> None:
        for mode, conclusion in (("main", "success"), ("pull", "failure"), ("pull", "cancelled")):
            with self.subTest(mode=mode, conclusion=conclusion):
                api = FakeGitHub()
                terminal = {"status": "completed", "conclusion": conclusion,
                            "output": {"title": "First terminal result", "summary": ""}}
                compiler_github.complete_check(api, HEAD, mode, marker(mode=mode), terminal)
                compiler_github.start(api, environment(mode=mode))
                self.assertEqual(len(api.checks), 1)
                self.assertEqual(api.checks[0]["conclusion"], conclusion)
                self.assertEqual(api.checks[0]["output"]["title"], "First terminal result")

    def test_foreign_and_other_attempt_checks_are_never_adopted(self) -> None:
        api = FakeGitHub()
        foreign_app = api.add_check(HEAD, marker(), app=99)
        copied = api.add_check(HEAD, marker() + "x")
        other_mode = api.add_check(HEAD, marker(), name="9700X compiler benchmark (pull request)")
        earlier = api.add_check(HEAD, marker(attempt="1"), status="completed")
        api.jobs = [compare_job("in_progress")]
        rows = compiler_github.start(api, environment(attempt="2"))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["external_id"], marker(attempt="2"))
        for row in (foreign_app, copied, other_mode):
            self.assertEqual(row["status"], "queued")
        self.assertEqual(earlier["status"], "completed")

    def test_completion_is_final_and_status_never_moves_backwards(self) -> None:
        api = FakeGitHub()
        row = api.add_check(HEAD, marker(), status="in_progress")
        compiler_github.advance(api, [row], {"status": "queued", "output": {"title": "late", "summary": ""}})
        self.assertEqual(row["status"], "in_progress")
        done = {"status": "completed", "conclusion": "success", "output": {"title": "first", "summary": ""}}
        self.assertTrue(compiler_github.complete_check(api, HEAD, "main", marker(), done)[1])
        late = dict(done, conclusion="failure", output={"title": "late", "summary": ""})
        rows, written = compiler_github.complete_check(api, HEAD, "main", marker(), late)
        self.assertFalse(written)
        self.assertEqual((row["conclusion"], row["output"]["title"]), ("success", "first"))

    def test_late_older_attempt_cannot_touch_the_newer_attempt(self) -> None:
        api = FakeGitHub()
        newer = api.add_check(HEAD, marker(attempt="2"), status="completed")
        newer["conclusion"] = "success"
        compiler_github.complete_check(api, HEAD, "main", marker(attempt="1"),
                                       {"status": "completed", "conclusion": "failure",
                                        "output": {"title": "old", "summary": ""}})
        self.assertEqual(newer["conclusion"], "success")
        self.assertEqual(len(api.checks), 2)

    def test_publisher_creates_a_check_when_no_start_ran(self) -> None:
        api = FakeGitHub()
        rows, written = compiler_github.complete_check(
            api, HEAD, "main", marker(), {"status": "completed", "conclusion": "failure",
                                          "output": {"title": "Not benchmarked", "summary": ""}})
        self.assertTrue(written)
        self.assertEqual((rows[0]["status"], rows[0]["conclusion"]), ("completed", "failure"))

    def test_serialized_orphan_publisher_and_setup_keep_first_terminal_result(self) -> None:
        for delivery in itertools.permutations(("setup", "publisher", "orphan")):
            with self.subTest(delivery=delivery):
                api = FakeGitHub()
                row = api.add_check(HEAD, marker())
                first_terminal = None
                for writer in delivery:
                    if writer == "setup":
                        compiler_github.start(api, environment())
                    elif writer == "publisher":
                        compiler_github.complete_check(api, HEAD, "main", marker(),
                                                       {"status": "completed", "conclusion": "success",
                                                        "output": {"title": "Measured", "summary": "valid"}})
                    else:
                        compiler_github.reconcile_main(api, "d" * 40, HEAD, "newer attempt",
                                                       "2026-10-09T09:00:00Z", [HEAD])
                    if row["status"] == "completed":
                        terminal = (row["conclusion"], row["output"]["title"])
                        if first_terminal is None:
                            first_terminal = terminal
                        self.assertEqual(terminal, first_terminal)
                self.assertIsNotNone(first_terminal)

    def test_main_reconciliation_closes_displaced_and_unpublished_attempts(self) -> None:
        api = FakeGitHub()
        api.commits = [{"sha": HEAD, "parents": [{"sha": PARENT}, {"sha": "d" * 40}]},
                       {"sha": "d" * 40, "parents": [{"sha": OLDER}]},
                       {"sha": PARENT, "parents": [{"sha": OLDER}]}, {"sha": OLDER, "parents": [{"sha": "e" * 40}]}]
        displaced = api.add_check(PARENT, marker(PARENT, request="80"))
        unpublished = api.add_check(OLDER, marker(OLDER, request="70"), status="in_progress")
        measured = api.add_check(OLDER, marker(OLDER, request="60"), status="completed")
        measured["conclusion"] = "success"
        pull_side = api.add_check("d" * 40, marker("d" * 40, request="75"))
        current = api.add_check(HEAD, marker())
        closed = compiler_github.reconcile_main(api, HEAD, PARENT, "run", "2026-10-06T16:00:00Z")
        self.assertEqual(sorted(closed), sorted([displaced["id"], unpublished["id"]]))
        self.assertEqual((displaced["conclusion"], displaced["output"]["title"]), ("neutral", "Not measured"))
        self.assertIn("displaced", displaced["output"]["summary"])
        self.assertIn("execution metadata is unavailable", displaced["output"]["summary"])
        self.assertNotIn("never started", displaced["output"]["summary"])
        self.assertNotIn("range comparison", displaced["output"]["summary"])
        self.assertEqual(unpublished["conclusion"], "neutral")
        self.assertEqual(measured["conclusion"], "success")
        self.assertEqual((current["status"], pull_side["status"]), ("queued", "queued"))

    def test_legacy_generic_and_executor_urls_remain_bounded_neutral_cleanup(self) -> None:
        for details in (f"https://github.com/{REPO}/actions/runs/81/attempts/1",
                        f"https://github.com/{REPO}/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run"):
            for status in ("queued", "in_progress"):
                with self.subTest(details=details, status=status):
                    api = FakeGitHub()
                    old = api.add_check(PARENT, marker(PARENT, request="80"), status=status)
                    old["details_url"] = details
                    old["output"] = {"summary": f"Baseline `{OLDER}` (first parent).\nLegacy run."}
                    closed = compiler_github.reconcile_main(api, HEAD, PARENT, "run", "now", [PARENT])
                    self.assertEqual(closed, [old["id"]])
                    self.assertEqual((old["status"], old["conclusion"]), ("completed", "neutral"))
                    self.assertIn("execution metadata is unavailable", old["output"]["summary"])
                    self.assertIn(f"Baseline `{OLDER}`", old["output"]["summary"])
                    self.assertNotIn("never started", old["output"]["summary"])
                    self.assertNotIn("its 9700X job started", old["output"]["summary"])

    def test_main_reconciliation_defers_tagged_attempts_to_native_terminal_recovery(self) -> None:
        # The custom check can be queued after compare physically started or
        # was cancelled. Neither a next request nor the check state proves a skip.
        for details in (f"https://github.com/{REPO}/actions/runs/81/attempts/1",
                        f"https://github.com/{REPO}/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run"):
            for physical in ("in_progress", "cancelled"):
                with self.subTest(details=details, physical=physical):
                    api = FakeGitHub()
                    old_marker = marker(PARENT, request="80")
                    old = api.add_check(PARENT, old_marker)
                    old["details_url"] = details
                    old["output"] = {"summary": compiler_github.NATIVE_LIFECYCLE}
                    api.jobs = [compare_job("completed" if physical == "cancelled" else physical,
                                            "cancelled" if physical == "cancelled" else None)]
                    closed = compiler_github.reconcile_main(api, HEAD, PARENT, "run", "now", [PARENT])
                    self.assertEqual(closed, [])
                    self.assertEqual((old["status"], old["conclusion"]), ("queued", None))
                    self.assertFalse(api.writes)
                    self.assertEqual(len(api.jobs), 1)  # The backstop does not inspect or poll jobs.
                    # Native completion closes this exact attempt, even without a successor;
                    # subsequent range reconciliation cannot overwrite the terminal result.
                    compiler_github.complete_check(api, PARENT, "main", old_marker,
                        {"status": "completed", "conclusion": "cancelled",
                         "output": {"title": "Native terminal recovery", "summary": ""}})
                    self.assertEqual(compiler_github.reconcile_main(api, HEAD, PARENT, "run", "later", [PARENT]), [])
                    self.assertEqual((old["conclusion"], old["output"]["title"]),
                                     ("cancelled", "Native terminal recovery"))

    def test_main_reconciliation_names_the_range_that_covers_an_unmeasured_commit(self) -> None:
        api = FakeGitHub()
        api.commits = [{"sha": HEAD, "parents": [{"sha": PARENT}]}, {"sha": PARENT, "parents": [{"sha": OLDER}]},
                       {"sha": OLDER, "parents": [{"sha": "e" * 40}]}]
        displaced = api.add_check(PARENT, marker(PARENT, request="80"))
        measured = api.add_check(OLDER, marker(OLDER, request="60"), status="completed")
        measured["conclusion"] = "success"
        chain = compiler_github.first_parent_chain(api, HEAD)
        self.assertEqual(chain, [PARENT, OLDER, "e" * 40])
        self.assertIn("range of 2 first-parent main commits", compiler_github.baseline_label(chain, OLDER))
        self.assertEqual(compiler_github.baseline_label(chain, PARENT), "first parent")
        self.assertEqual(compiler_github.baseline_label(chain, HEAD), "")
        closed = compiler_github.reconcile_main(api, HEAD, OLDER, "run", "2026-10-06T16:00:00Z", chain)
        self.assertEqual(closed, [displaced["id"]])
        self.assertEqual(displaced["conclusion"], "neutral")
        self.assertIn(f"inside the range comparison of `{HEAD}` against `{OLDER}`", displaced["output"]["summary"])

    def test_unchanged_range_reconciliation_never_promises_a_comparison(self) -> None:
        # #3923: a skipped range has no comparison to cover the displaced commit.
        api = FakeGitHub()
        api.commits = [{"sha": HEAD, "parents": [{"sha": PARENT}]}, {"sha": PARENT, "parents": [{"sha": OLDER}]},
                       {"sha": OLDER, "parents": [{"sha": "e" * 40}]}]
        displaced = api.add_check(PARENT, marker(PARENT, request="80"))
        chain = compiler_github.first_parent_chain(api, HEAD)
        closed = compiler_github.reconcile_main(api, HEAD, OLDER, "run", "2026-10-06T16:00:00Z", chain, True)
        self.assertEqual(closed, [displaced["id"]])
        summary = displaced["output"]["summary"]
        self.assertEqual(displaced["conclusion"], "neutral")
        self.assertIn(f"from `{OLDER}`, the last actual measurement, to `{HEAD}`", summary)
        self.assertIn("is not measured", summary)
        self.assertNotIn("range comparison", summary)

    def test_pull_reconciliation_supersedes_only_earlier_heads_of_that_pull_request(self) -> None:
        api = FakeGitHub()
        api.pull_commits = [OLDER, PARENT, HEAD]
        old = api.add_check(OLDER, marker(OLDER, "pull"), name="9700X compiler benchmark (pull request)")
        main_check = api.add_check(PARENT, marker(PARENT))
        compiler_github.reconcile_pull(api, "7", HEAD, "run", "2026-10-06T16:00:00Z")
        self.assertEqual((old["status"], old["conclusion"]), ("completed", "neutral"))
        self.assertEqual(main_check["status"], "queued")

    def test_cli_never_fails_the_request_or_measurement(self) -> None:
        with mock.patch.dict(os.environ, {"BQ_REPOSITORY": REPO}, clear=True), \
                mock.patch.object(sys, "argv", ["compiler_github.py", "announce"]), \
                mock.patch("sys.stdout", new_callable=io.StringIO) as output:
            self.assertEqual(compiler_github.main(), 0)
        self.assertIn("::warning::", output.getvalue())


def entry(attempt: str = "1", run: str = "92", conclusion: str = "success", text: str = "report") -> dict:
    return {"mode": "main", "head": HEAD, "base": PARENT, "run_id": run, "run_attempt": attempt,
            "request_run_id": "91", "trusted_revision": TRUSTED, "conclusion": conclusion,
            "title": f"Measured attempt {attempt}", "captured_at": "2026-10-06T15:46:00Z",
            "check_url": f"https://github.com/{REPO}/runs/1", "run_url": f"https://github.com/{REPO}/actions/runs/{run}",
            "artifact_url": "", "artifact_expires_at": "NA", "markdown": f"### {text} {run}.{attempt}"}


PUBLICATION = {"run_id": "92", "run_attempt": "1", "revision": TRUSTED, "at": "t1", "url": "u"}


class CommentTest(unittest.TestCase):
    def owned(self, api: FakeGitHub) -> list[dict]:
        return [row for row in api.comments if compiler_comment.owned(row, HEAD, "main")]

    def test_entry_validation_rejects_wrong_mode_subject_and_links(self) -> None:
        self.assertEqual(compiler_comment.validate_entry(entry(), REPO), [])
        self.assertEqual(compiler_comment.validate_entry(dict(entry(), base=""), REPO), [])
        for change in ({"mode": "pull"}, {"head": "main"}, {"run_id": "0"}, {"conclusion": "faster"},
                       {"check_url": "https://example.com/x"}, {"run_url": f"https://github.com/{REPO}/a b"},
                       {"markdown": ""}, {"markdown": "x" * 50000}):
            with self.subTest(change=change):
                self.assertTrue(compiler_comment.validate_entry(dict(entry(), **change), REPO))
        self.assertTrue(compiler_comment.validate_entry(None, REPO))

    def test_creates_one_owned_comment_and_repeated_delivery_updates_it(self) -> None:
        api = FakeGitHub()
        compiler_comment.upsert(api, entry(), PUBLICATION)
        compiler_comment.upsert(api, entry(), dict(PUBLICATION, at="t2"))
        rows = self.owned(api)
        self.assertEqual(len(rows), 1)
        state, markdown = compiler_comment.parse(rows[0]["body"], HEAD, "main")
        self.assertEqual(markdown, "### report 92.1")
        self.assertEqual([item["at"] for item in state["attempts"][0]["publications"]], ["t1", "t2"])

    def test_lost_create_response_does_not_duplicate(self) -> None:
        api = FakeGitHub()
        api.lose.add("POST commits")
        compiler_comment.upsert(api, entry(), PUBLICATION)
        self.assertEqual(len(api.comments), 1)

    def test_copied_markers_and_human_comments_are_never_edited(self) -> None:
        api = FakeGitHub()
        body = compiler_comment.render(HEAD, "main", [], "forged")
        human = api.add_comment(HEAD, body, HUMAN)
        other_head = api.add_comment(HEAD, compiler_comment.render(OLDER, "main", [], "other"))
        plain = api.add_comment(HEAD, "looks good")
        compiler_comment.upsert(api, entry(), PUBLICATION)
        self.assertEqual(human["body"], body)
        self.assertIn("other", other_head["body"])
        self.assertEqual(plain["body"], "looks good")
        self.assertEqual(len(self.owned(api)), 1)

    def test_pagination_finds_the_owned_comment_past_the_first_page(self) -> None:
        api = FakeGitHub()
        for index in range(150):
            api.add_comment(HEAD, f"note {index}", HUMAN)
        compiler_comment.upsert(api, entry(), PUBLICATION)
        compiler_comment.upsert(api, entry(attempt="2"), PUBLICATION)
        self.assertEqual(len(self.owned(api)), 1)
        self.assertEqual(len(api.comments), 151)

    def test_older_attempt_never_replaces_the_newer_report(self) -> None:
        api = FakeGitHub()
        compiler_comment.upsert(api, entry(attempt="2", text="newer"), PUBLICATION)
        compiler_comment.upsert(api, entry(attempt="1", conclusion="failure", text="older"), PUBLICATION)
        state, markdown = compiler_comment.parse(self.owned(api)[0]["body"], HEAD, "main")
        self.assertEqual(markdown, "### newer 92.2")
        self.assertEqual([(row["run_attempt"], row["conclusion"]) for row in state["attempts"]],
                         [("1", "failure"), ("2", "success")])
        self.assertIn("| 92 | 1 | failure |", self.owned(api)[0]["body"])

    def test_concurrent_duplicates_merge_into_the_oldest(self) -> None:
        api = FakeGitHub()
        first = api.add_comment(HEAD, compiler_comment.render(HEAD, "main", [
            dict({key: entry(run="90")[key] for key in compiler_comment.FIELDS}, publications=[])], "### a 90.1"))
        api.add_comment(HEAD, compiler_comment.render(HEAD, "main", [
            dict({key: entry(run="91")[key] for key in compiler_comment.FIELDS}, publications=[])], "### b 91.1"))
        compiler_comment.upsert(api, entry(run="89", text="stale"), PUBLICATION)
        rows = self.owned(api)
        self.assertEqual([row["id"] for row in rows], [first["id"]])
        state, markdown = compiler_comment.parse(rows[0]["body"], HEAD, "main")
        self.assertEqual(markdown, "### b 91.1")
        self.assertEqual([row["run_id"] for row in state["attempts"]], ["89", "90", "91"])

    def test_publication_failure_is_explicit(self) -> None:
        class Broken(FakeGitHub):
            def request(self, path, data=None, method=""):
                raise urllib.error.HTTPError(path, 403, "Resource not accessible by integration", None, None)
        with tempfile.TemporaryDirectory() as directory:
            step = Path(directory) / "summary.md"
            values = {"BQ_REPOSITORY": REPO, "BQ_COMMENT": json.dumps(entry()), "BQ_PUBLISHER_RUN_ID": "95",
                      "BQ_PUBLISHER_ATTEMPT": "1", "BQ_TRUSTED_REVISION": TRUSTED, "GH_TOKEN": "t",
                      "GITHUB_STEP_SUMMARY": str(step)}
            with mock.patch.dict(os.environ, values, clear=True), \
                    mock.patch.object(compiler_comment, "Api", lambda repository, token: Broken()), \
                    mock.patch("sys.stderr", new_callable=io.StringIO):
                self.assertEqual(compiler_comment.main(), 1)
            self.assertIn("was NOT published", step.read_text())


class ReportTest(unittest.TestCase):
    def shown(self) -> dict:
        data = receipt()
        data["timings"].update(started_at="2026-10-06T15:32:57Z", finished_at="2026-10-06T15:46:00Z")
        return dict(data, reasons=[])

    def test_valid_report_states_scope_units_uncertainty_and_policy(self) -> None:
        text = compiler_publish.commit_report(self.shown(), summary(), "success", "Measured", [], "LINKS")
        for marker in ("report-only", "Candidate is SLOWER.", "1.0200, 95% CI [1.0100, 1.0300], slower",
                       "1 s / 1.02 s", "| Complete pairs | 12 |", "AMD Ryzen 7 9700X", "compiler-compare-v1",
                       "not included in compiler-compare-v1", "2026-10-06T15:46:00Z", "LINKS", "<details>"):
            self.assertIn(marker, text)

    def test_failed_or_missing_evidence_never_reads_as_a_measurement(self) -> None:
        text = compiler_publish.commit_report({"mode": "main", "identity": dict(EXPECTED),
                                               "reasons": ["no readable receipt.json"]},
                                              summary(), "failure", "Not benchmarked", [], "LINKS")
        self.assertIn("No valid measurement", text)
        self.assertIn("| Wall time B/A | NA, 95% CI [NA, NA], NA |", text)
        self.assertIn("- no readable receipt.json", text)
        self.assertIn("| Achieved uncertainty | NA (not measured) |", text)
        self.assertIn("| Host cost per leg | builds NA + NA s, self-host measurement NA s, corpus NA s, "
                      "total NA s; queue delay NA s |", text)
        self.assertNotIn("unresolved at the achieved resolution", text)

    def test_report_names_route_profile_uncertainty_and_leg_costs(self) -> None:
        # #3212 items 6-7: the selected profile, its trusted route, the achieved
        # interval and every leg's recorded seconds are visible on the commit page.
        shown = self.shown()
        shown["timings"].update(build_seconds={"baseline": 61.0, "candidate": 62.0}, measurement_seconds=600.0,
                                throughput_seconds=23.0, total_seconds=822.0, queue_delay_seconds=5.0)
        route = {"main_owned": False, "main_profile": "compiler-compare-v1", "main_policy_revision": "b" * 40,
                 "main_certificate_revision": "-"}
        text = compiler_publish.commit_report(dict(shown, main_route=route), summary(), "success", "Measured", [], "LINKS")
        for marker in ("| Achieved uncertainty | ±1.00% (half-width of the 95% CI of wall B/A) |",
                       "| Selected profile | `compiler-compare-v1` (Main routing disabled at policy `bbbbbbbbbbbb`) |",
                       "| Host cost per leg | builds 61 + 62 s, self-host measurement 600 s, corpus 23 s, total 822 s; "
                       "queue delay 5 s |"):
            self.assertIn(marker, text)
        route = dict(route, main_owned=True, main_profile="compiler-main-40pairs-v1", main_certificate_revision="c" * 40)
        text = compiler_publish.commit_report(dict(shown, main_route=route), summary(), "success", "Measured", [], "LINKS")
        self.assertIn("| Selected profile | `compiler-main-40pairs-v1` (authenticated automatic Main route, policy "
                      "`bbbbbbbbbbbb`, certificate `cccccccccccc`) |", text)
        text = compiler_publish.commit_report(dict(shown, mode="pull"), summary(), "success", "Measured", [], "LINKS")
        self.assertIn("| Selected profile | `compiler-compare-v1` (explicit pull-request request) |", text)
        text = compiler_publish.commit_report(shown, summary(), "success", "Measured", [], "LINKS")
        self.assertIn("| Selected profile | `compiler-compare-v1` (Main route not resolved) |", text)

    def test_unresolved_outcomes_never_read_as_equivalence(self) -> None:
        for outcome in ("no detectable difference", "below-floor", "inconclusive"):
            data = summary()
            data["verdict"] = dict(data["verdict"], outcome=outcome, ci_low=0.99, ci_high=1.01)
            text = compiler_publish.commit_report(self.shown(), data, "success", "Measured", [], "LINKS")
            with self.subTest(outcome=outcome):
                self.assertIn("neither equivalence nor a performance pass", text)
        text = compiler_publish.commit_report(self.shown(), summary(), "success", "Measured", [], "LINKS")
        self.assertNotIn("neither equivalence nor a performance pass", text)

    def test_links_name_check_attempt_and_expiring_evidence(self) -> None:
        line = compiler_publish.links("https://c", "https://w", "2", "https://e", "2027-01-04T00:00:00Z")
        self.assertIn("[Workflow run, attempt 2](https://w)", line)
        self.assertIn("retained until 2027-01-04T00:00:00Z", line)
        self.assertIn("Evidence artifact: unavailable", compiler_publish.links("", "https://w", "1", "", ""))


class RecoveryApi(FakeGitHub):
    """A past main attempt: its run, jobs, artifact and the commit records authorization reads."""

    def __init__(self, compare: str = "success", artifacts: int = 1, run_path: str = compiler_github.BENCH_WORKFLOW):
        super().__init__()
        self.compare, self.artifacts, self.run_path = compare, artifacts, run_path
        # The original run's bench workflow; this default predates machine-record retention.
        self.workflow = "jobs:\n  compare:\n"
        # The original run's producer; this default predates sealing (#2918).
        self.producer = "def export_tree():\n"
        self.payload = archive({"receipt.json": json.dumps(self.recorded()), "lab/summary.json": json.dumps(summary()), **corpus_members()})
        self.add_check(HEAD, compiler_receipt.check_marker(HEAD), status="completed")

    @staticmethod
    def recorded() -> dict:
        data = receipt()
        data["identity"].update(trusted_revision=TRUSTED, run_id="37486885378", run_attempt="1", pull="0",
                                pull_head=HEAD)
        return data

    def request(self, path: str, data: dict | None = None, method: str = "") -> object:
        bare = urllib.parse.urlsplit(path).path
        tree = lambda sha, tree_sha: {"sha": sha, "commit": {"tree": {"sha": tree_sha}}}  # noqa: E731
        answers = {
            "/actions/runs/37486885378": {"id": 37486885378, "path": self.run_path, "event": "workflow_run",
                                          "run_attempt": 1, "head_sha": TRUSTED, "head_branch": "main",
                                          "repository": {"full_name": REPO}, "head_repository": {"full_name": REPO},
                                          "display_title": f"9700X request 91.1 head {HEAD}"},
            "/actions/runs/37486885378/attempts/1": {"id": 37486885378, "path": self.run_path, "event": "workflow_run",
                                          "run_attempt": 1, "head_sha": TRUSTED, "head_branch": "main",
                                          "repository": {"full_name": REPO}, "head_repository": {"full_name": REPO},
                                          "display_title": f"9700X request 91.1 head {HEAD}"},
            "/actions/runs/37486885378/attempts/1/jobs": {"jobs": [
                {"name": "Authorize the main commit comparison", "conclusion": "success", "run_attempt": 1},
                {"name": "Compare the main commit compiler", "conclusion": self.compare, "run_attempt": 1,
                 "created_at": "2026-10-06T15:32:00Z", "started_at": "2026-10-06T15:32:10Z"}]},
            "/actions/runs/37486885378/artifacts": {"total_count": self.artifacts, "artifacts": [
                {"id": 7, "name": f"buster-9700x-compiler-{HEAD}-1", "expired": False, "size_in_bytes": 100,
                 "archive_download_url": "https://api.invalid/zip", "expires_at": "2027-01-04T00:00:00Z"}
            ] * self.artifacts},
            "/actions/runs/91": {"id": 91, "run_attempt": 1, "path": ".github/workflows/9700x-compiler-request.yml", "event": "push",
                                 "head_branch": "main", "status": "completed", "conclusion": "success",
                                 "head_sha": HEAD, "repository": {"full_name": REPO},
                                 "head_repository": {"full_name": REPO}},
            f"/commits/{HEAD}": dict(tree(HEAD, "d" * 40), parents=[{"sha": "b" * 40}]),
            f"/commits/{'b' * 40}": tree("b" * 40, "e" * 40),
            f"/compare/{HEAD}...main": {"status": "ahead"},
            f"/commits/{HEAD}/pulls": [],
        }
        if bare == "/actions/runs/91/attempts/1":
            return copy.deepcopy(answers["/actions/runs/91"])
        if bare == f"/compare/{TRUSTED}...main":
            return {"status": "ahead"}
        if bare == "/contents/" + compiler_github.BENCH_WORKFLOW:
            if urllib.parse.parse_qs(urllib.parse.urlsplit(path).query) != {"ref": [TRUSTED]}:
                raise urllib.error.HTTPError(path, 404, "not the original run revision", {}, None)
            raw = self.workflow.encode()
            return {"type": "file", "encoding": "base64", "size": len(raw), "content": base64.b64encode(raw).decode()}
        if bare == "/contents/" + compiler_publish.SEALING_PRODUCER:
            if urllib.parse.parse_qs(urllib.parse.urlsplit(path).query) != {"ref": [TRUSTED]}:
                raise urllib.error.HTTPError(path, 404, "not the original run revision", {}, None)
            raw = self.producer.encode()
            return {"type": "file", "encoding": "base64", "size": len(raw), "content": base64.b64encode(raw).decode()}
        if bare in ("/contents/docs/compiler-main-profile-routing-v1.tsv",
                    "/contents/tools/compiler_main_profile_policy.c"):
            raise urllib.error.HTTPError(path, 404, "historical policy absence", {}, None)
        return answers[bare] if bare in answers else super().request(path, data, method)

    def download(self, url: str) -> bytes:
        return self.payload


class RecoveryTest(unittest.TestCase):
    def run_recovery(self, api: RecoveryApi) -> tuple[int, dict]:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            values = {"BQ_REPOSITORY": REPO, "BQ_RECOVER_RUN_ID": "37486885378", "BQ_RECOVER_ATTEMPT": "1",
                      "GH_TOKEN": "t", "GITHUB_OUTPUT": str(output), "GITHUB_STEP_SUMMARY": os.devnull}
            with mock.patch.dict(os.environ, values, clear=True), \
                    mock.patch.object(sys, "argv", PUBLISH_ARGV), \
                    mock.patch.object(compiler_publish, "Api", lambda repository, token: api), \
                    mock.patch("sys.stdout", new_callable=io.StringIO), \
                    mock.patch("sys.stderr", new_callable=io.StringIO):
                code = compiler_publish.main()
            text = output.read_text() if output.exists() else ""
        values = dict(line.split("=", 1) for line in text.splitlines())
        return code, json.loads(values["comment"]) if "comment" in values else {}

    def test_backfill_republishes_retained_evidence_without_writing_checks(self) -> None:
        api = RecoveryApi()
        code, published = self.run_recovery(api)
        self.assertEqual(code, 0)
        self.assertEqual((published["conclusion"], published["head"], published["trusted_revision"]),
                         ("success", HEAD, TRUSTED))
        self.assertEqual((published["run_id"], published["run_attempt"]), ("37486885378", "1"))
        self.assertTrue(published["check_url"].startswith(f"https://github.com/{REPO}/runs/"))
        self.assertTrue(published["artifact_url"].endswith("/actions/runs/37486885378/artifacts/7"))
        self.assertEqual(compiler_comment.validate_entry(published, REPO), [])
        self.assertEqual(api.writes, [])

    def test_unverifiable_or_missing_evidence_is_refused(self) -> None:
        for api in (RecoveryApi(artifacts=0), RecoveryApi(run_path=".github/workflows/other.yml")):
            with self.subTest(path=api.run_path, artifacts=api.artifacts):
                code, published = self.run_recovery(api)
                self.assertEqual((code, published), (1, {}))

    def test_recovered_range_keeps_its_baseline_only_on_the_first_parent_chain(self) -> None:
        api = RecoveryApi()
        api.commits = [{"sha": HEAD, "parents": [{"sha": "b" * 40}]}, {"sha": "b" * 40, "parents": [{"sha": OLDER}]},
                       {"sha": OLDER, "parents": [{"sha": "e" * 40}]}]
        recorded = RecoveryApi.recorded()
        recorded["identity"].update(base=OLDER, base_tree="1" * 40)
        recorded["coverage"] = {"first_parent": "b" * 40, "range": "2"}
        api.payload = archive({"receipt.json": json.dumps(recorded), "lab/summary.json": json.dumps(summary()), **corpus_members()})
        original = api.request
        routed = lambda path, data=None, method="": {"sha": OLDER, "commit": {"tree": {"sha": "1" * 40}}} \
            if path == f"/commits/{OLDER}" else original(path, data, method)  # noqa: E731
        with mock.patch.object(api, "request", routed):
            code, published = self.run_recovery(api)
        self.assertEqual((code, published["conclusion"], published["base"]), (0, "success", OLDER))
        self.assertIn("range of 2 first-parent main commits", published["markdown"])
        # The same receipt without the chain cannot prove its baseline.
        api.commits = []
        with mock.patch.object(api, "request", routed):
            code, published = self.run_recovery(api)
        self.assertEqual((code, published), (1, {}))

    def test_recovery_waives_the_machine_record_only_for_a_legacy_workflow_revision(self) -> None:
        modern = "jobs:\n  compare:\n    steps:\n      - name: Retain the machine-specification record\n"
        record = machine_bytes(run_id={"value": "37486885378", "status": "available", "reason": ""})
        for label, workflow, machine, conclusion in (
                ("legacy revision without a record", "jobs:\n  compare:\n", None, "success"),
                ("modern revision without a record", modern, None, "failure"),
                ("modern revision with its record", modern, record, "success"),
                ("legacy revision with a stale record", "jobs:\n  compare:\n", machine_bytes(), "failure")):
            with self.subTest(label=label):
                api = RecoveryApi()
                api.workflow = workflow
                if machine is not None:
                    api.payload = archive({"receipt.json": json.dumps(api.recorded()), "lab/summary.json": json.dumps(summary()),
                                           **corpus_members(), compiler_publish.MACHINE_RECORD: machine})
                code, published = self.run_recovery(api)
                self.assertEqual((code, published["conclusion"]), (0, conclusion))
                if conclusion == "failure":
                    self.assertIn("machine-specification record", published["markdown"])
                self.assertEqual(api.writes, [])

    def sealed_recovery(self, *, claim: bool = True, controls: bool = True, unsafe: bool = False, tamper: bool = False,
                        title: str = f"9700X request 91.1 head {HEAD}", producer_seals: bool = True) -> tuple[int, dict]:
        """Recovery of a sealed v1 attempt (#2918) through a stand-in native verify and replay.

        The stand-in verifies only under request attempt 1 and only an untampered bundle; replay
        has its own tests. producer_seals says whether the original run's producer sealed.
        """
        recorded = RecoveryApi.recorded()
        if claim:
            recorded["evidence_bundle"] = {"inventory": compiler_receipt.EVIDENCE_INVENTORY, "state": "sealed",
                                           "manifest_sha256": "a" * 64, "manifest_bytes": 9, "members": 3,
                                           "payload_bytes": 4, "omissions": 0}
        members = {"receipt.json": json.dumps(recorded), "lab/summary.json": json.dumps(summary()), **corpus_members()}
        if controls:
            members.update({compiler_receipt.EVIDENCE_MANIFEST: "manifest\n", compiler_receipt.EVIDENCE_SEAL: "seal\n"})
        if tamper:
            members["lab/tampered.txt"] = "x"
        api = RecoveryApi()
        api.payload = archive(members)
        if producer_seals:
            api.producer = compiler_publish.SEALING_PRODUCER_MARKER.decode() + "receipt, evidence):\n"
        if unsafe:
            stream = io.BytesIO(api.payload)
            with zipfile.ZipFile(stream, "a") as output:
                info = zipfile.ZipInfo("lab/run.sh")
                info.external_attr = 0o100755 << 16
                output.writestr(info, "#!/bin/sh\n")
            api.payload = stream.getvalue()
        original = api.request

        def titled(path: str, data: dict | None = None, method: str = "") -> object:
            answer = original(path, data, method)
            if urllib.parse.urlsplit(path).path.startswith("/actions/runs/37486885378") and isinstance(answer, dict) and \
                    "display_title" in answer:
                answer = dict(answer, display_title=title)
            return answer

        script = ("import sys, os\n"
                  "identity = open(sys.argv[3]).read()\n"
                  "ok = 'request_attempt\\t1\\n' in identity and not os.path.exists(os.path.join(sys.argv[2], 'lab', 'tampered.txt'))\n"
                  "print('COMPILER_EVIDENCE_VERIFIED members=3 payload_bytes=4 manifest_sha256=' + 'a' * 64 if ok else\n"
                  "      'COMPILER_EVIDENCE_REFUSED reason=retained bytes, roles or identity do not reproduce the sealed manifest')\n"
                  "raise SystemExit(0 if ok else 1)\n")
        with mock.patch.object(api, "request", titled), \
                mock.patch.object(compiler_publish, "EVIDENCE_VERIFY_COMMAND", (sys.executable, "-c", script)), \
                mock.patch.object(compiler_publish, "sealed_lab_replay", lambda files: 12), \
                mock.patch.object(compiler_publish, "sealed_corpus_replay", lambda files, temporary: 12):
            return self.run_recovery(api)

    def test_recovery_verifies_sealed_evidence_under_the_original_request_attempt(self) -> None:
        code, published = self.sealed_recovery()
        self.assertEqual((code, published["conclusion"]), (0, "success"), published.get("markdown"))
        for label, change, expected in (
                ("altered bytes", {"tamper": True}, "sealed raw evidence refused: native verify exited 1"),
                ("missing controls", {"controls": False}, "is required on this route, but the artifact carries neither"),
                ("missing controls of a legacy producer", {"controls": False, "producer_seals": False},
                 "receipt names sealed raw evidence that the artifact does not carry"),
                ("unsafe archive", {"unsafe": True}, "sealed raw evidence refused: sealed evidence archive refused"),
                ("another request run", {"title": f"9700X request 92.1 head {HEAD}"},
                 "the authorized identity cannot name a sealed bundle"),
                ("no run-name", {"title": "9700X direct workload benchmark"},
                 "the authorized identity cannot name a sealed bundle")):
            with self.subTest(label=label):
                code, published = self.sealed_recovery(**change)
                self.assertEqual((code, published["conclusion"]), (0, "failure"))
                self.assertIn(expected, published["markdown"])

    def test_recovery_of_an_unsealed_attempt_is_legacy_only_before_sealing(self) -> None:
        code, published = self.sealed_recovery(claim=False, controls=False, producer_seals=False)
        self.assertEqual((code, published["conclusion"]), (0, "success"))
        self.assertIn("legacy-unsealed", published["markdown"])
        code, published = self.sealed_recovery(claim=False, controls=False)
        self.assertEqual((code, published["conclusion"]), (0, "failure"))
        self.assertIn("is required on this route", published["markdown"])

    def test_recovered_failure_keeps_its_meaning(self) -> None:
        code, published = self.run_recovery(RecoveryApi(compare="failure"))
        self.assertEqual((code, published["conclusion"]), (0, "failure"))
        self.assertIn("No valid measurement", published["markdown"])


# A natively verified and replayed sealed bundle (#2918), as verify_evidence_bundle reports it.
VERIFIED = {"state": "verified", "inventory": compiler_receipt.EVIDENCE_INVENTORY, "members": 3, "payload_bytes": 4, "manifest_sha256": "a" * 64,
            "replay": {"lab_pairs": 12, "corpus_cases": 12}}


class PublishTest(unittest.TestCase):
    def publish(self, record: bytes | None, document: dict | None = None,
                mode: str = "main", admission: str | None = "admitted",
                sealed: bool = True) -> tuple[RecoveryApi, dict, dict]:
        """(api, started check, job outputs) after one publication of a retained receipt (default: valid main).

        Pull mode stubs only the request-file freshness gate, which has its own tests. A sealed
        publication stubs native verify and replay (#2918), which have their own tests, and its
        receipt names that verified manifest.
        """
        api = RecoveryApi()
        api.checks = []
        started = api.add_check(HEAD, marker(request="91", attempt="1", mode=mode), status="in_progress",
                                name=compiler_receipt.check_name(mode))
        machine = {} if record is None else {compiler_publish.MACHINE_RECORD: record}
        document = copy.deepcopy(receipt() if document is None else document)
        if sealed:
            document.setdefault("evidence_bundle", {"inventory": compiler_receipt.EVIDENCE_INVENTORY, "state": "sealed",
                                                    **{key: VERIFIED[key] for key in ("manifest_sha256", "members",
                                                                                     "payload_bytes")}})
        api.payload = archive({"receipt.json": json.dumps(document),
                               "lab/summary.json": json.dumps(summary()), **corpus_members(), **machine})
        listing = {"artifacts": [{"id": 8, "name": f"buster-9700x-compiler-{HEAD}-1", "expired": False,
                                  "size_in_bytes": 100, "archive_download_url": "https://api.invalid/zip",
                                  "expires_at": "2027-01-04T00:00:00Z"}]}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            values = {"BQ_MODE": "main", "BQ_REPOSITORY": REPO, "BQ_REF": "refs/heads/main", "BQ_PULL": "7",
                      "BQ_PULL_HEAD": "c" * 40, "BQ_BASE_COMMIT": "b" * 40, "BQ_BASE_TREE": "e" * 40,
                      "BQ_HEAD_COMMIT": HEAD, "BQ_HEAD_TREE": "d" * 40, "BQ_TRUSTED_REVISION": "9" * 40,
                      "BQ_REQUEST_RUN_ID": "91", "BQ_REQUEST_ATTEMPT": "1", "BQ_RUN_ID": "92",
                      "BQ_RUN_ATTEMPT": "1", "BQ_AUTHORIZE_RESULT": "success", "BQ_AUTHORIZED_ATTEMPT": "1",
                      "BQ_COMPARE_RESULT": "success", "BQ_FIRST_PARENT": "b" * 40, "BQ_RANGE": "1",
                      "GH_TOKEN": "t", "GITHUB_OUTPUT": str(output), "GITHUB_STEP_SUMMARY": os.devnull}
            if mode == "pull":
                values.update(BQ_MODE="pull", BQ_REF="refs/pull/7/head")
                for key in ("BQ_FIRST_PARENT", "BQ_RANGE"):
                    values.pop(key)
                if admission is not None:
                    values["BQ_HARNESS_ADMISSION"] = admission
            original = api.request
            def routed(path, data=None, method=""):
                bare = urllib.parse.urlsplit(path).path
                if bare == "/actions/runs/92/attempts/1":
                    return {"id": 92, "run_attempt": 1, "path": compiler_github.BENCH_WORKFLOW,
                            "event": "workflow_run", "head_branch": "main", "head_sha": TRUSTED,
                            "repository": {"full_name": REPO}, "head_repository": {"full_name": REPO},
                            "display_title": f"9700X request 91.1 head {HEAD}"}
                if "/runs/92/artifacts" in path:
                    return listing
                if bare == "/actions/runs/92/attempts/1/jobs":
                    return {"jobs": []}
                return original(path, data, method)
            with mock.patch.dict(os.environ, values, clear=True), \
                    mock.patch.object(sys, "argv", PUBLISH_ARGV), \
                    mock.patch.object(api, "request", routed), \
                    mock.patch.object(compiler_publish, "Api", lambda repository, token: api), \
                    mock.patch.object(compiler_publish, "verify_analyzer_request_freshness",
                                      lambda *unused: "" if mode == "pull" else "not a pull request"), \
                    mock.patch.object(compiler_publish, "verify_evidence_bundle",
                                      (lambda *unused: dict(VERIFIED)) if sealed else compiler_publish.verify_evidence_bundle), \
                    mock.patch("sys.stdout", new_callable=io.StringIO):
                self.assertEqual(compiler_publish.main(), 0)
            written = dict(line.split("=", 1) for line in output.read_text().splitlines()) if output.exists() else {}
        return api, started, written

    def test_malformed_scaling_status_still_completes_the_check(self) -> None:
        # #3656 review: an unhashable scaling status must fail the check, not leave it in progress.
        for status in ("failed", {}, []):
            with self.subTest(status=status):
                document = receipt()
                document["mode"] = "pull"
                document["identity"].update(mode="pull", ref="refs/pull/7/head")
                document["scaling_profile"] = copy.deepcopy(compiler_receipt.SCALING_PROFILE)
                document["scaling"] = {name: {"status": status} for name in compiler_receipt.SCALING_PROFILE["series"]}
                pull_record = machine_bytes(job={"value": "compare-pull", "status": "available", "reason": ""})
                api, started, unused_written = self.publish(pull_record, document, "pull")
                self.assertEqual([row for row in api.checks if row["external_id"] == started["external_id"]], [started])
                self.assertEqual((started["status"], started["conclusion"]), ("completed", "failure"))
                shown = json.loads(started["output"]["text"].removeprefix("```json\n").removesuffix("\n```"))
                self.assertEqual(shown["inventory_coverage"]["discharged"], [])
                self.assertEqual(shown["inventory_coverage"]["route"], "pull-compare")
                legs = {entry["leg"]: entry["status"] for entry in shown["inventory_coverage"]["legs"]}
                self.assertEqual(legs["scaling"], "not discharged: leg failed")

    def test_pull_harness_admission_gates_publication(self) -> None:
        # #3714: an unsupported base was never dispatched and publishes its explicit refusal; a missing or
        # unreviewed disposition never reads evidence; only an admitted one validates the receipt.
        document = receipt()
        document["mode"] = "pull"
        document["identity"].update(mode="pull", ref="refs/pull/7/head")
        pull_record = machine_bytes(job={"value": "compare-pull", "status": "available", "reason": ""})
        for admission, title, reason in (
                ("unsupported", "Not benchmarked: unsupported historical harness",
                 "unsupported historical throughput harness"),
                (None, "Not benchmarked", "hosted harness admission '' is not a reviewed disposition"),
                ("maybe", "Not benchmarked", "hosted harness admission 'maybe' is not a reviewed disposition")):
            with self.subTest(admission=admission):
                api, started, unused_written = self.publish(pull_record, copy.deepcopy(document), "pull", admission)
                self.assertEqual((started["status"], started["conclusion"]), ("completed", "failure"))
                self.assertIn(title, started["output"]["title"])
                self.assertIn(reason, started["output"]["text"])
                shown = json.loads(started["output"]["text"].removeprefix("```json\n").removesuffix("\n```"))
                self.assertEqual(shown["inventory_coverage"]["discharged"], [])
                # No receipt was read: the shown identity is the authorized one, not the artifact's.
                self.assertNotIn("binaries", shown)

    def test_malformed_production_profile_binaries_end_in_a_terminal_failure(self) -> None:
        """Rendering a malformed role record must not raise before the check completes (#2761)."""
        for label, role in (("object", {"sha256": 1}), ("list", [1]), ("string", "x"), ("boolean", True)):
            with self.subTest(role=label):
                recorded = receipt()
                recorded["production_profile"] = {"requested": True, "status": "failed",
                                                  "binaries": {"baseline": role, "candidate": role}}
                api, started, unused_written = self.publish(machine_bytes(), recorded)
                self.assertEqual((len(api.checks), started["status"], started["conclusion"]),
                                 (1, "completed", "failure"))

    def test_out_of_range_production_profile_numbers_end_in_a_terminal_failure(self) -> None:
        """An integer past the float range in rendered cells must not raise before the check completes (#3675)."""
        for label, wall in (("huge integer", [10 ** 309, 1.0]), ("huge negative", [-(10 ** 309), 1.0]),
                            ("boolean", [True, 1.0])):
            with self.subTest(wall=label):
                recorded = receipt()
                recorded["production_profile"] = {"requested": True, "status": "failed", "binaries": {},
                                                  "cells": [{"name": "tiny_startup/fast", "decision": "regression",
                                                             "wall_seconds": wall}]}
                api, started, unused_written = self.publish(machine_bytes(), recorded)
                self.assertEqual((len(api.checks), started["status"], started["conclusion"]),
                                 (1, "completed", "failure"))

    def test_publisher_completes_the_started_check_and_emits_the_entry(self) -> None:
        api, started, written = self.publish(machine_bytes())
        self.assertEqual(len(api.checks), 1)
        self.assertEqual((started["status"], started["conclusion"]), ("completed", "success"))
        self.assertIn("[Evidence artifact]", started["output"]["summary"])
        published = json.loads(written["comment"])
        self.assertEqual(written["head"], HEAD)
        self.assertEqual(published["check_url"], started["html_url"])
        self.assertEqual(compiler_comment.validate_entry(published, REPO), [])
        self.assertIn(f"`{'b' * 40}` (first parent)", published["markdown"])
        # The check names the inventory rows this validated main attempt discharged (#2761).
        self.assertIn("`uarch_lab:compare@main`", started["output"]["summary"])
        shown = json.loads(started["output"]["text"].removeprefix("```json\n").removesuffix("\n```"))
        self.assertEqual(shown["inventory_coverage"]["discharged"],
                         ["build:bench_throughput@main", "uarch_lab:compare@main"])
        self.assertIn("| Inventory rows discharged | `build:bench_throughput@main`, `uarch_lab:compare@main` |",
                      published["markdown"])
        self.assertIn('"cpu_vendor": "unknown (not exposed', started["output"]["text"])

    def test_publisher_requires_sealed_raw_evidence_on_the_ordinary_routes(self) -> None:
        # #2918: a measured ordinary attempt without v1 control files fails on main and pull alike.
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull", ref="refs/pull/7/head")
        for mode, document, record in (
                ("main", None, machine_bytes()),
                ("pull", pull, machine_bytes(job={"value": "compare-pull", "status": "available", "reason": ""}))):
            with self.subTest(mode=mode):
                started = self.publish(record, document, mode, sealed=False)[1]
                self.assertEqual((started["status"], started["conclusion"]), ("completed", "failure"))
                self.assertIn("is required on this route, but the artifact carries neither control file",
                              started["output"]["summary"])
                started = self.publish(record, document, mode)[1]
                self.assertEqual(started["conclusion"], "success", started["output"]["summary"])
                self.assertIn("replayed 12 lab pairs and 12 corpus cases", started["output"]["summary"])

    def test_publisher_refuses_a_missing_or_stale_machine_record(self) -> None:
        stale = machine_bytes(run_id={"value": "90", "status": "available", "reason": ""})
        for record, reason in ((None, "is missing from the evidence artifact"), (stale, "run_id '90' is not")):
            with self.subTest(reason=reason):
                started = self.publish(record)[1]
                self.assertEqual((started["status"], started["conclusion"]), ("completed", "failure"))
                self.assertIn(reason, started["output"]["summary"])


if __name__ == "__main__":
    unittest.main()
