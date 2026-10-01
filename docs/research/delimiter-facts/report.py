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
        sources = read_json("sources.json")
        if not sources:
            raise RuntimeError("Missing frozen-source manifest")
        for name, expected_sha in sources.items():
            if hashlib.sha256(archive.read("frozen/" + name)).hexdigest() != expected_sha:
                raise RuntimeError("Frozen source hash disagreement: " + name)
        records["sources.json"] = sources
        records["build-details.json"] = {}
        for arm in ("baseline", "candidate"):
            compile_rows = read_json(arm + "-compile_commands.json")
            records["build-details.json"][arm] = {
                "ide_compile": [row for row in compile_rows if row["file"].endswith("/src/buster/apps/ide/ide.c")],
                "cmake_c_flags": [line for line in archive.read(arm + "-CMakeCache.txt").decode().splitlines()
                                  if line.startswith("CMAKE_C_FLAGS")]}
        records["host-details.json"] = {name: archive.read(name).decode() for name in
            ("clang.stdout", "cmake.stdout", "ninja.stdout", "uname.stdout", "lscpu.stdout")}
        hashes = {name: archive.read(name).decode().strip() for name in names if name.endswith(".sha256")}
        binaries = {"driver.sha256": "build-driver", "baseline-binary.sha256": "baseline-ide",
                    "candidate-binary.sha256": "candidate-ide", "baseline-census-binary.sha256": "baseline-census-ide",
                    "candidate-census-binary.sha256": "candidate-census-ide"}
        for hash_name, binary_name in binaries.items():
            if hash_name not in hashes and run_id == 36921566974:
                continue  # Preliminary capture did not record the driver hash.
            if hashlib.sha256(archive.read(binary_name)).hexdigest() != hashes[hash_name]:
                raise RuntimeError("Binary hash disagreement: " + binary_name)
        records["census.json"] = [dict(row, metrics={key: value for key, value in row["metrics"].items()
            if key.startswith("work.delimiter.") or key in ("preprocessed.tokens", "allocation.arena_calls",
                                                          "allocation.arena_bytes")}) for row in records["census.json"]]
        logs = {name: archive.read(name).decode(errors="replace") for name in names
                if name.endswith("-self-host.stdout") or name.endswith("-modes.stdout") or name == "source-size.stdout"}
        logs = {name: "\n".join(line for line in data.splitlines() if
            "SELF_HOST fixed_point" in line or "Self-host " in line or "MODE_MATRIX" in line or
            "SOURCE_SIZE_RESULT_V1" in line or "SOURCE_SIZE_RATCHET_V1" in line) + "\n"
            for name, data in logs.items()}
        package = {"source_manifest_verified": True, "archived_binaries_verified": True, "run_id": run_id, "head": expected, "conclusion": run["conclusion"],
                   "artifact_id": artifact["id"], "artifact_zip_sha256": archive_sha, "artifact_bytes": total,
                   "records": records, "hashes": hashes, "correctness_logs": logs}
        all_reports.append(package)
    print("DELIMITER_CAPTURE=" + json.dumps(package, separators=(",", ":")), flush=True)
checked = subprocess.run(["git", "diff", "--check", "c5a073139691e9111986c4fca6f6b03d2a6dfcf1", "HEAD"],
                         text=True, capture_output=True, check=False)
print("DELIMITER_DIFF_CHECK=" + json.dumps({"status": checked.returncode, "stdout": checked.stdout,
                                          "stderr": checked.stderr}), flush=True)
if checked.returncode:
    raise SystemExit(checked.returncode)
