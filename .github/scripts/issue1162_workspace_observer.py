#!/usr/bin/env python3
"""Read-only, fd-anchored workspace product observations for the #1162 slice.

The base-build checkpoint enumerates settled materializer source metadata. The
candidate-build and throughput checkpoints observe fixed handoff paths only;
neither observation claims to have witnessed a mkdir, copy, or promotion call.
"""
from __future__ import annotations

from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import stat
import sys
import tempfile
import time
from types import SimpleNamespace
from unittest.mock import patch

import issue1162_live_probe as probe

WORKSPACES = probe.WORKSPACES
INSTALLED = "/opt/buster-bench/installed"
REVISION = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})\Z")
MAX_MANIFEST = 64 * 1024
MAX_FILES = 4096
MAX_DIRS = 480
MAX_PATH = 192
MAX_FILE = 64 * 1024 * 1024
MAX_TOTAL = 512 * 1024 * 1024
MAX_DEPTH = 192
MAX_EVIDENCE = 16 * 1024 * 1024
ROOT_UID = 0


def require(condition: bool, message: str) -> None:
    probe._fail(condition, message)


def identity(info: os.stat_result) -> tuple[int, ...]:
    return (info.st_dev, info.st_ino, info.st_mode, info.st_uid, info.st_gid)


def settled_identity(info: os.stat_result) -> tuple[int, ...]:
    return identity(info) + (info.st_nlink, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def metadata(path: str, info: os.stat_result) -> dict:
    return {"path": path, "type": "directory" if stat.S_ISDIR(info.st_mode) else "file",
            "device": info.st_dev, "inode": info.st_ino, "uid": info.st_uid,
            "gid": info.st_gid, "mode": stat.S_IMODE(info.st_mode), "links": info.st_nlink,
            "size": info.st_size, "mtime_ns": info.st_mtime_ns, "ctime_ns": info.st_ctime_ns}


def check_directory(info: os.stat_result, path: str, uid: int, gid: int, mode: int) -> None:
    require(stat.S_ISDIR(info.st_mode) and info.st_uid == uid and info.st_gid == gid and
            stat.S_IMODE(info.st_mode) == mode and info.st_nlink >= 2,
            f"directory metadata mismatch: {path}")


def open_directory(parent: int, name: str, path: str, uid: int, gid: int,
                   mode: int) -> tuple[int, os.stat_result]:
    before = os.stat(name, dir_fd=parent, follow_symlinks=False)
    check_directory(before, path, uid, gid, mode)
    fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                 dir_fd=parent)
    try:
        require(identity(os.fstat(fd)) == identity(before), f"directory swapped before open: {path}")
        return fd, before
    except BaseException:
        os.close(fd)
        raise


def recheck(parent: int, name: str, fd: int, before: os.stat_result,
            path: str, settled: bool = False) -> None:
    stable = settled_identity if settled else identity
    require(stable(os.fstat(fd)) == stable(before) and
            stable(os.stat(name, dir_fd=parent, follow_symlinks=False)) == stable(before),
            f"directory changed or replaced: {path}")


def recheck_fixed(parent: int, name: str, fd: int, record: dict) -> None:
    expected = (record["device"], record["inode"], stat.S_IFDIR | record["mode"],
                record["uid"], record["gid"])
    require(identity(os.fstat(fd)) == expected and
            identity(os.stat(name, dir_fd=parent, follow_symlinks=False)) == expected,
            f"fixed workspace entry changed or replaced: {record['path']}")


def recheck_fixed_file(parent: int, name: str, record: dict) -> None:
    if record["presence"] == "absent":
        try:
            os.stat(name, dir_fd=parent, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            raise probe.ProbeError(f"fixed workspace file appeared during capture: {record['path']}")
    else:
        current = os.stat(name, dir_fd=parent, follow_symlinks=False)
        require(identity(current) == (record["device"], record["inode"],
                                      stat.S_IFREG | record["mode"], record["uid"], record["gid"]) and
                current.st_nlink == record["links"] and current.st_size == record["size"] and
                current.st_mtime_ns == record["mtime_ns"] and current.st_ctime_ns == record["ctime_ns"],
                f"fixed workspace file changed or replaced: {record['path']}")


def recheck_absent(parent: int, name: str, record: dict) -> None:
    require(record["presence"] == "absent", "expected absent fixed workspace path")
    try:
        os.stat(name, dir_fd=parent, follow_symlinks=False)
    except FileNotFoundError:
        pass
    else:
        raise probe.ProbeError(f"fixed workspace path appeared during capture: {record['path']}")


def read_file(parent: int, name: str, path: str, maximum: int, uid: int,
              gid: int | None, mode: int | None) -> tuple[bytes, os.stat_result]:
    before = os.stat(name, dir_fd=parent, follow_symlinks=False)
    require(stat.S_ISREG(before.st_mode) and before.st_uid == uid and
            (gid is None or before.st_gid == gid) and
            (mode is None and stat.S_IMODE(before.st_mode) & 0o222 == 0 or
             mode is not None and stat.S_IMODE(before.st_mode) == mode) and
            before.st_nlink == 1 and 0 < before.st_size <= maximum,
            f"file metadata mismatch: {path}")
    fd = os.open(name, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC | os.O_NOFOLLOW,
                 dir_fd=parent)
    try:
        require(settled_identity(os.fstat(fd)) == settled_identity(before),
                f"file changed before read: {path}")
        content = bytearray()
        while len(content) < before.st_size:
            data = os.read(fd, min(65536, before.st_size - len(content)))
            require(bool(data), f"short file read: {path}")
            content.extend(data)
        require(os.read(fd, 1) == b"" and
                settled_identity(os.fstat(fd)) == settled_identity(before) and
                settled_identity(os.stat(name, dir_fd=parent, follow_symlinks=False)) == settled_identity(before),
                f"file changed during read: {path}")
        return bytes(content), before
    finally:
        os.close(fd)


def source_manifest(raw: bytes, revision: str) -> dict[str, str]:
    header = f"BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision={revision}\n".encode()
    require(raw.startswith(header) and raw.endswith(b"\n") and len(raw) <= MAX_MANIFEST and
            b"\r" not in raw and b"\0" not in raw, "source manifest header or framing mismatch")
    entries: dict[str, str] = {}
    previous = ""
    for line in raw[len(header):-1].split(b"\n"):
        try:
            digest, sep, encoded = line.partition(b" ")
            path = encoded.decode("ascii")
        except UnicodeDecodeError as exc:
            raise probe.ProbeError("non-ASCII source path") from exc
        parts = path.split("/")
        require(sep == b" " and re.fullmatch(rb"[0-9a-f]{64}", digest) is not None and
                0 < len(encoded) <= MAX_PATH and previous < path and
                path != ".source-manifest" and all(part not in ("", ".", "..") for part in parts) and
                all(0x21 <= byte <= 0x7e and byte != 0x5c for byte in encoded) and
                len(entries) < MAX_FILES, "invalid, unordered, or oversized source manifest entry")
        entries[path] = digest.decode("ascii")
        previous = path
    require(bool(entries), "empty source manifest")
    return entries


def source_inventory(fd: int, label: str, manifest: dict[str, str], service_uid: int,
                     candidate_gid: int, deadline: float) -> dict:
    """Enumerate every sealed entry, rejecting extras and omissions."""
    root = os.fstat(fd)
    check_directory(root, label, service_uid, candidate_gid, 0o550)
    nodes: list[dict] = []
    seen: set[str] = set()
    directories = 0
    total = 0
    evidence_bytes = 0
    def append(record: dict) -> None:
        nonlocal evidence_bytes
        evidence_bytes += len(json.dumps(record, sort_keys=True).encode()) + 1
        require(len(nodes) < MAX_FILES + MAX_DIRS + 1 and evidence_bytes <= MAX_EVIDENCE,
                "source evidence node or byte bound exceeded")
        nodes.append(record)
    # The external root fd remains open. Descendants stay pinned until their
    # complete listing and path identity have been checked.
    frames: list[tuple[int, str, os.stat_result, list[str] | None, int]] = [(fd, "", root, None, 0)]
    try:
        while frames:
            probe._remaining(deadline, 0)
            current, prefix, start, children, index = frames[-1]
            if children is None:
                check_directory(start, f"{label}/{prefix}", service_uid, candidate_gid, 0o550)
                directories += 1
                require(directories <= MAX_DIRS, "source directory count exceeds bound")
                append(metadata(prefix or ".", start))
                children = sorted(os.listdir(current))
                frames[-1] = (current, prefix, start, children, 0)
                continue
            if index == len(children):
                require(settled_identity(os.fstat(current)) == settled_identity(start),
                        f"source directory changed: {prefix}")
                frames.pop()
                if frames:
                    parent = frames[-1][0]
                    recheck(parent, prefix.rsplit("/", 1)[-1], current, start, prefix, settled=True)
                    os.close(current)
                continue
            name = children[index]
            frames[-1] = (current, prefix, start, children, index + 1)
            path = f"{prefix}/{name}" if prefix else name
            require(name not in ("", ".", "..") and "/" not in name and
                    0 < len(os.fsencode(path)) <= MAX_PATH and len(frames) < MAX_DEPTH,
                    "source path or depth bound exceeded")
            before = os.stat(name, dir_fd=current, follow_symlinks=False)
            if stat.S_ISDIR(before.st_mode):
                child, opened = open_directory(current, name, path, service_uid, candidate_gid, 0o550)
                require(settled_identity(opened) == settled_identity(before),
                        f"source directory changed before open: {path}")
                frames.append((child, path, before, None, 0))
            else:
                expected_mode = 0o440
                require(stat.S_ISREG(before.st_mode) and before.st_uid == service_uid and
                        before.st_gid == candidate_gid and stat.S_IMODE(before.st_mode) == expected_mode and
                        before.st_nlink == 1 and 0 <= before.st_size <= MAX_FILE,
                        f"source file metadata mismatch: {path}")
                descriptor = os.open(name, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC | os.O_NOFOLLOW,
                                     dir_fd=current)
                try:
                    require(settled_identity(os.fstat(descriptor)) == settled_identity(before) and
                            settled_identity(os.stat(name, dir_fd=current, follow_symlinks=False)) ==
                            settled_identity(before), f"source file changed or swapped: {path}")
                finally:
                    os.close(descriptor)
                if path != ".source-manifest":
                    require(path in manifest and path not in seen, f"unlisted source file: {path}")
                    seen.add(path)
                    total += before.st_size
                    require(len(seen) <= MAX_FILES and total <= MAX_TOTAL,
                            "source file count or total bytes exceeds bound")
                record = metadata(path, before)
                if path in manifest:
                    record["declared_sha256"] = manifest[path]
                append(record)
        expected_dirs = {"."}
        for path in manifest:
            parts = path.split("/")
            expected_dirs.update("/".join(parts[:index]) for index in range(1, len(parts)))
        observed_dirs = {node["path"] for node in nodes if node["type"] == "directory"}
        require(observed_dirs == expected_dirs, "unlisted or missing source directory")
        require(seen == set(manifest) and sum(n["path"] == ".source-manifest" for n in nodes) == 1,
                "source manifest entries or seal file missing")
        return {"root": label, "nodes": nodes, "files": len(seen),
                "directories": directories, "source_bytes": total,
                "scope": "complete sealed-source metadata census and declared digests; file contents are not rehashed"}
    finally:
        for current, path, _, _, _ in frames:
            if path:
                os.close(current)


def exact_workspace_seal(fd: int, job: int, attempt: int, request_sha: str,
                         baseline: str, candidate: str, service_uid: int,
                         candidate_gid: int) -> dict:
    expected = (f"BQ-WORKSPACE-V1\njob={job}\ntoken={attempt}\nrequest={request_sha}\n"
                f"recipe=validate-buster-v1\nbase={baseline}\ncandidate={candidate}\n").encode()
    raw, info = read_file(fd, ".identity", ".identity", 512, service_uid, candidate_gid, 0o400)
    require(raw == expected, "workspace seal content mismatch")
    return {**metadata(".identity", info), "sha256": hashlib.sha256(raw).hexdigest()}


def accounts() -> tuple[int, int, int, int]:
    service = pwd.getpwnam("buster-bench")
    candidate = pwd.getpwnam("buster-bench-candidate")
    require(service.pw_uid != candidate.pw_uid and service.pw_uid != ROOT_UID and
            candidate.pw_uid != ROOT_UID and candidate.pw_gid != 0,
            "service/candidate numeric identities alias or are privileged")
    return service.pw_uid, service.pw_gid, candidate.pw_uid, candidate.pw_gid


def _open_workspace(identity_record: dict, stack: ExitStack,
                    service_uid: int, candidate_gid: int) -> tuple[int, int, os.stat_result, os.stat_result]:
    parent = probe._safe_dir(WORKSPACES, service_uid, candidate_gid, 0o2710)
    stack.callback(os.close, parent)
    root_info = os.fstat(parent)
    leaf = f"job-{identity_record['job']}-attempt-{identity_record['attempt']}"
    attempt, attempt_info = open_directory(parent, leaf, leaf, service_uid, candidate_gid, 0o2710)
    stack.callback(os.close, attempt)
    return parent, attempt, root_info, attempt_info


def _workspace_stable(parent: int, attempt: int, parent_info: os.stat_result,
                      attempt_info: os.stat_result,
                      identity_record: dict) -> None:
    leaf = f"job-{identity_record['job']}-attempt-{identity_record['attempt']}"
    recheck(parent, leaf, attempt, attempt_info, leaf)
    require(identity(os.fstat(parent)) == identity(parent_info), "workspace parent metadata changed")
    reopened = probe._open_dir_nofollow(WORKSPACES)
    try:
        require(identity(os.fstat(reopened)) == identity(parent_info),
                "canonical workspace parent changed")
    finally:
        os.close(reopened)


def fixed_metadata(parent: int, name: str, path: str, uid: int, gid: int,
                   mode: int | None, required: bool) -> tuple[dict, int]:
    try:
        before = os.stat(name, dir_fd=parent, follow_symlinks=False)
    except FileNotFoundError:
        require(not required, f"required workspace entry absent: {path}")
        return {"path": path, "presence": "absent"}, -1
    require(stat.S_ISDIR(before.st_mode) and before.st_uid == uid and before.st_gid == gid and
            before.st_nlink >= 2 and (mode is None or stat.S_IMODE(before.st_mode) == mode),
            f"fixed workspace metadata mismatch: {path}")
    fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                 dir_fd=parent)
    try:
        require(identity(os.fstat(fd)) == identity(before), f"fixed workspace entry swapped: {path}")
    except BaseException:
        os.close(fd)
        raise
    return {**metadata(path, before), "presence": "present"}, fd


def fixed_absent(parent: int, name: str, path: str) -> dict:
    try:
        os.stat(name, dir_fd=parent, follow_symlinks=False)
    except FileNotFoundError:
        return {"path": path, "presence": "absent"}
    raise probe.ProbeError(f"fixed workspace path must be absent: {path}")


def fixed_file(parent: int, name: str, path: str, uid: int, gid: int,
               mode: int | None, required: bool) -> dict:
    try:
        before = os.stat(name, dir_fd=parent, follow_symlinks=False)
    except FileNotFoundError:
        require(not required, f"required workspace file absent: {path}")
        return {"path": path, "presence": "absent"}
    require(stat.S_ISREG(before.st_mode) and before.st_uid == uid and before.st_gid == gid and
            before.st_nlink == 1 and before.st_size > 0 and
            (mode is None or stat.S_IMODE(before.st_mode) == mode),
            f"fixed workspace file metadata mismatch: {path}")
    # O_PATH pins metadata only. Never read or mutate an active stage output.
    descriptor = os.open(name, os.O_PATH | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
    try:
        require(settled_identity(os.fstat(descriptor)) == settled_identity(before) and
                settled_identity(os.stat(name, dir_fd=parent, follow_symlinks=False)) ==
                settled_identity(before), f"fixed workspace file changed or swapped: {path}")
    finally:
        os.close(descriptor)
    return {**metadata(path, before), "presence": "present"}


def base_build_workspace(identity_record: dict, baseline: str, candidate: str,
                         deadline: float) -> dict:
    """One complete settled materializer product at an externally pinned live checkpoint."""
    require(REVISION.fullmatch(baseline) is not None and REVISION.fullmatch(candidate) is not None,
            "invalid source revision")
    service_uid, _, _, candidate_gid = accounts()
    job, attempt, request_sha = (identity_record[key] for key in ("job", "attempt", "request_sha256"))
    require(job > 0 and attempt > 0 and probe.HEX64.fullmatch(request_sha) is not None,
            "invalid attempt identity")
    before = probe._open_records(job, request_sha)[2]
    require(before == identity_record, "workspace record or boot identity changed before capture")
    with ExitStack() as stack:
        parent, attempt_fd, parent_info, attempt_info = _open_workspace(before, stack, service_uid, candidate_gid)
        result = {"checkpoint": "live-base-build-settled-materializer-product",
                  "job": job, "attempt": attempt, "boot_id": before["boot_id"],
                  "worker_sha256": before["worker_sha256"], "instance_sha256": before["instance_sha256"],
                  "request_sha256": request_sha,
                  "paths": [metadata("workspaces", os.fstat(parent)),
                            metadata(f"job-{job}-attempt-{attempt}", attempt_info)],
                  "seal": exact_workspace_seal(attempt_fd, job, attempt, request_sha,
                                                baseline, candidate, service_uid, candidate_gid),
                  "sources": {}}
        for subject, revision in (("base", baseline), ("candidate", candidate)):
            probe._remaining(deadline, 0)
            sub_fd, sub_info = open_directory(attempt_fd, subject, subject,
                                              service_uid, candidate_gid, 0o2710)
            with ExitStack() as child_stack:
                child_stack.callback(os.close, sub_fd)
                source_fd, source_info = open_directory(sub_fd, "source", f"{subject}/source",
                                                        service_uid, candidate_gid, 0o550)
                child_stack.callback(os.close, source_fd)
                build_fd, build_info = open_directory(sub_fd, "build", f"{subject}/build",
                                                      service_uid, candidate_gid, 0o2700)
                child_stack.callback(os.close, build_fd)
                installed = probe._open_dir_nofollow(f"{INSTALLED}/sources/{revision}")
                child_stack.callback(os.close, installed)
                installed_info = os.fstat(installed)
                require(stat.S_ISDIR(installed_info.st_mode) and installed_info.st_uid == ROOT_UID and
                        stat.S_IMODE(installed_info.st_mode) & 0o222 == 0,
                        f"installed source directory metadata mismatch: {revision}")
                installed_raw, installed_manifest = read_file(installed, "source.manifest", "installed manifest",
                                                               MAX_MANIFEST, ROOT_UID, None, None)
                copied_raw, copied_manifest = read_file(source_fd, ".source-manifest", ".source-manifest",
                                                         MAX_MANIFEST, service_uid, candidate_gid, 0o440)
                require(installed_raw == copied_raw, f"copied source manifest differs from installed {revision}")
                manifest_entries = source_manifest(copied_raw, revision)
                inventory = source_inventory(source_fd, f"{subject}/source", manifest_entries,
                                             service_uid, candidate_gid, deadline)
                inventory.update({"revision": revision,
                                  "manifest_sha256": hashlib.sha256(copied_raw).hexdigest(),
                                  "installed_manifest": metadata("installed/source.manifest", installed_manifest),
                                  "copied_manifest": metadata(".source-manifest", copied_manifest)})
                result["sources"][subject] = inventory
                result["paths"].extend([metadata(subject, sub_info), metadata(f"{subject}/source", source_info),
                                        metadata(f"{subject}/build", build_info)])
                recheck(sub_fd, "source", source_fd, source_info, f"{subject}/source", settled=True)
                recheck(sub_fd, "build", build_fd, build_info, f"{subject}/build")
                recheck(attempt_fd, subject, sub_fd, sub_info, subject)
                require(settled_identity(os.fstat(installed)) == settled_identity(installed_info),
                        f"installed source directory changed: {revision}")
                reopened = probe._open_dir_nofollow(f"{INSTALLED}/sources/{revision}")
                try:
                    require(identity(os.fstat(reopened)) == identity(installed_info),
                            f"installed source directory replaced: {revision}")
                finally:
                    os.close(reopened)
        candidate_fd, candidate_info = open_directory(attempt_fd, "candidate", "candidate",
                                                      service_uid, candidate_gid, 0o2710)
        with ExitStack() as child_stack:
            child_stack.callback(os.close, candidate_fd)
            entry, stage_fd = fixed_metadata(candidate_fd, "staging", "candidate/staging",
                                             service_uid, candidate_gid, 0o2770, True)
            child_stack.callback(os.close, stage_fd)
            output, output_fd = fixed_metadata(stage_fd, "throughput-results",
                                               "candidate/staging/throughput-results",
                                               service_uid, candidate_gid, 0o2770, True)
            child_stack.callback(os.close, output_fd)
            result["paths"].extend([entry, output])
            recheck_fixed(stage_fd, "throughput-results", output_fd, output)
            recheck_fixed(candidate_fd, "staging", stage_fd, entry)
            recheck(attempt_fd, "candidate", candidate_fd, candidate_info, "candidate")
        _workspace_stable(parent, attempt_fd, parent_info, attempt_info, before)
    require(probe._open_records(job, request_sha)[2] == before,
            "workspace record or boot identity changed after capture")
    result["complete_monotonic_us"] = time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1000
    require(len(json.dumps(result, sort_keys=True).encode()) + 1 <= MAX_EVIDENCE,
            "workspace evidence exceeds byte bound")
    return result


def stage_handoff(identity_record: dict, stage: str, deadline: float) -> dict:
    """Fixed metadata during one exact live candidate-build/throughput invocation."""
    require(stage in ("candidate-build", "throughput"), "unexpected workspace stage")
    service_uid, _, candidate_uid, candidate_gid = accounts()
    before = probe._open_records(identity_record["job"], identity_record["request_sha256"])[2]
    require(before == identity_record, "workspace stage record or boot identity changed before capture")
    with ExitStack() as stack:
        parent, attempt_fd, parent_info, attempt_info = _open_workspace(before, stack, service_uid, candidate_gid)
        candidate_fd, candidate_info = open_directory(attempt_fd, "candidate", "candidate",
                                                      service_uid, candidate_gid, 0o2710)
        stack.callback(os.close, candidate_fd)
        result = {"checkpoint": f"live-{stage}-fixed-workspace-paths", "job": before["job"],
                  "attempt": before["attempt"], "boot_id": before["boot_id"],
                  "worker_sha256": before["worker_sha256"],
                  "instance_sha256": before["instance_sha256"],
                  "scope": "fixed metadata at the checkpoint only; active output contents are not read",
                  "paths": []}
        build_mode = 0o2700 if stage == "candidate-build" else 0o550
        build, build_fd = fixed_metadata(candidate_fd, "build", "candidate/build",
                                         service_uid, candidate_gid, build_mode, True)
        stack.callback(os.close, build_fd)
        staging, staging_fd = fixed_metadata(candidate_fd, "staging", "candidate/staging",
                                             service_uid, candidate_gid, 0o2770, True)
        stack.callback(os.close, staging_fd)
        if stage == "candidate-build":
            # candidate-generate removes and recreates the staging tree. The
            # service recreates this handoff directory after candidate-build.
            output = fixed_absent(staging_fd, "throughput-results",
                                  "candidate/staging/throughput-results")
            output_fd = -1
        else:
            output, output_fd = fixed_metadata(staging_fd, "throughput-results",
                                               "candidate/staging/throughput-results",
                                               service_uid, candidate_gid, 0o2770, True)
            stack.callback(os.close, output_fd)
        result["paths"].extend([build, staging, output])
        # A candidate build output can appear at any point during its unit; it
        # is never opened/read while active. The final trusted copy must exist
        # before the throughput unit can run, but no content is inferred here.
        for label, directory, required, mode in (
                ("candidate/staging/Release/ide", staging_fd, stage == "throughput", None),
                ("candidate/build/Release/ide", build_fd, stage == "throughput", 0o550 if stage == "throughput" else None)):
            release, release_fd = fixed_metadata(directory, "Release", label.rsplit("/", 1)[0],
                                                  service_uid if directory == build_fd else candidate_uid,
                                                  candidate_gid, 0o550 if stage == "throughput" and directory == build_fd else None,
                                                  required)
            if release_fd >= 0:
                stack.callback(os.close, release_fd)
                file_info = fixed_file(release_fd, "ide", label,
                                       service_uid if directory == build_fd else candidate_uid,
                                       candidate_gid, mode, required)
                result["paths"].append(file_info)
                recheck_fixed_file(release_fd, "ide", file_info)
                recheck_fixed(directory, "Release", release_fd, release)
            else:
                result["paths"].append({"path": label, "presence": "absent"})
                recheck_absent(directory, "Release", release)
            result["paths"].append(release)
        for child_parent, leaf, fd, record in (
                (candidate_fd, "staging", staging_fd, staging),
                (candidate_fd, "build", build_fd, build)):
            recheck_fixed(child_parent, leaf, fd, record)
        if output_fd >= 0:
            recheck_fixed(staging_fd, "throughput-results", output_fd, output)
        else:
            recheck_absent(staging_fd, "throughput-results", output)
        recheck(attempt_fd, "candidate", candidate_fd, candidate_info, "candidate")
        _workspace_stable(parent, attempt_fd, parent_info, attempt_info, before)
        probe._remaining(deadline, 0)
    require(probe._open_records(before["job"], before["request_sha256"])[2] == before,
            "workspace stage record or boot identity changed after capture")
    result["complete_monotonic_us"] = time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1000
    require(len(json.dumps(result, sort_keys=True).encode()) + 1 <= MAX_EVIDENCE,
            "stage workspace evidence exceeds byte bound")
    return result


def self_test() -> None:
    """Exercise production readers against real descriptor-backed fixtures."""
    checks = 0
    def reject(operation, reason: str) -> None:
        nonlocal checks
        try:
            operation()
        except (OSError, ValueError, probe.ProbeError) as exc:
            require(reason in str(exc), f"expected {reason!r}; got {exc!r}")
        else:
            raise AssertionError(f"expected rejection: {reason}")
        checks += 1

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        user, group = os.getuid(), os.getgid()
        workspaces = root / "workspaces"
        attempt = workspaces / "job-7-attempt-2"
        workspaces.mkdir()
        attempt.mkdir()
        for directory in (workspaces, attempt):
            os.chmod(directory, 0o2710)
        revisions = ("1" * 40, "2" * 40)
        installed = root / "installed"
        identity_record = {"job": 7, "attempt": 2, "request_sha256": "a" * 64,
                           "boot_id": "b" * 36, "worker_sha256": "c" * 64,
                           "instance_sha256": "d" * 64}
        seal = (f"BQ-WORKSPACE-V1\njob=7\ntoken=2\nrequest={'a' * 64}\n"
                f"recipe=validate-buster-v1\nbase={revisions[0]}\ncandidate={revisions[1]}\n").encode()
        (attempt / ".identity").write_bytes(seal)
        os.chmod(attempt / ".identity", 0o400)
        for subject, revision in zip(("base", "candidate"), revisions):
            sub = attempt / subject
            source = sub / "source"
            build = sub / "build"
            (source / "nested").mkdir(parents=True)
            build.mkdir()
            entries = {"foo.c": b"fixture-one", "nested/bar.h": b"fixture-two"}
            manifest = (f"BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision={revision}\n" +
                        "".join(f"{hashlib.sha256(value).hexdigest()} {name}\n"
                                for name, value in sorted(entries.items()))).encode()
            installed_revision = installed / "sources" / revision
            installed_revision.mkdir(parents=True)
            (installed_revision / "source.manifest").write_bytes(manifest)
            os.chmod(installed_revision / "source.manifest", 0o440)
            os.chmod(installed_revision, 0o550)
            (source / ".source-manifest").write_bytes(manifest)
            os.chmod(source / ".source-manifest", 0o440)
            for name, value in entries.items():
                (source / name).write_bytes(value)
                os.chmod(source / name, 0o440)
            for directory in (source / "nested", source):
                os.chmod(directory, 0o550)
            os.chmod(build, 0o2700)
            os.chmod(sub, 0o2710)
        staging = attempt / "candidate" / "staging"
        output = staging / "throughput-results"
        output.mkdir(parents=True)
        os.chmod(staging, 0o2770)
        os.chmod(output, 0o2770)
        deadline = time.monotonic() + 30
        def records(*args, **kwargs):
            return b"worker", b"instance", identity_record
        def observe():
            return base_build_workspace(identity_record, *revisions, deadline)
        def stage(name: str):
            return stage_handoff(identity_record, name, deadline)
        real_accounts = accounts
        # The source root is restored for TemporaryDirectory cleanup under an
        # ordinary hosted UID; all production captures remain read-only.
        try:
            with patch(__name__ + ".WORKSPACES", str(workspaces)), \
                 patch(__name__ + ".INSTALLED", str(installed)), \
                 patch(__name__ + ".ROOT_UID", user), \
                 patch(__name__ + ".accounts", return_value=(user, group, user, group)), \
                 patch.object(probe, "_open_records", side_effect=records), \
                 patch.object(pwd, "getpwnam", return_value=SimpleNamespace(pw_uid=user, pw_gid=group)):
                positive = observe()
                assert positive["sources"]["base"]["files"] == 2
                assert positive["sources"]["candidate"]["directories"] == 2
                assert {p["path"] for p in positive["paths"]} >= {
                    "candidate/staging", "candidate/staging/throughput-results", "candidate/build"}
                prepared_output = next(p for p in positive["paths"] if p["path"] ==
                                       "candidate/staging/throughput-results")
                assert prepared_output["presence"] == "present"
                checks += 1
                # candidate-generate calls generate_add(), which removes and
                # recreates candidate/staging before candidate-build starts.
                output.rmdir()
                staging.rmdir()
                staging.mkdir()
                os.chmod(staging, 0o2770)
                candidate_build = stage("candidate-build")
                assert next(p for p in candidate_build["paths"] if p["path"] ==
                            "candidate/staging/throughput-results")["presence"] == "absent"
                assert next(p for p in candidate_build["paths"] if p["path"] ==
                            "candidate/build/Release/ide")["presence"] == "absent"
                checks += 1
                output.mkdir()
                os.chmod(output, 0o700)
                reject(lambda: stage("candidate-build"), "fixed workspace path must be absent")
                output.rmdir()
                output.symlink_to(attempt / "candidate" / "build", target_is_directory=True)
                reject(lambda: stage("candidate-build"), "fixed workspace path must be absent")
                output.unlink()
                real_recheck_absent = recheck_absent
                def appear_during_capture(parent: int, name: str, record: dict) -> None:
                    if record["path"] == "candidate/staging/throughput-results":
                        output.mkdir()
                        os.chmod(output, 0o2770)
                    real_recheck_absent(parent, name, record)
                with patch(__name__ + ".recheck_absent", side_effect=appear_during_capture):
                    reject(lambda: stage("candidate-build"), "fixed workspace path appeared during capture")
                output.rmdir()
                stage_release = staging / "Release"
                stage_release.mkdir()
                (stage_release / "ide").write_bytes(b"candidate-output")
                os.chmod(stage_release, 0o750)
                os.chmod(stage_release / "ide", 0o750)
                final = attempt / "candidate" / "build"
                final_release = final / "Release"
                final_release.mkdir()
                (final_release / "ide").write_bytes(b"trusted-copy")
                os.chmod(final_release / "ide", 0o550)
                os.chmod(final_release, 0o550)
                os.chmod(final, 0o550)
                output.mkdir()
                os.chmod(output, 0o2770)
                throughput = stage("throughput")
                assert next(p for p in throughput["paths"] if p["path"] ==
                            "candidate/staging/throughput-results")["presence"] == "present"
                assert next(p for p in throughput["paths"] if p["path"] ==
                            "candidate/build/Release/ide")["presence"] == "present"
                checks += 1
                os.chmod(final, 0o2700)
                reject(lambda: stage("throughput"), "fixed workspace metadata mismatch")
                os.chmod(final, 0o550)
                os.chmod(output, 0o2700)
                reject(lambda: stage("throughput"), "fixed workspace metadata mismatch")
                os.chmod(output, 0o2770)
                os.chmod(final, 0o550)
                output.rmdir()
                reject(lambda: stage("throughput"), "required workspace entry absent")
                os.chmod(final, 0o2700)
                reject(lambda: observe(), "required workspace entry absent")
                output.mkdir()
                os.chmod(output, 0o2770)
                os.chmod(attempt / "candidate" / "source", 0o750)
                link = attempt / "candidate" / "source" / "linked"
                link.symlink_to("foo.c")
                os.chmod(attempt / "candidate" / "source", 0o550)
                reject(observe, "source file metadata mismatch")
                os.chmod(attempt / "candidate" / "source", 0o750)
                link.unlink()
                os.chmod(attempt / "candidate" / "source", 0o550)
                os.chmod(attempt / "candidate" / "source", 0o750)
                extra_directory = attempt / "candidate" / "source" / "unlisted"
                extra_directory.mkdir()
                os.chmod(extra_directory, 0o550)
                os.chmod(attempt / "candidate" / "source", 0o550)
                reject(observe, "unlisted or missing source directory")
                os.chmod(attempt / "candidate" / "source", 0o750)
                extra_directory.rmdir()
                os.chmod(attempt / "candidate" / "source", 0o550)
                source_file = attempt / "base" / "source" / "foo.c"
                os.chmod(source_file, 0o640)
                reject(observe, "source file metadata mismatch")
                os.chmod(source_file, 0o440)
                os.chmod(attempt / ".identity", 0o600)
                (attempt / ".identity").write_bytes(seal.replace(b"token=2", b"token=3"))
                os.chmod(attempt / ".identity", 0o400)
                reject(observe, "workspace seal content mismatch")
                os.chmod(attempt / ".identity", 0o600)
                (attempt / ".identity").write_bytes(seal)
                os.chmod(attempt / ".identity", 0o400)
                with patch(__name__ + ".accounts", return_value=(user + 1, group, user, group)):
                    reject(observe, "directory identity mismatch")
                with patch(__name__ + ".accounts", return_value=(user, group, user, group + 1)):
                    reject(observe, "directory identity mismatch")
                with patch(__name__ + ".MAX_FILES", 1):
                    reject(observe, "manifest entry")
                with patch(__name__ + ".MAX_DIRS", 1):
                    reject(observe, "source directory count")
                with patch(__name__ + ".MAX_EVIDENCE", 100):
                    reject(observe, "source evidence node or byte bound")
                changed = {**identity_record, "worker_sha256": "f" * 64}
                with patch.object(probe, "_open_records", return_value=(b"", b"", changed)):
                    reject(observe, "record or boot identity changed before")
                with patch.object(probe, "_open_records", side_effect=((b"", b"", identity_record),
                                                                         (b"", b"", changed))):
                    reject(observe, "record or boot identity changed after")
                real_inventory = source_inventory
                source_path = attempt / "base" / "source"
                retired_path = attempt / "base" / "source-retired"
                def replace_after_walk(*args, **kwargs):
                    result = real_inventory(*args, **kwargs)
                    if args[1] == "base/source":
                        source_path.rename(retired_path)
                        source_path.mkdir()
                        os.chmod(source_path, 0o550)
                    return result
                try:
                    with patch(__name__ + ".source_inventory", side_effect=replace_after_walk):
                        reject(observe, "directory changed or replaced")
                finally:
                    if retired_path.exists():
                        source_path.rmdir()
                        retired_path.rename(source_path)
                another = root / "another-workspaces"
                another.mkdir()
                os.chmod(another, 0o2710)
                original_open = probe._open_dir_nofollow
                def swapped(path: str) -> int:
                    return original_open(str(another) if path == str(workspaces) else path)
                with patch.object(probe, "_open_dir_nofollow", side_effect=swapped):
                    reject(observe, "canonical workspace parent changed")
                with patch.object(pwd, "getpwnam", side_effect=(SimpleNamespace(pw_uid=user + 1, pw_gid=group),
                                                               SimpleNamespace(pw_uid=user + 1, pw_gid=group))):
                    reject(real_accounts, "numeric identities alias")
                assert observe()["sources"]["base"]["files"] == 2
                checks += 1
        finally:
            for item in sorted(root.rglob("*"), key=lambda p: len(p.parts), reverse=True):
                if item.is_dir() and not item.is_symlink():
                    os.chmod(item, 0o700)
    print(f"WORKSPACE_OBSERVER_SELF_TEST checks={checks} failures=0 fixtures-only-not-live-proof")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    else:
        print("usage: issue1162_workspace_observer.py --self-test", file=sys.stderr)
        raise SystemExit(2)
