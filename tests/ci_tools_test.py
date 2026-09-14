#!/usr/bin/env python3
"""Network-free tests of cache integrity and failure-summary contracts."""
import copy
import hashlib
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
import ci_pack_evidence
import ci_summary
import ci_zig
import github_ci_time
import native_retirement_archive


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
        for target in ci_zig.TARGETS:
            version, digest = ci_zig.load_pin(ROOT / ".github/zig.json", target)
            self.assertEqual(version, "0.16.0")
            self.assertEqual(len(digest), 64)

    def test_reject_incomplete_or_mutable_pins(self):
        for field, value in (("version", "latest"), ("version", "../bad"), ("sha256", {})):
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
        def bad_download(url, destination):
            Path(destination).write_bytes(b"bad download")
        with mock.patch.object(ci_zig, "download_archive", side_effect=bad_download), mock.patch.object(ci_zig.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                ci_zig.install("x86_64-linux", self.manifest, self.root / "cache", self.root / "install")
            run.assert_not_called()

    def test_network_retries_are_bounded_and_partial_is_removed(self):
        target = self.root / "archive"
        with mock.patch.object(ci_zig.urllib.request, "urlopen", side_effect=OSError("offline")) as request, mock.patch.object(ci_zig.time, "sleep"):
            with self.assertRaises(OSError):
                ci_zig.download_archive("https://ziglang.org/test", target)
            self.assertEqual(request.call_count, 3)
            self.assertFalse(target.exists())
            self.assertFalse(target.with_name("archive.part").exists())

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
        self.assertEqual(list(steps)[-5:], ["Native result and reproduction", "Pack native logs",
                                            "Retain native logs", "Record native packaging failure", "Retain unpacked native logs"])
        pack, packed, unpacked = (steps[name] for name in (
            "Pack native logs", "Retain native logs", "Retain unpacked native logs"))
        self.assertIn("id: pack\n", pack)
        self.assertIn("if: ${{ !cancelled() && steps.checkout.outcome == 'success' }}", pack)
        self.assertIn("if: ${{ !cancelled() && steps.pack.outcome == 'success' }}", packed)
        # Exactly one upload runs: the archive, or the original tree after a failed pack.
        self.assertIn("if: ${{ !cancelled() && steps.pack.outcome != 'success' }}", unpacked)
        artifact = "name: native-${{ matrix.os }}-${{ matrix.arch }}-${{ github.run_id }}-${{ github.run_attempt }}\n"
        for block in (packed, unpacked):
            self.assertIn(artifact, block)
            self.assertIn("retention-days: 7\n", block)
        self.assertIn("path: ${{ runner.temp }}/native-ci-upload/\n", packed)
        self.assertIn("compression-level: 0\n", packed)
        self.assertIn("if-no-files-found: error\n", packed)
        self.assertIn("!${{ runner.temp }}/buster-ci/differential/**/program\n", unpacked)
        self.assertIn("!${{ runner.temp }}/buster-ci/differential/**/subject.o\n", unpacked)
        self.assertEqual(ci_pack_evidence.GENERATED, frozenset(("program", "subject.o")))
        failed_summary = steps["Record native packaging failure"]
        self.assertIn("steps.pack.outcome != 'success'", failed_summary)
        self.assertIn("BUSTER_CI_REQUIRED: modes differential pack", failed_summary)
        self.assertIn("run: python3 tools/ci_summary.py", failed_summary)
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


class WorkflowPolicyTests(unittest.TestCase):
    @staticmethod
    def analyzer_step(name):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        block = text.split("      - name: " + name + "\n", 1)[1]
        block = block.split("      - name:", 1)[0]
        return textwrap.dedent(block.split("        run: |\n", 1)[1])

    def test_all_six_platforms_and_commands_remain(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        names = re.findall(r"^          - name: (.+)$", text, re.M)
        self.assertEqual(sorted(names), sorted(github_ci_time.PLATFORMS + github_ci_time.MOBILE + github_ci_time.NATIVE))
        self.assertIn("fail-fast: false", text)
        self.assertIn("test_all_combinations_ci --verbose=1", text)
        self.assertIn("test_mode_matrix --config Release", text)
        self.assertIn("./android/test_ci.sh --all", text)
        self.assertIn("./ios/test_ci.sh --all", text)
        self.assertNotRegex(text, r"(?m)^\s*continue-on-error:")
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
                for action in re.findall(r"uses:\s*(\S+)", text):
                    self.assertRegex(action, r"^[^@]+@[0-9a-f]{40}$")
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

    def test_platform_and_bootstrap_events_cover_the_same_revisions(self):
        expected = ("  pull_request:", "  push:", "    branches: [main]",
                    "    tags: ['**']", "  merge_group:",
                    "    types: [checks_requested]", "  workflow_dispatch:")
        for name in ("ci.yml", "self-host-audit.yml"):
            with self.subTest(workflow=name):
                text = (ROOT / ".github/workflows" / name).read_text()
                block = re.search(r"(?ms)^on:(.*?)(?=^[A-Za-z_][\w-]*:|\Z)", text)
                self.assertIsNotNone(block)
                lines = tuple(line.rstrip() for line in block.group(1).splitlines()
                              if line.strip() and not line.lstrip().startswith("#"))
                self.assertEqual(lines, expected)
                # Default checkout is the PR/merge-group merge revision, not
                # an independently selected head or a stale branch ref.
                self.assertNotRegex(text, r"(?m)^\s+(ref|repository):")

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

    @unittest.skipIf(os.name == "nt", "The analyzer workflow policy uses the Unix hosted runner")
    def test_analyzer_comparison_selection_uses_verified_commit_identities(self):
        script = self.analyzer_step("Bootstrap candidate and select reference build driver")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "--quiet", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "CI fixture"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "ci@example.invalid"], check=True)
            (root / "build.c").write_text("int baseline;\n")
            subprocess.run(["git", "-C", root, "add", "build.c"], check=True)
            subprocess.run(["git", "-C", root, "commit", "--quiet", "-m", "baseline"], check=True)
            baseline = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip()
            (root / "build.c").write_text("int candidate;\n")
            subprocess.run(["git", "-C", root, "commit", "--quiet", "-am", "candidate"], check=True)
            candidate = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip()
            fake_bin = root / "fake-bin"
            fake_bin.mkdir()
            clang = fake_bin / "clang"
            clang.write_text("""#!/bin/sh
if [ "$1" = "--version" ]; then
    printf '%s\\n' 'fixture clang'
else
    output=
    while [ "$#" -gt 0 ]; do
        if [ "$1" = "-o" ]; then
            shift
            output=$1
        fi
        shift
    done
    : > "$output"
fi
""")
            clang.chmod(0o755)

            def run(reference, event):
                runner_temp = root / ("runner-" + event + "-" + reference[:8])
                runner_temp.mkdir()
                environment = dict(os.environ, BASELINE_REVISION=reference, EVENT_NAME=event,
                                   RUNNER_TEMP=str(runner_temp),
                                   GITHUB_ENV=str(runner_temp / "environment"),
                                   GITHUB_STEP_SUMMARY=str(runner_temp / "summary"),
                                   PATH=str(fake_bin) + os.pathsep + os.environ["PATH"])
                result = subprocess.run(["bash", "--noprofile", "--norc", "-c", script],
                                        cwd=root, env=environment, capture_output=True, text=True, timeout=30)
                return result, runner_temp

            same, same_temp = run(candidate, "push")
            self.assertEqual(same.returncode, 0, same.stdout + same.stderr)
            self.assertEqual((same_temp / "buster-analyzer/comparison-selection.txt").read_text(),
                             "BUSTER_ANALYZER_COMPARISON_SELECTION_V1\n"
                             "event=push\n"
                             f"candidate_revision={candidate}\nreference_revision={candidate}\n"
                             "selection=skip\nreason=same-revision\n")
            self.assertIn("ANALYZER_COMPARISON_SELECTION=skip\n", (same_temp / "environment").read_text())
            self.assertFalse((root / "build/analyzer-baseline").exists())

            dispatched, dispatched_temp = run(candidate, "workflow_dispatch")
            self.assertEqual(dispatched.returncode, 0, dispatched.stdout + dispatched.stderr)
            self.assertIn("selection=skip\nreason=same-revision\n",
                          (dispatched_temp / "buster-analyzer/comparison-selection.txt").read_text())

            different, different_temp = run(baseline, "pull_request")
            self.assertEqual(different.returncode, 0, different.stdout + different.stderr)
            self.assertIn(f"candidate_revision={candidate}\nreference_revision={baseline}\n",
                          (different_temp / "buster-analyzer/comparison-selection.txt").read_text())
            self.assertIn("selection=compare\nreason=distinct-revisions\n",
                          (different_temp / "buster-analyzer/comparison-selection.txt").read_text())
            self.assertIn("ANALYZER_COMPARISON_SELECTION=compare\n", (different_temp / "environment").read_text())
            self.assertTrue((root / "build/analyzer-baseline").is_file())

            merge_group, merge_group_temp = run(candidate, "merge_group")
            self.assertEqual(merge_group.returncode, 0, merge_group.stdout + merge_group.stderr)
            self.assertIn("selection=compare\nreason=event-requires-comparison\n",
                          (merge_group_temp / "buster-analyzer/comparison-selection.txt").read_text())

            unknown, unknown_temp = run(candidate, "schedule")
            self.assertEqual(unknown.returncode, 0, unknown.stdout + unknown.stderr)
            self.assertIn("selection=compare\nreason=event-requires-comparison\n",
                          (unknown_temp / "buster-analyzer/comparison-selection.txt").read_text())

            invalid, invalid_temp = run("f" * 40, "push")
            self.assertNotEqual(invalid.returncode, 0)
            self.assertFalse((invalid_temp / "buster-analyzer/comparison-selection.txt").exists())

    @unittest.skipIf(os.name == "nt", "The analyzer workflow policy uses the Unix hosted runner")
    def test_analyzer_campaign_rejects_missing_selection_and_keeps_candidate_aggregate(self):
        script = self.analyzer_step("Compare reference analysis and aggregate all module shards")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "--quiet", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "CI fixture"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "ci@example.invalid"], check=True)
            (root / "build.c").write_text("int candidate;\n")
            subprocess.run(["git", "-C", root, "add", "build.c"], check=True)
            subprocess.run(["git", "-C", root, "commit", "--quiet", "-m", "candidate"], check=True)
            candidate = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip()
            (root / "build").mkdir()
            driver = root / "build/analyzer-driver"
            driver.write_text("""#!/bin/sh
printf '%s\\n' "$*" >> "$DRIVER_LOG"
case "${FAIL_MODE-}:$*" in
    reference:*--baseline-driver*) exit 31 ;;
    candidate:*--aggregate*) ;;
    candidate:*) exit 32 ;;
esac
""")
            driver.chmod(0o755)

            def run(selection, event, baseline, record="valid", fail_mode=""):
                driver_log = root / ("driver-" + selection + "-" + event + "-" + record +
                                     ("-baseline" if baseline else "") + ".log")
                if driver_log.exists():
                    driver_log.unlink()
                baseline_path = root / "build/analyzer-baseline"
                baseline_target = root / "build/analyzer-baseline-target"
                (root / "runner/buster-analyzer").mkdir(parents=True, exist_ok=True)
                if baseline_path.exists() or baseline_path.is_symlink():
                    baseline_path.unlink()
                if baseline_target.exists():
                    baseline_target.unlink()
                if baseline == "symlink":
                    baseline_target.write_text("fixture\n")
                    baseline_target.chmod(0o755)
                    baseline_path.symlink_to(baseline_target.name)
                elif baseline == "dangling":
                    baseline_path.symlink_to("missing-analyzer-baseline")
                elif baseline:
                    baseline_path.write_text("fixture\n")
                    baseline_path.chmod(0o755)
                selection_path = root / "runner/buster-analyzer/comparison-selection.txt"
                if selection_path.exists() or selection_path.is_symlink():
                    selection_path.unlink()
                expected_selection = ("skip" if event in ("push", "workflow_dispatch") else "compare")
                expected_reason = ("same-revision" if expected_selection == "skip" else
                                   "event-requires-comparison")
                fields = ["BUSTER_ANALYZER_COMPARISON_SELECTION_V1", f"event={event}",
                          f"candidate_revision={candidate}", f"reference_revision={candidate}",
                          f"selection={expected_selection}", f"reason={expected_reason}"]
                if record == "tampered":
                    fields[4] = "selection=compare" if expected_selection == "skip" else "selection=skip"
                elif record == "stale":
                    fields[2] = "candidate_revision=" + "f" * 40
                elif record == "wrong-reference":
                    fields[3] = "reference_revision=" + "e" * 40
                elif record == "wrong-reason":
                    fields[5] = "reason=distinct-revisions"
                elif record == "malformed":
                    fields.append("unexpected=field")
                if record == "no-final-lf":
                    selection_path.write_text("\n".join(fields))
                elif record == "nul":
                    selection_path.write_bytes(("\n".join(fields) + "\n").encode() + b"\0")
                elif record == "symlink":
                    target = selection_path.with_name("comparison-selection-target.txt")
                    target.write_text("\n".join(fields) + "\n")
                    selection_path.symlink_to(target.name)
                elif record != "missing":
                    selection_path.write_text("\n".join(fields) + "\n")
                environment = dict(os.environ, ANALYZER_COMPARISON_SELECTION=selection,
                                   BASELINE_REVISION=candidate, EVENT_NAME=event,
                                   RUNNER_TEMP=str(root / "runner"), DRIVER_LOG=str(driver_log),
                                   FAIL_MODE=fail_mode)
                result = subprocess.run(["bash", "--noprofile", "--norc", "-c", script], cwd=root,
                                        env=environment, capture_output=True, text=True, timeout=30)
                lines = driver_log.read_text().splitlines() if driver_log.exists() else []
                return result, lines

            skipped, skipped_lines = run("skip", "push", False)
            self.assertEqual(skipped.returncode, 0, skipped.stdout + skipped.stderr)
            self.assertEqual(len(skipped_lines), 2)
            self.assertNotIn("--baseline-driver", skipped_lines[0])
            self.assertIn("--aggregate", skipped_lines[1])

            compared, compared_lines = run("compare", "merge_group", True)
            self.assertEqual(compared.returncode, 0, compared.stdout + compared.stderr)
            self.assertEqual(len(compared_lines), 2)
            self.assertIn("--baseline-driver build/analyzer-baseline", compared_lines[0])
            self.assertIn("--aggregate", compared_lines[1])
            self.assertEqual(compared_lines[0].replace(" --baseline-driver build/analyzer-baseline", ""),
                             skipped_lines[0])
            self.assertEqual(compared_lines[1], skipped_lines[1])

            for record in ("missing", "malformed", "tampered", "stale", "wrong-reference",
                           "wrong-reason", "no-final-lf", "nul", "symlink"):
                with self.subTest(record=record):
                    rejected, rejected_lines = run("skip", "push", False, record)
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertFalse(rejected_lines)
            missing_export, missing_export_lines = run("", "push", False)
            self.assertNotEqual(missing_export.returncode, 0)
            self.assertFalse(missing_export_lines)
            stale_baseline, stale_baseline_lines = run("skip", "push", True)
            self.assertNotEqual(stale_baseline.returncode, 0)
            self.assertFalse(stale_baseline_lines)
            absent, absent_lines = run("compare", "merge_group", False)
            self.assertNotEqual(absent.returncode, 0)
            self.assertFalse(absent_lines)
            linked, linked_lines = run("compare", "merge_group", "symlink")
            self.assertNotEqual(linked.returncode, 0)
            self.assertFalse(linked_lines)
            dangling, dangling_lines = run("skip", "push", "dangling")
            self.assertNotEqual(dangling.returncode, 0)
            self.assertFalse(dangling_lines)
            reference_failure, reference_failure_lines = run(
                "compare", "merge_group", True, fail_mode="reference")
            self.assertNotEqual(reference_failure.returncode, 0)
            self.assertEqual(len(reference_failure_lines), 1)
            candidate_failure, candidate_failure_lines = run(
                "skip", "push", False, fail_mode="candidate")
            self.assertNotEqual(candidate_failure.returncode, 0)
            self.assertEqual(len(candidate_failure_lines), 1)

    def test_analyzer_selection_qualification_is_bounded_and_fail_closed(self):
        text = (ROOT / ".github/workflows/ci-603-analyzer-qualification.yml").read_text()
        self.assertIn("- sol/603-analyzer-selection-20260914", text)
        self.assertIn("github.repository == 'buster14a/buster'", text)
        self.assertIn("github.server_url == 'https://github.com'", text)
        self.assertIn("github.event_name == 'push'", text)
        self.assertIn("github.ref == 'refs/heads/sol/603-analyzer-selection-20260914'", text)
        self.assertIn("vars.GH_ACTIONS_CI_ENABLED == 'true'", text)
        self.assertIn("runs-on: ubuntu-26.04", text)
        self.assertIn("timeout-minutes: 60", text)
        self.assertIn("persist-credentials: false", text)
        self.assertIn("fetch-depth: 0", text)
        self.assertIn('test "$source_revision" = "$GITHUB_SHA"', text)
        self.assertIn("treatments=(compare skip)", text)
        self.assertIn("treatments=(skip compare)", text)
        self.assertIn('run_sample 1 1 "${treatments[0]}"', text)
        self.assertIn('run_sample 1 2 "${treatments[1]}"', text)
        self.assertIn('campaign_pipeline=("${PIPESTATUS[@]}")', text)
        self.assertIn('aggregate_pipeline=("${PIPESTATUS[@]}")', text)
        self.assertIn("--baseline-driver build/analyzer-baseline", text)
        self.assertIn("--shards 8 --jobs 2 --quiet --results \"$current\"", text)
        self.assertIn("--aggregate --results \"$current\"", text)
        self.assertIn("cmp -s \"$reference_manifest\" \"$current/manifest.txt\"", text)
        self.assertIn("cmp -s \"$reference_canonical\" \"$sample_root/candidate-results.txt\"", text)
        self.assertIn("Only shard elapsed_us, shard peak_child_rss_bytes and per-unit elapsed_us fields are omitted.", text)
        self.assertIn("test \"$(find \"$QUALIFICATION_ROOT/samples\"", text)
        self.assertIn("if: always()", text)
        self.assertIn("actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02", text)
        self.assertIn("if-no-files-found: error", text)

    @unittest.skipIf(os.name == "nt", "The tee failure control uses /dev/full")
    def test_analyzer_selection_qualification_rejects_tee_failure(self):
        text = (ROOT / ".github/workflows/ci-603-analyzer-qualification.yml").read_text()
        body = text.split("          pipeline_failed() {\n", 1)[1].split("          }\n", 1)[0]
        function = "pipeline_failed() {\n" + textwrap.dedent(body) + "}\n"
        probe = function + r'''
set -euo pipefail
if pipeline_failed 0 0; then exit 10; fi
pipeline_failed 1 0
pipeline_failed 0 1
set +e
printf fixture | tee /dev/full >/dev/null 2>&1
statuses=("${PIPESTATUS[@]}")
set -e
test "${#statuses[@]}" -eq 2
test "${statuses[0]}" -eq 0
test "${statuses[1]}" -ne 0
pipeline_failed "${statuses[0]}" "${statuses[1]}"
'''
        result = subprocess.run(["bash", "--noprofile", "--norc", "-c", probe],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_analyzer_selection_qualification_uses_outer_candidate_metric(self):
        text = (ROOT / ".github/workflows/ci-603-analyzer-qualification.yml").read_text()
        marker = "          python3 - \"$QUALIFICATION_ROOT\" <<'PY'\n"
        source = textwrap.dedent(text.split(marker, 1)[1].split("\n          PY", 1)[0])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            samples = root / "samples"
            samples.mkdir()
            compare = samples / "pair-1-position-1-compare"
            skip = samples / "pair-1-position-2-skip"
            compare.mkdir()
            skip.mkdir()
            compare.joinpath("campaign.log").write_text(
                "ANALYZE_RUN elapsed_us=500000000 jobs=2 status=pass\n"
                "ANALYZE_BASELINE eligible=104 elapsed_us=500100000 status=pass\n"
                "ANALYZE_RUN elapsed_us=400000000 jobs=2 status=pass\n")
            skip.joinpath("campaign.log").write_text(
                "ANALYZE_RUN elapsed_us=410000000 jobs=2 status=pass\n")
            root.joinpath("campaigns.tsv").write_text(
                f"{compare.name}\tcompare\t1\t2\t900000000\t0\t0\t0\n"
                f"{skip.name}\tskip\t3\t4\t410000000\t0\t0\t0\n")
            result = subprocess.run([sys.executable, "-", str(root)], input=source,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            measurements = {}
            for row in root.joinpath("measurements.tsv").read_text().splitlines()[1:]:
                fields = row.split("\t")
                measurements[fields[0]] = fields
            self.assertEqual(measurements["compare_candidate_s"][2], "400.000000")
            self.assertEqual(measurements["compare_baseline_nested_s"][2], "500.000000")
            self.assertEqual(measurements["compare_baseline_wrapper_s"][2], "500.100000")
            self.assertEqual(measurements["compare_baseline_overhead_s"][2], "0.100000")
            self.assertEqual(measurements["skip_candidate_s"][2], "410.000000")

    @unittest.skipIf(os.name == "nt", "The failure-propagation probe uses the Unix Clang driver")
    def test_recoverable_ubsan_error_is_fatal_with_correctness_environment(self):
        compiler = shutil.which("clang")
        self.assertIsNotNone(compiler, "Clang is a CI prerequisite")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "probe.c"
            executable = root / "probe"
            source.write_text("int main(void) { volatile int x = 2147483647; volatile int y = x + 1; (void)y; return 0; }\n")
            subprocess.run([compiler, "-fsanitize=undefined", "-fsanitize-recover=all", str(source), "-o", str(executable)], check=True, timeout=30, capture_output=True)
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
            job["steps"] = [{"name": step, "conclusion": "success"} for step in (
                "Execution-mode matrix", "Native configuration differential matrix")]
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

    def test_suite_matrix_counts_all_fifteen_jobs(self):
        sample, reason = github_ci_time.measure(self.suite_sample())
        self.assertIsNone(reason)
        self.assertEqual(sample["runner_seconds"], 900)
        report = github_ci_time.summarize({"runs": [self.sharded_sample(),
            dict(self.suite_sample(), id=2)]})
        self.assertEqual(len(report["cohorts"]), 2)

    def test_current_matrix_counts_uefi_and_analyzer_cost(self):
        sample, reason = github_ci_time.measure(self.current_sample())
        self.assertIsNone(reason)
        self.assertEqual(sample["runner_seconds"], 1020)

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
            for step in range(2):
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
