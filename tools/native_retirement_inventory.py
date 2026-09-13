#!/usr/bin/env python3
"""Read-only #510 history gate; publication and semantic replay stay in #504/#510.

The archive owner supplies native_retirement_archive.checked (via PYTHONPATH).
origin_errors compares live Actions metadata with frozen identities; select_parts
resolves only actual release assets; verify_parts binds retrieved bytes to the
original ZIP, independently of the release's self-described manifest. No upload,
compiler execution, extraction, or retirement-acceptance decision occurs here.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

import native_retirement_archive as archive


REPOSITORY = "buster14a/buster"
SCHEMA = "buster-native-retirement-history-v1"
CHUNK = 1024 * 1024


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def origin_errors(expected, actual):
    fields = {"id": expected["id"], "name": expected["name"],
              "size_in_bytes": expected["size"], "digest": "sha256:" + expected["sha256"]}
    errors = [f"Actions {name} differs from frozen identity" for name, value in fields.items()
              if actual.get(name) != value]
    run = actual.get("workflow_run", {})
    if run.get("id") != expected["run"] or run.get("head_sha") != expected["workflow_commit"]:
        errors.append("Actions producing run/revision differs from frozen identity")
    # expires_at is observed, not compared for equality: retention can change.
    if not isinstance(actual.get("expired"), bool) or not actual.get("expires_at"):
        errors.append("Actions expiration metadata is missing")
    else:
        try:
            instant = datetime.fromisoformat(actual["expires_at"].replace("Z", "+00:00"))
            if instant.tzinfo is None:
                errors.append("Actions expiration lacks a timezone")
        except (ValueError, TypeError):
            errors.append("Actions expiration is malformed")
    return errors


def select_parts(expected, assets):
    name = expected["name"] + ".zip"
    whole = [item for item in assets if item["name"] == name]
    split = [item for item in assets if item["name"].startswith(name + ".part-")]
    if len(whole) > 1 or (whole and split):
        raise ValueError("ambiguous whole/split durable archive")
    result = whole
    if split:
        indexed = {}
        for item in split:
            match = re.fullmatch(re.escape(name) + r"\.part-(\d{2,})", item["name"])
            if match is None or int(match[1]) in indexed:
                raise ValueError("malformed or duplicate durable archive part")
            indexed[int(match[1])] = item
        if sorted(indexed) != list(range(len(indexed))):
            raise ValueError("missing durable archive part")
        result = [indexed[index] for index in range(len(indexed))]
    if result and sum(item["size"] for item in result) != expected["size"]:
        raise ValueError("durable archive byte count differs from original Actions ZIP")
    for item in result:
        if item.get("state") != "uploaded" or not re.fullmatch(r"sha256:[0-9a-f]{64}", item.get("digest") or ""):
            raise ValueError("durable asset lacks uploaded state or SHA-256 metadata")
    return result


def verify_parts(expected, parts, directory):
    if not parts:
        raise ValueError("no durable archive bytes supplied")
    combined = hashlib.sha256()
    total = 0
    for item in parts:
        path = archive.checked(Path(directory) / item["name"], item["size"], item["digest"][7:])
        with path.open("rb") as source:
            for block in iter(lambda: source.read(CHUNK), b""):
                total += len(block)
                combined.update(block)
    if total != expected["size"] or combined.hexdigest() != expected["sha256"]:
        raise ValueError("retrieved ZIP differs from original Actions byte count/SHA-256")
    return {"size": total, "sha256": combined.hexdigest()}


def api(endpoint, output, label):
    command = ["gh", "api", endpoint]
    process = subprocess.run(command, capture_output=True, text=True, timeout=90, check=False)
    if process.returncode:
        write_json(output / (label + "-error.json"), {"command": command, "exit_code": process.returncode,
                                                     "stderr": process.stderr})
        raise ValueError(f"GitHub API unavailable: {endpoint} (exit {process.returncode})")
    value = json.loads(process.stdout)
    write_json(output / (label + ".json"), value)
    return value


def pages(endpoint, key, output, label):
    result = []
    complete = False
    page = 1
    while not complete and page <= 100:
        separator = "&" if "?" in endpoint else "?"
        value = api(f"{endpoint}{separator}per_page=100&page={page}", output, f"{label}-{page}")
        items = value[key] if key else value
        if not isinstance(items, list):
            raise ValueError("GitHub pagination returned a non-list")
        result.extend(items)
        complete = len(items) < 100
        page += 1
    if not complete:
        raise ValueError("GitHub pagination limit reached; inventory is incomplete")
    return result


def discover(output):
    root = f"repos/{REPOSITORY}"
    runs = pages(root + "/actions/runs?branch=codex%2F36-census-evidence-de594a5",
                 "workflow_runs", output, "runs")
    artifacts = {}
    for run in runs:
        if run["path"] == ".github/workflows/native-retirement-evidence.yml":
            for item in pages(f"{root}/actions/runs/{run['id']}/artifacts", "artifacts", output, f"run-{run['id']}"):
                if item["name"].startswith(("native-retirement-", "strict-retirement-")):
                    artifacts[item["id"]] = item
    return artifacts


def check_catalog(catalog):
    if catalog.get("schema") != SCHEMA or catalog.get("repository") != REPOSITORY:
        raise ValueError("unsupported history contract")
    items = catalog["artifacts"]
    if not items or len({item["id"] for item in items}) != len(items):
        raise ValueError("empty history or duplicate artifact identity")
    for item in items:
        if not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) or item["size"] <= 0:
            raise ValueError("history requires a positive byte count and frozen SHA-256")
        if not re.fullmatch(r"[A-Za-z0-9_-]+", item["name"]):
            raise ValueError("invalid historical artifact name")
        for name in ("workflow_commit", "candidate_commit", "candidate_tree"):
            if not re.fullmatch(r"[0-9a-f]{40}", item[name]):
                raise ValueError(f"history lacks immutable {name}")
    return items


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = {"schema": SCHEMA, "observed_at": datetime.now(timezone.utc).isoformat(),
              "checkout_sha": os.environ.get("GITHUB_SHA"), "run_id": os.environ.get("GITHUB_RUN_ID"),
              "durable_bytes_complete": False, "retirement_accepted": False,
              "source_reproduction": "NOT RUN", "independent_join": "NOT RUN",
              "strict_semantic_replay": "NOT RUN", "artifacts": [], "errors": []}
    try:
        catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
        expected = check_catalog(catalog)
        report["catalog_sha256"] = archive.digest(args.catalog)
        report["archive_helper_sha256"] = archive.digest(Path(archive.__file__))
        root = f"repos/{REPOSITORY}"
        observed = discover(args.output)
        required_ids = {item["id"] for item in expected}
        report["unregistered_artifacts"] = [item for key, item in observed.items() if key not in required_ids]
        release = api(f"{root}/releases/{catalog['release_id']}", args.output, "release")
        if release["tag_name"] != catalog["release_tag"] or release["draft"]:
            raise ValueError("approved durable release identity/state changed")
        report["inspected_destination"] = release["html_url"]
        assets = pages(f"{root}/releases/{release['id']}/assets", None, args.output, "release-assets")
        for item in sorted(expected, key=lambda value: value["expires_at_observed"]):
            row = {"expected": item, "status": "NOT VERIFIED", "origin_errors": []}
            report["artifacts"].append(row)
            try:
                actual = observed.get(item["id"])
                if actual is None:
                    actual = api(f"{root}/actions/artifacts/{item['id']}", args.output, f"artifact-{item['id']}")
                row["origin"] = actual
                row["origin_errors"] = origin_errors(item, actual)
            except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
                row["origin_errors"].append(str(error))
            # An expired/unavailable origin is never replaced. An exact already
            # durable copy can still be checked against its frozen original hash.
            try:
                selected = select_parts(item, assets)
                row["destination_assets"] = selected
                if not selected:
                    row["status"] = "MISSING FROM INSPECTED DURABLE DESTINATION"
                else:
                    with tempfile.TemporaryDirectory(prefix="retirement-history-") as temporary:
                        for part in selected:
                            command = ["gh", "api", f"{root}/releases/assets/{part['id']}",
                                       "-H", "Accept: application/octet-stream"]
                            with (Path(temporary) / part["name"]).open("wb") as stream:
                                subprocess.run(command, stdout=stream, check=True, timeout=600)
                        row["retrieved"] = verify_parts(item, selected, temporary)
                        row["status"] = "VERIFIED DURABLE BYTES"
            except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
                row["status"] = "FAILED DURABLE BYTE VALIDATION"
                row["error"] = str(error)
            write_json(args.output / "inventory.json", report)
            print(f"artifact {item['id']}: {row['status']}", flush=True)
        report["durable_bytes_complete"] = all(row["status"] == "VERIFIED DURABLE BYTES" for row in report["artifacts"])
        report["inventory_complete"] = (not report["unregistered_artifacts"] and
                                        not any(row["origin_errors"] for row in report["artifacts"]))
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        report["errors"].append(str(error))
    write_json(args.output / "inventory.json", report)
    status = 0 if report["durable_bytes_complete"] and report.get("inventory_complete") and not report["errors"] else 1
    return status


if __name__ == "__main__":
    raise SystemExit(main())
