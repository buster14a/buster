#!/usr/bin/env python3
"""Read the exact #1912 hosted artifact, without executing retained binaries."""
import hashlib
import io
import json
import os
from pathlib import Path
import urllib.error
import urllib.request
import zipfile


ARTIFACT = 11235490942
ARCHIVE_SHA256 = "634810c4bdee77100692f41ac3822950caf618a8a52f3d602e4367020cb3781e"
SOURCE = "9af21c5cfb2911dd46d508e58f6f9c1c9c99628c"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file, code, message, headers, url):
        return None


def main():
    if os.environ.get("GITHUB_ACTIONS") != "true" or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise RuntimeError("Requires a disposable GitHub-hosted runner")
    output = Path(os.environ["RUNNER_TEMP"]) / "compact-slot-evidence"
    output.mkdir(exist_ok=False)
    request = urllib.request.Request(
        f"https://api.github.com/repos/buster14a/buster/actions/artifacts/{ARTIFACT}/zip",
        headers={"Authorization": "Bearer " + os.environ["GH_TOKEN"],
                 "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"})
    opener = urllib.request.build_opener(NoRedirect())
    try:
        response = opener.open(request, timeout=60)
        data = response.read()
    except urllib.error.HTTPError as error:
        if error.code != 302:
            raise
        location = error.headers["Location"]
        if not location.startswith("https://"):
            raise RuntimeError("Artifact redirect must use HTTPS")
        # Follow the signed object URL without forwarding the GitHub token.
        with urllib.request.urlopen(location, timeout=60) as response:
            data = response.read()
    if hashlib.sha256(data).hexdigest() != ARCHIVE_SHA256:
        raise RuntimeError("Artifact ZIP hash mismatch")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        summary = json.loads(archive.read("summary.json"))
        if summary["head"] != SOURCE or summary["acceptance"]:
            raise RuntimeError("Artifact source/acceptance mismatch")
        for label, build in summary["builds"].items():
            actual = hashlib.sha256(archive.read(label + "-ide")).hexdigest()
            if actual != build["binary_sha256"]:
                raise RuntimeError("Retained compiler hash mismatch: " + label)
        for entry in archive.infolist():
            name = entry.filename
            if "/" not in name and name.endswith((".json", ".jsonl", ".log", ".rss", ".txt", ".c", ".cachegrind")):
                (output / name).write_bytes(archive.read(entry))
        print(json.dumps({"source": SOURCE, "artifact": ARTIFACT, "zip_sha256": ARCHIVE_SHA256,
                          "builds": summary["builds"], "valgrind_copyright_sha256": summary.get("valgrind_copyright_sha256")}), flush=True)
        for name, workload in summary["workloads"].items():
            print(json.dumps({"workload": name, "removed_work": workload["removed_work"],
                              "hashes": workload["hashes"],
                              "population_count": {k: len(v) for k, v in workload["populations"].items()},
                              "positive_interval_count": {k: sum(row[2] != 0 for row in v) for k, v in workload["intervals"].items()},
                              "timings": workload["timings"], "cachegrind": workload["cachegrind"]}), flush=True)
            for variant in ("baseline", "candidate"):
                log = archive.read(f"{name}-{variant}-cachegrind.log").decode(errors="replace")
                print(name, variant, "CACHE_FAILURE_LOG", log[-10000:], flush=True)
        print("ORIGINAL_IDENTITY", archive.read("identity.log").decode(errors="replace"), flush=True)
        print("ORIGINAL_VALGRIND_COPYRIGHT", archive.read("valgrind-copyright.txt").decode(errors="replace"), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
