#!/usr/bin/env python3
"""Hosted real native owner/bridge controls and bounded data contract tests; no measurement claims."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import os
import signal
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
import compiler_compare as compare
import compiler_owned_phase as contract

NATIVE_DRIVER = None


def encoded(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def marker(driver="d" * 64):
    required = ("build.c", "tools/compiler_closure.c", "tools/compiler_closure_owned_phase.c", "tools/compiler_closure_phase.c")
    return ("\n".join(["BUSTER_BOOTSTRAP_CACHE_V1", "config\t" + "c" * 64, "artifact\tbuild-fixture\t" + driver,
                       *("dependency\t" + path + "\t" + "a" * 64 for path in sorted(required)), "END"]) + "\n").encode()


def record(argv, cwd="/checkout", timeout=5, stdout=b"", stderr=b"", ordinal=1):
    value = {"schema": contract.SCHEMA, "ownership_schema": contract.OWNERSHIP_SCHEMA, "state": "complete",
             "command_sha256": contract.sha(contract.command_bytes(argv)), "cwd_sha256": contract.sha(cwd.encode()),
             "driver_sha256": "d" * 64, "receipt_path_sha256": contract.sha(f"/evidence/owned-phases/{ordinal:04d}.json".encode()),
             "timeout_us": timeout * 1_000_000, "duration_us": 100, "duration_scope": contract.SCOPE,
             "receipt_publication_us": None, "exit_status_encoding": "posix-wait-status", "stdout_sha256": contract.sha(stdout), "stderr_sha256": contract.sha(stderr),
             "trusted_root_sha256": contract.sha(b"/trusted"), "bootstrap_config_sha256": "c" * 64,
             "bootstrap_marker_sha256": contract.sha(marker()), "bootstrap_dependency_count": 4,
             "cleanup_proven": True}
    value.update({key: 0 for key in ("exit_status", "timed_out", "cancelled", "capture_failed", "output_truncated",
                  "cleanup_us", "cleanup_waves", "cleanup_signalled", "cleanup_reaped", "reservation_retained",
                  "ownership_lost", "tree_cleanup_failed")})
    return value


def corpus_bundle():
    from compiler_test import corpus
    documents = corpus("regression")
    return {name: encoded(document) for name, document in documents.items()}


def population():
    from compiler_owned_plan_test import fixture
    current, ownership, planned = fixture("main")
    ownership.update(schema=contract.POPULATION_SCHEMA, state="complete",
                     trusted_revision="e" * 40, trusted_tree="f" * 40,
                     driver_sha256="d" * 64, bootstrap_marker_sha256=contract.sha(marker()),
                     driver_path="/trusted/.cache/bootstrap-driver/posix/" + "c" * 64 + "/build-fixture")
    rows, raw = [], {}
    for ordinal, recipe in enumerate(planned, 1):
        argv = list(recipe["argv"])
        if argv[0].startswith("/trusted/.cache/"):
            argv[0] = ownership["driver_path"]
        native = record(argv, cwd=recipe["cwd"], timeout=recipe["timeout"], ordinal=ordinal)
        if recipe["phase"] == "throughput":
            native.update(state="failed", exit_status=256)
        receipt = encoded(native)
        name = f"{ordinal:04d}.json"
        rows.append(dict(recipe, ordinal=ordinal, file=name, argv=argv,
                         bridge_wall_us=150, receipt_sha256=contract.sha(receipt)))
        raw[name] = {"receipt": receipt, "command": contract.command_bytes(argv),
                     "stdout": b"", "stderr": b"", "bootstrap": marker()}
    corpus = corpus_bundle()
    row = next(row for row in rows if row["phase"] == "throughput")
    row.update(corpus_summary_sha256=contract.sha(corpus["summary"]), corpus_metadata_sha256=contract.sha(corpus["metadata"]))
    from compiler_test import BINARIES
    from compiler_receipt import throughput_digest
    current["binaries"] = copy.deepcopy(BINARIES)
    current["throughput"] = dict(throughput_digest(json.loads(corpus["summary"])), exit=1, exit_policy="corpus-report-only-v1")
    ownership.update(count=len(rows), phases=rows)
    current["phase_ownership"] = ownership
    return current, raw


class DataContract(unittest.TestCase):
    def test_full_core_population_and_identity_are_required(self):
        current, raw = population()
        self.assertEqual(contract.validate_population(current, raw, "d" * 64, "e" * 40, corpus_bundle()), [])
        for case in ("stripped", "omitted", "extra", "duplicate", "order", "driver", "source", "command", "marker", "path", "scope"):
            changed, members = copy.deepcopy(current), copy.deepcopy(raw)
            rows = changed["phase_ownership"]["phases"]
            if case == "stripped":
                changed.pop("phase_ownership")
            elif case == "omitted":
                rows.pop()
                changed["phase_ownership"]["count"] -= 1
                members.pop("0012.json")
            elif case == "extra":
                members["9999.json"] = members["0001.json"]
            elif case == "duplicate":
                rows[4] = copy.deepcopy(rows[1])
            elif case == "order":
                rows[0], rows[1] = rows[1], rows[0]
            elif case == "driver":
                changed["phase_ownership"]["driver_sha256"] = "a" * 64
            elif case == "source":
                changed["phase_ownership"]["trusted_revision"] = "a" * 40
            elif case == "command":
                members["0001.json"]["command"] += b"changed"
            elif case == "marker":
                members["0001.json"]["bootstrap"] += b"wrong"
            else:
                native = json.loads(members["0001.json"]["receipt"])
                native["receipt_path_sha256" if case == "path" else "duration_scope"] = "changed"
                members["0001.json"]["receipt"] = encoded(native)
                rows[0]["receipt_sha256"] = contract.sha(members["0001.json"]["receipt"])
            with self.subTest(case=case):
                self.assertTrue(contract.validate_population(changed, members, "d" * 64, "e" * 40, corpus_bundle()))


    def test_corpus_exit_one_requires_exact_complete_raw_reports_and_fixed_command(self):
        current, raw = population()
        corpus = corpus_bundle()
        self.assertEqual(contract.validate_population(current, raw, "d" * 64, "e" * 40, corpus), [])
        for case in ("missing", "tamper", "zero-count", "status2", "signal", "probe-policy", "other-run-policy", "command", "top-status"):
            changed, members, reports = copy.deepcopy(current), copy.deepcopy(raw), copy.deepcopy(corpus)
            row = next(item for item in changed["phase_ownership"]["phases"] if item["phase"] == "throughput")
            native = json.loads(members[row["file"]]["receipt"])
            if case == "missing":
                reports.pop("metadata")
            elif case == "tamper":
                reports["summary"] += b" "
            elif case == "zero-count":
                from compiler_test import corpus as fixture
                reports = {key: encoded(value) for key, value in fixture().items()}
                row.update(corpus_summary_sha256=contract.sha(reports["summary"]), corpus_metadata_sha256=contract.sha(reports["metadata"]))
            elif case == "status2":
                native["exit_status"] = 512
            elif case == "signal":
                native["exit_status"] = 11
            elif case == "probe-policy":
                row["kind"] = "capture"
            elif case == "other-run-policy":
                changed["phase_ownership"]["phases"][0]["exit_policy"] = "corpus-report-only-v1"
            elif case == "command":
                row["argv"] = ["/bin/false"]
                native["command_sha256"] = contract.sha(contract.command_bytes(row["argv"]))
                members[row["file"]]["command"] = contract.command_bytes(row["argv"])
            else:
                changed["throughput"]["exit"] = 0
            members[row["file"]]["receipt"] = encoded(native)
            row["receipt_sha256"] = contract.sha(members[row["file"]]["receipt"])
            with self.subTest(case=case):
                self.assertTrue(contract.validate_population(changed, members, "d" * 64, "e" * 40, reports))

    def test_exit_status_encoding_and_bounds_are_bound(self):
        argv = ["/bin/true"]
        for key, value in (("exit_status_encoding", "exit-code"), ("exit_status", -1),
                           ("exit_status", 65536), ("exit_status", True)):
            native = record(argv)
            native[key] = value
            with self.subTest(key=key, value=value):
                self.assertTrue(contract.validate_record(native, argv, "/checkout", 5, "d" * 64, b"", b"", nominal=False))

    def test_default_legacy_run_uses_the_existing_lane(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(compare, "OWNED_PHASE_CONTEXT", None):
                self.assertEqual(compare.run(["/bin/sh", "-c", "printf legacy"], root, root / "legacy.log", 5), 0)
            self.assertIn(b"legacy", (root / "legacy.log").read_bytes())


@unittest.skipUnless(NATIVE_DRIVER is not None and sys.platform.startswith("linux"), "requires hosted canonical TCC native driver")
class ActualNativeOwner(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.work = self.root / "work"
        self.evidence = self.root / "evidence"
        self.work.mkdir()
        self.evidence.mkdir()
        self.receipt = {"state": "failed", "reasons": [], "phase": "diagnostic-owned-phase"}
        self.context = compare.NativePhaseContext(NATIVE_DRIVER, self.work, self.evidence, self.receipt)
        self.prior = compare.OWNED_PHASE_CONTEXT
        compare.OWNED_PHASE_CONTEXT = self.context

    def tearDown(self):
        compare.OWNED_PHASE_CONTEXT = self.prior
        self.temp.cleanup()

    def raw(self):
        path = self.context.directory / "0001.json"
        native = contract.read_record(path.read_bytes())
        return native, path

    def test_probe_cannot_select_report_only_corpus_policy(self):
        self.receipt["phase"] = "throughput"
        with mock.patch.object(compare.subprocess, "Popen") as spawn:
            with self.assertRaises(compare.OwnedPhaseFailed):
                self.context.execute(["/bin/false"], self.work, None, 5, kind="capture",
                                     exit_policy="corpus-report-only-v1")
            spawn.assert_not_called()
        self.assert_latched_without_next_child()

    def test_arbitrary_run_cannot_select_report_only_corpus_policy(self):
        self.receipt["phase"] = "throughput"
        with mock.patch.object(compare.subprocess, "Popen") as spawn:
            with self.assertRaises(compare.OwnedPhaseFailed):
                self.context.execute(["/bin/false"], self.work, None, 5,
                                     exit_policy="corpus-report-only-v1")
            spawn.assert_not_called()
        self.assert_latched_without_next_child()

    def test_actual_nominal_producer_reader_and_expected_probe_failure(self):
        self.assertEqual(compare.run(["/bin/sh", "-c", "printf owned"], self.work, self.evidence / "run.log", 5), 0)
        native, path = self.raw()
        argv = ["/bin/sh", "-c", "printf owned"]
        self.assertEqual(contract.validate_record(native, argv, str(self.work), 5, self.context.driver_hash,
                         Path(str(path) + ".stdout").read_bytes(), Path(str(path) + ".stderr").read_bytes(),
                         receipt_path=str(path)), [])
        self.assertEqual(contract.validate_bootstrap(native, Path(str(path) + ".bootstrap.complete").read_bytes(),
                                                     self.receipt["phase_ownership"]), [])
        result = compare.captured_run(["/bin/sh", "-c", "exit 1"], capture_output=True, check=False, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.context.stopped)

    def escaped(self, mode):
        program = self.work / "escaped.py"
        program.write_text("import os,signal,time\nowner=os.getppid()\nchild=os.fork()\n"
            "if child==0:\n os.setsid()\n grand=os.fork()\n"
            " if grand==0:\n  os.setsid()\n  time.sleep(1.5)\n  open('late-marker','w').write('escaped')\n  time.sleep(5)\n"
            " time.sleep(5)\n"
            "time.sleep(.2)\n" +
            (f"os.kill(owner,signal.{mode})\n" if mode != "timeout" else "") + "time.sleep(5)\n")
        with self.assertRaises(compare.OwnedPhaseFailed):
            compare.run([sys.executable, "-B", str(program)], self.work, self.evidence / "escaped.log",
                        1 if mode == "timeout" else 5)
        native, path = self.raw()
        self.assertTrue(native["cleanup_proven"])
        self.assertGreaterEqual(native["cleanup_signalled"], 2)
        self.assertGreaterEqual(native["cleanup_reaped"], 2)
        self.assertEqual(native["state"], "failed")
        if mode == "timeout":
            self.assertEqual(native["timed_out"], 1)
        else:
            self.assertEqual(native["cancelled"], getattr(signal, mode))
        argv = [sys.executable, "-B", str(program)]
        self.assertEqual(contract.validate_record(native, argv, str(self.work), 1 if mode == "timeout" else 5,
            self.context.driver_hash, Path(str(path) + ".stdout").read_bytes(), Path(str(path) + ".stderr").read_bytes(),
            nominal=False, receipt_path=str(path)), [])
        self.assertTrue(contract.validate_record(native, argv, str(self.work), 1 if mode == "timeout" else 5,
            self.context.driver_hash, Path(str(path) + ".stdout").read_bytes(), Path(str(path) + ".stderr").read_bytes(),
            receipt_path=str(path)))
        with self.assertRaises(compare.OwnedPhaseFailed):
            compare.run(["/bin/sh", "-c", "printf wrong > next-marker"], self.work, self.evidence / "next.log", 5)
        self.assertFalse((self.work / "late-marker").exists())
        self.assertFalse((self.work / "next-marker").exists())
        self.assertTrue(self.work.is_dir())
        print(f"ORDINARY_NATIVE_PHASE mode={mode} cleanup_proven=1 signalled={native['cleanup_signalled']} reaped={native['cleanup_reaped']} no_next_phase=1", flush=True)

    def test_real_timeout_escaped_descendants_stop_next_phase(self):
        self.escaped("timeout")

    def test_real_sigterm_escaped_descendants_stop_next_phase(self):
        self.escaped("SIGTERM")

    def test_real_sigint_escaped_descendants_stop_next_phase(self):
        self.escaped("SIGINT")

    def test_missing_proof_retains_work_and_refuses_every_next_child(self):
        native, path = None, None
        with mock.patch.object(compare.subprocess, "Popen") as spawn:
            spawn.return_value.wait.return_value = 1
            with self.assertRaises(compare.ClosureCleanupUncertain):
                compare.run(["/bin/true"], self.work, self.evidence / "missing.log", 5)
        self.assertFalse(self.receipt["cleanup_proven"])
        self.assertEqual(self.receipt["work_retained"], str(self.work))
        self.assertTrue((self.evidence / "cleanup-uncertain").exists())
        with self.assertRaises(compare.OwnedPhaseFailed):
            compare.run(["/bin/sh", "-c", "touch next-marker"], self.work, self.evidence / "next.log", 5)
        self.assertFalse((self.work / "next-marker").exists())


    def assert_latched_without_next_child(self):
        self.assertTrue(self.context.stopped)
        self.assertEqual(self.receipt["phase_ownership"]["state"], "failed")
        self.assertEqual(self.receipt["work_retained"], str(self.work))
        with mock.patch.object(compare.subprocess, "Popen") as spawn:
            with self.assertRaises(compare.OwnedPhaseFailed):
                compare.run(["/bin/true"], self.work, self.evidence / "later.log", 5)
            spawn.assert_not_called()

    def test_prelaunch_oserror_then_repaired_driver_never_admits_next_phase(self):
        with mock.patch.object(compare, "sha256", side_effect=PermissionError("diagnostic unreadable driver")), \
                mock.patch.object(compare.subprocess, "Popen") as spawn:
            with self.assertRaises(PermissionError):
                compare.run(["/bin/true"], self.work, self.evidence / "unreadable.log", 5)
            spawn.assert_not_called()
        # The real readable driver is restored, but this attempt stays stopped.
        self.assert_latched_without_next_child()


    def test_unsupported_capture_options_latch_before_launch(self):
        with mock.patch.object(compare.subprocess, "Popen") as spawn:
            with self.assertRaises(compare.OwnedPhaseFailed):
                compare.captured_run(["/bin/true"], env={"LC_ALL": "C"}, timeout=5)
            spawn.assert_not_called()
        self.assert_latched_without_next_child()

    def test_captured_text_decode_failure_latches_after_proven_cleanup(self):
        with self.assertRaises(UnicodeDecodeError):
            compare.captured_run([sys.executable, "-B", "-c", "import sys; sys.stdout.buffer.write(bytes([255]))"], cwd=self.work,
                                 capture_output=True, check=False, text=True, timeout=5)
        native, _ = self.raw()
        self.assertTrue(native["cleanup_proven"])
        self.assert_latched_without_next_child()

    def test_checkpoint_failure_latches_before_launch(self):
        with mock.patch.object(compare, "checkpoint", return_value="diagnostic failed checkpoint"), \
                mock.patch.object(compare.subprocess, "Popen") as spawn:
            with self.assertRaises(compare.OwnedPhaseFailed):
                compare.run(["/bin/true"], self.work, self.evidence / "checkpoint.log", 5)
            spawn.assert_not_called()
        self.assert_latched_without_next_child()

    def test_log_publication_oserror_after_native_cleanup_stops_next_phase(self):
        log = self.evidence / "directory-log"
        log.mkdir()
        with self.assertRaises(IsADirectoryError):
            compare.run(["/bin/true"], self.work, log, 5)
        native, _ = self.raw()
        self.assertTrue(native["cleanup_proven"])
        self.assert_latched_without_next_child()

    def test_signal_crashed_probe_cannot_be_an_expected_failure(self):
        with self.assertRaises(compare.OwnedPhaseFailed):
            compare.captured_run(["/bin/sh", "-c", "kill -SEGV $$"], cwd=self.work,
                                 capture_output=True, check=False, timeout=5)
        native, _ = self.raw()
        self.assertTrue(native["cleanup_proven"])
        self.assertFalse(os.WIFEXITED(native["exit_status"]))
        self.assert_latched_without_next_child()

    def test_changed_bootstrap_marker_refuses_before_spawn(self):
        with mock.patch.object(compare, "bounded_owned_member", return_value=b"changed marker"), \
                mock.patch.object(compare.subprocess, "Popen") as spawn:
            with self.assertRaises(compare.OwnedPhaseFailed):
                compare.run(["/bin/true"], self.work, self.evidence / "marker.log", 5)
            spawn.assert_not_called()
        self.assert_latched_without_next_child()

    def test_nested_checkout_driver_is_rejected_before_any_process(self):
        trusted = self.root / "trusted"
        driver = trusted / "nested" / ".cache" / "bootstrap-driver" / "posix" / ("a" * 64) / "build-fixture"
        driver.parent.mkdir(parents=True)
        driver.write_bytes(b"diagnostic")
        driver.chmod(0o755)
        with mock.patch.object(compare, "TRUSTED_ROOT", trusted), \
                mock.patch.object(compare.subprocess, "Popen") as spawn:
            with self.assertRaises(ValueError):
                compare.NativePhaseContext(driver, self.work, self.evidence, {})
            spawn.assert_not_called()

    def test_native_expected_bootstrap_pin_rejects_before_command(self):
        path = self.evidence / "wrong-native-pin.json"
        late = self.work / "should-not-exist"
        argv = [str(NATIVE_DRIVER), "compiler_closure", "owned-phase", str(path), str(self.work), "5",
                self.context.driver_hash, "a" * 64, "--", "/bin/sh", "-c", f"touch {late}"]
        result = compare.subprocess.run(argv, cwd=compare.TRUSTED_ROOT, capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(late.exists())
        self.assertFalse(Path(str(path) + ".claim").exists())

    def test_changed_trusted_driver_refuses_before_spawn(self):
        with mock.patch.object(compare, "sha256", return_value="a" * 64), \
                mock.patch.object(compare.subprocess, "Popen") as spawn:
            with self.assertRaises(compare.OwnedPhaseFailed):
                compare.run(["/bin/true"], self.work, self.evidence / "changed.log", 5)
            spawn.assert_not_called()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--native-driver", type=Path)
    options, remaining = parser.parse_known_args()
    if options.native_driver:
        NATIVE_DRIVER = options.native_driver.resolve(strict=True)
        # unittest's class decorator is evaluated before argv parsing.
        ActualNativeOwner.__unittest_skip__ = False
    unittest.main(argv=[sys.argv[0], *remaining])
