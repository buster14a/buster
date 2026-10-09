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
             "cleanup_proven": True, "launch_attempted": 1, "manager_launched": 1, "manager_terminal": 1}
    value.update({key: 0 for key in ("exit_status", "timed_out", "cancelled", "capture_failed", "output_truncated",
                  "cleanup_us", "cleanup_waves", "cleanup_signalled", "cleanup_reaped", "reservation_retained",
                  "ownership_lost", "tree_cleanup_failed")})
    return value


def corpus_bundle():
    from compiler_test import corpus
    documents = corpus("regression")
    return {name: encoded(document) for name, document in documents.items()}


def population(utility_policy=None, *, main_policy=None, main_profile="compiler-compare-v1"):
    from compiler_owned_plan_test import fixture, utility_fixture, main_owned_fixture
    if utility_policy is not None and main_policy is not None:
        raise ValueError("fixture routes are mutually exclusive")
    current, ownership, planned = main_owned_fixture(main_policy, main_profile) if main_policy is not None else \
        fixture("main") if utility_policy is None else utility_fixture(utility_policy)
    ownership.update(schema=contract.MAIN_POPULATION_SCHEMA if main_policy is not None else contract.POPULATION_SCHEMA if utility_policy is None else contract.UTILITY_POPULATION_SCHEMA, state="complete",
                     trusted_revision="e" * 40, trusted_tree="f" * 40,
                     driver_sha256="d" * 64, bootstrap_marker_sha256=contract.sha(marker()),
                     driver_path="/trusted/.cache/bootstrap-driver/posix/" + "c" * 64 + "/build-fixture")
    if utility_policy is not None or main_policy is not None:
        current["identity"]["pull_head"] = "c" * 40
        current["coverage"] = {"first_parent": current["identity"]["base"], "range": "1"}
        current["toolchain"] = {tool: "NA (FileNotFoundError)" for tool, _ in contract.VERSION_PROBES}
        ownership["owned_preflight"] = True
        from compiler_github import RECONCILE_DEPTH
        root = ownership["candidate_root"]
        prefix = [["git", "-C", "/trusted", "rev-parse", value] for value in ("HEAD", "HEAD^{tree}")]
        capture_outputs = [ownership["trusted_revision"], ownership["trusted_tree"]]
        for tool, flag in contract.VERSION_PROBES:
            if tool in contract.MANDATORY_VERSION_TOOLS:
                current["toolchain"][tool] = "diagnostic " + tool + " version"
                prefix.append([tool, flag])
                capture_outputs.append(current["toolchain"][tool])
        prefix += [["git", "-C", root, "rev-parse", "--verify", "--quiet", "HEAD^2"],
            ["git", "-C", root, "rev-list", "--first-parent", f"--max-count={RECONCILE_DEPTH}", "HEAD^1"],
            ["git", "-C", root, "rev-parse", "HEAD"],
            ["git", "-C", root, "rev-parse", "HEAD^{tree}"],
            ["git", "-C", root, "rev-parse", current["identity"]["base"] + "^{tree}"]]
        capture_outputs += [current["identity"]["pull_head"], current["identity"]["base"],
                            current["identity"]["head"], current["identity"]["head_tree"], current["identity"]["base_tree"]]
        planned = [dict(phase="preflight", kind="capture",
                        allow_exit_failure=len(argv) == 2 or argv[-1] == "HEAD^2",
                        argv=argv, cwd="/trusted", timeout=30 if len(argv) == 2 else 120) for argv in prefix] + planned
    rows, raw = [], {}
    for ordinal, recipe in enumerate(planned, 1):
        argv = list(recipe["argv"])
        if argv[0].startswith("/trusted/.cache/"):
            argv[0] = ownership["driver_path"]
        stdout = b""
        if recipe["kind"] == "capture":
            stdout = (capture_outputs[ordinal-1] + "\n").encode()
        native = record(argv, cwd=recipe["cwd"], timeout=recipe["timeout"], ordinal=ordinal, stdout=stdout)
        if recipe["phase"] == "throughput":
            native.update(state="failed", exit_status=256)
        receipt = encoded(native)
        name = f"{ordinal:04d}.json"
        rows.append(dict(recipe, ordinal=ordinal, file=name, argv=argv,
                         bridge_wall_us=150, receipt_sha256=contract.sha(receipt)))
        raw[name] = {"receipt": receipt, "command": contract.command_bytes(argv),
                     "stdout": stdout, "stderr": b"", "bootstrap": marker()}
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
    def test_supported_main_named_recipes_require_trusted_authority_and_complete_population(self):
        from compiler_receipt import validate_closure, MAIN_PROFILES
        for policy in ("legacy-rebuild", "snapshot-v1"):
            for profile in MAIN_PROFILES:
                with self.subTest(policy=policy, profile=profile):
                    current, raw = population(main_policy=policy, main_profile=profile)
                    options = dict(expected_phase_schema=contract.MAIN_POPULATION_SCHEMA, expected_profile=profile)
                    self.assertEqual(contract.validate_population(current, raw, throughput_bundle=corpus_bundle(), **options), [])
                    self.assertTrue(contract.validate_population(current, raw, throughput_bundle=corpus_bundle(),
                        expected_phase_schema=contract.MAIN_POPULATION_SCHEMA))
                    for changed in ("compiler-compare-v1", "compiler-main-40pairs-v1", "forty", 40, True):
                        if changed != profile:
                            self.assertTrue(contract.validate_population(current, raw, throughput_bundle=corpus_bundle(),
                                expected_phase_schema=contract.MAIN_POPULATION_SCHEMA, expected_profile=changed))
                    for key in ("profile", "phase_ownership"):
                        missing = copy.deepcopy(current)
                        missing.pop(key)
                        self.assertTrue(contract.validate_population(missing, raw, throughput_bundle=corpus_bundle(), **options))
                    truncated = copy.deepcopy(raw)
                    truncated.pop(next(iter(truncated)))
                    self.assertTrue(contract.validate_population(current, truncated, throughput_bundle=corpus_bundle(), **options))
                    unowned = copy.deepcopy(current)
                    unowned["phase_ownership"]["owned_preflight"] = False
                    self.assertTrue(contract.validate_population(unowned, raw, throughput_bundle=corpus_bundle(), **options))
                    if policy == "legacy-rebuild":
                        bundle = {"owned_phases": raw, "owned_throughput": corpus_bundle()}
                        required = dict(expected_policy=policy, expected_profile=profile,
                            expected_phase_schema=contract.MAIN_POPULATION_SCHEMA,
                            require_owned_phases=True, require_owned_preflight=True)
                        self.assertEqual(validate_closure(current, bundle, **required), [])
                        for key in required:
                            incomplete = dict(required)
                            incomplete.pop(key)
                            if key == "expected_phase_schema":
                                # Explicit MAIN profile still cannot use the historical default schema.
                                if profile == "compiler-compare-v1":
                                    continue
                            self.assertTrue(validate_closure(current, bundle, **incomplete))

    def test_short_main_summary_requires_fixed_complete_population_and_inference_fields(self):
        from compiler_test import summary, BINARIES
        from compiler_receipt import classify
        document = summary("slower")
        document["plan"].update(pairs=40, complete_pairs=40, seed=20261003, confidence=0.95,
            bootstrap_resamples=2000, fresh_copy=True, order="ABBA")
        for role in ("baseline", "candidate"):
            document[role]["runs"] = 40
        document["verdict"].update(n=40, min_effect_percent=0.5)
        self.assertEqual(classify(document, BINARIES, expected_profile="compiler-main-40pairs-v1"), [])
        for key, value in (("pairs", 39), ("complete_pairs", 39), ("seed", 1), ("confidence", 0.9),
                           ("bootstrap_resamples", 1), ("fresh_copy", 1), ("order", "AB")):
            changed = copy.deepcopy(document)
            changed["plan"][key] = value
            self.assertTrue(classify(changed, BINARIES, expected_profile="compiler-main-40pairs-v1"))
        self.assertTrue(classify(document, BINARIES, expected_profile="40"))

    def test_main_profile_cli_refuses_unsupported_routes_before_children(self):
        from compiler_receipt import IDENTITY_KEYS
        argv = ["--candidate", "/candidate", "--lab", "/trusted/tools/uarch_lab.py",
                "--work", "/work", "--evidence", "/evidence", "--summary", "/summary"]
        for key in IDENTITY_KEYS:
            argv += ["--" + key.replace("_", "-"), "main" if key == "mode" else "e" * 40]
        for extra in (["--main-profile", "compiler-main-40pairs-v1"],
                      ["--main-owned-phases"],
                      ["--main-owned-phases", "--closure-driver", "/driver", "--utility-owned-phases"],
                      ["--main-owned-phases", "--closure-driver", "/driver", "--main-profile", "40"]):
            with self.subTest(extra=extra), mock.patch.object(compare.subprocess, "Popen") as child:
                with self.assertRaises(SystemExit):
                    compare.parse(argv + extra)
                child.assert_not_called()
        supported = compare.parse(argv + ["--main-owned-phases", "--closure-driver", "/driver"])
        self.assertEqual(supported.main_profile, "compiler-compare-v1")
        pull = list(argv)
        pull[pull.index("--mode") + 1] = "pull"
        with self.assertRaises(SystemExit):
            compare.parse(pull + ["--main-owned-phases", "--closure-driver", "/driver"])

    def test_native_context_route_flags_are_strict_bool_before_file_or_child_observation(self):
        for key in ("utility", "owned_preflight", "main_owned"):
            for value in (0, 1, None, "true"):
                with self.subTest(flag=key, value=value), mock.patch.object(compare, "sha256") as digest, \
                        mock.patch.object(compare.subprocess, "Popen") as spawn:
                    with self.assertRaisesRegex(ValueError, "flags must be boolean"):
                        compare.NativePhaseContext(Path("/missing-driver"), Path("/work"), Path("/evidence"), {}, **{key: value})
                    digest.assert_not_called()
                    spawn.assert_not_called()

    def test_owned_metadata_missing_mandatory_tool_stops_before_any_later_probe(self):
        for absent in contract.MANDATORY_VERSION_TOOLS:
            attempted = []
            def probe(argv, **options):
                attempted.append(argv[0])
                return compare.subprocess.CompletedProcess(argv, 0, "diagnostic version\n", "")
            with self.subTest(tool=absent), mock.patch.object(compare, "OWNED_PHASE_CONTEXT", object()), \
                    mock.patch.object(compare.shutil, "which", side_effect=lambda tool: None if tool == absent else "/diagnostic/" + tool), \
                    mock.patch.object(compare, "captured_run", side_effect=probe):
                with self.assertRaises(FileNotFoundError):
                    compare.toolchain()
            tools = [tool for tool, _ in compare.TOOLS]
            self.assertEqual(attempted, tools[:tools.index(absent)])

    def test_owned_metadata_missing_optional_tools_have_zero_child_attempts(self):
        attempted = []
        def probe(argv, **options):
            attempted.append(argv[0])
            return compare.subprocess.CompletedProcess(argv, 0, "diagnostic version\n", "")
        with mock.patch.object(compare, "OWNED_PHASE_CONTEXT", object()), \
                mock.patch.object(compare.shutil, "which", side_effect=lambda tool: "/diagnostic/" + tool if tool in contract.MANDATORY_VERSION_TOOLS else None), \
                mock.patch.object(compare, "captured_run", side_effect=probe):
            versions = compare.toolchain()
        self.assertEqual(attempted, [tool for tool, _ in compare.TOOLS if tool in contract.MANDATORY_VERSION_TOOLS])
        for tool, _ in compare.TOOLS:
            self.assertEqual(versions[tool], "diagnostic version" if tool in contract.MANDATORY_VERSION_TOOLS else "NA (FileNotFoundError)")

    def test_owned_preflight_reader_requires_bool_and_mandatory_tool_population(self):
        for policy in ("legacy-rebuild", "snapshot-v1"):
            current, raw = population(policy)
            kwargs = dict(expected_phase_schema=contract.UTILITY_POPULATION_SCHEMA)
            for value in (0, 1, None, "true"):
                with self.subTest(policy=policy, value=value):
                    self.assertTrue(contract.validate_population(current, raw, "d" * 64, "e" * 40, corpus_bundle(),
                        require_owned_preflight=value, **kwargs))
            for tool in contract.MANDATORY_VERSION_TOOLS:
                changed = copy.deepcopy(current)
                changed["toolchain"][tool] = "NA (FileNotFoundError)"
                with self.subTest(policy=policy, tool=tool):
                    self.assertTrue(contract.validate_population(changed, raw, "d" * 64, "e" * 40, corpus_bundle(), **kwargs))

    def test_failed_early_initialization_launches_no_metadata_identity_or_measurement_child(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "candidate"
            candidate.mkdir()
            work, evidence = root / "work", root / "evidence"
            values = {"mode": "main", "repository": "buster14a/buster", "ref": "refs/heads/main", "pull": "1",
                "pull_head": "a" * 40, "base": "b" * 40, "base_tree": "c" * 40, "head": "a" * 40,
                "head_tree": "d" * 40, "trusted_revision": "e" * 40, "request_run_id": "1", "run_id": "1", "run_attempt": "1"}
            argv = ["--candidate", str(candidate), "--lab", str(Path(__file__).resolve()),
                "--work", str(work), "--evidence", str(evidence), "--summary", str(root / "summary.md"),
                "--closure-policy", "legacy-rebuild", "--utility-owned-phases",
                "--closure-driver", str(root / "missing-driver")]
            for key, value in values.items():
                argv.extend(["--" + key.replace("_", "-"), value])
            with mock.patch.object(compare, "OWNED_PHASE_CONTEXT", None), \
                    mock.patch.object(compare, "captured_run") as probes, \
                    mock.patch.object(compare, "toolchain") as metadata, \
                    mock.patch.object(compare, "measure") as measurement, \
                    mock.patch.object(compare, "host_problem", return_value=""):
                self.assertEqual(compare.main(argv), 1)
                probes.assert_not_called()
                metadata.assert_not_called()
                measurement.assert_not_called()
            failed = json.loads((evidence / "receipt.json").read_bytes())
            self.assertEqual(failed["state"], "failed")
            self.assertEqual(failed["work_retained"], str(work))
            self.assertTrue(any("initialization failed" in reason for reason in failed["reasons"]))

    def test_utility_both_recipes_require_explicit_trusted_schema_and_full_population(self):
        from compiler_receipt import validate_closure
        for policy in ("legacy-rebuild", "snapshot-v1"):
            current, raw = population(policy)
            kwargs = dict(expected_phase_schema=contract.UTILITY_POPULATION_SCHEMA)
            self.assertEqual(contract.validate_population(current, raw, "d" * 64, "e" * 40, corpus_bundle(), **kwargs), [])
            self.assertTrue(contract.validate_population(current, raw, "d" * 64, "e" * 40, corpus_bundle()))
            for case in ("stripped", "schema", "preflight-stripped", "capture125", "capture-compile", "capture-after-core", "core-omitted"):
                changed, members = copy.deepcopy(current), copy.deepcopy(raw)
                ownership = changed["phase_ownership"]
                rows = ownership["phases"]
                if case == "stripped":
                    changed.pop("phase_ownership")
                elif case == "schema":
                    ownership["schema"] = contract.POPULATION_SCHEMA
                elif case == "preflight-stripped":
                    ownership.pop("owned_preflight")
                elif case == "capture125":
                    row = rows[2]
                    native = json.loads(members[row["file"]]["receipt"])
                    native.update(state="failed", exit_status=125 * 256)
                    members[row["file"]]["receipt"] = encoded(native)
                    row["receipt_sha256"] = contract.sha(members[row["file"]]["receipt"])
                elif case == "capture-compile":
                    row = rows[0]
                    row["argv"] = ["/bin/true"]
                    native = json.loads(members[row["file"]]["receipt"])
                    native["command_sha256"] = contract.sha(contract.command_bytes(row["argv"]))
                    members[row["file"]]["command"] = contract.command_bytes(row["argv"])
                    members[row["file"]]["receipt"] = encoded(native)
                    row["receipt_sha256"] = contract.sha(members[row["file"]]["receipt"])
                elif case == "capture-after-core":
                    rows[6], rows[7] = rows[7], rows[6]
                else:
                    removed = rows.pop()
                    members.pop(removed["file"])
                    ownership["count"] -= 1
                with self.subTest(policy=policy, case=case):
                    self.assertTrue(contract.validate_population(changed, members, "d" * 64, "e" * 40, corpus_bundle(), **kwargs))
            if policy == "legacy-rebuild":
                bundle = {"owned_phases": raw, "owned_throughput": corpus_bundle()}
                self.assertEqual(validate_closure(current, bundle, expected_policy=policy, require_owned_phases=True,
                    expected_phase_driver_sha256="d" * 64, expected_trusted_revision="e" * 40, **kwargs), [])
                historical = copy.deepcopy(current)
                historical.pop("phase_ownership")
                self.assertEqual(validate_closure(historical, {}), [])
                self.assertTrue(validate_closure(historical, {}, expected_policy=policy, require_owned_phases=True, **kwargs))

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
