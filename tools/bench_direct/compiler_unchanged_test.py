#!/usr/bin/env python3
"""Offline checks of the automatic MAIN unchanged-input outcome (#3923).

The native classifier (tools/ci_no_code.c, event main_push) decides; these
tests cover the hosted publisher's re-validation of that decision and its
neutral "Not measured" report, which must never count as a measurement.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize_compiler  # noqa: E402
import compiler_comment  # noqa: E402
import compiler_github  # noqa: E402
import compiler_publish  # noqa: E402

REPOSITORY = "buster14a/buster"
HEAD, BASE, TRUSTED, FIRST = "a" * 40, "b" * 40, "c" * 40, "d" * 40
REFERENCE_URL = f"https://github.com/{REPOSITORY}/runs/700"
REFERENCE_AT = "2026-10-01T02:03:04Z"


def proof_receipt(**change: object) -> str:
    receipt = {"schema": "buster-ci-main-unchanged-v1", "event": "main_push", "unchanged": True, "base": BASE,
               "head": HEAD, "policy": TRUSTED, "subject_changes": 1, "policy_changes": 0,
               "reason": "reviewed-prose-only"}
    receipt.update(change)
    return json.dumps({key: value for key, value in receipt.items() if value is not None})


def environment(**change: str) -> dict:
    values = {"GH_TOKEN": "t", "BQ_MODE": "main", "BQ_REPOSITORY": REPOSITORY, "BQ_REF": "refs/heads/main",
              "BQ_HEAD_COMMIT": HEAD, "BQ_REQUEST_RUN_ID": "91", "BQ_REQUEST_ATTEMPT": "1", "BQ_RUN_ID": "92",
              "BQ_RUN_ATTEMPT": "1", "BQ_TRUSTED_REVISION": TRUSTED, "BQ_AUTHORIZE_RESULT": "success",
              "BQ_AUTHORIZED_ATTEMPT": "1", "BQ_COMPARE_RESULT": "skipped", "BQ_PULL": "0", "BQ_PULL_HEAD": HEAD,
              "BQ_BASE_COMMIT": BASE, "BQ_BASE_TREE": "e" * 40, "BQ_HEAD_TREE": "f" * 40, "BQ_FIRST_PARENT": FIRST,
              "BQ_RANGE": "2", "BQ_UNCHANGED": "true", "BQ_UNCHANGED_RECEIPT": proof_receipt(),
              "BQ_CLASSIFICATION_MS": "4210", "BQ_BASE_MEASURED": "true", "BQ_REFERENCE_CHECK_URL": REFERENCE_URL,
              "BQ_REFERENCE_MEASURED_AT": REFERENCE_AT,
              "BQ_DETAILS_URL": f"https://github.com/{REPOSITORY}/actions/runs/92/attempts/1"}
    values.update(change)
    return values


def reference_row(**change: object) -> dict:
    row = {"id": 700, "name": "9700X compiler benchmark", "head_sha": BASE, "status": "completed",
           "conclusion": "success", "app": {"id": compiler_github.GITHUB_ACTIONS_APP_ID}, "html_url": REFERENCE_URL,
           "completed_at": REFERENCE_AT, "external_id": f"buster-9700x-compiler-main-v1:{BASE}:80.1:1"}
    row.update(change)
    return row


class FakeApi:
    """Check-run listings by head; records every write."""

    def __init__(self, rows: dict[str, list[dict]]):
        self.rows = rows
        self.writes: list[tuple[str, dict, str]] = []

    def pages(self, path: str, field: str = "") -> list:
        head = path.split("/commits/")[1].split("/")[0]
        return list(self.rows.get(head, []))

    def request(self, path: str, data: dict | None = None, method: str = "") -> object:
        self.writes.append((path, data or {}, method))
        created = dict(data or {}, id=900 + len(self.writes), app={"id": compiler_github.GITHUB_ACTIONS_APP_ID},
                       html_url=f"https://github.com/{REPOSITORY}/runs/{900 + len(self.writes)}")
        self.rows.setdefault(created.get("head_sha", HEAD), []).append(created)
        return created


def expected() -> dict:
    return {"mode": "main", "repository": REPOSITORY, "ref": "refs/heads/main", "pull": "0", "pull_head": HEAD,
            "base": BASE, "base_tree": "e" * 40, "head": HEAD, "head_tree": "f" * 40, "trusted_revision": TRUSTED,
            "request_run_id": "91", "run_id": "92", "run_attempt": "1", "first_parent": FIRST, "range": "2"}


LEGACY_ROUTE = dict({name: "-" for name in compiler_publish.MAIN_ROUTE_FIELDS}, main_owned=False,
                    main_profile="compiler-compare-v1", main_preparation_policy="legacy-rebuild",
                    main_measurement_revision=TRUSTED, main_policy_revision=TRUSTED)


class UnchangedProofTest(unittest.TestCase):
    def proof(self, env: dict | None = None, route: dict | None = LEGACY_ROUTE, compare: str = "skipped",
              rows: list[dict] | None = None) -> tuple[dict, list[str]]:
        api = FakeApi({BASE: [reference_row()] if rows is None else rows})
        return compiler_publish.unchanged_proof(api, env or environment(), expected(), route, compare)

    def test_valid_decision_links_the_reference_measurement(self) -> None:
        proof, problems = self.proof()
        self.assertEqual(problems, [])
        self.assertEqual(proof["reference"], {"check_url": REFERENCE_URL, "measured_at": REFERENCE_AT})
        self.assertEqual(proof["classification_ms"], 4210)

    def test_tampered_incomplete_or_foreign_decisions_never_skip(self) -> None:
        cases = {
            "not unchanged": environment(BQ_UNCHANGED_RECEIPT=proof_receipt(unchanged=False)),
            "other base": environment(BQ_UNCHANGED_RECEIPT=proof_receipt(base=FIRST)),
            "other head": environment(BQ_UNCHANGED_RECEIPT=proof_receipt(head=FIRST)),
            "other policy": environment(BQ_UNCHANGED_RECEIPT=proof_receipt(policy=FIRST)),
            "other reason": environment(BQ_UNCHANGED_RECEIPT=proof_receipt(reason="incomplete-diff")),
            "pull event": environment(BQ_UNCHANGED_RECEIPT=proof_receipt(event="pull_request")),
            "missing key": environment(BQ_UNCHANGED_RECEIPT=proof_receipt(policy_changes=None)),
            "extra key": environment(BQ_UNCHANGED_RECEIPT=proof_receipt(extra=1)),
            "count": environment(BQ_UNCHANGED_RECEIPT=proof_receipt(subject_changes="1")),
            "duplicate key": environment(BQ_UNCHANGED_RECEIPT=proof_receipt()[:-1] + ',"unchanged":false}'),
            "empty": environment(BQ_UNCHANGED_RECEIPT=""),
            "not json": environment(BQ_UNCHANGED_RECEIPT="unchanged=true"),
            "no reference": environment(BQ_BASE_MEASURED="false"),
            "no timing": environment(BQ_CLASSIFICATION_MS=""),
        }
        for name, env in cases.items():
            with self.subTest(name=name):
                proof, problems = self.proof(env)
                self.assertEqual(proof, {})
                self.assertTrue(problems)

    def test_ran_owned_or_unrouted_attempts_never_skip(self) -> None:
        for compare in ("success", "failure", "cancelled", ""):
            with self.subTest(compare=compare):
                self.assertTrue(self.proof(compare=compare)[1])
        for route in (None, {"main_owned": True}, {}):
            with self.subTest(route=route):
                self.assertTrue(self.proof(route=route)[1])

    def test_reference_that_changed_while_planning_never_anchors_a_skip(self) -> None:
        for rows in ([], [reference_row(conclusion="failure")], [reference_row(conclusion="neutral")],
                     [reference_row(html_url=f"https://github.com/{REPOSITORY}/runs/701")],
                     [reference_row(completed_at="2026-10-02T00:00:00Z")], [reference_row(head_sha=HEAD)]):
            with self.subTest(rows=rows):
                proof, problems = self.proof(rows=rows)
                self.assertEqual(proof, {})
                self.assertIn("the baseline measurement changed or vanished while planning", problems)


class UnchangedPublishTest(unittest.TestCase):
    def run_main(self, env: dict, rows: dict[str, list[dict]]) -> tuple[int, FakeApi, dict]:
        api = FakeApi(rows)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            summary = Path(directory) / "summary"
            env = dict(env, GITHUB_OUTPUT=str(output), GITHUB_STEP_SUMMARY=str(summary))
            with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(sys, "argv", ["compiler_publish.py"]), \
                    mock.patch.object(compiler_publish, "Api", return_value=api), \
                    mock.patch.object(compiler_publish, "main_route", return_value=dict(LEGACY_ROUTE)), \
                    mock.patch.object(compiler_publish, "read_evidence",
                                      return_value=(None, None, "no evidence artifact", {}, {})) as evidence, \
                    mock.patch.object(compiler_publish, "queue_delay", return_value="NA"):
                code = compiler_publish.main()
            outputs = dict(line.split("=", 1) for line in output.read_text().splitlines()) if output.exists() else {}
            outputs["evidence_read"] = evidence.called
        return code, api, outputs

    def test_unchanged_attempt_completes_neutral_without_reading_evidence(self) -> None:
        code, api, outputs = self.run_main(environment(), {BASE: [reference_row()]})
        self.assertEqual(code, 0)
        self.assertFalse(outputs["evidence_read"])
        created = [data for path, data, method in api.writes if path == "/check-runs"]
        self.assertEqual(len(created), 1)
        check = created[0]
        self.assertEqual((check["status"], check["conclusion"], check["output"]["title"]),
                         ("completed", "neutral", compiler_publish.UNCHANGED_TITLE))
        summary = check["output"]["summary"]
        for text in (HEAD, BASE, TRUSTED, REFERENCE_URL, REFERENCE_AT, "request run 91.1", "run 92 attempt 1",
                     "No 9700X job was allocated", "4.210 s", "not estimated"):
            self.assertIn(text, summary)
        self.assertNotIn("Measured (", summary)
        shown = json.loads(check["output"]["text"].removeprefix("```json\n").removesuffix("\n```"))
        self.assertEqual(shown["inventory_coverage"]["discharged"], [])
        self.assertNotIn("summary", shown)
        # The neutral row never becomes a baseline for the next request.
        self.assertFalse(compiler_github.measured(api.rows[HEAD][-1], HEAD))
        self.assertEqual(authorize_compiler.choose_base([HEAD, BASE], api.rows), BASE)
        entry = json.loads(outputs["comment"])
        self.assertEqual(compiler_comment.validate_entry(entry, REPOSITORY), [])
        self.assertEqual((entry["conclusion"], entry["base"], entry["artifact_url"]), ("neutral", BASE, ""))

    def test_completed_attempt_is_not_reopened(self) -> None:
        marker = compiler_publish.attempt_marker(HEAD, "main", "91", "1", "1")
        done = {"id": 5, "name": "9700X compiler benchmark", "head_sha": HEAD, "status": "completed",
                "conclusion": "failure", "app": {"id": compiler_github.GITHUB_ACTIONS_APP_ID}, "external_id": marker}
        code, api, _ = self.run_main(environment(), {BASE: [reference_row()], HEAD: [done]})
        self.assertEqual(code, 0)
        self.assertEqual(api.writes, [])

    def test_refused_decision_reports_the_missing_measurement(self) -> None:
        code, api, outputs = self.run_main(environment(BQ_BASE_MEASURED="false"), {BASE: [reference_row()]})
        self.assertEqual(code, 0)
        self.assertTrue(outputs["evidence_read"])
        check = [data for path, data, method in api.writes if path == "/check-runs"][0]
        self.assertEqual(check["conclusion"], "failure")
        self.assertIn("unchanged-input skip refused: the baseline is not an actual measurement",
                      check["output"]["summary"])

    def test_measure_decision_uses_the_ordinary_path(self) -> None:
        code, api, outputs = self.run_main(environment(BQ_UNCHANGED="false", BQ_COMPARE_RESULT="success"),
                                           {BASE: [reference_row()]})
        self.assertTrue(outputs["evidence_read"])
        check = [data for path, data, method in api.writes if path == "/check-runs"][0]
        self.assertNotIn("unchanged-input", check["output"]["summary"])


if __name__ == "__main__":
    unittest.main()
