#!/usr/bin/env python3
"""Dispatch one read-only rebind for each exact front-of-queue group (#1811).

This trusted-main controller makes a bounded pass and never executes candidate
bytes. GitHub still owns the queue; this only schedules an optional closure
check once its exact predecessor is live main. A failed worker is never retried
to green automatically.
"""

import argparse
import json
import os
from pathlib import Path
import sys
import urllib.request

import merge_queue_admission as admission


WORKFLOW = "native-retirement-rebind.yml"
WORKFLOW_PATH = ".github/workflows/" + WORKFLOW
MAIN_WORKFLOW_PATHS = {WORKFLOW_PATH + "@main", WORKFLOW_PATH + "@refs/heads/main"}


class Dispatcher:
    def __init__(self, repository, token):
        admission.require(admission.REPOSITORY.fullmatch(repository) is not None,
                          "invalid repository")
        admission.require(bool(token), "GH_TOKEN is required")
        self.url = "https://api.github.com/repos/" + repository + "/actions/workflows/" + WORKFLOW + "/dispatches"
        self.token = token

    def dispatch(self, candidate):
        body = {"ref": "main", "inputs": {
            "base_sha": candidate["base"], "head_sha": candidate["head"],
            "head_ref": candidate["head_ref"],
        }}
        request = urllib.request.Request(self.url, data=json.dumps(body).encode(),
                                         method="POST", headers={
            "Authorization": "Bearer " + self.token,
            "Accept": "application/vnd.github+json",
            "Content-Type": "application/json",
            "X-GitHub-Api-Version": "2022-11-28",
        })
        with urllib.request.urlopen(request, timeout=30) as response:
            admission.require(response.status == 204, "rebind dispatch did not return 204")


def existing_worker(api, repository, head):
    runs = api.pages("actions/workflows/" + WORKFLOW + "/runs", "workflow_runs",
                     event="workflow_dispatch", branch="main")
    for run in runs:
        admission.require(isinstance(run, dict), "malformed rebind workflow run")
        if run.get("display_title") != "Native rebind " + head:
            continue
        admission.require(run.get("path") in MAIN_WORKFLOW_PATHS and
                          run.get("event") == "workflow_dispatch" and
                          run.get("head_branch") == "main" and
                          run.get("repository", {}).get("full_name") == repository and
                          type(run.get("id")) is int and run["id"] > 0,
                          "same-name rebind run has an invalid identity")
        return {"run_id": run["id"], "run_attempt": run.get("run_attempt"),
                "status": run.get("status"), "conclusion": run.get("conclusion")}
    return None


def reconcile(args, api, dispatcher):
    repo = args.repo_root
    policy = admission.git(repo, "rev-parse", "HEAD")
    main, refs = admission.queue_refs(repo)
    groups = []
    for ref, head in sorted(refs.items()):
        row = {"head_ref": ref, "head_sha": head}
        try:
            if admission.ancestor(repo, head, main):
                row["status"] = "landed"
            else:
                base = admission.git(repo, "rev-parse", head + "^1")
                row["base_sha"] = base
                if base != main and admission.ancestor(repo, main, base):
                    row["status"] = "waiting"
                elif base != main:
                    row["status"] = "divergent"
                elif policy != main:
                    row["status"] = "stale-policy"
                else:
                    event = {"action": "checks_requested",
                             "repository": {"full_name": args.repository},
                             "merge_group": {"base_ref": "refs/heads/main", "base_sha": base,
                                             "head_sha": head, "head_ref": ref}}
                    candidate = admission.identity(event, args.repository, head, repo,
                                                   trusted_sha=policy)
                    admission.require(admission.live_identity(api, candidate),
                                      "group changed before dispatch")
                    admission.verify_trusted_policy(candidate, repo)
                    existing = existing_worker(api, args.repository, head)
                    if existing:
                        row.update(status="present", worker=existing)
                    else:
                        admission.require(admission.live_identity(api, candidate),
                                          "group changed during dispatch scan")
                        admission.verify_trusted_policy(candidate, repo)
                        dispatcher.dispatch(candidate)
                        row["status"] = "dispatched"
        except (admission.AdmissionError, OSError, KeyError, TypeError, ValueError) as error:
            row.update(status="retry", detail=str(error))
        groups.append(row)
    return {"policy_sha": policy, "main": main, "groups": groups}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    result = reconcile(args, admission.GitHub(args.repository, os.environ.get("GH_TOKEN", "")),
                       Dispatcher(args.repository, os.environ.get("GH_TOKEN", "")))
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(json.dumps(result, sort_keys=True))
    return 1 if any(row["status"] == "retry" for row in result["groups"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
