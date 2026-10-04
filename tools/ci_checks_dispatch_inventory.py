#!/usr/bin/env python3
"""Retain complete original Actions dispatch pages for the #2610 population gate.

collect() validates the original publication before any API request, then keeps
every raw page without filtering branches, source, status, failures or retries.
fetch_page() performs only an explicit GET with bounded clocks and parsing size.
An incomplete or changing inventory leaves its original pages and failure.json,
never a qualifying manifest. Existing output directories are never overwritten.

Usage: python3 -B tools/ci_checks_dispatch_inventory.py
       --publication ORIGINAL_ISSUE2610_COMMENT.json --output NEW_DIRECTORY
Success prints the manifest REF {path,sha256}; paths inside it are relative.
The unchanged population reader independently replays the pages and reconciles
every campaign dispatch. A digest proves retained bytes, not external origin;
the operator must retain actual authenticated API custody. Collection cannot
prove that a run deleted from GitHub before capture ever existed. It makes no
CI mutation, authentication refresh, sampling or performance decision.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

SCHEMA = "buster-ci-checks-dispatch-inventory-v1"
ENDPOINT = "https://api.github.com/repos/buster14a/buster/actions/workflows/ci.yml/runs"
API_PATH = "repos/buster14a/buster/actions/workflows/ci.yml/runs"
PER_PAGE = 100
MAX_RUNS = 1000
MAX_PAGE_BYTES = 16 * 1024 * 1024
MAX_PUBLICATION_BYTES = 1024 * 1024
REQUEST_SECONDS = 30
COLLECTION_SECONDS = 300
MARKER = re.compile(r"CI_CHECKS_POPULATION_DECLARATION_V1 sha256=[0-9a-f]{64}\Z")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def utc_now():
    return datetime.now(timezone.utc)


def timestamp(value):
    require(isinstance(value, str) and value, "missing API timestamp")
    result = datetime.fromisoformat(value.replace("Z", "+00:00"))
    require(result.tzinfo is not None, "API timestamp lacks timezone")
    return result.astimezone(timezone.utc)


def stamp(value):
    return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


def reference(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        while data := stream.read(1024 * 1024):
            checksum.update(data)
    return {"path": path.name, "sha256": checksum.hexdigest()}


def publication(path):
    path = Path(path)
    require(not path.is_symlink() and path.is_file() and path.stat().st_size <= MAX_PUBLICATION_BYTES,
            "publication is not a bounded regular file")
    original = path.read_bytes()
    receipt = json.loads(original)
    require(isinstance(receipt, dict), "publication is not an API comment object")
    identifier = receipt.get("id")
    require(type(identifier) is int and identifier > 0 and
            receipt.get("issue_url") == "https://api.github.com/repos/buster14a/buster/issues/2610" and
            receipt.get("url") == f"https://api.github.com/repos/buster14a/buster/issues/comments/{identifier}" and
            receipt.get("html_url") == f"https://github.com/buster14a/buster/issues/2610#issuecomment-{identifier}",
            "publication is not the original issue2610 API comment")
    body = receipt.get("body")
    require(isinstance(body, str) and sum(MARKER.fullmatch(line) is not None for line in body.splitlines()) == 1,
            "publication needs exactly one declaration marker")
    created = timestamp(receipt.get("created_at"))
    require(timestamp(receipt.get("updated_at")) == created and created <= utc_now(),
            "publication was edited or is in the future")
    return original, created


def fetch_page(directory, created_after, page, deadline):
    """Retain exact stdout even on refusal; no shell or implicit POST method."""
    remaining = deadline - time.monotonic()
    require(remaining > 0, "dispatch inventory collection deadline exhausted")
    response_path = directory / f"page-{page:03d}.json"
    error_path = directory / f"page-{page:03d}.stderr"
    command = ["gh", "api", "--method", "GET", API_PATH,
               "-f", "event=workflow_dispatch", "-f", "created=>=" + created_after,
               "-F", "per_page=" + str(PER_PAGE), "-F", "page=" + str(page)]
    environment = dict(os.environ, GH_PROMPT_DISABLED="1", GH_PAGER="cat")
    with response_path.open("xb") as stdout, error_path.open("xb") as stderr:
        result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                env=environment, timeout=min(REQUEST_SECONDS, remaining), check=False)
    require(result.returncode == 0, f"dispatch API page {page} failed; original output retained")
    require(response_path.stat().st_size <= MAX_PAGE_BYTES and error_path.stat().st_size <= MAX_PAGE_BYTES,
            f"dispatch API page {page} exceeds retained parsing limit")
    response = json.loads(response_path.read_bytes())
    require(isinstance(response, dict), "dispatch API page is not an object")
    return response_path, response


def collect(publication_path, output_directory):
    original, published = publication(publication_path)
    directory = Path(output_directory)
    directory.mkdir(parents=True, exist_ok=False)
    (directory / "publication.json").write_bytes(original)
    created_after = stamp(published)
    pages, records, total = [], [], None
    deadline = time.monotonic() + COLLECTION_SECONDS
    try:
        page = 1
        while total is None or len(records) < total:
            response_path, response = fetch_page(directory, created_after, page, deadline)
            pages.append({"page": page, "response": reference(response_path)})
            count = response.get("total_count")
            require(type(count) is int and 0 <= count <= MAX_RUNS and (total is None or count == total),
                    "dispatch count exceeds API limit or changed during pagination")
            total = count
            rows = response.get("workflow_runs")
            expected = min(PER_PAGE, max(0, total - (page - 1) * PER_PAGE))
            require(isinstance(rows, list) and len(rows) == expected,
                    "dispatch API page is incomplete")
            records.extend(rows)
            page += 1
        require(len(pages) == max(1, (total + PER_PAGE - 1) // PER_PAGE) and len(records) == total,
                "dispatch pagination is incomplete")
        require(all(isinstance(record, dict) and type(record.get("id")) is int and record["id"] > 0
                    for record in records) and len({record["id"] for record in records}) == total,
                "duplicate or invalid dispatch API run")
        collected = utc_now()
        require(collected >= published, "capture clock precedes publication")
        for record in records:
            created = timestamp(record.get("created_at"))
            require(record.get("event") == "workflow_dispatch" and record.get("path") == ".github/workflows/ci.yml" and
                    published <= created <= collected, "dispatch API record outside publication scope")
        require(time.monotonic() <= deadline, "dispatch inventory collection deadline exhausted")
        value = {"schema": SCHEMA, "endpoint": ENDPOINT, "event": "workflow_dispatch",
                 "created_after": created_after, "per_page": PER_PAGE,
                 "collected_at": stamp(collected), "pages": pages}
        manifest_path = directory / "inventory.json"
        with manifest_path.open("x", encoding="utf-8") as stream:
            stream.write(json.dumps(value, indent=2) + "\n")
        result = reference(manifest_path)
        result["path"] = str(manifest_path.resolve())
    except (OSError, ValueError, TypeError, AttributeError, subprocess.TimeoutExpired) as error:
        failure = {"schema": "buster-ci-checks-dispatch-inventory-failure-v1", "status": "failed",
                   "endpoint": ENDPOINT, "created_after": created_after, "pages": pages,
                   "retained_files": [reference(path) for path in sorted(directory.glob("page-*"))],
                   "error": str(error), "success_manifest": False}
        with (directory / "failure.json").open("x", encoding="utf-8") as stream:
            stream.write(json.dumps(failure, indent=2) + "\n")
        raise
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--publication", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = 0
    try:
        print(json.dumps(collect(args.publication, args.output)))
    except (OSError, ValueError, TypeError, AttributeError, subprocess.TimeoutExpired) as error:
        print(f"Dispatch inventory collection failed: {error}. Retained output: {args.output}", file=sys.stderr)
        result = 1
    return result


if __name__ == "__main__":
    sys.exit(main())
