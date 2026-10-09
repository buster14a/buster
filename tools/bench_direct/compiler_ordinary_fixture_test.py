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
EXPORT_ROOT = None
EXPORTED_CASES = []
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
        parser.add_argument("--pairs", type=int)
        parser.add_argument("--seed", type=int)
        parser.add_argument("--min-effect", type=float)
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
        case = os.environ.get("BUSTER_ORDINARY_DIAGNOSTIC_CORPUS_CASE", "regression")
        if case not in ("regression", "invalid", "missing", "bad-exit", "partial-numeric", "inconsistent-regression"):
            raise ValueError("private diagnostic corpus case is unsupported")
        documents = corpus_data("regression" if case in ("regression", "invalid", "partial-numeric", "inconsistent-regression") else "no substantial regression detected")
        documents["metadata"]["compiler_provenance"] = [{"sha256": a, "bytes": arguments.baseline.stat().st_size},
                                                      {"sha256": b, "bytes": arguments.candidate.stat().st_size}]
        documents["metadata"].update(baseline_id=arguments.baseline_id, candidate_id=arguments.candidate_id,
                                     diagnostic_fixture=copy.deepcopy(DIAGNOSTIC))
        documents["summary"]["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
        documents["summary"]["diagnostic_case"] = case
        documents["metadata"]["diagnostic_case"] = case
        if case == "invalid":
            documents["summary"]["valid"] = False
        if case == "partial-numeric":
            documents["summary"]["comparisons"][0]["medians"] = {}
            documents["summary"]["comparisons"][0]["tests"][0].pop("ci_low")
        if case == "inconsistent-regression":
            for cell in documents["summary"]["comparisons"]:
                for test in cell["tests"]:
                    test.update(regression=False, p_value=1.0, margin_exceedances=0)
        if case == "missing":
            documents = {}
    else:
        if (arguments.target_minutes, arguments.warmups) !=                 (receipt_contract.PROFILE["target_minutes"], receipt_contract.PROFILE["warmups"]):
            raise ValueError("diagnostic caller changed the historical lab recipe")
        documents = {"summary": lab_data("slower"), "metadata": {"diagnostic_fixture": copy.deepcopy(DIAGNOSTIC)}}
        documents["summary"]["baseline"]["sha256"] = a
        documents["summary"]["candidate"]["sha256"] = b
        documents["summary"]["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
        if any(value is not None for value in (arguments.pairs, arguments.seed, arguments.min_effect)):
            if (arguments.pairs, arguments.seed, arguments.min_effect) != (40, 20261003, 0.5):
                raise ValueError("diagnostic caller changed the named forty-pair recipe")
            documents["summary"]["plan"].update(pairs=40, complete_pairs=40, seed=20261003, confidence=0.95,
                bootstrap_resamples=2000, fresh_copy=True, order="ABBA", reason="--pairs 40")
            for role in ("baseline", "candidate"):
                documents["summary"][role]["runs"] = 40
            documents["summary"]["verdict"].update(n=40, min_effect_percent=0.5)
    arguments.output.mkdir(parents=True, exist_ok=False)
    for name, document in documents.items():
        write(arguments.output / (name + ".json"), document)
    (arguments.output / "DIAGNOSTIC-UNQUALIFIED").write_text(
        "Synthetic contract data only. No timed samples or performance qualification.\n", encoding="utf-8")
    complete_data = not corpus or case in ("regression", "bad-exit")
    print("COMPILER_ORDINARY_DIAGNOSTIC_DATA data_complete=" + str(int(complete_data)) +
          " case=" + (case if corpus else "lab") + " qualified=0 activation=0")
    return 1 if corpus else 0


def prepare_export(requested: Path) -> Path:
    guard()
    if not requested.is_absolute() or requested != requested.resolve() or requested.parent != requested.parent.resolve(strict=True) or \
            requested.exists() or requested.is_symlink() or requested == requested.parent or \
            (compare.overlaps(requested, compare.TRUSTED_ROOT) or requested == Path.home().resolve() or requested in Path.home().resolve().parents):
        raise ValueError("diagnostic export requires a fresh canonical absolute root outside trusted tools and home ancestors")
    requested.mkdir(mode=0o700)
    (requested / "DIAGNOSTIC-UNQUALIFIED").write_text(
        "Private hosted functional producer/reader data only. Synthetic statistics; no physical qualification or activation.\n")
    write(requested / "index.json", dict(DIAGNOSTIC, state="pending", cases=[]))
    return requested


def retain_case(case) -> None:
    """Export available success/failure bytes before temporary cleanup; never execute artifacts."""
    if EXPORT_ROOT is None:
        return
    result = case._outcome.result
    failed = any(test is case for test, _ in result.failures + result.errors)
    errors = []
    initialization_context = getattr(case, "initialization_context", None)
    initialization_receipt = getattr(case, "initialization_receipt", None)
    if initialization_context is not None and isinstance(initialization_receipt, dict):
        initialization_context.finish()
        initialization_receipt["state"] = "failed" if initialization_context.stopped else "complete"
        initialization_receipt["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
        problem = compare.write_receipt(initialization_receipt, case.directory / "initialize-evidence")
        if problem:
            errors.append(problem)
    current = getattr(case, "current", None)
    context = getattr(case, "context", None)
    if context is not None:
        context.finish()
    if isinstance(current, dict):
        current["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
        if failed:
            current["state"] = "failed"
            current["reasons"].append("private diagnostic integration test failed; no qualification")
        problem = compare.write_receipt(current, case.evidence)
        if problem:
            errors.append(problem)
    destination = EXPORT_ROOT / case._testMethodName
    destination.mkdir()
    roots = {"fixture": case.export,
             "initialization-evidence": case.directory / "initialize-evidence",
             "ordinary-evidence": case.evidence,
             "raw-lab": case.work / "lab", "raw-throughput": case.work / "throughput",
             "public-evidence": case.directory / "public-evidence"}
    paths, omissions = {}, {}
    for name, source in roots.items():
        if source.is_dir():
            required = ("fixture-plan.json",) if name == "fixture" else ()
            if name == "ordinary-evidence" and isinstance(current, dict):
                required = ("receipt.json",)
            if name == "public-evidence":
                required = ("receipt.json",)
            problems, omitted = compare.export_tree(
                source, destination / name, EXPORT_ROOT, compare.EVIDENCE_IGNORE, required)
            paths[name] = name
            errors.extend(problems)
            if omitted:
                omissions[name] = omitted
                errors.append("private diagnostic export omitted raw members: " + name)
    fixture = getattr(case, "fixture", {})
    metadata = dict(DIAGNOSTIC, state="failed" if failed or errors else "passed",
                    test=case._testMethodName, native_fixture=fixture, paths=paths,
                    ordinary_state=current.get("state") if isinstance(current, dict) else None,
                    identity=current.get("identity") if isinstance(current, dict) else None,
                    phase_count=current.get("phase_ownership", {}).get("count") if isinstance(current, dict) else None,
                    errors=errors, omissions=omissions)
    write(destination / "diagnostic-attempt.json", metadata)
    EXPORTED_CASES.append({"path": destination.name, "state": metadata["state"]})
    write(EXPORT_ROOT / "index.json", dict(DIAGNOSTIC, state="pending", cases=EXPORTED_CASES))
    if errors:
        raise AssertionError("; ".join(errors))


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
    raw["owned_throughput"] = {"summary": (evidence / "throughput/summary.json").read_bytes(),
                              "metadata": (evidence / "throughput/metadata.json").read_bytes()}
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
        self.addCleanup(retain_case, self)
        initialization_work = self.directory / "initialize-work"
        initialization_evidence = self.directory / "initialize-evidence"
        initialization_work.mkdir()
        initialization_evidence.mkdir()
        initialization_receipt = {"state": "failed", "reasons": [], "phase": "diagnostic-initialize"}
        initialization_context = compare.NativePhaseContext(
            NATIVE_DRIVER, initialization_work, initialization_evidence, initialization_receipt)
        self.initialization_context = initialization_context
        self.initialization_receipt = initialization_receipt
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
        # addCleanup exports data before removing the private temporary roots.

    def identity(self, mode="pull"):
        return {"mode": mode, "repository": "buster14a/buster", "ref": "refs/pull/1/head" if mode == "pull" else "refs/heads/main",
                "pull": "1", "pull_head": self.fixture["head"], "base": self.fixture["base"],
                "base_tree": self.fixture["base_tree"], "head": self.fixture["head"],
                "head_tree": self.fixture["head_tree"], "trusted_revision": self.trusted_revision,
                "request_run_id": "1", "run_id": "1", "run_attempt": "1"}

    def measurement(self):
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
        self.current = current
        context = compare.NativePhaseContext(NATIVE_DRIVER, self.work, self.evidence, current)
        self.context = context
        compare.OWNED_PHASE_CONTEXT = context
        return arguments, current, context, summaries

    def supported_main(self, policy, profile):
        identity = self.identity("main")
        arguments = ["--candidate", str(self.candidate), "--lab", str(Path(__file__).resolve()),
            "--work", str(self.work), "--evidence", str(self.evidence),
            "--summary", str(self.directory / "supported-main-summary.md"),
            "--closure-policy", policy, "--main-owned-phases", "--main-profile", profile,
            "--closure-driver", str(NATIVE_DRIVER)]
        for key, value in identity.items():
            arguments.extend(["--" + key.replace("_", "-"), value])
        def diagnostic_host(current):
            current["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
            return ""
        prior_cwd = os.getcwd()
        try:
            os.chdir(self.directory)
            with mock.patch.object(compare, "host_problem", side_effect=diagnostic_host):
                status = compare.main(arguments)
        finally:
            os.chdir(prior_cwd)
        self.current = json.loads((self.evidence / "receipt.json").read_bytes())
        self.assertEqual(status, 0, self.current["reasons"])
        self.assertEqual(self.current["state"], "measured")
        self.assertEqual(self.current["profile"], receipt_contract.named_main_profile(profile))
        ownership = self.current["phase_ownership"]
        self.assertEqual(ownership["schema"], owned.MAIN_POPULATION_SCHEMA)
        self.assertIs(ownership["owned_preflight"], True)
        core = [row for row in ownership["phases"] if row["kind"] == "run"]
        self.assertEqual(len(core), 11 if policy == "legacy-rebuild" else 12)
        expected = dict(expected_policy=policy, expected_profile=profile,
            expected_phase_schema=owned.MAIN_POPULATION_SCHEMA, require_owned_phases=True, require_owned_preflight=True,
            expected_phase_driver_sha256=digest(NATIVE_DRIVER), expected_trusted_revision=self.trusted_revision)
        bundle = raw_bundle(self.current, self.evidence)
        self.assertEqual(receipt_contract.validate_closure(self.current, bundle, **expected), [])
        summary = json.loads((self.evidence / "lab/summary.json").read_bytes())
        self.assertEqual(receipt_contract.classify(summary, self.current["binaries"], expected_profile=profile), [])
        self.assertEqual(summary["verdict"]["outcome"], "slower")
        self.assertEqual(self.current["throughput"]["exit"], 1)
        lab = next(row for row in core if row["phase"] == "lab")
        if profile == "compiler-main-40pairs-v1":
            self.assertEqual(lab["timeout"], 300)
            self.assertEqual(lab["argv"][-6:], ["--pairs", "40", "--seed", "20261003", "--min-effect", "0.5"])
        else:
            self.assertEqual(lab["timeout"], compare.LAB_TIMEOUT_SECONDS)
            self.assertNotIn("--pairs", lab["argv"])
        stripped = copy.deepcopy(bundle)
        stripped["owned_phases"].pop(next(iter(stripped["owned_phases"])))
        self.assertTrue(receipt_contract.validate_closure(self.current, stripped, **expected))
        self.assertIsNone(compare.OWNED_PHASE_CONTEXT)
        print("COMPILER_MAIN_OWNED_DIAGNOSTIC policy=" + policy + " profile=" + profile +
              " core=" + str(len(core)) + " strict_replay=1 regression_report_only=1 qualification=unqualified")

    def test_supported_main_owned_long_legacy_producer_then_reader(self):
        self.supported_main("legacy-rebuild", "compiler-compare-v1")

    def test_supported_main_owned_forty_legacy_producer_then_reader(self):
        self.supported_main("legacy-rebuild", "compiler-main-40pairs-v1")

    def test_supported_main_owned_forty_snapshot_producer_then_reader(self):
        self.supported_main("snapshot-v1", "compiler-main-40pairs-v1")

    def test_canonical_driver_path_reports_only_identity_validated_current_bootstrap(self):
        result = subprocess.run([str(NATIVE_DRIVER), "compiler_closure", "driver-path"],
            cwd=self.directory, capture_output=True, text=True, timeout=30, start_new_session=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), str(NATIVE_DRIVER))
        import shutil
        impostor = self.directory / "copied-driver"
        shutil.copyfile(NATIVE_DRIVER, impostor)
        impostor.chmod(0o755)
        refused = subprocess.run([str(impostor), "compiler_closure", "driver-path"],
            cwd=self.directory, capture_output=True, text=True, timeout=30, start_new_session=True)
        self.assertNotEqual(refused.returncode, 0)
        self.assertNotEqual(refused.stdout.strip(), str(impostor))

    def test_normal_main_snapshot_initializes_owned_preflight_before_all_children(self):
        identity = self.identity("main")
        arguments = ["--candidate", str(self.candidate), "--lab", str(Path(__file__).resolve()),
            "--work", str(self.work), "--evidence", str(self.evidence),
            "--summary", str(self.directory / "main-summary.md"),
            "--closure-policy", "snapshot-v1", "--closure-driver", str(NATIVE_DRIVER)]
        for key, value in identity.items():
            arguments.extend(["--" + key.replace("_", "-"), value])
        def diagnostic_host(current):
            current["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
            return ""
        prior_cwd = os.getcwd()
        try:
            os.chdir(self.directory)
            self.assertNotEqual(Path.cwd().resolve(), compare.TRUSTED_ROOT)
            with mock.patch.object(compare, "host_problem", side_effect=diagnostic_host):
                status = compare.main(arguments)
        finally:
            os.chdir(prior_cwd)
        self.current = json.loads((self.evidence / "receipt.json").read_bytes())
        self.assertEqual(status, 0, self.current.get("reasons"))
        self.assertEqual(self.current["phase_ownership"]["schema"], owned.POPULATION_SCHEMA)
        rows = self.current["phase_ownership"]["phases"]
        self.assertEqual([row["argv"] for row in rows[:2]], [
            ["git", "-C", str(compare.TRUSTED_ROOT), "rev-parse", "HEAD"],
            ["git", "-C", str(compare.TRUSTED_ROOT), "rev-parse", "HEAD^{tree}"]])
        self.assertTrue(all(row["kind"] == "capture" for row in rows[:2]))
        self.assertEqual(receipt_contract.validate_closure(
            self.current, raw_bundle(self.current, self.evidence), expected_policy="snapshot-v1",
            expected_phase_driver_sha256=digest(NATIVE_DRIVER), expected_trusted_revision=self.trusted_revision,
            require_owned_phases=True, require_owned_preflight=True), [])
        self.assertIsNone(compare.OWNED_PHASE_CONTEXT)

    def test_normal_main_snapshot_missing_mandatory_tool_retains_failed_preflight_without_later_children(self):
        identity = self.identity("main")
        arguments = ["--candidate", str(self.candidate), "--lab", str(Path(__file__).resolve()),
            "--work", str(self.work), "--evidence", str(self.evidence),
            "--summary", str(self.directory / "missing-tool-summary.md"),
            "--closure-policy", "snapshot-v1", "--closure-driver", str(NATIVE_DRIVER)]
        for key, value in identity.items():
            arguments.extend(["--" + key.replace("_", "-"), value])
        def diagnostic_host(current):
            current["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
            return ""
        original_which = compare.shutil.which
        with mock.patch.object(compare, "host_problem", side_effect=diagnostic_host), \
                mock.patch.object(compare.shutil, "which", side_effect=lambda tool: None if tool == "clang" else original_which(tool)), \
                mock.patch.object(compare, "measure") as measurement:
            self.assertEqual(compare.main(arguments), 1)
            measurement.assert_not_called()
        self.current = json.loads((self.evidence / "receipt.json").read_bytes())
        self.assertEqual(self.current["state"], "failed")
        self.assertEqual(self.current["phase_ownership"]["state"], "failed")
        self.assertEqual(self.current["work_retained"], str(self.work))
        rows = self.current["phase_ownership"]["phases"]
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(row["kind"] == "capture" and row["phase"] == "preflight" for row in rows))
        self.assertTrue(any("configured tool clang" in reason for reason in self.current["reasons"]))
        self.assertNotIn("closure", self.current)
        self.assertIsNone(compare.OWNED_PHASE_CONTEXT)
        for row in rows:
            native = owned.read_record((self.evidence / "owned-phases" / row["file"]).read_bytes())
            self.assertTrue(native["cleanup_proven"])
            self.assertEqual(native["exit_status"], 0)

    def test_actual_full_ordinary_measure_writer_then_strict_reader(self):
        arguments, current, context, summaries = self.measurement()
        compare.measure(arguments, self.candidate, self.work, self.evidence, self.bins,
                        self.evidence / "build.log", current, summaries)
        context.finish()
        compare.OWNED_PHASE_CONTEXT = self.prior
        self.assertEqual(current["reasons"], [])
        self.assertEqual(current["state"], "measured")  # Structural state only; diagnostic marker remains mandatory.
        self.assertFalse(context.stopped)
        self.assertEqual(current["phase_ownership"]["state"], "complete")
        current.pop("phase", None)
        self.assertEqual(compare.write_receipt(current, self.evidence), "")
        persisted = json.loads((self.evidence / "receipt.json").read_bytes())
        self.assertEqual(persisted, current)
        rows = current["phase_ownership"]["phases"]
        core = [row for row in rows if row["kind"] == "run"]
        captured = [row for row in rows if row["kind"] == "capture"]
        self.assertEqual(len(core), 12)
        self.assertEqual(len(captured), 2)  # Real diff plus expected missing scaling selector probe.
        self.assertEqual([row["ordinal"] for row in rows], list(range(1, 15)))
        raw = raw_bundle(current, self.evidence)
        self.assertEqual(receipt_contract.validate_closure(persisted, raw, expected_policy="snapshot-v1",
            expected_phase_driver_sha256=context.driver_hash, expected_trusted_revision=self.trusted_revision,
            require_owned_phases=True), [])
        self.assertEqual(plan_contract.validate_plan(current, current["phase_ownership"], core), [])
        self.assertEqual(receipt_contract.classify(summaries[0], current["binaries"]), [])
        self.assertEqual(summaries[0]["verdict"]["outcome"], "slower")
        self.assertEqual(summaries[0]["diagnostic_fixture"], DIAGNOSTIC)
        corpus = json.loads((self.evidence / "throughput/summary.json").read_bytes())
        metadata = json.loads((self.evidence / "throughput/metadata.json").read_bytes())
        self.assertEqual(receipt_contract.classify_throughput(corpus, metadata, current["binaries"]), [])
        self.assertGreater(corpus["confirmed_regressions"], 0)
        self.assertEqual(current["throughput"]["exit"], 1)
        self.assertEqual(receipt_contract.classify_throughput_exit(1, corpus, metadata, current["binaries"]), [])
        throughput_row = next(row for row in core if row["phase"] == "throughput")
        self.assertEqual(throughput_row["exit_policy"], "corpus-report-only-v1")
        self.assertEqual(throughput_row["corpus_summary_sha256"], owned.sha(raw["owned_throughput"]["summary"]))
        self.assertEqual(throughput_row["corpus_metadata_sha256"], owned.sha(raw["owned_throughput"]["metadata"]))
        self.assertEqual(owned.read_record(raw["owned_phases"][throughput_row["file"]]["receipt"])["exit_status"], 256)
        self.assertEqual(owned.read_record(raw["owned_phases"][throughput_row["file"]]["receipt"])["state"], "failed")
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
                            for row in core if row["phase"] != "throughput"))
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

    def rejected_corpus(self, case):
        arguments, current, context, summaries = self.measurement()
        try:
            with mock.patch.dict(os.environ, {"BUSTER_ORDINARY_DIAGNOSTIC_CORPUS_CASE": case}):
                with self.assertRaises(compare.OwnedPhaseFailed) as caught:
                    compare.measure(arguments, self.candidate, self.work, self.evidence, self.bins,
                                    self.evidence / "build.log", current, summaries)
            context.finish()
            current["state"] = "failed"
            current["reasons"].append("private diagnostic corpus rejection: " + str(caught.exception)[:200])
            self.assertTrue(context.stopped)
            self.assertEqual(current["phase_ownership"]["state"], "failed")
            self.assertEqual(current["work_retained"], str(self.work))
            rows = current["phase_ownership"]["phases"]
            self.assertEqual(len(rows), 11)
            self.assertTrue(all(row["kind"] == "run" for row in rows))
            self.assertEqual(rows[-1]["phase"], "throughput")
            self.assertEqual(rows[-1]["exit_policy"], "corpus-report-only-v1")
            self.assertNotIn("verify", current["closure"])
            self.assertFalse((self.evidence / "closure-verify.json").exists())
            path = self.evidence / "owned-phases" / rows[-1]["file"]
            native = owned.read_record(path.read_bytes())
            self.assertEqual(native["exit_status"], 256)
            self.assertEqual(native["state"], "failed")
            self.assertTrue(native["cleanup_proven"])
            for key in ("cleanup_signalled", "cleanup_reaped", "reservation_retained", "ownership_lost",
                        "timed_out", "cancelled", "capture_failed", "output_truncated", "tree_cleanup_failed"):
                self.assertEqual(native[key], 0)
            raw_summary = self.work / "throughput/summary.json"
            raw_metadata = self.work / "throughput/metadata.json"
            if case == "missing":
                self.assertFalse(raw_summary.exists())
                self.assertFalse(raw_metadata.exists())
            else:
                summary = json.loads(raw_summary.read_bytes())
                metadata = json.loads(raw_metadata.read_bytes())
                self.assertEqual(summary["diagnostic_case"], case)
                self.assertEqual(metadata["diagnostic_case"], case)
                if case == "invalid":
                    self.assertIs(summary["valid"], False)
                    self.assertGreater(summary["confirmed_regressions"], 0)
                elif case in ("partial-numeric", "inconsistent-regression"):
                    self.assertIs(summary["valid"], True)
                    self.assertGreater(summary["confirmed_regressions"], 0)
                else:
                    self.assertIs(summary["valid"], True)
                    self.assertEqual(summary["confirmed_regressions"], 0)
                    self.assertEqual(receipt_contract.classify_throughput(summary, metadata, current["binaries"]), [])
                self.assertTrue(receipt_contract.classify_throughput_exit(1, summary, metadata, current["binaries"]))
            marker = self.work / "no-next-child"
            with mock.patch.object(compare.subprocess, "Popen") as spawn:
                with self.assertRaises(compare.OwnedPhaseFailed):
                    context.execute(["/bin/sh", "-c", "touch " + str(marker)], self.work,
                                    self.evidence / "no-next.log", 5)
                spawn.assert_not_called()
            self.assertFalse(marker.exists())
            print("COMPILER_ORDINARY_CORPUS_REJECTION case=" + case +
                  " raw_exit=256 cleanup_proven=1 no_next_phase=1 qualified=0 activation=0")
        finally:
            context.finish()
            compare.OWNED_PHASE_CONTEXT = self.prior
            current["diagnostic_fixture"] = copy.deepcopy(DIAGNOSTIC)
            compare.write_receipt(current, self.evidence)

    def test_full_ordinary_invalid_corpus_exit1_stops_before_next_phase(self):
        self.rejected_corpus("invalid")

    def test_full_ordinary_missing_corpus_exit1_stops_before_next_phase(self):
        self.rejected_corpus("missing")

    def test_full_ordinary_partial_numeric_exit1_stops_before_next_phase(self):
        self.rejected_corpus("partial-numeric")

    def test_full_ordinary_inconsistent_regression_exit1_stops_before_next_phase(self):
        self.rejected_corpus("inconsistent-regression")

    def test_full_ordinary_unexplained_corpus_exit1_stops_before_next_phase(self):
        self.rejected_corpus("bad-exit")

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
        write(self.evidence / "native-request-refusal.json", dict(DIAGNOSTIC,
            state="complete", returncode=result.returncode, stdout=result.stdout, stderr=result.stderr,
            candidate=str(candidate), output=str(output), candidate_exists=False, output_exists=False))


def main(argv=None):
    global NATIVE_DRIVER, EXPORT_ROOT
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments[:1] == ["compare"]:
        return emit(arguments, corpus=False)
    if arguments[:1] == ["--emit-corpus"]:
        return emit(arguments[1:], corpus=True)
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--native-driver", type=Path)
    parser.add_argument("--export", type=Path)
    options = parser.parse_args(arguments)
    guard()
    NATIVE_DRIVER = options.native_driver.resolve(strict=True) if options.native_driver else None
    if options.export:
        if NATIVE_DRIVER is None or not sys.platform.startswith("linux"):
            raise ValueError("diagnostic export requires an actual hosted native-driver run")
        EXPORT_ROOT = prepare_export(options.export)
    if NATIVE_DRIVER is not None and sys.platform.startswith("linux"):
        # The class decorator runs before argv parsing.
        ActualOrdinaryMeasure.__unittest_skip__ = False
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if EXPORT_ROOT is not None:
        expected_cases = set(unittest.defaultTestLoader.getTestCaseNames(ActualOrdinaryMeasure))
        complete = result.wasSuccessful() and len(EXPORTED_CASES) == len(expected_cases) and \
            {case["path"] for case in EXPORTED_CASES} == expected_cases and \
            all(case["state"] == "passed" for case in EXPORTED_CASES)
        write(EXPORT_ROOT / "index.json", dict(DIAGNOSTIC, state="complete" if complete else "failed",
              cases=EXPORTED_CASES, tests_run=result.testsRun, failures=len(result.failures), errors=len(result.errors)))
        if not complete:
            return 1
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
