#!/usr/bin/env python3
"""The 9700X compiler comparison's check lifecycle on the measured commit (#2803).

Ownership: `tools/bench_direct`, trusted `main` only, run by hosted jobs only;
the 9700X never receives a token. One check run per measurement attempt,
named check_name(mode) with external ID attempt_marker(...), moves
queued -> completed and never backwards (legacy in_progress is also owned):

    announce  `9700x-compiler-request.yml` (push to main, `checks: write`)
              creates the queued check before the bench run exists, so the
              wait for the main comparison's concurrency group is visible.
    start     `start-compiler` / `start-pull` of `9700x-direct-bench.yml`
              (after this attempt's authorization) reconciles orphans, adopts
              or creates the attempt's check in one bounded pass. Native
              Actions job state is the authoritative live execution indicator;
              the custom check waits for validated terminal evidence.
    publish   `compiler_publish.py` completes the same check (complete_check).

Ownership of a check run is the GitHub Actions app ID, the exact name, the
exact head and the exact attempt marker together; an external ID alone is not
trusted. A completed check is never rewritten, so a late or repeated writer of
an older attempt cannot replace a result.

Orphans (reconcile_main, reconcile_pull): adopted or announced main checks
carry a stable native protocol annotation and defer to terminal recovery, which reads
the exact terminal provenance. Queued custom checks do not identify physical
job state. Checks without the native protocol annotation retain the bounded legacy
backstop with neutral conclusion and explicit execution uncertainty. A pull request head's open checks are
superseded by the next requested head of the same pull request.

Range baseline (#2752): a main commit is compared with the nearest
first-parent ancestor, at most RECONCILE_DEPTH back, whose own main check
holds a valid measurement (measured); normally that is its first parent. So
the change of every unmeasured commit in between is inside a measured range
(baseline_label), and its "Not measured" check names the comparison that
covers it. A forged check can only move the baseline to another main
ancestor, which is trusted code; authorize_compiler verifies the ancestry.

Map: Api (bounded GET with retries, POST/PATCH/DELETE, pages, download),
owns, owned_checks, measured, write_check, ensure_check, advance,
complete_check, queued_output, parse_chain, first_parent_chain,
baseline_label, reconcile_main, reconcile_pull, announce,
start, main.
"""

from __future__ import annotations

import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from compiler_receipt import DECIMAL, MODES, SHA, attempt_marker, check_marker, check_name, range_label

API = "https://api.github.com"
SERVER = "https://github.com"
ARTIFACT_LIMIT = 64 * 1024 * 1024
GITHUB_ACTIONS_APP_ID = 15368
PAGES = 10
GET_TRIES = 3
TEXT_LIMIT = 60000
STATUS_ORDER = {"queued": 0, "in_progress": 1, "completed": 2}
COMPARE_JOBS = {"main": "Compare the main commit compiler", "pull": "Compare the pull request compiler"}
BENCH_WORKFLOW = ".github/workflows/9700x-direct-bench.yml"
NATIVE_LIFECYCLE = "Lifecycle protocol: terminal-native-v1."
# How many earlier first-parent main commits, or earlier heads of the same
# pull request, a start job reconciles, and how far back the authorizer looks
# for a measured range baseline. Bounded reads; older orphans stay.
RECONCILE_DEPTH = 15


class Api:
    def __init__(self, repository: str, token: str):
        self.repository = repository
        self.prefix = f"{API}/repos/{repository}"
        self.token = token
        self.requests = 0

    def request(self, path: str, data: dict | None = None, method: str = "") -> object:
        """One API call. Only GET retries; writes are made idempotent by their callers' lookups."""
        method = method or ("GET" if data is None else "POST")
        result = None
        for attempt in range(1, (GET_TRIES if method == "GET" else 1) + 1):
            request = urllib.request.Request(
                self.prefix + path, data=None if data is None else json.dumps(data).encode(), method=method, headers={
                    "Authorization": "Bearer " + self.token,
                    "Accept": "application/vnd.github+json",
                    "Content-Type": "application/json",
                    "X-GitHub-Api-Version": "2022-11-28",
                })
            try:
                self.requests += 1
                with urllib.request.urlopen(request, timeout=30) as response:
                    payload = response.read()
                result = json.loads(payload) if payload else None
                break
            except (urllib.error.URLError, TimeoutError) as error:
                retry = method == "GET" and attempt < GET_TRIES and \
                    (not isinstance(error, urllib.error.HTTPError) or error.code >= 500)
                if not retry:
                    raise
                time.sleep(2 * attempt)
        return result

    def pages(self, path: str, field: str = "") -> list:
        """Every row of a paged listing, at most PAGES pages; a longer listing raises."""
        rows: list = []
        complete = False
        for page in range(1, PAGES + 1):
            response = self.request(path + ("&" if "?" in path else "?") + f"per_page=100&page={page}")
            batch = response.get(field) if field and isinstance(response, dict) else response
            if not isinstance(batch, list):
                raise OSError(f"malformed listing from {path}")
            rows.extend(batch)
            if len(batch) < 100:
                complete = True
                break
        if not complete:
            raise OSError(f"listing {path} exceeds {PAGES} pages")
        return rows

    def download(self, url: str, max_bytes: int = ARTIFACT_LIMIT) -> bytes:
        """Follow one storage redirect; the default preserves ordinary artifact bounds."""
        if type(max_bytes) is not int or not 0 < max_bytes <= 2 << 30:
            raise ValueError("artifact download bound is invalid")
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
            payload = response.read(max_bytes + 1)
        if len(payload) > max_bytes:
            raise OSError("artifact archive exceeds the size limit")
        return payload


def owns(row: object, head: str, mode: str, marker: str) -> bool:
    """Whether a check-run row is this publisher's check for exactly this attempt."""
    app = row.get("app") if isinstance(row, dict) else None
    return isinstance(row, dict) and type(row.get("id")) is int and row.get("name") == check_name(mode) and \
        row.get("head_sha") == head and isinstance(app, dict) and app.get("id") == GITHUB_ACTIONS_APP_ID and \
        (row.get("external_id") == marker if marker else
         isinstance(row.get("external_id"), str) and row["external_id"].startswith(check_marker(head, mode) + ":")) \
        and row.get("status") in STATUS_ORDER


def measured(row: object, sha: str) -> bool:
    """Whether a check-run row is a completed, valid main measurement of sha (any attempt, or pre-#2803)."""
    return (owns(row, sha, "main", "") or owns(row, sha, "main", check_marker(sha, "main"))) and \
        row.get("status") == "completed" and row.get("conclusion") == "success"


def owned_checks(api: Api, head: str, mode: str, marker: str) -> list[dict]:
    """This publisher's checks on head: one attempt's, or (marker '') every attempt's; oldest first."""
    query = urllib.parse.urlencode({"check_name": check_name(mode), "filter": "all", "app_id": GITHUB_ACTIONS_APP_ID})
    rows = api.pages(f"/commits/{head}/check-runs?{query}", "check_runs")
    return sorted((row for row in rows if owns(row, head, mode, marker)), key=lambda row: row["id"])


def write_check(api: Api, row: dict | None, fields: dict) -> dict:
    """POST a new check, or PATCH one owned row forward; never moves a status backwards."""
    result = row
    if row is None:
        result = api.request("/check-runs", fields)
    elif row["status"] != "completed" and STATUS_ORDER[fields["status"]] >= STATUS_ORDER[row["status"]]:
        update = {key: value for key, value in fields.items() if key not in ("name", "head_sha", "external_id")}
        result = api.request(f"/check-runs/{row['id']}", update, "PATCH")
    return result if isinstance(result, dict) else row or {}


def ensure_check(api: Api, head: str, mode: str, marker: str, fields: dict) -> list[dict]:
    """The attempt's owned checks, creating one if none exists.

    A lost POST response is resolved by looking up again before one more
    POST, so a transport retry does not create a second check.
    """
    rows = owned_checks(api, head, mode, marker)
    tries = 0
    while not rows and tries < 2:
        tries += 1
        try:
            created = write_check(api, None, dict(fields, name=check_name(mode), head_sha=head, external_id=marker))
            rows = [created] if owns(created, head, mode, marker) else owned_checks(api, head, mode, marker)
        except (urllib.error.URLError, TimeoutError, ValueError) as error:
            print(f"BENCH_COMPILER_CHECK_RETRY create: {error}", file=sys.stderr)
            rows = owned_checks(api, head, mode, marker)
    if not rows:
        raise OSError(f"could not create the {check_name(mode)} check on {head}")
    return rows


def advance(api: Api, rows: list[dict], fields: dict) -> list[dict]:
    """Move every open owned row forward to fields; completed rows stay as they are."""
    return [write_check(api, row, fields) for row in rows]


def complete_check(api: Api, head: str, mode: str, marker: str, fields: dict) -> tuple[list[dict], bool]:
    """(rows, written): complete the attempt's check, creating it only if none exists.

    An attempt whose check another writer already completed is left as it is.
    """
    rows = owned_checks(api, head, mode, marker)
    written = False
    if not rows:
        rows = ensure_check(api, head, mode, marker, fields)
        written = True
    elif any(row["status"] != "completed" for row in rows):
        rows = advance(api, rows, fields)
        written = True
    return rows, written


def run_url(repository: str, run_id: str, attempt: str = "") -> str:
    return f"{SERVER}/{repository}/actions/runs/{run_id}" + (f"/attempts/{attempt}" if attempt else "")


def queued_output(mode: str, head: str, lines: list[str]) -> dict:
    return {"title": "Queued: waiting for the 9700X comparison",
            "summary": "\n".join([f"**{check_name(mode)}: queued** (report-only; it never blocks merging)", "",
                                  NATIVE_LIFECYCLE, f"Candidate `{head}`.", *lines])[:TEXT_LIMIT]}


def parse_chain(rows: object, head: str) -> list[str]:
    """Up to RECONCILE_DEPTH first-parent ancestors of head, nearest first, from one commit listing."""
    parents = {}
    for row in rows if isinstance(rows, list) else []:
        listed = row.get("parents") if isinstance(row, dict) else None
        if isinstance(row.get("sha"), str) and isinstance(listed, list) and listed and isinstance(listed[0], dict):
            parents[row["sha"]] = listed[0].get("sha")
    chain: list[str] = []
    current = parents.get(head)
    while isinstance(current, str) and SHA.fullmatch(current) and current not in chain and \
            len(chain) < RECONCILE_DEPTH:
        chain.append(current)
        current = parents.get(current)
    return chain


def first_parent_chain(api: Api, head: str) -> list[str]:
    return parse_chain(api.request(f"/commits?sha={head}&per_page=100"), head)


def baseline_label(chain: list[str], base: str) -> str:
    """How base relates to the head whose first-parent chain is chain."""
    return range_label(str(chain.index(base) + 1) if base in chain else "", chain[0] if chain else "")


def close_orphans(api: Api, commits: list[str], mode: str, fields_for) -> list[int]:
    """Complete every open owned check on commits with fields_for(row); returns the closed IDs."""
    closed = []
    for sha in commits:
        for row in owned_checks(api, sha, mode, ""):
            if row["status"] != "completed":
                fields = fields_for(row)
                if fields is not None:
                    write_check(api, row, fields)
                    closed.append(row["id"])
    return closed


def reconcile_main(api: Api, head: str, base: str, reconciler: str, now: str, chain: list[str] | None = None) \
        -> list[int]:
    """Reconcile earlier legacy main checks; exact bound attempts belong to native terminal recovery.

    A closed commit strictly between base and head is named as covered by
    this attempt's range comparison; it still has no measurement of its own.
    """
    chain = first_parent_chain(api, head) if chain is None else chain
    covered = chain[:chain.index(base)] if base in chain else []

    def fields(row: dict) -> dict | None:
        # The custom check stays queued during physical execution. Tagged rows
        # must be classified by the one native terminal provenance authority,
        # even if a newer start wins the shared writer lock before recovery.
        output = row.get("output")
        summary = output.get("summary", "") if isinstance(output, dict) else ""
        if isinstance(summary, str) and NATIVE_LIFECYCLE in summary.splitlines():
            print(f"BENCH_COMPILER_ORPHAN_DEFERRED check_run={row['id']}: native terminal recovery",
                  file=sys.stderr)
            return None
        cause = ("a legacy open check was displaced by this newer request; its exact execution metadata "
                 "is unavailable, so this cleanup records no measurement")
        cover = (f" Its change is inside the range comparison of `{head}` against `{base}`, which is now requested; "
                 "any validated result covers the whole range and does not isolate this commit.") \
            if row["head_sha"] in covered else ""
        prior = row.get("output", {}).get("summary", "") if isinstance(row.get("output"), dict) else ""
        baseline = "\n".join(line for line in prior.splitlines() if line.startswith("Baseline"))
        return {"status": "completed", "conclusion": "neutral", "completed_at": now,
                "output": {"title": "Not measured", "summary": (
                    f"**{check_name('main')}: not measured.** This commit has no validated 9700X compiler measurement: "
                    f"{cause}. This is not a performance result.{cover} Closed while preparing the comparison of "
                    f"`{head}`: {reconciler}\n{baseline}")[:TEXT_LIMIT]}}
    return close_orphans(api, chain, "main", fields)


def reconcile_pull(api: Api, pull: str, head: str, reconciler: str, now: str) -> list[int]:
    """Close earlier heads' open checks of the same pull request as superseded."""
    rows = api.request(f"/pulls/{pull}/commits?per_page=100")
    commits = [row.get("sha") for row in rows if isinstance(row, dict)] if isinstance(rows, list) else []
    commits = [sha for sha in commits if isinstance(sha, str) and SHA.fullmatch(sha) and sha != head]

    def fields(row: dict) -> dict:
        return {"status": "completed", "conclusion": "neutral", "completed_at": now,
                "output": {"title": "Superseded: a newer pull request head was requested", "summary": (
                    f"**{check_name('pull')}: superseded.** A comparison of a newer head `{head}` of pull "
                    f"request #{pull} was requested; this earlier head has no validated publication. It is "
                    f"not a measurement. {reconciler}")[:TEXT_LIMIT]}}
    return close_orphans(api, commits[-RECONCILE_DEPTH:], "pull", fields)


def stamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def announce(api: Api, environment: dict) -> list[dict]:
    """Request bridge: the queued check of attempt 1 of the bench run this request will start."""
    head, request_run, request_attempt = (environment.get(key, "") for key in (
        "BQ_HEAD_COMMIT", "BQ_REQUEST_RUN_ID", "BQ_REQUEST_ATTEMPT"))
    marker = attempt_marker(head, "main", request_run, request_attempt, "1")
    bench = f"{SERVER}/{api.repository}/actions/workflows/9700x-direct-bench.yml?query=event%3Aworkflow_run"
    lines = [
        "Baseline: its first parent, the main commit it landed on, or, when that commit has no valid "
        "measurement, the nearest earlier main commit with one (a range comparison). Mode: main, profile "
        "`compiler-compare-v1`.",
        f"Request run {request_run} attempt {request_attempt}: {run_url(api.repository, request_run, request_attempt)}",
        "",
        "Waiting for the main comparison's concurrency group: one main comparison runs at a time and GitHub "
        "keeps only the newest pending main commit, so this commit may be displaced and is then marked "
        f"**Not measured**. The comparison run will appear under {bench}.",
    ]
    return ensure_check(api, head, "main", marker, {"status": "queued", "details_url": bench,
                                                    "output": queued_output("main", head, lines)})


def start(api: Api, environment: dict) -> list[dict]:
    """One pass: reconcile, adopt or create this attempt's check; never await host scheduling."""
    get = lambda key: environment.get(key, "")  # noqa: E731
    mode, head, run_id, attempt = get("BQ_MODE"), get("BQ_HEAD_COMMIT"), get("BQ_RUN_ID"), get("BQ_RUN_ATTEMPT")
    marker = attempt_marker(head, mode, get("BQ_REQUEST_RUN_ID"), get("BQ_REQUEST_ATTEMPT"), attempt)
    here = run_url(api.repository, run_id, attempt)
    relation = "first parent or nearest measured first-parent ancestor" if mode == "main" else \
        f"merge base of pull request #{get('BQ_PULL')}"
    try:
        if mode == "main":
            chain = first_parent_chain(api, head)
            relation = baseline_label(chain, get("BQ_BASE_COMMIT")) or relation
            closed = reconcile_main(api, head, get("BQ_BASE_COMMIT"), here, stamp(), chain)
        else:
            closed = reconcile_pull(api, get("BQ_PULL"), head, here, stamp())
        if closed:
            print(f"BENCH_COMPILER_RECONCILED {mode} check_runs={closed}")
    except (OSError, ValueError) as error:
        print(f"BENCH_COMPILER_RECONCILE_FAIL {error}", file=sys.stderr)
    lines = [
        f"Baseline `{get('BQ_BASE_COMMIT')}` ({relation}). Mode: {mode}, profile `compiler-compare-v1`.",
        f"Workflow run {run_id} attempt {attempt}: {here}",
        f"Request run {get('BQ_REQUEST_RUN_ID')} attempt {get('BQ_REQUEST_ATTEMPT')}; trusted harness "
        f"`{get('BQ_TRUSTED_REVISION')}`.",
        "",
        f"Authorized at {stamp()}; awaiting validated terminal evidence. Native Actions job state at {here} "
        "is authoritative for runner queueing and live execution; this short setup does not claim measurement.",
    ]
    output = queued_output(mode, head, lines)
    output["title"] = "Awaiting validated terminal evidence; see live Actions progress"
    queued = {"status": "queued", "details_url": here, "output": output}
    rows = advance(api, ensure_check(api, head, mode, marker, queued), queued)
    return rows


def main() -> int:
    """Display only: a failure is reported and never fails the request or the measurement."""
    control_started = time.monotonic()
    environment = dict(os.environ)
    command = sys.argv[1] if len(sys.argv) == 2 else ""
    repository, head, token = (environment.get(key, "") for key in ("BQ_REPOSITORY", "BQ_HEAD_COMMIT", "GH_TOKEN"))
    mode = "main" if command == "announce" else environment.get("BQ_MODE", "")
    try:
        if command not in ("announce", "start") or mode not in MODES or not SHA.fullmatch(head) or not token or \
                not all(DECIMAL.fullmatch(environment.get(key, "")) for key in (
                    ("BQ_REQUEST_RUN_ID", "BQ_REQUEST_ATTEMPT") if command == "announce" else
                    ("BQ_REQUEST_RUN_ID", "BQ_REQUEST_ATTEMPT", "BQ_RUN_ID", "BQ_RUN_ATTEMPT"))):
            raise ValueError("invalid workflow inputs")
        api = Api(repository, token)
        rows = announce(api, environment) if command == "announce" else start(api, environment)
        print(f"BENCH_COMPILER_CHECK {command} " + ", ".join(f"{row.get('id')}:{row.get('status')}" for row in rows)
              + f" api_requests={api.requests} control_execution_seconds={time.monotonic() - control_started:.6f}")
    except Exception as error:  # noqa: BLE001 - display only: report every failure, never fail the run
        print(f"::warning::9700X compiler benchmark check {command or 'command'} failed: {error!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
