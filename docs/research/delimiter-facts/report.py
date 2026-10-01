#!/usr/bin/env python3
"""Read and verify hosted evidence in cloud; never execute archived programs."""
import hashlib
import json
import os
import pathlib
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

if os.environ.get("GITHUB_ACTIONS") != "true":
    raise SystemExit("Cloud-only report extraction.")
token = os.environ["GH_TOKEN"]
api = "https://api.github.com/repos/buster14a/buster/"
runs = {
    36921566974: "0df37ef91b91c8c444bfbf8002aee93b27c72f99",
    36923086652: "187b463a305ddd80ac26a6e855916b741e677823",
}
headers = {"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json",
           "X-GitHub-Api-Version": "2022-11-28"}
def get(path):
    with urllib.request.urlopen(urllib.request.Request(api + path, headers=headers), timeout=60) as response:
        return json.load(response)
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, hdrs, newurl):
        return None
reader = urllib.request.build_opener(NoRedirect)
all_reports = []
for run_id, expected in runs.items():
    deadline = time.monotonic() + 1800
    while True:
        run = get("actions/runs/" + str(run_id))
        if run["head_sha"] != expected:
            raise RuntimeError("Unexpected run source")
        if run["status"] == "completed":
            break
        if time.monotonic() > deadline:
            raise RuntimeError("Capture deadline")
        time.sleep(30)
    artifacts = get("actions/runs/" + str(run_id) + "/artifacts")["artifacts"]
    if len(artifacts) != 1:
        raise RuntimeError("Expected exactly one capture artifact")
    artifact = artifacts[0]
    if artifact["expired"] or artifact["size_in_bytes"] > 536870912:
        raise RuntimeError("Expired or oversized artifact")
    try:
        reader.open(urllib.request.Request(api + "actions/artifacts/" + str(artifact["id"]) + "/zip",
                                          headers=headers), timeout=60)
    except urllib.error.HTTPError as error:
        if error.code not in (301, 302, 303, 307, 308):
            raise
        location = error.headers["Location"]
    parsed = urllib.parse.urlparse(location)
    if parsed.scheme != "https" or parsed.username or parsed.password:
        raise RuntimeError("Unexpected artifact location")
    archive_path = pathlib.Path(os.environ["RUNNER_TEMP"]) / (str(run_id) + ".zip")
    digest = hashlib.sha256()
    total = 0
    # A fresh request intentionally carries no GitHub Authorization header.
    with urllib.request.urlopen(location, timeout=60) as response, archive_path.open("wb") as output:
        while True:
            chunk = response.read(1048576)
            if not chunk:
                break
            total += len(chunk)
            if total > 536870912:
                raise RuntimeError("Download bound")
            digest.update(chunk)
            output.write(chunk)
    archive_sha = digest.hexdigest()
    if total != artifact["size_in_bytes"] or artifact.get("digest") != "sha256:" + archive_sha:
        raise RuntimeError("Artifact digest/size disagreement")
    with zipfile.ZipFile(archive_path) as archive:
        if sum(row.file_size for row in archive.infolist()) > 2147483648 or archive.testzip() is not None:
            raise RuntimeError("Archive capacity/CRC disagreement")
        names = set(archive.namelist())
        def read_json(name):
            return json.loads(archive.read(name)) if name in names else None
        manifest = read_json("manifest.json")
        if not manifest or manifest["candidate"] != expected:
            raise RuntimeError("Capture manifest source disagreement")
        records = {name: read_json(name) for name in ("manifest.json", "summary.json", "quality.json", "census.json",
                   "timing.json", "commands.json", "host-compiler.json", "baseline-overlay-sources.json", "candidate-overlay-sources.json")}
        hashes = {name: archive.read(name).decode().strip() for name in names if name.endswith(".sha256")}
        logs = {name: archive.read(name).decode(errors="replace") for name in names
                if name.endswith("-self-host.stdout") or name.endswith("-modes.stdout") or name == "source-size.stdout"}
        package = {"run_id": run_id, "head": expected, "conclusion": run["conclusion"],
                   "artifact_id": artifact["id"], "artifact_zip_sha256": archive_sha, "artifact_bytes": total,
                   "records": records, "hashes": hashes, "correctness_logs": logs}
        all_reports.append(package)
    print("DELIMITER_CAPTURE=" + json.dumps(package, separators=(",", ":")), flush=True)
audit = subprocess.check_output(["python3", "tools/new_audit.py",
    "delimiter producer-fact experiment; cloud work counts and negative 2x result",
    "--platform", "GitHub-hosted Ubuntu x86-64; qualified-host acceptance pending"], text=True).strip()
path = pathlib.Path(audit)
if not path.is_file():
    # The helper may print prose beside its canonical relative path.
    matches = list(pathlib.Path("docs/performance-audits").glob("2026-10-01T*.md"))
    path = max(matches, key=lambda p: p.name)
print("DELIMITER_AUDIT=" + json.dumps({"path": str(path), "template": path.read_text()}), flush=True)
