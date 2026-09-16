#!/usr/bin/env python3
"""Network-free regressions for analyzer comparison selection."""
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AnalyzerSelectionTests(unittest.TestCase):
    @staticmethod
    def analyzer_step(name):
        text = (ROOT / ".github/workflows/ci.yml").read_text()
        block = text.split("      - name: " + name + "\n", 1)[1]
        block = block.split("      - name:", 1)[0]
        return textwrap.dedent(block.split("        run: |\n", 1)[1])

    @unittest.skipIf(os.name == "nt", "The analyzer workflow policy uses the Unix hosted runner")
    def test_analyzer_comparison_selection_uses_verified_commit_identities(self):
        script = self.analyzer_step("Bootstrap candidate and select reference build driver")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "--quiet", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "CI fixture"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "ci@example.invalid"], check=True)
            (root / "build.c").write_text("int baseline;\n")
            subprocess.run(["git", "-C", root, "add", "build.c"], check=True)
            subprocess.run(["git", "-C", root, "commit", "--quiet", "-m", "baseline"], check=True)
            baseline = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip()
            (root / "build.c").write_text("int candidate;\n")
            subprocess.run(["git", "-C", root, "commit", "--quiet", "-am", "candidate"], check=True)
            candidate = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip()
            fake_bin = root / "fake-bin"
            fake_bin.mkdir()
            clang = fake_bin / "clang"
            clang.write_text("""#!/bin/sh
if [ "$1" = "--version" ]; then
    printf '%s\\n' 'fixture clang'
else
    output=
    while [ "$#" -gt 0 ]; do
        if [ "$1" = "-o" ]; then
            shift
            output=$1
        fi
        shift
    done
    : > "$output"
fi
""")
            clang.chmod(0o755)

            def run(reference, event):
                runner_temp = root / ("runner-" + event + "-" + reference[:8])
                runner_temp.mkdir()
                environment = dict(os.environ, BASELINE_REVISION=reference, EVENT_NAME=event,
                                   RUNNER_TEMP=str(runner_temp),
                                   GITHUB_ENV=str(runner_temp / "environment"),
                                   GITHUB_STEP_SUMMARY=str(runner_temp / "summary"),
                                   PATH=str(fake_bin) + os.pathsep + os.environ["PATH"])
                result = subprocess.run(["bash", "--noprofile", "--norc", "-c", script],
                                        cwd=root, env=environment, capture_output=True, text=True, timeout=30)
                return result, runner_temp

            same, same_temp = run(candidate, "push")
            self.assertEqual(same.returncode, 0, same.stdout + same.stderr)
            self.assertEqual((same_temp / "buster-analyzer/comparison-selection.txt").read_text(),
                             "BUSTER_ANALYZER_COMPARISON_SELECTION_V1\n"
                             "event=push\n"
                             f"candidate_revision={candidate}\nreference_revision={candidate}\n"
                             "selection=skip\nreason=same-revision\n")
            self.assertIn("ANALYZER_COMPARISON_SELECTION=skip\n", (same_temp / "environment").read_text())
            self.assertFalse((root / "build/analyzer-baseline").exists())

            dispatched, dispatched_temp = run(candidate, "workflow_dispatch")
            self.assertEqual(dispatched.returncode, 0, dispatched.stdout + dispatched.stderr)
            self.assertIn("selection=skip\nreason=same-revision\n",
                          (dispatched_temp / "buster-analyzer/comparison-selection.txt").read_text())

            different, different_temp = run(baseline, "pull_request")
            self.assertEqual(different.returncode, 0, different.stdout + different.stderr)
            self.assertIn(f"candidate_revision={candidate}\nreference_revision={baseline}\n",
                          (different_temp / "buster-analyzer/comparison-selection.txt").read_text())
            self.assertIn("selection=compare\nreason=distinct-revisions\n",
                          (different_temp / "buster-analyzer/comparison-selection.txt").read_text())
            self.assertIn("ANALYZER_COMPARISON_SELECTION=compare\n", (different_temp / "environment").read_text())
            self.assertTrue((root / "build/analyzer-baseline").is_file())

            merge_group, merge_group_temp = run(candidate, "merge_group")
            self.assertEqual(merge_group.returncode, 0, merge_group.stdout + merge_group.stderr)
            self.assertIn("selection=compare\nreason=event-requires-comparison\n",
                          (merge_group_temp / "buster-analyzer/comparison-selection.txt").read_text())

            unknown, unknown_temp = run(candidate, "schedule")
            self.assertEqual(unknown.returncode, 0, unknown.stdout + unknown.stderr)
            self.assertIn("selection=compare\nreason=event-requires-comparison\n",
                          (unknown_temp / "buster-analyzer/comparison-selection.txt").read_text())

            invalid, invalid_temp = run("f" * 40, "push")
            self.assertNotEqual(invalid.returncode, 0)
            self.assertFalse((invalid_temp / "buster-analyzer/comparison-selection.txt").exists())

    @unittest.skipIf(os.name == "nt", "The analyzer workflow policy uses the Unix hosted runner")
    def test_analyzer_campaign_rejects_missing_selection_and_keeps_candidate_aggregate(self):
        script = self.analyzer_step("Compare reference analysis and aggregate all module shards")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "--quiet", root], check=True)
            subprocess.run(["git", "-C", root, "config", "user.name", "CI fixture"], check=True)
            subprocess.run(["git", "-C", root, "config", "user.email", "ci@example.invalid"], check=True)
            (root / "build.c").write_text("int candidate;\n")
            subprocess.run(["git", "-C", root, "add", "build.c"], check=True)
            subprocess.run(["git", "-C", root, "commit", "--quiet", "-m", "candidate"], check=True)
            candidate = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip()
            (root / "build").mkdir()
            driver = root / "build/analyzer-driver"
            driver.write_text("""#!/bin/sh
printf '%s\\n' "$*" >> "$DRIVER_LOG"
case "${FAIL_MODE-}:$*" in
    reference:*--baseline-driver*) exit 31 ;;
    candidate:*--aggregate*) ;;
    candidate:*) exit 32 ;;
esac
""")
            driver.chmod(0o755)

            def run(selection, event, baseline, record="valid", fail_mode=""):
                driver_log = root / ("driver-" + selection + "-" + event + "-" + record +
                                     ("-baseline" if baseline else "") + ".log")
                if driver_log.exists():
                    driver_log.unlink()
                baseline_path = root / "build/analyzer-baseline"
                baseline_target = root / "build/analyzer-baseline-target"
                (root / "runner/buster-analyzer").mkdir(parents=True, exist_ok=True)
                if baseline_path.exists() or baseline_path.is_symlink():
                    baseline_path.unlink()
                if baseline_target.exists():
                    baseline_target.unlink()
                if baseline == "symlink":
                    baseline_target.write_text("fixture\n")
                    baseline_target.chmod(0o755)
                    baseline_path.symlink_to(baseline_target.name)
                elif baseline == "dangling":
                    baseline_path.symlink_to("missing-analyzer-baseline")
                elif baseline:
                    baseline_path.write_text("fixture\n")
                    baseline_path.chmod(0o755)
                selection_path = root / "runner/buster-analyzer/comparison-selection.txt"
                if selection_path.exists() or selection_path.is_symlink():
                    selection_path.unlink()
                expected_selection = ("skip" if event in ("push", "workflow_dispatch") else "compare")
                expected_reason = ("same-revision" if expected_selection == "skip" else
                                   "event-requires-comparison")
                fields = ["BUSTER_ANALYZER_COMPARISON_SELECTION_V1", f"event={event}",
                          f"candidate_revision={candidate}", f"reference_revision={candidate}",
                          f"selection={expected_selection}", f"reason={expected_reason}"]
                if record == "tampered":
                    fields[4] = "selection=compare" if expected_selection == "skip" else "selection=skip"
                elif record == "stale":
                    fields[2] = "candidate_revision=" + "f" * 40
                elif record == "wrong-reference":
                    fields[3] = "reference_revision=" + "e" * 40
                elif record == "wrong-reason":
                    fields[5] = "reason=distinct-revisions"
                elif record == "malformed":
                    fields.append("unexpected=field")
                if record == "no-final-lf":
                    selection_path.write_text("\n".join(fields))
                elif record == "nul":
                    selection_path.write_bytes(("\n".join(fields) + "\n").encode() + b"\0")
                elif record == "symlink":
                    target = selection_path.with_name("comparison-selection-target.txt")
                    target.write_text("\n".join(fields) + "\n")
                    selection_path.symlink_to(target.name)
                elif record != "missing":
                    selection_path.write_text("\n".join(fields) + "\n")
                environment = dict(os.environ, ANALYZER_COMPARISON_SELECTION=selection,
                                   BASELINE_REVISION=candidate, EVENT_NAME=event,
                                   RUNNER_TEMP=str(root / "runner"), DRIVER_LOG=str(driver_log),
                                   FAIL_MODE=fail_mode)
                result = subprocess.run(["bash", "--noprofile", "--norc", "-c", script], cwd=root,
                                        env=environment, capture_output=True, text=True, timeout=30)
                lines = driver_log.read_text().splitlines() if driver_log.exists() else []
                return result, lines

            skipped, skipped_lines = run("skip", "push", False)
            self.assertEqual(skipped.returncode, 0, skipped.stdout + skipped.stderr)
            self.assertEqual(len(skipped_lines), 2)
            self.assertNotIn("--baseline-driver", skipped_lines[0])
            self.assertIn("--aggregate", skipped_lines[1])

            compared, compared_lines = run("compare", "merge_group", True)
            self.assertEqual(compared.returncode, 0, compared.stdout + compared.stderr)
            self.assertEqual(len(compared_lines), 2)
            self.assertIn("--baseline-driver build/analyzer-baseline", compared_lines[0])
            self.assertIn("--aggregate", compared_lines[1])
            self.assertEqual(compared_lines[0].replace(" --baseline-driver build/analyzer-baseline", ""),
                             skipped_lines[0])
            self.assertEqual(compared_lines[1], skipped_lines[1])

            for record in ("missing", "malformed", "tampered", "stale", "wrong-reference",
                           "wrong-reason", "no-final-lf", "nul", "symlink"):
                with self.subTest(record=record):
                    rejected, rejected_lines = run("skip", "push", False, record)
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertFalse(rejected_lines)
            missing_export, missing_export_lines = run("", "push", False)
            self.assertNotEqual(missing_export.returncode, 0)
            self.assertFalse(missing_export_lines)
            stale_baseline, stale_baseline_lines = run("skip", "push", True)
            self.assertNotEqual(stale_baseline.returncode, 0)
            self.assertFalse(stale_baseline_lines)
            absent, absent_lines = run("compare", "merge_group", False)
            self.assertNotEqual(absent.returncode, 0)
            self.assertFalse(absent_lines)
            linked, linked_lines = run("compare", "merge_group", "symlink")
            self.assertNotEqual(linked.returncode, 0)
            self.assertFalse(linked_lines)
            dangling, dangling_lines = run("skip", "push", "dangling")
            self.assertNotEqual(dangling.returncode, 0)
            self.assertFalse(dangling_lines)
            reference_failure, reference_failure_lines = run(
                "compare", "merge_group", True, fail_mode="reference")
            self.assertNotEqual(reference_failure.returncode, 0)
            self.assertEqual(len(reference_failure_lines), 1)
            candidate_failure, candidate_failure_lines = run(
                "skip", "push", False, fail_mode="candidate")
            self.assertNotEqual(candidate_failure.returncode, 0)
            self.assertEqual(len(candidate_failure_lines), 1)


if __name__ == "__main__":
    unittest.main()
