#!/usr/bin/env python3
"""Reconcile retirement requests using the existing trusted writer (#1791).

Ownership: this file and its workflow execute only from protected main. PR
comments are a durable deduplication ledger, NOT authorization; the writer
requires the exact request artifact of this trusted controller run. No candidate
code, generated-file publication, merge mutation or host execution occurs here.

Map: ledger codec; writer/outcome reconciliation; candidate selection; two-phase
plan -> immutable artifact upload -> dispatch. An uncertain POST is never retried.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys

import native_retirement_automation as automation
import native_retirement_integration as integration

MARKER = "<!-- native-retirement-automation:v1 -->\n"
LEDGER_SCHEMA = "buster-native-retirement-automation-ledger-v1"
STATES = frozenset(("claimed", "dispatched", "published", "superseded", "blocked"))
ACTIVE = frozenset(("queued", "in_progress", "waiting", "pending", "requested"))
OUTCOME_JOB = "Native retirement automation outcome"
OUTCOME_STEPS = {"Superseded request": "superseded", "Published request": "published",
                 "Blocked request": "blocked"}


def ledger_body(record: dict) -> str:
    return MARKER + automation.canonical(record).decode()


def parse_ledger(comment: dict, repository: str) -> dict | None:
    body = comment.get("body", "")
    result = None
    if isinstance(body, str) and body.startswith(MARKER) and automation.is_bot(comment.get("user")):
        record = automation.decode(body[len(MARKER):])
        if (set(record) != {"schema", "request", "state", "run_id", "detail"} or
                record.get("schema") != LEDGER_SCHEMA or record.get("state") not in STATES or
                not isinstance(record.get("detail"), str)):
            raise automation.AutomationError("invalid controller ledger")
        automation.validate_request(record["request"], repository)
        if record["run_id"] is not None:
            automation.positive(record["run_id"], "ledger writer run")
        result = {**record, "comment_id": automation.positive(comment.get("id"), "ledger comment")}
    return result


def ledger(api, number: int) -> list[dict]:
    records = []
    keys = set()
    for comment in api.all("issues/" + str(number) + "/comments"):
        record = parse_ledger(comment, api.repository)
        if record is not None:
            if record["request"]["number"] != number or record["request"]["key"] in keys:
                raise automation.AutomationError("duplicate or cross-PR controller claim")
            keys.add(record["request"]["key"])
            records.append(record)
    return records


def save(api, record: dict, state: str, detail: str, run_id: int | None = None) -> dict:
    value = {key: record[key] for key in ("schema", "request", "state", "run_id", "detail")}
    value.update(state=state, detail=detail, run_id=run_id or value["run_id"])
    api.request("issues/comments/" + str(record["comment_id"]), method="PATCH",
                body={"body": ledger_body(value)})
    return {**value, "comment_id": record["comment_id"]}


def find_writer(api, record: dict) -> dict | None:
    request = record["request"]
    matches = []
    if record["run_id"] is not None:
        matches = [api.request("actions/runs/" + str(record["run_id"]))]
    else:
        controller = api.request("actions/runs/" + str(request["controller_run_id"]))
        created = controller.get("created_at")
        if not isinstance(created, str):
            raise automation.AutomationError("controller creation identity unavailable")
        runs = automation.collection(
            api, "actions/workflows/" + Path(automation.WRITER_PATH).name + "/runs",
            "workflow_runs", branch="main", event="workflow_dispatch", created=">=" + created)
        matches = [run for run in runs if run.get("display_title") == automation.writer_title(request["key"])]
    if len(matches) > 1:
        raise automation.AutomationError("ambiguous duplicate writer runs; owner reconciliation required")
    run = matches[0] if matches else None
    if run is not None:
        automation.verify_run(run, api.repository, run["id"], automation.WRITER_PATH,
                              request["base"], frozenset(("workflow_dispatch",)), bot=True)
        if run.get("display_title") != automation.writer_title(request["key"]):
            raise automation.AutomationError("ledger refers to a different writer request")
    return run


def writer_outcome(api, run: dict) -> str:
    if run.get("status") in ACTIVE:
        result = "active"
    elif run.get("status") != "completed":
        raise automation.AutomationError("unknown writer lifecycle state")
    elif run.get("conclusion") == "success":
        result = "published"
    elif run.get("conclusion") != "failure":
        # Cancellation, timeout, action-required and unknown conclusions do not
        # authorize redispatch, even when the inputs have meanwhile advanced.
        result = "blocked"
    else:
        jobs = automation.collection(api, "actions/runs/" + str(run["id"]) +
                                     "/attempts/1/jobs", "jobs")
        outcomes = [job for job in jobs if job.get("name") == OUTCOME_JOB]
        result = "blocked"
        if len(outcomes) == 1 and outcomes[0].get("conclusion") == "success":
            steps = [OUTCOME_STEPS[step["name"]] for step in outcomes[0].get("steps", [])
                     if step.get("name") in OUTCOME_STEPS and step.get("conclusion") == "success"]
            if steps == ["superseded"]:
                result = "superseded"
    return result


def reconcile(api, record: dict) -> dict:
    if record["state"] not in ("claimed", "dispatched"):
        return record
    run = find_writer(api, record)
    if run is None:
        controller = api.request("actions/runs/" + str(record["request"]["controller_run_id"]))
        if controller.get("status") not in ACTIVE:
            # A crash between POST and its receipt is intentionally not a new
            # POST. The exact-key run search above can recover a visible run;
            # an absent/uncertain dispatch stops here rather than duplicates it.
            return save(api, record, "blocked", "dispatch absent or uncertain; no automatic POST retry")
        return record
    outcome = writer_outcome(api, run)
    if outcome == "active":
        result = save(api, record, "dispatched", "writer active", run["id"])
    else:
        result = save(api, record, outcome, "verified writer outcome: " + outcome, run["id"])
    return result


def disposition(records: list[dict], request: dict) -> str:
    result = "eligible"
    for record in records:
        old = record["request"]
        if record["state"] in ("claimed", "dispatched"):
            result = "active-or-uncertain"
            break
        if (record["state"] == "blocked" and old["source_head"] == request["source_head"] and
                old["policy_sha256"] == request["policy_sha256"]):
            result = "failed-or-cancelled-source"
            break
        if old["key"] == request["key"]:
            # Even a superseded observation cannot retry the same immutable
            # identity. Fresh input or policy is necessary.
            result = "already-requested"
            break
    return result


def eligible_pr(pr: dict, repository: str) -> bool:
    return (pr.get("state") == "open" and pr.get("draft") is False and
            pr.get("base", {}).get("ref") == "main" and
            (pr.get("base", {}).get("repo") or {}).get("full_name") == repository and
            (pr.get("head", {}).get("repo") or {}).get("full_name") == repository)


def resolve_candidate(repo: Path, base: str, pr: dict, api) -> dict:
    # Lazy import keeps the ledger/state-machine tests independent of unrelated
    # repository materializers; production uses the existing trusted verifier.
    import native_retirement_merge_gate as gate
    number = automation.positive(pr.get("number"), "PR number")
    expected = automation.hex_value(pr["head"]["sha"], 40, "PR head")
    reference = "refs/remotes/origin/native-retirement-controller-" + str(number)
    integration._git(repo, "fetch", "--no-tags", "origin",
                     "+refs/pull/" + str(number) + "/head:" + reference)
    if integration._commit(repo, reference) != expected:
        raise automation.AutomationMoved("PR changed during controller resolution", 76)
    try:
        record = gate.source_candidate(repo, base, expected, api)
    except gate.AdmissionError as error:
        raise automation.AutomationError(str(error)) from error
    classification = integration.classify_candidate(repo, base, record["source_head"])
    record["classification_record"] = classification.as_dict()
    record["requires_writer"] = gate.classification_requires_writer(
        classification, classification.changed_paths, gate.bound_sources(repo, base))
    parents, trailers = gate.integration_record(repo, expected)
    record["already_current"] = (len(parents) == 2 and trailers.get(gate.TRAILER_BASE) == base)
    record["previously_published"] = record["source_head"] != expected
    return record


def prerequisite_ci(api, head: str) -> bool:
    # This is the ordinary build/test aggregate, not retirement admission or
    # generated-file preflight. Missing/running/failed/cancelled CI defers work;
    # it never dispatches a new writer merely to get another test attempt.
    checks = automation.collection(api, "commits/" + head + "/check-runs", "check_runs", filter="latest")
    own = [check for check in checks if check.get("head_sha") == head and
           check.get("app", {}).get("id") == 15368]
    matches = [check for check in own if check.get("name") == "CI complete"]
    # These two gates consume the writer's future attestation. All other
    # existing Actions checks, including performance, must finish without a
    # failure/cancellation before we replace a head and thereby trigger CI.
    dependent = {"Native retirement merge admission", "Main integration admission"}
    healthy = all(check.get("status") == "completed" and
                  check.get("conclusion") in ("success", "neutral", "skipped")
                  for check in own if check.get("name") not in dependent)
    return healthy and bool(matches) and all(check.get("conclusion") == "success" for check in matches)


def active_writer(api) -> bool:
    active = False
    path = "actions/workflows/" + Path(automation.WRITER_PATH).name + "/runs"
    for status in sorted(ACTIVE):
        runs = automation.collection(api, path, "workflow_runs", branch="main", status=status)
        if runs:
            active = True
            break
    return active


def controller_context(api, base: str, run_id: int) -> dict:
    run = api.request("actions/runs/" + str(run_id))
    automation.verify_run(run, api.repository, run_id, automation.CONTROLLER_PATH,
                          base, automation.CONTROLLER_EVENTS)
    if run.get("status") != "in_progress":
        raise automation.AutomationError("controller is not an active fresh run")
    return run


def plan(api, repo: Path, base: str, run_id: int) -> dict:
    controller_context(api, base, run_id)
    if integration._commit(repo, "HEAD") != base:
        raise automation.AutomationError("controller checkout differs from its immutable main revision")
    policy, policy_digest = automation.read_policy(api, base)
    if not policy["enabled"]:
        return {"status": "disabled", "observations": []}
    automation.require_enabled(api, policy)
    if active_writer(api):
        return {"status": "writer-active", "observations": []}
    observations = []
    pulls = api.all("pulls", state="open", base="main")
    pulls.sort(key=lambda pr: (pr.get("auto_merge") is None, pr.get("number", 0)))
    selected = None
    for pull in pulls:
        if not eligible_pr(pull, api.repository):
            continue
        number = automation.positive(pull.get("number"), "PR number")
        try:
            records = [reconcile(api, record) for record in ledger(api, number)]
            if any(record["state"] in ("claimed", "dispatched") for record in records):
                observations.append({"number": number, "status": "active-or-uncertain"})
                continue
            candidate = resolve_candidate(repo, base, pull, api)
            if not candidate["requires_writer"] or candidate["already_current"]:
                observations.append({"number": number, "status": "no-publication-needed"})
                continue
            if candidate["previously_published"] and pull.get("auto_merge") is None:
                observations.append({"number": number, "status": "stale-idle-pr-deferred"})
                continue
            automation.require_scope(policy, number, candidate["classification_record"])
            if not prerequisite_ci(api, candidate["head"]):
                observations.append({"number": number, "status": "waiting-for-successful-ci-complete"})
                continue
            request = automation.new_request(
                api.repository, number, base, candidate["head"], candidate["source_head"],
                candidate["classification"], policy_digest, run_id)
            state = disposition(records, request)
            observations.append({"number": number, "status": state})
            if selected is None and state == "eligible":
                selected = request
        except (ValueError, integration.IntegrationError) as error:
            observations.append({"number": number, "status": "blocked", "detail": str(error)[:500]})
    if selected is None:
        return {"status": "idle", "observations": observations}
    # Fail before recording a claim if main/enablement changed during scanning.
    final_policy, final_digest = automation.read_policy(api, base)
    automation.require_enabled(api, final_policy)
    if final_digest != policy_digest:
        raise automation.AutomationError("policy changed during reconciliation")
    live = api.request("pulls/" + str(selected["number"]))
    if not eligible_pr(live, api.repository) or live["head"]["sha"] != selected["head"]:
        raise automation.AutomationMoved("selected PR changed during reconciliation", 76)
    # One serialized controller is the only ledger writer; a persisted claim
    # precedes artifact upload and POST, so job interruption cannot duplicate it.
    existing = ledger(api, selected["number"])
    if disposition(existing, selected) != "eligible":
        raise automation.AutomationError("candidate acquired a concurrent claim")
    record = {"schema": LEDGER_SCHEMA, "request": selected, "state": "claimed",
              "run_id": None, "detail": "claim persisted before artifact upload and dispatch"}
    comment = api.request("issues/" + str(selected["number"]) + "/comments", method="POST",
                          body={"body": ledger_body(record)})
    automation.positive(comment.get("id"), "claim comment id")
    return {"status": "planned", "request": selected, "observations": observations}


def _dispatch(api, request: dict) -> dict:
    automation.validate_request(request, api.repository)
    controller_context(api, request["base"], request["controller_run_id"])
    policy, policy_digest = automation.read_policy(api, request["base"])
    automation.require_enabled(api, policy)
    if policy_digest != request["policy_sha256"]:
        raise automation.AutomationError("policy changed before dispatch")
    pr = api.request("pulls/" + str(request["number"]))
    if not eligible_pr(pr, api.repository) or pr["head"]["sha"] != request["head"]:
        raise automation.AutomationMoved("PR changed before dispatch", 76)
    records = [record for record in ledger(api, request["number"])
               if record["request"]["key"] == request["key"]]
    if len(records) != 1 or records[0]["state"] != "claimed" or records[0]["request"] != request:
        raise automation.AutomationError("dispatch requires its own unique unconsumed claim")
    record = records[0]
    # Artifact upload must already have committed one exact controller request.
    automation.read_request_artifact(api, request)
    existing = find_writer(api, record)
    if existing is not None:
        save(api, record, "dispatched", "recovered existing exact-key writer", existing["id"])
        return {"status": "dispatched", "key": request["key"], "run_id": existing["id"]}
    # A manual writer may have started after planning. The existing writer
    # concurrency group serializes that race; do not discard this durable claim.
    inputs = {"pull_request": str(request["number"]), "expected_head": request["head"],
              "expected_base": request["base"], "transition_kind": request["classification"],
              "authorization_mode": "automation", "automation_key": request["key"],
              "automation_request": automation.canonical(request).decode().strip()}
    # Deliberately one POST, no HTTP retry. Older GitHub API versions return
    # 204; the durable claim plus exact-key run search handles that response.
    response = api.request("actions/workflows/" + Path(automation.WRITER_PATH).name + "/dispatches",
                           method="POST", body={"ref": "main", "inputs": inputs})
    run_id = None
    if response is not None:
        run_id = automation.positive(response.get("workflow_run_id"), "dispatched writer run")
    save(api, record, "dispatched", "dispatch accepted; awaiting exact-key run readback", run_id)
    return {"status": "dispatched", "key": request["key"], "run_id": run_id}


def dispatch(api, request: dict) -> dict:
    try:
        result = _dispatch(api, request)
    except automation.AutomationMoved:
        # Every AutomationMoved above precedes POST. This is positive proof
        # that no writer was requested, unlike a lost HTTP response or cancel.
        records = [record for record in ledger(api, request["number"])
                   if record["request"] == request and record["state"] == "claimed"]
        if len(records) == 1:
            save(api, records[0], "superseded", "inputs moved before dispatch; no POST performed")
        raise
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "dispatch"))
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--request", type=Path, required=True)
    args = parser.parse_args(argv)
    result = 1
    try:
        api = integration.GitHub(os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"])
        if args.command == "plan":
            report = plan(api, args.repo_root, os.environ["GITHUB_WORKFLOW_SHA"],
                          int(os.environ["GITHUB_RUN_ID"]))
            if report["status"] == "planned":
                args.request.parent.mkdir(parents=True, exist_ok=True)
                args.request.write_bytes(automation.canonical(report["request"]))
                with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
                    output.write("ready=true\n")
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
                summary.write("## Retirement controller\n\n```json\n" +
                              json.dumps(report, indent=2) + "\n```\n")
        else:
            report = dispatch(api, automation.decode(args.request.read_bytes()))
        print(json.dumps(report, indent=2))
        result = 0
    except (KeyError, ValueError, OSError, integration.IntegrationError) as error:
        print("retirement controller blocked: " + str(error), file=sys.stderr)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
