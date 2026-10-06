#!/usr/bin/env python3
"""Bounded collector controls; synthetic API pages never qualify a campaign."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import ci_checks_dispatch_inventory as inventory
import ci_checks_population as population


class DispatchInventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "capture"
        self.published = "2026-10-04T08:00:00Z"
        self.captured = datetime(2026, 10, 4, 9, tzinfo=timezone.utc)
        self.receipt = {"id": 42, "issue_url": "https://api.github.com/repos/buster14a/buster/issues/2610",
                        "url": "https://api.github.com/repos/buster14a/buster/issues/comments/42",
                        "html_url": "https://github.com/buster14a/buster/issues/2610#issuecomment-42",
                        "body": "Published\nCI_CHECKS_POPULATION_DECLARATION_V1 sha256=" + "a" * 64,
                        "created_at": self.published, "updated_at": self.published}
        self.publication = self.root / "comment.json"
        self.publication.write_text(json.dumps(self.receipt), encoding="utf-8")
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(inventory, "utc_now", return_value=self.captured).start()
        self.commands = []

    def runs(self, count):
        return [{"id": index + 1, "event": "workflow_dispatch", "path": ".github/workflows/ci.yml",
                 "head_branch": "unrelated/ref" if index else "codex/2120-evidence-v2-combined-overlap",
                 "created_at": "2026-10-04T08:10:00Z", "head_sha": "b" * 40,
                 "run_attempt": 2 if index == 1 else 1, "status": "completed",
                 "conclusion": "failure" if index == 2 else "cancelled" if index == 3 else "success"}
                for index in range(count)]

    def api(self, responses, returncode=0, timeout=False):
        def invoke(command, **arguments):
            self.commands.append((command, arguments))
            page = int(command[-1].split("=", 1)[1])
            response = responses[page - 1]
            raw = response if isinstance(response, bytes) else json.dumps(response, indent=1).encode("utf-8") + b"\n"
            arguments["stdout"].write(raw)
            arguments["stderr"].write(b"original diagnostic\n" if returncode else b"")
            if timeout:
                raise subprocess.TimeoutExpired(command, arguments["timeout"])
            return subprocess.CompletedProcess(command, returncode)
        return mock.patch.object(inventory.subprocess, "run", side_effect=invoke)

    def assert_failed(self, responses, **options):
        with self.api(responses, **options), self.assertRaises((ValueError, OSError, subprocess.TimeoutExpired)):
            inventory.collect(self.publication, self.output)
        self.assertFalse((self.output / "inventory.json").exists())
        failure = json.loads((self.output / "failure.json").read_bytes())
        self.assertFalse(failure["success_manifest"])
        self.assertEqual(failure["status"], "failed")
        return failure

    def test_empty_inventory_retains_original_bytes_and_explicit_get_scope(self):
        raw = b'{ "workflow_runs": [], "total_count": 0, "extra": "original" }\n'
        with self.api([raw]):
            reference = inventory.collect(self.publication, self.output)
        manifest = json.loads(Path(reference["path"]).read_bytes())
        self.assertEqual(hashlib.sha256(Path(reference["path"]).read_bytes()).hexdigest(), reference["sha256"])
        self.assertEqual((self.output / "page-001.json").read_bytes(), raw)
        self.assertEqual((self.output / "publication.json").read_bytes(), self.publication.read_bytes())
        self.assertEqual(manifest["created_after"], self.published)
        self.assertEqual(manifest["collected_at"], "2026-10-04T09:00:00Z")
        self.assertEqual(set(manifest), {"schema", "endpoint", "event", "created_after", "per_page", "collected_at", "pages"})
        command, arguments = self.commands[0]
        self.assertEqual(command, ["gh", "api", "--method", "GET", inventory.API_PATH,
                                   "-f", "event=workflow_dispatch", "-f", "created=>=" + self.published,
                                   "-F", "per_page=100", "-F", "page=1"])
        self.assertLessEqual(arguments["timeout"], inventory.REQUEST_SECONDS)
        self.assertEqual(arguments["stdin"], subprocess.DEVNULL)
        self.assertNotIn("shell", arguments)

    def test_complete_pages_keep_foreign_refs_failures_and_current_retries(self):
        records = self.runs(141)
        responses = [{"total_count": 141, "workflow_runs": records[:100]},
                     {"total_count": 141, "workflow_runs": records[100:]}]
        with self.api(responses):
            reference = inventory.collect(self.publication, self.output)
        manifest = json.loads(Path(reference["path"]).read_bytes())
        self.assertEqual([page["page"] for page in manifest["pages"]], [1, 2])
        retained = [record for page in manifest["pages"]
                    for record in json.loads((self.output / page["response"]["path"]).read_bytes())["workflow_runs"]]
        self.assertEqual(retained, records)
        archive = self.root / "run.json"
        archive.write_text(json.dumps(records[0]) + "\n", encoding="utf-8")
        attempt = {"run": {"path": archive.name, "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()},
                   "intake_completed_at": "2026-10-04T08:30:00Z"}
        checked = population.dispatch_inventory(self.root, reference, inventory.timestamp(self.published), [attempt])
        self.assertEqual(checked["run_ids"], [1])
        self.assertEqual(checked["api_runs"], 141)

    def test_999_records_complete_ten_pages_preserve_every_original_byte(self):
        records = self.runs(999)
        responses = [json.dumps({"workflow_runs": records[index * 100:(index + 1) * 100],
                                 "total_count": 999, "original_page": index + 1}, indent=2).encode("utf-8") +
                     (b"\n" if index % 2 else b"") for index in range(10)]
        with self.api(responses):
            reference = inventory.collect(self.publication, self.output)
        manifest_path = Path(reference["path"])
        manifest = json.loads(manifest_path.read_bytes())
        self.assertEqual(reference["sha256"], hashlib.sha256(manifest_path.read_bytes()).hexdigest())
        self.assertEqual([page["page"] for page in manifest["pages"]], list(range(1, 11)))
        self.assertEqual([command[-1] for command, _ in self.commands],
                         ["page=" + str(page) for page in range(1, 11)])
        retained = []
        for page, original in zip(manifest["pages"], responses):
            response = page["response"]
            actual = (self.output / response["path"]).read_bytes()
            self.assertEqual(actual, original)
            self.assertEqual(response["sha256"], hashlib.sha256(original).hexdigest())
            retained.extend(json.loads(actual)["workflow_runs"])
        self.assertEqual(retained, records)
        self.assertFalse((self.output / "failure.json").exists())
        self.assertFalse((self.output / "page-011.json").exists())
        archive = self.root / "run.json"
        archive.write_text(json.dumps(records[0]) + "\n", encoding="utf-8")
        attempt = {"run": {"path": archive.name, "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()},
                   "intake_completed_at": "2026-10-04T08:30:00Z"}
        checked = population.dispatch_inventory(self.root, reference, inventory.timestamp(self.published), [attempt])
        self.assertEqual(checked["api_runs"], 999)
        self.assertEqual(checked["run_ids"], [1])

    def test_1000_cap_saturation_retains_first_page_and_stops_without_manifest(self):
        original = json.dumps({"total_count": 1000, "workflow_runs": self.runs(100)},
                              indent=3).encode("utf-8")
        failure = self.assert_failed([original])
        self.assertEqual(len(self.commands), 1)
        self.assertEqual(self.commands[0][0][-1], "page=1")
        self.assertEqual((self.output / "page-001.json").read_bytes(), original)
        expected = {"path": "page-001.json", "sha256": hashlib.sha256(original).hexdigest()}
        self.assertEqual(failure["pages"], [{"page": 1, "response": expected}])
        self.assertIn(expected, failure["retained_files"])
        self.assertIn("cap", failure["error"])
        self.assertFalse((self.output / "page-002.json").exists())

    def test_valid_original_api_json_without_final_lf_is_not_rewritten(self):
        raw = b'{"total_count":0,"workflow_runs":[]}'
        with self.api([raw]):
            reference = inventory.collect(self.publication, self.output)
        self.assertEqual((self.output / "page-001.json").read_bytes(), raw)
        checked = population.dispatch_inventory(self.root, reference, inventory.timestamp(self.published), [])
        self.assertEqual(checked["api_runs"], 0)

    def test_original_comment_without_final_lf_validates_in_both_readers(self):
        raw = self.publication.read_bytes()
        self.assertFalse(raw.endswith(b"\n"))
        observed, created = inventory.publication(self.publication)
        reference = inventory.reference(self.publication)
        frozen = population.publication(self.root, reference, {"sha256": "a" * 64})
        self.assertEqual(observed, raw)
        self.assertEqual(created, frozen)

    def test_count_cap_types_and_incomplete_pages_never_write_success(self):
        for index, response in enumerate(({"total_count": 1001, "workflow_runs": []},
                                          {"total_count": True, "workflow_runs": []},
                                          {"total_count": 2, "workflow_runs": self.runs(1)},
                                          {"total_count": 0, "workflow_runs": self.runs(1)})):
            self.output = self.root / ("failure-" + str(index))
            with self.subTest(response=response):
                failure = self.assert_failed([response])
                self.assertTrue(failure["retained_files"])

    def test_changing_pagination_and_cross_page_duplicates_preserve_all_pages(self):
        records = self.runs(101)
        cases = [[{"total_count": 101, "workflow_runs": records[:100]},
                  {"total_count": 102, "workflow_runs": records[100:]}],
                 [{"total_count": 101, "workflow_runs": records[:100]},
                  {"total_count": 101, "workflow_runs": [records[0]]}]]
        for index, responses in enumerate(cases):
            self.output = self.root / ("changed-" + str(index))
            with self.subTest(index=index):
                failure = self.assert_failed(responses)
                self.assertEqual(len(failure["pages"]), 2)
                self.assertTrue((self.output / "page-002.json").exists())

    def test_invalid_json_nonzero_exit_timeout_and_size_refuse_but_retain_bytes(self):
        cases = [(b"not JSON\n", {}), (b'{"error":"failed"}', {"returncode": 1}),
                 (b"partial response", {"timeout": True})]
        for index, (raw, options) in enumerate(cases):
            self.output = self.root / ("capture-failure-" + str(index))
            with self.subTest(index=index):
                self.assert_failed([raw], **options)
                self.assertEqual((self.output / "page-001.json").read_bytes(), raw)
        self.output = self.root / "oversize"
        with mock.patch.object(inventory, "MAX_PAGE_BYTES", 8):
            self.assert_failed([b"x" * 9])
        self.assertEqual((self.output / "page-001.json").stat().st_size, 9)

    def test_existing_output_is_not_overwritten_or_queried(self):
        self.output.mkdir()
        sentinel = self.output / "original"
        sentinel.write_bytes(b"keep")
        with self.api([]), self.assertRaises(FileExistsError):
            inventory.collect(self.publication, self.output)
        self.assertEqual(sentinel.read_bytes(), b"keep")
        self.assertFalse(self.commands)

    def test_unpublished_edited_foreign_or_ambiguous_receipts_precede_no_api_call(self):
        for field, value in (("issue_url", "https://api.github.com/repos/buster14a/buster/issues/2120"),
                             ("body", "no declaration"),
                             ("body", self.receipt["body"] + "\n" + self.receipt["body"].splitlines()[-1]),
                             ("updated_at", "2026-10-04T08:01:00Z"),
                             ("created_at", "2026-10-04T08:00:00"),
                             ("id", True)):
            changed = dict(self.receipt, **{field: value})
            self.publication.write_text(json.dumps(changed), encoding="utf-8")
            with self.subTest(field=field, value=value), self.api([]), self.assertRaises(ValueError):
                inventory.collect(self.publication, self.output)
            self.assertFalse(self.output.exists())
            self.assertFalse(self.commands)

    def test_out_of_scope_records_and_exhausted_clock_refuse(self):
        for index, (field, value) in enumerate((("event", "push"), ("path", ".github/workflows/other.yml"),
                                                ("created_at", "2026-10-04T07:59:59Z"),
                                                ("created_at", "2026-10-04T09:00:01Z"))):
            self.output = self.root / ("scope-" + str(index))
            record = dict(self.runs(1)[0], **{field: value})
            with self.subTest(field=field, value=value):
                self.assert_failed([{"total_count": 1, "workflow_runs": [record]}])
        self.output = self.root / "clock-exhausted"
        with mock.patch.object(inventory.time, "monotonic", side_effect=[0, inventory.COLLECTION_SECONDS + 1]):
            self.assert_failed([])


if __name__ == "__main__":
    unittest.main()
