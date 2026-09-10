"""Temporary read-only source/coordination export, never compiler validation."""
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.request

BASE = "ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae"
REPO = "buster14a/buster"
out = Path(os.environ["SNAPSHOT_OUT"])
out.mkdir(parents=True, exist_ok=False)
report = {"baseline": BASE, "pages": [], "errors": []}

def read(path):
    request = urllib.request.Request(
        "https://api.github.com/repos/" + REPO + "/" + path,
        headers={"Authorization": "Bearer " + os.environ["GH_TOKEN"],
                 "Accept": "application/vnd.github+json", "User-Agent": "buster-z06-read-snapshot"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)

def pages(path):
    rows = []
    page = 1
    while True:
        url = path + ("&" if "?" in path else "?") + "per_page=100&page=" + str(page)
        batch = read(url)
        if not isinstance(batch, list):
            raise ValueError("Expected list at " + url)
        report["pages"].append({"path": url, "count": len(batch)})
        rows.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    return rows

def save(name, value):
    (out / name).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")

try:
    report["observed_main"] = read("branches/main")["commit"]["sha"]
    subprocess.run(["git", "archive", "--format=tar.gz", "--output=" + str(out / "source.tar.gz"), BASE], check=True)
    (out / "source.sha").write_text(BASE + "\n", encoding="ascii")
    issues = pages("issues?state=all&sort=created&direction=asc")
    save("issues-all.json", issues)
    report["open_issues"] = [x["number"] for x in issues if x["state"] == "open" and "pull_request" not in x]
    report["open_pull_requests"] = [x["number"] for x in issues if x["state"] == "open" and "pull_request" in x]
    report["unique_records"] = len({x["number"] for x in issues})
    report["records"] = len(issues)
    related = re.compile(r"preprocess|macro.expans|source.?map|include.guard|once_paths|CPpToken|CMacroExpansionTask|CPreprocessTokenNode|c_source[.]c|#51\b|#60\b", re.I)
    report["related"] = []
    for issue in issues:
        number = issue["number"]
        is_pr = "pull_request" in issue
        match = related.search((issue.get("title") or "") + "\n" + (issue.get("body") or ""))
        if number in (46, 51, 60, 81, 104, 105, 128, 147, 258) or match or (is_pr and issue["state"] == "open"):
            report["related"].append(number)
            detail = {"issue": issue}
            try:
                detail["comments"] = pages("issues/" + str(number) + "/comments") if issue["comments"] else []
                if is_pr:
                    prefix = "pulls/" + str(number)
                    detail["pull_request"] = read(prefix)
                    detail["files"] = pages(prefix + "/files")
                    detail["review_comments"] = pages(prefix + "/comments")
                    detail["reviews"] = pages(prefix + "/reviews")
                save("discussion-" + str(number) + ".json", detail)
            except Exception as error:
                report["errors"].append({"number": number, "error": str(error)})
                save("discussion-" + str(number) + "-partial.json", detail)
    paths = ["src/buster/lib/compiler/frontend/c/c_source.c", "src/buster/tests/compiler/frontend/c/c_test.c", "tools/throughput", "docs/agents", "PERFORMANCE_AUDITS.md"]
    with (out / "recent-history.txt").open("w", encoding="utf-8") as log:
        subprocess.run(["git", "log", "-80", "--format=fuller", "--stat", BASE, "--"] + paths, stdout=log, check=True)
    with (out / "recent-preprocessor-patches.txt").open("w", encoding="utf-8") as log:
        subprocess.run(["git", "log", "-12", "--format=fuller", "-p", BASE, "--", paths[0]], stdout=log, check=True)
    with (out / "hardware.txt").open("w", encoding="utf-8") as log:
        subprocess.run(["lscpu"], stdout=log, check=True)
except Exception as error:
    report["errors"].append({"stage": "snapshot", "error": str(error)})
finally:
    save("coverage.json", report)
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
        summary.write("Read-only source/coordination snapshot. No compilation, tests, or performance measurements.\n\n")
        summary.write("Baseline: `" + BASE + "`. Retrieval errors: " + str(len(report["errors"])) + ".\n")
if report["errors"]:
    raise SystemExit(1)
