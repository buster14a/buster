#!/usr/bin/env python3
"""Verify and copy original SSA measurement text; never run a compiler or trial."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

REPOSITORY = "buster14a/buster"
SOURCE_RUN_ID = 37116754332
SOURCE_ATTEMPT = 1
SOURCE_HEAD_SHA = "0a5dd27e9b53864edd9ba23071614b85668546ed"
SOURCE_CANDIDATE_SHA = "159c9a3096147c89698f0eca913bf2791da2acf5"
SOURCE_ARTIFACT_NAME = "ssa-cloud-perf-37116754332-1"
API_ROOT = "https://api.github.com/repos/" + REPOSITORY
TEXT_SUFFIXES = {".csv", ".json", ".jsonl", ".md", ".txt", ".log", ".tsv", ".metrics", ".cmake", ".c", ".h", ".sha256"}
CONFIG_NAMES = {"CMakeCache.txt", "compile_commands.json"}


class EvidenceError(Exception):
    pass


class StripRedirectAuthorization(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        if urllib.parse.urlsplit(new_url).scheme != "https":
            raise EvidenceError("artifact download refused a non-HTTPS redirect")
        redirected = super().redirect_request(request, response, code, message, headers, new_url)
        if redirected is not None:
            # urllib normally carries request headers to redirect targets.
            # Authorization must never reach a signed storage URL, even if
            # an intermediate redirect remains on the API host.
            for name in tuple(redirected.headers):
                if name.lower() == "authorization":
                    del redirected.headers[name]
            for name in tuple(redirected.unredirected_hdrs):
                if name.lower() == "authorization":
                    del redirected.unredirected_hdrs[name]
        return redirected


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def api_request(path, token):
    return urllib.request.Request(API_ROOT + path, headers={
        "Authorization": "Bearer " + token,
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "buster-immutable-ssa-evidence-extractor",
    })


def api_json(opener, path, token):
    with opener.open(api_request(path, token), timeout=60) as response:
        return json.load(response)


def publish_step_summary(text):
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with Path(summary).open("a", encoding="utf-8") as stream:
            stream.write(text + "\n")


def select_artifact(opener, token):
    # The attempt endpoint pins first-attempt completion even if someone later
    # reruns the original workflow. The artifact name also pins attempt one.
    run = api_json(opener, f"/actions/runs/{SOURCE_RUN_ID}/attempts/{SOURCE_ATTEMPT}", token)
    require(run.get("id") == SOURCE_RUN_ID and run.get("run_attempt") == SOURCE_ATTEMPT,
            "original workflow run/attempt identity mismatch")
    require(run.get("head_sha") == SOURCE_HEAD_SHA, "original workflow head identity mismatch")
    require((run.get("repository") or {}).get("full_name") == REPOSITORY,
            "original workflow repository identity mismatch")
    require(run.get("status") == "completed", "original workflow attempt is not complete")
    matches = []
    page = 1
    while True:
        listing = api_json(opener, f"/actions/runs/{SOURCE_RUN_ID}/artifacts?per_page=100&page={page}", token)
        artifacts = listing.get("artifacts")
        require(isinstance(artifacts, list), "original artifact listing is malformed")
        matches.extend(item for item in artifacts if item.get("name") == SOURCE_ARTIFACT_NAME)
        if len(artifacts) < 100:
            break
        page += 1
    require(len(matches) == 1, "expected exactly one original named artifact")
    identifier = matches[0].get("id")
    require(isinstance(identifier, int) and identifier > 0, "original artifact ID is malformed")
    artifact = api_json(opener, f"/actions/artifacts/{identifier}", token)
    workflow_run = artifact.get("workflow_run") or {}
    require(artifact.get("id") == identifier and artifact.get("name") == SOURCE_ARTIFACT_NAME,
            "original artifact identity changed")
    require(workflow_run.get("id") == SOURCE_RUN_ID and workflow_run.get("head_sha") == SOURCE_HEAD_SHA,
            "original artifact workflow identity mismatch")
    require(artifact.get("expired") is False, "original artifact is expired")
    require(isinstance(artifact.get("size_in_bytes"), int) and artifact["size_in_bytes"] > 0,
            "original artifact size metadata is malformed")
    digest = artifact.get("digest")
    require(isinstance(digest, str) and re.fullmatch(r"sha256:[0-9a-fA-F]{64}", digest),
            "original artifact has no valid SHA256 metadata digest")
    return run, artifact


def download_archive(opener, artifact, token, destination):
    sha = hashlib.sha256()
    size = 0
    deadline = time.monotonic() + 900
    with opener.open(api_request(f"/actions/artifacts/{artifact['id']}/zip", token), timeout=60) as response:
        with destination.open("wb") as stream:
            while True:
                require(time.monotonic() <= deadline, "artifact download exceeded its fixed deadline")
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                stream.write(chunk)
                sha.update(chunk)
                size += len(chunk)
    actual_digest = sha.hexdigest()
    require(actual_digest == artifact["digest"].split(":", 1)[1].lower(),
            "downloaded ZIP SHA256 differs from original artifact metadata")
    require(size == artifact["size_in_bytes"], "downloaded ZIP size differs from original artifact metadata")
    return actual_digest, size


def extract_text(archive, output):
    retained = []
    excluded = []
    names = set()
    with zipfile.ZipFile(archive) as bundle:
        for member in bundle.infolist():
            path = PurePosixPath(member.filename)
            require(member.filename not in names, "original ZIP contains duplicate member names")
            names.add(member.filename)
            require(not path.is_absolute() and ".." not in path.parts and "\\" not in member.filename,
                    "original ZIP contains an unsafe member path")
            if member.is_dir():
                continue
            require(not stat.S_ISLNK(member.external_attr >> 16), "original ZIP contains a symbolic link")
            selected = path.suffix.lower() in TEXT_SUFFIXES or path.name in CONFIG_NAMES
            if not selected:
                excluded.append({"path": member.filename, "uncompressed_bytes": member.file_size})
                continue
            destination = output.joinpath(*path.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            sha = hashlib.sha256()
            size = 0
            with bundle.open(member) as source, destination.open("xb") as target:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    target.write(chunk)
                    sha.update(chunk)
                    size += len(chunk)
            require(size == member.file_size, "retained ZIP member size mismatch")
            retained.append({"path": member.filename, "bytes": size, "sha256": sha.hexdigest()})
    require(retained, "original artifact contains no selected text evidence")
    return retained, excluded


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(exist_ok=False)
    token = os.environ.get("GH_TOKEN")
    require(token, "read-only GitHub Actions token is missing")
    opener = urllib.request.build_opener(StripRedirectAuthorization())
    run, artifact = select_artifact(opener, token)
    write_json(output / "original-run.json", run)
    # Metadata contains only the stable API archive endpoint; never record a
    # storage redirect or its signed URL. The download request constructs its
    # own API endpoint from the verified numeric artifact ID.
    metadata = {key: value for key, value in artifact.items() if key != "archive_download_url"}
    write_json(output / "original-artifact.json", metadata)
    with tempfile.TemporaryDirectory(prefix="ssa-perf-extract-") as temporary:
        archive = Path(temporary) / "original.zip"
        zip_digest, zip_bytes = download_archive(opener, artifact, token, archive)
        retained, excluded = extract_text(archive, output)
    settings_files = [item["path"] for item in retained if PurePosixPath(item["path"]).name == "predeclared-settings.json"]
    require(len(settings_files) == 1, "original artifact does not contain exactly one declared-settings record")
    settings = json.loads(output.joinpath(*PurePosixPath(settings_files[0]).parts).read_text(encoding="utf-8"))
    require(settings.get("candidate_sha") == SOURCE_CANDIDATE_SHA, "original declared candidate identity mismatch")
    write_json(output / "extraction-receipt.json", {
        "extraction_complete": True,
        "repository": REPOSITORY,
        "original_run_id": SOURCE_RUN_ID,
        "original_run_attempt": SOURCE_ATTEMPT,
        "original_run_head": SOURCE_HEAD_SHA,
        "measured_candidate_head": SOURCE_CANDIDATE_SHA,
        "original_run_conclusion": run.get("conclusion"),
        "original_artifact_id": artifact["id"],
        "original_artifact_name": SOURCE_ARTIFACT_NAME,
        "original_artifact_metadata_digest": artifact["digest"],
        "downloaded_zip_sha256": zip_digest,
        "downloaded_zip_bytes": zip_bytes,
        "retained_files": retained,
        "unselected_members": excluded,
        "selection": "complete original members with declared text/source/config suffixes; no sample or log edits",
        "measurements_rerun": False,
        "original_artifact_deleted": False,
    })
    count = len(retained)
    size = sum(item["bytes"] for item in retained)
    summary = (
        "# Original SSA measurement text evidence\n\n"
        f"Original run `{SOURCE_RUN_ID}`, attempt `{SOURCE_ATTEMPT}`, head `{SOURCE_HEAD_SHA}`.\n\n"
        f"Original artifact `{artifact['id']}` / `{SOURCE_ARTIFACT_NAME}`; metadata and downloaded ZIP SHA256 agree: `{zip_digest}`.\n\n"
        f"Retained {count} complete members with the declared text/source/config suffixes ({size} uncompressed bytes), without changing samples or logs. "
        "Compiler executables and other unselected members remain in the original full artifact. "
        "No compiler, trial or measurement was rerun.\n\n"
        "Only `extraction_complete: true` in the receipt establishes a completed extraction. "
        "Extraction success is not a performance or correctness verdict.\n"
    )
    (output / "extraction-summary.md").write_text(summary, encoding="utf-8")
    publish_step_summary(summary)
    print(f"Verified original artifact {artifact['id']}; retained {count} complete text/config files ({size} bytes).", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except EvidenceError as error:
        # Our own messages contain no credentials, response bodies or URLs.
        message = "SSA evidence extraction failed: " + str(error)
        print(message, file=sys.stderr, flush=True)
        publish_step_summary(message)
        sys.exit(1)
    except urllib.error.HTTPError as error:
        # HTTPError and redirect failures may contain signed URLs. Never print
        # the exception, request, headers or response body.
        message = f"SSA evidence extraction failed: HTTP status {error.code}."
        print(message, file=sys.stderr, flush=True)
        publish_step_summary(message)
        sys.exit(1)
    except Exception as error:
        message = "SSA evidence extraction failed: " + type(error).__name__ + "."
        print(message, file=sys.stderr, flush=True)
        publish_step_summary(message)
        sys.exit(1)
