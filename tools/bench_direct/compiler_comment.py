#!/usr/bin/env python3
"""One maintained 9700X compiler report comment on the measured commit (#2804).

Ownership: `tools/bench_direct`, trusted `main` only. Run by the hosted
`comment-compiler` job of `.github/workflows/9700x-direct-bench.yml` and the
`comment` job of `.github/workflows/9700x-compiler-report.yml`, the only jobs
that hold the commit-comment write permission. Neither downloads evidence:
the input is the report entry that `compiler_publish.py` validated and
rendered (BQ_COMMENT, JSON data), re-checked here by validate_entry.

One general commit comment per measured commit and mode (main only for now)
is upserted. It is ours only when all hold (owned): the GitHub Actions bot
wrote it, it is on exactly that commit, its first line is the hidden STATE
marker and the marker's JSON names that head and mode. A copied marker in a
human or other comment is ignored, and nothing else is ever edited.

The comment shows the newest measurement attempt in full, ordered by (bench
run ID, run attempt), and lists every attempt with its links in a history
table; an older or repeated attempt adds or refreshes its history row but
never replaces the newest report. Each republication of an attempt is
recorded separately under its publications. Writes converge (upsert): a
lost create response is found by the next listing, and duplicate owned
comments, from concurrent publishers, are merged into the oldest and the
rest deleted.

Map: STATE, HISTORY, validate_entry, owned, parse, combine, render, upsert,
main.
"""

from __future__ import annotations

import json
import os
import re
import sys
import time
import urllib.error

from compiler_github import Api, SERVER
from compiler_receipt import DECIMAL, SHA

STATE = "buster-9700x-compiler-report-v1"
HISTORY = "<!-- buster-9700x-compiler-report-history -->"
STATE_LINE = re.compile(r"<!-- " + STATE + r" (\{.*\}) -->\Z")
BOT = {"login": "github-actions[bot]", "id": 41898282}
MODES = ("main",)
CONCLUSIONS = ("success", "failure", "neutral", "cancelled", "skipped", "timed_out")
MARKDOWN_LIMIT = 40000
BODY_LIMIT = 65000
ATTEMPT_LIMIT = 30
PUBLICATION_LIMIT = 5
ROUNDS = 4
FIELDS = ("mode", "head", "base", "run_id", "run_attempt", "request_run_id", "trusted_revision", "conclusion",
          "title", "captured_at", "check_url", "run_url", "artifact_url", "artifact_expires_at")


def validate_entry(entry: object, repository: str) -> list[str]:
    """Why a report entry is not publishable; empty when it is."""
    problems = []
    entry = entry if isinstance(entry, dict) else {}
    if entry.get("mode") not in MODES:
        problems.append(f"mode {entry.get('mode')!r} has no commit report")
    for key in ("head", "base", "trusted_revision"):
        # A refused authorization never resolved a baseline: shown as NA.
        if not (isinstance(entry.get(key), str) and (SHA.fullmatch(entry[key]) or key == "base" and not entry[key])):
            problems.append(f"{key} is not an exact commit")
    for key in ("run_id", "run_attempt", "request_run_id"):
        if not (isinstance(entry.get(key), str) and DECIMAL.fullmatch(entry[key])):
            problems.append(f"{key} is not a decimal ID")
    if entry.get("conclusion") not in CONCLUSIONS:
        problems.append(f"conclusion {entry.get('conclusion')!r} is unknown")
    for key in ("title", "captured_at", "artifact_expires_at"):
        if not isinstance(entry.get(key), str) or len(entry[key]) > 200:
            problems.append(f"{key} is missing or too long")
    for key in ("check_url", "run_url", "artifact_url"):
        value = entry.get(key)
        if not (value == "" or isinstance(value, str) and value.startswith(f"{SERVER}/{repository}/")
                and len(value) < 300 and not re.search(r"[\s()<>\[\]]", value)):
            problems.append(f"{key} is not a link into {repository}")
    if not (isinstance(entry.get("markdown"), str) and 0 < len(entry["markdown"]) <= MARKDOWN_LIMIT):
        problems.append("report markdown is missing or too long")
    return problems


def parse(body: object, head: str, mode: str) -> tuple[dict, str] | None:
    """(state, current markdown) of a body carrying our marker for head and mode, else None."""
    result = None
    first, _, rest = body.partition("\n") if isinstance(body, str) else ("", "", "")
    match = STATE_LINE.fullmatch(first)
    try:
        state = json.loads(match.group(1)) if match else None
    except ValueError:
        state = None
    if isinstance(state, dict) and state.get("head") == head and state.get("mode") == mode and \
            isinstance(state.get("attempts"), list):
        result = (state, rest.partition("\n" + HISTORY)[0].strip())
    return result


def owned(comment: object, head: str, mode: str) -> bool:
    user = comment.get("user") if isinstance(comment, dict) else None
    return isinstance(comment, dict) and type(comment.get("id")) is int and isinstance(user, dict) and \
        {"login": user.get("login"), "id": user.get("id")} == BOT and comment.get("commit_id") == head and \
        not comment.get("path") and comment.get("position") is None and parse(comment.get("body"), head, mode) is not None


def key(attempt: dict) -> tuple[int, int]:
    return int(attempt.get("run_id", 0)), int(attempt.get("run_attempt", 0))


def combine(comments: list[dict], entry: dict, publication: dict) -> tuple[list[dict], str]:
    """(attempts, current markdown) merged from every owned comment and the new entry."""
    attempts: dict[tuple[int, int], dict] = {}
    current: tuple[tuple[int, int], str] | None = None
    for comment in comments:
        state, markdown = parse(comment["body"], entry["head"], entry["mode"])
        rows = [row for row in state["attempts"] if isinstance(row, dict) and
                all(isinstance(row.get(name), str) and DECIMAL.fullmatch(row[name]) for name in ("run_id", "run_attempt"))]
        for row in rows:
            known = attempts.get(key(row))
            if known is not None:
                merged = [*known.get("publications", []), *row.get("publications", [])]
                row = dict(row, publications=[item for index, item in enumerate(merged) if item not in merged[:index]])
            attempts[key(row)] = row
        if rows and markdown and (current is None or max(map(key, rows)) > current[0]):
            current = (max(map(key, rows)), markdown)
    previous = attempts.get(key(entry), {})
    publications = [item for item in previous.get("publications", []) if isinstance(item, dict)]
    publications = [item for item in publications if item != publication] + [publication]
    attempts[key(entry)] = dict({name: entry[name] for name in FIELDS},
                                publications=publications[-PUBLICATION_LIMIT:])
    # The newest attempt's report is shown; a tie is the same attempt republished.
    if current is None or key(entry) >= current[0]:
        current = (key(entry), entry["markdown"])
    ordered = sorted(attempts.values(), key=key)[-ATTEMPT_LIMIT:]
    return ordered, current[1]


def render(head: str, mode: str, attempts: list[dict], markdown: str) -> str:
    state = json.dumps({"head": head, "mode": mode, "attempts": attempts}, sort_keys=True, separators=(",", ":"))
    lines = [f"<!-- {STATE} {state} -->", markdown, "", HISTORY, "",
             f"<details><summary>Every recorded attempt for this commit ({len(attempts)})</summary>", "",
             "| Run | Attempt | Conclusion | Result | Captured | Links | Published |",
             "| --- | --- | --- | --- | --- | --- | --- |"]
    for row in reversed(attempts):
        links = " · ".join(f"[{label}]({row[name]})" for label, name in (
            ("check", "check_url"), ("workflow attempt", "run_url"), ("evidence", "artifact_url")) if row.get(name))
        published = ", ".join(f"[{item.get('at', 'NA')}]({item['url']})" if isinstance(item.get("url"), str)
                              else str(item.get("at", "NA")) for item in row.get("publications", []))
        lines.append(f"| {row['run_id']} | {row['run_attempt']} | {row['conclusion']} | "
                     f"{row['title'].replace('|', '/')} | {row['captured_at']} | {links or 'NA'} | {published} |")
    lines += ["", "</details>"]
    return "\n".join(lines)[:BODY_LIMIT]


def upsert(api: Api, entry: dict, publication: dict) -> dict:
    """Create or update the one owned comment for entry's head and mode; returns it."""
    head, mode = entry["head"], entry["mode"]
    path = f"/commits/{head}/comments"
    result: dict | None = None
    for _ in range(ROUNDS):
        comments = sorted((comment for comment in api.pages(path) if owned(comment, head, mode)),
                          key=lambda comment: comment["id"])
        attempts, markdown = combine(comments, entry, publication)
        body = render(head, mode, attempts, markdown)
        if len(comments) == 1 and comments[0]["body"] == body:
            result = comments[0]
            break
        try:
            if not comments:
                api.request(path, {"body": body})
            else:
                if comments[0]["body"] != body:
                    api.request(f"/comments/{comments[0]['id']}", {"body": body}, "PATCH")
                for extra in comments[1:]:
                    api.request(f"/comments/{extra['id']}", None, "DELETE")
        except (urllib.error.URLError, TimeoutError, ValueError) as error:
            # A lost response may still have written; the next listing decides.
            print(f"BENCH_COMPILER_COMMENT_RETRY {error}", file=sys.stderr)
    if result is None:
        raise OSError(f"the report comment on {head} did not converge in {ROUNDS} rounds")
    return result


def main() -> int:
    environment = os.environ
    repository = environment.get("BQ_REPOSITORY", "")
    code = 1
    try:
        entry = json.loads(environment.get("BQ_COMMENT", "") or "null")
    except ValueError:
        entry = None
    problems = validate_entry(entry, repository)
    publication = {"run_id": environment.get("BQ_PUBLISHER_RUN_ID", ""),
                   "run_attempt": environment.get("BQ_PUBLISHER_ATTEMPT", ""),
                   "revision": environment.get("BQ_TRUSTED_REVISION", ""),
                   "at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    publication["url"] = f"{SERVER}/{repository}/actions/runs/{publication['run_id']}/attempts/{publication['run_attempt']}"
    if not (DECIMAL.fullmatch(publication["run_id"]) and DECIMAL.fullmatch(publication["run_attempt"])
            and SHA.fullmatch(publication["revision"]) and environment.get("GH_TOKEN")):
        problems.append("publisher run, attempt, revision or token")
    if problems:
        print("BENCH_COMPILER_COMMENT_FAIL " + "; ".join(problems), file=sys.stderr)
    else:
        try:
            comment = upsert(Api(repository, environment["GH_TOKEN"]), entry, publication)
            print(f"BENCH_COMPILER_COMMENTED {comment.get('html_url') or comment.get('id')}")
            line = f"Commit report comment: {comment.get('html_url') or comment.get('id')}"
            code = 0
        except (OSError, ValueError) as error:
            line = f"The commit report comment was NOT published: {error}. The measurement and its check stand."
            print(f"BENCH_COMPILER_COMMENT_FAIL {error}", file=sys.stderr)
        with open(environment.get("GITHUB_STEP_SUMMARY") or os.devnull, "a", encoding="utf-8") as stream:
            stream.write(line + "\n")
    return code


if __name__ == "__main__":
    sys.exit(main())
