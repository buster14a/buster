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


if __name__ == "__main__":
    unittest.main()
