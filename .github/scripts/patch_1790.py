#!/usr/bin/env python3
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one exact replacement, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def regex_replace_once(path: str, pattern: str, replacement: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE | re.DOTALL)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex replacement, found {count}")
    target.write_text(updated, encoding="utf-8")


replace_once(
    ".github/actions/native-artifact-upload/action.yml",
    """name: Upload CI log artifacts with one bounded retry
description: Upload CI log evidence and retry one transient failure.
""",
    """name: Upload CI log artifacts with one bounded retry
description: Upload CI log evidence, expose composite entry, and retry one transient upload failure.
""",
)

replace_once(
    ".github/actions/native-artifact-upload/action.yml",
    """  retention-days:
    description: Artifact retention period.
    required: false
    default: "7"
runs:
  using: composite
  steps:
    - name: Retain ${{ inputs.label }} logs
""",
    """  retention-days:
    description: Artifact retention period.
    required: false
    default: "7"
outputs:
  entered:
    description: Whether the composite began after resolving its nested action dependencies.
    value: ${{ steps.entry.outputs.entered }}
runs:
  using: composite
  steps:
    - name: Record artifact action entry
      id: entry
      shell: bash
      run: printf 'entered=true\\n' >> "$GITHUB_OUTPUT"

    - name: Retain ${{ inputs.label }} logs
""",
)

desktop_old = """      - name: Retain desktop logs
        if: ${{ !cancelled() && steps.checkout.outcome == 'success' }}
        # The same-commit action uses actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a for both attempts.
        uses: ./.github/actions/native-artifact-upload
        with:
          label: desktop
          label_title: Desktop
          name: desktop-${{ matrix.os }}-${{ matrix.arch }}-${{ matrix.shard }}-${{ github.run_id }}-${{ github.run_attempt }}
          path: ${{ runner.temp }}/buster-ci/
          compression-level: 6
          if-no-files-found: ignore
          retention-days: 7
"""

desktop_new = """      # The local action exposes whether it entered after resolving its nested
      # upload dependency. A pre-entry resolution failure gets one new
      # top-level attempt; upload/API failures retain the action's own bounded
      # retry and never multiply into another outer upload attempt.
      - name: Attempt desktop log retention
        id: desktop_logs
        if: ${{ !cancelled() && steps.checkout.outcome == 'success' }}
        continue-on-error: true
        uses: ./.github/actions/native-artifact-upload
        with:
          label: desktop
          label_title: Desktop
          name: desktop-${{ matrix.os }}-${{ matrix.arch }}-${{ matrix.shard }}-${{ github.run_id }}-${{ github.run_attempt }}
          path: ${{ runner.temp }}/buster-ci/
          compression-level: 6
          if-no-files-found: ignore
          retention-days: 7

      - name: Back off before retrying desktop action resolution
        if: ${{ !cancelled() && steps.desktop_logs.outcome == 'failure' && steps.desktop_logs.outputs.entered != 'true' }}
        shell: bash
        run: |
          printf '::warning::Desktop artifact action resolution failed before execution; retrying once after 15 seconds.\\n'
          printf '## Desktop artifact action resolution\\n\\nInitial action resolution failed before composite entry; retrying once after a 15-second backoff.\\n' >> "$GITHUB_STEP_SUMMARY"
          sleep 15

      - name: Retry desktop log retention after action resolution failure
        id: desktop_logs_resolution_retry
        if: ${{ !cancelled() && steps.desktop_logs.outcome == 'failure' && steps.desktop_logs.outputs.entered != 'true' }}
        continue-on-error: true
        uses: ./.github/actions/native-artifact-upload
        with:
          label: desktop
          label_title: Desktop
          name: desktop-${{ matrix.os }}-${{ matrix.arch }}-${{ matrix.shard }}-${{ github.run_id }}-${{ github.run_attempt }}
          path: ${{ runner.temp }}/buster-ci/
          compression-level: 6
          if-no-files-found: ignore
          retention-days: 7

      # This exact step name is the aggregate's retained-evidence proof.
      - name: Retain desktop logs
        if: ${{ !cancelled() && steps.checkout.outcome == 'success' && (steps.desktop_logs.outcome == 'success' || steps.desktop_logs_resolution_retry.outcome == 'success') }}
        shell: bash
        env:
          PRIMARY_OUTCOME: ${{ steps.desktop_logs.outcome }}
          RESOLUTION_RETRY_OUTCOME: ${{ steps.desktop_logs_resolution_retry.outcome }}
        run: |
          printf 'DESKTOP_ARTIFACT_RESULT status=success primary_outcome=%s resolution_retry_outcome=%s\\n' \\
            "$PRIMARY_OUTCOME" "$RESOLUTION_RETRY_OUTCOME"

      # Keep this diagnostic step successful so a post-test infrastructure
      # failure cannot trigger matrix/watcher cancellation of unrelated shards.
      # CI complete still fails closed because the proof step above is absent.
      - name: Record failed desktop log retention
        if: ${{ always() && !cancelled() && steps.checkout.outcome == 'success' && steps.desktop_logs.outcome == 'failure' && steps.desktop_logs_resolution_retry.outcome != 'success' }}
        shell: bash
        env:
          PRIMARY_ENTERED: ${{ steps.desktop_logs.outputs.entered }}
          RESOLUTION_RETRY_ENTERED: ${{ steps.desktop_logs_resolution_retry.outputs.entered }}
        run: |
          set -euo pipefail
          if [[ "$PRIMARY_ENTERED" != true && "$RESOLUTION_RETRY_ENTERED" != true ]]; then
            layer='action-resolution'
            detail='both bounded action-resolution attempts failed before composite entry'
          else
            layer='artifact-upload'
            detail='the action loaded but its bounded artifact upload attempts did not retain evidence'
          fi
          printf '::error::Desktop artifact retention failed: layer=%s; %s.\\n' "$layer" "$detail"
          printf '## Desktop artifact retention\\n\\nResult: **failure**\\n\\nLayer: `%s`\\n\\n%s.\\n' \\
            "$layer" "$detail" >> "$GITHUB_STEP_SUMMARY"
"""

replace_once(".github/workflows/ci.yml", desktop_old, desktop_new)

desktop_test = r"""    def test_desktop_log_upload_reuses_bounded_action_for_all_shards(self):
        repository_root = Path(__file__).resolve().parents[1]
        workflow = (repository_root / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
        desktop = workflow.split("\n  test:", 1)[1].split("\n  native:", 1)[0]
        lanes = re.search(r"(?m)^        lane: \[([^\]]+)\]$", desktop)
        shards = re.search(r"(?m)^        shard: \[([^\]]+)\]$", desktop)
        self.assertIsNotNone(lanes)
        self.assertIsNotNone(shards)
        self.assertEqual(lanes.group(1).split(", "), [
            "linux-x86_64", "linux-aarch64", "macos-x86_64", "macos-aarch64",
            "windows-x86_64", "windows-aarch64",
        ])
        self.assertEqual(shards.group(1).split(", "), ["release", "checks"])
        steps = dict(re.findall(
            r"(?ms)^      - name: ([^\n]+)\n(.*?)(?=^      - name:|\Z)", desktop,
        ))
        primary = steps["Attempt desktop log retention"]
        backoff = steps["Back off before retrying desktop action resolution"]
        retry = steps["Retry desktop log retention after action resolution failure"]
        proof = steps["Retain desktop logs"]
        failed = steps["Record failed desktop log retention"]

        self.assertIn("id: checkout", steps["Checkout"])
        self.assertLess(desktop.index("      - name: Checkout"),
                        desktop.index("      - name: Attempt desktop log retention"))
        self.assertLess(desktop.index("      - name: Attempt desktop log retention"),
                        desktop.index("      - name: Retain desktop logs"))

        primary_condition = re.search(r"(?m)^        if: \$\{\{ (.+) \}\}$", primary)
        self.assertIsNotNone(primary_condition)
        terms = primary_condition.group(1).split(" && ")
        for checkout, cancelled, earlier_failure, expected in (
            ("success", False, None, True),
            ("success", False, "build", True),
            ("success", False, "test", True),
            ("success", False, "summary", True),
            ("failure", False, "checkout", False),
            ("skipped", False, "checkout", False),
            ("success", True, "test", False),
        ):
            with self.subTest(checkout=checkout, cancelled=cancelled,
                              earlier_failure=earlier_failure):
                values = {
                    "!cancelled()": not cancelled,
                    "steps.checkout.outcome == 'success'": checkout == "success",
                }
                self.assertEqual(set(terms), set(values))
                self.assertEqual(all(values[term] for term in terms), expected)

        def upload_inputs(step):
            return dict(re.findall(
                r"(?m)^          ([a-z_-]+): (.+)$",
                step.split("        with:\n", 1)[1],
            ))

        for upload in (primary, retry):
            self.assertIn("continue-on-error: true", upload)
            self.assertIn("uses: ./.github/actions/native-artifact-upload", upload)
            self.assertNotIn("uses: actions/upload-artifact@", upload)
        self.assertIn("id: desktop_logs", primary)
        self.assertIn("id: desktop_logs_resolution_retry", retry)
        primary_inputs = upload_inputs(primary)
        retry_inputs = upload_inputs(retry)
        self.assertEqual(primary_inputs, retry_inputs)
        self.assertEqual(primary_inputs, {
            "label": "desktop",
            "label_title": "Desktop",
            "name": "desktop-${{ matrix.os }}-${{ matrix.arch }}-${{ matrix.shard }}-${{ github.run_id }}-${{ github.run_attempt }}",
            "path": "${{ runner.temp }}/buster-ci/",
            "compression-level": "6",
            "if-no-files-found": "ignore",
            "retention-days": "7",
        })

        resolution_failure = "steps.desktop_logs.outcome == 'failure'"
        pre_entry = "steps.desktop_logs.outputs.entered != 'true'"
        for guarded in (backoff, retry):
            self.assertIn("!cancelled()", guarded)
            self.assertIn(resolution_failure, guarded)
            self.assertIn(pre_entry, guarded)
        self.assertEqual(backoff.count("sleep 15"), 1)
        self.assertNotIn("sleep 15", retry)

        self.assertIn("steps.desktop_logs.outcome == 'success'", proof)
        self.assertIn("steps.desktop_logs_resolution_retry.outcome == 'success'", proof)
        self.assertIn("DESKTOP_ARTIFACT_RESULT status=success", proof)
        self.assertNotIn("continue-on-error", proof)

        self.assertIn("always() && !cancelled()", failed)
        self.assertIn(resolution_failure, failed)
        self.assertIn("steps.desktop_logs_resolution_retry.outcome != 'success'", failed)
        self.assertIn("PRIMARY_ENTERED", failed)
        self.assertIn("RESOLUTION_RETRY_ENTERED", failed)
        self.assertIn("layer='action-resolution'", failed)
        self.assertIn("layer='artifact-upload'", failed)
        self.assertIn("::error::Desktop artifact retention failed", failed)
        self.assertNotRegex(failed, r"(?m)^\s*exit\s+[1-9]")
"""

regex_replace_once(
    "tools/ci_native_observation_test.py",
    r"    def test_desktop_log_upload_reuses_bounded_action_for_all_shards\(self\):\n.*?(?=    def test_mobile_log_upload_reuses_bounded_action_for_all_matrix_entries)",
    desktop_test + "\n",
)

action_test = r"""    def test_log_upload_action_retries_once_and_fails_closed(self):
        repository_root = Path(__file__).resolve().parents[1]
        action_path = repository_root / ".github" / "actions" / "native-artifact-upload" / "action.yml"
        action = action_path.read_text(encoding="utf-8")
        steps = re.findall(r"(?ms)^    - name: ([^\n]+)\n(.*?)(?=^    - name:|\Z)", action)
        expected = [
            "Record artifact action entry",
            "Retain ${{ inputs.label }} logs",
            "Back off before retrying ${{ inputs.label }} log upload",
            "Retain ${{ inputs.label }} logs (retry)",
            "Record recovered ${{ inputs.label }} log upload",
            "Record failed ${{ inputs.label }} log upload",
        ]
        self.assertEqual([name for name, _ in steps], expected)
        step_map = dict(steps)
        entry = step_map[expected[0]]
        primary = step_map[expected[1]]
        backoff = step_map[expected[2]]
        retry = step_map[expected[3]]
        recovered = step_map[expected[4]]
        failed = step_map[expected[5]]
        label_input = action.split("  label:\n", 1)[1].split("\n  name:", 1)[0]
        self.assertRegex(label_input, r"(?m)^    default: native$")
        title_input = action.split("  label_title:\n", 1)[1].split("\n  name:", 1)[0]
        self.assertRegex(title_input, r"(?m)^    default: Native$")
        pin = "actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a"
        self.assertEqual(action.count("uses: " + pin), 2)
        self.assertEqual(action.count("continue-on-error: true"), 1)

        self.assertIn("outputs:\n  entered:", action)
        self.assertIn("value: ${{ steps.entry.outputs.entered }}", action)
        self.assertIn("id: entry", entry)
        self.assertIn("entered=true", entry)
        self.assertLess(action.index("id: entry"), action.index("uses: " + pin))

        self.assertIn("if: ${{ !cancelled() }}", primary)
        self.assertIn("id: artifact_upload", primary)
        self.assertIn("continue-on-error: true", primary)
        self.assertNotIn("overwrite: true", primary)
        failure_condition = "steps.artifact_upload.outcome == 'failure'"
        self.assertIn(failure_condition, backoff)
        self.assertIn("sleep 15", backoff)
        self.assertEqual(backoff.count("sleep 15"), 1)
        self.assertIn("id: artifact_upload_retry", retry)
        self.assertIn(failure_condition, retry)
        for guarded_step in (backoff, retry, recovered):
            self.assertIn("!cancelled()", guarded_step)
        self.assertNotIn("continue-on-error", retry)
        self.assertIn("overwrite: true", retry)
        self.assertIn("steps.artifact_upload_retry.outcome == 'success'", recovered)
        self.assertIn("Retry succeeded;", recovered)
        self.assertIn("GITHUB_STEP_SUMMARY", recovered)
        self.assertIn(failure_condition, failed)
        self.assertIn("steps.artifact_upload_retry.outcome == 'failure'", failed)
        self.assertIn("always() && !cancelled()", failed)
        self.assertIn("evidence was not retained", failed)
        self.assertEqual(action_pins.check_text(action, action_path), [])

        primary_inputs = dict(re.findall(r"(?m)^        ([a-z-]+): (.+)$",
                                        primary.split("      with:\n", 1)[1]))
        retry_inputs = dict(re.findall(r"(?m)^        ([a-z-]+): (.+)$",
                                      retry.split("      with:\n", 1)[1]))
        self.assertEqual(
            primary_inputs,
            {key: value for key, value in retry_inputs.items() if key != "overwrite"},
        )
        self.assertEqual(retry_inputs["overwrite"], "true")
"""

regex_replace_once(
    "tools/ci_native_observation_test.py",
    r"    def test_log_upload_action_retries_once_and_fails_closed\(self\):\n.*?(?=\nif __name__ == \"__main__\":)",
    action_test.rstrip(),
)

matrix_test = r"""
    def test_artifact_resolution_containment_still_requires_success_proof(self):
        jobs = self.sample()
        target = next(job for job in jobs if job["name"] == "Windows AArch64 checks")
        proof = next(step for step in target["steps"] if step["name"] == "Retain desktop logs")
        proof.update(status="completed", conclusion="skipped")
        target["steps"].extend([
            {
                "name": "Attempt desktop log retention",
                "status": "completed",
                "conclusion": "success",
            },
            {
                "name": "Record failed desktop log retention",
                "status": "completed",
                "conclusion": "success",
            },
        ])
        errors = self.check(jobs)
        self.assertTrue(errors)
        self.assertTrue(any(
            target["name"] in error
            and "Retain desktop logs" in error
            and "lacks unique completion proof" in error
            for error in errors
        ), errors)

"""
replace_once(
    "tools/matrix_shard_test.py",
    "    def test_cross_run_source_or_future_attempt_is_not_accepted(self):\n",
    matrix_test + "    def test_cross_run_source_or_future_attempt_is_not_accepted(self):\n",
)

replace_once(
    "docs/agents/testing.md",
    """  On `merge_group`, desktop and mobile enable matrix fail-fast; the native
  matrix retains `fail-fast: false` under the frozen CI test contract. The trusted
  controller cancels exact-head merge-group runs after a failed Buster CI job
  or required check from another workflow.
""",
    """  On `merge_group`, desktop and mobile enable matrix fail-fast; the native
  matrix retains `fail-fast: false` under the frozen CI test contract. The trusted
  controller cancels exact-head merge-group runs after a failed Buster CI job
  or required check from another workflow. A desktop artifact transfer that
  fails after successful compiler/test work is contained instead of becoming
  an immediate shard failure: the local upload action publishes an `entered`
  output, so only a pre-entry dependency-resolution failure receives one
  second top-level resolution attempt. Upload/API failures use only the
  action's existing bounded retry. A terminal retention failure records its
  layer but omits the aggregate-required `Retain desktop logs` proof; the
  shard can finish without cancelling siblings, while `CI complete` still
  rejects the exact run as incomplete.
""",
)

replace_once(
    "docs/ci-cancellation-recovery.md",
    """The Buster native matrix retains `fail-fast: false` under the frozen CI test
contract, but its first failed job still triggers the trusted watcher. The separate required
workflows have no shared `needs` dependency, so the watcher closes that gap.
""",
    """The Buster native matrix retains `fail-fast: false` under the frozen CI test
contract, but its first failed job still triggers the trusted watcher. The separate required
workflows have no shared `needs` dependency, so the watcher closes that gap.

A desktop artifact-retention failure after successful compiler/test work is
contained at the caller boundary rather than exposed as an immediate failed
matrix job. The same-commit composite reports whether it entered after nested
action resolution. An empty entry result permits exactly one second top-level
resolution attempt; once entered, only the composite's existing bounded
upload/API retry applies. If neither path retains evidence, the shard records
an error annotation but does not trigger sibling cancellation. Its
aggregate-required `Retain desktop logs` proof step remains absent, so
`CI complete` fails closed after the other shards have had a chance to finish.
Compiler, test, summary, checkout and other substantive shard failures remain
ordinary failures and retain native fail-fast/watcher behavior.
""",
)

# The temporary patch carrier must never appear in the submitted branch.
for temporary in (
    ROOT / ".github" / "scripts" / "patch_1790.py",
    ROOT / ".github" / "workflows" / "patch-1790.yml",
):
    temporary.unlink(missing_ok=True)
