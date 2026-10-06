#!/usr/bin/env python3
"""Validate a 9700X compiler receipt and publish its exact-head check (#2752, #2769).

Run from trusted `main` by the hosted `publish-compiler` (main mode) and
`publish-pull` (pull mode) jobs of `.github/workflows/9700x-direct-bench.yml`,
the only jobs there that complete the check. It reads this run's evidence
artifact through the API as data (a size-bounded zip; nothing in it is
executed), re-checks the receipt against the identities authorization produced
in the same attempt, requires the observed host to be the approved Zen 5 host,
re-derives validity from the lab's own summary.json and the throughput
corpus's own summary.json and metadata.json (#2761), and, when the receipt
names the scaling profile (#424), each scaling bundle's own scaling.json and
scaling-metadata.json, and completes the
attempt's check run named check_name(mode) with external ID attempt_marker(...)
on the head. The checks are report-only; nothing gates merging on them.

Conclusions (the performance verdict never decides; report-only):
    success  a valid core measurement, whatever its direction
    neutral  a pull request head moved before measurement
    failure  authorization refused, evidence missing, mismatched or invalid,
             or an unimplemented regression policy was requested
A failure says the candidate was not benchmarked; it is never relabelled.

The check is the attempt's own check (#2803): the request bridge or the start
job usually created it queued and moved it to in_progress; this completes the
same check through compiler_github.complete_check, creates it only when none
exists, and leaves an already completed one unchanged. The report carries
explicit links to the check, the originating workflow attempt and the
evidence artifact.

Main mode also writes the commit report entry (#2804) to the `comment` job
output: validated data plus the rendered commit_report markdown, which the
separately permitted comment job upserts with compiler_comment.py.

Publication-only recovery (#2804): with BQ_RECOVER_RUN_ID and
BQ_RECOVER_ATTEMPT, run by `.github/workflows/9700x-compiler-report.yml`, it
re-derives a past main attempt's identity from GitHub's records
(recovered_identity, bind_request), re-reads that attempt's retained
evidence, and emits only the comment entry. It writes no check and runs no
measurement; expired or unverifiable evidence is reported and not published.

Map: decide (pure decision), read_evidence, queue_delay, links,
commit_report, recovered_identity, bind_request, report_check_url, main.
Api lives in compiler_github.
"""

from __future__ import annotations

import io
import json
import os
import re
import sys
import urllib.parse
import zipfile
from datetime import datetime

from authorize_compiler import verify as verify_main
from compiler_github import (ARTIFACT_LIMIT, BENCH_WORKFLOW, COMPARE_JOBS, SERVER, TEXT_LIMIT, Api, complete_check,
                             owned_checks, parse_chain, run_url)
from compiler_receipt import (DECIMAL, IDENTITY_KEYS, MODES, PROFILE, RECEIPT_SCHEMA, SHA, attempt_marker, check_marker,
                              check_name, classify, classify_scaling, classify_throughput, host_problem, number,
                              range_label, regression_policy, render, SCALING_PROFILE, scaling_digest,
                              THROUGHPUT_PROFILE, throughput_digest)

ARTIFACT_PREFIX = "buster-9700x-compiler-"
MEMBER_LIMIT = 8 * 1024 * 1024
AUTHORIZE_JOBS = {"main": "Authorize the main commit comparison"}
REPORT_MARKDOWN_LIMIT = 36000


def artifact_name(head: str, attempt: str) -> str:
    return f"{ARTIFACT_PREFIX}{head}-{attempt}"


def decide(expected: dict, authorized: bool, compare_result: str, receipt: object, summary: object,
           policy_value: str, throughput: object = None, require_throughput: bool = True) -> tuple[str, str, list[str]]:
    """(conclusion, title, reasons) for one attempt; never consults the verdict's direction.

    throughput is {"summary": ..., "metadata": ..., "scaling": {series: {"summary", "metadata"}}}
    from the evidence artifact. Scaling is checked whenever the receipt names
    its profile; only a pull request that asked for it has one.
    A receipt from before the corpus leg (#2761) has no throughput_profile;
    only publication-only recovery of such a past attempt passes
    require_throughput=False, and a receipt that names the profile is always
    checked against it.
    """
    reasons: list[str] = []
    conclusion, title = "failure", "Not benchmarked"
    policy, problem = regression_policy(policy_value)
    if not authorized:
        reasons.append("authorization refused or did not complete in this attempt; this candidate has a "
                       "benchmark coverage gap (see the authorize-compiler job)")
    elif not isinstance(receipt, dict):
        reasons.append("no readable receipt.json in this attempt's evidence artifact "
                       f"(compare job result: {compare_result or 'unknown'})")
    else:
        if receipt.get("schema") != RECEIPT_SCHEMA:
            reasons.append(f"receipt schema {receipt.get('schema')!r} is not {RECEIPT_SCHEMA}")
        identity = receipt.get("identity") if isinstance(receipt.get("identity"), dict) else {}
        for key in IDENTITY_KEYS:
            if identity.get(key) != expected.get(key):
                reasons.append(f"receipt {key} {identity.get(key)!r} does not match {expected.get(key)!r}")
        if receipt.get("mode") != expected.get("mode"):
            reasons.append(f"receipt mode {receipt.get('mode')!r} is not {expected.get('mode')!r}")
        # Receipts before range baselines (#2752) carry no coverage.
        coverage = {"first_parent": expected.get("first_parent"), "range": expected.get("range")}
        if "coverage" in receipt and receipt.get("coverage") != coverage:
            reasons.append(f"receipt coverage {receipt.get('coverage')!r} does not match {coverage!r}")
        if receipt.get("profile") != PROFILE:
            reasons.append("receipt profile is not the frozen comparison profile")
        if host_problem(receipt):
            reasons.append(host_problem(receipt))
        state = receipt.get("state")
        if not reasons and state == "superseded":
            conclusion, title = "neutral", "Superseded before measurement"
            reasons.extend(item for item in receipt.get("reasons", []) if isinstance(item, str))
        elif not reasons and state == "measured":
            reasons.extend(classify(summary, receipt.get("binaries")))
            if require_throughput or "throughput_profile" in receipt:
                corpus = throughput if isinstance(throughput, dict) else {}
                if receipt.get("throughput_profile") != THROUGHPUT_PROFILE:
                    reasons.append("receipt throughput profile is not the frozen corpus profile")
                reasons.extend(classify_throughput(corpus.get("summary"), corpus.get("metadata"),
                                                   receipt.get("binaries")))
            if "scaling_profile" in receipt:
                corpus = throughput if isinstance(throughput, dict) else {}
                if receipt.get("scaling_profile") != SCALING_PROFILE:
                    reasons.append("receipt scaling profile is not the frozen scaling profile")
                reasons.extend(classify_scaling(corpus.get("scaling"), receipt.get("binaries")))
            if compare_result != "success":
                reasons.append(f"compare job result is {compare_result!r}, not success")
            if problem:
                reasons.append(problem)
            if not reasons:
                verdict = summary.get("verdict", {})
                conclusion = "success"
                title = f"Measured ({policy}): wall B/A {verdict.get('ratio'):.4f}, {verdict.get('outcome')}"
        elif not reasons:
            reasons.append(f"host measurement state is {state!r}")
            reasons.extend(item for item in receipt.get("reasons", []) if isinstance(item, str))
    return conclusion, title, reasons


def read_evidence(api: Api, run_id: str, name: str) -> tuple[object, object, str, dict, dict]:
    """(receipt, summary, problem, artifact row, throughput) from this run's one evidence artifact."""
    listing = api.request(f"/actions/runs/{run_id}/artifacts?" + urllib.parse.urlencode({"name": name, "per_page": 10}))
    rows = [row for row in listing.get("artifacts", []) if isinstance(row, dict) and row.get("name") == name] \
        if isinstance(listing, dict) else []
    receipt = summary = None
    problem = ""
    artifact: dict = {}
    throughput: dict = {}
    if len(rows) != 1:
        problem = f"expected one evidence artifact {name}, found {len(rows)}"
    elif rows[0].get("expired") or type(rows[0].get("size_in_bytes")) is not int or \
            rows[0]["size_in_bytes"] > ARTIFACT_LIMIT or not isinstance(rows[0].get("archive_download_url"), str):
        problem = "evidence artifact is expired, oversized or malformed"
    else:
        artifact = rows[0]
        with zipfile.ZipFile(io.BytesIO(api.download(rows[0]["archive_download_url"]))) as archive:
            members = {info.filename: info for info in archive.infolist()}
            values = []
            scaling = [f"scaling/{name}/{leaf}" for name in SCALING_PROFILE["series"]
                       for leaf in ("scaling.json", "scaling-metadata.json")]
            for member in ("receipt.json", "lab/summary.json", "throughput/summary.json", "throughput/metadata.json",
                           *scaling):
                info = members.get(member)
                value = None
                if info is not None and info.file_size <= MEMBER_LIMIT:
                    try:
                        value = json.loads(archive.read(info).decode("utf-8"))
                    except ValueError:
                        value = None
                values.append(value)
            receipt, summary = values[:2]
            throughput = {"summary": values[2], "metadata": values[3],
                          "scaling": {name: {"summary": values[4 + 2 * index], "metadata": values[5 + 2 * index]}
                                      for index, name in enumerate(SCALING_PROFILE["series"])}}
    return receipt, summary, problem, artifact, throughput


def queue_delay(api: Api, run_id: str, attempt: str, mode: str) -> object:
    """Seconds the compare job waited for the 9700X runner, or None."""
    delay = None
    jobs = api.request(f"/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100")
    rows = [job for job in jobs.get("jobs", []) if isinstance(job, dict) and job.get("name") == COMPARE_JOBS[mode]] \
        if isinstance(jobs, dict) else []
    if len(rows) == 1 and isinstance(rows[0].get("created_at"), str) and isinstance(rows[0].get("started_at"), str):
        parse = lambda text: datetime.fromisoformat(text.replace("Z", "+00:00"))  # noqa: E731
        delay = (parse(rows[0]["started_at"]) - parse(rows[0]["created_at"])).total_seconds()
    return delay


def artifact_link(repository: str, run_id: str, artifact: dict) -> str:
    return f"{SERVER}/{repository}/actions/runs/{run_id}/artifacts/{artifact['id']}" \
        if type(artifact.get("id")) is int else ""


def links(check_url: str, workflow_url: str, attempt: str, evidence_url: str, expires: str) -> str:
    """Explicit links: a check's own details URL is not relied on (#2804)."""
    parts = [f"[Full check]({check_url})" if check_url else "Full check: NA",
             f"[Workflow run, attempt {attempt}]({workflow_url})",
             f"[Evidence artifact]({evidence_url}) (retained until {expires or 'NA'}; it expires even though "
             "this report stays)" if evidence_url else "Evidence artifact: unavailable"]
    return " · ".join(parts)


def commit_report(shown: dict, summary: object, conclusion: str, title: str, notes: list[str],
                  link_line: str) -> str:
    """The commit-page report (#2804): verdict, scope, metrics with units, links; tables collapsed."""
    identity = shown.get("identity", {}) if isinstance(shown.get("identity"), dict) else {}
    summary = summary if isinstance(summary, dict) else {}
    verdict = summary.get("verdict") if isinstance(summary.get("verdict"), dict) and conclusion == "success" else {}
    metrics = summary.get("metrics") if isinstance(summary.get("metrics"), dict) else {}
    wall = metrics.get("wall") if isinstance(metrics.get("wall"), dict) and verdict else {}
    plan = summary.get("plan") if isinstance(summary.get("plan"), dict) and verdict else {}
    host = shown.get("host") if isinstance(shown.get("host"), dict) else {}
    profile = shown.get("profile") if isinstance(shown.get("profile"), dict) else {}
    timings = shown.get("timings") if isinstance(shown.get("timings"), dict) else {}
    unit = wall.get("unit") if isinstance(wall.get("unit"), str) else "s"
    coverage = shown.get("coverage") if isinstance(shown.get("coverage"), dict) else {}
    baseline = range_label(coverage.get("range"), coverage.get("first_parent")) or "first parent or range baseline"
    lines = [
        f"### {check_name(shown.get('mode', 'main'))}: {conclusion}, {title}", "",
        (verdict.get("text") if isinstance(verdict.get("text"), str) else
         "No valid measurement: this commit was not benchmarked by this attempt.")
        + " Performance policy: report-only. This never blocks merging, and a successful measurement is not by "
        "itself a speedup or an equivalence.", "",
        "| | |", "| --- | --- |",
        f"| Candidate | `{identity.get('head', 'NA')}` (this commit) |",
        f"| Baseline | `{identity.get('base', 'NA')}` ({baseline}) |",
        "| Wall time B/A | %s, 95%% CI [%s, %s], %s |" % (
            number(verdict.get("ratio"), "%.4f"), number(verdict.get("ci_low"), "%.4f"),
            number(verdict.get("ci_high"), "%.4f"), verdict.get("outcome", "NA")),
        f"| Wall median A / B | {number(wall.get('a_median'), '%.6g')} {unit} / "
        f"{number(wall.get('b_median'), '%.6g')} {unit} |",
        f"| Complete pairs | {number(plan.get('complete_pairs'), '%d')} |",
        f"| Host (observed) | `{host.get('cpu_model', 'NA')}` |",
        f"| Scope | `{profile.get('name', 'NA')}`: {profile.get('workload', 'NA')}; corpus "
        f"{profile.get('corpus', 'NA')} |",
        f"| Captured | {timings.get('started_at', 'NA')} to {timings.get('finished_at', 'NA')} by run "
        f"{identity.get('run_id', 'NA')} attempt {identity.get('run_attempt', 'NA')}; trusted harness "
        f"`{identity.get('trusted_revision', 'NA')}` |",
        "", link_line, "",
    ]
    reasons = shown.get("reasons") if isinstance(shown.get("reasons"), list) else []
    lines += [f"- {item}" for item in (*notes, *reasons)][:20]
    detail = render(shown, summary, conclusion, notes)
    lines += ["", "<details><summary>All metrics, identities and host timings</summary>", "", detail, "", "</details>"]
    text = "\n".join(lines)
    if len(text) > REPORT_MARKDOWN_LIMIT:
        text = "\n".join(lines[:lines.index("<details><summary>All metrics, identities and host timings</summary>")]) \
            + "\nThe full tables exceed the comment bound; see the full check."
    return text


def recovered_identity(api: Api, repository: str, run_id: str, attempt: str) -> tuple[dict, str, str, list[str]]:
    """(expected, authorize result, compare result, problems) of a past main attempt from GitHub's records.

    Publication-only recovery (#2804): the attempt's own jobs, not this run,
    supply the authorization; the identity is re-derived the way
    authorize_compiler derives it and the receipt must still equal it.
    """
    problems: list[str] = []
    run = api.request(f"/actions/runs/{run_id}")
    run = run if isinstance(run, dict) else {}
    if not (run.get("path") == BENCH_WORKFLOW and run.get("event") == "workflow_run" and
            str(run.get("id")) == run_id and type(run.get("run_attempt")) is int and run["run_attempt"] >= int(attempt)
            and isinstance(run.get("head_sha"), str) and SHA.fullmatch(run["head_sha"])):
        problems.append("the run is not an attempt of the 9700X direct workload benchmark workflow")
    jobs = api.request(f"/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100")
    rows = [job for job in (jobs.get("jobs", []) if isinstance(jobs, dict) else []) if isinstance(job, dict)]
    result = {}
    for role, name in (("authorize", AUTHORIZE_JOBS["main"]), ("compare", COMPARE_JOBS["main"])):
        matches = [job for job in rows if job.get("name") == name and str(job.get("run_attempt", attempt)) == attempt]
        result[role] = matches[0].get("conclusion") or "" if len(matches) == 1 else ""
    listing = api.pages(f"/actions/runs/{run_id}/artifacts", "artifacts")
    pattern = re.compile(re.escape(ARTIFACT_PREFIX) + r"([0-9a-f]{40})-" + attempt + r"\Z")
    heads = [match.group(1) for row in listing if isinstance(row, dict) and isinstance(row.get("name"), str)
             for match in [pattern.fullmatch(row["name"])] if match]
    head = heads[0] if len(heads) == 1 else ""
    expected = {"mode": "main", "repository": repository, "ref": "refs/heads/main", "head": head,
                "trusted_revision": run.get("head_sha", ""), "run_id": run_id, "run_attempt": attempt}
    if not head:
        problems.append(f"expected one evidence artifact of attempt {attempt}, found {len(heads)}")
    return expected, result["authorize"], result["compare"], problems


def bind_request(api: Api, expected: dict, receipt: object) -> list[str]:
    """Fill expected from the receipt's request run, re-verified exactly as authorize-compiler does."""
    identity = receipt.get("identity") if isinstance(receipt, dict) and isinstance(receipt.get("identity"), dict) else {}
    request_run, head, repository = identity.get("request_run_id", ""), expected["head"], expected["repository"]
    # The baseline the attempt measured: it must still be on head's first-parent chain.
    base = identity.get("base", "")
    problems = []
    if not (isinstance(request_run, str) and DECIMAL.fullmatch(request_run) and SHA.fullmatch(head)):
        problems.append("the receipt names no request run")
    else:
        commit = api.request(f"/commits/{head}")
        failures, result = verify_main(
            repository, int(request_run), head, api.request(f"/actions/runs/{request_run}"), commit,
            api.request(f"/commits/{base}") if isinstance(base, str) and SHA.fullmatch(base) else None,
            api.request(f"/compare/{head}...main"), api.request(f"/commits/{head}/pulls?per_page=10"),
            parse_chain(api.request(f"/commits?sha={head}&per_page=100"), head))
        problems.extend(f"request re-verification failed: {item}" for item in failures)
        expected.update(result, request_run_id=request_run)
    return problems


def report_check_url(api: Api, head: str, marker: str) -> str:
    """The attempt's completed check, or the legacy single check of a pre-#2803 attempt."""
    rows = [row for row in owned_checks(api, head, "main", marker) if row.get("status") == "completed"]
    if not rows:
        rows = [row for row in owned_checks(api, head, "main", check_marker(head, "main"))
                if row.get("status") == "completed"]
    return rows[-1].get("html_url", "") if len(rows) >= 1 and isinstance(rows[-1].get("html_url"), str) else ""


def write_output(path: str, values: dict) -> None:
    with open(path or os.devnull, "a", encoding="utf-8") as stream:
        for name, value in values.items():
            stream.write(f"{name}={value}\n")


def main() -> int:
    environment = os.environ
    get = lambda key: environment.get(key, "")  # noqa: E731
    recover = bool(get("BQ_RECOVER_RUN_ID"))
    run_id = get("BQ_RECOVER_RUN_ID") if recover else get("BQ_RUN_ID")
    attempt = get("BQ_RECOVER_ATTEMPT") if recover else get("BQ_RUN_ATTEMPT")
    mode = "main" if recover else get("BQ_MODE")
    repository = get("BQ_REPOSITORY")
    head = get("BQ_HEAD_COMMIT")
    code = 1
    if not (mode in MODES and (recover or SHA.fullmatch(head)) and DECIMAL.fullmatch(run_id) and
            DECIMAL.fullmatch(attempt) and get("GH_TOKEN") and
            (recover or DECIMAL.fullmatch(get("BQ_REQUEST_RUN_ID")) and DECIMAL.fullmatch(get("BQ_REQUEST_ATTEMPT")))):
        print("BENCH_COMPILER_PUBLISH_FAIL invalid workflow inputs", file=sys.stderr)
    else:
        api = Api(repository, get("GH_TOKEN"))
        notes: list[str] = []
        if recover:
            expected, authorize_result, compare_result, problems = recovered_identity(api, repository, run_id, attempt)
            head = expected["head"]
            authorized = authorize_result == "success" and not problems
        else:
            expected = {"mode": mode, "repository": repository, "ref": get("BQ_REF"), "pull": get("BQ_PULL"),
                        "pull_head": get("BQ_PULL_HEAD"), "base": get("BQ_BASE_COMMIT"),
                        "base_tree": get("BQ_BASE_TREE"), "head": head, "head_tree": get("BQ_HEAD_TREE"),
                        "trusted_revision": get("BQ_TRUSTED_REVISION"), "request_run_id": get("BQ_REQUEST_RUN_ID"),
                        "run_id": run_id, "run_attempt": attempt}
            if mode == "main":
                expected.update(first_parent=get("BQ_FIRST_PARENT"), range=get("BQ_RANGE"))
            compare_result, problems = get("BQ_COMPARE_RESULT"), []
            authorized = get("BQ_AUTHORIZE_RESULT") == "success" and get("BQ_AUTHORIZED_ATTEMPT") == attempt
        notes.extend(problems)
        receipt = summary = None
        artifact: dict = {}
        throughput: dict = {}
        if authorized:
            receipt, summary, problem, artifact, throughput = read_evidence(api, run_id, artifact_name(head, attempt))
            if problem:
                notes.append(problem)
            if recover:
                problems = bind_request(api, expected, receipt)
                notes.extend(problems)
                authorized = not problems
        conclusion, title, reasons = decide(expected, authorized, compare_result, receipt, summary,
                                            get("BQ_REGRESSION_POLICY"), throughput, not recover)
        shown = dict(receipt) if isinstance(receipt, dict) else {"mode": mode, "identity": expected}
        # The authorized range, never the host's own account of it.
        shown.pop("coverage", None)
        if mode == "main" and expected.get("range"):
            shown["coverage"] = {"first_parent": expected.get("first_parent"), "range": expected.get("range")}
        shown["reasons"] = reasons
        # The corpus as its own retained summary says, never the host's digest.
        if isinstance(throughput.get("summary"), dict):
            shown["throughput"] = throughput_digest(throughput["summary"])
        # Scaling likewise comes from the retained bundles, never the host's digest.
        if isinstance(receipt, dict) and "scaling_profile" in receipt:
            shown["scaling"] = scaling_digest(throughput.get("scaling"))
        if isinstance(shown.get("timings"), dict):
            shown["timings"] = dict(shown["timings"], queue_delay_seconds=queue_delay(api, run_id, attempt, mode))
        workflow_url = run_url(repository, run_id, attempt)
        evidence_url = artifact_link(repository, run_id, artifact)
        expires = artifact.get("expires_at", "") if isinstance(artifact.get("expires_at"), str) else ""
        check_url = ""
        if recover:
            check_url = report_check_url(api, head, "") if SHA.fullmatch(head) else ""
        else:
            marker = attempt_marker(head, mode, expected["request_run_id"], get("BQ_REQUEST_ATTEMPT"), attempt)
            report = render(shown, summary, conclusion, notes) + "\n\n" + links("", workflow_url, attempt,
                                                                                 evidence_url, expires)
            body = {"details_url": get("BQ_DETAILS_URL"), "status": "completed", "conclusion": conclusion,
                    "output": {"title": title[:200], "summary": report[:TEXT_LIMIT],
                               "text": ("```json\n" + json.dumps(shown, sort_keys=True, indent=2))[:TEXT_LIMIT - 4]
                               + "\n```"}}
            rows, written = complete_check(api, head, mode, marker, body)
            check_url = rows[-1].get("html_url", "") if rows and isinstance(rows[-1].get("html_url"), str) else ""
            print(f"BENCH_COMPILER_PUBLISHED {conclusion} check_runs={[row.get('id') for row in rows]}"
                  + ("" if written else " (already completed by another writer; left unchanged)"))
        link_line = links(check_url, workflow_url, attempt, evidence_url, expires)
        with open(get("GITHUB_STEP_SUMMARY") or os.devnull, "a", encoding="utf-8") as stream:
            stream.write(render(shown, summary, conclusion, notes) + "\n\n" + link_line + "\n")
        identity = shown.get("identity") if isinstance(shown.get("identity"), dict) else expected
        timings = shown.get("timings") if isinstance(shown.get("timings"), dict) else {}
        # Recovery republishes only what the retained evidence still proves;
        # missing or unverifiable evidence is reported, never published.
        recoverable = not recover or (authorized and isinstance(receipt, dict))
        if not recoverable:
            print("BENCH_COMPILER_RECOVERY_REFUSED " + "; ".join(notes or reasons), file=sys.stderr)
        if mode == "main" and SHA.fullmatch(head) and recoverable:
            entry = {"mode": mode, "head": head, "base": expected.get("base") or identity.get("base", ""),
                     "run_id": run_id, "run_attempt": attempt,
                     "request_run_id": expected.get("request_run_id") or identity.get("request_run_id", ""),
                     "trusted_revision": expected.get("trusted_revision", ""), "conclusion": conclusion,
                     "title": title[:200], "captured_at": str(timings.get("finished_at") or timings.get("started_at")
                                                              or "NA")[:40],
                     "check_url": check_url, "run_url": workflow_url, "artifact_url": evidence_url,
                     "artifact_expires_at": expires or "NA",
                     "markdown": commit_report(shown, summary, conclusion, title, notes, link_line)}
            write_output(get("GITHUB_OUTPUT"), {"comment": json.dumps(entry, sort_keys=True, separators=(",", ":")),
                                                "head": head})
        code = 0 if recoverable else 1
    return code


if __name__ == "__main__":
    sys.exit(main())
