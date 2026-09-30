#!/usr/bin/env python3
"""Failure-first tests of the #881 production context generators.

Usage: retirement_context_generators_test.py

BindingContextTests drives retirement_binding_context.py over the #511 test
record as its inputs (retirement_binding_context_fixture.py
--production-inputs, with a census directory made from the same record): its
bytes equal the fixture's record context (--record-context) for the same
facts, are deterministic,
and pass ``check_context`` (the C importer's layout); a context with two
sections swapped, a section missing, the admission sentinel moved or a
non-canonical section is refused, and so are a wrong binary digest, odd or
out-of-range pairs, a missing or disagreeing host fact, a contract or census
file off its profile pin and a bootstrap count off the derived family.

RequiredChecksTests drives retirement_required_checks.py over a plan shaped
like the preparation fixture's twelve checks (retirement_check_runner_tests.h),
a copy of the host shell as the one ELF tool and a hosted acceptance record:
the bytes are the importer's canonical text, deterministic, and every
coverage, token, ordering, tool and record rule the importer enforces is
refused here first.

The C round trips (bq_prep_worker_unit_production_context and
bq_prep_worker_unit_generated_checks in retirement_worker_unit_tests.h) run
both generators' output through the real importers and checkers in
`bench_service self-test`.
"""

import copy
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools"))
import retirement_binding_context as generator  # noqa: E402
import retirement_binding_context_fixture as fixture  # noqa: E402
import retirement_required_checks as checks  # noqa: E402

# The census file of each pinned role in the #511 test record.
RECORD_CENSUS = {
    "support.tsv": "docs/native-retirement-support-v1.tsv",
    "manifest.txt": "census/manifest.txt",
    "inputs.tsv": "census/inputs.tsv",
    "rows.tsv": "census/rows.tsv",
    "performance-rows.json": "performance/rows.json",
    "validator-report.json": "census/validator-report.json",
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class BindingContextTests(unittest.TestCase):
    """The production binding-context generator over the test record."""

    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="retirement-context-")
        cls.root = Path(cls.temporary.name)
        _record, contents = fixture.binding_tests.BindingTests._build_record()
        cls.contents = contents
        census = cls.root / "census"
        census.mkdir()
        for name, path in RECORD_CENSUS.items():
            (census / name).write_bytes(contents[path])
        cls.census = census
        cls.binaries = {}
        for side, path in (("baseline", "subjects/baseline-ide"), ("candidate", "subjects/candidate-ide")):
            (cls.root / side).write_bytes(contents[path])
            cls.binaries[side] = sha256(contents[path])
        cls.inputs_directory = cls.root / "inputs"
        cls.inputs_directory.mkdir()
        cls.inputs = fixture.production_inputs(cls.inputs_directory, census, cls.root / "baseline",
                                               cls.root / "candidate", 7, 60, 100000)
        cls.evidence = cls.inputs_directory / "evidence"
        cls.record = (cls.inputs_directory / "binaries.record").read_bytes()
        cls.profile = (cls.inputs_directory / "profile").read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def generate(self, inputs=None, record=None, profile=None, evidence=None):
        return generator.context(self.inputs if inputs is None else inputs, evidence or self.evidence, self.census,
                                 self.record if record is None else record,
                                 self.profile if profile is None else profile)

    def refused(self, pattern, **arguments):
        with self.assertRaisesRegex(generator.ContextError, pattern):
            self.generate(**arguments)

    def edited(self, edit):
        inputs = copy.deepcopy(self.inputs)
        edit(inputs)
        return inputs

    def test_equals_fixture_generator(self):
        output = self.root / "fixture.context"
        contract = sha256(self.contents["docs/native-retirement-performance-contract.md"])
        subprocess.run([sys.executable, "-W", "error", str(HERE / "retirement_binding_context_fixture.py"),
                        "--record-context", str(self.census), str(output), self.binaries["baseline"], self.binaries["candidate"],
                        contract, "7", "60", "100000"], check=True)
        self.assertEqual(self.generate(), output.read_bytes())

    def test_layout_is_the_importer_layout(self):
        data = self.generate()
        sections = generator.check_context(data)
        lines = data.decode().split("\n")
        self.assertEqual(lines[0], generator.HEADER)
        self.assertEqual([line.split("=", 1)[0] for line in lines[1:-1]], [*generator.SECTIONS, "admission"])
        self.assertTrue(lines[2].startswith("execution=" + generator.EXECUTION_PREFIX + generator.ADMISSION_SENTINEL))
        self.assertEqual(sections["subjects"]["baseline"]["binary"]["sha256"], self.binaries["baseline"])
        self.assertEqual(sections["subjects"]["candidate"]["binary"]["sha256"], self.binaries["candidate"])
        self.assertEqual(sections["rules"]["sampling"]["pairs_per_round"], 60)

    def test_deterministic(self):
        first = self.generate()
        reordered = json.loads(json.dumps(self.inputs), object_pairs_hook=lambda pairs: dict(reversed(pairs)))
        self.assertEqual(first, self.generate())
        self.assertEqual(first, self.generate(inputs=reordered))
        output = [self.root / "cli-a", self.root / "cli-b"]
        for path in output:
            subprocess.run([sys.executable, "-W", "error", str(HERE / "retirement_binding_context.py"),
                            "--inputs", str(self.inputs_directory / "inputs.json"), "--evidence-root",
                            str(self.evidence), "--census", str(self.census), "--binaries-record",
                            str(self.inputs_directory / "binaries.record"), "--profile",
                            str(self.inputs_directory / "profile"), "--output", str(path)], check=True)
        self.assertEqual(output[0].read_bytes(), first)
        self.assertEqual(output[1].read_bytes(), first)

    def test_layout_refusals(self):
        lines = self.generate().decode().split("\n")
        swapped = lines[:3] + [lines[4], lines[3]] + lines[5:]
        missing = lines[:6] + lines[7:]
        execution = lines[2]
        host = execution[len("execution=" + generator.EXECUTION_PREFIX):]
        moved = ("execution={" + generator.ADMISSION_SENTINEL + ',"host":{' +
                 host[len(generator.ADMISSION_SENTINEL) + 1:])
        spaced = lines[:1] + [lines[1].replace(":", ": ", 1)] + lines[2:]
        for name, edited in (("section", swapped), ("section", missing), ("sentinel", lines[:2] + [moved] + lines[3:]),
                             ("canonical", spaced)):
            with self.subTest(name=name), self.assertRaisesRegex(generator.ContextError, name):
                generator.check_context("\n".join(edited).encode())
        with self.assertRaisesRegex(generator.ContextError, "sentinel"):
            generator.check_context("\n".join(lines).replace(generator.PENDING, "1" * 64, 1).encode())
        # Canonical, first member in place, but a second copy later in execution.
        doubled = execution[:-1] + ',"zz":{' + generator.ADMISSION_SENTINEL + '}}'
        generator._load_json(doubled.split("=", 1)[1].encode(), "doubled")
        with self.assertRaisesRegex(generator.ContextError, "exactly once"):
            generator.check_context("\n".join(lines[:2] + [doubled] + lines[3:]).encode())

    def test_json_the_c_reader_refuses(self):
        lines = self.generate().decode().split("\n")
        rules = next(index for index, line in enumerate(lines) if line.startswith("rules="))
        for constant in ("NaN", "Infinity", "-Infinity"):
            with self.subTest(constant=constant), self.assertRaisesRegex(generator.ContextError, "non-finite"):
                edited = lines[rules].replace(":1.02,", f":{constant},", 1)
                self.assertNotEqual(edited, lines[rules])
                generator.check_context("\n".join(lines[:rules] + [edited] + lines[rules + 1:]).encode())
        for depth, accepted in ((255, True), (257, False)):
            nested = '"zz":' + "[" * depth + "]" * depth
            edited = lines[1][:-1] + "," + nested + "}"
            data = "\n".join(lines[:1] + [edited] + lines[2:]).encode()
            with self.subTest(depth=depth):
                if accepted:
                    generator.check_context(data)
                else:
                    with self.assertRaisesRegex(generator.ContextError, "deeper than 256"):
                        generator.check_context(data)

    def test_wrong_binary_digest(self):
        other = self.record.replace(self.binaries["baseline"].encode(), sha256(b"other").encode())
        self.refused("baseline binary digest", record=other)
        swapped = self.record.decode().split("\n")
        swapped[8], swapped[9] = ("base-binary=" + swapped[9].split("=", 1)[1],
                                  "candidate-binary=" + swapped[8].split("=", 1)[1])
        self.refused("baseline binary digest", record="\n".join(swapped).encode())
        same = self.record.replace(self.binaries["candidate"].encode(), self.binaries["baseline"].encode())
        self.refused("same binary", record=same)
        self.refused("canonical", record=self.record.replace(b"-V2\n", b"-V1\n"))
        self.refused("job= is not a canonical decimal", record=self.record.replace(
            b"\njob=82\n", b"\njob=18446744073709551616\n"))
        self.refused("token= is not a canonical decimal", record=self.record.replace(b"\ntoken=1\n", b"\ntoken=0\n"))

    def test_pairs(self):
        for pairs, pattern in ((61, "inputs.campaign.pairs_per_round must be even"), (58, "60..254"),
                               (256, "60..254"), ("60", "60..254"), (True, "60..254")):
            with self.subTest(pairs=pairs):
                self.refused(pattern, inputs=self.edited(lambda inputs: inputs["campaign"].update(
                    pairs_per_round=pairs)))
        widest = generator.check_context(self.generate(inputs=self.edited(
            lambda inputs: inputs["campaign"].update(pairs_per_round=254))))
        self.assertEqual(widest["rules"]["sampling"]["pairs_per_round"], 254)
        # The generator's own check, before any #511 rule sees the value.
        family = generator.check_context(self.generate())["population"]["statistical_family"]
        campaign = dict(self.inputs["campaign"], pairs_per_round=62)
        self.assertEqual(generator.campaign_policy(campaign, family)["pairs_per_round"], 62)
        with self.assertRaisesRegex(generator.ContextError, "^inputs.campaign.pairs_per_round must be even"):
            generator.campaign_policy(dict(campaign, pairs_per_round=63), family)

    def test_campaign_values(self):
        cases = ((lambda campaign: campaign.update(seed=0), "seed"),
                 (lambda campaign: campaign.update(seed=1 << 64), "seed"),
                 (lambda campaign: campaign.update(resamples=99999), "resamples"),
                 (lambda campaign: campaign.update(bootstrap_members_per_scope=
                                                   campaign["bootstrap_members_per_scope"] + 1), "bootstrap"),
                 (lambda campaign: campaign.pop("resamples"), "missing fields: resamples"))
        for edit, pattern in cases:
            with self.subTest(pattern=pattern):
                self.refused(pattern, inputs=self.edited(lambda inputs: edit(inputs["campaign"])))

    def test_missing_host_fact(self):
        for fact in ("machine_id", "qualification_receipt", "profile"):
            with self.subTest(fact=fact):
                self.refused(f"missing fields: {fact}", inputs=self.edited(lambda inputs: inputs["host"].pop(fact)))
        self.refused("missing fields: host", inputs=self.edited(lambda inputs: inputs.pop("host")))
        self.refused("host profile identity differs", inputs=self.edited(lambda inputs: inputs["host"].update(
            machine_id="zen5-9700x-02")))
        self.refused("cannot be read", inputs=self.edited(lambda inputs: inputs["host"].update(
            qualification_receipt="execution/absent.json")))
        evidence = self.root / "profile-as-qualification"
        shutil.copytree(self.evidence, evidence)
        shutil.copy(evidence / self.inputs["host"]["qualification_receipt"], evidence / "execution/other.json")
        self.refused("profile_receipt", evidence=evidence, inputs=self.edited(
            lambda inputs: inputs["host"].update(profile="execution/other.json")))

    def evidence_with(self, path, edit, name):
        """A copy of the evidence root with the JSON receipt at path edited
        (or its bytes replaced when edit returns bytes)."""
        evidence = self.root / name
        shutil.copytree(self.evidence, evidence)
        target = evidence / path
        value = json.loads(target.read_bytes())
        replaced = edit(value)
        target.write_bytes(replaced if isinstance(replaced, bytes) else fixture._json_bytes(value))
        return evidence

    def test_receipt_contents_are_checked(self):
        """The #511 evidence checks over every receipt the context names."""
        qualification = self.inputs["host"]["qualification_receipt"]
        profile = self.inputs["host"]["profile"]
        cases = (
            (qualification, lambda value: value.update(qualified=False), "qualification receipt.qualified"),
            (qualification, lambda value: value.update(profile_version="profile-v2"),
             "host qualification identity differs"),
            (qualification, lambda value: value.update(whole_host_isolation=False),
             "qualification receipt.whole_host_isolation"),
            (profile, lambda value: value.update(whole_host_isolation=False), "profile receipt.whole_host_isolation"),
            (qualification, lambda value: value.update(logical_cpu=value["logical_cpu"] + 1), "CPU/target differs"),
            (qualification, lambda value: value.update(version=2), "qualification schema/version"),
            (qualification, lambda value: value.update(extra=1), "unknown fields: extra"),
            (self.inputs["execution"]["service"]["recipe"], lambda value: value.update(service_id="other-service"),
             "service receipt identity differs"),
            (self.inputs["execution"]["lease_receipt"], lambda value: value.update(candidate_can_access=True),
             "inaccessible supervisor"),
            (self.inputs["provenance"]["relation_receipt"],
             lambda value: value["candidate"].update(binary_sha256=sha256(b"another binary")),
             "relation_receipt.candidate does not match"),
            (self.inputs["provenance"]["strict_receipt"], lambda value: b"not json\n", "not a readable JSON receipt"),
            (self.inputs["provenance"]["census_receipt"], lambda value: value.update(success=False),
             "census replay receipt is not successful"),
        )
        for index, (path, edit, pattern) in enumerate(cases):
            with self.subTest(pattern=pattern):
                self.refused(pattern, evidence=self.evidence_with(path, edit, f"receipt-{index}"))

    def test_receipt_bytes_are_read_once(self):
        """A receipt swapped after it was hashed is not what gets checked: the
        non-binding bytes that were hashed are refused even though binding
        bytes are on disk by the time the receipt checks run."""
        path = self.inputs["subjects"]["candidate"]["build_receipt"]
        evidence = self.evidence_with(path, lambda value: value.update(binary_sha256=sha256(b"other")), "swapped")
        binding_bytes = (self.evidence / path).read_bytes()
        read = generator.Evidence._read

        def swap_after(reader, relative, name, cap):
            data = read(reader, relative, name, cap)
            if relative == path:
                (evidence / path).write_bytes(binding_bytes)
            return data

        with mock.patch.object(generator.Evidence, "_read", swap_after):
            self.refused("build receipt does not bind", evidence=evidence)
        self.generate(evidence=evidence)

    def test_profile_pins(self):
        text = self.profile.decode()
        contract = [line for line in text.split("\n") if line.startswith("contract-sha256=")][0]
        self.refused("contract-sha256", profile=text.replace(contract, "contract-sha256=" + "2" * 64).encode())
        rows = [line for line in text.split("\n") if line.startswith("census-rows-sha256=")][0]
        self.refused("census-rows-sha256", profile=text.replace(rows, "census-rows-sha256=" + "3" * 64).encode())
        self.refused("no census-rows-sha256", profile=text.replace(rows + "\n", "").encode())
        self.refused("repeats", profile=(text + contract + "\n").encode())

    def test_evidence_is_hashed_not_typed(self):
        evidence = self.root / "symlinked-evidence"
        shutil.copytree(self.evidence, evidence)
        flags = evidence / self.inputs["producer"]["build"]["flags"]
        flags.unlink()
        flags.symlink_to(evidence / self.inputs["producer"]["build"]["configuration"])
        self.refused("without a symbolic link", evidence=evidence)
        flags.unlink()
        shutil.copy(self.evidence / self.inputs["producer"]["build"]["flags"], flags)
        (self.root / "evidence-link").symlink_to(evidence)
        self.refused("evidence root is not a directory reached without a symbolic link",
                     evidence=self.root / "evidence-link")
        (self.root / "census-link").symlink_to(self.census)
        with self.assertRaisesRegex(generator.ContextError, "census directory is not a directory"):
            generator.context(self.inputs, self.evidence, self.root / "census-link", self.record, self.profile)
        self.refused("normalized relative path", inputs=self.edited(
            lambda inputs: inputs["measurement"].update(harness_binary="../escape")))
        self.refused("must name a file below", inputs=self.edited(
            lambda inputs: inputs["measurement"].update(harness_binary=".")))
        receipt = evidence / self.inputs["subjects"]["candidate"]["build_receipt"]
        value = json.loads(receipt.read_bytes())
        value["binary_sha256"] = sha256(b"another binary")
        receipt.write_bytes(fixture._json_bytes(value))
        self.refused("build receipt does not bind", evidence=evidence)

    def test_cli_fails_closed(self):
        output = self.root / "exists"
        output.write_bytes(b"kept\n")
        arguments = [sys.executable, "-W", "error", str(HERE / "retirement_binding_context.py"),
                     "--inputs", str(self.inputs_directory / "inputs.json"), "--evidence-root", str(self.evidence),
                     "--census", str(self.census), "--binaries-record", str(self.inputs_directory / "binaries.record"),
                     "--profile", str(self.inputs_directory / "profile")]
        refused = subprocess.run(arguments + ["--output", str(output)], capture_output=True, text=True)
        self.assertEqual(refused.returncode, 1)
        self.assertEqual(output.read_bytes(), b"kept\n")
        missing = subprocess.run(arguments[:-2] + ["--output", str(self.root / "never")], capture_output=True,
                                 text=True)
        self.assertEqual(missing.returncode, 1)
        self.assertIn("--profile", missing.stderr)
        self.assertFalse((self.root / "never").exists())


# The preparation fixture's check script shape (retirement_check_runner_tests.h
# bq_check_test_authority): one pinned tool, both binaries and sources.
SCRIPT_ARGV = ["{{tool:0}}", "-c", "printf '%s ok\\n' \"$0\"", "check-{index}", "{{binary:0}}", "{{source:0}}",
               "{{binary:1}}", "{{source:1}}", "{{tool:0}}"]
SCRIPT_ENVIRONMENT = ["LC_ALL=C", "PATH=/usr/bin:/bin", "WORK={{work}}"]
NATIVE = 11
FOREIGN = (5, 8, 2, 10, 4)


class RequiredChecksTests(unittest.TestCase):
    """The production required-check generator."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="retirement-checks-")
        self.root = Path(self.temporary.name)
        self.tools = self.root / "checks"
        self.tools.mkdir()
        shutil.copy(os.path.realpath("/bin/sh"), self.tools / "sh")
        os.chmod(self.tools / "sh", 0o500)
        self.commit, self.tree = "b" * 40, "c" * 40
        self.hosted = self.hosted_record(FOREIGN)
        self.plan = {
            "schema": checks.PLAN_SCHEMA,
            "projection": {"support_sha256": sha256(b"support"), "census_sha256": sha256(b"census"),
                           "population_sha256": sha256(b"population"), "native_target": NATIVE, "rows": 6,
                           "object_rows": 4, "eligible_rows": 5},
            "candidate": {"commit": self.commit, "tree": self.tree},
            "tools": ["sh"],
            "checks": [self.script("census", 0, 4, "native", 0), self.script("semantic", NATIVE, 1, "native", 1),
                       *(self.hosted_check(target) for target in FOREIGN),
                       self.script("matrix", NATIVE, 5, "native", 7), self.script("no-fallback", 0, 5, "native", 8),
                       self.script("self-host", 0, 1, "native", 9), self.script("fixed-point", 0, 1, "native", 10),
                       self.script("semantic", FOREIGN[0], 1, "compile-only", 11)],
        }

    def tearDown(self):
        self.temporary.cleanup()

    def hosted_record(self, lanes):
        return (f"{checks.HOSTED_HEADER}\ncommit={self.commit}\ntree={self.tree}\nrun=fixture\n" +
                "".join(f"lane={lane} passed\n" for lane in lanes)).encode()

    @staticmethod
    def script(kind, target, rows, evidence, index):
        return {"kind": kind, "target": target, "rows": rows, "evidence": evidence, "timeout_seconds": 30,
                "memory_mib": 1024, "stdout_sha256": sha256(f"check-{index} ok\n".encode()),
                "configuration": f"fixture {kind} {target}",
                "argv": [argument.replace("{index}", str(index)) for argument in SCRIPT_ARGV],
                "environment": list(SCRIPT_ENVIRONMENT)}

    @staticmethod
    def hosted_check(target):
        return {"kind": "semantic", "target": target, "rows": 1, "evidence": "hosted", "timeout_seconds": 30,
                "memory_mib": 1024, "configuration": f"fixture semantic {target}", "argv": [], "environment": []}

    def expected(self):
        """The importer's canonical text, composed independently."""
        tool = sha256((self.tools / "sh").read_bytes())
        hosted = sha256(self.hosted)
        projection = self.plan["projection"]
        text = io.StringIO()
        text.write(f"BQ-RETIREMENT-REQUIRED-CHECKS-V1\nsupport={projection['support_sha256']}\n"
                   f"census={projection['census_sha256']}\npopulation={projection['population_sha256']}\n"
                   f"native-target={NATIVE}\nhosted={self.commit} {self.tree} {hosted}\ntools=1\ntool=0 {tool} sh\n"
                   f"checks={len(self.plan['checks'])}\n")
        for index, check in enumerate(self.plan["checks"]):
            output = hosted if check["evidence"] == "hosted" else check["stdout_sha256"]
            text.write(f"check={index} {check['kind']} {check['target']} {check['rows']} {check['evidence']} "
                       f"30 1024 {output}\nconfiguration={check['configuration']}\nargv={len(check['argv'])}\n")
            text.write("".join(f"arg={argument}\n" for argument in check["argv"]))
            text.write(f"environment={len(check['environment'])}\n")
            text.write("".join(f"env={entry}\n" for entry in check["environment"]))
        return text.getvalue().encode()

    def generate(self, plan=None, hosted=None, census=None):
        return checks.authority(self.plan if plan is None else plan, self.tools,
                                self.hosted if hosted is None else hosted, census)

    def refused(self, pattern, edit=None, hosted=None):
        plan = copy.deepcopy(self.plan)
        if edit:
            edit(plan)
        with self.assertRaisesRegex(checks.ChecksError, pattern):
            self.generate(plan, hosted)

    def test_canonical_text(self):
        self.assertEqual(self.generate(), self.expected())

    def test_deterministic(self):
        plan = self.root / "plan.json"
        plan.write_text(json.dumps(self.plan, indent=2))
        (self.root / "hosted").write_bytes(self.hosted)
        outputs = []
        for name in ("a", "b"):
            subprocess.run([sys.executable, "-W", "error", str(HERE / "retirement_required_checks.py"), "--plan",
                            str(plan), "--checks-dir", str(self.tools), "--hosted-record", str(self.root / "hosted"),
                            "--output", str(self.root / name)], check=True)
            outputs.append((self.root / name).read_bytes())
        self.assertEqual(outputs, [self.expected(), self.expected()])
        refused = subprocess.run([sys.executable, "-W", "error", str(HERE / "retirement_required_checks.py"),
                                  "--plan", str(plan), "--checks-dir", str(self.tools), "--hosted-record",
                                  str(self.root / "hosted"), "--output", str(self.root / "a")],
                                 capture_output=True, text=True)
        self.assertEqual(refused.returncode, 1)

    def test_coverage_refusals(self):
        cases = (
            ("omit kinds", lambda plan: plan["checks"].pop(10)),
            ("hosts \\[4\\]", lambda plan: plan["checks"].pop(6)),
            ("native census check names target 5", lambda plan: plan["checks"][0].update(target=5)),
            ("object rows", lambda plan: plan["checks"][0].update(rows=3)),
            ("compiler-eligible rows", lambda plan: plan["checks"][7].update(rows=4)),
            ("share kind", lambda plan: plan["checks"].append(copy.deepcopy(plan["checks"][9]))),
            ("only a semantic check", lambda plan: plan["checks"][2].update(kind="matrix", rows=5)),
            ("names no target", lambda plan: plan["checks"][11].update(target=0)),
        )
        for pattern, edit in cases:
            with self.subTest(pattern=pattern):
                self.refused(pattern, edit)
        # Every host covered, host 11 through the hosted record, but no
        # native semantic lane on the native target.
        self.refused("no native semantic", lambda plan: plan["checks"].__setitem__(1, self.hosted_check(NATIVE)),
                     hosted=self.hosted_record((*FOREIGN, NATIVE)))

    def test_field_refusals(self):
        cases = (
            ("argv\\[0\\] must be exactly", lambda plan: plan["checks"][0]["argv"].__setitem__(0, "/bin/sh")),
            ("more than one token", lambda plan: plan["checks"][0]["argv"].__setitem__(3, "{{work}}{{work}}")),
            ("unknown token", lambda plan: plan["checks"][0]["argv"].__setitem__(3, "{{tool:1}}")),
            ("unknown token", lambda plan: plan["checks"][0]["argv"].__setitem__(3, "{{tool:00}}")),
            ("printable", lambda plan: plan["checks"][0]["argv"].__setitem__(3, "tab\there")),
            ("strictly ascending", lambda plan: plan["checks"][0]["environment"].reverse()),
            ("repeats a name", lambda plan: plan["checks"][0]["environment"].insert(1, "LC_ALL=D")),
            ("not NAME=value", lambda plan: plan["checks"][0]["environment"].__setitem__(0, "1X=y")),
            ("configuration holds a token", lambda plan: plan["checks"][0].update(configuration="{{work}}")),
            ("hosted, so it has no argv", lambda plan: plan["checks"][2].update(argv=["{{tool:0}}"])),
            ("unknown \\['stdout_sha256'\\]", lambda plan: plan["checks"][2].update(stdout_sha256="0" * 64)),
            ("missing \\['stdout_sha256'\\]", lambda plan: plan["checks"][0].pop("stdout_sha256")),
            ("timeout_seconds", lambda plan: plan["checks"][0].update(timeout_seconds=0)),
            ("memory_mib", lambda plan: plan["checks"][0].update(memory_mib=15)),
            ("rows must be", lambda plan: plan["checks"][9].update(rows=7)),
            ("unknown kind", lambda plan: plan["checks"][9].update(kind="smoke")),
            ("target", lambda plan: plan["checks"][9].update(target=13)),
            ("repeats a name", lambda plan: plan["tools"].append("sh")),
            ("revision", lambda plan: plan["candidate"].update(commit="HEAD")),
            ("population_sha256", lambda plan: plan["projection"].update(population_sha256="0" * 63)),
            ("plan.schema", lambda plan: plan.update(schema="v0")),
        )
        for pattern, edit in cases:
            with self.subTest(pattern=pattern):
                self.refused(pattern, edit)

    def test_hosted_record_refusals(self):
        self.refused("target 4", hosted=self.hosted_record(FOREIGN[:-1]))
        self.refused("candidate commit", hosted=self.hosted.replace(b"commit=b", b"commit=a"))
        self.refused("candidate commit", lambda plan: plan["candidate"].update(tree="d" * 40))
        self.refused("NUL", hosted=self.hosted + b"\0")
        # A lane named only inside another line does not count, with or
        # without a trailing newline.
        self.refused("not an admitted key", hosted=self.hosted_record(FOREIGN[:-1]) + b"note lane=4 passed\n")
        self.refused("bytes after its last line", hosted=self.hosted_record(FOREIGN[:-1]) + b"note lane=4 passed")
        self.refused("failed lanes \\[4\\]", hosted=self.hosted_record(FOREIGN[:-1]) + b"lane=4 failed\n")
        self.refused("repeats lane 4", hosted=self.hosted + b"lane=4 failed\n")
        self.refused("repeats lane 4", hosted=self.hosted + b"lane=4 passed\n")
        self.refused("bytes after its last line", hosted=self.hosted + b"junk")
        self.refused("not an admitted key", hosted=self.hosted + b"commit=" + b"b" * 40 + b"\n")
        self.refused("not an admitted key", hosted=self.hosted + b"tree=" + b"c" * 40 + b"\n")
        self.refused("not an admitted key", hosted=self.hosted + b"\n")
        self.refused("repeats run=", hosted=self.hosted + b"run=again\n")
        self.refused("lane=<target>", hosted=self.hosted + b"lane=07 passed\n")
        self.refused("lane=<target>", hosted=self.hosted + b"lane=13 passed\n")
        self.refused("lane=<target>", hosted=self.hosted + b"lane=3 passed \n")

    def test_tool_refusals(self):
        (self.tools / "extra").write_bytes(b"\x7fELF")
        self.refused("not exactly the pinned tools")
        (self.tools / "extra").unlink()
        os.chmod(self.tools / "sh", 0o700)
        (self.tools / "sh").write_bytes(b"#!/bin/sh\n")
        self.refused("not an ELF")
        (self.tools / "sh").unlink()
        (self.tools / "sh").symlink_to(os.path.realpath("/bin/sh"))
        self.refused("regular executable")

    def test_census_pins(self):
        census = self.root / "census"
        census.mkdir()
        (census / "support.tsv").write_bytes(b"support")
        (census / "rows.tsv").write_bytes(b"census")
        self.assertEqual(self.generate(census=census), self.expected())
        (census / "rows.tsv").write_bytes(b"other")
        with self.assertRaisesRegex(checks.ChecksError, "census_sha256"):
            self.generate(census=census)


if __name__ == "__main__":
    unittest.main()
