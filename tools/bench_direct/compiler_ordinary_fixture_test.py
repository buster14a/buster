#!/usr/bin/env python3
"""Private hosted full ordinary bridge fixture. DIAGNOSTIC-UNQUALIFIED; no performance claims.

The actual native initializer and ordinary measurement functions are exercised.
Only summary/corpus data are synthetic, from the existing compiler_test fixtures.
This file is also the private lab/data adapter. It launches no measurement children.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compiler_compare as compare
import compiler_owned_phase as owned
import compiler_owned_plan as plan_contract
import compiler_receipt as receipt_contract

NATIVE_DRIVER = None
DIAGNOSTIC = {"schema": "buster-compiler-ordinary-diagnostic-fixture-v1",
              "diagnostic_only": True, "performance_qualified": False, "activation_allowed": False}


def guard() -> None:
    """Private fixtures never accept physical request environments or the protected host."""
    if any(key.startswith("BQ_") and not (key == "BQ_REQUIRE_DISTINCT_GROUP" and value == "1")
           for key, value in os.environ.items()):
        raise ValueError("private diagnostic fixture refuses physical BQ environment")
    model = compare.cpu_model()
    if not model or "9700x" in model.lower():
        raise ValueError("private diagnostic fixture refuses unknown or protected 9700X host")


def digest(path: Path) -> str:
    if not path.is_file() or path.is_symlink() or not os.access(path, os.X_OK) or path.stat().st_size == 0:
        raise ValueError("diagnostic binary must be a real nonempty regular executable")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path: Path, value: object) -> None:
    path.write_text(receipt_contract.dumps(value) + "\n", encoding="utf-8")


def emit(argv: list[str], *, corpus: bool) -> int:
    guard()
    parser = argparse.ArgumentParser(description="Private diagnostic data only, never measurements")
    parser.add_argument("command", choices=("run",) if corpus else ("compare",))
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpu", type=int, required=True)
    parser.add_argument("--warmups", type=int, required=True)
    if corpus:
        parser.add_argument("--baseline-id", required=True)
        parser.add_argument("--candidate-id", required=True)
        parser.add_argument("--profile", required=True)
        parser.add_argument("--mode", required=True)
        parser.add_argument("--pairs", type=int, required=True)
        parser.add_argument("--timeout", type=int, required=True)
    else:
        parser.add_argument("--repo-root", type=Path, required=True)
        parser.add_argument("--target-minutes", type=int, required=True)
    arguments = parser.parse_args(argv)
    a, b = digest(arguments.baseline), digest(arguments.candidate)
    if arguments.cpu != receipt_contract.PROFILE["cpu"]:
        raise ValueError("diagnostic caller changed the frozen CPU argument")
    from compiler_test import corpus as corpus_data, summary as lab_data
    if corpus:
        profile = receipt_contract.THROUGHPUT_PROFILE
        if (arguments.profile, arguments.mode, arguments.pairs, arguments.warmups, arguments.timeout) !=                 ("ci", "all", profile["pairs_per_round"], profile["warmups"], 120):
            raise ValueError("diagnostic caller changed the frozen corpus recipe")
        if any(not re.fullmatch(r"[a-f0-9]{40}", value) for value in (arguments.baseline_id, arguments.candidate_id)):
            raise ValueError("diagnostic corpus source pins malformed")
        # A complete detected regression remains complete scientific raw data.
        documents = corpus_data("regression")
        documents["metadata"]["compiler_provenance"] = [{"sha256": a}, {"sha256": b}]
        documents["metadata"].update(baseline_id=arguments.baseline_id, candidate_id=arguments.candidate_id,
                                     diagnostic_fixture=copy.deepcopy(DIAGNOSTIC))
        documents["summary"]["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
    else:
        if (arguments.target_minutes, arguments.warmups) !=                 (receipt_contract.PROFILE["target_minutes"], receipt_contract.PROFILE["warmups"]):
            raise ValueError("diagnostic caller changed the historical lab recipe")
        documents = {"summary": lab_data("slower"), "metadata": {"diagnostic_fixture": copy.deepcopy(DIAGNOSTIC)}}
        documents["summary"]["baseline"]["sha256"] = a
        documents["summary"]["candidate"]["sha256"] = b
        documents["summary"]["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
    arguments.output.mkdir(parents=True, exist_ok=False)
    for name, document in documents.items():
        write(arguments.output / (name + ".json"), document)
    (arguments.output / "DIAGNOSTIC-UNQUALIFIED").write_text(
        "Synthetic contract data only. No timed samples or performance qualification.\n", encoding="utf-8")
    print("COMPILER_ORDINARY_DIAGNOSTIC_DATA complete=1 qualified=0 activation=0")
    return 0


def raw_bundle(current: dict, evidence: Path) -> dict:
    raw = {operation: (evidence / f"closure-{operation}.json.manifest.tsv").read_bytes()
           for operation in ("snapshot", "restore", "verify")}
    members = {}
    for row in current["phase_ownership"]["phases"]:
        path = evidence / "owned-phases" / row["file"]
        members[row["file"]] = {"receipt": path.read_bytes(),
                               "command": Path(str(path) + ".argv").read_bytes(),
                               "stdout": Path(str(path) + ".stdout").read_bytes(),
                               "stderr": Path(str(path) + ".stderr").read_bytes(),
                               "bootstrap": Path(str(path) + ".bootstrap.complete").read_bytes()}
    raw["owned_phases"] = members
    return raw


class PrivateGuards(unittest.TestCase):
    def test_physical_bq_requests_are_refused_without_launch(self):
        with mock.patch.dict(os.environ, {"BQ_REQUEST_RUN_ID": "diagnostic-must-refuse"}),                 mock.patch.object(compare, "cpu_model", return_value="AMD EPYC hosted"):
            with self.assertRaises(ValueError):
                guard()

    def test_protected_host_cannot_use_private_data_provider(self):
        with mock.patch.object(compare, "cpu_model", return_value="AMD Ryzen 7 9700X 8-Core Processor"):
            with self.assertRaises(ValueError):
                guard()


@unittest.skipUnless(NATIVE_DRIVER is not None and sys.platform.startswith("linux"),
                     "requires the hosted canonical trusted native driver")
class ActualOrdinaryMeasure(unittest.TestCase):
    def setUp(self):
        guard()
        self.temp = tempfile.TemporaryDirectory(prefix="ordinary-diagnostic-")
        self.directory = Path(self.temp.name).resolve()
        self.candidate = self.directory / "candidate with spaces"
        self.export = self.directory / "native-init"
        self.work = self.directory / "work"
        self.evidence = self.directory / "evidence"
        self.prior = compare.OWNED_PHASE_CONTEXT
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(setattr, compare, "OWNED_PHASE_CONTEXT", self.prior)
        initialization_work = self.directory / "initialize-work"
        initialization_evidence = self.directory / "initialize-evidence"
        initialization_work.mkdir()
        initialization_evidence.mkdir()
        initialization_receipt = {"state": "failed", "reasons": [], "phase": "diagnostic-initialize"}
        initialization_context = compare.NativePhaseContext(
            NATIVE_DRIVER, initialization_work, initialization_evidence, initialization_receipt)
        compare.OWNED_PHASE_CONTEXT = initialization_context
        try:
            result = initialization_context.execute(
                [str(NATIVE_DRIVER), "compiler_closure", "ordinary-fixture-initialize",
                 str(self.candidate), str(self.export)], compare.TRUSTED_ROOT,
                initialization_evidence / "initialize.log", 180)
            initialization_context.finish()
        finally:
            compare.OWNED_PHASE_CONTEXT = self.prior
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors="replace"))
        initialization_row = initialization_receipt["phase_ownership"]["phases"][0]
        initialization_path = initialization_evidence / "owned-phases" / initialization_row["file"]
        initialization_native = owned.read_record(initialization_path.read_bytes())
        self.assertEqual(owned.validate_record(
            initialization_native, initialization_row["argv"], initialization_row["cwd"], 180,
            initialization_context.driver_hash, Path(str(initialization_path) + ".stdout").read_bytes(),
            Path(str(initialization_path) + ".stderr").read_bytes(), receipt_path=str(initialization_path)), [])
        self.assertEqual(owned.validate_bootstrap(
            initialization_native, Path(str(initialization_path) + ".bootstrap.complete").read_bytes(),
            initialization_receipt["phase_ownership"]), [])
        self.fixture = json.loads((self.export / "fixture-plan.json").read_bytes())
        self.assertEqual(self.fixture["schema"], DIAGNOSTIC["schema"])
        for key in ("diagnostic_only", "performance_qualified", "activation_allowed"):
            self.assertIs(self.fixture[key], DIAGNOSTIC[key])
        self.assertEqual(self.fixture["state"], "complete")
        self.assertEqual(self.fixture["root"], str(self.candidate))
        self.assertEqual(self.fixture["output"], str(self.export))
        self.assertEqual(self.fixture["provider"], str(Path(__file__).resolve()))
        self.assertNotEqual(self.fixture["base"], self.fixture["head"])
        self.assertNotEqual(self.fixture["base_tree"], self.fixture["head_tree"])
        compare.prepare_scratch(self.work)
        compare.prepare_scratch(self.evidence)
        self.bins = self.work / "bin"
        self.bins.mkdir()
        self.trusted_revision = compare.git(compare.TRUSTED_ROOT, "rev-parse", "HEAD")

    def tearDown(self):
        compare.OWNED_PHASE_CONTEXT = self.prior
        self.temp.cleanup()

    def identity(self, mode="pull"):
        return {"mode": mode, "repository": "buster14a/buster", "ref": "refs/pull/1/head" if mode == "pull" else "refs/heads/main",
                "pull": "1", "pull_head": self.fixture["head"], "base": self.fixture["base"],
                "base_tree": self.fixture["base_tree"], "head": self.fixture["head"],
                "head_tree": self.fixture["head_tree"], "trusted_revision": self.trusted_revision,
                "request_run_id": "1", "run_id": "1", "run_attempt": "1"}

    def test_actual_full_ordinary_measure_writer_then_strict_reader(self):
        identity = self.identity()
        arguments = argparse.Namespace(**identity, candidate=self.candidate, lab=Path(__file__).resolve(),
                                       work=self.work, evidence=self.evidence, closure_policy="snapshot-v1",
                                       closure_driver=NATIVE_DRIVER)
        current = {"schema": receipt_contract.RECEIPT_SCHEMA, "mode": "pull", "state": "failed", "reasons": [],
                   "identity": identity, "profile": copy.deepcopy(receipt_contract.PROFILE),
                   "throughput_profile": copy.deepcopy(receipt_contract.THROUGHPUT_PROFILE),
                   "host": {"cpu_model": compare.cpu_model()}, "binaries": {}, "lab": {},
                   "timings": {"build_seconds": {}}, "diagnostic_fixture": copy.deepcopy(DIAGNOSTIC)}
        summaries = []
        context = compare.NativePhaseContext(NATIVE_DRIVER, self.work, self.evidence, current)
        compare.OWNED_PHASE_CONTEXT = context
        compare.measure(arguments, self.candidate, self.work, self.evidence, self.bins,
                        self.evidence / "build.log", current, summaries)
        context.finish()
        compare.OWNED_PHASE_CONTEXT = self.prior
        self.assertEqual(current["reasons"], [])
        self.assertEqual(current["state"], "measured")  # Structural state only; diagnostic marker remains mandatory.
        self.assertFalse(context.stopped)
        self.assertEqual(current["phase_ownership"]["state"], "complete")
        rows = current["phase_ownership"]["phases"]
        core = [row for row in rows if row["kind"] == "run"]
        captured = [row for row in rows if row["kind"] == "capture"]
        self.assertEqual(len(core), 12)
        self.assertEqual(len(captured), 2)  # Real diff plus expected missing scaling selector probe.
        self.assertEqual([row["ordinal"] for row in rows], list(range(1, 15)))
        raw = raw_bundle(current, self.evidence)
        self.assertEqual(receipt_contract.validate_closure(current, raw, expected_policy="snapshot-v1",
            expected_phase_driver_sha256=context.driver_hash, expected_trusted_revision=self.trusted_revision,
            require_owned_phases=True), [])
        self.assertEqual(owned.validate_population(current, raw["owned_phases"],
                                                  context.driver_hash, self.trusted_revision), [])
        self.assertEqual(plan_contract.validate_plan(current, current["phase_ownership"], core), [])
        self.assertEqual(receipt_contract.classify(summaries[0], current["binaries"]), [])
        self.assertEqual(summaries[0]["verdict"]["outcome"], "slower")
        self.assertEqual(summaries[0]["diagnostic_fixture"], DIAGNOSTIC)
        corpus = json.loads((self.evidence / "throughput/summary.json").read_bytes())
        metadata = json.loads((self.evidence / "throughput/metadata.json").read_bytes())
        self.assertEqual(receipt_contract.classify_throughput(corpus, metadata, current["binaries"]), [])
        self.assertGreater(corpus["confirmed_regressions"], 0)
        self.assertEqual(corpus["diagnostic_fixture"], DIAGNOSTIC)
        self.assertEqual(metadata["diagnostic_fixture"], DIAGNOSTIC)
        self.assertNotEqual(current["binaries"]["baseline"]["sha256"], current["binaries"]["candidate"]["sha256"])
        for row in rows:
            native = owned.read_record(raw["owned_phases"][row["file"]]["receipt"])
            self.assertTrue(native["cleanup_proven"])
            for key in ("cleanup_signalled", "cleanup_reaped", "reservation_retained", "ownership_lost",
                        "timed_out", "cancelled", "capture_failed", "output_truncated", "tree_cleanup_failed"):
                self.assertEqual(native[key], 0, (row["file"], key))
        # Capture cat-file is allowed to fail cleanly; each core success is raw wait status zero.
        self.assertTrue(any(owned.read_record(raw["owned_phases"][row["file"]]["receipt"])["exit_status"]
                            for row in captured))
        self.assertTrue(all(owned.read_record(raw["owned_phases"][row["file"]]["receipt"])["exit_status"] == 0
                            for row in core))
        self.assertEqual(compare.git(self.candidate, "rev-parse", "HEAD"), self.fixture["base"])
        # Replay must reject missing proof and a self-consistent replacement of the actual lab command.
        missing = copy.deepcopy(raw)
        missing["owned_phases"].pop(rows[0]["file"])
        self.assertTrue(receipt_contract.validate_closure(current, missing, expected_policy="snapshot-v1",
            expected_phase_driver_sha256=context.driver_hash, expected_trusted_revision=self.trusted_revision,
            require_owned_phases=True))
        forged, forged_raw = copy.deepcopy(current), copy.deepcopy(raw)
        lab_row = next(row for row in forged["phase_ownership"]["phases"] if row["phase"] == "lab")
        lab_row["argv"] = ["/bin/true"]
        member = forged_raw["owned_phases"][lab_row["file"]]
        member["command"] = owned.command_bytes(lab_row["argv"])
        native = owned.read_record(member["receipt"])
        native["command_sha256"] = owned.sha(member["command"])
        member["receipt"] = (receipt_contract.dumps(native) + "\n").encode()
        lab_row["receipt_sha256"] = owned.sha(member["receipt"])
        self.assertTrue(receipt_contract.validate_closure(forged, forged_raw, expected_policy="snapshot-v1",
            expected_phase_driver_sha256=context.driver_hash, expected_trusted_revision=self.trusted_revision,
            require_owned_phases=True))
        (self.evidence / "DIAGNOSTIC-UNQUALIFIED").write_text(
            "Actual ordinary native phase writer/reader integration; synthetic slow data only; no physical qualification.\n")
        write(self.evidence / "diagnostic-receipt.json", current)
        print("COMPILER_ORDINARY_FULL_DIAGNOSTIC core=12 capture=2 strict_replay=1 qualified=0 activation=0")

    def test_public_main_observed_host_gate_still_refuses_before_build(self):
        # No host override or profile override: exercise the actual public host gate on the hosted CPU.
        identity = self.identity("main")
        work, evidence = self.directory / "public-work", self.directory / "public-evidence"
        argv = ["--candidate", str(self.candidate), "--lab", str(Path(__file__).resolve()),
                "--work", str(work), "--evidence", str(evidence), "--summary", str(self.directory / "public-summary.md")]
        for name in receipt_contract.IDENTITY_KEYS:
            argv += ["--" + name.replace("_", "-"), identity[name]]
        with mock.patch.object(compare, "run", side_effect=AssertionError("public hosted path must never build")) as run:
            self.assertEqual(compare.main(argv), 1)
            run.assert_not_called()
        public = json.loads((evidence / "receipt.json").read_bytes())
        self.assertEqual(public["state"], "failed")
        self.assertEqual(public["host"]["cpu_model"], compare.cpu_model())
        self.assertTrue(any("observed CPU" in value for value in public["reasons"]))
        self.assertEqual(public["binaries"], {})
        self.assertEqual(public["profile"], receipt_contract.PROFILE)
        self.assertNotIn("phase_ownership", public)

    def test_native_initializer_refuses_physical_request_before_writes(self):
        candidate, output = self.directory / "blocked-candidate", self.directory / "blocked-output"
        environment = dict(os.environ, BQ_REQUEST_RUN_ID="diagnostic-must-refuse")
        result = subprocess.run([str(NATIVE_DRIVER), "compiler_closure", "ordinary-fixture-initialize",
                                 str(candidate), str(output)], cwd=compare.TRUSTED_ROOT, env=environment,
                                stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                timeout=30, start_new_session=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(candidate.exists())
        self.assertFalse(output.exists())


def main(argv=None):
    global NATIVE_DRIVER
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments[:1] == ["compare"]:
        return emit(arguments, corpus=False)
    if arguments[:1] == ["--emit-corpus"]:
        return emit(arguments[1:], corpus=True)
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--native-driver", type=Path)
    options = parser.parse_args(arguments)
    guard()
    NATIVE_DRIVER = options.native_driver.resolve(strict=True) if options.native_driver else None
    if NATIVE_DRIVER is not None and sys.platform.startswith("linux"):
        # The class decorator runs before argv parsing.
        ActualOrdinaryMeasure.__unittest_skip__ = False
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
