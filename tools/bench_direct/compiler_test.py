#!/usr/bin/env python3
"""Offline checks of the 9700X compiler comparison receipt, harness and publisher (#2752)."""

from __future__ import annotations

import argparse
import base64
import copy
import gzip
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import unittest
import warnings
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compiler_compare  # noqa: E402
import compiler_owned_phase  # noqa: E402
import compiler_publish  # noqa: E402
import compiler_receipt  # noqa: E402
import inline_acceptance  # noqa: E402
import ast_pilot_acceptance  # noqa: E402
import constant_reuse_acceptance  # noqa: E402
import process_floor_acceptance  # noqa: E402
import throughput_real_source  # noqa: E402
import selfhost_inline_compare  # noqa: E402

A256, B256 = "1" * 64, "2" * 64


def fixture_git(repository: Path, *arguments: str) -> str:
    """Run Git in a disposable fixture repository and return its stripped stdout (#3361).

    These repositories have no housekeeping work worth detaching. A completed
    commit must leave no automatic gc/maintenance worker writing under .git
    while TemporaryDirectory cleanup strictly removes it.
    """
    return subprocess.run(["git", "-c", "maintenance.auto=false", "-c", "gc.auto=0", "-C", str(repository),
                           *arguments], check=True, capture_output=True, text=True).stdout.strip()


def summary(outcome: str = "slower") -> dict:
    return {
        "schema": compiler_receipt.LAB_SCHEMA,
        "baseline": {"sha256": A256, "runs": 12, "failed": 0, "deterministic": True},
        "candidate": {"sha256": B256, "runs": 12, "failed": 0, "deterministic": True},
        "plan": {"pairs": 12, "complete_pairs": 12, "warmups": 1},
        "verdict": {"metric": "wall", "outcome": outcome, "ratio": 1.02, "ci_low": 1.01, "ci_high": 1.03,
                    "text": "Candidate is SLOWER."},
        "metrics": {"wall": {"a_median": 1.0, "b_median": 1.02, "ratio": 1.02, "ci_low": 1.01, "ci_high": 1.03,
                             "outcome": outcome}},
        "warnings": [],
    }


BINARIES = {"baseline": {"sha256": A256}, "candidate": {"sha256": B256}}
MODES_ALL = ("fast", "quality")


# Full normal producer fields for synthetic, data-only report contracts.
TESTS = [{"metric": metric, "round": number, "median_ratio": 1.0, "ci_low": 0.999, "ci_high": 1.001,
          "baseline_relative_mad": 0.0, "candidate_relative_mad": 0.0, "margin_exceedances": 0,
          "pairs": 20, "p_value": 1.0, "regression": False}
         for metric in ("wall_seconds", "peak_rss_bytes") for number in range(2)]


def corpus(decision: str = "no substantial regression detected", **summary_change) -> dict:
    """A complete synthetic normal throughput-corpus-v2 document on BINARIES."""
    profile = compiler_receipt.THROUGHPUT_PROFILE
    tests = copy.deepcopy(TESTS)
    for test in tests:
        if test["metric"] == "wall_seconds" and (decision == "regression" or decision == "inconclusive" and test["round"] == 0):
            test.update(median_ratio=1.3, ci_low=1.299, ci_high=1.301, regression=True,
                        margin_exceedances=20, p_value=0.000001)
    cases = [{"name": f"{name}/{mode}", "medians": {"wall_seconds": [1.0, 1.3],
              "peak_rss_bytes": [1048576, 1048576]}, "tests": copy.deepcopy(tests), "decision": decision}
             for name in profile["workloads"] for mode in MODES_ALL]
    result = {"schema": 2, "guard_enabled": True, "family_alpha": 0.01, "per_test_alpha": 0.01 / 24,
              "comparisons": cases, "confirmed_regressions": len(cases) if decision == "regression" else 0,
              "inconclusive_cases": len(cases) if decision == "inconclusive" else 0, "valid": True}
    result.update(summary_change)
    jobs = [{"job": index, "name": name, "mode": mode, "artifact": "object", "source": f"/diagnostic/{name}.b",
             "sha256": "7" * 64, "bytes": 32, "physical_lines": 2, "defined_functions": 1}
            for index, (name, mode) in enumerate((name, mode) for name in profile["workloads"] for mode in MODES_ALL)]
    metadata = {"schema": 2, "seed": 20260907, "profile": "ci", "pairs_per_round": profile["pairs_per_round"],
                "rounds": profile["rounds"], "warmups": profile["warmups"], "cpu": profile["cpu"],
                "scale": 1, "input_schema": 1, "cache_policy": "warm-filesystem-new-process", "clock": "monotonic",
                "workloads": list(profile["workloads"]), "jobs": jobs,
                "compiler_provenance": [{"sha256": A256, "bytes": 1}, {"sha256": B256, "bytes": 1}]}
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


def analyzer_receipt(state: str = "measured", request_line: str = compiler_receipt.ANALYZER_REQUEST_LINE) -> dict:
    result = receipt(state)
    result["mode"] = "pull"
    result["identity"].update(mode="pull", ref="refs/pull/7/head")
    profile = compiler_receipt.ANALYZER_PROFILE_BY_LINE[request_line]
    result["profile"] = copy.deepcopy(profile)
    result["analyzer_profile"] = copy.deepcopy(profile)
    result["analyzer_request_line"] = request_line
    result.pop("throughput_profile")
    result.pop("binaries")
    return result


class ReceiptTest(unittest.TestCase):
    def test_report_only_policy_requires_original_complete_normal_fields(self) -> None:
        for decision, status in (("regression", 1), ("inconclusive", 0), ("no substantial regression detected", 0)):
            data = corpus(decision)
            self.assertEqual(compiler_receipt.classify_throughput_exit(status, data["summary"], data["metadata"], BINARIES), [])
        def check(change):
            data = corpus("regression")
            change(data)
            self.assertTrue(compiler_receipt.classify_throughput_exit(1, data["summary"], data["metadata"], BINARIES))
        for change in (
                lambda data: data["summary"]["comparisons"][0].update(medians={}),
                lambda data: data["summary"]["comparisons"][0]["tests"][0].pop("ci_low"),
                lambda data: data["summary"]["comparisons"][0]["tests"][0].update(pairs=19),
                lambda data: data["summary"]["comparisons"][0]["tests"][0].update(p_value=1.01),
                lambda data: data["summary"]["comparisons"][0]["tests"][0].update(p_value=1.0),
                lambda data: data["summary"]["comparisons"][0]["tests"][0].update(baseline_relative_mad=-1),
                lambda data: data["summary"]["comparisons"][0]["tests"][0].update(regression=1),
                lambda data: data["summary"]["comparisons"][0]["tests"][0].update(ci_high=float("inf")),
                lambda data: data["summary"]["comparisons"][0]["tests"][0].update(median_ratio=10 ** 1000),
                lambda data: data["summary"]["comparisons"][0].update(tests=[
                    dict(test, regression=False) for test in data["summary"]["comparisons"][0]["tests"]]),
                lambda data: data["summary"].update(family_alpha=True),
                lambda data: data["metadata"].update(seed=True),
                lambda data: data["metadata"]["compiler_provenance"][0].pop("bytes"),
                lambda data: data["metadata"]["jobs"][0].update(job=False),
                lambda data: data["metadata"]["jobs"][0].pop("sha256")):
            check(change)
        # Historical interpretation remains separate from this new process exit policy.
        data = corpus("regression")
        data["summary"]["comparisons"][0]["medians"] = {}
        self.assertEqual(compiler_receipt.classify_throughput(data["summary"], data["metadata"], BINARIES), [])

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

    def test_sample_accounting_matches_the_declared_plan(self) -> None:
        def change(**fields) -> dict:
            data = summary()
            for path, value in fields.items():
                head, _, leaf = path.partition("__")
                data[head][leaf] = value
            return data
        cases = {
            "one run per side claiming twelve pairs": change(baseline__runs=1, candidate__runs=1),
            "mismatched sides": change(candidate__runs=11),
            "runs beyond the claim": change(baseline__runs=13, candidate__runs=13),
            "truncated claiming completion": change(plan__pairs=20),
            "shortened plan": change(plan__pairs=8),
            "no declared plan": change(plan__pairs=None),
            "plan below the floor": change(plan__pairs=5, plan__complete_pairs=5, baseline__runs=5, candidate__runs=5),
            "fewer complete pairs than planned": change(plan__complete_pairs=10),
            "non-integer runs": change(baseline__runs=12.0),
        }
        for name, data in cases.items():
            with self.subTest(case=name):
                self.assertTrue(compiler_receipt.classify(data, BINARIES))
        data = summary()
        del data["plan"]["pairs"]
        self.assertTrue(compiler_receipt.classify(data, BINARIES))
        # The minimum and larger completed experiments are accepted.
        for count in (6, 12, 40):
            with self.subTest(count=count):
                data = change(plan__pairs=count, plan__complete_pairs=count, baseline__runs=count, candidate__runs=count)
                self.assertEqual(compiler_receipt.classify(data, BINARIES), [])
        # A complete experiment with an inconclusive verdict is distinct from one that did not finish.
        complete = summary("inconclusive")
        reasons = compiler_receipt.classify(complete, BINARIES)
        self.assertEqual(len(reasons), 1)
        self.assertIn("verdict", reasons[0])

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


class AnalyzerReceiptTest(unittest.TestCase):
    @staticmethod
    def record_number(data: bytearray, value: int) -> None:
        data.extend(f"{value}\n".encode("ascii"))

    @classmethod
    def record_string(cls, data: bytearray, value: str | bytes) -> None:
        raw = value if isinstance(value, bytes) else value.encode("utf-8")
        cls.record_number(data, len(raw))
        data.extend(raw)
        data.extend(b"\n")

    @staticmethod
    def plan(results: str, *, reason: str = "proven", proof: bytes = b"opaque proof",
             input_digest: str = "a" * 64, search_fingerprint: str = "b" * 64) -> bytes:
        data = bytearray(b"BUSTER_CLANG_ANALYZE_PLAN_V2\n")
        def number(value: int) -> None:
            data.extend(f"{value}\n".encode("ascii"))
        def string(value: str | bytes) -> None:
            raw = value if isinstance(value, bytes) else value.encode("utf-8")
            number(len(raw))
            data.extend(raw)
            data.extend(b"\n")
        for value in (results, "Release", "/usr/bin/clang"):
            string(value)
        for value in (8, 600, 1, 0, 1, 0, 0):
            number(value)
        string(b"[]")
        for value in (0, 0, 0):
            number(value)
        for value in ("module", "src/unit.c", "/candidate", "/build/unit.o"):
            string(value)
        number(1)
        string("src/unit.c")
        number(1)
        string("-c")
        number(1)
        string(reason)
        string(proof)
        number(1)
        for value in ("include/config.h", input_digest, "mode=100644;size=9"):
            string(value)
        number(1)
        number(1)
        string("/candidate/include")
        number(1)
        string(search_fingerprint)
        return bytes(data)

    @classmethod
    def full_raw_profile(cls, *, context_change: tuple[str, int, bytes] | None = None,
                         log_change: tuple[str, int, bytes] | None = None,
                         results_parent_change: tuple[str, str] | None = None,
                         representative_change: tuple[str, int, int] | None = None,
                         alias_envelope_change: tuple[str, int, str] | None = None,
                         tree_status_change: tuple[str, str] | None = None,
                         tree_reason_change: tuple[str, str] | None = None,
                         checkout_change: tuple[str, bytes] | None = None,
                         clang_change: bool = False,
                         missing_result: tuple[str, int] | None = None,
                         helper_status: str = "pass",
                         request_line: str = compiler_receipt.ANALYZER_REQUEST_LINE,
                         selected_count: int = 182, alias_count: int = 47,
                         fallback_groups: int = 0) -> tuple[dict, dict, list[tuple[int, int]]]:
        """Build a test-only complete raw V1/V2 bundle; it is never host measurement evidence."""
        identity = dict(EXPECTED, mode="pull", ref="refs/pull/7/head")
        dynamic_inventory = request_line == compiler_receipt.ANALYZER_REQUEST_LINE_V2
        compile_commands = b"[]"
        alias_representative: dict[int, int] = {}
        alias_pairs: list[tuple[int, int]] = []
        if dynamic_inventory:
            class_count = alias_count if alias_count else fallback_groups
            if selected_count < class_count * 2 or alias_count > selected_count:
                raise ValueError("synthetic V2 inventory does not fit its requested alias classes")
            for group in range(class_count):
                root_index, alias_index = group * 2, group * 2 + 1
                alias_pairs.append((root_index, alias_index))
                if alias_count:
                    alias_representative[alias_index] = root_index
        else:
            for shard in range(8):
                indexes = [index for index in range(selected_count) if index % 8 == shard]
                pair_count = alias_count // 8 + (1 if shard < alias_count % 8 else 0)
                for pair in range(pair_count):
                    root_index, alias_index = indexes[pair * 2:pair * 2 + 2]
                    alias_representative[alias_index] = root_index
                    alias_pairs.append((root_index, alias_index))
        alias_roots = {root for root, _ in alias_pairs}
        candidate_rows = []
        for index in range(selected_count):
            representative = alias_representative.get(index, index)
            group_member = next((pair for pair in alias_pairs if index in pair), None) if dynamic_inventory else None
            duplicated = representative in alias_roots or group_member is not None
            canonical = index == representative
            has_context = duplicated and canonical
            if dynamic_inventory:
                module = f"module-{group_member[0]:03}" if group_member else f"unit-{index:03}"
                source = f"src/{module}.c"
                directory = "/candidate"
                output = f"/build/Release/obj/{index:03}.o"
                original_argv = ["clang", "-c", "-DTEST=1", source, "-o", output]
                projected_argv = compiler_receipt.analyzer_database_project(original_argv, "/usr/bin/clang")
                shard = compiler_receipt.analyzer_database_shard(module, 8)
                fallback_member = bool(fallback_groups and group_member is not None and not alias_count)
                if fallback_member:
                    representative = index
                    has_context = False
                reason = ("context-not-proven-for-shared-invocation" if fallback_member else
                          "" if duplicated else "single-row")
                proven = duplicated and not fallback_member
            else:
                module = f"module-{representative:03}"
                source = f"src/unit-{representative:03}.c"
                directory = f"/candidate/dir-{representative:03}"
                output = (f"/build/000-root-{representative:03}.o" if duplicated and index == representative else
                          f"/build/999-alias-{index:03}.o" if duplicated else f"/build/100-single-{index:03}.o")
                original_argv = ["clang", "-c", source, "-o", output]
                projected_argv = ["clang", "-c", source]
                shard = index % 8
                reason = "" if duplicated else "single-row"
                proven = duplicated
            candidate_rows.append({
                "index": index, "shard": shard, "representative": representative,
                "module": module, "file": source,
                "directory": directory, "output": output,
                "original_argv": original_argv,
                "argv": projected_argv, "proven": proven,
                "reason": reason,
                "proof": f"BUSTER_CLANG_ANALYZE_CONTEXT_V4:{representative}".encode() if has_context else b"",
                "input_inventory": ([{"path": f"/candidate/{source}",
                                       "sha256": hashlib.sha256(source.encode()).hexdigest(),
                                       "metadata": hashlib.sha256(b"mode=100644;size=17").hexdigest(),
                                       "content_rechecked": True}] if has_context else []),
                "search_inventory": ([{"path": "/candidate/include", "entries": 2,
                                        "fingerprint": hashlib.sha256(b"include-tree").hexdigest()}] if has_context else []),
            })
        if dynamic_inventory:
            database_rows = [{"directory": row["directory"], "file": row["file"], "output": row["output"],
                              "arguments": row["original_argv"]} for row in candidate_rows]
            for index in range(selected_count):
                database_rows.append({"directory": "/candidate", "file": f"src/debug-{index:03}.c",
                                      "output": f"/build/Debug/obj/{index:03}.o",
                                      "arguments": ["clang", "-c", f"src/debug-{index:03}.c",
                                                    "-o", f"/build/Debug/obj/{index:03}.o"]})
            compile_commands = json.dumps(database_rows, separators=(",", ":")).encode("utf-8")

        def encode_plan(version: int, label: str, rows: list[dict]) -> bytes:
            data = bytearray(f"BUSTER_CLANG_ANALYZE_PLAN_V{version}\n".encode("ascii"))
            parent = "/runner/work/buster/analyzer/profile"
            if results_parent_change and results_parent_change[0] == label:
                parent = results_parent_change[1]
            for value in (f"{parent}/{label}", "Release", "/usr/bin/clang"):
                cls.record_string(data, value)
            excluded_rows = selected_count if dynamic_inventory else 0
            aliases = sum(row["representative"] != row["index"] for row in rows) if version == 2 else 0
            unique = len(rows) - aliases
            for value in ((8, 600, selected_count, excluded_rows) if version == 1 else
                          (8, 600, selected_count, excluded_rows, unique, aliases, 0)):
                cls.record_number(data, value)
            cls.record_string(data, compile_commands)
            for row in rows:
                cls.record_number(data, row["index"])
                cls.record_number(data, row["shard"])
                if version == 2:
                    representative = row["representative"]
                    if representative_change and representative_change[:2] == (label, row["index"]):
                        representative = representative_change[2]
                    cls.record_number(data, representative)
                cls.record_string(data, row["module"])
                cls.record_string(data, row["file"])
                if version == 2:
                    cls.record_string(data, row["directory"])
                    cls.record_string(data, row["output"])
                    cls.record_number(data, len(row["original_argv"]))
                    for argument in row["original_argv"]:
                        cls.record_string(data, argument)
                cls.record_number(data, len(row["argv"]))
                for argument in row["argv"]:
                    cls.record_string(data, argument)
                if version == 2:
                    cls.record_number(data, int(row["proven"]))
                    cls.record_string(data, row["reason"])
                    proof = row["proof"]
                    if context_change and context_change[:2] == (label, row["index"]):
                        proof = context_change[2]
                    if alias_envelope_change and alias_envelope_change[:2] == (label, row["index"]):
                        field = alias_envelope_change[2]
                        if field == "proof":
                            proof = b"unexpected alias-owned context"
                    cls.record_string(data, proof)
                    input_inventory = row["input_inventory"]
                    if alias_envelope_change and alias_envelope_change[:2] == (label, row["index"]) and \
                            alias_envelope_change[2] == "input":
                        input_inventory = [{"path": "/candidate/alias.h", "sha256": "c" * 64,
                                            "metadata": "d" * 64, "content_rechecked": True}]
                    cls.record_number(data, len(input_inventory))
                    for item in input_inventory:
                        for field in ("path", "sha256", "metadata"):
                            value = item[field]
                            if context_change and context_change[:2] == (label, row["index"]) and field == "sha256":
                                value = "c" * 64
                            cls.record_string(data, value)
                        cls.record_number(data, int(item["content_rechecked"]))
                    search_inventory = row["search_inventory"]
                    if alias_envelope_change and alias_envelope_change[:2] == (label, row["index"]) and \
                            alias_envelope_change[2] == "search":
                        search_inventory = [{"path": "/candidate/include", "entries": 1,
                                            "fingerprint": "e" * 64}]
                    cls.record_number(data, len(search_inventory))
                    for item in search_inventory:
                        cls.record_string(data, item["path"])
                        cls.record_number(data, item["entries"])
                        value = item["fingerprint"]
                        if context_change and context_change[:2] == (label, row["index"]):
                            value = "d" * 64
                        cls.record_string(data, value)
            return bytes(data)

        files: dict[str, bytes] = {
            "request.txt": (request_line + "\n").encode(),
            "compile_commands.json": compile_commands,
            "clang.json": json.dumps({
                "schema": "buster-analyzer-clang-provenance-v1", "path": "/usr/bin/clang",
                "sha256": "1" * 64, "size_bytes": 100, "version": "clang synthetic fixture",
                "resource_dir": "/usr/lib/clang/fixture", "resource_tree_sha256": "2" * 64,
                "resource_file_count": 1, "resource_total_bytes": 100,
            }).encode(),
            "drivers/baseline.driver": b"synthetic baseline driver",
            "drivers/candidate.driver": b"synthetic candidate driver",
            "drivers/baseline.checkout": (identity["base"] + "\n").encode(),
            "drivers/candidate.checkout": (identity["head"] + "\n").encode(),
        }
        if checkout_change:
            files[f"drivers/{checkout_change[0]}.checkout"] = checkout_change[1]
        if clang_change:
            clang_value = json.loads(files["clang.json"])
            clang_value["sha256"] = "3" * 64
            files["clang.json"] = json.dumps(clang_value).encode()
        helper_digest = hashlib.sha256(b"synthetic helper source").hexdigest()
        for role in ("baseline", "candidate"):
            driver = files[f"drivers/{role}.driver"]
            marker = "\n".join((
                "BUSTER_BOOTSTRAP_CACHE_V1", f"config\t{'4' * 64}",
                f"artifact\tbuild-Release\t{hashlib.sha256(driver).hexdigest()}",
                f"dependency\tbuild.c\t{'5' * 64}",
                f"dependency\ttools/clang_analyze.c\t{'6' * 64}",
                f"dependency\ttools/clang_analyze_benchmark.c\t{helper_digest}", "END", ""))
            files[f"drivers/{role}.complete"] = marker.encode()

        plans: dict[str, tuple[int, bytes, list[dict]]] = {}
        role_for = {"prepare-baseline": "baseline", "prepare-candidate": "candidate",
                    "baseline-0": "baseline", "candidate-0": "candidate",
                    "candidate-1": "candidate", "baseline-1": "baseline"}
        for label, role in role_for.items():
            version = 1 if role == "baseline" else 2
            rows = candidate_rows
            manifest = encode_plan(version, label, rows)
            plans[label] = (version, manifest, rows)
            files[f"profile/{label}/manifest.txt"] = manifest

        all_phases = []
        for role, trial in (("baseline", 0), ("candidate", 0)):
            label = f"prepare-{role}"
            if role == "candidate":
                candidate_partition = compiler_receipt.analyzer_candidate_partition_counts({"rows": candidate_rows})
                candidate_groups = candidate_partition["candidate_groups"]
                proven_groups = candidate_partition["proven_groups"]
                aliases = candidate_partition["alias_rows"]
                unique = candidate_partition["unique_executions"]
                files[f"profile/{label}.stdout.log"] = (
                    f"ANALYZE_PREPARE selected_rows={selected_count} unique_executions={unique} aliases={aliases} "
                    f"candidate_groups={candidate_groups} proven_groups={proven_groups} "
                    f"excluded_config_or_language={selected_count if dynamic_inventory else 0} "
                    "planning_us=1 context_proof_us=1 status=pass\n").encode()
            else:
                files[f"profile/{label}.stdout.log"] = b""
            files[f"profile/{label}.stderr.log"] = b""
            all_phases.append((label, role, trial))
        for trial, (label, role) in enumerate(zip(compiler_receipt.ANALYZER_RUNS,
                                                   compiler_receipt.ANALYZER_PROFILE["order"])):
            version, manifest, rows = plans[label]
            for phase, phase_name in (("analysis", f"analysis-{label}"), ("aggregate", f"aggregate-{label}")):
                all_phases.append((phase_name, role, trial))
            for shard in range(8):
                shard_rows = [row for row in rows if row["shard"] == shard]
                executions = sum(row["representative"] == row["index"] for row in shard_rows)
                aliases = len(shard_rows) - executions if version == 2 else 0
                data = bytearray(f"BUSTER_CLANG_ANALYZE_RESULT_V{version}\n".encode())
                data.extend(hashlib.sha256(manifest).hexdigest().encode() + b"\n")
                cls.record_number(data, shard)
                cls.record_number(data, len(shard_rows))
                if version == 2:
                    cls.record_number(data, executions)
                    cls.record_number(data, aliases)
                cls.record_number(data, 1000)
                cls.record_number(data, 1024)
                for row in shard_rows:
                    index = row["index"]
                    cls.record_number(data, index)
                    launched = version == 1 or row["representative"] == index
                    if version == 2:
                        cls.record_number(data, row["representative"])
                        cls.record_number(data, int(launched))
                    cls.record_number(data, 0)
                    cls.record_number(data, 100 + index if launched else 0)
                    log_data = b"synthetic analyzer diagnostics\n"
                    if log_change and log_change[:2] == (label, index):
                        log_data = log_change[2]
                    digest = hashlib.sha256(log_data).hexdigest()
                    data.extend(digest.encode() + b"\n")
                    files[f"profile/{label}/shard-{shard}/unit-{index}.log"] = log_data
                files[f"profile/{label}/shard-{shard}/result.txt"] = bytes(data)
            if missing_result and missing_result == (label, 0):
                files.pop(f"profile/{label}/shard-0/result.txt", None)
            expected_exec = sum(row["representative"] == row["index"] for row in rows)
            expected_alias = len(rows) - expected_exec if version == 2 else 0
            if version == 2:
                aggregate_record = (f"ANALYZE_AGGREGATE selected_rows={selected_count} checked={selected_count} unique_executions={expected_exec} "
                                    f"aliased_rows={expected_alias} excluded_config_or_language={selected_count if dynamic_inventory else 0} failures=0 shards=8 "
                                    "peak_child_rss_bytes=1024 status=pass\n")
                aggregate_plan = (f"ANALYZE_PLAN mode=aggregate selected_rows={selected_count} unique_executions={expected_exec} "
                                  f"aliases={expected_alias} planning_us=100 context_proof_us=80\n")
                aggregate_stdout = aggregate_plan + aggregate_record
                analysis_records = [
                    f"ANALYZE_PLAN mode=run selected_rows={selected_count} unique_executions={expected_exec} aliases={expected_alias} "
                    "planning_us=300 context_proof_us=200"]
                for shard in range(8):
                    shard_rows = [row for row in rows if row["shard"] == shard]
                    shard_exec = sum(row["representative"] == row["index"] for row in shard_rows)
                    shard_alias = len(shard_rows) - shard_exec
                    analysis_records.append(
                        f"ANALYZE_PLAN mode=worker shard={shard} selected_rows={selected_count} unique_executions={expected_exec} "
                        f"aliases={expected_alias} planning_us={10 + shard} context_proof_us={shard + 1}")
                    analysis_records.append(
                        f"ANALYZE_SHARD shard={shard} selected_rows={len(shard_rows)} unique_executions={shard_exec} "
                        f"aliased_rows={shard_alias} elapsed_us={100 + shard} context_preflight_us=5 "
                        "context_postflight_us=6 peak_child_rss_bytes=1024 status=pass")
            else:
                aggregate_stdout = (
                    f"ANALYZE_AGGREGATE eligible={selected_count} checked={selected_count} "
                    f"excluded_config_or_language={selected_count if dynamic_inventory else 0} "
                    "failures=0 shards=8 peak_child_rss_bytes=1024 status=pass\n")
                analysis_records = [
                    f"ANALYZE_SHARD shard={shard} units={len([row for row in rows if row['shard'] == shard])} "
                    f"elapsed_us={100 + shard} peak_child_rss_bytes=1024 status=pass"
                    for shard in range(8)]
                aggregate_record = aggregate_stdout
            files[f"profile/aggregate-{label}.stdout.log"] = aggregate_stdout.encode()
            tree_status = tree_status_change[1] if tree_status_change and tree_status_change[0] == label else "complete"
            tree_reason = tree_reason_change[1] if tree_reason_change and tree_reason_change[0] == label else "none"
            analysis_records.append(aggregate_record.rstrip("\n"))
            analysis_records.append(
                f"ANALYZE_RUN status=pass results=/runner/work/buster/analyzer/profile/{label} elapsed_us=1000 peak_pending_workers=2 jobs=2 "
                f"samples=3 peak_live_processes=2 sampled_peak_tree_rss_bytes=1024 "
                f"process_tree_status={tree_status} process_tree_reason={tree_reason}")
            files[f"profile/analysis-{label}.stdout.log"] = ("\n".join(analysis_records) + "\n").encode()
            files[f"profile/analysis-{label}.stderr.log"] = b""
            files[f"profile/aggregate-{label}.stderr.log"] = b""
        header = "\t".join(compiler_receipt.ANALYZER_PHASE_FIELDS)
        lines = [header]
        for phase, role, trial in all_phases:
            lines.append("\t".join((phase, role, str(trial), "1000", "500", "200", "1024", "observed",
                                    "observed", "0", "0", "0", "0", "100", "0", "complete")))
        files["profile/profile.tsv"] = ("\n".join(lines) + "\n").encode()
        files["profile/helper.log"] = (
            f"exit=0\nANALYZE_BENCHMARK_PROFILE name=clang-analyze-full-v1 elapsed_us=10000 "
            f"phases=10 status={helper_status}\n").encode()
        return files, identity, alias_pairs

    @classmethod
    def failed_raw_profile(cls) -> tuple[dict[str, bytes], dict, list[tuple[int, int]]]:
        """Build failed trials with seven baseline diagnostics and no candidate terminal results."""
        files, identity, aliases = cls.full_raw_profile()
        for label in ("baseline-0", "baseline-1"):
            result_path = f"profile/{label}/shard-0/result.txt"
            result = compiler_receipt.analyzer_parse_result(files[result_path], 1)
            data = bytearray(b"BUSTER_CLANG_ANALYZE_RESULT_V1\n")
            data.extend(result["fingerprint"].encode("ascii") + b"\n")
            cls.record_number(data, result["shard"])
            cls.record_number(data, result["selected_rows"])
            cls.record_number(data, result["elapsed_us"])
            cls.record_number(data, result["peak_child_rss_bytes"])
            failed_indices = {row["index"] for row in result["rows"][:7]}
            for row in result["rows"]:
                cls.record_number(data, row["index"])
                status = row["status"]
                if row["index"] in failed_indices:
                    status = 1
                    log_path = f"profile/{label}/shard-0/unit-{row['index']}.log"
                    log = b"warning: synthetic NullPointerArithm diagnostic\n"
                    files[log_path] = log
                    row["log_sha256"] = hashlib.sha256(log).hexdigest()
                cls.record_number(data, status)
                cls.record_number(data, row["duration_us"])
                data.extend(row["log_sha256"].encode("ascii") + b"\n")
            files[result_path] = bytes(data)

            aggregate = ("ANALYZE_AGGREGATE eligible=182 checked=182 excluded_config_or_language=0 "
                         "failures=7 shards=8 peak_child_rss_bytes=1024 status=fail")
            files[f"profile/aggregate-{label}.stdout.log"] = (aggregate + "\n").encode()
            analysis_lines = []
            for line in files[f"profile/analysis-{label}.stdout.log"].decode().splitlines():
                if line.startswith("ANALYZE_SHARD shard=0 "):
                    line = line.replace("status=pass", "status=fail")
                if line.startswith("ANALYZE_AGGREGATE "):
                    line = aggregate
                if line.startswith("ANALYZE_RUN "):
                    line = line.replace("status=pass", "status=fail")
                analysis_lines.append(line)
            files[f"profile/analysis-{label}.stdout.log"] = ("\n".join(analysis_lines) + "\n").encode()

        for label in ("candidate-0", "candidate-1"):
            for shard in range(8):
                files.pop(f"profile/{label}/shard-{shard}/result.txt", None)
                for name in tuple(files):
                    if name.startswith(f"profile/{label}/shard-{shard}/unit-"):
                        files.pop(name)
            aggregate = ("ANALYZE_AGGREGATE selected_rows=182 checked=0 unique_executions=0 aliased_rows=0 "
                        "excluded_config_or_language=0 failures=0 shards=8 peak_child_rss_bytes=0 status=fail")
            files[f"profile/aggregate-{label}.stdout.log"] = (
                files[f"profile/aggregate-{label}.stdout.log"].decode().splitlines()[0] + "\n" + aggregate + "\n").encode()
            analysis_lines = []
            for line in files[f"profile/analysis-{label}.stdout.log"].decode().splitlines():
                if line.startswith("ANALYZE_PLAN mode=worker ") or line.startswith("ANALYZE_SHARD "):
                    continue
                if line.startswith("ANALYZE_PLAN mode=run "):
                    analysis_lines.append(line)
                    analysis_lines.extend(f"error: worker shard manifest is malformed or inconsistent shard={shard}"
                                          for shard in range(8))
                    continue
                if line.startswith("ANALYZE_AGGREGATE "):
                    line = aggregate
                if line.startswith("ANALYZE_RUN "):
                    line = line.replace("status=pass", "status=fail")
                analysis_lines.append(line)
            files[f"profile/analysis-{label}.stdout.log"] = ("\n".join(analysis_lines) + "\n").encode()

        phase_lines = files["profile/profile.tsv"].decode().splitlines()
        phase_header = phase_lines[0].split("\t")
        state_index = phase_header.index("state")
        exit_index = phase_header.index("exit_status")
        for index in range(1, len(phase_lines)):
            fields = phase_lines[index].split("\t")
            if fields[0].startswith(("analysis-", "aggregate-")):
                fields[state_index] = "failed"
                fields[exit_index] = "256"
                phase_lines[index] = "\t".join(fields)
        files["profile/profile.tsv"] = ("\n".join(phase_lines) + "\n").encode()
        files["profile/helper.log"] = (
            b"exit=1\nANALYZE_BENCHMARK_PROFILE name=clang-analyze-full-v1 elapsed_us=10000 phases=10 status=fail\n")
        return files, identity, aliases

    def test_plan_identity_binds_candidate_context_and_explicit_dependency_inventories(self) -> None:
        prepared = compiler_receipt.analyzer_parse_plan(self.plan("profile/prepare-candidate"))
        repeated = compiler_receipt.analyzer_parse_plan(self.plan("profile/candidate-0"))
        self.assertEqual(compiler_receipt.analyzer_plan_identity(prepared),
                         compiler_receipt.analyzer_plan_identity(repeated))
        variants = (
            self.plan("profile/candidate-0", reason="changed reason"),
            self.plan("profile/candidate-0", proof=b"changed opaque context"),
            self.plan("profile/candidate-0", input_digest="c" * 64),
            self.plan("profile/candidate-0", search_fingerprint="d" * 64),
        )
        for changed in variants:
            parsed = compiler_receipt.analyzer_parse_plan(changed)
            self.assertNotEqual(compiler_receipt.analyzer_plan_identity(prepared),
                                compiler_receipt.analyzer_plan_identity(parsed))

    def test_bundle_verifier_rejects_missing_and_tampered_raw_evidence(self) -> None:
        profile_receipt = analyzer_receipt()
        self.assertTrue(compiler_receipt.validate_analyzer_bundle(profile_receipt, {}, {"files": {}}))
        files = {path: b"invalid" for path in compiler_receipt.ANALYZER_REQUIRED_FILES}
        files["request.txt"] = (compiler_receipt.ANALYZER_REQUEST_LINE + "\n").encode("utf-8")
        files["clang.json"] = b"{}"
        problems = compiler_receipt.validate_analyzer_bundle(profile_receipt, {}, {"files": files})
        self.assertTrue(problems)
        self.assertTrue(any("malformed" in item or "manifest" in item or "provenance" in item for item in problems),
                        problems)

    def test_complete_synthetic_raw_profile_reaches_actual_verifier_and_tampering_fails(self) -> None:
        files, identity, aliases = self.full_raw_profile()
        summary, summary_problems = compiler_receipt.analyzer_profile_summary(files, identity)
        self.assertEqual(summary_problems, [])
        self.assertEqual(summary["status"], "complete")
        self.assertFalse(summary["comparison_invalid"])
        self.assertEqual(summary["inventory"]["selected_rows"], 182)
        self.assertEqual(summary["inventory"]["candidate_unique_executions"], 135)
        self.assertEqual(summary["inventory"]["candidate_alias_rows"], 47)
        self.assertEqual(len(summary["per_tu"]), 182)
        self.assertEqual(sum(row["candidate_planned_execution"] for row in summary["per_tu"]), 135)
        self.assertEqual(sum(row["candidate_executed"] is True for row in summary["per_tu"]), 135)
        self.assertTrue(all(row["candidate_executed"] is not None for row in summary["per_tu"]))
        self.assertEqual([run["name"] for run in summary["runs"]], list(compiler_receipt.ANALYZER_RUNS))
        for run in summary["runs"]:
            costs = run["internal_costs"]
            if run["role"] == "baseline":
                self.assertEqual(costs["planning_context_status"], "unavailable in baseline PLAN_V1")
                self.assertIsNone(costs["run_plan"])
                self.assertIsNone(costs["worker_plans"])
                self.assertIsNone(costs["aggregate_plan"])
                self.assertEqual(len(costs["shards"]), 8)
            else:
                self.assertEqual(costs["planning_context_status"], "reported by candidate PLAN_V2")
                self.assertEqual(costs["run_plan"]["planning_us"], 300)
                self.assertEqual(len(costs["worker_plans"]), 8)
                self.assertEqual(len(costs["shards"]), 8)
                self.assertEqual(costs["aggregate_plan"]["context_proof_us"], 80)
        receipt = analyzer_receipt()
        receipt["identity"] = identity
        receipt["analyzer"] = summary
        receipt["analyzer_request_sha256"] = summary["request"]["sha256"]
        receipt["analyzer_clang_provenance"] = summary["clang"]
        receipt["analyzer_driver_checkouts"] = summary["driver_checkouts"]
        receipt["analyzer_driver_provenance"] = summary["driver_provenance"]
        raw_bundle = {"summary": summary, "files": files}
        self.assertEqual(compiler_receipt.validate_analyzer_bundle(receipt, summary, raw_bundle), [])
        summary_mismatch = compiler_receipt.validate_analyzer_bundle(receipt, {"schema": "tampered"}, raw_bundle)
        self.assertIn("retained analyzer summary does not match the raw V1/V2 plans, results and logs",
                      summary_mismatch)
        report = compiler_receipt.render(receipt, summary, "success", [])
        for label in compiler_receipt.ANALYZER_RUNS:
            self.assertIn(label, report)
        self.assertIn("unavailable in baseline PLAN_V1", report)
        self.assertIn("not summed into serial wall time or a critical-path duration", report)
        self.assertIn("Shard elapsed begins after context preflight", report)
        self.assertIn("preflight is outside elapsed and postflight is already inside it", report)
        self.assertIn("Independent aggregate PLAN planning / context proof", report)
        self.assertIn("Analysis wait4 CPU", report)
        self.assertIn("Aggregate wait4 CPU", report)
        decision = compiler_publish.decide(identity, True, "success", receipt, summary, "",
                                           {"analyzer": raw_bundle})
        self.assertEqual(decision[0], "success", decision)

        mutations: list[tuple[str, dict[str, bytes], str]] = []
        alias_root, alias_index = aliases[0]
        shard = alias_index % 8
        alias_log = copy.deepcopy(files)
        alias_log[f"profile/candidate-0/shard-{shard}/unit-{alias_index}.log"] += b"tampered alias\n"
        mutations.append(("alias diagnostic no longer binds its canonical root", alias_log, "diagnostic log"))

        changed_proof, _, _ = self.full_raw_profile(context_change=("candidate-1", alias_index,
                                                                      b"different opaque proof"))
        mutations.append(("candidate repeat context differs from preflight", changed_proof,
                          "reproduce its separate prepare plan"))
        changed_alias_proof, _, _ = self.full_raw_profile(
            alias_envelope_change=("candidate-1", alias_index, "proof"))
        mutations.append(("alias row owns a context proof", changed_alias_proof, "alias row"))
        changed_alias_input, _, _ = self.full_raw_profile(
            alias_envelope_change=("candidate-1", alias_index, "input"))
        mutations.append(("alias row owns an input inventory", changed_alias_input, "alias row"))
        changed_mapping, _, _ = self.full_raw_profile(
            representative_change=("candidate-1", alias_index, alias_index))
        mutations.append(("candidate alias maps away from its canonical root", changed_mapping,
                          "canonical root"))
        changed_root, _, _ = self.full_raw_profile(results_parent_change=("candidate-1", "/other/profile"))
        mutations.append(("run escapes the common fresh results root", changed_root,
                          "do not share one exact fresh results root"))
        changed_tree, _, _ = self.full_raw_profile(tree_status_change=("candidate-0", "incomplete"))
        mutations.append(("whole-tree RSS sampler is incomplete", changed_tree, "sampler is incomplete"))
        contradictory_tree, _, _ = self.full_raw_profile(
            tree_reason_change=("candidate-0", "parentage-unavailable"))
        mutations.append(("complete sampler reports a failure reason", contradictory_tree,
                          "sampler is incomplete or unavailable"))
        changed_checkout, _, _ = self.full_raw_profile(checkout_change=("candidate", ("f" * 40 + "\n").encode()))
        mutations.append(("candidate driver checkout does not match head", changed_checkout,
                          "checkout records do not match"))
        changed_clang, _, _ = self.full_raw_profile(clang_change=True)
        mutations.append(("Clang executable provenance changed after receipt", changed_clang,
                          "Clang executable/resource provenance does not match"))
        missing_row, _, _ = self.full_raw_profile(missing_result=("candidate-1", 0))
        mutations.append(("a shard terminal record is missing", missing_row, "missing shard 0 terminal result"))
        failed_helper, _, _ = self.full_raw_profile(helper_status="fail")
        mutations.append(("native helper reports failure", failed_helper, "native helper did not complete"))
        changed_prepare, _, _ = self.full_raw_profile()
        changed_prepare["profile/prepare-candidate.stdout.log"] = \
            changed_prepare["profile/prepare-candidate.stdout.log"].replace(b"candidate_groups=47", b"candidate_groups=46")
        mutations.append(("candidate preparation counts differ from its manifest", changed_prepare,
                          "candidate preparation candidate_groups does not match"))

        for description, changed_files, expected_reason in mutations:
            with self.subTest(tamper=description):
                errors = compiler_receipt.validate_analyzer_bundle(receipt, summary, {"files": changed_files})
                self.assertTrue(errors, description)
                self.assertTrue(any(expected_reason in error for error in errors), (description, errors[:8]))

    def test_v2_rederives_changed_full_inventory_aliases_and_safe_fallback(self) -> None:
        line = compiler_receipt.ANALYZER_REQUEST_LINE_V2
        files, identity, _ = self.full_raw_profile(request_line=line, selected_count=184, alias_count=47)
        summary, problems = compiler_receipt.analyzer_profile_summary(files, identity, line)
        self.assertEqual(problems, [], problems)
        self.assertEqual(summary["schema"], compiler_receipt.ANALYZER_SUMMARY_SCHEMA_V2)
        self.assertEqual(summary["status"], "complete")
        self.assertEqual(summary["compile_commands"]["native_database_entries"], 368)
        self.assertEqual(summary["inventory"]["selected_rows"], 184)
        self.assertEqual(summary["inventory"]["excluded_rows"], 184)
        self.assertEqual(summary["inventory"]["candidate_unique_executions"], 137)
        self.assertEqual(summary["inventory"]["candidate_alias_rows"], 47)
        self.assertEqual(summary["inventory"]["candidate_groups"], 47)
        self.assertEqual(summary["inventory"]["candidate_proven_groups"], 47)
        self.assertEqual(len(summary["per_tu"]), 184)

        inventory = compiler_receipt.analyzer_native_release_inventory(files["compile_commands.json"], "/usr/bin/clang")
        baseline = compiler_receipt.analyzer_parse_plan(files["profile/prepare-baseline/manifest.txt"])
        candidate = compiler_receipt.analyzer_parse_plan(files["profile/prepare-candidate/manifest.txt"])
        self.assertEqual(compiler_receipt.analyzer_inventory_plan_problems(inventory, baseline, candidate), [])
        omission_baseline, omission_candidate = copy.deepcopy(baseline), copy.deepcopy(candidate)
        omission_baseline["rows"].pop()
        omission_candidate["rows"].pop()
        self.assertTrue(any("omits or adds rows" in item for item in
                            compiler_receipt.analyzer_inventory_plan_problems(
                                inventory, omission_baseline, omission_candidate)))
        foreign_baseline, foreign_candidate = copy.deepcopy(baseline), copy.deepcopy(candidate)
        foreign_baseline["rows"][10]["file"] = "foreign/omitted.c"
        foreign_candidate["rows"][10]["file"] = "foreign/omitted.c"
        self.assertTrue(compiler_receipt.analyzer_inventory_plan_problems(
            inventory, foreign_baseline, foreign_candidate))
        reordered_baseline, reordered_candidate = copy.deepcopy(baseline), copy.deepcopy(candidate)
        reordered_baseline["rows"][10:12] = reversed(reordered_baseline["rows"][10:12])
        reordered_candidate["rows"][10:12] = reversed(reordered_candidate["rows"][10:12])
        self.assertTrue(compiler_receipt.analyzer_inventory_plan_problems(
            inventory, reordered_baseline, reordered_candidate))
        wrong_context = copy.deepcopy(candidate)
        wrong_context["rows"][10]["directory"] = "/foreign/build"
        self.assertTrue(any("directory" in item for item in
                            compiler_receipt.analyzer_inventory_plan_problems(inventory, baseline, wrong_context)))
        alias_failure = copy.deepcopy(candidate)
        alias_index = next(index for index, row in enumerate(alias_failure["rows"])
                           if row["representative"] != row["index"])
        alias_failure["rows"][alias_index]["representative"] = alias_index
        self.assertTrue(compiler_receipt.analyzer_candidate_plan_problems(alias_failure, allow_fallback=True))

        v2_receipt = analyzer_receipt(request_line=line)
        v2_receipt["identity"] = identity
        v2_receipt["analyzer"] = summary
        v2_receipt["analyzer_request_sha256"] = summary["request"]["sha256"]
        v2_receipt["analyzer_clang_provenance"] = summary["clang"]
        v2_receipt["analyzer_driver_checkouts"] = summary["driver_checkouts"]
        v2_receipt["analyzer_driver_provenance"] = summary["driver_provenance"]
        raw_bundle = {"summary": summary, "files": files}
        self.assertEqual(compiler_receipt.validate_analyzer_bundle(v2_receipt, summary, raw_bundle), [])
        v2_summary_mismatch = compiler_receipt.validate_analyzer_bundle(
            v2_receipt, {"schema": "tampered"}, raw_bundle)
        self.assertIn("retained analyzer summary does not match the raw V2 plans, derived inventory, results and logs",
                      v2_summary_mismatch)
        self.assertEqual(compiler_publish.decide(identity, True, "success", v2_receipt, summary, "",
                                                 {"analyzer": raw_bundle})[0], "success")
        self.assertIn("clang-analyze-full-v2", compiler_receipt.render(v2_receipt, summary, "success", []))
        self.assertIn("clang-analyze-full-v2", compiler_publish.commit_report(
            v2_receipt, summary, "success", "Measured", [], "links"))

        fallback_files, fallback_identity, _ = self.full_raw_profile(
            request_line=line, selected_count=184, alias_count=0, fallback_groups=47)
        fallback_summary, fallback_problems = compiler_receipt.analyzer_profile_summary(
            fallback_files, fallback_identity, line)
        self.assertEqual(fallback_problems, [], fallback_problems)
        self.assertEqual(fallback_summary["inventory"]["candidate_unique_executions"], 184)
        self.assertEqual(fallback_summary["inventory"]["candidate_alias_rows"], 0)
        self.assertEqual(fallback_summary["inventory"]["candidate_groups"], 47)
        self.assertEqual(fallback_summary["inventory"]["candidate_proven_groups"], 0)
        fallback_candidate = compiler_receipt.analyzer_parse_plan(
            fallback_files["profile/prepare-candidate/manifest.txt"])
        mixed_class = copy.deepcopy(fallback_candidate)
        mixed_class["rows"][1]["representative"] = 0
        self.assertTrue(compiler_receipt.analyzer_candidate_plan_problems(mixed_class, allow_fallback=True))
        generic_reason = copy.deepcopy(fallback_candidate)
        generic_reason["rows"][0]["reason"] = "single-row"
        self.assertTrue(compiler_receipt.analyzer_candidate_plan_problems(generic_reason, allow_fallback=True))
        proof_fallback = copy.deepcopy(fallback_candidate)
        proof_fallback["rows"][0]["context_proof_present"] = True
        self.assertTrue(compiler_receipt.analyzer_candidate_plan_problems(proof_fallback, allow_fallback=True))
        sampler_files, sampler_identity, _ = self.full_raw_profile(
            request_line=line, selected_count=184, alias_count=47,
            tree_status_change=("candidate-0", "incomplete"))
        _, sampler_problems = compiler_receipt.analyzer_profile_summary(sampler_files, sampler_identity, line)
        self.assertTrue(any("sampler is incomplete" in item for item in sampler_problems))

    def test_v2_inventory_matches_linux_cmake_selection_and_rejects_ambiguous_database(self) -> None:
        clang = "/usr/bin/clang-21"
        database = [
            {"directory": "/work/build", "file": "../src/mod_test.c", "output": "obj/Release/mod_test.o",
             "command": "/usr/bin/cc -DNAME=\"two words\" -c ../src/mod_test.c -o obj/Release/mod_test.o -MMD -MF dep.d"},
            {"directory": "/work/build", "file": "../src/debug.c", "output": "obj/Debug/debug.o",
             "arguments": ["/usr/bin/cc", "-DCMAKE_INTDIR=Debug", "-c", "../src/debug.c", "-o", "obj/Debug/debug.o"]},
            {"directory": "/work/build", "file": "../src/tool.cpp", "output": "obj/Release/tool.o",
             "arguments": ["/usr/bin/c++", "-DCMAKE_INTDIR=Release", "-c", "../src/tool.cpp", "-o", "obj/Release/tool.o"]},
        ]
        inventory = compiler_receipt.analyzer_native_release_inventory(
            json.dumps(database, separators=(",", ":")).encode(), clang)
        self.assertEqual((inventory["entries"], inventory["selected_rows"], inventory["excluded_rows"]), (3, 1, 2))
        row = inventory["rows"][0]
        self.assertEqual((row["index"], row["file"], row["module"], row["shard"]),
                         (0, "../src/mod_test.c", "mod", compiler_receipt.analyzer_database_shard("mod", 8)))
        self.assertEqual(row["original_argv"], ["/usr/bin/cc", "-DNAME=two words", "-c", "../src/mod_test.c",
                                                "-o", "obj/Release/mod_test.o", "-MMD", "-MF", "dep.d"])
        self.assertEqual(row["argv"], [clang, "--analyze", "-Xanalyzer", "-analyzer-output=text",
                                       "-fno-color-diagnostics", "-Wno-error=unused-command-line-argument",
                                       "-DNAME=two words", "../src/mod_test.c"])

        self.assertTrue(compiler_receipt.analyzer_database_path_equal("/work/src.c/", r"\work\src.c"))
        self.assertFalse(compiler_receipt.analyzer_database_path_equal("/work/Release/src.c", "/work/Debug/src.c"))
        self.assertFalse(compiler_receipt.analyzer_database_path_equal("C:/work/src.c", "c:/work/src.c"))

        duplicate_property = b'[{"directory":"/work/build","directory":"/work/other","file":"x.c","arguments":["cc","x.c","-DCMAKE_INTDIR=Release"]}]'
        with self.assertRaisesRegex(ValueError, "repeats a JSON property"):
            compiler_receipt.analyzer_native_release_inventory(duplicate_property, clang)

        duplicate_identity = [database[0], dict(database[0])]
        with self.assertRaisesRegex(ValueError, "duplicate selected TU identity"):
            compiler_receipt.analyzer_native_release_inventory(
                json.dumps(duplicate_identity, separators=(",", ":")).encode(), clang)

        unbound_source = [{"directory": "/work/build", "file": "../src/expected.c", "output": "obj/Release/expected.o",
                           "arguments": ["cc", "-DCMAKE_INTDIR=Release", "-c", "../src/foreign.c", "-o", "obj/Release/expected.o"]}]
        with self.assertRaisesRegex(ValueError, "does not explicitly name its source"):
            compiler_receipt.analyzer_native_release_inventory(
                json.dumps(unbound_source, separators=(",", ":")).encode(), clang)

    def test_failed_profile_retains_observations_and_remains_unqualifiable(self) -> None:
        files, identity, _ = self.failed_raw_profile()
        profile_summary, summary_problems = compiler_receipt.analyzer_profile_summary(files, identity)
        self.assertTrue(summary_problems)
        self.assertEqual(profile_summary["status"], "failed")
        self.assertTrue(profile_summary["comparison_invalid"])
        self.assertEqual(len(profile_summary["measurements"]), 10)
        self.assertEqual(len(profile_summary["runs"]), 4)
        self.assertEqual(profile_summary["sampler"]["status"], "complete")
        self.assertEqual(len(profile_summary["sampler"]["runs"]), 4)
        self.assertEqual(len(profile_summary["per_tu"]), 182)
        runs = {row["name"]: row for row in profile_summary["runs"]}
        self.assertEqual(runs["baseline-0"]["status"], "failed")
        self.assertEqual(runs["baseline-0"]["result_coverage"], {
            "planned_rows": 182, "terminal_shards": 8, "observed_rows": 182,
            "passing_rows_observed": 175, "failed_rows_observed": 7, "unrecorded_rows": 0})
        self.assertEqual(runs["candidate-0"]["status"], "failed")
        self.assertEqual(runs["candidate-0"]["result_coverage"], {
            "planned_rows": 182, "terminal_shards": 0, "observed_rows": 0,
            "passing_rows_observed": 0, "failed_rows_observed": 0, "unrecorded_rows": 182})
        self.assertEqual(runs["candidate-0"]["executed_tus"], None)
        self.assertEqual(runs["candidate-0"]["whole_tree_sampler_status"], "complete")
        row_zero = next(row for row in profile_summary["per_tu"] if row["index"] == 0)
        self.assertEqual(row_zero["run_evidence"]["baseline-0"]["state"], "failed")
        self.assertEqual(row_zero["run_evidence"]["baseline-0"]["status"], 1)
        self.assertEqual(row_zero["run_evidence"]["candidate-0"]["state"], "missing")
        self.assertIsNone(row_zero["candidate_executed"])
        self.assertIsNone(row_zero["baseline_median_elapsed_us"])
        self.assertIsNone(row_zero["candidate_median_elapsed_us"])

        profile_receipt = analyzer_receipt()
        profile_receipt["identity"] = identity
        profile_receipt["analyzer"] = profile_summary
        profile_receipt["analyzer_request_sha256"] = profile_summary["request"]["sha256"]
        profile_receipt["analyzer_clang_provenance"] = profile_summary["clang"]
        profile_receipt["analyzer_driver_checkouts"] = profile_summary["driver_checkouts"]
        profile_receipt["analyzer_driver_provenance"] = profile_summary["driver_provenance"]
        raw_bundle = {"summary": profile_summary, "files": files}
        self.assertTrue(compiler_receipt.validate_analyzer_bundle(profile_receipt, profile_summary, raw_bundle))
        decision = compiler_publish.decide(identity, True, "success", profile_receipt, profile_summary, "",
                                           {"analyzer": raw_bundle})
        self.assertEqual(decision[0], "failure", decision)
        report = compiler_receipt.render_analyzer(profile_receipt, profile_summary, decision[0], decision[2])
        self.assertIn("**Comparison status: invalid.**", report)
        self.assertIn("Failed translation-unit result rows:", report)
        self.assertIn("`src/unit-000.c`", report)
        self.assertIn("0 / 182", report)
        self.assertIn("complete", report)
        self.assertIn("Observed aggregate counters (diagnostic only", report)
        self.assertIn("fail / failed / identity matched / plan-bound-failed", report)
        self.assertIn("| 182 | 182 | N/A (PLAN_V1) | N/A (PLAN_V1) | 7 | 1024 |", report)
        self.assertIn("| 182 | 0 | 0 | 0 | 0 | 0 |", report)
        self.assertNotIn("Wall time B/A", report)

    def test_failed_cost_records_preserve_individual_plan_bound_observations(self) -> None:
        files, identity, _ = self.failed_raw_profile()
        summary, problems = compiler_receipt.analyzer_profile_summary(files, identity)
        self.assertTrue(problems)
        runs = {row["name"]: row for row in summary["runs"]}
        baseline_costs = runs["baseline-0"]["internal_costs"]
        self.assertEqual(baseline_costs["validation_status"], "failed")
        self.assertEqual(baseline_costs["record_sets"]["shards"], "complete")
        failed_shard = next(row for row in baseline_costs["shards"] if row["status"] == "fail")
        self.assertEqual(failed_shard["validation_status"], "plan-bound")
        self.assertEqual(failed_shard["observed_numeric"]["elapsed_us"], 100)
        self.assertEqual(failed_shard["observed_numeric"]["peak_child_rss_bytes"], 1024)
        baseline_aggregate = baseline_costs["analysis_aggregate_records"][0]
        self.assertEqual(baseline_aggregate["observed_numeric"]["eligible"], 182)
        self.assertEqual(baseline_aggregate["observed_numeric"]["checked"], 182)
        self.assertEqual(baseline_aggregate["observed_numeric"]["failures"], 7)
        self.assertGreater(baseline_aggregate["observed_numeric"]["peak_child_rss_bytes"], 0)
        self.assertEqual(baseline_aggregate["plan_identity_status"], "matched")
        self.assertEqual(baseline_aggregate["result_state"], "failed")
        self.assertEqual(baseline_aggregate["validation_status"], "plan-bound-failed")
        self.assertTrue(baseline_aggregate["plan_checks"]["eligible"]["matches"])
        self.assertFalse(baseline_aggregate["plan_checks"]["failures"]["matches"])

        candidate_costs = runs["candidate-0"]["internal_costs"]
        self.assertEqual(candidate_costs["validation_status"], "failed")
        self.assertEqual(candidate_costs["record_sets"]["worker_plan"], "missing")
        self.assertEqual(candidate_costs["run_plan"]["planning_us"], 300)
        self.assertEqual(candidate_costs["aggregate_plan"]["context_proof_us"], 80)
        self.assertEqual(candidate_costs["worker_plan_records"], [])
        self.assertTrue(candidate_costs["raw_record_sets"]["analysis_plan"]["records"])
        self.assertTrue(candidate_costs["raw_record_sets"]["aggregate_plan"]["records"])
        candidate_aggregate = candidate_costs["independent_aggregate_records"][0]
        self.assertEqual(candidate_aggregate["observed_numeric"], {
            "selected_rows": 182, "checked": 0, "unique_executions": 0, "aliased_rows": 0,
            "excluded_config_or_language": 0, "failures": 0, "shards": 8,
            "peak_child_rss_bytes": 0})
        self.assertEqual(candidate_aggregate["plan_identity_status"], "mismatch")
        self.assertEqual(candidate_aggregate["count_match_status"], "mismatch")
        self.assertEqual(candidate_aggregate["result_state"], "failed")
        self.assertTrue(all(row["baseline_median_elapsed_us"] is None and
                            row["candidate_median_elapsed_us"] is None for row in summary["per_tu"]))

        report = compiler_receipt.render_analyzer(analyzer_receipt(), summary, "failure", problems)
        self.assertIn("fail / plan-bound", report)
        self.assertIn("missing worker PLAN", report)
        self.assertIn("0.000 s (plan-bound)", report)
        self.assertIn("NA (missing record)", report)

    def test_malformed_and_plan_mismatched_aggregate_counters_stay_unqualified(self) -> None:
        files, identity, _ = self.failed_raw_profile()
        for path in ("profile/analysis-candidate-0.stdout.log", "profile/aggregate-candidate-0.stdout.log"):
            files[path] = files[path].replace(b"checked=0", b"checked=bad", 1)
        summary, problems = compiler_receipt.analyzer_profile_summary(files, identity)
        self.assertTrue(problems)
        self.assertEqual(summary["status"], "failed")
        self.assertTrue(summary["comparison_invalid"])
        runs = {row["name"]: row for row in summary["runs"]}
        malformed = runs["candidate-0"]["internal_costs"]["analysis_aggregate_records"][0]
        self.assertEqual(malformed["field_checks"]["checked"]["state"], "invalid")
        self.assertNotIn("checked", malformed["observed_numeric"])
        self.assertEqual(malformed["result_state"], "incomplete")
        malformed_receipt = analyzer_receipt()
        malformed_receipt["identity"] = identity
        self.assertTrue(compiler_receipt.validate_analyzer_bundle(
            malformed_receipt, summary, {"files": files}))

        files, identity, _ = self.full_raw_profile()
        aggregate_path = "profile/aggregate-candidate-0.stdout.log"
        aggregate_lines = files[aggregate_path].decode().splitlines()
        files[aggregate_path] = ("\n".join(
            line.replace("selected_rows=182", "selected_rows=181", 1)
            if line.startswith("ANALYZE_AGGREGATE ") else line for line in aggregate_lines) + "\n").encode()
        summary, problems = compiler_receipt.analyzer_profile_summary(files, identity)
        self.assertTrue(problems)
        self.assertEqual(summary["status"], "failed")
        self.assertTrue(summary["comparison_invalid"])
        runs = {row["name"]: row for row in summary["runs"]}
        mismatch = runs["candidate-0"]["internal_costs"]["independent_aggregate_records"][0]
        self.assertEqual(mismatch["observed_numeric"]["selected_rows"], 181)
        self.assertFalse(mismatch["plan_checks"]["selected_rows"]["matches"])
        self.assertEqual(mismatch["plan_identity_status"], "mismatch")
        self.assertEqual(mismatch["result_state"], "failed")
        mismatch_receipt = analyzer_receipt()
        mismatch_receipt["identity"] = identity
        self.assertTrue(compiler_receipt.validate_analyzer_bundle(
            mismatch_receipt, summary, {"files": files}))

    def test_malformed_run_records_results_and_manifests_stay_fail_closed(self) -> None:
        expected = dict(EXPECTED, mode="pull", ref="refs/pull/7/head")
        files, identity, _ = self.full_raw_profile()
        run_path = "profile/analysis-baseline-0.stdout.log"
        run_record = next(line for line in files[run_path].decode().splitlines()
                          if line.startswith("ANALYZE_RUN "))
        files[run_path] += (run_record + "\n").encode()
        files["profile/candidate-0/shard-0/result.txt"] = b"BUSTER_CLANG_ANALYZE_RESULT_V2\ntruncated\n"
        phase_lines = files["profile/profile.tsv"].decode().splitlines()
        files["profile/profile.tsv"] = ("\n".join(
            line for line in phase_lines if not line.startswith("analysis-candidate-1\t")) + "\n").encode()
        profile_summary, problems = compiler_receipt.analyzer_profile_summary(files, identity)
        self.assertTrue(problems)
        self.assertTrue(profile_summary["comparison_invalid"])
        self.assertEqual(profile_summary["status"], "failed")
        self.assertEqual(len(profile_summary["runs"]), 4)
        runs = {row["name"]: row for row in profile_summary["runs"]}
        self.assertEqual(runs["baseline-0"]["driver_record_status"], "duplicate")
        self.assertEqual(len(runs["baseline-0"]["driver_record"]), 2)
        self.assertEqual(runs["candidate-0"]["result_coverage"]["terminal_shards"], 7)
        self.assertGreater(runs["candidate-0"]["result_coverage"]["unrecorded_rows"], 0)
        self.assertIsNone(runs["candidate-1"]["analysis_process_wall_us"])
        self.assertIsNone(profile_summary["totals"]["matched_full_analysis_process_wall_us"])
        report = compiler_receipt.render_analyzer(analyzer_receipt(), profile_summary, "failure", [])
        self.assertIn("NA s", report)
        self.assertIn("**Comparison status: invalid.**", report)

        malformed = dict(files)
        malformed["profile/candidate-1/manifest.txt"] = b"BUSTER_CLANG_ANALYZE_PLAN_V2\ntruncated\n"
        malformed_summary, malformed_problems = compiler_receipt.analyzer_profile_summary(malformed, identity)
        self.assertTrue(malformed_problems)
        self.assertTrue(malformed_summary["comparison_invalid"])
        receipt_for_malformed = analyzer_receipt()
        receipt_for_malformed["identity"] = identity
        receipt_for_malformed["analyzer"] = malformed_summary
        receipt_for_malformed["analyzer_request_sha256"] = malformed_summary["request"]["sha256"]
        receipt_for_malformed["analyzer_clang_provenance"] = malformed_summary["clang"]
        receipt_for_malformed["analyzer_driver_checkouts"] = malformed_summary["driver_checkouts"]
        receipt_for_malformed["analyzer_driver_provenance"] = malformed_summary["driver_provenance"]
        malformed_bundle = {"summary": malformed_summary, "files": malformed}
        decision = compiler_publish.decide(expected, True, "success", receipt_for_malformed, malformed_summary, "",
                                           {"analyzer": malformed_bundle})
        self.assertEqual(decision[0], "failure", decision)

    def test_candidate_full_arm_plan_and_shard_cost_records_are_bound(self) -> None:
        files, _, _ = self.full_raw_profile()
        label = "candidate-0"
        plan = compiler_receipt.analyzer_parse_plan(files[f"profile/{label}/manifest.txt"])
        analysis_path = f"profile/analysis-{label}.stdout.log"
        aggregate_path = f"profile/aggregate-{label}.stdout.log"
        analysis = files[analysis_path].decode("utf-8")
        aggregate = files[aggregate_path].decode("utf-8")
        self.assertEqual(compiler_receipt.analyzer_parse_run_costs(analysis, aggregate, plan, label)["format"],
                         "PLAN_V2")

        analysis_lines = analysis.splitlines()
        aggregate_lines = aggregate.splitlines()
        run_plan = next(line for line in analysis_lines if line.startswith("ANALYZE_PLAN mode=run "))
        worker_plan = next(line for line in analysis_lines if line.startswith("ANALYZE_PLAN mode=worker shard=0 "))
        shard_record = next(line for line in analysis_lines if line.startswith("ANALYZE_SHARD shard=0 "))
        aggregate_plan = next(line for line in aggregate_lines if line.startswith("ANALYZE_PLAN mode=aggregate "))
        aggregate_record = next(line for line in aggregate_lines if line.startswith("ANALYZE_AGGREGATE "))
        cases = [
            ("missing run plan", analysis.replace(run_plan + "\n", "", 1), aggregate,
             "one run PLAN and one worker PLAN per shard"),
            ("duplicate run plan", analysis + run_plan + "\n", aggregate,
             "one run PLAN and one worker PLAN per shard"),
            ("malformed run-plan counter", analysis.replace(run_plan, run_plan.replace("planning_us=300", "planning_us=x", 1), 1), aggregate,
             "invalid nonnegative planning_us"),
            ("missing worker plan", analysis.replace(worker_plan + "\n", "", 1), aggregate,
             "one run PLAN and one worker PLAN per shard"),
            ("duplicate worker plan", analysis + worker_plan + "\n", aggregate,
             "one run PLAN and one worker PLAN per shard"),
            ("wrong worker shard", analysis.replace(worker_plan, worker_plan.replace("shard=0", "shard=8", 1), 1), aggregate,
             "duplicate or invalid shard IDs"),
            ("malformed worker counter", analysis.replace(worker_plan, worker_plan.replace("planning_us=10", "planning_us=x", 1), 1), aggregate,
             "invalid nonnegative planning_us"),
            ("missing shard record", analysis.replace(shard_record + "\n", "", 1), aggregate,
             "one ANALYZE_SHARD record per shard"),
            ("duplicate shard record", analysis + shard_record + "\n", aggregate,
             "one ANALYZE_SHARD record per shard"),
            ("wrong shard record ID", analysis.replace(shard_record, shard_record.replace("shard=0", "shard=8", 1), 1), aggregate,
             "duplicate or invalid shard IDs"),
            ("malformed shard counter", analysis.replace(shard_record, shard_record.replace("context_preflight_us=5", "context_preflight_us=x", 1), 1), aggregate,
             "invalid nonnegative context_preflight_us"),
            ("wrong run-plan count", analysis.replace(run_plan, run_plan.replace("selected_rows=182", "selected_rows=181", 1), 1), aggregate,
             "run PLAN counts differ"),
            ("missing aggregate plan", analysis, aggregate.replace(aggregate_plan + "\n", "", 1),
             "exactly one aggregate PLAN"),
            ("duplicate aggregate plan", analysis, aggregate + aggregate_plan + "\n",
             "exactly one aggregate PLAN"),
            ("wrong aggregate plan count", analysis, aggregate.replace(aggregate_plan,
             aggregate_plan.replace("selected_rows=182", "selected_rows=181", 1), 1),
             "aggregate PLAN counts differ"),
            ("wrong aggregate plan mode", analysis, aggregate.replace(aggregate_plan,
             aggregate_plan.replace("mode=aggregate", "mode=worker", 1), 1),
             "aggregate PLAN has the wrong mode"),
            ("malformed aggregate plan counter", analysis, aggregate.replace(aggregate_plan,
             aggregate_plan.replace("context_proof_us=80", "context_proof_us=x", 1), 1),
             "invalid nonnegative context_proof_us"),
            ("aggregate result does not match plan", analysis, aggregate.replace(aggregate_record,
             aggregate_record.replace("selected_rows=182", "selected_rows=181", 1), 1),
             "independent aggregate selected_rows does not match"),
            ("missing in-run aggregate result", analysis.replace(aggregate_record + "\n", "", 1), aggregate,
             "exactly one aggregate result"),
            ("duplicate in-run aggregate result", analysis + aggregate_record + "\n", aggregate,
             "exactly one aggregate result"),
        ]
        for description, changed_analysis, changed_aggregate, expected in cases:
            with self.subTest(record=description):
                with self.assertRaisesRegex(ValueError, expected):
                    compiler_receipt.analyzer_parse_run_costs(changed_analysis, changed_aggregate, plan, label)

    def test_native_v2_full_inventory_fixture_admits_representative_owned_envelopes(self) -> None:
        root = Path(__file__).resolve().parents[2]
        fixture_dir = root / "docs/performance-audits/evidence/2026-10-09-issue3160/native-plan-v2"
        metadata = json.loads((fixture_dir / "metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["schema"], "buster-analyzer-native-plan-fixture-v1")
        producer = metadata["producer"]
        self.assertEqual(producer["revision_scope"],
                         "local analyzer-mode producer commit, not an approved or landed performance result")
        self.assertEqual(producer["local_revision"], "e99f087d4d87bb31ba7fbf43f2dbfc1afd1613a7")
        self.assertEqual(producer["local_tree"], "501876983e66df54ba9bc9012213c2c1e97ba7b5")
        for key in ("analyzer_source_sha256", "build_driver_source_sha256", "native_driver_sha256",
                    "clang_sha256", "compile_commands_sha256"):
            self.assertRegex(producer[key], r"^[0-9a-f]{64}$")
        self.assertEqual(producer["analyzer_source_sha256"],
                         "4e4beb298c23fcf25d616431b4f13abc035fa327490c47aa7bb34b1bbae2c8e6")
        self.assertEqual(producer["native_driver_sha256"],
                         "8639c929c50f155e790d995ce9d90c1f44a29a58b921a5576c46b4e23c8824b0")
        self.assertEqual(producer["clang_sha256"],
                         "2451bd5e44d0fe575e3088eb29725b4b0820e5c335a3fdb889ae036f55b57f56")
        self.assertEqual(producer["analyzer_profile"]["clang_path_in_plan_header"], "")
        self.assertEqual(producer["analyzer_profile"]["clang_argv0_in_compile_database"],
                         "/usr/lib/llvm-21/bin/clang")
        compressed = (fixture_dir / "final-ci-plan-v2.manifest.gz").read_bytes()
        self.assertEqual(hashlib.sha256(compressed).hexdigest(), metadata["compressed_sha256"])
        raw = gzip.decompress(compressed)
        self.assertEqual(len(raw), metadata["manifest_size_bytes"])
        self.assertEqual(hashlib.sha256(raw).hexdigest(), metadata["manifest_sha256"])
        plan = compiler_receipt.analyzer_parse_plan(raw)
        self.assertEqual(hashlib.sha256(plan["database"]).hexdigest(), producer["compile_commands_sha256"])
        self.assertEqual(len(plan["database"]), producer["compile_commands_size_bytes"])
        self.assertEqual(plan["clang"], producer["analyzer_profile"]["clang_path_in_plan_header"])
        self.assertIn(producer["analyzer_profile"]["clang_argv0_in_compile_database"].encode(), plan["database"])
        self.assertEqual((plan["config"], plan["shards"], plan["timeout"]), ("Release", 8, 600))
        self.assertEqual((plan["version"], plan["selected_rows"], plan["unique_executions"], plan["aliases"]),
                         (2, 182, 135, 47))
        self.assertEqual(compiler_receipt.analyzer_candidate_plan_problems(plan), [])
        self.assertIn("Structural PLAN_V2 parser and representative-envelope fixture only", metadata["fixture_scope"])
        self.assertIn("did not run the full analyzer workload", metadata["fixture_scope"])
        self.assertIn("hosted argv0 shape", metadata["fixture_scope"])

        aliases = [row for row in plan["rows"] if row["representative"] != row["index"]]
        roots = {row["representative"] for row in aliases}
        self.assertEqual(len(roots), metadata["canonical_alias_groups"])
        self.assertTrue(all(row["proven"] and not row["reason"] and not row["context_proof_present"] and
                            not row["input_inventory"] and not row["search_inventory"] for row in aliases))
        self.assertTrue(all(plan["rows"][index]["context_proof_present"] and plan["rows"][index]["input_inventory"] and
                            plan["rows"][index]["search_inventory"] for index in roots))

        changed_mapping = copy.deepcopy(plan)
        changed_mapping["rows"][aliases[0]["index"]]["representative"] = aliases[0]["index"]
        self.assertTrue(compiler_receipt.analyzer_candidate_plan_problems(changed_mapping))
        changed_alias_proof = copy.deepcopy(plan)
        changed_alias_proof["rows"][aliases[0]["index"]]["context_proof_present"] = True
        self.assertTrue(compiler_receipt.analyzer_candidate_plan_problems(changed_alias_proof))
        changed_alias_input = copy.deepcopy(plan)
        changed_alias_input["rows"][aliases[0]["index"]]["input_inventory"] = [{"path": "/alias.h"}]
        self.assertTrue(compiler_receipt.analyzer_candidate_plan_problems(changed_alias_input))
        changed_root_context = copy.deepcopy(plan)
        root_index = aliases[0]["representative"]
        changed_root_context["rows"][root_index]["context_proof_present"] = False
        changed_root_context["rows"][root_index]["input_inventory"] = []
        changed_root_context["rows"][root_index]["search_inventory"] = []
        self.assertTrue(compiler_receipt.analyzer_candidate_plan_problems(changed_root_context))

    def test_fixture_git_commit_leaves_no_housekeeping_writer_for_strict_cleanup(self) -> None:
        # #3361: force housekeeping even for this tiny repository. Git's trace
        # records child launches before detachment, so this needs no scheduling race.
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        repo = root / "repo"
        repo.mkdir()
        fixture_git(repo, "init", "-q")
        fixture_git(repo, "config", "user.name", "Analyzer Test")
        fixture_git(repo, "config", "user.email", "analyzer@example.invalid")
        for key, value in (("maintenance.auto", "true"), ("maintenance.loose-objects.enabled", "true"),
                           ("maintenance.loose-objects.auto", "1"), ("gc.auto", "1")):
            fixture_git(repo, "config", key, value)
        for index in range(40):
            (repo / f"unit{index}.c").write_text(f"int value{index};\n", encoding="utf-8")
        fixture_git(repo, "add", ".")
        trace = root / "git-trace.jsonl"
        with mock.patch.dict(os.environ, {"GIT_TRACE2_EVENT": str(trace)}):
            fixture_git(repo, "commit", "-qm", "seed")
        events = [json.loads(line) for line in trace.read_text(encoding="utf-8").splitlines()]
        self.assertTrue(any(event.get("event") == "start" for event in events))
        housekeeping = [event for event in events if event.get("event") == "child_start" and
                        ("maintenance" in event.get("argv", []) or "gc" in event.get("argv", []))]
        self.assertEqual(housekeeping, [])
        self.assertEqual(fixture_git(repo, "show", "HEAD:unit0.c"), "int value0;")
        # Strict teardown: cleanup errors are not ignored.
        directory.cleanup()
        self.assertFalse(root.exists())

    def test_source_immutability_detects_nonignored_untracked_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            fixture_git(repo, "init", "-q")
            fixture_git(repo, "config", "user.name", "Analyzer Test")
            fixture_git(repo, "config", "user.email", "analyzer@example.invalid")
            source = repo / "tracked.c"
            source.write_text("int value;\n", encoding="utf-8")
            fixture_git(repo, "add", "tracked.c")
            fixture_git(repo, "commit", "-qm", "seed")
            self.assertEqual(compiler_compare.analyzer_source_immutability_problem(repo), "")
            (repo / "injected.h").write_text("#define VALUE 1\n", encoding="utf-8")
            self.assertIn("untracked", compiler_compare.analyzer_source_immutability_problem(repo))
            (repo / "injected.h").unlink()
            source.write_text("int changed;\n", encoding="utf-8")
            self.assertIn("tracked", compiler_compare.analyzer_source_immutability_problem(repo))

    def test_profile_selector_must_be_added_at_the_current_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            def git(*args: str) -> str:
                return fixture_git(repo, *args)
            git("init", "-q")
            git("config", "user.name", "Analyzer Test")
            git("config", "user.email", "analyzer@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            (repo / "tracked.c").write_text("int seed;\n", encoding="utf-8")
            git("add", "tracked.c")
            git("commit", "-qm", "seed")
            seed = git("rev-parse", "HEAD")
            self.assertEqual(compiler_compare.request_selector_count(
                repo, seed, compiler_receipt.ANALYZER_REQUEST_LINE), 0)
            request.write_text("# request history\n", encoding="utf-8")
            git("add", compiler_receipt.ANALYZER_REQUEST_PATH)
            git("commit", "-qm", "add request history")
            request.write_text(request.read_text(encoding="utf-8") + compiler_receipt.ANALYZER_REQUEST_LINE + "\n",
                                encoding="utf-8")
            git("commit", "-qam", "request analyzer")
            first_request = git("rev-parse", "HEAD")
            self.assertTrue(compiler_compare.analyzer_profile_requested(repo, first_request))
            request.write_text(request.read_text(encoding="utf-8") + "# unrelated fresh compiler request\n",
                                encoding="utf-8")
            git("commit", "-qam", "unrelated request")
            stale_request = git("rev-parse", "HEAD")
            self.assertFalse(compiler_compare.analyzer_profile_requested(repo, stale_request))
            request.write_text(request.read_text(encoding="utf-8") + compiler_receipt.ANALYZER_REQUEST_LINE + "\n",
                                encoding="utf-8")
            git("commit", "-qam", "explicit analyzer repeat")
            second_request = git("rev-parse", "HEAD")
            self.assertTrue(compiler_compare.analyzer_profile_requested(repo, second_request))
            request.write_bytes(request.read_bytes() + compiler_receipt.ANALYZER_REQUEST_LINE.encode("utf-8") +
                                b"\n\xff\n" +
                                (compiler_receipt.INLINE_ACCEPTANCE_REQUEST_LINE + "\n").encode("utf-8") * 2)
            git("add", compiler_receipt.ANALYZER_REQUEST_PATH)
            git("commit", "-qm", "analyzer request plus unrelated malformed byte")
            malformed_request = git("rev-parse", "HEAD")
            self.assertTrue(compiler_compare.analyzer_profile_requested(repo, malformed_request))
            self.assertTrue(compiler_compare.request_selector_increased_at_head(
                repo, malformed_request, compiler_receipt.INLINE_ACCEPTANCE_REQUEST_LINE))
            request.write_bytes(request.read_bytes() +
                                (compiler_receipt.ANALYZER_REQUEST_LINE + "\n").encode("utf-8") * 2)
            git("add", compiler_receipt.ANALYZER_REQUEST_PATH)
            git("commit", "-qm", "duplicate analyzer selectors")
            duplicate_request = git("rev-parse", "HEAD")
            requested, problem = compiler_compare.analyzer_profile_request_status(repo, duplicate_request)
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)
            with mock.patch.object(compiler_compare, "git", side_effect=subprocess.CalledProcessError(1, ["git"])):
                requested, problem = compiler_compare.analyzer_profile_request_status(repo, duplicate_request)
            self.assertFalse(requested)
            self.assertIn("could not prove", problem)

            real_run = subprocess.run
            def fail_tree(command, *args, **kwargs):
                if len(command) > 4 and command[3] == "ls-tree":
                    return subprocess.CompletedProcess(command, 128, b"", b"injected tree lookup failure")
                return real_run(command, *args, **kwargs)
            with mock.patch.object(compiler_compare.subprocess, "run", side_effect=fail_tree):
                self.assertIsNone(compiler_compare.request_selector_count(
                    repo, duplicate_request, compiler_receipt.ANALYZER_REQUEST_LINE))

            tree_listing = git("ls-tree", "-z", duplicate_request, "--", compiler_receipt.ANALYZER_REQUEST_PATH)
            self.assertTrue(tree_listing)
            def fail_blob_read(command, *args, **kwargs):
                if len(command) > 3 and command[3] == "show":
                    return subprocess.CompletedProcess(command, 128, b"", b"injected blob read failure")
                return real_run(command, *args, **kwargs)
            with mock.patch.object(compiler_compare.subprocess, "run", side_effect=fail_blob_read):
                self.assertIsNone(compiler_compare.request_selector_count(
                    repo, duplicate_request, compiler_receipt.ANALYZER_REQUEST_LINE))

    def test_v2_freshness_allows_historical_v1_but_rejects_mixed_or_duplicate_additions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            def git(*args: str) -> str:
                return fixture_git(repo, *args)
            git("init", "-q")
            git("config", "user.name", "Analyzer Test")
            git("config", "user.email", "analyzer@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            (repo / "tracked.c").write_text("int seed;\n", encoding="utf-8")
            history = (compiler_receipt.ANALYZER_REQUEST_LINE + "\n") * 4
            request.write_text("# historical profile requests\n" + history, encoding="utf-8")
            git("add", ".")
            git("commit", "-qm", "retain historical v1 request lines")
            parent = git("rev-parse", "HEAD")
            self.assertFalse(compiler_compare.analyzer_profile_requested(repo, parent))
            request.write_text(request.read_text(encoding="utf-8") +
                               compiler_receipt.ANALYZER_REQUEST_LINE_V2 + "\n", encoding="utf-8")
            git("commit", "-qam", "fresh v2 request with v1 history unchanged")
            v2_head = git("rev-parse", "HEAD")
            selected, problem = compiler_compare.analyzer_profile_request_selection(repo, v2_head)
            self.assertEqual((selected, problem), (compiler_receipt.ANALYZER_REQUEST_LINE_V2, ""))
            self.assertEqual(compiler_compare.request_selector_parent_deltas(
                repo, v2_head, compiler_receipt.ANALYZER_REQUEST_LINE), [0])

            request.write_text(request.read_text(encoding="utf-8") + compiler_receipt.ANALYZER_REQUEST_LINE + "\n" +
                               compiler_receipt.ANALYZER_REQUEST_LINE_V2 + "\n", encoding="utf-8")
            git("add", compiler_receipt.ANALYZER_REQUEST_PATH)
            git("commit", "-qm", "mixed fresh v1 and v2 selectors")
            mixed_head = git("rev-parse", "HEAD")
            selected, problem = compiler_compare.analyzer_profile_request_selection(repo, mixed_head)
            self.assertIsNone(selected)
            self.assertIn("only one versioned analyzer selector", problem)

            request.write_text(request.read_text(encoding="utf-8") +
                               (compiler_receipt.ANALYZER_REQUEST_LINE_V2 + "\n") * 2, encoding="utf-8")
            git("add", compiler_receipt.ANALYZER_REQUEST_PATH)
            git("commit", "-qm", "duplicate fresh v2 selectors")
            duplicate_head = git("rev-parse", "HEAD")
            selected, problem = compiler_compare.analyzer_profile_request_selection(repo, duplicate_head)
            self.assertIsNone(selected)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_fresh_selector_against_every_github_parent(self) -> None:
        head, first_parent, second_parent = "a" * 40, "b" * 40, "c" * 40
        selector = (compiler_receipt.ANALYZER_REQUEST_LINE + "\n").encode("utf-8")
        head_bytes = selector + selector

        class Api:
            def __init__(self, parent_counts=(1, 1), parent_inline_counts=(0, 0), remote_head=None):
                self.parent_counts = parent_counts
                self.parent_inline_counts = parent_inline_counts
                self.remote_head = head_bytes if remote_head is None else remote_head
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": first_parent}, {"sha": second_parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                parent_index = (first_parent, second_parent).index(revision) if revision != head else None
                content = self.remote_head if revision == head else (
                    selector * self.parent_counts[parent_index] +
                    (compiler_receipt.INLINE_ACCEPTANCE_REQUEST_LINE + "\n").encode("utf-8") *
                    self.parent_inline_counts[parent_index])
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        profile_receipt = analyzer_receipt()
        profile_receipt["analyzer_request_sha256"] = hashlib.sha256(head_bytes).hexdigest()
        expected = {"mode": "pull", "head": head}
        throughput = {"analyzer": {"files": {"request.txt": head_bytes}}}
        self.assertEqual(compiler_publish.verify_analyzer_request_freshness(Api(), expected, profile_receipt, throughput), "")
        inline_line = (compiler_receipt.INLINE_ACCEPTANCE_REQUEST_LINE + "\n").encode("utf-8")
        historical_inline_bytes = head_bytes + inline_line
        historical_inline_receipt = dict(profile_receipt,
                                         analyzer_request_sha256=hashlib.sha256(historical_inline_bytes).hexdigest())
        historical_inline_bundle = {"analyzer": {"files": {"request.txt": historical_inline_bytes}}}
        self.assertEqual(compiler_publish.verify_analyzer_request_freshness(
            Api(parent_inline_counts=(1, 1), remote_head=historical_inline_bytes), expected,
            historical_inline_receipt, historical_inline_bundle), "")
        fresh_inline_bytes = head_bytes + inline_line
        fresh_inline_receipt = dict(profile_receipt,
                                    analyzer_request_sha256=hashlib.sha256(fresh_inline_bytes).hexdigest())
        fresh_inline_bundle = {"analyzer": {"files": {"request.txt": fresh_inline_bytes}}}
        self.assertIn("cannot be combined", compiler_publish.verify_analyzer_request_freshness(
            Api(parent_inline_counts=(0, 0), remote_head=fresh_inline_bytes), expected,
            fresh_inline_receipt, fresh_inline_bundle))
        duplicate_inline_bytes = head_bytes + inline_line * 2
        duplicate_inline_receipt = dict(profile_receipt,
                                        analyzer_request_sha256=hashlib.sha256(duplicate_inline_bytes).hexdigest())
        duplicate_inline_bundle = {"analyzer": {"files": {"request.txt": duplicate_inline_bytes}}}
        self.assertIn("cannot be combined", compiler_publish.verify_analyzer_request_freshness(
            Api(parent_inline_counts=(0, 0), remote_head=duplicate_inline_bytes), expected,
            duplicate_inline_receipt, duplicate_inline_bundle))
        stale = Api(parent_counts=(2, 1))
        self.assertIn("not freshly added", compiler_publish.verify_analyzer_request_freshness(
            stale, expected, profile_receipt, throughput))
        tampered = {"analyzer": {"files": {"request.txt": selector}}}
        self.assertIn("do not match", compiler_publish.verify_analyzer_request_freshness(
            Api(), expected, profile_receipt, tampered))
        freshness_error = compiler_publish.verify_analyzer_request_freshness(
            stale, expected, profile_receipt, throughput)
        self.assertIn("not freshly added", freshness_error)
        decision_summary = {"inventory": {"selected_rows": 182}}
        with mock.patch.object(compiler_publish, "validate_analyzer_bundle", return_value=[]):
            decision = compiler_publish.decide(profile_receipt["identity"], True, "success", profile_receipt,
                                               decision_summary, "", {"analyzer": {"files": {"request.txt": head_bytes}}},
                                               extra_reasons=[freshness_error])
        self.assertEqual(decision[0], "failure")
        self.assertTrue(any("not freshly added" in reason for reason in decision[2]), decision[2])

        ordinary_bytes = b"# no active analyzer request\n"
        legacy = dict(profile_receipt, profile=copy.deepcopy(compiler_receipt.PROFILE))
        legacy.pop("analyzer_request_line", None)
        legacy.pop("analyzer_profile", None)
        legacy_bundle = {"analyzer": {"files": {}}}
        self.assertEqual(compiler_publish.verify_analyzer_request_freshness(
            Api(parent_counts=(0, 0), remote_head=ordinary_bytes), expected, legacy, legacy_bundle), "")
        self.assertIn("does not match the legacy", compiler_publish.verify_analyzer_request_freshness(
            Api(parent_counts=(1, 1), remote_head=head_bytes), expected, legacy, legacy_bundle))

        duplicate_analyzer_bytes = selector * 3
        duplicate_analyzer_receipt = dict(profile_receipt,
                                          analyzer_request_sha256=hashlib.sha256(duplicate_analyzer_bytes).hexdigest())
        duplicate_analyzer_bundle = {"analyzer": {"files": {"request.txt": duplicate_analyzer_bytes}}}
        self.assertIn("not freshly added once", compiler_publish.verify_analyzer_request_freshness(
            Api(parent_counts=(1, 1), remote_head=duplicate_analyzer_bytes), expected,
            duplicate_analyzer_receipt, duplicate_analyzer_bundle))
        malformed_bytes = selector + b"\xff\n"
        malformed_receipt = dict(profile_receipt,
                                  analyzer_request_sha256=hashlib.sha256(malformed_bytes).hexdigest())
        malformed_bundle = {"analyzer": {"files": {"request.txt": malformed_bytes}}}
        self.assertIn("not UTF-8", compiler_publish.verify_analyzer_request_freshness(
            Api(parent_counts=(0, 0), remote_head=malformed_bytes), expected,
            malformed_receipt, malformed_bundle))

        class ReadFailure:
            def request(self, path: str, data: dict | None = None) -> object:
                raise OSError("injected GitHub commit read failure")
        self.assertIn("commit read failed", compiler_publish.verify_analyzer_request_freshness(
            ReadFailure(), expected, legacy, legacy_bundle))

    def test_publisher_v2_binds_selector_marker_head_bytes_and_every_parent(self) -> None:
        head, first_parent, second_parent = "a" * 40, "b" * 40, "c" * 40
        v1 = (compiler_receipt.ANALYZER_REQUEST_LINE + "\n").encode("utf-8")
        v2 = (compiler_receipt.ANALYZER_REQUEST_LINE_V2 + "\n").encode("utf-8")
        history = b"# retained requests\n" + v1 * 4

        class Api:
            def __init__(self, head_bytes: bytes, parent_bytes: tuple[bytes, bytes] = (history, history)):
                self.contents = {head: head_bytes, first_parent: parent_bytes[0], second_parent: parent_bytes[1]}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": first_parent}, {"sha": second_parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = self.contents[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        head_bytes = history + v2
        profile_receipt = analyzer_receipt(request_line=compiler_receipt.ANALYZER_REQUEST_LINE_V2)
        profile_receipt["analyzer_request_sha256"] = hashlib.sha256(head_bytes).hexdigest()
        expected = {"mode": "pull", "head": head}
        bundle = {"analyzer": {"files": {"request.txt": head_bytes}}}
        self.assertEqual(compiler_publish.verify_analyzer_request_freshness(
            Api(head_bytes), expected, profile_receipt, bundle), "")

        mixed_bytes = head_bytes + v1
        mixed_receipt = dict(profile_receipt,
                             analyzer_request_sha256=hashlib.sha256(mixed_bytes).hexdigest())
        mixed_bundle = {"analyzer": {"files": {"request.txt": mixed_bytes}}}
        self.assertIn("other recognized analyzer selector count changed",
                      compiler_publish.verify_analyzer_request_freshness(
                          Api(mixed_bytes), expected, mixed_receipt, mixed_bundle))

        duplicate_bytes = history + v2 * 2
        duplicate_receipt = dict(profile_receipt,
                                 analyzer_request_sha256=hashlib.sha256(duplicate_bytes).hexdigest())
        duplicate_bundle = {"analyzer": {"files": {"request.txt": duplicate_bytes}}}
        self.assertIn("not freshly added once", compiler_publish.verify_analyzer_request_freshness(
            Api(duplicate_bytes), expected, duplicate_receipt, duplicate_bundle))

        wrong_marker = dict(profile_receipt, analyzer_request_line=compiler_receipt.ANALYZER_REQUEST_LINE)
        self.assertIn("does not match its exact selector", compiler_publish.verify_analyzer_request_freshness(
            Api(head_bytes), expected, wrong_marker, bundle))

    def test_superseded_analyzer_profile_does_not_become_failed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "candidate"
            request = candidate / "benchmarks/9700x/compiler-compare.request"
            request.parent.mkdir(parents=True)
            request.write_text(compiler_receipt.ANALYZER_REQUEST_LINE + "\n", encoding="utf-8")
            work, evidence = root / "work", root / "evidence"
            work.mkdir()
            evidence.mkdir()
            current = {"state": "superseded", "identity": dict(EXPECTED),
                       "reasons": ["pull request head moved before measurement"], "timings": {}}
            args = argparse.Namespace(mode="pull", base="b" * 40, head="a" * 40)
            summaries = []
            with mock.patch.object(compiler_compare, "analyzer_clang_provenance",
                                   return_value={"schema": "buster-analyzer-clang-provenance-v1"}), \
                 mock.patch.object(compiler_compare, "scaling_requested", return_value=False), \
                 mock.patch.object(compiler_compare, "run") as run:
                compiler_compare.measure_analyzer_profile(args, candidate, work, evidence, current, summaries)
            self.assertEqual(current["state"], "superseded")
            run.assert_not_called()

    def test_malformed_requested_selector_fails_closed_before_driver_build(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "candidate"
            request = candidate / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_bytes(compiler_receipt.ANALYZER_REQUEST_LINE.encode("utf-8") + b"\n\xff\n")
            work, evidence = root / "work", root / "evidence"
            work.mkdir()
            evidence.mkdir()
            current = {"state": "pending", "identity": dict(EXPECTED), "reasons": [], "timings": {}}
            args = argparse.Namespace(mode="pull", base="b" * 40, head="a" * 40)
            with mock.patch.object(compiler_compare, "analyzer_clang_provenance",
                                   return_value={"schema": "buster-analyzer-clang-provenance-v1"}), \
                 mock.patch.object(compiler_compare, "scaling_requested", return_value=False), \
                 mock.patch.object(compiler_compare, "build_analyzer_driver") as build_driver:
                compiler_compare.measure_analyzer_profile(args, candidate, work, evidence, current, [])
            self.assertEqual(current["state"], "failed")
            self.assertTrue(any("request is not UTF-8" in item for item in current["reasons"]), current["reasons"])
            build_driver.assert_not_called()

    def test_unprovable_or_duplicate_request_does_not_fall_back_to_legacy_build(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "candidate"
            candidate.mkdir()
            work, evidence, bins = root / "work", root / "evidence", root / "bins"
            for directory in (work, evidence, bins):
                directory.mkdir()
            request_problem = "analyzer selector must be added exactly once relative to every head parent"
            current = {"state": "failed", "reasons": [], "timings": {}, "binaries": {}, "lab": {}}
            args = argparse.Namespace(mode="pull", base="b" * 40, head="a" * 40,
                                      analyzer_profile_requested=False,
                                      analyzer_profile_request_problem=request_problem,
                                      lab=root / "uarch_lab.py")
            with mock.patch.object(compiler_compare, "build") as build, \
                 mock.patch.object(compiler_compare, "run") as run:
                compiler_compare.measure(args, candidate, work, evidence, bins, evidence / "build.log", current, [])
            self.assertIn(request_problem, current["reasons"])
            build.assert_not_called()
            run.assert_not_called()



class FrozenClosureTest(unittest.TestCase):
    def fixture(self):
        current = receipt()
        base, tree = current["identity"]["base"], current["identity"]["base_tree"]
        rows = []
        def row(scope, path, digest=A256):
            rows.append(f"{scope}\tF\t493\t123\t0\t1\t{digest}\t{path}")
        for path in ("build.c", "build.sh", "tools/bootstrap_driver.sh"):
            row("source", path)
        for path in ("CMakeCache.txt", "Release/ide", "throughput-tools/throughput"):
            row("build", path)
        configuration = "c" * 64
        artifact = f"posix/{configuration}/driver"
        row("bootstrap", artifact)
        row("bootstrap", artifact + ".complete")
        bindings = [f"binding\tbootstrap_config\t{configuration}", f"binding\tbootstrap_artifact\t{artifact}",
                    f"binding\tbootstrap_marker\t{artifact}.complete"]
        for key in ("CMAKE_C_COMPILER", "CMAKE_LINKER", "CMAKE_MAKE_PROGRAM", "clang", "cmake", "ninja", "tcc"):
            row("tool", key)
            bindings.append(f"binding\t{key}\t/usr/bin/{key}")
        bindings.append("binding\tresource\t/usr/lib/clang/include")
        row("resource", "stddef.h")
        manifest = ("\n".join(["BUSTER_COMPILER_CLOSURE_V1", "root\t/checkout", f"base\t{base}", f"tree\t{tree}",
                               *rows, *bindings, f"END\t{len(rows)}\t{len(rows)}"]) + "\n").encode()
        record = {"schema": "buster-compiler-closure-v1", "policy": "snapshot-v1", "state": "complete",
                  "base": base, "base_tree": tree, "root_sha256": hashlib.sha256(b"/checkout").hexdigest(),
                  "manifest_sha256": hashlib.sha256(manifest).hexdigest(), "duration_us": 123, "harness_preparation_us": 23, "harness_sha256": A256,
                  "bootstrap_artifact_sha256": A256, "bootstrap_marker_sha256": A256,
                  "ownership_schema": "buster-native-qualification-supervisor-v1", "cleanup_proven": True,
                  "cleanup_us": 12, "cleanup_waves": 1, "cleanup_signalled": 0, "cleanup_reaped": 0}
        current["closure"] = {"policy": "snapshot-v1", "fallback": None,
                              "snapshot": dict(record, operation="snapshot"), "restore": dict(record, operation="restore"),
                              "verify": dict(record, operation="verify")}
        return current, {"snapshot": manifest, "restore": manifest, "verify": manifest}

    def test_unproven_native_cleanup_retains_work_and_aborts_bridge(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / "evidence"
            evidence.mkdir()
            work = root / "work"
            work.mkdir()
            arguments = argparse.Namespace(base=EXPECTED["base"], base_tree=EXPECTED["base_tree"])
            record = {"state": "failed", "cleanup_proven": False, "harness_preparation_us": 0}
            receipt = {"closure": {"policy": "snapshot-v1"}, "timings": {}}
            with mock.patch.object(compiler_compare, "run", return_value=1), \
                    mock.patch.object(compiler_compare, "read_exported_json", return_value=(record, "")):
                with self.assertRaises(compiler_compare.ClosureCleanupUncertain):
                    compiler_compare.closure_phase(arguments, root / "candidate", work, evidence, receipt, "snapshot")
            self.assertIs(receipt["cleanup_proven"], False)
            self.assertEqual(receipt["work_retained"], str(work))
            self.assertTrue((evidence / "cleanup-uncertain").is_file())
            self.assertTrue(work.is_dir())


    def test_snapshot_policy_cannot_be_stripped_to_legacy(self):
        current, bundle = self.fixture()
        current["preparation_policy"] = "legacy-rebuild"
        self.assertTrue(compiler_receipt.validate_closure(current, bundle))
        current["preparation_policy"] = "snapshot-v1"
        current.pop("closure")
        self.assertTrue(compiler_receipt.validate_closure(current, bundle))
        current.pop("preparation_policy")
        self.assertTrue(compiler_receipt.validate_closure(current, bundle, expected_policy="snapshot-v1"))
        self.assertEqual(compiler_receipt.validate_closure(current, {}, expected_policy="legacy-rebuild"), [])

    def test_native_producer_and_consumer_manifest_identity_is_replayed(self):
        current, bundle = self.fixture()
        self.assertEqual(compiler_receipt.validate_closure(current, bundle), [])
        complete_corpus = corpus()
        complete_corpus["closure"] = bundle
        conclusion, _, reasons = compiler_publish.decide(dict(EXPECTED), True, "success", current, summary(), "",
                                                         complete_corpus, True)
        self.assertEqual((conclusion, reasons), ("success", []))

    def test_missing_tampered_source_root_toolchain_configuration_and_fallback_fail(self):
        current, bundle = self.fixture()
        cases = []
        for operation in ("snapshot", "restore", "verify"):
            for key, value in (("base", "0" * 40), ("base_tree", "0" * 40), ("root_sha256", B256),
                               ("state", "failed"), ("manifest_sha256", B256), ("duration_us", -1),
                               ("harness_sha256", B256), ("bootstrap_marker_sha256", B256),
                               ("bootstrap_artifact_sha256", B256), ("harness_preparation_us", None),
                               ("harness_preparation_us", -1), ("harness_preparation_us", True), ("cleanup_proven", False), ("cleanup_signalled", 1),
                               ("cleanup_reaped", 1), ("cleanup_us", None)):
                altered = copy.deepcopy(current)
                altered["closure"][operation][key] = value
                cases.append((altered, bundle))
        changed = dict(bundle, restore=bundle["restore"] + b"candidate-generated-header")
        cases.extend(((current, {}), (current, changed)))
        fallback = copy.deepcopy(current)
        fallback["closure"]["fallback"] = "silent candidate repair"
        cases.append((fallback, bundle))
        invalid = bundle["snapshot"].replace(b"CMAKE_LINKER", b"missing_linker")
        adjusted = copy.deepcopy(current)
        for operation in ("snapshot", "restore", "verify"):
            adjusted["closure"][operation]["manifest_sha256"] = hashlib.sha256(invalid).hexdigest()
        cases.append((adjusted, dict(snapshot=invalid, restore=invalid, verify=invalid)))
        binary = copy.deepcopy(current)
        binary["binaries"]["baseline"]["sha256"] = B256
        cases.append((binary, bundle))
        for index, (record, raw) in enumerate(cases):
            with self.subTest(case=index):
                self.assertTrue(compiler_receipt.validate_closure(record, raw))

    def test_legacy_receipts_do_not_synthesize_a_snapshot(self):
        self.assertEqual(compiler_receipt.validate_closure(receipt(), None), [])


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
        self.assertEqual(compiler_receipt.THROUGHPUT_PROFILE["modes"], list(MODES_ALL))
        def mutated(change) -> dict:
            data = corpus()
            change(data["summary"])
            return data
        def rows(summary: dict) -> list:
            return summary["comparisons"]
        def set_counts(summary: dict, regressions: int, inconclusive: int) -> None:
            summary["confirmed_regressions"], summary["inconclusive_cases"] = regressions, inconclusive
        cases = {
            "only fast rows": (lambda s: s.update(comparisons=[r for r in rows(s) if r["name"].endswith("/fast")]),
                               "miss 6 of 12"),
            "bare workload names": (lambda s: s.update(comparisons=[{"name": r["name"].split("/")[0]} for r in rows(s)[::2]]),
                                    "miss"),
            "one missing cell": (lambda s: rows(s).pop(), "miss 1 of 12"),
            "one allocator missing": (lambda s: s.update(comparisons=[r for r in rows(s) if not r["name"].endswith("/fast")]),
                                      "miss 6 of 12"),
            "duplicated": (lambda s: s.update(comparisons=rows(s) + copy.deepcopy(rows(s))), "repeat"),
            "wrong mode": (lambda s: rows(s)[0].update(name="tiny_startup/turbo"), "outside the profile"),
            "retired mode": (lambda s: rows(s).append(dict(copy.deepcopy(rows(s)[0]), name="tiny_startup/none")),
                             "outside the profile"),
            "historical v1 cells": (lambda s: rows(s).extend(dict(copy.deepcopy(r), name=r["name"].split("/")[0] + "/mir-stack")
                                                            for r in rows(s)[::2]), "outside the profile"),
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
        historical_profile = receipt()
        historical_profile["throughput_profile"] = dict(historical_profile["throughput_profile"],
                                                        name="throughput-corpus-v1", modes=["none", "mir-stack", "fast", "quality"])
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
            "historical four-mode corpus profile": {"receipt": historical_profile},
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

    def test_metric_table_states_units_and_ratio_direction(self) -> None:
        data = summary()
        data["metrics"] = {
            "wall": {"unit": "s", "a_median": 0.0123, "b_median": 0.0119, "ratio": 0.9675, "ci_low": 0.95, "ci_high": 0.98,
                     "outcome": "faster"},
            "instructions": {"unit": "count", "a_median": 16934571004, "b_median": 16934571005, "ratio": 1.0, "ci_low": 1.0,
                             "ci_high": 1.0, "outcome": "slower"},
            "cycles": {"unit": "count", "a_median": None, "b_median": None, "ratio": None, "ci_low": None, "ci_high": None,
                       "outcome": "no data"},
            "peak_rss": {"unit": "bytes", "a_median": 877584384, "b_median": 877584385, "ratio": 1.0, "ci_low": 1.0, "ci_high": 1.0,
                         "outcome": "slower"},
            "task_clock": {"unit": "ms", "a_median": 1.0, "b_median": 1.0, "ratio": 1.0, "ci_low": 1.0, "ci_high": 1.0,
                           "outcome": "faster"},
        }
        report = compiler_receipt.render(receipt(), data, "success", [])
        self.assertIn("A = baseline, B = candidate", report)
        self.assertIn("confidence interval of the dimensionless B/A ratio", report)
        self.assertIn("| Metric | Unit | A (baseline) median | B (candidate) median | B/A ratio | 95% CI of B/A | Outcome |", report)
        self.assertIn("| wall time (harness span) | s | 0.0123 | 0.0119 | 0.9675 | [0.9500, 0.9800] | faster |", report)
        self.assertIn("| peak RSS | bytes | 8.77584e+08 | 8.77584e+08 |", report)
        self.assertIn("| cycles | count | NA | NA | NA | [NA, NA] | no data |", report)
        self.assertIn("| task-clock | s | NA | NA | NA | [NA, NA] | rejected: unit 'ms', expected 's' |", report)

    def test_only_recovery_of_a_legacy_receipt_skips_the_corpus(self) -> None:
        legacy = receipt()
        del legacy["throughput_profile"]
        self.assertEqual(self.decide(receipt=legacy, throughput=None, require_throughput=False)[0], "success")
        # A receipt that names the profile is always checked.
        self.assertEqual(self.decide(throughput=None, require_throughput=False)[0], "failure")

    def test_superseded_group_is_neutral_and_transfers_nothing(self) -> None:
        conclusion, title, _ = self.decide(receipt=receipt("superseded"), summary=None)
        self.assertEqual((conclusion, title), ("neutral", "Superseded before measurement"))

    def test_analyzer_profile_is_pull_only_and_revalidated_from_raw_bundle(self) -> None:
        expected = dict(EXPECTED, mode="pull", ref="refs/pull/7/head")
        profile_receipt = analyzer_receipt()
        profile_summary = {"status": "complete", "inventory": {"selected_rows": 182,
                            "excluded_rows": 0, "baseline_unique_executions": 182,
                            "candidate_unique_executions": 135, "candidate_alias_rows": 47}}
        raw_bundle = {"summary": profile_summary, "files": {"request.txt": b"fixed request"}}
        analyzer_throughput = {"analyzer": raw_bundle}
        with mock.patch.object(compiler_publish, "validate_analyzer_bundle", return_value=[]) as validate:
            outcome = compiler_publish.decide(expected, True, "success", profile_receipt, profile_summary,
                                              "", analyzer_throughput)
        self.assertEqual(outcome[0], "success")
        self.assertIn("full analyzer inventory, 182 rows", outcome[1])
        self.assertEqual(outcome[2], [])
        validate.assert_called_once_with(profile_receipt, profile_summary, raw_bundle)
        report = compiler_publish.commit_report(profile_receipt, profile_summary, outcome[0], outcome[1], [], "Evidence")
        self.assertIn("Selected / excluded rows | 182 / 0", report)
        self.assertIn("no speedup or regression verdict", report)
        self.assertNotIn("Wall time B/A", report)

        wrong_mode = dict(profile_receipt, mode="main")
        wrong_mode["identity"] = dict(wrong_mode["identity"], mode="main", ref="refs/heads/main")
        self.assertEqual(compiler_publish.decide(expected, True, "success", wrong_mode, profile_summary,
                                                 "", analyzer_throughput)[0], "failure")
        mixed_profile = dict(profile_receipt, throughput_profile=compiler_receipt.THROUGHPUT_PROFILE)
        self.assertEqual(compiler_publish.decide(expected, True, "success", mixed_profile, profile_summary,
                                                 "", analyzer_throughput)[0], "failure")
        missing = compiler_publish.decide(expected, True, "success", profile_receipt, profile_summary, "", None)
        self.assertEqual(missing[0], "failure")
        self.assertTrue(missing[2])

    def test_analyzer_superseded_receipt_stays_neutral_without_raw_measurement(self) -> None:
        expected = dict(EXPECTED, mode="pull", ref="refs/pull/7/head")
        superseded = analyzer_receipt("superseded")
        superseded["reasons"] = ["pull request head moved before measurement"]
        with mock.patch.object(compiler_publish, "validate_analyzer_bundle") as validate:
            result = compiler_publish.decide(expected, True, "success", superseded, None, "", None)
        self.assertEqual(result[:2], ("neutral", "Superseded before measurement"))
        validate.assert_not_called()


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


def machine_fields(**change: object) -> dict:
    """A #2758 record as the C collector writes it: every field carries value, status and reason."""
    unknown = {"value": "unknown", "status": "unknown", "reason": "not exposed by this OS, unsupported or probe failed"}
    available = {"schema": "buster-machine-specifications-v1", "run_id": EXPECTED["run_id"],
                 "run_attempt": EXPECTED["run_attempt"], "job": "compare", "cpu_model": HOST["cpu_model"],
                 "machine_arch": "x86_64", "cpu_logical_online": "16", "kernel_release": "6.8.0"}
    record = {key: {"value": value, "status": "available", "reason": ""} for key, value in available.items()}
    record["cpu_vendor"] = dict(unknown)
    record["cpu_affinity"] = {"value": "unavailable", "status": "unavailable", "reason": "probe refused"}
    record.update(change)
    return {key: value for key, value in record.items() if value is not None}


def machine_bytes(**change: object) -> bytes:
    return json.dumps(machine_fields(**change)).encode()


class MachineRecordTest(unittest.TestCase):
    """The publisher's consumption of the retained #2758 record (#2761)."""

    def check(self, raw: object, mode: str = "main", run_id: str = "92", attempt: str = "1",
              current: object = None, require: bool = True) -> tuple[list[str], dict]:
        return compiler_publish.machine_record(raw, receipt() if current is None else current, mode, run_id, attempt,
                                               require=require)

    def test_matching_record_passes_and_keeps_unavailable_facts_explicit(self) -> None:
        problems, facts = self.check(machine_bytes())
        self.assertEqual(problems, [])
        self.assertEqual(facts["cpu_model"], HOST["cpu_model"])
        self.assertEqual(facts["cpu_vendor"], "unknown (not exposed by this OS, unsupported or probe failed)")
        self.assertEqual(facts["cpu_affinity"], "unavailable (probe refused)")
        self.assertEqual(facts["memory_total_bytes"], "absent from the record")
        pull = machine_bytes(job={"value": "compare-pull", "status": "available", "reason": ""})
        self.assertEqual(self.check(pull, mode="pull")[0], [])

    def test_absent_record_fails_unless_recovering_a_past_attempt(self) -> None:
        self.assertIn("missing", self.check(None)[0][0])
        self.assertEqual(self.check(None, require=False), ([], {}))
        self.assertTrue(self.check(machine_bytes(cpu_model={"value": "Intel", "status": "available", "reason": ""}),
                                   require=False)[0])

    def test_malformed_record_fails(self) -> None:
        for raw in (b"{not json", b"\xff", b"[]", b"{}", b'{"a": 1, "a": 1}', b'{"cpu_model": "AMD Ryzen 7 9700X"}',
                    json.dumps({"cpu_model": {"value": "x", "status": "guessed", "reason": ""}}).encode(),
                    json.dumps({"cpu_model": {"value": 1, "status": "available", "reason": ""}}).encode(),
                    json.dumps(dict(machine_fields(), job={"value": "compare", "status": "available"})).encode(),
                    "AMD Ryzen 7 9700X"):
            with self.subTest(raw=raw):
                problems, facts = self.check(raw)
                self.assertTrue(problems)
                self.assertIn("malformed", problems[0])
                self.assertEqual(facts, {})

    def test_required_facts_must_be_available_and_are_never_fabricated(self) -> None:
        for key in compiler_publish.MACHINE_REQUIRED:
            for value in (None, {"value": "unknown", "status": "unknown", "reason": "probe failed"},
                          {"value": "AMD Ryzen 7 9700X", "status": "partial", "reason": "bounded field truncated"}):
                with self.subTest(key=key, value=value):
                    problems = self.check(machine_bytes(**{key: value}))[0]
                    self.assertEqual(problems, [f"machine-specification record lacks required host fact {key}: "
                                                + ("absent from the record" if value is None else
                                                   f"{value['status']} ({value['reason']})")])

    def test_non_9700x_record_fails(self) -> None:
        for model in ("AMD EPYC 9B14", "AMD Ryzen 9 9950X 16-Core Processor", "Intel(R) Xeon(R) Platinum 8370C"):
            with self.subTest(model=model):
                fields = {"cpu_model": {"value": model, "status": "available", "reason": ""}}
                problems = self.check(machine_bytes(**fields),
                                      current=dict(receipt(), host=dict(HOST, cpu_model=model)))[0]
                self.assertEqual(len(problems), 1)
                self.assertIn("is not the approved Zen 5 host", problems[0])
        arch = self.check(machine_bytes(machine_arch={"value": "aarch64", "status": "available", "reason": ""}))[0]
        self.assertEqual(arch, ["machine-specification record architecture 'aarch64' is not x86_64"])

    def test_record_and_receipt_must_name_the_same_model(self) -> None:
        other = "AMD Ryzen 7 9700X 8-Core Processor (other)"
        for current in (dict(receipt(), host=dict(HOST, cpu_model=other)), dict(receipt(), host={}),
                        dict(receipt(), host=None)):
            with self.subTest(host=current["host"]):
                problems = self.check(machine_bytes(), current=current)[0]
                self.assertEqual(len(problems), 1)
                self.assertIn("differs from the receipt's observed CPU", problems[0])

    def test_stale_run_attempt_or_job_identity_fails(self) -> None:
        for label, change in (("an earlier run", {"run_id": "91"}), ("an earlier attempt", {"attempt": "2"}),
                              ("another job", {"mode": "pull"}), ("an unknown mode", {"mode": "queue"})):
            with self.subTest(label=label):
                problems = self.check(machine_bytes(), **change)[0]
                self.assertEqual(len(problems), 1)
                self.assertIn("is not this attempt's", problems[0])
        schema = {"value": "buster-machine-specifications-v0", "status": "available", "reason": ""}
        self.assertIn("schema", self.check(machine_bytes(schema=schema))[0][0])

    def test_publication_fails_through_the_retained_artifact(self) -> None:
        base = {"receipt.json": json.dumps(receipt()), "lab/summary.json": json.dumps(summary()),
                "throughput/summary.json": json.dumps(corpus()["summary"]),
                "throughput/metadata.json": json.dumps(corpus()["metadata"])}
        stale = machine_bytes(run_attempt={"value": "2", "status": "available", "reason": ""})
        for members, expected in ((base, "failure"), (dict(base, **{compiler_publish.MACHINE_RECORD: machine_bytes()}), "success"),
                                  (dict(base, **{compiler_publish.MACHINE_RECORD: stale}), "failure"),
                                  (dict(base, **{compiler_publish.MACHINE_RECORD: b"{"}), "failure")):
            with self.subTest(members=sorted(members), expected=expected):
                got = compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
                problems = self.check(got[4]["machine"])[0]
                decision = compiler_publish.decide(dict(EXPECTED), True, "success", got[0], got[1], "", got[4],
                                                   extra_reasons=problems)
                self.assertEqual(decision[0], expected)
                self.assertEqual(decision[2], problems)
        oversized = dict(base, **{compiler_publish.MACHINE_RECORD: b" " * (compiler_publish.MEMBER_LIMIT + 1)})
        got = compiler_publish.read_evidence(FakeApi(archive(oversized)), "92", "buster-9700x-compiler-x-1")
        self.assertIn("malformed", self.check(got[4]["machine"])[0][0])

    def test_owned_main_layout_keeps_the_record_at_the_same_path(self) -> None:
        files = compiler_publish.main_archive_files(archive({"evidence/receipt.json": "{}",
                                                             "evidence/" + compiler_publish.MACHINE_RECORD: machine_bytes()}))
        self.assertEqual(files[compiler_publish.MACHINE_RECORD], machine_bytes())


def contents_record(text: str) -> dict:
    raw = text.encode()
    return {"type": "file", "encoding": "base64", "size": len(raw), "content": base64.b64encode(raw).decode()}


class MachineRecordRequiredTest(unittest.TestCase):
    """Recovery waives the record only for a bench workflow revision that never retained it."""

    REVISION = "7" * 40
    WORKFLOW = Path(__file__).resolve().parents[2] / ".github" / "workflows" / "9700x-direct-bench.yml"

    def required(self, answer: object, revision: str = REVISION) -> tuple[bool, list[str]]:
        paths: list[str] = []

        class Contents:
            def request(self, path: str, data: dict | None = None) -> object:
                paths.append(path)
                if isinstance(answer, Exception):
                    raise answer
                return answer
        return compiler_publish.machine_record_required(Contents(), revision), paths

    def test_current_workflow_retains_the_record_in_every_compare_upload(self) -> None:
        text = self.WORKFLOW.read_text(encoding="utf-8")
        self.assertEqual(text.count(compiler_publish.MACHINE_RETENTION_STEP), 3)
        self.assertEqual(self.required(contents_record(text))[0], True)

    def test_only_a_readable_legacy_revision_waives_the_record(self) -> None:
        legacy = self.WORKFLOW.read_text(encoding="utf-8").replace(compiler_publish.MACHINE_RETENTION_STEP, "x\n")
        required, paths = self.required(contents_record(legacy))
        self.assertEqual(required, False)
        self.assertEqual(urllib.parse.urlsplit(paths[0]).path, "/contents/.github/workflows/9700x-direct-bench.yml")
        self.assertEqual(urllib.parse.parse_qs(urllib.parse.urlsplit(paths[0]).query), {"ref": [self.REVISION]})
        for label, answer in (("absent file", urllib.error.HTTPError("u", 404, "absent", {}, None)),
                              ("server error", urllib.error.HTTPError("u", 502, "bad gateway", {}, None)),
                              ("network", OSError("reset")), ("malformed record", {"type": "dir"}),
                              ("size mismatch", dict(contents_record(legacy), size=1))):
            with self.subTest(label=label):
                self.assertEqual(self.required(answer)[0], True)
        self.assertEqual(self.required(contents_record(legacy), revision="main"), (True, []))


class SealingRequiredTest(unittest.TestCase):
    """Recovery waives sealed raw evidence only for a producer revision that never sealed (#2918)."""

    REVISION = "7" * 40
    PRODUCER = Path(__file__).resolve().with_name("compiler_compare.py")

    def required(self, answer: object, revision: str = REVISION) -> tuple[bool, list[str]]:
        paths: list[str] = []

        class Contents:
            def request(self, path: str, data: dict | None = None) -> object:
                paths.append(path)
                if isinstance(answer, Exception):
                    raise answer
                return answer
        return compiler_publish.sealing_required(Contents(), revision), paths

    def test_the_current_producer_seals_and_only_a_readable_legacy_one_waives_it(self) -> None:
        text = self.PRODUCER.read_text(encoding="utf-8")
        self.assertIn(compiler_publish.SEALING_PRODUCER_MARKER.decode(), text)
        self.assertEqual(self.required(contents_record(text))[0], True)
        legacy = text.replace(compiler_publish.SEALING_PRODUCER_MARKER.decode(), "def unsealed(")
        required, paths = self.required(contents_record(legacy))
        self.assertEqual(required, False)
        self.assertEqual(urllib.parse.urlsplit(paths[0]).path, "/contents/tools/bench_direct/compiler_compare.py")
        for label, answer in (("absent file", urllib.error.HTTPError("u", 404, "absent", {}, None)),
                              ("server error", urllib.error.HTTPError("u", 502, "bad gateway", {}, None)),
                              ("network", OSError("reset")), ("malformed record", {"type": "dir"})):
            with self.subTest(label=label):
                self.assertEqual(self.required(answer)[0], True)
        self.assertEqual(self.required(contents_record(legacy), revision="main"), (True, []))

    def test_a_required_seal_missing_from_a_measured_attempt_is_a_refusal(self) -> None:
        unsealed = compiler_publish.verify_evidence_bundle({}, {}, compiler_receipt.THROUGHPUT_PROFILE, "1")
        self.assertEqual(unsealed, {"state": "unsealed"})
        self.assertEqual(compiler_publish.evidence_bundle_note(unsealed), "")
        self.assertIn("is required on this route", compiler_publish.evidence_bundle_problems({}, unsealed)[0])
        legacy = compiler_publish.verify_evidence_bundle({}, {}, compiler_receipt.THROUGHPUT_PROFILE, "1", required=False)
        self.assertEqual((legacy, compiler_publish.evidence_bundle_problems({}, legacy)), ({"state": "legacy-unsealed"}, []))


def raw_lab(directory: Path, pairs: int = 10) -> dict[str, bytes]:
    """A raw compiler-compare-v1 lab and its summary from the trusted uarch_lab statistics, as `lab/` members."""
    from sampling_qualification_receipt import _lab
    directory.mkdir(parents=True)
    write = lambda name, text: ((directory / name).parent.mkdir(parents=True, exist_ok=True),  # noqa: E731
                                (directory / name).write_text(text))
    variants = {key: {"role": role, "ide": f"/work/bin/ide-{key}", "sha256": key * 64, "size_bytes": 4096}
                for key, role in _lab.VARIANTS}
    config = {"command": "IDE cc -o OUT", "repo_root": "/work/candidate", "cpu": 2, "perf": "perf", "pairs": None,
              "target_minutes": 10, "warmups": 1, "seed": _lab.DEFAULT_SEED, "profile_steps": [], "sudo": False,
              "require_identical_output": False, "extra": [], "canonical_inline_pair": False,
              "extra_by_variant": {"a": [], "b": []}, "fresh_copy": True, "min_effect_percent": _lab.DEFAULT_MIN_EFFECT}
    write("compare.json", json.dumps({"version": 1, "mode": "compare", "config": config, "variants": variants,
                                      "plan": {"pairs": pairs, "reason": "--target-minutes 10: fixture", "order": "ABBA",
                                               "fresh_copy": True},
                                      "steps": {name: {"status": "ok"} for name in ("env", "prepare", "timed")},
                                      "phase_metrics": {"enabled": True, "reason": "both compilers support -fmetrics-out"}}))
    write("a/env/env.json", json.dumps({"cpu_model": "AMD Ryzen 7 9700X 8-Core Processor"}))
    for key, usable in (("a", True), ("b", False)):
        write(f"{key}/lab.json", json.dumps({"capabilities": {"perf_stat": {"usable": usable, "reason": f"probe {key}"}},
                                             "collection": {"metrics_out": True}}))
    records = []
    for pair in range(1, pairs + 1):
        for key in ("ab" if pair % 2 else "ba"):
            seconds = 1.0 + 0.01 * pair + (0.002 if key == "b" else 0.0)
            record = {"pair": pair, "order": "AB" if pair % 2 else "BA", "variant": key, "exit": 0, "identical": True,
                      "span_s": seconds, "counters": key == "a", "cpu_s": seconds * 0.9, "maxrss_bytes": 400000000,
                      "harness_rss_bytes": 20000000, "wrapper_rss_bytes": 10000000}
            records.append(record)
            if key == "a":
                write(f"pairs/{pair:04d}-a.csv", f"{seconds * 900:.2f},msec,task-clock,1,100.00,,\n"
                                                 f"{1000000 + pair},,instructions,1,100.00,,\n{800000 + pair},,cycles,1,100.00,,\n")
            phases = " ".join(f"{phase}_ns={(index + 1) * 1000 + pair}" for index, phase in enumerate(_lab.PHASES))
            write(f"pairs/{pair:04d}-{key}.ccmetrics", f"CC_METRICS wall_ns={int(seconds * 1e9)}\n"
                  f"CC_METRICS_INPUT measured=1 start_ns=0 total_ns={9000 + pair} {phases}\n")
    write("pairs.json", json.dumps(records))
    write("summary.json", json.dumps(_lab.compare_summary(str(directory))))
    return {"lab/" + path.relative_to(directory).as_posix(): path.read_bytes()
            for path in sorted(directory.rglob("*")) if path.is_file()}


def raw_corpus(directory: Path, pairs: int = 20) -> None:
    """A complete one-job native corpus bundle without summaries, as `bench_throughput run` leaves its raw files."""
    header = ("round,pair,order,variant,job,wall_seconds,user_seconds,system_seconds,peak_rss_bytes,cycles,instructions,"
              "branches,branch_misses,cache_references,cache_misses,arena_calls,arena_bytes,output_bytes,source_bytes,"
              "source_lines,source_functions,output_sha256,minor_faults,major_faults,voluntary_context_switches,"
              "involuntary_context_switches\n")

    def row(round_: int, pair: int, order: int, variant: int, wall: float, counted: bool) -> str:
        counters = "100,200,30,4,50,6,3,15" if counted else "NA,NA,NA,NA,NA,NA,NA,NA"
        return f"{round_},{pair},{order},{variant},0,{wall!r},0.25,0.125,67108864,{counters},4,16,2,1,{'0' * 64},1,0,2,3\n"
    directory.mkdir(parents=True)
    (directory / "samples.csv").write_text(header + "".join(
        row(round_, pair, variant ^ (pair & 1), variant, 1.0 + 0.001 * pair + 0.01 * variant, False)
        for round_ in range(2) for pair in range(pairs) for variant in range(2)))
    (directory / "telemetry.csv").write_text(header + "".join(
        row(kind, repeat, variant, variant, 1.0, True) for kind in range(2) for repeat in range(3) for variant in range(2)))
    for name, text in (("metadata.json", "{}\n"), ("jobs.tsv", "0\ttiny_startup/fast\n"), ("commands.jsonl", "{}\n"),
                       ("capabilities.jsonl", "{}\n")):
        (directory / name).write_text(text)
    (directory / "complete.txt").write_text(f"schema=2 jobs=1 pairs={pairs} rounds=2 guard=1\n" + "".join(
        hashlib.sha256((directory / name).read_bytes()).hexdigest() + " " + name + "\n"
        for name in ("samples.csv", "telemetry.csv", "metadata.json", "jobs.tsv", "commands.jsonl", "capabilities.jsonl")))


class SealedReplayTest(unittest.TestCase):
    """A verified bundle's lab and corpus are replayed from its raw bytes by the trusted statistics (#2918)."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.files = raw_lab(self.root / "lab")

    def tearDown(self) -> None:
        self.directory.cleanup()

    def edited(self, name: str, change) -> dict[str, bytes]:
        files = dict(self.files)
        files[name] = json.dumps(change(json.loads(files[name]))).encode("utf-8")
        return files

    def test_a_complete_lab_replays_from_its_raw_pairs(self) -> None:
        self.assertEqual(compiler_publish.sealed_lab_replay(self.files), 10)

    def test_a_lab_whose_raw_bytes_or_saved_inference_disagree_is_refused(self) -> None:
        def records(change):
            return lambda rows: [change(dict(row), index) for index, row in enumerate(rows)]
        cases = {
            "changed counter bytes": dict(self.files, **{"lab/pairs/0003-a.csv": self.files["lab/pairs/0003-a.csv"].replace(
                b"927.00,msec,task-clock", b"999.00,msec,task-clock")}),
            "changed compiler metrics": dict(self.files, **{"lab/pairs/0004-b.ccmetrics": self.files[
                "lab/pairs/0004-b.ccmetrics"].replace(b"read_ns=1004", b"read_ns=9004")}),
            "missing compiler metrics": {name: data for name, data in self.files.items() if name != "lab/pairs/0005-b.ccmetrics"},
            "duplicated pair": self.edited("lab/pairs.json", records(lambda row, index: dict(row, pair=2) if index in (4, 5) else row)),
            "failed member": self.edited("lab/pairs.json", records(lambda row, index: dict(row, exit=1) if index == 7 else row)),
            "unordered members": self.edited("lab/pairs.json", lambda rows: [rows[1], rows[0], *rows[2:]]),
            "truncated population": self.edited("lab/pairs.json", lambda rows: rows[:-1]),
            "counter claim without its probe": self.edited("lab/b/lab.json", lambda meta: dict(
                meta, capabilities={"perf_stat": {"usable": True, "reason": "x"}})),
            "edited verdict": self.edited("lab/summary.json", lambda summary: dict(
                summary, verdict=dict(summary["verdict"], outcome="faster"))),
            "edited population": self.edited("lab/summary.json", lambda summary: dict(
                summary, baseline=dict(summary["baseline"], failed=1))),
            "edited plan": self.edited("lab/summary.json", lambda summary: dict(
                summary, plan=dict(summary["plan"], complete_pairs=12))),
            "another profile": self.edited("lab/compare.json", lambda raw: dict(raw, config=dict(raw["config"], cpu=3))),
            "a fixed pair count": self.edited("lab/compare.json", lambda raw: dict(raw, config=dict(raw["config"], pairs=10))),
            "an odd plan": self.edited("lab/compare.json", lambda raw: dict(raw, plan=dict(raw["plan"], pairs=9))),
            "a flag pair": self.edited("lab/compare.json", lambda raw: dict(raw, config=dict(raw["config"],
                                                                                             canonical_inline_pair=True))),
            "duplicate key": dict(self.files, **{"lab/compare.json": b'{"version": 1, "version": 1}'}),
        }
        for label, files in cases.items():
            with self.subTest(label=label), self.assertRaises(ValueError):
                compiler_publish.sealed_lab_replay(files)

    def corpus_command(self) -> tuple:
        driver = bootstrapped_driver()
        if driver is None or shutil.which("clang") is None:
            self.skipTest("no native driver or clang: set BUSTER_COMPILER_EVIDENCE_DRIVER or put tcc and clang on PATH")
        return (str(driver), "bench_throughput", "compare")

    def test_a_corpus_replays_through_the_trusted_native_comparison(self) -> None:
        command = self.corpus_command()
        produced = self.root / "produced"
        raw_corpus(produced)
        made = subprocess.run([*command, "--output", str(produced)], cwd=compiler_publish.TRUSTED_ROOT,
                              capture_output=True, text=True, timeout=600, check=False)
        self.assertEqual(made.returncode, 0, made.stdout + made.stderr)
        files = {"throughput/" + path.relative_to(produced).as_posix(): path.read_bytes()
                 for path in sorted(produced.rglob("*")) if path.is_file()}
        self.assertIn("throughput/summary.md", files)
        replay = lambda members: compiler_publish.sealed_corpus_replay(  # noqa: E731
            members, tempfile.mkdtemp(dir=self.root))
        with mock.patch.object(compiler_publish, "CORPUS_REPLAY_COMMAND", command):
            self.assertEqual(replay(files), 1)
            samples = files["throughput/samples.csv"].replace(b"\n0,1,1,0,0,1.001,", b"\n0,1,1,0,0,1.5,", 1)
            self.assertTrue(samples != files["throughput/samples.csv"])
            summary = files["throughput/summary.json"].replace(b'"valid":true', b'"valid":false')
            for label, members in (("raw sample", dict(files, **{"throughput/samples.csv": samples})),
                                   ("summary", dict(files, **{"throughput/summary.json": summary})),
                                   ("report", dict(files, **{"throughput/summary.md": b"# edited\n"}))):
                with self.subTest(label=label), self.assertRaisesRegex(ValueError, "does not replay"):
                    replay(members)

    def test_a_corpus_replay_that_cannot_run_is_refused(self) -> None:
        files = {"throughput/summary.json": b'{"comparisons": []}'}
        with mock.patch.object(compiler_publish, "CORPUS_REPLAY_COMMAND", ("/nonexistent/build.sh",)), \
                self.assertRaisesRegex(ValueError, "corpus replay did not run"):
            compiler_publish.sealed_corpus_replay(files, str(self.root))


class EvidenceTest(unittest.TestCase):
    def test_reads_receipt_and_summary_as_data(self) -> None:
        payload = archive({"receipt.json": json.dumps(receipt()), "lab/summary.json": json.dumps(summary()),
                           "throughput/summary.json": json.dumps(corpus()["summary"]),
                           "throughput/metadata.json": json.dumps(corpus()["metadata"])})
        got = compiler_publish.read_evidence(FakeApi(payload), "92", "buster-9700x-compiler-x-1")
        self.assertEqual(got[:3], (receipt(), summary(), ""))
        self.assertEqual(got[3]["name"], "buster-9700x-compiler-x-1")
        unscaled = {name: {"summary": None, "metadata": None} for name in compiler_receipt.SCALING_PROFILE["series"]}
        self.assertEqual(got[4], dict(corpus(), scaling=unscaled, machine=None))
        bundles = scaling()
        members = {f"scaling/{name}/{leaf}": json.dumps(bundles[name][key]) for name in bundles
                   for leaf, key in (("scaling.json", "summary"), ("scaling-metadata.json", "metadata"))}
        payload = archive({"receipt.json": json.dumps(receipt()), **members})
        self.assertEqual(compiler_publish.read_evidence(FakeApi(payload), "92", "buster-9700x-compiler-x-1")[4]["scaling"],
                         bundles)

    def test_reads_a_pull_snapshot_phase_population(self) -> None:
        # A non-owned pull snapshot-v1 receipt's declared phases are read with their five members, so
        # validate_closure replays them; a missing member is a problem, never an empty population (#3211).
        current = dict(receipt(), preparation_policy="snapshot-v1",
                       phase_ownership={"count": 1, "phases": [{"file": "0001.json"}]})
        phase = {"owned-phases/0001.json" + suffix: suffix or "{}" for suffix in compiler_receipt.EVIDENCE_PHASE_SUFFIXES}
        members = {"receipt.json": json.dumps(current), "throughput/summary.json": "{}", "throughput/metadata.json": "[]", **phase}
        got = compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
        self.assertEqual(got[2], "")
        self.assertEqual(got[4]["closure"], {"owned_phases": {"0001.json": {
            "receipt": b"{}", "command": b".argv", "stdout": b".stdout", "stderr": b".stderr", "bootstrap": b".bootstrap.complete"}},
            "owned_throughput": {"summary": b"{}", "metadata": b"[]"}})
        members.pop("owned-phases/0001.json.stderr")
        self.assertEqual(compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")[2],
                         "required native phase evidence owned-phases/0001.json missing or oversized")
        ambiguous = dict(current, phase_ownership={"count": 2, "phases": [{"file": "0001.json"}, {"file": "0001.json"}]})
        members.update(phase, **{"receipt.json": json.dumps(ambiguous)})
        self.assertEqual(compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")[2],
                         "native phase population is ambiguous")

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

    def analyzer_members(self) -> tuple[dict, dict]:
        profile_summary = {"schema": compiler_receipt.ANALYZER_SUMMARY_SCHEMA, "status": "failed",
                           "inventory": {"selected_rows": 182}}
        raw = {name: f"raw {name}\n".encode("utf-8") for name in compiler_receipt.ANALYZER_REQUIRED_FILES}
        raw["request.txt"] = (compiler_receipt.ANALYZER_REQUEST_LINE + "\n").encode("utf-8")
        return raw, profile_summary

    def test_analyzer_bundle_members_are_loaded_bounded_and_required(self) -> None:
        profile_receipt = analyzer_receipt()
        raw, profile_summary = self.analyzer_members()
        members = {"receipt.json": json.dumps(profile_receipt),
                   **{f"analyzer/{name}": value for name, value in raw.items()},
                   "analyzer/summary.json": json.dumps(profile_summary)}
        got = self.evidence(members)
        self.assertEqual(got[:3], (profile_receipt, profile_summary, ""))
        expected_raw = {**raw, "summary.json": json.dumps(profile_summary).encode("utf-8")}
        self.assertEqual(got[4]["analyzer"]["files"], expected_raw)
        missing = dict(members)
        del missing["analyzer/profile/helper.log"]
        rejected = self.evidence(missing)
        self.assertIn("required analyzer evidence member", rejected[2])
        oversized = dict(members)
        oversized["analyzer/profile/helper.log"] = b"x" * 65
        with mock.patch.object(compiler_publish, "ANALYZER_MEMBER_LIMIT", 64):
            rejected = self.evidence(oversized)
        self.assertIn("size bound", rejected[2])

    def test_analyzer_v2_bundle_is_loaded_by_its_exact_profile_marker(self) -> None:
        profile_receipt = analyzer_receipt(request_line=compiler_receipt.ANALYZER_REQUEST_LINE_V2)
        profile_summary = {"schema": compiler_receipt.ANALYZER_SUMMARY_SCHEMA_V2, "status": "failed",
                           "inventory": {"selected_rows": 184}}
        raw = {name: f"raw {name}\n".encode("utf-8") for name in compiler_receipt.ANALYZER_REQUIRED_FILES}
        raw["request.txt"] = (compiler_receipt.ANALYZER_REQUEST_LINE_V2 + "\n").encode("utf-8")
        members = {"receipt.json": json.dumps(profile_receipt),
                   **{f"analyzer/{name}": value for name, value in raw.items()},
                   "analyzer/summary.json": json.dumps(profile_summary)}
        got = self.evidence(members)
        self.assertEqual(got[:3], (profile_receipt, profile_summary, ""))
        self.assertEqual(got[4]["analyzer"]["files"]["request.txt"], raw["request.txt"])

    def test_superseded_analyzer_receipt_needs_no_measurement_bundle_to_stay_neutral(self) -> None:
        head, parent = "a" * 40, "b" * 40
        request_bytes = (compiler_receipt.ANALYZER_REQUEST_LINE + "\n").encode("utf-8")
        profile_receipt = analyzer_receipt("superseded")
        profile_receipt["analyzer_request_sha256"] = hashlib.sha256(request_bytes).hexdigest()
        profile_receipt["reasons"] = ["pull request head moved before measurement"]
        payload = archive({"receipt.json": json.dumps(profile_receipt), "analyzer/request.txt": request_bytes})

        class Api(FakeApi):
            def request(self, path: str, data: dict | None = None) -> object:
                if path.startswith("/actions/runs/92/artifacts?"):
                    return super().request(path, data)
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = request_bytes if revision == head else b"# prior request history\n"
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        api = Api(payload)
        read = compiler_publish.read_evidence(api, "92", "buster-9700x-compiler-x-1")
        self.assertEqual(read[:3], (profile_receipt, None, ""))
        expected = dict(EXPECTED, mode="pull", ref="refs/pull/7/head")
        freshness = compiler_publish.verify_analyzer_request_freshness(api, expected, read[0], read[4])
        self.assertEqual(freshness, "")
        result = compiler_publish.decide(expected, True, "success", read[0], read[1], "", read[4],
                                         extra_reasons=[freshness] if freshness else [])
        self.assertEqual(result[:2], ("neutral", "Superseded before measurement"), result[2])


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
           "candidate": variant(value("--candidate")), "plan": {"pairs": 12, "complete_pairs": 12, "order": "ABBA", "fresh_copy": True},
           "verdict": {"metric": "wall", "outcome": "no detectable difference", "ratio": 1.0, "ci_low": 0.99,
                       "ci_high": 1.01, "text": "NO DETECTABLE DIFFERENCE"},
           "metrics": {"wall": {"outcome": "no detectable difference", "ratio": 1.0, "ci_low": 0.99, "ci_high": 1.01}},
           "warnings": []}
open(os.path.join(value("--output"), "summary.json"), "w").write(json.dumps(summary))
open(os.path.join(value("--output"), "reference.exe"), "w").write("excluded")
if os.environ.get("FAKE_RAW"):  # the v1 raw inventory (#2918): 10 pairs, counters on A only, metrics on both
    write = lambda name, text: (os.makedirs(os.path.dirname(os.path.join(value("--output"), name)), exist_ok=True),
                                open(os.path.join(value("--output"), name), "w").write(text))
    write("compare.json", json.dumps({"plan": {"pairs": 10, "order": "ABBA"}}))
    write("pairs.json", "[]")
    write("a/env/env.json", "{}")
    for key, usable in (("a", True), ("b", False)):
        write(key + "/lab.json", json.dumps({"capabilities": {"perf_stat": {"usable": usable}}, "collection": {"metrics_out": True}}))
        for log in ("commands.log", "wrapper-rss.log", "source-run.log", "metrics-probe.log", "warmup-0.log"):
            write(key + "/" + log, log + "\\n")
        for pair in range(1, 11):
            write("pairs/%04d-%s.ccmetrics" % (pair, key), "metrics\\n")
            if usable:
                write("pairs/%04d-%s.csv" % (pair, key), "1,,cycles\\n")
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
covered = workloads[:1] if behavior in ("partial", "partial-exit1") else workloads
regression = behavior in ("regression", "partial-exit1")
tests = [{"metric": metric, "round": number, "median_ratio": 1.3 if regression and metric == "wall_seconds" else 1.0,
          "ci_low": 1.299 if regression and metric == "wall_seconds" else 0.999,
          "ci_high": 1.301 if regression and metric == "wall_seconds" else 1.001,
          "baseline_relative_mad": 0.0, "candidate_relative_mad": 0.0, "pairs": 20,
          "margin_exceedances": 20 if regression and metric == "wall_seconds" else 0,
          "p_value": 0.000001 if regression and metric == "wall_seconds" else 1.0,
          "regression": regression and metric == "wall_seconds"}
         for metric in ("wall_seconds", "peak_rss_bytes") for number in range(2)]
cases = [{"name": name + "/" + mode, "medians": {"wall_seconds": [1.0, 1.3], "peak_rss_bytes": [4096, 4096]},
          "tests": tests, "decision": "regression" if regression else "no substantial regression detected"}
         for name in covered for mode in ("fast", "quality")]
summary = {"schema": 2, "guard_enabled": True, "family_alpha": 0.01, "per_test_alpha": 0.01 / 24,
           "comparisons": cases, "confirmed_regressions": len(cases) if regression else 0,
           "inconclusive_cases": 0, "valid": True}
metadata = {"schema": 2, "seed": 20260907, "profile": value("--profile"), "pairs_per_round": int(value("--pairs")), "rounds": 2,
            "warmups": int(value("--warmups")), "cpu": int(value("--cpu")), "workloads": workloads,
            "scale": 1, "input_schema": 1, "cache_policy": "warm-filesystem-new-process", "clock": "monotonic",
            "jobs": [{"job": job, "name": name, "mode": mode, "artifact": "object", "source": "/diagnostic/" + name + ".b",
                      "sha256": "7" * 64, "bytes": 32, "physical_lines": 2, "defined_functions": 1}
                     for job, (name, mode) in enumerate((name, mode) for name in workloads for mode in ("fast", "quality"))],
            "compiler_provenance": [{"sha256": digest(value("--baseline")), "bytes": os.path.getsize(value("--baseline"))},
                                    {"sha256": digest(value("--candidate")), "bytes": os.path.getsize(value("--candidate"))}]}
open(os.path.join(output, "summary.json"), "w").write(json.dumps(summary))
open(os.path.join(output, "metadata.json"), "w").write(json.dumps(metadata))
if os.environ.get("FAKE_RAW"):  # the v1 raw corpus inventory (#2918)
    os.makedirs(os.path.join(output, "inputs"))
    os.makedirs(os.path.join(output, "artifacts"))
    for name in ("samples.csv", "telemetry.csv", "jobs.tsv", "commands.jsonl", "capabilities.jsonl", "complete.txt", "cpuinfo.txt"):
        open(os.path.join(output, name), "w").write(name + "\\n")
    for name in workloads:
        open(os.path.join(output, "inputs", name + ".c"), "w").write("int main(void) { return 0; }\\n")
    open(os.path.join(output, "artifacts", "tiny_startup-fast-0.log"), "w").write("log\\n")
if behavior == "missing-exit1": os.unlink(os.path.join(output, "metadata.json"))
sys.exit(3 if behavior == "fail" else 1 if behavior in ("regression", "partial-exit1", "missing-exit1", "badexit") else 0)
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


def bootstrapped_driver() -> Path | None:
    """BUSTER_COMPILER_EVIDENCE_DRIVER, else this checkout's own native driver when TCC can bootstrap it."""
    import shutil
    named = os.environ.get("BUSTER_COMPILER_EVIDENCE_DRIVER")
    driver = Path(named) if named else None
    if driver is None and shutil.which("tcc"):
        resolved = subprocess.run([str(Path(__file__).resolve().parents[2] / "build.sh"), "compiler_closure", "driver-path"],
                                  capture_output=True, text=True, timeout=600, check=False)
        lines = resolved.stdout.splitlines()
        driver = Path(lines[-1]) if resolved.returncode == 0 and lines and lines[-1].startswith("/") else None
    return driver if driver is not None and driver.is_file() else None


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


class RunLifecycleTest(unittest.TestCase):
    """compiler_compare.run owns and reaps the whole process group of a command (#2926)."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.log = self.root / "log"

    def tearDown(self) -> None:
        self.directory.cleanup()

    def late(self, script: str, timeout: int = 5) -> tuple[int, bool]:
        status = compiler_compare.run(["/bin/sh", "-c", script], self.root, self.log, timeout)
        time.sleep(0.8)
        return status, (self.root / "late-marker").exists()

    def test_timeout_kills_descendants_before_returning(self) -> None:
        status, late = self.late('(sleep 0.3; printf x > late-marker) & wait', timeout=0)
        self.assertEqual((status, late), (124, False))
        self.assertIn("exit=124", self.log.read_text())

    def test_nested_children_are_killed_on_timeout(self) -> None:
        status, late = self.late('/bin/sh -c "(sleep 0.4; printf x > late-marker) & wait" & wait', timeout=0)
        self.assertEqual((status, late), (124, False))

    def test_normal_completion_reaps_background_leftovers(self) -> None:
        status, late = self.late('(sleep 0.3; printf x > late-marker) &')
        self.assertEqual((status, late), (0, False))

    def test_exit_status_is_preserved(self) -> None:
        self.assertEqual(self.late("exit 3"), (3, False))

    def test_launch_failure_is_127(self) -> None:
        self.assertEqual(compiler_compare.run(["/nonexistent/tool"], self.root, self.log, 5), 127)

    def test_exception_while_waiting_still_reaps_the_group(self) -> None:
        real, calls = subprocess.Popen.wait, []

        def interrupted(process: subprocess.Popen, timeout: float | None = None) -> int:
            calls.append(timeout)
            if len(calls) == 1:
                raise KeyboardInterrupt()
            return real(process, timeout)

        with mock.patch.object(subprocess.Popen, "wait", interrupted):
            with self.assertRaises(KeyboardInterrupt):
                compiler_compare.run(["/bin/sh", "-c", "(sleep 0.3; printf x > late-marker) & sleep 5"],
                                     self.root, self.log, 5)
        time.sleep(0.8)
        self.assertFalse((self.root / "late-marker").exists())

    def test_unprovable_cleanup_is_a_failure_not_a_pass(self) -> None:
        with mock.patch.object(compiler_compare, "reap_group", return_value=False):
            self.assertEqual(compiler_compare.run(["/bin/sh", "-c", "exit 0"], self.root, self.log, 5),
                             compiler_compare.CLEANUP_STATUS)
        self.assertIn("could not be proven (original exit=0)", self.log.read_text())


class ExportTest(unittest.TestCase):
    """compiler_compare.export_tree bounds and reports evidence before it is copied (#2929)."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.source = self.root / "source"
        self.evidence = self.root / "evidence"
        self.source.mkdir()
        self.evidence.mkdir()
        (self.source / "summary.json").write_text("{}")
        (self.source / "raw.txt").write_text("raw")

    def tearDown(self) -> None:
        self.directory.cleanup()

    def export(self, required: tuple = ("summary.json",), ignore: tuple = ("*.exe",)) -> tuple[list, list]:
        return compiler_compare.export_tree(self.source, self.evidence / "lab", self.evidence, ignore, required)

    def exported(self) -> list[str]:
        return sorted(path.relative_to(self.evidence / "lab").as_posix()
                      for path in (self.evidence / "lab").rglob("*") if path.is_file())

    def test_complete_export_has_no_omissions_and_honours_ignores(self) -> None:
        (self.source / "ref.exe").write_text("binary")
        (self.source / "nested").mkdir()
        (self.source / "nested" / "a.txt").write_text("a")
        self.assertEqual(self.export(), ([], []))
        self.assertEqual(self.exported(), ["nested/a.txt", "raw.txt", "summary.json"])
        self.assertFalse((self.evidence / "lab.staging").exists())

    def test_oversized_file_is_never_copied_and_is_reported(self) -> None:
        (self.source / "big.txt").write_bytes(b"x" * 100)
        with mock.patch.object(compiler_compare, "EVIDENCE_FILE_LIMIT", 50):
            problems, omissions = self.export()
        self.assertEqual(problems, [])
        self.assertEqual([item["path"] for item in omissions], ["big.txt"])
        self.assertIn("exceeds", omissions[0]["reason"])
        self.assertNotIn("big.txt", self.exported())
        self.assertTrue((self.source / "big.txt").is_file())

    def test_oversized_required_member_makes_the_export_incomplete(self) -> None:
        (self.source / "summary.json").write_bytes(b" " * 100)
        with mock.patch.object(compiler_compare, "EVIDENCE_MEMBER_LIMIT", 50):
            problems, omissions = self.export()
        self.assertEqual(len(problems), 1)
        self.assertIn("required evidence lab/summary.json not exported", problems[0])
        self.assertEqual(omissions[0]["path"], "summary.json")
        self.assertNotIn("summary.json", self.exported())

    def test_absent_required_member_is_a_problem(self) -> None:
        problems, _ = self.export(required=("summary.json", "metadata.json"))
        self.assertEqual(len(problems), 1)
        self.assertIn("metadata.json not exported: absent from the source", problems[0])

    def test_many_small_files_hit_the_aggregate_budget_before_copying(self) -> None:
        for index in range(20):
            (self.source / f"part{index:02}.txt").write_bytes(b"y" * 10)
        with mock.patch.object(compiler_compare, "EVIDENCE_TOTAL_LIMIT", 100):
            problems, omissions = self.export()
        self.assertEqual(problems, [])
        total = sum(path.stat().st_size for path in (self.evidence / "lab").rglob("*") if path.is_file())
        self.assertLessEqual(total, 100)
        self.assertTrue(omissions)
        self.assertTrue(all("budget" in item["reason"] for item in omissions))
        # The lab directory itself is one of the four entries (#2918 counts directories too).
        with mock.patch.object(compiler_compare, "EVIDENCE_FILE_COUNT", 4):
            self.export()
        self.assertEqual(len(self.exported()), 3)

    def test_required_members_of_the_whole_tree_precede_optional_ones(self) -> None:
        # artifacts/ sorts before inputs/; the budget must still reach the required inputs (#2918).
        (self.source / "artifacts").mkdir()
        (self.source / "inputs").mkdir()
        for index in range(5):
            (self.source / "artifacts" / f"run-{index}.log").write_text("log")
        (self.source / "inputs" / "tiny.c").write_text("int main;")
        compiler_compare.EVIDENCE_OMISSION_LEDGER.clear()
        # Seven entries: lab/, artifacts/ and inputs/, then two required and two optional files.
        with mock.patch.object(compiler_compare, "EVIDENCE_FILE_COUNT", 7):
            problems, omissions = self.export(required=("summary.json", "inputs/tiny.c"))
        self.assertEqual(problems, [])
        self.assertEqual(self.exported(), ["artifacts/run-0.log", "artifacts/run-1.log", "inputs/tiny.c", "summary.json"])
        dropped = [*(f"artifacts/run-{index}.log" for index in range(2, 5)), "raw.txt"]
        self.assertEqual([item["path"] for item in omissions], dropped)
        self.assertEqual(compiler_compare.EVIDENCE_OMISSION_LEDGER, [("lab/" + path, "file-budget") for path in dropped])

    def test_empty_optional_directories_never_starve_a_required_member(self) -> None:
        for index in range(300):
            (self.source / "empty" / f"{index:03}" / "deeper").mkdir(parents=True)
        with mock.patch.object(compiler_compare, "EVIDENCE_FILE_COUNT", 2):
            problems, omissions = self.export()
        self.assertEqual(problems, [])
        self.assertEqual(self.exported(), ["summary.json"])
        self.assertFalse((self.evidence / "lab" / "empty").exists())
        self.assertEqual(omissions, [{"path": "raw.txt", "reason": "evidence file-count budget exhausted"}])

    def test_sealed_optional_members_wait_for_every_later_required_member(self) -> None:
        # A later leg's required member must not be starved by an earlier leg's optional data (#2918).
        for index in range(6):
            (self.source / f"optional-{index}.log").write_bytes(b"o" * 10)
        corpus = self.root / "corpus"
        corpus.mkdir()
        (corpus / "summary.json").write_bytes(b"{}" + b" " * 38)
        compiler_compare.EVIDENCE_OMISSION_LEDGER.clear()
        compiler_compare.EVIDENCE_DEFERRED_OPTIONAL.clear()
        receipt_omissions: dict = {}
        with mock.patch.object(compiler_compare, "EVIDENCE_TOTAL_LIMIT", 85):
            lab_problems, _ = self.export()
            exported_before = self.exported()
            corpus_problems, _ = compiler_compare.export_tree(corpus, self.evidence / "throughput", self.evidence,
                                                              ("*.exe",), ("summary.json",))
        self.assertEqual((lab_problems, exported_before), ([], ["optional-0.log", "optional-1.log", "optional-2.log",
                                                                 "optional-3.log", "optional-4.log", "optional-5.log",
                                                                 "raw.txt", "summary.json"]))
        self.assertEqual(len(corpus_problems), 1)  # the eager export starves the later required member
        shutil.rmtree(self.evidence / "lab")
        shutil.rmtree(self.evidence / "throughput")
        compiler_compare.EVIDENCE_OMISSION_LEDGER.clear()
        with mock.patch.object(compiler_compare, "EVIDENCE_TOTAL_LIMIT", 85):
            lab_problems, _ = compiler_compare.export_tree(self.source, self.evidence / "lab", self.evidence, ("*.exe",),
                                                           ("summary.json",), defer_optional=True)
            self.assertEqual(self.exported(), ["summary.json"])
            corpus_problems, _ = compiler_compare.export_tree(corpus, self.evidence / "throughput", self.evidence,
                                                              ("*.exe",), ("summary.json",), defer_optional=True)
            compiler_compare.export_deferred_optional(self.evidence, receipt_omissions)
        self.assertEqual((lab_problems, corpus_problems), ([], []))
        self.assertTrue((self.evidence / "throughput" / "summary.json").is_file())
        # 2 + 40 required bytes leave 43: four optional logs, then raw.txt; the last two logs do not fit.
        self.assertEqual(self.exported(), ["optional-0.log", "optional-1.log", "optional-2.log", "optional-3.log", "raw.txt",
                                           "summary.json"])
        dropped = ["optional-4.log", "optional-5.log"]
        self.assertEqual(receipt_omissions, {"lab": {"count": 2, "shown": [
            {"path": path, "reason": "evidence byte budget exhausted"} for path in dropped]}})
        self.assertEqual(compiler_compare.EVIDENCE_OMISSION_LEDGER, [("lab/" + path, "byte-budget") for path in dropped])
        self.assertEqual(compiler_compare.EVIDENCE_DEFERRED_OPTIONAL, [])

    def test_an_earlier_opt_in_export_defers_its_optional_data_in_a_sealed_attempt(self) -> None:
        # The ast-pilot and inline legs call export_tree without defer_optional before the ordinary lab and
        # corpus; a sealed attempt's EVIDENCE_DEFER_ALL must still keep their optional data for last (#2918).
        opt_in = self.root / "ast_pilot"
        opt_in.mkdir()
        (opt_in / "acceptance.json").write_text("{}")
        for index in range(6):
            (opt_in / f"diagnostic-{index}.log").write_bytes(b"d" * 10)
        corpus = self.root / "corpus"
        corpus.mkdir()
        (corpus / "summary.json").write_bytes(b"{}" + b" " * 38)
        outcomes = {}
        for sealed in (False, True):
            shutil.rmtree(self.evidence)
            self.evidence.mkdir()
            compiler_compare.EVIDENCE_OMISSION_LEDGER.clear()
            compiler_compare.EVIDENCE_DEFERRED_OPTIONAL.clear()
            with mock.patch.object(compiler_compare, "EVIDENCE_TOTAL_LIMIT", 70), \
                    mock.patch.object(compiler_compare, "EVIDENCE_DEFER_ALL", sealed):
                opt_problems, _ = compiler_compare.export_tree(opt_in, self.evidence / "ast_pilot_acceptance", self.evidence,
                                                               ("*.exe",), ("acceptance.json",))
                lab_problems, _ = compiler_compare.export_tree(self.source, self.evidence / "lab", self.evidence, ("*.exe",),
                                                               ("summary.json",), defer_optional=sealed)
                corpus_problems, _ = compiler_compare.export_tree(corpus, self.evidence / "throughput", self.evidence,
                                                                  ("*.exe",), ("summary.json",), defer_optional=sealed)
                receipt_omissions: dict = {}
                compiler_compare.export_deferred_optional(self.evidence, receipt_omissions)
            outcomes[sealed] = (opt_problems, lab_problems, corpus_problems)
        # Eager: six 10-byte diagnostics (plus 2+5 lab bytes) leave no room for the 40-byte corpus summary.
        self.assertEqual(len(outcomes[False][2]), 1)
        self.assertEqual(outcomes[True], ([], [], []))
        self.assertTrue((self.evidence / "throughput" / "summary.json").is_file())
        self.assertTrue((self.evidence / "ast_pilot_acceptance" / "diagnostic-0.log").is_file())
        self.assertIn("ast_pilot_acceptance", receipt_omissions)

    def deferred_parts(self) -> Path:
        """A leg shaped like #3901's self-host inline leg: three parts, each exported with required=()."""
        root = self.root / "selfhost"
        for part, name in (("stage1-baseline", "stage1.json"), ("stage1-candidate", "stage1.json"),
                           ("compare", "result.json")):
            (root / part).mkdir(parents=True)
            (root / part / name).write_text("{\"ok\": true}")
            (root / part / "trace.log").write_text("trace\n")
        return root

    def test_a_leg_reading_unlisted_members_still_sees_them_in_a_sealed_attempt(self) -> None:
        # Deferral must not hide a member a leg validates without listing it as required (#3883 review).
        root = self.deferred_parts()
        exported = self.evidence / "selfhost_inline"
        compiler_compare.EVIDENCE_OMISSION_LEDGER.clear()
        compiler_compare.EVIDENCE_DEFERRED_OPTIONAL.clear()
        receipt_omissions: dict = {}
        with mock.patch.object(compiler_compare, "EVIDENCE_DEFER_ALL", True):
            for part in ("stage1-baseline", "stage1-candidate", "compare"):
                problems, _ = compiler_compare.export_tree(root / part, exported / part, self.evidence, ("*.exe",), ())
                self.assertEqual(problems, [])
            self.assertFalse((exported / "compare" / "trace.log").exists())
            documents = [compiler_compare.read_exported_json(exported / name) for name in
                         ("stage1-baseline/stage1.json", "stage1-candidate/stage1.json", "compare/result.json")]
            self.assertEqual(documents, [({"ok": True}, "")] * 3)
            self.assertFalse((exported / "compare" / "trace.log").exists())  # bulk optional data stays deferred
            compiler_compare.export_deferred_optional(self.evidence, receipt_omissions)
        files = sorted(path.relative_to(exported).as_posix() for path in exported.rglob("*") if path.is_file())
        self.assertEqual(files, ["compare/result.json", "compare/trace.log", "stage1-baseline/stage1.json",
                                 "stage1-baseline/trace.log", "stage1-candidate/stage1.json", "stage1-candidate/trace.log"])
        self.assertEqual((receipt_omissions, compiler_compare.EVIDENCE_OMISSION_LEDGER), ({}, []))

    def test_on_demand_export_follows_no_link_and_records_a_refusal_once(self) -> None:
        root = self.deferred_parts()
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "secret.json").write_text("{}")
        (root / "compare" / "linked").symlink_to(outside, target_is_directory=True)
        exported = self.evidence / "selfhost_inline"
        compiler_compare.EVIDENCE_OMISSION_LEDGER.clear()
        compiler_compare.EVIDENCE_DEFERRED_OPTIONAL.clear()
        receipt_omissions: dict = {}
        with mock.patch.object(compiler_compare, "EVIDENCE_DEFER_ALL", True):
            compiler_compare.export_tree(root / "compare", exported / "compare", self.evidence, ("*.exe",), ())
            document, problem = compiler_compare.read_exported_json(exported / "compare" / "linked" / "secret.json")
            self.assertIsNone(document)
            self.assertFalse((exported / "compare" / "linked").exists())
            with mock.patch.object(compiler_compare, "EVIDENCE_TOTAL_LIMIT", 0):
                document, problem = compiler_compare.read_exported_json(exported / "compare" / "result.json")
            self.assertIsNone(document)
            compiler_compare.export_deferred_optional(self.evidence, receipt_omissions)
        refused = [item for item in compiler_compare.EVIDENCE_OMISSION_LEDGER if item[0] == "selfhost_inline/compare/result.json"]
        self.assertEqual(refused, [("selfhost_inline/compare/result.json", "byte-budget")])
        # The linked directory was the leg's own export omission; the deferred section adds only result.json.
        self.assertIn(("selfhost_inline/compare/linked", "excluded-type"), compiler_compare.EVIDENCE_OMISSION_LEDGER)
        self.assertEqual(receipt_omissions["selfhost_inline/compare"]["count"], 1)
        self.assertTrue((exported / "compare" / "trace.log").is_file())
        self.assertFalse((exported / "compare" / "result.json").exists())

    def test_every_omission_reaches_the_ledger_with_its_fixed_code(self) -> None:
        (self.source / "big.txt").write_bytes(b"x" * 100)
        (self.source / "link.txt").symlink_to(self.source / "raw.txt")
        compiler_compare.EVIDENCE_OMISSION_LEDGER.clear()
        with mock.patch.object(compiler_compare, "EVIDENCE_FILE_LIMIT", 50):
            self.export()
        self.assertEqual(sorted(compiler_compare.EVIDENCE_OMISSION_LEDGER),
                         [("lab/big.txt", "member-limit"), ("lab/link.txt", "excluded-type")])

    def test_symlinks_are_not_followed_or_copied(self) -> None:
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "secret.txt").write_text("secret")
        (self.source / "link.txt").symlink_to(outside / "secret.txt")
        (self.source / "linkdir").symlink_to(outside, target_is_directory=True)
        problems, omissions = self.export()
        self.assertEqual(problems, [])
        self.assertEqual(sorted(item["path"] for item in omissions), ["link.txt", "linkdir"])
        self.assertEqual(self.exported(), ["raw.txt", "summary.json"])

    def test_a_file_that_grows_while_copying_is_dropped_and_reported(self) -> None:
        (self.source / "live.txt").write_bytes(b"z" * 100)
        real = os.lstat

        def stale(path, *args, **kwargs):  # the size seen before copying is small; the bytes then grow
            result = real(path, *args, **kwargs)
            if str(path).endswith("live.txt"):
                return os.stat_result((result.st_mode, result.st_ino, result.st_dev, result.st_nlink, result.st_uid,
                                       result.st_gid, 1, 0, 0, 0))
            return result

        with mock.patch.object(compiler_compare, "EVIDENCE_FILE_LIMIT", 50), mock.patch("os.lstat", stale):
            problems, omissions = self.export()
        self.assertEqual(problems, [])
        self.assertEqual([item["path"] for item in omissions], ["live.txt"])
        self.assertIn("grew", omissions[0]["reason"])
        self.assertNotIn("live.txt", self.exported())

    def test_copy_error_is_reported_and_required_loss_is_a_problem(self) -> None:
        real = os.open

        def failing(path, *args, **kwargs):
            if str(path).endswith("summary.json"):
                raise OSError("injected")
            return real(path, *args, **kwargs)

        with mock.patch("os.open", failing):
            problems, omissions = self.export()
        self.assertEqual([item["path"] for item in omissions], ["summary.json"])
        self.assertIn("copy failed: injected", omissions[0]["reason"])
        self.assertEqual(len(problems), 1)

    def test_publisher_member_limit_matches_the_producer(self) -> None:
        self.assertEqual(compiler_compare.EVIDENCE_MEMBER_LIMIT, compiler_publish.MEMBER_LIMIT)
        self.assertLess(compiler_compare.EVIDENCE_TOTAL_LIMIT, compiler_publish.ARTIFACT_LIMIT)

    def test_export_budget_keeps_the_sealed_control_reserve(self) -> None:
        source = (Path(__file__).resolve().parents[2] / "tools" / "compiler_evidence.c").read_text()
        self.assertIn("#define COMPILER_EVIDENCE_TOTAL_LIMIT (48ull << 20)", source)
        self.assertIn("#define COMPILER_EVIDENCE_ENTRY_LIMIT 4096ull", source)
        self.assertEqual(compiler_compare.EVIDENCE_SEALED_LIMITS, (48 << 20, 4096))
        self.assertEqual(compiler_compare.EVIDENCE_TOTAL_LIMIT + compiler_compare.EVIDENCE_CONTROL_RESERVE[0], 48 << 20)
        self.assertEqual(compiler_compare.EVIDENCE_FILE_COUNT + compiler_compare.EVIDENCE_CONTROL_RESERVE[1], 4096)


class HarnessTest(unittest.TestCase):
    """The host harness against a real two-parent group with stand-in builds and lab."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name)
        self.repo = root / "candidate"
        git = lambda *arguments: fixture_git(self.repo, *arguments)  # noqa: E731
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
                    scaling_behavior: str = "", floor: str | None = None, **change) -> tuple[int, dict, Path]:
        values = {"mode": "main", "candidate": str(self.repo), "lab": str(self.lab), "work": str(self.root / "work"),
                  "evidence": str(self.root / "evidence"), "summary": str(self.root / "step.md"),
                  "repository": "buster14a/buster", "ref": "refs/heads/main",
                  "pull": "7", "pull-head": self.pull_head, "base": self.base, "base-tree": self.trees[1],
                  "head": self.head, "head-tree": self.trees[0], "trusted-revision": "9" * 40,
                  "request-run-id": "91", "run-id": "92", "run-attempt": "1"}
        values.update(change)
        argv = [item for key, value in values.items() for item in ("--" + key, value)]
        # The fixture's own base stands in for the real harness floor unless a test names another.
        with mock.patch.object(compiler_compare, "queue_head", return_value=live), \
                mock.patch.object(compiler_compare, "cpu_model", return_value=cpu), \
                mock.patch.object(compiler_compare, "THROUGHPUT_HARNESS_FLOOR", floor or self.base), \
                mock.patch.dict(os.environ, {"PATH": os.environ.get("PATH", ""), "FAKE_THROUGHPUT": corpus_behavior,
                                             "FAKE_SCALING": scaling_behavior}):
            code = compiler_compare.main(argv)
        evidence = self.root / "evidence"
        return code, json.loads((evidence / "receipt.json").read_text()), evidence

    def run_sealed(self, sealer: tuple, raw: bool = True) -> tuple[int, dict, Path]:
        with mock.patch.object(compiler_compare, "EVIDENCE_SEAL_COMMAND", sealer), \
                mock.patch.dict(os.environ, {"FAKE_RAW": "1" if raw else ""}):
            return self.run_harness(self.head, **{"request-attempt": "2"})

    def test_ordinary_attempt_seals_its_raw_evidence_last(self) -> None:
        # A stand-in records what it was asked to seal and writes the seal the receipt binds (#2918).
        script = ("import sys, os, pathlib\n"
                  "bundle, identity = pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])\n"
                  "assert sys.argv[1] == 'seal' and not (bundle / 'receipt.json.tmp').exists()\n"
                  "(bundle.parent / 'sealed-identity.tsv').write_text(identity.read_text())\n"
                  "(bundle / 'evidence-manifest.tsv').write_text('manifest\\n')\n"
                  "(bundle / 'evidence-seal.txt').write_text('schema=x\\nstate=complete\\nmanifest_bytes=9\\n'\n"
                  "    'manifest_sha256=' + 'a' * 64 + '\\nmembers=3\\npayload_bytes=4\\n')\n"
                  "print('COMPILER_EVIDENCE_SEALED')\n")
        code, result, evidence = self.run_sealed((sys.executable, "-c", script))
        self.assertEqual((code, result["state"], result["reasons"]), (0, "measured", []))
        self.assertEqual(result["evidence_bundle"], {"inventory": compiler_receipt.EVIDENCE_INVENTORY, "state": "sealed",
                                                     "manifest_sha256": "a" * 64, "manifest_bytes": 9, "members": 3,
                                                     "payload_bytes": 4, "omissions": 0})
        omissions = json.loads((evidence / compiler_receipt.EVIDENCE_OMISSIONS).read_text())
        self.assertEqual((omissions["schema"], omissions["total"], omissions["examples"]),
                         (compiler_receipt.EVIDENCE_OMISSIONS_SCHEMA, 0, []))
        self.assertEqual(set(omissions["by_reason"]), set(compiler_receipt.EVIDENCE_OMISSION_REASONS))
        identity = dict(line.split("\t") for line in (self.root / "sealed-identity.tsv").read_text().splitlines())
        self.assertEqual(list(identity), list(compiler_receipt.EVIDENCE_IDENTITY_KEYS))
        self.assertEqual((identity["request_attempt"], identity["executor_run"], identity["candidate_revision"],
                          identity["corpus_profile"], identity["corpus_harness_revision"]),
                         ("2", "92", self.head, compiler_receipt.THROUGHPUT_PROFILE["name"], self.base))
        self.assertTrue((evidence / "lab" / "pairs" / "0010-a.csv").is_file())
        self.assertTrue((evidence / "throughput" / "inputs" / "tiny_startup.c").is_file())
        # Optional members join after every leg's required ones, before the seal.
        self.assertTrue((evidence / "throughput" / "artifacts" / "tiny_startup-fast-0.log").is_file())
        self.assertEqual(compiler_compare.EVIDENCE_DEFERRED_OPTIONAL, [])
        self.assertFalse(compiler_compare.EVIDENCE_DEFER_ALL)

    def test_a_sealed_attempt_defers_every_export_including_opt_in_legs(self) -> None:
        seen = []
        real = compiler_compare.export_tree

        def spy(*arguments, **options):
            seen.append((arguments[1].relative_to(self.root / "evidence").as_posix(), compiler_compare.EVIDENCE_DEFER_ALL))
            return real(*arguments, **options)

        stand_in = (sys.executable, "-c", "raise SystemExit(1)")
        with mock.patch.object(compiler_compare, "export_tree", spy):
            self.run_sealed(stand_in)
        self.assertEqual(seen, [("lab", True), ("throughput", True)])
        self.assertFalse(compiler_compare.EVIDENCE_DEFER_ALL)

    def test_a_refused_seal_fails_the_attempt(self) -> None:
        script = "print('COMPILER_EVIDENCE_INCOMPLETE reason=required raw evidence is missing: x'); raise SystemExit(1)"
        code, result, _ = self.run_sealed((sys.executable, "-c", script))
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertIn("raw evidence not sealed (exit 1): COMPILER_EVIDENCE_INCOMPLETE", result["reasons"][-1])
        self.assertEqual(result["evidence_bundle"]["state"], "failed")

    def test_sealing_requires_the_v1_inventory_before_the_sealer_runs(self) -> None:
        code, result, _ = self.run_sealed((sys.executable, "-c", "raise SystemExit('sealer must not run')"), raw=False)
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertIn("required evidence lab/compare.json not exported: absent from the source", result["reasons"])
        self.assertIn("required evidence throughput/inputs/tiny_startup.c not exported: absent from the source", result["reasons"])
        self.assertNotIn("evidence_bundle", result)

    def test_an_unrequested_seal_keeps_the_legacy_inventory(self) -> None:
        code, result, evidence = self.run_harness(self.head)
        self.assertEqual((code, result["state"]), (0, "measured"))
        self.assertNotIn("evidence_bundle", result)
        self.assertFalse((evidence / compiler_receipt.EVIDENCE_OMISSIONS).exists())

    def test_the_native_driver_seals_and_verifies_a_harness_bundle(self) -> None:
        driver = bootstrapped_driver()
        if driver is None:
            self.skipTest("no native driver: set BUSTER_COMPILER_EVIDENCE_DRIVER or put tcc on PATH")
        code, result, evidence = self.run_sealed((str(driver), "compiler_evidence"))
        self.assertEqual((code, result["state"], result["reasons"]), (0, "measured", []))
        self.assertEqual(result["evidence_bundle"]["state"], "sealed")
        identity = self.root / "identity.tsv"
        identity.write_text(compiler_receipt.evidence_identity(result, "2"))
        verify = lambda: subprocess.run([str(driver), "compiler_evidence", "verify", str(evidence), str(identity)],  # noqa: E731
                                        capture_output=True, text=True, timeout=300, check=False)
        verified = verify()
        self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)
        self.assertIn(f"manifest_sha256={result['evidence_bundle']['manifest_sha256']}", verified.stdout)
        self.assertIn(f"members={result['evidence_bundle']['members']}", verified.stdout)
        (evidence / "lab" / "pairs" / "0004-a.csv").write_text("tampered\n")
        self.assertIn("COMPILER_EVIDENCE_REFUSED", verify().stdout)

    def zipped(self, evidence: Path) -> bytes:
        """The evidence as actions/upload-artifact uploads it: hidden files, the scratch marker among them, dropped."""
        self.assertTrue((evidence / compiler_compare.SCRATCH_MARKER).is_file())
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(evidence.rglob("*")):
                if path.is_file() and not any(part.startswith(".") for part in path.relative_to(evidence).parts):
                    archive.writestr(path.relative_to(evidence).as_posix(), path.read_bytes())
        return stream.getvalue()

    def test_the_publisher_verifies_a_sealed_bundle_natively(self) -> None:
        driver = bootstrapped_driver()
        if driver is None:
            self.skipTest("no native driver: set BUSTER_COMPILER_EVIDENCE_DRIVER or put tcc on PATH")
        code, result, evidence = self.run_sealed((str(driver), "compiler_evidence"))
        self.assertEqual((code, result["state"]), (0, "measured"))
        expected = dict(result["identity"], first_parent=self.base, range="1")
        summary = json.loads((evidence / "lab" / "summary.json").read_text())
        (evidence / "machine-specifications").mkdir()
        (evidence / "machine-specifications" / "report.json").write_text("{}")  # appended after the seal
        throughput = dict(retained(evidence), sealed_files=compiler_publish.sampling_archive(
            self.zipped(evidence), member_limit=compiler_publish.EVIDENCE_ARCHIVE_MEMBERS, require_nonexecutable=True))
        # The harness's stand-in lab and corpus carry no real statistics; SealedReplayTest replays real ones.
        replayed: list[dict] = []

        def lab(files: dict) -> int:
            replayed.append(files)
            return 10

        def corpus(files: dict, temporary: str) -> int:
            if "replay-refused" in files:
                raise ValueError("corpus summary.json does not replay")
            return 12
        with mock.patch.object(compiler_publish, "EVIDENCE_VERIFY_COMMAND", (str(driver), "compiler_evidence")), \
                mock.patch.object(compiler_publish, "sealed_lab_replay", lab), \
                mock.patch.object(compiler_publish, "sealed_corpus_replay", corpus):
            verified = compiler_publish.verify_evidence_bundle(throughput, expected, compiler_receipt.THROUGHPUT_PROFILE, "2")
            other_attempt = compiler_publish.verify_evidence_bundle(throughput, expected, compiler_receipt.THROUGHPUT_PROFILE, "3")
            files = dict(throughput["sealed_files"])
            files["lab/pairs/0007-b.ccmetrics"] = b"tampered\n"
            tampered = compiler_publish.verify_evidence_bundle(dict(throughput, sealed_files=files), expected,
                                                               compiler_receipt.THROUGHPUT_PROFILE, "2")
            with mock.patch.object(compiler_publish, "EVIDENCE_VERIFY_COMMAND", (sys.executable, "-c", (
                    "print('COMPILER_EVIDENCE_VERIFIED members=3 payload_bytes=4 manifest_sha256=' + 'a' * 64)"))):
                unreplayed = compiler_publish.verify_evidence_bundle(
                    dict(throughput, sealed_files=dict(throughput["sealed_files"], **{"replay-refused": b""})), expected,
                    compiler_receipt.THROUGHPUT_PROFILE, "2")
        self.maxDiff = None
        self.assertEqual(verified, {"state": "verified", "inventory": compiler_receipt.EVIDENCE_INVENTORY,
                                    "replay": {"lab_pairs": 10, "corpus_cases": 12},
                                    **{key: result["evidence_bundle"][key]
                                       for key in ("members", "payload_bytes", "manifest_sha256")}})
        self.assertIs(replayed[0], throughput["sealed_files"])
        self.assertEqual((other_attempt["state"], tampered["state"]), ("refused", "refused"))
        self.assertIn("COMPILER_EVIDENCE_REFUSED", tampered["reason"])
        self.assertEqual(unreplayed, {"state": "refused", "reason": "sealed raw evidence does not replay: corpus summary.json "
                                                                    "does not replay"})
        self.assertIn("replayed 10 lab pairs and 12 corpus cases", compiler_publish.evidence_bundle_note(verified))
        decide = lambda bundle, receipt=result: compiler_publish.decide(  # noqa: E731
            expected, True, "success", receipt, summary, "", throughput, evidence_bundle=bundle)
        self.assertEqual(decide(verified)[0], "success")
        self.assertIn("sealed raw evidence refused", decide(tampered)[2][0])
        self.assertIn("sealed raw evidence refused: sealed raw evidence does not replay", decide(unreplayed)[2][0])
        forged = dict(result, evidence_bundle=dict(result["evidence_bundle"], members=1))
        self.assertIn("receipt evidence_bundle does not name the verified sealed manifest", decide(verified, forged)[2])
        self.assertIn("receipt names sealed raw evidence that the artifact does not carry",
                      decide({"state": "legacy-unsealed"})[2])

    def test_unsealed_evidence_is_refused_unless_its_producer_predates_sealing(self) -> None:
        code, result, evidence = self.run_harness(self.head)
        self.assertEqual((code, result["state"]), (0, "measured"))
        expected = dict(result["identity"], first_parent=self.base, range="1")
        summary = json.loads((evidence / "lab" / "summary.json").read_text())
        # Only recovery of a run whose producer predates sealing may stand unsealed.
        bundle = compiler_publish.verify_evidence_bundle(retained(evidence), expected, compiler_receipt.THROUGHPUT_PROFILE,
                                                         "1", required=False)
        self.assertEqual(bundle, {"state": "legacy-unsealed"})
        self.assertEqual(compiler_publish.decide(expected, True, "success", result, summary, "", retained(evidence),
                                                 evidence_bundle=bundle)[0], "success")
        self.assertIn("unverified", compiler_publish.evidence_bundle_note(bundle))
        required = compiler_publish.verify_evidence_bundle(retained(evidence), expected, compiler_receipt.THROUGHPUT_PROFILE, "1")
        self.assertEqual(required, {"state": "unsealed"})
        decided = compiler_publish.decide(expected, True, "success", result, summary, "", retained(evidence),
                                          evidence_bundle=required)
        self.assertEqual(decided[0], "failure")
        self.assertIn("sealed raw evidence v1 is required on this route, but the artifact carries neither control file",
                      decided[2])

    def test_an_unreadable_or_unverifiable_sealed_bundle_fails_closed(self) -> None:
        expected = {"repository": "buster14a/buster", "request_run_id": "91", "run_id": "92", "run_attempt": "1",
                    "trusted_revision": "9" * 40, "base": self.base, "head": self.head, "base_tree": self.trees[1],
                    "head_tree": self.trees[0]}
        corpus = compiler_receipt.THROUGHPUT_PROFILE
        unsafe = compiler_publish.verify_evidence_bundle({"sealed_files": None, "sealed_problem": "sealed evidence archive "
                                                          "refused: unsafe"}, expected, corpus, "1")
        self.assertEqual(unsafe, {"state": "refused", "reason": "sealed evidence archive refused: unsafe"})
        files = {"evidence-seal.txt": b"seal\n"}
        for script, reason in (("raise SystemExit(3)", "native verify exited 3: no output"),
                               ("print('COMPILER_EVIDENCE_VERIFIED members=x')", "native verify exited 0: "
                                "COMPILER_EVIDENCE_VERIFIED members=x")):
            with self.subTest(reason=reason), mock.patch.object(compiler_publish, "EVIDENCE_VERIFY_COMMAND",
                                                                (sys.executable, "-c", script)):
                self.assertEqual(compiler_publish.verify_evidence_bundle({"sealed_files": files}, expected, corpus, "1"),
                                 {"state": "refused", "reason": reason})
        with mock.patch.object(compiler_publish, "EVIDENCE_VERIFY_COMMAND", ("/nonexistent/build.sh", "compiler_evidence")):
            missing = compiler_publish.verify_evidence_bundle({"sealed_files": files}, expected, corpus, "1")
        self.assertEqual(missing["state"], "refused")
        self.assertEqual(compiler_publish.verify_evidence_bundle({"sealed_files": files}, dict(expected, head=""), corpus, "1"),
                         {"state": "refused", "reason": "the authorized identity cannot name a sealed bundle"})

    def run_snapshot_pull(self, policy: str, sealer: tuple) -> tuple[int, dict, list]:
        """A pull attempt on `policy` whose native owner and measurement are stand-ins (#3211).

        Only main's seal decision and the sealed identity are under test; the real snapshot
        producer/consumer path is compiler_ordinary_fixture_test's.
        """
        seen = []

        class Owner:
            def __init__(self, driver, work, evidence, receipt, **options):
                (evidence / "owned-phases").mkdir()
                receipt["phase_ownership"] = {"state": "pending", "count": 14, "phases": []}

            def finish(self) -> None:
                pass

        def measured(arguments, candidate, work, evidence, bins, log, receipt, summaries) -> None:
            seen.append((arguments.evidence_seal, compiler_compare.EVIDENCE_DEFER_ALL))
            receipt["preparation_policy"] = policy
            if policy == "snapshot-v1":
                receipt["closure"] = {"policy": "snapshot-v1", "fallback": None}
            receipt["state"] = "measured"

        driver = self.root / "driver"
        driver.write_text("")
        with mock.patch.object(compiler_compare, "NativePhaseContext", Owner), \
                mock.patch.object(compiler_compare, "measure", measured), \
                mock.patch.object(compiler_compare, "EVIDENCE_SEAL_COMMAND", sealer):
            code, result, _ = self.run_harness(self.head, mode="pull", ref="refs/pull/7/head", **{
                "pull-head": self.head, "request-attempt": "2", "closure-policy": policy, "closure-driver": str(driver)})
        return code, result, seen

    def test_a_pull_snapshot_attempt_is_sealed_under_its_route(self) -> None:
        # The ordinary pull snapshot-v1 route seals like legacy-rebuild, its identity naming the route
        # and the owned-phase count, which select the snapshot inventory (#2918, #3211).
        script = ("import sys, pathlib\n"
                  "bundle, identity = pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])\n"
                  "(bundle.parent / 'sealed-identity.tsv').write_text(identity.read_text())\n"
                  "(bundle / 'evidence-manifest.tsv').write_text('manifest\\n')\n"
                  "(bundle / 'evidence-seal.txt').write_text('schema=x\\nstate=complete\\nmanifest_bytes=9\\n'\n"
                  "    'manifest_sha256=' + 'a' * 64 + '\\nmembers=3\\npayload_bytes=4\\n')\n"
                  "print('COMPILER_EVIDENCE_SEALED')\n")
        for policy, inventory, route in (
                ("snapshot-v1", compiler_receipt.EVIDENCE_SNAPSHOT_INVENTORY, [("preparation_policy", "snapshot-v1"), ("owned_phases", "14")]),
                ("legacy-rebuild", compiler_receipt.EVIDENCE_INVENTORY, [])):
            with self.subTest(policy=policy):
                shutil.rmtree(self.root / "work", ignore_errors=True)
                shutil.rmtree(self.root / "evidence", ignore_errors=True)
                code, result, seen = self.run_snapshot_pull(policy, (sys.executable, "-c", script))
                self.assertEqual((code, result["state"], result["reasons"]), (0, "measured", []))
                self.assertEqual(seen, [(True, True)])
                self.assertEqual((result["evidence_bundle"]["state"], result["evidence_bundle"]["inventory"]), ("sealed", inventory))
                lines = [tuple(line.split("\t")) for line in (self.root / "sealed-identity.tsv").read_text().splitlines()]
                self.assertEqual([key for key, _ in lines[:13]], list(compiler_receipt.EVIDENCE_IDENTITY_KEYS))
                self.assertEqual(lines[13:], route)

    def test_a_snapshot_identity_needs_its_exact_phase_count(self) -> None:
        base = {"identity": {"repository": "buster14a/buster", "request_run_id": "91", "run_id": "92", "run_attempt": "1",
                             "trusted_revision": "9" * 40, "base": "b" * 40, "head": "c" * 40, "base_tree": "e" * 40,
                             "head_tree": "d" * 40},
                "profile": compiler_receipt.PROFILE, "throughput_profile": compiler_receipt.THROUGHPUT_PROFILE}
        legacy = compiler_receipt.evidence_identity(base, "1")
        self.assertEqual(len(legacy.splitlines()), 13)
        self.assertEqual(compiler_receipt.evidence_identity(dict(base, preparation_policy="legacy-rebuild"), "1"), legacy)
        snapshot = dict(base, preparation_policy="snapshot-v1", phase_ownership={"count": 256})
        self.assertEqual(compiler_receipt.evidence_identity(snapshot, "1"),
                         legacy + "preparation_policy\tsnapshot-v1\nowned_phases\t256\n")
        # The declared policy is read as validate_closure reads it: a closure record without one is snapshot-v1.
        self.assertEqual(compiler_receipt.evidence_inventory(dict(base, closure={})), compiler_receipt.EVIDENCE_SNAPSHOT_INVENTORY)
        for broken in (dict(snapshot, phase_ownership={"count": 0}), dict(snapshot, phase_ownership={"count": 257}),
                       dict(snapshot, phase_ownership={"count": "14"}), dict(snapshot, phase_ownership={"count": True}),
                       dict(base, preparation_policy="snapshot-v1"), dict(base, closure={}),
                       dict(snapshot, preparation_policy="snapshot-v2")):
            self.assertEqual(compiler_receipt.evidence_identity(broken, "1"), "")

    def test_the_publisher_verifies_a_snapshot_bundle_under_its_route(self) -> None:
        expected = {"repository": "buster14a/buster", "request_run_id": "91", "run_id": "92", "run_attempt": "1",
                    "trusted_revision": "9" * 40, "base": self.base, "head": self.head, "base_tree": self.trees[1],
                    "head_tree": self.trees[0]}
        corpus = compiler_receipt.THROUGHPUT_PROFILE
        receipt = {"preparation_policy": "snapshot-v1", "closure": {"policy": "snapshot-v1"}, "phase_ownership": {"count": 14},
                   "evidence_bundle": {"state": "sealed", "inventory": compiler_receipt.EVIDENCE_SNAPSHOT_INVENTORY,
                                       "manifest_sha256": "a" * 64, "members": 3, "payload_bytes": 4}}
        script = ("import sys\n"
                  "lines = open(sys.argv[3]).read().splitlines()\n"
                  "assert lines[13:] == ['preparation_policy\\tsnapshot-v1', 'owned_phases\\t14'], lines\n"
                  "print('COMPILER_EVIDENCE_VERIFIED members=3 payload_bytes=4 manifest_sha256=' + 'a' * 64)\n")
        files = {"evidence-seal.txt": b"seal\n"}
        # Replay has its own tests (SealedReplayTest); the snapshot inventory carries every member it reads.
        with mock.patch.object(compiler_publish, "EVIDENCE_VERIFY_COMMAND", (sys.executable, "-c", script)), \
                mock.patch.object(compiler_publish, "sealed_lab_replay", lambda files: 10), \
                mock.patch.object(compiler_publish, "sealed_corpus_replay", lambda files, temporary: 12):
            verified = compiler_publish.verify_evidence_bundle({"sealed_files": files}, expected, corpus, "1", receipt)
            # The ordinary identity reproduces no snapshot manifest: the stand-in refuses it like the native verifier.
            ordinary = compiler_publish.verify_evidence_bundle({"sealed_files": files}, expected, corpus, "1")
        self.assertEqual(verified, {"state": "verified", "inventory": compiler_receipt.EVIDENCE_SNAPSHOT_INVENTORY,
                                    "members": 3, "payload_bytes": 4, "manifest_sha256": "a" * 64,
                                    "replay": {"lab_pairs": 10, "corpus_cases": 12}})
        self.assertEqual(ordinary["state"], "refused")
        self.assertEqual(compiler_publish.evidence_bundle_problems(receipt, verified), [])
        self.assertIn(compiler_receipt.EVIDENCE_SNAPSHOT_INVENTORY, compiler_publish.evidence_bundle_note(verified))
        claimed_ordinary = dict(receipt, evidence_bundle=dict(receipt["evidence_bundle"], inventory=compiler_receipt.EVIDENCE_INVENTORY))
        self.assertEqual(compiler_publish.evidence_bundle_problems(claimed_ordinary, verified),
                         ["receipt evidence_bundle does not name the verified sealed manifest"])
        uncounted = dict(receipt, phase_ownership={"state": "complete"})
        self.assertEqual(compiler_publish.verify_evidence_bundle({"sealed_files": files}, expected, corpus, "1", uncounted),
                         {"state": "refused", "reason": "the authorized identity cannot name a sealed bundle"})

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
        git = lambda *arguments: fixture_git(self.repo, *arguments)  # noqa: E731
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
        pull_tree = fixture_git(self.repo, "rev-parse", self.pull_head + "^{tree}")
        fixture_git(self.repo, "checkout", "-q", "--detach", self.pull_head)
        code, result, evidence = self.run_harness(self.pull_head, mode="pull", ref="refs/pull/7/head", head=self.pull_head,
                                                  **{"head-tree": pull_tree})
        self.assertEqual((code, result["state"], result["mode"]), (0, "measured", "pull"), result["reasons"])
        expected = dict(result["identity"])
        summary = json.loads((evidence / "lab" / "summary.json").read_text())
        self.assertEqual(compiler_publish.decide(expected, True, "success", result, summary, "", retained(evidence))[0], "success")
        # A main-mode publisher never accepts a pull-mode receipt.
        self.assertEqual(compiler_publish.decide(dict(expected, mode="main"), True, "success", result, summary, "", retained(evidence))[0],
                         "failure")

    def assert_unadmitted(self, code: int, result: dict, evidence: Path, reason: str) -> None:
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertEqual(len(result["reasons"]), 1, result["reasons"])
        self.assertIn(reason, result["reasons"][0])
        # Rejected before the expensive work: nothing built, no lab and no corpus.
        self.assertEqual((result["timings"]["build_seconds"], result["binaries"], result["lab"]), ({}, {}, {}))
        self.assertNotIn("throughput", result)
        self.assertFalse((evidence / "lab").exists())
        self.assertFalse((evidence / "throughput").exists())

    def test_base_predating_the_harness_floor_is_rejected_before_measurement(self) -> None:
        # #3714: the floor (here the pull head) is not in the base's history, as #3406's base lacks
        # the FAST/QUALITY retirement, so its --mode all cannot produce the v2 population.
        pull_tree = fixture_git(self.repo, "rev-parse", self.pull_head + "^{tree}")
        fixture_git(self.repo, "checkout", "-q", "--detach", self.pull_head)
        code, result, evidence = self.run_harness(self.pull_head, mode="pull", ref="refs/pull/7/head", head=self.pull_head,
                                                  floor=self.pull_head, **{"head-tree": pull_tree})
        self.assert_unadmitted(code, result, evidence, "unsupported historical throughput harness")
        self.assertIn(compiler_receipt.THROUGHPUT_PROFILE["name"], result["reasons"][0])

    def test_unplaceable_harness_floor_fails_closed(self) -> None:
        code, result, evidence = self.run_harness(self.head, floor="f" * 40)
        self.assert_unadmitted(code, result, evidence, "throughput harness admission could not place")

    def test_harness_floor_probe_faults_retain_a_failed_receipt(self) -> None:
        # A timed-out or unlaunchable floor probe is a retained refusal, never an escaped exception.
        real = compiler_compare.captured_run
        for fault in (subprocess.TimeoutExpired(["git"], 120), FileNotFoundError("git")):
            def probe(argv, **options):
                if argv[3:5] == ["merge-base", "--is-ancestor"] and argv[5] == self.base:
                    raise fault
                return real(argv, **options)
            with self.subTest(fault=type(fault).__name__), mock.patch.object(compiler_compare, "captured_run", probe):
                code, result, evidence = self.run_harness(self.head)
                self.assert_unadmitted(code, result, evidence, "throughput harness admission probe failed")
                self.assertIn(type(fault).__name__, result["reasons"][0])

    def test_harness_floor_is_the_fast_quality_retirement(self) -> None:
        # #3406's historical base 3bf9d004 predates the floor; current history contains it.
        floor = compiler_receipt.THROUGHPUT_HARNESS_FLOOR
        self.assertRegex(floor, "^[0-9a-f]{40}$")
        historical = "3bf9d004f2d1f6b4312e02825f17c56d7f3da14c"
        root = Path(__file__).resolve().parents[2]
        present = [subprocess.run(["git", "-C", str(root), "cat-file", "-e", f"{commit}^{{commit}}"],
                                  capture_output=True, check=False).returncode == 0 for commit in (floor, historical)]
        if not all(present):
            self.skipTest("this checkout lacks the floor or #3406's base commit")
        self.assertEqual(compiler_compare.throughput_harness_problem(root, "HEAD"), "")
        self.assertIn("unsupported historical throughput harness",
                      compiler_compare.throughput_harness_problem(root, historical))

    def test_native_evidence_inventory_matches_the_receipt_contract(self) -> None:
        # tools/compiler_evidence.c derives required raw members from the same admitted profiles (#2918).
        import re
        source = (Path(__file__).resolve().parents[2] / "tools" / "compiler_evidence.c").read_text()
        def table(name: str) -> list[str]:
            found = re.search(r"compiler_evidence_" + name + r"\[\] = \{(.*?)\};", source, re.S)
            self.assertIsNotNone(found, name)
            return re.findall(r'S8_INITIALIZER\("([^"]+)"\)', found.group(1))
        self.assertEqual(table("workloads"), compiler_receipt.THROUGHPUT_PROFILE["workloads"])
        self.assertEqual(table("lab_fixed"), ["lab/" + name for name in compiler_receipt.EVIDENCE_LAB_FIXED])
        self.assertEqual(table("lab_logs"), list(compiler_receipt.EVIDENCE_LAB_LOGS))
        self.assertEqual(table("corpus_core"), ["throughput/" + name for name in compiler_receipt.EVIDENCE_CORPUS_CORE])
        self.assertEqual(table("omission_reasons"), list(compiler_receipt.EVIDENCE_OMISSION_REASONS))
        self.assertEqual(re.findall(r'S8_INITIALIZER\("([a-z_]+)"\), \'', source), [*compiler_receipt.EVIDENCE_IDENTITY_KEYS,
                                                                                     *compiler_receipt.EVIDENCE_ROUTE_KEYS])
        self.assertEqual(table("closure_members"), list(compiler_receipt.EVIDENCE_CLOSURE_MEMBERS))
        self.assertEqual(table("phase_suffixes"), [suffix for suffix in compiler_receipt.EVIDENCE_PHASE_SUFFIXES if suffix])
        self.assertIn('S8_INITIALIZER(""), S8_INITIALIZER(".argv")', source)
        self.assertIn(f"#define COMPILER_EVIDENCE_PHASE_LIMIT {compiler_owned_phase.PHASE_LIMIT}ull", source)
        for name in ("INVENTORY", "SNAPSHOT_INVENTORY", "MANIFEST", "SEAL", "OMISSIONS", "OMISSIONS_SCHEMA"):
            self.assertIn(f'#define COMPILER_EVIDENCE_{name} "{getattr(compiler_receipt, "EVIDENCE_" + name)}"', source)
        self.assertIn(f"#define COMPILER_EVIDENCE_OMISSION_EXAMPLES {compiler_receipt.EVIDENCE_OMISSION_EXAMPLES}ull", source)
        self.assertIn(f'#define COMPILER_EVIDENCE_HARNESS_REVISION "{compiler_receipt.THROUGHPUT_HISTORICAL_HARNESS_REVISION}"', source)
        for name in (compiler_receipt.PROFILE["name"], compiler_receipt.THROUGHPUT_PROFILE["name"],
                     compiler_receipt.THROUGHPUT_HISTORICAL_PROFILE["name"]):
            self.assertIn(f'S8("{name}")', source)

    def historical_fixture(self, extra_lines: tuple[str, ...] = ()) -> tuple[str, str, str]:
        """(pinned harness commit, head commit, head tree) for the #3714 historical recipe.

        The pinned commit's fake corpus also writes pinned.txt, so the export shows which harness ran.
        """
        git = lambda *arguments: fixture_git(self.repo, *arguments)  # noqa: E731
        git("checkout", "-q", "--detach", self.base)
        marker = "open(os.path.join(output, 'pinned.txt'), 'w').write('pinned harness')\nsys.exit("
        self.assertEqual(FAKE_THROUGHPUT.count("\nsys.exit("), 1)
        (self.repo / "throughput_fake.py").write_text(FAKE_THROUGHPUT.replace("\nsys.exit(", "\n" + marker))
        git("commit", "-qam", "pinned harness")
        pinned = git("rev-parse", "HEAD")
        git("checkout", "-q", "--detach", self.pull_head)
        request = self.repo / compiler_receipt.ANALYZER_REQUEST_PATH
        request.parent.mkdir(parents=True, exist_ok=True)
        request.write_text("".join(line + "\n" for line in (compiler_receipt.THROUGHPUT_HISTORICAL_REQUEST_LINE,
                                                            *extra_lines)))
        git("add", compiler_receipt.ANALYZER_REQUEST_PATH)
        git("commit", "-qm", "request the historical corpus")
        return pinned, git("rev-parse", "HEAD"), git("rev-parse", "HEAD^{tree}")

    def run_historical(self, pinned: str, head: str, tree: str, admission: str = "historical",
                       floor: str | None = None) -> tuple[int, dict, Path]:
        # The pull head stands in for a floor the base predates.
        with mock.patch.object(compiler_compare, "THROUGHPUT_HISTORICAL_HARNESS_REVISION", pinned):
            return self.run_harness(head, mode="pull", ref="refs/pull/7/head", head=head, floor=floor or self.pull_head,
                                    **{"head-tree": tree, "pull-head": head, "harness-admission": admission})

    def test_historical_recipe_measures_the_corpus_with_the_pinned_harness(self) -> None:
        pinned, head, tree = self.historical_fixture()
        code, result, evidence = self.run_historical(pinned, head, tree)
        self.assertEqual((code, result["state"]), (0, "measured"), result["reasons"])
        self.assertEqual(result["throughput_profile"], compiler_receipt.THROUGHPUT_HISTORICAL_PROFILE)
        self.assertEqual(result["throughput_historical"],
                         {"harness_revision": pinned, "observed_revision": pinned, "status": "prepared"})
        # The corpus ran from the pinned worktree; the candidate checkout kept the base tree.
        self.assertEqual((evidence / "throughput" / "pinned.txt").read_text(), "pinned harness")
        self.assertEqual((self.repo / "compiler.txt").read_text(), "base compiler\n")
        summary = json.loads((evidence / "lab" / "summary.json").read_text())
        expected = dict(result["identity"])
        historical = compiler_receipt.THROUGHPUT_HISTORICAL_PROFILE
        with mock.patch.object(compiler_publish, "THROUGHPUT_HISTORICAL_HARNESS_REVISION", pinned):
            decided = compiler_publish.decide(expected, True, "success", result, summary, "", retained(evidence),
                                              expected_throughput_profile=historical)
            self.assertEqual(decided[0], "success", decided[2])
            # It discharges only its own inventory row, never the v2 corpus row.
            rows = compiler_receipt.covered_inventory_rows(result, "success", "pull")["rows"]
            self.assertEqual(rows["build:bench_throughput@throughput-corpus-historical"], "discharged")
            self.assertEqual(rows["build:bench_throughput"], "not discharged: not requested")
            # Without the hosted historical admission the same receipt never publishes.
            self.assertEqual(compiler_publish.decide(expected, True, "success", result, summary, "",
                                                     retained(evidence))[0], "failure")
        # Nor does a receipt whose harness was not the pinned revision.
        decided = compiler_publish.decide(expected, True, "success", result, summary, "", retained(evidence),
                                          expected_throughput_profile=historical)
        self.assertEqual(decided[0], "failure")
        self.assertTrue(any("pinned revision" in reason for reason in decided[2]), decided[2])

    def test_historical_recipe_refusals_happen_before_any_build(self) -> None:
        pinned, head, tree = self.historical_fixture()
        cases = (
            # The recipe on a base that already carries the floor.
            ({"floor": self.base}, "already carries"),
            # The host and the hosted disposition disagree.
            ({"admission": "admitted"}, "hosted harness admission 'admitted'"),
        )
        for change, reason in cases:
            with self.subTest(reason=reason):
                code, result, evidence = self.run_historical(pinned, head, tree, **change)
                self.assert_unadmitted(code, result, evidence, reason)
        # An unpreparable pinned revision fails before any build.
        code, result, evidence = self.run_historical("f" * 40, head, tree)
        self.assert_unadmitted(code, result, evidence, "historical corpus harness worktree exited")
        self.assertEqual(result["throughput_historical"]["status"], "failed")

    def test_historical_recipe_refuses_every_other_opt_in_leg_before_preparation(self) -> None:
        # Each fresh opt-in selector beside the historical one would still run the base's own harness.
        for line in (compiler_receipt.WILD_LINKER_REQUEST_LINE, compiler_receipt.THROUGHPUT_REAL_SOURCE_REQUEST_LINE,
                     compiler_receipt.PRODUCTION_PROFILE_REQUEST_LINE, compiler_receipt.COMPILER_PROBES_REQUEST_LINE,
                     compiler_receipt.FRONTEND_SELECTOR_REQUEST_LINE, compiler_receipt.FRONTEND_NESTING_REQUEST_LINE,
                     compiler_receipt.AST_PILOT_ACCEPTANCE_REQUEST_LINE):
            with self.subTest(line=line):
                fixture_git(self.repo, "checkout", "-q", "main")
                pinned, head, tree = self.historical_fixture((line,))
                with mock.patch.object(compiler_compare, "prepare_historical_harness",
                                       side_effect=AssertionError("prepared beside another opt-in leg")):
                    code, result, evidence = self.run_historical(pinned, head, tree)
                self.assertEqual((code, result["state"]), (1, "failed"))
                self.assertTrue(any("cannot be combined" in reason for reason in result["reasons"]), result["reasons"])
                self.assertEqual((result["timings"]["build_seconds"], result["binaries"], result["lab"]), ({}, {}, {}))
                self.assertNotIn("throughput_historical", result)

    def test_hosted_historical_admission_without_the_request_is_refused(self) -> None:
        pull_tree = fixture_git(self.repo, "rev-parse", self.pull_head + "^{tree}")
        fixture_git(self.repo, "checkout", "-q", "--detach", self.pull_head)
        code, result, evidence = self.run_harness(self.pull_head, mode="pull", ref="refs/pull/7/head",
                                                  head=self.pull_head, **{"head-tree": pull_tree,
                                                                          "harness-admission": "historical"})
        self.assert_unadmitted(code, result, evidence, "hosted harness admission 'historical'")

    def test_publisher_reports_an_undispatched_unsupported_base_explicitly(self) -> None:
        conclusion, title, reasons = compiler_publish.decide({}, False, "skipped", None, None, "",
                                                             dispatch_refusal="unsupported historical throughput harness")
        self.assertEqual((conclusion, title, reasons), ("failure", "Not benchmarked: unsupported historical harness",
                                                        ["unsupported historical throughput harness"]))

    def scaling_head(self) -> tuple[str, str]:
        """A pull request head that adds the scaling request on top of the candidate; (commit, tree)."""
        git = lambda *arguments: fixture_git(self.repo, *arguments)  # noqa: E731
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
        pull_tree = fixture_git(self.repo, "rev-parse", self.pull_head + "^{tree}")
        fixture_git(self.repo, "checkout", "-q", "--detach", self.pull_head)
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
        self.assertEqual((result["throughput"]["exit"], len(result["throughput"]["cases"])), (0, 12))
        metadata = retained(evidence)["metadata"]
        self.assertEqual([row["sha256"] for row in metadata["compiler_provenance"]],
                         [result["binaries"][role]["sha256"] for role in ("baseline", "candidate")])
        self.assertIn("throughput_seconds", result["timings"])

    def test_complete_corpus_exit_one_regression_is_report_only(self) -> None:
        code, result, evidence = self.run_harness(self.head, corpus_behavior="regression")
        self.assertEqual((code, result["state"], result["throughput"]["exit"]), (0, "measured", 1), result["reasons"])
        self.assertEqual(result["throughput"]["confirmed_regressions"], 12)
        raw = retained(evidence)
        self.assertEqual(compiler_receipt.classify_throughput_exit(1, raw["summary"], raw["metadata"], result["binaries"]), [])

    def test_failed_or_partial_corpus_fails_the_receipt(self) -> None:
        for behavior, expected in (("fail", "bench_throughput run exited 3"), ("partial", "workload/mode cells"),
                                   ("partial-exit1", "workload/mode cells"), ("missing-exit1", "metadata.json"),
                                   ("badexit", "no counted confirmed corpus regression")):
            with self.subTest(behavior=behavior):
                # Each run leaves the frozen base checked out.
                fixture_git(self.repo, "checkout", "-q", "--detach", self.head)
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
        pull_tree = fixture_git(self.repo, "rev-parse", self.pull_head + "^{tree}")
        fixture_git(self.repo, "checkout", "-q", "--detach", self.pull_head)
        code, result, _ = self.run_harness(self.pull_head, head=self.pull_head, pull="0", **{"head-tree": pull_tree})
        self.assertEqual((code, result["state"]), (0, "measured"), result["reasons"])
        self.assertEqual(result["identity"]["pull_head"], self.pull_head)

    def test_moved_pull_request_is_superseded_without_building(self) -> None:
        pull_tree = fixture_git(self.repo, "rev-parse", self.pull_head + "^{tree}")
        fixture_git(self.repo, "checkout", "-q", "--detach", self.pull_head)
        code, result, _ = self.run_harness("", mode="pull", ref="refs/pull/7/head", head=self.pull_head,
                                           **{"head-tree": pull_tree})
        self.assertEqual((code, result["state"]), (0, "superseded"))
        self.assertEqual(result["timings"]["build_seconds"], {})

    def test_filesystem_faults_keep_an_attributable_incomplete_receipt(self) -> None:
        # #2927: an error after a completed phase must not erase timings, identity or the failing stage.
        for target, phase in (("shutil.copyfile", "build-baseline"), ("compiler_compare.collect_evidence", "lab-evidence"),
                              ("compiler_compare.sha256", "build-baseline")):
            fixture_git(self.repo, "checkout", "-q", "--detach", self.head)
            with self.subTest(target=target), mock.patch(target, side_effect=OSError("injected")):
                code, result, _ = self.run_harness(self.head)
                self.assertEqual((code, result["state"]), (1, "failed"))
                self.assertIn(f"attempt aborted in phase {phase}", " ".join(result["reasons"]))
                self.assertIn("injected", " ".join(result["reasons"]))
                self.assertEqual(result["identity"]["head"], self.head)
                self.assertIn("baseline", result["timings"]["build_seconds"])

    def test_omitted_required_evidence_fails_the_receipt_and_is_listed(self) -> None:
        with mock.patch.object(compiler_compare, "EVIDENCE_MEMBER_LIMIT", 10):
            code, result, evidence = self.run_harness(self.head)
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertIn("required evidence lab/summary.json not exported", " ".join(result["reasons"]))
        self.assertEqual(result["evidence_omissions"]["lab"]["shown"][0]["path"], "summary.json")
        self.assertFalse((evidence / "lab" / "summary.json").exists())

    def run_with_tampered_export(self, tamper: dict, **change) -> tuple[int, dict, Path]:
        """Run the harness while each export is altered right after it is copied: {destination name: action}.

        The measurement trees stay valid, so only a classifier that reads the exported bytes can notice (#2929).
        """
        real = compiler_compare.export_tree

        def export(source: Path, destination: Path, evidence: Path, ignore: tuple, required: tuple, **options) -> tuple:
            outcome = real(source, destination, evidence, ignore, required, **options)
            key = destination.relative_to(evidence).as_posix()
            if key in tamper:
                tamper[key](destination)
            return outcome

        with mock.patch.object(compiler_compare, "export_tree", export):
            return self.run_harness(self.head, **change)

    def test_classification_reads_the_exported_bytes_not_the_source_summaries(self) -> None:
        def truncate_cells(directory: Path) -> None:
            path = directory / "summary.json"
            document = json.loads(path.read_text())
            document["comparisons"] = document["comparisons"][:2]
            path.write_text(json.dumps(document))

        def hash_mismatch(directory: Path) -> None:
            path = directory / "metadata.json"
            document = json.loads(path.read_text())
            document["compiler_provenance"][0]["sha256"] = "0" * 64
            path.write_text(json.dumps(document))

        def invalid_json(directory: Path) -> None:
            (directory / "summary.json").write_text("TAMPERED-EXPORT")

        def remove_member(directory: Path) -> None:
            (directory / "metadata.json").unlink()

        def symlink_member(directory: Path) -> None:
            (directory / "summary.json").unlink()
            (directory / "summary.json").symlink_to(self.root / "lab.py")

        def empty_member(directory: Path) -> None:
            (directory / "summary.json").write_bytes(b"")

        cases = (("truncated", truncate_cells, "workload/mode cells"),
                 ("provenance", hash_mismatch, "throughput"),
                 ("invalid", invalid_json, "exported evidence throughput/summary.json unreadable"),
                 ("removed", remove_member, "exported evidence throughput/metadata.json unreadable"),
                 ("symlink", symlink_member, "exported evidence throughput/summary.json unreadable: not a regular file"),
                 ("empty", empty_member, "exported evidence throughput/summary.json unreadable"))
        for label, action, expected in cases:
            with self.subTest(label=label):
                fixture_git(self.repo, "checkout", "-q", "--detach", self.head)
                code, result, _ = self.run_with_tampered_export({"throughput": action})
                self.assertEqual((code, result["state"]), (1, "failed"), result["reasons"])
                self.assertIn(expected, " ".join(result["reasons"]))
        # The untampered export still classifies clean.
        fixture_git(self.repo, "checkout", "-q", "--detach", self.head)
        code, result, _ = self.run_with_tampered_export({})
        self.assertEqual((code, result["state"], result["reasons"]), (0, "measured", []))

    def test_lab_classification_reads_the_exported_summary(self) -> None:
        def alter(directory: Path) -> None:
            path = directory / "summary.json"
            document = json.loads(path.read_text())
            document["candidate"]["failed"] = 3
            path.write_text(json.dumps(document))

        def remove(directory: Path) -> None:
            (directory / "summary.json").unlink()

        for label, action, expected in (("altered", alter, "candidate has failed or missing timed runs"),
                                        ("removed", remove, "lab summary unreadable")):
            with self.subTest(label=label):
                fixture_git(self.repo, "checkout", "-q", "--detach", self.head)
                code, result, _ = self.run_with_tampered_export({"lab": action})
                self.assertEqual((code, result["state"]), (1, "failed"), result["reasons"])
                self.assertIn(expected, " ".join(result["reasons"]))

    def test_scaling_classification_reads_the_exported_bundle(self) -> None:
        head, tree = self.scaling_head()
        real = compiler_compare.export_tree

        def metadata_hash(directory: Path) -> None:
            path = directory / "scaling-metadata.json"
            document = json.loads(path.read_text())
            document["compiler_sha256"] = "0" * 64
            path.write_text(json.dumps(document))

        def invalid_summary(directory: Path) -> None:
            (directory / "scaling.json").write_text("TAMPERED-EXPORT")

        def altered_status(directory: Path) -> None:
            path = directory / "scaling.json"
            document = json.loads(path.read_text())
            document["status"] = "invalid"
            path.write_text(json.dumps(document))

        for label, action, expected in (("hash", metadata_hash, "compiler"),
                                        ("invalid", invalid_summary, "exported evidence scaling/cores/scaling.json unreadable"),
                                        ("status", altered_status, "scaling")):
            def export(source: Path, destination: Path, evidence: Path, ignore: tuple, required: tuple, **options) -> tuple:
                outcome = real(source, destination, evidence, ignore, required, **options)
                if destination.relative_to(evidence).as_posix() == "scaling/cores":
                    action(destination)
                return outcome

            with self.subTest(label=label), mock.patch.object(compiler_compare, "export_tree", export):
                fixture_git(self.repo, "checkout", "-q", "--detach", head)
                code, result, _ = self.run_harness(head, mode="pull", ref="refs/pull/7/head", head=head,
                                                   **{"pull-head": head, "head-tree": tree})
                self.assertEqual((code, result["state"]), (1, "failed"), result["reasons"])
                self.assertIn(expected, " ".join(result["reasons"]))

    def test_read_exported_json_is_bounded_and_never_raises(self) -> None:
        directory = self.root / "exported"
        directory.mkdir()
        good = directory / "good.json"
        good.write_text('{"a": 1}')
        self.assertEqual(compiler_compare.read_exported_json(good), ({"a": 1}, ""))
        deep = directory / "deep.json"
        deep.write_text("[" * 100000 + "]" * 100000)
        (directory / "binary.json").write_bytes(b"\xff\xfe")
        (directory / "link.json").symlink_to(good)
        (directory / "folder.json").mkdir()
        for name in ("deep.json", "binary.json", "link.json", "folder.json", "absent.json"):
            with self.subTest(name=name):
                document, problem = compiler_compare.read_exported_json(directory / name)
                self.assertEqual(document, None)
                self.assertTrue(problem)
        with mock.patch.object(compiler_compare, "EVIDENCE_MEMBER_LIMIT", 4):
            document, problem = compiler_compare.read_exported_json(good)
        self.assertEqual(document, None)
        self.assertIn("member limit", problem)

    def test_missing_cmake_cache_is_a_build_failure_not_a_crash(self) -> None:
        (self.repo / "build.sh").write_text("#!/usr/bin/env bash\nmkdir -p build/Release\ncp compiler.txt build/Release/ide\n")
        fixture_git(self.repo, "update-index", "--assume-unchanged", "build.sh")
        code, result, _ = self.run_harness(self.head)
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertIn("not tests-off", " ".join(result["reasons"]))

    def test_interruption_between_lab_and_corpus_leaves_the_lab_outcome(self) -> None:
        with mock.patch.object(compiler_compare, "measure_throughput", side_effect=KeyboardInterrupt()):
            with self.assertRaises(KeyboardInterrupt):
                self.run_harness(self.head)
        result = json.loads((self.root / "evidence" / "receipt.json").read_text())
        self.assertEqual((result["state"], result["lab"]["exit"]), ("failed", 0))
        self.assertIn("attempt aborted in phase throughput", " ".join(result["reasons"]))
        self.assertTrue((self.root / "evidence" / "lab" / "summary.json").is_file())

    def test_kill_leaves_the_last_checkpoint_and_no_checkpoint_is_measured(self) -> None:
        states = []
        real = compiler_compare.checkpoint

        def spy(receipt: dict, evidence: Path, phase: str) -> str:
            problem = real(receipt, evidence, phase)
            shown = json.loads((evidence / "receipt.json").read_text())
            states.append((phase, shown["state"], shown["phase"]))
            return problem

        with mock.patch.object(compiler_compare, "checkpoint", spy):
            code, result, _ = self.run_harness(self.head)
        self.assertEqual((code, result["state"]), (0, "measured"))
        self.assertEqual([phase for phase, _, _ in states][:3], ["build-baseline", "build-candidate", "build-closure"])
        self.assertTrue({"lab", "throughput", "validate"} <= {phase for phase, _, _ in states})
        self.assertEqual({state for _, state, _ in states}, {"failed"})
        self.assertNotIn("phase", result)

    def test_unpersistable_final_receipt_is_never_measured(self) -> None:
        with mock.patch.object(compiler_compare, "write_receipt", return_value="final receipt not persisted: disk full"):
            code, result, _ = self.run_harness(self.head)
        self.assertEqual(code, 1)
        self.assertEqual(result["state"], "failed")  # the last checkpoint, not a measured receipt

    def test_identity_mismatch_and_failed_build_are_failures(self) -> None:
        code, result, _ = self.run_harness(self.head, base="8" * 40)
        self.assertEqual((code, result["state"]), (1, "failed"))
        (self.repo / "build.sh").write_text("#!/usr/bin/env bash\nexit 3\n")
        fixture_git(self.repo, "update-index", "--assume-unchanged", "build.sh")
        code, result, _ = self.run_harness(self.head)
        self.assertEqual((code, result["state"]), (1, "failed"))
        self.assertIn("failed with exit 3", " ".join(result["reasons"]))



class InlineAcceptanceTest(unittest.TestCase):

    def test_publisher_revalidates_raw_inline_documents(self) -> None:
        head, candidate, off, on = "a" * 40, "b" * 64, "c" * 64, "d" * 64

        def raw_profile(a_sha: str, b_sha: str) -> dict:
            summary = {"schema": inline_acceptance.UARCH_SCHEMA,
                       "plan": {"pairs": 12, "complete_pairs": 12, "order": "ABBA", "fresh_copy": True},
                       "baseline": {"sha256": a_sha, "failed": 0, "deterministic": True, "runs": 12},
                       "candidate": {"sha256": b_sha, "failed": 0, "deterministic": True, "runs": 12},
                       "metrics": {"wall": {"a_median": 1.0, "b_median": 0.9, "ratio": 0.9},
                                   "instructions": {"a_median": 100.0, "b_median": 90.0, "ratio": 0.9},
                                   "peak_rss": {"a_median": 1000.0, "b_median": 900.0, "ratio": 0.9}},
                       "code_bytes": {"a_value": 1000, "b_value": 900, "ratio": 0.9},
                       "counters": {"perf_stat": True, "reason": "available"},
                       "outputs_identical": True}
            meta = {"config": {"canonical_inline_pair": True,
                               "extra_by_variant": {"a": [], "b": ["-fcanonical-inline"]},
                               "pairs": 12, "warmups": 1, "cpu": 2, "extra": []}}
            labs = {"a": {"config": {"extra": [], "cpu": 2}},
                    "b": {"config": {"extra": ["-fcanonical-inline"], "cpu": 2}}}
            return {"summary": summary, "meta": meta, "a": labs["a"], "b": labs["b"]}

        first, second = raw_profile(candidate, candidate), raw_profile(off, on)
        stage1_metrics = inline_acceptance.metrics(first["summary"])
        selfhost_metrics = inline_acceptance.metrics(second["summary"])
        acceptance = {"schema": compiler_receipt.INLINE_ACCEPTANCE_SCHEMA, "status": "complete",
                      "source_revision": head,
                      "profile": {**compiler_receipt.INLINE_ACCEPTANCE_PROFILE,
                                  "source_sha256": "e" * 64, "cpu": 2},
                      "candidate_compiler": {"sha256": candidate, "size_bytes": 10},
                      "stage1_compilers": {"off": {"sha256": off, "size_bytes": 11},
                                           "on": {"sha256": on, "size_bytes": 12}},
                      "fixed_point": {"off": True, "on": True},
                      "stage1": {"metrics": stage1_metrics, "outputs_identical": True},
                      "selfhost_runtime": {"metrics": selfhost_metrics, "outputs_identical": True}}
        stage_ids = {"off": {"sha256": off, "size_bytes": 11},
                     "on": {"sha256": on, "size_bytes": 12}}
        bundle = {"acceptance": acceptance, "stage1_ids": stage_ids,
                  "stage1_summary": first["summary"], "stage1_meta": first["meta"],
                  "stage1_a": first["a"], "stage1_b": first["b"],
                  "selfhost_ids": stage_ids,
                  "selfhost_summary": second["summary"], "selfhost_meta": second["meta"],
                  "selfhost_a": second["a"], "selfhost_b": second["b"]}
        requested = {"requested": True, "request_line": compiler_receipt.INLINE_ACCEPTANCE_REQUEST_LINE,
                     "profile": compiler_receipt.INLINE_ACCEPTANCE_PROFILE, "summary": acceptance,
                     "status": "complete", "exit": 0}
        self.assertEqual(compiler_publish.validate_inline_bundle(requested, bundle, head, candidate), [])
        broken = json.loads(json.dumps(bundle))
        broken["selfhost_meta"]["config"]["extra_by_variant"]["b"] = []
        errors = compiler_publish.validate_inline_bundle(requested, broken, head, candidate)
        self.assertTrue(any("raw issue #48 profile" in error for error in errors), errors)
        broken_ids = json.loads(json.dumps(bundle))
        broken_ids["selfhost_ids"]["on"]["sha256"] = "f" * 64
        self.assertTrue(any("do not reproduce" in error for error in
                            compiler_publish.validate_inline_bundle(requested, broken_ids, head, candidate)))
        incomplete = dict(requested, status="failed")
        self.assertTrue(any("did not complete" in error for error in
                            compiler_publish.validate_inline_bundle(incomplete, bundle, head, candidate)))
        self.assertTrue(compiler_publish.validate_inline_bundle(requested, None, head, candidate))


    def test_request_selector_requires_exact_line(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            candidate = Path(temporary)
            request = candidate / "benchmarks/9700x/compiler-compare.request"
            request.parent.mkdir(parents=True)
            request.write_text("canonical-inline-self-host-v1-extra\n", encoding="utf-8")
            self.assertFalse(compiler_receipt.inline_acceptance_requested(candidate))
            request.write_text("canonical-inline-self-host-v1\n", encoding="utf-8")
            self.assertTrue(compiler_receipt.inline_acceptance_requested(candidate))

    def test_compare_command_is_fixed_and_does_not_accept_request_args(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with mock.patch.object(inline_acceptance.subprocess, "run") as run:
                inline_acceptance.run_compare(root / "uarch_lab.py", root / "off", root / "on", root,
                                              2, root / "out", "perf")
            argv = run.call_args.args[0]
            self.assertIn("--canonical-inline-pair", argv)
            self.assertEqual(argv[argv.index("--pairs") + 1], "12")
            self.assertNotIn("--", argv)

    @staticmethod
    def valid_uarch(root: Path, instructions: object = 0.95) -> tuple[str, str]:
        root.mkdir(parents=True)
        a_sha, b_sha = "a" * 64, "b" * 64
        for key, extra in (("a", []), ("b", ["-fcanonical-inline"])):
            (root / key).mkdir()
            (root / key / "lab.json").write_text(json.dumps({"config": {"extra": extra, "cpu": 2}}), encoding="utf-8")
        (root / "compare.json").write_text(json.dumps({"config": {
            "canonical_inline_pair": True, "extra_by_variant": {"a": [], "b": ["-fcanonical-inline"]},
            "pairs": 12, "warmups": 1, "cpu": 2, "extra": []}}),
            encoding="utf-8")
        insn = {"ratio": instructions, "a_median": 100.0 if instructions is not None else None,
                "b_median": 100.0 * instructions if instructions is not None else None}
        summary = {"schema": inline_acceptance.UARCH_SCHEMA,
                   "plan": {"pairs": 12, "complete_pairs": 12, "order": "ABBA", "fresh_copy": True},
                   "baseline": {"sha256": a_sha, "failed": 0, "deterministic": True, "runs": 12},
                   "candidate": {"sha256": b_sha, "failed": 0, "deterministic": True, "runs": 12},
                   "metrics": {"wall": {"a_median": 1.0, "b_median": 0.9, "ratio": 0.9},
                               "instructions": insn, "peak_rss": {"ratio": 1.0}},
                   "code_bytes": {"a_value": 100, "b_value": 104, "ratio": 1.04},
                   "counters": {"perf_stat": instructions is not None,
                                "reason": "counter unavailable" if instructions is None else "usable"}}
        (root / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
        return a_sha, b_sha

    def test_loader_requires_wall_code_size_and_frozen_binary_identities(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "profile"
            a_sha, b_sha = self.valid_uarch(root)
            inline_acceptance.load_complete(root, a_sha, b_sha)
            with self.assertRaisesRegex(RuntimeError, "binary identity"):
                inline_acceptance.load_complete(root, "c" * 64, b_sha)
            summary = json.loads((root / "summary.json").read_text(encoding="utf-8"))
            summary["code_bytes"]["a_value"] = None
            (root / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "code-byte"):
                inline_acceptance.load_complete(root, a_sha, b_sha)

    def test_instruction_na_requires_explicit_unavailable_counter_reason(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "profile"
            a_sha, b_sha = self.valid_uarch(root, instructions=None)
            summary = json.loads((root / "summary.json").read_text(encoding="utf-8"))
            summary["counters"] = {"perf_stat": False, "reason": "perf_event_paranoid"}
            (root / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
            inline_acceptance.load_complete(root, a_sha, b_sha)
            summary["counters"] = {"perf_stat": True, "reason": "usable"}
            (root / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "NA without"):
                inline_acceptance.load_complete(root, a_sha, b_sha)

    def test_receipt_checks_exact_profile_fixed_points_and_required_measurements(self) -> None:
        stage = {"metrics": {
            "wall": {"a_median": 1.0, "b_median": 1.0, "ratio": 1.0},
            "instructions": {"a_median": 10.0, "b_median": 10.0, "ratio": 1.0,
                             "counter_availability": {"perf_stat": True, "reason": "usable"}},
            "peak_rss": {"ratio": 1.0},
            "code_bytes": {"a_value": 100, "b_value": 100, "ratio": 1.0}}}
        summary = {"schema": compiler_receipt.INLINE_ACCEPTANCE_SCHEMA, "status": "complete",
                   "source_revision": "a" * 40,
                   "profile": {**compiler_receipt.INLINE_ACCEPTANCE_PROFILE, "source_sha256": "b" * 64, "cpu": 2},
                   "candidate_compiler": {"sha256": "c" * 64, "size_bytes": 1},
                   "fixed_point": {"off": True, "on": True}, "stage1": stage, "selfhost_runtime": stage}
        self.assertEqual(compiler_receipt.validate_inline_acceptance(summary, "a" * 40, "c" * 64), [])
        self.assertTrue(compiler_receipt.validate_inline_acceptance(summary, "a" * 40, "d" * 64))
        summary["stage1"]["metrics"]["code_bytes"]["a_value"] = None
        self.assertTrue(compiler_receipt.validate_inline_acceptance(summary, "a" * 40, "c" * 64))

    def test_profile_uses_candidate_head_and_requires_same_mode_fixed_points(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "ide"
            candidate.write_bytes(b"candidate compiler")
            repo = root / "repo"
            source = repo / "src/buster/apps/ide/ide.c"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"int frozen_unity_source;\n")
            generated = repo / "build/generated"
            generated.mkdir(parents=True)
            (generated / "generated.h").write_bytes(b"/* generated for HEAD */\n")
            output = root / "profile"
            lab = root / "trusted-uarch.py"
            lab.write_bytes(b"trusted test stub")
            args = argparse.Namespace(lab=lab, repo_root=repo, candidate_ide=candidate,
                                      output=output, head_revision="a" * 40, cpu=2, perf="perf")
            def fake_run_compare(lab_path, compiler_a, compiler_b, repo_root, cpu, directory, perf):
                (directory / "a").mkdir(parents=True)
                (directory / "b").mkdir(parents=True)
                for name, payload in zip(("a", "b"), (b"stage1-off", b"stage1-on")):
                    (directory / name / "reference.exe").write_bytes(payload)
            complete = {"schema": inline_acceptance.UARCH_SCHEMA, "plan": {"pairs": 12, "complete_pairs": 12, "order": "ABBA", "fresh_copy": True},
                        "baseline": {"sha256": "a" * 64, "failed": 0, "deterministic": True, "runs": 12},
                        "candidate": {"sha256": "b" * 64, "failed": 0, "deterministic": True, "runs": 12},
                        "metrics": {"wall": {"a_median": 1.0, "b_median": 0.9, "ratio": 0.9},
                                    "instructions": {"a_median": 100.0, "b_median": 90.0, "ratio": 0.9},
                                    "peak_rss": {"a_median": 1000, "b_median": 900, "ratio": 0.9}},
                        "code_bytes": {"a_value": 1000, "b_value": 900, "ratio": 0.9},
                        "counters": {"perf_stat": True, "reason": "available"}}
            with mock.patch.object(inline_acceptance, "git", return_value="a" * 40), \
                 mock.patch.object(inline_acceptance, "run_compare", side_effect=fake_run_compare), \
                 mock.patch.object(inline_acceptance, "load_complete", return_value=complete):
                result = inline_acceptance.execute(args)
            self.assertEqual(result["source_revision"], args.head_revision)
            self.assertEqual(result["profile"]["pairs"], 12)
            self.assertEqual(result["profile"]["pairing"], "ABBA")
            self.assertEqual(result["fixed_point"], {"off": True, "on": True})
            stage1_ids = json.loads((output / "stage1/identities.json").read_text(encoding="utf-8"))
            stage2_ids = json.loads((output / "selfhost/identities.json").read_text(encoding="utf-8"))
            self.assertEqual(stage1_ids, result["stage1_compilers"])
            self.assertEqual(stage2_ids, stage1_ids)
            self.assertIn("build/generated", result["frozen_input_tree"])


def ast_pilot_raw_cell(cell: str, binary: str = "c" * 64, wall: float = 0.9, instructions: object = 0.9) -> dict:
    """One synthetic, complete raw uarch_lab cell of the #3397 profile."""
    pairs = compiler_receipt.AST_PILOT_ACCEPTANCE_PROFILE["pairs"]
    argv = ast_pilot_acceptance.cell_command(cell)
    labs = {}
    for key, extra in ast_pilot_acceptance.EXTRA_BY_VARIANT.items():
        command = ["/bin/ide"] + (argv[:-2] + extra + argv[-2:] if cell == "object" else argv + extra)
        labs[key] = {"config": {"extra": list(extra), "cpu": 2, "ast_pilot_cell": cell, "ide": "/bin/ide",
                                "command": " ".join(command)}}
    insn = {"ratio": instructions, "a_median": 1e10 if instructions is not None else None,
            "b_median": 1e10 * instructions if instructions is not None else None}
    summary = {"schema": ast_pilot_acceptance.UARCH_SCHEMA,
               "plan": {"pairs": pairs, "complete_pairs": pairs, "order": "ABBA", "fresh_copy": True},
               "baseline": {"sha256": binary, "failed": 0, "deterministic": True, "runs": pairs},
               "candidate": {"sha256": binary, "failed": 0, "deterministic": True, "runs": pairs},
               "metrics": {"wall": {"a_median": 2.0, "b_median": 2.0 * wall, "ratio": wall,
                                    "ci_low": wall - 0.01, "ci_high": wall + 0.005},
                           "instructions": insn, "peak_rss": {"a_median": 1e9, "b_median": 1.02e9, "ratio": 1.02}},
               "counters": {"perf_stat": instructions is not None,
                            "reason": "usable" if instructions is not None else "perf_event_paranoid"},
               "outputs_identical": True, "diagnostics_identical": True}
    meta = {"config": {"ast_pilot_pair": cell, "extra_by_variant": copy.deepcopy(ast_pilot_acceptance.EXTRA_BY_VARIANT),
                       "canonical_inline_pair": False, "command": " ".join(["IDE"] + argv), "pairs": pairs,
                       "warmups": 1, "cpu": 2, "extra": [], "profile_steps": [], "fresh_copy": True}}
    return {"summary": summary, "meta": meta, "a": labs["a"], "b": labs["b"]}


AST_PILOT_PROBE = {"command": compiler_receipt.AST_PILOT_COMPILE + ["-fsyntax-only", "-v"],
                   "a": {"exit": 0, "extra": [], "tree_lines": []},
                   "b": {"exit": 0, "extra": ["-fc-ast-pilot"], "tree_lines": [
                       "C_AST nodes=2776521 tokens=4180360 build_ns=65332858 retained_bytes=36094773 "
                       "transient_high_water=61008 column_copy_bytes=0 finalize_child_entries=0 layout=implicit"]}}


def ast_pilot_evidence(head: str = "a" * 40, binary: str = "c" * 64, **cell_change) -> tuple[dict, dict, dict]:
    """(receipt record, raw bundle, acceptance) built through the helper's own record and verdict code."""
    raw = {cell: ast_pilot_raw_cell(cell, binary, **cell_change.get(cell, {})) for cell in compiler_receipt.AST_PILOT_CELLS}
    cells = {cell: ast_pilot_acceptance.cell_record(raw[cell]["summary"]) for cell in raw}
    tree = ast_pilot_acceptance.validate_probe(AST_PILOT_PROBE)
    acceptance = {"schema": compiler_receipt.AST_PILOT_ACCEPTANCE_SCHEMA, "status": "complete", "source_revision": head,
                  "profile": {**compiler_receipt.AST_PILOT_ACCEPTANCE_PROFILE, "source_sha256": "e" * 64},
                  "candidate_compiler": {"sha256": binary, "size_bytes": 10}, "cells": cells, "tree": tree,
                  "verdict": ast_pilot_acceptance.verdict(cells, tree)}
    bundle = {"acceptance": acceptance, "probe": copy.deepcopy(AST_PILOT_PROBE)}
    for cell, documents in raw.items():
        for key, value in documents.items():
            bundle[f"{cell}_{key}"] = value
    record = {"requested": True, "request_line": compiler_receipt.AST_PILOT_ACCEPTANCE_REQUEST_LINE,
              "profile": compiler_receipt.AST_PILOT_ACCEPTANCE_PROFILE, "summary": acceptance,
              "status": "complete", "exit": 0}
    return json.loads(json.dumps(record)), json.loads(json.dumps(bundle)), acceptance


class AstPilotAcceptanceTest(unittest.TestCase):
    def test_publisher_revalidates_raw_cells_and_tree_probe(self) -> None:
        head, binary = "a" * 40, "c" * 64
        record, bundle, acceptance = ast_pilot_evidence(head, binary)
        self.assertEqual(compiler_publish.validate_ast_pilot_bundle(record, bundle, head, binary), [])
        self.assertEqual(acceptance["verdict"]["outcome"], "pass")

        def broken(change) -> list[str]:
            changed = copy.deepcopy(bundle)
            change(changed)
            return compiler_publish.validate_ast_pilot_bundle(record, changed, head, binary)
        cases = {
            "wrong flag": lambda value: value["syntax-only_meta"]["config"].update(
                extra_by_variant={"a": [], "b": ["-fcanonical-inline"]}),
            "flag on A": lambda value: value["object_a"]["config"].update(extra=["-fc-ast-pilot"]),
            "lab command": lambda value: value["object_b"]["config"].update(
                command=value["object_a"]["config"]["command"]),
            "linking command": lambda value: value["object_meta"]["config"].update(
                command=" ".join(["IDE"] + compiler_receipt.AST_PILOT_COMPILE + ["-o", "OUT"])),
            "cell swap": lambda value: value["object_meta"]["config"].update(ast_pilot_pair="syntax-only"),
            "wrong binary": lambda value: value["syntax-only_summary"]["candidate"].update(sha256="d" * 64),
            "cpu": lambda value: value["object_meta"]["config"].update(cpu=3),
            "pair count": lambda value: value["object_summary"]["plan"].update(pairs=12, complete_pairs=12),
            "missing cell": lambda value: [value.pop(f"object_{key}") for key in ("summary", "meta", "a", "b")],
            "A built a tree": lambda value: value["probe"]["a"].update(tree_lines=value["probe"]["b"]["tree_lines"]),
            "B built no tree": lambda value: value["probe"]["b"].update(tree_lines=[]),
            "retained bytes": lambda value: value["acceptance"]["tree"].update(retained_bytes=1),
            "metrics": lambda value: value["object_summary"]["metrics"]["wall"].update(ratio=1.2),
            "diagnostics": lambda value: value["object_summary"].update(diagnostics_identical=None),
        }
        for name, change in cases.items():
            with self.subTest(case=name):
                self.assertTrue(broken(change))
        self.assertTrue(compiler_publish.validate_ast_pilot_bundle(dict(record, status="failed"), bundle, head, binary))
        self.assertTrue(compiler_publish.validate_ast_pilot_bundle(dict(record, request_line="ast-pilot"), bundle, head, binary))
        self.assertTrue(compiler_publish.validate_ast_pilot_bundle(record, None, head, binary))
        self.assertTrue(compiler_publish.validate_ast_pilot_bundle(record, bundle, head, "d" * 64))
        self.assertEqual(compiler_publish.validate_ast_pilot_bundle(None, None, head, binary), [])

    def test_receipt_validator_needs_both_cells_frozen_profile_and_recomputed_verdict(self) -> None:
        head, binary = "a" * 40, "c" * 64
        unused_record, unused_bundle, acceptance = ast_pilot_evidence(head, binary)
        self.assertEqual(compiler_receipt.validate_ast_pilot_acceptance(acceptance, head, binary), [])
        for name, change in {
            "profile": lambda value: value["profile"].update(pairs=2),
            "extra profile key": lambda value: value["profile"].update(flags=["-O3"]),
            "missing cell": lambda value: value["cells"].pop("syntax-only"),
            "verdict": lambda value: value["verdict"].update(outcome="pass" if value["verdict"]["outcome"] != "pass" else "fail"),
            "revision": lambda value: value.update(source_revision="b" * 40),
            "binary": lambda value: value["candidate_compiler"].update(sha256="d" * 64),
            "no tree": lambda value: value["tree"].update(retained_bytes=0),
            "A tree": lambda value: value["tree"].update(a_tree_lines=1),
        }.items():
            changed = copy.deepcopy(acceptance)
            change(changed)
            with self.subTest(case=name):
                self.assertTrue(compiler_receipt.validate_ast_pilot_acceptance(changed, head, binary))

    def test_budget_verdict_follows_the_ast_md_adoption_budget(self) -> None:
        cases = (({}, "pass"),
                 ({"syntax-only": {"wall": 0.995}}, "fail"),
                 ({"syntax-only": {"instructions": 0.995}}, "fail"),
                 ({"syntax-only": {"instructions": None}}, "inconclusive"),
                 ({"object": {"wall": 1.03}}, "fail"),
                 ({"object": {"wall": 1.005}}, "pass"))
        for change, expected in cases:
            with self.subTest(change=change):
                self.assertEqual(ast_pilot_evidence(**change)[2]["verdict"]["outcome"], expected)
        unused_record, unused_bundle, acceptance = ast_pilot_evidence()
        cells = {cell: acceptance["cells"][cell]["metrics"] for cell in compiler_receipt.AST_PILOT_CELLS}
        identical = {"object_outputs_identical": True, "diagnostics_identical": True}
        noisy = copy.deepcopy(cells)
        noisy["syntax-only"]["wall"]["ci_high"] = 1.001
        self.assertEqual(compiler_receipt.ast_pilot_budget_verdict(noisy, 36094773, identical)["outcome"], "inconclusive")
        verdict = compiler_receipt.ast_pilot_budget_verdict(cells, 10_000_000, identical)
        self.assertEqual((verdict["outcome"], verdict["checks"]["object_peak_rss"]["outcome"]), ("fail", "fail"))
        differs = compiler_receipt.ast_pilot_budget_verdict(cells, 36094773, dict(identical, object_outputs_identical=False))
        self.assertEqual((differs["outcome"], differs["checks"]["correctness"]["outcome"]), ("fail", "fail"))

    def test_raw_cell_loader_refuses_wrong_flags_binaries_and_unexplained_na(self) -> None:
        binary = "c" * 64
        for cell in compiler_receipt.AST_PILOT_CELLS:
            raw = ast_pilot_raw_cell(cell, binary)
            documents = (raw["summary"], raw["meta"], {"a": raw["a"], "b": raw["b"]})
            ast_pilot_acceptance.validate_documents(cell, *documents, binary)
            with self.subTest(cell=cell), self.assertRaisesRegex(RuntimeError, "frozen candidate-HEAD"):
                ast_pilot_acceptance.validate_documents(cell, *documents, "d" * 64)
            other = "object" if cell == "syntax-only" else "syntax-only"
            with self.subTest(cell=cell), self.assertRaisesRegex(RuntimeError, "fixed -fc-ast-pilot"):
                ast_pilot_acceptance.validate_documents(other, *documents, binary)
            na = ast_pilot_raw_cell(cell, binary, instructions=None)
            ast_pilot_acceptance.validate_documents(cell, na["summary"], na["meta"], {"a": na["a"], "b": na["b"]}, binary)
            na["summary"]["counters"] = {"perf_stat": True, "reason": "usable"}
            with self.subTest(cell=cell), self.assertRaisesRegex(RuntimeError, "NA without"):
                ast_pilot_acceptance.validate_documents(cell, na["summary"], na["meta"], {"a": na["a"], "b": na["b"]}, binary)
        with self.assertRaisesRegex(RuntimeError, "unknown"):
            ast_pilot_acceptance.validate_documents("link", *documents, binary)

    def test_compare_command_is_fixed_and_uses_one_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for cell in compiler_receipt.AST_PILOT_CELLS:
                with mock.patch.object(ast_pilot_acceptance.subprocess, "run") as run:
                    ast_pilot_acceptance.run_compare(root / "uarch_lab.py", root / "ide", root, 2, root / cell, "perf", cell)
                argv = run.call_args.args[0]
                self.assertEqual(argv[argv.index("--ast-pilot-pair") + 1], cell)
                self.assertEqual(argv[argv.index("--baseline") + 1], argv[argv.index("--candidate") + 1])
                self.assertEqual(argv[argv.index("--pairs") + 1], "20")
                self.assertNotIn("--", argv)
                self.assertNotIn("--canonical-inline-pair", argv)

    def test_profile_runs_both_cells_on_candidate_head_and_records_the_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "ide"
            candidate.write_bytes(b"candidate compiler")
            repo = root / "repo"
            source = repo / "src/buster/apps/ide/ide.c"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"int frozen_unity_source;\n")
            (repo / "build/generated").mkdir(parents=True)
            lab = root / "trusted-uarch.py"
            lab.write_bytes(b"trusted test stub")
            binary = hashlib.sha256(b"candidate compiler").hexdigest()
            args = argparse.Namespace(lab=lab, repo_root=repo, candidate_ide=candidate, output=root / "profile",
                                      head_revision="a" * 40, cpu=2, perf="perf")
            cells = []
            with mock.patch.object(ast_pilot_acceptance, "git", return_value="a" * 40), \
                 mock.patch.object(ast_pilot_acceptance, "run_compare",
                                   side_effect=lambda *values: cells.append(values[-1])), \
                 mock.patch.object(ast_pilot_acceptance, "load_complete",
                                   side_effect=lambda path, cell, sha: ast_pilot_raw_cell(cell, sha)["summary"]), \
                 mock.patch.object(ast_pilot_acceptance, "probe_tree", return_value=copy.deepcopy(AST_PILOT_PROBE)):
                result = ast_pilot_acceptance.execute(args)
            self.assertEqual(cells, ["syntax-only", "object"])
            self.assertEqual(result["tree"]["retained_bytes"], 36094773)
            self.assertEqual(compiler_receipt.validate_ast_pilot_acceptance(result, "a" * 40, binary), [])
            args.cpu = 3
            args.output = root / "other"
            with mock.patch.object(ast_pilot_acceptance, "git", return_value="a" * 40), \
                 self.assertRaisesRegex(RuntimeError, "pins CPU 2"):
                ast_pilot_acceptance.execute(args)

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            def git(*args: str) -> str:
                return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True,
                                      text=True).stdout.strip()
            git("init", "-q")
            git("config", "user.name", "Ast Pilot Test")
            git("config", "user.email", "ast-pilot@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            git("add", compiler_receipt.ANALYZER_REQUEST_PATH)
            git("commit", "-qm", "seed")
            line = compiler_receipt.AST_PILOT_ACCEPTANCE_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                git("commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.ast_pilot_request_selection(repo, git("rev-parse", "HEAD")), expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            git("commit", "-qam", "duplicate")
            requested, problem = compiler_compare.ast_pilot_request_selection(repo, git("rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.AST_PILOT_ACCEPTANCE_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = receipt()
        pull.update(mode="pull", ast_pilot_acceptance=ast_pilot_evidence()[0])
        expected = {"mode": "pull", "head": head}
        self.assertEqual(compiler_publish.verify_analyzer_request_freshness(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", compiler_publish.verify_analyzer_request_freshness(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", compiler_publish.verify_analyzer_request_freshness(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("ast_pilot_acceptance")
        self.assertIn("no ast-pilot acceptance record",
                      compiler_publish.verify_analyzer_request_freshness(Api(1, 0), expected, plain, {}))
        self.assertEqual(compiler_publish.verify_analyzer_request_freshness(Api(1, 1), expected, plain, {}), "")
        self.assertEqual(compiler_publish.verify_analyzer_request_freshness(
            Api(1, 0), expected, dict(plain, state="superseded"), {}), "")

    def test_decision_needs_pull_mode_the_9700x_and_the_complete_bundle(self) -> None:
        record, bundle, unused_acceptance = ast_pilot_evidence("a" * 40, B256)
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["ast_pilot_acceptance"] = record
        expected = dict(EXPECTED, mode="pull")
        values = {"expected": expected, "authorized": True, "compare_result": "success", "receipt": pull,
                  "summary": summary(), "policy_value": "", "throughput": dict(corpus(), ast_pilot_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "missing cell": {"throughput": dict(corpus(), ast_pilot_bundle={
                key: value for key, value in bundle.items() if not key.startswith("object_")})},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("Issue #3397 -fc-ast-pilot adoption budget: complete, verdict pass", report)
        self.assertIn("| syntax-only | 0.9000", report)



def constant_reuse_raw_cell(cell: str, binary: str = "c" * 64, wall: float = 0.99, instructions: object = 0.98) -> dict:
    """One synthetic, complete raw uarch_lab cell of the #3011 constant-reuse profile."""
    pairs = compiler_receipt.CONSTANT_REUSE_ACCEPTANCE_PROFILE["pairs"]
    argv = constant_reuse_acceptance.cell_command(cell)
    extra_by_variant = constant_reuse_acceptance.extra_by_variant(cell)
    labs = {}
    for key, extra in extra_by_variant.items():
        command = ["/bin/ide"] + argv[:-2] + extra + argv[-2:]
        labs[key] = {"config": {"extra": list(extra), "cpu": 2, "constant_reuse_cell": cell, "ide": "/bin/ide",
                                "command": " ".join(command)}}
    insn = {"ratio": instructions, "a_median": 1e10 if instructions is not None else None,
            "b_median": 1e10 * instructions if instructions is not None else None}
    summary = {"schema": constant_reuse_acceptance.UARCH_SCHEMA,
               "plan": {"pairs": pairs, "complete_pairs": pairs, "order": "ABBA", "fresh_copy": True},
               "baseline": {"sha256": binary, "failed": 0, "deterministic": True, "runs": pairs},
               "candidate": {"sha256": binary, "failed": 0, "deterministic": True, "runs": pairs},
               "metrics": {"wall": {"a_median": 2.0, "b_median": 2.0 * wall, "ratio": wall,
                                    "ci_low": wall - 0.01, "ci_high": wall + 0.005},
                           "instructions": insn, "peak_rss": {"a_median": 1e9, "b_median": 0.98e9, "ratio": 0.98}},
               "code_bytes": {"a_value": 28520268, "b_value": 28298232},
               "counters": {"perf_stat": instructions is not None,
                            "reason": "usable" if instructions is not None else "perf_event_paranoid"},
               "outputs_identical": False, "diagnostics_identical": True}
    meta = {"config": {"constant_reuse_pair": cell, "extra_by_variant": extra_by_variant, "ast_pilot_pair": None,
                       "canonical_inline_pair": False, "command": " ".join(["IDE"] + argv), "pairs": pairs,
                       "warmups": 1, "cpu": 2, "extra": [], "profile_steps": [], "fresh_copy": True}}
    return {"summary": summary, "meta": meta, "a": labs["a"], "b": labs["b"]}


def constant_reuse_evidence(head: str = "a" * 40, binary: str = "c" * 64) -> tuple[dict, dict, dict]:
    """(receipt record, raw bundle, acceptance) built through the helper's own record code."""
    raw = {cell: constant_reuse_raw_cell(cell, binary) for cell in compiler_receipt.CONSTANT_REUSE_CELLS}
    cells = {cell: constant_reuse_acceptance.cell_record(raw[cell]["summary"]) for cell in raw}
    acceptance = {"schema": compiler_receipt.CONSTANT_REUSE_ACCEPTANCE_SCHEMA, "status": "complete",
                  "source_revision": head,
                  "profile": {**compiler_receipt.CONSTANT_REUSE_ACCEPTANCE_PROFILE, "source_sha256": "e" * 64},
                  "candidate_compiler": {"sha256": binary, "size_bytes": 10}, "cells": cells}
    bundle = {"acceptance": acceptance}
    for cell, documents in raw.items():
        for key, value in documents.items():
            bundle[f"{cell}_{key}"] = value
    record = {"requested": True, "request_line": compiler_receipt.CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE,
              "profile": compiler_receipt.CONSTANT_REUSE_ACCEPTANCE_PROFILE, "summary": acceptance,
              "status": "complete", "exit": 0}
    return json.loads(json.dumps(record)), json.loads(json.dumps(bundle)), acceptance


class ConstantReuseAcceptanceTest(unittest.TestCase):
    def test_publisher_revalidates_raw_cells(self) -> None:
        head, binary = "a" * 40, "c" * 64
        record, bundle, unused_acceptance = constant_reuse_evidence(head, binary)
        self.assertEqual(compiler_publish.validate_constant_reuse_bundle(record, bundle, head, binary), [])

        def broken(change) -> list[str]:
            changed = copy.deepcopy(bundle)
            change(changed)
            return compiler_publish.validate_constant_reuse_bundle(record, changed, head, binary)
        cases = {
            "wrong flag": lambda value: value["intern_meta"]["config"].update(
                extra_by_variant={"a": [], "b": ["-fcanonical-fast-cse"]}),
            "flag on A": lambda value: value["cse_a"]["config"].update(extra=["-fcanonical-fast-cse"]),
            "lab command": lambda value: value["intern_b"]["config"].update(
                command=value["intern_a"]["config"]["command"]),
            "syntax-only command": lambda value: value["cse_meta"]["config"].update(
                command=" ".join(["IDE"] + compiler_receipt.AST_PILOT_COMPILE + ["-fsyntax-only"])),
            "cell swap": lambda value: value["cse_meta"]["config"].update(constant_reuse_pair="intern"),
            "ast pilot pair": lambda value: value["cse_meta"]["config"].update(ast_pilot_pair="object"),
            "wrong binary": lambda value: value["intern_summary"]["candidate"].update(sha256="d" * 64),
            "cpu": lambda value: value["cse_meta"]["config"].update(cpu=3),
            "pair count": lambda value: value["cse_summary"]["plan"].update(pairs=12, complete_pairs=12),
            "missing cell": lambda value: [value.pop(f"cse_{key}") for key in ("summary", "meta", "a", "b")],
            "metrics": lambda value: value["cse_summary"]["metrics"]["wall"].update(ratio=1.2),
            "code bytes": lambda value: value["intern_summary"].update(code_bytes={}),
            "diagnostics": lambda value: value["intern_summary"].update(diagnostics_identical=False),
        }
        for name, change in cases.items():
            with self.subTest(case=name):
                self.assertTrue(broken(change))
        self.assertTrue(compiler_publish.validate_constant_reuse_bundle(dict(record, status="failed"), bundle, head, binary))
        self.assertTrue(compiler_publish.validate_constant_reuse_bundle(
            dict(record, request_line="constant-reuse"), bundle, head, binary))
        self.assertTrue(compiler_publish.validate_constant_reuse_bundle(record, None, head, binary))
        self.assertTrue(compiler_publish.validate_constant_reuse_bundle(record, bundle, head, "d" * 64))
        self.assertEqual(compiler_publish.validate_constant_reuse_bundle(None, None, head, binary), [])

    def test_receipt_validator_needs_both_cells_and_frozen_profile(self) -> None:
        head, binary = "a" * 40, "c" * 64
        unused_record, unused_bundle, acceptance = constant_reuse_evidence(head, binary)
        self.assertEqual(compiler_receipt.validate_constant_reuse_acceptance(acceptance, head, binary), [])
        for name, change in {
            "profile": lambda value: value["profile"].update(pairs=2),
            "extra profile key": lambda value: value["profile"].update(flags=["-O3"]),
            "missing cell": lambda value: value["cells"].pop("cse"),
            "revision": lambda value: value.update(source_revision="b" * 40),
            "binary": lambda value: value["candidate_compiler"].update(sha256="d" * 64),
            "code bytes": lambda value: value["cells"]["intern"]["metrics"].update(code_bytes={"a_value": 1}),
            "diagnostics": lambda value: value["cells"]["cse"].update(diagnostics_identical=False),
        }.items():
            changed = copy.deepcopy(acceptance)
            change(changed)
            with self.subTest(case=name):
                self.assertTrue(compiler_receipt.validate_constant_reuse_acceptance(changed, head, binary))

    def test_out_of_range_numbers_fail_closed(self) -> None:
        huge, head, binary = 10 ** 309, "a" * 40, "c" * 64
        self.assertFalse(constant_reuse_acceptance._positive_number(huge))
        self.assertTrue(constant_reuse_acceptance._positive_number(1))
        record, bundle, acceptance = constant_reuse_evidence(head, binary)
        for metric, key in (("wall", "ci_high"), ("peak_rss", "b_median"), ("instructions", "a_median"),
                            ("code_bytes", "b_value")):
            changed = copy.deepcopy(acceptance)
            changed["cells"]["cse"]["metrics"][metric][key] = huge
            with self.subTest(metric=metric):
                self.assertTrue(compiler_receipt.validate_constant_reuse_acceptance(changed, head, binary))
                raw = copy.deepcopy(bundle)
                target = raw["cse_summary"] if metric == "code_bytes" else raw["cse_summary"]["metrics"]
                target[metric][key] = huge
                self.assertTrue(compiler_publish.validate_constant_reuse_bundle(record, raw, head, binary))
                pull = receipt()
                pull["constant_reuse_acceptance"] = dict(record, summary=changed)
                self.assertIn("Issue #3011 constant-reuse arms", compiler_receipt.render(pull, summary(), "failure", []))

    def test_raw_cell_loader_refuses_wrong_flags_binaries_and_unexplained_na(self) -> None:
        binary = "c" * 64
        for cell in compiler_receipt.CONSTANT_REUSE_CELLS:
            raw = constant_reuse_raw_cell(cell, binary)
            documents = (raw["summary"], raw["meta"], {"a": raw["a"], "b": raw["b"]})
            constant_reuse_acceptance.validate_documents(cell, *documents, binary)
            with self.subTest(cell=cell), self.assertRaisesRegex(RuntimeError, "frozen candidate-HEAD"):
                constant_reuse_acceptance.validate_documents(cell, *documents, "d" * 64)
            other = "cse" if cell == "intern" else "intern"
            with self.subTest(cell=cell), self.assertRaisesRegex(RuntimeError, "fixed constant-reuse"):
                constant_reuse_acceptance.validate_documents(other, *documents, binary)
            na = constant_reuse_raw_cell(cell, binary, instructions=None)
            na_documents = (na["summary"], na["meta"], {"a": na["a"], "b": na["b"]})
            constant_reuse_acceptance.validate_documents(cell, *na_documents, binary)
            na["summary"]["counters"] = {"perf_stat": True, "reason": "usable"}
            with self.subTest(cell=cell), self.assertRaisesRegex(RuntimeError, "NA without"):
                constant_reuse_acceptance.validate_documents(cell, *na_documents, binary)
        with self.assertRaisesRegex(RuntimeError, "unknown"):
            constant_reuse_acceptance.validate_documents("table", *documents, binary)

    def test_compare_command_is_fixed_and_uses_one_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for cell in compiler_receipt.CONSTANT_REUSE_CELLS:
                with mock.patch.object(constant_reuse_acceptance.subprocess, "run") as run:
                    constant_reuse_acceptance.run_compare(root / "uarch_lab.py", root / "ide", root, 2, root / cell,
                                                          "perf", cell)
                argv = run.call_args.args[0]
                self.assertEqual(argv[argv.index("--constant-reuse-pair") + 1], cell)
                self.assertEqual(argv[argv.index("--baseline") + 1], argv[argv.index("--candidate") + 1])
                self.assertEqual(argv[argv.index("--pairs") + 1], "20")
                self.assertNotIn("--", argv)
                self.assertNotIn("--ast-pilot-pair", argv)

    def test_profile_runs_both_cells_on_candidate_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "ide"
            candidate.write_bytes(b"candidate compiler")
            repo = root / "repo"
            source = repo / "src/buster/apps/ide/ide.c"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"int frozen_unity_source;\n")
            (repo / "build/generated").mkdir(parents=True)
            lab = root / "trusted-uarch.py"
            lab.write_bytes(b"trusted test stub")
            binary = hashlib.sha256(b"candidate compiler").hexdigest()
            args = argparse.Namespace(lab=lab, repo_root=repo, candidate_ide=candidate, output=root / "profile",
                                      head_revision="a" * 40, cpu=2, perf="perf")
            cells = []
            with mock.patch.object(constant_reuse_acceptance, "git", return_value="a" * 40), \
                 mock.patch.object(constant_reuse_acceptance, "run_compare",
                                   side_effect=lambda *values: cells.append(values[-1])), \
                 mock.patch.object(constant_reuse_acceptance, "load_complete",
                                   side_effect=lambda path, cell, sha: constant_reuse_raw_cell(cell, sha)["summary"]):
                result = constant_reuse_acceptance.execute(args)
            self.assertEqual(cells, ["intern", "cse"])
            self.assertEqual(result["cells"]["intern"]["metrics"]["code_bytes"]["b_value"], 28298232)
            self.assertEqual(compiler_receipt.validate_constant_reuse_acceptance(result, "a" * 40, binary), [])
            args.cpu = 3
            args.output = root / "other"
            with mock.patch.object(constant_reuse_acceptance, "git", return_value="a" * 40), \
                 self.assertRaisesRegex(RuntimeError, "pins CPU 2"):
                constant_reuse_acceptance.execute(args)

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            def git(*args: str) -> str:
                return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True,
                                      text=True).stdout.strip()
            git("init", "-q")
            git("config", "user.name", "Constant Reuse Test")
            git("config", "user.email", "constant-reuse@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            git("add", compiler_receipt.ANALYZER_REQUEST_PATH)
            git("commit", "-qm", "seed")
            line = compiler_receipt.CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                git("commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.constant_reuse_request_selection(repo, git("rev-parse", "HEAD")),
                                     expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            git("commit", "-qam", "duplicate")
            requested, problem = compiler_compare.constant_reuse_request_selection(repo, git("rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = receipt()
        pull.update(mode="pull", constant_reuse_acceptance=constant_reuse_evidence()[0])
        expected = {"mode": "pull", "head": head}
        self.assertEqual(compiler_publish.verify_analyzer_request_freshness(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", compiler_publish.verify_analyzer_request_freshness(Api(1, 1), expected, pull, {}))
        plain = dict(pull)
        plain.pop("constant_reuse_acceptance")
        self.assertIn("no constant-reuse acceptance record",
                      compiler_publish.verify_analyzer_request_freshness(Api(1, 0), expected, plain, {}))
        self.assertEqual(compiler_publish.verify_analyzer_request_freshness(Api(1, 1), expected, plain, {}), "")

    def test_decision_needs_pull_mode_the_9700x_and_the_complete_bundle(self) -> None:
        record, bundle, unused_acceptance = constant_reuse_evidence("a" * 40, B256)
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["constant_reuse_acceptance"] = record
        expected = dict(EXPECTED, mode="pull")
        values = {"expected": expected, "authorized": True, "compare_result": "success", "receipt": pull,
                  "summary": summary(), "policy_value": "", "throughput": dict(corpus(), constant_reuse_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        both = copy.deepcopy(pull)
        both["ast_pilot_acceptance"] = ast_pilot_evidence("a" * 40, B256)[0]
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "missing cell": {"throughput": dict(corpus(), constant_reuse_bundle={
                key: value for key, value in bundle.items() if not key.startswith("cse_")})},
            "with ast pilot": {"receipt": both},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        # main's request history holds the inline selector, so every pull compare carries an inline record.
        with_inline = copy.deepcopy(pull)
        with_inline["inline_acceptance"] = {"requested": True, "request_line": compiler_receipt.INLINE_ACCEPTANCE_REQUEST_LINE,
                                            "profile": compiler_receipt.INLINE_ACCEPTANCE_PROFILE, "status": "complete"}
        unused_conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, receipt=with_inline))
        self.assertFalse([reason for reason in reasons if "cannot be combined" in reason], reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("Issue #3011 constant-reuse arms: complete", report)
        self.assertIn("| intern (-fcanonical-intern-constants) | 0.9900", report)


PROCESS_FLOOR_WALL = {"aa": 1.0, "aarch64": 0.7, "syntax-only": 0.65}


def process_floor_raw_cell(cell: str, binary: str = "c" * 64, instructions: object = 0.3) -> dict:
    """One synthetic, complete raw uarch_lab cell of the #1125 process-floor profile."""
    pairs = compiler_receipt.PROCESS_FLOOR_PROFILE["pairs"]
    commands = {key: process_floor_acceptance.variant_command(cell, key) for key in ("a", "b")}
    labs = {key: {"config": {"extra": [], "cpu": 2, "process_floor_cell": cell, "ide": "/bin/ide",
                             "command": " ".join(["/bin/ide"] + commands[key])}} for key in ("a", "b")}
    wall = PROCESS_FLOOR_WALL[cell]
    insn_ratio = 1.0 if cell == "aa" else instructions
    insn = {"ratio": insn_ratio, "a_median": 1e7 if insn_ratio is not None else None,
            "b_median": 1e7 * insn_ratio if insn_ratio is not None else None}
    summary = {"schema": process_floor_acceptance.UARCH_SCHEMA,
               "plan": {"pairs": pairs, "complete_pairs": pairs, "order": "ABBA", "fresh_copy": True},
               "baseline": {"sha256": binary, "failed": 0, "deterministic": True, "runs": pairs},
               "candidate": {"sha256": binary, "failed": 0, "deterministic": True, "runs": pairs},
               "metrics": {"wall": {"a_median": 0.01, "b_median": 0.01 * wall, "ratio": wall,
                                    "ci_low": wall - 0.005, "ci_high": wall + 0.004},
                           "task_clock": {"a_median": 0.008, "b_median": 0.008 * wall, "ratio": wall},
                           "compiler_wall": {"a_median": 0.006, "b_median": 0.006 * wall, "ratio": wall},
                           "instructions": insn, "peak_rss": {"a_median": None, "b_median": None, "ratio": None}},
               "counters": {"perf_stat": instructions is not None,
                            "reason": "usable" if instructions is not None else "perf_event_paranoid"},
               "outputs_identical": cell == "aa", "diagnostics_identical": True}
    meta = {"config": {"process_floor_pair": cell, "extra_by_variant": {"a": [], "b": []},
                       "command_by_variant": {key: " ".join(["IDE"] + commands[key]) for key in commands},
                       "command": " ".join(["IDE"] + commands["a"]), "canonical_inline_pair": False,
                       "pairs": pairs, "warmups": 2, "cpu": 2, "extra": [], "profile_steps": [], "fresh_copy": True}}
    return {"summary": summary, "meta": meta, "a": labs["a"], "b": labs["b"]}


def process_floor_evidence(head: str = "a" * 40, binary: str = "c" * 64) -> tuple[dict, dict, dict]:
    """(receipt record, raw bundle, acceptance) built through the helper's own record code."""
    raw = {cell: process_floor_raw_cell(cell, binary) for cell in compiler_receipt.PROCESS_FLOOR_CELLS}
    cells = {cell: process_floor_acceptance.cell_record(raw[cell]["summary"]) for cell in raw}
    acceptance = {"schema": compiler_receipt.PROCESS_FLOOR_SCHEMA, "status": "complete", "source_revision": head,
                  "profile": compiler_receipt.PROCESS_FLOOR_PROFILE,
                  "candidate_compiler": {"sha256": binary, "size_bytes": 10}, "cells": cells,
                  "verdict": compiler_receipt.process_floor_verdict(cells)}
    bundle = {"acceptance": acceptance}
    for cell, documents in raw.items():
        for key, value in documents.items():
            bundle[f"{cell}_{key}"] = value
    record = {"requested": True, "request_line": compiler_receipt.PROCESS_FLOOR_REQUEST_LINE,
              "profile": compiler_receipt.PROCESS_FLOOR_PROFILE, "summary": acceptance, "status": "complete", "exit": 0}
    return json.loads(json.dumps(record)), json.loads(json.dumps(bundle)), json.loads(json.dumps(acceptance))


class ProcessFloorTest(unittest.TestCase):
    def test_profile_is_frozen_and_matches_the_lab_cells(self) -> None:
        profile = compiler_receipt.PROCESS_FLOOR_PROFILE
        self.assertEqual(compiler_receipt.PROCESS_FLOOR_REQUEST_LINE, "profile: " + profile["name"])
        self.assertIs(profile["report_only"], True)
        self.assertEqual(profile["source_sha256"],
                         hashlib.sha256(b"int main(void){return 0;}\n").hexdigest())
        self.assertEqual(profile["cells"], {"aa": "cc -g0 -c tiny.c -o OUT",
                                            "aarch64": "cc -g0 -target aarch64-unknown-linux -c tiny.c -o OUT",
                                            "syntax-only": "cc -g0 -fsyntax-only tiny.c"})
        self.assertEqual(json.loads(json.dumps(profile)), profile)
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        try:
            import uarch_lab as lab
        finally:
            sys.path.pop(0)
        self.assertEqual(lab.PROCESS_FLOOR_CELLS, compiler_receipt.PROCESS_FLOOR_CELLS)
        self.assertEqual(lab.PROCESS_FLOOR_OBJECT, compiler_receipt.PROCESS_FLOOR_OBJECT)
        # Every cell fits the leg's bound, which with the ordinary comparison stays inside the 90-minute job.
        self.assertGreaterEqual(compiler_compare.PROCESS_FLOOR_TIMEOUT_SECONDS,
                                len(compiler_receipt.PROCESS_FLOOR_CELLS) * process_floor_acceptance.COMPARE_TIMEOUT_SECONDS)
        self.assertLessEqual(compiler_compare.PROCESS_FLOOR_TIMEOUT_SECONDS, 75 * 60)

    def test_verdict_applies_the_a_a_stop_rule(self) -> None:
        unused_record, unused_bundle, acceptance = process_floor_evidence()
        verdict = acceptance["verdict"]
        self.assertEqual(verdict["aa_interval"], [0.995, 1.004])
        self.assertEqual(verdict["aarch64"], {"ci": [0.695, 0.704], "outside_aa": True, "wall_ms": 3.0,
                                              "task_clock_ms": 2.4})
        self.assertTrue(verdict["syntax-only"]["outside_aa"])
        inside = copy.deepcopy(acceptance["cells"])
        inside["aarch64"]["metrics"]["wall"].update(ci_low=0.998, ci_high=1.002)
        self.assertFalse(compiler_receipt.process_floor_verdict(inside)["aarch64"]["outside_aa"])
        overlap = copy.deepcopy(acceptance["cells"])
        overlap["aarch64"]["metrics"]["wall"].update(ci_high=0.996)
        self.assertFalse(compiler_receipt.process_floor_verdict(overlap)["aarch64"]["outside_aa"])
        missing = copy.deepcopy(acceptance["cells"])
        missing.pop("aa")
        self.assertEqual(compiler_receipt.process_floor_verdict(missing)["aarch64"]["outside_aa"], False)
        self.assertIsNone(compiler_receipt.process_floor_verdict({})["syntax-only"]["wall_ms"])

    def test_publisher_revalidates_raw_cells(self) -> None:
        head, binary = "a" * 40, "c" * 64
        record, bundle, unused_acceptance = process_floor_evidence(head, binary)
        self.assertEqual(compiler_publish.validate_process_floor_bundle(record, bundle, head, binary), [])

        def broken(change) -> list[str]:
            changed = copy.deepcopy(bundle)
            change(changed)
            return compiler_publish.validate_process_floor_bundle(record, changed, head, binary)
        cases = {
            "B command on A": lambda value: value["aarch64_a"]["config"].update(
                command=value["aarch64_b"]["config"]["command"]),
            "recorded variant commands": lambda value: value["syntax-only_meta"]["config"]["command_by_variant"].update(
                b=value["aarch64_meta"]["config"]["command_by_variant"]["b"]),
            "extra flags": lambda value: value["aa_meta"]["config"].update(extra_by_variant={"a": [], "b": ["-O1"]}),
            "cell swap": lambda value: value["aarch64_meta"]["config"].update(process_floor_pair="aa"),
            "constant reuse pair": lambda value: value["aa_meta"]["config"].update(constant_reuse_pair="intern"),
            "wrong binary": lambda value: value["aa_summary"]["candidate"].update(sha256="d" * 64),
            "cpu": lambda value: value["aa_meta"]["config"].update(cpu=3),
            "pair count": lambda value: value["aarch64_summary"]["plan"].update(pairs=12, complete_pairs=12),
            "missing cell": lambda value: [value.pop(f"syntax-only_{key}") for key in ("summary", "meta", "a", "b")],
            "metrics": lambda value: value["aarch64_summary"]["metrics"]["wall"].update(ratio=0.5),
            "task clock": lambda value: value["aa_summary"]["metrics"]["task_clock"].update(b_median=None),
            "a/a objects": lambda value: value["aa_summary"].update(outputs_identical=False),
            "diagnostics": lambda value: value["syntax-only_summary"].update(diagnostics_identical=False),
        }
        for name, change in cases.items():
            with self.subTest(case=name):
                self.assertTrue(broken(change))
        self.assertTrue(compiler_publish.validate_process_floor_bundle(dict(record, status="failed"), bundle, head, binary))
        self.assertTrue(compiler_publish.validate_process_floor_bundle(
            dict(record, request_line="process-floor"), bundle, head, binary))
        self.assertTrue(compiler_publish.validate_process_floor_bundle(record, None, head, binary))
        self.assertTrue(compiler_publish.validate_process_floor_bundle(record, bundle, head, "d" * 64))
        self.assertEqual(compiler_publish.validate_process_floor_bundle(None, None, head, binary), [])

    def test_receipt_validator_needs_every_cell_the_frozen_profile_and_its_verdict(self) -> None:
        head, binary = "a" * 40, "c" * 64
        unused_record, unused_bundle, acceptance = process_floor_evidence(head, binary)
        self.assertEqual(compiler_receipt.validate_process_floor_acceptance(acceptance, head, binary), [])
        for name, change in {
            "profile": lambda value: value["profile"].update(pairs=2),
            "extra profile key": lambda value: value["profile"].update(flags=["-O3"]),
            "missing cell": lambda value: value["cells"].pop("syntax-only"),
            "revision": lambda value: value.update(source_revision="b" * 40),
            "binary": lambda value: value["candidate_compiler"].update(sha256="d" * 64),
            "a/a objects": lambda value: value["cells"]["aa"].update(outputs_identical=False),
            "diagnostics": lambda value: value["cells"]["aarch64"].update(diagnostics_identical=False),
            "verdict": lambda value: value["verdict"]["aarch64"].update(outside_aa=False),
            "peak rss": lambda value: value["cells"]["aa"]["metrics"]["peak_rss"].update(a_median=-1),
            "compiler wall": lambda value: value["cells"]["aa"]["metrics"].pop("compiler_wall"),
            "huge wall": lambda value: value["cells"]["aa"]["metrics"]["wall"].update(ci_high=10 ** 309),
        }.items():
            changed = copy.deepcopy(acceptance)
            change(changed)
            with self.subTest(case=name):
                self.assertTrue(compiler_receipt.validate_process_floor_acceptance(changed, head, binary))

    def test_raw_cell_loader_refuses_wrong_commands_binaries_and_unexplained_na(self) -> None:
        binary = "c" * 64
        for cell in compiler_receipt.PROCESS_FLOOR_CELLS:
            raw = process_floor_raw_cell(cell, binary)
            documents = (raw["summary"], raw["meta"], {"a": raw["a"], "b": raw["b"]})
            process_floor_acceptance.validate_documents(cell, *documents, binary)
            with self.subTest(cell=cell), self.assertRaisesRegex(RuntimeError, "frozen candidate-HEAD"):
                process_floor_acceptance.validate_documents(cell, *documents, "d" * 64)
            other = "aarch64" if cell != "aarch64" else "syntax-only"
            with self.subTest(cell=cell), self.assertRaisesRegex(RuntimeError, "fixed process-floor"):
                process_floor_acceptance.validate_documents(other, *documents, binary)
            na = process_floor_raw_cell(cell, binary, instructions=None)
            na["summary"]["metrics"]["instructions"] = {"ratio": None, "a_median": None, "b_median": None}
            na_documents = (na["summary"], na["meta"], {"a": na["a"], "b": na["b"]})
            process_floor_acceptance.validate_documents(cell, *na_documents, binary)
            na["summary"]["counters"] = {"perf_stat": True, "reason": "usable"}
            with self.subTest(cell=cell), self.assertRaisesRegex(RuntimeError, "NA without"):
                process_floor_acceptance.validate_documents(cell, *na_documents, binary)
        with self.assertRaisesRegex(RuntimeError, "unknown"):
            process_floor_acceptance.validate_documents("x86", *documents, binary)
        self.assertEqual(process_floor_acceptance.variant_command("syntax-only", "b"),
                         ["cc", "-g0", "-fsyntax-only", "tiny.c"])
        self.assertEqual(process_floor_acceptance.variant_command("syntax-only", "a"),
                         ["cc", "-g0", "-c", "tiny.c", "-o", "OUT"])

    def test_compare_command_is_fixed_and_uses_one_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for cell in compiler_receipt.PROCESS_FLOOR_CELLS:
                with mock.patch.object(process_floor_acceptance.subprocess, "run") as run:
                    process_floor_acceptance.run_compare(root / "uarch_lab.py", root / "ide", root, 2, root / cell,
                                                         "perf", cell)
                argv = run.call_args.args[0]
                self.assertEqual(argv[argv.index("--process-floor-pair") + 1], cell)
                self.assertEqual(argv[argv.index("--baseline") + 1], argv[argv.index("--candidate") + 1])
                self.assertEqual(argv[argv.index("--pairs") + 1], "100")
                self.assertEqual(argv[argv.index("--warmups") + 1], "2")
                self.assertNotIn("--", argv)
                self.assertNotIn("--constant-reuse-pair", argv)

    def test_profile_writes_the_source_and_runs_every_cell_in_its_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "ide"
            candidate.write_bytes(b"candidate compiler")
            repo = root / "repo"
            repo.mkdir()
            lab = root / "trusted-uarch.py"
            lab.write_bytes(b"trusted test stub")
            binary = hashlib.sha256(b"candidate compiler").hexdigest()
            args = argparse.Namespace(lab=lab, repo_root=repo, candidate_ide=candidate, output=root / "profile",
                                      head_revision="a" * 40, cpu=2, perf="perf")
            calls = []
            with mock.patch.object(process_floor_acceptance, "git", return_value="a" * 40), \
                 mock.patch.object(process_floor_acceptance, "run_compare",
                                   side_effect=lambda *values: calls.append((values[2], values[-1]))), \
                 mock.patch.object(process_floor_acceptance, "load_complete",
                                   side_effect=lambda path, cell, sha: process_floor_raw_cell(cell, sha)["summary"]):
                result = process_floor_acceptance.execute(args)
            source = (root / "profile/source").resolve()
            self.assertEqual(calls, [(source, "aa"), (source, "aarch64"), (source, "syntax-only")])
            self.assertEqual((source / "tiny.c").read_bytes(), b"int main(void){return 0;}\n")
            self.assertEqual(compiler_receipt.validate_process_floor_acceptance(result, "a" * 40, binary), [])
            self.assertTrue(result["verdict"]["aarch64"]["outside_aa"])
            args.output = root / "stray"
            with mock.patch.object(process_floor_acceptance, "git", return_value="a" * 40), \
                 mock.patch.object(process_floor_acceptance, "run_compare",
                                   side_effect=lambda *values: (values[2] / "tiny.o").write_bytes(b"")), \
                 mock.patch.object(process_floor_acceptance, "load_complete",
                                   side_effect=lambda path, cell, sha: process_floor_raw_cell(cell, sha)["summary"]), \
                 self.assertRaisesRegex(RuntimeError, "source directory changed"):
                process_floor_acceptance.execute(args)
            args.cpu = 3
            args.output = root / "other"
            with mock.patch.object(process_floor_acceptance, "git", return_value="a" * 40), \
                 self.assertRaisesRegex(RuntimeError, "pins CPU 2"):
                process_floor_acceptance.execute(args)
            args.cpu = 2
            with mock.patch.object(process_floor_acceptance, "git", return_value="b" * 40), \
                 self.assertRaisesRegex(RuntimeError, "expected candidate HEAD"):
                process_floor_acceptance.execute(args)

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            def git(*args: str) -> str:
                return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True,
                                      text=True).stdout.strip()
            git("init", "-q")
            git("config", "user.name", "Process Floor Test")
            git("config", "user.email", "process-floor@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            git("add", compiler_receipt.ANALYZER_REQUEST_PATH)
            git("commit", "-qm", "seed")
            line = compiler_receipt.PROCESS_FLOOR_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                git("commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.process_floor_request_selection(repo, git("rev-parse", "HEAD")),
                                     expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            git("commit", "-qam", "duplicate")
            requested, problem = compiler_compare.process_floor_request_selection(repo, git("rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.PROCESS_FLOOR_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = receipt()
        pull.update(mode="pull", process_floor=process_floor_evidence()[0])
        expected = {"mode": "pull", "head": head}
        verify = compiler_publish.verify_analyzer_request_freshness
        self.assertEqual(verify(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", verify(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", verify(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("process_floor")
        self.assertIn("no process-floor record", verify(Api(1, 0), expected, plain, {}))
        self.assertEqual(verify(Api(1, 1), expected, plain, {}), "")

    def test_decision_needs_pull_mode_the_9700x_and_the_complete_bundle(self) -> None:
        record, bundle, unused_acceptance = process_floor_evidence("a" * 40, B256)
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["process_floor"] = record
        expected = dict(EXPECTED, mode="pull")
        values = {"expected": expected, "authorized": True, "compare_result": "success", "receipt": pull,
                  "summary": summary(), "policy_value": "", "throughput": dict(corpus(), process_floor_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        both = copy.deepcopy(pull)
        both["constant_reuse_acceptance"] = constant_reuse_evidence("a" * 40, B256)[0]
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "missing cell": {"throughput": dict(corpus(), process_floor_bundle={
                key: value for key, value in bundle.items() if not key.startswith("aarch64_")})},
            "failed run": {"receipt": dict(pull, process_floor=dict(record, status="failed", exit=1))},
            "with constant reuse": {"receipt": both, "throughput": dict(
                corpus(), process_floor_bundle=bundle, constant_reuse_bundle=constant_reuse_evidence("a" * 40, B256)[1])},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        # main's request history holds the inline selector, so every pull compare carries an inline record.
        with_inline = copy.deepcopy(pull)
        with_inline["inline_acceptance"] = {"requested": True, "request_line": compiler_receipt.INLINE_ACCEPTANCE_REQUEST_LINE,
                                            "profile": compiler_receipt.INLINE_ACCEPTANCE_PROFILE, "status": "complete"}
        unused_conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, receipt=with_inline))
        self.assertFalse([reason for reason in reasons if "cannot be combined" in reason], reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("Issue #1125 per-process floor: complete", report)
        self.assertIn("| aarch64 (`cc -g0 -target aarch64-unknown-linux -c tiny.c -o OUT`) | 10.000 / 7.000 | 3.000 "
                      "| 0.7000 [0.6950, 0.7040] | 8.000 / 5.600 | 10000000 / 3000000 | True |", report)
        self.assertIn("| aa (`cc -g0 -c tiny.c -o OUT`) | 10.000 / 10.000 | NA | 1.0000 [0.9950, 1.0040]", report)
        coverage = compiler_receipt.covered_inventory_rows(pull, "success", "pull")
        self.assertIn("uarch_lab:compare@process-floor", coverage["discharged"])
        unrequested = compiler_receipt.covered_inventory_rows(receipt(), "success", "pull")
        self.assertEqual(unrequested["rows"]["uarch_lab:compare@process-floor"], "not discharged: not requested")

    def test_request_with_another_opt_in_leg_fails_before_any_build(self) -> None:
        for other in ("constant_reuse_requested", "frontend_nesting_requested", "selfhost_inline_requested"):
            with self.subTest(combined=other), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                candidate, work, evidence, bins = root / "candidate", root / "work", root / "evidence", root / "bins"
                for directory in (candidate, work, evidence, bins):
                    directory.mkdir()
                current = {"state": "pending", "reasons": [], "timings": {}, "binaries": {}, "lab": {}}
                args = argparse.Namespace(mode="pull", base="b" * 40, head="a" * 40, process_floor_requested=True,
                                          lab=root / "uarch_lab.py", **{other: True})
                with mock.patch.object(compiler_compare, "scaling_requested", return_value=False), \
                        mock.patch.object(compiler_compare, "build") as build, \
                        mock.patch.object(compiler_compare, "run") as run:
                    compiler_compare.measure(args, candidate, work, evidence, bins, evidence / "build.log", current, [])
                self.assertTrue(any("cannot be combined" in reason for reason in current["reasons"]), current["reasons"])
                self.assertEqual(current["process_floor"]["status"], "failed")
                build.assert_not_called()
                run.assert_not_called()

    def test_leg_exports_only_the_required_documents_when_optional_files_are_deferred(self) -> None:
        record_value, bundle, acceptance = process_floor_evidence("a" * 40, B256)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("work", "evidence", "candidate"):
                (root / name).mkdir()
            for key, member in compiler_publish.PROCESS_FLOOR_BUNDLE_FILES.items():
                path = root / "work/process-floor" / member.split("/", 1)[1]
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(bundle[key]), encoding="utf-8")
            (root / "work/process-floor/aa/pairs").mkdir()
            (root / "work/process-floor/aa/pairs/0001-a.csv").write_text("optional\n", encoding="utf-8")
            pull = {"state": "pending", "reasons": [], "timings": {}, "binaries": {"candidate": {"sha256": B256}},
                    "process_floor": {"requested": True, "status": "pending"}}
            arguments = argparse.Namespace(lab=root / "uarch_lab.py", head="a" * 40)
            exported_required = []

            def deferred_export(source: Path, destination: Path, evidence: Path, ignore: tuple, required: tuple):
                exported_required.append(required)
                for name in required:
                    target = destination / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source / name, target)
                return [], []

            with mock.patch.object(compiler_compare, "run", return_value=0) as run, \
                    mock.patch.object(compiler_compare, "export_tree", side_effect=deferred_export):
                compiler_compare.measure_process_floor(arguments, root / "candidate", root / "ide-cand", root / "work",
                                                       root / "evidence", pull)
            argv = run.call_args.args[0]
            self.assertEqual(argv[argv.index("--cpu") + 1], "2")
            self.assertTrue(argv[2].endswith("process_floor_acceptance.py"))
            record = pull["process_floor"]
            self.assertEqual((record["status"], record["errors"]), ("complete", []))
            self.assertEqual(record["summary"], acceptance)
            self.assertEqual(sorted(name for names in exported_required for name in names),
                             sorted(compiler_compare.PROCESS_FLOOR_REQUIRED))
            self.assertFalse((root / "evidence/process_floor/aa/pairs/0001-a.csv").exists())
            self.assertEqual(compiler_publish.validate_process_floor_bundle(
                dict(record_value, summary=record["summary"]), bundle, "a" * 40, B256), [])

def real_source_evidence() -> tuple[dict, dict]:
    """A complete synthetic throughput-real-source-v1 record and its exported bundle on BINARIES."""
    profile = compiler_receipt.THROUGHPUT_REAL_SOURCE_PROFILE
    inputs = profile["inputs"]
    tests = [{"metric": metric, "round": number, "median_ratio": 1.0, "ci_low": 0.98, "ci_high": 1.02,
              "baseline_relative_mad": 0.01, "candidate_relative_mad": 0.01, "margin_exceedances": 0,
              "pairs": profile["pairs_per_round"], "p_value": 1, "regression": False}
             for metric in ("wall_seconds", "peak_rss_bytes") for number in range(profile["rounds"])]
    cells = [f"{name}/{mode}" for name in inputs for mode in profile["modes"]]
    result = {"schema": 2, "guard_enabled": False, "family_alpha": 0.01, "per_test_alpha": 0.01 / (len(cells) * 2),
              "telemetry": [], "confirmed_regressions": 0, "inconclusive_cases": 0, "valid": True,
              "comparisons": [{"name": cell, "medians": {"wall_seconds": [0.5, 0.49], "peak_rss_bytes": [9e7, 9e7]},
                               "tests": copy.deepcopy(tests), "decision": "diagnostic (guard disabled)"} for cell in cells]}
    jobs = [{"job": index, "name": name, "mode": mode, "artifact": "executable",
             "source": "/work/candidate/" + inputs[name]["descriptor"], "sha256": inputs[name]["input_tree_sha256"],
             "bytes": inputs[name]["requested_translation_unit_bytes"], "physical_lines": 0, "defined_functions": 0}
            for index, (name, mode) in enumerate((name, mode) for name in inputs for mode in profile["modes"])]
    metadata = {"schema": 2, "seed": 20260907, "pairs_per_round": 20, "rounds": 2, "warmups": 2, "cpu": 2,
                "profile": "ci", "scale": 1, "input_schema": 1, "workloads": [], "flags": [], "allocation_compilers": [],
                "cache_policy": "warm-filesystem-new-process", "clock": "monotonic", "jobs": jobs,
                "real_sources": [{"name": name, "descriptor": "/work/candidate/" + pins["descriptor"],
                                  "descriptor_sha256": pins["descriptor_sha256"], "source_root": "/work/inputs/" + name,
                                  "input_tree_sha256": pins["input_tree_sha256"],
                                  "requested_translation_unit_bytes": pins["requested_translation_unit_bytes"],
                                  "inputs": 4, "link_inputs": 2, "operation": "source-to-linked-executable",
                                  "template": "compile_link_argv"} for name, pins in inputs.items()],
                "compiler_provenance": [{"sha256": A256, "bytes": 1}, {"sha256": B256, "bytes": 1}]}
    staging = {"schema": compiler_receipt.THROUGHPUT_REAL_SOURCE_STAGING_SCHEMA, "status": "complete",
               "inputs": {name: {"descriptor": pins["descriptor"], "descriptor_sha256": pins["descriptor_sha256"],
                                 "root": "/work/inputs/" + name,
                                 "archive_sha256": pins["archive"]["sha256"] if pins["archive"] else None,
                                 "files": [{"path": item["path"], "sha256": item["sha256"]} for item in pins["files"]],
                                 "copies": [{"path": item["path"], "sha256": item["sha256"]} for item in pins["copies"]]}
                          for name, pins in inputs.items()}}
    cpuinfo = "processor\t: 0\nmodel name\t: AMD Ryzen 7 9700X 8-Core Processor\nprocessor\t: 1\n" \
              "model name\t: AMD Ryzen 7 9700X 8-Core Processor\n"
    bundle = {"summary": result, "metadata": metadata, "staging": staging, "cpuinfo": cpuinfo}
    record = {"requested": True, "request_line": compiler_receipt.THROUGHPUT_REAL_SOURCE_REQUEST_LINE,
              "profile": copy.deepcopy(profile), "status": "complete", "exit": 0, "errors": [],
              "cells": compiler_receipt.throughput_real_source_digest(result)}
    return record, bundle


class RealSourceThroughputTest(unittest.TestCase):
    """#2761: the fixed throughput-real-source-v1 profile on the pull-compare route."""

    def validate(self, bundle: dict, status: object = 0, binaries: dict = BINARIES) -> list[str]:
        return compiler_receipt.validate_throughput_real_source(status, bundle["summary"], bundle["metadata"],
                                                                bundle["staging"], bundle["cpuinfo"], binaries)

    def test_profile_is_frozen_report_only_and_fits_the_job_budget(self) -> None:
        profile = compiler_receipt.THROUGHPUT_REAL_SOURCE_PROFILE
        self.assertEqual(profile["name"], "throughput-real-source-v1")
        self.assertIs(profile["report_only"], True)
        self.assertIn("--no-guard", profile["arguments"])
        self.assertEqual(profile["arguments"][profile["arguments"].index("--pairs") + 1], str(profile["pairs_per_round"]))
        self.assertEqual(profile["arguments"][profile["arguments"].index("--warmups") + 1], str(profile["warmups"]))
        self.assertEqual(profile["arguments"][profile["arguments"].index("--cpu") + 1], str(profile["cpu"]))
        self.assertEqual(compiler_receipt.THROUGHPUT_REAL_SOURCE_REQUEST_LINE, "profile: " + profile["name"])
        root = Path(__file__).resolve().parents[2]
        for name, pins in profile["inputs"].items():
            with self.subTest(input=name):
                descriptor = (root / pins["descriptor"]).read_bytes()
                self.assertEqual(hashlib.sha256(descriptor).hexdigest(), pins["descriptor_sha256"])
                text = descriptor.decode("utf-8")
                self.assertIn(f"name={name}\n", text)
                self.assertIn(f"input_tree_sha256={pins['input_tree_sha256']}\n", text)
                self.assertIn(f"requested_translation_unit_bytes={pins['requested_translation_unit_bytes']}\n", text)
                for item in pins["copies"]:
                    self.assertEqual(hashlib.sha256((root / item["from"]).read_bytes()).hexdigest(), item["sha256"])
        # The leg's own timeout stays inside the unchanged 90-minute compare-pull job.
        self.assertLessEqual(compiler_compare.REAL_SOURCE_TIMEOUT_SECONDS, 30 * 60)

    def test_complete_evidence_validates_and_every_gap_fails(self) -> None:
        unused_record, bundle = real_source_evidence()
        self.assertEqual(self.validate(bundle), [])
        def change(edit):
            changed = copy.deepcopy(bundle)
            edit(changed)
            return changed
        cases = {
            "missing summary": (change(lambda b: b.update(summary=None)), 0),
            "missing metadata": (change(lambda b: b.update(metadata=None)), 0),
            "failed harness": (bundle, 2),
            "cancelled harness": (bundle, None),
            "signalled harness": (bundle, -15),
            "invalid summary": (change(lambda b: b["summary"].update(valid=False)), 0),
            "guarded run": (change(lambda b: b["summary"].update(guard_enabled=True)), 0),
            "partial cells": (change(lambda b: b["summary"]["comparisons"].pop()), 0),
            "foreign cell": (change(lambda b: b["summary"]["comparisons"][0].update(name="tiny_startup/fast")), 0),
            "gated decision": (change(lambda b: b["summary"]["comparisons"][0].update(decision="regression")), 0),
            "short round": (change(lambda b: b["summary"]["comparisons"][1]["tests"][0].update(pairs=19)), 0),
            "missing round": (change(lambda b: b["summary"]["comparisons"][2]["tests"].pop()), 0),
            "missing median": (change(lambda b: b["summary"]["comparisons"][3]["medians"].update(wall_seconds=[0.5])), 0),
            "different pairs": (change(lambda b: b["metadata"].update(pairs_per_round=10)), 0),
            "synthetic corpus": (change(lambda b: b["metadata"].update(workloads=["tiny_startup"])), 0),
            "extra flag": (change(lambda b: b["metadata"].update(flags=["-O0"])), 0),
            "changed descriptor": (change(lambda b: b["metadata"]["real_sources"][0].update(descriptor_sha256="0" * 64)), 0),
            "changed tree": (change(lambda b: b["metadata"]["real_sources"][1].update(input_tree_sha256="0" * 64)), 0),
            "missing descriptor": (change(lambda b: b["metadata"]["real_sources"].pop()), 0),
            "object job": (change(lambda b: b["metadata"]["jobs"][0].update(artifact="object")), 0),
            "job on another tree": (change(lambda b: b["metadata"]["jobs"][5].update(sha256="0" * 64)), 0),
            "partial jobs": (change(lambda b: b["metadata"]["jobs"].pop()), 0),
            "stale baseline": (change(lambda b: b["metadata"]["compiler_provenance"][0].update(sha256="3" * 64)), 0),
            "stale candidate": (change(lambda b: b["metadata"]["compiler_provenance"][1].update(sha256=A256)), 0),
            "incomplete staging": (change(lambda b: b["staging"].update(status="incomplete")), 0),
            "missing staging": (change(lambda b: b.update(staging=None)), 0),
            "unpinned archive": (change(lambda b: b["staging"]["inputs"]["lua-5.4.8-native"].update(archive_sha256="0" * 64)), 0),
            "unpinned file": (change(lambda b: b["staging"]["inputs"]["cjson-1.7.19-native"]["files"][0].update(sha256="0" * 64)), 0),
            "other host": (change(lambda b: b.update(cpuinfo="model name\t: AMD EPYC 7763 64-Core Processor\n")), 0),
            "mixed host": (change(lambda b: b.update(cpuinfo=b["cpuinfo"] + "model name\t: Intel(R) Xeon(R)\n")), 0),
            "missing host": (change(lambda b: b.update(cpuinfo=None)), 0),
        }
        for name, (changed, status) in cases.items():
            with self.subTest(case=name):
                self.assertTrue(self.validate(changed, status), name)

    def test_decision_needs_pull_mode_and_the_complete_bundle(self) -> None:
        record, bundle = real_source_evidence()
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["throughput_real_source"] = record
        values = {"expected": dict(EXPECTED, mode="pull"), "authorized": True, "compare_result": "success",
                  "receipt": pull, "summary": summary(), "policy_value": "",
                  "throughput": dict(corpus(), real_source_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        failed = copy.deepcopy(record)
        failed.update(status="failed", exit=2)
        stale = copy.deepcopy(record)
        stale["cells"][0]["wall_seconds"] = [0.5, 0.1]
        widened = copy.deepcopy(record)
        widened["profile"]["pairs_per_round"] = 5
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "partial bundle": {"throughput": dict(corpus(), real_source_bundle={
                key: value for key, value in bundle.items() if key != "staging"})},
            "failed run": {"receipt": dict(pull, throughput_real_source=failed)},
            "stale cells": {"receipt": dict(pull, throughput_real_source=stale)},
            "changed profile": {"receipt": dict(pull, throughput_real_source=widened)},
            "not a request": {"receipt": dict(pull, throughput_real_source=dict(record, requested=False))},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("Real-source throughput (throughput-real-source-v1): complete", report)
        self.assertIn("| lua-5.4.8-native/fast | 500.0 | 490.0 | 0.9800 |", report)

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            fixture_git(repo, "init", "-q")
            fixture_git(repo, "config", "user.name", "Real Source Test")
            fixture_git(repo, "config", "user.email", "real-source@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            fixture_git(repo, "add", compiler_receipt.ANALYZER_REQUEST_PATH)
            fixture_git(repo, "commit", "-qm", "seed")
            line = compiler_receipt.THROUGHPUT_REAL_SOURCE_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                fixture_git(repo, "commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.real_source_request_selection(
                        repo, fixture_git(repo, "rev-parse", "HEAD")), expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            fixture_git(repo, "commit", "-qam", "duplicate")
            requested, problem = compiler_compare.real_source_request_selection(repo, fixture_git(repo, "rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.THROUGHPUT_REAL_SOURCE_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = receipt()
        pull.update(mode="pull", throughput_real_source=real_source_evidence()[0])
        expected = {"mode": "pull", "head": head}
        verify = compiler_publish.verify_analyzer_request_freshness
        self.assertEqual(verify(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", verify(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", verify(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("throughput_real_source")
        self.assertIn("no real-source record", verify(Api(1, 0), expected, plain, {}))
        self.assertEqual(verify(Api(1, 1), expected, plain, {}), "")
        self.assertEqual(verify(Api(1, 0), expected, dict(plain, state="superseded"), {}), "")

    def test_staging_checks_every_pin_and_refuses_unsafe_members(self) -> None:
        def tar_gz(members: dict[str, bytes], symlink: str | None = None) -> bytes:
            buffer = io.BytesIO()
            with tarfile.open(fileobj=buffer, mode="w:gz") as bundle:
                for name, data in members.items():
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    bundle.addfile(info, io.BytesIO(data))
                if symlink:
                    info = tarfile.TarInfo(symlink)
                    info.type = tarfile.SYMTYPE
                    info.linkname = "/etc/passwd"
                    bundle.addfile(info)
            return buffer.getvalue()
        def zipped(members: dict[str, bytes]) -> bytes:
            buffer = io.BytesIO()
            with zipfile.ZipFile(buffer, "w") as bundle:
                for name, data in members.items():
                    bundle.writestr(name, data)
            return buffer.getvalue()
        digest = lambda data: hashlib.sha256(data).hexdigest()  # noqa: E731
        archive = tar_gz({"pkg/src/a.c": b"int a;\n", "pkg/src/sub/b.h": b"#define B\n", "pkg/README": b"x\n"})
        amalgamation = zipped({"amal/": b"", "amal/one.c": b"int one;\n"})
        single = b"int single;\n"
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "repo"
            (repo / "tools").mkdir(parents=True)
            for name in ("t", "z", "f"):
                (repo / "tools" / f"{name}.workload").write_text(name, encoding="utf-8")
            (repo / "copy.c").write_bytes(b"int copy;\n")
            pins = {
                "tarred": {"descriptor": "tools/t.workload", "descriptor_sha256": digest(b"t"), "files": [], "copies": [],
                           "archive": {"url": "https://example.invalid/t.tar.gz", "format": "tar.gz", "prefix": "pkg/src/",
                                       "sha256": digest(archive)}},
                "zipped": {"descriptor": "tools/z.workload", "descriptor_sha256": digest(b"z"), "files": [], "copies": [],
                           "archive": {"url": "https://example.invalid/z.zip", "format": "zip", "prefix": "amal/",
                                       "sha256": digest(amalgamation)}},
                "fetched": {"descriptor": "tools/f.workload", "descriptor_sha256": digest(b"f"),
                            "files": [{"path": "single.c", "url": "https://example.invalid/single.c", "sha256": digest(single)}],
                            "copies": [{"path": "generated/copy.c", "from": "copy.c", "sha256": digest(b"int copy;\n")}],
                            "archive": None}}
            served = {"https://example.invalid/t.tar.gz": archive, "https://example.invalid/z.zip": amalgamation,
                      "https://example.invalid/single.c": single}
            def stage(work: str, pinned: dict = pins, fetched: dict = served):
                with mock.patch.object(throughput_real_source, "THROUGHPUT_REAL_SOURCE_INPUTS", pinned):
                    return throughput_real_source.stage(repo, Path(temporary) / work, fetched.__getitem__)
            roots, record = stage("good")
            self.assertEqual(record["status"], "complete")
            self.assertEqual(sorted(str(path.relative_to(roots["tarred"])) for path in roots["tarred"].rglob("*")
                                    if path.is_file()), ["a.c", "sub/b.h"])
            self.assertEqual((roots["zipped"] / "one.c").read_bytes(), b"int one;\n")
            self.assertEqual((roots["fetched"] / "generated/copy.c").read_bytes(), b"int copy;\n")
            self.assertEqual(record["inputs"]["fetched"]["files"], [{"path": "single.c", "sha256": digest(single)}])
            with mock.patch.object(throughput_real_source, "THROUGHPUT_REAL_SOURCE_INPUTS", pins):
                self.assertEqual(throughput_real_source.harness_arguments(repo, roots)[:3],
                                 ["--real-source", str((repo / "tools/t.workload").resolve()), str(roots["tarred"].resolve())])
            bad = {
                "tampered archive": (pins, dict(served, **{"https://example.invalid/t.tar.gz": tar_gz({"pkg/src/a.c": b"int b;\n"})})),
                "tampered file": (pins, dict(served, **{"https://example.invalid/single.c": b"int other;\n"})),
                "changed descriptor": ({**pins, "tarred": dict(pins["tarred"], descriptor_sha256="0" * 64)}, served),
                "changed copy": ({**pins, "fetched": dict(pins["fetched"], copies=[dict(pins["fetched"]["copies"][0], sha256="0" * 64)])}, served),
            }
            for traversal in ("pkg/src/../escape.c", "pkg/src//abs.c"):
                unsafe = tar_gz({traversal: b"x\n"})
                bad[f"unsafe {traversal}"] = ({**pins, "tarred": dict(pins["tarred"], archive=dict(pins["tarred"]["archive"], sha256=digest(unsafe)))},
                                              dict(served, **{"https://example.invalid/t.tar.gz": unsafe}))
            linked = tar_gz({"pkg/src/a.c": b"int a;\n"}, symlink="pkg/src/link.c")
            bad["symlink member"] = ({**pins, "tarred": dict(pins["tarred"], archive=dict(pins["tarred"]["archive"], sha256=digest(linked)))},
                                     dict(served, **{"https://example.invalid/t.tar.gz": linked}))
            for index, (name, (pinned, fetched)) in enumerate(bad.items()):
                with self.subTest(case=name):
                    with self.assertRaises(ValueError):
                        stage(f"bad-{index}", pinned, fetched)

    def test_compare_leg_records_staging_and_harness_failures(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "work").mkdir()
            (root / "evidence").mkdir()
            def leg() -> dict:
                receipt_value = {"binaries": copy.deepcopy(BINARIES), "timings": {},
                                 "throughput_real_source": {"requested": True, "status": "pending"}}
                compiler_compare.measure_real_source(root / "candidate", root / "bin", root / "work", root / "evidence",
                                                     "b" * 40, "a" * 40, receipt_value)
                return receipt_value["throughput_real_source"]
            with mock.patch.object(throughput_real_source, "stage", side_effect=ValueError("archive SHA-256 differs")):
                record = leg()
            self.assertEqual(record["status"], "failed")
            self.assertIn("could not be staged: archive SHA-256 differs", record["errors"][0])
            (root / "work" / "inputs").mkdir()
            with mock.patch.object(throughput_real_source, "stage", return_value=({}, {"status": "complete"})), \
                    mock.patch.object(throughput_real_source, "harness_arguments", return_value=[]), \
                    mock.patch.object(compiler_compare, "run", return_value=2) as run:
                record = leg()
            self.assertEqual(run.call_args.args[0][:3], ["./build.sh", "bench_throughput", "run"])
            self.assertEqual(run.call_args.args[0][-len(compiler_receipt.THROUGHPUT_REAL_SOURCE_PROFILE["arguments"]):],
                             compiler_receipt.THROUGHPUT_REAL_SOURCE_PROFILE["arguments"])
            self.assertEqual((record["status"], record["exit"]), ("failed", 2))
            self.assertTrue(any("exited 2" in error for error in record["errors"]), record["errors"])



def production_profile_evidence(head: str = "a" * 40) -> tuple[dict, dict]:
    """A complete synthetic production-profile-v1 record and its exported bundle: release A256, pgo-lto B256."""
    phases = compiler_receipt.PRODUCTION_PROFILE_PHASES
    ledger = (f"{compiler_receipt.PRODUCTION_PROFILE_LEDGER}\tcomplete=1\tphases={phases}\texpected={phases}\n"
              "index\tlabel\tkind\tvariant\tstart_us\tduration_us\toutcome\tuser_cpu_us\tsystem_cpu_us\tpeak_memory_bytes\n" +
              "".join(f"{slot}\t{name}\t{kind}\t{variant}\t{slot}\t1\tpassed\t1\t1\t1\n"
                      for slot, name, kind, variant in compiler_receipt.PRODUCTION_PROFILE_LEDGER_PHASES))
    build_summary = (f"status=passed\nsource_revision={head}\nsource_tree={'d' * 40}\nproduction_compiler_sha256={B256}\n"
                     "debug_sections=absent\ncorrectness=test_all\nbenchmarks=skipped\nphase_ledger=evidence/phases.tsv\n")
    timed = corpus()
    bundle = {"build_summary": build_summary, "ledger": ledger, "summary": timed["summary"], "metadata": timed["metadata"]}
    record = {"requested": True, "request_line": compiler_receipt.PRODUCTION_PROFILE_REQUEST_LINE,
              "profile": copy.deepcopy(compiler_receipt.PRODUCTION_PROFILE_PROFILE), "status": "complete",
              "build_exit": 0, "exit": 0, "errors": [], "binaries": copy.deepcopy(BINARIES),
              "cells": compiler_receipt.throughput_real_source_digest(timed["summary"])}
    return record, bundle


class ProductionProfileTest(unittest.TestCase):
    """#2761: the fixed production-profile-v1 PGO/LTO profile on the pull-compare route."""

    def validate(self, bundle: dict, build_status: object = 0, status: object = 0, binaries: dict = BINARIES,
                 head: str = "a" * 40) -> list[str]:
        return compiler_receipt.validate_production_profile(build_status, bundle["build_summary"], bundle["ledger"], head,
                                                            status, bundle["summary"], bundle["metadata"], binaries)

    def test_profile_is_frozen_report_only_and_times_the_ordinary_corpus(self) -> None:
        profile = compiler_receipt.PRODUCTION_PROFILE_PROFILE
        self.assertEqual(profile["name"], "production-profile-v1")
        self.assertEqual(compiler_receipt.PRODUCTION_PROFILE_REQUEST_LINE, "profile: " + profile["name"])
        self.assertIs(profile["report_only"], True)
        self.assertEqual(profile["arguments"], [*compiler_receipt.THROUGHPUT_PROFILE["arguments"], "--require-identical-output"])
        self.assertEqual(profile["compilers"], {"baseline": "release", "candidate": "pgo-lto"})
        self.assertIn("--no-benchmark", profile["build_arguments"])
        # The build leg's bound plus the corpus stays inside the unchanged 90-minute compare-pull job.
        self.assertLessEqual(compiler_compare.PRODUCTION_PROFILE_BUILD_TIMEOUT_SECONDS
                             + compiler_compare.THROUGHPUT_TIMEOUT_SECONDS, 75 * 60)
        source = (Path(__file__).resolve().parents[2] / "tools/production_profile.c").read_text(encoding="utf-8")
        self.assertIn(f'LEDGER_VERSION "{compiler_receipt.PRODUCTION_PROFILE_LEDGER}"', source)

    def test_complete_evidence_validates_and_every_gap_fails(self) -> None:
        unused_record, bundle = production_profile_evidence()
        self.assertEqual(self.validate(bundle), [])
        regressed = dict(bundle, **corpus("regression"))
        self.assertEqual(self.validate(regressed, status=1), [], "a measured regression is reported, not refused")
        def change(**fields):
            return dict(bundle, **fields)
        def summary_line(key: str, value: str) -> str:
            return "".join(f"{key}={value}\n" if line.startswith(key + "=") else line + "\n"
                           for line in bundle["build_summary"].splitlines())
        ledger_lines = bundle["ledger"].splitlines(keepends=True)
        cases = {
            "failed build": (bundle, {"build_status": 2}),
            "cancelled build": (bundle, {"build_status": None}),
            "failed corpus": (bundle, {"status": 2}),
            "cancelled corpus": (bundle, {"status": None}),
            "missing build summary": (change(build_summary=None), {}),
            "failed build summary": (change(build_summary=summary_line("status", "failed")), {}),
            "other revision": (change(build_summary=summary_line("source_revision", "f" * 40)), {}),
            "benchmarked build": (change(build_summary=summary_line("benchmarks", "complete")), {}),
            "untested compiler": (change(build_summary=summary_line("correctness", "none")), {}),
            "debug sections": (change(build_summary=summary_line("debug_sections", "present")), {}),
            "other production compiler": (change(build_summary=summary_line("production_compiler_sha256", A256)), {}),
            "repeated key": (change(build_summary=bundle["build_summary"] + "status=passed\n"), {}),
            "missing ledger": (change(ledger=None), {}),
            "incomplete ledger": (change(ledger=bundle["ledger"].replace("complete=1", "complete=0")), {}),
            "partial ledger": (change(ledger="".join(ledger_lines[:-1])), {}),
            "failed phase": (change(ledger=bundle["ledger"].replace("\tpassed\t", "\tfailed\t", 1)), {}),
            "duplicate phases": (change(ledger="".join(ledger_lines[:2] + [ledger_lines[2]] * len(ledger_lines[2:]))), {}),
            "missing training": (change(ledger="".join(line for line in ledger_lines if "\ttrain-lto\t" not in line)
                                        + ledger_lines[-1]), {}),
            "substituted phase": (change(ledger=bundle["ledger"].replace("\ttest-production\t", "\tskip-tests\t")), {}),
            "reordered phases": (change(ledger="".join(ledger_lines[:2] + [ledger_lines[3], ledger_lines[2]] + ledger_lines[4:])), {}),
            "changed columns": (change(ledger=bundle["ledger"].replace("\tpeak_memory_bytes", "\tpeak")), {}),
            "missing corpus": (change(summary=None, metadata=None), {}),
            "partial corpus": (change(summary=dict(bundle["summary"], comparisons=bundle["summary"]["comparisons"][:-1])), {}),
            "stale release": (bundle, {"binaries": {"baseline": {"sha256": "3" * 64}, "candidate": {"sha256": B256}}}),
            "same binary": (bundle, {"binaries": {"baseline": {"sha256": B256}, "candidate": {"sha256": B256}}}),
            "no binaries": (bundle, {"binaries": {}}),
        }
        for name, (changed, options) in cases.items():
            with self.subTest(case=name):
                self.assertTrue(self.validate(changed, **options), name)

    def test_decision_needs_pull_mode_and_the_complete_bundle(self) -> None:
        record, bundle = production_profile_evidence()
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["production_profile"] = record
        values = {"expected": dict(EXPECTED, mode="pull"), "authorized": True, "compare_result": "success",
                  "receipt": pull, "summary": summary(), "policy_value": "",
                  "throughput": dict(corpus(), production_profile_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        failed = dict(copy.deepcopy(record), status="failed", build_exit=2)
        stale = copy.deepcopy(record)
        stale["cells"][0]["wall_seconds"] = [1.0, 0.1]
        widened = copy.deepcopy(record)
        widened["profile"]["build_arguments"] = ["--no-benchmark"]
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "partial bundle": {"throughput": dict(corpus(), production_profile_bundle={
                key: value for key, value in bundle.items() if key != "ledger"})},
            "failed run": {"receipt": dict(pull, production_profile=failed)},
            "stale cells": {"receipt": dict(pull, production_profile=stale)},
            "changed profile": {"receipt": dict(pull, production_profile=widened)},
            "not a request": {"receipt": dict(pull, production_profile=dict(record, requested=False))},
            "with real-source": {"receipt": dict(pull, throughput_real_source=real_source_evidence()[0]),
                                 "throughput": dict(corpus(), production_profile_bundle=bundle,
                                                    real_source_bundle=real_source_evidence()[1])},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("PGO/LTO production profile (production-profile-v1): complete", report)
        self.assertIn("| tiny_startup/fast | 1000.0 | 1300.0 | 1.3000 |", report)

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            fixture_git(repo, "init", "-q")
            fixture_git(repo, "config", "user.name", "Production Profile Test")
            fixture_git(repo, "config", "user.email", "production-profile@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            fixture_git(repo, "add", compiler_receipt.ANALYZER_REQUEST_PATH)
            fixture_git(repo, "commit", "-qm", "seed")
            line = compiler_receipt.PRODUCTION_PROFILE_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                fixture_git(repo, "commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.production_profile_request_selection(
                        repo, fixture_git(repo, "rev-parse", "HEAD")), expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            fixture_git(repo, "commit", "-qam", "duplicate")
            requested, problem = compiler_compare.production_profile_request_selection(
                repo, fixture_git(repo, "rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.PRODUCTION_PROFILE_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = receipt()
        pull.update(mode="pull", production_profile=production_profile_evidence()[0])
        expected = {"mode": "pull", "head": head}
        verify = compiler_publish.verify_analyzer_request_freshness
        self.assertEqual(verify(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", verify(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", verify(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("production_profile")
        self.assertIn("no production-profile record", verify(Api(1, 0), expected, plain, {}))
        self.assertEqual(verify(Api(1, 1), expected, plain, {}), "")
        self.assertEqual(verify(Api(1, 0), expected, dict(plain, state="superseded"), {}), "")

    def test_compare_leg_records_build_and_corpus_failures(self) -> None:
        head = "a" * 40
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("work", "evidence", "bin", "candidate"):
                (root / name).mkdir()
            missing: tuple[str, ...] = ()
            def leg(run_status) -> dict:
                receipt_value = {"timings": {}, "production_profile": {"requested": True, "status": "pending"}}
                with mock.patch.object(compiler_compare, "run", side_effect=run_status) as run, \
                     mock.patch.object(compiler_compare.shutil, "which", side_effect=lambda tool: None if tool in missing else "/usr/bin/" + tool):
                    compiler_compare.measure_production_profile(root / "candidate", root / "bin", root / "work",
                                                                root / "evidence", head, receipt_value)
                return receipt_value["production_profile"], run
            record, run = leg(lambda argv, *rest: 0 if argv[0] == "git" else 2)
            self.assertEqual(run.call_args.args[0][:2], ["./build.sh", "production_profile"])
            self.assertEqual(run.call_args.args[1], root / "work" / "production-profile-source")
            self.assertEqual((record["status"], record["build_exit"], record["exit"]), ("failed", 2, None))
            self.assertTrue(any("build exited 2" in error for error in record["errors"]), record["errors"])
            def built(argv, cwd, *rest):
                if argv[:2] == ["./build.sh", "production_profile"]:
                    output = cwd / "build" / "production-profile"
                    for tree, content in (("release", b"release"), ("pgo-lto", b"pgo-lto")):
                        (output / tree / "Release").mkdir(parents=True)
                        (output / tree / "Release" / "ide").write_bytes(content)
                    (output / "evidence").mkdir()
                    unused_record, bundle = production_profile_evidence(head)
                    (output / "summary.txt").write_text(bundle["build_summary"], encoding="utf-8")
                    (output / "evidence" / "phases.tsv").write_text(bundle["ledger"], encoding="utf-8")
                    return 0
                if argv[0] == "git" and "clone" in argv:
                    (cwd.parent / "work" / "production-profile-source").mkdir()
                return 0 if argv[0] == "git" else 3
            record, run = leg(built)
            self.assertEqual(run.call_args.args[0][:3], ["./build.sh", "bench_throughput", "run"])
            self.assertEqual(run.call_args.args[0][-len(compiler_receipt.PRODUCTION_PROFILE_PROFILE["arguments"]):],
                             compiler_receipt.PRODUCTION_PROFILE_PROFILE["arguments"])
            self.assertEqual(sorted(record["binaries"]), ["baseline", "candidate"])
            self.assertFalse((root / "evidence" / "production_profile" / "release").exists())
            self.assertTrue((root / "evidence" / "production_profile" / "evidence" / "phases.tsv").is_file())
            self.assertEqual((record["status"], record["build_exit"], record["exit"]), ("failed", 0, 3))
            self.assertTrue(any("corpus" in error for error in record["errors"]), record["errors"])
            missing = ("ld.lld",)
            record, run = leg(built)
            run.assert_not_called()
            self.assertEqual((record["status"], record["missing_tools"]), ("failed", ["ld.lld"]))
            self.assertIn("production profile host lacks ld.lld", record["errors"][0])


PROBE_HASHES = {"fast_ra": {"baseline": "3" * 64, "candidate": "4" * 64},
                "line_mark": {"baseline": "5" * 64, "candidate": "6" * 64}}


def probe_line(probe: str, case: dict, value: int) -> str:
    """One sample line in the exact format each probe prints (real 955ea4b2 output, sizes substituted)."""
    markers = case["markers"]
    if probe == "fast_ra":
        return (f"blocks={markers['blocks']} prepass_ns={value // 3} placement_ns={value - value // 3} total_ns={value} "
                "prepass_bytes=167960 total_bytes=938140 spills=0 reloads=0 copies=0 edits=0 frame=0\n")
    return (f"mode=schedule leaves={markers['leaves']} rows={markers['rows']} batch=1 schedule_ns={value} "
            "retained_bytes=393232 permuted=8185 displacement=18282762 hash=c07477f4bdba3597\n")


def probe_invocation(probe: str, case: dict, value: int) -> dict:
    """One passed invocation as compiler_probes.c records it: every *_ns field but the metric dropped."""
    fields = dict(token.split("=", 1) for token in probe_line(probe, case, value).split())
    metric = compiler_receipt.COMPILER_PROBES_PROFILE["probes"][probe]["metric"]
    return {"exit": 0, "samples": [int(fields[metric])] * compiler_receipt.COMPILER_PROBES_PROFILE["repeats"],
            "check": {key: text for key, text in fields.items() if not key.endswith("_ns")}}


def build_probe_harness(directory: Path) -> Path:
    """Compile the trusted native harness exactly as compiler_compare does."""
    harness = directory / "compiler-probes-harness"
    source = Path(compiler_compare.__file__).resolve().with_name(compiler_receipt.COMPILER_PROBES_HARNESS)
    subprocess.run(["clang", *compiler_receipt.COMPILER_PROBES_HARNESS_FLAGS, str(source), "-o", str(harness)],
                   check=True, capture_output=True)
    return harness


def compiler_probes_evidence(base: str = "b" * 40, head: str = "a" * 40) -> tuple[dict, dict]:
    """A complete synthetic compiler-probes-v1 record and bundle: the head is 10% faster on every case."""
    profile = compiler_receipt.COMPILER_PROBES_PROFILE
    cases = []
    for probe, definition in profile["probes"].items():
        for case in definition["cases"]:
            values = {"baseline": 1000, "candidate": 900}
            cases.append({"probe": probe, "name": case["name"], "arguments": list(case["arguments"]),
                          "warmups": {role: [probe_invocation(probe, case, values[role])
                                             for unused in range(profile["warmups"])] for role in values},
                          "pairs": [dict({"order": ["baseline", "candidate"] if index % 2 == 0 else ["candidate", "baseline"]},
                                         **{role: probe_invocation(probe, case, values[role]) for role in values})
                                    for index in range(profile["pairs"])]})
    binaries = {probe: {role: {"sha256": digest, "size_bytes": 1, "revision": base if role == "baseline" else head}
                        for role, digest in roles.items()} for probe, roles in PROBE_HASHES.items()}
    builds = [{"probe": probe, "role": role, "revision": revision, "exit": 0}
              for probe in profile["probes"] for role, revision in (("baseline", base), ("candidate", head))]
    summary = {"schema": compiler_receipt.COMPILER_PROBES_SCHEMA, "profile": profile["name"], "base": base,
               "head": head, "builds": builds, "cases": cases, "status": "complete", "errors": []}
    summary = json.loads(json.dumps(summary))
    record = {"requested": True, "request_line": compiler_receipt.COMPILER_PROBES_REQUEST_LINE,
              "profile": copy.deepcopy(profile), "status": "complete", "exit": 0, "harness_exit": 0, "errors": [],
              "binaries": copy.deepcopy(binaries), "probe_source_sha256": {probe: "7" * 64 for probe in profile["probes"]},
              "cells": compiler_receipt.compiler_probes_digest(summary)}
    return record, {"summary": summary}


class CompilerProbesTest(unittest.TestCase):
    """#2761: the fixed compiler-probes-v1 FAST RA and line-mark profile on the pull-compare route."""

    def validate(self, summary: object, status: object = 0, binaries: dict | None = None, sources: object = None,
                 base: str = "b" * 40, head: str = "a" * 40) -> list[str]:
        record = compiler_probes_evidence()[0]
        return compiler_receipt.validate_compiler_probes(status, summary, base, head,
                                                         record["binaries"] if binaries is None else binaries,
                                                         record["probe_source_sha256"] if sources is None else sources)

    def test_profile_is_frozen_report_only_and_uses_the_audited_commands(self) -> None:
        profile = compiler_receipt.COMPILER_PROBES_PROFILE
        root = Path(__file__).resolve().parents[2]
        self.assertEqual(profile["name"], "compiler-probes-v1")
        self.assertEqual(compiler_receipt.COMPILER_PROBES_REQUEST_LINE, "profile: " + profile["name"])
        self.assertIs(profile["report_only"], True)
        self.assertEqual((profile["cpu"], profile["warmups"], profile["pairs"]), (2, 2, 30))
        for probe, definition in profile["probes"].items():
            with self.subTest(probe=probe):
                self.assertTrue((root / definition["source"]).is_file())
                audit = (root / definition["audit"]).read_text(encoding="utf-8")
                self.assertIn(definition["source"].split("/")[-1], audit)
                # Every flag and source comes from the audit's own command; codegen.c is the one
                # recorded addition, needed by machine.c since #2876.
                for item in [*definition["compile"], *definition["link"], *definition["sources"]]:
                    if item != "src/buster/lib/compiler/codegen/codegen.c":
                        self.assertIn(item.replace("-Isrc", "-I"), audit, item)
                for case in definition["cases"]:
                    self.assertEqual(case["arguments"].count(str(profile["repeats"])), 1, case)
                    self.assertLessEqual(set(case["markers"]), set(definition["check_fields"]), case)
        self.assertLessEqual(compiler_compare.COMPILER_PROBES_TIMEOUT_SECONDS, 20 * 60)

    def test_complete_evidence_validates_and_every_gap_fails(self) -> None:
        record, bundle = compiler_probes_evidence()
        summary = bundle["summary"]
        self.assertEqual(self.validate(summary), [])
        results = compiler_receipt.compiler_probes_digest(summary)
        self.assertEqual(len(results), 12)
        self.assertTrue(all(row["ratio"] == 0.9 and row["candidate_faster"] == 30 for row in results))
        same = compiler_probes_evidence(head="b" * 40)[1]["summary"]
        same_binaries = {probe: {role: {"sha256": "3" * 64, "revision": "b" * 40} for role in ("baseline", "candidate")}
                         for probe in compiler_receipt.COMPILER_PROBES_PROFILE["probes"]}
        self.assertEqual(self.validate(same, binaries=same_binaries, head="b" * 40), [], "an A/A run is valid")

        def changed(edit) -> dict:
            value = copy.deepcopy(summary)
            edit(value)
            return value

        def invocation(edit, case: int = 0, pair: int = 3, role: str = "candidate"):
            return changed(lambda value: edit(value["cases"][case]["pairs"][pair][role]))

        cases = {
            "failed run": (summary, {"status": 1}),
            "cancelled run": (summary, {"status": None}),
            "missing summary": (None, {}),
            "other schema": (changed(lambda value: value.update(schema="other")), {}),
            "changed profile": (changed(lambda value: value.update(profile="compiler-probes-v2")), {}),
            "failed build": (changed(lambda value: value["builds"][3].update(exit=1)), {}),
            "missing build": (changed(lambda value: value["builds"].pop()), {}),
            "build from another revision": (changed(lambda value: value["builds"][0].update(revision="c" * 40)), {}),
            "other base": (summary, {"base": "c" * 40}),
            "other head": (summary, {"head": "c" * 40}),
            "failed status": (changed(lambda value: value.update(status="failed")), {}),
            "reported error": (changed(lambda value: value.update(errors=["line_mark baseline build exited 1"])), {}),
            "missing source hash": (summary, {"sources": {"line_mark": "7" * 64}}),
            "malformed source hash": (summary, {"sources": {"fast_ra": "7", "line_mark": "7" * 64}}),
            "unhashed binary": (summary, {"binaries": dict(record["binaries"], fast_ra={
                "baseline": {"revision": "b" * 40}, "candidate": record["binaries"]["fast_ra"]["candidate"]})}),
            "missing binary": (summary, {"binaries": dict(record["binaries"], line_mark={
                "baseline": record["binaries"]["line_mark"]["baseline"]})}),
            "binary from another revision": (summary, {"binaries": dict(record["binaries"], fast_ra=dict(
                record["binaries"]["fast_ra"], candidate={"sha256": "4" * 64, "revision": "c" * 40}))}),
            "no measured binaries": (summary, {"binaries": {}}),
            "missing case": (changed(lambda value: value["cases"].pop()), {}),
            "reordered cases": (changed(lambda value: value["cases"].reverse()), {}),
            "changed case arguments": (changed(lambda value: value["cases"][0].update(arguments=["256", "1", "chain"])), {}),
            "missing pair": (changed(lambda value: value["cases"][2]["pairs"].pop()), {}),
            "pair out of order": (changed(lambda value: value["cases"][2]["pairs"][1].update(
                order=["baseline", "candidate"])), {}),
            "failed invocation": (invocation(lambda value: value.update(exit=1)), {}),
            "timed-out invocation": (invocation(lambda value: value.update(exit=124)), {}),
            "partial samples": (invocation(lambda value: value["samples"].pop()), {}),
            "zero sample": (invocation(lambda value: value["samples"].__setitem__(0, 0)), {}),
            "overflowing pair samples": (invocation(lambda value: value.update(samples=[10 ** 309] * 11)), {}),
            "overflowing warmup samples": (changed(lambda value: value["cases"][9]["warmups"]["baseline"][1].update(
                samples=[10 ** 309] * 11)), {}),
            "sample past the invocation timeout": (invocation(lambda value: value["samples"].__setitem__(
                5, compiler_receipt.COMPILER_PROBES_SAMPLE_LIMIT_NS + 1)), {}),
            "float sample": (invocation(lambda value: value["samples"].__setitem__(0, 1.5)), {}),
            "unparsed self-check": (invocation(lambda value: value.update(check=None)), {}),
            "other probe size": (invocation(lambda value: value["check"].update(blocks="512")), {}),
            "self-check reduced to its size": (changed(lambda value: [
                invocation_value.update(check={"blocks": invocation_value["check"]["blocks"]})
                for case in value["cases"] if case["probe"] == "fast_ra"
                for invocation_value in [*case["warmups"]["baseline"], *case["warmups"]["candidate"],
                                         *(pair[role] for pair in case["pairs"] for role in ("baseline", "candidate"))]]), {}),
            "missing spill count": (invocation(lambda value: value["check"].pop("spills")), {}),
            "extra self-check field": (invocation(lambda value: value["check"].update(note="1")), {}),
            "non-numeric frame": (invocation(lambda value: value["check"].update(frame="-1")), {}),
            "missing line-mark hash": (invocation(lambda value: value["check"].pop("hash"), case=8), {}),
            "malformed line-mark hash": (invocation(lambda value: value["check"].update(hash="c07477f4"), case=8), {}),
            "wrong line-mark mode": (invocation(lambda value: value["check"].update(mode="remap-ordered"), case=8), {}),
            "wrong line-mark batch": (invocation(lambda value: value["check"].update(batch="999"), case=8), {}),
            "wrong line-mark rows": (invocation(lambda value: value["check"].update(rows="2048"), case=8), {}),
            "remapped line-mark everywhere": (changed(lambda value: [
                invocation_value["check"].update(mode="remap", batch="999")
                for case in value["cases"] if case["probe"] == "line_mark"
                for invocation_value in [*case["warmups"]["baseline"], *case["warmups"]["candidate"],
                                         *(pair[role] for pair in case["pairs"] for role in ("baseline", "candidate"))]]), {}),
            "changing self-check": (invocation(lambda value: value["check"].update(hash="0100")), {}),
            "missing warmup": (changed(lambda value: value["cases"][1]["warmups"]["baseline"].pop()), {}),
            "failed warmup": (changed(lambda value: value["cases"][1]["warmups"]["candidate"][0].update(exit=2)), {}),
        }
        for name, (value, options) in cases.items():
            with self.subTest(case=name):
                self.assertTrue(self.validate(value, **options), name)
                # Malformed evidence is refused with reasons, never raised.
                self.assertIsInstance(compiler_receipt.compiler_probes_digest(value), list)
        self.assertEqual(compiler_receipt.compiler_probes_interval([float(item) for item in range(30)]), (9.0, 20.0))

    def test_decision_needs_pull_mode_and_the_complete_bundle(self) -> None:
        record, bundle = compiler_probes_evidence()
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["compiler_probes"] = record
        values = {"expected": dict(EXPECTED, mode="pull"), "authorized": True, "compare_result": "success",
                  "receipt": pull, "summary": summary(), "policy_value": "",
                  "throughput": dict(corpus(), compiler_probes_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        stale = copy.deepcopy(record)
        stale["cells"][0]["ratio"] = 0.5
        widened = copy.deepcopy(record)
        widened["profile"]["pairs"] = 5
        rebound = copy.deepcopy(record)
        rebound["binaries"]["line_mark"]["candidate"]["revision"] = "c" * 40
        unsourced = copy.deepcopy(record)
        unsourced["probe_source_sha256"] = {}
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "empty bundle": {"throughput": dict(corpus(), compiler_probes_bundle={})},
            "failed run": {"receipt": dict(pull, compiler_probes=dict(record, status="failed", exit=1))},
            "cancelled run": {"receipt": dict(pull, compiler_probes=dict(record, status="pending", exit=None))},
            "stale cells": {"receipt": dict(pull, compiler_probes=stale)},
            "changed profile": {"receipt": dict(pull, compiler_probes=widened)},
            "binary from another revision": {"receipt": dict(pull, compiler_probes=rebound)},
            "no source hashes": {"receipt": dict(pull, compiler_probes=unsourced)},
            "other selector": {"receipt": dict(pull, compiler_probes=dict(record, request_line="profile: other"))},
            "not a request": {"receipt": dict(pull, compiler_probes=dict(record, requested=False))},
            "other base": {"expected": dict(EXPECTED, mode="pull", base="c" * 40)},
            "with the production profile": {"receipt": dict(pull, production_profile=production_profile_evidence()[0])},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("Compiler probes (compiler-probes-v1): complete", report)
        self.assertIn("| fast_ra/chain-256 | 1.0 | 0.9 | 0.9000 [0.9000, 0.9000] | 30/30 | yes |", report)

    def test_artifact_is_read_and_decided_end_to_end(self) -> None:
        def decided(record: dict, probes_summary: object, drop: bool = False) -> tuple:
            pull = receipt()
            pull["mode"] = "pull"
            pull["identity"].update(mode="pull")
            pull["compiler_probes"] = record
            members = {"receipt.json": json.dumps(pull), "lab/summary.json": json.dumps(summary()),
                       "throughput/summary.json": json.dumps(corpus()["summary"]),
                       "throughput/metadata.json": json.dumps(corpus()["metadata"])}
            if not drop:
                members["compiler_probes/summary.json"] = json.dumps(probes_summary)
            got = compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
            return compiler_publish.decide(dict(EXPECTED, mode="pull"), True, "success", got[0], got[1], "",
                                           got[4], extra_reasons=[got[2]] if got[2] else [])

        record, bundle = compiler_probes_evidence()
        conclusion, unused_title, reasons = decided(record, bundle["summary"])
        self.assertEqual((conclusion, reasons), ("success", []))

        def consistent(edit) -> tuple:
            """A malformed artifact whose receipt cells agree with its own samples."""
            changed_summary = copy.deepcopy(bundle["summary"])
            for case in changed_summary["cases"]:
                for item in [*case["warmups"]["baseline"], *case["warmups"]["candidate"],
                             *(pair[role] for pair in case["pairs"] for role in ("baseline", "candidate"))]:
                    edit(case["probe"], item["check"])
            return dict(record, cells=compiler_receipt.compiler_probes_digest(changed_summary)), changed_summary

        def reduce(probe: str, check: dict) -> None:
            for key in [key for key in check if key not in ("blocks", "leaves")]:
                check.pop(key)

        def remap(probe: str, check: dict) -> None:
            if probe == "line_mark":
                check.update(mode="remap", batch="999")

        def overflowed(locate) -> tuple:
            """An artifact with one invocation's samples past any finite float, cells re-derived from it."""
            changed_summary = copy.deepcopy(bundle["summary"])
            locate(changed_summary["cases"][4]).update(samples=[10 ** 309] * 11)
            return dict(record, cells=compiler_receipt.compiler_probes_digest(changed_summary)), changed_summary

        for name, (changed_record, changed_summary, drop) in {
            "overflowing warmup samples": (*overflowed(lambda case: case["warmups"]["candidate"][0]), False),
            "overflowing timed pair samples": (*overflowed(lambda case: case["pairs"][7]["baseline"]), False),
            "self-checks reduced to their size": (*consistent(reduce), False),
            "remapped line-mark": (*consistent(remap), False),
            "missing member": (record, None, True),
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = decided(changed_record, changed_summary, drop)
                self.assertEqual(conclusion, "failure", reasons)

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            fixture_git(repo, "init", "-q")
            fixture_git(repo, "config", "user.name", "Compiler Probes Test")
            fixture_git(repo, "config", "user.email", "compiler-probes@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            fixture_git(repo, "add", compiler_receipt.ANALYZER_REQUEST_PATH)
            fixture_git(repo, "commit", "-qm", "seed")
            line = compiler_receipt.COMPILER_PROBES_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                fixture_git(repo, "commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.compiler_probes_request_selection(
                        repo, fixture_git(repo, "rev-parse", "HEAD")), expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            fixture_git(repo, "commit", "-qam", "duplicate")
            requested, problem = compiler_compare.compiler_probes_request_selection(repo, fixture_git(repo, "rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.COMPILER_PROBES_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = receipt()
        pull.update(mode="pull", compiler_probes=compiler_probes_evidence()[0])
        expected = {"mode": "pull", "head": head}
        verify = compiler_publish.verify_analyzer_request_freshness
        self.assertEqual(verify(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", verify(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", verify(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("compiler_probes")
        self.assertIn("no compiler-probes record", verify(Api(1, 0), expected, plain, {}))
        self.assertEqual(verify(Api(1, 1), expected, plain, {}), "")
        self.assertEqual(verify(Api(1, 0), expected, dict(plain, state="superseded"), {}), "")

    def test_native_harness_self_test_and_profile_match_the_frozen_profile(self) -> None:
        """compiler_probes.c parses, pairs and records invocations itself; its profile is this profile."""
        with tempfile.TemporaryDirectory() as temporary:
            harness = build_probe_harness(Path(temporary))
            result = subprocess.run([str(harness), "--self-test"], capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("COMPILER_PROBES_SELF_TEST PASS failures=0", result.stdout)
            native = json.loads(subprocess.run([str(harness), "--profile"], capture_output=True, text=True,
                                               check=True).stdout)
            profile = compiler_receipt.COMPILER_PROBES_PROFILE
            self.assertEqual({key: native[key] for key in ("name", "compiler", "cpu", "repeats", "warmups", "pairs")},
                             {key: profile[key] for key in ("name", "compiler", "cpu", "repeats", "warmups", "pairs")})
            self.assertEqual(native["invocation_timeout_seconds"], compiler_receipt.COMPILER_PROBES_INVOCATION_TIMEOUT_SECONDS)
            self.assertEqual(native["probes"], {
                probe: {"source": definition["source"], "metric": definition["metric"], "compile": definition["compile"],
                        "sources": definition["sources"], "link": definition["link"],
                        "cases": [{"name": case["name"], "arguments": case["arguments"]} for case in definition["cases"]]}
                for probe, definition in profile["probes"].items()})
            # A leg whose revisions cannot be checked out writes a failed summary the validator refuses.
            root = Path(temporary)
            (root / "repository").mkdir()
            status = subprocess.run([str(harness), str(root / "repository"), "b" * 40, "a" * 40, str(root / "work"),
                                     str(root / "out")], capture_output=True, check=False).returncode
            failed = json.loads((root / "out" / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual((status, failed["status"], failed["cases"]), (1, "failed", []))
            self.assertTrue(any("could not be checked out" in error for error in failed["errors"]), failed["errors"])
            self.assertTrue(compiler_receipt.validate_compiler_probes(status, failed, "b" * 40, "a" * 40, {}, {}))

    def test_compare_leg_hashes_binaries_and_records_failures(self) -> None:
        record_value, bundle = compiler_probes_evidence()
        head, base = "a" * 40, "b" * 40
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("work", "evidence", "candidate"):
                (root / name).mkdir()
            def leg(run_status) -> dict:
                receipt_value = {"timings": {}, "compiler_probes": {"requested": True, "status": "pending"}}
                with mock.patch.object(compiler_compare, "run", side_effect=run_status) as run:
                    compiler_compare.measure_compiler_probes(root / "candidate", root / "work", root / "evidence",
                                                             base, head, receipt_value)
                return receipt_value["compiler_probes"], run
            record, run = leg(lambda *rest: 2)
            source = str(Path(compiler_compare.__file__).resolve().with_name("compiler_probes.c"))
            self.assertEqual(run.call_count, 1, "a failed harness build runs nothing")
            self.assertEqual(run.call_args.args[0][:1] + run.call_args.args[0][-3:],
                             ["clang", source, "-o", str(root / "work" / "compiler-probes-harness")])
            self.assertEqual((record["status"], record["harness_exit"], record["exit"], record["binaries"]),
                             ("failed", 2, 2, {}))
            self.assertTrue(any("exited 2" in error for error in record["errors"]), record["errors"])
            def produced(argv, cwd, *rest):
                if argv[0] == "clang":
                    return 0
                output = Path(argv[5])
                (output / "bin").mkdir(parents=True)
                sources = Path(argv[4]) / "probe-sources"
                sources.mkdir(parents=True)
                for definition in compiler_receipt.COMPILER_PROBES_PROFILE["probes"].values():
                    (sources / Path(definition["source"]).name).write_bytes(b"probe")
                for probe in compiler_receipt.COMPILER_PROBES_PROFILE["probes"]:
                    for role in ("baseline", "candidate"):
                        (output / "bin" / f"{probe}-{role}").write_bytes(f"{probe}-{role}".encode())
                (output / "summary.json").write_text(json.dumps(bundle["summary"]), encoding="utf-8")
                return 0
            output_root = root / "work" / "compiler-probes"
            record, run = leg(produced)
            self.assertFalse((root / "evidence" / "compiler_probes" / "bin").exists())
            self.assertTrue((root / "evidence" / "compiler_probes" / "summary.json").is_file())
            self.assertEqual(run.call_args.args[0][:4], [str(root / "work" / "compiler-probes-harness"),
                                                         str(root / "candidate"), base, head])
            self.assertEqual(record["binaries"]["fast_ra"]["baseline"],
                             {"sha256": hashlib.sha256(b"fast_ra-baseline").hexdigest(), "size_bytes": 16, "revision": base})
            self.assertEqual(record["probe_source_sha256"]["line_mark"], hashlib.sha256(b"probe").hexdigest())
            # The synthetic summary validates against the hashes compare took itself.
            self.assertEqual((record["status"], record["errors"]), ("complete", []))
            self.assertEqual(record["cells"], compiler_receipt.compiler_probes_digest(bundle["summary"]))
            self.assertTrue(output_root.is_dir())

    def test_inventory_coverage_names_both_probe_rows(self) -> None:
        """The compiler-probes leg discharges its two inventory rows only on a validated success."""
        rows = ["tool:fast_ra_scaling.c", "tool:machine_line_mark_scaling.c"]
        self.assertEqual(list(compiler_receipt.INVENTORY_LEGS["pull"]["compiler-probes"]), rows)
        pull = receipt()
        pull.update(mode="pull", compiler_probes=compiler_probes_evidence()[0])
        def leg(receipt_value: dict, conclusion: str) -> dict:
            coverage = compiler_receipt.covered_inventory_rows(receipt_value, conclusion, "pull")
            return next(entry for entry in coverage["legs"] if entry["leg"] == "compiler-probes")
        self.assertEqual(leg(pull, "success")["status"], "discharged")
        self.assertTrue(set(rows) <= set(compiler_receipt.covered_inventory_rows(pull, "success", "pull")["discharged"]))
        failed = dict(pull, compiler_probes=dict(pull["compiler_probes"], status="failed"))
        self.assertEqual(leg(failed, "failure")["status"], "not discharged: leg failed")
        plain = dict(pull)
        plain.pop("compiler_probes")
        self.assertEqual(leg(plain, "success")["status"], "not discharged: not requested")
        self.assertFalse(set(rows) & set(compiler_receipt.covered_inventory_rows(plain, "success", "pull")["discharged"]))


class InventoryCoverageTest(unittest.TestCase):
    """#2761: a published attempt names exactly the inventory rows its validated evidence discharged."""

    PULL_ROWS = ["build:bench_throughput", "build:bench_throughput@throughput-real-source", "build:production_profile",
                 "uarch_lab:compare", "uarch_lab:compare@ast-pilot-acceptance",
                 "uarch_lab:compare@canonical-inline-self-host"]

    def pull(self, **records) -> dict:
        result = receipt()
        result["mode"] = "pull"
        result["identity"].update(mode="pull", ref="refs/pull/7/head")
        result["inline_acceptance"] = {"requested": False, "status": "not requested"}
        result["throughput"] = dict(compiler_receipt.throughput_digest(corpus()["summary"]), exit=0)
        result.update(records)
        return result

    def opt_ins(self, status: str = "complete") -> dict:
        record = {"requested": True, "status": status}
        return {"inline_acceptance": dict(record), "ast_pilot_acceptance": dict(record),
                "throughput_real_source": dict(record), "production_profile": dict(record),
                "scaling_profile": copy.deepcopy(compiler_receipt.SCALING_PROFILE),
                "scaling": {name: {"status": "valid"} for name in compiler_receipt.SCALING_PROFILE["series"]}}

    def legs(self, coverage: dict) -> dict:
        return {entry["leg"]: entry["status"] for entry in coverage["legs"]}

    def test_every_mapped_leg_is_recognized_and_names_rows(self) -> None:
        for mode, legs in compiler_receipt.INVENTORY_LEGS.items():
            self.assertIn(mode, compiler_receipt.MODES)
            for leg, rows in legs.items():
                with self.subTest(mode=mode, leg=leg):
                    self.assertTrue(rows)
                    self.assertNotEqual(compiler_receipt.inventory_leg_record(self.pull(), leg)[1], "unknown leg")

    def test_validated_main_attempt_discharges_the_lab_and_corpus_rows(self) -> None:
        main = dict(receipt(), throughput=dict(compiler_receipt.throughput_digest(corpus()["summary"]), exit=0))
        coverage = compiler_receipt.covered_inventory_rows(main, "success", "main")
        self.assertEqual(coverage["schema"], compiler_receipt.INVENTORY_COVERAGE_SCHEMA)
        self.assertEqual(coverage["route"], "main-compare")
        self.assertEqual(coverage["discharged"], ["build:bench_throughput@main", "uarch_lab:compare@main"])
        self.assertEqual(set(self.legs(coverage).values()), {"discharged"})

    def test_validated_pull_attempt_discharges_each_requested_leg_only(self) -> None:
        coverage = compiler_receipt.covered_inventory_rows(self.pull(**self.opt_ins()), "success", "pull")
        self.assertEqual(coverage["discharged"], self.PULL_ROWS)
        self.assertEqual(self.legs(coverage)["analyzer"], "not discharged: not requested")
        for row in ("build:clang_analyze_benchmark", "tool:clang_analyze_benchmark.c"):
            self.assertEqual(coverage["rows"][row], "not discharged: not requested")
        # A default pull request discharges only the lab and the corpus.
        plain = compiler_receipt.covered_inventory_rows(self.pull(), "success", "pull")
        self.assertEqual(plain["discharged"], ["build:bench_throughput", "uarch_lab:compare"])
        for leg in ("scaling", "inline", "ast-pilot", "real-source", "production-profile"):
            self.assertEqual(self.legs(plain)[leg], "not discharged: not requested")

    def test_mode_is_trusted_not_the_receipts_claim(self) -> None:
        coverage = compiler_receipt.covered_inventory_rows(self.pull(**self.opt_ins()), "success", "main")
        self.assertEqual(coverage["discharged"], ["build:bench_throughput@main", "uarch_lab:compare@main"])
        unknown = compiler_receipt.covered_inventory_rows(self.pull(), "success", "other")
        self.assertEqual((unknown["route"], unknown["legs"], unknown["discharged"]), ("none", [], []))

    def test_failed_legs_discharge_nothing(self) -> None:
        failed = self.pull(**dict(self.opt_ins(), production_profile={"requested": True, "status": "failed"}))
        failed["state"] = "failed"
        coverage = compiler_receipt.covered_inventory_rows(failed, "failure", "pull")
        self.assertEqual(coverage["discharged"], [])
        legs = self.legs(coverage)
        self.assertEqual(legs["production-profile"], "not discharged: leg failed")
        self.assertEqual(legs["self-host"], "not discharged: leg failed")
        self.assertEqual(coverage["rows"]["build:production_profile"], "not discharged: leg failed")
        # A leg the host recorded complete is still not covered when the publisher refused the attempt.
        refused = compiler_receipt.covered_inventory_rows(self.pull(**self.opt_ins()), "failure", "pull")
        self.assertEqual(refused["discharged"], [])
        self.assertEqual(self.legs(refused)["inline"], "not discharged: evidence refused by the publisher")
        # Pending, skipped or malformed records are failures, never coverage.
        for status in ("pending", "skipped", "unknown"):
            with self.subTest(status=status):
                record = self.pull(**dict(self.opt_ins(), throughput_real_source={"requested": True, "status": status}))
                coverage = compiler_receipt.covered_inventory_rows(record, "failure", "pull")
                self.assertEqual(self.legs(coverage)["real-source"], f"not discharged: leg {status}")
        broken_corpus = self.pull(throughput=dict(self.pull()["throughput"], exit=2))
        coverage = compiler_receipt.covered_inventory_rows(broken_corpus, "failure", "pull")
        self.assertEqual(self.legs(coverage)["corpus"], "not discharged: leg failed")
        partial_scaling = self.pull(**dict(self.opt_ins(), scaling={"equal": {"status": "valid"}}))
        coverage = compiler_receipt.covered_inventory_rows(partial_scaling, "failure", "pull")
        self.assertEqual(self.legs(coverage)["scaling"], "not discharged: leg failed")
        # A row both scaling and the corpus discharge is not covered while either requested leg is not.
        self.assertEqual(coverage["rows"]["build:bench_throughput"], "not discharged: evidence refused by the publisher")

    def test_malformed_leg_values_are_not_discharged_and_never_raise(self) -> None:
        # Valid JSON can hold an object or a list where a string belongs; none may stop publication.
        for bad in ({}, [], None, 3, "pending"):
            with self.subTest(value=bad):
                scaling = {name: {"status": bad} for name in compiler_receipt.SCALING_PROFILE["series"]}
                record = self.pull(**dict(self.opt_ins(), scaling=scaling, inline_acceptance={"requested": True, "status": bad},
                                          throughput_real_source={"requested": bad, "status": "complete"}),
                                   throughput=dict(self.pull()["throughput"], valid=bad, exit=bad), state=bad)
                for conclusion in ("success", "failure", "neutral"):
                    coverage = compiler_receipt.covered_inventory_rows(record, conclusion, "pull")
                    if conclusion != "success":
                        self.assertEqual(coverage["discharged"], [])
                self.assertEqual(compiler_receipt.inventory_leg_record(record, "scaling"), (True, "failed"))
                self.assertEqual(compiler_receipt.inventory_leg_record(record, "corpus"), (True, "failed"))
                failed = compiler_receipt.covered_inventory_rows(record, "failure", "pull")
                self.assertEqual(self.legs(failed)["scaling"], "not discharged: leg failed")
        for bad in ({}, [], 3):
            with self.subTest(record=bad):
                coverage = compiler_receipt.covered_inventory_rows(self.pull(production_profile=bad), "failure", "pull")
                self.assertEqual(self.legs(coverage)["production-profile"],
                                 "not discharged: no production-profile record (malformed)")
                coverage = compiler_receipt.covered_inventory_rows(self.pull(scaling_profile={}, scaling=bad), "failure", "pull")
                self.assertEqual(self.legs(coverage)["scaling"], "not discharged: leg failed")

    def test_missing_legs_discharge_nothing(self) -> None:
        for conclusion in ("success", "failure"):
            coverage = compiler_receipt.covered_inventory_rows(None, conclusion, "pull")
            self.assertEqual(coverage["discharged"], [])
            self.assertEqual(set(self.legs(coverage).values()), {"not discharged: no readable receipt"})
            self.assertEqual(set(coverage["rows"]), {row for rows in compiler_receipt.INVENTORY_LEGS["pull"].values()
                                                     for row in rows})
        legacy = receipt()
        legacy.pop("throughput_profile")
        coverage = compiler_receipt.covered_inventory_rows(legacy, "success", "main")
        self.assertEqual(coverage["discharged"], ["uarch_lab:compare@main"])
        self.assertEqual(self.legs(coverage)["corpus"], "not discharged: no corpus record (missing)")
        malformed = self.pull(production_profile="complete")
        coverage = compiler_receipt.covered_inventory_rows(malformed, "success", "pull")
        self.assertEqual(self.legs(coverage)["production-profile"], "not discharged: no production-profile record (malformed)")
        self.assertNotIn("build:production_profile", coverage["discharged"])

    def test_superseded_receipt_discharges_nothing(self) -> None:
        coverage = compiler_receipt.covered_inventory_rows(dict(self.pull(**self.opt_ins()), state="superseded"),
                                                           "neutral", "pull")
        self.assertEqual(coverage["discharged"], [])
        self.assertEqual(self.legs(coverage)["self-host"], "not discharged: superseded before measurement")
        self.assertEqual(self.legs(coverage)["analyzer"], "not discharged: not requested")

    def test_analyzer_only_profile_discharges_only_the_analyzer_rows(self) -> None:
        coverage = compiler_receipt.covered_inventory_rows(analyzer_receipt(), "success", "pull")
        self.assertEqual(coverage["discharged"], ["build:clang_analyze_benchmark", "tool:clang_analyze_benchmark.c"])
        legs = self.legs(coverage)
        for leg in ("self-host", "corpus", "scaling", "inline", "ast-pilot", "real-source", "production-profile"):
            self.assertEqual(legs[leg], "not discharged: not requested")
        for row in ("uarch_lab:compare", "build:bench_throughput"):
            self.assertEqual(coverage["rows"][row], "not discharged: not requested")
        # An analyzer receipt cannot discharge a main row.
        self.assertEqual(compiler_receipt.covered_inventory_rows(analyzer_receipt(), "success", "main")["discharged"], [])
        failed = compiler_receipt.covered_inventory_rows(analyzer_receipt("failed"), "failure", "pull")
        self.assertEqual(self.legs(failed)["analyzer"], "not discharged: leg failed")

    def test_coverage_is_report_only_and_rendered(self) -> None:
        pull = self.pull(**self.opt_ins())
        before = copy.deepcopy(pull)
        coverage = compiler_receipt.covered_inventory_rows(pull, "success", "pull")
        self.assertEqual(pull, before, "the mapping never edits the receipt")
        report = compiler_receipt.render(dict(pull, inventory_coverage=coverage), summary(), "success", [])
        self.assertIn("route `pull-compare`): 6 rows discharged", report)
        self.assertIn("| production-profile | `build:production_profile` | complete | discharged |", report)
        self.assertIn("| analyzer | `build:clang_analyze_benchmark`, `tool:clang_analyze_benchmark.c` | not requested "
                      "| not discharged: not requested |", report)
        analyzer = analyzer_receipt()
        rendered = compiler_receipt.render(
            dict(analyzer, inventory_coverage=compiler_receipt.covered_inventory_rows(analyzer, "failure", "pull")),
            {}, "failure", [])
        self.assertIn("0 rows discharged", rendered)
        shown = {"inventory_coverage": coverage}
        self.assertIn("`build:production_profile`", compiler_publish.inventory_rows(shown))
        self.assertEqual(compiler_publish.inventory_rows({}), "none")



ZEN5 = "AMD Ryzen 7 9700X 8-Core Processor"


def frontend_selector_evidence() -> tuple[dict, dict]:
    """A complete synthetic frontend-selector-v1 record and its exported bundle on BINARIES."""
    profile = compiler_receipt.FRONTEND_SELECTOR_PROFILE
    scaling, selection, bench_profile = profile["frontend_scaling"], profile["selection"], profile["ide_bench"]
    hashes = {"baseline": A256, "candidate": B256}
    provenance = {"binary_sha256": dict(hashes), "suite": "all", "warmups": scaling["warmups"],
                  "repetitions": scaling["repetitions"], "cpu": profile["cpu"], "affinity": [profile["cpu"]],
                  "cpu_model": ZEN5, "complete": True}
    results = []
    for kind, count, mode in compiler_receipt.frontend_scaling_population():
        row = {"kind": kind, "count": count, "mode": mode, "source_bytes": 10, "source_sha256": "5" * 64,
               "compilers": {role: {"command": [role], "wall_seconds": [0.01] * scaling["repetitions"],
                                    "cpu_seconds": [0.0] * scaling["repetitions"], "wall_median": 0.01, "cpu_median": 0.0}
                             for role in hashes}}
        if mode == "object":
            row["object_sha256"] = "6" * 64
        results.append(row)
    def sample(name: str) -> dict:
        metrics = {"version": 1, "iterations": 30, "functions": 3, "fallback_functions": 0, "ir_instructions": 40,
                   "mir_instructions": 50, "min_ns": 900, "median_ns": 1000, "prepare_median_ns": 10,
                   "total_median_ns": 1100, "arena_bytes": 100, "peak_arena_bytes": 90, "module_bytes": 80}
        return {"selection": metrics, "target": "BENCH_SELECT_TARGET arch=x86_64",
                "objects": {allocator: {"wall_ns": 2000, "sha256": hashlib.sha256((name + allocator).encode()).hexdigest(),
                                        "bytes": 64} for allocator in selection["allocators"]}}
    report = {"schema_version": 1, "build_description": profile["name"],
              "host": {"system": "Linux", "cpu": ZEN5, "affinity": [profile["cpu"]]},
              "compilers": {role: {"path": f"/bin/{role}", "sha256": digest} for role, digest in hashes.items()},
              "workloads": [{"path": f"/work/selection-sources/{name}", "sha256": "4" * 64, "bytes": 1,
                             "samples": {role: [sample(name) for _ in range(selection["samples"])] for role in hashes},
                             "summary": {role: {"selection_median_ns": 1000, "selection_ns_per_mir": 20.0,
                                                "compile_median_ns": {"fast": 2000, "quality": 2000}} for role in hashes}}
                            for name in selection["workloads"]]}
    line = ("BENCH_C_FRONTEND path=tests/basic_c_operations.c iterations=30 bytes=4096 min_ns=900 median_ns=1000 "
            "throughput_mb_s=4")
    runs = [{"role": role, "exit": 0, "line": line} for pair in range(bench_profile["pairs"])
            for role in (("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline"))]
    bench = {"schema": compiler_receipt.FRONTEND_SELECTOR_SCHEMA, "cpu": profile["cpu"], "cpu_model": ZEN5,
             "binaries": dict(hashes), "runs": runs}
    bundle = {"provenance": provenance, "results": results, "report": report, "bench": bench}
    record = {"requested": True, "request_line": compiler_receipt.FRONTEND_SELECTOR_REQUEST_LINE,
              "profile": copy.deepcopy(profile), "status": "complete", "errors": [],
              "exits": {"frontend_scaling": 0, "selection": 0},
              "cells": compiler_receipt.frontend_selector_digest(results, report, bench)}
    return record, bundle


class FrontendSelectorTest(unittest.TestCase):
    """#2761: the fixed frontend-selector-v1 probes on the pull-compare route."""

    def validate(self, bundle: dict, scaling_status: object = 0, selection_status: object = 0,
                 binaries: dict = BINARIES) -> list[str]:
        return compiler_receipt.validate_frontend_selector(scaling_status, bundle["provenance"], bundle["results"],
                                                           selection_status, bundle["report"], bundle["bench"], binaries)

    def test_profile_is_frozen_report_only_and_fits_the_job(self) -> None:
        profile = compiler_receipt.FRONTEND_SELECTOR_PROFILE
        self.assertEqual(profile["name"], "frontend-selector-v1")
        self.assertEqual(compiler_receipt.FRONTEND_SELECTOR_REQUEST_LINE, "profile: " + profile["name"])
        self.assertIs(profile["report_only"], True)
        for leg in ("frontend_scaling", "selection"):
            arguments = profile[leg]["arguments"]
            self.assertEqual(arguments[arguments.index("--cpu") + 1], str(profile["cpu"]))
        self.assertIn("--check", profile["selection"]["arguments"])
        self.assertEqual(len(compiler_receipt.frontend_scaling_population()), 54)
        # Each tool's bound plus the ordinary comparison stays inside the unchanged 90-minute compare-pull job.
        self.assertLessEqual(compiler_compare.FRONTEND_SCALING_TIMEOUT_SECONDS + compiler_compare.SELECTION_TIMEOUT_SECONDS
                             + 2 * profile["ide_bench"]["pairs"] * compiler_compare.IDE_BENCH_TIMEOUT_SECONDS, 75 * 60)

    def test_population_matches_the_frontend_scaling_tool(self) -> None:
        """The real tool, on stand-in compilers, emits exactly the frozen population and a complete provenance."""
        root_dir = Path(__file__).resolve().parents[2]
        profile = compiler_receipt.FRONTEND_SELECTOR_PROFILE
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compilers = {}
            for role in ("baseline", "candidate"):
                compilers[role] = root / role
                compilers[role].write_text("#!/bin/sh\nwhile [ $# -gt 0 ]; do if [ \"$1\" = -o ]; then "
                                           "printf object > \"$2\"; fi; shift; done\n", encoding="utf-8")
                compilers[role].chmod(0o755)
            arguments = list(profile["frontend_scaling"]["arguments"])
            del arguments[arguments.index("--cpu"):arguments.index("--cpu") + 2]
            subprocess.run([sys.executable, "-B", str(root_dir / "tools/benchmark_frontend_scaling.py"),
                            "--baseline", str(compilers["baseline"]), "--candidate", str(compilers["candidate"]),
                            "--out", str(root / "out"), *arguments], check=True, capture_output=True)
            provenance = json.loads((root / "out/provenance.json").read_text(encoding="utf-8"))
            results = json.loads((root / "out/results.json").read_text(encoding="utf-8"))
        self.assertIs(provenance["complete"], True)
        provenance.update(cpu=profile["cpu"], affinity=[profile["cpu"]], cpu_model=ZEN5)
        unused_record, bundle = frontend_selector_evidence()
        binaries = {role: {"sha256": digest} for role, digest in provenance["binary_sha256"].items()}
        bundle.update(provenance=provenance, results=results)
        problems = [problem for problem in self.validate(bundle, binaries=binaries) if problem.startswith("frontend")]
        self.assertEqual(problems, [])

    def test_complete_evidence_validates_and_every_gap_fails(self) -> None:
        unused_record, bundle = frontend_selector_evidence()
        self.assertEqual(self.validate(bundle), [])
        def change(key: str, mutate) -> dict:
            changed = copy.deepcopy(bundle)
            mutate(changed[key])
            return changed
        def drop(container: dict, key: str):
            container.pop(key)
        def first_sample(report: dict) -> dict:
            return report["workloads"][0]["samples"]["candidate"][0]
        cases = {
            "failed frontend scaling": (bundle, {"scaling_status": 1}),
            "cancelled frontend scaling": (bundle, {"scaling_status": None}),
            "failed selection": (bundle, {"selection_status": 1}),
            "cancelled selection": (bundle, {"selection_status": 124}),
            "stale baseline": (bundle, {"binaries": {"baseline": {"sha256": "3" * 64}, "candidate": {"sha256": B256}}}),
            "no binaries": (bundle, {"binaries": {}}),
            "missing provenance": (dict(bundle, provenance=None), {}),
            "incomplete provenance": (change("provenance", lambda value: value.update(complete=False)), {}),
            "other suite": (change("provenance", lambda value: value.update(suite="legacy")), {}),
            "fewer repetitions": (change("provenance", lambda value: value.update(repetitions=3)), {}),
            "unpinned frontend": (change("provenance", lambda value: value.update(affinity=[0, 1, 2, 3])), {}),
            "frontend not on Zen 5": (change("provenance", lambda value: value.update(cpu_model="Intel Xeon")), {}),
            "frontend other binary": (change("provenance", lambda value: value["binary_sha256"].update(candidate=A256)), {}),
            "missing results": (dict(bundle, results=None), {}),
            "partial results": (change("results", lambda value: value.pop()), {}),
            "reordered results": (change("results", lambda value: value.reverse()), {}),
            "short samples": (change("results", lambda value: value[3]["compilers"]["candidate"]["wall_seconds"].pop()), {}),
            "zero wall": (change("results", lambda value: value[0]["compilers"]["baseline"]["wall_seconds"].__setitem__(0, 0)), {}),
            "object without hash": (change("results", lambda value: drop(value[1], "object_sha256")), {}),
            "missing report": (dict(bundle, report=None), {}),
            "selection not on Zen 5": (change("report", lambda value: value["host"].update(cpu="AMD EPYC 7763")), {}),
            "unpinned selection": (change("report", lambda value: value["host"].update(affinity=None)), {}),
            "selection other binary": (change("report", lambda value: value["compilers"]["candidate"].update(sha256=A256)), {}),
            "missing workload": (change("report", lambda value: value["workloads"].pop()), {}),
            "short selection": (change("report", lambda value: value["workloads"][1]["samples"]["baseline"].pop()), {}),
            "fallback selection": (change("report", lambda value: first_sample(value)["selection"].update(
                fallback_functions=1)), {}),
            "changed work": (change("report", lambda value: first_sample(value)["selection"].update(mir_instructions=51)), {}),
            "changed target": (change("report", lambda value: first_sample(value).update(target="other")), {}),
            "different object": (change("report", lambda value: first_sample(value)["objects"]["fast"].update(
                sha256="9" * 64)), {}),
            "missing allocator": (change("report", lambda value: drop(first_sample(value)["objects"], "quality")), {}),
            "missing bench": (dict(bundle, bench=None), {}),
            "bench not on Zen 5": (change("bench", lambda value: value.update(cpu_model="NA")), {}),
            "bench changed binary": (change("bench", lambda value: value.update(binaries=None)), {}),
            "partial bench": (change("bench", lambda value: value["runs"].pop()), {}),
            "failed bench run": (change("bench", lambda value: value["runs"][4].update(exit=1)), {}),
            "unreported bench run": (change("bench", lambda value: value["runs"][5].update(line=None)), {}),
            "other bench source": (change("bench", lambda value: value["runs"][2].update(
                line=value["runs"][2]["line"].replace("bytes=4096", "bytes=4097"))), {}),
            "debug bench": (change("bench", lambda value: value["runs"][2].update(
                line=value["runs"][2]["line"].replace("iterations=30", "iterations=5"))), {}),
            "unpaired bench": (change("bench", lambda value: value["runs"].__setitem__(1, dict(value["runs"][0]))), {}),
        }
        for name, (changed, options) in cases.items():
            with self.subTest(case=name):
                self.assertTrue(self.validate(changed, **options), name)

    def test_decision_needs_pull_mode_and_the_complete_bundle(self) -> None:
        record, bundle = frontend_selector_evidence()
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["frontend_selector"] = record
        values = {"expected": dict(EXPECTED, mode="pull"), "authorized": True, "compare_result": "success",
                  "receipt": pull, "summary": summary(), "policy_value": "",
                  "throughput": dict(corpus(), frontend_selector_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        failed = dict(copy.deepcopy(record), status="failed", exits={"frontend_scaling": 1, "selection": 0})
        stale = copy.deepcopy(record)
        stale["cells"][0]["values"] = [1.0, 0.1]
        widened = copy.deepcopy(record)
        widened["profile"]["selection"]["samples"] = 3
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "partial bundle": {"throughput": dict(corpus(), frontend_selector_bundle={
                key: value for key, value in bundle.items() if key != "bench"})},
            "failed run": {"receipt": dict(pull, frontend_selector=failed)},
            "stale cells": {"receipt": dict(pull, frontend_selector=stale)},
            "changed profile": {"receipt": dict(pull, frontend_selector=widened)},
            "not a request": {"receipt": dict(pull, frontend_selector=dict(record, requested=False))},
            "with production profile": {"receipt": dict(pull, production_profile=production_profile_evidence()[0]),
                                        "throughput": dict(corpus(), frontend_selector_bundle=bundle,
                                                           production_profile_bundle=production_profile_evidence()[1])},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("Frontend and selector probes (frontend-selector-v1): complete", report)
        self.assertIn("| frontend small-1/syntax | 10.0 ms | 10.0 ms | 1.0000 |", report)
        self.assertIn("| ide bench | 1.0 us | 1.0 us | 1.0000 |", report)

    def publish(self, record: dict, bundle: dict) -> tuple:
        """ZIP -> read_evidence -> decide on a pull receipt carrying record, as the publisher runs it."""
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["frontend_selector"] = record
        members = {"receipt.json": json.dumps(pull), "lab/summary.json": json.dumps(summary()),
                   "throughput/summary.json": json.dumps(corpus()["summary"]),
                   "throughput/metadata.json": json.dumps(corpus()["metadata"]),
                   **{path: json.dumps(bundle[key]) for key, path in compiler_publish.FRONTEND_SELECTOR_BUNDLE_FILES.items()}}
        read, lab, problem, unused_artifact, throughput = compiler_publish.read_evidence(
            FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
        self.assertEqual(problem, "")
        return compiler_publish.decide(dict(EXPECTED, mode="pull"), True, "success", read, lab, "", throughput)

    def test_reported_medians_are_rederived_from_the_samples(self) -> None:
        record, bundle = frontend_selector_evidence()
        self.assertEqual(self.publish(record, bundle)[0], "success")
        frontend = copy.deepcopy(bundle)
        frontend["results"][0]["compilers"]["candidate"]["wall_median"] = 0.001
        selection = copy.deepcopy(bundle)
        selection["report"]["workloads"][0]["summary"]["candidate"]["selection_median_ns"] = 100
        compile_time = copy.deepcopy(bundle)
        compile_time["report"]["workloads"][2]["summary"]["baseline"]["compile_median_ns"]["quality"] = 1
        for name, changed in (("frontend wall median", frontend), ("selection median", selection),
                              ("compile median", compile_time)):
            with self.subTest(case=name):
                # The cells come from the samples, so a contradictory summary cannot move the reported ratio.
                self.assertEqual(compiler_receipt.frontend_selector_digest(changed["results"], changed["report"],
                                                                           changed["bench"]), record["cells"])
                self.assertTrue(any("medians its samples do not give" in problem for problem in self.validate(changed)))
                conclusion, unused_title, reasons = self.publish(record, changed)
                self.assertEqual(conclusion, "failure", reasons)

    def test_malformed_selection_identities_fail_without_raising(self) -> None:
        record, bundle = frontend_selector_evidence()
        def every_sample(report: dict, mutate) -> None:
            for row in report["workloads"]:
                for samples in row["samples"].values():
                    for sample in samples:
                        mutate(sample)
        cases = {
            "list target": lambda report: report["workloads"][0]["samples"]["baseline"][0].update(target=[]),
            "object hash": lambda report: report["workloads"][0]["samples"]["candidate"][3]["objects"]["fast"].update(
                sha256={}),
            "no target anywhere": lambda report: every_sample(report, lambda sample: sample.pop("target")),
            "no ir_instructions anywhere": lambda report: every_sample(
                report, lambda sample: sample["selection"].pop("ir_instructions")),
            "boolean work count": lambda report: report["workloads"][1]["samples"]["baseline"][2]["selection"].update(
                functions=True),
        }
        for name, mutate in cases.items():
            changed = copy.deepcopy(bundle)
            mutate(changed["report"])
            with self.subTest(case=name):
                self.assertTrue(any("malformed" in problem for problem in self.validate(changed)), name)
                conclusion, unused_title, reasons = self.publish(record, changed)
                self.assertEqual(conclusion, "failure", reasons)

    def test_out_of_range_numbers_fail_closed_through_publication(self) -> None:
        record, bundle = frontend_selector_evidence()
        huge = 10 ** 309
        line = bundle["bench"]["runs"][0]["line"]
        cases = {
            "frontend wall sample": lambda changed: changed["results"][0]["compilers"]["candidate"]["wall_seconds"]
            .__setitem__(0, huge),
            "frontend cpu sample": lambda changed: changed["results"][5]["compilers"]["baseline"]["cpu_seconds"]
            .__setitem__(1, huge),
            "selection median": lambda changed: changed["report"]["workloads"][0]["samples"]["baseline"][0]["selection"]
            .update(median_ns=huge),
            "selection compile time": lambda changed: changed["report"]["workloads"][1]["samples"]["candidate"][0]["objects"]
            ["quality"].update(wall_ns=huge),
            "ide bench median": lambda changed: [run.update(line=line.replace("median_ns=1000", "median_ns=" + "9" * 330))
                                                 for run in changed["bench"]["runs"][:2]],
            # Past Python's 4300-digit integer-string conversion limit, where int() itself raises.
            "ide bench 5000-digit median": lambda changed: [run.update(
                line=line.replace("median_ns=1000", "median_ns=" + "9" * 5000)) for run in changed["bench"]["runs"][:2]],
            "ide bench 5000-digit bytes": lambda changed: changed["bench"]["runs"][3].update(
                line=line.replace("bytes=4096", "bytes=" + "1" * 5000)),
        }
        for name, mutate in cases.items():
            changed = copy.deepcopy(bundle)
            mutate(changed)
            with self.subTest(case=name):
                self.assertTrue(self.validate(changed), name)
                conclusion, unused_title, reasons = self.publish(record, changed)
                self.assertEqual(conclusion, "failure", reasons)

    def test_over_limit_integer_text_fails_closed(self) -> None:
        """A BENCH_C_FRONTEND token or a JSON integer past the conversion limit is a structured refusal."""
        record, bundle = frontend_selector_evidence()
        line = bundle["bench"]["runs"][0]["line"]
        for field in ("iterations=30", "bytes=4096", "min_ns=900", "median_ns=1000", "throughput_mb_s=4"):
            key = field.split("=")[0]
            with self.subTest(field=key):
                self.assertIsNone(compiler_receipt.ide_bench_fields(line.replace(field, f"{key}={'7' * 5000}")))
                self.assertIsNone(compiler_receipt.ide_bench_fields(line.replace(field, f"{key}={'7' * 20}")))
        self.assertEqual(compiler_receipt.ide_bench_fields(line.replace("bytes=4096", "bytes=" + "9" * 18))["bytes"],
                         int("9" * 18))
        # A JSON member carrying such an integer is unreadable evidence, not an exception.
        members = {"receipt.json": json.dumps(dict(receipt(), mode="pull", frontend_selector=record)),
                   "lab/summary.json": json.dumps(summary()),
                   "throughput/summary.json": json.dumps(corpus()["summary"]),
                   "throughput/metadata.json": json.dumps(corpus()["metadata"]),
                   **{path: json.dumps(bundle[key]) for key, path in compiler_publish.FRONTEND_SELECTOR_BUNDLE_FILES.items()}}
        path = compiler_publish.FRONTEND_SELECTOR_BUNDLE_FILES["results"]
        members[path] = members[path].replace('"count": 1,', '"count": ' + "1" * 5000 + ",", 1)
        got = compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
        self.assertIn(path, got[2])
        self.assertIsNone(got[4]["frontend_selector_bundle"])

    def test_any_wrongly_typed_field_fails_closed_without_raising(self) -> None:
        unused_record, bundle = frontend_selector_evidence()
        def paths(value: object, prefix: tuple = ()):
            yield prefix
            children = value.items() if isinstance(value, dict) else enumerate(value[:1]) if isinstance(value, list) else ()
            for key, child in children:
                yield from paths(child, (*prefix, key))
        replacements = (None, [], {}, "x", -1, 1.5, True, [[]], {"": {}}, 10 ** 309, -(10 ** 309),
                        float("nan"), float("inf"))
        checked = 0
        for key in bundle:
            for path in paths(bundle[key]):
                for replacement in replacements:
                    changed = copy.deepcopy(bundle)
                    if path:
                        holder = changed[key]
                        for step in path[:-1]:
                            holder = holder[step]
                        holder[path[-1]] = replacement
                    else:
                        changed[key] = replacement
                    compiler_receipt.frontend_selector_digest(changed["results"], changed["report"], changed["bench"])
                    self.validate(changed)
                    checked += 1
        self.assertGreater(checked, 500)

    def test_read_evidence_requires_every_bundle_member(self) -> None:
        record, bundle = frontend_selector_evidence()
        pull = dict(receipt(), mode="pull", frontend_selector=record)
        files = compiler_publish.FRONTEND_SELECTOR_BUNDLE_FILES
        complete = {"receipt.json": json.dumps(pull), "lab/summary.json": json.dumps(summary()),
                    "throughput/summary.json": json.dumps(corpus()["summary"]),
                    "throughput/metadata.json": json.dumps(corpus()["metadata"]),
                    **{path: json.dumps(bundle[key]) for key, path in files.items()}}
        got = compiler_publish.read_evidence(FakeApi(archive(complete)), "92", "buster-9700x-compiler-x-1")
        self.assertEqual((got[2], got[4]["frontend_selector_bundle"]), ("", bundle))
        for key, path in files.items():
            for name, members in (("missing", {member: value for member, value in complete.items() if member != path}),
                                  ("malformed", dict(complete, **{path: "{not json"})),
                                  ("duplicate key", dict(complete, **{path: '{"a": 1, "a": 2}'}))):
                with self.subTest(member=key, case=name):
                    got = compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
                    self.assertIn(path, got[2])
                    self.assertIsNone(got[4]["frontend_selector_bundle"])

    def test_inventory_coverage_names_the_three_probe_rows(self) -> None:
        """The frontend-selector leg discharges its three inventory rows only on a validated success."""
        rows = ["tool:benchmark_frontend_scaling.py", "tool:selection_benchmark.py", "ide:bench"]
        self.assertEqual(list(compiler_receipt.INVENTORY_LEGS["pull"]["frontend-selector"]), rows)
        pull = receipt()
        pull.update(mode="pull", frontend_selector=frontend_selector_evidence()[0])
        def leg(receipt_value: dict, conclusion: str) -> dict:
            coverage = compiler_receipt.covered_inventory_rows(receipt_value, conclusion, "pull")
            return next(entry for entry in coverage["legs"] if entry["leg"] == "frontend-selector")
        self.assertEqual(leg(pull, "success")["status"], "discharged")
        self.assertTrue(set(rows) <= set(compiler_receipt.covered_inventory_rows(pull, "success", "pull")["discharged"]))
        failed = dict(pull, frontend_selector=dict(pull["frontend_selector"], status="failed"))
        self.assertEqual(leg(failed, "failure")["status"], "not discharged: leg failed")
        plain = dict(pull)
        plain.pop("frontend_selector")
        self.assertEqual(leg(plain, "success")["status"], "not discharged: not requested")
        self.assertFalse(set(rows) & set(compiler_receipt.covered_inventory_rows(plain, "success", "pull")["discharged"]))

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            fixture_git(repo, "init", "-q")
            fixture_git(repo, "config", "user.name", "Frontend Selector Test")
            fixture_git(repo, "config", "user.email", "frontend-selector@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            fixture_git(repo, "add", compiler_receipt.ANALYZER_REQUEST_PATH)
            fixture_git(repo, "commit", "-qm", "seed")
            line = compiler_receipt.FRONTEND_SELECTOR_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                fixture_git(repo, "commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.frontend_selector_request_selection(
                        repo, fixture_git(repo, "rev-parse", "HEAD")), expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            fixture_git(repo, "commit", "-qam", "duplicate")
            requested, problem = compiler_compare.frontend_selector_request_selection(
                repo, fixture_git(repo, "rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.FRONTEND_SELECTOR_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = receipt()
        pull.update(mode="pull", frontend_selector=frontend_selector_evidence()[0])
        expected = {"mode": "pull", "head": head}
        verify = compiler_publish.verify_analyzer_request_freshness
        self.assertEqual(verify(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", verify(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", verify(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("frontend_selector")
        self.assertIn("no frontend-selector record", verify(Api(1, 0), expected, plain, {}))
        self.assertEqual(verify(Api(1, 1), expected, plain, {}), "")
        self.assertEqual(verify(Api(1, 0), expected, dict(plain, state="superseded"), {}), "")

    def test_compare_leg_records_tool_failures_and_missing_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("work", "evidence", "bin", "candidate"):
                (root / name).mkdir()
            for name, content in (("ide-base", b"base"), ("ide-cand", b"cand")):
                (root / "bin" / name).write_bytes(content)
            binaries = {"baseline": {"sha256": hashlib.sha256(b"base").hexdigest()},
                        "candidate": {"sha256": hashlib.sha256(b"cand").hexdigest()}}
            def leg(run_status) -> tuple[dict, mock.MagicMock]:
                receipt_value = {"timings": {}, "binaries": binaries,
                                 "frontend_selector": {"requested": True, "status": "pending"}}
                with mock.patch.object(compiler_compare, "run", side_effect=run_status) as run:
                    compiler_compare.measure_frontend_selector(root / "candidate", root / "bin", root / "work",
                                                               root / "evidence", receipt_value)
                return receipt_value["frontend_selector"], run
            record, run = leg(lambda argv, cwd, log, timeout: 2)
            commands = [call.args[0] for call in run.call_args_list]
            self.assertEqual(commands[0][2], "tools/benchmark_frontend_scaling.py")
            self.assertEqual(commands[1][2], "tools/selection_benchmark.py")
            self.assertEqual(len(commands), 2 + 2 * compiler_receipt.FRONTEND_SELECTOR_PROFILE["ide_bench"]["pairs"])
            self.assertEqual(commands[2][:3], ["taskset", "-c", "2"])
            self.assertEqual([command[3] for command in commands[2:6]],
                             [str(root / "bin" / name) for name in ("ide-base", "ide-cand", "ide-cand", "ide-base")])
            self.assertEqual(record["status"], "failed")
            self.assertEqual(record["exits"], {"frontend_scaling": 2, "selection": 2})
            self.assertTrue(any("frontend scaling exited 2" in error for error in record["errors"]), record["errors"])
            self.assertTrue(any("ide bench run 0" in error for error in record["errors"]), record["errors"])
            self.assertTrue((root / "evidence" / "frontend_selector" / "ide_bench" / "ide-bench.json").is_file())

def frontend_nesting_evidence() -> tuple[dict, dict]:
    """A complete synthetic frontend-nesting-v1 record and its exported bundle on BINARIES."""
    profile = compiler_receipt.FRONTEND_NESTING_PROFILE
    scaling = profile["frontend_scaling"]
    hashes = {"baseline": A256, "candidate": B256}
    provenance = {"binary_sha256": dict(hashes), "suite": "nesting", "warmups": scaling["warmups"],
                  "repetitions": scaling["repetitions"], "cpu": profile["cpu"], "affinity": [profile["cpu"]],
                  "cpu_model": ZEN5, "complete": True}
    results = []
    for kind, count, mode in compiler_receipt.frontend_scaling_population(compiler_receipt.FRONTEND_NESTING_CASES):
        row = {"kind": kind, "count": count, "mode": mode, "source_bytes": 10, "source_sha256": "5" * 64,
               "compilers": {role: {"command": [role], "wall_seconds": [0.01] * scaling["repetitions"],
                                    "cpu_seconds": [0.0] * scaling["repetitions"], "wall_median": 0.01, "cpu_median": 0.0}
                             for role in hashes}}
        if mode == "object":
            row["object_sha256"] = "6" * 64
        results.append(row)
    bundle = {"provenance": provenance, "results": results}
    record = {"requested": True, "request_line": compiler_receipt.FRONTEND_NESTING_REQUEST_LINE,
              "profile": copy.deepcopy(profile), "status": "complete", "errors": [], "exit": 0,
              "cells": compiler_receipt.frontend_scaling_digest(results)}
    return record, bundle


class FrontendNestingTest(unittest.TestCase):
    """#2676: the fixed frontend-nesting-v1 deep-nesting families on the pull-compare route."""

    def validate(self, bundle: dict, status: object = 0, binaries: dict = BINARIES) -> list[str]:
        return compiler_receipt.validate_frontend_nesting(status, bundle["provenance"], bundle["results"], binaries)

    def pull(self, record: dict) -> dict:
        value = receipt()
        value["mode"] = "pull"
        value["identity"].update(mode="pull")
        value["frontend_nesting"] = record
        return value

    def test_profile_is_frozen_report_only_and_matches_the_tool(self) -> None:
        profile = compiler_receipt.FRONTEND_NESTING_PROFILE
        self.assertEqual(profile["name"], "frontend-nesting-v1")
        self.assertEqual(compiler_receipt.FRONTEND_NESTING_REQUEST_LINE, "profile: " + profile["name"])
        self.assertIs(profile["report_only"], True)
        arguments = profile["frontend_scaling"]["arguments"]
        self.assertEqual(arguments[arguments.index("--cpu") + 1], str(profile["cpu"]))
        self.assertEqual(arguments[arguments.index("--suite") + 1], profile["frontend_scaling"]["suite"])
        self.assertEqual(len(compiler_receipt.frontend_scaling_population(compiler_receipt.FRONTEND_NESTING_CASES)), 68)
        # The frozen families and depths are the tool's, so a renamed family cannot silently drop a cell.
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        try:
            import benchmark_frontend_scaling as tool
        finally:
            sys.path.pop(0)
        self.assertEqual([name for name, unused_open, unused_close in tool.NESTING_FAMILIES],
                         list(compiler_receipt.FRONTEND_NESTING_FAMILIES))
        self.assertEqual(tool.NESTING_DEPTHS, compiler_receipt.FRONTEND_NESTING_DEPTHS)
        # Each depth four times the last: a quadratic term is a sixteenfold step.
        depths = compiler_receipt.FRONTEND_NESTING_DEPTHS
        self.assertTrue(all(later == 4 * earlier for earlier, later in zip(depths, depths[1:])))
        # Its bound plus the ordinary comparison stays inside the unchanged 90-minute compare-pull job.
        self.assertLessEqual(compiler_compare.FRONTEND_NESTING_TIMEOUT_SECONDS, 75 * 60)

    def test_generated_sources_nest_to_their_depth(self) -> None:
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        try:
            import benchmark_frontend_scaling as tool
        finally:
            sys.path.pop(0)
        for family in compiler_receipt.FRONTEND_NESTING_FAMILIES:
            with self.subTest(family=family):
                source = tool.nesting_source(family, 3)
                self.assertTrue(source.startswith("int f(int x) {\n") and source.endswith("return x; }\n"))
                self.assertEqual(source.count("{"), source.count("}"))
                self.assertEqual(source.count("("), source.count(")"))
                self.assertEqual(source.count("x -= 1;"), 1)
        self.assertEqual(tool.nesting_source("goto", 2),
                         "int f(int x) {\nfor (; x > 0;) { if (x == 0) goto L0; L0: ;\n"
                         "for (; x > 1;) { if (x == 1) goto L1; L1: ;\nx -= 1;\n}\n}\nreturn x; }\n")
        self.assertEqual(tool.nesting_source("braceless-do", 2),
                         "int f(int x) {\ndo\ndo\nx -= 1;\nwhile (x > 0);\nwhile (x > 0);\nreturn x; }\n")

    def test_population_matches_the_frontend_scaling_tool(self) -> None:
        """The real tool, on stand-in compilers, emits exactly the frozen population and a complete provenance."""
        root_dir = Path(__file__).resolve().parents[2]
        profile = compiler_receipt.FRONTEND_NESTING_PROFILE
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compilers = {}
            for role in ("baseline", "candidate"):
                compilers[role] = root / role
                compilers[role].write_text("#!/bin/sh\nwhile [ $# -gt 0 ]; do if [ \"$1\" = -o ]; then "
                                           "printf object > \"$2\"; fi; shift; done\n", encoding="utf-8")
                compilers[role].chmod(0o755)
            arguments = list(profile["frontend_scaling"]["arguments"])
            del arguments[arguments.index("--cpu"):arguments.index("--cpu") + 2]
            arguments[arguments.index("--repetitions") + 1] = "1"
            subprocess.run([sys.executable, "-B", str(root_dir / "tools/benchmark_frontend_scaling.py"),
                            "--baseline", str(compilers["baseline"]), "--candidate", str(compilers["candidate"]),
                            "--out", str(root / "out"), *arguments], check=True, capture_output=True)
            provenance = json.loads((root / "out/provenance.json").read_text(encoding="utf-8"))
            results = json.loads((root / "out/results.json").read_text(encoding="utf-8"))
        self.assertIs(provenance["complete"], True)
        self.assertEqual([(row["kind"], row["count"], row["mode"]) for row in results],
                         compiler_receipt.frontend_scaling_population(compiler_receipt.FRONTEND_NESTING_CASES))
        self.assertEqual(provenance["suite"], "nesting")

    def test_complete_evidence_validates_and_every_gap_fails(self) -> None:
        unused_record, bundle = frontend_nesting_evidence()
        self.assertEqual(self.validate(bundle), [])
        def change(key: str, mutate) -> dict:
            changed = copy.deepcopy(bundle)
            mutate(changed[key])
            return changed
        cases = {
            "failed run": (bundle, {"status": 1}),
            "cancelled run": (bundle, {"status": None}),
            "stale baseline": (bundle, {"binaries": {"baseline": {"sha256": "3" * 64}, "candidate": {"sha256": B256}}}),
            "no binaries": (bundle, {"binaries": {}}),
            "missing provenance": (dict(bundle, provenance=None), {}),
            "incomplete provenance": (change("provenance", lambda value: value.update(complete=False)), {}),
            "selector suite": (change("provenance", lambda value: value.update(suite="all")), {}),
            "fewer repetitions": (change("provenance", lambda value: value.update(repetitions=3)), {}),
            "unpinned": (change("provenance", lambda value: value.update(affinity=[0, 1, 2, 3])), {}),
            "not on Zen 5": (change("provenance", lambda value: value.update(cpu_model="Intel Xeon")), {}),
            "other binary": (change("provenance", lambda value: value["binary_sha256"].update(candidate=A256)), {}),
            "missing results": (dict(bundle, results=None), {}),
            "partial results": (change("results", lambda value: value.pop()), {}),
            "reordered results": (change("results", lambda value: value.reverse()), {}),
            "selector population": (dict(bundle, results=frontend_selector_evidence()[1]["results"]), {}),
            "short samples": (change("results", lambda value: value[3]["compilers"]["candidate"]["wall_seconds"].pop()), {}),
            "zero wall": (change("results", lambda value: value[0]["compilers"]["baseline"]["wall_seconds"].__setitem__(0, 0)), {}),
            "object without hash": (change("results", lambda value: value[1].pop("object_sha256")), {}),
            "stated median": (change("results", lambda value: value[2]["compilers"]["candidate"].update(wall_median=5.0)), {}),
        }
        for name, (changed, options) in cases.items():
            with self.subTest(case=name):
                self.assertTrue(self.validate(changed, **options), name)

    def test_any_wrongly_typed_field_fails_closed_without_raising(self) -> None:
        unused_record, bundle = frontend_nesting_evidence()
        replacements = (None, [], {}, "x", -1, 1.5, True, 10 ** 309, float("nan"), float("inf"))
        for key, path in (("provenance", ("cpu",)), ("provenance", ("binary_sha256",)), ("results", (0, "count")),
                          ("results", (0, "compilers")), ("results", (1, "object_sha256")),
                          ("results", (2, "compilers", "baseline", "wall_seconds"))):
            for replacement in replacements:
                changed = copy.deepcopy(bundle)
                holder = changed[key]
                for step in path[:-1]:
                    holder = holder[step]
                holder[path[-1]] = replacement
                with self.subTest(key=key, path=path, replacement=repr(replacement)):
                    compiler_receipt.frontend_scaling_digest(changed["results"])
                    self.assertTrue(self.validate(changed))

    def test_decision_needs_pull_mode_and_the_complete_bundle(self) -> None:
        record, bundle = frontend_nesting_evidence()
        pull = self.pull(record)
        values = {"expected": dict(EXPECTED, mode="pull"), "authorized": True, "compare_result": "success",
                  "receipt": pull, "summary": summary(), "policy_value": "",
                  "throughput": dict(corpus(), frontend_nesting_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        stale = copy.deepcopy(record)
        stale["cells"][0]["values"] = [1.0, 0.1]
        widened = copy.deepcopy(record)
        widened["profile"]["frontend_scaling"]["depths"] = [1000, 4000, 16000]
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "partial bundle": {"throughput": dict(corpus(), frontend_nesting_bundle={"results": bundle["results"]})},
            "failed run": {"receipt": dict(pull, frontend_nesting=dict(record, status="failed", exit=1))},
            "cancelled run": {"receipt": dict(pull, frontend_nesting=dict(record, status="pending", exit=None))},
            "stale cells": {"receipt": dict(pull, frontend_nesting=stale)},
            "changed profile": {"receipt": dict(pull, frontend_nesting=widened)},
            "not a request": {"receipt": dict(pull, frontend_nesting=dict(record, requested=False))},
            "other selector": {"receipt": dict(pull, frontend_nesting=dict(
                record, request_line="profile: frontend-nesting-v2"))},
            "with frontend-selector probes": {"receipt": dict(pull, frontend_selector=frontend_selector_evidence()[0]),
                                              "throughput": dict(corpus(), frontend_nesting_bundle=bundle,
                                                                 frontend_selector_bundle=frontend_selector_evidence()[1])},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("Deep-nesting families (frontend-nesting-v1): complete", report)
        self.assertIn("| frontend goto-32000/object | 10.0 ms | 10.0 ms | 1.0000 |", report)

    def test_read_evidence_requires_every_bundle_member(self) -> None:
        record, bundle = frontend_nesting_evidence()
        files = compiler_publish.FRONTEND_NESTING_BUNDLE_FILES
        complete = {"receipt.json": json.dumps(self.pull(record)), "lab/summary.json": json.dumps(summary()),
                    "throughput/summary.json": json.dumps(corpus()["summary"]),
                    "throughput/metadata.json": json.dumps(corpus()["metadata"]),
                    **{path: json.dumps(bundle[key]) for key, path in files.items()}}
        got = compiler_publish.read_evidence(FakeApi(archive(complete)), "92", "buster-9700x-compiler-x-1")
        self.assertEqual((got[2], got[4]["frontend_nesting_bundle"]), ("", bundle))
        self.assertEqual(compiler_publish.decide(dict(EXPECTED, mode="pull"), True, "success", got[0], got[1], "",
                                                 got[4])[0], "success")
        for key, path in files.items():
            for name, members in (("missing", {member: value for member, value in complete.items() if member != path}),
                                  ("malformed", dict(complete, **{path: "{not json"})),
                                  ("duplicate key", dict(complete, **{path: '{"a": 1, "a": 2}'}))):
                with self.subTest(member=key, case=name):
                    got = compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
                    self.assertIn(path, got[2])
                    self.assertIsNone(got[4]["frontend_nesting_bundle"])

    def test_inventory_coverage_names_the_nesting_row(self) -> None:
        rows = ["tool:benchmark_frontend_scaling.py@frontend-nesting"]
        self.assertEqual(list(compiler_receipt.INVENTORY_LEGS["pull"]["frontend-nesting"]), rows)
        pull = self.pull(frontend_nesting_evidence()[0])
        def leg(receipt_value: dict, conclusion: str) -> dict:
            coverage = compiler_receipt.covered_inventory_rows(receipt_value, conclusion, "pull")
            return next(entry for entry in coverage["legs"] if entry["leg"] == "frontend-nesting")
        self.assertEqual(leg(pull, "success")["status"], "discharged")
        self.assertTrue(set(rows) <= set(compiler_receipt.covered_inventory_rows(pull, "success", "pull")["discharged"]))
        failed = dict(pull, frontend_nesting=dict(pull["frontend_nesting"], status="failed"))
        self.assertEqual(leg(failed, "failure")["status"], "not discharged: leg failed")
        plain = dict(pull)
        plain.pop("frontend_nesting")
        self.assertEqual(leg(plain, "success")["status"], "not discharged: not requested")

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            fixture_git(repo, "init", "-q")
            fixture_git(repo, "config", "user.name", "Frontend Nesting Test")
            fixture_git(repo, "config", "user.email", "frontend-nesting@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            fixture_git(repo, "add", compiler_receipt.ANALYZER_REQUEST_PATH)
            fixture_git(repo, "commit", "-qm", "seed")
            line = compiler_receipt.FRONTEND_NESTING_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (compiler_receipt.FRONTEND_SELECTOR_REQUEST_LINE + "\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                fixture_git(repo, "commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.frontend_nesting_request_selection(
                        repo, fixture_git(repo, "rev-parse", "HEAD")), expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            fixture_git(repo, "commit", "-qam", "duplicate")
            requested, problem = compiler_compare.frontend_nesting_request_selection(
                repo, fixture_git(repo, "rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.FRONTEND_NESTING_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = self.pull(frontend_nesting_evidence()[0])
        expected = {"mode": "pull", "head": head}
        verify = compiler_publish.verify_analyzer_request_freshness
        self.assertEqual(verify(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", verify(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", verify(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("frontend_nesting")
        self.assertIn("no frontend-nesting record", verify(Api(1, 0), expected, plain, {}))
        self.assertEqual(verify(Api(1, 1), expected, plain, {}), "")
        self.assertEqual(verify(Api(1, 0), expected, dict(plain, state="superseded"), {}), "")

    def test_compare_leg_records_tool_failures_and_missing_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("work", "evidence", "bin", "candidate"):
                (root / name).mkdir()
            for name, content in (("ide-base", b"base"), ("ide-cand", b"cand")):
                (root / "bin" / name).write_bytes(content)
            receipt_value = {"timings": {}, "binaries": BINARIES,
                             "frontend_nesting": {"requested": True, "status": "pending"}}
            with mock.patch.object(compiler_compare, "run", side_effect=lambda argv, cwd, log, timeout: 2) as run:
                compiler_compare.measure_frontend_nesting(root / "candidate", root / "bin", root / "work",
                                                          root / "evidence", receipt_value)
            commands = [call.args[0] for call in run.call_args_list]
            self.assertEqual(len(commands), 1)
            self.assertEqual(commands[0][2], "tools/benchmark_frontend_scaling.py")
            self.assertEqual(commands[0][-len(compiler_receipt.FRONTEND_NESTING_PROFILE["frontend_scaling"]["arguments"]):],
                             compiler_receipt.FRONTEND_NESTING_PROFILE["frontend_scaling"]["arguments"])
            record = receipt_value["frontend_nesting"]
            self.assertEqual((record["status"], record["exit"]), ("failed", 2))
            self.assertTrue(any("frontend scaling exited 2" in error for error in record["errors"]), record["errors"])
            self.assertTrue(any("produced no output" in error for error in record["errors"]), record["errors"])
            self.assertEqual(record["cells"], [])


WILD_PATH = "/usr/local/bin/wild"
# Wild's own identity format (Args::linker_identity at wild e9a9b0cb): the positive control, in the
# form the 9700X host's distribution Wild reports (run 38242826849).
WILD_VERSION = "Wild 0.10.0 non-git-build (compatible with GNU linkers)"
WILD_ROOT = "/work/wild-linker-source"


def wild_linker_evidence(head: str = "a" * 40, kind: str = "mold") -> tuple[dict, dict]:
    """A complete synthetic wild-linker-v1 record and its exported bundle, laid out as the harness writes them."""
    profile = compiler_receipt.WILD_LINKER_PROFILE
    frozen = compiler_receipt.WILD_LINKER_COMPARATORS[kind]
    comparator_path = "/usr/bin/" + frozen["command"]
    linkers = {"wild": {"command": "wild", "path": WILD_PATH, "realpath": WILD_PATH, "version": WILD_VERSION,
                        "sha256": "7" * 64, "sha256_after": "7" * 64, "kind": "wild",
                        "policy": profile["wild"]["policy"], "threads1": profile["wild"]["threads1"]},
               "comparator": {"command": frozen["command"], "path": comparator_path, "realpath": comparator_path,
                              "version": frozen["version"] + "2.42.1", "sha256": "8" * 64, "sha256_after": "8" * 64,
                              "kind": kind, "policy": frozen["policy"], "threads1": frozen["threads1"],
                              "substituted": kind != "mold"}}
    raw = ["sequence,cell,variant,round,exit,wall_ms,user_ms,system_ms,maxrss_kib,minor_faults,major_faults"]
    commands, summary = [], ["cell,n,median_A_ms,median_B_ms,mad_A_ms,mad_B_ms,median_paired_B_over_A,ci95_low,ci95_high"]
    sequence = 0

    def row(cell: str, variant: str, round_: int, command: str, exit_status: int = 0, wall: float = 1.0) -> None:
        nonlocal sequence
        raw.append(f"{sequence},{cell},{variant},{round_},{exit_status},{wall:.6f},1.000000,1.000000,100,10,0")
        commands.append(f"{sequence}\t{cell}\t{variant}\t{round_}\t{command}")
        sequence += 1

    for cell, variant, exit_status in (("self-test-exit", "zero", 0), ("self-test-exit", "seven", 7),
                                       ("self-test-quoting", "literal", 0)):
        row(cell, variant, -1, "exit 0", exit_status)
    orders, states = compiler_receipt.wild_linker_schedule()
    common = f"--no-gc-sections --build-id={compiler_receipt.WILD_LINKER_BUILD_ID}"
    manifests, argvs = {}, {}
    for cell in profile["cells"]:
        tree, config = profile["cell_trees"][cell]
        directory = f"{WILD_ROOT}/{tree}"
        objects = [f"CMakeFiles/ide.dir/{config}/src/{name}.c.o" for name in ("ide", "os", "arena")]
        argvs[cell] = (f'"{WILD_PATH}" "-z" "relro" "-pie" "-o" "{config}/ide" '
                       f'"--dependency-file=CMakeFiles/ide.dir/{config}/link.d" "/lib/x86_64-linux-gnu/Scrt1.o" ' +
                       " ".join(f'"{name}"' for name in objects) + ' "-lm" "/lib/x86_64-linux-gnu/crtn.o"')
        manifests[cell] = "".join(f"{'3' * 64}  {directory}/{name}\n" for name in objects)
        wild = f"cd '{directory}' && {argvs[cell]} {common} {profile['wild']['policy']}"
        reference = f"cd '{directory}' && '{comparator_path}'{argvs[cell][len(WILD_PATH) + 2:]} {common} {frozen['policy']}"
        for name in ("capture-link-command", "capture-direct-linker", "freeze-object-manifest"):
            row(name, "setup", -1, "true")
        comparisons = [(f"{cell}-default", reference, wild, profile["pairs"]),
                       (f"{cell}-threads1", f"{reference} {frozen['threads1']}", f"{wild} {profile['wild']['threads1']}",
                        profile["pairs"])]
        if cell == "ide-debug":
            comparisons += [("ide-debug-aa-comparator", reference, reference, profile["control_pairs"]),
                            ("ide-debug-aa-wild", wild, wild, profile["control_pairs"])]
        for name, first, second, pairs in comparisons:
            walls = {"A": [], "B": []}
            for index, lead in enumerate(orders[name]):
                round_ = index - profile["warmups"]
                for variant in ("A", "B") if lead == 0 else ("B", "A"):
                    wall = (20.0 if variant == "A" else 15.0) + (index % 5) * 0.25
                    row(name, variant, round_, first if variant == "A" else second, wall=wall)
                    if round_ >= 0:
                        walls[variant].append(wall)
            ratios = [b / a for a, b in zip(walls["A"], walls["B"])]
            low, high = compiler_receipt._paired_interval(ratios, states[name])
            summary.append("%s,%d,%.6f,%.6f,0.000000,0.000000,%.6f,%.6f,%.6f" % (
                name, pairs, sorted(walls["A"])[pairs // 2], sorted(walls["B"])[pairs // 2], sorted(ratios)[pairs // 2],
                low, high))
        for command in (reference, wild):
            for name in ("validated-link", "output-identity", "linked-compiler-smoke", "same-linker-repeat",
                         "same-linker-byte-identity"):
                row(name, "setup", -1, command)
        row("unchanged-object-manifest", "setup", -1, "sha256sum --quiet --check")
    identity = {"schema": compiler_receipt.WILD_LINKER_SCHEMA, "profile": profile["name"], "head": head, "cpu_model": ZEN5,
                "cpus": list(profile["cpus"]), "linkers": linkers,
                "harness": {"wild_linker_acceptance.c": "4" * 64, "wild_linker_bench.c": "5" * 64},
                "inputs": {cell: hashlib.sha256(text.encode()).hexdigest() for cell, text in manifests.items()},
                "build_root": WILD_ROOT, "exit": 0}
    bundle = {"identity": json.dumps(identity), "raw": "\n".join(raw) + "\n", "summary": "\n".join(summary) + "\n",
              "commands": "\n".join(commands) + "\n",
              "affinity": "affinity=%s\n" % ",".join(str(cpu) for cpu in profile["cpus"]), "completion": "link_only=PASS\n"}
    for cell in profile["cells"]:
        bundle[f"inputs {cell}"] = manifests[cell]
        bundle[f"argv {cell}"] = argvs[cell]
        for linker in compiler_receipt.WILD_LINKER_LINKERS:
            bundle[f"output {cell} {linker}"] = ("9" if linker == "wild" else "6") * 64 + "\n"
            bundle[f"comment {cell} {linker}"] = "clang version 21.1.8\n" + (
                "Linker: " + WILD_VERSION + "\n" if linker == "wild" else
                "mold 2.42.1 (compatible with GNU ld)\n" if kind == "mold" else "")
    record = {"requested": True, "request_line": compiler_receipt.WILD_LINKER_REQUEST_LINE,
              "profile": copy.deepcopy(profile), "status": "complete", "errors": [], "build_exit": 0, "exit": 0,
              "linkers": copy.deepcopy(linkers), "identity": copy.deepcopy(identity),
              "cells": compiler_receipt.wild_linker_digest(bundle["raw"], bundle["summary"])}
    return record, bundle


class WildLinkerTest(unittest.TestCase):
    """#2761: the fixed wild-linker-v1 leg times the host's installed Wild on the pull-compare route."""

    def validate(self, bundle: dict, status: object = 0, head: str = "a" * 40, identity: object = None) -> list[str]:
        return compiler_receipt.validate_wild_linker(status, bundle, head, identity)

    def test_profile_is_frozen_report_only_and_fits_the_job(self) -> None:
        profile = compiler_receipt.WILD_LINKER_PROFILE
        self.assertEqual(profile["name"], "wild-linker-v1")
        self.assertEqual(compiler_receipt.WILD_LINKER_REQUEST_LINE, "profile: " + profile["name"])
        self.assertIs(profile["report_only"], True)
        self.assertEqual(profile["cells"], ["ide-debug", "ide-unity-release", "ide-separate-release"])
        # The audit's link policy: no LTO, Wild never forks or folds code, one fixed benchmark build ID.
        self.assertEqual({key: profile["wild"][key] for key in ("command", "policy", "threads1")},
                         {"command": "wild", "policy": "--no-fork --icf=none", "threads1": "--threads=1"})
        self.assertTrue(compiler_receipt.WILD_IDENTITY.fullmatch(WILD_VERSION))
        self.assertEqual(sorted(profile["cell_trees"]), sorted(profile["cells"]))
        self.assertEqual(compiler_receipt.WILD_LINKER_COMPARATORS["mold"]["policy"], profile["wild"]["policy"])
        for arguments in profile["trees"].values():
            self.assertIn("--no-lto", arguments)
            self.assertEqual(arguments[arguments.index("--linker") + 1], "DEFAULT")
        self.assertEqual(len(compiler_receipt.wild_linker_population()), 8)
        self.assertEqual(json.loads(json.dumps(profile)), profile)
        # Both bounds together stay inside the unchanged 90-minute compare-pull job with its ordinary comparison.
        self.assertLessEqual(compiler_compare.WILD_LINKER_BUILD_TIMEOUT_SECONDS +
                             compiler_compare.WILD_LINKER_HARNESS_TIMEOUT_SECONDS, 40 * 60)

    def test_identity_accepts_every_version_form_wild_builds(self) -> None:
        # wild/build.rs at e9a9b0cb: release tag, merge-base hash, modified checkout, no git. The
        # 9700X host's distribution Wild reports the last form (run 38242826849).
        merge_base = "e9a9b0cb6fd63a23cc7b93aafc931aadd5746be6"
        for version in ("0.10.0", merge_base, f"0.10.0 {merge_base}-modified", "0.10.0 non-git-build"):
            with self.subTest(version=version):
                self.assertTrue(compiler_receipt.WILD_IDENTITY.fullmatch(
                    f"Wild {version} (compatible with GNU linkers)"))
        for line in ("Ubuntu LLD 18.1.3 (compatible with GNU linkers)", "Wild (compatible with GNU linkers)",
                     "Wild 0.10.0 dirty (compatible with GNU linkers)", "Wild 0.10.0 non-git-build",
                     f"Wild {merge_base[:12]} (compatible with GNU linkers)",
                     "Wild 0.10.0 non-git-build (compatible with GNU linkers)\nmold 2.42.1"):
            with self.subTest(line=line):
                self.assertIsNone(compiler_receipt.WILD_IDENTITY.fullmatch(line))

    def test_schedule_replays_the_harness_generator(self) -> None:
        """The seeded xorshift order and bootstrap match the C harness's random_u32 (seed 2645)."""
        state, draws = 2645, []
        for _ in range(3):
            state = compiler_receipt._xorshift(state)
            draws.append(state)
        self.assertEqual(draws, [673182960, 112355760, 990757426])
        orders, states = compiler_receipt.wild_linker_schedule()
        self.assertEqual([len(orders[cell]) for cell, pairs, _, _ in compiler_receipt.wild_linker_population()],
                         [pairs + 2 for _, pairs, _, _ in compiler_receipt.wild_linker_population()])
        self.assertEqual(len(set(states.values())), len(states))

    def test_complete_evidence_validates_and_every_gap_fails(self) -> None:
        for kind in compiler_receipt.WILD_LINKER_COMPARATORS:
            with self.subTest(comparator=kind):
                record, bundle = wild_linker_evidence(kind=kind)
                self.assertEqual(self.validate(bundle, identity=record["identity"]), [])
        record, bundle = wild_linker_evidence()

        def identity(mutate) -> dict:
            changed = json.loads(bundle["identity"])
            mutate(changed)
            return dict(bundle, identity=json.dumps(changed))

        def text(key: str, mutate) -> dict:
            return dict(bundle, **{key: mutate(bundle[key])})

        def raw_line(match: str, mutate) -> dict:
            lines = bundle["raw"].splitlines()
            index = next(index for index, line in enumerate(lines) if match in line)
            lines[index] = mutate(lines[index])
            return dict(bundle, raw="\n".join(lines) + "\n")

        def manifest(cell: str, mutate) -> dict:
            """A changed manifest whose hash the identity also records, so only the binding predicates refuse it."""
            changed = mutate(bundle[f"inputs {cell}"])
            record = json.loads(bundle["identity"])
            record["inputs"][cell] = hashlib.sha256(changed.encode()).hexdigest()
            return dict(bundle, identity=json.dumps(record), **{f"inputs {cell}": changed})

        def without(key: str) -> dict:
            return {name: value for name, value in bundle.items() if name != key}

        def swap_first_round(raw: str) -> str:
            lines = raw.splitlines()
            index = next(index for index, line in enumerate(lines) if ",ide-debug-default," in line)
            first, second = lines[index].split(","), lines[index + 1].split(",")
            first[2], second[2] = second[2], first[2]
            lines[index:index + 2] = [",".join(first), ",".join(second)]
            return "\n".join(lines) + "\n"

        def summary_field(cell: str, column: int, value: str):
            def mutate(summary: str) -> str:
                lines = summary.splitlines()
                for index, line in enumerate(lines):
                    fields = line.split(",")
                    if fields[0] == cell:
                        fields[column] = value
                        lines[index] = ",".join(fields)
                return "\n".join(lines) + "\n"
            return mutate

        def linker(role: str, **values):
            return identity(lambda value: value["linkers"][role].update(values))

        cases = {
            "failed harness": (bundle, {"status": 1}),
            "cancelled harness": (bundle, {"status": None}),
            "timed-out harness": (bundle, {"status": 124}),
            "missing raw": (without("raw"), {}),
            "missing summary": (without("summary"), {}),
            "missing completion": (without("completion"), {}),
            "missing output": (without("output ide-debug wild"), {}),
            "missing manifest": (without("inputs ide-unity-release"), {}),
            "malformed identity": (dict(bundle, identity="{"), {}),
            "duplicate identity key": (dict(bundle, identity=bundle["identity"][:-1] + ', "exit": 0}'), {}),
            "not Zen 5": (identity(lambda value: value.update(cpu_model="AMD EPYC 9V74 96-Core Processor")), {}),
            "other head": (identity(lambda value: value.update(head="f" * 40)), {}),
            "other CPUs": (identity(lambda value: value.update(cpus=[2])), {}),
            "nonzero recorded exit": (identity(lambda value: value.update(exit=1)), {}),
            "Wild replaced during the leg": (linker("wild", sha256_after="0" * 64), {}),
            "Wild unhashed": (linker("wild", sha256=None, sha256_after=None), {}),
            "Wild without version": (linker("wild", version=""), {}),
            "relative Wild path": (linker("wild", path="wild"), {}),
            "Wild under another name": (linker("wild", command="ld.wild"), {}),
            "Wild without no-fork": (linker("wild", policy="--icf=none"), {}),
            "unsupported comparator": (linker("comparator", kind="lld", command="ld.lld"), {}),
            "comparator not mold": (linker("comparator", version="GNU ld 2.42"), {}),
            "unrecorded substitution": (linker("comparator", substituted=True), {}),
            "comparator without its policy": (linker("comparator", policy=""), {}),
            "same executable": (linker("comparator", sha256="7" * 64, sha256_after="7" * 64), {}),
            "unidentified harness": (identity(lambda value: value["harness"].pop("wild_linker_bench.c")), {}),
            "other input manifest": (text("inputs ide-debug", lambda value: value.replace("3" * 64, "4" * 64)), {}),
            "empty input manifest": (dict(bundle, **{"inputs ide-separate-release": ""}), {}),
            "unhashed output": (dict(bundle, **{"output ide-unity-release comparator": "NA\n"}), {}),
            "unpinned": (dict(bundle, affinity="affinity=0,1,2,3,4,5,6,7\n"), {}),
            "incomplete pass": (dict(bundle, completion=""), {}),
            "partial population": (text("raw", lambda value: "\n".join(value.splitlines()[:-60]) + "\n"), {}),
            "failed link": (raw_line(",ide-unity-release-threads1,B,3,", lambda line: line.replace(",B,3,0,", ",B,3,1,")), {}),
            "zero wall": (raw_line(",ide-separate-release-default,A,0,", lambda line: ",".join(
                [*line.split(",")[:5], "0.000000", *line.split(",")[6:]])), {}),
            "unseeded order": (text("raw", swap_first_round), {}),
            "edited median": (text("summary", summary_field("ide-debug-default", 3, "1.000000")), {}),
            "edited interval": (text("summary", summary_field("ide-unity-release-default", 8, "2.000000")), {}),
            "missing summary cell": (text("summary", lambda value: "\n".join(value.splitlines()[:-1]) + "\n"), {}),
            "failed byte identity": (raw_line(",same-linker-byte-identity,", lambda line: line.replace(",-1,0,", ",-1,1,")), {}),
            "changed object manifest": (raw_line(",unchanged-object-manifest,", lambda line: line.replace(",-1,0,", ",-1,1,")), {}),
            "other linker in a slot": (text("commands", lambda value: value.replace(f'"{WILD_PATH}"', '"/tmp/wild"', 1)), {}),
            "other build ID": (text("commands", lambda value: value.replace("0x2645", "0x1111", 1)), {}),
            "one-thread option dropped": (text("commands", lambda value: value.replace(" --threads=1", "", 1)), {}),
            "receipt identity differs": (bundle, {"identity": dict(record["identity"], cpus=[2, 3])}),
            # #3689 review: an impostor installed as `wild` is refused by its version and by its output.
            "impostor Wild version": (linker("wild", version="Ubuntu LLD 18.1.3 (compatible with GNU linkers)"), {}),
            "Wild prefix only": (linker("wild", version="Wild version 0.10.0"), {}),
            "Wild output without identity": (dict(bundle, **{"comment ide-unity-release wild": "Linker: LLD 18\n"}), {}),
            "Wild output with another version": (dict(bundle, **{"comment ide-debug wild": (
                "Linker: Wild 0.9.0 (compatible with GNU linkers)\n")}), {}),
            "Wild identity without Wild's prefix": (dict(bundle, **{"comment ide-debug wild": WILD_VERSION + "\n"}), {}),
            "comparator output is Wild": (dict(bundle, **{"comment ide-debug comparator": "Linker: " + WILD_VERSION + "\n"}),
                                          {}),
            "comparator output is bare Wild": (dict(bundle, **{"comment ide-debug comparator": WILD_VERSION + "\n"}), {}),
            "dependency file elsewhere": (text("argv ide-debug", lambda value: value.replace(
                "CMakeFiles/ide.dir/Debug/link.d", "/tmp/link.d")), {}),
            "dependency file of another config": (text("argv ide-debug", lambda value: value.replace(
                "CMakeFiles/ide.dir/Debug/link.d", "CMakeFiles/ide.dir/Release/link.d")), {}),
            "missing comment": (without("comment ide-separate-release wild"), {}),
            "no build root": (identity(lambda value: value.pop("build_root")), {}),
            # #3689 review: the timed argv is bound to the hashed inputs and nothing else can run.
            "missing argv": (without("argv ide-debug"), {}),
            "unhashed object": (text("argv ide-debug", lambda value: value.replace("/src/os.c.o", "/src/other.c.o")), {}),
            "object dropped from manifest": (manifest("ide-unity-release", lambda value: value.split("\n", 1)[1]), {}),
            "foreign absolute object": (text("argv ide-separate-release", lambda value: value.replace(
                '"/lib/x86_64-linux-gnu/crtn.o"', '"/tmp/payload.o"')), {}),
            "object under the build root": (text("argv ide-debug", lambda value: value.replace(
                '"/lib/x86_64-linux-gnu/crtn.o"', f'"{WILD_ROOT}/build-wild-unity/x.o"')), {}),
            "manifest outside its tree": (manifest("ide-debug", lambda value: value.replace(
                "build-wild-separate", "build-wild-unity", 1)), {}),
            "argv writes elsewhere": (text("argv ide-debug", lambda value: value.replace('"Debug/ide"', '"Debug/other"')), {}),
            "argv not Wild": (text("argv ide-debug", lambda value: value.replace(f'"{WILD_PATH}"', '"/usr/bin/ld.lld"')), {}),
            "injected command": (text("commands", lambda value: value.replace(" --icf=none", " --icf=none; touch /tmp/x", 1)), {}),
            "injected object": (text("commands", lambda value: value.replace(' "-lm"', ' "/tmp/payload.o" "-lm"', 1)), {}),
            "identity comment disabled": (text("commands", lambda value: value.replace(
                " --icf=none", " --icf=none --no-identity-comment")), {}),
            "validated link replaced": (text("commands", lambda value: value.replace(
                "\tvalidated-link\tsetup\t-1\tcd ", "\tvalidated-link\tsetup\t-1\ttrue; cd ", 1)), {}),
        }
        for name, (changed, options) in cases.items():
            with self.subTest(case=name):
                self.assertTrue(self.validate(changed, **options), name)

    def test_cells_are_rederived_from_the_samples(self) -> None:
        record, bundle = wild_linker_evidence()
        cells = {row["name"]: row for row in record["cells"]}
        self.assertEqual(sorted(cells), sorted(cell for cell, _, _, _ in compiler_receipt.wild_linker_population()))
        self.assertEqual(cells["ide-debug-default"]["linkers"], ["comparator", "wild"])
        self.assertEqual(cells["ide-debug-aa-wild"]["pairs"], 9)
        self.assertTrue(all(row["ratio"] < 1 for name, row in cells.items() if "-aa-" not in name))
        edited = bundle["summary"].replace(bundle["summary"].splitlines()[1].split(",")[2], "99.000000", 1)
        self.assertEqual(compiler_receipt.wild_linker_digest(bundle["raw"], edited), record["cells"])

    def publish(self, record: dict, bundle: dict, **extra) -> tuple:
        """ZIP -> read_evidence -> decide on a pull receipt carrying record, as the publisher runs it."""
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["wild_linker"] = record
        pull.update(extra)
        members = {"receipt.json": json.dumps(pull), "lab/summary.json": json.dumps(summary()),
                   "throughput/summary.json": json.dumps(corpus()["summary"]),
                   "throughput/metadata.json": json.dumps(corpus()["metadata"]),
                   **{path: bundle[key] for key, path in compiler_publish.WILD_LINKER_BUNDLE_FILES.items() if key in bundle}}
        read, lab, problem, unused_artifact, throughput = compiler_publish.read_evidence(
            FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
        if problem:
            return "failure", "", [problem]
        return compiler_publish.decide(dict(EXPECTED, mode="pull"), True, "success", read, lab, "", throughput)

    def test_forged_argv_with_matching_commands_fails_publication(self) -> None:
        """#3689 review: a packet whose retained argv and every derived command row are rewritten in lockstep."""
        record, bundle = wild_linker_evidence()
        self.assertEqual(self.publish(record, bundle)[0], "success")
        tail = bundle["argv ide-debug"][len(WILD_PATH) + 2:]
        for name, suffix in (("appended shell syntax", " ; :"), ("command substitution", ' "$(touch /tmp/x)"'),
                             ("quoted separator", ' "; :"'), ("unquoted extra word", " -lm"),
                             ("backquoted word", ' "`id`"'), ("redirection", ' > "/tmp/x"')):
            with self.subTest(case=name):
                changed = dict(bundle, **{"argv ide-debug": bundle["argv ide-debug"] + suffix,
                                          "commands": bundle["commands"].replace(tail, tail + suffix)})
                self.assertIn(tail + suffix, changed["commands"])
                problems = self.validate(changed, identity=record["identity"])
                self.assertTrue(any("shell-inert" in problem for problem in problems), problems)
                conclusion, unused_title, reasons = self.publish(record, changed)
                self.assertEqual(conclusion, "failure", reasons)
        # A clean extra argument in Clang's own form is admitted, and the commands are rebuilt from tokens.
        plain = ' "-lm"'
        changed = dict(bundle, **{"argv ide-debug": bundle["argv ide-debug"] + plain,
                                  "commands": bundle["commands"].replace(tail, tail + plain)})
        self.assertEqual(self.validate(changed, identity=record["identity"]), [])

    def test_argv_grammar_refuses_unbound_linker_inputs(self) -> None:
        """#3689 review: inputs Wild would read beyond the hashed manifest, forged into the argv and every
        derived command row in lockstep, fail publication; Clang's own flags stay admitted."""
        record, bundle = wild_linker_evidence()
        tail = bundle["argv ide-debug"][len(WILD_PATH) + 2:]

        def forged(suffix: str) -> dict:
            return dict(bundle, **{"argv ide-debug": bundle["argv ide-debug"] + suffix,
                                   "commands": bundle["commands"].replace(tail, tail + suffix)})

        for name, suffix, reason in (
                ("response file", ' "@/tmp/foreign.rsp"', "response file"),
                ("relative response file", ' "@inputs.rsp"', "response file"),
                ("linker script", ' "-T" "/tmp/x.ld"', "unadmitted option"),
                ("joined script", ' "--script=/tmp/x.ld"', "unadmitted option"),
                ("plugin", ' "--plugin" "/tmp/p.so"', "unadmitted option"),
                ("just symbols", ' "--just-symbols=/tmp/x"', "unadmitted option"),
                ("foreign search directory", ' "-L/tmp" "-lfoo"', "not a system directory"),
                ("escaping search directory", ' "-L/usr/../tmp"', "not a system directory"),
                ("foreign shared object", ' "/tmp/x.so"', "not an object or archive"),
                ("relative shared object", ' "x.so"', "not an object or archive"),
                ("library path as name", ' "-l/tmp/x"', "unadmitted option"),
                ("unadmitted -z keyword", ' "-z" "muldefs"', "unadmitted operand"),
                ("second output", ' "-o" "Debug/other"', "unadmitted operand"),
                ("escaping system object", ' "/usr/../tmp/x.o"', "neither a hashed input nor a system file")):
            with self.subTest(case=name):
                conclusion, unused_title, reasons = self.publish(record, forged(suffix))
                self.assertEqual(conclusion, "failure", reasons)
                self.assertTrue(any(reason in item for item in reasons), reasons)
        for name, suffix in (("as-needed libraries", ' "--as-needed" "-lm" "--no-as-needed"'),
                             ("system search directory", ' "-L/usr/lib/../lib64"'),
                             ("now", ' "-z" "now"')):
            with self.subTest(positive=name):
                self.assertEqual(self.publish(record, forged(suffix))[:3:2], ("success", []))

    def test_traversal_out_of_the_cell_tree_fails_publication(self) -> None:
        """#3689 review: an object operand and its manifest entry rewritten in lockstep to a path that leaves the
        cell's tree once normalised, with the manifest hash recomputed in the export and the receipt."""
        record, bundle = wild_linker_evidence()
        original = "CMakeFiles/ide.dir/Debug/src/ide.c.o"
        directory = f"{WILD_ROOT}/build-wild-separate"

        def forged(operand: str | None, extra: str = "") -> tuple[dict, dict]:
            changed = dict(bundle)
            manifest = bundle["inputs ide-debug"]
            if operand is not None:
                changed["argv ide-debug"] = bundle["argv ide-debug"].replace(f'"{original}"', f'"{operand}"')
                changed["commands"] = bundle["commands"].replace(f'"{original}"', f'"{operand}"')
                manifest = manifest.replace(f"{directory}/{original}", f"{directory}/{operand}")
            changed["inputs ide-debug"] = manifest + extra
            identity = json.loads(bundle["identity"])
            identity["inputs"]["ide-debug"] = hashlib.sha256(changed["inputs ide-debug"].encode()).hexdigest()
            changed["identity"] = json.dumps(identity)
            return dict(copy.deepcopy(record), identity=identity), changed

        self.assertEqual(self.publish(*forged(original))[0], "success")
        for name, operand, extra, reason in (
                ("reviewer's traversal", "../../tmp/foreign.o", "", "leaves build-wild-separate"),
                ("deep traversal", "CMakeFiles/ide.dir/../../../../tmp/x.o", "", "leaves build-wild-separate"),
                ("dot component", "./CMakeFiles/ide.dir/Debug/src/ide.c.o", "", "leaves build-wild-separate"),
                ("empty component", "CMakeFiles//ide.dir/Debug/src/ide.c.o", "", "leaves build-wild-separate"),
                ("manifest-only traversal", None, f"{'3' * 64}  {directory}/../../tmp/foreign.o\n",
                 "lists files outside build-wild-separate")):
            with self.subTest(case=name):
                conclusion, unused_title, reasons = self.publish(*forged(operand, extra))
                self.assertEqual(conclusion, "failure", reasons)
                self.assertTrue(any(reason in item for item in reasons), reasons)

    def test_decision_needs_pull_mode_and_the_complete_bundle(self) -> None:
        record, bundle = wild_linker_evidence()
        self.assertEqual(self.publish(record, bundle), ("success", self.publish(record, bundle)[1], []))
        substituted, substituted_bundle = wild_linker_evidence(kind="bfd")
        self.assertEqual(self.publish(substituted, substituted_bundle)[0], "success")
        pull = receipt()
        pull.update(mode="pull", wild_linker=record)
        pull["identity"].update(mode="pull")
        values = {"expected": dict(EXPECTED, mode="pull"), "authorized": True, "compare_result": "success",
                  "receipt": pull, "summary": summary(), "policy_value": "",
                  "throughput": dict(corpus(), wild_linker_bundle=bundle)}
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        stale = copy.deepcopy(record)
        stale["cells"][0]["values"] = [1.0, 0.1]
        widened = copy.deepcopy(record)
        widened["profile"]["pairs"] = 5
        relinked = copy.deepcopy(record)
        relinked["linkers"]["wild"]["path"] = "/opt/wild"
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "partial bundle": {"throughput": dict(corpus(), wild_linker_bundle={
                key: value for key, value in bundle.items() if key != "raw"})},
            "failed run": {"receipt": dict(pull, wild_linker=dict(record, status="failed", exit=2))},
            "failed input build": {"receipt": dict(pull, wild_linker=dict(record, build_exit=1))},
            "cancelled run": {"receipt": dict(pull, wild_linker=dict(record, status="pending", exit=None))},
            "stale cells": {"receipt": dict(pull, wild_linker=stale)},
            "changed profile": {"receipt": dict(pull, wild_linker=widened)},
            "linkers differ from identity": {"receipt": dict(pull, wild_linker=relinked)},
            "not a request": {"receipt": dict(pull, wild_linker=dict(record, requested=False))},
            "other selector": {"receipt": dict(pull, wild_linker=dict(record, request_line="profile: wild-linker-v2"))},
            "with production profile": {"receipt": dict(pull, production_profile=production_profile_evidence()[0]),
                                        "throughput": dict(corpus(), wild_linker_bundle=bundle,
                                                           production_profile_bundle=production_profile_evidence()[1])},
            "with frontend-selector probes": {"receipt": dict(pull, frontend_selector=frontend_selector_evidence()[0]),
                                              "throughput": dict(corpus(), wild_linker_bundle=bundle,
                                                                 frontend_selector_bundle=frontend_selector_evidence()[1])},
            "with compiler probes": {"receipt": dict(pull, compiler_probes=compiler_probes_evidence()[0]),
                                     "throughput": dict(corpus(), wild_linker_bundle=bundle,
                                                        compiler_probes_bundle=compiler_probes_evidence()[1])},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        for name, member in (("missing member", "raw"), ("missing output", "output ide-debug wild")):
            with self.subTest(case=name):
                partial = {key: value for key, value in bundle.items() if key != member}
                self.assertEqual(self.publish(record, partial)[0], "failure")
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("Host Wild linker (wild-linker-v1): complete", report)
        self.assertIn(f"wild `{WILD_PATH}` ({WILD_VERSION}, `777777777777`)", report)
        self.assertRegex(report, r"\| ide-debug-default \| 21 \| 20\.\d{3} \| 15\.\d{3} \| 0\.\d{4} \| \[0\.\d{4}, 0\.\d{4}\] \|")
        self.assertNotIn("default ld", report)
        pull["wild_linker"] = substituted
        self.assertIn("mold is not installed on this host, so the comparator is its default ld",
                      compiler_receipt.render(pull, summary(), "success", []))

    def test_selector_needs_one_exact_fresh_line(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            fixture_git(repo, "init", "-q")
            fixture_git(repo, "config", "user.name", "Wild Linker Test")
            fixture_git(repo, "config", "user.email", "wild-linker@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            fixture_git(repo, "add", compiler_receipt.ANALYZER_REQUEST_PATH)
            fixture_git(repo, "commit", "-qm", "seed")
            line = compiler_receipt.WILD_LINKER_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                fixture_git(repo, "commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.wild_linker_request_selection(
                        repo, fixture_git(repo, "rev-parse", "HEAD")), expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            fixture_git(repo, "commit", "-qam", "duplicate")
            requested, problem = compiler_compare.wild_linker_request_selection(repo, fixture_git(repo, "rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.WILD_LINKER_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}
            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = receipt()
        pull.update(mode="pull", wild_linker=wild_linker_evidence()[0])
        expected = {"mode": "pull", "head": head}
        verify = compiler_publish.verify_analyzer_request_freshness
        self.assertEqual(verify(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", verify(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", verify(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("wild_linker")
        self.assertIn("no wild-linker record", verify(Api(1, 0), expected, plain, {}))
        self.assertEqual(verify(Api(1, 1), expected, plain, {}), "")

    def leg(self, root: Path, tools: dict, run_status: int = 1) -> tuple[dict, list]:
        """measure_wild_linker with only `tools` on PATH; run is mocked, so nothing builds or links."""
        directory = root / "path"
        directory.mkdir(exist_ok=True)
        for name, version in tools.items():
            (directory / name).write_text(f"#!/bin/sh\necho '{version}'\n", encoding="utf-8")
            (directory / name).chmod(0o755)
        for name in ("work", "evidence"):
            (root / name).mkdir(exist_ok=True)
        receipt_value = {"timings": {}, "wild_linker": {"requested": True, "status": "pending"}}
        with mock.patch.dict(os.environ, {"PATH": str(directory)}), \
                mock.patch.object(compiler_compare, "run", return_value=run_status) as run:
            compiler_compare.measure_wild_linker(root, root / "work", root / "evidence", "a" * 40, receipt_value)
        return receipt_value["wild_linker"], [call.args[0] for call in run.call_args_list]

    def test_leg_finds_installed_linkers_and_records_missing_ones(self) -> None:
        cases = {
            "no Wild": ({"mold": "mold 2.42.1"}, "the host has no installed wild"),
            "no comparator": ({"wild": WILD_VERSION}, "comparator bfd: the host has no installed ld"),
            "mold that is not mold": ({"wild": WILD_VERSION, "mold": "LLD 18", "ld": "GNU ld 2.42"},
                                      "not a mold version"),
            "silent Wild": ({"wild": ""}, "did not report a version"),
            "impostor Wild": ({"wild": "Ubuntu LLD 18.1.3 (compatible with GNU linkers)", "mold": "mold 2.42.1"},
                              "not Wild's own identity"),
        }
        for name, (tools, reason) in cases.items():
            with self.subTest(case=name), tempfile.TemporaryDirectory() as temporary:
                record, commands = self.leg(Path(temporary), tools)
                self.assertEqual((record["status"], commands), ("failed", []))
                self.assertTrue(any(reason in error for error in record["errors"]), record["errors"])
        with tempfile.TemporaryDirectory() as temporary:
            record, commands = self.leg(Path(temporary), {"wild": WILD_VERSION, "mold": "mold 2.42.1",
                                                          "ld": "GNU ld 2.42"})
            self.assertEqual(record["linkers"]["comparator"]["kind"], "mold")
            self.assertIs(record["linkers"]["comparator"]["substituted"], False)
            self.assertEqual(record["linkers"]["wild"]["sha256"],
                             hashlib.sha256(f"#!/bin/sh\necho '{WILD_VERSION}'\n".encode()).hexdigest())
            # The input build failed, so nothing was timed; the failure is recorded.
            self.assertEqual((record["status"], record["build_exit"], len(commands)), ("failed", 1, 1))
            self.assertEqual(commands[0][:4], ["git", "-c", "core.hooksPath=/dev/null", "clone"])
        with tempfile.TemporaryDirectory() as temporary:
            record, commands = self.leg(Path(temporary), {"wild": WILD_VERSION, "ld": "GNU ld 2.42"}, 0)
            self.assertEqual((record["linkers"]["comparator"]["kind"], record["linkers"]["comparator"]["substituted"]),
                             ("bfd", True))
            generates = [command for command in commands if command[:2] == ["./build.sh", "generate"]]
            self.assertEqual([command[3] for command in generates], ["build-wild-unity", "build-wild-separate"])
            harness = commands[-1]
            self.assertEqual(harness[:3], ["taskset", "-c", "2,3,4,5"])
            self.assertEqual(harness[4:5] + harness[6:], ["--link-only", str(Path(temporary) / "work/wild-linker-source"),
                                                         str(Path(temporary) / "path/wild"), str(Path(temporary) / "path/ld"),
                                                         "", ""])
            # The mocked harness wrote nothing, so publication evidence is refused.
            self.assertEqual(record["status"], "failed")
            self.assertTrue(any("wild-linker evidence lacks" in error for error in record["errors"]), record["errors"])


class OutOfRangeEvidenceNumberTest(unittest.TestCase):
    """#3675: a JSON integer past the float range fails the check instead of raising OverflowError."""

    HUGE = 10 ** 309

    def inline_summary(self, **wall: object) -> dict:
        stage = {"metrics": {
            "wall": {"a_median": 1.0, "b_median": 1.0, "ratio": 1.0, **wall},
            "instructions": {"a_median": 10.0, "b_median": 10.0, "ratio": 1.0},
            "counter_availability": {"perf_stat": True, "reason": "usable"},
            "peak_rss": {"ratio": 1.0},
            "code_bytes": {"a_value": 100, "b_value": 100, "ratio": 1.0}}}
        return {"schema": compiler_receipt.INLINE_ACCEPTANCE_SCHEMA, "status": "complete", "source_revision": "a" * 40,
                "profile": {**compiler_receipt.INLINE_ACCEPTANCE_PROFILE, "source_sha256": "b" * 64, "cpu": 2},
                "candidate_compiler": {"sha256": "c" * 64, "size_bytes": 1},
                "fixed_point": {"off": True, "on": True}, "stage1": stage, "selfhost_runtime": copy.deepcopy(stage)}

    def test_predicates_refuse_out_of_range_integers_and_accept_normal_values(self) -> None:
        huge = self.HUGE
        for predicate in (compiler_receipt._finite_positive, inline_acceptance._positive_number,
                          ast_pilot_acceptance._positive_number):
            with self.subTest(predicate=predicate.__module__):
                self.assertFalse(predicate(huge))
                self.assertFalse(predicate(-huge))
                self.assertTrue(predicate(1))
                self.assertTrue(predicate(0.5))
        self.assertEqual(compiler_receipt.number(huge, "%.4f"), "NA")
        self.assertEqual(compiler_receipt.number(1.25, "%.4f"), "1.2500")
        self.assertEqual(compiler_receipt.number(3, "%.1f"), "3.0")
        with self.assertRaisesRegex(ValueError, "non-finite"):
            compiler_publish.campaign_ppm(1, huge)
        self.assertEqual(compiler_publish.campaign_ppm(1.0, 1.0)[:2], (0, 0))

        self.assertEqual(compiler_receipt.validate_inline_acceptance(self.inline_summary(), "a" * 40, "c" * 64), [])
        for key in ("a_median", "b_median", "ratio"):
            with self.subTest(inline=key):
                problems = compiler_receipt.validate_inline_acceptance(self.inline_summary(**{key: huge}), "a" * 40, "c" * 64)
                self.assertTrue(any("wall-time measurements are missing" in problem for problem in problems), problems)
        changed = self.inline_summary()
        changed["stage1"]["metrics"]["code_bytes"]["ratio"] = huge
        self.assertTrue(compiler_receipt.validate_inline_acceptance(changed, "a" * 40, "c" * 64))
        changed = self.inline_summary()
        changed["stage1"]["metrics"]["instructions"]["ratio"] = huge
        self.assertTrue(compiler_receipt.validate_inline_acceptance(changed, "a" * 40, "c" * 64))

        unused_record, unused_bundle, acceptance = ast_pilot_evidence()
        self.assertEqual(compiler_receipt.validate_ast_pilot_acceptance(acceptance, "a" * 40, "c" * 64), [])
        for metric, key in (("wall", "ratio"), ("peak_rss", "b_median"), ("instructions", "a_median")):
            changed = copy.deepcopy(acceptance)
            changed["cells"]["object"]["metrics"][metric][key] = huge
            with self.subTest(ast_pilot=metric):
                self.assertTrue(compiler_receipt.validate_ast_pilot_acceptance(changed, "a" * 40, "c" * 64))
        changed = copy.deepcopy(acceptance)
        changed["tree"]["retained_bytes"] = huge
        problems = compiler_receipt.validate_ast_pilot_acceptance(changed, "a" * 40, "c" * 64)
        self.assertTrue(any("tree retained bytes are missing" in problem for problem in problems), problems)

        self.assertEqual(compiler_receipt.classify(summary(), BINARIES), [])
        for key in ("ratio", "ci_low", "ci_high"):
            lab = summary()
            lab["verdict"][key] = huge
            lab["metrics"]["wall"][key] = huge
            with self.subTest(verdict=key):
                reasons = compiler_receipt.classify(lab, BINARIES)
                self.assertTrue(any("not a finite positive ratio" in reason for reason in reasons), reasons)

        good = corpus()
        self.assertNotIn("throughput report-only family_alpha does not match the original gate",
                         compiler_receipt.classify_throughput_exit(0, good["summary"], good["metadata"], BINARIES))
        good["summary"]["family_alpha"] = huge
        self.assertIn("throughput report-only family_alpha does not match the original gate",
                      compiler_receipt.classify_throughput_exit(0, good["summary"], good["metadata"], BINARIES))

    def publish(self, pull: dict, lab: dict, bundle: dict | None = None) -> tuple:
        """ZIP -> read_evidence -> decide (then render), as the ordinary publisher runs it."""
        members = {"receipt.json": json.dumps(pull), "lab/summary.json": json.dumps(lab),
                   "throughput/summary.json": json.dumps(corpus()["summary"]),
                   "throughput/metadata.json": json.dumps(corpus()["metadata"])}
        for key, value in (bundle or {}).items():
            members[compiler_publish.AST_PILOT_BUNDLE_FILES[key]] = json.dumps(value)
        read, summary_read, problem, unused_artifact, throughput = compiler_publish.read_evidence(
            FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
        self.assertEqual(problem, "")
        decision = compiler_publish.decide(dict(EXPECTED, mode=pull["mode"]), True, "success", read, summary_read, "",
                                           throughput)
        compiler_receipt.render(read, summary_read, decision[0], decision[2])
        return decision

    def test_out_of_range_lab_summary_fails_through_publication(self) -> None:
        main = receipt()
        self.assertEqual(self.publish(main, summary())[0], "success")
        verdict = summary()
        verdict["verdict"]["ratio"] = self.HUGE
        verdict["metrics"]["wall"]["ratio"] = self.HUGE
        metric = summary()
        metric["verdict"]["ci_high"] = self.HUGE
        for name, lab in (("verdict ratio", verdict), ("wall ci_high", metric)):
            with self.subTest(case=name):
                conclusion, unused_title, reasons = self.publish(main, lab)
                self.assertEqual(conclusion, "failure", reasons)
                self.assertTrue(any("not a finite positive ratio" in reason for reason in reasons), reasons)

    def test_out_of_range_ast_pilot_receipt_fails_through_publication(self) -> None:
        record, bundle, unused_acceptance = ast_pilot_evidence("a" * 40, B256)
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["ast_pilot_acceptance"] = record
        self.assertEqual(self.publish(pull, summary(), bundle)[:3:2], ("success", []))

        def changed(path: tuple, raw: bool) -> tuple[dict, dict]:
            current, documents = copy.deepcopy(pull), copy.deepcopy(bundle)
            targets = [current["ast_pilot_acceptance"]["summary"], documents["acceptance"]]
            if raw:
                targets = [documents]
            for target in targets:
                for key in path[:-1]:
                    target = target[key]
                target[path[-1]] = self.HUGE
            return current, documents
        cases = {
            "acceptance wall ratio": (("cells", "syntax-only", "metrics", "wall", "ratio"), False),
            "acceptance instruction median": (("cells", "object", "metrics", "instructions", "b_median"), False),
            "acceptance peak RSS": (("cells", "object", "metrics", "peak_rss", "a_median"), False),
            "acceptance tree retained bytes": (("tree", "retained_bytes"), False),
            "raw cell wall median": (("object_summary", "metrics", "wall", "a_median"), True),
        }
        for name, (path, raw) in cases.items():
            with self.subTest(case=name):
                current, documents = changed(path, raw)
                conclusion, unused_title, reasons = self.publish(current, summary(), documents)
                self.assertEqual(conclusion, "failure", reasons)
                self.assertTrue(reasons)


LINK_TLS_TOOL = Path(__file__).resolve().parents[1] / "link_tls_bench.py"


def load_link_tls_bench():
    """Import the trusted tools/link_tls_bench.py by path, as compiler_compare runs it."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("link_tls_bench", LINK_TLS_TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def link_tls_invocation(wall: int) -> dict:
    return {"exit": 0, "wall_ns": wall, "cpu_ns": wall - 1000}


def link_tls_evidence(binaries: dict | None = None) -> tuple[dict, dict]:
    """A complete synthetic link-tls-v1 record and bundle: the candidate links 10% faster in every cell."""
    profile = compiler_receipt.LINK_TLS_PROFILE
    binaries = copy.deepcopy(BINARIES) if binaries is None else binaries
    walls = {"baseline": 1_000_000, "candidate": 900_000}
    cells = []
    for index, cell in enumerate(profile["cells"]):
        image = {"exit": 0, "sha256": f"{index:x}" * 64, "size_bytes": 100 + index, "run_exit": 0,
                 "stdout": cell["stdout"]}
        cells.append({"name": cell["name"], "arguments": list(cell["arguments"]),
                      "identity": {role: dict(image) for role in walls},
                      "warmups": {role: [link_tls_invocation(walls[role]) for unused in range(profile["warmups"])]
                                  for role in walls},
                      "pairs": [dict({"order": ["baseline", "candidate"] if pair % 2 == 0 else ["candidate", "baseline"]},
                                     **{role: link_tls_invocation(walls[role]) for role in walls})
                                for pair in range(profile["pairs"])]})
    summary = {"schema": compiler_receipt.LINK_TLS_SCHEMA, "profile": profile["name"], "cpu": profile["cpu"],
               "cpu_model": "AMD Ryzen 7 9700X 8-Core Processor",
               "binaries": {role: binaries[role]["sha256"] for role in ("baseline", "candidate")},
               "toolchain": {"clang": "clang version 21.1.8", "ar": "GNU ar (GNU Binutils) 2.45"},
               "inputs": {name: "5" * 64 for name in profile["inputs"]}, "cells": cells, "status": "complete",
               "errors": []}
    summary = json.loads(json.dumps(summary))
    record = {"requested": True, "request_line": compiler_receipt.LINK_TLS_REQUEST_LINE,
              "profile": copy.deepcopy(profile), "status": "complete", "exit": 0, "errors": [],
              "harness_sha256": "6" * 64, "cells": compiler_receipt.link_tls_digest(summary)}
    return record, {"summary": summary}


class LinkTlsTest(unittest.TestCase):
    """#1915: the fixed link-tls-v1 whole-link TLS companion profile on the pull-compare route."""

    def test_profile_is_frozen_report_only_and_its_outputs_follow_the_sources(self) -> None:
        profile = compiler_receipt.LINK_TLS_PROFILE
        self.assertEqual(profile["name"], "link-tls-v1")
        self.assertEqual(compiler_receipt.LINK_TLS_REQUEST_LINE, "profile: " + profile["name"])
        self.assertIs(profile["report_only"], True)
        self.assertEqual((profile["cpu"], profile["warmups"], profile["pairs"]), (2, 2, 20))
        self.assertLessEqual(compiler_compare.LINK_TLS_TIMEOUT_SECONDS, 20 * 60)
        printed = subprocess.run([sys.executable, "-I", str(LINK_TLS_TOOL), "--profile"], capture_output=True,
                                 text=True, check=True).stdout
        self.assertEqual(json.loads(printed), json.loads(json.dumps(profile)))
        bench = load_link_tls_bench()
        for cell in profile["cells"]:
            with self.subTest(cell=cell["name"]):
                operands = [item for item in cell["arguments"][1:] if not item.startswith("-")]
                self.assertEqual(operands[0], "main.o")
                self.assertTrue(set(operands) <= set(profile["inputs"]))
                recipe = profile["inputs"][operands[1]]
                kind = profile["inputs"][recipe["archive"][0]]["source"] if "archive" in recipe else recipe["source"]
                # The image runs sum_all twice: the frozen output is what that source computes.
                source = bench.source_text(kind)
                if kind.startswith("tls:"):
                    count = int(kind[4:])
                    self.assertEqual(source.count("__thread long v"), count)
                    values = list(range(count))
                    first = sum(values)
                    second = sum(value + 1 for value in values)
                else:
                    first = second = sum(range(bench.PLAIN_FUNCTIONS))
                self.assertEqual(cell["stdout"], f"{first} {second}\n")

    def test_complete_evidence_validates_and_every_gap_fails(self) -> None:
        record, bundle = link_tls_evidence()
        summary = bundle["summary"]
        self.assertEqual(compiler_receipt.validate_link_tls(0, summary, BINARIES), [])
        results = compiler_receipt.link_tls_digest(summary)
        self.assertEqual([row["name"] for row in results], [cell["name"] for cell in compiler_receipt.LINK_TLS_PROFILE["cells"]])
        self.assertTrue(all(row["ratio"] == 0.9 and row["candidate_faster"] == 20 for row in results))

        def changed(edit) -> dict:
            value = copy.deepcopy(summary)
            edit(value)
            return value

        def timed(edit, cell: int = 2, pair: int = 3, role: str = "candidate") -> dict:
            return changed(lambda value: edit(value["cells"][cell]["pairs"][pair][role]))

        same = {"baseline": {"sha256": A256}, "candidate": {"sha256": A256}}
        cases = {
            "failed run": (summary, {"status": 1}),
            "cancelled run": (summary, {"status": None}),
            "missing summary": (None, {}),
            "other schema": (changed(lambda value: value.update(schema="other")), {}),
            "changed profile": (changed(lambda value: value.update(profile="link-tls-v2")), {}),
            "other CPU": (changed(lambda value: value.update(cpu=3)), {}),
            "failed status": (changed(lambda value: value.update(status="failed")), {}),
            "reported error": (changed(lambda value: value.update(errors=["tls-gd-2048: a timed link failed"])), {}),
            "other binaries": (summary, {"binaries": {"baseline": {"sha256": B256}, "candidate": {"sha256": A256}}}),
            "same binary": (changed(lambda value: value.update(binaries={"baseline": A256, "candidate": A256})),
                            {"binaries": same}),
            "no measured binaries": (summary, {"binaries": {}}),
            "missing input": (changed(lambda value: value["inputs"].pop("m16384.a")), {}),
            "unhashed input": (changed(lambda value: value["inputs"].update({"main.o": "5"})), {}),
            "extra input": (changed(lambda value: value["inputs"].update({"other.o": "5" * 64})), {}),
            "unrecorded toolchain": (changed(lambda value: value["toolchain"].pop("ar")), {}),
            "missing cell": (changed(lambda value: value["cells"].pop()), {}),
            "reordered cells": (changed(lambda value: value["cells"].reverse()), {}),
            "changed arguments": (changed(lambda value: value["cells"][0].update(
                arguments=["cc", "-pie", "main.o", "m1024.o"])), {}),
            "different images": (changed(lambda value: value["cells"][3]["identity"]["candidate"].update(
                sha256="f" * 64)), {}),
            "different image sizes": (changed(lambda value: value["cells"][3]["identity"]["candidate"].update(
                size_bytes=1)), {}),
            "wrong output": (changed(lambda value: [value["cells"][4]["identity"][role].update(stdout="0 0\n")
                                                    for role in ("baseline", "candidate")]), {}),
            "image did not run": (changed(lambda value: value["cells"][1]["identity"]["baseline"].update(
                run_exit="TimeoutExpired")), {}),
            "failed identity link": (changed(lambda value: value["cells"][1]["identity"]["candidate"].update(exit=1)), {}),
            "malformed identity": (changed(lambda value: value["cells"][1]["identity"]["candidate"].pop("stdout")), {}),
            "missing pair": (changed(lambda value: value["cells"][2]["pairs"].pop()), {}),
            "pair out of order": (changed(lambda value: value["cells"][2]["pairs"][1].update(
                order=["baseline", "candidate"])), {}),
            "failed timed link": (timed(lambda value: value.update(exit=1)), {}),
            "timed-out link": (timed(lambda value: value.update(exit=124)), {}),
            "zero wall time": (timed(lambda value: value.update(wall_ns=0)), {}),
            "float wall time": (timed(lambda value: value.update(wall_ns=1.5)), {}),
            "wall time past the invocation timeout": (timed(lambda value: value.update(
                wall_ns=compiler_receipt.LINK_TLS_SAMPLE_LIMIT_NS + 1)), {}),
            "overflowing wall time": (timed(lambda value: value.update(wall_ns=10 ** 309)), {}),
            "missing CPU time": (timed(lambda value: value.pop("cpu_ns")), {}),
            "missing warmup": (changed(lambda value: value["cells"][5]["warmups"]["baseline"].pop()), {}),
            "failed warmup": (changed(lambda value: value["cells"][5]["warmups"]["candidate"][0].update(exit=2)), {}),
        }
        for name, (value, options) in cases.items():
            with self.subTest(case=name):
                self.assertTrue(compiler_receipt.validate_link_tls(options.get("status", 0), value,
                                                                   options.get("binaries", BINARIES)), name)
                # Malformed evidence is refused with reasons, never raised.
                self.assertIsInstance(compiler_receipt.link_tls_digest(value), list)

    def test_decision_needs_pull_mode_and_the_complete_bundle(self) -> None:
        record, bundle = link_tls_evidence()
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["link_tls"] = record
        values = {"expected": dict(EXPECTED, mode="pull"), "authorized": True, "compare_result": "success",
                  "receipt": pull, "summary": summary(), "policy_value": "",
                  "throughput": dict(corpus(), link_tls_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        stale = copy.deepcopy(record)
        stale["cells"][0]["ratio"] = 0.5
        widened = copy.deepcopy(record)
        widened["profile"]["pairs"] = 5
        for name, change in {
            "not the Zen 5 host": {"receipt": dict(pull, host={"cpu_model": "AMD EPYC 7763 64-Core Processor"})},
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "empty bundle": {"throughput": dict(corpus(), link_tls_bundle={})},
            "failed run": {"receipt": dict(pull, link_tls=dict(record, status="failed", exit=1))},
            "cancelled run": {"receipt": dict(pull, link_tls=dict(record, status="pending", exit=None))},
            "stale cells": {"receipt": dict(pull, link_tls=stale)},
            "changed profile": {"receipt": dict(pull, link_tls=widened)},
            "other selector": {"receipt": dict(pull, link_tls=dict(record, request_line="profile: other"))},
            "not a request": {"receipt": dict(pull, link_tls=dict(record, requested=False))},
            "other binaries": {"receipt": dict(pull, binaries={"baseline": {"sha256": B256},
                                                               "candidate": {"sha256": A256}})},
            "with the compiler probes": {"receipt": dict(pull, compiler_probes=compiler_probes_evidence()[0]),
                                         "throughput": dict(corpus(), link_tls_bundle=bundle,
                                                            compiler_probes_bundle=compiler_probes_evidence()[1])},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("TLS companion links (link-tls-v1): complete", report)
        self.assertIn("| tls-gd-32768 | 1.000 | 0.900 | 0.9000 [0.9000, 0.9000] | 20/20 |", report)

    def test_artifact_is_read_and_decided_end_to_end(self) -> None:
        record, bundle = link_tls_evidence()
        pull = receipt()
        pull["mode"] = "pull"
        pull["identity"].update(mode="pull")
        pull["link_tls"] = record

        def decided(link_tls: object, drop: bool = False) -> tuple:
            members = {"receipt.json": json.dumps(pull), "lab/summary.json": json.dumps(summary()),
                       "throughput/summary.json": json.dumps(corpus()["summary"]),
                       "throughput/metadata.json": json.dumps(corpus()["metadata"])}
            if not drop:
                members["link_tls/summary.json"] = link_tls if isinstance(link_tls, str) else json.dumps(link_tls)
            got = compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
            return compiler_publish.decide(dict(EXPECTED, mode="pull"), True, "success", got[0], got[1], "",
                                           got[4], extra_reasons=[got[2]] if got[2] else [])

        self.assertEqual(decided(bundle["summary"])[::2], ("success", []))
        changed = copy.deepcopy(bundle["summary"])
        for pair in changed["cells"][2]["pairs"]:
            pair["candidate"]["wall_ns"] = 2_000_000
        for name, (value, drop) in {"missing member": (None, True), "malformed member": ("{", False),
                                    "duplicate key": ('{"schema": 1, "schema": 2}', False),
                                    "samples that disagree with the receipt": (changed, False)}.items():
            with self.subTest(case=name):
                self.assertEqual(decided(value, drop)[0], "failure")

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            fixture_git(repo, "init", "-q")
            fixture_git(repo, "config", "user.name", "Link TLS Test")
            fixture_git(repo, "config", "user.email", "link-tls@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            fixture_git(repo, "add", compiler_receipt.ANALYZER_REQUEST_PATH)
            fixture_git(repo, "commit", "-qm", "seed")
            line = compiler_receipt.LINK_TLS_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                fixture_git(repo, "commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.link_tls_request_selection(
                        repo, fixture_git(repo, "rev-parse", "HEAD")), expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            fixture_git(repo, "commit", "-qam", "duplicate")
            requested, problem = compiler_compare.link_tls_request_selection(repo, fixture_git(repo, "rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.LINK_TLS_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}

            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = receipt()
        pull.update(mode="pull", link_tls=link_tls_evidence()[0])
        expected = {"mode": "pull", "head": head}
        verify = compiler_publish.verify_analyzer_request_freshness
        self.assertEqual(verify(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", verify(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", verify(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("link_tls")
        self.assertIn("no link-tls record", verify(Api(1, 0), expected, plain, {}))
        self.assertEqual(verify(Api(1, 1), expected, plain, {}), "")

    def test_leg_runs_the_trusted_harness_on_both_binaries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("work", "evidence", "work/bin"):
                (root / name).mkdir(exist_ok=True)
            pull = {"timings": {}, "binaries": copy.deepcopy(BINARIES),
                    "link_tls": {"requested": True, "status": "pending"}}
            with mock.patch.object(compiler_compare, "run", return_value=1) as run:
                compiler_compare.measure_link_tls(root / "work/bin", root / "work", root / "evidence", pull)
            argv = run.call_args.args[0]
            self.assertEqual(argv[1:3], ["-I", str(LINK_TLS_TOOL)])
            self.assertEqual(argv[3:5], [str(root / "work/bin/ide-base"), str(root / "work/bin/ide-cand")])
            self.assertEqual(pull["link_tls"]["harness_sha256"], hashlib.sha256(LINK_TLS_TOOL.read_bytes()).hexdigest())
            # The mocked harness exited 1 and wrote nothing, so the leg is refused.
            self.assertEqual(pull["link_tls"]["status"], "failed")
            self.assertTrue(any("exited 1" in error for error in pull["link_tls"]["errors"]), pull["link_tls"]["errors"])

    def test_harness_links_real_inputs_and_records_complete_evidence(self) -> None:
        """A reduced profile, fake ide binaries that link with clang: identity, timing and validation end to end."""
        bench = load_link_tls_bench()
        profile = copy.deepcopy(compiler_receipt.LINK_TLS_PROFILE)
        profile["inputs"] = {name: profile["inputs"][name] for name in ("main.o", "m1024.o", "plain.o")}
        profile["cells"] = [cell for cell in profile["cells"] if cell["name"] in ("tls-gd-2048", "no-tls-control")]
        profile.update(warmups=1, pairs=2, cpu=0)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binaries = {}
            for role in ("baseline", "candidate"):
                binaries[role] = root / f"ide-{role}"
                binaries[role].write_text(f"#!/bin/sh\n# {role}\nshift\nexec clang \"$@\"\n", encoding="utf-8")
                binaries[role].chmod(0o755)
            with mock.patch.object(bench, "LINK_TLS_PROFILE", profile), \
                    mock.patch.object(compiler_receipt, "LINK_TLS_PROFILE", profile):
                status = bench.main([str(binaries["baseline"]), str(binaries["candidate"]), str(root / "work"),
                                     str(root / "out")])
                written = json.loads((root / "out/summary.json").read_text(encoding="utf-8"))
                hashes = {role: {"sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for role, path in binaries.items()}
                self.assertEqual((status, written["errors"]), (0, []))
                self.assertEqual(compiler_receipt.validate_link_tls(status, written, hashes), [])
                self.assertEqual(len(compiler_receipt.link_tls_digest(written)), 2)
            self.assertEqual(sorted(written["inputs"]), ["m1024.o", "main.o", "plain.o"])
            # A candidate that links differently is refused before anything is timed.
            binaries["candidate"].write_text("#!/bin/sh\nshift\nexec clang -Wl,--defsym=link_tls_test_marker=1 \"$@\"\n",
                                             encoding="utf-8")
            with mock.patch.object(bench, "LINK_TLS_PROFILE", profile):
                status = bench.main([str(binaries["baseline"]), str(binaries["candidate"]), str(root / "work2"),
                                     str(root / "out2")])
            written = json.loads((root / "out2/summary.json").read_text(encoding="utf-8"))
            self.assertEqual((status, written["status"]), (1, "failed"))
            self.assertTrue(all(cell["pairs"] == [] for cell in written["cells"]))
            self.assertTrue(any("images differ" in error for error in written["errors"]), written["errors"])


SELFHOST_INLINE_TOOL = Path(__file__).resolve().with_name("selfhost_inline_compare.py")
SELFHOST_BINARIES = {"baseline": {"sha256": A256, "revision": EXPECTED["base"]},
                     "candidate": {"sha256": B256, "revision": EXPECTED["head"]}}
SELFHOST_STAGE1 = {"baseline": "c" * 64, "candidate": "f" * 64}


def selfhost_inline_metric(a: float, b: float) -> dict:
    return {"a_median": a, "b_median": b, "ratio": b / a, "ci_low": b / a - 0.01, "ci_high": b / a + 0.01,
            "outcome": "below-floor"}


def selfhost_inline_evidence(binaries: dict | None = None) -> tuple[dict, dict]:
    """A complete synthetic self-host-inline-compare-v1 record and bundle: head 2% faster, outputs identical."""
    profile = compiler_receipt.SELFHOST_INLINE_PROFILE
    binaries = copy.deepcopy(SELFHOST_BINARIES) if binaries is None else binaries
    flags = list(profile["compile_flags"])
    stage1 = {role: {"schema": compiler_receipt.SELFHOST_INLINE_STAGE1_SCHEMA, "status": "complete",
                     "revision": binaries[role]["revision"],
                     "compiler": {"sha256": binaries[role]["sha256"], "size_bytes": 1000},
                     "stage1": {"sha256": SELFHOST_STAGE1[role], "size_bytes": 2000},
                     "source_sha256": "3" * 64, "frozen_input_tree": {}, "command": list(profile["stage1_command"]),
                     "elapsed_seconds": 2.0} for role in ("baseline", "candidate")}
    result = {"schema": compiler_receipt.SELFHOST_INLINE_SCHEMA, "status": "complete", "profile": profile["name"],
              "base_revision": binaries["baseline"]["revision"], "frozen_input_tree": {},
              "stage1_compilers": {role: {"sha256": SELFHOST_STAGE1[role]} for role in ("baseline", "candidate")},
              "elapsed_seconds": 600.0}
    lab = {"schema": selfhost_inline_compare.UARCH_SCHEMA,
           "plan": {"pairs": profile["pairs"], "complete_pairs": profile["pairs"], "order": "ABBA", "fresh_copy": True},
           "baseline": {"sha256": SELFHOST_STAGE1["baseline"], "runs": profile["pairs"], "failed": 0,
                        "deterministic": True},
           "candidate": {"sha256": SELFHOST_STAGE1["candidate"], "runs": profile["pairs"], "failed": 0,
                         "deterministic": True},
           "outputs_identical": True, "counters": {"perf_stat": True},
           "metrics": {"wall": selfhost_inline_metric(5.0, 4.9), "task_clock": selfhost_inline_metric(5.0, 4.9),
                       "instructions": selfhost_inline_metric(4.0e10, 3.9e10), "cycles": selfhost_inline_metric(2.5e10, 2.45e10),
                       "ipc": selfhost_inline_metric(1.6, 1.59), "branch_misses": selfhost_inline_metric(1.0e8, 0.99e8),
                       "peak_rss": selfhost_inline_metric(1.0e9, 1.0e9)},
           "code_bytes": {"a_value": 9000, "b_value": 8900, "ratio": 8900 / 9000},
           "profile": {"topdown": {"status": "ok", "metrics": [
               {"group": "PipelineL1", "metric": "frontend_bound", "unit": "%", "a": 20.0, "b": 19.0, "delta": -1.0,
                "relative_change_percent": -5.0},
               {"group": "cache", "metric": "l2_hit_rate", "unit": "%", "a": 90.0, "b": 90.0, "delta": 0.0,
                "relative_change_percent": 0.0}]}},
           "verdict": {"text": "below-floor"}, "warnings": []}
    meta = {"config": {"extra": flags, "extra_by_variant": {"a": flags, "b": flags}, "canonical_inline_pair": False,
                       "pairs": profile["pairs"], "warmups": profile["warmups"], "cpu": profile["cpu"],
                       "profile_steps": list(profile["profile_steps"]), "require_identical_output": True,
                       "min_effect_percent": profile["min_effect_percent"], "fresh_copy": True},
            "outputs_identical": True,
            "steps": {step: {"status": "ok"} for step in ("prepare", "timed", *profile["profile_steps"])}}
    labs = {key: {"config": {"extra": flags, "cpu": profile["cpu"]}} for key in ("a", "b")}
    bundle = {"stage1-baseline/stage1.json": stage1["baseline"], "stage1-candidate/stage1.json": stage1["candidate"],
              "compare/result.json": result, "compare/lab/summary.json": lab, "compare/lab/compare.json": meta,
              "compare/lab/a/lab.json": labs["a"], "compare/lab/b/lab.json": labs["b"]}
    bundle = json.loads(json.dumps(bundle))
    record = {"requested": True, "request_line": compiler_receipt.SELFHOST_INLINE_REQUEST_LINE,
              "profile": copy.deepcopy(profile), "status": "complete", "exit": 0, "errors": [],
              "stage1_exit": {"baseline": 0, "candidate": 0}, "harness_sha256": "6" * 64,
              "digest": compiler_receipt.selfhost_inline_digest(bundle["compare/lab/summary.json"])}
    return record, bundle


def selfhost_inline_problems(bundle: dict, binaries: dict | None = None) -> list[str]:
    binaries = SELFHOST_BINARIES if binaries is None else binaries
    stage1 = {role: bundle[f"stage1-{role}/stage1.json"] for role in ("baseline", "candidate")}
    expected = {role: (stage1[role].get("stage1") or {}).get("sha256") if isinstance(stage1[role], dict) else None
                for role in stage1}
    return [*compiler_receipt.validate_selfhost_inline(stage1, bundle["compare/result.json"], binaries),
            *selfhost_inline_compare.validate_compare_documents(
                bundle["compare/lab/summary.json"], bundle["compare/lab/compare.json"],
                {"a": bundle["compare/lab/a/lab.json"], "b": bundle["compare/lab/b/lab.json"]}, expected)]


def selfhost_pull_receipt(record: dict) -> dict:
    pull = receipt()
    pull["mode"] = "pull"
    pull["identity"].update(mode="pull")
    pull["binaries"] = copy.deepcopy(SELFHOST_BINARIES)
    pull["selfhost_inline"] = record
    return pull


class SelfHostInlineCompareTest(unittest.TestCase):
    """#58: the fixed self-host-inline-compare-v1 base/head self-built inline-mode profile."""

    def test_profile_is_frozen_report_only_and_printed_by_the_helper(self) -> None:
        profile = compiler_receipt.SELFHOST_INLINE_PROFILE
        self.assertEqual(profile["name"], "self-host-inline-compare-v1")
        self.assertEqual(compiler_receipt.SELFHOST_INLINE_REQUEST_LINE, "profile: " + profile["name"])
        self.assertIs(profile["report_only"], True)
        self.assertEqual((profile["cpu"], profile["warmups"], profile["pairs"], profile["order"]), (2, 1, 30, "ABBA"))
        self.assertEqual(profile["compile_flags"], ["-fcanonical-inline"])
        self.assertEqual(profile["stage1_command"][-1], "-fcanonical-inline")
        self.assertEqual(profile["profile_steps"], ["topdown", "sampling"])
        self.assertLessEqual(compiler_compare.SELFHOST_INLINE_TIMEOUT_SECONDS, 45 * 60)
        printed = subprocess.run([sys.executable, "-B", str(SELFHOST_INLINE_TOOL), "profile"], capture_output=True,
                                 text=True, check=True, cwd=SELFHOST_INLINE_TOOL.parent).stdout
        self.assertEqual(json.loads(printed), json.loads(json.dumps(profile)))

    def test_complete_evidence_validates_and_every_gap_fails(self) -> None:
        record, bundle = selfhost_inline_evidence()
        self.assertEqual(selfhost_inline_problems(bundle), [])
        digest = record["digest"]
        self.assertAlmostEqual(digest["metrics"]["wall"]["ratio"], 0.98)
        self.assertEqual([row["metric"] for row in digest["frontend"]], ["frontend_bound"])

        def changed(member: str, edit) -> dict:
            value = copy.deepcopy(bundle)
            edit(value[member])
            return value

        lab, meta, result = "compare/lab/summary.json", "compare/lab/compare.json", "compare/result.json"
        base, head = "stage1-baseline/stage1.json", "stage1-candidate/stage1.json"
        cases = {
            "incomplete stage 1": changed(head, lambda value: value.update(status="failed")),
            "stage 1 from the wrong revision": changed(head, lambda value: value.update(revision=EXPECTED["base"])),
            "stage 1 by another compiler": changed(base, lambda value: value["compiler"].update(sha256=B256)),
            "stage 1 command changed": changed(base, lambda value: value.update(command=["cc", "-O2"])),
            "malformed stage-1 identity": changed(head, lambda value: value["stage1"].update(sha256="x")),
            "result schema": changed(result, lambda value: value.update(schema="other")),
            "result incomplete": changed(result, lambda value: value.update(status="failed")),
            "result for another profile": changed(result, lambda value: value.update(profile="other")),
            "timed another tree": changed(result, lambda value: value.update(base_revision=EXPECTED["head"])),
            "timed other compilers": changed(result, lambda value: value["stage1_compilers"]["candidate"].update(
                sha256="e" * 64)),
            "uarch schema": changed(lab, lambda value: value.update(schema="other")),
            "missing pairs": changed(lab, lambda value: value["plan"].update(complete_pairs=29)),
            "not fresh copies": changed(lab, lambda value: value["plan"].update(fresh_copy=False)),
            "timed binary differs": changed(lab, lambda value: value["candidate"].update(sha256=SELFHOST_STAGE1["baseline"])),
            "nondeterministic output": changed(lab, lambda value: value["baseline"].update(deterministic=False)),
            "failed runs": changed(lab, lambda value: value["candidate"].update(failed=1)),
            "no wall interval": changed(lab, lambda value: value["metrics"]["wall"].update(ci_low=None)),
            "no hardware counters": changed(lab, lambda value: value.update(counters={"perf_stat": False,
                                                                                       "reason": "perf missing"})),
            "no cycles": changed(lab, lambda value: value["metrics"]["cycles"].update(a_median=None)),
            "no topdown": changed(lab, lambda value: value["profile"].update(topdown={"status": "failed", "metrics": []})),
            "cache-only topdown": changed(lab, lambda value: value["profile"]["topdown"]["metrics"].pop(0)),
            "malformed topdown row": changed(lab, lambda value: value["profile"]["topdown"]["metrics"].append({})),
            "front end without values": changed(lab, lambda value: value["profile"]["topdown"]["metrics"][0].update(
                a=None, b=None)),
            "one-sided front end": changed(lab, lambda value: value["profile"]["topdown"]["metrics"][0].update(b=None)),
            "non-finite front end": changed(lab, lambda value: value["profile"]["topdown"]["metrics"][0].update(
                a=float("nan"))),
            "only a level-2 front-end metric": changed(lab, lambda value: value["profile"]["topdown"]["metrics"][0].update(
                group="PipelineL2", metric="frontend_bound_by_latency")),
            "an unavailable extra front-end group": changed(lab, lambda value: value["profile"]["topdown"]["metrics"].append(
                {"group": "decoder", "metric": "op_cache_hit_rate", "unit": "%", "a": 80.0, "b": None})),
            "other flags": changed(meta, lambda value: value["config"].update(extra=[])),
            "fixed inline pair": changed(meta, lambda value: value["config"].update(canonical_inline_pair=True)),
            "other pair count": changed(meta, lambda value: value["config"].update(pairs=12)),
            "other CPU": changed(meta, lambda value: value["config"].update(cpu=3)),
            "no profile steps": changed(meta, lambda value: value["config"].update(profile_steps=[])),
            "outputs not required identical": changed(meta, lambda value: value["config"].update(
                require_identical_output=False)),
            "outputs differ": changed(meta, lambda value: value.update(outputs_identical=False)),
            "failed sampling": changed(meta, lambda value: value["steps"]["sampling"].update(status="failed")),
            "variant flags": changed("compare/lab/b/lab.json", lambda value: value["config"].update(extra=[])),
        }
        for name, value in cases.items():
            with self.subTest(case=name):
                self.assertTrue(selfhost_inline_problems(value), name)
        for name, binaries in {"no revisions": BINARIES,
                               "other baseline binary": dict(SELFHOST_BINARIES, baseline={"sha256": B256,
                                                                                          "revision": EXPECTED["base"]})
                               }.items():
            with self.subTest(case=name):
                self.assertTrue(selfhost_inline_problems(bundle, binaries))
        for value in (None, [], {"metrics": []}):
            self.assertIsNone(compiler_receipt.selfhost_inline_digest(value))
        self.assertTrue(compiler_receipt.validate_selfhost_inline(None, None, SELFHOST_BINARIES))
        self.assertTrue(selfhost_inline_compare.validate_compare_documents(None, None, None, {}))

    def test_decision_needs_pull_mode_and_the_complete_bundle(self) -> None:
        record, bundle = selfhost_inline_evidence()
        pull = selfhost_pull_receipt(record)
        values = {"expected": dict(EXPECTED, mode="pull"), "authorized": True, "compare_result": "success",
                  "receipt": pull, "summary": summary(), "policy_value": "",
                  "throughput": dict(corpus(), selfhost_inline_bundle=bundle)}
        conclusion, unused_title, reasons = compiler_publish.decide(**values)
        self.assertEqual((conclusion, reasons), ("success", []))
        main = copy.deepcopy(pull)
        main["mode"] = "main"
        main["identity"].update(mode="main")
        stale = copy.deepcopy(record)
        stale["digest"]["metrics"]["wall"]["ratio"] = 0.5
        widened = copy.deepcopy(record)
        widened["profile"]["pairs"] = 5
        for name, change in {
            "main mode": {"receipt": main, "expected": dict(EXPECTED)},
            "missing bundle": {"throughput": corpus()},
            "empty bundle": {"throughput": dict(corpus(), selfhost_inline_bundle={})},
            "failed run": {"receipt": dict(pull, selfhost_inline=dict(record, status="failed", exit=1))},
            "stale digest": {"receipt": dict(pull, selfhost_inline=stale)},
            "changed profile": {"receipt": dict(pull, selfhost_inline=widened)},
            "other selector": {"receipt": dict(pull, selfhost_inline=dict(record, request_line="profile: other"))},
            "not a request": {"receipt": dict(pull, selfhost_inline=dict(record, requested=False))},
            "other binaries": {"receipt": dict(pull, binaries=dict(SELFHOST_BINARIES, candidate={
                "sha256": A256, "revision": EXPECTED["head"]}))},
            "with the link-tls leg": {"receipt": dict(pull, link_tls=link_tls_evidence()[0]),
                                      "throughput": dict(corpus(), selfhost_inline_bundle=bundle,
                                                         link_tls_bundle=link_tls_evidence()[1])},
            "with the scaling leg": {"receipt": dict(pull, scaling_profile=copy.deepcopy(compiler_receipt.SCALING_PROFILE)),
                                     "throughput": dict(corpus(), selfhost_inline_bundle=bundle, scaling=scaling())},
            "with constant-reuse acceptance": {
                "receipt": dict(pull, constant_reuse_acceptance=constant_reuse_evidence(EXPECTED["head"], B256)[0]),
                "throughput": dict(corpus(), selfhost_inline_bundle=bundle,
                                   constant_reuse_bundle=constant_reuse_evidence(EXPECTED["head"], B256)[1])},
        }.items():
            with self.subTest(case=name):
                conclusion, unused_title, reasons = compiler_publish.decide(**dict(values, **change))
                self.assertEqual(conclusion, "failure", reasons)
        report = compiler_receipt.render(pull, summary(), "success", [])
        self.assertIn("Self-built inline-mode compilers (self-host-inline-compare-v1): complete", report)
        self.assertIn("| wall | 5 | 4.9 | 0.9800 [0.9700, 0.9900] |", report)
        self.assertIn("| PipelineL1 frontend_bound | 20 | 19 | -5.00% |", report)

    def test_artifact_is_read_and_decided_end_to_end(self) -> None:
        record, bundle = selfhost_inline_evidence()
        pull = selfhost_pull_receipt(record)

        def decided(replace: dict | None = None, drop: str = "") -> tuple:
            members = {"receipt.json": json.dumps(pull), "lab/summary.json": json.dumps(summary()),
                       "throughput/summary.json": json.dumps(corpus()["summary"]),
                       "throughput/metadata.json": json.dumps(corpus()["metadata"])}
            for name, value in bundle.items():
                if name != drop:
                    members[f"selfhost_inline/{name}"] = json.dumps(value)
            members.update(replace or {})
            got = compiler_publish.read_evidence(FakeApi(archive(members)), "92", "buster-9700x-compiler-x-1")
            return compiler_publish.decide(dict(EXPECTED, mode="pull"), True, "success", got[0], got[1], "",
                                           got[4], extra_reasons=[got[2]] if got[2] else [])

        self.assertEqual(decided()[::2], ("success", []))
        slower = copy.deepcopy(bundle["compare/lab/summary.json"])
        slower["metrics"]["wall"]["ratio"] = 1.2
        for name, (replace, drop) in {
            "missing member": (None, "compare/lab/compare.json"),
            "malformed member": ({"selfhost_inline/compare/result.json": "{"}, ""),
            "duplicate key": ({"selfhost_inline/compare/result.json": '{"schema": 1, "schema": 2}'}, ""),
            "summary that disagrees with the receipt": ({"selfhost_inline/compare/lab/summary.json": json.dumps(slower)}, ""),
        }.items():
            with self.subTest(case=name):
                self.assertEqual(decided(replace, drop)[0], "failure")

    def test_selector_must_be_freshly_added_once_at_the_head(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            fixture_git(repo, "init", "-q")
            fixture_git(repo, "config", "user.name", "Self Host Inline Test")
            fixture_git(repo, "config", "user.email", "selfhost-inline@example.invalid")
            request = repo / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text("# request history\n", encoding="utf-8")
            fixture_git(repo, "add", compiler_receipt.ANALYZER_REQUEST_PATH)
            fixture_git(repo, "commit", "-qm", "seed")
            line = compiler_receipt.SELFHOST_INLINE_REQUEST_LINE + "\n"
            for content, expected in ((line, (True, "")), ("# unrelated request\n", (False, "")),
                                      (line.rstrip("\n") + "-extra\n", (False, ""))):
                request.write_text(request.read_text(encoding="utf-8") + content, encoding="utf-8")
                fixture_git(repo, "commit", "-qam", "request")
                with self.subTest(content=content):
                    self.assertEqual(compiler_compare.selfhost_inline_request_selection(
                        repo, fixture_git(repo, "rev-parse", "HEAD")), expected)
            request.write_text(request.read_text(encoding="utf-8") + line * 2, encoding="utf-8")
            fixture_git(repo, "commit", "-qam", "duplicate")
            requested, problem = compiler_compare.selfhost_inline_request_selection(
                repo, fixture_git(repo, "rev-parse", "HEAD"))
            self.assertFalse(requested)
            self.assertIn("exactly once", problem)

    def test_publisher_rechecks_selector_freshness_against_every_parent(self) -> None:
        head, parent = "a" * 40, "b" * 40
        line = (compiler_receipt.SELFHOST_INLINE_REQUEST_LINE + "\n").encode("utf-8")

        class Api:
            def __init__(self, head_count: int, parent_count: int):
                self.counts = {head: head_count, parent: parent_count}

            def request(self, path: str, data: dict | None = None) -> object:
                if path == f"/commits/{head}":
                    return {"sha": head, "parents": [{"sha": parent}]}
                revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get("ref", [""])[0]
                content = line * self.counts[revision]
                return {"type": "file", "encoding": "base64", "size": len(content),
                        "content": base64.b64encode(content).decode("ascii")}

        pull = selfhost_pull_receipt(selfhost_inline_evidence()[0])
        expected = {"mode": "pull", "head": head}
        verify = compiler_publish.verify_analyzer_request_freshness
        self.assertEqual(verify(Api(1, 0), expected, pull, {}), "")
        self.assertIn("not freshly added", verify(Api(1, 1), expected, pull, {}))
        self.assertIn("not freshly added", verify(Api(2, 0), expected, pull, {}))
        plain = dict(pull)
        plain.pop("selfhost_inline")
        self.assertIn("no self-host inline record", verify(Api(1, 0), expected, plain, {}))
        self.assertEqual(verify(Api(1, 1), expected, plain, {}), "")

    def test_request_with_another_opt_in_leg_or_scaling_fails_before_any_build(self) -> None:
        for name, patches in {"scaling": {"scaling_requested": True},
                              "link-tls": {"scaling_requested": False},
                              "constant-reuse": {"scaling_requested": False}}.items():
            with self.subTest(combined=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                candidate, work, evidence, bins = root / "candidate", root / "work", root / "evidence", root / "bins"
                for directory in (candidate, work, evidence, bins):
                    directory.mkdir()
                current = {"state": "pending", "reasons": [], "timings": {}, "binaries": {}, "lab": {}}
                args = argparse.Namespace(mode="pull", base="b" * 40, head="a" * 40, selfhost_inline_requested=True,
                                          link_tls_requested=name == "link-tls",
                                          constant_reuse_requested=name == "constant-reuse", lab=root / "uarch_lab.py")
                with mock.patch.object(compiler_compare, "scaling_requested", return_value=patches["scaling_requested"]), \
                        mock.patch.object(compiler_compare, "build") as build, \
                        mock.patch.object(compiler_compare, "run") as run:
                    compiler_compare.measure(args, candidate, work, evidence, bins, evidence / "build.log", current, [])
                self.assertTrue(any("self-host-inline-compare-v1 cannot be combined" in reason
                                    for reason in current["reasons"]), current["reasons"])
                self.assertEqual(current["selfhost_inline"]["status"], "failed")
                build.assert_not_called()
                run.assert_not_called()

    def test_leg_exports_and_reads_each_part_s_mandatory_documents_when_optional_files_are_deferred(self) -> None:
        """A deferred export copies only a part's required members before measure returns; the leg must name them."""
        record_value, bundle = selfhost_inline_evidence()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("work", "evidence", "candidate"):
                (root / name).mkdir()
            for name, value in bundle.items():
                path = root / "work/selfhost-inline" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(value), encoding="utf-8")
            (root / "work/selfhost-inline/compare/lab/a/report.md").write_text("optional\n", encoding="utf-8")
            pull = {"timings": {}, "binaries": copy.deepcopy(SELFHOST_BINARIES),
                    "selfhost_inline": {"requested": True, "status": "pending",
                                        "stage1_exit": {"baseline": 0, "candidate": 0}}}
            arguments = argparse.Namespace(lab=root / "uarch_lab.py", base=EXPECTED["base"])
            exported_required = []

            def deferred_export(source: Path, destination: Path, evidence: Path, ignore: tuple, required: tuple):
                exported_required.append(required)
                for name in required:
                    target = destination / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source / name, target)
                return [], []

            with mock.patch.object(compiler_compare, "run", return_value=0), \
                    mock.patch.object(compiler_compare, "export_tree", side_effect=deferred_export):
                compiler_compare.measure_selfhost_inline(arguments, root / "candidate", root / "work",
                                                         root / "evidence", pull)
            record = pull["selfhost_inline"]
            self.assertEqual((record["status"], record["errors"]), ("complete", []))
            self.assertEqual(sorted(name for names in exported_required for name in names),
                             sorted(["stage1.json", "stage1.json", "result.json", "lab/summary.json",
                                     "lab/compare.json", "lab/a/lab.json", "lab/b/lab.json"]))
            self.assertFalse((root / "evidence/selfhost_inline/compare/lab/a/report.md").exists())
            self.assertEqual(record["digest"], record_value["digest"])

    def test_the_presence_selected_48_inline_leg_does_not_block_this_leg(self) -> None:
        """inline_acceptance_requested reads presence, and main has carried its line since #48 (#3969 refusal)."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate, work, evidence, bins = root / "candidate", root / "work", root / "evidence", root / "bins"
            for directory in (work, evidence, bins):
                directory.mkdir()
            request = candidate / compiler_receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)
            request.write_text(compiler_receipt.INLINE_ACCEPTANCE_REQUEST_LINE + "\n" +
                               compiler_receipt.SELFHOST_INLINE_REQUEST_LINE + "\n", encoding="utf-8")
            current = {"state": "pending", "reasons": [], "timings": {"build_seconds": {}}, "binaries": {}, "lab": {}}
            args = argparse.Namespace(mode="pull", base="b" * 40, head="a" * 40, selfhost_inline_requested=True,
                                      lab=root / "uarch_lab.py")
            with mock.patch.object(compiler_compare, "scaling_requested", return_value=False), \
                    mock.patch.object(compiler_compare, "build", return_value=("stopped by the test", 0.0)) as build:
                compiler_compare.measure(args, candidate, work, evidence, bins, evidence / "build.log", current, [])
            self.assertIs(current["inline_acceptance"]["requested"], True)
            self.assertFalse(any("cannot be combined" in reason for reason in current["reasons"]), current["reasons"])
            build.assert_called_once()
        record, bundle = selfhost_inline_evidence()
        pull = selfhost_pull_receipt(record)
        pull["inline_acceptance"] = {"requested": True, "status": "failed"}
        unused, unused_title, reasons = compiler_publish.decide(
            dict(EXPECTED, mode="pull"), True, "success", pull, summary(), "",
            dict(corpus(), selfhost_inline_bundle=bundle))
        self.assertFalse(any("self-host inline leg cannot be combined" in reason for reason in reasons), reasons)

    def test_leg_runs_the_trusted_helper_and_refuses_missing_stage1(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("work", "evidence", "candidate"):
                (root / name).mkdir()
            pull = {"timings": {}, "binaries": copy.deepcopy(SELFHOST_BINARIES),
                    "selfhost_inline": {"requested": True, "status": "pending"}}
            arguments = argparse.Namespace(lab=root / "uarch_lab.py", base=EXPECTED["base"])
            with mock.patch.object(compiler_compare, "run", return_value=2) as run:
                compiler_compare.build_selfhost_inline_stage1("candidate", root / "ide-cand", root / "candidate",
                                                              EXPECTED["head"], root / "work", root / "evidence", pull)
            argv = run.call_args.args[0]
            self.assertEqual(argv[1:4], ["-B", str(SELFHOST_INLINE_TOOL), "stage1"])
            self.assertEqual(argv[argv.index("--revision") + 1], EXPECTED["head"])
            self.assertEqual(pull["selfhost_inline"]["stage1_exit"], {"candidate": 2})
            with mock.patch.object(compiler_compare, "run", return_value=0) as run:
                compiler_compare.measure_selfhost_inline(arguments, root / "candidate", root / "work",
                                                         root / "evidence", pull)
            # The candidate stage 1 failed and the baseline never ran, so nothing is timed and the leg fails.
            run.assert_not_called()
            record = pull["selfhost_inline"]
            self.assertEqual(record["status"], "failed")
            self.assertEqual(record["harness_sha256"], hashlib.sha256(SELFHOST_INLINE_TOOL.read_bytes()).hexdigest())
            self.assertTrue(any("candidate stage-1 build exited 2" in error for error in record["errors"]),
                            record["errors"])

    def test_helper_builds_stage1_and_runs_the_fixed_comparison(self) -> None:
        """The real helper and uarch_lab on a fake compiler: stage 1, fixed compare argv, documents validated."""
        lab = Path(__file__).resolve().parents[1] / "uarch_lab.py"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tree = root / "tree"
            (tree / "src/buster/apps/ide").mkdir(parents=True)
            (tree / "build/generated").mkdir(parents=True)
            (tree / "src/buster/apps/ide/ide.c").write_text("int main(void) { return 0; }\n", encoding="utf-8")
            (tree / "build/generated/header.h").write_text("#define GENERATED 1\n", encoding="utf-8")
            fixture_git(tree, "init", "-q")
            fixture_git(tree, "config", "user.name", "Self Host Inline Test")
            fixture_git(tree, "config", "user.email", "selfhost-inline@example.invalid")
            fixture_git(tree, "add", "src")
            fixture_git(tree, "commit", "-qm", "tree")
            revision = fixture_git(tree, "rev-parse", "HEAD")
            ide = root / "ide"
            # A fake compiler: its stage-1 output is a copy of itself, so the stage-1 compiler is runnable too.
            ide.write_text("#!/bin/sh\nself=$0\nout=\nwhile [ $# -gt 0 ]; do [ \"$1\" = \"-o\" ] && out=$2; shift; done\n"
                           "[ -n \"$out\" ] && cp \"$self\" \"$out\" && chmod +x \"$out\"\nexit 0\n", encoding="utf-8")
            ide.chmod(0o755)
            for role in ("baseline", "candidate"):
                self.assertEqual(selfhost_inline_compare.main(["stage1", "--ide", str(ide), "--repo-root", str(tree),
                                                               "--revision", revision,
                                                               "--output", str(root / f"stage1-{role}")]), 0)
            record = json.loads((root / "stage1-baseline/stage1.json").read_text(encoding="utf-8"))
            self.assertEqual((record["status"], record["revision"]), ("complete", revision))
            self.assertEqual(record["stage1"]["sha256"], hashlib.sha256(ide.read_bytes()).hexdigest())
            self.assertEqual(selfhost_inline_compare.main(["stage1", "--ide", str(ide), "--repo-root", str(tree),
                                                           "--revision", "0" * 40, "--output", str(root / "wrong")]), 1)
            profile = dict(compiler_receipt.SELFHOST_INLINE_PROFILE, pairs=6, profile_steps=[])
            with mock.patch.object(selfhost_inline_compare, "PROFILE", profile), \
                    mock.patch.object(selfhost_inline_compare.subprocess, "run",
                                      wraps=selfhost_inline_compare.subprocess.run) as spawned:
                status = selfhost_inline_compare.main([
                    "compare", "--lab", str(lab), "--baseline-stage1", str(root / "stage1-baseline/stage1.json"),
                    "--candidate-stage1", str(root / "stage1-candidate/stage1.json"), "--repo-root", str(tree),
                    "--base-revision", revision, "--perf", "buster-no-such-perf", "--output", str(root / "compare")])
            argv = next(call.args[0] for call in spawned.call_args_list if str(lab) in call.args[0])
            self.assertEqual(argv[argv.index("--pairs") + 1], "6")
            self.assertEqual(argv[-2:], ["--", "-fcanonical-inline"])
            self.assertIn("--require-identical-output", argv)
            # Without a working perf the timed series completes but its required PMU evidence is missing.
            self.assertEqual(status, 1)
            summary_value = json.loads((root / "compare/lab/summary.json").read_text(encoding="utf-8"))
            meta = json.loads((root / "compare/lab/compare.json").read_text(encoding="utf-8"))
            labs = {key: json.loads((root / f"compare/lab/{key}/lab.json").read_text(encoding="utf-8"))
                    for key in ("a", "b")}
            sha = record["stage1"]["sha256"]
            with mock.patch.object(selfhost_inline_compare, "PROFILE", profile):
                problems = selfhost_inline_compare.validate_compare_documents(
                    summary_value, meta, labs, {"baseline": sha, "candidate": sha})
            self.assertEqual(summary_value["plan"]["complete_pairs"], 6)
            self.assertTrue(problems)
            self.assertTrue(all("counts are missing" in problem or "hardware counters" in problem or
                                "topdown metrics" in problem for problem in problems), problems)
            self.assertFalse((root / "compare/result.json").exists())


if __name__ == "__main__":
    unittest.main()
