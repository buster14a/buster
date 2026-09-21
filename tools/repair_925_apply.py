#!/usr/bin/env python3
"""Finish PR #927's bootstrap against the exact current-main seed."""

from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"expected one literal match in {path}: {old[:80]!r}")
    file.write_text(text.replace(old, new))


def regex_once(path: str, pattern: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    updated, count = re.subn(pattern, lambda _: new, text, count=1, flags=re.MULTILINE)
    if count != 1:
        raise SystemExit(f"expected one regex match in {path}: {pattern!r}")
    file.write_text(updated)


api = ".github/workflows/api-migration-policy.yml"
replace_once(api, " && vars.GH_ACTIONS_CI_ENABLED == 'true'", "")
guard = """      - name: Require CI admission to be enabled
        shell: bash
        env:
          CI_ENABLED: ${{ vars.GH_ACTIONS_CI_ENABLED }}
        run: |
          if [[ "$CI_ENABLED" != true ]]; then
            echo '::error::Required validation is disabled; refusing a skipped admission check.'
            exit 1
          fi
"""
replace_once(api, "    steps:\n      - name: Check out exact candidate\n",
             "    steps:\n" + guard + "      - name: Check out exact candidate\n")


def status_block(head: str) -> str:
    return """            rows="$RUNNER_TEMP/native-retirement-status-rows.json"
            gh api "repos/$GITHUB_REPOSITORY/statuses/$HEAD" > "$rows"
            python3 - "$HEAD" "$rows" "$RUNNER_TEMP/native-retirement-status.json" <<'PY_STATUS'
          import json, pathlib, sys
          head, rows_path, output_path = sys.argv[1:]
          rows = json.loads(pathlib.Path(rows_path).read_text())
          if not isinstance(rows, list):
              raise SystemExit("commit status list is not an array")
          pathlib.Path(output_path).write_text(json.dumps({"sha": head, "statuses": rows}))
          PY_STATUS""".replace("$HEAD", f"${head}")


regex_once(
    api,
    r'^ {12}gh api "repos/\$GITHUB_REPOSITORY/commits/\$HEAD_SHA/status" \\\n {14}> "\$RUNNER_TEMP/native-retirement-status\.json"$',
    status_block("HEAD_SHA"),
)
regex_once(
    ".github/workflows/native-retirement-rebind.yml",
    r'^ {12}gh api "repos/\$GITHUB_REPOSITORY/commits/\$CANDIDATE_HEAD/status" \\\n {14}> "\$RUNNER_TEMP/native-retirement-status\.json"$',
    status_block("CANDIDATE_HEAD"),
)

integration = ".github/workflows/native-retirement-integration.yml"
replace_once(
    integration,
    '          askpass="$RUNNER_TEMP/native-retirement-askpass.sh"\n'
    "          printf '%s\\n' \\\n",
    '          askpass="$RUNNER_TEMP/native-retirement-askpass.sh"\n'
    "          # Literal lines for the child script; expansion there is intentional.\n"
    "          # shellcheck disable=SC2016\n"
    "          printf '%s\\n' \\\n",
)

replace_once(
    "docs/agents/workflow.md",
    "exact candidate state in a disposable checkout; only the serialized trusted\n"
    "integration workflow may publish it.\n",
    "exact candidate state in a disposable checkout. The repository ruleset's\n"
    "required `API migration policy` status is the merge-admission authority. For\n"
    "retirement-sensitive changes it accepts only a current-main, two-parent\n"
    "integration head with a successful exact-head\n"
    "`Native retirement trusted integration` status from `github-actions[bot]`.\n"
    "When `main` advances that status is invalidated; rerun the protected writer\n"
    "instead of hand-editing generated state or requiring a manual rebase.\n",
)

marker = "    def test_sensitive_merge_group_fails_closed(self):\n"
test = """    def test_workflows_fetch_creator_bearing_status_rows(self):
        root = Path(__file__).resolve().parents[1]
        for relative, head in (
            (".github/workflows/api-migration-policy.yml", "HEAD_SHA"),
            (".github/workflows/native-retirement-rebind.yml", "CANDIDATE_HEAD"),
        ):
            with self.subTest(workflow=relative):
                content = (root / relative).read_text()
                self.assertIn(f"statuses/${head}", content)
                self.assertNotIn(f"commits/${head}/status", content)

"""
replace_once("tools/native_retirement_merge_gate_test.py", marker, test + marker)
