#!/usr/bin/env python3
"""Fixed three-pair Windows case-scheduling experiment for #2034.

Both arms run the same frozen assertions through the same case owner. Alternate
arm order, use fresh fixtures in every test, retain every run and never retry.
This measures suite latency; it does not run or predict the compiler matrix.
"""
import argparse
import contextlib
import json
import os
from pathlib import Path
import platform
import signal
import sys
import time

import bootstrap_wrapper_cases as scheduler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    signal.signal(signal.SIGTERM, signal.default_int_handler)
    report = {"source": os.environ.get("GITHUB_SHA"), "machine": platform.machine(),
              "python": sys.executable, "python_version": platform.python_version(),
              "image_os": os.environ.get("ImageOS"), "image_version": os.environ.get("ImageVersion"),
              "orders": [[1, 2], [2, 1], [1, 2]], "runs": [],
              "cpu_seconds": None, "process_tree_peak_rss": None,
              "whole_compiler_job_seconds": None}
    started = time.monotonic()
    success = True
    expected = [case.id() for case in scheduler.behavior_cases()]
    for pair, order in enumerate(report["orders"]):
        for jobs in order:
            name = "pair-%d-jobs-%d.log" % (pair + 1, jobs)
            print("BOOTSTRAP_COMPARE_START pair=%d jobs=%d" % (pair + 1, jobs), flush=True)
            with (args.output / name).open("w", encoding="utf-8") as output:
                with contextlib.redirect_stdout(output):
                    summary = scheduler.run_cases(scheduler.behavior_cases(), jobs=jobs)
            summary.update(pair=pair + 1, log=name)
            report["runs"].append(summary)
            success = (success and summary["success"] and
                       [row["test"] for row in summary["cases"]] == expected and
                       summary["owned_children"] == 22 and summary["peak_live_children"] <= 6 and
                       summary["unreaped_children"] == 0)
            report["success"] = success
            report["experiment_elapsed_seconds"] = time.monotonic() - started
            (args.output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
            print("BOOTSTRAP_COMPARE_END " + json.dumps({
                "pair": pair + 1, "jobs": jobs, "success": summary["success"],
                "elapsed_seconds": summary["elapsed_seconds"],
                "peak_live_children": summary["peak_live_children"],
            }), flush=True)
            if summary["cancelled"]:
                break
        if any(run["cancelled"] for run in report["runs"]):
            break
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
