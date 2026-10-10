#!/usr/bin/env python3
"""Network-free controller and workflow regressions for #1791.

Reuse the bounded fake GitHub API from the authorization suite; no network or
candidate execution occurs while exercising request lifecycle decisions.
"""
import base64
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import urllib.error
import zipfile

from native_retirement_automation_test import (API, BASE, HEAD, SOURCE, BOT,
                                              REPOSITORY, CLASSIFICATION, ROOT, a, c, i,
                                              run_record)


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.api = API()
        self.download = mock.patch.object(a, "download_archive", side_effect=lambda api, number: api.raw_archive)
        self.download.start()
        self.addCleanup(self.download.stop)

    def test_human_comments_cannot_masquerade_as_controller_claims(self):
        self.api.claim()
        comment = self.api.comments[0]
        comment["user"] = {**BOT, "id": 1}
        self.assertIsNone(c.parse_ledger(comment, REPOSITORY))

    def test_duplicate_claims_fail_closed(self):
        self.api.claim()
        self.api.comments.append({**self.api.comments[0], "id": 2})
        with self.assertRaises(a.AutomationError):
            c.ledger(self.api, 1791)

    def test_successful_dispatch_is_one_post_and_recorded(self):
        self.api.claim()
        result = c.dispatch(self.api, self.api.request_data)
        self.assertEqual(result["status"], "dispatched")
        self.assertEqual(len(self.api.posts), 1)
        self.assertEqual(c.ledger(self.api, 1791)[0]["state"], "dispatched")
        with self.assertRaises(a.AutomationError):
            c.dispatch(self.api, self.api.request_data)
        self.assertEqual(len(self.api.posts), 1)

    def test_moved_input_before_post_releases_only_as_superseded(self):
        self.api.claim()
        self.api.base = "e" * 40
        with self.assertRaises(a.AutomationMoved):
            c.dispatch(self.api, self.api.request_data)
        record = c.ledger(self.api, 1791)[0]
        self.assertEqual(record["state"], "superseded")
        self.assertEqual(self.api.posts, [])
        request = a.new_request(REPOSITORY, 1791, "e" * 40, HEAD, SOURCE, "ordinary",
                                self.api.policy_digest, 101)
        self.assertEqual(c.disposition([record], request), "eligible")

    def test_manual_writer_starting_after_plan_uses_existing_serialization(self):
        self.api.claim()
        self.api.active = [self.api.runs[200]]
        self.assertEqual(c.dispatch(self.api, self.api.request_data)["status"], "dispatched")
        self.assertEqual(len(self.api.posts), 1)

    def test_dispatch_supports_returned_run_identity(self):
        self.api.claim()
        self.api.dispatch_response = {"workflow_run_id": 200}
        c.dispatch(self.api, self.api.request_data)
        self.assertEqual(c.ledger(self.api, 1791)[0]["run_id"], 200)

    def test_ambiguous_post_is_not_retried_and_claim_survives(self):
        self.api.claim()
        self.api.dispatch_error = OSError("response lost")
        with self.assertRaises(OSError):
            c.dispatch(self.api, self.api.request_data)
        self.assertEqual(c.ledger(self.api, 1791)[0]["state"], "claimed")
        self.api.runs[100].update(status="completed", conclusion="failure")
        record = c.reconcile(self.api, c.ledger(self.api, 1791)[0])
        self.assertEqual(record["state"], "blocked")
        self.assertEqual(len(self.api.posts), 1)

    def test_lost_receipt_recovers_exact_key_existing_writer(self):
        record = self.api.claim()
        self.api.writer_visible = True
        self.api.runs[100].update(status="completed", conclusion="failure")
        result = c.reconcile(self.api, record)
        self.assertEqual(result["state"], "dispatched")
        self.assertEqual(result["run_id"], 200)
        self.assertEqual(self.api.posts, [])

    def test_cancellation_blocks_same_source_after_main_advances(self):
        record = self.api.claim()
        self.api.writer_visible = True
        self.api.runs[200].update(status="completed", conclusion="cancelled")
        record = c.reconcile(self.api, record)
        self.assertEqual(record["state"], "blocked")
        request = a.new_request(REPOSITORY, 1791, "e" * 40, HEAD, SOURCE, "ordinary",
                                self.api.policy_digest, 101)
        self.assertEqual(c.disposition([record], request), "failed-or-cancelled-source")

    def test_catch_up_block_before_post_bars_only_its_main_revision(self):
        # #3327: a catch-up keeps its empty source head while its PR stays
        # open; a block positively recorded before the POST may retry on the
        # next main revision, never on the same one.
        record = c.save(self.api, self.api.claim(), "blocked", c.NOT_DISPATCHED + "fixture refusal")
        same_main = a.new_request(REPOSITORY, 1791, BASE, HEAD, SOURCE, "ordinary",
                                  self.api.policy_digest, 101)
        next_main = a.new_request(REPOSITORY, 1791, "e" * 40, HEAD, SOURCE, "ordinary",
                                  self.api.policy_digest, 101)
        self.assertEqual(c.disposition([record], same_main, True), "failed-or-cancelled-source")
        self.assertEqual(c.disposition([record], next_main, True), "eligible")
        self.assertEqual(c.disposition([record], next_main), "failed-or-cancelled-source")

    def test_uncertain_or_writer_blocked_catch_up_stays_blocked_after_main_advances(self):
        # A changed key and no active writer do not prove an uncertain POST was
        # never accepted; cancellations and writer failures are not retried.
        next_main = a.new_request(REPOSITORY, 1791, "e" * 40, HEAD, SOURCE, "ordinary",
                                  self.api.policy_digest, 101)
        uncertain = self.api.claim()
        self.api.dispatch_error = OSError("response lost")
        with self.assertRaises(OSError):
            c.dispatch(self.api, self.api.request_data)
        self.api.runs[100].update(status="completed", conclusion="failure")
        uncertain = c.reconcile(self.api, c.ledger(self.api, 1791)[0])
        self.assertEqual(uncertain["state"], "blocked")
        self.assertEqual(c.disposition([uncertain], next_main, True), "failed-or-cancelled-source")
        for conclusion in ("cancelled", "timed_out", "failure"):
            record = self.api.claim()
            self.api.writer_visible = True
            self.api.runs[200].update(status="completed", conclusion=conclusion)
            record = c.reconcile(self.api, record)
            self.assertEqual(record["state"], "blocked", conclusion)
            self.assertEqual(c.disposition([record], next_main, True), "failed-or-cancelled-source",
                             conclusion)

    def test_rejected_request_artifact_is_blocked_before_any_post(self):
        # #3327: machine records added to the sealed request made every
        # dispatch fail; the refusal precedes the POST, so record it as such.
        self.api.claim()
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as output:
            output.writestr("request.json", a.canonical(self.api.request_data))
            output.writestr("machine-specifications/report.json", b"{}")
        self.api.raw_archive = stream.getvalue()
        self.api.artifact.update(size_in_bytes=len(self.api.raw_archive),
                                 digest="sha256:" + a.digest(self.api.raw_archive))
        with self.assertRaises(a.AutomationError):
            c.dispatch(self.api, self.api.request_data)
        record = c.ledger(self.api, 1791)[0]
        self.assertEqual(record["state"], "blocked")
        self.assertTrue(record["detail"].startswith(c.NOT_DISPATCHED))
        self.assertEqual(self.api.posts, [])

    def test_real_failure_cannot_claim_supersession_from_arbitrary_job(self):
        self.api.runs[200].update(status="completed", conclusion="failure")
        self.api.outcomes = [{"name": "Candidate test", "conclusion": "success",
                              "steps": [{"name": "Superseded request", "conclusion": "success"}]}]
        self.assertEqual(c.writer_outcome(self.api, self.api.runs[200]), "blocked")

    def test_only_trusted_supersession_allows_new_identity(self):
        record = self.api.claim()
        self.api.writer_visible = True
        self.api.runs[200].update(status="completed", conclusion="failure")
        self.api.outcomes = [{"name": c.OUTCOME_JOB, "conclusion": "success",
                              "steps": [{"name": "Superseded request", "conclusion": "success"}]}]
        record = c.reconcile(self.api, record)
        self.assertEqual(record["state"], "superseded")
        self.assertEqual(c.disposition([record], self.api.request_data), "already-requested")
        request = a.new_request(REPOSITORY, 1791, "e" * 40, HEAD, SOURCE, "ordinary",
                                self.api.policy_digest, 101)
        self.assertEqual(c.disposition([record], request), "eligible")

    def test_cancelled_run_with_old_superseded_marker_still_blocks(self):
        self.api.runs[200].update(status="completed", conclusion="cancelled")
        self.api.outcomes = [{"name": c.OUTCOME_JOB, "conclusion": "success",
                              "steps": [{"name": "Superseded request", "conclusion": "success"}]}]
        self.assertEqual(c.writer_outcome(self.api, self.api.runs[200]), "blocked")

    def test_future_retry_attempt_does_not_reuse_request_authority(self):
        record = self.api.claim()
        self.api.writer_visible = True
        self.api.runs[200]["run_attempt"] = 2
        with self.assertRaises(a.AutomationError):
            c.reconcile(self.api, record)

    def test_external_draft_closed_and_wrong_base_are_ineligible(self):
        self.assertTrue(c.eligible_pr(self.api.pr, REPOSITORY))
        for mutate in (lambda pr: pr.update(draft=True), lambda pr: pr.update(state="closed"),
                       lambda pr: pr["head"].update(repo=None), lambda pr: pr["base"].update(ref="feature")):
            pr = copy.deepcopy(self.api.pr)
            mutate(pr)
            self.assertFalse(c.eligible_pr(pr, REPOSITORY))

    def candidate(self):
        return {"requires_writer": True, "already_current": False, "previously_published": False,
                "head": HEAD, "source_head": SOURCE, "classification": "ordinary",
                "classification_record": CLASSIFICATION}

    def plan(self, candidate=None):
        with mock.patch.object(i, "_commit", return_value=BASE), \
                mock.patch.object(c, "resolve_candidate", return_value=candidate or self.candidate()):
            return c.plan(self.api, ROOT, BASE, 100)

    def test_plan_claims_before_dispatch_and_repeated_event_deduplicates(self):
        self.assertEqual(self.plan()["status"], "planned")
        self.assertEqual(self.plan()["status"], "idle")
        self.assertEqual(len(self.api.comments), 1)
        self.assertEqual(len(self.api.posts), 1)

    def test_writer_activity_defers_new_claims(self):
        self.api.active = [self.api.runs[200]]
        self.assertEqual(self.plan()["status"], "writer-active")
        self.assertEqual(self.api.comments, [])

    def test_failed_ci_never_schedules_a_new_test_attempt(self):
        self.api.ci_success = False
        self.assertEqual(self.plan()["status"], "idle")
        self.assertEqual(self.api.comments, [])

    def test_performance_failure_is_not_retried_even_with_green_build_ci(self):
        self.api.extra_checks = [{"name": "Compiler throughput", "head_sha": HEAD,
                                  "app": {"id": 15368}, "status": "completed", "conclusion": "failure"}]
        self.assertEqual(self.plan()["status"], "idle")
        self.assertEqual(self.api.comments, [])

    def test_admission_consumers_do_not_create_circular_prerequisites(self):
        self.api.extra_checks = [{"name": name, "head_sha": HEAD, "app": {"id": 15368},
                                  "status": "completed", "conclusion": "failure"}
                                 for name in ("Native retirement merge admission", "Main integration admission")]
        self.assertEqual(self.plan()["status"], "planned")

    def test_already_published_head_does_not_loop(self):
        candidate = {**self.candidate(), "already_current": True, "previously_published": True}
        self.assertEqual(self.plan(candidate)["status"], "idle")
        self.assertEqual(self.api.comments, [])

    def test_idle_stale_pr_does_not_fan_out_after_each_main_push(self):
        self.api.pr["auto_merge"] = None
        self.assertEqual(self.plan({**self.candidate(), "previously_published": True})["status"], "idle")

    def test_disabled_policy_makes_no_claims(self):
        self.api.policy["enabled"] = False
        self.assertEqual(self.plan()["status"], "disabled")
        self.assertEqual(self.api.comments, [])

    def test_writer_refused_candidates_are_blocked_not_fatal(self):
        # #1933: the real resolve_candidate -> gate.source_candidate path raises
        # the merge gate's IntegrationError; plan() must record it per PR.
        import native_retirement_merge_gate as gate
        self.assertIs(gate.integration, i)
        split_head, generated_head = "e" * 40, "f" * 40
        heads = {1791: HEAD, 1796: split_head, 1696: generated_head}
        ordinary_pr = self.api.pr
        pulls = [ordinary_pr]
        for number in (1796, 1696):
            pr = copy.deepcopy(ordinary_pr)
            pr["number"], pr["head"]["sha"] = number, heads[number]
            pulls.append(pr)
        classifications = {
            HEAD: i.classify_paths(["tools/native_retirement_rebind.py"]),
            split_head: i.Classification(
                "split-required",
                ("docs/native-retirement-support-v1.tsv", "tools/native_retirement_contract.py"),
                (), ("tools/native_retirement_contract.py",), ("docs/native-retirement-support-v1.tsv",)),
            generated_head: i.Classification(
                "ordinary", ("tools/native_retirement_dependency_binding.generated.h",),
                ("tools/native_retirement_dependency_binding.generated.h",), (), ()),
        }
        fixture_all = self.api.all

        def all_pages(path, **query):
            if path == "pulls":
                return copy.deepcopy(pulls)
            if path in ("issues/1796/comments", "issues/1696/comments"):
                return []
            return fixture_all(path, **query)

        def commit(repo, revision):
            prefix = "refs/remotes/origin/native-retirement-controller-"
            if revision.startswith(prefix):
                return heads[int(revision[len(prefix):])]
            return BASE if revision == "HEAD" else revision

        self.api.all = all_pages
        with mock.patch.object(i, "_git"), \
                mock.patch.object(i, "_commit", side_effect=commit), \
                mock.patch.object(i, "classify_candidate",
                                  side_effect=lambda repo, base, head: classifications[head]), \
                mock.patch.object(gate, "integration_record", return_value=((), {})), \
                mock.patch.object(gate, "clean_merge_tree", return_value="a" * 40):
            result = c.plan(self.api, ROOT, BASE, 100)
        self.assertEqual(result["status"], "planned")
        self.assertEqual(result["request"]["number"], 1791)
        statuses = {entry["number"]: entry for entry in result["observations"]}
        self.assertEqual(statuses[1791]["status"], "eligible")
        self.assertEqual(statuses[1796]["status"], "blocked")
        self.assertIn("split it into a backwards-compatible bootstrap", statuses[1796]["detail"])
        self.assertEqual(statuses[1696]["status"], "blocked")
        self.assertIn("generated artifacts were edited manually", statuses[1696]["detail"])
        self.assertEqual(len(self.api.comments), 1)

    def test_bot_catch_up_request_needs_no_prerequisite_ci(self):
        self.api.ci_success = False
        self.assertEqual(self.plan()["status"], "idle")
        catch_up = {**self.candidate(), "catch_up": True,
                    "classification_record": {"kind": "ordinary", "changed_paths": []}}
        self.assertEqual(self.plan(catch_up)["status"], "planned")


def git(repo: Path, *arguments: str) -> str:
    return subprocess.run(["git", "-c", "core.hooksPath=/dev/null", "-C", str(repo), *arguments],
                          capture_output=True, text=True, check=True).stdout.strip()


class CatchUpDetectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name) / "repo"
        subprocess.run(["git", "clone", "-q", "--shared", "--no-checkout", str(ROOT), str(self.repo)],
                       check=True, capture_output=True)
        git(self.repo, "config", "user.name", "Catch-up Test")
        git(self.repo, "config", "user.email", "test@example.invalid")
        self.fresh = self.fresh_snapshot(git(ROOT, "rev-parse", "HEAD"))

    def commit(self, parent: str, path: str, content: bytes) -> str:
        blob = subprocess.run(["git", "-C", str(self.repo), "hash-object", "-w", "--stdin"],
                              input=content, capture_output=True, check=True).stdout.decode().strip()
        index = Path(self.temporary.name) / "index"
        env = {**os.environ, "GIT_INDEX_FILE": str(index)}
        subprocess.run(["git", "-C", str(self.repo), "read-tree", parent], env=env, check=True)
        subprocess.run(["git", "-C", str(self.repo), "update-index", "--cacheinfo",
                        "100644," + blob + "," + path], env=env, check=True)
        tree = subprocess.run(["git", "-C", str(self.repo), "write-tree"], env=env,
                              capture_output=True, text=True, check=True).stdout.strip()
        return git(self.repo, "commit-tree", tree, "-p", parent, "-m", "change " + path)

    def fresh_snapshot(self, head: str) -> str:
        """Base the tests on a commit whose snapshot matches its sources.

        The checkout's own snapshot can lag its sources: after an ordinary merge
        until its catch-up lands, and on a trust transition until the writer
        publishes (its self-tests run on that candidate's commit).
        """
        import native_retirement_dependency_binding as authority
        policy_raw = c._blob(self.repo, head, authority.POLICY_PATH)
        policy = authority.parse_policy(policy_raw)

        def identity(source: str) -> tuple[int, str]:
            data = c._blob(self.repo, head, source)
            return len(data), hashlib.sha256(data).hexdigest()

        rendered, _records = authority.render_snapshot(policy_raw, policy, identity)
        fresh = head
        if rendered != c._blob(self.repo, head, authority.SNAPSHOT_PATH):
            fresh = self.commit(head, authority.SNAPSHOT_PATH, rendered)
        return fresh

    def test_snapshot_staleness_tracks_admitted_source_bytes_only(self):
        self.assertFalse(c.snapshot_stale(self.repo, self.fresh))
        source = self.commit(self.fresh, "src/buster/lib/hash.h",
                             (ROOT / "src/buster/lib/hash.h").read_bytes() + b"\n// probe\n")
        self.assertTrue(c.snapshot_stale(self.repo, source))
        unrelated = self.commit(self.fresh, "README.md", b"unrelated\n")
        self.assertFalse(c.snapshot_stale(self.repo, unrelated))

    def test_published_catch_up_stays_current_until_generated_state_changes(self):
        import native_retirement_merge_gate as gate
        empty = git(self.repo, "commit-tree", self.fresh + "^{tree}", "-p", self.fresh, "-m", "request")
        self.assertFalse(c.catch_up_admissible(self.repo, self.fresh, empty))
        header = "tools/native_retirement_dependency_binding.generated.h"
        published_tree = git(self.repo, "rev-parse",
                             self.commit(self.fresh, header, b"/* newer */\n") + "^{tree}")
        message = f"catch-up\n\n{gate.TRAILER_BASE}: {self.fresh}\n"
        published = git(self.repo, "commit-tree", published_tree, "-p", self.fresh, "-p", empty,
                        "-m", message)
        self.assertTrue(c.catch_up_admissible(self.repo, self.fresh, published))
        later = self.commit(self.fresh, "src/buster/lib/hash.h", b"changed\n")
        self.assertTrue(c.catch_up_admissible(self.repo, later, published))
        refreshed = self.commit(later, header, b"/* newest */\n")
        self.assertFalse(c.catch_up_admissible(self.repo, refreshed, published))

    def test_writer_is_requested_only_for_trust_transitions_and_needed_catch_ups(self):
        import native_retirement_merge_gate as gate
        human = {"number": 7, "state": "open", "draft": False, "user": {"login": "author", "id": 5, "type": "User"},
                 "head": {"ref": "feature", "sha": HEAD, "repo": {"full_name": REPOSITORY}},
                 "base": {"ref": "main", "repo": {"full_name": REPOSITORY}}}
        bot = copy.deepcopy(human)
        bot["user"] = copy.deepcopy(BOT)
        bot["head"]["ref"] = gate.CATCH_UP_BRANCH
        api = mock.Mock(repository=REPOSITORY)
        cases = (
            (human, ["src/buster/lib/hash.h"], None, None, False),
            (human, ["tools/native_retirement_rebind.py"], None, None, True),
            (bot, [], False, True, True),
            (bot, [], True, True, False),
            (bot, [], False, False, False),
        )
        for pr, paths, admissible, stale, expected in cases:
            with self.subTest(paths=paths, admissible=admissible, stale=stale), \
                    mock.patch.object(i, "_git"), mock.patch.object(i, "_commit", return_value=HEAD), \
                    mock.patch.object(gate, "source_candidate", return_value={"source_head": HEAD}), \
                    mock.patch.object(gate, "integration_record", return_value=((), {})), \
                    mock.patch.object(i, "classify_candidate", return_value=i.classify_paths(paths)), \
                    mock.patch.object(c, "catch_up_admissible", return_value=admissible), \
                    mock.patch.object(c, "snapshot_stale", return_value=stale):
                record = c.resolve_candidate(self.repo, BASE, pr, api)
                self.assertEqual(record["requires_writer"], expected)
                self.assertEqual(record["catch_up"], pr is bot)

    def test_only_bot_owned_catch_up_branch_is_a_catch_up_request(self):
        import native_retirement_merge_gate as gate
        pr = {"number": 7, "state": "open", "draft": False, "user": copy.deepcopy(BOT),
              "head": {"ref": gate.CATCH_UP_BRANCH, "sha": HEAD, "repo": {"full_name": REPOSITORY}},
              "base": {"ref": "main", "repo": {"full_name": REPOSITORY}}}
        self.assertTrue(c.is_catch_up_pr(pr, REPOSITORY))
        for mutate in (lambda value: value.update(user={"login": "someone", "id": 5, "type": "User"}),
                       lambda value: value["head"].update(ref="feature"),
                       lambda value: value.update(draft=True)):
            changed = copy.deepcopy(pr)
            mutate(changed)
            self.assertFalse(c.is_catch_up_pr(changed, REPOSITORY))


class CatchUpAPI:
    repository = REPOSITORY

    def __init__(self, pulls=()):
        self.main = BASE
        self.policy = {"schema": a.POLICY_SCHEMA, "repository": REPOSITORY, "enabled": True,
                       "epoch": 1, "classes": ["ordinary"], "paused_pull_requests": []}
        self.run = run_record(300, c.CATCH_UP_PATH, "push")
        self.pulls = list(pulls)
        self.calls = []
        self.token = "fixture-token"
        # Live catch-up branch value (None: absent), moved only by lease().
        self.branch = None
        # (method, path) after whose response a racing writer publishes FRESH.
        self.publish_after = None
        # Published catch-up heads: sha -> (recorded base, CI complete check runs).
        self.published = {}
        self.ahead = True
        self.writer_active = False

    def all(self, path, **query):
        if path != "pulls" or query != {"state": "open", "base": "main"}:
            raise AssertionError((path, query))
        return copy.deepcopy(self.pulls)

    def lease(self, commit, expected):
        # The writer's and the opener's branch updates are both leased pushes.
        if self.branch == expected:
            self.branch = commit
        return self.branch == commit

    def request(self, path, *, method="GET", body=None, **query):
        self.calls.append((method, path, body))
        response = self.respond(path, method, body, query)
        if self.publish_after == (method, path):
            self.publish_after = None
            self.lease(FRESH, self.branch)
        return response

    def respond(self, path, method, body, query):
        if path == "actions/runs/300":
            return copy.deepcopy(self.run)
        if path == "git/ref/heads/main":
            return {"object": {"sha": self.main}}
        if path == "contents/" + a.POLICY_PATH:
            raw = a.canonical(self.policy)
            return {"type": "file", "encoding": "base64", "content": base64.b64encode(raw).decode()}
        if path == "actions/workflows/" + Path(a.CONTROLLER_PATH).name:
            return {"path": a.CONTROLLER_PATH, "state": "active"}
        if path.startswith("git/commits/"):
            sha = path[len("git/commits/"):]
            commit = {"parents": [{"sha": BASE}], "message": "Request native-retirement catch-up\n"}
            if sha in self.published:
                commit = {"parents": [{"sha": BASE}, {"sha": sha}],
                          "message": "Publish\n\nNative-retirement-base: " + self.published[sha][0] + "\n"}
            return commit
        if path.startswith("commits/") and path.endswith("/check-runs"):
            sha = path[len("commits/"):-len("/check-runs")]
            return {"check_runs": [{"head_sha": sha, "app": {"id": 15368}, "name": "CI complete", **check}
                                   for check in self.published.get(sha, ("", []))[1]]}
        if path.startswith("compare/"):
            return {"status": "ahead" if self.ahead else "behind"}
        if path == "actions/workflows/" + Path(a.WRITER_PATH).name + "/runs":
            active = self.writer_active and query.get("status") == "in_progress"
            return {"workflow_runs": [{"id": 400}] if active else []}
        if method == "GET" and path.startswith("pulls/"):
            live = copy.deepcopy(next(pull for pull in self.pulls if path == "pulls/" + str(pull["number"])))
            live["head"]["sha"] = self.branch
            return live
        if path == "git/ref/heads/native-retirement/catch-up":
            if self.branch is None:
                raise urllib.error.HTTPError(path, 404, "Not Found", {}, None)
            return {"object": {"sha": self.branch}}
        if method == "POST" and path.startswith("issues/") and path.endswith("/comments"):
            return {"id": 7}
        if method in ("POST", "PATCH") and path.startswith("pulls"):
            return {"number": 1900, "node_id": "PR_node"} if path == "pulls" else {}
        raise AssertionError((method, path, body, query))


FRESH = "a" * 40
REQUEST = "f" * 40


class CatchUpOpenerTests(unittest.TestCase):
    def run_catch_up(self, api, stale):
        with mock.patch.object(i, "_commit", return_value=BASE), \
                mock.patch.object(c, "snapshot_stale", return_value=stale), \
                mock.patch.object(c, "catch_up_commit", return_value=REQUEST), \
                mock.patch.object(c, "lease_catch_up_branch",
                                  side_effect=lambda repo, token, commit, expected:
                                  api.lease(commit, expected)):
            report = c.catch_up(api, ROOT, BASE, 300)
        return report

    def failed_api(self):
        api = CatchUpAPI([self.catch_up_pr()])
        api.branch = HEAD
        api.published[HEAD] = (MOVED, [{"status": "completed", "conclusion": "failure"}])
        return api

    def catch_up_pr(self, number=1899):
        import native_retirement_merge_gate as gate
        return {"number": number, "state": "open", "draft": False, "user": copy.deepcopy(BOT),
                "head": {"ref": gate.CATCH_UP_BRANCH, "sha": HEAD, "repo": {"full_name": REPOSITORY}},
                "base": {"ref": "main", "repo": {"full_name": REPOSITORY}}}

    def test_stale_main_opens_one_empty_request_without_auto_merge(self):
        for branch in (None, "9" * 40):
            with self.subTest(branch=branch):
                api = CatchUpAPI()
                api.branch = branch
                report = self.run_catch_up(api, True)
                self.assertEqual(report, {"status": "opened", "pull_request": 1900, "head": REQUEST})
                self.assertEqual(api.branch, REQUEST)
                self.assertEqual([call[:2] for call in api.calls if call[0] != "GET"], [("POST", "pulls")])
                # A GITHUB_TOKEN enqueue would start no merge_group CI; the
                # writer enables auto-merge with its publication credential.
                self.assertFalse(hasattr(c, "AUTO_MERGE_MUTATION"))

    def test_open_request_is_reused_and_fresh_main_retires_it(self):
        api = CatchUpAPI([self.catch_up_pr()])
        api.branch = HEAD
        report = self.run_catch_up(api, True)
        self.assertEqual(report, {"status": "pending", "pull_requests": [1899]})
        self.assertFalse([call for call in api.calls if call[0] != "GET"])
        report = self.run_catch_up(api, False)
        self.assertEqual(report, {"status": "current", "closed": [1899]})
        self.assertIn(("PATCH", "pulls/1899", {"state": "closed"}), api.calls)

    def test_failed_published_catch_up_is_replaced_once_main_advances(self):
        # #3271: a published catch-up whose exact head failed CI complete sat
        # open forever. Once main advances past its recorded base, the opener
        # closes it with an explanation and opens one fresh request.
        api = self.failed_api()
        report = self.run_catch_up(api, True)
        self.assertEqual(report, {"status": "opened", "pull_request": 1900, "head": REQUEST,
                                  "replaced": [1899]})
        writes = [call[:2] for call in api.calls if call[0] != "GET"]
        self.assertEqual(writes, [("PATCH", "pulls/1899"), ("POST", "pulls"),
                                  ("POST", "issues/1899/comments")])
        self.assertEqual(api.branch, REQUEST)
        self.assertIn(("GET", "compare/" + MOVED + "..." + BASE, None), api.calls)

    def test_writer_publication_at_any_point_is_never_closed_or_overwritten(self):
        # Review of #3363: the opener and writer have separate concurrency
        # groups, so a writer may publish after any read the opener makes.
        # Both move the branch only by a leased push, so the opener replaces
        # exactly the failed head or nothing: a publication before the live
        # read leaves the PR alone, and one after it, including after the
        # close and immediately before the replacement, refuses the lease and
        # reopens the PR at the fresh head.
        writer = "actions/workflows/" + Path(a.WRITER_PATH).name + "/runs"
        before = []
        after = [("PATCH", "pulls/1899", {"state": "closed"}),
                 ("PATCH", "pulls/1899", {"state": "open"})]
        cases = {
            "during the scan": (("GET", "compare/" + MOVED + "..." + BASE), before),
            "after the writer check": (("GET", writer), before),
            "after the final live read": (("GET", "pulls/1899"), after),
            "after the close": (("PATCH", "pulls/1899"), after),
        }
        for name, (trigger, expected) in cases.items():
            with self.subTest(name):
                api = self.failed_api()
                api.publish_after = trigger
                report = self.run_catch_up(api, True)
                self.assertEqual(api.branch, FRESH)
                self.assertEqual([call for call in api.calls if call[0] != "GET"], expected)
                self.assertNotIn(("POST", "pulls"), [call[:2] for call in api.calls])
                self.assertEqual(report["pull_requests" if not expected else "kept"], [1899])

    def test_active_writer_defers_failed_catch_up_retirement(self):
        api = self.failed_api()
        api.writer_active = True
        self.assertEqual(self.run_catch_up(api, True), {"status": "pending", "pull_requests": [1899]})
        self.assertFalse([call for call in api.calls if call[0] != "GET"])
        self.assertEqual(api.branch, HEAD)

    def test_failed_catch_up_on_superseded_main_run_writes_nothing(self):
        # A stale-main exit must precede every write (main-push-maintenance.md).
        api = self.failed_api()
        original = api.request

        def moving(path, **kwargs):
            if path == "git/ref/heads/main":
                api.calls.append(("GET", path, None))
                return {"object": {"sha": "d" * 40}}
            return original(path, **kwargs)
        api.request = moving
        with self.assertRaises(a.AutomationMoved):
            self.run_catch_up(api, True)
        self.assertFalse([call for call in api.calls if call[0] != "GET"])

    def test_failed_catch_up_is_not_rebuilt_without_new_main(self):
        # Identical inputs would reproduce a deterministic failure: the same
        # main revision, a newer recorded base, pending or passing CI and an
        # unpublished empty head all leave the request pending.
        cases = {
            "same main": (BASE, "completed", "failure", True),
            "recorded base newer than this run": (MOVED, "completed", "failure", False),
            "pending CI": (MOVED, "in_progress", None, True),
            "passing CI": (MOVED, "completed", "success", True),
            "unpublished": (None, "completed", "failure", True),
        }
        for name, (recorded, status, conclusion, ahead) in cases.items():
            with self.subTest(name):
                api = CatchUpAPI([self.catch_up_pr()])
                api.branch = HEAD
                api.ahead = ahead
                if recorded is not None:
                    api.published[HEAD] = (recorded, [{"status": status, "conclusion": conclusion}])
                report = self.run_catch_up(api, True)
                self.assertEqual(report, {"status": "pending", "pull_requests": [1899]})
                self.assertFalse([call for call in api.calls if call[0] != "GET"])

    def test_replacement_failing_again_waits_for_the_next_main_revision(self):
        # The replacement records the main it was built for, so a
        # deterministic failure costs at most one writer run per main revision.
        replacement = self.catch_up_pr(1900)
        api = CatchUpAPI([replacement])
        api.branch = HEAD
        api.published[HEAD] = (BASE, [{"status": "completed", "conclusion": "failure"}])
        self.assertEqual(self.run_catch_up(api, True), {"status": "pending", "pull_requests": [1900]})
        api.published[HEAD] = (MOVED, [{"status": "completed", "conclusion": "failure"}])
        self.assertEqual(self.run_catch_up(api, True)["replaced"], [1900])

    def test_human_pr_on_catch_up_branch_is_ignored(self):
        human = self.catch_up_pr()
        human["user"] = {"login": "someone", "id": 5, "type": "User"}
        api = CatchUpAPI([human])
        report = self.run_catch_up(api, False)
        self.assertEqual(report, {"status": "current", "closed": []})

    def test_disabled_grant_moved_main_or_foreign_run_open_nothing(self):
        api = CatchUpAPI()
        api.policy["enabled"] = False
        self.assertEqual(self.run_catch_up(api, True), {"status": "disabled"})
        api = CatchUpAPI()
        api.run["path"] = a.CONTROLLER_PATH
        with self.assertRaises(a.AutomationError):
            self.run_catch_up(api, True)
        api = CatchUpAPI()
        original = api.request

        def moving(path, **kwargs):
            if path == "git/ref/heads/main" and any(call[1] == "actions/workflows/" +
                                                    Path(a.CONTROLLER_PATH).name for call in api.calls):
                api.calls.append(("GET", path, None))
                return {"object": {"sha": "e" * 40}}
            return original(path, **kwargs)
        api.request = moving
        with self.assertRaises(a.AutomationMoved):
            self.run_catch_up(api, True)
        self.assertFalse([call for call in api.calls if call[0] == "POST"])


class CatchUpLeaseTests(unittest.TestCase):
    """The branch compare-and-swap against a real Git remote, no network."""

    def git(self, repo, *arguments):
        return subprocess.run(["git", "-C", os.fspath(repo), *arguments], check=True,
                              capture_output=True, text=True).stdout.strip()

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.remote = root / "remote.git"
        self.work = root / "work"
        subprocess.run(["git", "init", "-q", "--bare", os.fspath(self.remote)], check=True)
        subprocess.run(["git", "init", "-q", os.fspath(self.work)], check=True)
        (self.work / "file").write_text("main\n")
        self.git(self.work, "add", "file")
        self.git(self.work, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "main")
        self.git(self.work, "remote", "add", "origin", os.fspath(self.remote))
        self.base = self.git(self.work, "rev-parse", "HEAD")

    def branch(self):
        import native_retirement_merge_gate as gate
        line = self.git(self.remote, "for-each-ref", "--format=%(objectname)",
                        "refs/heads/" + gate.CATCH_UP_BRANCH)
        return line or None

    def test_request_commit_is_empty_on_main_and_lease_is_compare_and_swap(self):
        import native_retirement_merge_gate as gate
        commit = c.catch_up_commit(self.work, self.base)
        self.assertEqual(self.git(self.work, "rev-parse", commit + "^{tree}"),
                         self.git(self.work, "rev-parse", self.base + "^{tree}"))
        self.assertEqual(self.git(self.work, "rev-list", "--parents", "-n", "1", commit).split()[1:],
                         [self.base])
        self.assertIn("github-actions[bot]", self.git(self.work, "log", "-1", "--format=%an %cn", commit))
        # Absent branch: only an absent expectation creates it.
        self.assertFalse(c.lease_catch_up_branch(self.work, "t", commit, self.base))
        self.assertIsNone(self.branch())
        self.assertTrue(c.lease_catch_up_branch(self.work, "t", commit, None))
        self.assertEqual(self.branch(), commit)
        # A writer publishes over the failed head after the opener read it:
        # the opener's lease on that failed head is refused and keeps it.
        published = self.git(self.work, "commit-tree", self.base + "^{tree}", "-p", self.base,
                             "-p", commit, "-m", "published")
        self.git(self.work, "push", "-q", "--force-with-lease=refs/heads/" + gate.CATCH_UP_BRANCH +
                 ":" + commit, "origin", published + ":refs/heads/" + gate.CATCH_UP_BRANCH)
        replacement = c.catch_up_commit(self.work, self.base)
        self.assertFalse(c.lease_catch_up_branch(self.work, "t", replacement, commit))
        self.assertEqual(self.branch(), published)
        # And the converse: once the opener replaced it, the writer's lease
        # on the head it was requested for is refused in turn.
        self.assertTrue(c.lease_catch_up_branch(self.work, "t", replacement, published))
        late = subprocess.run(["git", "-C", os.fspath(self.work), "push", "-q",
                               "--force-with-lease=refs/heads/" + gate.CATCH_UP_BRANCH + ":" + published,
                               "origin", published + ":refs/heads/" + gate.CATCH_UP_BRANCH],
                              capture_output=True, check=False)
        self.assertNotEqual(late.returncode, 0)
        self.assertEqual(self.branch(), replacement)


MOVED = "e" * 40


class SupersededAPI(CatchUpAPI):
    """Catch-up fixture plus a push-run inventory for successor proofs (#2003)."""
    def __init__(self):
        super().__init__()
        self.main = MOVED
        self.workflow_runs = {name: [] for name in c.SUPERSEDABLE.values()}
        self.inventory_error = None

    def push_run(self, workflow, run_id, number, head):
        self.workflow_runs[workflow].append({
            "id": run_id, "run_number": number, "event": "push", "head_branch": "main",
            "status": "queued", "head_sha": head})

    def request(self, path, *, method="GET", body=None, **query):
        for workflow, runs in self.workflow_runs.items():
            if path == "actions/workflows/" + workflow + "/runs":
                if self.inventory_error is not None:
                    raise self.inventory_error
                if query.get("event") != "push" or query.get("branch") != "main":
                    raise AssertionError(query)
                return {"workflow_runs": copy.deepcopy(runs)}
        return super().request(path, method=method, body=body, **query)


class SupersededPushTests(unittest.TestCase):
    """Main-push catch-up/plan runs are green only when provably superseded."""

    def run_main(self, api, command, *, event="push", raised=None):
        with tempfile.TemporaryDirectory() as temporary:
            summary = Path(temporary) / "summary.md"
            environment = {"GITHUB_REPOSITORY": REPOSITORY, "GH_TOKEN": "fixture",
                           "GITHUB_WORKFLOW_SHA": BASE, "GITHUB_RUN_ID": "300",
                           "GITHUB_RUN_NUMBER": "5", "GITHUB_EVENT_NAME": event,
                           "GITHUB_STEP_SUMMARY": str(summary),
                           "GITHUB_OUTPUT": str(Path(temporary) / "output")}

            def failing(*_args):
                if raised is not None:
                    raise raised
                # The production stale-main exit, not a synthetic one.
                return a.read_policy(api, BASE)
            argv = [command, "--repo-root", str(ROOT)]
            if command == "plan":
                argv += ["--request", str(Path(temporary) / "request.json")]
            with mock.patch.dict(os.environ, environment), \
                    mock.patch.object(i, "GitHub", return_value=api), \
                    mock.patch.object(c, "catch_up", side_effect=failing), \
                    mock.patch.object(c, "plan", side_effect=failing), \
                    mock.patch("main_push_maintenance.SUCCESSOR_WAIT_SECONDS", 0), \
                    mock.patch("sys.stdout"), mock.patch("sys.stderr"):
                status = c.main(argv)
            text = summary.read_text() if summary.exists() else ""
        return status, text

    def test_superseded_catch_up_and_plan_with_successor_are_green_no_ops(self):
        for command in ("catch-up", "plan"):
            with self.subTest(command=command):
                api = SupersededAPI()
                api.push_run(c.SUPERSEDABLE[command], 301, 6, MOVED)
                status, summary = self.run_main(api, command)
                self.assertEqual(status, 0)
                self.assertIn('"status": "superseded"', summary)
                self.assertIn('"current_main": "' + MOVED + '"', summary)
                self.assertFalse([call for call in api.calls if call[0] != "GET"])

    def test_failure_on_newest_main_stays_red(self):
        api = SupersededAPI()
        api.main = BASE
        api.push_run(c.SUPERSEDABLE["catch-up"], 301, 6, BASE)
        moved = a.AutomationMoved("main moved before automation authorization", 75)
        self.assertEqual(self.run_main(api, "catch-up", raised=moved), (1, ""))

    def test_moved_main_without_exact_successor_run_stays_red(self):
        cases = {
            "no successor": [],
            "older run for live main": [("catch-up", 299, 4, MOVED)],
            "successor for another sha": [("catch-up", 301, 6, "d" * 40)],
            "successor in the other workflow": [("plan", 301, 6, MOVED)],
        }
        for label, runs in cases.items():
            with self.subTest(label):
                api = SupersededAPI()
                for command, run_id, number, head in runs:
                    api.push_run(c.SUPERSEDABLE[command], run_id, number, head)
                self.assertEqual(self.run_main(api, "catch-up"), (1, ""))

    def test_api_error_policy_violation_other_event_or_pr_movement_stay_red(self):
        api = SupersededAPI()
        api.push_run(c.SUPERSEDABLE["catch-up"], 301, 6, MOVED)
        api.inventory_error = urllib.error.HTTPError("runs", 502, "Bad Gateway", {}, None)
        self.assertEqual(self.run_main(api, "catch-up"), (1, ""))
        for event in ("schedule", "workflow_dispatch", "workflow_run"):
            with self.subTest(event=event):
                api = SupersededAPI()
                api.push_run(c.SUPERSEDABLE["plan"], 301, 6, MOVED)
                self.assertEqual(self.run_main(api, "plan", event=event), (1, ""))
        for error in (a.AutomationError("standing automation policy is disabled"),
                      a.AutomationMoved("selected PR changed during reconciliation", 76)):
            with self.subTest(error=str(error)):
                api = SupersededAPI()
                api.push_run(c.SUPERSEDABLE["plan"], 301, 6, MOVED)
                self.assertEqual(self.run_main(api, "plan", raised=error), (1, ""))

    def test_dispatch_is_never_superseded(self):
        self.assertEqual(set(c.SUPERSEDABLE), {"catch-up", "plan"})
        self.assertEqual(c.SUPERSEDABLE["catch-up"], Path(c.CATCH_UP_PATH).name)
        self.assertEqual(c.SUPERSEDABLE["plan"], Path(a.CONTROLLER_PATH).name)


class WorkflowTests(unittest.TestCase):
    def test_writer_keeps_one_privileged_publisher_and_no_dispatch_authority(self):
        text = (ROOT / a.WRITER_PATH).read_text()
        self.assertEqual(text.count("contents: write"), 1)
        self.assertNotIn("actions: write", text)
        self.assertIn("group: native-retirement-integration-writer", text)
        validate = text.split("\n  validate:", 1)[1].split("\n  publish:", 1)[0]
        self.assertNotIn("secrets.", validate)
        self.assertNotIn("contents: write", validate)
        self.assertIn("--automation-publication", text)
        self.assertIn("Automatic integration requires NATIVE_RETIREMENT_PUBLICATION_TOKEN", text)

    def test_writer_queues_a_published_catch_up_with_the_publication_credential(self):
        # A GITHUB_TOKEN enqueue starts no merge_group workflows, so only the
        # publication credential may enable auto-merge, and only after the push.
        text = (ROOT / a.WRITER_PATH).read_text()
        publication = text.split("name: Publish and attest exact integration head", 1)[1]
        publication = publication.split("\n      - name:", 1)[0]
        self.assertIn("GITHUB_TOKEN: ${{ secrets.NATIVE_RETIREMENT_PUBLICATION_TOKEN || github.token }}",
                      publication)
        enable = publication.index("enablePullRequestAutoMerge")
        self.assertLess(publication.index("--force-with-lease="), enable)
        self.assertLess(publication.index("disablePullRequestAutoMerge"), enable)
        self.assertIn('GH_TOKEN="$GITHUB_TOKEN" gh api graphql', publication)
        self.assertIn("native-retirement/catch-up", publication)
        self.assertEqual(text.count("enablePullRequestAutoMerge"), 1)
        opener = (ROOT / c.CATCH_UP_PATH).read_text() + (ROOT / "tools/native_retirement_controller.py").read_text()
        self.assertNotIn("enablePullRequestAutoMerge", opener)

    def test_controller_has_no_candidate_execution_or_publication_credentials(self):
        text = (ROOT / a.CONTROLLER_PATH).read_text()
        self.assertIn("ref: ${{ github.workflow_sha }}", text)
        self.assertNotIn("pull_request_target:", text)
        self.assertNotIn("contents: write", text)
        # Its ledger comments land on pull requests, which need this scope.
        self.assertIn("      pull-requests: write\n", text)
        self.assertNotIn("secrets.", text)
        self.assertNotIn("self-hosted", text)
        self.assertLess(text.index("Seal the trusted request"), text.index("Dispatch the existing single writer once"))
        self.assertIn("Native retirement catch-up]", text)

    def test_sealed_request_artifact_holds_only_request_json(self):
        # The writer and dispatch accept exactly one request.json (#3327).
        text = (ROOT / a.CONTROLLER_PATH).read_text()
        seal = text.split("- name: Seal the trusted request before dispatch\n", 1)[1]
        seal = seal.split("\n      - name:", 1)[0]
        self.assertIn("name: native-retirement-automation-request-${{ github.run_id }}", seal)
        self.assertIn("\n          path: ${{ runner.temp }}/native-retirement-request/request.json\n", seal)
        self.assertNotIn("machine-specifications", seal)
        self.assertEqual(text.count("native-retirement-automation-request-"), 1)
        self.assertNotIn("retention-directory: ${{ runner.temp }}/native-retirement-request", text)
        self.assertLess(text.index("Seal the trusted request"), text.index("mode: retain"))

    def test_catch_up_opener_is_trusted_main_only_and_cannot_publish_or_dispatch(self):
        text = (ROOT / c.CATCH_UP_PATH).read_text()
        self.assertIn("ref: ${{ github.workflow_sha }}", text)
        self.assertIn("persist-credentials: false", text)
        self.assertIn("\npermissions:\n  contents: read\n", text)
        self.assertIn("github.run_attempt == 1", text)
        self.assertIn("vars.GH_ACTIONS_CI_ENABLED == 'true'", text)
        for forbidden in ("pull_request", "secrets.", "self-hosted", "actions: write",
                          "statuses: write", "workflow_run", "native_retirement_rebind.py"):
            self.assertNotIn(forbidden, text)
        self.assertEqual(text.count("contents: write"), 1)
        self.assertIn("checks: read", text)
        self.assertNotIn("checks: write", text)
        self.assertIn("native_retirement_controller.py catch-up", text)
        self.assertIn("name: Native retirement catch-up\n", text)
        for module in (i.TRUST_IMPLEMENTATION_PATHS,):
            self.assertIn(c.CATCH_UP_PATH, module)

    def test_scope_accepts_empty_catch_up_classification_but_not_a_missing_one(self):
        policy = {"classes": ["ordinary"], "paused_pull_requests": []}
        a.require_scope(policy, 1, {"kind": "ordinary", "changed_paths": []})
        for classification in ({"kind": "ordinary"}, {"kind": "ordinary", "changed_paths": None},
                               {"kind": "ordinary", "changed_paths": [1]}):
            with self.assertRaises(a.AutomationError):
                a.require_scope(policy, 1, classification)

    def test_checked_in_policy_has_explicit_valid_activation_state(self):
        policy = a.decode((ROOT / a.POLICY_PATH).read_bytes())
        a.validate_policy(policy, REPOSITORY)
        self.assertIs(type(policy["enabled"]), bool)

    def test_actual_outcome_script_never_labels_validation_failure_superseded(self):
        text = (ROOT / a.WRITER_PATH).read_text().split("  automation_outcome:", 1)[1]
        script = text.split("python3 - <<'PY'\n", 1)[1].split("          PY\n", 1)[0]
        script = "\n".join(line[10:] for line in script.splitlines()) + "\n"
        cases = [
            ({"prepare": {"result": "failure", "outputs": {"superseded": "true"}},
              "validate": {"result": "skipped"}, "publish": {"result": "skipped"}}, "superseded"),
            ({"prepare": {"result": "success"}, "validate": {"result": "failure"},
              "publish": {"result": "skipped", "outputs": {"superseded": "true"}}}, "blocked"),
            ({"prepare": {"result": "success"}, "validate": {"result": "success"},
              "publish": {"result": "failure", "outputs": {"superseded": "true"}}}, "superseded"),
            ({"prepare": {"result": "success"}, "validate": {"result": "success"},
              "publish": {"result": "success"}}, "published"),
            ({"prepare": {"result": "cancelled", "outputs": {"superseded": "true"}},
              "validate": {"result": "skipped"}, "publish": {"result": "skipped"}}, "blocked"),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "output"
            for needs, expected in cases:
                with self.subTest(expected=expected, needs=needs):
                    output.write_text("")
                    subprocess.run([os.sys.executable, "-c", script], check=True,
                                   env={**os.environ, "NEEDS_JSON": json.dumps(needs), "GITHUB_OUTPUT": str(output)})
                    self.assertEqual(output.read_text(), "state=" + expected + "\n")


if __name__ == "__main__":
    unittest.main()
