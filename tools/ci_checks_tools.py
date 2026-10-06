#!/usr/bin/env python3
"""Opt-in selected Go/Ninja/adb identity receipts for CI conditions.

main() gates all parsing and I/O on BUSTER_CI_CONDITIONS_EVIDENCE=1.
binding() retains exact Actions source/run/job identity; selected_path() uses
the configured CMake executable for Ninja. observe() bounds one fixed version
command and checks the selected binary before/after. Only comparable's digest
and full version output belong in normalized conditions; paths are provenance.
No compiler, installation, cache mutation or qualification decision runs here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time

SCHEMA = "buster-ci-selected-tool-v1"
TIMEOUT_SECONDS = 30
MAX_OUTPUT_BYTES = 64 * 1024
MAX_CACHE_BYTES = 4 * 1024 * 1024
CHUNK_BYTES = 8192
POLL_SECONDS = 0.01
VERSION_ARGUMENT = {"go": "version", "ninja": "--version", "adb": "version"}
BINDINGS = (("repository", "GITHUB_REPOSITORY"), ("source_revision", "GITHUB_SHA"),
            ("run_id", "GITHUB_RUN_ID"), ("run_attempt", "GITHUB_RUN_ATTEMPT"), ("job", "GITHUB_JOB"))


class EvidenceError(ValueError):
    def __init__(self, message, status="unknown"):
        super().__init__(message)
        self.status = status


def binding(environment):
    value = {key: environment.get(name, "") for key, name in BINDINGS}
    patterns = {"repository": r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", "source_revision": r"[0-9a-f]{40}",
                "run_id": r"[1-9][0-9]*", "run_attempt": r"[1-9][0-9]*", "job": r"[A-Za-z_][A-Za-z0-9_-]*"}
    for key, pattern in patterns.items():
        if not re.fullmatch(pattern, value[key]):
            raise EvidenceError(f"invalid or missing Actions binding: {key}")
    return value


def cmake_selection(path):
    values = []
    consumed = 0
    if not path.is_file():
        raise EvidenceError("CMake cache is not a regular file")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(descriptor, "rb") as source:
        if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
            raise EvidenceError("CMake cache is not a regular file")
        while True:
            raw = source.readline(MAX_CACHE_BYTES + 1)
            if not raw:
                break
            consumed += len(raw)
            if consumed > MAX_CACHE_BYTES:
                raise EvidenceError("CMake cache exceeds observation bound")
            try:
                line = raw.decode("utf-8").rstrip("\r\n")
            except UnicodeDecodeError as error:
                raise EvidenceError("CMake cache is not strict UTF-8") from error
            if line.startswith("CMAKE_MAKE_PROGRAM:"):
                match = re.fullmatch(r"CMAKE_MAKE_PROGRAM:(FILEPATH|STRING)=(.+)", line)
                if match is None:
                    raise EvidenceError("invalid CMAKE_MAKE_PROGRAM cache entry")
                values.append(match.group(2))
    if len(values) != 1 or "\0" in values[0] or not Path(values[0]).is_absolute():
        raise EvidenceError("CMake cache needs one absolute CMAKE_MAKE_PROGRAM")
    return values[0]


def selected_path(args):
    if args.cmake_cache is not None:
        if args.tool != "ninja":
            raise EvidenceError("CMake selection is supported only for Ninja")
        cache = args.cmake_cache.resolve(strict=True)
        requested = cmake_selection(cache)
        value = {"kind": "cmake-cache", "requested": requested, "cmake_cache": str(cache)}
        path = Path(requested)
    else:
        requested = args.executable
        found = shutil.which(requested)
        if found is None:
            raise EvidenceError("selected executable is unavailable")
        value = {"kind": "executable", "requested": requested}
        path = Path(os.path.abspath(found))
    resolved = path.resolve(strict=True)
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise EvidenceError("selected executable is not a regular executable file")
    value.update(path=str(path), resolved_path=str(resolved))
    return value


def binary_identity(path):
    def fingerprint(value):
        return (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns)

    digest = hashlib.sha256()
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(descriptor, "rb") as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise EvidenceError("selected executable is not a regular file")
        while True:
            data = source.read(1024 * 1024)
            if not data:
                break
            digest.update(data)
        after = os.fstat(source.fileno())
    if fingerprint(before) != fingerprint(after):
        raise EvidenceError("selected executable changed while hashing", "failed")
    return digest.hexdigest(), fingerprint(after)


def stop_process(child):
    try:
        if os.name == "posix":
            os.killpg(child.pid, signal.SIGKILL)
        elif child.poll() is None:
            child.kill()
    except ProcessLookupError:
        pass
    try:
        child.wait(timeout=2)
    except subprocess.TimeoutExpired as error:
        raise EvidenceError("version process cleanup failed", "failed") from error


def version_output(command, timeout=TIMEOUT_SECONDS):
    """Retain at most limit+1 bytes; draining never allocates unbounded output."""
    output = bytearray()
    failure = []
    done = threading.Event()
    deadline = time.monotonic() + timeout
    child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             start_new_session=os.name == "posix")

    def read_output():
        try:
            while True:
                data = child.stdout.read(min(CHUNK_BYTES, MAX_OUTPUT_BYTES + 1 - len(output)))
                if not data:
                    break
                output.extend(data)
                if len(output) > MAX_OUTPUT_BYTES:
                    failure.append("version output exceeds observation bound")
                    break
        except (OSError, ValueError):
            failure.append("version output capture failed")
        finally:
            child.stdout.close()
            done.set()

    reader = threading.Thread(target=read_output, daemon=True)
    try:
        reader.start()
    except RuntimeError as error:
        stop_process(child)
        child.stdout.close()
        raise EvidenceError("version output capture could not start", "failed") from error
    # Keep the leader unreaped until capture closes or owned-group cleanup is
    # complete. Its PID then reserves killpg authority even if it has exited.
    while not done.is_set():
        if failure:
            break
        if time.monotonic() >= deadline:
            failure.append("version command exceeded deadline")
            break
        done.wait(POLL_SECONDS)
    if not failure:
        try:
            value = output.decode("utf-8")
            if not value.strip():
                failure.append("version output is empty")
        except UnicodeDecodeError:
            failure.append("version output is not strict UTF-8")
    if not failure:
        try:
            child.wait(timeout=max(0, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            failure.append("version command exceeded deadline")
    if failure:
        stop_process(child)
    reader.join(timeout=2)
    if reader.is_alive():
        failure.append("version output capture did not close")
    if failure:
        raise EvidenceError("; ".join(failure), "failed")
    if child.returncode != 0:
        raise EvidenceError(f"version command exited with status {child.returncode}", "failed")
    return value


def observe(args, receipt):
    selection = selected_path(args)
    receipt["selection"] = selection
    path = Path(selection["resolved_path"])
    before = binary_identity(path)
    command = [str(path), VERSION_ARGUMENT[args.tool]]
    receipt["command"] = command
    version = version_output(command)
    after = binary_identity(path)
    if before != after or Path(selection["path"]).resolve(strict=True) != path:
        raise EvidenceError("selected executable changed during version observation", "failed")
    receipt.update(status="observed", comparable={"sha256": before[0], "version": version})


def write_receipt(path, receipt):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as output:
            temporary = Path(output.name)
            json.dump(receipt, output, sort_keys=True, indent=2, ensure_ascii=False)
            output.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main(argv=None):
    result = 0
    if os.environ.get("BUSTER_CI_CONDITIONS_EVIDENCE") == "1":
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--tool", choices=VERSION_ARGUMENT, required=True)
        selection = parser.add_mutually_exclusive_group(required=True)
        selection.add_argument("--executable")
        selection.add_argument("--cmake-cache", type=Path)
        parser.add_argument("--output", type=Path, required=True)
        args = parser.parse_args(argv)
        receipt = {"schema": SCHEMA, "tool": args.tool, **{key: os.environ.get(name, "") for key, name in BINDINGS}}
        try:
            receipt.update(binding(os.environ))
            observe(args, receipt)
        except (ValueError, OSError, RuntimeError) as error:
            receipt.update(status=error.status if isinstance(error, EvidenceError) else "unknown", reason=str(error))
            result = 1
        try:
            write_receipt(args.output, receipt)
        except OSError as error:
            print(f"could not retain selected-tool receipt: {error}", file=sys.stderr)
            result = 1
    return result


if __name__ == "__main__":
    sys.exit(main())
