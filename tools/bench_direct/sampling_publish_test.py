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


if __name__ == "__main__":
    unittest.main()
