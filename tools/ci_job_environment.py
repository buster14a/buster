#!/usr/bin/env python3
"""Opt-in actual job environment provenance; no probes or qualification.

main() gates before argument parsing or output I/O. collect() retains only
strict Actions bindings and the named environment whitelist, including unknown
observations verbatim. write_receipt() publishes bounded regular JSON once,
through checked POSIX directory descriptors. The five callers run on Ubuntu.
GITHUB_JOB is a workflow key, and GITHUB_WORKFLOW_SHA is a workflow commit;
numeric API job IDs and workflow blob identities require separate actual joins.
"""
import argparse
import json
import os
import re
import secrets
import stat
import sys

SCHEMA = "buster-ci-job-environment-v1"
MAX_FIELD_BYTES = 512
MAX_RECEIPT_BYTES = 8192
MAX_PATH_BYTES = 2048
BINDINGS = (("repository", "GITHUB_REPOSITORY"), ("source_revision", "GITHUB_SHA"),
            ("run_id", "GITHUB_RUN_ID"), ("run_attempt", "GITHUB_RUN_ATTEMPT"), ("job", "GITHUB_JOB"))
ENV_KEYS = ("BUSTER_CI_RUNNER", "RUNNER_OS", "RUNNER_ARCH", "RUNNER_NAME", "ImageOS", "ImageVersion",
            "GITHUB_WORKFLOW", "GITHUB_WORKFLOW_REF", "GITHUB_WORKFLOW_SHA")
UNKNOWN = frozenset(("", "unknown", "unavailable", "missing", "pending", "none", "null", "n/a"))


class EvidenceError(ValueError):
    """An invalid or unsafe bounded provenance request."""


def bounded_value(value, key):
    if value is not None:
        if not isinstance(value, str):
            raise EvidenceError("non-string environment field: " + key)
        try:
            size = len(value.encode("utf-8"))
        except UnicodeEncodeError as error:
            raise EvidenceError("invalid UTF-8 environment field: " + key) from error
        if size > MAX_FIELD_BYTES:
            raise EvidenceError("environment field exceeds bound: " + key)
        if value.strip() and any(ord(character) < 32 or ord(character) == 127 for character in value):
            raise EvidenceError("control character in environment field: " + key)
    return value


def actions_binding(environment):
    value = {key: bounded_value(environment.get(name), name) for key, name in BINDINGS}
    patterns = {"repository": r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", "source_revision": r"[0-9a-f]{40}",
                "run_id": r"[1-9][0-9]*", "run_attempt": r"[1-9][0-9]*", "job": r"[A-Za-z_][A-Za-z0-9_-]*"}
    invalid = [key for key, pattern in patterns.items()
               if not isinstance(value[key], str) or re.fullmatch(pattern, value[key]) is None]
    return value, invalid


def collect(environment):
    bindings, invalid = actions_binding(environment)
    observed = {key: bounded_value(environment.get(key), key) for key in ENV_KEYS}
    unavailable = [key for key, value in observed.items() if value is None or value.strip().casefold() in UNKNOWN]
    if observed["GITHUB_WORKFLOW_SHA"] is not None and observed["GITHUB_WORKFLOW_SHA"].strip().casefold() not in UNKNOWN:
        if re.fullmatch(r"[0-9a-f]{40}", observed["GITHUB_WORKFLOW_SHA"]) is None:
            unavailable.append("GITHUB_WORKFLOW_SHA")
    return {"schema": SCHEMA, "status": "incomplete" if invalid or unavailable else "complete", **bindings,
            "environment": observed, "invalid_bindings": invalid, "unavailable_fields": unavailable}


def serialize(receipt):
    data = (json.dumps(receipt, sort_keys=True, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
    if len(data) > MAX_RECEIPT_BYTES:
        raise EvidenceError("environment receipt exceeds byte bound")
    return data


def output_components(value):
    if not isinstance(value, str) or not value.startswith("/"):
        raise EvidenceError("receipt output must be an absolute POSIX JSON path")
    try:
        size = len(value.encode("utf-8"))
    except UnicodeEncodeError as error:
        raise EvidenceError("invalid UTF-8 receipt output path") from error
    parts = value.split("/")[1:]
    if size > MAX_PATH_BYTES or len(parts) > 32 or any(part in ("", ".", "..") for part in parts):
        raise EvidenceError("receipt output path exceeds bound or contains traversal")
    if any(ord(character) < 32 or ord(character) == 127 for character in value) or not parts[-1].endswith(".json"):
        raise EvidenceError("receipt output must name one JSON file")
    return parts


def write_receipt(path, data):
    if not isinstance(data, bytes) or not 0 < len(data) <= MAX_RECEIPT_BYTES:
        raise EvidenceError("environment receipt exceeds byte bound")
    parts = output_components(path)
    if os.name != "posix" or not hasattr(os, "O_NOFOLLOW"):
        raise EvidenceError("safe job environment publication requires POSIX no-follow directories")
    directory = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    temporary = None
    try:
        for component in parts[:-1]:
            try:
                os.mkdir(component, 0o700, dir_fd=directory)
            except FileExistsError:
                pass
            child = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
            os.close(directory)
            directory = child
        try:
            os.stat(parts[-1], dir_fd=directory, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            raise EvidenceError("receipt destination already exists")
        name = ".ci-job-environment-" + secrets.token_hex(8) + ".tmp"
        descriptor = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=directory)
        temporary = name
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
            if not stat.S_ISREG(os.fstat(output.fileno()).st_mode):
                raise EvidenceError("receipt temporary is not regular")
        os.link(temporary, parts[-1], src_dir_fd=directory, dst_dir_fd=directory, follow_symlinks=False)
        os.fsync(directory)
    finally:
        if temporary is not None:
            try:
                os.unlink(temporary, dir_fd=directory)
            except FileNotFoundError:
                pass
        os.close(directory)


def main(argv=None):
    result = 0
    if os.environ.get("BUSTER_CI_CONDITIONS_EVIDENCE") == "1":
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--output", required=True)
        args = parser.parse_args(argv)
        try:
            receipt = collect(os.environ)
            data = serialize(receipt)
            write_receipt(args.output, data)
            result = 1 if receipt["invalid_bindings"] else 0
            print("CI_JOB_ENVIRONMENT status=" + receipt["status"])
        except (ValueError, OSError) as error:
            print("CI_JOB_ENVIRONMENT retention failed: " + (str(error) if isinstance(error, EvidenceError) else "filesystem operation failed"), file=sys.stderr)
            result = 1
    return result


if __name__ == "__main__":
    sys.exit(main())
