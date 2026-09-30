#!/usr/bin/env python3
"""Network-free regressions for analyzer provenance and comparison selection."""
from __future__ import annotations

import io
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import textwrap
import unittest

import analyzer_reference as provenance

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "tools/analyzer_reference.py"
DRIVER_DEPENDENCIES = (
    "build.c",
    "src/buster/lib/base.h",
    "src/buster/lib/driver.h",
    "src/buster/lib/transitive.h",
    "tools/clang_analyze.c",
)


class AnalyzerSelectionTests(unittest.TestCase):
    @staticmethod
    def command(arguments, *, cwd=None, env=None, check=True, timeout=30):
        return subprocess.run(
            [str(argument) for argument in arguments],
            cwd=cwd,
            env=env,
            check=check,
            capture_output=True,
            text=True,
            timeout=timeout,
        )

    @classmethod
    def git(cls, repository, *arguments):
        return cls.command(["git", "-C", repository, *arguments]).stdout.strip()

    @classmethod
    def commit(cls, repository, message):
        cls.git(repository, "add", "-A")
        cls.git(repository, "commit", "--quiet", "-m", message)
        return cls.git(repository, "rev-parse", "HEAD")

    @staticmethod
    def analyzer_step(name):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        block = text.split("      - name: " + name + "\n", 1)[1]
        block = block.split("      - name:", 1)[0]
        return textwrap.dedent(block.split("        run: |\n", 1)[1])

    @classmethod
    def fixture_repository(cls, root):
        cls.command(["git", "init", "--quiet", root])
        cls.git(root, "config", "user.name", "CI fixture")
        cls.git(root, "config", "user.email", "ci@example.invalid")
        (root / "src/buster/lib").mkdir(parents=True)
        (root / "tools").mkdir()
        (root / ".github/workflows").mkdir(parents=True)
        shutil.copy2(HELPER, root / "tools/analyzer_reference.py")
        (root / ".github/workflows/ci.yml").write_text("name: fixture\n")
        (root / "build.c").write_text(
            "#include <buster/lib/base.h>\n"
            "#include <buster/lib/driver.h>\n"
            "#include \"tools/clang_analyze.c\"\n"
            "int main(void) { return fixture_value; }\n"
        )
        (root / "src/buster/lib/base.h").write_text("enum { fixture_value = 0 };\n")
        (root / "src/buster/lib/driver.h").write_text(
            "#include <buster/lib/transitive.h>\n"
        )
        (root / "src/buster/lib/transitive.h").write_text("/* BASELINE */\n")
        (root / "tools/clang_analyze.c").write_text("/* analyzer implementation */\n")
        return cls.commit(root, "baseline")

    @staticmethod
    def write_depfile(path, target="driver", dependencies=DRIVER_DEPENDENCIES):
        path.write_text(f"{target}: " + " \\\n  ".join(dependencies) + "\n")

    @staticmethod
    def materialize(repository, revision, destination):
        archive = subprocess.check_output(
            ["git", "-C", str(repository), "archive", "--format=tar", revision]
        )
        destination.mkdir()
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as stream:
            stream.extractall(destination, filter="data")

    @staticmethod
    def write_manifest(repository, revision, root, depfile, output):
        manifest = provenance.build_manifest(repository, revision, root, depfile)
        provenance.write_manifest(output, manifest)
        return manifest

    @staticmethod
    def select(repository, event, requested, candidate, reference,
               candidate_manifest, reference_manifest, output):
        record = provenance.select_campaign(
            repository,
            event,
            "true" if requested else "false",
            candidate,
            reference,
            candidate_manifest,
            reference_manifest,
        )
        provenance.write_atomic(output, record)
        return provenance.load_selection(output)

    @staticmethod
    def parse_exports(path):
        return dict(line.split("=", 1) for line in path.read_text().splitlines())

    @staticmethod
    def fake_clang(directory):
        directory.mkdir()
        clang = directory / "clang"
        clang.write_text(
            """#!/bin/sh
set -eu
if [ "${1-}" = "--version" ]; then
    printf '%s\\n' 'fixture clang'
    exit 0
fi
depfile=
output=
previous=
for argument in "$@"; do
    if [ "$previous" = -MF ]; then depfile=$argument; fi
    if [ "$previous" = -o ]; then output=$argument; fi
    previous=$argument
done
printf '%s: build.c src/buster/lib/base.h src/buster/lib/driver.h src/buster/lib/transitive.h tools/clang_analyze.c\\n' "$output" > "$depfile"
printf 'cwd=%s header=%s\\n' "$PWD" "$(cat src/buster/lib/transitive.h)" >> "$CLANG_LOG"
cat > "$output" <<'DRIVER'
#!/bin/sh
if [ -n "${DRIVER_LOG-}" ]; then printf '%s\\n' "$*" >> "$DRIVER_LOG"; fi
case "${FAIL_MODE-}:$*" in
    candidate:*) exit 32 ;;
    reference:*--baseline-driver*) exit 31 ;;
esac
DRIVER
chmod +x "$output"
"""
        )
        clang.chmod(0o755)
        return clang

    @unittest.skipIf(os.name == "nt", "The analyzer policy uses the Unix hosted runner")
    def test_compiler_dependency_manifest_and_policy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repository"
            baseline = self.fixture_repository(root)
            baseline_root = Path(temporary) / "baseline"
            self.materialize(root, baseline, baseline_root)
            depfile = Path(temporary) / "driver.d"
            self.write_depfile(depfile)
            baseline_path = Path(temporary) / "baseline.json"
            baseline_manifest = self.write_manifest(
                root, baseline, baseline_root, depfile, baseline_path
            )
            self.assertTrue(baseline_manifest["complete"])
            self.assertEqual(
                [entry["path"] for entry in baseline_manifest["dependencies"]],
                list(DRIVER_DEPENDENCIES),
            )

            (root / "README.md").write_text("unrelated\n")
            unrelated = self.commit(root, "unrelated source change")
            unrelated_path = Path(temporary) / "unrelated.json"
            unrelated_manifest = self.write_manifest(
                root, unrelated, root, depfile, unrelated_path
            )
            self.assertEqual(
                unrelated_manifest["closure_sha256"], baseline_manifest["closure_sha256"]
            )
            record = Path(temporary) / "unrelated-selection.txt"
            fields = self.select(
                root, "pull_request", False, unrelated, baseline,
                unrelated_path, baseline_path, record,
            )
            self.assertEqual(fields["selection"], "skip")
            self.assertEqual(fields["reason"], "unchanged-driver-closure")

            (root / "src/buster/lib/transitive.h").write_text("/* CANDIDATE */\n")
            changed = self.commit(root, "change driver dependency")
            changed_path = Path(temporary) / "changed.json"
            self.write_manifest(root, changed, root, depfile, changed_path)
            fields = self.select(
                root, "pull_request", False, changed, baseline,
                changed_path, baseline_path, Path(temporary) / "changed-selection.txt",
            )
            self.assertEqual(fields["selection"], "compare")
            self.assertEqual(fields["reason"], "changed-driver-closure")

            malformed = Path(temporary) / "malformed.d"
            malformed.write_text("not a dependency file\n")
            uncertain_path = Path(temporary) / "uncertain.json"
            uncertain = self.write_manifest(root, changed, root, malformed, uncertain_path)
            self.assertFalse(uncertain["complete"])
            fields = self.select(
                root, "pull_request", False, changed, baseline,
                uncertain_path, baseline_path, Path(temporary) / "uncertain-selection.txt",
            )
            self.assertEqual(fields["reason"], "provenance-uncertain")

            fields = self.select(
                root, "push", False, changed, changed,
                changed_path, changed_path, Path(temporary) / "push-selection.txt",
            )
            self.assertEqual(fields["reason"], "same-revision")
            fields = self.select(
                root, "merge_group", False, changed, changed,
                changed_path, changed_path, Path(temporary) / "merge-selection.txt",
            )
            self.assertEqual(fields["reason"], "event-requires-comparison")
            fields = self.select(
                root, "workflow_dispatch", True, changed, changed,
                changed_path, changed_path, Path(temporary) / "requested-selection.txt",
            )
            self.assertEqual(fields["reason"], "requested")

            tampered = json.loads(changed_path.read_text())
            tampered["dependencies"][0]["oid"] = "0" * 40
            changed_path.write_text(json.dumps(tampered, sort_keys=True, indent=2) + "\n")
            with self.assertRaises(provenance.ProvenanceError):
                provenance.select_campaign(
                    root, "push", "false", changed, changed, changed_path, changed_path
                )

    @unittest.skipIf(os.name == "nt", "The analyzer workflow uses the Unix hosted runner")
    def test_historical_bootstrap_and_comparison_campaign(self):
        bootstrap = self.analyzer_step("Bootstrap candidate and select reference build driver")
        campaign = self.analyzer_step("Compare reference analysis and aggregate all module shards")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repository"
            baseline = self.fixture_repository(root)
            (root / "src/buster/lib/transitive.h").write_text("/* CANDIDATE */\n")
            self.commit(root, "candidate dependency")
            fake_bin = Path(temporary) / "fake-bin"
            self.fake_clang(fake_bin)
            clang_log = Path(temporary) / "clang.log"
            runner = Path(temporary) / "runner"
            runner.mkdir()
            environment = dict(
                os.environ,
                BASELINE_REVISION=baseline,
                EVENT_NAME="pull_request",
                COMPARISON_REQUESTED="false",
                RUNNER_TEMP=str(runner),
                GITHUB_WORKSPACE=str(root),
                GITHUB_ENV=str(runner / "environment"),
                GITHUB_STEP_SUMMARY=str(runner / "summary"),
                CLANG_LOG=str(clang_log),
                PATH=str(fake_bin) + os.pathsep + os.environ["PATH"],
            )
            result = self.command(
                ["bash", "--noprofile", "--norc", "-c", bootstrap],
                cwd=root,
                env=environment,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            lines = clang_log.read_text().splitlines()
            self.assertEqual(len(lines), 2)
            self.assertIn("header=/* CANDIDATE */", lines[0])
            self.assertIn("/reference-tree", lines[1])
            self.assertIn("header=/* BASELINE */", lines[1])
            exports = self.parse_exports(runner / "environment")
            self.assertEqual(exports["ANALYZER_COMPARISON_SELECTION"], "compare")
            self.assertEqual(exports["ANALYZER_COMPARISON_REASON"], "changed-driver-closure")
            self.assertEqual(exports["ANALYZER_REFERENCE_MATERIALIZED"], "true")
            self.assertTrue((root / "build/analyzer-baseline").is_file())

            driver_log = Path(temporary) / "driver.log"
            driver = root / "build/analyzer-driver"
            driver.write_text(
                """#!/bin/sh
printf '%s\\n' "$*" >> "$DRIVER_LOG"
case "${FAIL_MODE-}:$*" in
    fail:*--baseline-driver*) exit 31 ;;
esac
"""
            )
            driver.chmod(0o755)
            campaign_environment = dict(
                os.environ,
                BASELINE_REVISION=baseline,
                EVENT_NAME="pull_request",
                COMPARISON_REQUESTED="false",
                RUNNER_TEMP=str(runner),
                GITHUB_WORKSPACE=str(root),
                DRIVER_LOG=str(driver_log),
                FAIL_MODE="",
                **exports,
            )
            result = self.command(
                ["bash", "--noprofile", "--norc", "-c", campaign],
                cwd=root,
                env=campaign_environment,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = driver_log.read_text().splitlines()
            self.assertEqual(len(calls), 2)
            self.assertIn("--baseline-driver build/analyzer-baseline", calls[0])
            self.assertIn("--aggregate", calls[1])
            self.assertFalse((runner / "buster-analyzer/reference-tree").exists())

    @unittest.skipIf(os.name == "nt", "The analyzer workflow uses the Unix hosted runner")
    def test_unchanged_closure_skips_and_tampering_fails_closed(self):
        bootstrap = self.analyzer_step("Bootstrap candidate and select reference build driver")
        campaign = self.analyzer_step("Compare reference analysis and aggregate all module shards")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repository"
            baseline = self.fixture_repository(root)
            (root / "README.md").write_text("unrelated\n")
            self.commit(root, "unrelated candidate")
            fake_bin = Path(temporary) / "fake-bin"
            self.fake_clang(fake_bin)
            runner = Path(temporary) / "runner"
            runner.mkdir()
            common = dict(
                os.environ,
                BASELINE_REVISION=baseline,
                EVENT_NAME="pull_request",
                COMPARISON_REQUESTED="false",
                RUNNER_TEMP=str(runner),
                GITHUB_WORKSPACE=str(root),
                GITHUB_ENV=str(runner / "environment"),
                GITHUB_STEP_SUMMARY=str(runner / "summary"),
                CLANG_LOG=str(Path(temporary) / "clang.log"),
                PATH=str(fake_bin) + os.pathsep + os.environ["PATH"],
            )
            result = self.command(
                ["bash", "--noprofile", "--norc", "-c", bootstrap],
                cwd=root,
                env=common,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            exports = self.parse_exports(runner / "environment")
            self.assertEqual(exports["ANALYZER_COMPARISON_SELECTION"], "skip")
            self.assertEqual(exports["ANALYZER_COMPARISON_REASON"], "unchanged-driver-closure")
            self.assertFalse((root / "build/analyzer-baseline").exists())

            driver_log = Path(temporary) / "driver.log"
            driver = root / "build/analyzer-driver"
            driver.write_text("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$DRIVER_LOG\"\n")
            driver.chmod(0o755)
            campaign_environment = dict(
                common,
                DRIVER_LOG=str(driver_log),
                ANALYZER_COMPARISON_SELECTION=exports["ANALYZER_COMPARISON_SELECTION"],
                ANALYZER_COMPARISON_REASON=exports["ANALYZER_COMPARISON_REASON"],
                ANALYZER_REFERENCE_MATERIALIZED=exports["ANALYZER_REFERENCE_MATERIALIZED"],
            )
            result = self.command(
                ["bash", "--noprofile", "--norc", "-c", campaign],
                cwd=root,
                env=campaign_environment,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = driver_log.read_text().splitlines()
            self.assertEqual(len(calls), 2)
            self.assertNotIn("--baseline-driver", calls[0])
            self.assertIn("--aggregate", calls[1])

            # Recreate evidence, corrupt the retained selection, and require a
            # failure before the analyzer driver launches.
            shutil.rmtree(runner)
            runner.mkdir()
            common["RUNNER_TEMP"] = str(runner)
            common["GITHUB_ENV"] = str(runner / "environment")
            common["GITHUB_STEP_SUMMARY"] = str(runner / "summary")
            result = self.command(
                ["bash", "--noprofile", "--norc", "-c", bootstrap],
                cwd=root,
                env=common,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            record = runner / "buster-analyzer/comparison-selection.txt"
            record.write_text(record.read_text().replace("selection=skip", "selection=compare"))
            driver_log.unlink(missing_ok=True)
            exports = self.parse_exports(runner / "environment")
            campaign_environment.update(
                RUNNER_TEMP=str(runner),
                ANALYZER_COMPARISON_SELECTION=exports["ANALYZER_COMPARISON_SELECTION"],
                ANALYZER_COMPARISON_REASON=exports["ANALYZER_COMPARISON_REASON"],
                ANALYZER_REFERENCE_MATERIALIZED=exports["ANALYZER_REFERENCE_MATERIALIZED"],
            )
            result = self.command(
                ["bash", "--noprofile", "--norc", "-c", campaign],
                cwd=root,
                env=campaign_environment,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(driver_log.exists())

    def materialization(self, repository, event, requested, candidate, reference, manifest=None):
        evidence = ["--candidate-manifest", manifest] if manifest is not None else []
        return self.command(
            [sys.executable, HELPER, "materialization",
             "--repository", repository, "--event", event, "--requested", requested,
             "--candidate-revision", candidate, "--reference-revision", reference, *evidence],
            check=False,
        )

    @unittest.skipIf(os.name == "nt", "The analyzer policy uses the Unix hosted runner")
    def test_materialization_matches_selection_policy_and_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repository"
            baseline = self.fixture_repository(root)
            (root / "README.md").write_text("unrelated\n")
            candidate = self.commit(root, "unrelated candidate")
            depfile = Path(temporary) / "driver.d"
            self.write_depfile(depfile)
            manifests = {}
            for revision in (baseline, candidate):
                path = Path(temporary) / f"{revision}.json"
                self.write_manifest(root, revision, root, depfile, path)
                manifests[revision] = path
            for event in ("push", "workflow_dispatch", "pull_request", "merge_group", "schedule"):
                for requested in (False, True):
                    if requested and event != "workflow_dispatch":
                        continue
                    for reference in (candidate, baseline):
                        with self.subTest(event=event, requested=requested, same=reference == candidate):
                            result = self.materialization(
                                root, event, "true" if requested else "false", "HEAD", reference
                            )
                            self.assertEqual(result.returncode, 0, result.stderr)
                            fields = self.select(
                                root, event, requested, candidate, reference,
                                manifests[candidate], manifests[reference],
                                Path(temporary) / "selection.txt",
                            )
                            expected = "false" if fields["reason"] == "same-revision" else "true"
                            self.assertEqual(result.stdout, expected + "\n")
            self.assertEqual(
                self.materialization(root, "push", "false", candidate, candidate).stdout, "false\n"
            )
            for event, requested, reference in (
                ("push", "true", candidate),
                ("push", "maybe", candidate),
                ("push;", "false", candidate),
                ("push", "false", "0" * 40),
            ):
                with self.subTest(invalid=(event, requested, reference)):
                    result = self.materialization(root, event, requested, candidate, reference)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(result.stdout, "")
                    self.assertIn("error:", result.stderr)

    @unittest.skipIf(os.name == "nt", "The analyzer workflow uses the Unix hosted runner")
    def test_same_revision_push_skips_reference_and_rejects_stale_materialization(self):
        bootstrap = self.analyzer_step("Bootstrap candidate and select reference build driver")
        campaign = self.analyzer_step("Compare reference analysis and aggregate all module shards")
        for step in (bootstrap, campaign):
            self.assertIn("python3 tools/analyzer_reference.py materialization", step)
            self.assertNotIn('"$EVENT_NAME" == push', step)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repository"
            head = self.fixture_repository(root)
            fake_bin = Path(temporary) / "fake-bin"
            self.fake_clang(fake_bin)
            clang_log = Path(temporary) / "clang.log"
            runner = Path(temporary) / "runner"
            runner.mkdir()
            common = dict(
                os.environ,
                BASELINE_REVISION=head,
                EVENT_NAME="push",
                COMPARISON_REQUESTED="false",
                RUNNER_TEMP=str(runner),
                GITHUB_WORKSPACE=str(root),
                GITHUB_ENV=str(runner / "environment"),
                GITHUB_STEP_SUMMARY=str(runner / "summary"),
                CLANG_LOG=str(clang_log),
                PATH=str(fake_bin) + os.pathsep + os.environ["PATH"],
            )
            result = self.command(
                ["bash", "--noprofile", "--norc", "-c", bootstrap], cwd=root, env=common, check=False
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(len(clang_log.read_text().splitlines()), 1)
            exports = self.parse_exports(runner / "environment")
            self.assertEqual(exports["ANALYZER_REFERENCE_MATERIALIZED"], "false")
            self.assertEqual(exports["ANALYZER_COMPARISON_SELECTION"], "skip")
            self.assertEqual(exports["ANALYZER_COMPARISON_REASON"], "same-revision")
            self.assertFalse((runner / "buster-analyzer/reference-tree").exists())
            self.assertFalse((root / "build/analyzer-baseline").exists())

            driver_log = Path(temporary) / "driver.log"
            driver = root / "build/analyzer-driver"
            driver.write_text("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$DRIVER_LOG\"\n")
            driver.chmod(0o755)
            campaign_environment = dict(common, DRIVER_LOG=str(driver_log), **exports)
            campaign_environment["ANALYZER_REFERENCE_MATERIALIZED"] = "true"
            result = self.command(
                ["bash", "--noprofile", "--norc", "-c", campaign],
                cwd=root, env=campaign_environment, check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("materialization evidence is missing or stale", result.stdout + result.stderr)
            self.assertFalse(driver_log.exists())

            campaign_environment["ANALYZER_REFERENCE_MATERIALIZED"] = "false"
            result = self.command(
                ["bash", "--noprofile", "--norc", "-c", campaign],
                cwd=root, env=campaign_environment, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = driver_log.read_text().splitlines()
            self.assertEqual(len(calls), 2)
            self.assertNotIn("--baseline-driver", calls[0])

    def merge_group_fixture(self, temporary, event="merge_group", requested="false"):
        root = Path(temporary) / "repository"
        head = self.fixture_repository(root)
        fake_bin = Path(temporary) / "fake-bin"
        self.fake_clang(fake_bin)
        runner = Path(temporary) / "runner"
        runner.mkdir()
        environment = dict(
            os.environ, BASELINE_REVISION=head, EVENT_NAME=event,
            COMPARISON_REQUESTED=requested, RUNNER_TEMP=str(runner),
            GITHUB_WORKSPACE=str(root), GITHUB_ENV=str(runner / "environment"),
            GITHUB_STEP_SUMMARY=str(runner / "summary"),
            CLANG_LOG=str(Path(temporary) / "clang.log"),
            DRIVER_LOG=str(Path(temporary) / "driver.log"),
            PATH=str(fake_bin) + os.pathsep + os.environ["PATH"],
        )
        bootstrap = self.analyzer_step("Bootstrap candidate and select reference build driver")
        result = self.command(["bash", "--noprofile", "--norc", "-c", bootstrap],
                              cwd=root, env=environment, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        environment.update(self.parse_exports(runner / "environment"))
        return root, head, runner / "buster-analyzer", environment

    @unittest.skipIf(os.name == "nt", "The analyzer workflow uses the Unix hosted runner")
    def test_same_driver_merge_group_materialization_and_campaign(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, head, evidence, environment = self.merge_group_fixture(temporary)
            candidate = evidence / "candidate-driver-provenance.json"
            manifest = json.loads(candidate.read_text())
            self.assertTrue(manifest["complete"])
            self.assertIsNotNone(manifest["execution_context"])
            self.assertEqual(candidate.read_bytes(), (evidence / "reference-driver-provenance.json").read_bytes())
            self.assertEqual(environment["ANALYZER_REFERENCE_MATERIALIZED"], "false")
            self.assertEqual(environment["ANALYZER_COMPARISON_REASON"], "same-driver-merge-group")
            self.assertEqual(len(Path(environment["CLANG_LOG"]).read_text().splitlines()), 1)
            self.assertFalse((evidence / "reference-tree").exists())
            self.assertFalse((root / "build/analyzer-baseline").exists())
            self.assertEqual(self.materialization(root, "merge_group", "false", head, head, candidate).stdout, "false\n")
            # Without proof, equality of revision strings is insufficient.
            self.assertEqual(self.materialization(root, "merge_group", "false", head, head).stdout, "true\n")
            campaign = self.analyzer_step("Compare reference analysis and aggregate all module shards")
            result = self.command(["bash", "--noprofile", "--norc", "-c", campaign],
                                  cwd=root, env=environment, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = Path(environment["DRIVER_LOG"]).read_text().splitlines()
            self.assertEqual(len(calls), 2)
            self.assertNotIn("--baseline-driver", calls[0])
            self.assertIn("--shards 8 --jobs 2", calls[0])
            self.assertIn("--aggregate", calls[1])

    @unittest.skipIf(os.name == "nt", "The analyzer workflow uses the Unix hosted runner")
    def test_merge_group_unknown_or_mismatched_proof_compares(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, head, evidence, _ = self.merge_group_fixture(temporary)
            candidate = evidence / "candidate-driver-provenance.json"
            original = json.loads(candidate.read_text())
            reference = evidence / "reference-driver-provenance.json"
            output = evidence / "test-selection.txt"
            for field in ("driver_sha256", "compiler_sha256", "compiler_version_sha256",
                          "environment_sha256", "root", "driver", "compiler", "compile_command"):
                with self.subTest(mismatch=field):
                    changed = copy.deepcopy(original)
                    context = changed["execution_context"]
                    if field.endswith("sha256"):
                        context[field] = "0" * 64
                    elif field == "compile_command":
                        context[field].append("-DCHANGED")
                    else:
                        context[field] += "-different"
                    provenance.write_manifest(reference, changed)
                    fields = self.select(root, "merge_group", False, head, head, candidate, reference, output)
                    self.assertEqual(fields["selection"], "compare")
            for state in ("incomplete", "unknown-context", "changed-command", "missing-policy", "missing-root"):
                with self.subTest(state=state):
                    changed = copy.deepcopy(original)
                    if state == "incomplete":
                        changed["complete"] = False
                        changed["issues"] = ["fixture uncertainty"]
                    elif state == "unknown-context":
                        changed["execution_context"] = None
                    elif state == "changed-command":
                        changed["execution_context"]["compile_command"].append("-DCHANGED")
                    elif state == "missing-policy":
                        path = changed["policy_inputs"][0]["path"]
                        changed["policy_inputs"][0] = {"path": path, "state": "missing"}
                    else:
                        changed["dependencies"] = [entry for entry in changed["dependencies"] if entry["path"] != "build.c"]
                    changed["closure_sha256"] = provenance.closure_fingerprint(changed["dependencies"], changed["policy_inputs"])
                    provenance.write_manifest(candidate, changed)
                    provenance.write_manifest(reference, changed)
                    fields = self.select(root, "merge_group", False, head, head, candidate, reference, output)
                    self.assertEqual(fields["selection"], "compare")
                    self.assertEqual(self.materialization(root, "merge_group", "false", head, head, candidate).stdout, "true\n")
            provenance.write_manifest(candidate, original)
            provenance.write_manifest(reference, original)
            for event in ("pull_request", "schedule"):
                fields = self.select(root, event, False, head, head, candidate, reference, output)
                self.assertEqual(fields["selection"], "compare")
            fields = self.select(root, "workflow_dispatch", True, head, head, candidate, reference, output)
            self.assertEqual(fields["reason"], "requested")
            (root / "README.md").write_text("distinct revision, identical driver closure\n")
            distinct = self.commit(root, "unrelated")
            with self.assertRaises(provenance.ProvenanceError):
                self.select(root, "merge_group", False, distinct, head, candidate, reference, output)
            changed = copy.deepcopy(original)
            changed["revision"] = distinct
            changed["tree"] = self.git(root, "rev-parse", "HEAD^{tree}")
            provenance.write_manifest(candidate, changed)
            fields = self.select(root, "merge_group", False, distinct, head, candidate, reference, output)
            self.assertEqual(fields["selection"], "compare")

    @unittest.skipIf(os.name == "nt", "The analyzer workflow uses the Unix hosted runner")
    def test_merge_group_recomputation_rejects_stale_or_tampered_evidence(self):
        campaign = self.analyzer_step("Compare reference analysis and aggregate all module shards")
        for mutation in ("driver", "clang", "environment", "manifest", "depfile", "selection", "revision", "event", "export", "symlink"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root, _, evidence, environment = self.merge_group_fixture(temporary)
                if mutation in ("driver", "clang"):
                    path = root / "build/analyzer-driver" if mutation == "driver" else Path(temporary) / "fake-bin/clang"
                    path.write_text(path.read_text() + "# changed bytes\n")
                elif mutation == "environment":
                    environment["CPATH"] = "/different/include"
                elif mutation == "manifest":
                    path = evidence / "candidate-driver-provenance.json"
                    value = json.loads(path.read_text())
                    value["execution_context"]["compiler_sha256"] = "0" * 64
                    provenance.write_manifest(path, value)
                elif mutation == "depfile":
                    (evidence / "candidate-driver.d").write_text("bad dependencies\n")
                elif mutation == "selection":
                    path = evidence / "comparison-selection.txt"
                    path.write_text(path.read_text().replace("selection=skip", "selection=compare"))
                elif mutation == "revision":
                    (root / "README.md").write_text("stale revision\n")
                    self.commit(root, "advance head")
                elif mutation == "event":
                    environment["EVENT_NAME"] = "pull_request"
                elif mutation == "export":
                    environment["ANALYZER_REFERENCE_MATERIALIZED"] = "true"
                else:
                    (root / "build/analyzer-baseline").symlink_to("absent")
                result = self.command(["bash", "--noprofile", "--norc", "-c", campaign],
                                      cwd=root, env=environment, check=False)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse(Path(environment["DRIVER_LOG"]).exists())

    @unittest.skipIf(os.name == "nt", "The analyzer workflow uses the Unix hosted runner")
    def test_forced_comparison_and_candidate_failures_remain_fatal(self):
        campaign = self.analyzer_step("Compare reference analysis and aggregate all module shards")
        for event, requested, failure in (("merge_group", "false", "candidate"),
                                           ("workflow_dispatch", "true", "candidate"),
                                           ("workflow_dispatch", "true", "reference")):
            with self.subTest(event=event, failure=failure), tempfile.TemporaryDirectory() as temporary:
                root, _, _, environment = self.merge_group_fixture(temporary, event, requested)
                forced = requested == "true"
                self.assertEqual(environment["ANALYZER_REFERENCE_MATERIALIZED"], "true" if forced else "false")
                self.assertEqual(len(Path(environment["CLANG_LOG"]).read_text().splitlines()), 2 if forced else 1)
                environment["FAIL_MODE"] = failure
                result = self.command(["bash", "--noprofile", "--norc", "-c", campaign],
                                      cwd=root, env=environment, check=False)
                self.assertNotEqual(result.returncode, 0)
                calls = Path(environment["DRIVER_LOG"]).read_text().splitlines()
                self.assertEqual(len(calls), 1)
                self.assertEqual("--baseline-driver" in calls[0], forced)


if __name__ == "__main__":
    unittest.main()
