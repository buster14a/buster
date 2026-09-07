#!/usr/bin/env python3
"""File the 2026-09-07 audit issues on GitHub. Writes only with --publish.

No token is accepted on the command line or stored. GitHub is reached one of two ways,
chosen automatically: an authenticated `gh` CLI when one is on PATH (preferred - writes
then carry your GitHub identity), otherwise the REST API with GH_TOKEN/GITHUB_TOKEN from
the environment, which is what an agent sandbox provides. The default run is entirely
offline and prints a plan.

    python3 publish.py                      # offline plan, no network at all
    python3 publish.py --check              # online preflight: drift + duplicates, no writes
    python3 publish.py --publish            # create the issues
    python3 publish.py --publish --only c-enum-underlying-type
    python3 publish.py --link-references    # second pass: cross-link filed issues by number

Before any write, every issue's cited production files are compared against current
main by git blob SHA. A file that has changed since the audit means the issue's line
numbers may be stale, and the run stops rather than filing a misleading report; pass
--allow-drift to file anyway, which appends an explicit drift notice to the body.

GitHub writes cannot be made atomic across 26 issues. Each successful creation is
recorded in publication-results.json as it happens, and a re-run reuses what is
already filed rather than duplicating it.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

BUNDLE = Path(__file__).resolve().parent
MANIFEST = json.loads((BUNDLE / "manifest.json").read_text())
REPO = MANIFEST["repository"]
RESULTS = BUNDLE / "publication-results.json"
AUDITED_REVISION = MANIFEST["audited_revision"]


def command(args: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=300)
    if check and result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(args[:3])}\n"
                           f"{result.stderr.strip()}")
    return result


# --- transport -------------------------------------------------------------
# Two ways to reach GitHub, because the two environments that might run this
# have different halves: a developer machine has an authenticated `gh` and no
# token in the environment; an agent sandbox has a proxy-injected token and no
# `gh` binary. Prefer `gh` when present so writes carry the human's identity.

def _token() -> str | None:
    for name in ("GH_TOKEN", "GITHUB_TOKEN"):
        value = os.environ.get(name)
        if value:
            return value
    return None


def transport() -> str:
    if shutil.which("gh"):
        return "gh"
    if _token():
        return "api"
    raise RuntimeError(
        "No way to reach GitHub: `gh` is not on PATH and neither GH_TOKEN nor GITHUB_TOKEN is set.\n"
        "Install and authenticate the GitHub CLI (`gh auth login`), or run this where a token is provided.")


def check_auth() -> str:
    mode = transport()
    if mode == "gh":
        command(["gh", "auth", "status", "--hostname", "github.com"])
    else:
        # Cheapest call that proves the token reaches this repository.
        status, _ = api("GET", f"/repos/{REPO}")
        if status == 403:
            raise RuntimeError(
                f"The token cannot reach {REPO} (403). In an agent sandbox this usually means the "
                "repository is not attached to this session; a session's repository access is fixed "
                "when it starts, so authorizing access mid-session does not help. Start a fresh "
                "session with the repository attached, or run this from a machine with `gh`.")
        if status >= 400:
            raise RuntimeError(f"Token check failed against {REPO}: HTTP {status}")
    return mode


def api(method: str, path: str, payload: dict | None = None) -> tuple[int, object]:
    """Direct REST call. Returns (status, parsed body|None)."""
    request = urllib.request.Request(
        "https://api.github.com" + path, method=method,
        data=json.dumps(payload).encode() if payload is not None else None,
        headers={"Authorization": f"Bearer {_token()}",
                 "Accept": "application/vnd.github+json",
                 "X-GitHub-Api-Version": "2022-11-28",
                 "User-Agent": "buster-audit-publish",
                 **({"Content-Type": "application/json"} if payload is not None else {})})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            body = response.read().decode()
            return response.status, (json.loads(body) if body.strip() else None)
    except urllib.error.HTTPError as error:
        body = error.read().decode()
        try:
            return error.code, json.loads(body) if body.strip() else None
        except json.JSONDecodeError:
            return error.code, body
    except urllib.error.URLError as error:
        raise RuntimeError(f"Could not reach api.github.com: {error.reason}") from error


def validate_bundle(items: list[dict]) -> None:
    """Every body must be present, unmodified since the manifest, and carry its marker."""
    for item in items:
        path = BUNDLE / item["body"]
        if not path.exists():
            raise RuntimeError(f"Missing issue body: {item['body']}")
        text = path.read_text()
        actual = hashlib.sha256(text.encode()).hexdigest()
        if actual != item["sha256"]:
            raise RuntimeError(
                f"{item['body']} has been modified since the manifest was written.\n"
                f"  manifest: {item['sha256']}\n  actual:   {actual}\n"
                "Update manifest.json deliberately, or restore the file.")
        if item["marker"] not in text:
            raise RuntimeError(f"Missing audit marker, which is what prevents duplicate filing: {item['body']}")


def remote_blob_sha(path: str, ref: str) -> str | None:
    """Git blob SHA of `path` at `ref` on GitHub, or None when the file is gone."""
    if transport() == "gh":
        result = command(["gh", "api", f"repos/{REPO}/contents/{path}?ref={ref}", "--jq", ".sha"],
                         check=False)
        if result.returncode:
            if "404" in result.stderr or "Not Found" in result.stderr:
                return None
            raise RuntimeError(f"Could not read {path} at {ref}: {result.stderr.strip()}")
        return result.stdout.strip()
    status, body = api("GET", f"/repos/{REPO}/contents/{urllib.parse.quote(path)}?ref={ref}")
    if status == 404:
        return None
    if status >= 400 or not isinstance(body, dict):
        raise RuntimeError(f"Could not read {path} at {ref}: HTTP {status} {body}")
    return body["sha"]


def check_drift(items: list[dict], ref: str) -> dict[str, list[str]]:
    """Map each issue key to the cited files that have changed since the audit."""
    cache: dict[str, str | None] = {}
    drift: dict[str, list[str]] = {}
    for item in items:
        changed = []
        for path, audited_sha in item.get("sources", {}).items():
            if path not in cache:
                cache[path] = remote_blob_sha(path, ref)
            current = cache[path]
            if current is None:
                changed.append(f"{path} (deleted or moved)")
            elif current != audited_sha:
                changed.append(path)
        if changed:
            drift[item["key"]] = changed
    return drift


def remote_issues() -> list[dict]:
    if transport() == "gh":
        output = command(["gh", "api", f"repos/{REPO}/issues?state=all&per_page=100", "--paginate",
                          "--jq", ".[] | select(.pull_request == null) | "
                                  "{number,title,state,body,html_url}"]).stdout
        return [json.loads(line) for line in output.splitlines() if line.strip()]
    issues, page = [], 1
    while True:
        status, body = api("GET", f"/repos/{REPO}/issues?state=all&per_page=100&page={page}")
        if status >= 400 or not isinstance(body, list):
            raise RuntimeError(f"Could not list issues: HTTP {status} {body}")
        issues += [{"number": e["number"], "title": e["title"], "state": e["state"],
                    "body": e.get("body"), "html_url": e["html_url"]}
                   for e in body if "pull_request" not in e]
        if len(body) < 100:
            break
        page += 1
    return issues


def existing_issue(item: dict, remote: list[dict]) -> dict | None:
    """Marker match is authoritative; exact title is a weaker safety net.

    Neither is semantic duplicate detection - review the open issues yourself before
    a first publication.
    """
    matches = [entry for entry in remote
               if item["marker"] in (entry.get("body") or "") or entry["title"] == item["title"]]
    if len(matches) > 1:
        raise RuntimeError(f"More than one issue matches {item['key']}; resolve the duplicates first: "
                           + ", ".join(entry["html_url"] for entry in matches))
    result = matches[0] if matches else None
    if result is not None and result["state"] != "open":
        raise RuntimeError(f"A matching issue is closed; review before re-filing: {result['html_url']}")
    return result


def load_results() -> dict:
    if RESULTS.exists():
        return json.loads(RESULTS.read_text())
    return {"repository": REPO, "audited_revision": AUDITED_REVISION, "issues": {}}


def save_results(state: dict) -> None:
    temporary = RESULTS.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, indent=2) + "\n")
    temporary.replace(RESULTS)


def create_issue(title: str, body: str) -> dict:
    if transport() == "gh":
        body_path = BUNDLE / ".publish-body.tmp.md"
        body_path.write_text(body)
        try:
            output = command(["gh", "issue", "create", "--repo", REPO,
                              "--title", title, "--body-file", str(body_path)]).stdout
        finally:
            body_path.unlink(missing_ok=True)
        return created_number(output)
    status, response = api("POST", f"/repos/{REPO}/issues", {"title": title, "body": body})
    if status != 201 or not isinstance(response, dict):
        raise RuntimeError(f"Issue creation failed: HTTP {status} {response}\n"
                           "Check GitHub before retrying so you do not file a duplicate.")
    return {"number": response["number"], "url": response["html_url"]}


def update_issue_body(number: int, body: str) -> None:
    if transport() == "gh":
        body_path = BUNDLE / ".publish-body.tmp.md"
        body_path.write_text(body)
        try:
            command(["gh", "issue", "edit", str(number), "--repo", REPO,
                     "--body-file", str(body_path)])
        finally:
            body_path.unlink(missing_ok=True)
        return
    status, response = api("PATCH", f"/repos/{REPO}/issues/{number}", {"body": body})
    if status >= 400:
        raise RuntimeError(f"Could not update issue #{number}: HTTP {status} {response}")


def created_number(output: str) -> dict:
    matches = re.findall(rf"https://github\.com/{re.escape(REPO)}/issues/([0-9]+)", output)
    if len(matches) != 1:
        raise RuntimeError(f"Could not read an issue number from gh output; check GitHub before "
                           f"retrying so you do not file a duplicate: {output!r}")
    number = int(matches[0])
    return {"number": number, "url": f"https://github.com/{REPO}/issues/{number}"}


def body_for(item: dict, drift: list[str] | None) -> str:
    text = (BUNDLE / item["body"]).read_text()
    if drift:
        text += ("\n## Drift notice\n\nThis report was filed with `--allow-drift`. Since the audit at "
                 f"`{AUDITED_REVISION[:8]}`, these cited files have changed on `main`:\n\n"
                 + "".join(f"- `{path}`\n" for path in drift)
                 + "\nThe defect may still be present, but **the line numbers above are not "
                   "trustworthy** - re-locate each site before starting.\n")
    return text


def publish(items: list[dict], *, allow_drift: bool, ref: str) -> int:
    mode = check_auth()
    print(f"Reaching GitHub via: {'gh CLI' if mode == 'gh' else 'REST API with an environment token'}\n")

    drift = check_drift(items, ref)
    if drift and not allow_drift:
        lines = [f"  {key}: " + ", ".join(paths) for key, paths in sorted(drift.items())]
        raise RuntimeError(
            f"{len(drift)} issue(s) cite files that changed on {ref} since {AUDITED_REVISION[:8]}:\n"
            + "\n".join(lines)
            + "\n\nRe-verify those findings against current main rather than filing stale line "
              "numbers. Use --allow-drift to file anyway with an explicit notice appended, or "
              "--skip to leave them out.")

    remote = remote_issues()
    matches = {item["key"]: existing_issue(item, remote) for item in items}

    state = load_results()
    state.setdefault("issues", {})
    created = reused = 0
    for item in items:
        key = item["key"]
        if key in state["issues"] and not state["issues"][key].get("failed"):
            print(f"already filed  {key}  {state['issues'][key]['url']}", flush=True)
            continue
        match = matches[key]
        if match is not None:
            record = {"number": match["number"], "url": match["html_url"], "reused": True}
            reused += 1
        else:
            record = create_issue(item["title"], body_for(item, drift.get(key)))
            record["priority"] = item["priority"]
            if key in drift:
                record["filed_with_drift"] = drift[key]
            created += 1
        state["issues"][key] = record
        save_results(state)
        print(f"{'reused ' if record.get('reused') else 'created'}  {key}  {record['url']}", flush=True)

    print(f"\n{created} created, {reused} already existed. Recorded in {RESULTS.name}.")
    return 0


def link_references(items: list[dict]) -> int:
    """Rewrite each filed body so references to other issue keys carry their numbers."""
    state = load_results()
    numbers = {key: record["number"] for key, record in state.get("issues", {}).items()
               if not record.get("failed")}
    if not numbers:
        raise RuntimeError(f"No filed issues recorded in {RESULTS.name}; publish first.")

    updated = 0
    for item in items:
        key = item["key"]
        if key not in numbers:
            continue
        text = body_for(item, state["issues"][key].get("filed_with_drift"))
        replaced = text
        for other, number in numbers.items():
            if other == key:
                continue
            # Only touch a backticked key that is not already followed by its number.
            replaced = re.sub(rf"`{re.escape(other)}`(?!\s*\(#)", f"`{other}` (#{number})", replaced)
        if replaced == text:
            continue
        update_issue_body(numbers[key], replaced)
        updated += 1
        print(f"linked   {key}  #{numbers[key]}", flush=True)

    print(f"\n{updated} issue(s) updated with cross-reference numbers.")
    return 0


def offline_plan(items: list[dict]) -> int:
    print(f"OFFLINE PLAN - no GitHub reads or writes. Repository: {REPO}")
    print(f"Audited revision: {AUDITED_REVISION}")
    print(f"{len(items)} issue(s) would be filed:\n")
    for item in items:
        print(f"  [{item['priority']}] {item['title']}")
        print(f"        key={item['key']}  body={item['body']}  cites={len(item.get('sources', {}))} file(s)")
    print("\nNothing has been contacted. Run with --check for an online preflight "
          "(drift and duplicates, still no writes), then --publish to file.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--publish", action="store_true",
                        help="Explicitly enable GitHub writes. Without this nothing is created.")
    parser.add_argument("--check", action="store_true",
                        help="Online preflight only: report drift and existing matches, write nothing.")
    parser.add_argument("--link-references", action="store_true",
                        help="Second pass over already-filed issues, adding #numbers to key references.")
    parser.add_argument("--allow-drift", action="store_true",
                        help="File even when cited files changed, appending a drift notice to the body.")
    parser.add_argument("--ref", default="main", help="Branch to compare cited files against (default: main).")
    parser.add_argument("--only", action="append", metavar="KEY", help="File only this key; repeatable.")
    parser.add_argument("--skip", action="append", metavar="KEY", help="Exclude this key; repeatable.")
    args = parser.parse_args()

    result = 0
    try:
        items = list(MANIFEST["issues"])
        known = {item["key"] for item in items}
        for key in (args.only or []) + (args.skip or []):
            if key not in known:
                raise RuntimeError(f"Unknown issue key: {key}")
        if args.only:
            items = [item for item in items if item["key"] in set(args.only)]
        if args.skip:
            items = [item for item in items if item["key"] not in set(args.skip)]
        if not items:
            raise RuntimeError("No issues selected.")

        validate_bundle(items)

        if args.link_references:
            result = link_references(items)
        elif args.check:
            mode = check_auth()
            print(f"Reaching GitHub via: "
                  f"{'gh CLI' if mode == 'gh' else 'REST API with an environment token'}")
            drift = check_drift(items, args.ref)
            remote = remote_issues()
            matches = {item["key"]: existing_issue(item, remote) for item in items}
            print(f"Preflight against {REPO}@{args.ref} - nothing written.\n")
            for item in items:
                key = item["key"]
                notes = []
                if matches[key] is not None:
                    notes.append(f"already open at {matches[key]['html_url']}")
                if key in drift:
                    notes.append("cited files changed: " + ", ".join(drift[key]))
                print(f"  {key}: " + ("; ".join(notes) if notes else "ready"))
            print(f"\n{len(drift)} issue(s) show file drift; "
                  f"{sum(1 for m in matches.values() if m)} already exist.")
        elif args.publish:
            result = publish(items, allow_drift=args.allow_drift, ref=args.ref)
        else:
            result = offline_plan(items)
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        if RESULTS.exists():
            print(f"Check {RESULTS} and GitHub for anything already filed before retrying.",
                  file=sys.stderr)
        result = 1
    return result


if __name__ == "__main__":
    raise SystemExit(main())
