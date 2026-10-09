#!/usr/bin/env python3
"""Hosted controls for bounded sampling evidence data and whole-job accounting."""
import io
import stat
import unittest
import zipfile
from unittest import mock
import compiler_publish as publisher


def archive(entries):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
        for name, data, kind in entries:
            info = zipfile.ZipInfo(name)
            info.external_attr = (kind | 0o600) << 16
            zipped.writestr(info, data)
    return stream.getvalue()


class SamplingPublicationControls(unittest.TestCase):
    def test_regular_data_stays_in_memory(self):
        payload = archive([("identity.tsv", b"schema\tv1\n", stat.S_IFREG)])
        with mock.patch.object(zipfile.ZipFile, "extract", side_effect=AssertionError("extraction")), \
                mock.patch.object(zipfile.ZipFile, "extractall", side_effect=AssertionError("extraction")):
            self.assertEqual(publisher.sampling_archive(payload), {"identity.tsv": b"schema\tv1\n"})

    def test_archive_aliases_and_links_are_rejected(self):
        for entries in [
            [("../identity.tsv", b"x", stat.S_IFREG)],
            [("/identity.tsv", b"x", stat.S_IFREG)],
            [("a\\identity.tsv", b"x", stat.S_IFREG)],
            [("./identity.tsv", b"x", stat.S_IFREG)],
            [("identity.tsv", b"x", stat.S_IFLNK)],
            [("identity.tsv", b"x", stat.S_IFIFO)],
            [("identity.tsv", b"x", stat.S_IFREG), ("identity.tsv", b"y", stat.S_IFREG)],
            [("a/", b"", stat.S_IFDIR), ("a", b"x", stat.S_IFREG)],
        ]:
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                publisher.sampling_archive(archive(entries))

    def test_inflated_member_bound_is_checked_before_read(self):
        payload = archive([("pairs.json", b"123456789", stat.S_IFREG)])
        with mock.patch.object(publisher, "ANALYZER_MEMBER_LIMIT", 8), self.assertRaises(ValueError):
            publisher.sampling_archive(payload)

    def test_trailing_data_and_excessive_directory_rejected(self):
        payload = archive([("identity.tsv", b"x", stat.S_IFREG)])
        with self.assertRaises(ValueError):
            publisher.sampling_archive(payload + b"junk")
        payload = archive([(str(i), b"", stat.S_IFREG) for i in range(2049)])
        with self.assertRaises(ValueError):
            publisher.sampling_archive(payload)

    def test_strict_tsv_duplicate_and_missing_cells(self):
        self.assertEqual(publisher.sampling_tsv(b"a\tone\nb\ttwo\n"), {"a": "one", "b": "two"})
        self.assertEqual(publisher.sampling_tsv(b"a\tb\none\ttwo\n", True), [{"a": "one", "b": "two"}])
        for raw in [b"a\t1\na\t2\n", b"a\t\n", b"a\tb\r\n", b"a\tb", b"a\tb\tc\n", b"a\t\x00\n"]:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                publisher.sampling_tsv(raw)
        with self.assertRaises(ValueError):
            publisher.sampling_tsv(b"a\ta\n1\t2\n", True)

    def test_cleanup_proof_rejects_adopted_or_uncertain_children(self):
        raw = (b"schema\tbuster-native-qualification-supervisor-v1\ncleanup_proven\ttrue\nwall_us\t100\n"
               b"adoption_waves\t1\nadopted_signalled\t0\nadopted_reaped\t0\n")
        self.assertEqual(publisher.sampling_supervision(raw)["wall_us"], "100")
        for changed in [raw.replace(b"cleanup_proven\ttrue", b"cleanup_proven\tfalse"),
                        raw.replace(b"adopted_reaped\t0", b"adopted_reaped\t1"),
                        raw.replace(b"wall_us\t100", b"wall_us\t0"), raw + b"extra\t1\n"]:
            with self.assertRaises(ValueError):
                publisher.sampling_supervision(changed)

    def test_whole_job_not_only_native_measurement_charges_budget(self):
        job = {"name": publisher.SAMPLING_HOST_JOB, "status": "completed", "conclusion": "success",
               "created_at": "2026-10-09T00:00:00Z", "started_at": "2026-10-09T00:01:00Z",
               "completed_at": "2026-10-09T00:02:00Z"}
        observed = publisher.sampling_job_accounting(job, 40000000, 62)
        self.assertEqual(observed["physical_job_wall_us"], 60000000)
        self.assertEqual(observed["physical_job_wall_upper_us"], 62000000)
        self.assertEqual(observed["queue_delay_seconds"], 60)
        for changed, wall, budget in [
            (job, 40000000, 61), (job, 63000000, 100),
            (dict(job, completed_at=None), 1, 100),
            (dict(job, started_at="2026-10-09T00:03:00Z"), 1, 100),
            (dict(job, conclusion="cancelled"), 1, 100),
            (dict(job, started_at="2026-10-09T00:01:00"), 1, 100),
        ]:
            with self.subTest(job=changed), self.assertRaises(ValueError):
                publisher.sampling_job_accounting(changed, wall, budget)


    def authority(self):
        return {"head": "a" * 40, "request_id": "101", "run_id": "202",
                "admitted": {"sampling_freeze_sha256": "b" * 64, "sampling_phase": "pilot", "sampling_packet": "0"}}

    def fake_api(self, authority, completed=False, lost_response=False):
        class FakeApi:
            def __init__(self):
                self.rows = []
                self.posts = self.patches = 0
            def pages(self, path, field):
                return self.rows
            def request(self, path, fields, method=""):
                if path == "/check-runs":
                    self.posts += 1
                    row = dict(fields, id=303, app={"id": 15368})
                    row["details_url"] = "https://github.com/buster14a/buster/runs/303"
                    self.rows.append(row)
                    if lost_response and self.posts == 1:
                        raise publisher.urllib.error.URLError("response lost")
                    return row
                self.patches += 1
                self.rows[0].update(fields)
                return self.rows[0]
        api = FakeApi()
        if completed:
            api.rows.append({"id": 303, "name": publisher.SAMPLING_CHECK_NAME, "head_sha": authority["head"],
                             "external_id": publisher.sampling_check_marker(authority), "app": {"id": 15368},
                             "status": "completed", "conclusion": "failure"})
        return api

    def test_sampling_check_is_attempt_bound_and_terminal_immutable(self):
        authority = self.authority()
        api = self.fake_api(authority, completed=True)
        row = publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual(row["conclusion"], "failure")
        self.assertEqual((api.posts, api.patches), (0, 0))
        api.rows[0]["external_id"] = api.rows[0]["external_id"].replace(":202:1", ":999:1")
        self.assertEqual(publisher.sampling_checks(api, authority), [])

    def test_lost_check_post_response_is_resolved_without_duplicate(self):
        authority = self.authority()
        api = self.fake_api(authority, lost_response=True)
        row = publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual((api.posts, api.patches, row["id"]), (1, 0, 303))
        self.assertEqual(row["details_url"], "https://github.com/buster14a/buster/runs/303")

    def test_duplicate_owned_checks_are_rejected(self):
        authority = self.authority()
        api = self.fake_api(authority, completed=True)
        api.rows.append(dict(api.rows[0], id=304))
        with self.assertRaises(ValueError):
            publisher.sampling_checks(api, authority)

    def test_unbound_sampling_workflow_is_rejected_before_api_access(self):
        with mock.patch.object(publisher, "Api", side_effect=AssertionError("must not access API")):
            with self.assertRaises(ValueError):
                publisher.sampling_authority({"BQ_REPOSITORY": "buster14a/buster"})


    def test_in_progress_check_never_moves_back_to_queued(self):
        authority = self.authority()
        api = self.fake_api(authority, completed=True)
        api.rows[0].update(status="in_progress")
        row = publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual(row["status"], "in_progress")
        self.assertEqual((api.posts, api.patches), (0, 0))

    def test_ambiguous_lost_post_never_retries_creation(self):
        authority = self.authority()
        api = self.fake_api(authority, lost_response=True)
        api.pages = lambda path, field: []
        with self.assertRaises(ValueError):
            publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual(api.posts, 1)

    def test_successful_post_is_verified_without_visibility_retry(self):
        authority = self.authority()
        api = self.fake_api(authority)
        row = publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual((api.posts, api.patches, row["id"]), (1, 0, 303))


if __name__ == "__main__":
    unittest.main()
