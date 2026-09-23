#!/usr/bin/env python3
"""Network-free tests of cache integrity and failure-summary contracts."""
import copy
import csv
import hashlib
import io
import json
import os
import re
import shutil
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import textwrap
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import check_action_pins
import ci_pack_evidence
import ci_summary
import ci_zig
import github_ci_time
import native_retirement_archive
import native_retirement_contract


class ZigResponse:
    def __init__(self, payload, chunks=None, declared_length=None):
        self.stream = io.BytesIO(payload)
        self.chunks = iter(chunks or ())
        self.declared_length = declared_length
        self.read_sizes = []
        self.headers = {} if declared_length is None else {"Content-Length": str(declared_length)}

    def __enter__(self):
        return self

    def __exit__(self, *arguments):
        self.stream.close()

    def read(self, size=-1):
        self.read_sizes.append(size)
        if size == 0:
            return b""
        chunk_size = next(self.chunks, size)
        return self.stream.read(min(size, chunk_size))


class ZigTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest_data = json.loads((ROOT / ".github/zig.json").read_text())
        self.payload = b"verified test fixture"
        digest = hashlib.sha256(self.payload).hexdigest()
        self.manifest_data["sha256"]["x86_64-linux"] = digest
        self.manifest = self.root / "zig.json"
        self.manifest.write_text(json.dumps(self.manifest_data))

    def test_manifest_covers_every_platform(self):
        expected_sizes = {
            "x86_64-linux": 55478392,
            "aarch64-linux": 51211944,
            "x86_64-macos": 57396836,
            "aarch64-macos": 52238004,
            "x86_64-windows": 97217739,
            "aarch64-windows": 93109828,
        }
        for target in ci_zig.TARGETS:
            version, digest, size = ci_zig.load_pin(ROOT / ".github/zig.json", target)
            self.assertEqual(version, "0.16.0")
            self.assertEqual(len(digest), 64)
            self.assertEqual(size, expected_sizes[target])

    def test_reject_incomplete_or_mutable_pins(self):
        for field, value in (("version", "latest"), ("version", "../bad"),
                             ("sha256", {}), ("size", {}),
                             ("size", {target: True for target in ci_zig.TARGETS})):
            with self.subTest(field=field, value=value):
                data = copy.deepcopy(self.manifest_data)
                data[field] = value
                self.manifest.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    ci_zig.load_pin(self.manifest, "x86_64-linux")
        with self.assertRaises(ValueError):
            ci_zig.load_pin(ROOT / ".github/zig.json", "../../evil")

    def test_all_digests_are_validated(self):
        self.manifest_data["sha256"]["aarch64-windows"] = "not-a-digest"
        self.manifest.write_text(json.dumps(self.manifest_data))
        with self.assertRaises(ValueError):
            ci_zig.load_pin(self.manifest, "x86_64-linux")

    def test_corrupt_cache_never_executes_or_downloads(self):
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "archive").write_bytes(b"poisoned cache")
        with mock.patch.object(ci_zig, "download_archive") as download, mock.patch.object(ci_zig.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                ci_zig.install("x86_64-linux", self.manifest, cache, self.root / "install")
            download.assert_not_called()
            run.assert_not_called()

    def test_hit_revalidates_and_installs(self):
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "archive").write_bytes(self.payload)
        output = self.root / "path"
        with mock.patch.object(ci_zig, "download_archive") as download, mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            ci_zig.install("x86_64-linux", self.manifest, cache, self.root / "install", output)
            self.assertEqual(run.call_count, 2)
            download.assert_not_called()
        self.assertEqual(output.read_text().strip(), str((self.root / "install").resolve()))

    @unittest.skipIf(os.name == "nt", "Creating symlinks requires Windows developer privileges")
    def test_install_path_resolves_symlinked_temporary_directory(self):
        real = self.root / "real"
        real.mkdir()
        alias = self.root / "alias"
        alias.symlink_to(real, target_is_directory=True)
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "archive").write_bytes(self.payload)
        output = self.root / "path"
        with mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            ci_zig.install("x86_64-linux", self.manifest, cache, alias / "install", output)
        self.assertEqual(output.read_text().strip(), str((real / "install").resolve()))

    def test_bad_download_never_extracts(self):
        def bad_download(url, destination, max_bytes):
            self.assertEqual(max_bytes, self.manifest_data["size"]["x86_64-linux"])
            Path(destination).write_bytes(b"bad download")
        with mock.patch.object(ci_zig, "download_archive", side_effect=bad_download), mock.patch.object(ci_zig.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                ci_zig.install("x86_64-linux", self.manifest, self.root / "cache", self.root / "install")
            run.assert_not_called()

    def test_network_retries_are_bounded_and_partial_is_removed(self):
        target = self.root / "archive"
        with mock.patch.object(ci_zig.urllib.request, "urlopen", side_effect=OSError("offline")) as request, mock.patch.object(ci_zig.time, "sleep"):
            with self.assertRaises(OSError):
                ci_zig.download_archive("https://ziglang.org/test", target, max_bytes=4)
            self.assertEqual(request.call_count, 3)
            self.assertFalse(target.exists())
            self.assertFalse(target.with_name("archive.part").exists())

    def test_download_rejects_receipt_over_budget_before_commit(self):
        target = self.root / "archive"
        with mock.patch.object(ci_zig.urllib.request, "urlopen", return_value=ZigResponse(b"12345")):
            with self.assertRaisesRegex(ValueError, "maximum"):
                ci_zig.download_archive("https://ziglang.org/test", target, max_bytes=4)
        self.assertFalse(target.exists())
        self.assertFalse(target.with_name("archive.part").exists())

    def test_download_budget_boundaries_include_empty_and_exact_receipts(self):
        cases = (("empty", b"", 0, True), ("below", b"abc", 4, True),
                 ("exact", b"abcd", 4, True), ("over", b"abcde", 4, False))
        for name, payload, maximum, succeeds in cases:
            with self.subTest(name=name):
                target = self.root / name
                response = ZigResponse(payload)
                with mock.patch.object(ci_zig.urllib.request, "urlopen", return_value=response):
                    if succeeds:
                        ci_zig.download_archive("https://ziglang.org/test", target, maximum)
                        self.assertEqual(target.read_bytes(), payload)
                    else:
                        with self.assertRaisesRegex(ValueError, "maximum"):
                            ci_zig.download_archive("https://ziglang.org/test", target, maximum)
                        self.assertFalse(target.exists())
                self.assertFalse(target.with_name(target.name + ".part").exists())

    def test_download_uses_actual_bytes_with_missing_or_misleading_lengths(self):
        target = self.root / "short"
        with mock.patch.object(ci_zig.urllib.request, "urlopen",
                               return_value=ZigResponse(b"abc", chunks=[1, 1, 1], declared_length=999)):
            ci_zig.download_archive("https://ziglang.org/test", target, 4)
        self.assertEqual(target.read_bytes(), b"abc")

        target = self.root / "long"
        writes = []
        original_open = Path.open
        partial = target.with_name(target.name + ".part")

        def recording_open(path, mode="r", *arguments, **keywords):
            stream = original_open(path, mode, *arguments, **keywords)
            if path != partial:
                return stream

            class RecordingWriter:
                def __enter__(self):
                    stream.__enter__()
                    return self

                def __exit__(self, *exit_arguments):
                    return stream.__exit__(*exit_arguments)

                def write(self, data):
                    writes.append(bytes(data))
                    return stream.write(data)

            return RecordingWriter()

        response = ZigResponse(b"abcde", chunks=[2, 1, 2], declared_length=1)
        with mock.patch.object(ci_zig.urllib.request, "urlopen", return_value=response), \
                mock.patch.object(ci_zig.Path, "open", new=recording_open):
            with self.assertRaisesRegex(ValueError, "maximum"):
                ci_zig.download_archive("https://ziglang.org/test", target, 4)
        self.assertLessEqual(sum(map(len, writes)), 4)
        self.assertFalse(target.exists())
        self.assertFalse(partial.exists())
        self.assertEqual(response.read_sizes[-1], 2)

        exact = ZigResponse(b"abcd", chunks=[4], declared_length=999)
        with mock.patch.object(ci_zig.urllib.request, "urlopen", return_value=exact):
            ci_zig.download_archive("https://ziglang.org/test", self.root / "exact", 4)
        self.assertIn(1, exact.read_sizes)

    def test_read_failure_retries_and_cleans_each_partial_attempt(self):
        class ReadFailure(ZigResponse):
            def read(self, size=-1):
                if self.stream.tell() >= 2:
                    raise OSError("read failed")
                return super().read(size)

        target = self.root / "read-failure"
        partial = target.with_name(target.name + ".part")
        attempt_writes = []
        original_open = Path.open

        def recording_open(path, mode="r", *arguments, **keywords):
            stream = original_open(path, mode, *arguments, **keywords)
            if path != partial:
                return stream
            current = []

            class RecordingWriter:
                def __enter__(self):
                    stream.__enter__()
                    return self

                def __exit__(self, *exit_arguments):
                    attempt_writes.append(sum(current))
                    return stream.__exit__(*exit_arguments)

                def write(self, data):
                    current.append(len(data))
                    return stream.write(data)

            return RecordingWriter()

        responses = [ReadFailure(b"abcdef", chunks=[2]) for _ in range(3)]
        with mock.patch.object(ci_zig.urllib.request, "urlopen", side_effect=responses) as request, \
                mock.patch.object(ci_zig.Path, "open", new=recording_open), \
                mock.patch.object(ci_zig.time, "sleep"):
            with self.assertRaises(OSError):
                ci_zig.download_archive("https://ziglang.org/test", target, 8)
        self.assertEqual(request.call_count, 3)
        self.assertEqual(attempt_writes, [2, 2, 2])
        self.assertTrue(all(written <= 8 for written in attempt_writes))
        self.assertFalse(target.exists())
        self.assertFalse(partial.exists())

    def test_write_failure_retries_and_cleans_partial(self):
        target = self.root / "write-failure"
        partial = target.with_name(target.name + ".part")
        original_open = Path.open

        def failing_open(path, mode="r", *arguments, **keywords):
            stream = original_open(path, mode, *arguments, **keywords)
            if path != partial:
                return stream

            class BrokenWriter:
                def __enter__(self):
                    stream.__enter__()
                    return self

                def __exit__(self, *exit_arguments):
                    return stream.__exit__(*exit_arguments)

                def write(self, data):
                    raise OSError("write failed")

            return BrokenWriter()

        responses = [ZigResponse(b"abc") for _ in range(3)]
        with mock.patch.object(ci_zig.urllib.request, "urlopen", side_effect=responses) as request, \
                mock.patch.object(ci_zig.Path, "open", new=failing_open), \
                mock.patch.object(ci_zig.time, "sleep"):
            with self.assertRaises(OSError):
                ci_zig.download_archive("https://ziglang.org/test", target, 4)
        self.assertEqual(request.call_count, 3)
        self.assertFalse(target.exists())
        self.assertFalse(partial.exists())

    def test_cold_install_passes_pinned_budget_and_publishes_only_after_success(self):
        cache = self.root / "cache"
        output = self.root / "path"
        calls = []

        def download(url, destination, maximum):
            calls.append((url, destination, maximum))
            Path(destination).write_bytes(self.payload)

        with mock.patch.object(ci_zig, "download_archive", side_effect=download), \
                mock.patch.object(ci_zig.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, stdout="0.16.0\n")
            ci_zig.install("x86_64-linux", self.manifest, cache, self.root / "install", output)
        self.assertEqual(calls[0][2], self.manifest_data["size"]["x86_64-linux"])
        self.assertEqual(output.read_text().strip(), str((self.root / "install").resolve()))

    def test_over_budget_download_never_extracts_or_publishes_path(self):
        self.manifest_data["size"]["x86_64-linux"] = 4
        self.manifest.write_text(json.dumps(self.manifest_data))
        output = self.root / "path"
        cache = self.root / "cache"
        with mock.patch.object(ci_zig.urllib.request, "urlopen", return_value=ZigResponse(b"abcde")), \
                mock.patch.object(ci_zig.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "maximum"):
                ci_zig.install("x86_64-linux", self.manifest, cache, self.root / "install", output)
        run.assert_not_called()
        self.assertFalse((cache / "archive").exists())
        self.assertFalse((cache / "archive.part").exists())
        self.assertFalse((self.root / "install").exists())
        self.assertFalse(output.exists())

    def test_version_mismatch_does_not_publish_installation_or_path(self):
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "archive").write_bytes(self.payload)
        output = self.root / "path"
        results = [subprocess.CompletedProcess([], 0),
                   subprocess.CompletedProcess([], 0, stdout="0.15.2\n")]
        with mock.patch.object(ci_zig.subprocess, "run", side_effect=results):
            with self.assertRaisesRegex(ValueError, "pinned version"):
                ci_zig.install("x86_64-linux", self.manifest, cache, self.root / "install", output)
        self.assertFalse((self.root / "install").exists())
        self.assertFalse(output.exists())

    def test_extraction_failure_does_not_publish_path(self):
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "archive").write_bytes(self.payload)
        output = self.root / "path"
        with mock.patch.object(ci_zig.subprocess, "run", side_effect=subprocess.CalledProcessError(2, "tar")):
            with self.assertRaises(subprocess.CalledProcessError):
                ci_zig.install("x86_64-linux", self.manifest, cache, self.root / "install", output)
        self.assertFalse(output.exists())
        self.assertFalse((self.root / "install").exists())

    def test_installation_is_not_overwritten(self):
        installed = self.root / "install"
        installed.mkdir()
        with self.assertRaisesRegex(ValueError, "already exists"):
            ci_zig.install("x86_64-linux", self.manifest, self.root / "cache", installed)


class SummaryTests(unittest.TestCase):
    def test_missing_skipped_cancelled_and_failed_are_not_success(self):
        for outcome in (None, "skipped", "cancelled", "failure"):
            with self.subTest(outcome=outcome):
                steps = {} if outcome is None else {"test": {"outcome": outcome}}
                self.assertEqual(ci_summary.assess(steps, ["test"]), ["test"])

    def test_continue_on_error_cannot_disguise_failure(self):
        steps = {"test": {"outcome": "failure", "conclusion": "success"}}
        self.assertEqual(ci_summary.assess(steps, ["test"]), ["test"])

    def test_platform_inapplicable_steps_can_be_skipped(self):
        steps = {"test": {"outcome": "success"}, "windows": {"outcome": "skipped"}}
        self.assertEqual(ci_summary.assess(steps, ["test"]), [])

    def test_report_escapes_metadata_and_does_not_copy_secrets(self):
        with tempfile.TemporaryDirectory() as temporary:
            environment = {"RUNNER_TEMP": temporary, "BUSTER_CI_STEPS": '{"test":{"outcome":"success"}}',
                           "BUSTER_CI_REQUIRED": "test", "GITHUB_REF": "<script>alert(1)</script>",
                           "GITHUB_TOKEN": "must-not-appear", "BUSTER_CI_REPRO": "echo '<b>'"}
            self.assertEqual(ci_summary.write_report(environment), 0)
            directory = Path(temporary) / "buster-ci"
            text = (directory / "summary.md").read_text()
            self.assertIn("&lt;script&gt;", text)
            self.assertNotIn("<script>", text)
            self.assertNotIn("must-not-appear", text + (directory / "result.json").read_text())
            environment["BUSTER_CI_STEPS"] = "{}"
            self.assertEqual(ci_summary.write_report(environment), 1)
            self.assertFalse(json.loads((directory / "result.json").read_text())["success"])

    def test_required_list_must_be_explicit(self):
        with self.assertRaises(ValueError):
            ci_summary.write_report({"BUSTER_CI_STEPS": "{}"})


class EvidencePackTests(unittest.TestCase):
    KEEP = {
        "modes.log": b"modes\n",
        "result.json": b'{"success": true}\n',
        "summary.md": b"## Buster CI\n",
        ".hidden.log": b"dotfiles are evidence too\n",
        "program": b"outside differential/\n",
        "other/subject.o": b"outside differential/\n",
        "differential/processes.tsv": b"phase\telapsed_us\n",
        "differential/case/config/link.argv": b"clang\0-O0\0subject.o\0",
        "differential/case/config/run.stderr": b"",
        "differential/case/config/program.log": b"not a generated program\n",
        "differential/subject.o.extra": b"not a generated object\n",
        "differential/nested case/with spaces/run.stdout": b"\x00\xff binary\n",
        "differential/" + "d" * 60 + "/" + "f" * 60 + ".stdout": b"beyond the ustar name limit\n",
    }
    GENERATED = ("differential/program", "differential/subject.o",
                 "differential/case/config/program", "differential/nested case/with spaces/subject.o")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        # Laid out like RUNNER_TEMP so the workflow's own command can run here.
        self.root = Path(self.temporary.name) / "runner temp"
        self.source = self.root / "buster-ci"
        self.output = self.root / "native-ci-upload"
        for name, data in list(self.KEEP.items()) + [(name, b"generated\n") for name in self.GENERATED]:
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (self.source / "differential/empty case").mkdir()

    def members(self):
        with tarfile.open(self.output / ci_pack_evidence.ARCHIVE, "r:gz") as bundle:
            members = bundle.getmembers()
            files = {member.name: bundle.extractfile(member).read() for member in members if member.isfile()}
        return files, {member.name for member in members if member.isdir()}

    def native_steps(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        native = text.split("\n  native:", 1)[1].split("\n  mobile:", 1)[0]
        return dict(re.findall(r"(?ms)^      - name: ([^\n]+)\n(.*?)(?=^      - name:|\Z)", native))

    def test_archive_round_trips_every_retained_file_and_only_excludes_generated_outputs(self):
        self.assertEqual(ci_pack_evidence.pack(self.source, self.output), (len(self.KEEP), len(self.GENERATED)))
        files, directories = self.members()
        self.assertEqual(files, {"buster-ci/" + name: data for name, data in self.KEEP.items()})
        self.assertIn("buster-ci/differential/empty case", directories)
        self.assertEqual(sorted(path.name for path in self.output.iterdir()),
                         sorted((ci_pack_evidence.ARCHIVE,) + ci_pack_evidence.SUMMARIES))
        for name in ci_pack_evidence.SUMMARIES:
            self.assertEqual((self.output / name).read_bytes(), self.KEEP[name])
        # The original tree stays on disk for any later step.
        for name in list(self.KEEP) + list(self.GENERATED):
            self.assertTrue((self.source / name).is_file())

    @unittest.skipIf(os.name == "nt", "POSIX permission bits")
    def test_permission_bits_are_retained(self):
        script = self.source / "differential/case/reproduce.sh"
        script.write_bytes(b"#!/bin/sh\n")
        script.chmod(0o755)
        directory = script.parent
        directory.chmod(0o750)
        stamp = 1700000000.125
        os.utime(directory, (stamp, stamp))
        ci_pack_evidence.pack(self.source, self.output)
        with tarfile.open(self.output / ci_pack_evidence.ARCHIVE, "r:gz") as bundle:
            self.assertEqual(bundle.getmember("buster-ci/differential/case/reproduce.sh").mode, 0o755)
            member = bundle.getmember("buster-ci/differential/case")
            self.assertEqual(member.mode, 0o750)
            self.assertEqual(member.mtime, stamp)

    def test_missing_input_overlapping_output_links_and_special_files_are_refused(self):
        with self.assertRaisesRegex(ValueError, "missing evidence"):
            ci_pack_evidence.pack(self.root / "absent", self.output)
        result = subprocess.run([sys.executable, str(ROOT / "tools/ci_pack_evidence.py"),
                                 "--source", str(self.root / "absent"), "--output", str(self.output)],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing evidence directory", result.stderr)
        for output in (self.source, self.source / "upload", self.root):
            with self.subTest(output=output), self.assertRaisesRegex(ValueError, "separate"):
                ci_pack_evidence.pack(self.source, output)
        if hasattr(os, "mkfifo"):
            fifo = self.source / "differential/fifo"
            os.mkfifo(fifo)
            with self.assertRaisesRegex(ValueError, "special file"):
                ci_pack_evidence.pack(self.source, self.output)
            fifo.unlink()
        try:
            os.symlink(self.root / "outside", self.source / "differential/link")
        except (OSError, NotImplementedError):
            self.skipTest("symbolic links are unavailable to this user")
        with self.assertRaisesRegex(ValueError, "symbolic link"):
            ci_pack_evidence.pack(self.source, self.output)
        self.assertFalse((self.output / ci_pack_evidence.ARCHIVE).exists())

    def test_linked_roots_are_refused_but_system_ancestor_aliases_work(self):
        alias = self.root / "source-link"
        try:
            alias.symlink_to(self.source, target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest("symbolic links are unavailable to this user")
        with self.assertRaisesRegex(ValueError, "symbolic link"):
            ci_pack_evidence.pack(alias, self.output)
        parent_alias = self.root / "parent-link"
        parent_alias.symlink_to(self.source.parent, target_is_directory=True)
        ci_pack_evidence.pack(parent_alias / self.source.name, self.output)
        files, _ = self.members()
        self.assertEqual(set(files), {"buster-ci/" + name for name in self.KEEP})

    def test_failed_write_leaves_no_stale_or_partial_upload(self):
        self.output.mkdir(parents=True)
        for name in (ci_pack_evidence.ARCHIVE,) + ci_pack_evidence.SUMMARIES:
            (self.output / name).write_bytes(b"stale from an earlier pack")
        with mock.patch.object(ci_pack_evidence.tarfile.TarFile, "addfile", side_effect=OSError("disk full")):
            with self.assertRaisesRegex(OSError, "disk full"):
                ci_pack_evidence.pack(self.source, self.output)
        self.assertEqual(list(self.output.iterdir()), [])

    def test_summary_write_close_verify_and_publish_failures_leave_no_bundle(self):
        original_write = Path.write_bytes
        original_close = ci_pack_evidence.tarfile.TarFile.close

        def fail_summary(path, data):
            if path.name == "summary.md":
                raise OSError("summary close failed")
            return original_write(path, data)

        def fail_close(bundle):
            writing = bundle.mode == "w" and not bundle.closed
            original_close(bundle)
            if writing:
                raise OSError("archive close failed")

        failures = (
            mock.patch.object(Path, "write_bytes", fail_summary),
            mock.patch.object(ci_pack_evidence.tarfile.TarFile, "close", fail_close),
            mock.patch.object(ci_pack_evidence, "verify", side_effect=ValueError("verify failed")),
            mock.patch.object(ci_pack_evidence.os, "replace", side_effect=OSError("publish failed")),
        )
        for failure in failures:
            with self.subTest(failure=failure), failure, self.assertRaises((OSError, ValueError)):
                ci_pack_evidence.pack(self.source, self.output)
            self.assertFalse((self.output / ci_pack_evidence.ARCHIVE).exists())
            self.assertEqual(list(self.root.glob("native-ci-upload.partial-*")), [])
            for name, data in self.KEEP.items():
                self.assertEqual((self.source / name).read_bytes(), data)

    def test_missing_summary_and_missing_source_remove_stale_success(self):
        ci_pack_evidence.pack(self.source, self.output)
        (self.source / "summary.md").unlink()
        with self.assertRaisesRegex(ValueError, "missing evidence summaries"):
            ci_pack_evidence.pack(self.source, self.output)
        self.assertEqual(list(self.output.iterdir()), [])
        for name in (ci_pack_evidence.ARCHIVE,) + ci_pack_evidence.SUMMARIES:
            (self.output / name).write_bytes(b"stale")
        with self.assertRaisesRegex(ValueError, "missing evidence directory"):
            ci_pack_evidence.pack(self.root / "absent", self.output)
        self.assertEqual(list(self.output.iterdir()), [])

    def test_summary_copies_match_the_archived_snapshot(self):
        original_verify = ci_pack_evidence.verify

        def change_source_after_archive(*args):
            original_verify(*args)
            (self.source / "result.json").write_bytes(b"changed after archive verification")

        with mock.patch.object(ci_pack_evidence, "verify", change_source_after_archive):
            ci_pack_evidence.pack(self.source, self.output)
        files, _ = self.members()
        self.assertEqual((self.output / "result.json").read_bytes(), files["buster-ci/result.json"])

    def test_verification_rejects_changed_missing_and_extra_members(self):
        ci_pack_evidence.pack(self.source, self.output)
        archive = self.output / ci_pack_evidence.ARCHIVE
        files, directories = self.members()
        digests = {name: hashlib.sha256(data).digest() for name, data in files.items()}
        ci_pack_evidence.verify(archive, digests, directories)
        changed = dict(digests)
        changed["buster-ci/modes.log"] = hashlib.sha256(b"different bytes").digest()
        missing = dict(digests)
        del missing["buster-ci/modes.log"]
        extra = dict(digests)
        extra["buster-ci/unpacked.log"] = hashlib.sha256(b"").digest()
        for expected, folders in ((changed, directories), (missing, directories), (extra, directories),
                                  (digests, directories - {"buster-ci/differential/empty case"})):
            with self.subTest(), self.assertRaisesRegex(ValueError, "differ"):
                ci_pack_evidence.verify(archive, expected, folders)

    def test_workflow_packs_after_the_summary_and_never_loses_evidence(self):
        steps = self.native_steps()
        self.assertEqual(list(steps)[-9:], [
            "Native result and reproduction", "Pack native logs", "Retain native logs",
            "Back off before retrying native log upload", "Retain native logs (retry)",
            "Record recovered native log upload", "Record failed native log upload",
            "Record native packaging failure", "Retain unpacked native logs",
        ])
        pack, packed, backoff, retry, recovered, failed_upload, failed_pack, unpacked = (
            steps[name] for name in (
                "Pack native logs", "Retain native logs", "Back off before retrying native log upload",
                "Retain native logs (retry)", "Record recovered native log upload",
                "Record failed native log upload", "Record native packaging failure",
                "Retain unpacked native logs"))
        self.assertIn("id: pack\n", pack)
        self.assertIn("if: ${{ !cancelled() && steps.checkout.outcome == 'success' }}", pack)
        self.assertIn("if: ${{ !cancelled() && steps.pack.outcome == 'success' }}", packed)
        self.assertIn("id: native_upload\n", packed)
        self.assertIn("continue-on-error: true\n", packed)
        self.assertNotIn("overwrite: true", packed)
        self.assertIn("steps.native_upload.outcome == 'failure'", backoff)
        self.assertIn("sleep 15", backoff)
        self.assertIn("if: ${{ !cancelled() && steps.pack.outcome == 'success' && steps.native_upload.outcome == 'failure' }}", retry)
        self.assertIn("id: native_upload_retry\n", retry)
        self.assertNotIn("continue-on-error", retry)
        self.assertIn("overwrite: true\n", retry)
        self.assertIn("steps.native_upload_retry.outcome == 'success'", recovered)
        self.assertIn("steps.native_upload_retry.outcome == 'failure'", failed_upload)
        self.assertIn("evidence was not retained", failed_upload)
        # A failed pack still uploads the original tree; it never reaches the packed retry path.
        self.assertIn("if: ${{ !cancelled() && steps.pack.outcome != 'success' }}", unpacked)
        artifact = "name: native-${{ matrix.os }}-${{ matrix.arch }}-${{ github.run_id }}-${{ github.run_attempt }}\n"
        for block in (packed, retry, unpacked):
            self.assertIn(artifact, block)
            self.assertIn("retention-days: 7\n", block)
        self.assertIn("path: ${{ runner.temp }}/native-ci-upload/\n", packed)
        self.assertIn("compression-level: 0\n", packed)
        self.assertIn("if-no-files-found: error\n", packed)
        self.assertIn("!${{ runner.temp }}/buster-ci/differential/**/program\n", unpacked)
        self.assertIn("!${{ runner.temp }}/buster-ci/differential/**/subject.o\n", unpacked)
        self.assertEqual(ci_pack_evidence.GENERATED, frozenset(("program", "subject.o")))
        self.assertIn("steps.pack.outcome != 'success'", failed_pack)
        self.assertIn("BUSTER_CI_REQUIRED: modes differential pack", failed_pack)
        self.assertIn("run: python3 tools/ci_summary.py", failed_pack)
        outcomes = {"modes": {"outcome": "success"}, "differential": {"outcome": "success"},
                    "pack": {"outcome": "failure"}}
        self.assertEqual(ci_summary.assess(outcomes, ["modes", "differential", "pack"]), ["pack"])

    @unittest.skipIf(os.name == "nt", "Native lanes run only on Unix")
    def test_actual_workflow_command_packs_runner_evidence(self):
        command = re.search(r"(?m)^        run: (.+)$", self.native_steps()["Pack native logs"]).group(1)
        environment = dict(os.environ, RUNNER_TEMP=str(self.root))
        result = subprocess.run(["bash", "--noprofile", "--norc", "-c", command], cwd=ROOT,
                                env=environment, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        files, _ = self.members()
        self.assertEqual(set(files), {"buster-ci/" + name for name in self.KEEP})



class NativeRetirementArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.assets = self.root / "assets"
        self.commit = "1" * 40
        self.tree = "2" * 40
        self.compiler = b"archived compiler\n"

    def archive_source(self, name):
        tree = self.root / (name + "-tree")
        tree.mkdir()
        (tree / "source.txt").write_text(name)
        with tarfile.open(self.source / name, "w:gz") as bundle:
            bundle.add(tree, arcname=name.removesuffix(".tar.gz"))

    def make_inputs(self):
        census = self.source / "census.zip"
        with zipfile.ZipFile(census, "w") as bundle:
            bundle.writestr("candidate/evidence/manifest.txt", "complete=1\n")
        evidence = self.root / "evidence"
        (evidence / "strict-binary").mkdir(parents=True)
        (evidence / "strict-differential").mkdir()
        (evidence / "strict-binary/ide").write_bytes(self.compiler)
        (evidence / "strict-binary/source.txt").write_text(self.commit + "\n" + self.tree + "\n")
        compiler_hash = hashlib.sha256(self.compiler).hexdigest()
        (evidence / "strict-binary/sha256.txt").write_text(compiler_hash + "  evidence/strict-binary/ide\n")
        (evidence / "strict-differential/summary.txt").write_text(
            "version=1 failures=0 io_failed=0 configurations=432\n")
        result = {"success": True, "required_steps": ["strict_build", "strict_self_test", "strict_execute"],
                  "steps": {name: {"outcome": "success"} for name in
                            ("strict_build", "strict_self_test", "strict_execute")}}
        (evidence / "result.json").write_text(json.dumps(result) + "\n")
        (evidence / "summary.md").write_text("## Buster CI\n\n**Result: SUCCESS**\n")
        packed = self.root / "native-ci-logs.tar.gz"
        with tarfile.open(packed, "w:gz") as bundle:
            bundle.add(evidence, arcname="evidence")
        strict = self.source / "strict.zip"
        with zipfile.ZipFile(strict, "w", compression=zipfile.ZIP_STORED) as bundle:
            bundle.write(packed, packed.name)
            bundle.write(evidence / "result.json", "result.json")
            bundle.write(evidence / "summary.md", "summary.md")
        for name in ("validation.tar.gz", "candidate.tar.gz", "direct.tar.gz"):
            self.archive_source(name)
        contract = {
            "schema": "test", "release": {"tag": "test", "url": "https://example.invalid/test"},
            "sources": {
                "validation": {"commit": self.commit, "tree": self.tree, "asset": "validation.tar.gz"},
                "candidate": {"commit": self.commit, "tree": self.tree, "asset": "candidate.tar.gz"},
                "direct_oracle": {"commit": self.commit, "tree": self.tree, "asset": "direct.tar.gz"},
            },
            "artifacts": {
                "census": {"archive_name": census.name, "size": census.stat().st_size,
                           "sha256": hashlib.sha256(census.read_bytes()).hexdigest()},
                "strict": {"archive_name": strict.name, "size": strict.stat().st_size,
                           "sha256": hashlib.sha256(strict.read_bytes()).hexdigest(), "part_size": 100},
            },
            "identities": {"candidate_binary_sha256": compiler_hash, "direct_oracle_binary_sha256": "0" * 64,
                           "census_rows": 1, "strict_configurations": 7776},
        }
        path = self.root / "contract.json"
        path.write_text(json.dumps(contract))
        return path

    def test_prepared_archives_round_trip_and_corruption_fails_closed(self):
        contract = self.make_inputs()
        native_retirement_archive.prepare(contract, self.source, self.assets)
        manifest = json.loads((self.assets / native_retirement_archive.MANIFEST_NAME).read_text())
        self.assertGreater(len(manifest["artifacts"]["strict"]["release_assets"]), 1)
        self.assertEqual(manifest["identities"]["strict_candidate_binary_sha256"],
                         hashlib.sha256(self.compiler).hexdigest())
        census_output = self.root / "census-output"
        native_retirement_archive.verify_census(self.assets, census_output, False)
        self.assertEqual((census_output / "candidate/evidence/manifest.txt").read_text(), "complete=1\n")
        strict_output = self.root / "strict-output"
        native_retirement_archive.verify_strict(self.assets, strict_output, False)
        self.assertEqual((strict_output / "archived-ide").read_bytes(), self.compiler)
        damaged = self.assets / manifest["artifacts"]["strict"]["release_assets"][0]
        damaged.write_bytes(b"damage" + damaged.read_bytes())
        with self.assertRaisesRegex(ValueError, "size mismatch"):
            native_retirement_archive.verify_strict(self.assets, self.root / "damaged", False)

    def test_census_upload_download_replay_preserves_hidden_ledger_input(self):
        evidence = self.root / "candidate" / "evidence"
        checkout_git = self.root / "candidate" / ".git" / "objects"
        checkout_git.mkdir(parents=True)
        (checkout_git / "private-object").write_bytes(b"must not be uploaded")
        shard = evidence / "census-integrated-0"
        input_root = shard / "inputs" / "tests"
        input_root.mkdir(parents=True)
        hidden = b"*.generated\n"
        (input_root / ".gitignore").write_bytes(hidden)
        support_row = {
            "path": "tests/.gitignore", "role": "support-file", "compile_obligation": "dependency-only",
            "bytes": str(len(hidden)), "sha256": hashlib.sha256(hidden).hexdigest(),
        }
        input_row = {
            **support_row, "buster_hash_64": "0", "fixture_recipe": "compiler-default", "fixture_flags": "",
        }
        for path, fields, row in (
            (shard / "support-contract.tsv", native_retirement_contract.SUPPORT_FIELDS, support_row),
            (shard / "inputs.tsv", native_retirement_contract.INPUT_FIELDS, input_row),
        ):
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t", lineterminator="\n")
                writer.writeheader()
                writer.writerow(row)
        manifest = {
            "support_contract_sha256": native_retirement_contract.sha256(shard / "support-contract.tsv"),
            "inputs": "1",
        }

        uploaded = self.root / "census-upload.zip"
        with zipfile.ZipFile(uploaded, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
            for path in evidence.rglob("*"):
                if path.is_file():
                    bundle.write(path, Path("evidence") / path.relative_to(evidence))
        downloaded = self.root / "downloaded"
        native_retirement_archive.extract_zip(uploaded, downloaded)
        replayed = downloaded / "evidence" / "census-integrated-0"
        self.assertEqual((replayed / "inputs/tests/.gitignore").read_bytes(), hidden)
        self.assertFalse(any(".git" in path.parts for path in downloaded.rglob("*")))
        native_retirement_contract.validate_inputs(replayed, manifest)

        stripped = self.root / "census-upload-without-hidden.zip"
        with zipfile.ZipFile(stripped, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
            for path in evidence.rglob("*"):
                if path.is_file() and path.name != ".gitignore":
                    bundle.write(path, Path("evidence") / path.relative_to(evidence))
        stripped_download = self.root / "stripped-download"
        native_retirement_archive.extract_zip(stripped, stripped_download)
        with self.assertRaises(AssertionError):
            native_retirement_contract.validate_inputs(
                stripped_download / "evidence" / "census-integrated-0", manifest)

    def test_census_receipt_records_a_nonidentical_clean_rebuild(self):
        contract = self.make_inputs()
        value = json.loads(contract.read_text())
        archived = self.root / "archived-direct"
        rebuilt = self.root / "rebuilt-direct"
        archived.write_bytes(b"archived direct compiler\n")
        rebuilt.write_bytes(b"clean rebuilt direct compiler\n")
        archived_sha256 = hashlib.sha256(archived.read_bytes()).hexdigest()
        value["identities"]["direct_oracle_binary_sha256"] = archived_sha256
        contract.write_text(json.dumps(value))
        native_retirement_archive.prepare(contract, self.source, self.assets)
        manifest = json.loads((self.assets / native_retirement_archive.MANIFEST_NAME).read_text())
        report = self.root / "report.json"
        report.write_text(json.dumps({
            "rows_validated": 1,
            "binaries_sha256": {
                "candidate-ide.exe": manifest["identities"]["candidate_binary_sha256"],
                "baseline-ide.exe": archived_sha256,
            },
            "inputs_sha256": {"tests/input.c": "3" * 64},
        }))
        recorded = self.root / "recorded"
        replayed = self.root / "replayed"
        recorded.mkdir()
        replayed.mkdir()
        (recorded / "results.tsv").write_text("same\n")
        (replayed / "results.tsv").write_text("same\n")
        receipt = self.root / "receipt.json"
        native_retirement_archive.census_receipt(
            self.assets / native_retirement_archive.MANIFEST_NAME, report, recorded, replayed,
            archived, rebuilt, "123", receipt)
        result = json.loads(receipt.read_text())
        self.assertFalse(result["rebuilt_matches_archived"])
        self.assertEqual(result["archived_direct_oracle_sha256"], archived_sha256)
        self.assertEqual(result["rebuilt_direct_oracle_sha256"],
                         hashlib.sha256(rebuilt.read_bytes()).hexdigest())


class NativeRetirementCensusTextTests(unittest.TestCase):
    def test_census_document_counts_match_support_manifest(self):
        manifest = ROOT / "docs/native-retirement-support-v1.tsv"
        with manifest.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        counts = {}
        for row in rows:
            counts[row["role"]] = counts.get(row["role"], 0) + 1
        expected = {
            "subject": 411,
            "negative-diagnostic-fixture": 12,
            "support-file": 72,
            "dormant-custom-language": 64,
        }
        self.assertEqual(counts, expected)
        self.assertEqual(len(rows), sum(expected.values()))
        subject_obligations = {}
        for row in rows:
            if row["role"] == "subject":
                obligation = row["compile_obligation"]
                subject_obligations[obligation] = subject_obligations.get(obligation, 0) + 1
        self.assertEqual(subject_obligations, {
            "supported-object-zero-fallback": 405,
            "registered-non-object-control": 6,
        })
        non_object_controls = [
            row["path"] for row in rows
            if row["role"] == "subject" and
            row["compile_obligation"] == "registered-non-object-control"
        ]
        prose = " ".join((ROOT / "docs/native-retirement-census.md").read_text().split())
        sentence = (f"Its {len(rows)} explicit SHA-256 rows bind every tracked test byte at the approval point: "
                    f"{expected['subject']} subject inputs "
                    f"({subject_obligations['supported-object-zero-fallback']} supported-object subjects and "
                    f"{subject_obligations['registered-non-object-control']} registered non-object controls), "
                    f"{expected['negative-diagnostic-fixture']} "
                    f"registered rejection controls, {expected['support-file']} support files and "
                    f"{expected['dormant-custom-language']} dormant custom-language files.")
        self.assertIn(sentence, prose)
        self.assertIn("The six subject-level non-object controls are", prose)
        for path in non_object_controls:
            self.assertIn(f"`{path}`", prose)
        self.assertNotIn("396 supported-object subjects", prose)
        self.assertNotIn("five non-object controls", prose)
        self.assertNotIn("five subject-level non-object controls", prose)


class WorkflowPolicyTests(unittest.TestCase):
    def test_all_six_platforms_and_commands_remain(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        names = re.findall(r"^          - name: (.+)$", text, re.M)
        self.assertEqual(sorted(names), sorted(github_ci_time.PLATFORMS + github_ci_time.MOBILE + github_ci_time.NATIVE))
        self.assertIn("fail-fast: false", text)
        self.assertIn("test_all_combinations_ci --verbose=1", text)
        self.assertIn("test_mode_matrix --config Release", text)
        self.assertIn("./android/test_ci.sh --all", text)
        self.assertIn("./ios/test_ci.sh --all", text)
        steps = re.findall(r"(?ms)^      - name: ([^\n]+)\n(.*?)(?=^      - name:|\Z)", text)
        tolerated = [
            (name, line.strip())
            for name, block in steps
            for line in block.splitlines()
            if re.match(r"^ {8}continue-on-error\s*:", line)
        ]
        self.assertEqual(tolerated, [("Retain native logs", "continue-on-error: true")])
        self.assertNotIn("BUSTER_INCLUDE_TESTS=OFF", text.split("\n  uefi:", 1)[0])

    def test_integrity_and_security_policy(self):
        for path in (ROOT / ".github/workflows").glob("*.yml"):
            with self.subTest(path=path.name):
                text = path.read_text()
                self.assertNotIn("pull_request_target", text)
                self.assertIn("contents: read", text)
                self.assertIn("persist-credentials: false", text)
                self.assertIn("concurrency:", text)
                self.assertIn("timeout-minutes:", text)
                self.assertEqual(check_action_pins.check_text(text, path), [])
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertNotIn("restore-keys:", text)
        self.assertNotIn("install-vulkan-sdk", text)
        self.assertIn("hashFiles('.github/zig.json')", text)
        workflow_environment = text.split("\njobs:", 1)[0]
        self.assertNotIn("UBSAN_OPTIONS:", workflow_environment)
        self.assertNotIn("detect_leaks=0", text)
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn('set(BUSTER_UBSAN_OPTIONS "halt_on_error=1:exitcode=87:print_stacktrace=1")', cmake)
        self.assertIn('list(APPEND BUSTER_TEST_ENV "UBSAN_OPTIONS=${BUSTER_UBSAN_OPTIONS}")', cmake)
        self.assertNotIn("ENV{UBSAN_OPTIONS}", cmake)

    def test_independent_suites_are_not_guarded_by_prior_test_success(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        condition = re.search(r"id: modes\n        if: (.+)", text).group(1)
        self.assertIn("!cancelled()", condition)
        self.assertNotIn("steps.combinations_", condition)
        mobile = text.split("\n  mobile:", 1)[1].split("\n  complete:", 1)[0]
        self.assertNotIn("needs:", mobile)
        self.assertIn("needs: [lint, test, native, mobile, uefi, analyzer]", text)
        self.assertIn("github.run_id", text.split("concurrency:", 1)[1].split("permissions:", 1)[0])

    def test_windows_runs_native_worker_controls_before_the_combination_matrix(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        block = text.split("      - name: Combination matrix (Windows)", 1)[1].split("      - name:", 1)[0]
        control = block.index("test_differential --self-test")
        self.assertLess(control, block.index("test_all_combinations_ci"))
        self.assertIn("differential-self-test.log", block)
        self.assertIn("if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }", block[control:])

    def test_native_suites_are_independent_and_keep_all_four_unix_runners(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        desktop = text.split("\n  test:", 1)[1].split("\n  native:", 1)[0]
        native = text.split("\n  native:", 1)[1].split("\n  mobile:", 1)[0]
        self.assertNotIn("needs:", native)
        self.assertNotIn("test_mode_matrix", desktop)
        self.assertNotIn("test_differential", desktop.replace("test_differential --self-test", ""))
        self.assertEqual(desktop.count("test_differential --self-test"), 1)
        self.assertNotIn("test_all_combinations_ci", native)
        self.assertIn("fail-fast: false", native)
        self.assertNotIn("actions/download-artifact", native)
        self.assertIn("BUSTER_CI_REQUIRED: modes differential", native)
        entries = re.findall(r"(?m)^          - name: (.+)\n            runner: (.+)$", native)
        self.assertEqual(entries, list(zip(github_ci_time.NATIVE, (
            "ubuntu-26.04", "ubuntu-26.04-arm", "macos-26-intel", "macos-26"))))
        for suite in ("modes", "differential"):
            condition = re.search(r"id: " + suite + r"\n        if: (.+)", native).group(1)
            self.assertIn("!cancelled()", condition)
            self.assertIn("steps.checkout.outcome == 'success'", condition)
            self.assertNotIn("steps.modes", condition)
            self.assertNotIn("steps.combinations", condition)
        self.assertEqual(native.count('"$driver" generate --cc clang --config Release --linker DEFAULT'), 2)
        self.assertIn('if [[ ! -f build/CMakeCache.txt ]]; then', native)
        self.assertIn('"$driver" test_differential --self-test', native)
        self.assertIn('"$driver" test_differential --ide build/Release/ide '
                      '--out "$RUNNER_TEMP/buster-ci/differential" --sanitize-oracle', native)
        self.assertIn("!${{ runner.temp }}/buster-ci/differential/**/program", native)
        self.assertIn("!${{ runner.temp }}/buster-ci/differential/**/subject.o", native)
        primary_upload = native.split("      - name: Retain native logs\n", 1)[1].split(
            "      - name:", 1)[0]
        retry_upload = native.split("      - name: Retain native logs (retry)\n", 1)[1].split(
            "      - name:", 1)[0]
        self.assertIn("id: native_upload", primary_upload)
        self.assertIn("continue-on-error: true", primary_upload)
        self.assertNotIn("overwrite: true", primary_upload)
        self.assertIn("id: native_upload_retry", retry_upload)
        self.assertNotIn("continue-on-error", retry_upload)
        self.assertIn("steps.native_upload.outcome == 'failure'", retry_upload)
        self.assertIn("overwrite: true", retry_upload)
        self.assertIn("sleep 15", native)
        self.assertIn("steps.native_upload_retry.outcome == 'success'", native)
        self.assertIn("steps.native_upload_retry.outcome == 'failure'", native)

    def test_platform_and_bootstrap_events_cover_the_same_revisions(self):
        common = ("  pull_request:", "  push:", "    branches: [main]",
                  "    tags: ['**']", "  merge_group:",
                  "    types: [checks_requested]")
        expected = {
            "ci.yml": common + (
                "  workflow_dispatch:",
                "    inputs:",
                "      cmake_profile:",
                "        description: Retain native per-tree CMake command profiles",
                "        required: false",
                "        default: false",
                "        type: boolean",
                "      analyzer_comparison:",
                "        description: Run an explicit reference/candidate Clang analyzer comparison",
                "        required: false",
                "        default: false",
                "        type: boolean",
            ),
            "self-host-audit.yml": common + ("  workflow_dispatch:",),
        }
        for name in ("ci.yml", "self-host-audit.yml"):
            with self.subTest(workflow=name):
                workflow = (ROOT / ".github/workflows" / name).read_text()
                block = re.search(r"(?ms)^on:(.*?)(?=^[A-Za-z_][\w-]*:|\Z)", workflow)
                self.assertIsNotNone(block)
                lines = tuple(line.rstrip() for line in block.group(1).splitlines()
                              if line.strip() and not line.lstrip().startswith("#"))
                self.assertEqual(lines, expected[name])
                # Default checkout is the PR/merge-group merge revision, not
                # an independently selected head or a stale branch ref.
                self.assertNotRegex(workflow, r"(?m)^\s+(ref|repository):")
        ci = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertIn("inputs.cmake_profile", ci)
        self.assertIn("inputs.analyzer_comparison", ci)
        self.assertNotIn("vars.BUSTER_CMAKE_PROFILE", ci)

    def test_bootstrap_cancellation_is_isolated_by_workflow_and_event(self):
        suffix = ("${{ github.workflow }}-${{ github.event_name }}-"
                  "${{ github.event_name == 'pull_request' && github.event.pull_request.number || "
                  "github.event_name == 'merge_group' && github.ref || github.run_id }}")
        cancel = "${{ github.event_name == 'pull_request' || github.event_name == 'merge_group' }}"
        for name, prefix in (("ci.yml", "ci-"), ("self-host-audit.yml", "bootstrap-")):
            with self.subTest(workflow=name):
                text = (ROOT / ".github/workflows" / name).read_text()
                block = re.search(r"(?ms)^concurrency:(.*?)(?=^[A-Za-z_][\w-]*:|\Z)", text)
                self.assertIsNotNone(block)
                lines = tuple(line.strip() for line in block.group(1).splitlines()
                              if line.strip() and not line.lstrip().startswith("#"))
                self.assertEqual(lines, ("group: " + prefix + suffix, "cancel-in-progress: " + cancel))

    def test_bootstrap_keeps_every_native_gate_in_order(self):
        text = (ROOT / ".github/workflows/self-host-audit.yml").read_text()
        command_text = text[:text.index(
            "      - name: Check the bootstrap probe against independent compiler oracles")]
        matches = re.findall(
            r"(?m)^(?:        run: '\"\$RUNNER_TEMP/buster-build\" ([^']+)'|"
            r"            \"\$RUNNER_TEMP/buster-build\" ([^\n]+))$",
            command_text,
        )
        commands = [inline or block for inline, block in matches]
        self.assertEqual(commands, [
            "self_host_audit_self_test",
            "generate --cc clang --ci --linker DEFAULT",
            "test_self_host --config Release",
            "test_self_host_audit --config Release",
            "build --config Release -t test_all",
        ])
        gates = text[text.index("      - name: Test the bootstrap checker"):].split(
            "      - name: Retain stage evidence even on failure", 1)[0]
        self.assertNotRegex(gates, r"(?m)^\s*continue-on-error:")
        blocks = re.findall(r"(?ms)^      - name: ([^\n]+)\n(.*?)(?=^      - name:|\Z)", gates)
        self.assertEqual([name for name, _ in blocks], [
            "Test the bootstrap checker",
            "Configure production compiler",
            "Preserve ordinary bootstrap and alternate-backend gates",
            "Verify each generation and repeat",
            "Run compiler regressions",
            "Check the bootstrap probe against independent compiler oracles",
        ])
        evidence_gates = {
            "Run compiler regressions",
            "Check the bootstrap probe against independent compiler oracles",
        }
        for name, block in blocks:
            with self.subTest(gate=name):
                conditions = re.findall(r"(?m)^        if: (.+)$", block)
                # These two independent results survive an audit failure, but
                # cannot run before ordinary bootstrap or after cancellation.
                expected = (["${{ !cancelled() && steps.ordinary_bootstrap.outcome == 'success' }}"]
                            if name in evidence_gates else [])
                self.assertEqual(conditions, expected)
        self.assertIn("        id: ordinary_bootstrap\n", dict(blocks)[
            "Preserve ordinary bootstrap and alternate-backend gates"])
        oracle = dict(blocks)["Check the bootstrap probe against independent compiler oracles"]
        self.assertIn('"$RUNNER_TEMP/buster-build" test_differential --self-test', oracle)
        self.assertIn('"$RUNNER_TEMP/buster-build" test_differential --ide build/Release/ide '
                      '--cc clang --source tests/self_host_bootstrap_probe.c --sanitize-oracle '
                      '--out build/self-host-audit/probe-oracle', oracle)
        self.assertNotIn("needs:", text)
        self.assertIn("name: Linux x86-64 bootstrap evidence", text)
        self.assertIn("runs-on: ubuntu-26.04", text)
        self.assertIn("timeout-minutes: 30", text)
        self.assertNotIn("secrets.", text)
        self.assertNotRegex(text, r"(?m)^\s*[^#\n]+: write$")
        artifact = text.split("      - name: Retain stage evidence even on failure", 1)[1]
        self.assertIn("name: bootstrap-evidence-${{ github.sha }}-${{ github.run_id }}-${{ github.run_attempt }}", artifact)
        self.assertIn("if: ${{ !cancelled() }}", artifact)
        self.assertNotIn("always()", artifact)

    def test_retirement_evidence_uses_the_exact_candidate_on_six_native_hosts(self):
        text = (ROOT / ".github/workflows/native-retirement-evidence.yml").read_text()
        self.assertIn("BUSTER_RETIREMENT_CANDIDATE: ${{ github.sha }}", text)
        concurrency = text.split("concurrency:", 1)[1].split("permissions:", 1)[0]
        self.assertIn("github.event_name == 'pull_request' && github.event.pull_request.number || github.run_id", concurrency)
        self.assertIn("cancel-in-progress: ${{ github.event_name == 'pull_request' }}", concurrency)
        self.assertNotIn("ref: 2bb4ce939d99c3956848ca7bc9c347f3ae8db231", text)
        self.assertNotIn("--compiler-revision 2bb4ce939d99c3956848ca7bc9c347f3ae8db231", text)
        self.assertEqual(text.count('test "$(git rev-parse HEAD)" = "$BUSTER_RETIREMENT_CANDIDATE"'), 2)
        self.assertEqual(text.count('--resource-include "$(clang -print-resource-dir)/include"'), 4)
        census_validation = text.split("      - name: Independently validate every census shard and row", 1)[1].split(
            "      - name: Retain raw evidence and build recipes", 1)[0]
        self.assertIn("../validation/tools/native_retirement_contract.py validate-shards", census_validation)
        self.assertEqual(len(re.findall(r"evidence/census-integrated-[0-3]", census_validation)), 4)
        self.assertIn("--out evidence/census-validation-v2.json", census_validation)
        self.assertIn("--require-clean-candidate", census_validation)
        self.assertIn("--require-clean-acceptance", census_validation)
        self.assertIn("--reference-supplements", census_validation)
        self.assertEqual(text.count('--baseline-revision "$BUSTER_RETIREMENT_CANDIDATE"'), 5)
        self.assertIn("../validation/tools/native_retirement_reference.py", text)
        self.assertNotIn("--baseline-ide ../reference/", text)
        self.assertNotIn("join-census.py", census_validation)
        self.assertNotIn("validate-census-v2.py", census_validation)
        upload = text.split("      - name: Retain raw evidence and build recipes", 1)[1].split(
            "\n  strict_differential:", 1)[0]
        self.assertIn("include-hidden-files: true", upload)
        self.assertIn("candidate/evidence/", upload)
        self.assertNotIn("candidate/.git", upload)
        strict = text.split("\n  strict_differential:", 1)[1]
        entries = re.findall(r"(?m)^          - name: (.+)\n            runner: (.+)\n            slug: (.+)\n            platform: (.+)$", strict)
        self.assertEqual(entries, [
            ("Linux x86-64 native", "ubuntu-26.04", "linux-x86_64", "unix"),
            ("Linux AArch64 native", "ubuntu-26.04-arm", "linux-aarch64", "unix"),
            ("macOS x86-64 native", "macos-26-intel", "macos-x86_64", "unix"),
            ("macOS AArch64 native", "macos-26", "macos-aarch64", "unix"),
            ("Windows x86-64 native", "windows-2025", "windows-x86_64", "windows"),
            ("Windows AArch64 native", "windows-11-arm", "windows-aarch64", "windows"),
        ])
        self.assertIn("fail-fast: false", strict)
        self.assertIn("--strict-mir --sanitize-oracle", strict)
        self.assertIn("candidate_commit=%s\\ncandidate_tree=%s\\nrunner=%s\\noracle_sanitizer=%s", strict)
        self.assertIn("candidate_commit=$Commit", strict)
        self.assertIn("sanitizer: required", strict)
        self.assertIn("sanitizer: not-run", strict)
        self.assertIn("runner-llvm-package-omits-aarch64-asan-runtime", strict)
        self.assertIn("Get-ChildItem -Path $ResourceDir -Filter 'clang_rt.asan_dynamic-${{ matrix.clang_arch }}.dll'", strict)
        self.assertIn("if ($RuntimeDlls.Count -ne 1)", strict)
        self.assertIn("oracle_sanitizer_runtime=$RuntimeDll", strict)
        self.assertIn('$env:PATH = "$RuntimeDir;$env:PATH"', strict)
        self.assertIn("$DifferentialArgs += '--sanitize-oracle'", strict)
        self.assertIn("$LibraryPaths = @($env:LIB -split ';'", strict)
        self.assertIn("$DifferentialArgs += @('--library-path', $LibraryPath)", strict)
        self.assertIn("BUSTER_CI_REQUIRED: ${{ matrix.platform == 'windows' && 'strict_windows' ||", strict)
        self.assertIn("strict-retirement-${{ env.BUSTER_RETIREMENT_CANDIDATE }}-${{ matrix.slug }}", strict)
        complete = text.split("\n  complete:", 1)[1]
        self.assertIn("needs: [census, strict_differential]", complete)
        self.assertIn("name: Native retirement acceptance complete", complete)
        self.assertIn('[[ "$CENSUS_RESULT" == success && "$STRICT_RESULT" == success ]]', complete)

    def test_native_retirement_workflow_paths_cover_authoritative_census_inputs(self):
        contract_text = (ROOT / ".github/workflows/native-retirement-contract.yml").read_text()
        contract_paths = contract_text.split("    paths:\n", 1)[1].split("  workflow_dispatch:", 1)[0]
        expected_contract_inputs = (
            ".github/workflows/native-retirement-contract.yml",
            "build.c",
            "docs/agents/build.md",
            "docs/native-retirement-census.md",
            "docs/native-retirement-support-v1.tsv",
            "docs/native-retirement-supported-gaps-v1.tsv",
            "tools/differential.c",
            "tools/native_retirement_census.c",
            "tools/native_retirement_contract.py",
            "tools/native_retirement_contract_test.py",
            "tests/**",
        )
        for path in expected_contract_inputs:
            with self.subTest(workflow="contract", path=path):
                self.assertIn(f"      - {path}\n", contract_paths)

        evidence_text = (ROOT / ".github/workflows/native-retirement-evidence.yml").read_text()
        evidence_paths = evidence_text.split("    paths:\n", 1)[1].split("  workflow_dispatch:", 1)[0]
        self.assertIn("      - docs/native-retirement-supported-gaps-v1.tsv\n", evidence_paths)

    def test_windows_oracle_keeps_the_full_corpus_with_required_link_shims(self):
        differential = (ROOT / "tools/differential.c").read_text()
        frontend = (ROOT / "src/buster/lib/compiler/frontend/c/c_gen.c").read_text()
        machine_test = (ROOT / "src/buster/tests/compiler/codegen/machine_test.c").read_text()
        clear_cache_subject = (ROOT / "tests/differential/clear_cache.c").read_text()
        clear_cache_host = (ROOT / "tests/differential/clear_cache_host.c").read_text()
        self.assertIn("#if BUSTER_WINDOWS", differential)
        self.assertIn('if (!object_only && !test.host.length) { argv[count++] = S8("-llegacy_stdio_definitions"); }', differential)
        self.assertNotIn('if (host && !object_only && !test.host.length)', differential)
        self.assertIn('S8("-L{S8}")', differential)
        self.assertIn('string_equal(arg, S8("--library-path"))', differential)
        self.assertIn("library_path_count={u64}", differential)
        self.assertIn("builder->target.cpu_arch == CPU_ARCH_AARCH64", frontend)
        self.assertIn("builder->target.os == OPERATING_SYSTEM_WINDOWS", frontend)
        self.assertIn('S8("__clear_cache")', frontend)
        self.assertIn("defined(_WIN32)", clear_cache_host)
        self.assertIn("defined(_M_ARM64) || defined(__aarch64__)", clear_cache_host)
        self.assertIn("void __clear_cache(void *begin, void *end)", clear_cache_host)
        self.assertIn("FlushInstructionCache(GetCurrentProcess(), begin, size)", clear_cache_host)
        self.assertIn("ExitProcess(1)", clear_cache_host)
        self.assertIn("__builtin___clear_cache(p + 3, p + 65)", clear_cache_subject)
        self.assertNotIn("__builtin___clear_cache(p + 3, p + 3)", clear_cache_subject)
        self.assertIn("clear_arguments(bytes + start, 65, &first, &second)", clear_cache_host)
        self.assertIn("clear_instruction_count == (target_index == 2 ? 0u : 3u)", machine_test)
        self.assertIn("direct_maintenance_words == 0", machine_test)
        self.assertIn("CODEGEN_MODULE_RELOCATION_AARCH64_CALL26", machine_test)

    def test_actual_aggregate_rejects_missing_skipped_cancelled_and_failed_shards(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        aggregate = text.split("\n  complete:", 1)[1]
        self.assertIn("needs: [lint, test, native, mobile, uefi, analyzer]", aggregate)
        self.assertIn("always()", aggregate)
        # Execute the workflow's real shell body, not a Python copy of its
        # predicate. Exercise all 625 existing shard outcomes with UEFI/analyzer
        # green, then reject unavailable/unsuccessful UEFI and analyzer results.
        body = aggregate.split("        run: |\n", 1)[1]
        body = textwrap.dedent(body)
        with tempfile.TemporaryDirectory() as temporary:
            gate = Path(temporary) / "aggregate.sh"
            gate.write_bytes(body.encode("utf-8"))
            environment = dict(os.environ, BUSTER_CI_GATE=gate.as_posix(),
                               GITHUB_STEP_SUMMARY=(Path(temporary) / "summary.md").as_posix())
            script = r"""
set -eu
checked=0
UEFI_RESULT=success ANALYZER_RESULT=success
export UEFI_RESULT ANALYZER_RESULT
for LINT_RESULT in success failure cancelled skipped ''; do
  for DESKTOP_RESULT in success failure cancelled skipped ''; do
    for NATIVE_RESULT in success failure cancelled skipped ''; do
     for MOBILE_RESULT in success failure cancelled skipped ''; do
      export LINT_RESULT DESKTOP_RESULT NATIVE_RESULT MOBILE_RESULT
      actual=0
      ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
      if [[ "$LINT_RESULT" == success && "$DESKTOP_RESULT" == success && "$NATIVE_RESULT" == success && "$MOBILE_RESULT" == success ]]; then
        [[ "$actual" -eq 0 ]] || exit 1
      else
        [[ "$actual" -ne 0 ]] || exit 1
      fi
      checked=$((checked + 1))
     done
    done
  done
done
LINT_RESULT=success DESKTOP_RESULT=success NATIVE_RESULT=success MOBILE_RESULT=success
export LINT_RESULT DESKTOP_RESULT NATIVE_RESULT MOBILE_RESULT
for UEFI_RESULT in failure cancelled skipped ''; do
  export UEFI_RESULT
  actual=0
  ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
  [[ "$actual" -ne 0 ]] || exit 1
  checked=$((checked + 1))
done
UEFI_RESULT=success
export UEFI_RESULT
for ANALYZER_RESULT in failure cancelled skipped ''; do
  export ANALYZER_RESULT
  actual=0
  ( . "$BUSTER_CI_GATE" ) >/dev/null 2>&1 || actual=$?
  [[ "$actual" -ne 0 ]] || exit 1
  checked=$((checked + 1))
done
printf '%s\n' "$checked"
"""
            # Windows CreateProcess can choose System32/bash.exe (WSL)
            # before PATH. Use an absolute shell path; on Windows select
            # the installed Git Bash, not an unrelated WSL distribution.
            bash = shutil.which("bash")
            if os.name == "nt":
                git = shutil.which("git")
                self.assertIsNotNone(git, "Git for Windows is a CI prerequisite")
                bash = next((str(parent / "bin/bash.exe")
                             for parent in Path(git).resolve().parents
                             if (parent / "bin/bash.exe").is_file()), None)
            self.assertIsNotNone(bash, "Bash is a CI prerequisite")
            result = subprocess.run([bash, "--noprofile", "--norc", "-c", script], env=environment,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), "633")

    @unittest.skipIf(os.name == "nt", "The failure-propagation probe uses the Unix Clang driver")
    def test_recoverable_ubsan_error_is_fatal_with_correctness_environment(self):
        compiler = shutil.which("clang")
        self.assertIsNotNone(compiler, "Clang is a CI prerequisite")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "probe.c"
            executable = root / "probe"
            source.write_text("int main(void) { volatile int x = 2147483647; volatile int y = x + 1; (void)y; return 0; }\n")
            try:
                subprocess.run(
                    [compiler, "-fsanitize=undefined", "-fsanitize-recover=all",
                     str(source), "-o", str(executable)],
                    check=True, timeout=30, capture_output=True, text=True)
            except subprocess.CalledProcessError as error:
                diagnostics = "\n".join(
                    output.strip() for output in (error.stdout, error.stderr)
                    if output and output.strip()) or "(compiler produced no diagnostics)"
                runtime = re.search(
                    r"[^\s:'\"]*libclang_rt\.ubsan[^\s:'\"]*", diagnostics)
                missing = re.search(
                    r"(?:cannot (?:find|open)|unable to find|no such file|not found)",
                    diagnostics, re.IGNORECASE)
                if runtime and missing:
                    self.fail(
                        "UBSan fatality gate unavailable: missing Clang compiler-rt "
                        f"runtime {runtime.group(0)}.\n"
                        f"Compiler diagnostics:\n{diagnostics}")
                self.fail(
                    "UBSan fatality probe compilation failed before policy validation.\n"
                    f"Compiler diagnostics:\n{diagnostics}")
            recovering = subprocess.run([str(executable)], env=dict(os.environ, UBSAN_OPTIONS="halt_on_error=0"), capture_output=True, text=True, timeout=30)
            self.assertEqual(recovering.returncode, 0)
            self.assertIn("runtime error", recovering.stderr)
            environment = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
            result = subprocess.run([str(executable)], env=environment, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("runtime error", result.stderr)


class TimingTests(unittest.TestCase):
    def sample(self, identity=1, duration=60, revision="a"):
        run = {"id": identity, "head_sha": "source", "workflow_blob_sha": revision,
               "status": "completed", "conclusion": "success", "run_attempt": 1,
               "created_at": "2026-09-07T12:00:00Z", "jobs": []}
        from datetime import datetime, timedelta, timezone
        start = datetime(2026, 9, 7, 12, 0, 10, tzinfo=timezone.utc)
        finish = start + timedelta(seconds=duration)
        for name in github_ci_time.PLATFORMS:
            steps = ["Combination matrix (Windows)" if name.startswith("Windows") else "Combination matrix (Linux, macOS)"]
            if not name.startswith("Windows"):
                steps.append("Execution-mode matrix")
            if name.startswith("macOS"):
                steps.append("Test (iOS simulator)")
            if name == "Linux x86-64":
                steps.append("Test (Android)")
            run["jobs"].append({"name": name, "conclusion": "success", "run_attempt": 1,
                                "started_at": start.isoformat(), "completed_at": finish.isoformat(),
                                "labels": [name], "steps": [{"name": step, "conclusion": "success"} for step in steps]})
        return run

    def sharded_sample(self):
        run = self.sample()
        for job in run["jobs"]:
            job["steps"] = [step for step in job["steps"] if not step["name"].startswith("Test (")]
        for name in github_ci_time.MOBILE + ("Workflow lint", "CI complete"):
            job = copy.deepcopy(run["jobs"][0])
            job["name"] = name
            step = ("Test (Android)" if name.startswith("Android") else
                    "Test (iOS simulator)" if name.startswith("iOS") else
                    "Validate every GitHub workflow" if name == "Workflow lint" else "Require every shard")
            job["steps"] = [{"name": step, "conclusion": "success"}]
            run["jobs"].append(job)
        return run

    def test_sharded_matrix_includes_mobile_lint_and_aggregate_cost(self):
        sample, reason = github_ci_time.measure(self.sharded_sample())
        self.assertIsNone(reason)
        self.assertEqual(sample["runner_seconds"], 660)

    def test_sharded_matrix_rejects_missing_or_skipped_work(self):
        for index in range(len(github_ci_time.SHARDED_JOBS)):
            run = self.sharded_sample()
            run["jobs"][index]["steps"] = []
            self.assertIsNone(github_ci_time.measure(run)[0])
            run = self.sharded_sample()
            run["jobs"].pop(index)
            self.assertIsNone(github_ci_time.measure(run)[0])

    def suite_sample(self):
        run = self.sharded_sample()
        for job in run["jobs"]:
            job["steps"] = [step for step in job["steps"] if step["name"] != "Execution-mode matrix"]
        for name in github_ci_time.NATIVE:
            job = copy.deepcopy(run["jobs"][0])
            job["name"] = name
            steps = (("Execution-mode matrix (Windows)",) if name.startswith("Windows") else
                     ("Execution-mode matrix", "Native configuration differential matrix"))
            job["steps"] = [{"name": step, "conclusion": "success"} for step in steps]
            run["jobs"].append(job)
        return run

    def current_sample(self):
        run = self.suite_sample()
        required = {
            "UEFI firmware boot": ("Build compiler and boot both architectures in all allocators",),
            "Clang analyzer shards": ("Exercise analyzer failure and coverage controls",
                                      "Compare reference analysis and aggregate all module shards"),
        }
        for name, steps in required.items():
            job = copy.deepcopy(run["jobs"][0])
            job["name"] = name
            job["labels"] = ["ubuntu-26.04"]
            job["steps"] = [{"name": step, "conclusion": "success"} for step in steps]
            run["jobs"].append(job)
        return run

    def test_suite_matrix_counts_all_seventeen_jobs(self):
        sample, reason = github_ci_time.measure(self.suite_sample())
        self.assertIsNone(reason)
        self.assertEqual(sample["runner_seconds"], 1020)
        report = github_ci_time.summarize({"runs": [self.sharded_sample(),
            dict(self.suite_sample(), id=2)]})
        self.assertEqual(len(report["cohorts"]), 2)

    def test_current_matrix_counts_uefi_and_analyzer_cost(self):
        sample, reason = github_ci_time.measure(self.current_sample())
        self.assertIsNone(reason)
        self.assertEqual(sample["runner_seconds"], 1140)

    def test_suite_matrix_rejects_missing_duplicate_failed_and_skipped_coverage(self):
        for index in range(len(github_ci_time.PARTITIONED_JOBS)):
            run = self.suite_sample()
            run["jobs"].pop(index)
            self.assertIsNone(github_ci_time.measure(run)[0])
            run = self.suite_sample()
            run["jobs"].append(copy.deepcopy(run["jobs"][index]))
            self.assertIsNone(github_ci_time.measure(run)[0])
            for conclusion in ("failure", "cancelled", "skipped", None):
                run = self.suite_sample()
                run["jobs"][index]["conclusion"] = conclusion
                self.assertIsNone(github_ci_time.measure(run)[0])
        for index in range(len(github_ci_time.SHARDED_JOBS), len(github_ci_time.PARTITIONED_JOBS)):
            for step in range(len(self.suite_sample()["jobs"][index]["steps"])):
                for conclusion in ("failure", "cancelled", "skipped", None):
                    run = self.suite_sample()
                    run["jobs"][index]["steps"][step]["conclusion"] = conclusion
                    self.assertIsNone(github_ci_time.measure(run)[0])
                run = self.suite_sample()
                run["jobs"][index]["steps"].pop(step)
                self.assertIsNone(github_ci_time.measure(run)[0])

    def test_current_matrix_rejects_missing_duplicate_or_failed_new_gates(self):
        first = len(github_ci_time.PARTITIONED_JOBS)
        for index in range(first, len(github_ci_time.SUITE_JOBS)):
            run = self.current_sample()
            run["jobs"].pop(index)
            self.assertIsNone(github_ci_time.measure(run)[0])
            run = self.current_sample()
            run["jobs"].append(copy.deepcopy(run["jobs"][index]))
            self.assertIsNone(github_ci_time.measure(run)[0])
            for conclusion in ("failure", "cancelled", "skipped", None):
                run = self.current_sample()
                run["jobs"][index]["conclusion"] = conclusion
                self.assertIsNone(github_ci_time.measure(run)[0])
            steps = len(self.current_sample()["jobs"][index]["steps"])
            for step in range(steps):
                run = self.current_sample()
                run["jobs"][index]["steps"].pop(step)
                self.assertIsNone(github_ci_time.measure(run)[0])

    def test_known_median_and_queue_are_separate(self):
        data = {"runs": [self.sample(1, 40), self.sample(2, 60), self.sample(3, 80)]}
        result = github_ci_time.summarize(data)
        row = result["cohorts"][0]
        self.assertEqual(row["n"], 3)
        self.assertEqual(row["medians"]["elapsed_seconds"], 70)
        self.assertEqual(row["medians"]["execution_span_seconds"], 60)
        self.assertEqual(row["medians"]["runner_seconds"], 360)
        self.assertEqual(row["medians"]["initial_queue_seconds"], 10)

    def test_failures_cancellations_reruns_and_partial_coverage_are_excluded(self):
        samples = [self.sample(index) for index in range(6)]
        samples[0]["conclusion"] = "failure"
        samples[1]["conclusion"] = "cancelled"
        samples[2]["run_attempt"] = 2
        samples[3]["jobs"].pop()
        samples[4]["jobs"][0]["steps"].pop()
        samples[5]["status"] = "in_progress"
        report = github_ci_time.summarize({"runs": samples})
        self.assertFalse(report["cohorts"])
        self.assertEqual(sum(report["excluded"].values()), 6)

    def test_different_workflows_or_runners_never_share_a_median(self):
        changed_runner = self.sample(3)
        changed_runner["jobs"][0]["labels"] = ["other-image"]
        report = github_ci_time.summarize({"runs": [self.sample(1), self.sample(2, revision="b"), changed_runner]})
        self.assertEqual(len(report["cohorts"]), 3)

    def test_duplicate_observations_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            github_ci_time.summarize({"runs": [self.sample(), self.sample()]})

    def test_missing_times_and_workflow_identity_are_not_imputed(self):
        samples = [self.sample(1), self.sample(2)]
        samples[0]["jobs"][0]["completed_at"] = None
        samples[1].pop("workflow_blob_sha")
        report = github_ci_time.summarize({"runs": samples})
        self.assertFalse(report["cohorts"])
        self.assertEqual(sum(report["excluded"].values()), 2)


if __name__ == "__main__":
    unittest.main()
