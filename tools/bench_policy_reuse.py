#!/usr/bin/env python3
"""Read-only benchmark-policy queue-to-main reuse (#2462).

verify collects exact, complete GitHub execution evidence twice around the read;
decide falls back to the unchanged tests on uncertainty; finish rechecks the
bound receipt after those tests were skipped. No check/ref writes, test executor,
new HTTP client or import of the shared Buster CI matrix policy lives here.
"""

import argparse
from datetime import datetime, timedelta
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

REPOSITORY = "buster14a/buster"
REPOSITORY_ID = 1071732997
WORKFLOW_ID = 361813014
WORKFLOW_PATH = ".github/workflows/bench-service-policy.yml"
JOB_NAME = "Benchmark service workflow policy"
SCHEMA = "buster-benchmark-policy-reuse-v1"
MAX_AGE = timedelta(hours=2)
MAX_RECEIPT_BYTES = 32768
WORK_STEPS = (
    "Prove the direct 9700X workflow gate",
    "Replay micro-architecture lab formats",
)
CONTROL_STEP = "Test benchmark policy reuse"
DECISION_STEP = "Decide verified queue reuse"
FINISH_STEP = "Verify reused benchmark policy evidence"
REQUIRED_STEPS = ("Require CI admission to be enabled", CONTROL_STEP, DECISION_STEP) + WORK_STEPS


class ReuseRefused(ValueError):
    """Missing or changed execution proof, never permission to skip tests."""


def require(condition, message):
    if not condition:
        raise ReuseRefused(message)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def timestamp(value):
    require(isinstance(value, str), "missing timestamp")
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    require(parsed.tzinfo is not None, "timestamp lacks timezone")
    return parsed


def positive_int(value):
    return type(value) is int and value > 0


def run_identity(run, run_id, sha, event, branch):
    require(isinstance(run, dict) and positive_int(run_id) and
            type(run.get("id")) is int and run["id"] == run_id, "wrong run ID")
    require(type(run.get("workflow_id")) is int and run["workflow_id"] == WORKFLOW_ID and
            run.get("path") == WORKFLOW_PATH, "wrong workflow")
    for key in ("repository", "head_repository"):
        repository = run.get(key)
        require(isinstance(repository, dict) and repository.get("full_name") == REPOSITORY and
                type(repository.get("id")) is int and repository["id"] == REPOSITORY_ID,
                "wrong repository")
    require(run.get("head_sha") == sha and run.get("head_commit", {}).get("id") == sha,
            "wrong source commit")
    require(run.get("event") == event and run.get("head_branch") == branch,
            "wrong event or branch")
    require(type(run.get("run_attempt")) is int and run["run_attempt"] == 1,
            "rerun requires fresh validation")


def unique_row(payload, field):
    # Exactly one source run and one policy job are permitted. A capped or
    # incomplete page cannot prove uniqueness; do not silently filter it.
    require(isinstance(payload, dict) and type(payload.get("total_count")) is int and
            payload["total_count"] == 1, "missing or ambiguous " + field)
    rows = payload.get(field)
    require(isinstance(rows, list) and len(rows) == 1 and isinstance(rows[0], dict),
            "incomplete " + field)
    return rows[0]


def discovery(api, sha):
    return unique_row(api.get(f"actions/workflows/{WORKFLOW_ID}/runs",
                             head_sha=sha, event="merge_group", per_page=100, page=1),
                      "workflow_runs")


def source_identity(source, sha, main_created):
    branch = source.get("head_branch")
    require(isinstance(branch, str) and branch.startswith("gh-readonly-queue/main/") and
            len(branch) > len("gh-readonly-queue/main/"), "wrong queue ref")
    run_identity(source, source.get("id"), sha, "merge_group", branch)
    require(source.get("status") == "completed" and source.get("conclusion") == "success",
            "queue execution has not succeeded")
    completed = timestamp(source.get("updated_at"))
    require(timestamp(source.get("created_at")) <= completed and
            timedelta(0) <= main_created - completed <= MAX_AGE,
            "source is stale or did not finish before main started")
    return {"run_id": source["id"], "attempt": 1, "branch": branch,
            "created_at": source["created_at"], "completed_at": source["updated_at"]}


def declared_steps(workflow_bytes):
    # The existing workflow uses one literal, named step per declaration.
    # Derive additional controls from those exact bytes, not a second inventory.
    names = re.findall(r"(?m)^      - name: (.+)$", workflow_bytes.decode("utf-8"))
    require(len(names) == len(set(names)) and
            (set(REQUIRED_STEPS) | {FINISH_STEP}) <= set(names),
            "invalid named workflow step inventory")
    return set(names)


def job_evidence(job, source, sha, expected_steps):
    require(positive_int(job.get("id")) and job.get("name") == JOB_NAME and
            type(job.get("run_id")) is int and job["run_id"] == source["run_id"] and
            type(job.get("run_attempt")) is int and job["run_attempt"] == 1 and
            job.get("head_sha") == sha and job.get("head_branch") == source["branch"] and
            job.get("workflow_name") == JOB_NAME, "wrong policy job identity")
    require(job.get("status") == "completed" and job.get("conclusion") == "success" and
            positive_int(job.get("runner_id")) and job.get("labels") == ["ubuntu-latest"],
            "policy job did not execute successfully on its declared runner")
    require(timestamp(source["created_at"]) <= timestamp(job.get("started_at")) <=
            timestamp(job.get("completed_at")) <= timestamp(source["completed_at"]),
            "invalid job execution interval")
    steps = job.get("steps")
    require(isinstance(steps, list) and steps, "missing policy steps")
    names, numbers, evidence = set(), set(), []
    for step in steps:
        require(isinstance(step, dict), "malformed step")
        name, number = step.get("name"), step.get("number")
        require(isinstance(name, str) and name and name not in names and
                positive_int(number) and number not in numbers, "duplicate or invalid step")
        names.add(name)
        numbers.add(number)
        conclusion = "skipped" if name == FINISH_STEP else "success"
        require(step.get("status") == "completed" and step.get("conclusion") == conclusion,
                "source step was not freshly executed: " + name)
        evidence.append({"name": name, "number": number, "conclusion": conclusion})
    require(expected_steps <= names, "missing mandatory policy step")
    return {"id": job["id"], "runner_id": job["runner_id"], "labels": job["labels"],
            "started_at": job["started_at"], "completed_at": job["completed_at"],
            "steps": evidence}


def verify(api, sha, run_id, workflow_bytes):
    require(isinstance(sha, str) and re.fullmatch(r"[0-9a-f]{40}", sha), "invalid commit SHA")
    expected_steps = declared_steps(workflow_bytes)
    main = api.get(f"actions/runs/{run_id}")
    run_identity(main, run_id, sha, "push", "main")
    require(main.get("status") == "in_progress" and main.get("conclusion") is None,
            "main run is not active")
    main_created = timestamp(main.get("created_at"))
    source = source_identity(discovery(api, sha), sha, main_created)
    source_path = f"actions/runs/{source['run_id']}"
    require(source_identity(api.get(source_path), sha, main_created) == source,
            "source changed during discovery")
    workflow_blob = hashlib.sha1(b"blob " + str(len(workflow_bytes)).encode() + b"\0" +
                                workflow_bytes).hexdigest()
    blob = api.get("contents/" + WORKFLOW_PATH, ref=sha)
    require(isinstance(blob, dict) and blob.get("type") == "file" and
            blob.get("path") == WORKFLOW_PATH and blob.get("sha") == workflow_blob and
            type(blob.get("size")) is int and blob["size"] == len(workflow_bytes),
            "local workflow differs from exact-source workflow")
    job = unique_row(api.get(source_path + "/attempts/1/jobs", per_page=100, page=1), "jobs")
    evidence = job_evidence(job, source, sha, expected_steps)
    require(source_identity(discovery(api, sha), sha, main_created) == source and
            source_identity(api.get(source_path), sha, main_created) == source,
            "source was replaced or changed during collection")
    current = api.get(f"actions/runs/{run_id}")
    run_identity(current, run_id, sha, "push", "main")
    require(current.get("created_at") == main["created_at"] and
            current.get("status") == "in_progress" and current.get("conclusion") is None,
            "main run changed during collection")
    return {"schema": SCHEMA, "repository": REPOSITORY, "repository_id": REPOSITORY_ID,
            "sha": sha, "workflow_id": WORKFLOW_ID, "workflow_blob": workflow_blob,
            "main_run_id": run_id, "main_attempt": 1, "main_created_at": main["created_at"],
            "source": source, "job": evidence}


def make_api():
    # Reuse the existing GET-only reader and its bounded transport retries.
    from merge_queue_admission import GitHub
    return GitHub(REPOSITORY, os.environ.get("GH_TOKEN", ""))


def eligible(environment):
    return all(environment.get(key) == value for key, value in (
        ("GITHUB_SERVER_URL", "https://github.com"), ("GITHUB_REPOSITORY", REPOSITORY),
        ("GITHUB_EVENT_NAME", "push"), ("GITHUB_REF", "refs/heads/main"),
        ("GITHUB_RUN_ATTEMPT", "1")))


def collect(root):
    sha = os.environ.get("GITHUB_SHA", "")
    head = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], check=True,
                          capture_output=True, text=True, timeout=10).stdout.strip()
    require(head == sha, "checkout does not match main run")
    run_id = os.environ.get("GITHUB_RUN_ID", "")
    require(re.fullmatch(r"[1-9][0-9]*", run_id) is not None, "invalid main run ID")
    return verify(make_api(), sha, int(run_id), (root / WORKFLOW_PATH).read_bytes())


def safe_reason(error):
    # Transport errors can contain remote text. Persist only our own bounded
    # messages or the exception type; never tokens, response bodies or headers.
    message = str(error) if isinstance(error, ReuseRefused) else type(error).__name__
    token = os.environ.get("GH_TOKEN", "")
    if token:
        message = message.replace(token, "[redacted]")
    return " ".join(message.split())[:1024]


def cli(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("decide", "finish"))
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    args = parser.parse_args(argv)
    status = 0
    try:
        if args.phase == "decide":
            report = {"schema": SCHEMA, "mode": "fresh", "reason": "not a first-attempt main push"}
            if eligible(os.environ):
                try:
                    receipt = collect(args.repo_root)
                    report = {"schema": SCHEMA, "mode": "reuse-selected", "receipt": receipt,
                              "sha256": hashlib.sha256(canonical(receipt).encode()).hexdigest()}
                except Exception as error:
                    # This is only an optimization. No proof means original tests.
                    report["reason"] = safe_reason(error)
            encoded = canonical(report)
            require(len(encoded.encode()) + 1 <= MAX_RECEIPT_BYTES, "receipt exceeds bound")
            args.receipt.write_text(encoded + "\n", encoding="utf-8")
            print(encoded)
            with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
                output.write("reused=" + str(report["mode"] == "reuse-selected").lower() + "\n")
        else:
            require(eligible(os.environ), "finalization is not a first-attempt main push")
            with args.receipt.open("rb") as source:
                data = source.read(MAX_RECEIPT_BYTES + 1)
            require(len(data) <= MAX_RECEIPT_BYTES, "receipt exceeds bound")
            report = json.loads(data)
            require(isinstance(report, dict) and report.get("schema") == SCHEMA and
                    report.get("mode") == "reuse-selected", "missing selected receipt")
            receipt = collect(args.repo_root)
            encoded = canonical(receipt)
            require(canonical(report.get("receipt")) == encoded and
                    report.get("sha256") == hashlib.sha256(encoded.encode()).hexdigest(),
                    "bound evidence changed after policy tests were skipped")
            source_id = receipt["source"]["run_id"]
            message = (f"Benchmark policy validation reused from https://github.com/{REPOSITORY}/"
                       f"actions/runs/{source_id}/attempts/1 for `{receipt['sha']}`. "
                       "Policy tests executed in that source run, not again on main. "
                       "CI-enabled admission and reuse controls executed freshly on main.\n")
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
                summary.write(message + "\n```json\n" + encoded + "\n```\n")
            print(message + encoded)
    except Exception as error:
        print("Benchmark policy reuse refused: " + safe_reason(error), file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(cli())
