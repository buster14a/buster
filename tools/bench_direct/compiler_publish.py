#!/usr/bin/env python3
"""Validate a 9700X compiler receipt and publish its exact-head check (#2752).

Run from trusted `main` by the hosted `publish-compiler` job of
`.github/workflows/9700x-direct-bench.yml`, which alone holds `checks: write`.
It reads this run's evidence artifact through the API as data (a size-bounded
zip; nothing in it is executed), re-checks the receipt against the identities
authorization produced in the same attempt, re-derives validity from the lab's
own summary.json, and creates one completed check run named CHECK_NAME with
external ID `buster-9700x-compiler-bench-v1:<head>` on the group head.

Conclusions (the performance verdict never decides; report-only):
    success  a valid core measurement, whatever its direction
    neutral  the group was replaced or removed before measurement
    failure  authorization refused, evidence missing, mismatched or invalid,
             or an unimplemented regression policy was requested
A failure says the candidate was not benchmarked; it is never relabelled.

Map: decide (pure decision), Api (bounded GET, artifact download without
forwarding the token, one POST), main.
"""

from __future__ import annotations

import io
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from datetime import datetime

from compiler_receipt import (CHECK_NAME, PROFILE, RECEIPT_SCHEMA, SHA, check_marker, classify, regression_policy,
                              render)

API = "https://api.github.com"
ARTIFACT_PREFIX = "buster-9700x-compiler-"
ARTIFACT_LIMIT = 64 * 1024 * 1024
MEMBER_LIMIT = 8 * 1024 * 1024
TEXT_LIMIT = 60000
COMPARE_JOB = "Compare the merge group compiler"
IDENTITY_KEYS = ("repository", "queue_branch", "pull", "pull_head", "base", "base_tree", "head", "head_tree",
                 "trusted_revision", "request_run_id", "run_id", "run_attempt")


def artifact_name(head: str, attempt: str) -> str:
    return f"{ARTIFACT_PREFIX}{head}-{attempt}"


def decide(expected: dict, authorized: bool, compare_result: str, receipt: object, summary: object,
           policy_value: str) -> tuple[str, str, list[str]]:
    """(conclusion, title, reasons) for one attempt; never consults the verdict's direction."""
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
        if receipt.get("profile") != PROFILE:
            reasons.append("receipt profile is not the frozen queue profile")
        state = receipt.get("state")
        if not reasons and state == "superseded":
            conclusion, title = "neutral", "Superseded before measurement"
            reasons.extend(item for item in receipt.get("reasons", []) if isinstance(item, str))
        elif not reasons and state == "measured":
            reasons.extend(classify(summary, receipt.get("binaries")))
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


class Api:
    def __init__(self, repository: str, token: str):
        self.prefix = f"{API}/repos/{repository}"
        self.token = token

    def request(self, path: str, data: dict | None = None) -> object:
        request = urllib.request.Request(self.prefix + path, data=None if data is None else json.dumps(data).encode(),
                                         method="GET" if data is None else "POST", headers={
            "Authorization": "Bearer " + self.token,
            "Accept": "application/vnd.github+json",
            "Content-Type": "application/json",
            "X-GitHub-Api-Version": "2022-11-28",
        })
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    def download(self, url: str) -> bytes:
        """Follow GitHub's one redirect to storage without forwarding the token."""
        class Stop(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, *arguments, **keywords):
                return None
        opener = urllib.request.build_opener(Stop)
        request = urllib.request.Request(url, headers={"Authorization": "Bearer " + self.token,
                                                       "X-GitHub-Api-Version": "2022-11-28"})
        location = ""
        try:
            opener.open(request, timeout=30).close()
        except urllib.error.HTTPError as error:
            if error.code not in (301, 302, 303, 307, 308):
                raise
            location = error.headers.get("Location", "")
        if urllib.parse.urlsplit(location).scheme != "https":
            raise OSError("artifact download did not redirect to HTTPS storage")
        with urllib.request.urlopen(urllib.request.Request(location), timeout=60) as response:
            payload = response.read(ARTIFACT_LIMIT + 1)
        if len(payload) > ARTIFACT_LIMIT:
            raise OSError("artifact archive exceeds the size limit")
        return payload


def read_evidence(api: Api, run_id: str, name: str) -> tuple[object, object, str]:
    """(receipt, summary, problem) from this run's one evidence artifact."""
    listing = api.request(f"/actions/runs/{run_id}/artifacts?" + urllib.parse.urlencode({"name": name, "per_page": 10}))
    rows = [row for row in listing.get("artifacts", []) if isinstance(row, dict) and row.get("name") == name] \
        if isinstance(listing, dict) else []
    receipt = summary = None
    problem = ""
    if len(rows) != 1:
        problem = f"expected one evidence artifact {name}, found {len(rows)}"
    elif rows[0].get("expired") or type(rows[0].get("size_in_bytes")) is not int or \
            rows[0]["size_in_bytes"] > ARTIFACT_LIMIT or not isinstance(rows[0].get("archive_download_url"), str):
        problem = "evidence artifact is expired, oversized or malformed"
    else:
        with zipfile.ZipFile(io.BytesIO(api.download(rows[0]["archive_download_url"]))) as archive:
            members = {info.filename: info for info in archive.infolist()}
            values = []
            for member in ("receipt.json", "lab/summary.json"):
                info = members.get(member)
                value = None
                if info is not None and info.file_size <= MEMBER_LIMIT:
                    try:
                        value = json.loads(archive.read(info).decode("utf-8"))
                    except ValueError:
                        value = None
                values.append(value)
            receipt, summary = values
    return receipt, summary, problem


def queue_delay(api: Api, run_id: str, attempt: str) -> object:
    """Seconds the compare job waited for the 9700X runner, or None."""
    delay = None
    jobs = api.request(f"/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100")
    rows = [job for job in jobs.get("jobs", []) if isinstance(job, dict) and job.get("name") == COMPARE_JOB] \
        if isinstance(jobs, dict) else []
    if len(rows) == 1 and isinstance(rows[0].get("created_at"), str) and isinstance(rows[0].get("started_at"), str):
        parse = lambda text: datetime.fromisoformat(text.replace("Z", "+00:00"))  # noqa: E731
        delay = (parse(rows[0]["started_at"]) - parse(rows[0]["created_at"])).total_seconds()
    return delay


def main() -> int:
    environment = os.environ
    get = lambda key: environment.get(key, "")  # noqa: E731
    head, attempt, run_id = get("BQ_HEAD_COMMIT"), get("BQ_RUN_ATTEMPT"), get("BQ_RUN_ID")
    expected = {"repository": get("BQ_REPOSITORY"), "queue_branch": get("BQ_HEAD_BRANCH"), "pull": get("BQ_PULL"),
                "pull_head": get("BQ_PULL_HEAD"), "base": get("BQ_BASE_COMMIT"), "base_tree": get("BQ_BASE_TREE"),
                "head": head, "head_tree": get("BQ_HEAD_TREE"), "trusted_revision": get("BQ_TRUSTED_REVISION"),
                "request_run_id": get("BQ_REQUEST_RUN_ID"), "run_id": run_id, "run_attempt": attempt}
    code = 1
    if not (SHA.fullmatch(head) and run_id.isdigit() and attempt.isdigit() and get("GH_TOKEN")):
        print("BENCH_COMPILER_PUBLISH_FAIL invalid workflow inputs", file=sys.stderr)
    else:
        api = Api(expected["repository"], get("GH_TOKEN"))
        authorized = get("BQ_AUTHORIZE_RESULT") == "success" and get("BQ_AUTHORIZED_ATTEMPT") == attempt
        receipt = summary = None
        notes: list[str] = []
        if authorized:
            receipt, summary, problem = read_evidence(api, run_id, artifact_name(head, attempt))
            if problem:
                notes.append(problem)
        conclusion, title, reasons = decide(expected, authorized, get("BQ_COMPARE_RESULT"), receipt, summary,
                                            get("BQ_REGRESSION_POLICY"))
        shown = dict(receipt) if isinstance(receipt, dict) else {"identity": expected}
        shown["reasons"] = reasons
        if isinstance(shown.get("timings"), dict):
            shown["timings"] = dict(shown["timings"], queue_delay_seconds=queue_delay(api, run_id, attempt))
        report = render(shown, summary, conclusion, notes)
        body = {"name": CHECK_NAME, "head_sha": head, "external_id": check_marker(head),
                "details_url": get("BQ_DETAILS_URL"), "status": "completed", "conclusion": conclusion,
                "output": {"title": title[:200], "summary": report[:TEXT_LIMIT],
                           "text": ("```json\n" + json.dumps(shown, sort_keys=True, indent=2))[:TEXT_LIMIT - 4] + "\n```"}}
        created = api.request("/check-runs", body)
        with open(get("GITHUB_STEP_SUMMARY") or os.devnull, "a", encoding="utf-8") as stream:
            stream.write(report + "\n")
        print(f"BENCH_COMPILER_PUBLISHED {conclusion} check_run={created.get('id') if isinstance(created, dict) else None}")
        code = 0
    return code


if __name__ == "__main__":
    sys.exit(main())
