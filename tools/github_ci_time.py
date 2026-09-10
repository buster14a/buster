#!/usr/bin/env python3
"""Collect and summarize GitHub CI timing without mixing partial runs or matrices.

Ownership: GitHub Actions observations only; tools/ci_time.py owns Forgejo's
very different timestamps/retention repair. No inferred or imputed durations.
Entry points: collect reads the GitHub REST API; summarize is entirely offline.
Each workflow-blob/runner-label cohort has its own median and exclusion counts.
"""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import statistics
import sys
import urllib.parse
import urllib.request

PLATFORMS = ("Linux x86-64", "Linux AArch64", "macOS x86-64", "macOS AArch64",
             "Windows x86-64", "Windows AArch64")
MOBILE = ("Android x86-64", "iOS x86-64", "iOS AArch64")
SHARDED_JOBS = PLATFORMS + MOBILE + ("Workflow lint", "CI complete")
NATIVE = tuple(name + " native" for name in PLATFORMS if not name.startswith("Windows"))
NATIVE_SHARDED_JOBS = PLATFORMS + NATIVE + MOBILE + ("Workflow lint", "CI complete")
# Select the expected layout independently of the jobs returned by GitHub.
# Otherwise four missing native shards could masquerade as an older complete CI.
LAYOUTS = {"legacy": PLATFORMS, "desktop": SHARDED_JOBS, "native": NATIVE_SHARDED_JOBS}
RUN_FIELDS = ("id", "head_sha", "head_branch", "event", "path", "status", "conclusion",
              "run_attempt", "created_at", "run_started_at", "html_url")
JOB_FIELDS = ("id", "name", "run_attempt", "status", "conclusion", "started_at", "completed_at", "labels")
STEP_FIELDS = ("name", "status", "conclusion", "started_at", "completed_at")


def timestamp(value):
    result = datetime.fromisoformat(value.replace("Z", "+00:00")) if value else None
    if result is not None and result.tzinfo is None:
        raise ValueError("GitHub timestamps must have a timezone")
    return result


def measure(run, layout="native"):
    """A successful first attempt of the requested layout, or an exclusion."""
    if layout not in LAYOUTS:
        raise ValueError(f"Unknown CI layout: {layout}")
    reason = None
    result = None
    jobs = run.get("jobs", [])
    names = sorted(job.get("name", "") for job in jobs)
    sharded = layout != "legacy"
    if run.get("status") != "completed":
        reason = "not-completed"
    elif run.get("conclusion") != "success":
        reason = run.get("conclusion") or "no-conclusion"
    elif run.get("run_attempt") != 1:
        reason = "rerun"
    elif names != sorted(LAYOUTS[layout]):
        reason = "incomplete-or-different-matrix"
    elif not run.get("workflow_blob_sha"):
        reason = "unknown-workflow-revision"
    else:
        starts = []
        finishes = []
        busy = 0.0
        step_seconds = {}
        for job in jobs:
            name = job["name"]
            required = set()
            if name in PLATFORMS:
                required.add("Combination matrix (Windows)" if name.startswith("Windows")
                             else "Combination matrix (Linux, macOS)")
                if not name.startswith("Windows") and layout != "native":
                    required.add("Execution-mode matrix")
                    if layout == "desktop":
                        required.add("Native configuration differential matrix")
                if not sharded:
                    if name.startswith("macOS"):
                        required.add("Test (iOS simulator)")
                    if name == "Linux x86-64":
                        required.add("Test (Android)")
            elif name in NATIVE:
                required.update(("Execution-mode matrix", "Native configuration differential matrix"))
            elif name.startswith("iOS"):
                required.add("Test (iOS simulator)")
            elif name.startswith("Android"):
                required.add("Test (Android)")
            elif name == "Workflow lint":
                required.add("Validate every GitHub workflow")
            elif name == "CI complete":
                required.add("Require every shard")
            passed = {step["name"] for step in job.get("steps", []) if step.get("conclusion") == "success"}
            if job.get("conclusion") != "success" or job.get("run_attempt") != 1 or not required <= passed:
                reason = "incomplete-coverage"
            start, finish = timestamp(job.get("started_at")), timestamp(job.get("completed_at"))
            if start is None or finish is None or finish < start:
                reason = "invalid-job-timestamps"
            else:
                starts.append(start)
                finishes.append(finish)
                busy += (finish - start).total_seconds()
            durations = {}
            for step in job.get("steps", []):
                left, right = timestamp(step.get("started_at")), timestamp(step.get("completed_at"))
                if left is not None and right is not None and right >= left:
                    durations[step["name"]] = (right - left).total_seconds()
            step_seconds[name] = durations
        created = timestamp(run.get("created_at"))
        if reason is None:
            if created is None or created > min(starts):
                reason = "invalid-run-timestamps"
            else:
                result = {"run_id": run["id"], "head_sha": run["head_sha"],
                          "elapsed_seconds": (max(finishes) - created).total_seconds(),
                          "execution_span_seconds": (max(finishes) - min(starts)).total_seconds(),
                          "initial_queue_seconds": (min(starts) - created).total_seconds(),
                          "runner_seconds": busy, "step_seconds": step_seconds}
    return result, reason


def summarize(data, layout="native"):
    cohorts = defaultdict(list)
    excluded = Counter()
    seen = set()
    for run in data["runs"]:
        identity = (run["id"], run.get("run_attempt"))
        if identity in seen:
            raise ValueError("Duplicate run/attempt observations would bias the median")
        seen.add(identity)
        sample, reason = measure(run, layout=layout)
        if reason:
            excluded[reason] += 1
        else:
            runners = tuple(sorted((job["name"], tuple(sorted(job.get("labels", [])))) for job in run["jobs"]))
            cohorts[(run["workflow_blob_sha"], runners)].append(sample)
    rows = []
    for (revision, runners), samples in sorted(cohorts.items()):
        rows.append({"workflow_blob_sha": revision, "runners": runners, "n": len(samples),
                     "medians": {key: statistics.median(sample[key] for sample in samples)
                                 for key in ("elapsed_seconds", "execution_span_seconds", "initial_queue_seconds", "runner_seconds")},
                     "samples": samples})
    return {"schema": 1, "layout": layout, "cohorts": rows, "excluded": dict(excluded),
            "notes": ["Elapsed = workflow creation to last required job completion; queueing is included.",
                      "Execution span still includes any staggered runner starts; runner_seconds sums active job intervals.",
                      "Cancelled, failed, partial, rerun and differently configured runs are never pooled into a speedup.",
                      "Cache state and compiler source changes require separate review; these are descriptive medians, not causal claims."]}


def api_get(repository, path, token):
    headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28",
               "User-Agent": "buster-ci-timing"}
    if token:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(f"https://api.github.com/repos/{repository}/{path}", headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        result = json.load(response)
    return result


def collect(args):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository):
        raise ValueError("Repository must have owner/name form")
    token = os.getenv("GH_TOKEN") or os.getenv("GITHUB_TOKEN")
    selected = []
    exhausted = False
    for page in range(1, args.max_pages + 1):
        if len(selected) < args.limit and not exhausted:
            query = {"per_page": 100, "page": page}
            if args.branch:
                query["branch"] = args.branch
            if args.head_sha:
                query["head_sha"] = args.head_sha
            batch = api_get(args.repository, "actions/runs?" + urllib.parse.urlencode(query), token)["workflow_runs"]
            exhausted = len(batch) < 100
            for run in batch:
                if run["path"].split("@", 1)[0] == args.workflow and len(selected) < args.limit:
                    selected.append({key: run.get(key) for key in RUN_FIELDS})
    for run in selected:
        run["jobs"] = []
        if run["status"] == "completed":
            page = 1
            total = None
            while total is None or len(run["jobs"]) < total:
                batch = api_get(args.repository, f"actions/runs/{run['id']}/jobs?filter=latest&per_page=100&page={page}", token)
                total = batch["total_count"]
                if not batch["jobs"] and len(run["jobs"]) < total:
                    raise ValueError("Incomplete job pagination; refusing a partial measurement")
                for job in batch["jobs"]:
                    record = {key: job.get(key) for key in JOB_FIELDS}
                    record["steps"] = [{key: step.get(key) for key in STEP_FIELDS} for step in job.get("steps", [])]
                    run["jobs"].append(record)
                page += 1
            query = urllib.parse.urlencode({"ref": run["head_sha"]})
            run["workflow_blob_sha"] = api_get(args.repository, f"contents/{args.workflow}?{query}", token)["sha"]
    return {"schema": 1, "repository": args.repository, "fetched_at": datetime.now(timezone.utc).isoformat(),
            "selection": {"branch": args.branch, "head_sha": args.head_sha, "workflow": args.workflow,
                          "limit": args.limit, "page_bound_reached": not exhausted and len(selected) < args.limit},
            "runs": selected}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    gather = sub.add_parser("collect")
    gather.add_argument("--repository", default="buster14a/buster")
    gather.add_argument("--branch")
    gather.add_argument("--head-sha")
    gather.add_argument("--workflow", default=".github/workflows/ci.yml")
    gather.add_argument("--limit", type=int, default=20)
    gather.add_argument("--max-pages", type=int, default=5)
    gather.add_argument("--output", required=True)
    report = sub.add_parser("summarize")
    report.add_argument("input")
    report.add_argument("--layout", choices=tuple(LAYOUTS), default="native",
                        help="Expected layout: native (15 jobs), desktop (11), or legacy (6). "
                             "Never inferred from a potentially incomplete job list.")
    report.add_argument("--output")
    args = parser.parse_args()
    status = 0
    try:
        if args.command == "collect":
            if not 1 <= args.limit <= 100 or not 1 <= args.max_pages <= 20:
                raise ValueError("Use limit 1..100 and max-pages 1..20")
            data = collect(args)
        else:
            data = summarize(json.loads(Path(args.input).read_text(encoding="utf-8")), layout=args.layout)
        text = json.dumps(data, indent=2) + "\n"
        if args.output:
            output = Path(args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(text, encoding="utf-8")
        else:
            print(text, end="")
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"CI timing failed: {error}", file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
