#!/usr/bin/env python3
from __future__ import annotations

import copy
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path

import ci_native_observation as observation


@contextmanager
def environment(values):
    previous = {key: os.environ.get(key) for key in values}
    os.environ.update(values)
    try:
        yield
    finally:
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


class NativeObservationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repository = self.root / "repo"
        self.repository.mkdir()
        subprocess.run(["git", "init", "-q"], cwd=self.repository, check=True)
        subprocess.run(["git", "config", "user.email", "test@example.invalid"], cwd=self.repository, check=True)
        subprocess.run(["git", "config", "user.name", "test"], cwd=self.repository, check=True)
        (self.repository / "tracked").write_text("test\n", encoding="utf-8")
        subprocess.run(["git", "add", "tracked"], cwd=self.repository, check=True)
        subprocess.run(["git", "commit", "-qm", "test"], cwd=self.repository, check=True)
        self.evidence = self.root / "evidence"
        self.artifact = self.root / "artifact"
        self.environment = {
            "GITHUB_REPOSITORY": "buster14a/buster",
            "GITHUB_REF": "refs/pull/891/merge",
            "GITHUB_WORKFLOW": "Buster CI",
            "GITHUB_WORKFLOW_REF": "buster14a/buster/.github/workflows/ci.yml@refs/pull/891/merge",
            "GITHUB_WORKFLOW_SHA": "1" * 40,
            "GITHUB_RUN_ID": "123",
            "GITHUB_RUN_NUMBER": "456",
            "GITHUB_RUN_ATTEMPT": "1",
            "GITHUB_JOB": "native",
            "GITHUB_EVENT_NAME": "pull_request",
            "RUNNER_NAME": "runner-1",
            "RUNNER_ENVIRONMENT": "github-hosted",
            "RUNNER_OS": "macOS",
            "RUNNER_ARCH": "X64",
            "ImageOS": "macos-26",
            "ImageVersion": "20260824.0517.1",
        }

    def tearDown(self):
        self.temporary.cleanup()

    def initialize(self):
        args = observation.parser().parse_args(
            [
                "init",
                "--root",
                str(self.evidence),
                "--job-name",
                "macOS x86-64 native",
                "--runner-label",
                "macos-26-intel",
                "--platform",
                "unix",
                "--os",
                "macos",
                "--arch",
                "x86_64",
                "--differential-workers",
                "4",
            ]
        )
        with environment(self.environment):
            previous = Path.cwd()
            os.chdir(self.repository)
            try:
                self.assertEqual(0, args.function(args))
            finally:
                os.chdir(previous)
        tool_args = observation.parser().parse_args(
            [
                "toolchain",
                "--root",
                str(self.evidence),
                "--compiler",
                sys.executable,
                "--cmake",
                sys.executable,
                "--ninja",
                sys.executable,
            ]
        )
        self.assertEqual(0, tool_args.function(tool_args))

    def run_phase(self, name, sequence, exit_code=0):
        command = [sys.executable, "-c", f"raise SystemExit({exit_code})"]
        args = observation.parser().parse_args(
            [
                "run",
                "--root",
                str(self.evidence),
                "--phase",
                name,
                "--sequence",
                str(sequence),
                "--",
                *command,
            ]
        )
        self.assertEqual(exit_code, args.function(args))

    def prepare_artifact(self):
        self.artifact.mkdir(parents=True, exist_ok=True)
        (self.artifact / "native-ci-logs.tar.gz").write_bytes(b"archive")
        (self.artifact / "result.json").write_text("{}\n", encoding="utf-8")
        (self.artifact / "summary.md").write_text("summary\n", encoding="utf-8")

    def finalize(self, required, complete="1"):
        arguments = [
            "finalize",
            "--root",
            str(self.evidence),
            "--artifact-directory",
            str(self.artifact),
            "--expect-complete",
            complete,
        ]
        for phase in required:
            arguments.extend(("--required-phase", phase))
        args = observation.parser().parse_args(arguments)
        return args.function(args)

    def test_stable_schema_identity_binding_percent_free_phase_evidence(self):
        self.initialize()
        self.run_phase("configuration", 10)
        self.run_phase("producer_build", 20)
        self.prepare_artifact()
        self.assertEqual(
            0,
            self.finalize(
                ["calibration_cpu", "calibration_filesystem", "configuration", "producer_build", "upload_handoff"]
            ),
        )
        document = json.loads((self.artifact / "native-observation.json").read_text(encoding="utf-8"))
        self.assertEqual(observation.OBSERVATION_SCHEMA, document["schema"])
        self.assertTrue(document["complete"])
        self.assertFalse(document["comparison_eligible"])
        self.assertEqual(
            ["calibration_cpu", "calibration_filesystem", "configuration", "producer_build", "upload_handoff"],
            document["required_phases"],
        )
        sequences = [phase["sequence"] for phase in document["phases"]]
        self.assertEqual(sorted(sequences), sequences)
        self.assertGreater(document["observer_overhead_ns"], 0)
        self.assertEqual(
            {"native-ci-logs.tar.gz", "result.json", "summary.md"},
            {entry["name"] for entry in document["upload_handoff"]["files"]},
        )

    def test_failed_and_cancelled_commands_are_retained_as_non_successful(self):
        for retained_status in ("failed", "cancelled"):
            with self.subTest(status=retained_status):
                if self.evidence.exists():
                    import shutil

                    shutil.rmtree(self.evidence)
                if self.artifact.exists():
                    import shutil

                    shutil.rmtree(self.artifact)
                self.initialize()
                self.run_phase("configuration", 10, exit_code=7)
                phase_path = next((self.evidence / "phases").glob("010-*.json"))
                phase = json.loads(phase_path.read_text(encoding="utf-8"))
                phase["status"] = retained_status
                phase["message"] = "signal=15" if retained_status == "cancelled" else ""
                phase_path.write_bytes(observation.canonical_bytes(phase))
                self.prepare_artifact()
                self.assertEqual(
                    0, self.finalize(["configuration", "producer_build", "upload_handoff"], complete="0")
                )
                document = json.loads((self.artifact / "native-observation.json").read_text(encoding="utf-8"))
                self.assertFalse(document["complete"])
                self.assertEqual("non-success", document["correctness_outcome"])
                self.assertEqual(["producer_build"], document["missing_phases"])
                self.assertEqual(["configuration"], document["unsuccessful_phases"])

    def test_packing_failure_retains_incomplete_observation_in_both_locations(self):
        self.initialize()
        self.run_phase("configuration", 10)
        self.assertEqual(
            0,
            self.finalize(["configuration", "upload_handoff"], complete="0"),
        )
        artifact_document = json.loads(
            (self.artifact / "native-observation.json").read_text(encoding="utf-8")
        )
        root_document = json.loads(
            (self.evidence / "native-observation.json").read_text(encoding="utf-8")
        )
        self.assertEqual(artifact_document, root_document)
        self.assertFalse(artifact_document["complete"])
        self.assertIn("upload_handoff", artifact_document["unsuccessful_phases"])
        self.assertTrue(artifact_document["upload_handoff"]["error"])

    def test_successful_job_fails_closed_on_missing_phase(self):
        self.initialize()
        self.run_phase("configuration", 10)
        self.prepare_artifact()
        with self.assertRaisesRegex(observation.ObservationError, "incomplete timing evidence"):
            self.finalize(["configuration", "producer_build", "upload_handoff"], complete="1")

    def test_malformed_truncated_duplicate_and_non_monotonic_records_are_rejected(self):
        self.initialize()
        self.run_phase("configuration", 10)
        identity = observation.validate_identity(observation.load_json(self.evidence / "identity.json"))
        identity_sha = observation.sha256_bytes(observation.canonical_bytes(identity))
        phase_path = next((self.evidence / "phases").glob("010-*.json"))

        phase_path.write_text("{", encoding="utf-8")
        with self.assertRaisesRegex(observation.ObservationError, "cannot read"):
            observation.load_phases(self.evidence, identity_sha)

        phase = {
            "schema": observation.PHASE_SCHEMA,
            "sequence": 10,
            "phase": "configuration",
            "identity_sha256": identity_sha,
            "clock": observation.CLOCK,
            "started_ns": 20,
            "ended_ns": 10,
            "elapsed_ns": 0,
            "started_utc": "2026-09-19T00:00:01Z",
            "ended_utc": "2026-09-19T00:00:02Z",
            "status": "success",
            "exit_code": 0,
            "command_sha256": "0" * 64,
            "observer_overhead_ns": 1,
            "message": "",
        }
        phase_path.write_bytes(observation.canonical_bytes(phase))
        with self.assertRaisesRegex(observation.ObservationError, "non-monotonic"):
            observation.load_phases(self.evidence, identity_sha)

        phase["started_ns"] = 10
        phase["ended_ns"] = 20
        phase["elapsed_ns"] = 10
        phase_path.write_bytes(observation.canonical_bytes(phase))
        duplicate = dict(phase)
        duplicate["sequence"] = 11
        (self.evidence / "phases" / "011-duplicate.json").write_bytes(observation.canonical_bytes(duplicate))
        with self.assertRaisesRegex(observation.ObservationError, "duplicate phase name"):
            observation.load_phases(self.evidence, identity_sha)

    def make_enriched(self, durations, *, attempt="1", eligible=True, source="a" * 40):
        identity = {
            "schema": observation.IDENTITY_SCHEMA,
            "source": {"repository": "buster14a/buster", "commit": source, "tree": "b" * 40, "ref": "refs/heads/main"},
            "workflow": {
                "name": "Buster CI",
                "workflow_ref": "workflow",
                "workflow_sha": "c" * 40,
                "run_id": "10",
                "run_number": "11",
                "run_attempt": attempt,
                "job_key": "native",
                "job_name": "macOS x86-64 native",
                "event": "push",
            },
            "runner": {
                "requested_label": "macos-26-intel",
                "name": "runner",
                "environment": "github-hosted",
                "os": "macOS",
                "arch": "X64",
                "image_os": "macos-26",
                "image_version": "20260824.0517.1",
                "platform": "unix",
                "matrix_os": "macos",
                "matrix_arch": "x86_64",
                "cpu_model": "Intel Core i7-8700B",
                "cpu_signature": "family=6;model=158;stepping=10",
                "machine": "x86_64",
                "cores": 4,
                "ram_mib": 14336,
                "job_log_identity": {
                    "required_for_comparison": True,
                    "provisioner_version": None,
                    "azure_region": None,
                    "source": "github-job-log-preamble",
                },
            },
            "configuration": {
                "name": "Release",
                "compiler": "clang",
                "linker": "DEFAULT",
                "debug_info": "OFF",
                "differential_workers": 4,
                "cmake_build_parallel_level": "",
                "ninja_flags": "",
            },
            "created_utc": "2026-09-19T00:00:00Z",
        }
        tool = {
            "argv": ["tool", "--version"],
            "path": "/opt/tools/tool",
            "first_line": "tool 1",
            "sha256": "d" * 64,
        }
        toolchain = {
            "schema": observation.TOOLCHAIN_SCHEMA,
            "identity_sha256": observation.sha256_bytes(observation.canonical_bytes(identity)),
            "compiler": copy.deepcopy(tool),
            "cmake": copy.deepcopy(tool),
            "ninja": copy.deepcopy(tool),
            "recorded_utc": "2026-09-19T00:00:00Z",
        }
        phases = []
        for sequence, (phase, duration) in enumerate(sorted(durations.items()), 1):
            phases.append(
                {
                    "phase": phase,
                    "elapsed_ns": duration,
                    "status": "success",
                    "sequence": sequence,
                    "command_sha256": observation.sha256_bytes(phase.encode("utf-8")),
                }
            )
        document = {
            "schema": observation.OBSERVATION_SCHEMA,
            "identity": identity,
            "toolchain": toolchain,
            "complete": True,
            "correctness_outcome": "success",
            "phases": phases,
        }
        return {
            "schema": observation.ENRICHED_SCHEMA,
            "observation": document,
            "runner_log": {
                "runner_version": "2.337.0",
                "provisioner_version": "20260819.586",
                "azure_region": "westus",
                "image_os": "macos-26",
                "image_version": "20260824.0517.1",
                "job_id": 1,
            },
            "clock_evidence": {},
            "comparison_eligible": eligible,
            "comparison_ineligible_reasons": [] if eligible else ["incomplete"],
        }

    def base_durations(self):
        return {
            "configuration": 10,
            "producer_build": 100,
            "mode_payload": 20,
            "differential_preparation": 30,
            "differential_corpus": 200,
            "evidence_packing": 40,
            "upload_handoff": 5,
        }

    def test_one_locally_slow_phase_is_not_broad(self):
        baseline = self.base_durations()
        target = dict(baseline)
        target["producer_build"] *= 3
        result = observation.classify_observations(
            self.make_enriched(target),
            [self.make_enriched(baseline, attempt="2"), self.make_enriched(baseline, attempt="3")],
        )
        self.assertEqual("mixed/inconclusive", result["classification"])
        self.assertEqual(["producer_build"], result["slow_phases"])

    def test_multiple_independent_slow_phases_are_broad(self):
        baseline = self.base_durations()
        target = dict(baseline)
        for phase in ("configuration", "producer_build", "mode_payload", "differential_corpus", "evidence_packing"):
            target[phase] *= 2
        result = observation.classify_observations(
            self.make_enriched(target),
            [self.make_enriched(baseline, attempt="2"), self.make_enriched(baseline, attempt="3")],
        )
        self.assertEqual("hosted-runner-wide", result["classification"])
        self.assertIn("differential_corpus", result["slow_phases"])

    def test_matched_reruns_can_reproduce_a_predeclared_source_signature(self):
        durations = {phase: value * 2 for phase, value in self.base_durations().items()}
        signature = (
            "configuration",
            "producer_build",
            "mode_payload",
            "differential_corpus",
            "evidence_packing",
        )
        result = observation.classify_observations(
            self.make_enriched(durations),
            [self.make_enriched(durations, attempt="2"), self.make_enriched(durations, attempt="3")],
            signature,
        )
        self.assertEqual("source-specific", result["classification"])
        self.assertEqual(sorted(signature), result["reproduced_slowdown_phases"])

    def test_timestamp_invalid_preempts_performance_attribution(self):
        baseline = self.base_durations()
        target = self.make_enriched(baseline)
        target["clock_evidence"] = {
            "log_timestamps_non_monotonic": False,
            "api_log_disagreement": True,
        }
        target["comparison_eligible"] = False
        target["comparison_ineligible_reasons"] = ["Actions API and job-log clocks disagree"]
        result = observation.classify_observations(
            target,
            [self.make_enriched(baseline, attempt="2"), self.make_enriched(baseline, attempt="3")],
        )
        self.assertEqual("timestamp-invalid", result["classification"])

    def test_insufficient_comparators_and_phase_conflicts_are_inconclusive(self):
        baseline = self.base_durations()
        result = observation.classify_observations(
            self.make_enriched(baseline),
            [self.make_enriched(baseline, attempt="2")],
        )
        self.assertEqual("mixed/inconclusive", result["classification"])
        changed = self.make_enriched(baseline, attempt="2")
        changed["observation"]["phases"][0]["command_sha256"] = "f" * 64
        result = observation.classify_observations(
            self.make_enriched(baseline),
            [changed, self.make_enriched(baseline, attempt="3")],
        )
        self.assertEqual("mixed/inconclusive", result["classification"])
        self.assertTrue(any("command identity differs" in reason for reason in result["classification_reasons"]))

    def test_changed_source_image_compiler_workers_job_or_provider_are_rejected(self):
        baseline = self.base_durations()
        mutations = (
            ("source.commit", "f" * 40),
            ("runner.image_version", "different-image"),
            ("toolchain.compiler.sha256", "e" * 64),
            ("configuration.differential_workers", 8),
            ("workflow.job_name", "Linux x86-64 native"),
            ("runner_log.provisioner_version", "different-provisioner"),
            ("runner_log.azure_region", "eastus"),
        )
        for path, value in mutations:
            with self.subTest(path=path):
                changed = self.make_enriched(baseline, attempt="2")
                components = path.split(".")
                if components[0] == "toolchain":
                    current = changed["observation"]
                elif components[0] in ("source", "runner", "configuration", "workflow"):
                    current = changed["observation"]["identity"]
                else:
                    current = changed
                for component in components[:-1]:
                    current = current[component]
                current[components[-1]] = value
                result = observation.classify_observations(
                    self.make_enriched(baseline),
                    [changed, self.make_enriched(baseline, attempt="3")],
                )
                self.assertEqual("mixed/inconclusive", result["classification"])
                self.assertTrue(
                    any("identity mismatch" in reason for reason in result["classification_reasons"])
                )

    def test_non_successful_observation_is_retained_but_inconclusive(self):
        baseline = self.base_durations()
        result = observation.classify_observations(
            self.make_enriched(baseline, eligible=False),
            [self.make_enriched(baseline, attempt="2"), self.make_enriched(baseline, attempt="3")],
        )
        self.assertEqual("mixed/inconclusive", result["classification"])
        self.assertTrue(any("target:" in reason for reason in result["classification_reasons"]))

    def test_enrichment_rejects_clock_disagreement_and_image_contradiction(self):
        baseline = self.make_enriched(self.base_durations())["observation"]
        observation_path = self.root / "observation.json"
        observation_path.write_bytes(observation.canonical_bytes(baseline))
        job_log = self.root / "job.log"
        job_log.write_text(
            "2026-09-19T00:00:10.0000000Z Current runner version: '2.337.0'\n"
            "2026-09-19T00:00:10.1000000Z ##[group]Runner Image Provisioner\n"
            "2026-09-19T00:00:10.2000000Z Hosted Compute Agent\n"
            "2026-09-19T00:00:10.3000000Z Version: 20260819.586\n"
            "2026-09-19T00:00:10.4000000Z Azure Region: westus\n"
            "2026-09-19T00:00:10.5000000Z ##[endgroup]\n"
            "2026-09-19T00:00:10.6000000Z ##[group]Runner Image\n"
            "2026-09-19T00:00:10.7000000Z Image: macos-26\n"
            "2026-09-19T00:00:10.8000000Z Version: wrong-image\n"
            "2026-09-19T00:00:10.9000000Z ##[endgroup]\n"
            "2026-09-19T00:00:09.0000000Z backwards\n",
            encoding="utf-8",
        )
        metadata = self.root / "job.json"
        metadata.write_text(
            json.dumps(
                {
                    "id": 1,
                    "run_id": 10,
                    "run_attempt": 1,
                    "name": "macOS x86-64 native",
                    "status": "completed",
                    "conclusion": "success",
                    "started_at": "2026-09-19T00:01:00Z",
                    "completed_at": "2026-09-19T00:02:00Z",
                }
            ),
            encoding="utf-8",
        )
        output = self.root / "enriched.json"
        args = observation.parser().parse_args(
            [
                "enrich",
                "--observation",
                str(observation_path),
                "--job-log",
                str(job_log),
                "--job-metadata",
                str(metadata),
                "--output",
                str(output),
            ]
        )
        self.assertEqual(0, args.function(args))
        enriched = json.loads(output.read_text(encoding="utf-8"))
        self.assertFalse(enriched["comparison_eligible"])
        self.assertIn("job-log image version contradicts artifact identity", enriched["comparison_ineligible_reasons"])
        self.assertIn("job-log timestamps are non-monotonic", enriched["comparison_ineligible_reasons"])
        self.assertIn("Actions API and job-log clocks disagree", enriched["comparison_ineligible_reasons"])

    def test_workflow_keeps_native_required_step_and_fail_closed_contracts(self):
        repository_root = Path(__file__).resolve().parents[1]
        workflow_path = repository_root / ".github" / "workflows" / "ci.yml"
        if not workflow_path.is_file():
            self.skipTest("repository workflow is not present in the standalone test fixture")
        workflow = workflow_path.read_text(encoding="utf-8")
        self.assertIn(
            "BUSTER_CI_REQUIRED: ${{ matrix.platform == 'windows' && 'modes_windows' || 'modes differential' }}",
            workflow,
        )
        self.assertIn(
            "BUSTER_CI_REQUIRED: ${{ matrix.platform == 'windows' && 'modes_windows pack' || 'modes differential pack' }}",
            workflow,
        )
        self.assertIn("tools/ci_native_observation.py init", workflow)
        self.assertIn("tools/ci_native_observation.py finalize", workflow)
        self.assertIn(
            'observation_root="$RUNNER_TEMP/buster-ci/native-observation"',
            workflow,
        )
        self.assertIn(
            "if: ${{ !cancelled() && steps.checkout.outcome == 'success' }}",
            workflow,
        )
        self.assertIn("${{ runner.temp }}/native-ci-upload/", workflow)
        native = workflow[workflow.index("  native:"):workflow.index("  mobile:")]
        self.assertNotRegex(native, r"(?m)^\s*continue-on-error:")


if __name__ == "__main__":
    unittest.main()
