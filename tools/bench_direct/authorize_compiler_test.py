#!/usr/bin/env python3
"""Offline checks of the main-commit compiler comparison authorization (#2752)."""

from __future__ import annotations

import unittest
import urllib.error
from pathlib import Path
from unittest import mock
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import authorize_compiler  # noqa: E402

REPOSITORY = "buster14a/buster"
HEAD, BASE, PULL_HEAD = "a" * 40, "b" * 40, "c" * 40
HEAD_TREE, BASE_TREE = "d" * 40, "e" * 40
OLDER, OLDER_TREE = "f" * 40, "1" * 40
OTHER = {"login": "github-actions[bot]", "id": 41898282}


def records() -> dict:
    return {
        "run": {"id": 91, "path": authorize_compiler.REQUEST_WORKFLOW, "event": "push", "head_branch": "main",
                "status": "completed", "conclusion": "success", "head_sha": HEAD,
                "repository": {"full_name": REPOSITORY}, "head_repository": {"full_name": REPOSITORY},
                # Main is trusted code: the pusher is not the authority.
                "actor": dict(OTHER), "triggering_actor": dict(OTHER)},
        "commit": {"sha": HEAD, "parents": [{"sha": BASE}, {"sha": PULL_HEAD}], "commit": {"tree": {"sha": HEAD_TREE}}},
        "base_commit": {"sha": BASE, "commit": {"tree": {"sha": BASE_TREE}}},
        "on_main": {"status": "identical"},
        "pulls": [{"number": 2774, "merge_commit_sha": HEAD, "user": dict(OTHER)}],
    }


class AuthorizeCompilerTest(unittest.TestCase):
    def check(self, value: dict, chain: list[str] | None = None) -> tuple[list[str], dict]:
        return authorize_compiler.verify(REPOSITORY, 91, HEAD, value["run"], value["commit"], value["base_commit"],
                                         value["on_main"], value["pulls"], chain)

    def test_landed_merge_commit_is_measured_against_its_first_parent(self) -> None:
        # Bot-authored pull requests are measured once they land on main.
        self.assertEqual(self.check(records()), ([], {"base": BASE, "base_tree": BASE_TREE, "head_tree": HEAD_TREE,
                                                      "pull": "2774", "pull_head": PULL_HEAD,
                                                      "first_parent": BASE, "range": "1"}))

    def test_range_baseline_must_be_on_the_first_parent_chain(self) -> None:
        value = records()
        value["base_commit"] = {"sha": OLDER, "commit": {"tree": {"sha": OLDER_TREE}}}
        failures, result = self.check(value, [BASE, OLDER])
        self.assertEqual(failures, [])
        self.assertEqual((result["base"], result["base_tree"], result["first_parent"], result["range"]),
                         (OLDER, OLDER_TREE, BASE, "2"))
        # Off the chain (the pull request side, or beyond the bounded chain), or a chain that does not
        # start at the first parent, is refused.
        for chain in (None, [BASE], [PULL_HEAD, OLDER], [OLDER]):
            with self.subTest(chain=chain):
                failures, result = self.check(value, chain)
                self.assertIn("baseline commit is on the first-parent chain", failures)
                self.assertEqual(result, {})

    def test_choose_base_takes_the_nearest_valid_measurement(self) -> None:
        def row(sha: str, conclusion: str = "success", status: str = "completed", app: int = 15368) -> dict:
            return {"id": 1, "name": "9700X compiler benchmark", "head_sha": sha, "status": status,
                    "conclusion": conclusion, "app": {"id": app},
                    "external_id": f"buster-9700x-compiler-main-v1:{sha}:91.1:1"}
        chain = [BASE, PULL_HEAD, OLDER]
        self.assertEqual(authorize_compiler.choose_base(chain, {BASE: [row(BASE)], OLDER: [row(OLDER)]}), BASE)
        # Not measured, failed, still open or foreign checks do not count; the nearest valid one does.
        checks = {BASE: [row(BASE, "skipped"), row(BASE, "failure"), row(BASE, None, "in_progress"),
                         row(BASE, app=1)],
                  PULL_HEAD: [dict(row(PULL_HEAD), head_sha=OLDER)], OLDER: [row(OLDER)]}
        self.assertEqual(authorize_compiler.choose_base(chain, checks), OLDER)
        # A pre-#2803 single check counts; nothing measured falls back to the first parent.
        legacy = dict(row(PULL_HEAD), external_id=f"buster-9700x-compiler-main-v1:{PULL_HEAD}")
        self.assertEqual(authorize_compiler.choose_base(chain, {PULL_HEAD: [legacy]}), PULL_HEAD)
        self.assertEqual(authorize_compiler.choose_base(chain, {}), BASE)

    def test_direct_push_and_older_main_commit(self) -> None:
        value = records()
        value["commit"]["parents"] = [{"sha": BASE}]
        value["on_main"] = {"status": "ahead"}
        value["pulls"] = []
        self.assertEqual(self.check(value)[1], {"base": BASE, "base_tree": BASE_TREE, "head_tree": HEAD_TREE,
                                                "pull": "0", "pull_head": HEAD, "first_parent": BASE, "range": "1"})

    def test_every_record_field_is_required(self) -> None:
        changes = (
            ("run", "id", 92), ("run", "path", ".github/workflows/9700x-direct-request.yml"),
            ("run", "event", "merge_group"), ("run", "event", "pull_request"), ("run", "head_branch", "feature"),
            ("run", "conclusion", "failure"), ("run", "status", "queued"), ("run", "head_sha", "9" * 40),
            ("run", "repository", {"full_name": "fork/buster"}), ("run", "head_repository", {"full_name": "fork/buster"}),
            ("commit", "parents", []), ("commit", "parents", [{"sha": BASE}] * 3), ("commit", "sha", "9" * 40),
            ("commit", "commit", {}), ("base_commit", "sha", "9" * 40), ("base_commit", "commit", {"tree": {"sha": "x"}}),
            ("on_main", "status", "diverged"), ("on_main", "status", "behind"),
        )
        for record, key, value in changes:
            with self.subTest(record=record, key=key, value=value):
                changed = records()
                changed[record][key] = value
                failures, result = self.check(changed)
                self.assertTrue(failures)
                self.assertEqual(result, {})

    def test_malformed_records_fail_closed(self) -> None:
        for record in ("run", "commit", "base_commit", "on_main"):
            for value in (None, [], "text", {"message": "Not Found"}):
                with self.subTest(record=record, value=value):
                    changed = records()
                    changed[record] = value
                    failures, result = self.check(changed)
                    self.assertTrue(failures)
                    self.assertEqual(result, {})
        # An unreadable pull request list only loses the attribution.
        changed = records()
        changed["pulls"] = {"message": "Not Found"}
        self.assertEqual(self.check(changed)[1]["pull"], "0")

    def test_main_chain_reads_the_listing_and_stops_at_the_first_measurement(self) -> None:
        listing = [{"sha": HEAD, "parents": [{"sha": BASE}, {"sha": PULL_HEAD}]},
                   {"sha": PULL_HEAD, "parents": [{"sha": OLDER}]},
                   {"sha": BASE, "parents": [{"sha": OLDER}]}, {"sha": OLDER, "parents": [{"sha": "2" * 40}]}]
        measured = {"id": 1, "name": "9700X compiler benchmark", "head_sha": OLDER, "status": "completed",
                    "conclusion": "success", "app": {"id": 15368},
                    "external_id": f"buster-9700x-compiler-main-v1:{OLDER}:90.1:1"}
        reads: list[str] = []

        def fetch(path: str, token: str) -> object:
            reads.append(path)
            if path.startswith("/repos/x/y/commits?"):
                return listing
            return {"check_runs": [measured] if f"/commits/{OLDER}/" in path else []}
        commit = records()["commit"]
        with mock.patch.object(authorize_compiler, "fetch", side_effect=fetch):
            self.assertEqual(authorize_compiler.main_chain("/repos/x/y", "t", HEAD, commit),
                             ([BASE, OLDER, "2" * 40], OLDER))
        self.assertEqual(len([path for path in reads if "/check-runs?" in path]), 2)

        def broken(path: str, token: str) -> object:
            raise urllib.error.URLError("unreachable")
        with mock.patch.object(authorize_compiler, "fetch", side_effect=broken), mock.patch("sys.stderr"):
            self.assertEqual(authorize_compiler.main_chain("/repos/x/y", "t", HEAD, commit), ([BASE], BASE))
        self.assertEqual(authorize_compiler.main_chain("/repos/x/y", "t", HEAD, {"parents": []}), ([], ""))



class MainRouteApiTests(unittest.TestCase):
    """Original attempt selection cannot be replaced with current-head or caller labels."""

    def fixture(self, *, attempt=3, owner=True):
        import base64
        from types import SimpleNamespace
        policy = "d" * 40
        request_head = "e" * 40
        repository = {"full_name": REPOSITORY}
        executor = {"id": 321, "run_attempt": attempt, "head_sha": policy,
                    "path": authorize_compiler.BENCH_WORKFLOW, "event": "workflow_run", "head_branch": "main",
                    "repository": repository, "head_repository": repository,
                    "display_title": f"9700X request 319.1 head {request_head}",
                    "triggering_actor": {"login": "davidgmbb" if owner else "other", "id": 39247043 if owner else 8}}
        request = {"id": 319, "run_attempt": 1, "head_sha": request_head,
                   "path": authorize_compiler.REQUEST_WORKFLOW, "event": "push", "head_branch": "main",
                   "repository": repository, "head_repository": repository,
                   "status": "completed", "conclusion": "success",
                   "actor": dict(OTHER), "triggering_actor": dict(OTHER)}
        fields = authorize_compiler.MAIN_ROUTE_FIELDS
        values = {name: "-" for name in fields}
        values.update(schema="buster-compiler-main-profile-routing-v1", repository=REPOSITORY, state="disabled",
                      main_profile="compiler-compare-v1", preparation_policy="legacy-rebuild")
        text = "".join(f"{key}\t{value}\n" for key, value in values.items())
        row = {"type": "file", "path": authorize_compiler.MAIN_ROUTE_PATH, "size": len(text),
               "encoding": "base64", "content": base64.b64encode(text.encode()).decode()}
        paths = {f"/actions/runs/321/attempts/{attempt}": executor,
                 "/actions/runs/319/attempts/1": request,
                 f"/compare/{policy}...main": {"status": "ahead"},
                 f"/contents/{authorize_compiler.MAIN_ROUTE_PATH}?ref={policy}": row,
                 f"/contents/{authorize_compiler.MAIN_ROUTE_MODULE_PATH}?ref={policy}":
                     urllib.error.HTTPError("module", 404, "historical absence", {}, None)}
        calls = []
        def read(path):
            calls.append(path)
            if path not in paths:
                raise AssertionError("unexpected API path: " + path)
            value = paths[path]
            if isinstance(value, Exception):
                raise value
            return value
        api = SimpleNamespace(request=read)
        output = {name: "-" for name in authorize_compiler.MAIN_ROUTE_OUTPUT_FIELDS}
        output.update(main_owned="false", main_profile="compiler-compare-v1",
                      main_preparation_policy="legacy-rebuild", main_phase_schema="-")
        return api, executor, request, paths, calls, output

    def route(self, value):
        api, executor, request, paths, calls, output = value
        with mock.patch.object(authorize_compiler, "main_route_native", return_value=output) as native:
            result = authorize_compiler.resolve_main_route(api, REPOSITORY, executor, str(executor["run_attempt"]))
        return result, native, calls

    def test_original_attempt_selects_original_policy_and_historical_disabled_profile(self):
        value = self.fixture()
        result, native, calls = self.route(value)
        self.assertIs(result["main_owned"], False)
        self.assertEqual(result["main_policy_revision"], "d" * 40)
        self.assertEqual(result["main_measurement_revision"], "d" * 40)
        self.assertEqual(calls[0], "/actions/runs/321/attempts/3")
        self.assertIn("/actions/runs/319/attempts/1", calls)
        self.assertNotIn("/actions/runs/321", calls)
        data = native.call_args.args[1]
        self.assertIn("executor_attempt\t3\n", data["facts"])
        self.assertIn("request_attempt\t1\n", data["facts"])
        self.assertIn("policy_revision\t" + "d" * 40 + "\n", data["facts"])

    def test_request_push_author_need_not_be_owner_but_executor_rerun_must_be(self):
        self.route(self.fixture(attempt=1, owner=False))
        with self.assertRaises(ValueError):
            self.route(self.fixture(attempt=3, owner=False))

    def test_rerun_actor_is_typed_api_owner_not_tuple_coercion(self):
        for actor in (None, "davidgmbb", [], {}, {"login": "davidgmbb"},
                      {"login": "davidgmbb", "id": 39247043.0},
                      {"login": "davidgmbb", "id": True},
                      {"login": "davidgmbb", "id": "39247043"}):
            with self.subTest(actor=actor):
                value = self.fixture()
                value[1]["triggering_actor"] = actor
                with self.assertRaises(ValueError):
                    self.route(value)

    def test_attempt_and_run_ids_are_typed_exact_api_values(self):
        for field, bad in (("id", 321.0), ("id", "321"), ("id", True),
                           ("run_attempt", 3.0), ("run_attempt", "3"), ("run_attempt", True),
                           ("run_attempt", 2)):
            value = self.fixture()
            returned = dict(value[1]); returned[field] = bad
            value[3]["/actions/runs/321/attempts/3"] = returned
            with self.subTest(field=field, bad=bad), self.assertRaises(ValueError):
                self.route(value)
        for bad in (None, "1", 1.0, True, 2):
            value = self.fixture()
            value[2]["run_attempt"] = bad
            with self.subTest(request_attempt=bad), self.assertRaises(ValueError):
                self.route(value)

    def test_foreign_executor_or_request_or_noncanonical_title_never_reaches_native(self):
        executor_changes = {"path": ".github/workflows/other.yml", "event": "push", "head_branch": "feature",
                            "repository": {"full_name": "foreign/repo"}, "head_repository": None,
                            "display_title": "9700X request 319.1 head " + "e" * 40 + " copied"}
        request_changes = {"path": ".github/workflows/other.yml", "event": "pull_request", "head_branch": "feature",
                           "head_sha": "f" * 40, "conclusion": "failure",
                           "head_repository": {"full_name": "foreign/repo"}}
        for role, changes in ((1, executor_changes), (2, request_changes)):
            for field, bad in changes.items():
                value = self.fixture(); value[role][field] = bad
                with self.subTest(role=role, field=field), self.assertRaises(ValueError):
                    self.route(value)

    def test_missing_original_policy_is_historical_only_and_other_errors_are_refusals(self):
        value = self.fixture()
        path = f"/contents/{authorize_compiler.MAIN_ROUTE_PATH}?ref=" + "d" * 40
        value[3][path] = urllib.error.HTTPError(path, 404, "missing", {}, None)
        result, native, calls = self.route(value)
        self.assertFalse(result["main_owned"])
        native.assert_not_called()
        for status in (403, 500):
            value = self.fixture()
            value[3][path] = urllib.error.HTTPError(path, status, "failure", {}, None)
            with self.subTest(status=status), self.assertRaises(urllib.error.HTTPError):
                self.route(value)


    def test_deleted_policy_with_native_routing_module_is_not_historical(self):
        import base64
        value = self.fixture()
        policy = "d" * 40
        path = f"/contents/{authorize_compiler.MAIN_ROUTE_PATH}?ref={policy}"
        value[3][path] = urllib.error.HTTPError(path, 404, "missing", {}, None)
        module_path = f"/contents/{authorize_compiler.MAIN_ROUTE_MODULE_PATH}?ref={policy}"
        text = "// native routing exists\n"
        value[3][module_path] = {"type": "file", "path": authorize_compiler.MAIN_ROUTE_MODULE_PATH,
                                "size": len(text), "encoding": "base64", "content": base64.b64encode(text.encode()).decode()}
        with self.assertRaises(ValueError):
            self.route(value)

    def test_existing_partial_or_malformed_policy_cannot_downgrade_to_history(self):
        import base64
        for bad in ("", "schema\tbuster-compiler-main-profile-routing-v1\n",
                    "schema\tbuster-compiler-main-profile-routing-v1\nschema\trepeated\n"):
            value = self.fixture()
            path = f"/contents/{authorize_compiler.MAIN_ROUTE_PATH}?ref=" + "d" * 40
            value[3][path].update(size=len(bad), content=base64.b64encode(bad.encode()).decode())
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.route(value)

    def test_source_content_size_type_and_encoding_are_bounded(self):
        for field, bad in (("size", True), ("size", 131073), ("size", 1),
                           ("content", "$invalid"), ("encoding", "none"), ("type", "symlink")):
            value = self.fixture()
            path = f"/contents/{authorize_compiler.MAIN_ROUTE_PATH}?ref=" + "d" * 40
            value[3][path][field] = bad
            with self.subTest(field=field), self.assertRaises((ValueError, TypeError)):
                self.route(value)

    def test_off_main_policy_revision_is_not_reinterpreted(self):
        value = self.fixture()
        value[3]["/compare/" + "d" * 40 + "...main"] = {"status": "diverged"}
        with self.assertRaises(ValueError):
            self.route(value)

    def test_pre_routing_run_without_canonical_title_recovers_historically(self):
        # Publication-only recovery of runs that predate both the routing policy and the request title.
        for title in (None, "9700X direct workload benchmark"):
            value = self.fixture(attempt=1)
            path = f"/contents/{authorize_compiler.MAIN_ROUTE_PATH}?ref=" + "d" * 40
            value[3][path] = urllib.error.HTTPError(path, 404, "missing", {}, None)
            value[1]["display_title"] = title
            value[3]["/actions/runs/321/attempts/1"] = dict(value[1])
            with self.subTest(title=title):
                result, native, calls = self.route(value)
                self.assertFalse(result["main_owned"])
                self.assertEqual(result["main_profile"], "compiler-compare-v1")
                native.assert_not_called()
                self.assertNotIn("/actions/runs/319/attempts/1", calls)

    def test_routed_run_without_canonical_title_is_refused_before_native(self):
        value = self.fixture()
        value[1]["display_title"] = "9700X direct workload benchmark"
        value[3]["/actions/runs/321/attempts/3"] = dict(value[1])
        with self.assertRaises(ValueError):
            self.route(value)

    def test_expired_native_resolver_is_a_refusal_not_a_crash(self):
        import subprocess
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as root, \
                mock.patch.object(subprocess, "run", side_effect=subprocess.TimeoutExpired(["build.sh"], 120)):
            with self.assertRaisesRegex(ValueError, "timed out"):
                authorize_compiler.main_route_native(Path(root), {"policy": "", "facts": ""})

if __name__ == "__main__":
    unittest.main()
