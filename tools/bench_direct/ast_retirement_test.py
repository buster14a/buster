#!/usr/bin/env python3
"""Data-only tests of #4058's fixed retirement profile and raw publisher replay.

Fixtures are synthetic raw observations, never host measurements. They retain
every pair/counter, reference chunk, transcript and verbose probe so a claimed
summary cannot stand in for evidence. Map: evidence, mutate_document,
RetirementPublisherTest, RetirementPublicationPolicyTest.
"""
from __future__ import annotations

import argparse
import base64
import copy
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
import urllib.parse
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ast_retirement_acceptance as retirement
import compiler_compare as compare
import compiler_publish as publisher
import compiler_receipt as receipt
import compiler_test as ordinary
from sampling_qualification_receipt import _lab

BASE, HEAD = "b" * 40, "a" * 40
CPU_MODEL = "AMD Ryzen 7 9700X 8-Core Processor"
BINARIES = {role: {"sha256": sha, "size_bytes": 16, "revision": revision}
            for role, sha, revision in (("baseline", ordinary.A256, BASE), ("candidate", ordinary.B256, HEAD))}
SOURCE_BYTES = b"int frozen_source(void) { return 0; }\n"


def encoded(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode("utf-8")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_binding() -> dict:
    return {"revision": BASE, "frozen_input_tree": {
                name: {"sha256": "3" * 64, "files": 1, "bytes": 16} for name in ("src", "build/generated")},
            "unity": {"path": "src/buster/apps/ide/ide.c", "sha256": digest(SOURCE_BYTES), "size_bytes": len(SOURCE_BYTES)},
            "corpus": {name: {"path": f"{receipt.AST_RETIREMENT_INPUT_DIRECTORY}/{name}.c",
                              "sha256": digest(SOURCE_BYTES), "size_bytes": len(SOURCE_BYTES)} for name in receipt.AST_RETIREMENT_CORPUS},
            "generator": {"revision": BASE, "sha256": "6" * 64, "size_bytes": 16}}


@lru_cache(maxsize=1)
def paired_observations() -> tuple:
    records, files, loaded = [], {}, []
    for pair, key in _lab.abba_schedule(receipt.AST_RETIREMENT_PROFILE["pairs"]):
        factor = 1.0 if key == "a" else 0.95
        record = {"pair": pair, "order": _lab.abba_order(pair), "variant": key, "exit": 0,
                  "identical": True, "span_s": 2.0 * factor, "maxrss_bytes": 1000000,
                  "harness_rss_bytes": 1000, "wrapper_rss_bytes": 1000, "cpu_s": 2.0 * factor, "counters": True}
        counter = (f"{2000 * factor},msec,task-clock,2000000000,100.00,,\n"
                   f"{200000000 * factor},,cycles:u,2000000000,100.00,,\n"
                   f"{100000000 * factor},,instructions:u,2000000000,100.00,,\n"
                   "100,,branch-misses:u,2000000000,100.00,,\n"
                   "10,,page-faults,2000000000,100.00,,\n"
                   "10,,minor-faults,2000000000,100.00,,\n"
                   "0,,major-faults,2000000000,100.00,,\n").encode("ascii")
        files[f"pairs/{pair:04d}-{key}.csv"] = counter
        rows = _lab.parse_stat(counter.decode("ascii"))
        loaded.append(dict(record, values=_lab.stat_values(rows), lines=_lab.stat_lines(rows),
                           metrics=_lab.parse_cc_metrics(""), cc_wall_s=None, task_s=_lab.task_seconds(rows)))
        records.append(record)
    pairs = _lab.complete_pairs(loaded)
    metrics = {key: dict(_lab.compare_series([(pair["metrics_a"][key], pair["metrics_b"][key]) for pair in pairs],
                unit, direction, _lab.DEFAULT_SEED, time_metric=unit == "s", floor=_lab.metric_floor(key, 0.5)), label=label)
               for key, unit, direction, label in _lab.COMPARE_METRICS}
    return records, files, metrics, _lab.compare_checks(pairs), _lab.compare_verdict(metrics, None, 0.5)


def probe_documents(cell: str, source: dict, reference: dict) -> tuple:
    command = receipt.ast_retirement_cell_command(cell)
    if receipt.AST_RETIREMENT_CELLS[cell]["operation"] == "object":
        command += ["-o", "OUT"]
    descriptor = source["unity"] if receipt.AST_RETIREMENT_CELLS[cell]["workload"] == "unity" else source["corpus"][receipt.AST_RETIREMENT_CELLS[cell]["workload"]]
    probe = {"command": [*command, "-v"], "source_sha256": descriptor["sha256"]}
    logs = {}
    for key, role in (("a", "baseline"), ("b", "candidate")):
        facts = 0 if receipt.AST_RETIREMENT_CELLS[cell]["operation"] == "syntax-only" else 200
        lines = {"C_AST": ["C_AST nodes=10 tokens=20 build_ns=1 retained_bytes=1000 transient_high_water=100 "
                           "column_copy_bytes=0 finalize_child_entries=1 layout=compact"],
                 "C_AST_FACTS": [f"C_AST_FACTS nodes=10 retained_bytes={facts} regions=0 candidates=0 tree=0 machine=0 "
                                 "completed=0 missing=0 unspanned=0 invalid=0 appends=0 busy=0 bound=0 bit_fields=0 "
                                 "strings=0 uncovered=0 outside=0 stale=0 completion_queries=0 recipes=0 "
                                 "builtin_identities=0 function_names=0 type_names=0 typed_type_names=0 missing_type_names=0"],
                 "C_SYMBOL_BINDINGS": [] if key == "a" else ["C_SYMBOL_BINDINGS rows=1 retained_bytes=16 copy_bytes=0"]}
        probe[key] = {"exit": 0, "sha256": BINARIES[role]["sha256"], "size_bytes": BINARIES[role]["size_bytes"],
                      "lines": lines,
                      "output_sha256": reference[key]["sha256"] if receipt.AST_RETIREMENT_CELLS[cell]["operation"] == "object" else None,
                      "output_size_bytes": reference[key]["size_bytes"] if receipt.AST_RETIREMENT_CELLS[cell]["operation"] == "object" else None}
        logs[key] = ("\n".join(line for population in lines.values() for line in population) + "\n").encode("ascii")
    return probe, logs


def evidence() -> tuple:
    source, files, bundle, cells, probes = source_binding(), {}, {}, {}, {}
    bundle["source"] = source
    for name in ("unity", *receipt.AST_RETIREMENT_CORPUS):
        files[f"ast_retirement_acceptance/inputs/{name}.c"] = SOURCE_BYTES
    records, counter_files, metrics, checks, verdict = paired_observations()
    for cell in receipt.AST_RETIREMENT_CELLS:
        prefix = f"ast_retirement_acceptance/{cell}/"
        command = receipt.ast_retirement_cell_command(cell)
        if receipt.AST_RETIREMENT_CELLS[cell]["operation"] == "object":
            command += ["-o", "OUT"]
        config = {"ast_retirement_cell": cell, "extra_by_variant": {"a": [], "b": []},
                  "canonical_inline_pair": False, "ast_pilot_pair": None, "constant_reuse_pair": None,
                  "process_floor_pair": None, "command": " ".join(["IDE", *command]), "pairs": 20,
                  "warmups": 1, "cpu": 2, "extra": [], "profile_steps": [], "fresh_copy": True,
                  "seed": _lab.DEFAULT_SEED, "min_effect_percent": 0.5, "sudo": False, "repo_root": "/repo"}
        plan = {"pairs": 20, "reason": "--pairs 20", "order": "ABBA", "fresh_copy": True}
        phase = {"enabled": False, "reason": "retirement profile records components only in untimed probes"}
        labs, variants, summary_variants, reference = {}, {}, {}, {}
        for key, role in (("a", "baseline"), ("b", "candidate")):
            path = "/bin/ide-" + role
            variants[key] = {"role": role, "ide": path, **BINARIES[role]}
            labs[key] = {"config": {"ast_retirement_cell": cell, "extra": [], "cpu": 2, "ide": path,
                                   "command": " ".join([path, *command]), "fresh_copy": True, "repo_root": "/repo", "role": role},
                         "capabilities": {"perf_stat": {"usable": True, "reason": "usable"}},
                         "collection": {"metrics_out": False}}
            summary_variants[role] = {"path": path, **BINARIES[role], "runs": 20, "failed": 0,
                                      "identical_runs": 20, "deterministic": True, "metrics_out_enabled": False}
            environment = {"cpu_model": CPU_MODEL, "cpu": 2, "ide_sha256": BINARIES[role]["sha256"], "git_revision": BASE}
            files[prefix + key + "/env/env.json"] = encoded(environment)
            image = b"identical object" if receipt.AST_RETIREMENT_CELLS[cell]["operation"] == "object" else b""
            chunks = [image, b""]
            reference[key] = {"size_bytes": len(image), "sha256": digest(image), "chunks": [
                {"path": f"reference.{index:03d}.bin.gz", "compression": "gzip-v1", "size_bytes": len(chunk),
                 "sha256": digest(chunk), "stored_size_bytes": len(retirement.compress_reference(chunk)),
                 "stored_sha256": digest(retirement.compress_reference(chunk))}
                for index, chunk in enumerate(chunks)]}
            for index, chunk in enumerate(chunks):
                files[prefix + f"reference.{index:03d}.bin.gz"] = retirement.compress_reference(chunk)
            files[prefix + key + "/reference.diagnostics"] = b""
        meta = {"version": 1, "mode": "compare", "config": config, "plan": plan, "variants": variants, "phase_metrics": phase}
        summary = {"schema": _lab.COMPARE_SCHEMA, "plan": dict(plan, seed=_lab.DEFAULT_SEED, confidence=_lab.CONFIDENCE,
                       bootstrap_resamples=_lab.BOOTSTRAP_RESAMPLES, complete_pairs=20), "method": _lab.COMPARE_METHOD,
                   "command": config["command"], "repo_root": "/repo", "cpu": 2, "host": {"cpu_model": CPU_MODEL},
                   **summary_variants, "metrics": metrics, "checks": checks, "verdict": verdict, "phases": None,
                   "phase_metrics": phase, "counters": {"perf_stat": True, "reason": "usable"},
                   "outputs_identical": True, "diagnostics_identical": True}
        for key, value in (("summary", summary), ("meta", meta), ("a", labs["a"]), ("b", labs["b"]), ("reference", reference)):
            bundle[f"{cell}_{key}"] = copy.deepcopy(value)
        files[prefix + "pairs.json"] = encoded(records)
        files.update({prefix + path: data for path, data in counter_files.items()})
        probe, logs = probe_documents(cell, source, reference)
        bundle[f"{cell}_probe"] = probe
        for key, data in logs.items():
            files[f"ast_retirement_acceptance/probe/{cell}/{key}.log"] = data
        cells[cell] = retirement.cell_record(summary)
        probes[cell] = retirement.validate_probe(cell, probe, BINARIES, source, logs)
    acceptance = {"schema": receipt.AST_RETIREMENT_SCHEMA, "status": "complete", "profile": receipt.AST_RETIREMENT_PROFILE,
                  "base_revision": BASE, "head_revision": HEAD, "binaries": BINARIES, "source": source,
                  "cpu_model": CPU_MODEL, "cells": cells, "probes": probes, "elapsed_seconds": 60.0}
    bundle["acceptance"] = acceptance
    for key, path in publisher.AST_RETIREMENT_BUNDLE_FILES.items():
        files[path] = encoded(bundle[key])
    bundle["files"] = files
    record = {"requested": True, "request_line": receipt.AST_RETIREMENT_REQUEST_LINE, "profile": receipt.AST_RETIREMENT_PROFILE,
              "status": "complete", "exit": 0, "summary": acceptance}
    return copy.deepcopy(record), copy.deepcopy(bundle)


def mutate_document(bundle: dict, key: str, change) -> None:
    change(bundle[key])
    bundle["files"][publisher.AST_RETIREMENT_BUNDLE_FILES[key]] = encoded(bundle[key])


class RetirementPublisherTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.good = evidence()

    def validate(self, record: dict, bundle: dict, **change) -> list:
        arguments = {"base": BASE, "head": HEAD, "binaries": BINARIES, "cpu_model": CPU_MODEL}
        arguments.update(change)
        return publisher.validate_ast_retirement_bundle(record, bundle, **arguments)

    def test_complete_raw_population_replays_all_twenty_cells(self) -> None:
        record, bundle = copy.deepcopy(self.good)
        self.assertEqual(self.validate(record, bundle), [])
        self.assertEqual(len(record["summary"]["cells"]), 20)

    def test_receipt_head_base_binary_profile_and_host_are_bound(self) -> None:
        for change in ({"base": "c" * 40}, {"head": "d" * 40}, {"cpu_model": "AMD Ryzen 9 7940HS"},
                       {"binaries": dict(BINARIES, candidate=dict(BINARIES["candidate"], sha256="7" * 64))}):
            with self.subTest(change=change):
                self.assertTrue(self.validate(*copy.deepcopy(self.good), **change))

    def test_raw_variant_manifest_must_bind_the_timed_role_hash_size_and_path(self) -> None:
        for field, value in (("sha256", "7" * 64), ("size_bytes", 17), ("ide", "/other/ide"), ("role", "candidate")):
            record, bundle = copy.deepcopy(self.good)
            mutate_document(bundle, "unity-syntax-only_meta", lambda d: d["variants"]["a"].update({field: value}))
            with self.subTest(field=field):
                self.assertTrue(self.validate(record, bundle))

    def test_raw_pair_counter_reference_and_probe_tampering_is_rejected(self) -> None:
        cell = "unity-syntax-only"
        prefix = f"ast_retirement_acceptance/{cell}/"
        mutations = {
            "missing pairs": lambda b: b["files"].pop(prefix + "pairs.json"),
            "counter NA": lambda b: b["files"].__setitem__(prefix + "pairs/0001-a.csv", b"<not counted>,,instructions:u,,,\n"),
            "wrong counter privilege": lambda b: b["files"].__setitem__(prefix + "pairs/0001-a.csv", b"100,,instructions,,,\n"),
            "counter privilege alias": lambda b: b["files"].__setitem__(prefix + "pairs/0001-a.csv",
                b["files"][prefix + "pairs/0001-a.csv"] + b"100000000.0,,instructions:k,2000000000,100.00,,\n"),
            "counter changes inference": lambda b: b["files"].__setitem__(prefix + "pairs/0001-a.csv",
                b["files"][prefix + "pairs/0001-a.csv"].replace(b"100000000.0,,instructions:u", b"200000000.0,,instructions:u")),
            "missing reference chunk": lambda b: b["files"].pop(prefix + "reference.001.bin.gz"),
            "different raw diagnostics": lambda b: b["files"].__setitem__(prefix + "a/reference.diagnostics", b"changed diagnostics"),
            "missing probe log": lambda b: b["files"].pop(f"ast_retirement_acceptance/probe/{cell}/a.log"),
            "probe log differs": lambda b: b["files"].__setitem__(f"ast_retirement_acceptance/probe/{cell}/b.log", b"C_AST_RETAINED tree_bytes=1000\n"),
        }
        for label, change in mutations.items():
            record, bundle = copy.deepcopy(self.good)
            change(bundle)
            with self.subTest(label=label):
                self.assertTrue(self.validate(record, bundle))

    def test_wrong_operation_flags_affinity_instrumentation_and_source_are_rejected(self) -> None:
        key = "unity-syntax-only_meta"
        for label, change in {
            "other cell": lambda d: d["config"].update(ast_retirement_cell="unity-object"),
            "extra flag": lambda d: d["config"].update(extra=["-fc-ast-pilot"]),
            "other CPU": lambda d: d["config"].update(cpu=3),
            "missing warmup": lambda d: d["config"].update(warmups=0),
            "stale source": lambda d: d["config"].update(repo_root="/other/repo"),
            "input-dependent pair count": lambda d: d["config"].update(pairs=None),
        }.items():
            record, bundle = copy.deepcopy(self.good)
            mutate_document(bundle, key, change)
            with self.subTest(label=label):
                self.assertTrue(self.validate(record, bundle))
        record, bundle = copy.deepcopy(self.good)
        mutate_document(bundle, "unity-syntax-only_a", lambda d: d["collection"].update(metrics_out=True))
        self.assertTrue(self.validate(record, bundle))

    def test_a_forged_summary_and_matching_receipt_cannot_replace_raw_measurements(self) -> None:
        record, bundle = copy.deepcopy(self.good)
        cell = "unity-syntax-only"
        mutate_document(bundle, cell + "_summary", lambda d: d["metrics"]["wall"].update(ratio=0.50))
        bundle["acceptance"]["cells"][cell] = retirement.cell_record(bundle[cell + "_summary"])
        bundle["files"][publisher.AST_RETIREMENT_BUNDLE_FILES["acceptance"]] = encoded(bundle["acceptance"])
        record["summary"] = copy.deepcopy(bundle["acceptance"])
        problems = self.validate(record, bundle)
        self.assertTrue(any("raw" in problem and "inference" in problem for problem in problems), problems)

    def test_nonfinite_missing_and_nonpositive_required_metrics_cannot_be_complete(self) -> None:
        for value in (None, 0, -1, True, float("inf"), float("nan")):
            record, bundle = copy.deepcopy(self.good)
            mutate_document(bundle, "unity-syntax-only_summary", lambda d: d["metrics"]["instructions"].update(ratio=value))
            with self.subTest(value=value):
                self.assertTrue(self.validate(record, bundle))

    def test_stale_source_bytes_and_raw_host_cannot_follow_a_plausible_summary(self) -> None:
        for path, data in (("ast_retirement_acceptance/inputs/unity.c", b"int different_source;\n"),
                           ("ast_retirement_acceptance/inputs/tiny_startup.c", b"int different_corpus;\n"),
                           ("ast_retirement_acceptance/unity-syntax-only/a/env/env.json",
                            encoded({"cpu_model": "AMD Ryzen 9 7940HS", "cpu": 2,
                                     "ide_sha256": BINARIES["baseline"]["sha256"], "git_revision": BASE}))):
            record, bundle = copy.deepcopy(self.good)
            bundle["files"][path] = data
            with self.subTest(path=path):
                self.assertTrue(self.validate(record, bundle))

    def test_failed_out_of_order_and_missing_rss_raw_pairs_are_rejected(self) -> None:
        path = "ast_retirement_acceptance/unity-syntax-only/pairs.json"
        for change in (lambda rows: rows[0].update(exit=1), lambda rows: rows[0].update(pair=2),
                       lambda rows: rows[0].update(maxrss_bytes=0), lambda rows: rows.pop()):
            record, bundle = copy.deepcopy(self.good)
            rows = json.loads(bundle["files"][path])
            change(rows)
            bundle["files"][path] = encoded(rows)
            self.assertTrue(self.validate(record, bundle))

    def test_object_probe_must_reproduce_its_timed_reference(self) -> None:
        record, bundle = copy.deepcopy(self.good)
        mutate_document(bundle, "unity-object_probe", lambda p: p["b"].update(output_sha256="7" * 64))
        self.assertTrue(self.validate(record, bundle))
        record, bundle = copy.deepcopy(self.good)
        mutate_document(bundle, "unity-syntax-only_probe", lambda p: p["a"].update(output_size_bytes=16))
        self.assertTrue(self.validate(record, bundle))

    def test_shared_compressed_reference_rejects_corruption_and_unbounded_expansion(self) -> None:
        cell = "unity-object"
        prefix = f"ast_retirement_acceptance/{cell}/"
        path = prefix + "reference.000.bin.gz"
        for case in ("stored corruption", "gzip CRC", "truncated gzip", "excess raw bytes", "trailing bytes",
                     "second gzip stream", "raw hash", "whole hash", "other arm"):
            with self.subTest(case=case):
                _, bundle = copy.deepcopy(self.good)
                raw = bundle["files"][path]
                if case == "stored corruption":
                    bundle["files"][path] = raw[:-1] + bytes([raw[-1] ^ 1])
                elif case == "other arm":
                    bundle[f"{cell}_reference"]["b"]["sha256"] = "f" * 64
                elif case == "raw hash":
                    for arm in ("a", "b"):
                        bundle[f"{cell}_reference"][arm]["chunks"][0]["sha256"] = "f" * 64
                elif case == "whole hash":
                    for arm in ("a", "b"):
                        bundle[f"{cell}_reference"][arm]["sha256"] = "f" * 64
                else:
                    changed = {"gzip CRC": raw[:-8] + bytes([raw[-8] ^ 1]) + raw[-7:],
                               "truncated gzip": raw[:-1], "excess raw bytes": retirement.compress_reference(b"x" * 1024),
                               "trailing bytes": raw + b"trailing", "second gzip stream": raw + retirement.compress_reference(b"")}[case]
                    bundle["files"][path] = changed
                    for arm in ("a", "b"):
                        chunk = bundle[f"{cell}_reference"][arm]["chunks"][0]
                        chunk.update(stored_size_bytes=len(changed), stored_sha256=digest(changed))
                with self.assertRaises(ValueError):
                    publisher.ast_retirement_reference(cell, bundle)
    def test_retained_aliases_do_not_double_count_or_invent_binding_bytes(self) -> None:
        record, bundle = copy.deepcopy(self.good)
        cell = "unity-syntax-only"
        log = f"ast_retirement_acceptance/probe/{cell}/a.log"
        bundle["files"][log] += b"C_AST_RETAINED tree_bytes=1000 span_bytes=5000\n"
        self.assertEqual(self.validate(record, bundle), [])
        retained = record["summary"]["probes"][cell]["baseline"]
        self.assertEqual(retained["tree_retained_bytes"], 1000)
        self.assertEqual(retained["fact_retained_bytes"], 0)
        self.assertIsNone(retained["symbol_binding_retained_bytes"])
        self.assertEqual(retained["symbol_binding_status"], "unreported")
        self.assertTrue(retained["symbol_binding_reason"])

    def test_artifact_reader_retains_exact_profile_bytes_and_refuses_bad_documents(self) -> None:
        record, bundle = copy.deepcopy(self.good)
        outer = ordinary.receipt()
        outer["ast_retirement_acceptance"] = record
        members = dict(bundle["files"], **{"receipt.json": encoded(outer), "lab/summary.json": encoded(ordinary.summary()),
            "throughput/summary.json": encoded(ordinary.corpus()["summary"]),
            "throughput/metadata.json": encoded(ordinary.corpus()["metadata"]), receipt.EVIDENCE_SEAL: b"synthetic seal\n"})
        unused_receipt, unused_summary, problem, unused_artifact, throughput = publisher.read_evidence(
            ordinary.FakeApi(ordinary.archive(members)), "92", "buster-9700x-compiler-x-1")
        self.assertEqual(problem, "")
        self.assertEqual(self.validate(record, throughput["ast_retirement_bundle"]), [])
        for broken in (b"{", b'{"revision": "first", "revision": "second"}'):
            damaged = dict(members, **{publisher.AST_RETIREMENT_BUNDLE_FILES["source"]: broken})
            read = publisher.read_evidence(ordinary.FakeApi(ordinary.archive(damaged)), "92", "buster-9700x-compiler-x-1")
            self.assertIn("source.json", read[2])


def publication_fixture() -> tuple:
    record, bundle = evidence()
    expected = dict(ordinary.EXPECTED, mode="pull", ref="refs/pull/7/head")
    current = ordinary.receipt()
    current.update(mode="pull", identity=expected, binaries=BINARIES, ast_retirement_acceptance=record)
    verified = {"state": "verified", "inventory": receipt.EVIDENCE_INVENTORY, "members": 800,
                "payload_bytes": 1000, "manifest_sha256": "7" * 64}
    current["evidence_bundle"] = dict(verified, state="sealed")
    throughput = dict(ordinary.corpus(), ast_retirement_bundle=bundle)
    return expected, current, throughput, verified


class RetirementPublicationPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.good = publication_fixture()

    def decide(self, expected: dict, current: dict, throughput: dict, verified: dict | None) -> tuple:
        return publisher.decide(expected, True, "success", current, ordinary.summary(), "", throughput,
                                evidence_bundle=verified)

    def test_publication_is_pull_only_sealed_and_report_only(self) -> None:
        expected, current, throughput, verified = copy.deepcopy(self.good)
        self.assertEqual(self.decide(expected, current, throughput, verified)[0], "success")
        self.assertEqual(ordinary.summary()["verdict"]["outcome"], "slower")
        for envelope in (None, {"state": "legacy-unsealed"}, {"state": "unsealed"}, {"state": "refused"}):
            with self.subTest(envelope=envelope):
                self.assertEqual(self.decide(expected, current, throughput, envelope)[0], "failure")
        expected["mode"] = current["mode"] = current["identity"]["mode"] = "main"
        self.assertEqual(self.decide(expected, current, throughput, verified)[0], "failure")

    def test_other_long_profiles_and_scaling_do_not_share_this_request(self) -> None:
        for key in ("ast_pilot_acceptance", "constant_reuse_acceptance", "process_floor", "throughput_real_source",
                    "production_profile", "compiler_probes", "frontend_selector", "frontend_nesting", "wild_linker",
                    "link_tls", "selfhost_inline", "scaling_profile"):
            expected, current, throughput, verified = copy.deepcopy(self.good)
            current[key] = {}
            with self.subTest(key=key):
                decision = self.decide(expected, current, throughput, verified)
                self.assertEqual(decision[0], "failure")
                self.assertTrue(any("ast-retirement acceptance cannot be combined" in reason for reason in decision[2]))

    def test_request_is_fresh_once_against_every_git_parent(self) -> None:
        expected, current, throughput, unused_verified = copy.deepcopy(self.good)
        line = receipt.AST_RETIREMENT_REQUEST_LINE + "\n"
        parents = ("c" * 40, "d" * 40)

        class Api:
            def __init__(self, counts: tuple[int, int], head_count: int = 1):
                self.contents = {HEAD: (line * head_count).encode("ascii"),
                                 **{parent: (line * count).encode("ascii") for parent, count in zip(parents, counts)}}

            def request(self, path: str) -> dict:
                if path == "/commits/" + HEAD:
                    result = {"sha": HEAD, "parents": [{"sha": parent} for parent in parents]}
                else:
                    revision = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query)["ref"][0]
                    raw = self.contents[revision]
                    result = {"type": "file", "encoding": "base64", "size": len(raw),
                              "content": base64.b64encode(raw).decode("ascii")}
                return result

        self.assertEqual(publisher.verify_analyzer_request_freshness(Api((0, 0)), expected, current, throughput), "")
        for counts, head_count in (((1, 0), 1), ((0, 1), 1), ((1, 1), 1), ((0, 0), 2)):
            with self.subTest(counts=counts, head_count=head_count):
                self.assertIn("freshly added once", publisher.verify_analyzer_request_freshness(
                    Api(counts, head_count), expected, current, throughput))
        current.pop("ast_retirement_acceptance")
        self.assertIn("has no ast-retirement acceptance record", publisher.verify_analyzer_request_freshness(
            Api((0, 0)), expected, current, throughput))


class RetirementProducerRoutingTest(unittest.TestCase):
    def test_actual_reference_transport_crosses_raw_chunk_boundary_without_raising_limits(self) -> None:
        data = b"B" * (retirement.CHUNK_BYTES + 17)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for arm in ("a", "b"):
                (root / arm).mkdir()
                (root / arm / "reference.exe").write_bytes(data)
            manifest = retirement.split_reference(root)
            self.assertEqual(manifest["a"], manifest["b"])
            self.assertEqual(manifest["a"]["size_bytes"], len(data))
            self.assertEqual([chunk["size_bytes"] for chunk in manifest["a"]["chunks"]], [retirement.CHUNK_BYTES, 17])
            self.assertEqual(len(list(root.glob("*.bin.gz"))), 2)
            self.assertEqual(retirement.compress_reference(b"same raw bytes"), retirement.compress_reference(b"same raw bytes"))
            prefix = "ast_retirement_acceptance/unity-object/"
            files = {prefix + chunk["path"]: (root / chunk["path"]).read_bytes() for chunk in manifest["a"]["chunks"]}
            files.update({prefix + arm + "/reference.diagnostics": b"" for arm in ("a", "b")})
            bundle = {"files": files, "unity-object_reference": manifest,
                      "unity-object_summary": {"outputs_identical": True, "diagnostics_identical": True}}
            self.assertEqual(publisher.ast_retirement_reference("unity-object", bundle), manifest)
            (root / "b" / "reference.exe").write_bytes(b"different compiler output")
            with self.assertRaises(RuntimeError):
                retirement.split_reference(root)
            with (root / "a" / "reference.exe").open("wb") as stream:
                stream.truncate(2 * retirement.CHUNK_BYTES + 1)
            with self.assertRaises(RuntimeError):
                retirement.split_reference(root)

    def test_sealed_export_cannot_complete_without_every_raw_counter(self) -> None:
        _, bundle = evidence()
        counters = {f"{cell}/pairs/{pair:04d}-{arm}.csv" for cell in receipt.AST_RETIREMENT_CELLS
                    for pair in range(1, 21) for arm in ("a", "b")}
        self.assertEqual(len(counters), 800)
        self.assertTrue(counters.issubset(compare.AST_RETIREMENT_REQUIRED))
        missing = "unity-syntax-only/pairs/0001-a.csv"
        for case in ("complete", "absent", "budget"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                work, exported = root / "work", root / "evidence"
                source = work / "ast-retirement-acceptance"
                exported.mkdir()
                for name, data in bundle["files"].items():
                    relative = name.removeprefix("ast_retirement_acceptance/")
                    if case == "absent" and relative == missing:
                        continue
                    target = source / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                record = {"requested": True, "status": "pending"}
                current = {"timings": {}, "binaries": BINARIES, "ast_retirement_acceptance": record}
                arguments = argparse.Namespace(lab=root / "trusted-lab.py", base=BASE, head=HEAD)
                with mock.patch.object(compare, "run", return_value=0), mock.patch.object(compare, "mark"), \
                        mock.patch.object(compare, "EVIDENCE_DEFER_ALL", True), \
                        mock.patch.object(compare, "EVIDENCE_DEFERRED_OPTIONAL", []), \
                        mock.patch.object(compare, "EVIDENCE_OMISSION_LEDGER", []), \
                        mock.patch.object(compare, "EVIDENCE_FILE_COUNT", 0 if case == "budget" else compare.EVIDENCE_FILE_COUNT):
                    compare.measure_ast_retirement(arguments, root, root / "bin", work, exported, current, None)
                if case == "complete":
                    self.assertEqual(record["status"], "complete")
                    self.assertEqual(record["errors"], [])
                    self.assertTrue(all((exported / "ast_retirement_acceptance" / name).is_file() for name in counters))
                else:
                    self.assertEqual(record["status"], "failed")
                    self.assertTrue(any(f"{missing} not exported" in error for error in record["errors"]))
                    self.assertFalse((exported / "ast_retirement_acceptance" / missing).exists())

    def test_real_git_merge_parents_cannot_replay_or_duplicate_a_request(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", directory], check=True)
            subprocess.run(["git", "config", "user.name", "retirement fixture"], cwd=root, check=True)
            subprocess.run(["git", "config", "user.email", "fixture@example.invalid"], cwd=root, check=True)
            request = root / receipt.ANALYZER_REQUEST_PATH
            request.parent.mkdir(parents=True)

            def tree(count: int) -> str:
                request.write_text(receipt.AST_RETIREMENT_REQUEST_LINE + "\n" if count == 1 else
                                   (receipt.AST_RETIREMENT_REQUEST_LINE + "\n") * count)
                subprocess.run(["git", "add", receipt.ANALYZER_REQUEST_PATH], cwd=root, check=True)
                return subprocess.check_output(["git", "write-tree"], cwd=root, text=True).strip()

            def commit(tree_id: str, parents: tuple[str, ...], message: str) -> str:
                command = ["git", "commit-tree", tree_id, *[value for parent in parents for value in ("-p", parent)]]
                return subprocess.check_output(command, input=message + "\n", cwd=root, text=True).strip()

            empty = tree(0)
            first = commit(empty, (), "first parent")
            other = commit(empty, (), "second parent")
            fresh = commit(tree(1), (first, other), "fresh merge request")
            self.assertEqual(compare.ast_retirement_request_selection(root, fresh), (True, ""))
            inherited = commit(tree(1), (fresh,), "unrelated head")
            self.assertEqual(compare.ast_retirement_request_selection(root, inherited), (False, ""))
            mixed = commit(tree(1), (fresh, other), "merge inherited marker")
            self.assertIn("every head parent", compare.ast_retirement_request_selection(root, mixed)[1])
            duplicate = commit(tree(2), (first, other), "duplicate merge marker")
            self.assertIn("every head parent", compare.ast_retirement_request_selection(root, duplicate)[1])

    def test_native_and_receipt_command_populations_agree_without_legacy_flag_pair(self) -> None:
        self.assertEqual(set(_lab.AST_RETIREMENT_CELLS), set(receipt.AST_RETIREMENT_CELLS))
        self.assertEqual(len(receipt.AST_RETIREMENT_CELLS), 20)
        for cell in receipt.AST_RETIREMENT_CELLS:
            with self.subTest(cell=cell):
                self.assertEqual(_lab.ast_retirement_command(cell), receipt.ast_retirement_cell_command(cell))
                self.assertFalse(any("ast-pilot" in flag for flag in _lab.ast_retirement_command(cell)))


if __name__ == "__main__":
    unittest.main()
