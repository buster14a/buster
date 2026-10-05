#!/usr/bin/env python3
"""Network-free authorization, controller and workflow regressions for #1791."""
from __future__ import annotations

import base64
import copy
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import zipfile

import native_retirement_automation as a
import native_retirement_controller as c
import native_retirement_integration as i

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "buster14a/buster"
BASE, HEAD, SOURCE = "b" * 40, "c" * 40, "d" * 40
BOT = {"login": a.BOT_LOGIN, "id": a.BOT_ID, "type": "Bot"}
CLASSIFICATION = {"kind": "ordinary", "changed_paths": ["src/buster/lib/value.c"]}


def archive(value, name="request.json"):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as output:
        output.writestr(name, a.canonical(value))
    return stream.getvalue()


def run_record(number, path, event, *, status="in_progress", conclusion=None, base=BASE):
    return {"id": number, "repository": {"full_name": REPOSITORY}, "path": path,
            "event": event, "head_branch": "main", "head_sha": base, "run_attempt": 1,
            "status": status, "conclusion": conclusion, "actor": copy.deepcopy(BOT),
            "triggering_actor": copy.deepcopy(BOT), "created_at": "2026-09-29T07:00:00Z"}


class API:
    def __init__(self):
        self.repository, self.token = REPOSITORY, "unused-fixture-token"
        self.base = BASE
        self.policy = {"schema": a.POLICY_SCHEMA, "repository": REPOSITORY, "enabled": True,
                       "epoch": 1, "classes": ["ordinary", "bootstrap", "policy"],
                       "paused_pull_requests": []}
        self.policy_bytes = a.canonical(self.policy)
        self.policy_digest = a.digest(self.policy_bytes)
        self.request_data = a.new_request(REPOSITORY, 1791, BASE, HEAD, SOURCE, "ordinary",
                                          self.policy_digest, 100)
        self.raw_archive = archive(self.request_data)
        self.artifact = {"id": 10, "name": a.artifact_name(100), "expired": False,
                         "size_in_bytes": len(self.raw_archive), "digest": "sha256:" + a.digest(self.raw_archive)}
        self.pr = {"number": 1791, "state": "open", "draft": False, "user": {"login": "author"},
                   "head": {"sha": HEAD, "repo": {"full_name": REPOSITORY}},
                   "base": {"ref": "main", "repo": {"full_name": REPOSITORY}},
                   "auto_merge": {"enabled_by": {"login": "author"}}}
        self.runs = {100: run_record(100, a.CONTROLLER_PATH, "workflow_run"),
                     200: run_record(200, a.WRITER_PATH, "workflow_dispatch")}
        self.runs[200]["display_title"] = a.writer_title(self.request_data["key"])
        self.context = {"request_json": a.canonical(self.request_data).decode(),
                        "request_key": self.request_data["key"], "expected_base": BASE,
                        "source_head": SOURCE, "actor_id": str(a.BOT_ID),
                        "triggering_actor": a.BOT_LOGIN, "event_name": "workflow_dispatch",
                        "workflow_ref": REPOSITORY + "/" + a.WRITER_PATH + "@refs/heads/main",
                        "workflow_sha": BASE, "run_attempt": "1", "run_id": "200",
                        "publication": False}
        self.comments = []
        self.writer_visible = False
        self.workflow_active = True
        self.active = []
        self.outcomes = []
        self.posts = []
        self.dispatch_error = None
        self.dispatch_response = None
        self.ci_success = True
        self.extra_checks = []

    def permission(self, login):
        raise AssertionError("machine authorization must not impersonate a collaborator")

    def all(self, path, **query):
        if path == "pulls":
            return [copy.deepcopy(self.pr)]
        if path == "issues/1791/comments":
            return copy.deepcopy(self.comments)
        raise AssertionError((path, query))

    def request(self, path, *, method="GET", body=None, **query):
        if method == "PATCH" and path.startswith("issues/comments/"):
            for comment in self.comments:
                if comment["id"] == int(path.rsplit("/", 1)[1]):
                    comment["body"] = body["body"]
                    return copy.deepcopy(comment)
            raise AssertionError("missing comment")
        if method == "POST" and path == "issues/1791/comments":
            value = {"id": len(self.comments) + 1, "body": body["body"], "user": copy.deepcopy(BOT)}
            self.comments.append(value)
            self.posts.append((path, body))
            return copy.deepcopy(value)
        if method == "POST" and path.endswith("/dispatches"):
            self.posts.append((path, body))
            if self.dispatch_error:
                raise self.dispatch_error
            return self.dispatch_response
        if path == "git/ref/heads/main":
            return {"object": {"sha": self.base}}
        if path == "contents/" + a.POLICY_PATH:
            raw = a.canonical(self.policy)
            return {"type": "file", "encoding": "base64", "content": base64.b64encode(raw).decode()}
        if path == "actions/workflows/" + Path(a.CONTROLLER_PATH).name:
            return {"path": a.CONTROLLER_PATH, "state": "active" if self.workflow_active else "disabled_manually"}
        if path.startswith("actions/runs/"):
            fields = path.split("/")
            run_id = int(fields[2])
            if len(fields) == 3:
                return copy.deepcopy(self.runs[run_id])
            if fields[3] == "artifacts":
                return {"artifacts": [copy.deepcopy(self.artifact)]}
            if fields[3:] == ["attempts", "1", "jobs"]:
                return {"jobs": copy.deepcopy(self.outcomes)}
        if path == "actions/workflows/" + Path(a.WRITER_PATH).name + "/runs":
            rows = self.active if "status" in query else ([self.runs[200]] if self.writer_visible else [])
            return {"workflow_runs": copy.deepcopy(rows)}
        if path == "pulls/1791":
            return copy.deepcopy(self.pr)
        if path == "commits/" + HEAD + "/check-runs":
            return {"check_runs": [{"id": 1, "name": "CI complete", "head_sha": HEAD,
                                    "app": {"id": 15368}, "status": "completed",
                                    "conclusion": "success" if self.ci_success else "failure"}] + self.extra_checks}
        raise AssertionError((path, method, body, query))

    def claim(self, state="claimed"):
        record = {"schema": c.LEDGER_SCHEMA, "request": self.request_data, "state": state,
                  "run_id": None, "detail": "fixture"}
        self.comments = [{"id": 1, "body": c.ledger_body(record), "user": copy.deepcopy(BOT)}]
        return c.parse_ledger(self.comments[0], REPOSITORY)


class AuthorizationTests(unittest.TestCase):
    def setUp(self):
        self.api = API()
        self.download = mock.patch.object(a, "download_archive", side_effect=lambda api, number: api.raw_archive)
        self.download.start()
        self.addCleanup(self.download.stop)

    def authorize(self):
        return a.authorize_request(self.api, self.api.pr, a.BOT_LOGIN, self.api.context, CLASSIFICATION)

    def test_valid_standing_grant_records_machine_not_review(self):
        result = self.authorize()
        self.assertEqual(result["maintainer_approvals"], [])
        self.assertEqual(result["dispatcher_permission"], "standing-automation")
        self.assertEqual(result["authorization"]["mode"], "automation")
        self.assertEqual(result["authorization"]["controller_artifact"]["id"], 10)

    def test_publication_requires_successful_controller(self):
        self.api.context["publication"] = True
        with self.assertRaisesRegex(a.AutomationError, "successfully complete"):
            self.authorize()
        self.api.runs[100].update(status="completed", conclusion="success")
        self.authorize()

    def test_controller_cancellation_rejected_even_before_publication(self):
        self.api.runs[100].update(status="completed", conclusion="cancelled")
        with self.assertRaises(a.AutomationError):
            self.authorize()

    def test_disabled_workflow_revokes_prepared_request(self):
        self.authorize()
        self.api.workflow_active = False
        with self.assertRaisesRegex(a.AutomationError, "revoked"):
            self.authorize()

    def test_revocation_during_artifact_download_rechecked(self):
        def download(api, number):
            api.workflow_active = False
            return api.raw_archive
        with mock.patch.object(a, "download_archive", side_effect=download):
            with self.assertRaisesRegex(a.AutomationError, "revoked"):
                self.authorize()

    def test_main_movement_is_explicit_staleness(self):
        self.api.base = "e" * 40
        with self.assertRaises(a.AutomationMoved) as caught:
            self.authorize()
        self.assertEqual(caught.exception.exit_code, 75)

    def test_head_movement_after_artifact_is_explicit_staleness(self):
        def download(api, number):
            api.pr["head"]["sha"] = "e" * 40
            return api.raw_archive
        with mock.patch.object(a, "download_archive", side_effect=download):
            with self.assertRaises(a.AutomationMoved) as caught:
                self.authorize()
        self.assertEqual(caught.exception.exit_code, 76)

    def test_wrong_context_and_retry_do_not_authorize(self):
        original = copy.deepcopy(self.api.context)
        for key, value in (("actor_id", "1"), ("triggering_actor", "author"),
                           ("event_name", "pull_request"), ("run_attempt", "2"),
                           ("workflow_sha", "f" * 40), ("run_id", "0"),
                           ("workflow_ref", "fork/repo/other@refs/heads/main")):
            with self.subTest(key=key):
                self.api.context = {**original, key: value}
                with self.assertRaises(a.AutomationError):
                    self.authorize()

    def test_wrong_run_identity_and_lookalike_bot_rejected(self):
        original = copy.deepcopy(self.api.runs[200])
        for key, value in (("path", "other.yml"), ("head_branch", "feature"),
                           ("run_attempt", 2), ("head_sha", "e" * 40),
                           ("actor", {**BOT, "id": 1}), ("actor", {**BOT, "type": "User"}),
                           ("display_title", "manual request"), ("status", "completed")):
            with self.subTest(key=key, value=value):
                self.api.runs[200] = {**original, key: value}
                with self.assertRaises(a.AutomationError):
                    self.authorize()

    def test_untrusted_controller_workflow_cannot_supply_artifact(self):
        self.api.runs[100]["path"] = ".github/workflows/untrusted.yml"
        with self.assertRaises(a.AutomationError):
            self.authorize()

    def test_policy_disabled_paused_or_outside_class_rejected(self):
        for key, value in (("enabled", False), ("paused_pull_requests", [1791]),
                           ("classes", ["bootstrap"]), ("epoch", 2)):
            with self.subTest(key=key):
                api = API()
                api.policy[key] = value
                with self.assertRaises(a.AutomationError):
                    a.authorize_request(api, api.pr, a.BOT_LOGIN, api.context, CLASSIFICATION)

    def test_self_authorization_and_threshold_paths_need_owner(self):
        for path in (a.POLICY_PATH, a.CONTROLLER_PATH, a.WRITER_PATH,
                     "tools/native_retirement_integration.py", "tools/native_retirement_automation.py",
                     "tools/throughput/retirement_stats.h", "tools/bench_direct/run_workloads.py"):
            with self.subTest(path=path):
                with self.assertRaises(a.AutomationError):
                    a.require_scope(self.api.policy, 1791, {"kind": "bootstrap", "changed_paths": [path]})

    def test_unrelated_bootstrap_can_be_delegated_explicitly(self):
        a.require_scope(self.api.policy, 1791,
                        {"kind": "bootstrap", "changed_paths": ["tools/native_retirement_materializer.py"]})

    def test_changed_closed_draft_or_external_pr_rejected(self):
        for field in ("state", "draft", "repo"):
            with self.subTest(field=field):
                api = API()
                def download(current, number):
                    if field == "state":
                        current.pr["state"] = "closed"
                    elif field == "draft":
                        current.pr["draft"] = True
                    else:
                        current.pr["head"]["repo"] = None
                    return current.raw_archive
                with mock.patch.object(a, "download_archive", side_effect=download):
                    with self.assertRaises(a.AutomationError):
                        a.authorize_request(api, api.pr, a.BOT_LOGIN, api.context, CLASSIFICATION)

    def test_wrong_expired_or_digestless_artifact_rejected(self):
        original = copy.deepcopy(self.api.artifact)
        for key, value in (("expired", True), ("digest", None), ("digest", "sha256:" + "0" * 64),
                           ("size_in_bytes", a.MAX_ARCHIVE_BYTES + 1), ("name", "candidate-artifact")):
            with self.subTest(key=key):
                self.api.artifact = {**original, key: value}
                with self.assertRaises(a.AutomationError):
                    self.authorize()

    def test_self_consistent_different_artifact_cannot_replace_request(self):
        other = dict(self.api.request_data, source_head="f" * 40)
        other["key"] = a.request_key(other)
        self.api.raw_archive = archive(other)
        self.api.artifact.update(digest="sha256:" + a.digest(self.api.raw_archive),
                                 size_in_bytes=len(self.api.raw_archive))
        with self.assertRaisesRegex(a.AutomationError, "does not match"):
            self.authorize()

    def test_core_machine_mode_uses_trusted_classification_not_permissions(self):
        classification = i.classify_paths(CLASSIFICATION["changed_paths"])
        with mock.patch.object(i, "_commit", return_value=BASE), \
                mock.patch.object(i, "classify_candidate", return_value=classification):
            report = i.authorize(self.api, 1791, HEAD, "ordinary", a.BOT_LOGIN,
                                 authorization_mode="automation", repo_root=ROOT,
                                 automation_context=self.api.context)
        self.assertEqual(report["authorization"]["mode"], "automation")

    def test_core_authorizes_actual_git_source_diff_not_candidate_authority(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            def git(*args):
                return subprocess.run(["git", "-C", str(repo), *args], check=True,
                                      text=True, capture_output=True).stdout.strip()
            git("init", "-b", "main")
            git("config", "user.name", "Fixture")
            git("config", "user.email", "fixture@example.invalid")
            source = repo / "src/buster/lib/value.c"
            source.parent.mkdir(parents=True)
            source.write_text("int value = 1;\n")
            git("add", ".")
            git("commit", "-m", "base")
            base = git("rev-parse", "HEAD")
            source.write_text("int value = 2;\n")
            git("commit", "-am", "candidate")
            head = git("rev-parse", "HEAD")
            git("checkout", "--detach", base)
            api = self.api
            api.base = base
            api.pr["head"]["sha"] = head
            request = a.new_request(REPOSITORY, 1791, base, head, head, "ordinary", api.policy_digest, 100)
            api.request_data = request
            api.raw_archive = archive(request)
            api.artifact.update(size_in_bytes=len(api.raw_archive), digest="sha256:" + a.digest(api.raw_archive))
            for run in api.runs.values():
                run["head_sha"] = base
            api.runs[200]["display_title"] = a.writer_title(request["key"])
            context = {**api.context, "expected_base": base, "source_head": head, "workflow_sha": base,
                       "request_json": a.canonical(request).decode(), "request_key": request["key"]}
            result = i.authorize(api, 1791, head, "ordinary", a.BOT_LOGIN,
                                  authorization_mode="automation", repo_root=repo, automation_context=context)
            self.assertEqual(result["authorization"]["request"]["source_head"], head)

    def test_core_main_movement_maps_to_existing_stale_exit(self):
        self.api.base = "e" * 40
        with mock.patch.object(i, "_commit", return_value=BASE), \
                mock.patch.object(i, "classify_candidate", return_value=i.classify_paths(CLASSIFICATION["changed_paths"])):
            with self.assertRaises(i.StaleMain):
                i.authorize(self.api, 1791, HEAD, "ordinary", a.BOT_LOGIN,
                            authorization_mode="automation", repo_root=ROOT,
                            automation_context=self.api.context)


class ManualCompatibilityTests(unittest.TestCase):
    def test_ordinary_manual_dispatch_and_stale_head_message_remain_compatible(self):
        api = API()
        api.permission = lambda login: "admin"
        report = i.authorize(api, 1791, HEAD, "ordinary", "owner")
        self.assertEqual(report["authorization"]["mode"], "dispatcher")
        with self.assertRaisesRegex(i.IntegrationError, "head SHA changed"):
            i.authorize(api, 1791, "e" * 40, "ordinary", "owner")

    def test_manual_independent_review_is_still_required(self):
        api = API()
        api.permission = lambda login: "admin"
        api.all = lambda path: []
        with self.assertRaisesRegex(i.IntegrationError, "approving maintainer"):
            i.authorize(api, 1791, HEAD, "bootstrap", "owner")
        api.all = lambda path: [{"id": 1, "state": "APPROVED", "commit_id": HEAD,
                                 "user": {"login": "independent"}}]
        self.assertEqual(i.authorize(api, 1791, HEAD, "bootstrap", "owner")["maintainer_approvals"],
                         ["independent"])

    def test_explicit_solo_mode_does_not_need_a_machine_request(self):
        api = API()
        api.permission = lambda login: "admin"
        context = {"configured_login": "owner", "expected_base": BASE,
                   "workflow_sha": BASE, "event_name": "workflow_dispatch",
                   "workflow_ref": REPOSITORY + "/" + a.WRITER_PATH + "@refs/heads/main",
                   "triggering_actor": "owner", "run_attempt": "1", "run_id": "300"}
        report = i.authorize(api, 1791, HEAD, "policy", "owner",
                             authorization_mode="solo-maintainer", solo_context=context)
        self.assertEqual(report["authorization"]["mode"], "solo-maintainer")
        self.assertEqual(report["maintainer_approvals"], [])


class FormatTests(unittest.TestCase):
    def test_duplicate_unknown_and_boolean_id_fields_fail(self):
        with self.assertRaises(a.AutomationError):
            a.decode('{"schema":1,"schema":2}')
        for field, value in (("extra", "unknown"), ("number", True), ("controller_attempt", True),
                             ("source_head", "d" * 39), ("classification", "split-required")):
            with self.subTest(field=field):
                request = {**API().request_data, field: value}
                with self.assertRaises(a.AutomationError):
                    a.validate_request(request, REPOSITORY)

    def test_request_key_deduplicates_controller_runs_not_candidate_trees(self):
        request = API().request_data
        self.assertEqual(a.request_key(request), a.request_key({**request, "controller_run_id": 101}))
        for field in ("base", "head", "source_head", "policy_sha256"):
            changed = {**request, field: "f" * len(request[field])}
            self.assertNotEqual(a.request_key(request), a.request_key(changed))

    def test_archive_inventory_and_size_bounds(self):
        request = API().request_data
        self.assertEqual(a.request_from_archive(archive(request)), request)
        for name in ("../request.json", "other.json", "/request.json", "folder/request.json"):
            with self.assertRaises(a.AutomationError):
                a.request_from_archive(archive(request, name))
        with self.assertRaises(a.AutomationError):
            a.request_from_archive(b"0" * (a.MAX_ARCHIVE_BYTES + 1))
        raw = io.BytesIO()
        with zipfile.ZipFile(raw, "w") as output:
            output.writestr("request.json", a.canonical(request))
            output.writestr("extra", "untrusted")
        with self.assertRaises(a.AutomationError):
            a.request_from_archive(raw.getvalue())

    def test_policy_schema_rejects_unknown_or_duplicate_classes(self):
        original = API().policy
        for key, value in (("enabled", "true"), ("classes", ["ordinary", "ordinary"]),
                           ("classes", ["generated-edit"]), ("epoch", False),
                           ("paused_pull_requests", [True]), ("unknown", 1)):
            with self.subTest(key=key):
                with self.assertRaises(a.AutomationError):
                    a.validate_policy({**original, key: value}, REPOSITORY)

    def test_new_authority_and_policy_paths_are_classified(self):
        for path in (a.CONTROLLER_PATH, "tools/native_retirement_automation.py",
                     "tools/native_retirement_controller.py"):
            self.assertEqual(i.classify_paths([path]).kind, "bootstrap")
        self.assertEqual(i.classify_paths([a.POLICY_PATH]).kind, "policy")
        self.assertEqual(i.classify_paths([a.POLICY_PATH, a.CONTROLLER_PATH]).kind, "split-required")




if __name__ == "__main__":
    unittest.main()
