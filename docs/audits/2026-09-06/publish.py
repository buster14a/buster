#!/usr/bin/env python3
"""Publish the audit only with --publish; the default is an offline plan.

No tokens are accepted or stored. GitHub CLI authentication is used by git via
an invocation-local credential helper. The live GitHub path is not validated
in the audit environment; review this script and the draft bodies before use.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import uuid

BUNDLE = Path(__file__).resolve().parent
MANIFEST = json.loads((BUNDLE / "manifest.json").read_text())
REPO = MANIFEST["repository"]
URL = f"https://github.com/{REPO}.git"
ENV = dict(os.environ, GIT_TERMINAL_PROMPT="0")


def command(args: list[str], *, cwd: Path | None = None,
            env: dict[str, str] | None = None, log: Path | None = None,
            check: bool = True) -> subprocess.CompletedProcess[str]:
    if log is not None:
        with log.open("w") as output:
            result = subprocess.run(args, cwd=cwd, env=env or ENV, text=True,
                                    stdout=output, stderr=subprocess.STDOUT, timeout=900)
    else:
        result = subprocess.run(args, cwd=cwd, env=env or ENV, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=900)
    if check and result.returncode:
        details = f"See {log}" if log is not None else result.stderr.strip()
        raise RuntimeError(f"Command failed ({result.returncode}): {args[0]} {args[1]}\n{details}")
    return result


def git(checkout: Path, *args: str, check: bool = True) -> str:
    # A helper used only for this command: no global credential configuration changes.
    return command(["git", "-C", str(checkout), "-c", "credential.helper=",
                    "-c", "credential.helper=!gh auth git-credential", *args], check=check).stdout.strip()


def api_items(resource: str, expression: str) -> list[dict]:
    # One JSON object per line works across all --paginate result pages.
    output = command(["gh", "api", f"repos/{REPO}/{resource}", "--paginate", "--jq", expression]).stdout
    return [json.loads(line) for line in output.splitlines() if line.strip()]


def existing_issue(item: dict, remote: list[dict]) -> dict | None:
    matches = [entry for entry in remote if item["marker"] in (entry.get("body") or "")
               or entry["title"] == item["title"]]
    if len(matches) > 1:
        raise RuntimeError(f"More than one matching issue for {item['key']}; review duplicates first.")
    result = matches[0] if matches else None
    if result is not None and result["state"] != "open":
        raise RuntimeError(f"Matching issue is closed; review before publishing: {result['html_url']}")
    return result


def existing_pr(item: dict, remote: list[dict]) -> dict | None:
    matches = [entry for entry in remote
               if entry.get("head_repo") == REPO and entry.get("head_ref") == item["branch"]]
    if len(matches) > 1:
        raise RuntimeError(f"Multiple PRs use {item['branch']}; review them first.")
    result = matches[0] if matches else None
    if result is not None:
        if result["state"] != "open" or item["marker"] not in (result.get("body") or ""):
            raise RuntimeError(f"Existing PR is closed or not this audit draft: {result['html_url']}")
    return result


def validate_bundle() -> None:
    for pr in MANIFEST["pull_requests"]:
        actual = hashlib.sha256((BUNDLE / pr["patch"]).read_bytes()).hexdigest()
        if actual != pr["patch_sha256"]:
            raise RuntimeError(f"Patch hash differs from manifest: {pr['patch']}")
        if pr["marker"] not in (BUNDLE / pr["body"]).read_text():
            raise RuntimeError(f"Missing audit marker: {pr['body']}")
    for item in MANIFEST["issues"]:
        if item["marker"] not in (BUNDLE / item["body"]).read_text():
            raise RuntimeError(f"Missing audit marker: {item['body']}")


def published_item(output: str, kind: str) -> dict:
    pattern = rf"https://github\.com/{re.escape(REPO)}/{kind}/([0-9]+)"
    matches = re.findall(pattern, output)
    if len(matches) != 1:
        raise RuntimeError(f"Unexpected gh output; inspect GitHub before retrying: {output!r}")
    number = int(matches[0])
    return {"number": number, "url": f"https://github.com/{REPO}/{kind}/{number}"}


def save_state(state: dict) -> None:
    target = BUNDLE / "publication-result.json"
    temporary = target.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, indent=2) + "\n")
    temporary.replace(target)


def publish(checkout: Path) -> None:
    if sys.platform != "linux":
        raise RuntimeError("The included focused regression runners require a Linux host.")
    for tool in ("git", "gh", "clang", "node"):
        if shutil.which(tool) is None:
            raise RuntimeError(f"Required command not found: {tool}")
    command(["gh", "auth", "status", "--hostname", "github.com"])
    git(checkout, "rev-parse", "--show-toplevel")
    git(checkout, "var", "GIT_AUTHOR_IDENT")  # Require the invoking user's configured Git identity.
    git(checkout, "var", "GIT_COMMITTER_IDENT")
    issues = api_items("issues?state=all&per_page=100",
                       '.[] | select(.pull_request == null) | {number,title,state,body,html_url}')
    prs = api_items("pulls?state=all&per_page=100",
                    '.[] | {number,title,state,body,html_url,head_ref:.head.ref,head_repo:.head.repo.full_name}')
    issue_matches = {item["key"]: existing_issue(item, issues) for item in MANIFEST["issues"]}
    pr_matches = {item["key"]: existing_pr(item, prs) for item in MANIFEST["pull_requests"]}
    for pr in MANIFEST["pull_requests"]:
        if pr_matches[pr["key"]] is None:
            remote_ref = git(checkout, "ls-remote", "--heads", URL, f"refs/heads/{pr['branch']}")
            if remote_ref:
                raise RuntimeError(f"Branch exists without a matching open audit PR: {pr['branch']}. "
                                   "Review it manually; this helper never overwrites branches.")

    state = {"repository": REPO, "issues": {}, "pull_requests": {}, "pushed_branches": []}
    worktrees: list[Path] = []
    temporary_ref = f"refs/buster-audit/{uuid.uuid4().hex}/main"
    logs = BUNDLE / "publication-logs"
    logs.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="buster-audit-publication-") as directory:
        root = Path(directory)
        try:
            git(checkout, "fetch", "--no-tags", "--no-write-fetch-head", URL,
                f"refs/heads/main:{temporary_ref}")
            base = git(checkout, "rev-parse", temporary_ref)
            state["base"] = base
            candidates = {}
            # Every patch must apply and pass before the first remote write.
            for pr in MANIFEST["pull_requests"]:
                for path, expected in pr["production_preimages"].items():
                    actual = git(checkout, "rev-parse", f"{base}:{path}")
                    if actual != expected:
                        raise RuntimeError(f"Current main changed {path}; re-audit/rebase instead of publishing stale findings.")
                worktree = root / pr["key"]
                git(checkout, "worktree", "add", "--detach", str(worktree), base)
                worktrees.append(worktree)
                git(worktree, "apply", "--check", str(BUNDLE / pr["patch"]))
                git(worktree, "apply", str(BUNDLE / pr["patch"]))
                git(worktree, "diff", "--check")
                env = dict(ENV, CC="clang", AUDIT_OPT="-O2", AUDIT_SANITIZE="1")
                command([sys.executable, pr["test"]], cwd=worktree, env=env,
                        log=logs / f"{pr['key']}.log")
                git(worktree, "add", "--all")
                git(worktree, "commit", "-m", pr["title"])
                candidates[pr["key"]] = worktree

            save_state(state)
            for item in MANIFEST["issues"]:
                match = issue_matches[item["key"]]
                if match is None:
                    output = command(["gh", "issue", "create", "--repo", REPO, "--title", item["title"],
                                      "--body-file", str(BUNDLE / item["body"])]).stdout
                    record = published_item(output, "issues")
                else:
                    record = {"number": match["number"], "url": match["html_url"], "reused": True}
                state["issues"][item["key"]] = record
                save_state(state)
                print(record["url"], flush=True)

            for pr in MANIFEST["pull_requests"]:
                match = pr_matches[pr["key"]]
                if match is not None:
                    record = {"number": match["number"], "url": match["html_url"], "reused": True}
                else:
                    worktree = candidates[pr["key"]]
                    # Recheck before pushing. No force push, even after a previous partial failure.
                    if git(checkout, "ls-remote", "--heads", URL, f"refs/heads/{pr['branch']}"):
                        raise RuntimeError(f"Branch appeared during validation: {pr['branch']}; review it manually.")
                    git(worktree, "push", URL, f"HEAD:refs/heads/{pr['branch']}")
                    state["pushed_branches"].append(pr["branch"])
                    save_state(state)
                    body = (BUNDLE / pr["body"]).read_text()
                    for key, issue in state["issues"].items():
                        body = body.replace("{{issue:" + key + "}}", "#" + str(issue["number"]))
                    if "{{issue:" in body:
                        raise RuntimeError("Unresolved issue placeholder; refusing to create an incomplete PR.")
                    body += (f"\n## Publication preflight\n\nApplied onto GitHub main `{base}` in an isolated worktree; "
                             "the included focused Clang -O2 ASan/UBSan regression passed before publication. "
                             "This is not a full test_all/self-host result.\n")
                    body_path = root / f"{pr['key']}-body.md"
                    body_path.write_text(body)
                    output = command(["gh", "pr", "create", "--repo", REPO, "--base", "main", "--head", pr["branch"],
                                      "--draft", "--title", pr["title"], "--body-file", str(body_path)]).stdout
                    record = published_item(output, "pull")
                state["pull_requests"][pr["key"]] = record
                save_state(state)
                print(record["url"], flush=True)
        finally:
            # Only worktrees and refs created by this invocation are removed.
            for worktree in reversed(worktrees):
                git(checkout, "worktree", "remove", "--force", str(worktree), check=False)
            git(checkout, "update-ref", "-d", temporary_ref, check=False)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", type=Path, help="Existing local Git checkout; working files are not modified")
    parser.add_argument("--publish", action="store_true", help="Explicitly enable GitHub writes after preflight")
    args = parser.parse_args()
    result = 0
    try:
        validate_bundle()
        if not args.publish:
            print(f"OFFLINE PLAN ONLY — no GitHub reads or writes. Repository: {REPO}")
            for item in MANIFEST["issues"]:
                print(f"ISSUE: {item['title']}")
            for pr in MANIFEST["pull_requests"]:
                print(f"DRAFT PR: {pr['title']}\n  branch={pr['branch']}  test={pr['test']}")
            print("Explicit --publish --checkout /path/to/buster is required for any remote action.")
        else:
            if args.checkout is None:
                raise RuntimeError("--publish requires --checkout /path/to/buster")
            publish(args.checkout.expanduser().resolve())
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print(f"ERROR: {error}\nReview publication-result.json and GitHub for any earlier successful writes.", file=sys.stderr)
        result = 1
    return result


if __name__ == "__main__":
    raise SystemExit(main())
