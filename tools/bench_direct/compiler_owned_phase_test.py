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
             "receipt_publication_us": None, "stdout_sha256": contract.sha(stdout), "stderr_sha256": contract.sha(stderr),
             "trusted_root_sha256": contract.sha(b"/trusted"), "bootstrap_config_sha256": "c" * 64,
             "bootstrap_marker_sha256": contract.sha(marker()), "bootstrap_dependency_count": 4,
             "cleanup_proven": True}
    value.update({key: 0 for key in ("exit_status", "timed_out", "cancelled", "capture_failed", "output_truncated",
                  "cleanup_us", "cleanup_waves", "cleanup_signalled", "cleanup_reaped", "reservation_retained",
                  "ownership_lost", "tree_cleanup_failed")})
    return value


def population():
    identity = {"base": "b" * 40, "head": "a" * 40}
    rows, raw = [], {}
    git = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null", "-C", "/checkout"]
    planned = []
    for role, revision in (("baseline", identity["base"]), ("candidate", identity["head"])):
        planned += [(f"build-{role}", [*git, "checkout", "--quiet", "--detach", revision]),
                    (f"build-{role}", ["./build.sh", "generate", "--cc", "clang", "--no-include-tests"]),
                    (f"build-{role}", ["./build.sh", "build", "--config", "Release", "-t", "ide"])]
        if role == "baseline":
            planned.append(("closure-snapshot", ["/trusted/driver", "compiler_closure", "snapshot"]))
    planned += [("build-closure", [*git, "checkout", "--quiet", "--detach", identity["base"]]),
                ("build-closure", ["/trusted/driver", "compiler_closure", "restore"]),
                ("lab", ["/usr/bin/python3", "-B", "/trusted/tools/uarch_lab.py", "compare"]),
                ("throughput", ["/checkout/build/throughput-tools/throughput", "run"]),
                ("validate", ["/trusted/driver", "compiler_closure", "verify"])]
    for ordinal, (phase, argv) in enumerate(planned, 1):
        native = record(argv, ordinal=ordinal)
        receipt = encoded(native)
        name = f"{ordinal:04d}.json"
        rows.append({"ordinal": ordinal, "file": name, "phase": phase, "kind": "run", "argv": argv, "cwd": "/checkout",
                     "timeout": 5, "allow_exit_failure": False, "bridge_wall_us": 150, "receipt_sha256": contract.sha(receipt)})
        raw[name] = {"receipt": receipt, "command": contract.command_bytes(argv), "stdout": b"", "stderr": b"", "bootstrap": marker()}
    current = {"identity": identity, "inline_acceptance": {"requested": False}, "phase_ownership":
               {"schema": contract.POPULATION_SCHEMA, "state": "complete", "trusted_root": "/trusted",
                "trusted_revision": "e" * 40, "trusted_tree": "f" * 40, "directory": "/evidence/owned-phases",
                "driver_sha256": "d" * 64, "count": len(rows), "phases": rows}}
    return current, raw


class DataContract(unittest.TestCase):
    def test_full_core_population_and_identity_are_required(self):
        current, raw = population()
        self.assertEqual(contract.validate_population(current, raw, "d" * 64, "e" * 40), [])
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
                self.assertTrue(contract.validate_population(changed, members, "d" * 64, "e" * 40))

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
