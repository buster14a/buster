#!/usr/bin/env python3
"""Authorize one 9700X compiler comparison of a commit on main (#2752).

Run from trusted `main` by the hosted `authorize-compiler` job of
`.github/workflows/9700x-direct-bench.yml`. That workflow starts on
`workflow_run` when `.github/workflows/9700x-compiler-request.yml` completes for
a `push` to main. Nothing in the payload is trusted: this re-reads the request
run, the pushed commit, its first-parent chain, the baseline and main itself,
and fails closed unless they agree.

The commit is measured against its first parent, the main commit it landed
on, unless that commit has no valid main measurement: then against the
nearest first-parent ancestor, at most compiler_github.RECONCILE_DEPTH back,
that has one (choose_base), so the commits a merge burst left unmeasured are
inside a measured range. Without such an ancestor in reach, or when the check
listing cannot be read, the first parent is the baseline. The baseline must
be on the commit's first-parent chain (verify), so it is always main's own
code. The commit must still be on main (main equals it or descends from it),
so a force-moved or foreign ref is never measured. A queue merge commit's second
parent and pull request are recorded for the report; main is trusted code,
so no author gate applies: whatever landed on main, including bot-authored
pull requests, is measured.
"""

from __future__ import annotations

import os
import sys
import urllib.parse

from authorize import COMMIT, DECIMAL, REPOSITORY, fetch, full_name
from compiler_github import Api, GITHUB_ACTIONS_APP_ID, measured, parse_chain
from compiler_receipt import check_name

REQUEST_WORKFLOW = ".github/workflows/9700x-compiler-request.yml"


def tree(commit: object) -> object:
    record = commit.get("commit") if isinstance(commit, dict) else None
    record = record.get("tree") if isinstance(record, dict) else None
    return record.get("sha") if isinstance(record, dict) else None


def merged_pull(head: str, pulls: object) -> str:
    """The pull request whose merge produced head, or "0" for a direct push."""
    numbers = [pull.get("number") for pull in (pulls if isinstance(pulls, list) else [])
               if isinstance(pull, dict) and pull.get("merge_commit_sha") == head and type(pull.get("number")) is int]
    return str(numbers[0]) if len(numbers) == 1 else "0"


def choose_base(chain: list[str], checks: dict) -> str:
    """The nearest commit of chain with a valid main measurement in checks (sha -> rows), else chain[0]."""
    found = [sha for sha in chain if any(measured(row, sha) for row in checks.get(sha, []))]
    return found[0] if found else chain[0]


def verify(repository: str, run_id: int, head: str, run: object, commit: object, base_commit: object,
           on_main: object, pulls: object, chain: list[str] | None = None) -> tuple[list[str], dict]:
    """Return the failed checks and the measurement identity.

    base_commit is the chosen baseline's record; chain is head's first-parent
    chain, nearest first (default: the first parent alone). The baseline must
    be on it, and range counts the first-parent commits it spans.
    """
    failures: list[str] = []
    result: dict = {}
    if not isinstance(run, dict):
        run = {}
        failures.append("request run record")
    for name, holds in (
        ("request run id", type(run.get("id")) is int and run.get("id") == run_id),
        ("request workflow", run.get("path") == REQUEST_WORKFLOW),
        ("request event", run.get("event") == "push"),
        ("request branch", run.get("head_branch") == "main"),
        ("request conclusion", run.get("status") == "completed" and run.get("conclusion") == "success"),
        ("request head commit", run.get("head_sha") == head),
        ("request repository", full_name(run.get("repository")) == repository),
        ("request head repository", full_name(run.get("head_repository")) == repository),
    ):
        if not holds:
            failures.append(name)
    parents = commit.get("parents") if isinstance(commit, dict) else None
    parents = [parent.get("sha") for parent in parents if isinstance(parent, dict)] if isinstance(parents, list) else []
    if not (isinstance(commit, dict) and commit.get("sha") == head and 1 <= len(parents) <= 2
            and all(isinstance(sha, str) and COMMIT.fullmatch(sha) for sha in parents)):
        failures.append("commit has a first parent on main")
        parents = [""]
    first_parent = parents[0]
    pull_head = parents[1] if len(parents) == 2 else head
    chain = chain if chain and chain[0] == first_parent else [first_parent]
    base = base_commit.get("sha") if isinstance(base_commit, dict) else None
    if not (isinstance(base, str) and base in chain):
        failures.append("baseline commit is on the first-parent chain")
    head_tree, base_tree = tree(commit), tree(base_commit)
    if not all(isinstance(sha, str) and COMMIT.fullmatch(sha) for sha in (head_tree, base_tree)):
        failures.append("commit and first parent trees")
    if not (isinstance(on_main, dict) and on_main.get("status") in ("identical", "ahead")):
        failures.append("commit is still on main")
    if not failures:
        result = {"base": base, "base_tree": base_tree, "head_tree": head_tree,
                  "pull": merged_pull(head, pulls), "pull_head": pull_head,
                  "first_parent": first_parent, "range": str(chain.index(base) + 1)}
    return failures, result


def main_chain(prefix: str, token: str, head: str, commit: object) -> tuple[list[str], str]:
    """(head's first-parent chain, chosen baseline) from GitHub's records; ([], '') without a first parent."""
    parents = commit.get("parents") if isinstance(commit, dict) else None
    first = parents[0].get("sha") if isinstance(parents, list) and parents and isinstance(parents[0], dict) else ""
    chain: list[str] = []
    base = ""
    if isinstance(first, str) and COMMIT.fullmatch(first):
        chain, base = [first], first
        try:
            listed = parse_chain(fetch(f"{prefix}/commits?sha={head}&per_page=100", token), head)
            chain = listed if listed and listed[0] == first else chain
            query = urllib.parse.urlencode({"check_name": check_name("main"), "filter": "all",
                                            "app_id": GITHUB_ACTIONS_APP_ID, "per_page": 100})
            checks: dict = {}
            for sha in chain:
                rows = fetch(f"{prefix}/commits/{sha}/check-runs?{query}", token)
                checks[sha] = rows.get("check_runs", []) if isinstance(rows, dict) else []
                if any(measured(row, sha) for row in checks[sha]):
                    break
            base = choose_base(chain, checks)
        except (OSError, ValueError) as error:
            # Losing the range never refuses the measurement: the first parent is measured.
            chain, base = [first], first
            print(f"BENCH_COMPILER_RANGE_UNAVAILABLE {error}; measuring against the first parent", file=sys.stderr)
    return chain, base


MAIN_ROUTE_PATH = "docs/compiler-main-profile-routing-v1.tsv"
MAIN_ROUTE_MODULE_PATH = "tools/compiler_main_profile_policy.c"
MAIN_CERTIFICATE_PATH = "docs/compiler-main-profile-certificate-v1.tsv"
MAIN_CAMPAIGN_PATH = "docs/compiler-main-profile-campaign-facts-v1.tsv"
MAIN_ARCHIVE_PATH = "docs/compiler-main-profile-archive-v1.tsv"
MAIN_REVIEW_PATH = "docs/compiler-main-profile-reviews-v1.tsv"
MAIN_CRITERIA_PATH = "docs/compiler-main-profile-criteria-v1.tsv"
BENCH_WORKFLOW = ".github/workflows/9700x-direct-bench.yml"
MAIN_ROUTE_FIELDS = ["schema","repository","state","measurement_revision","main_profile","preparation_policy","phase_schema","lab_sha256","python_path","python_sha256","driver_sha256","compare_sha256","receipt_sha256","owned_phase_sha256","owned_plan_sha256","certificate_revision","certificate_sha256","previous_policy_revision", "trusted_root", "candidate_root", "work_root", "evidence_root"]
MAIN_ROUTE_OUTPUT_FIELDS = ("main_owned", "main_profile", "main_preparation_policy", "main_phase_schema",
                            "main_measurement_revision", "main_certificate_revision", "main_certificate_sha256",
                            "lab_sha256", "python_path", "python_sha256", "driver_sha256", "compare_sha256",
                            "receipt_sha256", "owned_phase_sha256", "owned_plan_sha256",
                            "trusted_root", "candidate_root", "work_root", "evidence_root")


def main_route_record(text: str, fields: list[str] | tuple[str, ...] | None = None) -> dict[str, str]:
    """Strict bounded ordered data record; this collector never decides eligibility."""
    rows = text.splitlines(keepends=True)
    if not 0 < len(text.encode("utf-8")) <= 128 * 1024 or any(not row.endswith("\n") for row in rows):
        raise ValueError("Main route record is missing, partial or oversized")
    result = {}
    for index, row in enumerate(rows):
        columns = row[:-1].split("\t")
        if len(columns) != 2 or not all(0 < len(value) <= 512 and all(32 <= ord(c) <= 126 for c in value)
                                       for value in columns):
            raise ValueError("Main route record has a malformed field")
        name, value = columns
        if name in result or fields is not None and (index >= len(fields) or name != fields[index]):
            raise ValueError("Main route fields are repeated, unknown or out of order")
        result[name] = value
    if fields is not None and list(result) != list(fields):
        raise ValueError("Main route fields are incomplete")
    return result


def main_route_content(api, path: str, revision: str, *, absent_ok: bool = False) -> str | None:
    """API-selected committed bytes only; 404 is historical absence, never an empty new policy."""
    import base64
    import urllib.error
    if not COMMIT.fullmatch(revision):
        raise ValueError("Main route content revision is invalid")
    try:
        row = api.request(f"/contents/{path}?ref={revision}")
    except urllib.error.HTTPError as error:
        if absent_ok and error.code == 404:
            return None
        raise
    if not isinstance(row, dict) or row.get("type") != "file" or row.get("path") != path or \
            row.get("encoding") != "base64" or type(row.get("size")) is not int or not 0 <= row["size"] <= 128 * 1024 or \
            not isinstance(row.get("content"), str):
        raise ValueError("Main route content is missing or exceeds its data bound")
    encoded = row["content"]
    if any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\n" for c in encoded):
        raise ValueError("Main route content encoding is malformed")
    payload = base64.b64decode(encoded.replace("\n", ""), validate=True)
    if len(payload) != row["size"]:
        raise ValueError("Main route content byte count differs from the API")
    return payload.decode("utf-8")


def main_route_executor(api, repository: str, executor_run: dict, attempt: str) -> dict:
    """Independently reload original executor attempt N from the API and bind the caller to it."""
    if not REPOSITORY.fullmatch(repository) or not isinstance(attempt, str) or not DECIMAL.fullmatch(attempt) or \
            not isinstance(executor_run, dict) or type(executor_run.get("id")) is not int or executor_run["id"] <= 0:
        raise ValueError("Main route executor inputs are malformed")
    actual = api.request(f"/actions/runs/{executor_run['id']}/attempts/{attempt}")
    if not isinstance(actual, dict) or type(actual.get("id")) is not int or actual["id"] != executor_run["id"] or \
            type(actual.get("run_attempt")) is not int or actual["run_attempt"] != int(attempt) or \
            actual.get("path") != BENCH_WORKFLOW or actual.get("event") != "workflow_run" or \
            actual.get("head_branch") != "main" or full_name(actual.get("repository")) != repository or \
            full_name(actual.get("head_repository")) != repository or \
            not isinstance(actual.get("head_sha"), str) or not COMMIT.fullmatch(actual["head_sha"]):
        raise ValueError("Main route original API executor attempt is foreign or malformed")
    for key in ("id", "run_attempt", "head_sha", "path", "event", "head_branch", "repository", "head_repository", "display_title"):
        if executor_run.get(key) != actual.get(key):
            raise ValueError("Main route caller differs from the original API attempt")
    triggering = actual.get("triggering_actor")
    if actual["run_attempt"] > 1 and (not isinstance(triggering, dict) or
                                      triggering.get("login") != "davidgmbb" or
                                      type(triggering.get("id")) is not int or triggering["id"] != 39247043):
        raise ValueError("Main route executor rerun lacks the existing owner authority")
    return actual


def main_route_attempt(api, repository: str, executor_run: dict, attempt: str) -> tuple[dict, dict, str]:
    """Independently select original attempt N and its exact request from the canonical run title."""
    import re
    actual = main_route_executor(api, repository, executor_run, attempt)
    title = actual.get("display_title")
    match = re.fullmatch(r"9700X request ([1-9][0-9]*)\.([1-9][0-9]*) head ([0-9a-f]{40})", title) \
        if isinstance(title, str) else None
    if match is None:
        raise ValueError("Main route executor has no canonical request identity")
    request = api.request(f"/actions/runs/{match[1]}/attempts/{match[2]}")
    if not isinstance(request, dict) or type(request.get("id")) is not int or request["id"] != int(match[1]) or \
            type(request.get("run_attempt")) is not int or request["run_attempt"] != int(match[2]) or \
            request.get("path") != REQUEST_WORKFLOW or request.get("event") != "push" or \
            request.get("head_branch") != "main" or request.get("head_sha") != match[3] or \
            request.get("status") != "completed" or request.get("conclusion") != "success" or \
            full_name(request.get("repository")) != repository or full_name(request.get("head_repository")) != repository:
        raise ValueError("Main route original API request is not the successful exact main push")
    return actual, request, match[3]


def main_route_native(root, records: dict[str, str]) -> dict[str, str]:
    """One fixed bounded trusted C validation, with committed/API bytes consumed as data."""
    import subprocess
    import tempfile
    from pathlib import Path
    with tempfile.TemporaryDirectory(prefix="buster-main-route-") as temporary:
        directory = Path(temporary)
        names = ("policy", "facts", "certificate", "campaign", "archive", "reviews", "criteria", "previous")
        for name in names:
            (directory / f"{name}.tsv").write_text(records.get(name, ""), encoding="utf-8")
        output = directory / "native-output.txt"
        command = [str(root / "build.sh"), "compiler_profile_qualification", "--resolve-main-route",
                   *(str(directory / f"{name}.tsv") for name in names), str(output)]
        with (directory / "native.log").open("xb") as log:
            try:
                completed = subprocess.run(command, cwd=root, stdin=subprocess.DEVNULL, stdout=log,
                                           stderr=subprocess.STDOUT, timeout=120, check=False)
            except subprocess.TimeoutExpired as error:
                # Callers refuse ValueError; an expired resolver must never crash publication.
                raise ValueError("Main route trusted native eligibility timed out") from error
        if completed.returncode != 0 or not output.is_file() or output.stat().st_size > 8192:
            raise ValueError("Main route trusted native eligibility refused the original policy")
        rows = output.read_text(encoding="utf-8").splitlines()
        result = {}
        for row in rows:
            columns = row.split("=")
            if len(columns) != 2 or columns[0] in result:
                raise ValueError("Main route native output is ambiguous")
            result[columns[0]] = columns[1]
        if tuple(result) != MAIN_ROUTE_OUTPUT_FIELDS or result["main_owned"] not in ("true", "false"):
            raise ValueError("Main route native output is incomplete or malformed")
        return result


def resolve_main_route(api, repository: str, executor_run: dict, attempt: str) -> dict:
    """Original API executor attempt N selects protected P, which selects immutable H and the native policy."""
    import hashlib
    from pathlib import Path
    actual = main_route_executor(api, repository, executor_run, attempt)
    policy_revision = actual["head_sha"]
    on_main = api.request(f"/compare/{policy_revision}...main")
    if not isinstance(on_main, dict) or on_main.get("status") not in ("identical", "ahead"):
        raise ValueError("Main route policy revision is not retained on protected main")
    policy_text = main_route_content(api, MAIN_ROUTE_PATH, policy_revision, absent_ok=True)
    if policy_text is None:
        if main_route_content(api, MAIN_ROUTE_MODULE_PATH, policy_revision, absent_ok=True) is not None:
            raise ValueError("Main route policy is missing from a revision that already claims native routing")
        # Only an actually missing file at original P retains historical interpretation.
        return {"main_owned": False, "main_profile": "compiler-compare-v1",
                "main_preparation_policy": "legacy-rebuild", "main_phase_schema": "-",
                "main_measurement_revision": policy_revision, "main_policy_revision": policy_revision,
                "main_certificate_revision": "-", "main_certificate_sha256": "-",
                **{name: "-" for name in MAIN_ROUTE_OUTPUT_FIELDS[7:]}}
    # A routed revision also requires the canonical request identity in the executor title.
    actual, request, head = main_route_attempt(api, repository, executor_run, attempt)
    policy = main_route_record(policy_text, MAIN_ROUTE_FIELDS)
    facts = {"schema": "buster-compiler-main-route-github-facts-v1", "repository": repository,
             "executor_run": str(actual["id"]), "executor_attempt": str(actual["run_attempt"]),
             "policy_revision": policy_revision, "executor_workflow": actual["path"],
             "executor_event": actual["event"], "executor_branch": actual["head_branch"],
             "request_run": str(request["id"]), "request_attempt": str(request["run_attempt"]),
             "request_head": head, "request_workflow": request["path"], "request_event": request["event"],
             "request_branch": request["head_branch"], "request_status": request["status"],
             "request_conclusion": request["conclusion"]}
    records = {"policy": policy_text, "facts": "".join(f"{key}\t{value}\n" for key, value in facts.items())}
    references = []
    if policy["measurement_revision"] != "-":
        references.append(policy["measurement_revision"])
    if policy["certificate_revision"] != "-":
        certificate_text = main_route_content(api, MAIN_CERTIFICATE_PATH, policy["certificate_revision"])
        if hashlib.sha256(certificate_text.encode()).hexdigest() != policy["certificate_sha256"]:
            raise ValueError("Main route committed certificate hash changed")
        records["certificate"] = certificate_text
        certificate = main_route_record(certificate_text)
        for name, path in (("campaign", MAIN_CAMPAIGN_PATH), ("archive", MAIN_ARCHIVE_PATH),
                           ("reviews", MAIN_REVIEW_PATH), ("criteria", MAIN_CRITERIA_PATH)):
            prefix = "facts" if name == "campaign" else name
            revision = certificate.get(f"{prefix}_revision", "")
            text = main_route_content(api, path, revision)
            if hashlib.sha256(text.encode()).hexdigest() != certificate.get(f"{prefix}_sha256"):
                raise ValueError(f"Main route {name} proof hash changed")
            records[name] = text
            references.append(revision)
        references.append(policy["certificate_revision"])
    if policy["previous_policy_revision"] != "-":
        records["previous"] = main_route_content(api, MAIN_ROUTE_PATH, policy["previous_policy_revision"])
        references.append(policy["previous_policy_revision"])
    for revision in set(references):
        if not COMMIT.fullmatch(revision):
            raise ValueError("Main route reference is not an immutable revision")
        lineage = api.request(f"/compare/{revision}...{policy_revision}")
        if not isinstance(lineage, dict) or lineage.get("status") not in ("identical", "ahead"):
            raise ValueError("Main route proof or measurement harness is outside original policy ancestry")
    result = main_route_native(Path(__file__).resolve().parents[2], records)
    result["main_owned"] = result["main_owned"] == "true"
    result["main_policy_revision"] = policy_revision
    if not result["main_owned"]:
        result["main_measurement_revision"] = policy_revision
    else:
        suffix = f"run-{actual['id']}-attempt{actual['run_attempt']}"
        for name in ("work_root", "evidence_root"):
            result[name] = result[name] + "/" + suffix
    return result


def main_route_transport(api, repository: str, executor_run: dict, attempt: str, route: dict, identity: dict) -> dict[str, str]:
    """Serialize already authorized data for the tokenless fixed native MAIN controller."""
    import base64
    actual, request, head = main_route_attempt(api, repository, executor_run, attempt)
    if type(route.get("main_owned")) is not bool or route.get("main_policy_revision") != actual["head_sha"]:
        raise ValueError("Main route transport differs from original API policy")
    ordered = {}
    for name in (*MAIN_ROUTE_OUTPUT_FIELDS, "main_policy_revision"):
        value = route.get(name)
        ordered[name] = ("true" if value else "false") if type(value) is bool else value
        if not isinstance(ordered[name], str):
            raise ValueError("Main route transport field is malformed")
    facts = {"schema": "buster-compiler-main-route-github-facts-v1", "repository": repository,
             "executor_run": str(actual["id"]), "executor_attempt": str(actual["run_attempt"]),
             "policy_revision": actual["head_sha"], "executor_workflow": actual["path"],
             "executor_event": actual["event"], "executor_branch": actual["head_branch"],
             "request_run": str(request["id"]), "request_attempt": str(request["run_attempt"]),
             "request_head": head, "request_workflow": request["path"], "request_event": request["event"],
             "request_branch": request["head_branch"], "request_status": request["status"],
             "request_conclusion": request["conclusion"]}
    source = {"schema": "buster-compiler-main-identity-v1", **{key: identity.get(key) for key in
              ("base", "base_tree", "head", "head_tree", "pull", "pull_head", "first_parent", "range")}}
    if source["head"] != head or any(not isinstance(value, str) for value in source.values()):
        raise ValueError("Main route retained source identity is incomplete")
    result = {}
    for name, values in (("route", ordered), ("facts", facts), ("identity", source)):
        payload = "".join(f"{key}\t{value}\n" for key, value in values.items()).encode("utf-8")
        if len(payload) > 16384:
            raise ValueError("Main route transport exceeds the fixed native bound")
        result["main_" + name + "_data"] = base64.b64encode(payload).decode("ascii")
    return result


def main() -> int:
    environment = os.environ
    repository = environment.get("BQ_REPOSITORY", "")
    run_id = environment.get("BQ_REQUEST_RUN_ID", "")
    head = environment.get("BQ_HEAD_COMMIT", "")
    attempt = environment.get("BQ_RUN_ATTEMPT", "")
    token = environment.get("GH_TOKEN", "")
    output = environment.get("GITHUB_OUTPUT", "")
    failures: list[str] = []
    result: dict = {}
    if not (REPOSITORY.fullmatch(repository) and DECIMAL.fullmatch(run_id) and COMMIT.fullmatch(head)
            and DECIMAL.fullmatch(attempt) and token and output):
        failures.append("workflow inputs")
    else:
        prefix = f"/repos/{repository}"
        run = fetch(f"{prefix}/actions/runs/{run_id}", token)
        commit = fetch(f"{prefix}/commits/{head}", token)
        chain, base = main_chain(prefix, token, head, commit)
        base_commit = fetch(f"{prefix}/commits/{base}", token) if isinstance(base, str) and COMMIT.fullmatch(base) else None
        # status "ahead"/"identical": main descends from (or is) the commit.
        on_main = fetch(f"{prefix}/compare/{head}...main", token)
        pulls = fetch(f"{prefix}/commits/{head}/pulls?per_page=10", token)
        failures, result = verify(repository, int(run_id), head, run, commit, base_commit, on_main, pulls, chain)
        if not failures:
            api = Api(repository, token)
            executor_id = environment.get("GITHUB_RUN_ID", "")
            if not DECIMAL.fullmatch(executor_id):
                failures.append("original executor API identity")
            else:
                executor = api.request(f"/actions/runs/{executor_id}/attempts/{attempt}")
                route = resolve_main_route(api, repository, executor, attempt)
                result.update(route)
                if route["main_owned"]:
                    result.update(main_route_transport(api, repository, executor, attempt, route, dict(result, head=head)))
    if failures:
        print("BENCH_COMPILER_UNAUTHORIZED " + ", ".join(failures), file=sys.stderr)
    else:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"attempt={attempt}\nbase={result['base']}\nbase_tree={result['base_tree']}\n"
                         f"head_tree={result['head_tree']}\npull={result['pull']}\npull_head={result['pull_head']}\n"
                         f"first_parent={result['first_parent']}\nrange={result['range']}\n")
            for name in (*MAIN_ROUTE_OUTPUT_FIELDS, "main_policy_revision", "main_route_data", "main_facts_data", "main_identity_data"):
                if name in result:
                    value = result[name]
                    stream.write(f"{name}={'true' if value is True else 'false' if value is False else value}\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
