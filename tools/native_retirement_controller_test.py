#!/usr/bin/env python3
"""Network-free controller and workflow regressions for #1791.

Reuse the bounded fake GitHub API from the authorization suite; no network or
candidate execution occurs while exercising request lifecycle decisions.
"""
import base64
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import urllib.error

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
        self.branch_exists = False

    def all(self, path, **query):
        if path != "pulls" or query != {"state": "open", "base": "main"}:
            raise AssertionError((path, query))
        return copy.deepcopy(self.pulls)

    def request(self, path, *, method="GET", body=None, **query):
        self.calls.append((method, path, body))
        if path == "actions/runs/300":
            return copy.deepcopy(self.run)
        if path == "git/ref/heads/main":
            return {"object": {"sha": self.main}}
        if path == "contents/" + a.POLICY_PATH:
            raw = a.canonical(self.policy)
            return {"type": "file", "encoding": "base64", "content": base64.b64encode(raw).decode()}
        if path == "actions/workflows/" + Path(a.CONTROLLER_PATH).name:
            return {"path": a.CONTROLLER_PATH, "state": "active"}
        if path == "git/commits/" + BASE:
            return {"tree": {"sha": "e" * 40}}
        if path == "git/commits" and method == "POST":
            return {"sha": "f" * 40}
        if path == "git/ref/heads/native-retirement/catch-up":
            if not self.branch_exists:
                raise urllib.error.HTTPError(path, 404, "Not Found", {}, None)
            return {"object": {"sha": "9" * 40}}
        if method in ("POST", "PATCH") and path.startswith(("git/refs", "pulls")):
            return {"number": 1900, "node_id": "PR_node"} if path == "pulls" else {}
        raise AssertionError((method, path, body, query))


class CatchUpOpenerTests(unittest.TestCase):
    def run_catch_up(self, api, stale):
        with mock.patch.object(i, "_commit", return_value=BASE), \
                mock.patch.object(c, "snapshot_stale", return_value=stale):
            report = c.catch_up(api, ROOT, BASE, 300)
        return report

    def catch_up_pr(self, number=1899):
        import native_retirement_merge_gate as gate
        return {"number": number, "state": "open", "draft": False, "user": copy.deepcopy(BOT),
                "head": {"ref": gate.CATCH_UP_BRANCH, "sha": HEAD, "repo": {"full_name": REPOSITORY}},
                "base": {"ref": "main", "repo": {"full_name": REPOSITORY}}}

    def test_stale_main_opens_one_empty_request_without_auto_merge(self):
        for exists in (False, True):
            with self.subTest(branch_exists=exists):
                api = CatchUpAPI()
                api.branch_exists = exists
                report = self.run_catch_up(api, True)
                self.assertEqual(report, {"status": "opened", "pull_request": 1900, "head": "f" * 40})
                commit = [call for call in api.calls if call[:2] == ("POST", "git/commits")]
                self.assertEqual(commit[0][2]["tree"], "e" * 40)
                self.assertEqual(commit[0][2]["parents"], [BASE])
                if exists:
                    self.assertIn(("PATCH", "git/refs/heads/native-retirement/catch-up",
                                   {"sha": "f" * 40, "force": True}), api.calls)
                else:
                    self.assertIn(("POST", "git/refs", {"ref": "refs/heads/native-retirement/catch-up",
                                                        "sha": "f" * 40}), api.calls)
                # A GITHUB_TOKEN enqueue would start no merge_group CI; the
                # writer enables auto-merge with its publication credential.
                self.assertFalse(hasattr(c, "AUTO_MERGE_MUTATION"))

    def test_open_request_is_reused_and_fresh_main_retires_it(self):
        api = CatchUpAPI([self.catch_up_pr()])
        report = self.run_catch_up(api, True)
        self.assertEqual(report, {"status": "pending", "pull_requests": [1899]})
        self.assertFalse([call for call in api.calls if call[0] != "GET"])
        report = self.run_catch_up(api, False)
        self.assertEqual(report, {"status": "current", "closed": [1899]})
        self.assertIn(("PATCH", "pulls/1899", {"state": "closed"}), api.calls)

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
