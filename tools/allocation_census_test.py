#!/usr/bin/env python3
"""Offline protocol and export regressions; no compiler or benchmark is run."""
import contextlib
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import allocation_census as census


def counters(**changes):
    result = dict.fromkeys(census.FIELDS, 0)
    result.update(changes)
    return result


def escape(value):
    return value.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def row(epoch=1, kind="arena", file="source.c", line=17, function="allocate", **changes):
    values = counters(calls=2, bytes=7, empty=1, small=1, maximum=7)
    values.update(changes)
    return "\t".join(["BUSTER_ALLOC_V2", str(epoch), kind, escape(file), str(line), escape(function),
                      *(str(values[key]) for key in census.FIELDS)])


def epoch_records(epoch=1, rows=None):
    rows = [row(epoch)] if rows is None else rows
    result = list(rows)
    for kind in census.KINDS:
        samples = [list(map(int, record.split("\t")[6:])) for record in rows if record.split("\t")[2] == kind]
        values = [max((sample[index] for sample in samples), default=0) if key == "maximum"
                  else sum(sample[index] for sample in samples) for index, key in enumerate(census.FIELDS)]
        result.append("\t".join(["BUSTER_ALLOC_TOTAL_V2", str(epoch), kind, *map(str, values)]))
    result.append(f"BUSTER_ALLOC_END_V2\t{epoch}\t{len(rows)}")
    return result


def log(records=None, final=1):
    records = epoch_records() if records is None else records
    return "\n".join([*records, f"BUSTER_ALLOC_DONE_V2\t{final}", ""])


class ProtocolTests(unittest.TestCase):
    def assert_invalid(self, text):
        with self.assertRaises(ValueError):
            census.parse_census(text)

    def test_complete_census_and_all_five_independent_totals(self):
        report = census.parse_census(log())
        self.assertEqual((report["schema"], report["protocol"], report["completed"]), (2, "BUSTER_ALLOC_V2", True))
        self.assertEqual((report["epochs"], report["site_records"]), (1, 1))
        self.assertEqual(report["totals"]["arena"], counters(calls=2, bytes=7, empty=1, small=1, maximum=7))
        self.assertEqual(set(report["totals"]), set(census.KINDS))
        for kind in census.KINDS[1:]:
            self.assertEqual(report["totals"][kind], counters())

    def test_escapes_unicode_crlf_and_ordinary_output(self):
        filename = "dir\\name\tline\ncarriage\r café.c"
        function = "f\\n\t\n\r"
        text = log(epoch_records(rows=[row(file=filename, function=function)])).replace("\n", "\r\n")
        report = census.parse_census("compiler diagnostic\n" + text + "cleanup complete")
        self.assertEqual(report["sites"][0]["file"], filename)
        self.assertEqual(report["sites"][0]["function"], function)

    def test_textual_merging_and_maximum_across_epochs(self):
        records = epoch_records() + epoch_records(2, [row(2, calls=1, bytes=11, empty=0, maximum=11)])
        report = census.parse_census(log(records, final=2))
        self.assertEqual((report["site_records"], len(report["sites"])), (2, 1))
        self.assertEqual({key: report["sites"][0][key] for key in census.FIELDS},
                         counters(calls=3, bytes=18, empty=1, small=2, maximum=11))
        self.assertEqual(report["totals"]["arena"]["maximum"], 11)

    def test_distinct_textual_sites_and_deterministic_order(self):
        records = [row(file="b.c"), row(file="a.c"), row(kind="os_reserve", file="a.c"),
                   row(file="a.c", function="other"), row(file="a.c", line=18),
                   row(file="z.c", calls=1, bytes=12, empty=0, maximum=12)]
        forward = census.parse_census(log(epoch_records(rows=records)))
        reverse = census.parse_census(log(epoch_records(rows=list(reversed(records)))))
        self.assertEqual(forward, reverse)
        self.assertEqual(len(forward["sites"]), 6)
        self.assertEqual(forward["sites"][0]["file"], "z.c")
        self.assertEqual(forward["totals"]["arena"]["maximum"], 12)

    def test_interleaved_epochs_can_close_in_any_order(self):
        first = epoch_records(1)
        second = epoch_records(2, [row(2, file="worker.c")])
        records = [second[0], first[0], *second[1:4], *first[1:4], *second[4:], *first[4:]]
        report = census.parse_census(log(records, final=2))
        self.assertEqual((report["epochs"], report["site_records"]), (2, 2))

    def test_empty_epoch_requires_all_zero_totals_and_footer(self):
        report = census.parse_census(log(epoch_records(rows=[])))
        self.assertEqual(report["sites"], [])
        self.assertEqual(report["site_records"], 0)
        self.assertTrue(report["completed"])

    def test_missing_each_row_total_or_footer(self):
        records = epoch_records()
        for index in range(len(records)):
            with self.subTest(record=records[index].split("\t")[:3]):
                self.assert_invalid(log(records[:index] + records[index + 1:]))

    def test_duplicate_each_row_total_or_footer(self):
        records = epoch_records()
        for index in range(len(records)):
            with self.subTest(record=records[index].split("\t")[:3]):
                self.assert_invalid(log(records[:index + 1] + [records[index]] + records[index + 1:]))

    def test_missing_duplicate_or_early_final_marker(self):
        records = epoch_records()
        for text in ("\n".join(records) + "\n", log() + "BUSTER_ALLOC_DONE_V2\t1\n",
                     log([]), log(records[:-1]), log(records, final=2), ""):
            with self.subTest(text=text[-70:]):
                self.assert_invalid(text)

    def test_epoch_gaps_or_final_before_all_epochs(self):
        for records, final in ((epoch_records(2), 2), (epoch_records(1) + epoch_records(3), 3),
                               (epoch_records(1) + epoch_records(2), 1)):
            with self.subTest(final=final, first=records[0]):
                self.assert_invalid(log(records, final))

    def test_late_rows_and_totals_after_footer(self):
        records = epoch_records()
        for record in records[:-1]:
            with self.subTest(record=record[:50]):
                self.assert_invalid(log(records + [record]))

    def test_every_record_type_rejected_after_done(self):
        for record in [*epoch_records(2), "BUSTER_ALLOC_DONE_V2\t2", "BUSTER_ALLOC_ERROR_V2\toverflow"]:
            with self.subTest(record=record[:50]):
                self.assert_invalid(log() + record + "\n")

    def test_wrong_row_counts(self):
        for value in ("0", "2", "-1", "1.0", str(1 << 64)):
            with self.subTest(value=value):
                records = epoch_records()
                records[-1] = f"BUSTER_ALLOC_END_V2\t1\t{value}"
                self.assert_invalid(log(records))

    def test_truncated_or_extra_fields_in_every_record_type(self):
        records = [*epoch_records(), "BUSTER_ALLOC_DONE_V2\t1"]
        for index, record in enumerate(records):
            for altered in (record.rsplit("\t", 1)[0], record + "\textra"):
                with self.subTest(record=altered[:50]):
                    self.assert_invalid("\n".join([*records[:index], altered, *records[index + 1:], ""]))

    def test_final_record_requires_newline(self):
        self.assert_invalid(log().rstrip("\n"))
        self.assert_invalid(log() + "BUSTER_ALLOC_")

    def test_unknown_protocol_kind_and_mixed_text(self):
        for text in (log().replace("_V2", "_V1"), log().replace("os_commit", "os_unknown"),
                     "prefix " + log(), log().replace("BUSTER_ALLOC_V2", "BUSTER_ALLOC_OTHER_V2", 1),
                     "BUSTER_ALLOC_ERROR_V2\toverflow\n" + log()):
            with self.subTest(text=text[:70]):
                self.assert_invalid(text)

    def test_invalid_site_escapes_and_identity(self):
        parts = row().split("\t")
        for field, value in ((3, "bad\\q"), (3, "bad\\"), (3, "raw\x01control"),
                             (3, ""), (5, ""), (4, "0"), (4, "-1"), (4, str(1 << 32))):
            with self.subTest(field=field, value=value):
                altered = parts.copy()
                altered[field] = value
                records = epoch_records()
                records[0] = "\t".join(altered)
                self.assert_invalid(log(records))

    def test_invalid_decimal_counters_and_epochs(self):
        for value in ("", "-1", "+1", " 1", "1.0", "１", str(1 << 64), "0" * 21):
            with self.subTest(value=value):
                parts = row().split("\t")
                parts[6] = value
                records = epoch_records()
                records[0] = "\t".join(parts)
                self.assert_invalid(log(records))
                self.assert_invalid(log().replace("\t1\t", f"\t{value}\t"))
        self.assert_invalid(log().replace("\t1\t", "\t0\t"))

    def test_inconsistent_counter_semantics(self):
        changes = [dict(calls=0), dict(empty=3), dict(small=2), dict(maximum=8), dict(maximum=6),
                   dict(maximum=0), dict(zero_requested=8), dict(zero_written=1), dict(failures=3),
                   dict(failed_bytes=1), dict(failures=1), dict(bytes=0),
                   dict(calls=1, empty=0, small=0, bytes=64, maximum=64),
                   dict(calls=2, empty=0, small=0, bytes=129, maximum=65)]
        for change in changes:
            with self.subTest(change=change):
                self.assert_invalid(log(epoch_records(rows=[row(**change)])))

    def test_zero_call_site_is_not_a_total(self):
        self.assert_invalid(log(epoch_records(rows=[row(**counters())])))

    def test_os_failure_and_arena_zeroing_semantics(self):
        records = [row(zero_requested=7, zero_written=3, padding=9)]
        for kind in census.KINDS[1:]:
            records.append(row(kind=kind, calls=2, bytes=8192, empty=0, small=0, maximum=4096,
                               failures=1, failed_bytes=4096))
        report = census.parse_census(log(epoch_records(rows=records)))
        for kind in census.KINDS[1:]:
            self.assertEqual(report["totals"][kind]["failed_bytes"], 4096)
        self.assertEqual(report["totals"]["arena"]["zero_written"], 3)
        for changes in (dict(padding=1), dict(zero_requested=1), dict(zero_written=1),
                        dict(failures=2, failed_bytes=6), dict(failed_bytes=8)):
            with self.subTest(changes=changes):
                self.assert_invalid(log(epoch_records(rows=[row(kind="os_commit", **changes)])))

    def test_every_os_kind_is_independently_reconciled(self):
        for kind in census.KINDS[1:]:
            with self.subTest(kind=kind):
                records = epoch_records(rows=[row(), row(kind=kind)])
                # Both counter sets are individually valid; the independent total
                # must still detect missing OS bytes with unchanged arena totals.
                for index, record in enumerate(records):
                    if record.startswith(f"BUSTER_ALLOC_TOTAL_V2\t1\t{kind}\t"):
                        parts = record.split("\t")
                        parts[4] = parts[10] = "8"
                        records[index] = "\t".join(parts)
                self.assert_invalid(log(records))

    def test_uint64_boundaries_and_unbounded_cross_epoch_aggregation(self):
        maximum = (1 << 64) - 1
        records = epoch_records(1, [row(bytes=maximum, small=0, maximum=maximum)])
        records += epoch_records(2, [row(2, bytes=maximum, small=0, maximum=maximum)])
        report = census.parse_census(log(records, final=2))
        self.assertEqual(report["totals"]["arena"]["bytes"], 2 * maximum)
        self.assertEqual(report["sites"][0]["maximum"], maximum)


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="allocation-census-test-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "compiler.log"
        self.output = self.directory / "export"
        self.raw = ("diagnostic\r\n" + log(epoch_records(rows=[row(file="a\ncomma,quote\".c")]))).encode("utf-8")
        self.source.write_bytes(self.raw)

    def test_exact_raw_hash_provenance_json_and_csv(self):
        metadata = self.directory / "metadata.json"
        metadata_raw = b'{"compiler_sha256":"unverified", "argv":["cc","input.c"]}\n'
        metadata.write_bytes(metadata_raw)
        report = census.export_census(self.source, self.output, label="captured run", provenance_path=metadata)
        self.assertEqual(report, json.loads((self.output / "report.json").read_text(encoding="utf-8")))
        digest = hashlib.sha256(self.raw).hexdigest()
        self.assertEqual(report["raw_log"], {"path": str(self.source), "bytes": len(self.raw), "sha256": digest})
        supplied = report["provenance"]["supplied_metadata"]
        self.assertEqual(supplied["sha256"], hashlib.sha256(metadata_raw).hexdigest())
        self.assertEqual(supplied["data"], json.loads(metadata_raw))
        self.assertIn("not independently verified", supplied["authority"])
        self.assertEqual(report["provenance"]["label"], "captured run")
        with (self.output / "sites.csv").open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(rows[0]["file"], 'a\ncomma,quote".c')
        self.assertEqual(rows[0]["raw_log_sha256"], digest)
        self.assertEqual(rows[0]["bytes"], "7")

    def test_directory_publish_contains_both_complete_exports(self):
        real_rename = os.rename
        observed = []

        def publish(source, destination):
            self.assertFalse(destination.exists())
            self.assertEqual({path.name for path in source.iterdir()}, {"report.json", "sites.csv"})
            self.assertTrue(json.loads((source / "report.json").read_text())["completed"])
            with (source / "sites.csv").open(newline="") as stream:
                self.assertEqual(len(list(csv.DictReader(stream))), 1)
            observed.append(destination)
            real_rename(source, destination)

        with mock.patch.object(census.os, "rename", side_effect=publish):
            census.export_census(self.source, self.output)
        self.assertEqual(observed, [self.output])

    def test_invalid_or_truncated_input_publishes_nothing(self):
        for raw in (self.raw[:-1], b"not a census\n", b"\xff" + self.raw):
            with self.subTest(raw=raw[:15]):
                self.source.write_bytes(raw)
                with self.assertRaises(ValueError):
                    census.export_census(self.source, self.output)
                self.assertEqual({path.name for path in self.directory.iterdir()}, {"compiler.log"})

    def test_export_write_failure_removes_staging_and_publishes_nothing(self):
        with mock.patch.object(census.csv, "DictWriter", side_effect=OSError("simulated full disk")):
            with self.assertRaises(OSError):
                census.export_census(self.source, self.output)
        self.assertEqual({path.name for path in self.directory.iterdir()}, {"compiler.log"})

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        sentinel = self.output / "existing.txt"
        sentinel.write_text("preserve")
        with self.assertRaises(ValueError):
            census.export_census(self.source, self.output)
        self.assertEqual(sentinel.read_text(), "preserve")
        self.assertEqual(list(self.output.iterdir()), [sentinel])

    def test_invalid_provenance_publishes_nothing(self):
        metadata = self.directory / "metadata.json"
        for content in ("[]", "null", "{bad}", '{"bad":NaN}'):
            with self.subTest(content=content):
                metadata.write_text(content)
                with self.assertRaises(ValueError):
                    census.export_census(self.source, self.output, provenance_path=metadata)
                self.assertFalse(self.output.exists())
                self.assertFalse(list(self.directory.glob(".export.*")))

    def test_cli_success_and_failure_exit_codes(self):
        arguments = ["allocation_census.py", "--input", str(self.source), "--output", str(self.output)]
        output = io.StringIO()
        with mock.patch.object(census.sys, "argv", arguments), contextlib.redirect_stdout(output):
            self.assertEqual(census.main(), 0)
        self.assertIn("completed=true epochs=1 sites=1", output.getvalue())
        errors = io.StringIO()
        with mock.patch.object(census.sys, "argv", arguments), contextlib.redirect_stderr(errors):
            self.assertEqual(census.main(), 2)
        self.assertIn("output already exists", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
