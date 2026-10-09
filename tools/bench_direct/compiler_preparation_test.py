#!/usr/bin/env python3
"""Synthetic, data-only native preparation/qualification contract tests."""
from __future__ import annotations
import copy
import hashlib
import json
import unittest
import compiler_preparation as contract
from compiler_test import summary, corpus, A256, B256

C256, D256, H256, T256, G256 = ("3" * 64, "4" * 64, "5" * 64, "6" * 64, "7" * 64)
CONFIG = "c" * 64
EXPECTED = {"base": "b" * 40, "base_tree": "e" * 40, "head": "a" * 40, "head_tree": "d" * 40,
            "root": "/checkout", "output": "/qualification", "trusted_lab": "/trusted/tools/uarch_lab.py",
            "python": "/usr/bin/python3", "trusted_lab_sha256": "8" * 64, "python_sha256": "9" * 64}
CACHE = b"BUSTER_INCLUDE_TESTS:BOOL=OFF\nCMAKE_HOME_DIRECTORY:INTERNAL=/checkout\n"
BUILD = ("-checkout", "-generate", "-build", "-build-verify")
CONTROLS = {"legacy": ("ab", "immutable-aa"), "snapshot": ("ab", "immutable-aa", "cross-build-aa")}
MEASURE = ("-lab-binaries-before", "-lab", "-lab-binaries-after", "-throughput-binaries-before",
           "-throughput", "-throughput-binaries-after", "-post")


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def encoded(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def native_phases(arm, qualification=True, count=2):
    result = ["pins"] + (["secondary-pins"] if count == 3 else [])
    result += ["reset-checkout", "reset-tracked-source", "reset-build-cache"]
    result += ["baseline" + suffix for suffix in BUILD] + ["baseline-freeze"]
    if arm == "snapshot":
        result += ["closure-snapshot"]
    result += ["candidate" + suffix for suffix in BUILD] + ["candidate-freeze"]
    if count == 3:
        result += ["candidate2" + suffix for suffix in BUILD] + ["candidate2-freeze"]
    if arm == "legacy":
        result += ["baseline-closure" + suffix for suffix in BUILD] + ["baseline-corpus-prepare"]
    else:
        result += ["baseline-restore-checkout", "closure-restore", "closure-verify"]
    result += ["baseline-corpus-verify", "prepared"]
    if qualification:
        if arm == "snapshot":
            result += ["matched-frozen-workload"]
        for name in CONTROLS[arm]:
            result += [name + suffix for suffix in MEASURE]
    return result


def inventory(expected, arm, baseline):
    rows = []
    def row(scope, path, value=T256, mode=0o755, size=1):
        rows.append(f"{scope}\tF\t{mode}\t{123 if arm == 'legacy' else 456}\t0\t{size}\t{value}\t{path}")
    for path in ("build.c", "build.sh", "tools/bootstrap_driver.sh"):
        row("source", path, mode=0o755 if path == "build.sh" else 0o644)
    row("build", "CMakeCache.txt", digest(CACHE), 0o644, len(CACHE))
    row("build", "Release/ide", baseline)
    row("build", "throughput-tools/throughput", H256)
    row("build", "generated/fixture.h", T256, 0o644)
    row("build", "compile_commands.json", T256, 0o644)
    row("build", "build-Release.ninja", T256, 0o644)
    artifact = f"posix/{CONFIG}/driver-{arm}"
    row("bootstrap", artifact, G256)
    row("bootstrap", artifact + ".complete", T256, 0o644)
    bindings = [f"binding\tbootstrap_config\t{CONFIG}", f"binding\tbootstrap_marker\t{artifact}.complete",
                f"binding\tbootstrap_artifact\t{artifact}"]
    for name in ("CMAKE_C_COMPILER", "CMAKE_LINKER", "CMAKE_MAKE_PROGRAM", "clang", "cmake", "ninja", "tcc"):
        row("tool", name)
        bindings.append(f"binding\t{name}\t/usr/bin/{name}")
    bindings += ["binding\tld\tabsent", "binding\tld.lld\tabsent", "binding\tmold\tabsent",
                 "binding\tresource\t/usr/lib/clang/include"]
    row("resource", "stddef.h", T256, 0o644)
    total = sum(int(line.split("\t")[5]) for line in rows)
    return ("\n".join(["BUSTER_COMPILER_CLOSURE_V1", "root\t" + expected["root"], "base\t" + expected["base"],
                       "tree\t" + expected["base_tree"], *rows, *bindings, f"END\t{len(rows)}\t{total}"]) + "\n").encode()


def ledger(prepared, phases, arm, qualification):
    rows = ["BUSTER_COMPILER_PREPARATION_LEDGER_V1", "root\t/checkout", "policy\t" + prepared["policy"],
            *(key + "\t" + prepared[key] for key in contract.IDENTITY)]
    for ordinal, phase in enumerate(phases, 1):
        if qualification:
            start = (1000 if arm == "legacy" else 2000) if ordinal == 1 else \
                    (3000 if arm == "legacy" else 5000) + ordinal * 20
        else:
            start = 1000 + ordinal * 20
        rows.append(f"start\t{ordinal}\t{phase}\t{start}")
        if phase == "secondary-pins":
            rows += ["secondary_head\t" + prepared["secondary_head"], "secondary_tree\t" + prepared["secondary_tree"]]
        rows.append(f"finish\t{ordinal}\t{phase}\t{start + 10}\t10\tcomplete\t0")
    return ("\n".join(rows) + "\n").encode()


def fixture(qualification=True, count=2, policy="snapshot-v1"):
    expected = dict(EXPECTED)
    if not qualification:
        expected.update(policy=policy, arm_count=count, output="/prepared")
        if count == 3:
            expected.update(secondary_head="f" * 40, secondary_tree="0" * 40)
    arms = ("legacy", "snapshot") if qualification else (("legacy",) if policy == "legacy-rebuild" else ("snapshot",))
    bundles = {}
    for arm in arms:
        baseline = A256 if arm == "legacy" else C256
        prepared = {"schema": contract.PREPARATION_SCHEMA, "state": "complete",
                    "policy": "legacy-rebuild" if arm == "legacy" else "snapshot-v1", "arm_count": count,
                    **{key: expected[key] for key in contract.IDENTITY}, "root_sha256": digest(b"/checkout"),
                    "baseline_sha256": baseline, "candidate_sha256": B256, "harness_sha256": H256,
                    "baseline_bytes": 1, "candidate_bytes": 1, "harness_bytes": 1,
                    "baseline_mode": 0o755, "candidate_mode": 0o755, "harness_mode": 0o755,
                    "baseline_cache_sha256": digest(CACHE), "candidate_cache_sha256": digest(CACHE),
                    "bootstrap_configuration": CONFIG, "bootstrap_artifact_sha256": G256,
                    "secondary_head": "", "secondary_tree": "", "candidate2_sha256": "", "candidate2_bytes": 0,
                    "candidate2_mode": 0, "candidate2_cache_sha256": "", "candidate2_receipt_sha256": "",
                    "ownership_schema": contract.OWNERSHIP_SCHEMA, "cleanup_proven": True, "duration_us": 100000}
        if count == 3:
            prepared.update(secondary_head=expected["secondary_head"], secondary_tree=expected["secondary_tree"],
                            candidate2_sha256=D256, candidate2_bytes=1, candidate2_mode=0o755, candidate2_cache_sha256=digest(CACHE))
        raw = inventory(expected, arm, baseline)
        workload = contract.normalized_workload(raw, expected)
        phases = native_phases(arm, qualification, count)
        raw_ledger = ledger(prepared, phases, arm, qualification)
        prepared.update(prepared_manifest_sha256=digest(raw), frozen_workload_sha256=digest(workload),
                        ledger_sha256=digest(raw_ledger), stage_count=len(phases),
                        snapshot_digest=digest(raw) if arm == "snapshot" else "")
        files = {"phases.tsv": raw_ledger, "prepared.manifest.tsv": raw, "prepared.workload.tsv": workload}
        roles = ("baseline", "candidate", "candidate2") if count == 3 else ("baseline", "candidate")
        for role in roles:
            commit = expected["base" if role == "baseline" else "secondary_head" if role == "candidate2" else "head"]
            tree = expected["base_tree" if role == "baseline" else "secondary_tree" if role == "candidate2" else "head_tree"]
            record = {"schema": "buster-compiler-frozen-binary-v1", "state": "complete", "role": role, "commit": commit, "tree": tree,
                      "sha256": prepared[role + "_sha256"], "bytes": 1, "mode": 0o755,
                      "cache_sha256": digest(CACHE), "cache_bytes": len(CACHE), "cache_mode": 0o644}
            files[role + ".binary.json"] = encoded(record)
            files[role + ".CMakeCache.txt"] = CACHE
            prepared[role + "_receipt_sha256"] = digest(files[role + ".binary.json"])
        commands = contract.child_commands(arm, expected, prepared, qualification)
        for ordinal, phase in enumerate(phases, 1):
            if phase in commands:
                stem = f"{ordinal}-{phase}"
                files[stem + ".argv"] = b"".join(str(len(item.encode())).encode() + b":" + item.encode() + b"\n" for item in commands[phase])
                files[stem + ".stdout"] = files[stem + ".stderr"] = b""
                files[stem + ".cleanup.json"] = encoded({"schema": contract.OWNERSHIP_SCHEMA, "cleanup_proven": True,
                    "duration_us": 1, "waves": 1, "signalled": 0, "reaped": 0, "timed_out": 0, "cancelled": 0,
                    "reservation_retained": 0, "ownership_lost": 0})
        data = {"prepared": prepared, "manifest": raw, "workload": workload, "ledger": raw_ledger, "files": files}
        if arm == "snapshot":
            common = {"schema": "buster-compiler-closure-v1", "policy": "snapshot-v1", "state": "complete",
                      "base": expected["base"], "base_tree": expected["base_tree"], "root_sha256": digest(b"/checkout"),
                      "manifest_sha256": digest(raw), "harness_sha256": H256, "bootstrap_marker_sha256": T256,
                      "bootstrap_artifact_sha256": G256, "duration_us": 10, "harness_preparation_us": 1,
                      "ownership_schema": contract.OWNERSHIP_SCHEMA, "cleanup_proven": True,
                      "cleanup_us": 1, "cleanup_waves": 1, "cleanup_signalled": 0, "cleanup_reaped": 0}
            data["closure"] = {}
            data["closure_manifests"] = {}
            for operation in ("snapshot", "restore", "verify"):
                data["closure"][operation] = dict(common, operation=operation)
                data["closure_manifests"][operation] = raw
                files["closure-" + operation + ".json"] = encoded(data["closure"][operation])
                files["closure-" + operation + ".json.manifest.tsv"] = raw
        files["prepared.json"] = encoded(prepared)
        bundles[arm] = data
    if qualification:
        for arm, name, same in contract.SERIES:
            data = bundles[arm]
            base_arm = "legacy" if name == "cross-build-aa" else arm
            baseline = contract.frozen_binary(bundles[base_arm]["prepared"], base_arm, "baseline", expected)
            candidate = contract.frozen_binary(data["prepared"], arm, "baseline" if same else "candidate", expected)
            current = summary("no detectable difference" if same else "slower")
            for role, identity in zip(("baseline", "candidate"), (baseline, candidate)):
                current[role]["sha256"] = identity[1]
            if same:
                current["outputs_identical"] = True
                current["verdict"].update(ratio=1.0, ci_low=0.99, ci_high=1.01)
                current["metrics"]["wall"].update(ratio=1.0, ci_low=0.99, ci_high=1.01)
            full = corpus()
            for provenance, identity, revision in zip(full["metadata"]["compiler_provenance"], (baseline, candidate),
                    (expected["base"], expected["base" if same else "head"])):
                provenance.update(sha256=identity[1], revision_label=revision)
            config = {"command": contract.WORKLOAD_COMMAND, "repo_root": expected["root"], "cpu": 2, "perf": "perf",
                      "pairs": None, "target_minutes": 10.0, "warmups": 1, "seed": 20261003, "profile_steps": [], "sudo": False,
                      "require_identical_output": same, "extra": [], "canonical_inline_pair": False,
                      "extra_by_variant": {"a": [], "b": []}, "fresh_copy": True, "min_effect_percent": 0.5}
            lab = {"version": 1, "mode": "compare", "config": config,
                   "plan": {"pairs": 12, "order": "ABBA", "fresh_copy": True,
                            "reason": "--target-minutes 10: 1.000 s per pair (median of 2 pilot pairs), 0.1 min elapsed, "
                                      "profile steps about 0 compile-equivalents (0.0 min) -> 12 pairs "
                                      "(clamped to 10..1000, whole ABBA blocks)"},
                   "variants": {key: {"role": role, "ide": identity[0], "sha256": identity[1], "size_bytes": identity[2]}
                       for key, role, identity in zip(("a", "b"), ("baseline", "candidate"), (baseline, candidate))}}
            data[name] = {"summary": current, "lab": lab, "throughput": full["summary"], "metadata": full["metadata"]}
            for ordinal, phase in enumerate(native_phases(arm), 1):
                if phase in {name + suffix for suffix in ("-lab-binaries-before", "-lab-binaries-after",
                             "-throughput-binaries-before", "-throughput-binaries-after")}:
                    proof = ["BUSTER_COMPILER_FROZEN_BINARY_CHECK_V1"]
                    for path, value, size, mode in (baseline, candidate):
                        proof.append(f"{path}\t{value}\t{size}\t{mode}\t{value}\t{size}\t{mode}\tcomplete")
                    data["files"][f"{ordinal}-{phase}.binaries.tsv"] = ("\n".join(proof) + "\n").encode()
            data["files"][name + "-post.manifest.tsv"] = data["manifest"]
            data["files"][name + "-post.workload.tsv"] = data["workload"]
        receipt = {"schema": contract.SCHEMA, "profile": contract.SELECTOR, "state": "complete", "default_activated": False,
                   "ownership_schema": contract.OWNERSHIP_SCHEMA, "cleanup_proven": True,
                   **{key: expected[key] for key in contract.IDENTITY}, "root_sha256": digest(b"/checkout"),
                   "trusted_lab_sha256": expected["trusted_lab_sha256"], "python_sha256": expected["python_sha256"],
                   "cpu": 2, "target_minutes": 10, "warmups": 1, "seed": 20261003, "min_effect_percent": 0.5,
                   "planned_labs": 5, "planned_corpora": 5, "duration_us": 1000000}
        return expected, receipt, bundles
    return expected, bundles[arms[0]]["prepared"], bundles[arms[0]]


class ContractTest(unittest.TestCase):
    def test_complete_original_population_and_all_controls(self):
        expected, receipt, raw = fixture()
        self.assertEqual(contract.validate(expected, receipt, raw), [])
        self.assertEqual((raw["legacy"]["prepared"]["stage_count"], raw["snapshot"]["prepared"]["stage_count"]), (35, 42))
        self.assertEqual(contract.SERIES[-1], ("snapshot", "cross-build-aa", True))
        self.assertNotEqual(raw["legacy"]["manifest"], raw["snapshot"]["manifest"])
        self.assertEqual(raw["legacy"]["workload"], raw["snapshot"]["workload"])
        self.assertNotEqual(raw["legacy"]["prepared"]["baseline_sha256"], raw["snapshot"]["prepared"]["baseline_sha256"])
        self.assertTrue(contract.preparation_timings(raw["snapshot"]["prepared"], raw["snapshot"]["ledger"])["available"])

    def test_native_plan_fields_names_and_trusted_pins_fail_closed(self):
        expected, receipt, raw = fixture()
        for key, value in (("planned_labs", 4), ("planned_corpora", True), ("ownership_schema", "owner-env-v1"),
                           ("cleanup_proven", False), ("default_activated", True), ("trusted_lab_sha256", A256),
                           ("python_sha256", A256), ("duration_us", True)):
            with self.subTest(key=key):
                changed = dict(receipt, **{key: value})
                self.assertTrue(contract.validate(expected, changed, raw))
        changed = copy.deepcopy(raw)
        changed["snapshot"]["same-source-aa"] = changed["snapshot"].pop("cross-build-aa")
        self.assertTrue(contract.validate(expected, receipt, changed))

    def test_complete_cleanup_is_required_for_every_child_and_nominal_orphans_fail(self):
        expected, receipt, raw = fixture()
        name = next(key for key in raw["legacy"]["files"] if key.endswith(".cleanup.json"))
        for key, value in (("signalled", 1), ("reaped", 1), ("timed_out", 1), ("cancelled", 1),
                           ("reservation_retained", 1), ("ownership_lost", 1), ("cleanup_proven", False),
                           ("duration_us", True), ("waves", True)):
            with self.subTest(key=key):
                changed = copy.deepcopy(raw)
                record = json.loads(changed["legacy"]["files"][name])
                record[key] = value
                changed["legacy"]["files"][name] = encoded(record)
                self.assertTrue(contract.validate(expected, receipt, changed))
        for extra in (False, True):
            changed = copy.deepcopy(raw)
            if extra:
                changed["legacy"]["files"]["999-foreign.cleanup.json"] = raw["legacy"]["files"][name]
            else:
                changed["legacy"]["files"].pop(name)
            self.assertTrue(contract.validate(expected, receipt, changed))

    def test_binary_bytes_size_mode_and_cross_build_identity_are_replayed(self):
        expected, receipt, raw = fixture()
        for arm, fragment in (("legacy", "ab-lab-binaries-before"), ("legacy", "immutable-aa-throughput-binaries-after"),
                              ("snapshot", "cross-build-aa-lab-binaries-before"), ("snapshot", "cross-build-aa-throughput-binaries-after")):
            key = next(key for key in raw[arm]["files"] if fragment in key)
            for column, value in ((1, B256), (2, "2"), (3, "420"), (4, B256), (5, "2"), (6, "420"), (7, "failed")):
                with self.subTest(arm=arm, proof=fragment, column=column):
                    changed = copy.deepcopy(raw)
                    lines = changed[arm]["files"][key].decode().splitlines()
                    fields = lines[1].split("\t")
                    fields[column] = value
                    lines[1] = "\t".join(fields)
                    changed[arm]["files"][key] = ("\n".join(lines) + "\n").encode()
                    self.assertTrue(contract.validate(expected, receipt, changed))

    def test_raw_normalization_cannot_omit_changed_generated_inputs_or_tool_absence(self):
        expected, receipt, raw = fixture()
        for source, target in ((b"generated/fixture.h", b"generated/changed.h"),
                               (b"binding\tmold\tabsent", b"binding\tmold\t/usr/bin/mold"),
                               (b"binding\tbootstrap_config\t" + CONFIG.encode(), b"binding\tbootstrap_config\t" + A256.encode())):
            changed = copy.deepcopy(raw)
            arm = changed["snapshot"]
            arm["manifest"] = arm["manifest"].replace(source, target)
            arm["prepared"]["prepared_manifest_sha256"] = digest(arm["manifest"])
            arm["files"]["prepared.manifest.tsv"] = arm["manifest"]
            arm["files"]["prepared.json"] = encoded(arm["prepared"])
            self.assertTrue(contract.validate(expected, receipt, changed))
        changed = copy.deepcopy(raw)
        changed["snapshot"]["workload"] += b"producer\tinvented\tvalue\n"
        self.assertTrue(contract.validate(expected, receipt, changed))

    def test_ledger_count_order_failure_and_phase_argv_are_not_exit0_only(self):
        expected, receipt, raw = fixture()
        for mutate in ("count", "failed", "rename", "argv"):
            changed = copy.deepcopy(raw)
            arm = changed["legacy"]
            if mutate == "count":
                arm["prepared"]["stage_count"] += 1
                arm["files"]["prepared.json"] = encoded(arm["prepared"])
            elif mutate in ("failed", "rename"):
                arm["ledger"] = arm["ledger"].replace(b"\tcomplete\t0", b"\tfailed\t0", 1) if mutate == "failed" else \
                    arm["ledger"].replace(b"\treset-build-cache\t", b"\tomitted-clean-cache\t")
                arm["prepared"]["ledger_sha256"] = digest(arm["ledger"])
                arm["files"]["phases.tsv"] = arm["ledger"]
                arm["files"]["prepared.json"] = encoded(arm["prepared"])
            else:
                key = next(key for key in arm["files"] if key.endswith("ab-lab.argv"))
                arm["files"][key] = arm["files"][key].replace(b"2:10\n", b"1:1\n")
            with self.subTest(case=mutate):
                self.assertTrue(contract.validate(expected, receipt, changed))

    def test_actual_native_snapshot_restore_verify_receipts_and_post_closure_are_required(self):
        expected, receipt, raw = fixture()
        for operation in ("snapshot", "restore", "verify"):
            changed = copy.deepcopy(raw)
            changed["snapshot"]["closure"][operation]["bootstrap_marker_sha256"] = B256
            changed["snapshot"]["files"]["closure-" + operation + ".json"] = encoded(changed["snapshot"]["closure"][operation])
            self.assertTrue(contract.validate(expected, receipt, changed))
        changed = copy.deepcopy(raw)
        changed["snapshot"]["files"]["ab-post.manifest.tsv"] += b"changed-after-measurement"
        self.assertTrue(contract.validate(expected, receipt, changed))

    def test_historical_lab_and_full_corpus_population_stay_fixed(self):
        expected, receipt, raw = fixture()
        for key, value in (("pairs", 12), ("target_minutes", 1), ("profile_steps", ["sampling"]),
                           ("fresh_copy", False), ("process_ownership", None), ("canonical_inline_pair", True),
                           ("extra", ["-fcanonical-inline"]), ("require_identical_output", True)):
            changed = copy.deepcopy(raw)
            changed["legacy"]["ab"]["lab"]["config"][key] = value
            with self.subTest(key=key):
                self.assertTrue(contract.validate(expected, receipt, changed))
        for mutate in ("population", "labels", "same-output", "aa-regression"):
            changed = copy.deepcopy(raw)
            row = changed["snapshot"]["cross-build-aa"]
            if mutate == "population":
                row["throughput"]["comparisons"].pop()
            elif mutate == "labels":
                row["metadata"]["compiler_provenance"][1]["revision_label"] = expected["head"]
            elif mutate == "same-output":
                row["summary"]["outputs_identical"] = False
            else:
                row["throughput"]["confirmed_regressions"] = 1
            with self.subTest(case=mutate):
                self.assertTrue(contract.validate(expected, receipt, changed))

    def test_frozen_cache_and_duplicate_json_are_rejected(self):
        expected, receipt, raw = fixture()
        for member, suffix in (("candidate.CMakeCache.txt", b"changed"), ("candidate.binary.json", b"changed")):
            changed = copy.deepcopy(raw)
            changed["legacy"]["files"][member] += suffix
            self.assertTrue(contract.validate(expected, receipt, changed))
        changed = copy.deepcopy(raw)
        key = next(key for key in changed["legacy"]["files"] if key.endswith(".cleanup.json"))
        changed["legacy"]["files"][key] = b'{"schema":"wrong","schema":"buster-native-qualification-supervisor-v1","cleanup_proven":true}\n'
        self.assertTrue(contract.validate(expected, receipt, changed))
        self.assertTrue(contract.validate(expected, receipt, None))

    def test_public_prepared_two_and_three_arm_api_has_no_statistical_dependency(self):
        for policy, count in (("legacy-rebuild", 2), ("snapshot-v1", 2), ("snapshot-v1", 3)):
            with self.subTest(policy=policy, count=count):
                expected, prepared, bundle = fixture(False, count, policy)
                self.assertEqual(contract.validate_prepared(expected, prepared, bundle), [])
                self.assertNotIn("summary", bundle)
                self.assertEqual(prepared["stage_count"], 21 if policy == "legacy-rebuild" else 20 if count == 2 else 26)
        expected, prepared, bundle = fixture(False, 3)
        for mutate in ("secondary", "cache2", "receipt2", "two-arm"):
            changed = copy.deepcopy(bundle)
            record = copy.deepcopy(prepared)
            pins = dict(expected)
            if mutate == "secondary":
                changed["ledger"] = changed["ledger"].replace(expected["secondary_head"].encode(), expected["head"].encode())
                record["ledger_sha256"] = digest(changed["ledger"])
                changed["files"]["phases.tsv"] = changed["ledger"]
                changed["files"]["prepared.json"] = encoded(record)
                changed["prepared"] = record
            elif mutate == "cache2":
                changed["files"]["candidate2.CMakeCache.txt"] += b"wrong flags"
            elif mutate == "receipt2":
                changed["files"]["candidate2.binary.json"] = changed["files"]["candidate.binary.json"]
            else:
                pins.update(arm_count=2)
                pins.pop("secondary_head")
                pins.pop("secondary_tree")
            with self.subTest(case=mutate):
                self.assertTrue(contract.validate_prepared(pins, record, changed))


    def test_preparation_span_includes_gaps_and_never_claims_complete_net_cost(self):
        expected, prepared, bundle = fixture(False, 3)
        timing = contract.preparation_timings(prepared, bundle["ledger"])
        self.assertTrue(timing["available"])
        self.assertGreater(timing["preparation_span_us"], timing["stage_total_us"])
        self.assertEqual(timing["unassigned_us"], timing["preparation_span_us"] - timing["stage_total_us"])
        self.assertEqual(timing["initial_pins_us"], 20)
        self.assertFalse(timing["complete_cost_available"])
        self.assertIsNone(timing["publication_us"])
        self.assertNotIn("total_us", timing)
        failed = contract.preparation_timings(dict(prepared, state="failed"), bundle["ledger"])
        self.assertFalse(failed["available"])
        self.assertEqual(failed["controller_reported_duration_us"], prepared["duration_us"])
        self.assertFalse(failed["complete_cost_available"])
        self.assertIsNone(failed["publication_us"])

    def test_actual_sourced_helper_mode_and_bounded_replay_diagnostic(self):
        expected, prepared, bundle = fixture(False, 3)
        self.assertIn(b"source\tF\t420\t", bundle["manifest"])
        self.assertEqual(contract.validate_prepared(expected, prepared, bundle), [])
        changed = copy.deepcopy(bundle)
        changed["files"].pop("candidate2.binary.json")
        reasons = contract.validate_prepared(expected, prepared, changed)
        self.assertTrue(reasons)
        changed = copy.deepcopy(bundle)
        changed["manifest"] = b"wrong\n"
        changed["prepared"]["prepared_manifest_sha256"] = digest(changed["manifest"])
        changed["files"]["prepared.manifest.tsv"] = changed["manifest"]
        changed["files"]["prepared.json"] = encoded(changed["prepared"])
        reasons = contract.validate_prepared(expected, changed["prepared"], changed)
        self.assertTrue(any("raw manifest source/tree/root header mismatch" in reason for reason in reasons))
        self.assertTrue(all(len(reason) < 400 for reason in reasons))


if __name__ == "__main__":
    unittest.main()
