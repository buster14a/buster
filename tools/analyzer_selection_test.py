#!/usr/bin/env python3
"""Network-free regressions for single-pass CI and candidate provenance.

The historical filename is retained by existing workflow-suite registrations.
These tests execute the real workflow bodies with a logging compiler/driver;
native analyzer self-tests separately exercise real child and aggregate behavior.
"""
from __future__ import annotations

import contextlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest

import analyzer_reference as provenance

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "tools/analyzer_reference.py"
BOOTSTRAP = "Bootstrap and identify candidate build driver"
CAMPAIGN = "Analyze candidate and aggregate all module shards"
DRIVER_DEPENDENCIES = (
    "build.c", "src/buster/lib/base.h", "src/buster/lib/driver.h",
    "src/buster/lib/transitive.h", "tools/clang_analyze.c",
)


class AnalyzerSelectionTests(unittest.TestCase):
    @staticmethod
    def command(arguments, *, cwd=None, env=None, check=True, timeout=30):
        return subprocess.run([str(argument) for argument in arguments], cwd=cwd,
                              env=env, check=check, capture_output=True, text=True, timeout=timeout)

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
        return textwrap.dedent(block.split("      - name:", 1)[0].split("        run: |\n", 1)[1])

    @classmethod
    def fixture_repository(cls, root):
        cls.command(["git", "init", "--quiet", root])
        cls.git(root, "config", "user.name", "CI fixture")
        cls.git(root, "config", "user.email", "ci@example.invalid")
        (root / "src/buster/lib").mkdir(parents=True)
        (root / "tools").mkdir()
        (root / ".github/workflows").mkdir(parents=True)
        shutil.copy2(HELPER, root / "tools/analyzer_reference.py")
        shutil.copy2(ROOT / ".github/workflows/ci.yml", root / ".github/workflows/ci.yml")
        (root / "build.c").write_text(
            '#include <buster/lib/base.h>\n#include <buster/lib/driver.h>\n'
            '#include "tools/clang_analyze.c"\nint main(void) { return fixture_value; }\n')
        (root / "src/buster/lib/base.h").write_text("enum { fixture_value = 0 };\n")
        (root / "src/buster/lib/driver.h").write_text("#include <buster/lib/transitive.h>\n")
        (root / "src/buster/lib/transitive.h").write_text("/* BASELINE */\n")
        (root / "tools/clang_analyze.c").write_text("/* analyzer implementation */\n")
        return cls.commit(root, "baseline")

    @staticmethod
    def fake_clang(directory):
        directory.mkdir()
        clang = directory / "clang"
        clang.write_text('''#!/bin/sh
set -eu
if [ "${1-}" = --version ]; then printf '%s\\n' 'fixture clang'; exit 0; fi
if [ "${CLANG_FAIL-}" = 1 ]; then exit 9; fi
depfile=
output=
previous=
for argument in "$@"; do
    if [ "$previous" = -MF ]; then depfile=$argument; fi
    if [ "$previous" = -o ]; then output=$argument; fi
    previous=$argument
done
printf '%s: build.c src/buster/lib/base.h src/buster/lib/driver.h src/buster/lib/transitive.h tools/clang_analyze.c\\n' "$output" > "$depfile"
if [ "${BAD_DEPFILE-}" = 1 ]; then printf 'not a dependency file\\n' > "$depfile"; fi
printf 'cwd=%s\\n' "$PWD" >> "$CLANG_LOG"
cat > "$output" <<'DRIVER'
#!/bin/sh
printf '%s\\n' "$*" >> "$DRIVER_LOG"
case "${FAIL_MODE-}:$*" in
    candidate:*) exit 32 ;;
    aggregate:*--aggregate*) exit 33 ;;
esac
DRIVER
chmod +x "$output"
''')
        clang.chmod(0o755)
        return clang

    @contextlib.contextmanager
    def fixture(self, event="pull_request", ref="refs/pull/1/merge"):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            root = folder / "repository"
            baseline = self.fixture_repository(root)
            self.fake_clang(folder / "bin")
            runner = folder / "runner"
            runner.mkdir()
            environment = dict(os.environ, GITHUB_EVENT_NAME=event, EVENT_NAME=event,
                               GITHUB_REF=ref, BASELINE_REVISION=baseline,
                               COMPARISON_REQUESTED="true", ANALYZER_COMPARISON_SELECTION="compare",
                               ANALYZER_COMPARISON_REASON="changed-driver-closure",
                               RUNNER_TEMP=str(runner), GITHUB_WORKSPACE=str(root),
                               CLANG_LOG=str(folder / "clang.log"), DRIVER_LOG=str(folder / "driver.log"),
                               PATH=str(folder / "bin") + os.pathsep + os.environ["PATH"])
            yield root, folder, environment

    def run_step(self, name, root, environment, check=True):
        return self.command(["bash", "--noprofile", "--norc", "-c", self.analyzer_step(name)],
                            cwd=root, env=environment, check=check)

    @staticmethod
    def lines(path):
        return path.read_text().splitlines() if path.exists() else []

    def assert_one_campaign(self, root, folder, environment):
        self.run_step(BOOTSTRAP, root, environment)
        self.run_step(CAMPAIGN, root, environment)
        self.assertEqual(len(self.lines(folder / "clang.log")), 1)
        calls = self.lines(folder / "driver.log")
        self.assertEqual(len(calls), 2)
        self.assertIn("--shards 8 --jobs 2 --quiet", calls[0])
        self.assertNotIn("--aggregate", calls[0])
        self.assertIn("--aggregate", calls[1])
        self.assertTrue(all("--baseline-driver" not in call for call in calls))
        self.assertFalse((root / "build/analyzer-baseline").exists())
        evidence = folder / "runner/buster-analyzer"
        self.assertFalse((evidence / "reference-tree").exists())
        self.assertFalse((evidence / "reference-driver.d").exists())
        self.assertFalse((evidence / "comparison-selection.txt").exists())

    def test_workflow_has_no_reference_selection_or_dispatch_input(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        analyzer = workflow.split("\n  analyzer:\n", 1)[1].split("\n  complete:\n", 1)[0]
        for removed in ("analyzer_comparison", "--baseline-driver", "ANALYZER_COMPARISON",
                        "BASELINE_REVISION", "reference-tree", "reference-driver", "analyzer-baseline"):
            self.assertNotIn(removed, analyzer)
        self.assertNotIn("analyzer_comparison", workflow)
        for name in (BOOTSTRAP, CAMPAIGN):
            step = analyzer.split("      - name: " + name + "\n", 1)[1].split("      - name:", 1)[0]
            self.assertIn("if: ${{ needs.reuse.outputs.reuse != 'true' }}", step)
            self.assertNotIn("github.event_name", step)
        self.assertEqual(self.analyzer_step(CAMPAIGN).count("build/analyzer-driver clang_analyze "), 2)
        self.assertEqual(self.analyzer_step(CAMPAIGN).count("--aggregate"), 1)

    @unittest.skipIf(os.name == "nt", "The analyzer policy runs on hosted Unix")
    def test_all_events_run_once_even_with_changed_driver_or_workflow(self):
        events = (("pull_request", "refs/pull/1/merge"), ("merge_group", "refs/heads/gh-readonly-queue/main/pr-1"),
                  ("push", "refs/heads/main"), ("push", "refs/tags/v1"),
                  ("workflow_dispatch", "refs/heads/main"))
        changes = ("build.c", "src/buster/lib/transitive.h", "tools/clang_analyze.c", ".github/workflows/ci.yml")
        for event, ref in events:
            for change in changes:
                with self.subTest(event=event, ref=ref, change=change), self.fixture(event, ref) as (root, folder, env):
                    path = root / change
                    with path.open("a") as output:
                        output.write("\n# changed workflow\n" if change.endswith(".yml") else "\n/* changed input */\n")
                    self.commit(root, "changed candidate input")
                    self.assert_one_campaign(root, folder, env)

    @unittest.skipIf(os.name == "nt", "The analyzer policy runs on hosted Unix")
    def test_candidate_manifest_binds_complete_dependencies_and_executable(self):
        with self.fixture() as (root, folder, env):
            self.run_step(BOOTSTRAP, root, env)
            manifest, _ = provenance.load_manifest(folder / "runner/buster-analyzer/candidate-driver-provenance.json")
            self.assertTrue(manifest["complete"])
            self.assertEqual([entry["path"] for entry in manifest["dependencies"]], list(DRIVER_DEPENDENCIES))
            self.assertEqual(manifest["revision"], self.git(root, "rev-parse", "HEAD"))
            self.assertEqual(manifest["tree"], self.git(root, "rev-parse", "HEAD^{tree}"))
            self.assertEqual(manifest["execution_context"]["driver_sha256"],
                             provenance.sha256_bytes((root / "build/analyzer-driver").read_bytes()))

    @unittest.skipIf(os.name == "nt", "The analyzer policy runs on hosted Unix")
    def test_missing_unsafe_or_stale_provenance_never_launches_analysis(self):
        changes = ("manifest-missing", "depfile-missing", "driver-missing", "manifest-symlink", "depfile-symlink",
                   "driver-symlink", "manifest-data", "depfile-data", "source-data", "policy-data", "helper-data",
                   "compiler-data", "driver-data", "environment", "revision", "policy-missing")
        for change in changes:
            with self.subTest(change=change), self.fixture() as (root, folder, env):
                self.run_step(BOOTSTRAP, root, env)
                evidence = folder / "runner/buster-analyzer"
                paths = {"manifest": evidence / "candidate-driver-provenance.json", "depfile": evidence / "candidate-driver.d",
                         "driver": root / "build/analyzer-driver", "source": root / "src/buster/lib/transitive.h",
                         "policy": root / ".github/workflows/ci.yml", "helper": root / "tools/analyzer_reference.py",
                         "compiler": folder / "bin/clang"}
                if change == "environment":
                    env["CPATH"] = "unexpected-include-search"
                elif change == "revision":
                    (root / "README.md").write_text("new commit after bootstrap\n")
                    self.commit(root, "new revision")
                else:
                    name, mutation = change.split("-")
                    path = paths[name]
                    if mutation == "missing":
                        path.unlink()
                    elif mutation == "symlink":
                        saved = path.with_name(path.name + ".saved")
                        path.rename(saved)
                        path.symlink_to(saved)
                    else:
                        with path.open("a") as output:
                            output.write("\n# changed\n" if name in ("helper", "compiler", "driver", "policy") else "\n ")
                result = self.run_step(CAMPAIGN, root, env, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.lines(folder / "driver.log"), [])
                self.assertEqual(len(self.lines(folder / "clang.log")), 1)

    @unittest.skipIf(os.name == "nt", "The analyzer policy runs on hosted Unix")
    def test_bootstrap_failure_or_incomplete_provenance_is_fatal(self):
        for change in ("compiler", "depfile", "source", "policy"):
            with self.subTest(change=change), self.fixture() as (root, folder, env):
                if change == "compiler":
                    env["CLANG_FAIL"] = "1"
                elif change == "depfile":
                    env["BAD_DEPFILE"] = "1"
                elif change == "source":
                    (root / "src/buster/lib/transitive.h").write_text("uncommitted input\n")
                else:
                    (root / ".github/workflows/ci.yml").unlink()
                self.assertNotEqual(self.run_step(BOOTSTRAP, root, env, check=False).returncode, 0)
                self.assertFalse((folder / "runner/buster-analyzer/candidate-driver-provenance.json").exists())
                self.assertEqual(self.lines(folder / "driver.log"), [])

    @unittest.skipIf(os.name == "nt", "The analyzer policy runs on hosted Unix")
    def test_candidate_and_aggregate_failures_remain_fatal(self):
        for failure, calls in (("candidate", 1), ("aggregate", 2)):
            with self.subTest(failure=failure), self.fixture() as (root, folder, env):
                self.run_step(BOOTSTRAP, root, env)
                env["FAIL_MODE"] = failure
                result = self.run_step(CAMPAIGN, root, env, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(len(self.lines(folder / "driver.log")), calls)

    def test_retired_selection_commands_are_refused(self):
        for command in ("select", "materialization", "field"):
            result = self.command([os.sys.executable, HELPER, command], check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("invalid choice", result.stderr)

    def test_historical_selection_decoder_does_not_relabel_records(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "selection.txt"
            fields = {key: "a" * (64 if "sha256" in key else 40) for key in provenance.SELECTION_KEYS}
            fields.update(event="pull_request", requested="false", candidate_complete="true", reference_complete="true")
            for selection, reason in (("compare", "changed-driver-closure"), ("skip", "unchanged-driver-closure")):
                fields.update(selection=selection, reason=reason)
                raw = provenance.SELECTION_SCHEMA + "\n" + "\n".join(key + "=" + fields[key] for key in provenance.SELECTION_KEYS) + "\n"
                path.write_text(raw)
                self.assertEqual(provenance.load_selection(path), fields)
                for malformed in (raw[:-1], raw + "extra=1\n", raw + "\0", raw.replace("requested=false", "requested=maybe")):
                    path.write_text(malformed)
                    with self.assertRaises(provenance.ProvenanceError):
                        provenance.load_selection(path)


if __name__ == "__main__":
    unittest.main()
