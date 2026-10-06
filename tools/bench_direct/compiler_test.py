#!/usr/bin/env python3
"""Offline checks of the 9700X compiler comparison receipt, harness and publisher (#2752)."""

from __future__ import annotations

import copy
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
import warnings
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compiler_compare  # noqa: E402
import compiler_publish  # noqa: E402
import compiler_receipt  # noqa: E402

A256, B256 = "1" * 64, "2" * 64


def summary(outcome: str = "slower") -> dict:
    return {
        "schema": compiler_receipt.LAB_SCHEMA,
        "baseline": {"sha256": A256, "runs": 12, "failed": 0, "deterministic": True},
        "candidate": {"sha256": B256, "runs": 12, "failed": 0, "deterministic": True},
        "plan": {"complete_pairs": 12},
        "verdict": {"metric": "wall", "outcome": outcome, "ratio": 1.02, "ci_low": 1.01, "ci_high": 1.03,
                    "text": "Candidate is SLOWER."},
        "metrics": {"wall": {"a_median": 1.0, "b_median": 1.02, "ratio": 1.02, "ci_low": 1.01, "ci_high": 1.03,
                             "outcome": outcome}},
        "warnings": [],
    }


BINARIES = {"baseline": {"sha256": A256}, "candidate": {"sha256": B256}}
MODES_ALL = ("none", "mir-stack", "fast", "quality")


# One test per gate metric and round, as tools/throughput writes them.
TESTS = [{"metric": metric, "round": number, "median_ratio": 1.0, "regression": False}
         for metric in ("wall_seconds", "peak_rss_bytes") for number in range(2)]


def corpus(decision: str = "no substantial regression detected", **summary_change) -> dict:
    """A complete throughput-corpus-v1 run on BINARIES, as {summary, metadata}."""
    profile = compiler_receipt.THROUGHPUT_PROFILE
    cases = [{"name": f"{name}/{mode}", "medians": {}, "tests": copy.deepcopy(TESTS), "decision": decision}
             for name in profile["workloads"] for mode in MODES_ALL]
    regressions = len(cases) if decision == "regression" else 0
    result = {"schema": 2, "guard_enabled": True, "comparisons": cases, "confirmed_regressions": regressions,
              "inconclusive_cases": 0, "valid": True}
    result.update(summary_change)
    metadata = {"schema": 2, "profile": "ci", "pairs_per_round": profile["pairs_per_round"],
                "rounds": profile["rounds"], "warmups": profile["warmups"], "cpu": profile["cpu"],
                "workloads": list(profile["workloads"]),
                "compiler_provenance": [{"sha256": A256}, {"sha256": B256}]}
    return {"summary": result, "metadata": metadata}
def scaling(status: str = "valid", compiler: str = B256) -> dict:
    """A complete scaling-v1 run on the candidate of BINARIES, as {series: {summary, metadata}}."""
    point = {"workers": 2, "placement": "core", "observed_workers": 2, "wall_median": 0.5, "speedup": 1.6,
             "speedup_interval": [1.4, 1.8], "efficiency": 0.8, "cpu_inflation": 1.2, "rss_inflation": 1.3}
    return {name: {"summary": {"schema": compiler_receipt.SCALING_SCHEMA, "status": status, "reason": "",
                               "series": [{"name": "equal", "inputs": 28, "points": [point]},
                                          {"name": "diagnostic", "diagnostics_identical": True, "exit_code": 1}]},
                   "metadata": {"schema": compiler_receipt.SCALING_SCHEMA, "compiler_sha256": compiler,
                                "cpu_set": "1-7,9-15", "excluded_cpus": "0,8", "physical_cores": 7,
                                "logical_cpus": 14}}
            for name in compiler_receipt.SCALING_PROFILE["series"]}


HOST = {"hostname": "benchpress", "cpu_model": "AMD Ryzen 7 9700X 8-Core Processor"}
EXPECTED = {"mode": "main", "repository": "buster14a/buster", "ref": "refs/heads/main",
            "pull": "7", "pull_head": "c" * 40, "base": "b" * 40, "base_tree": "e" * 40, "head": "a" * 40,
            "head_tree": "d" * 40, "trusted_revision": "9" * 40, "request_run_id": "91", "run_id": "92",
            "run_attempt": "1"}


def receipt(state: str = "measured") -> dict:
    return {"schema": compiler_receipt.RECEIPT_SCHEMA, "mode": "main", "state": state, "reasons": [],
            "host": dict(HOST), "identity": dict(EXPECTED), "profile": copy.deepcopy(compiler_receipt.PROFILE),
            "throughput_profile": copy.deepcopy(compiler_receipt.THROUGHPUT_PROFILE),
            "binaries": copy.deepcopy(BINARIES), "timings": {"build_seconds": {"baseline": 60.0}}}


class ReceiptTest(unittest.TestCase):
    def test_every_complete_direction_is_a_valid_measurement(self) -> None:
        for outcome in compiler_receipt.MEASURED_OUTCOMES:
            with self.subTest(outcome=outcome):
                self.assertEqual(compiler_receipt.classify(summary(outcome), BINARIES), [])

    def test_incomplete_or_mismatched_core_evidence_is_invalid(self) -> None:
        def change(path: tuple, value: object) -> dict:
            data = summary()
            target = data
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            return data
        cases = {
            "schema": change(("schema",), "buster-uarch-lab-compare-v1"),
            "hash": change(("candidate", "sha256"), A256),
            "failed": change(("baseline", "failed"), 1),
            "nondeterministic": change(("candidate", "deterministic"), False),
            "pairs": change(("plan", "complete_pairs"), 5),
            "inconclusive": change(("verdict", "outcome"), "inconclusive"),
            "no verdict": change(("verdict", "outcome"), "no complete pair"),
            "metric": change(("verdict", "metric"), "instructions"),
            "ratio": change(("verdict", "ratio"), None),
        }
        for name, data in cases.items():
            with self.subTest(case=name):
                self.assertTrue(compiler_receipt.classify(data, BINARIES))
        for value in (None, [], "summary"):
            self.assertTrue(compiler_receipt.classify(value, BINARIES))
        self.assertTrue(compiler_receipt.classify(summary(), {}))

    def test_wall_ratio_domain_and_interval_are_validated(self) -> None:
        def mutate(**verdict) -> dict:
            data = summary()
            data["verdict"].update(verdict)
            data["metrics"]["wall"].update(verdict)
            return data
        bad = {"nan ratio": mutate(ratio=float("nan")), "inf ci_high": mutate(ci_high=float("inf")),
               "negative ratio": mutate(ratio=-1), "zero ci_low": mutate(ci_low=0), "bool ratio": mutate(ratio=True),
               "reversed": mutate(ci_low=2, ci_high=1)}
        for name, data in bad.items():
            with self.subTest(case=name):
                self.assertTrue(compiler_receipt.classify(data, BINARIES))
        self.assertTrue(any("reversed" in item for item in compiler_receipt.classify(bad["reversed"], BINARIES)))
        self.assertTrue(any("ci_high" in item for item in compiler_receipt.classify(bad["inf ci_high"], BINARIES)))
        missing = summary()
        del missing["verdict"]["ci_low"]
        self.assertTrue(compiler_receipt.classify(missing, BINARIES))
        # The verdict and the wall metric record must agree.
        data = summary()
        data["metrics"]["wall"]["ratio"] = 0.5
        self.assertTrue(any("contradicts" in item for item in compiler_receipt.classify(data, BINARIES)))
        data = summary()
        data["metrics"]["wall"]["outcome"] = "faster"
        self.assertTrue(compiler_receipt.classify(data, BINARIES))
        data = summary()
        del data["metrics"]
        self.assertTrue(compiler_receipt.classify(data, BINARIES))
        # A genuine faster result stays valid.
        self.assertEqual(compiler_receipt.classify(mutate(ratio=0.9, ci_low=0.88, ci_high=0.92, outcome="faster"),
                                                   BINARIES), [])

    def test_regression_policy_defaults_to_report_only_and_enforce_fails_closed(self) -> None:
        self.assertEqual(compiler_receipt.regression_policy(""), ("report-only", ""))
        self.assertEqual(compiler_receipt.regression_policy(" report-only "), ("report-only", ""))
        for value in ("enforce", "off", "Report-only"):
            self.assertTrue(compiler_receipt.regression_policy(value)[1])

    def test_host_must_be_the_observed_9700x(self) -> None:
        self.assertEqual(compiler_receipt.host_problem(receipt()), "")
        for model in ("AMD EPYC 9654 96-Core Processor", "AMD Ryzen 9 7950X", "", None, "znver5"):
            with self.subTest(model=model):
                self.assertTrue(compiler_receipt.host_problem(dict(receipt(), host={"cpu_model": model})))
        self.assertTrue(compiler_receipt.host_problem({}))

    def test_marker_binds_exact_head(self) -> None:
        self.assertEqual(compiler_receipt.check_marker("a" * 40), "buster-9700x-compiler-main-v1:" + "a" * 40)
        self.assertEqual(compiler_receipt.check_marker("a" * 40, "pull"), "buster-9700x-compiler-pr-v1:" + "a" * 40)
        self.assertNotEqual(compiler_receipt.check_name("pull"), compiler_receipt.check_name("main"))
        with self.assertRaises(ValueError):
            compiler_receipt.check_marker("main")


class DecideTest(unittest.TestCase):
    def decide(self, **change) -> tuple[str, str, list[str]]:
        values = {"expected": dict(EXPECTED), "authorized": True, "compare_result": "success",
                  "receipt": receipt(), "summary": summary(), "policy_value": "", "throughput": corpus()}
        values.update(change)
        return compiler_publish.decide(**values)

    def test_slow_valid_measurement_succeeds_report_only(self) -> None:
        conclusion, title, reasons = self.decide()
        self.assertEqual((conclusion, reasons), ("success", []))
        self.assertIn("report-only", title)
        self.assertIn("slower", title)

    def test_missing_or_invalid_evidence_is_never_success(self) -> None:
        mismatched = receipt()
        mismatched["identity"]["base"] = "8" * 40
        profile = receipt()
        profile["profile"] = dict(profile["profile"], target_minutes=1)
        other_host = dict(receipt(), host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})
        other_mode = dict(receipt(), mode="pull")
        cases = {
            "unauthorized": {"authorized": False},
            "not the Zen 5 host": {"receipt": other_host},
            "other mode": {"receipt": other_mode},
            "no receipt": {"receipt": None},
            "wrong schema": {"receipt": dict(receipt(), schema="other")},
            "mismatched identity": {"receipt": mismatched},
            "changed profile": {"receipt": profile},
            "failed host": {"receipt": receipt("failed")},
            "invalid samples": {"summary": dict(summary(), plan={"complete_pairs": 2})},
            "compare cancelled": {"compare_result": "cancelled"},
            "enforcement requested": {"policy_value": "enforce"},
        }
        for name, change in cases.items():
            with self.subTest(case=name):
                conclusion, _, reasons = self.decide(**change)
                self.assertEqual(conclusion, "failure")
                self.assertTrue(reasons)

    def test_corpus_regressions_are_reported_not_decided(self) -> None:
        conclusion, _, reasons = self.decide(throughput=corpus("regression"))
        self.assertEqual((conclusion, reasons), ("success", []))

    def test_corpus_needs_the_exact_workload_mode_population(self) -> None:
        def mutated(change) -> dict:
            data = corpus()
            change(data["summary"])
            return data
        def rows(summary: dict) -> list:
            return summary["comparisons"]
        def set_counts(summary: dict, regressions: int, inconclusive: int) -> None:
            summary["confirmed_regressions"], summary["inconclusive_cases"] = regressions, inconclusive
        cases = {
            "only none rows": (lambda s: s.update(comparisons=[r for r in rows(s) if r["name"].endswith("/none")]),
                               "miss 18 of 24"),
            "bare workload names": (lambda s: s.update(comparisons=[{"name": r["name"].split("/")[0]} for r in rows(s)[::4]]),
                                    "miss"),
            "one missing cell": (lambda s: rows(s).pop(), "miss 1 of 24"),
            "one allocator missing": (lambda s: s.update(comparisons=[r for r in rows(s) if not r["name"].endswith("/fast")]),
                                      "miss 6 of 24"),
            "duplicated": (lambda s: s.update(comparisons=rows(s) + copy.deepcopy(rows(s))), "repeat"),
            "wrong mode": (lambda s: rows(s)[0].update(name="tiny_startup/turbo"), "outside the profile"),
            "foreign workload": (lambda s: rows(s).append(dict(copy.deepcopy(rows(s)[0]), name="other/none")),
                                 "outside the profile"),
            "negative count": (lambda s: set_counts(s, -1, 0), "confirmed_regressions"),
            "inflated count": (lambda s: set_counts(s, 0, 9999), "inconclusive_cases"),
            "count without a case": (lambda s: set_counts(s, 1, 0), "confirmed_regressions"),
            "unknown decision": (lambda s: rows(s)[3].update(decision="fine"), "decision"),
            "diagnostic decision": (lambda s: rows(s)[3].update(decision="diagnostic (guard disabled)"), "decision"),
            "malformed row": (lambda s: rows(s).__setitem__(2, "row"), "not an object"),
            "no tests": (lambda s: rows(s)[1].update(tests=[]), "one test per"),
            "missing medians": (lambda s: rows(s)[1].pop("medians"), "medians"),
            "no comparisons": (lambda s: s.pop("comparisons"), "comparisons"),
        }
        for name, (change, text) in cases.items():
            with self.subTest(case=name):
                data = mutated(change)
                reasons = compiler_receipt.classify_throughput(data["summary"], data["metadata"], BINARIES)
                self.assertTrue(any(text in item for item in reasons), reasons)
                conclusion, _, _ = self.decide(throughput=data)
                self.assertEqual(conclusion, "failure")
        # Genuine regressions and inconclusive cells stay valid and are counted, not discarded.
        mixed = corpus()
        rows(mixed["summary"])[0]["decision"] = "regression"
        rows(mixed["summary"])[1]["decision"] = "inconclusive"
        set_counts(mixed["summary"], 1, 1)
        self.assertEqual(self.decide(throughput=mixed)[0], "success")

    def test_incomplete_or_unbound_corpus_is_never_success(self) -> None:
        other_binary = corpus()
        other_binary["metadata"]["compiler_provenance"][1]["sha256"] = "3" * 64
        fewer_pairs = corpus()
        fewer_pairs["metadata"]["pairs_per_round"] = 5
        partial = corpus()
        partial["summary"]["comparisons"] = partial["summary"]["comparisons"][:4]
        no_profile = receipt()
        del no_profile["throughput_profile"]
        changed_profile = receipt()
        changed_profile["throughput_profile"] = dict(changed_profile["throughput_profile"], pairs_per_round=5)
        cases = {
            "no corpus evidence": {"throughput": None},
            "no metadata": {"throughput": {"summary": corpus()["summary"]}},
            "invalid comparison": {"throughput": corpus(valid=False)},
            "guard disabled": {"throughput": corpus(guard_enabled=False)},
            "other schema": {"throughput": corpus(schema=1)},
            "another compiler": {"throughput": other_binary},
            "fewer pairs than the profile": {"throughput": fewer_pairs},
            "missing workloads": {"throughput": partial},
            "receipt without the corpus profile": {"receipt": no_profile},
            "changed corpus profile": {"receipt": changed_profile},
        }
        for name, change in cases.items():
            with self.subTest(case=name):
                conclusion, _, reasons = self.decide(**change)
                self.assertEqual(conclusion, "failure")
                self.assertTrue(reasons)

    def test_requested_scaling_is_validated_and_reported_not_decided(self) -> None:
        asked = receipt()
        asked["scaling_profile"] = copy.deepcopy(compiler_receipt.SCALING_PROFILE)
        self.assertEqual(self.decide(receipt=asked, throughput=dict(corpus(), scaling=scaling())),
                         ("success", self.decide()[1], []))
        changed = copy.deepcopy(asked)
        changed["scaling_profile"]["series"]["cores"] = ["--workers", "1"]
        cases = {
            "no bundles": {"receipt": asked, "throughput": corpus()},
            "invalid bundle": {"receipt": asked, "throughput": dict(corpus(), scaling=scaling("invalid"))},
            "another compiler": {"receipt": asked, "throughput": dict(corpus(), scaling=scaling(compiler=A256))},
            "missing series": {"receipt": asked, "throughput": dict(corpus(), scaling={"cores": scaling()["cores"]})},
            "changed profile": {"receipt": changed, "throughput": dict(corpus(), scaling=scaling())},
        }
        for label, change in cases.items():
            with self.subTest(label=label):
                conclusion, _, reasons = self.decide(**change)
                self.assertEqual(conclusion, "failure")
                self.assertIn("scaling", " ".join(reasons))
        # A comparison that did not ask for scaling ignores any bundle.
        self.assertEqual(self.decide(throughput=dict(corpus(), scaling=scaling("invalid")))[0], "success")
        report = compiler_receipt.render(dict(asked, scaling=compiler_receipt.scaling_digest(scaling())), summary(),
                                         "success", [])
        self.assertIn("Series `cores`: valid on CPU set `1-7,9-15` (7 cores, 14 logical CPUs; housekeeping `0,8` "
                      "excluded)", report)
        self.assertIn("| equal (28) | 2 | core | 2 | 0.5000 | 1.600 | [1.400, 1.800] | 0.800 | 1.200 | 1.300 |", report)

    def test_only_recovery_of_a_legacy_receipt_skips_the_corpus(self) -> None:
        legacy = receipt()
        del legacy["throughput_profile"]
        self.assertEqual(self.decide(receipt=legacy, throughput=None, require_throughput=False)[0], "success")
        # A receipt that names the profile is always checked.
        self.assertEqual(self.decide(throughput=None, require_throughput=False)[0], "failure")

    def test_superseded_group_is_neutral_and_transfers_nothing(self) -> None:
        conclusion, title, _ = self.decide(receipt=receipt("superseded"), summary=None)
        self.assertEqual((conclusion, title), ("neutral", "Superseded before measurement"))


class FakeApi:
    def __init__(self, archive: bytes, rows: list | None = None):
        self.archive = archive
        self.rows = rows

    def request(self, path: str, data: dict | None = None) -> object:
        return {"artifacts": self.rows if self.rows is not None else [
            {"name": "buster-9700x-compiler-x-1", "expired": False, "size_in_bytes": len(self.archive),
             "archive_download_url": "https://api.invalid/zip"}]}

    def download(self, url: str) -> bytes:
        return self.archive


def archive(members: dict) -> bytes:
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w") as output:
        for name, value in members.items():
            output.writestr(name, value)
    return stream.getvalue()


class EvidenceTest(unittest.TestCase):
    def test_reads_receipt_and_summary_as_data(self) -> None:
        payload = archive({"receipt.json": json.dumps(receipt()), "lab/summary.json": json.dumps(summary()),
                           "throughput/summary.json": json.dumps(corpus()["summary"]),
                           "throughput/metadata.json": json.dumps(corpus()["metadata"])})
        got = compiler_publish.read_evidence(FakeApi(payload), "92", "buster-9700x-compiler-x-1")
        self.assertEqual(got[:3], (receipt(), summary(), ""))
        self.assertEqual(got[3]["name"], "buster-9700x-compiler-x-1")
        unscaled = {name: {"summary": None, "metadata": None} for name in compiler_receipt.SCALING_PROFILE["series"]}
        self.assertEqual(got[4], dict(corpus(), scaling=unscaled))
        bundles = scaling()
        members = {f"scaling/{name}/{leaf}": json.dumps(bundles[name][key]) for name in bundles
                   for leaf, key in (("scaling.json", "summary"), ("scaling-metadata.json", "metadata"))}
        payload = archive({"receipt.json": json.dumps(receipt()), **members})
        self.assertEqual(compiler_publish.read_evidence(FakeApi(payload), "92", "buster-9700x-compiler-x-1")[4]["scaling"],
                         bundles)

    def test_missing_ambiguous_or_malformed_evidence(self) -> None:
        payload = archive({"receipt.json": "{not json"})
        self.assertEqual(compiler_publish.read_evidence(FakeApi(payload), "92", "buster-9700x-compiler-x-1")[:2],
                         (None, None))
        for rows in ([], [{"name": "buster-9700x-compiler-x-1"}] * 2,
                     [{"name": "buster-9700x-compiler-x-1", "expired": True, "size_in_bytes": 1,
                       "archive_download_url": "u"}]):
            with self.subTest(rows=rows):
                self.assertTrue(compiler_publish.read_evidence(FakeApi(b"", rows), "92",
                                                               "buster-9700x-compiler-x-1")[2])

    def evidence(self, members: dict) -> tuple:
        return compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")

    def unique_members(self) -> dict:
        return {"receipt.json": json.dumps(receipt()), "lab/summary.json": json.dumps(summary()),
                "throughput/summary.json": json.dumps(corpus()["summary"]),
                "throughput/metadata.json": json.dumps(corpus()["metadata"])}

    def test_duplicate_members_and_aliases_are_rejected(self) -> None:
        base = self.unique_members()
        for alias in ("receipt.json", "./receipt.json", "receipt.json/", "\\receipt.json", "/receipt.json",
                      ".//receipt.json"):
            for value in (base["receipt.json"], "{}"):
                with self.subTest(alias=alias, identical=value == base["receipt.json"]):
                    stream = io.BytesIO()
                    with zipfile.ZipFile(stream, "w") as output:
                        with warnings.catch_warnings():
                            warnings.simplefilter("ignore")
                            for name, text in base.items():
                                output.writestr(name, text)
                            output.writestr(alias, value)
                    got = compiler_publish.read_evidence(FakeApi(stream.getvalue()), "92", "buster-9700x-compiler-x-1")
                    self.assertIn("duplicate member", got[2])
                    self.assertIsNone(got[0])

    def test_duplicate_json_keys_are_rejected_at_every_level(self) -> None:
        for member, text in (("receipt.json", '{"a": 1, "a": 1}'), ("receipt.json", '{"a": 1, "a": 2}'),
                             ("lab/summary.json", '{"x": {"y": [{"k": 1, "k": 2}]}}'),
                             ("throughput/summary.json", '{"x": {"y": 1, "y": 1}}'),
                             ("throughput/metadata.json", '{"n": {"m": {"q": 1, "q": 1}}}')):
            with self.subTest(member=member, text=text):
                members = self.unique_members()
                members[member] = text
                got = self.evidence(members)
                self.assertIn("duplicate JSON key", got[2])
                self.assertIn(member, got[2])

    def test_unique_archive_still_validates(self) -> None:
        got = self.evidence(self.unique_members())
        self.assertEqual(got[:3], (receipt(), summary(), ""))


FAKE_BUILD = """#!/usr/bin/env bash
set -euo pipefail
if [[ $1 == bench_throughput && $2 == scale ]]; then
    exec python3 scaling_fake.py "$@"
elif [[ $1 == bench_throughput ]]; then
    exec python3 throughput_fake.py "$@"
elif [[ $1 == generate ]]; then
    mkdir -p build/Release
    printf 'BUSTER_INCLUDE_TESTS:BOOL=OFF\\n' > build/CMakeCache.txt
else
    cp compiler.txt build/Release/ide
fi
"""
FAKE_LAB = """import hashlib, json, sys
argv = sys.argv
value = lambda flag: argv[argv.index(flag) + 1]
digest = lambda path: hashlib.sha256(open(path, "rb").read()).hexdigest()
variant = lambda path: {"sha256": digest(path), "runs": 12, "failed": 0, "deterministic": True}
import os
os.makedirs(value("--output"))
summary = {"schema": "buster-uarch-lab-compare-v2", "baseline": variant(value("--baseline")),
           "candidate": variant(value("--candidate")), "plan": {"complete_pairs": 12},
           "verdict": {"metric": "wall", "outcome": "no detectable difference", "ratio": 1.0, "ci_low": 0.99,
                       "ci_high": 1.01, "text": "NO DETECTABLE DIFFERENCE"},
           "metrics": {"wall": {"outcome": "no detectable difference", "ratio": 1.0, "ci_low": 0.99, "ci_high": 1.01}},
           "warnings": []}
open(os.path.join(value("--output"), "summary.json"), "w").write(json.dumps(summary))
open(os.path.join(value("--output"), "reference.exe"), "w").write("excluded")
"""


# Stands in for `./build.sh bench_throughput run`; FAKE_THROUGHPUT=fail|partial
# makes it exit nonzero or cover only one workload.
FAKE_THROUGHPUT = """import hashlib, json, os, sys
argv = sys.argv
value = lambda flag: argv[argv.index(flag) + 1]
digest = lambda path: hashlib.sha256(open(path, "rb").read()).hexdigest()
behavior = os.environ.get("FAKE_THROUGHPUT", "")
workloads = ["tiny_startup", "large_function", "many_functions", "symbol_table", "control_flow", "backend_pressure"]
output = value("--output")
os.makedirs(output)
covered = workloads[:1] if behavior == "partial" else workloads
tests = [{"metric": metric, "round": number} for metric in ("wall_seconds", "peak_rss_bytes") for number in range(2)]
cases = [{"name": name + "/" + mode, "medians": {}, "tests": tests, "decision": "no substantial regression detected"}
         for name in covered for mode in ("none", "mir-stack", "fast", "quality")]
summary = {"schema": 2, "guard_enabled": True, "comparisons": cases, "confirmed_regressions": 0,
           "inconclusive_cases": 0, "valid": True}
metadata = {"schema": 2, "profile": value("--profile"), "pairs_per_round": int(value("--pairs")), "rounds": 2,
            "warmups": int(value("--warmups")), "cpu": int(value("--cpu")), "workloads": workloads,
            "compiler_provenance": [{"sha256": digest(value("--baseline"))}, {"sha256": digest(value("--candidate"))}]}
open(os.path.join(output, "summary.json"), "w").write(json.dumps(summary))
open(os.path.join(output, "metadata.json"), "w").write(json.dumps(metadata))
sys.exit(3 if behavior == "fail" else 0)
"""
# Stands in for `./build.sh bench_throughput scale`; FAKE_SCALING=fail makes
# the bundle invalid and the command exit 2.
FAKE_SCALING = """import hashlib, json, os, sys
argv = sys.argv
value = lambda flag: argv[argv.index(flag) + 1]
digest = lambda path: hashlib.sha256(open(path, "rb").read()).hexdigest()
failed = os.environ.get("FAKE_SCALING", "") == "fail"
output = value("--output")
os.makedirs(os.path.join(output, "inputs", "equal"))
open(os.path.join(output, "inputs", "equal", "tu0000.c"), "w").write("int main(void) { return 0; }\\n")
open(os.path.join(output, "samples.metrics"), "w").write("excluded")
point = {"workers": 2, "placement": "core", "observed_workers": 2, "speedup": 1.5}
summary = {"schema": "buster-throughput-scaling-v1", "status": "invalid" if failed else "valid", "reason": "",
           "series": [] if failed else [{"name": "equal", "inputs": 28, "points": [point]}]}
metadata = {"schema": "buster-throughput-scaling-v1", "compiler_sha256": digest(value("--compiler")),
            "cpu_set": value("--cpu-set"), "arguments": argv[2:]}
open(os.path.join(output, "scaling.json"), "w").write(json.dumps(summary))
open(os.path.join(output, "scaling-metadata.json"), "w").write(json.dumps(metadata))
sys.exit(2 if failed else 0)
"""


def retained(evidence: Path) -> dict:
    """The corpus and scaling documents as the publisher reads them from the artifact."""
    load = lambda path: json.loads(path.read_text()) if path.is_file() else None  # noqa: E731
    result = {name.split(".")[0]: json.loads((evidence / "throughput" / name).read_text())
              for name in ("summary.json", "metadata.json")}
    result["scaling"] = {name: {"summary": load(evidence / "scaling" / name / "scaling.json"),
                                "metadata": load(evidence / "scaling" / name / "scaling-metadata.json")}
                         for name in compiler_receipt.SCALING_PROFILE["series"]}
    return result


class ScratchPlanTest(unittest.TestCase):
    """Path-plan validation: rejected plans must leave every sentinel untouched."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name).resolve()
        self.candidate = self.root / "candidate"
        self.candidate.mkdir()
        (self.candidate / "sentinel").write_text("keep")
        self.lab = self.root / "lab" / "uarch_lab.py"
        self.lab.parent.mkdir()
        self.lab.write_text("lab")

    def tearDown(self) -> None:
        self.directory.cleanup()

    def snapshot(self) -> list:
        return sorted((str(path.relative_to(self.root)), path.read_text() if path.is_file() else None)
                      for path in self.root.rglob("*") if not path.is_symlink())

    def reject(self, work: Path, evidence: Path, text: str, candidate: Path | None = None) -> None:
        before = self.snapshot()
        with self.assertRaises(compiler_compare.ScratchError) as caught:
            compiler_compare.plan_scratch(candidate or self.candidate, self.lab, work, evidence)
        self.assertIn(text, str(caught.exception))
        self.assertEqual(self.snapshot(), before)

    def test_fresh_paths_are_planned_and_prepared_with_a_marker(self) -> None:
        work, evidence = compiler_compare.plan_scratch(self.candidate, self.lab, self.root / "s" / "work",
                                                       self.root / "s" / "evidence")
        for directory in (work, evidence):
            compiler_compare.prepare_scratch(directory)
            self.assertTrue(compiler_compare.owned_scratch(directory))

    def test_retry_clears_a_marked_directory_and_adopts_an_empty_one(self) -> None:
        work, evidence = self.root / "work", self.root / "evidence"
        compiler_compare.prepare_scratch(work)
        (work / "stale").write_text("old")
        evidence.mkdir()
        compiler_compare.plan_scratch(self.candidate, self.lab, work, evidence)
        compiler_compare.prepare_scratch(work)
        compiler_compare.prepare_scratch(evidence)
        self.assertEqual(sorted(path.name for path in work.iterdir()), [compiler_compare.SCRATCH_MARKER])
        self.assertTrue(compiler_compare.owned_scratch(evidence))

    def test_equal_and_nested_paths_are_rejected(self) -> None:
        work = self.root / "work"
        work.mkdir()
        (work / "sentinel").write_text("keep")
        self.reject(work, work, "equal or nested")
        self.reject(work, work / "inner", "equal or nested")
        self.reject(work / "inner", work, "equal or nested")

    def test_a_string_prefix_sibling_is_not_nesting(self) -> None:
        compiler_compare.plan_scratch(self.candidate, self.lab, self.root / "work", self.root / "work2")

    def test_symlink_alias_is_rejected(self) -> None:
        work = self.root / "work"
        work.mkdir()
        (work / "sentinel").write_text("keep")
        (self.root / "alias").symlink_to(work, target_is_directory=True)
        self.reject(work, self.root / "alias", "equal or nested")
        (self.root / "tocandidate").symlink_to(self.candidate, target_is_directory=True)
        self.reject(self.root / "tocandidate", self.root / "evidence", "candidate checkout")

    def test_unrelated_existing_content_is_refused_without_cleanup(self) -> None:
        work = self.root / "work"
        work.mkdir()
        (work / "sentinel").write_text("keep")
        self.reject(work, self.root / "evidence", "unrelated content")
        self.reject(self.root / "evidence2", work, "unrelated content")

    def test_overlap_with_candidate_and_trusted_inputs_is_rejected(self) -> None:
        self.reject(self.candidate, self.root / "evidence", "candidate checkout")
        self.reject(self.candidate / "build", self.root / "evidence", "candidate checkout")
        self.reject(self.root, self.root.parent / "elsewhere", "overlaps")
        self.reject(self.root / "work", self.lab, "trusted lab")
        self.reject(self.root / "work", compiler_compare.TRUSTED_ROOT / "tools", "trusted repository")

    def test_root_and_home_are_rejected(self) -> None:
        self.reject(Path("/"), self.root / "evidence", "filesystem root")
        with mock.patch.object(Path, "home", return_value=self.root / "home"):
            self.reject(self.root, self.root.parent / "elsewhere", "home directory", candidate=self.root.parent / "c")

    def test_main_refuses_before_any_mutation(self) -> None:
        work = self.root / "work"
        work.mkdir()
        (work / "sentinel").write_text("keep")
        before = self.snapshot()
        argv = ["--mode", "main", "--candidate", str(self.candidate), "--lab", str(self.lab), "--work", str(work),
                "--evidence", str(self.root / "evidence"), "--summary", str(self.root / "s.md")]
        for key in compiler_receipt.IDENTITY_KEYS[1:]:
            argv += ["--" + key.replace("_", "-"), "x"]
        with mock.patch("sys.stderr", new_callable=io.StringIO):
            self.assertEqual(compiler_compare.main(argv), 2)
        self.assertEqual(self.snapshot(), before)


class HarnessTest(unittest.TestCase):
    """The host harness against a real two-parent group with stand-in builds and lab."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name)
        self.repo = root / "candidate"
        git = lambda *arguments: subprocess.run(["git", "-C", str(self.repo), *arguments], check=True,  # noqa: E731
                                                capture_output=True, text=True).stdout.strip()
        self.repo.mkdir()
        git("init", "-q", "-b", "main")
        git("config", "user.name", "fixture")
        git("config", "user.email", "fixture@example.invalid")
        (self.repo / "build.sh").write_text(FAKE_BUILD)
        (self.repo / "build.sh").chmod(0o755)
        (self.repo / "compiler.txt").write_text("base compiler\n")
        (self.repo / "throughput_fake.py").write_text(FAKE_THROUGHPUT)
        (self.repo / "scaling_fake.py").write_text(FAKE_SCALING)
        git("add", ".")
        git("commit", "-qm", "base")
        self.base = git("rev-parse", "HEAD")
        git("checkout", "-qb", "pr")
        (self.repo / "compiler.txt").write_text("candidate compiler\n")
        git("commit", "-qam", "candidate")
        self.pull_head = git("rev-parse", "HEAD")
        git("checkout", "-q", "main")
        git("merge", "-q", "--no-ff", "-m", "group", "pr")
        self.head = git("rev-parse", "HEAD")
        self.trees = git("rev-parse", "HEAD^{tree}"), git("rev-parse", self.base + "^{tree}")
        self.lab = root / "lab.py"
        self.lab.write_text(FAKE_LAB)
        self.root = root

    def tearDown(self) -> None:
        self.directory.cleanup()

    def run_harness(self, live: object, cpu: str = HOST["cpu_model"], corpus_behavior: str = "",
                    scaling_behavior: str = "", **change) -> tuple[int, dict, Path]:
        values = {"mode": "main", "candidate": str(self.repo), "lab": str(self.lab), "work": str(self.root / "work"),
                  "evidence": str(self.root / "evidence"), "summary": str(self.root / "step.md"),
                  "repository": "buster14a/buster", "ref": "refs/heads/main",
                  "pull": "7", "pull-head": self.pull_head, "base": self.base, "base-tree": self.trees[1],
                  "head": self.head, "head-tree": self.trees[0], "trusted-revision": "9" * 40,
                  "request-run-id": "91", "run-id": "92", "run-attempt": "1"}
        values.update(change)
        argv = [item for key, value in values.items() for item in ("--" + key, value)]
        with mock.patch.object(compiler_compare, "queue_head", return_value=live), \
                mock.patch.object(compiler_compare, "cpu_model", return_value=cpu), \
                mock.patch.dict(os.environ, {"PATH": os.environ.get("PATH", ""), "FAKE_THROUGHPUT": corpus_behavior,
                                             "FAKE_SCALING": scaling_behavior}):
            code = compiler_compare.main(argv)
        evidence = self.root / "evidence"
        return code, json.loads((evidence / "receipt.json").read_text()), evidence

    def test_builds_base_then_candidate_and_measures_on_the_base_tree(self) -> None:
        code, result, evidence = self.run_harness(self.head)
        self.assertEqual((code, result["state"], result["reasons"]), (0, "measured", []))
        self.assertEqual(set(result["timings"]["build_seconds"]), {"baseline", "candidate", "closure"})
        self.assertNotEqual(result["binaries"]["baseline"]["sha256"], result["binaries"]["candidate"]["sha256"])
        self.assertEqual(result["binaries"]["baseline"]["revision"], self.base)
        self.assertEqual(result["identity"]["head"], self.head)
        self.assertEqual(result["profile"], compiler_receipt.PROFILE)
        # The frozen workload is the base tree, and compiled outputs are not retained.
        self.assertEqual((self.repo / "compiler.txt").read_text(), "base compiler\n")
        self.assertTrue((evidence / "lab" / "summary.json").is_file())
        self.assertFalse((evidence / "lab" / "reference.exe").exists())
        self.assertEqual(result["coverage"], {"first_parent": self.base, "range": "1"})
        expected = dict(result["identity"], first_parent=self.base, range="1")
        summary = json.loads((evidence / "lab" / "summary.json").read_text())
        self.assertEqual(compiler_publish.decide(expected, True, "success", result, summary, "", retained(evidence))[0], "success")
        # The host's range must equal the authorized one.
        self.assertEqual(compiler_publish.decide(dict(expected, range="2"), True, "success", result, summary, "", retained(evidence))[0],
                         "failure")

    def test_range_baseline_on_the_first_parent_chain(self) -> None:
        # A burst left head unmeasured: the next main commit is compared with base, two first-parent commits back.
        git = lambda *arguments: subprocess.run(["git", "-C", str(self.repo), *arguments], check=True,  # noqa: E731
                                                capture_output=True, text=True).stdout.strip()
        (self.repo / "compiler.txt").write_text("later compiler\n")
        git("commit", "-qam", "later")
        later, later_tree = git("rev-parse", "HEAD"), git("rev-parse", "HEAD^{tree}")
        code, result, _ = self.run_harness(later, head=later, pull="0", **{"pull-head": later, "head-tree": later_tree})
        self.assertEqual((code, result["state"]), (0, "measured"), result["reasons"])
        self.assertEqual(result["coverage"], {"first_parent": self.head, "range": "2"})
        self.assertEqual(result["binaries"]["baseline"]["revision"], self.base)
        # A pull request side commit is an ancestor but not on the first-parent chain.
        pull_tree = git("rev-parse", self.pull_head + "^{tree}")
        git("checkout", "-q", "--detach", later)
        code, result, _ = self.run_harness(later, head=later, pull="0", base=self.pull_head,
                                           **{"pull-head": later, "head-tree": later_tree, "base-tree": pull_tree})
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertIn("not on the first-parent chain", " ".join(result["reasons"]))
        self.assertNotIn("coverage", result)

    def test_pull_mode_compares_head_with_its_merge_base(self) -> None:
        # The pull request head (second parent) against the base it branched from.
        pull_tree = subprocess.run(["git", "-C", str(self.repo), "rev-parse", self.pull_head + "^{tree}"], check=True,
                                   capture_output=True, text=True).stdout.strip()
        subprocess.run(["git", "-C", str(self.repo), "checkout", "-q", "--detach", self.pull_head], check=True)
        code, result, evidence = self.run_harness(self.pull_head, mode="pull", ref="refs/pull/7/head", head=self.pull_head,
                                                  **{"head-tree": pull_tree})
        self.assertEqual((code, result["state"], result["mode"]), (0, "measured", "pull"), result["reasons"])
        expected = dict(result["identity"])
        summary = json.loads((evidence / "lab" / "summary.json").read_text())
        self.assertEqual(compiler_publish.decide(expected, True, "success", result, summary, "", retained(evidence))[0], "success")
        # A main-mode publisher never accepts a pull-mode receipt.
        self.assertEqual(compiler_publish.decide(dict(expected, mode="main"), True, "success", result, summary, "", retained(evidence))[0],
                         "failure")

    def scaling_head(self) -> tuple[str, str]:
        """A pull request head that adds the scaling request on top of the candidate; (commit, tree)."""
        git = lambda *arguments: subprocess.run(["git", "-C", str(self.repo), *arguments], check=True,  # noqa: E731
                                                capture_output=True, text=True).stdout.strip()
        git("checkout", "-q", "--detach", self.pull_head)
        request = self.repo / compiler_receipt.SCALING_REQUEST
        request.parent.mkdir(parents=True)
        request.write_text("# request: scaling\n")
        git("add", compiler_receipt.SCALING_REQUEST)
        git("commit", "-qm", "request scaling")
        return git("rev-parse", "HEAD"), git("rev-parse", "HEAD^{tree}")

    def test_requested_scaling_runs_on_the_candidate_only(self) -> None:
        head, tree = self.scaling_head()
        code, result, evidence = self.run_harness(head, mode="pull", ref="refs/pull/7/head", head=head,
                                                  **{"pull-head": head, "head-tree": tree})
        self.assertEqual((code, result["state"]), (0, "measured"), result["reasons"])
        self.assertEqual(result["scaling_profile"], compiler_receipt.SCALING_PROFILE)
        self.assertIn("scaling_seconds", result["timings"])
        documents = retained(evidence)["scaling"]
        for name, arguments in compiler_receipt.SCALING_PROFILE["series"].items():
            with self.subTest(series=name):
                metadata = documents[name]["metadata"]
                self.assertEqual(metadata["compiler_sha256"], result["binaries"]["candidate"]["sha256"])
                self.assertEqual(metadata["arguments"][1:5], ["--compiler", metadata["arguments"][2], "--output",
                                                              metadata["arguments"][4]])
                self.assertEqual(metadata["arguments"][5:], arguments)
                # Generated inputs and per-sample metrics stay out of the evidence.
                self.assertFalse((evidence / "scaling" / name / "inputs").exists())
                self.assertFalse((evidence / "scaling" / name / "samples.metrics").exists())
        summary = json.loads((evidence / "lab" / "summary.json").read_text())
        expected = dict(result["identity"])
        self.assertEqual(compiler_publish.decide(expected, True, "success", result, summary, "", retained(evidence))[0],
                         "success")

    def test_failed_scaling_fails_the_receipt(self) -> None:
        head, tree = self.scaling_head()
        code, result, _ = self.run_harness(head, mode="pull", ref="refs/pull/7/head", head=head, scaling_behavior="fail",
                                           **{"pull-head": head, "head-tree": tree})
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertIn("bench_throughput scale (cores) exited 2", " ".join(result["reasons"]))

    def test_scaling_needs_the_request_in_this_pull_request(self) -> None:
        # Pull mode without the request, and main mode with it, never run the scaling leg.
        pull_tree = subprocess.run(["git", "-C", str(self.repo), "rev-parse", self.pull_head + "^{tree}"], check=True,
                                   capture_output=True, text=True).stdout.strip()
        subprocess.run(["git", "-C", str(self.repo), "checkout", "-q", "--detach", self.pull_head], check=True)
        code, result, _ = self.run_harness(self.pull_head, mode="pull", ref="refs/pull/7/head", head=self.pull_head,
                                           **{"head-tree": pull_tree})
        self.assertEqual((code, result["state"]), (0, "measured"), result["reasons"])
        self.assertNotIn("scaling_profile", result)
        head, tree = self.scaling_head()
        code, result, _ = self.run_harness(head, head=head, pull="0", base=self.pull_head,
                                           **{"pull-head": head, "head-tree": tree,
                                              "base-tree": pull_tree})
        self.assertEqual((code, result["state"]), (0, "measured"), result["reasons"])
        self.assertNotIn("scaling_profile", result)

    def test_corpus_runs_on_the_measured_binaries(self) -> None:
        code, result, evidence = self.run_harness(self.head)
        self.assertEqual((code, result["state"]), (0, "measured"), result["reasons"])
        self.assertEqual(result["throughput_profile"], compiler_receipt.THROUGHPUT_PROFILE)
        self.assertEqual((result["throughput"]["exit"], len(result["throughput"]["cases"])), (0, 24))
        metadata = retained(evidence)["metadata"]
        self.assertEqual([row["sha256"] for row in metadata["compiler_provenance"]],
                         [result["binaries"][role]["sha256"] for role in ("baseline", "candidate")])
        self.assertIn("throughput_seconds", result["timings"])

    def test_failed_or_partial_corpus_fails_the_receipt(self) -> None:
        for behavior, expected in (("fail", "bench_throughput run exited 3"), ("partial", "workload/mode cells")):
            with self.subTest(behavior=behavior):
                # Each run leaves the frozen base checked out.
                subprocess.run(["git", "-C", str(self.repo), "checkout", "-q", "--detach", self.head], check=True)
                code, result, _ = self.run_harness(self.head, corpus_behavior=behavior)
                self.assertEqual((code, result["state"]), (1, "failed"))
                self.assertIn(expected, " ".join(result["reasons"]))

    def test_other_hardware_is_never_measured_as_zen5(self) -> None:
        code, result, _ = self.run_harness(self.head, cpu="AMD EPYC 9B14")
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertIn("not the approved Zen 5 host", " ".join(result["reasons"]))
        self.assertEqual(result["timings"]["build_seconds"], {})

    def test_main_commit_is_measured_after_main_moves_on(self) -> None:
        code, result, _ = self.run_harness("f" * 40)
        self.assertEqual((code, result["state"]), (0, "measured"), result["reasons"])

    def test_direct_push_is_its_own_pull_head(self) -> None:
        # pull_head's only parent is base: a single-parent commit on main.
        pull_tree = subprocess.run(["git", "-C", str(self.repo), "rev-parse", self.pull_head + "^{tree}"], check=True,
                                   capture_output=True, text=True).stdout.strip()
        subprocess.run(["git", "-C", str(self.repo), "checkout", "-q", "--detach", self.pull_head], check=True)
        code, result, _ = self.run_harness(self.pull_head, head=self.pull_head, pull="0", **{"head-tree": pull_tree})
        self.assertEqual((code, result["state"]), (0, "measured"), result["reasons"])
        self.assertEqual(result["identity"]["pull_head"], self.pull_head)

    def test_moved_pull_request_is_superseded_without_building(self) -> None:
        pull_tree = subprocess.run(["git", "-C", str(self.repo), "rev-parse", self.pull_head + "^{tree}"], check=True,
                                   capture_output=True, text=True).stdout.strip()
        subprocess.run(["git", "-C", str(self.repo), "checkout", "-q", "--detach", self.pull_head], check=True)
        code, result, _ = self.run_harness("", mode="pull", ref="refs/pull/7/head", head=self.pull_head,
                                           **{"head-tree": pull_tree})
        self.assertEqual((code, result["state"]), (0, "superseded"))
        self.assertEqual(result["timings"]["build_seconds"], {})

    def test_identity_mismatch_and_failed_build_are_failures(self) -> None:
        code, result, _ = self.run_harness(self.head, base="8" * 40)
        self.assertEqual((code, result["state"]), (1, "failed"))
        (self.repo / "build.sh").write_text("#!/usr/bin/env bash\nexit 3\n")
        subprocess.run(["git", "-C", str(self.repo), "update-index", "--assume-unchanged", "build.sh"], check=True)
        code, result, _ = self.run_harness(self.head)
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertIn("failed with exit 3", " ".join(result["reasons"]))


if __name__ == "__main__":
    unittest.main()
