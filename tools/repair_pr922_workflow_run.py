#!/usr/bin/env python3
"""One-shot branch repair for PR #922; self-deletes after validation."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import textwrap


ROOT = Path(__file__).resolve().parents[1]
BRANCH = "codex/869-merge-conflict-preflight"
BASE = "52d018e2c39097eabccb696b7569b1a4407e8d59"


def run(*arguments: str, capture: bool = False) -> str:
    result = subprocess.run(
        arguments,
        cwd=ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
    )
    return result.stdout.strip() if capture else ""


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected one {label}, found {count}")
    return text.replace(old, new)


def write(path: str, content: str) -> None:
    destination = ROOT / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(textwrap.dedent(content).lstrip(), encoding="utf-8")


def main() -> None:
    expected_head = os.environ["GITHUB_SHA"]
    if run("git", "rev-parse", "HEAD", capture=True) != expected_head:
        raise RuntimeError("checkout does not equal GITHUB_SHA")

    run(
        "git", "checkout", BASE, "--",
        "tests/ci_tools_test.py",
        "docs/native-retirement-support-v1.tsv",
    )

    write(
        ".github/workflows/merge-conflict-preflight.yml",
        r'''
        name: Merge conflict preflight

        on:
          workflow_run:
            workflows: [Merge conflict preflight regression]
            types: [completed]
          push:
            branches: [main]
          merge_group:
            types: [checks_requested]
          workflow_dispatch:

        permissions:
          contents: read

        concurrency:
          group: merge-conflict-preflight-${{ github.event_name }}-${{ github.event.workflow_run.id || github.ref }}
          cancel-in-progress: true

        jobs:
          preflight:
            name: Exact merge-tree preflight
            if: ${{ github.server_url == 'https://github.com' && vars.GH_ACTIONS_CI_ENABLED == 'true' }}
            permissions:
              contents: read
              pull-requests: read
              statuses: write
            runs-on: ubuntu-latest
            timeout-minutes: 5
            steps:
              # workflow_run and push execute the workflow definition from trusted
              # current main. Candidate bytes are fetched only into private refs and
              # consumed by read-only git plumbing; candidate code is never run.
              - name: Check out trusted current main
                uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683
                with:
                  ref: ${{ github.event.repository.default_branch }}
                  fetch-depth: 0
                  persist-credentials: false
              - name: Analyze exact main/head identities
                env:
                  GITHUB_TOKEN: ${{ github.token }}
                run: >-
                  python3 -B tools/merge_conflict_preflight.py github-event
                  --repo .
                  --event-path "$GITHUB_EVENT_PATH"
                  --repository "$GITHUB_REPOSITORY"
                  --report-dir evidence/merge-conflict-preflight
                  --summary "$GITHUB_STEP_SUMMARY"
              - name: Retain exact machine-readable reports
                if: ${{ always() }}
                uses: actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02
                with:
                  name: merge-conflict-preflight-${{ github.run_id }}-${{ github.run_attempt }}
                  path: evidence/merge-conflict-preflight/*.json
                  if-no-files-found: ignore
                  retention-days: 14
        ''',
    )
    write(
        ".github/workflows/merge-conflict-preflight-regression.yml",
        r'''
        name: Merge conflict preflight regression

        on:
          pull_request:
            branches: [main]
            types: [opened, reopened, synchronize, ready_for_review, edited]

        permissions:
          contents: read

        concurrency:
          group: merge-conflict-preflight-regression-${{ github.event.pull_request.number || github.ref }}
          cancel-in-progress: true

        jobs:
          regression:
            name: Preflight regression tests
            if: ${{ github.server_url == 'https://github.com' && vars.GH_ACTIONS_CI_ENABLED == 'true' }}
            permissions:
              contents: read
            runs-on: ubuntu-latest
            timeout-minutes: 2
            steps:
              - name: Check out candidate for unprivileged tests
                uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683
                with:
                  persist-credentials: false
              - name: Exercise merge-tree fixtures and workflow policy
                run: python3 -B tools/merge_conflict_preflight_test.py -v
        ''',
    )

    source_path = ROOT / "tools/merge_conflict_preflight.py"
    source = source_path.read_text(encoding="utf-8")
    source = replace_once(
        source,
        'raise PreflightError("pull_request_target event omits pull_request.number")',
        'raise PreflightError("pull request event omits pull_request.number")',
        "pull-event diagnostic",
    )
    marker = "\ndef _github_push_event("
    helper = r'''

def _workflow_run_pull_number(event: dict) -> int:
    run = event.get("workflow_run")
    if not isinstance(run, dict) or run.get("event") != "pull_request":
        raise PreflightError("workflow_run event is not for a pull_request workflow")
    pulls = run.get("pull_requests")
    if (not isinstance(pulls, list) or len(pulls) != 1 or
            not isinstance(pulls[0], dict)):
        raise PreflightError("workflow_run event must identify exactly one pull request")
    number = pulls[0].get("number")
    if not isinstance(number, int) or number <= 0:
        raise PreflightError("workflow_run pull request has an invalid number")
    return number


def _github_workflow_run_event(repo: Path, api: GitHubApi, event: dict,
                               report_dir: Path, summary: Path | None,
                               context: str) -> bool:
    number = _workflow_run_pull_number(event)
    report, head = _analyze_stable_pull(repo, api, number, context)
    output = report_dir / f"pr-{number}-{head}.json"
    _write_report(report, output, summary, f"PR #{number} merge-conflict preflight")
    api.publish_status(head, report, context, _target_url())
    return bool(report["outcome"]["blocking"])
'''
    source = replace_once(source, marker, helper + marker, "push-event marker")
    source = replace_once(
        source,
        '''    if event_name == "pull_request_target":
        blocking = _github_pull_event(repo, api, event, report_dir, summary, context)
''',
        '''    if event_name == "workflow_run":
        blocking = _github_workflow_run_event(repo, api, event, report_dir, summary, context)
''',
        "event dispatch",
    )
    source_path.write_text(source, encoding="utf-8")

    test_path = ROOT / "tools/merge_conflict_preflight_test.py"
    tests = test_path.read_text(encoding="utf-8")
    workflow_constant = (
        'WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / '
        '"merge-conflict-preflight.yml"\n'
    )
    tests = replace_once(
        tests,
        workflow_constant,
        workflow_constant + (
            'REGRESSION_WORKFLOW_PATH = (REPO_ROOT / ".github" / "workflows" /\n'
            '                            "merge-conflict-preflight-regression.yml")\n'
        ),
        "workflow path constant",
    )
    start = tests.index(
        "    def test_workflow_separates_untrusted_tests_from_trusted_status_publication(self) -> None:\n"
    )
    end = tests.index(
        "    def test_workflow_guidance_prescribes_each_classification_response(self) -> None:\n",
        start,
    )
    replacement = r'''    def test_workflow_separates_untrusted_tests_from_trusted_status_publication(self) -> None:
        trusted = WORKFLOW_PATH.read_text(encoding="utf-8")
        regression = REGRESSION_WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("workflow_run:\n", trusted)
        self.assertIn("workflows: [Merge conflict preflight regression]", trusted)
        self.assertIn("push:\n    branches: [main]", trusted)
        self.assertIn("merge_group:\n    types: [checks_requested]", trusted)
        self.assertIn("pull-requests: read\n      statuses: write", trusted)
        self.assertIn("ref: ${{ github.event.repository.default_branch }}", trusted)
        self.assertIn("persist-credentials: false", trusted)
        self.assertNotIn("pull_request_target", trusted)
        self.assertNotIn("python3 -B tools/merge_conflict_preflight_test.py -v", trusted)

        self.assertIn("pull_request:\n", regression)
        self.assertIn("python3 -B tools/merge_conflict_preflight_test.py -v", regression)
        self.assertIn("persist-credentials: false", regression)
        self.assertNotIn("statuses: write", regression)
        self.assertNotIn("GITHUB_TOKEN", regression)
        self.assertNotIn("tools/merge_conflict_preflight.py github-event", regression)

    def test_workflow_run_payload_requires_one_pull_request(self) -> None:
        event = {
            "workflow_run": {
                "event": "pull_request",
                "pull_requests": [{"number": 922}],
            },
        }
        self.assertEqual(PREFLIGHT._workflow_run_pull_number(event), 922)
        invalid = (
            {},
            {"workflow_run": {"event": "push", "pull_requests": [{"number": 922}]}},
            {"workflow_run": {"event": "pull_request", "pull_requests": []}},
            {"workflow_run": {"event": "pull_request", "pull_requests": [{"number": 0}]}},
            {"workflow_run": {"event": "pull_request", "pull_requests": [
                {"number": 1}, {"number": 2},
            ]}},
        )
        for payload in invalid:
            with self.subTest(payload=payload), self.assertRaises(PREFLIGHT.PreflightError):
                PREFLIGHT._workflow_run_pull_number(payload)

'''
    tests = tests[:start] + replacement + tests[end:]
    test_path.write_text(tests, encoding="utf-8")

    run("python3", "-B", "tools/merge_conflict_preflight_test.py", "-v")
    run("python3", "-B", "tests/ci_tools_test.py", "-v")
    run("git", "diff", "--check")

    run("git", "config", "user.name", "github-actions[bot]")
    run("git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com")
    run(
        "git", "add", "--",
        ".github/workflows/merge-conflict-preflight.yml",
        ".github/workflows/merge-conflict-preflight-regression.yml",
        "docs/native-retirement-support-v1.tsv",
        "tests/ci_tools_test.py",
        "tools/merge_conflict_preflight.py",
        "tools/merge_conflict_preflight_test.py",
    )
    run(
        "git", "rm", "--",
        ".github/workflows/repair-922-workflow-run.yml",
        "tools/repair_pr922_workflow_run.py",
    )
    expected = {
        ".github/workflows/merge-conflict-preflight-regression.yml",
        ".github/workflows/merge-conflict-preflight.yml",
        ".github/workflows/repair-922-workflow-run.yml",
        "docs/native-retirement-support-v1.tsv",
        "tests/ci_tools_test.py",
        "tools/merge_conflict_preflight.py",
        "tools/merge_conflict_preflight_test.py",
        "tools/repair_pr922_workflow_run.py",
    }
    actual = set(run("git", "diff", "--cached", "--name-only", capture=True).splitlines())
    if actual != expected:
        raise RuntimeError(f"unexpected staged paths: {sorted(actual ^ expected)}")
    run("git", "commit", "-m", "ci: hand off privileged preflight through workflow_run")
    run(
        "git", "push",
        f"--force-with-lease=refs/heads/{BRANCH}:{expected_head}",
        "origin", f"HEAD:refs/heads/{BRANCH}",
    )


if __name__ == "__main__":
    main()
