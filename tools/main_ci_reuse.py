#!/usr/bin/env python3
"""Read-only queue-to-main CI reuse for issue #1808.

Native, mobile, UEFI, desktop and analyzer validation are reusable. Desktop
jobs retain main's Zig cache lifecycle, analyzer retains a truthful receipt,
and lint still checks the main history. This module
never creates a check or changes a ref. A missing proof schedules the normal
jobs, while a proof that changes after jobs were skipped fails the aggregate.
"""

import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

import github_ci_time
from merge_queue_admission import AdmissionError, GitHub, require

REPOSITORY = "buster14a/buster"
REPOSITORY_ID = 1071732997
WORKFLOW_ID = 197051687
WORKFLOW_PATH = ".github/workflows/ci.yml"
POLICY = "buster-main-ci-reuse-v2"
MAX_AGE = timedelta(hours=2)
SHA = re.compile(r"[0-9a-f]{40}\Z")
ARTIFACT_DIGEST = re.compile(r"sha256:[0-9a-f]{64}\Z")
# Job name, artifact prefix, and mandatory coverage step. The artifact contains
# the resolved toolchain/runner provenance and the source-run diagnostics.
REUSED = (
    ("Linux x86-64 native", "native-linux-x86_64", "Execution-mode matrix"),
    ("Linux AArch64 native", "native-linux-aarch64", "Execution-mode matrix"),
    ("macOS AArch64 native", "native-macos-aarch64", "Execution-mode matrix"),
    ("Windows x86-64 native", "native-windows-x86_64", "Execution-mode matrix (Windows)"),
    ("Windows AArch64 native", "native-windows-aarch64", "Execution-mode matrix (Windows)"),
    ("Android x86-64", "mobile-android-x86_64", "Test (Android)"),
    ("iOS AArch64", "mobile-ios-aarch64", "Test (iOS simulator)"),
    ("UEFI firmware boot", "uefi-boot", "Build compiler and boot both architectures in all allocators"),
)
REUSED_NAMES = frozenset(row[0] for row in REUSED)
# Desktop jobs still execute cache publication on main, under their existing
# names and authority. Only their already-proven validation is reused.
DESKTOP = tuple((f"{name} {shard}", f"desktop-{os_name}-{arch}-{shard}",
                 "Combination matrix (Windows)" if os_name == "windows"
                 else "Combination matrix (Linux, macOS)")
                for name, os_name, arch in (
                    ("Linux x86-64", "linux", "x86_64"),
                    ("Linux AArch64", "linux", "aarch64"),
                    ("macOS AArch64", "macos", "aarch64"),
                    ("Windows x86-64", "windows", "x86_64"),
                    ("Windows AArch64", "windows", "aarch64"))
                for shard in github_ci_time.COMBINATION_SHARDS)
DESKTOP_NAMES = frozenset(row[0] for row in DESKTOP)
ANALYZER_STEPS = ("Bootstrap candidate and select reference build driver",
                  "Exercise analyzer failure and coverage controls",
                  "Configure the authoritative split-source database",
                  "Compare reference analysis and aggregate all module shards")
ANALYZER_RECEIPT_STEPS = ("Report reused analyzer validation",
                         "Retain analyzer inventory, results and measurements")
SOURCE_COVERAGE = REUSED + DESKTOP + (("Clang analyzer shards", "clang-analyzer", ANALYZER_STEPS[-1]),)
CACHE_STEPS = ("Checkout", "Resolve exact Zig cache policy", "Restore Zig archive",
               "Validate exact Zig cache restore", "Install verified Zig",
               "Retain exact Zig cache evidence", "Report reused desktop validation",
               "Sanitize desktop logs", "Retain desktop logs")
VALIDATION_STEPS = ("Workflow tool regression tests", "Bootstrap wrapper regression tests",
                    "Install mold", "Install latest stable LLVM", "Application compilers",
                    "Combination matrix (Linux, macOS)", "Combination matrix (Windows)",
                    "Collect CMake configure evidence", "Desktop result and reproduction")
RETAINED_NAMES = tuple(name for name in github_ci_time.COMBINATION_JOBS
                       if name not in REUSED_NAMES)


def instant(value, label):
    require(isinstance(value, str), label + " has no timestamp")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise AdmissionError(label + " has an invalid timestamp") from error
    require(parsed.tzinfo is not None, label + " has no timezone")
    return parsed


def exact_run(run, *, run_id, sha, event, branch):
    require(isinstance(run, dict) and run.get("id") == run_id and type(run_id) is int and run_id > 0,
            "run ID mismatch")
    require(run.get("workflow_id") == WORKFLOW_ID and
            run.get("path", "").split("@", 1)[0] == WORKFLOW_PATH,
            "workflow identity mismatch")
    require(run.get("repository", {}).get("full_name") == REPOSITORY and
            run.get("repository", {}).get("id") == REPOSITORY_ID and
            run.get("head_repository", {}).get("id") == REPOSITORY_ID,
            "repository identity mismatch")
    require(run.get("head_sha") == sha and run.get("head_commit", {}).get("id") == sha,
            "run commit mismatch")
    require(run.get("event") == event and run.get("head_branch") == branch,
            "run event or branch mismatch")
    # An explicit rerun is a fresh execution request. Refuse partial rerun
    # reconstruction and a superseding incomplete or failed attempt.
    require(run.get("run_attempt") == 1, "rerun requires fresh main validation")


def source_run(api, sha):
    rows = api.pages("actions/runs", "workflow_runs", event="merge_group", head_sha=sha)
    candidates = [row for row in rows if row.get("workflow_id") == WORKFLOW_ID]
    require(len(candidates) == 1, "missing or ambiguous exact queue workflow")
    source = api.get("actions/runs/" + str(candidates[0]["id"]))
    require(source.get("id") == candidates[0].get("id") and
            source.get("run_attempt") == candidates[0].get("run_attempt"),
            "queue run changed during discovery")
    branch = source.get("head_branch", "")
    require(isinstance(branch, str) and branch.startswith("gh-readonly-queue/main/"),
            "source is not a main merge-queue ref")
    exact_run(source, run_id=candidates[0]["id"], sha=sha, event="merge_group", branch=branch)
    require(source.get("status") == "completed" and source.get("conclusion") == "success",
            "queue workflow has not succeeded")
    return source


def successful_source_jobs(api, source, sha):
    run_id = source["id"]
    jobs = api.pages(f"actions/runs/{run_id}/attempts/1/jobs", "jobs")
    jobs, extras = github_ci_time.separate_reuse_job(jobs, run_id, 1, sha)
    require(not extras, "; ".join(extras))
    errors = github_ci_time.validate_required_jobs(jobs, run_id, 1, sha,
                                                   complete_active=False)
    require(not errors, "; ".join(errors))
    identifiers = [job.get("id") for job in jobs]
    require(all(type(identifier) is int and identifier > 0 for identifier in identifiers) and
            len(set(identifiers)) == len(identifiers), "source job IDs are missing or duplicated")
    by_name = {job["name"]: job for job in jobs}
    for name, _, step in SOURCE_COVERAGE:
        mandatory = {step}
        if name in github_ci_time.ANALYZER:
            mandatory.update(ANALYZER_STEPS)
        if name in github_ci_time.NATIVE:
            if name.startswith("Windows"):
                mandatory.add("Native MSVC reference differential")
            else:
                mandatory.add("Native configuration differential matrix")
        for required_step in mandatory:
            matches = [row for row in by_name[name].get("steps", [])
                       if row.get("name") == required_step]
            require(len(matches) == 1 and matches[0].get("status") == "completed" and
                    matches[0].get("conclusion") == "success",
                    name + ": required coverage step is missing or unsuccessful: " + required_step)
    return [{"name": name, "job_id": by_name[name]["id"]} for name, _, _ in SOURCE_COVERAGE]


def retained_artifacts(api, source, now):
    run_id = source["id"]
    rows = api.pages(f"actions/runs/{run_id}/artifacts", "artifacts")
    result = []
    for _, prefix, _ in SOURCE_COVERAGE:
        name = f"{prefix}-{run_id}-1"
        matches = [row for row in rows if row.get("name") == name]
        require(len(matches) == 1, "missing or ambiguous queue artifact: " + name)
        artifact = matches[0]
        origin = artifact.get("workflow_run", {})
        require(type(artifact.get("id")) is int and artifact["id"] > 0 and
                type(artifact.get("size_in_bytes")) is int and artifact["size_in_bytes"] > 0 and
                artifact.get("expired") is False and
                ARTIFACT_DIGEST.fullmatch(artifact.get("digest", "")) is not None and
                instant(artifact.get("expires_at"), name) > now and
                origin.get("id") == run_id and origin.get("repository_id") == REPOSITORY_ID and
                origin.get("head_repository_id") == REPOSITORY_ID and
                origin.get("head_sha") == source["head_sha"] and
                origin.get("head_branch") == source["head_branch"],
                "queue artifact expired, empty or misbound: " + name)
        result.append({"name": name, "id": artifact["id"], "digest": artifact["digest"]})
    return result


def receipt_digest(receipt):
    payload = json.dumps(receipt, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(payload).hexdigest()


def verify_source(api, sha, current_run_id, workflow_blob, now):
    current = api.get("actions/runs/" + str(current_run_id))
    exact_run(current, run_id=current_run_id, sha=sha, event="push", branch="main")
    require(current.get("run_attempt") == 1, "main run was rerun")
    source = source_run(api, sha)
    completed = instant(source.get("updated_at"), "source completion")
    started = instant(current.get("created_at"), "main creation")
    require(completed <= started and started - completed <= MAX_AGE,
            "queue result is not a fresh predecessor of this main push")
    require(completed <= now, "source completion is in the future")
    blob = api.get("contents/" + WORKFLOW_PATH, ref=sha)
    require(blob.get("type") == "file" and blob.get("sha") == workflow_blob,
            "workflow revision differs from exact checkout")
    jobs = successful_source_jobs(api, source, sha)
    artifacts = retained_artifacts(api, source, now)
    # Re-read both runs after paged jobs and artifacts: an attempt change or
    # interrupted source cannot authorize a skip during collection.
    final_source = api.get("actions/runs/" + str(source["id"]))
    final_current = api.get("actions/runs/" + str(current_run_id))
    require(final_source.get("run_attempt") == 1 and
            final_source.get("status") == "completed" and
            final_source.get("conclusion") == "success" and
            final_source.get("updated_at") == source["updated_at"] and
            final_current.get("run_attempt") == 1 and
            final_current.get("head_sha") == sha,
            "run or attempt moved during evidence collection")
    return {"policy": POLICY, "repository": REPOSITORY, "head_sha": sha,
            "workflow_id": WORKFLOW_ID, "workflow_path": WORKFLOW_PATH,
            "workflow_blob": workflow_blob, "source_run_id": source["id"],
            "source_attempt": 1, "source_completed_at": source["updated_at"],
            "source_jobs": jobs, "source_artifacts": artifacts}


def verify_current_jobs(api, sha, run_id):
    current = api.get("actions/runs/" + str(run_id))
    exact_run(current, run_id=run_id, sha=sha, event="push", branch="main")
    rows = api.pages(f"actions/runs/{run_id}/jobs", "jobs", filter="all")
    jobs = github_ci_time.latest_run_jobs(rows, run_id, 1, sha)
    jobs, extras = github_ci_time.separate_reuse_job(jobs, run_id, 1, sha, required=True)
    require(not extras, "; ".join(extras))
    reused = [job for job in jobs if job.get("name") in REUSED_NAMES]
    require(all(job.get("status") == "completed" and job.get("conclusion") == "skipped"
                for job in reused), "reusable main job ran or has an ambiguous result")
    jobs = [job for job in jobs if job.get("name") not in REUSED_NAMES]
    desktop = [job for job in jobs if job.get("name") in DESKTOP_NAMES]
    require({job["name"] for job in desktop} == DESKTOP_NAMES and len(desktop) == len(DESKTOP),
            "main cache job identities are missing or duplicated")
    for job in desktop:
        require(job.get("run_id") == run_id and job.get("head_sha") == sha and
                job.get("run_attempt") == 1 and job.get("status") == "completed" and
                job.get("conclusion") == "success", "main cache job failed or is misbound")
        for step_name in CACHE_STEPS + VALIDATION_STEPS:
            matches = [step for step in job.get("steps", []) if step.get("name") == step_name]
            expected = "success" if step_name in CACHE_STEPS else "skipped"
            require(len(matches) == 1 and matches[0].get("status") == "completed" and
                    matches[0].get("conclusion") == expected,
                    job["name"] + ": cache-only step proof missing: " + step_name)
    analyzer = [job for job in jobs if job.get("name") in github_ci_time.ANALYZER]
    require(len(analyzer) == 1, "main analyzer receipt job is missing or duplicated")
    for job in analyzer:
        require(job.get("run_id") == run_id and job.get("head_sha") == sha and
                job.get("run_attempt") == 1 and job.get("status") == "completed" and
                job.get("conclusion") == "success", "main analyzer receipt failed or is misbound")
        for step_name in ANALYZER_STEPS + ANALYZER_RECEIPT_STEPS:
            matches = [step for step in job.get("steps", []) if step.get("name") == step_name]
            expected = "success" if step_name in ANALYZER_RECEIPT_STEPS else "skipped"
            require(len(matches) == 1 and matches[0].get("status") == "completed" and
                    matches[0].get("conclusion") == expected,
                    "main analyzer receipt step proof missing: " + step_name)
    validation = [job for job in jobs if job.get("name") not in DESKTOP_NAMES and
                  job.get("name") not in github_ci_time.ANALYZER]
    errors = github_ci_time.validate_required_jobs(validation, run_id, 1, sha,
                       expected_names=tuple(name for name in RETAINED_NAMES if name not in DESKTOP_NAMES and
                                            name not in github_ci_time.ANALYZER))
    require(not errors, "; ".join(errors))
    identifiers = [job.get("id") for job in jobs]
    require(all(type(identifier) is int and identifier > 0 for identifier in identifiers) and
            len(set(identifiers)) == len(identifiers), "current job IDs are missing or duplicated")
    require(api.get("actions/runs/" + str(run_id)).get("run_attempt") == 1,
            "main attempt moved during job collection")
    return [{"name": job["name"], "job_id": job["id"]} for job in jobs]


def local_workflow_blob(sha):
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    require(head == sha, "checkout does not match the main run")
    return subprocess.check_output(["git", "hash-object", WORKFLOW_PATH], text=True).strip()


def cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("decide", "finish"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-digest", default="")
    arguments = parser.parse_args()
    status = 0
    receipt = None
    try:
        require(os.environ.get("GITHUB_REPOSITORY") == REPOSITORY and
                os.environ.get("GITHUB_EVENT_NAME") == "push" and
                os.environ.get("GITHUB_REF") == "refs/heads/main", "not a main push")
        sha = os.environ.get("GITHUB_SHA", "")
        require(SHA.fullmatch(sha) is not None, "invalid main commit")
        run_id = int(os.environ.get("GITHUB_RUN_ID", "0"))
        require(os.environ.get("GITHUB_RUN_ATTEMPT") == "1", "explicit rerun requires full CI")
        api = GitHub(REPOSITORY, os.environ.get("GH_TOKEN", ""))
        receipt = verify_source(api, sha, run_id, local_workflow_blob(sha),
                                datetime.now(timezone.utc))
        if arguments.phase == "finish":
            require(receipt_digest(receipt) == arguments.expected_digest,
                    "queue evidence changed after main jobs were skipped")
            receipt["main_jobs"] = verify_current_jobs(api, sha, run_id)
            source = api.get("actions/runs/" + str(receipt["source_run_id"]))
            require(source.get("run_attempt") == 1 and source.get("status") == "completed" and
                    source.get("conclusion") == "success" and
                    source.get("updated_at") == receipt["source_completed_at"],
                    "queue attempt moved after main job collection")
    except Exception as error:
        if arguments.phase == "finish":
            print("Main CI reuse re-verification failed: " + str(error), file=sys.stderr)
            status = 1
        else:
            print("Main CI reuse unavailable; running full validation: " + str(error))
    else:
        source = receipt["source_run_id"]
        print(f"Main CI reuse verified {len(SOURCE_COVERAGE)} source jobs in "
              f"https://github.com/{REPOSITORY}/actions/runs/{source}/attempts/1")
        arguments.output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    if arguments.phase == "decide":
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write("reuse=" + ("true" if receipt is not None else "false") + "\n")
            if receipt is not None:
                output.write("receipt_digest=" + receipt_digest(receipt) + "\n")
    if receipt is not None:
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
            summary.write(f"Queue validation reused for {len(SOURCE_COVERAGE)} jobs: "
                          f"[run {receipt['source_run_id']}, attempt 1]"
                          f"(https://github.com/{REPOSITORY}/actions/runs/"
                          f"{receipt['source_run_id']}/attempts/1). "
                          "Native, mobile and UEFI jobs did not execute on main. "
                          "Desktop jobs ran only the main Zig cache lifecycle and analyzer only retained a receipt; "
                          "their validation did not repeat.\n")
    return status


if __name__ == "__main__":
    sys.exit(cli())
