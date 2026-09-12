#!/usr/bin/env python3
"""Recover pinned historical Buster material from a local, self-contained Git bundle.

This tool only reads the supplied bundle and writes a new local output directory.
It has no GitHub API client, remote update, branch deletion, checkout, patch
application or historical-code execution path. Every exported payload is inert
.txt data. Source hashes and original commit provenance remain explicit.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
from typing import Any

REPOSITORY = "buster14a/buster"


def command(args: list[str], *, cwd: pathlib.Path | None = None,
            data: bytes | None = None) -> bytes:
    result = subprocess.run(args, cwd=cwd, input=data, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=300, check=False)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {args!r}\n"
                           + result.stderr.decode("utf-8", "replace"))
    return result.stdout



def git(repo: pathlib.Path, *args: str) -> str:
    return command(["git", "-C", str(repo), *args]).decode().strip()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: pathlib.Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")



MANIFEST = pathlib.Path(__file__).resolve().parents[1] / "docs/audits/2026-09-12-branch-recovery/manifest.json"


def export_salvage(repo: pathlib.Path, out: pathlib.Path, manifest_path: pathlib.Path = MANIFEST) -> list[dict[str, Any]]:
    """Export inert data only. No checkout, patch application, decoding or execution."""
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(manifest.get("schema_version") == 1 and manifest.get("repository") == REPOSITORY,
            "Unknown recovery manifest.")
    records = manifest["records"]
    require(isinstance(records, list) and bool(records), "Empty recovery manifest.")
    require(not out.exists(), "Recovery directory already exists.")
    out.mkdir()
    exported = []
    destinations: set[str] = set()
    for record in records:
        commit = record["commit"]
        require(re.fullmatch(r"[0-9a-f]{40}", commit) is not None, "Invalid source commit.")
        # --all bundles contain reachable objects, not arbitrary dangling objects.
        require(bool(git(repo, "for-each-ref", "--contains=" + commit, "--format=%(refname)")),
                "Recovery source is not reachable from a preserved ref: " + commit)
        name = record["destination"] + ".txt"
        parts = pathlib.PurePosixPath(name).parts
        require(not pathlib.PurePosixPath(name).is_absolute() and len(parts) > 1 and parts[0] == "salvage" and
                ".." not in parts and "\\" not in name and ":" not in name and name not in destinations,
                "Invalid or duplicate recovery destination.")
        destinations.add(name)
        method = record["method"]
        if method == "blob":
            blob = git(repo, "rev-parse", commit + ":" + record["source_path"])
            require(blob == record["git_blob"], "Recovery source blob changed.")
            data = command(["git", "-C", str(repo), "cat-file", "blob", blob])
            require(len(data) == record["bytes"] and hashlib.sha256(data).hexdigest() == record["sha256"],
                    "Recovery source checksum mismatch.")
        elif method == "commit-patch":
            require(git(repo, "rev-parse", commit + "^{tree}") == record["git_tree"],
                    "Recovery source tree changed.")
            data = command(["git", "-C", str(repo), "format-patch", "-1", "--stdout", "--no-signature",
                            "--no-stat", "--full-index", "--binary", "--no-renames", "--no-ext-diff",
                            "--no-textconv", "--no-numbered", "--no-thread", "--no-base",
                            "--subject-prefix=PATCH", commit, "--"])
        else:
            raise RuntimeError("Unknown recovery method.")
        destination = out.joinpath(*parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        exported.append({"destination": name, "source_commit": commit, "method": method,
                         "status": record["status"], "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    write_json(out / "manifest.json", manifest)
    write_json(out / "exported.json", exported)
    return exported



def recover(bundle: pathlib.Path, out: pathlib.Path, manifest: pathlib.Path = MANIFEST) -> list[dict[str, Any]]:
    source = bundle.resolve(strict=True)
    require(source.is_file(), "The input must be a local self-contained Git bundle file.")
    out = out.resolve()
    require(not out.exists(), "Output already exists; use a fresh directory.")
    out.mkdir(parents=True)
    evidence: dict[str, Any] = {"source_bundle_sha256": sha256(source), "complete": False}
    write_json(out / "recovery.json", evidence)
    try:
        repo = out / "history.git"
        # A local bundle is the only clone source. No URL or working tree is accepted.
        command(["git", "clone", "--mirror", "--no-hardlinks", str(source), str(repo)])
        git(repo, "bundle", "verify", str(source))
        require(sha256(source) == evidence["source_bundle_sha256"], "Source bundle changed during recovery.")
        exported = export_salvage(repo, out / "recovery", manifest)
        evidence.update({"complete": True, "records": len(exported)})
    except Exception as error:
        evidence["error"] = str(error)
        raise
    finally:
        write_json(out / "recovery.json", evidence)
    return exported


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=pathlib.Path, help="Local self-contained Git bundle file.")
    parser.add_argument("--output", required=True, type=pathlib.Path, help="New local output directory; never overwritten.")
    args = parser.parse_args()
    exported = recover(args.bundle, args.output)
    print(f"Recovered {len(exported)} historical records into {args.output.resolve()}; no remote state changed.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print("Recovery stopped:", error, file=sys.stderr)
        sys.exit(1)
