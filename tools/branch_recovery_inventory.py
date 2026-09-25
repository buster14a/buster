"""One-shot, read-only GitHub branch recovery inventory; never executes branch code."""
from __future__ import annotations
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

REPO = "buster14a/buster"
PINNED_MAIN = "d9e736e2cf08804a2c603d153e9fb608f18c5c49"
OUT = Path(sys.argv[1])
OUT.mkdir(parents=True, exist_ok=True)


def git(*args: str, binary: bool = False, check: bool = True):
    result = subprocess.run(["git", "-c", "core.hooksPath=/dev/null", *args],
                            check=check, capture_output=True, timeout=240)
    value = result.stdout if binary else result.stdout.decode("utf-8", "replace").strip()
    return value


def api(path: str):
    value = None
    for attempt in range(3):
        request = urllib.request.Request("https://api.github.com/repos/" + REPO + path,
            headers={"Authorization": "Bearer " + os.environ["GH_TOKEN"],
                     "Accept": "application/vnd.github+json", "User-Agent": "buster-branch-recovery",
                     "X-GitHub-Api-Version": "2022-11-28"})
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                value = json.load(response)
            break
        except urllib.error.HTTPError as exc:
            if exc.code not in (429, 500, 502, 503, 504) or attempt == 2:
                raise
            time.sleep(2 ** attempt)
        except (TimeoutError, urllib.error.URLError):
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)
    return value


def pages(path: str, key: str | None = None):
    result = []
    for page in range(1, 101):
        data = api(path + ("&" if "?" in path else "?") + f"per_page=100&page={page}")
        batch = data[key] if key else data
        if not isinstance(batch, list):
            raise ValueError("Unexpected API page")
        result.extend(batch)
        if len(batch) < 100:
            break
    else:
        raise RuntimeError("Pagination did not terminate")
    return result


def save(name: str, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


started = dt.datetime.now(dt.timezone.utc).isoformat()
# Public fetch; no repository code, hooks, build, benchmark or candidate executable is run.
git("fetch", "--prune", "--no-tags", "origin", "+refs/heads/*:refs/remotes/origin/*")
metadata = api("")
branch_api = {b["name"]: b for b in pages("/branches")}
all_branches = []
for line in git("for-each-ref", "--format=%(refname:strip=3)%00%(objectname)%00%(committerdate:iso-strict)%00%(committerdate:unix)%00%(tree)%00%(subject)%00%(symref)", "refs/remotes/origin/").splitlines():
    name, sha, date, epoch, tree, subject, symref = line.split("\0", 6)
    if symref:
        continue
    item = branch_api.get(name)
    all_branches.append({"name": name, "sha": sha, "committer_date": date,
        "epoch": int(epoch), "tree": tree, "subject": subject,
        "protected": item["protected"] if item else None,
        "api_sha": item["commit"]["sha"] if item else None,
        "snapshot_matches_api": bool(item and item["commit"]["sha"] == sha)})
all_branches.sort(key=lambda b: (b["epoch"], b["name"]))
selected = all_branches[:100]
assert len(selected) == 100
selected_names = {b["name"] for b in selected}
main = git("rev-parse", "refs/remotes/origin/" + metadata["default_branch"])
main_tree = git("rev-parse", main + "^{tree}")
prs = pages("/pulls?state=all&sort=created&direction=asc")
pr_data = [{k: p.get(k) for k in ("number", "title", "state", "draft", "body", "html_url", "created_at", "updated_at", "closed_at", "merged_at", "merge_commit_sha")} | {
    "head_ref": p["head"]["ref"], "head_sha": p["head"]["sha"],
    "head_repo": (p["head"].get("repo") or {}).get("full_name"),
    "base_ref": p["base"]["ref"], "base_sha": p["base"]["sha"],
    "base_repo": (p["base"].get("repo") or {}).get("full_name")}
    for p in prs]
issues = [{k: i.get(k) for k in ("number", "title", "state", "body", "html_url", "updated_at", "closed_at", "state_reason")}
    for i in pages("/issues?state=all&sort=created&direction=asc") if "pull_request" not in i]
runs = []
for status in ("queued", "in_progress", "waiting", "requested", "pending"):
    runs.extend({k: r.get(k) for k in ("id", "name", "status", "head_branch", "head_sha", "html_url", "event")}
        for r in pages("/actions/runs?status=" + status, "workflow_runs"))
runs = list({r["id"]: r for r in runs}.values())
try:
    rulesets = pages("/rulesets?includes_parents=true")
except urllib.error.HTTPError as exc:
    rulesets = {"unavailable_http_status": exc.code}

save("all-branches.json", all_branches)
save("pull-requests.json", pr_data)
save("issues.json", issues)
save("active-runs.json", runs)
save("rulesets.json", rulesets)
(OUT / "branches").mkdir(exist_ok=True)
for rank, b in enumerate(selected, 1):
    b["rank"] = rank
    sha = b["sha"]
    prefix = OUT / "branches" / f"{rank:03d}"
    b["associated_prs"] = [p["number"] for p in pr_data if p["head_repo"] == REPO and p["head_ref"] == b["name"]]
    b["open_pr_heads"] = [p["number"] for p in pr_data if p["state"] == "open" and p["head_repo"] == REPO and p["head_ref"] == b["name"]]
    b["open_pr_bases"] = [p["number"] for p in pr_data if p["state"] == "open" and p["base_repo"] == REPO and p["base_ref"] == b["name"]]
    b["active_runs"] = [r["id"] for r in runs if r["head_branch"] == b["name"]]
    b["merge_bases"] = git("merge-base", "--all", main, sha, check=False).splitlines()
    b["counts_behind_ahead"] = [int(v) for v in git("rev-list", "--left-right", "--count", main + "..." + sha).split()]
    b["is_ancestor_of_main"] = b["counts_behind_ahead"][1] == 0
    b["retained_containing_branches"] = [r for r in git("for-each-ref", "--contains=" + sha,
        "--format=%(refname:strip=3)", "refs/remotes/origin/").splitlines()
        if r not in selected_names and r != "HEAD"]
    b["holds"] = []
    for condition, reason in ((b["name"] == metadata["default_branch"], "default branch"),
        (b["protected"] is not False, "protected or protection unknown"),
        (not b["snapshot_matches_api"], "ref moved or missing during inventory"),
        (bool(b["open_pr_heads"]), "open PR head"), (bool(b["open_pr_bases"]), "open PR base"),
        (bool(b["active_runs"]), "active workflow run"),
        (b["name"].startswith("archive/"), "intentional archival branch: durable replacement required")):
        if condition:
            b["holds"].append(reason)
    if len(b["merge_bases"]) == 1:
        base = b["merge_bases"][0]
        b["diff_base"] = base
        b["numstat"] = git("diff", "--no-ext-diff", "--no-textconv", "--numstat", base, sha).splitlines()
        (prefix.with_suffix(".patch")).write_bytes(git("diff", "--no-ext-diff", "--no-textconv", "--binary", "--full-index", base, sha, binary=True))
        b["branch_commits"] = git("log", "--no-show-signature", "--format=%H%x09%s", "--reverse", base + ".." + sha).splitlines()
        b["cherry"] = git("cherry", main, sha).splitlines() if not b["is_ancestor_of_main"] else []
    else:
        b["diff_base"] = None
        b["numstat"] = []
        b["branch_commits"] = git("log", "--no-show-signature", "--format=%H%x09%s", "--max-count=100", sha).splitlines()
        b["cherry"] = []
        b["holds"].append("no unique merge base; preserve full history")
    save("branches/" + f"{rank:03d}.json", b)
    print(f"{rank:03d} {b['committer_date']} {sha} {b['name']} ahead={b['counts_behind_ahead'][1]} holds={b['holds']}", flush=True)

save("inventory.json", {"repository": REPO, "started_utc": started,
    "finished_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
    "ranking": "tip commit committer epoch ascending, then branch name; NOT ref last-push time",
    "observed_branch_count": len(all_branches), "api_branch_count": len(branch_api),
    "initial_main": PINNED_MAIN, "comparison_main": main, "comparison_main_tree": main_tree,
    "workflow_sha": os.environ.get("GITHUB_SHA"), "run_id": os.environ.get("GITHUB_RUN_ID"),
    "read_only": True, "deletions": [], "selected": selected})
refs = ["refs/remotes/origin/" + b["name"] for b in selected]
refs.append("refs/remotes/origin/" + metadata["default_branch"])
git("bundle", "create", str((OUT / "recovery.bundle").resolve()), *refs)
(OUT / "bundle-verification.txt").write_text(git("bundle", "verify", str((OUT / "recovery.bundle").resolve())), encoding="utf-8")
lines = ["# Oldest 100 branch recovery inventory", "", f"Main: `{main}`; tree: `{main_tree}`.",
    f"Ranked {len(all_branches)} branches by tip-commit committer date (not last push time).", "", "Read-only inventory. No branches deleted; no compiler workloads executed.", "",
    "| Rank | Commit date | Branch | Head | Ahead | PRs | Holds |", "|---:|---|---|---|---:|---|---|"]
for b in selected:
    lines.append(f"| {b['rank']} | {b['committer_date']} | `{b['name']}` | `{b['sha']}` | {b['counts_behind_ahead'][1]} | {b['associated_prs']} | {'; '.join(b['holds'])} |")
(OUT / "REPORT.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
if os.environ.get("GITHUB_STEP_SUMMARY"):
    Path(os.environ["GITHUB_STEP_SUMMARY"]).write_text("\n".join(lines) + "\n", encoding="utf-8")
checksums = []
for path in sorted(OUT.rglob("*")):
    if path.is_file():
        checksums.append(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + str(path.relative_to(OUT)))
(OUT / "SHA256SUMS").write_text("\n".join(checksums) + "\n", encoding="utf-8")
