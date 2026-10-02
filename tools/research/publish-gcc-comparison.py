"""Publish frozen compiler-comparison evidence without executing compiler output."""
import base64
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import statistics
import subprocess
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT = Path("docs/research/buster-gcc-comparison")
TOKEN = os.environ["GITHUB_TOKEN"]
REPO = os.environ["GITHUB_REPOSITORY"]
BRANCH = os.environ["GITHUB_REF_NAME"]
assert REPO == "buster14a/buster"
assert BRANCH == "codex/2274-publish-gcc-comparison"
CONFIG = json.loads(Path("tools/research/gcc-comparison-publication-inputs.json").read_text())
API = "https://api.github.com/repos/" + REPO + "/"
HEADERS = {"Authorization": "Bearer " + TOKEN, "Accept": "application/vnd.github+json",
           "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "buster-comparison-publication"}
def digest(data):
    return hashlib.sha256(data).hexdigest()
def api(path, data=None, method=None):
    body = json.dumps(data).encode() if data is not None else None
    request = urllib.request.Request(API + path, data=body, headers=HEADERS, method=method)
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None
def artifact_bytes(artifact_id):
    request = urllib.request.Request(API + "actions/artifacts/" + str(artifact_id) + "/zip", headers=HEADERS)
    opener = urllib.request.build_opener(NoRedirect)
    try:
        with opener.open(request, timeout=60) as response:
            return response.read()
    except urllib.error.HTTPError as error:
        if error.code not in (301, 302, 303, 307, 308):
            raise
        location = error.headers["Location"]
        assert urllib.parse.urlparse(location).scheme == "https"
        # The repository credential is never forwarded to the signed download host.
        with urllib.request.urlopen(location, timeout=90) as response:
            return response.read()
def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
def savejson(path, data):
    save(path, (json.dumps(data, indent=2, sort_keys=True) + "\n").encode())
def safe_member(name):
    path = PurePosixPath(name)
    assert not path.is_absolute() and ".." not in path.parts
    return path

outcomes = []
archives = {}
for item in CONFIG["runs"]:
    data = artifact_bytes(item["artifact_id"])
    assert digest(data) == item["sha256"], ("archive hash", item["run_id"])
    assert len(data) == item["bytes"], ("archive size", item["run_id"])
    archive_path = ROOT / "evidence" / item["file_name"]
    save(archive_path, data)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        assert archive.testzip() is None
        names = [name for name in archive.namelist() if not name.endswith("/")]
        if item.get("entries") is not None:
            assert len(names) == item["entries"]
        for name in names:
            path = safe_member(name)
            # Complete binaries and every stdout/stderr/time record remain in the exact ZIP.
            # These copied inputs and JSON tables make the principal data browsable in GitHub.
            if path.suffix in (".json", ".jsonl", ".c", ".sha256", ".expected", ".diff", ".txt", ".d"):
                save(ROOT / "data" / ("run-" + str(item["run_id"])) / path, archive.read(name))
    archives[item["run_id"]] = data
    outcomes.append({**{key: value for key, value in item.items() if key != "url"},
                     "archive_path": archive_path.as_posix(), "crc_verified": True})

delivery = []
for item in CONFIG["reports"]:
    with urllib.request.urlopen(item["url"], timeout=90) as response:
        data = response.read()
    if item.get("required_sha256"):
        assert digest(data) == item["required_sha256"], ("report hash", item["path"])
    if item["path"].endswith(".pdf"):
        assert data.startswith(b"%PDF-")
    save(ROOT / item["path"], data)
    delivery.append({"path": item["path"], "bytes": len(data), "sha256": digest(data),
                     "workspace_sha256": item.get("workspace_sha256"),
                     "matches_workspace_bytes": digest(data) == item.get("workspace_sha256")})

current = ROOT / "data/run-37005475203/gcc-comparison-evidence"
rows = [json.loads(line) for line in (current / "samples.jsonl").read_text().splitlines()]
summary = json.loads((current / "summary.json").read_text())
probes = json.loads((current / "probes.json").read_text())
metadata = json.loads((current / "metadata.json").read_text())
assert len(rows) == 390 and len(summary) == 18 and len(probes) == 56
assert all(row["exit"] == 0 and not row["timed_out"] for row in rows)
assert all(entry["accepted"] and entry["compile_valid"] for entry in summary)
keyed = {row["key"]: row for row in rows}
assert len(keyed) == 390
replayed = 0
paired = []
with zipfile.ZipFile(io.BytesIO(archives[37005475203])) as archive:
    for entry in summary:
        workload, tool = entry["workload"], entry["tool"]
        src = archive.read("gcc-comparison-evidence/" + workload + ".c")
        obj = archive.read("gcc-comparison-evidence/" + workload + "-" + tool + ".o")
        assert digest(src) == entry["input_sha256"]
        assert digest(obj) == entry["output_sha256"]
        for phase in ("compile", "runtime"):
            if not entry.get(phase + "_valid"):
                continue
            group = [keyed[workload + "-" + tool + "-" + phase + "-" + str(i)] for i in range(12)]
            for metric in ("wall_s", "cpu_s", "maxrss_kib"):
                vals = [row[metric] for row in group]
                expected = {"median": statistics.median(vals), "min": min(vals), "max": max(vals)}
                for name, value in expected.items():
                    assert math.isclose(value, entry[phase][metric][name], rel_tol=1e-12, abs_tol=1e-12)
                    replayed += 1
            if tool != "buster-fast":
                ratios = [row["wall_s"] / keyed[workload + "-buster-fast-" + phase + "-" + str(i)]["wall_s"]
                          for i, row in enumerate(group)]
                paired.append({"workload": workload, "comparator": tool, "phase": phase,
                               "paired_gcc_over_buster_wall_ratios": ratios,
                               "median_paired_ratio": statistics.median(ratios),
                               "buster_wins": sum(value > 1 for value in ratios), "pairs": 12})
    objdump = subprocess.run(["objdump", "--version"], text=True, capture_output=True, check=True)
    save(ROOT / "assembly/objdump-version.txt", objdump.stdout.encode())
    with tempfile.TemporaryDirectory() as temporary:
        for workload in ("array_loop", "scalar_float", "many_functions"):
            for tool in ("buster-fast", "gcc-O0", "gcc-O2"):
                name = workload + "-" + tool + ".o"
                path = Path(temporary) / name
                path.write_bytes(archive.read("gcc-comparison-evidence/" + name))
                output = subprocess.run(["objdump", "-dr", str(path)], text=True, capture_output=True, check=True)
                save(ROOT / "assembly" / (name + ".txt"), output.stdout.encode())
fields = sorted({key for row in rows for key in row})
stream = io.StringIO(newline="")
writer = csv.DictWriter(stream, fields)
writer.writeheader()
for row in rows:
    writer.writerow({key: json.dumps(value, sort_keys=True) if isinstance(value, (dict, list)) else value
                     for key, value in row.items()})
save(ROOT / "data/current-samples.csv", stream.getvalue().encode())
savejson(ROOT / "data/paired-comparisons.json", paired)
verification = {"samples": len(rows), "probes": len(probes), "cells": len(summary),
                "all_samples_successful": True, "all_18_source_and_object_hashes_match": True,
                "summary_values_independently_replayed": replayed, "paired_comparisons": len(paired),
                "archives": outcomes, "report_deliveries": delivery,
                "scope": "Artifact integrity and statistic replay only; no compiler build or generated-program execution."}
savejson(ROOT / "verification.json", verification)
# Preserve the exact publisher and workflow along with the already archived experiment source.
save(ROOT / "tooling/publish-evidence.py", Path("tools/research/publish-gcc-comparison.py").read_bytes())
save(ROOT / "tooling/publication-workflow.yml", Path(".github/workflows/gcc-comparison-publication.yml").read_bytes())

created = subprocess.run(["python3", "tools/new_audit.py", "--platform",
                          "GitHub-hosted Linux x86_64; historical EPYC 7763 diagnostics",
                          "Buster versus GCC: preserve complete diagnostic report and three experiment attempts"],
                         text=True, capture_output=True, check=True)
audit_path = Path(created.stdout.strip().splitlines()[-1]).resolve().relative_to(Path.cwd().resolve())
assert audit_path.as_posix().startswith("docs/performance-audits/") and audit_path.exists()
with audit_path.open("a") as output:
    output.write("\nPublication-only follow-up for #2274; no compiler or generated-program execution.\n\n"
                 "Full report and durable raw evidence: [comparison dossier](../research/buster-gcc-comparison/README.md).\n"
                 "The failed setup and rejected polling-based timing attempt remain retained separately.\n"
                 "GCC 15.2 measurements and GCC 16.2 source capabilities remain separate; qualified hardware acceptance is unrun.\n"
                 "Buster first-party license remains unselected; GCC compiler GPL-3.0-or-later and component exceptions are in the report.\n")
head = api("git/ref/heads/" + urllib.parse.quote(BRANCH, safe="/"))["object"]["sha"]
assert head == os.environ["GITHUB_SHA"], "Archival branch changed during publication"
parent = api("git/commits/" + head)
context = json.loads((ROOT / "provenance/publication-context.json").read_text())
files = []
for path in sorted(ROOT.rglob("*")):
    if path.is_file() and path.name not in ("manifest.json", "SHA256SUMS"):
        data = path.read_bytes()
        files.append({"path": path.relative_to(ROOT).as_posix(), "bytes": len(data), "sha256": digest(data),
                      "git_blob_sha": hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()})
savejson(ROOT / "manifest.json", {"schema": 1, "publication_parent": head, "context": context,
                                 "verification": verification, "audit_path": audit_path.as_posix(), "files": files})
checksum_rows = files + [{"path": "manifest.json", "sha256": digest((ROOT / "manifest.json").read_bytes())}]
save(ROOT / "SHA256SUMS", "".join(row["sha256"] + "  " + row["path"] + "\n" for row in checksum_rows).encode())
# One bounded, non-forced publication commit on the claimed archival branch.
entries = []
for path in sorted([path for path in ROOT.rglob("*") if path.is_file()] + [audit_path]):
    data = path.read_bytes()
    blob = api("git/blobs", {"content": base64.b64encode(data).decode(), "encoding": "base64"})
    expected_blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    assert blob["sha"] == expected_blob
    entries.append({"path": path.as_posix(), "mode": "100644", "type": "blob", "sha": blob["sha"]})
tree = api("git/trees", {"base_tree": parent["tree"]["sha"], "tree": entries})
commit = api("git/commits", {"message": "research: archive GCC comparison reports, raw data and rejected evidence",
                           "tree": tree["sha"], "parents": [head]})
assert api("git/ref/heads/" + urllib.parse.quote(BRANCH, safe="/"))["object"]["sha"] == head
api("git/refs/heads/" + urllib.parse.quote(BRANCH, safe="/"), {"sha": commit["sha"], "force": False}, "PATCH")
print(json.dumps({"publication_commit": commit["sha"], "payload_files": len(files),
                  "git_files": len(entries), "verification": verification}, indent=2))
