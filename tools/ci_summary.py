#!/usr/bin/env python3
"""Write bounded, fail-closed CI diagnostics without copying the environment.

Inputs are explicit step outcomes, required step IDs, and runner metadata.
Build/test orchestration remains in build.c and the existing mobile launchers.
"""
import html
import json
import os
from pathlib import Path
import sys


def assess(steps, required):
    missing = [name for name in required if steps.get(name, {}).get("outcome") != "success"]
    failed = [name for name, step in steps.items() if step.get("outcome") in ("failure", "cancelled")]
    return sorted(set(missing + failed))


def write_report(environment):
    steps = json.loads(environment.get("BUSTER_CI_STEPS", "{}"))
    required = environment.get("BUSTER_CI_REQUIRED", "").split()
    if not isinstance(steps, dict) or not required:
        raise ValueError("A step map and an explicit nonempty required-step list are mandatory")
    failures = assess(steps, required)
    metadata = {key: environment.get(key, "unknown") for key in (
        "GITHUB_REPOSITORY", "GITHUB_SHA", "GITHUB_REF", "GITHUB_RUN_ID",
        "GITHUB_RUN_ATTEMPT", "RUNNER_OS", "RUNNER_ARCH", "ImageOS", "ImageVersion", "BUSTER_CI_RUNNER")}
    report = {"schema": 1, "metadata": metadata, "required_steps": required,
              "steps": steps, "unsatisfied_steps": failures, "success": not failures}
    output = Path(environment["RUNNER_TEMP"]) / "buster-ci"
    output.mkdir(parents=True, exist_ok=True)
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = ["## Buster CI", "", "**Result: " + ("FAILURE" if failures else "SUCCESS") + "**", ""]
    for key, value in metadata.items():
        lines.append(f"{key}: <code>{html.escape(str(value))}</code><br>")
    lines += ["", "| Step | Outcome |", "|---|---|"]
    for name, step in steps.items():
        lines.append(f"| {html.escape(name).replace('|', '&#124;')} | "
                     f"{html.escape(step.get('outcome', 'missing'))} |")
    if failures:
        lines += ["", "Missing, skipped, cancelled, or failed required work is not a pass."]
    reproduction = environment.get("BUSTER_CI_REPRO", "See docs/ci-github-actions.md.")
    # HTML escaping keeps branch names and command text out of Markdown fences.
    lines += ["", "### Reproduce", "", "Check out the exact GITHUB_SHA above. "
              "Use the same runner image and tool versions recorded in the job log.",
              "", "<pre>" + html.escape(reproduction) + "</pre>", "",
              "The diagnostic artifact contains result.json and captured logs. "
              "It contains no cached build products or environment/credential dump.", ""]
    text = "\n".join(lines)
    (output / "summary.md").write_text(text, encoding="utf-8")
    if environment.get("GITHUB_STEP_SUMMARY"):
        with Path(environment["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as stream:
            stream.write(text)
    print("CI_SUMMARY " + ("failure: " + ", ".join(failures) if failures else "success"))
    return 1 if failures else 0


def main():
    status = 1
    try:
        status = write_report(os.environ)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"CI summary failed: {error}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
