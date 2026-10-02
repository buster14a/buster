#!/usr/bin/env python3
"""Cloud regression controls for the native external Clang harness.

ClangSuiteTests checks the complete Git ledger independently and reserves the
external checkout serially while applying reversible cleanliness/hash controls.
DRIVER and CHECKOUT are required; IDE/CLANG enable the two-fixture smoke gates.
RESULTS retains evidence when supplied. This file does not build compilers.
"""

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


UPSTREAM_COMMIT = "85ac560262434c9ccfc0c183ec22d4138ed647fb"
UPSTREAM_VERSION = "llvmorg-23.1.2"
SOURCE_ROOTS = (
    "clang/test",
    "clang/unittests",
    "clang/tools/scan-build-py/tests",
    "clang/bindings/python/tests",
    "clang/LICENSE.TXT",
    "llvm/LICENSE.TXT",
)
SOURCE_COUNTS = (30712, 410, 30, 38, 2)
CASE_CHECKS = {
    "macro-paste-simple": (b"A: barbaz123", b"B: ##"),
    "macro-paste-hashhash": (b'"x ## y";', b"A ## B;"),
}


class ClangSuiteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        driver = os.environ.get("BUSTER_CLANG_SUITE_DRIVER")
        checkout = os.environ.get("BUSTER_CLANG_SUITE_CHECKOUT")
        if not driver or not checkout:
            raise RuntimeError("BUSTER_CLANG_SUITE_DRIVER and BUSTER_CLANG_SUITE_CHECKOUT are required")
        cls.driver = Path(driver).resolve(strict=True)
        cls.checkout = Path(checkout).resolve(strict=True)
        if not cls.driver.is_file() or not os.access(cls.driver, os.X_OK) or not cls.checkout.is_dir():
            raise RuntimeError("required native driver/checkout paths are unavailable")
        cls.git = shutil.which("git")
        if not cls.git:
            raise RuntimeError("Git is required for independent source-ledger checks")
        cls.assert_clean_source()
        head = cls.git_run("rev-parse", "HEAD").stdout.strip().decode("ascii")
        if head != UPSTREAM_COMMIT:
            raise RuntimeError(f"source checkout is {head}, expected {UPSTREAM_COMMIT}")
        ide = os.environ.get("BUSTER_CLANG_SUITE_IDE")
        clang = os.environ.get("BUSTER_CLANG_SUITE_CLANG")
        if bool(ide) != bool(clang):
            raise RuntimeError("optional smoke gates require both BUSTER_CLANG_SUITE_IDE and BUSTER_CLANG_SUITE_CLANG")
        cls.ide = Path(ide).resolve(strict=True) if ide else None
        cls.clang = Path(clang).resolve(strict=True) if clang else None
        if cls.ide and (not cls.ide.is_file() or not cls.clang.is_file()):
            raise RuntimeError("optional compiler paths must be existing files")
        retained = os.environ.get("BUSTER_CLANG_SUITE_RESULTS")
        cls.retain = bool(retained)
        parent = Path(retained).resolve(strict=True) if retained else None
        cls.evidence = Path(tempfile.mkdtemp(prefix="clang-suite-regressions-", dir=parent)).resolve()
        if cls.evidence == cls.checkout or cls.checkout in cls.evidence.parents:
            raise RuntimeError("regression evidence must be outside the upstream checkout")

    @classmethod
    def tearDownClass(cls):
        cls.assert_clean_source()
        if not cls.retain:
            shutil.rmtree(cls.evidence)
        else:
            print(f"CLANG_SUITE_REGRESSION_EVIDENCE path={cls.evidence}")

    @classmethod
    def git_run(cls, *args, directory=None):
        command = [cls.git, "--no-replace-objects", "-C", str(directory or cls.checkout), *args]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        if result.returncode != 0:
            raise AssertionError(f"independent Git query failed: {args!r}: {result.stderr.decode(errors='replace')}")
        return result

    @classmethod
    def assert_clean_source(cls):
        status = cls.git_run("status", "--porcelain=v1", "--untracked-files=all", "--ignored").stdout
        if status:
            raise AssertionError(f"external source checkout is not clean: {status.decode(errors='replace')}")

    def invoke(self, *args):
        command = [str(self.driver), "test_clang_suite", *map(str, args)]
        try:
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
        except subprocess.TimeoutExpired as error:
            self.fail(f"native driver exceeded the 90-second regression deadline: {command!r}: {error}")
        self.assertGreaterEqual(result.returncode, 0, "native driver terminated by a signal")
        return result

    def rejected(self, result, diagnostic):
        self.assertEqual(result.returncode, 1, result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
        self.assertIn(diagnostic.encode(), result.stdout + result.stderr)

    def receipt(self, directory):
        lines = (directory / "receipt.txt").read_text(encoding="utf-8").splitlines()
        self.assertEqual(lines.pop(0), "BUSTER_CLANG_SUITE_RECEIPT_V1")
        fields = {}
        for line in lines:
            key, value = line.split("=", 1)
            self.assertNotIn(key, fields)
            fields[key] = value
        self.assertEqual(fields["version"], UPSTREAM_VERSION)
        self.assertEqual(fields["commit"], UPSTREAM_COMMIT)
        self.assertEqual(fields["source_ledger_executed"], "0")
        return fields

    def test_native_self_test(self):
        result = self.invoke("--self-test")
        self.assertEqual(result.returncode, 0, result.stdout.decode(errors="replace"))
        self.assertIn(b"CLANG_SUITE_SELF_TEST status=pass", result.stdout)
        self.assertRegex(result.stdout, rb"CLANG_SUITE_SMOKE_SELF_TEST cases=[1-9][0-9]* status=pass")

    def test_complete_inventory_and_repeatability(self):
        independent = self.git_run("ls-tree", "-r", "-z", "--full-tree", UPSTREAM_COMMIT, "--", *SOURCE_ROOTS).stdout
        expected = []
        counts = [0] * 5
        modes = {b"100644": 0, b"100755": 0, b"120000": 0}
        self.assertTrue(independent.endswith(b"\0"))
        for record in independent[:-1].split(b"\0"):
            metadata, path = record.split(b"\t", 1)
            mode, kind, blob = metadata.split(b" ")
            self.assertEqual(kind, b"blob")
            self.assertIn(mode, modes)
            modes[mode] += 1
            self.assertRegex(blob, rb"^[0-9a-f]{40}$")
            name = path.decode("utf-8")
            scope = 4 if name in SOURCE_ROOTS[4:] else next(i for i, root in enumerate(SOURCE_ROOTS[:4]) if name.startswith(root + "/"))
            counts[scope] += 1
            expected.append((mode, blob, path))
        self.assertEqual(counts, list(SOURCE_COUNTS))
        self.assertEqual(len(expected), 31192)
        self.assertEqual(modes, {b"100644": 31074, b"100755": 104, b"120000": 14})
        self.assertEqual([row[2] for row in expected], sorted(row[2] for row in expected))
        manifests = []
        for iteration in range(2):
            output = self.evidence / f"inventory-{iteration}"
            result = self.invoke("--inventory", self.checkout, output)
            self.assertEqual(result.returncode, 0, result.stdout.decode(errors="replace"))
            raw = (output / "git-ls-tree.bin").read_bytes()
            self.assertEqual(raw, independent)
            manifest = (output / "sources.tsv").read_bytes()
            lines = manifest.splitlines()
            self.assertEqual(lines[:2], [b"BUSTER_CLANG_SUITE_SOURCES_V1", b"mode\tblob\tpath\trole\tstate"])
            actual = []
            for line in lines[2:]:
                mode, blob, path, role, state = line.split(b"\t")
                self.assertEqual(state, b"unadapted")
                self.assertIn(role, (b"license", b"symlink", b"support-input", b"configuration", b"suite-source"))
                actual.append((mode, blob, path))
            self.assertEqual(actual, expected)
            fields = self.receipt(output)
            self.assertEqual(fields["source_files"], "31192")
            self.assertEqual(fields["source_manifest_sha256"], hashlib.sha256(manifest).hexdigest())
            self.assertEqual(fields["raw_git_inventory_sha256"], hashlib.sha256(independent).hexdigest())
            self.assertEqual(fields["smoke_requested"], "0")
            self.assertEqual(fields["checkout_status_clean"], "1")
            self.assertEqual(fields["status"], "pass")
            manifests.append(manifest)
        self.assertEqual(manifests[0], manifests[1])

    def test_bad_argument_shapes(self):
        output = self.evidence / "bad-arguments"
        cases = ((), ("--self-test", "extra"), ("--inventory", self.checkout), ("--inventory", self.checkout, output, "extra"))
        for args in cases:
            with self.subTest(args=args):
                self.rejected(self.invoke(*args), "usage: test_clang_suite")
        self.assertFalse(output.exists())

    def test_wrong_head_rejected(self):
        wrong = self.evidence / "wrong-head"
        wrong.mkdir()
        self.git_run("init", "--quiet", directory=wrong)
        (wrong / "control.txt").write_text("wrong source identity\n", encoding="utf-8")
        self.git_run("add", "control.txt", directory=wrong)
        self.git_run("-c", "user.name=Clang suite control", "-c", "user.email=clang-suite-control@example.invalid", "commit", "--quiet", "-m", "wrong identity control", directory=wrong)
        output = self.evidence / "wrong-head-output"
        self.rejected(self.invoke("--inventory", wrong, output), f"requires exact pin {UPSTREAM_COMMIT}")
        self.assertFalse(output.exists())

    def test_dirty_untracked_and_ignored_rejected(self):
        exclude_value = self.git_run("rev-parse", "--git-path", "info/exclude").stdout.strip().decode("utf-8")
        exclude = Path(exclude_value)
        exclude = exclude if exclude.is_absolute() else self.checkout / exclude
        original_exclude = exclude.read_bytes() if exclude.exists() else None
        for ignored in (False, True):
            name = f"buster-clang-suite-{'ignored' if ignored else 'untracked'}-control.c"
            control = self.checkout / name
            self.assertFalse(control.exists())
            output = self.evidence / f"dirty-{'ignored' if ignored else 'untracked'}"
            try:
                if ignored:
                    exclude.parent.mkdir(parents=True, exist_ok=True)
                    exclude.write_bytes((original_exclude or b"") + b"\n/" + name.encode() + b"\n")
                control.write_text("cleanliness negative control\n", encoding="utf-8")
                status = self.git_run("status", "--porcelain=v1", "--untracked-files=all", "--ignored").stdout
                self.assertIn(("!! " if ignored else "?? ").encode() + name.encode(), status)
                self.rejected(self.invoke("--inventory", self.checkout, output), "clean external checkout, including ignored/untracked files")
                self.assertFalse(output.exists())
            finally:
                control.unlink(missing_ok=True)
                if original_exclude is None:
                    exclude.unlink(missing_ok=True)
                else:
                    exclude.write_bytes(original_exclude)
            self.assert_clean_source()

    def test_occupied_output_preserved(self):
        output = self.evidence / "occupied"
        output.mkdir()
        sentinel = output / "sentinel"
        sentinel.write_bytes(b"must remain unchanged\0\n")
        self.rejected(self.invoke("--inventory", self.checkout, output), "output must be a fresh directory")
        self.assertEqual(sentinel.read_bytes(), b"must remain unchanged\0\n")
        self.assertEqual(list(output.iterdir()), [sentinel])

    def test_output_inside_checkout_and_alias_rejected(self):
        forbidden = self.checkout / "buster-clang-suite-forbidden-output"
        self.assertFalse(forbidden.exists())
        alias = self.evidence / "source-alias"
        alias.symlink_to(self.checkout, target_is_directory=True)
        for output in (forbidden, alias / forbidden.name):
            with self.subTest(output=output):
                self.rejected(self.invoke("--inventory", self.checkout, output), "fresh output outside the checkout")
                self.assertFalse(forbidden.exists())
        self.assert_clean_source()

    def require_smoke(self):
        if self.ide is None:
            self.skipTest("smoke execution unrun: provide BUSTER_CLANG_SUITE_IDE and BUSTER_CLANG_SUITE_CLANG")

    def test_two_pristine_smoke_cases(self):
        self.require_smoke()
        output = self.evidence / "smoke-success"
        result = self.invoke("--smoke", self.checkout, output, self.ide, self.clang)
        self.assertEqual(result.returncode, 0, result.stdout.decode(errors="replace"))
        self.assertIn(b"selected=2 attempted=2 passed=2 status=pass", (output / "smoke-summary.txt").read_bytes())
        for case, patterns in CASE_CHECKS.items():
            for compiler in ("clang", "buster"):
                raw = (output / f"{case}.{compiler}.stdout").read_bytes()
                canonical = re.sub(rb"[ \t]+", b" ", raw.replace(b"\r\n", b"\n"))
                cursor = 0
                for pattern in patterns:
                    position = canonical.find(pattern, cursor)
                    self.assertGreaterEqual(position, cursor, f"{case}/{compiler} missed literal {pattern!r}")
                    cursor = position + len(pattern)
                self.assertIn(b"process_status=success", (output / f"{case}.{compiler}.receipt").read_bytes())
                self.assertIn(b"literal_checks=pass", (output / f"{case}.{compiler}.receipt").read_bytes())
        fields = self.receipt(output)
        self.assertEqual(fields["smoke_requested"], "1")
        self.assertEqual(fields["checkout_status_clean"], "1")
        self.assertEqual(fields["status"], "pass")

    def test_hidden_fixture_mutation_rejected(self):
        self.require_smoke()
        relative = "clang/test/Preprocessor/macro_paste_simple.c"
        fixture = self.checkout / relative
        original = fixture.read_bytes()
        flags = self.git_run("ls-files", "-v", "--", relative).stdout
        self.assertTrue(flags)
        already_assumed = flags[:1].islower()
        output = self.evidence / "smoke-hidden-mutation"
        try:
            self.git_run("update-index", "--assume-unchanged", "--", relative)
            fixture.write_bytes(original + b"\n// hidden fixture hash negative control\n")
            self.assert_clean_source()
            self.rejected(self.invoke("--smoke", self.checkout, output, self.ide, self.clang), f"requires regular pristine fixture {relative}")
            self.assertIn(b"status=fail", (output / "smoke-summary.txt").read_bytes())
            self.assertEqual(self.receipt(output)["status"], "fail")
            self.assertFalse((output / "macro-paste-simple.buster.receipt").exists())
        finally:
            fixture.write_bytes(original)
            if not already_assumed:
                self.git_run("update-index", "--no-assume-unchanged", "--", relative)
        self.assertEqual(fixture.read_bytes(), original)
        self.assertEqual(self.git_run("ls-files", "-v", "--", relative).stdout, flags)
        self.assert_clean_source()

    def test_missing_compiler_launch_receipts(self):
        self.require_smoke()
        missing = self.evidence / "missing-clang-executable"
        self.assertFalse(missing.exists())
        output = self.evidence / "smoke-missing-compiler"
        self.rejected(self.invoke("--smoke", self.checkout, output, self.ide, missing), "process_status=launch-failed")
        for case in CASE_CHECKS:
            receipt = (output / f"{case}.clang.receipt").read_bytes()
            self.assertIn(b"process_status=launch-failed", receipt)
            self.assertIn(b"launch_failed=1", receipt)
            self.assertIn(b"literal_checks=fail", receipt)
        self.assertIn(b"selected=2 attempted=2 passed=0 status=fail", (output / "smoke-summary.txt").read_bytes())
        self.assertEqual(self.receipt(output)["status"], "fail")
        self.assert_clean_source()


if __name__ == "__main__":
    unittest.main()
