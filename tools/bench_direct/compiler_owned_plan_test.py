#!/usr/bin/env python3
"""Data-only checks of complete ordinary snapshot command recipes."""
from __future__ import annotations
import copy
import hashlib
import unittest

import compiler_owned_plan as contract
from compiler_receipt import PROFILE, THROUGHPUT_PROFILE, SCALING_PROFILE, INLINE_ACCEPTANCE_PROFILE


def fixture(mode="pull", inline=False, scaling=False, *, spaces=False):
    trusted, root, work, evidence = (("/trusted main", "/candidate tree", "/work space", "/evidence space")
                                    if spaces else ("/trusted", "/checkout", "/work", "/evidence"))
    python, lab = "/usr/bin/python3", trusted + "/tools/uarch_lab.py"
    driver = trusted + "/.cache/bootstrap-driver/posix/" + "c" * 64 + "/build-1234-5678"
    bins = work + "/bin"
    ownership = {"trusted_root": trusted, "candidate_root": root, "work_root": work, "evidence_root": evidence,
                 "binaries_root": bins, "directory": evidence + "/owned-phases", "python_path": python,
                 "lab_path": lab, "driver_path": driver, "bootstrap_marker_sha256": "d" * 64}
    identity = {"base": "b" * 40, "base_tree": "e" * 40, "head": "a" * 40, "head_tree": "f" * 40}
    manifest = "9" * 64
    receipt = {"mode": mode, "preparation_policy": "snapshot-v1", "identity": identity,
               "profile": copy.deepcopy(PROFILE), "throughput_profile": copy.deepcopy(THROUGHPUT_PROFILE),
               "inline_acceptance": {"requested": inline, "profile": copy.deepcopy(INLINE_ACCEPTANCE_PROFILE) if inline else None},
               "closure": {"policy": "snapshot-v1", "fallback": None, "snapshot": {
                   "base": identity["base"], "base_tree": identity["base_tree"],
                   "root_sha256": hashlib.sha256(root.encode()).hexdigest(), "manifest_sha256": manifest}}}
    if scaling:
        receipt["scaling_profile"] = copy.deepcopy(SCALING_PROFILE)
    git = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null", "-C", root]
    rows = []

    def row(phase, argv, timeout, cwd=root):
        # A matching self-claimed command digest does not establish the recipe.
        command = b"".join(str(len(item.encode())).encode() + b":" + item.encode() + b"\n" for item in argv)
        rows.append({"phase": phase, "kind": "run", "allow_exit_failure": False, "argv": argv,
                     "cwd": cwd, "timeout": timeout, "command_sha256": hashlib.sha256(command).hexdigest()})

    row("build-baseline", [*git, "checkout", "--quiet", "--detach", identity["base"]], 120)
    row("build-baseline", ["./build.sh", "generate", "--cc", "clang", "--no-include-tests"], 1800)
    row("build-baseline", ["./build.sh", "build", "--config", "Release", "-t", "ide"], 1800)
    row("closure-snapshot", [driver, "compiler_closure", "snapshot", root, work + "/frozen-baseline",
        identity["base"], identity["base_tree"], evidence + "/closure-snapshot.json", "-"], 1800, trusted)
    row("build-candidate", [*git, "checkout", "--quiet", "--detach", identity["head"]], 120)
    row("build-candidate", ["./build.sh", "generate", "--cc", "clang", "--no-include-tests"], 1800)
    row("build-candidate", ["./build.sh", "build", "--config", "Release", "-t", "ide"], 1800)
    if inline:
        row("inline-acceptance", [python, "-B", trusted + "/tools/bench_direct/inline_acceptance.py",
            "--lab", lab, "--candidate-ide", bins + "/ide-cand", "--repo-root", root,
            "--head-revision", identity["head"], "--cpu", "2", "--output", work + "/inline-acceptance"], 10800)
    row("build-closure", [*git, "checkout", "--quiet", "--detach", identity["base"]], 120)
    row("build-closure", [driver, "compiler_closure", "restore", root, work + "/frozen-baseline",
        identity["base"], identity["base_tree"], evidence + "/closure-restore.json", manifest], 1800, trusted)
    row("lab", [python, "-B", lab, "compare", "--baseline", bins + "/ide-base", "--candidate", bins + "/ide-cand",
        "--repo-root", root, "--cpu", "2", "--output", work + "/lab", "--target-minutes", "10", "--warmups", "1"], 3000)
    harness = root + "/build/throughput-tools/throughput"
    row("throughput", [harness, "run", "--baseline", bins + "/ide-base", "--candidate", bins + "/ide-cand",
        "--output", work + "/throughput", "--baseline-id", identity["base"], "--candidate-id", identity["head"],
        "--profile", "ci", "--mode", "all", "--pairs", "20", "--warmups", "2", "--timeout", "120", "--cpu", "2"], 1800)
    rows[-1]["exit_policy"] = "corpus-report-only-v1"
    if scaling:
        row("scaling", [harness, "scale", "--compiler", bins + "/ide-cand", "--output", work + "/scaling/cores",
            "--cpu-set", "auto", "--exclude-core", "0", "--workers", "1,2,4,7", "--allow-smt",
            "--profile", "ci", "--repeats", "15", "--warmups", "2", "--timeout", "120"], 1200)
        row("scaling", [harness, "scale", "--compiler", bins + "/ide-cand", "--output", work + "/scaling/machine",
            "--cpu-set", "auto", "--workers", "8", "--allow-smt", "--shape", "equal", "--shape", "skewed",
            "--shape", "tiny", "--profile", "ci", "--repeats", "15", "--warmups", "2", "--timeout", "120"], 1200)
    row("validate", [driver, "compiler_closure", "verify", root, work + "/frozen-baseline",
        identity["base"], identity["base_tree"], evidence + "/closure-verify.json", manifest], 1800, trusted)
    return receipt, ownership, rows


class OwnedPlanTest(unittest.TestCase):
    def test_full_original_core_and_optional_extension_recipes(self):
        for mode, inline, scaling in (("main", False, False), ("pull", False, False),
                                      ("pull", True, False), ("pull", False, True), ("pull", True, True)):
            for spaces in (False, True):
                with self.subTest(mode=mode, inline=inline, scaling=scaling, spaces=spaces):
                    receipt, ownership, rows = fixture(mode, inline, scaling, spaces=spaces)
                    self.assertEqual(contract.validate_plan(receipt, ownership, rows), [])
                    self.assertEqual(len(rows), 12 + int(inline) + 2 * int(scaling))
                    self.assertEqual(ownership["python_path"], "/usr/bin/python3")
                    self.assertEqual(rows[-1]["cwd"], ownership["trusted_root"])

    def test_every_self_consistent_noop_substitution_is_rejected(self):
        receipt, ownership, rows = fixture(inline=True, scaling=True)
        for index in range(len(rows)):
            changed = copy.deepcopy(rows)
            changed[index]["argv"] = ["/bin/true"]
            changed[index]["command_sha256"] = hashlib.sha256(b"9:/bin/true\n").hexdigest()
            with self.subTest(index=index, phase=rows[index]["phase"]):
                self.assertTrue(contract.validate_plan(receipt, ownership, changed))

    def test_every_command_cwd_timeout_and_kind_are_exact(self):
        receipt, ownership, rows = fixture(inline=True, scaling=True)
        for index in range(len(rows)):
            for key, value in (("cwd", "/other"), ("timeout", rows[index]["timeout"] + 1),
                               ("timeout", True), ("timeout", float(rows[index]["timeout"])),
                               ("kind", "capture"), ("allow_exit_failure", True)):
                changed = copy.deepcopy(rows)
                changed[index][key] = value
                with self.subTest(index=index, key=key, value=value):
                    self.assertTrue(contract.validate_plan(receipt, ownership, changed))

    def test_source_build_closure_and_lab_argv_substitutions_fail(self):
        receipt, ownership, rows = fixture()
        cases = [(0, 2, "gc.auto=1"), (0, -1, receipt["identity"]["head"]),
                 (1, -1, "--include-tests"), (2, 4, "Debug"),
                 (3, 3, "/other-root"), (3, 4, "/other-snapshot"),
                 (3, 5, receipt["identity"]["head"]), (3, 6, receipt["identity"]["head_tree"]),
                 (3, 7, ownership["evidence_root"] + "/unbound.json"), (3, -1, "9" * 64),
                 (8, -1, "-"), (9, 0, "/usr/bin/python3.11"), (9, 2, "/other/uarch_lab.py"),
                 (9, 5, ownership["binaries_root"] + "/ide-cand"), (9, 11, "3"),
                 (9, 15, "1"), (10, 0, "./build.sh"), (10, -9, "tiny_startup"),
                 (11, 2, "restore"), (11, -1, "8" * 64)]
        for index, column, value in cases:
            changed = copy.deepcopy(rows)
            changed[index]["argv"][column] = value
            with self.subTest(index=index, column=column):
                self.assertTrue(contract.validate_plan(receipt, ownership, changed))
        for index in (3, 8, 9, 10, 11):
            changed = copy.deepcopy(rows)
            changed[index]["argv"].append("--require-identical-output")
            self.assertTrue(contract.validate_plan(receipt, ownership, changed))

    def test_fixed_roots_driver_layout_and_marker_binding_are_required(self):
        receipt, ownership, rows = fixture()
        for key, value in (("binaries_root", "/elsewhere/bin"), ("directory", "/elsewhere/owned-phases"),
                           ("candidate_root", ownership["trusted_root"]), ("work_root", "/checkout/nested"),
                           ("evidence_root", "/work/nested"), ("trusted_root", "/checkout/nested"),
                           ("lab_path", "/work/lab.py"), ("python_path", "/checkout/python"),
                           ("candidate_root", "/checkout/../alias"), ("work_root", "/work/"),
                           ("trusted_root", "/"), ("bootstrap_marker_sha256", "wrong")):
            changed = dict(ownership, **{key: value})
            with self.subTest(key=key, value=value):
                self.assertTrue(contract.validate_plan(receipt, changed, rows))
        for driver in ("/trusted/nested/.cache/bootstrap-driver/posix/" + "c" * 64 + "/build-1",
                       "/trusted/build/driver", "/trusted/.cache/bootstrap-driver/posix/invalid/build-1",
                       "/trusted/.cache/bootstrap-driver/posix/" + "c" * 64 + "/other",
                       "/trusted/.cache/bootstrap-driver/posix/" + "c" * 64 + "/build-a/nested"):
            self.assertTrue(contract.validate_plan(receipt, dict(ownership, driver_path=driver), rows))
        for key in ("candidate_root", "driver_path", "bootstrap_marker_sha256", "python_path", "lab_path"):
            changed = dict(ownership)
            changed.pop(key)
            self.assertTrue(contract.validate_plan(receipt, changed, rows))

    def test_frozen_identity_profile_and_extensions_cannot_change(self):
        receipt, ownership, rows = fixture(inline=True, scaling=True)
        for group, key, value in (("identity", "base", "invalid"), ("identity", "head_tree", "A" * 40),
                                 ("profile", "warmups", True), ("throughput_profile", "rounds", 1)):
            changed = copy.deepcopy(receipt)
            changed[group][key] = value
            with self.subTest(group=group, key=key):
                self.assertTrue(contract.validate_plan(changed, ownership, rows))
        for mutation in ("manifest", "root-hash", "base-tree", "policy", "mode", "inline-profile",
                         "inline-requested", "scaling-profile", "scaling-order", "undeclared-inline", "undeclared-scaling"):
            changed = copy.deepcopy(receipt)
            commands = copy.deepcopy(rows)
            if mutation == "manifest":
                changed["closure"]["snapshot"]["manifest_sha256"] = "8" * 64
            elif mutation == "root-hash":
                changed["closure"]["snapshot"]["root_sha256"] = "8" * 64
            elif mutation == "base-tree":
                changed["closure"]["snapshot"]["base_tree"] = changed["identity"]["head_tree"]
            elif mutation == "policy":
                changed["preparation_policy"] = "legacy-rebuild"
            elif mutation == "mode":
                changed["mode"] = "main"
            elif mutation == "inline-profile":
                changed["inline_acceptance"]["profile"]["pairs"] = 2
            elif mutation == "inline-requested":
                changed["inline_acceptance"]["requested"] = 1
            elif mutation == "scaling-profile":
                changed["scaling_profile"]["series"]["cores"].append("--extra")
            elif mutation == "scaling-order":
                commands[-3], commands[-2] = commands[-2], commands[-3]
            elif mutation == "undeclared-inline":
                changed["inline_acceptance"] = {"requested": False, "profile": None}
            else:
                changed.pop("scaling_profile")
            with self.subTest(case=mutation):
                self.assertTrue(contract.validate_plan(changed, ownership, commands))

    def test_optional_extension_programs_outputs_and_flags_are_exact(self):
        receipt, ownership, rows = fixture(inline=True, scaling=True)
        for phase, column, value in (("inline-acceptance", 2, "/other/inline.py"),
                                    ("inline-acceptance", 6, ownership["binaries_root"] + "/ide-base"),
                                    ("inline-acceptance", -1, "/other/inline-output"),
                                    ("scaling", 0, "./build.sh"), ("scaling", 3, ownership["binaries_root"] + "/ide-base"),
                                    ("scaling", 5, "/other/scale-output"), ("scaling", -1, "1")):
            changed = copy.deepcopy(rows)
            index = next(index for index, row in enumerate(changed) if row["phase"] == phase)
            changed[index]["argv"][column] = value
            with self.subTest(phase=phase, column=column):
                self.assertTrue(contract.validate_plan(receipt, ownership, changed))

    def test_omitted_extra_duplicated_and_reordered_core_rows_fail(self):
        receipt, ownership, rows = fixture()
        for case in ("omitted", "extra", "duplicated", "reordered", "not-object"):
            changed = copy.deepcopy(rows)
            if case == "omitted":
                changed.pop()
            elif case == "extra":
                changed.append(copy.deepcopy(rows[-1]))
            elif case == "duplicated":
                changed[4] = copy.deepcopy(changed[0])
            elif case == "reordered":
                changed[0], changed[1] = changed[1], changed[0]
            else:
                changed[3] = None
            with self.subTest(case=case):
                self.assertTrue(contract.validate_plan(receipt, ownership, changed))

    def test_scientific_results_are_outside_this_command_contract(self):
        receipt, ownership, rows = fixture()
        receipt.update(state="failed", scientific_outcome="not equivalent", confirmed_regressions=2)
        self.assertEqual(contract.validate_plan(receipt, ownership, rows), [])
        for receipt_value, ownership_value, rows_value in ((None, ownership, rows), (receipt, None, rows),
                                                         (receipt, ownership, []), (receipt, ownership, "rows")):
            self.assertTrue(contract.validate_plan(receipt_value, ownership_value, rows_value))


def utility_fixture(policy="legacy-rebuild", *, spaces=False):
    """Independent complete main recipes; no owner process or compiler is launched."""
    receipt, ownership, snapshot_rows = fixture("main", spaces=spaces)
    ownership["schema"] = contract.UTILITY_POPULATION_SCHEMA
    receipt["preparation_policy"] = policy
    if policy == "snapshot-v1":
        return receipt, ownership, snapshot_rows
    receipt.pop("closure")
    root, work, bins = (ownership[key] for key in ("candidate_root", "work_root", "binaries_root"))
    git = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null", "-C", root]
    rows = []

    def add(phase, argv, timeout):
        command = b"".join(str(len(item.encode())).encode() + b":" + item.encode() + b"\n" for item in argv)
        rows.append({"phase": phase, "kind": "run", "allow_exit_failure": False, "argv": argv,
                     "cwd": root, "timeout": timeout, "command_sha256": hashlib.sha256(command).hexdigest()})

    for role, revision in (("baseline", receipt["identity"]["base"]),
                           ("candidate", receipt["identity"]["head"]),
                           ("closure", receipt["identity"]["base"])):
        add("build-" + role, [*git, "checkout", "--quiet", "--detach", revision], 120)
        add("build-" + role, ["./build.sh", "generate", "--cc", "clang", "--no-include-tests"], 1800)
        add("build-" + role, ["./build.sh", "build", "--config", "Release", "-t", "ide"], 1800)
    add("lab", [ownership["python_path"], "-B", ownership["lab_path"], "compare",
        "--baseline", bins + "/ide-base", "--candidate", bins + "/ide-cand", "--repo-root", root,
        "--cpu", "2", "--output", work + "/lab", "--target-minutes", "10", "--warmups", "1"], 3000)
    add("throughput", ["./build.sh", "bench_throughput", "run",
        "--baseline", bins + "/ide-base", "--candidate", bins + "/ide-cand",
        "--output", work + "/throughput", "--baseline-id", receipt["identity"]["base"],
        "--candidate-id", receipt["identity"]["head"],
        "--profile", "ci", "--mode", "all", "--pairs", "20", "--warmups", "2", "--timeout", "120", "--cpu", "2"], 1800)
    rows[-1]["exit_policy"] = "corpus-report-only-v1"
    return receipt, ownership, rows


class UtilityOwnedPlanTest(unittest.TestCase):
    @staticmethod
    def validate(receipt, ownership, rows, schema=contract.UTILITY_POPULATION_SCHEMA):
        return contract.validate_plan(receipt, ownership, rows, expected_phase_schema=schema)

    def test_complete_original_main_legacy_and_snapshot_recipes(self):
        for policy in ("legacy-rebuild", "snapshot-v1"):
            for spaces in (False, True):
                with self.subTest(policy=policy, spaces=spaces):
                    receipt, ownership, rows = utility_fixture(policy, spaces=spaces)
                    self.assertEqual(self.validate(receipt, ownership, rows), [])
                    self.assertEqual(len(rows), 11 if policy == "legacy-rebuild" else 12)
                    if policy == "legacy-rebuild":
                        self.assertNotIn("closure", receipt)
                        self.assertEqual([row["phase"] for row in rows],
                            ["build-baseline"] * 3 + ["build-candidate"] * 3 + ["build-closure"] * 3 + ["lab", "throughput"])
                        self.assertEqual(rows[6]["argv"][-1], receipt["identity"]["base"])
                        self.assertEqual(rows[-1]["argv"][:3], ["./build.sh", "bench_throughput", "run"])
                    else:
                        self.assertEqual(rows[-2]["argv"][:2],
                            [ownership["candidate_root"] + "/build/throughput-tools/throughput", "run"])
                        self.assertEqual(rows[-1]["phase"], "validate")
                    self.assertEqual(rows[-1 if policy == "legacy-rebuild" else -2]["exit_policy"], "corpus-report-only-v1")

    def test_receipt_selfclaim_cannot_select_the_utility_route(self):
        for policy in ("legacy-rebuild", "snapshot-v1"):
            receipt, ownership, rows = utility_fixture(policy)
            self.assertTrue(contract.validate_plan(receipt, ownership, rows))
            self.assertTrue(self.validate(receipt, ownership, rows, contract.POPULATION_SCHEMA))
            for schema in (None, "", True, "buster-compiler-utility-phases-v2"):
                self.assertTrue(self.validate(receipt, ownership, rows, schema))
            for declared in (None, contract.POPULATION_SCHEMA, "buster-compiler-utility-phases-v2"):
                changed = dict(ownership, schema=declared)
                self.assertTrue(self.validate(receipt, changed, rows))
            changed = dict(ownership)
            changed.pop("schema")
            self.assertTrue(self.validate(receipt, changed, rows))
        ordinary, ownership, rows = fixture("main")
        ownership["schema"] = contract.POPULATION_SCHEMA
        self.assertEqual(contract.validate_plan(ordinary, ownership, rows), [])
        self.assertTrue(self.validate(ordinary, ownership, rows))

    def test_every_utility_command_binding_is_exact(self):
        for policy in ("legacy-rebuild", "snapshot-v1"):
            receipt, ownership, rows = utility_fixture(policy)
            for index in range(len(rows)):
                for key, value in (("argv", ["/bin/true"]), ("cwd", "/other"), ("timeout", rows[index]["timeout"] + 1),
                                   ("timeout", True), ("timeout", float(rows[index]["timeout"])),
                                   ("kind", "capture"), ("allow_exit_failure", True)):
                    changed = copy.deepcopy(rows)
                    changed[index][key] = value
                    if key == "argv":
                        changed[index]["command_sha256"] = hashlib.sha256(b"9:/bin/true\n").hexdigest()
                    with self.subTest(policy=policy, index=index, key=key):
                        self.assertTrue(self.validate(receipt, ownership, changed))

    def test_legacy_third_rebuild_cannot_be_replaced_by_snapshot_or_head(self):
        receipt, ownership, rows = utility_fixture()
        _, _, snapshot = utility_fixture("snapshot-v1")
        for case in ("omit-generate", "omit-build", "head-checkout", "snapshot-restore", "snapshot-inventory",
                     "snapshot-harness", "tests-on", "debug-build", "git-background"):
            changed = copy.deepcopy(rows)
            if case == "omit-generate":
                changed.pop(7)
            elif case == "omit-build":
                changed.pop(8)
            elif case == "head-checkout":
                changed[6]["argv"][-1] = receipt["identity"]["head"]
            elif case == "snapshot-restore":
                changed[7] = copy.deepcopy(snapshot[8])
            elif case == "snapshot-inventory":
                changed[7] = copy.deepcopy(snapshot[3])
            elif case == "snapshot-harness":
                changed[-1]["argv"][:2] = [ownership["candidate_root"] + "/build/throughput-tools/throughput"]
            elif case == "tests-on":
                changed[7]["argv"][-1] = "--include-tests"
            elif case == "debug-build":
                changed[8]["argv"][3] = "Debug"
            else:
                changed[6]["argv"][2] = "gc.auto=1"
            with self.subTest(case=case):
                self.assertTrue(self.validate(receipt, ownership, changed))

    def test_no_pull_or_optional_extensions_or_profile_substitutions(self):
        for policy in ("legacy-rebuild", "snapshot-v1"):
            receipt, ownership, rows = utility_fixture(policy)
            for case in ("pull", "warmup-bool", "target", "cpu", "pairs", "rounds", "inline",
                         "inline-profile", "scaling", "analyzer", "analyzer-result", "policy"):
                changed = copy.deepcopy(receipt)
                if case == "pull":
                    changed["mode"] = "pull"
                elif case == "warmup-bool":
                    changed["profile"]["warmups"] = True
                elif case == "target":
                    changed["profile"]["target_minutes"] = 1
                elif case == "cpu":
                    changed["profile"]["cpu"] = 3
                elif case == "pairs":
                    changed["throughput_profile"]["pairs"] = 19
                elif case == "rounds":
                    changed["throughput_profile"]["rounds"] = 1
                elif case == "inline":
                    changed["inline_acceptance"] = {"requested": True, "profile": copy.deepcopy(INLINE_ACCEPTANCE_PROFILE)}
                elif case == "inline-profile":
                    changed["inline_acceptance"]["profile"] = {}
                elif case == "scaling":
                    changed["scaling_profile"] = copy.deepcopy(SCALING_PROFILE)
                elif case == "analyzer":
                    changed["analyzer_profile"] = {}
                elif case == "analyzer-result":
                    changed["analyzer"] = {}
                else:
                    changed["preparation_policy"] = "legacy-owned"
                with self.subTest(policy=policy, case=case):
                    self.assertTrue(self.validate(changed, ownership, rows))

    def test_utility_root_tool_and_bootstrap_layout_bindings(self):
        for policy in ("legacy-rebuild", "snapshot-v1"):
            receipt, ownership, rows = utility_fixture(policy)
            for key, value in (("candidate_root", ownership["trusted_root"]), ("candidate_root", "/checkout/../alias"),
                               ("work_root", "/checkout/nested"), ("evidence_root", "/work/nested"),
                               ("trusted_root", "/checkout/nested"), ("binaries_root", "/other/bin"),
                               ("directory", "/other/owned-phases"), ("python_path", "/checkout/python"),
                               ("lab_path", "/work/lab.py"), ("driver_path", ownership["trusted_root"] + "/build.sh"),
                               ("bootstrap_marker_sha256", "wrong")):
                with self.subTest(policy=policy, key=key):
                    self.assertTrue(self.validate(receipt, dict(ownership, **{key: value}), rows))
            for key in ("trusted_root", "candidate_root", "work_root", "evidence_root", "binaries_root",
                        "directory", "python_path", "lab_path", "driver_path", "bootstrap_marker_sha256"):
                changed = dict(ownership)
                changed.pop(key)
                self.assertTrue(self.validate(receipt, changed, rows))

    def test_original_utility_corpus_flags_ids_and_report_policy(self):
        for policy in ("legacy-rebuild", "snapshot-v1"):
            receipt, ownership, rows = utility_fixture(policy)
            index = next(index for index, row in enumerate(rows) if row["phase"] == "throughput")
            for flag, value in (("--baseline", ownership["binaries_root"] + "/ide-cand"),
                                ("--candidate", ownership["binaries_root"] + "/ide-base"),
                                ("--output", "/other/throughput"), ("--baseline-id", receipt["identity"]["head"]),
                                ("--candidate-id", receipt["identity"]["base"]), ("--profile", "quick"),
                                ("--mode", "fast"), ("--pairs", "19"), ("--warmups", "1"),
                                ("--timeout", "121"), ("--cpu", "3")):
                changed = copy.deepcopy(rows)
                argv = changed[index]["argv"]
                argv[argv.index(flag) + 1] = value
                with self.subTest(policy=policy, flag=flag):
                    self.assertTrue(self.validate(receipt, ownership, changed))
            for case in ("extra-identical-output", "missing-warmup", "missing-report-policy", "wrong-report-policy"):
                changed = copy.deepcopy(rows)
                if case == "extra-identical-output":
                    changed[index]["argv"].append("--require-identical-output")
                elif case == "missing-warmup":
                    argv = changed[index]["argv"]
                    start = argv.index("--warmups")
                    del argv[start:start + 2]
                elif case == "missing-report-policy":
                    changed[index].pop("exit_policy")
                else:
                    changed[index]["exit_policy"] = "zero"
                self.assertTrue(self.validate(receipt, ownership, changed))

    def test_exact_population_and_order_excludes_capture_and_extensions(self):
        for policy in ("legacy-rebuild", "snapshot-v1"):
            receipt, ownership, rows = utility_fixture(policy)
            for case in ("omitted", "extra", "duplicated", "reordered", "capture", "extension", "not-object"):
                changed = copy.deepcopy(rows)
                if case == "omitted":
                    changed.pop()
                elif case == "extra":
                    changed.append(copy.deepcopy(rows[-1]))
                elif case == "duplicated":
                    changed[4] = copy.deepcopy(changed[0])
                elif case == "reordered":
                    changed[0], changed[1] = changed[1], changed[0]
                elif case == "capture":
                    changed.insert(0, dict(rows[0], kind="capture"))
                elif case == "extension":
                    changed.insert(-1, dict(rows[0], phase="scaling"))
                else:
                    changed[0] = None
                with self.subTest(policy=policy, case=case):
                    self.assertTrue(self.validate(receipt, ownership, changed))

    def test_legacy_has_no_closure_and_snapshot_keeps_its_frozen_bindings(self):
        receipt, ownership, rows = utility_fixture()
        receipt["closure"] = {}
        self.assertTrue(self.validate(receipt, ownership, rows))
        receipt, ownership, rows = utility_fixture("snapshot-v1")
        for case in ("missing", "manifest", "root", "base", "tree", "fallback"):
            changed = copy.deepcopy(receipt)
            if case == "missing":
                changed.pop("closure")
            elif case == "fallback":
                changed["closure"]["fallback"] = "legacy-rebuild"
            else:
                key = {"manifest": "manifest_sha256", "root": "root_sha256", "base": "base", "tree": "base_tree"}[case]
                changed["closure"]["snapshot"][key] = "8" * (64 if case in ("manifest", "root") else 40)
            with self.subTest(case=case):
                self.assertTrue(self.validate(changed, ownership, rows))
        _, _, legacy = utility_fixture()
        self.assertTrue(self.validate(receipt, ownership, legacy))

    def test_complete_scientific_negative_stays_outside_the_recipe_contract(self):
        for policy in ("legacy-rebuild", "snapshot-v1"):
            receipt, ownership, rows = utility_fixture(policy)
            receipt.update(state="measured", scientific_outcome="detected slowdown", confirmed_regressions=2)
            self.assertEqual(self.validate(receipt, ownership, rows), [])


if __name__ == "__main__":
    unittest.main()
