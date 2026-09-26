#!/usr/bin/env python3
"""Read-only, bounded stage observations for one disposable #1162 hosted attempt.

The adjacent live probe owns the record and process-capture formats. This
observer never submits work, changes a unit, or treats a manifest as live proof.
"""
from __future__ import annotations

import argparse
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
import issue1162_workspace_observer as workspace_observer

STAGES = ("base-generate", "base-build", "candidate-generate", "candidate-build", "throughput")
BUILD_STAGES = ("base-build", "candidate-build")
MAX_MANIFEST = 32768
MAX_NODES = 100000
MAX_DEPTH = 256
MAX_PATH = 1024
MAX_INVENTORY_BYTES = 64 * 1024 * 1024
MAX_BINARY = 128 * 1024 * 1024
MAX_HASHED_BYTES = 2 * 1024 * 1024 * 1024
POLL_SECONDS = 0.12
WORKSPACES = "/var/lib/buster-bench/workspaces"
HEX_REVISION = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})\Z")


class ManifestPending(FileNotFoundError):
    """The published target still has the recipe's transient temporary link."""


def require(ok: bool, reason: str) -> None:
    if not ok:
        raise probe.ProbeError(reason)


def stage_unit(job: int, attempt: int, stage: str) -> str:
    require(stage in STAGES and job > 0 and attempt > 0, "invalid stage identity")
    return f"buster-bench-{job}-{attempt}-{stage}.service"


def record_identity(job: int, request_sha: str, expected: dict | None = None,
                    output: Path | None = None, prefix: str = "") -> dict:
    worker, instance, identity = probe._open_records(job, request_sha, output, prefix)
    if expected is not None:
        require(identity == expected, "worker/instance/boot identity changed")
    return identity


def capture_stage(stage: str, identity: dict, output: Path, deadline: float) -> dict:
    unit = stage_unit(identity["job"], identity["attempt"], stage)
    label = f"stage-{stage}"
    before = record_identity(identity["job"], identity["request_sha256"], identity,
                             output, f"{label}-before-")
    snapshot = probe.capture_unit(unit, output, label,
                                  min(1.5, probe._remaining(deadline)), deadline, reserve=0)
    captured = probe.validate_build(snapshot, unit)
    props = snapshot["properties"]
    require(props.get("Id") == unit and captured["cgroup"] ==
            f"/buster.slice/buster-bench.slice/{unit}", "unexpected stage unit or cgroup")
    require(snapshot.get("cgroup_root", {}).get("device") == identity["cgroup_root_device"] and
            snapshot.get("cgroup_root", {}).get("inode") == identity["cgroup_root_inode"] and
            snapshot.get("bench_slice", {}).get("device") == identity["slice_device"] and
            snapshot.get("bench_slice", {}).get("inode") == identity["slice_inode"],
            "stage cgroup ancestry changed")
    workspace_artifact = None
    workspace_sha256 = None
    if stage in ("candidate-build", "throughput"):
        observed = workspace_observer.stage_handoff(identity, stage, deadline)
        after_workspace = probe.capture_unit(unit, output, f"{label}-after-workspace",
                                             min(1.5, probe._remaining(deadline)), deadline, reserve=0)
        require(probe.same_build(captured, probe.validate_build(after_workspace, unit)),
                "stage invocation, process, or cgroup changed during workspace observation")
        workspace_artifact = f"{stage}-workspace.json"
        content = (json.dumps(observed, sort_keys=True, indent=2) + "\n").encode()
        require(len(content) <= workspace_observer.MAX_EVIDENCE,
                "stage workspace artifact exceeds byte bound")
        probe._write_private(output, workspace_artifact, content)
        workspace_sha256 = hashlib.sha256(content).hexdigest()
    after = record_identity(identity["job"], identity["request_sha256"], before,
                            output, f"{label}-after-")
    require(after == before, "stage record identity changed during capture")
    require(int(props.get("ExecMainStartTimestampMonotonic", "0")) > 0,
            "stage monotonic start timestamp missing")
    return {"stage": stage, "unit": unit, "identity": captured,
            "boot_id": identity["boot_id"],
            "exec_main_start_monotonic_us": int(props["ExecMainStartTimestampMonotonic"]),
            "record_worker_sha256": identity["worker_sha256"],
            "record_instance_sha256": identity["instance_sha256"],
            "workspace_artifact": workspace_artifact,
            "workspace_sha256": workspace_sha256}


def write_json(output: Path, name: str, obj: object) -> None:
    probe._write_private(output, name, (json.dumps(obj, sort_keys=True, indent=2) + "\n").encode())


def read_stage_manifest(identity: dict, baseline: str, subject: str, stage: str,
                        output: Path | None = None) -> dict:
    """Accept only a published, exact-attempt success checkpoint."""
    require(stage in BUILD_STAGES, "unexpected manifest stage")
    job, attempt = identity["job"], identity["attempt"]
    root = f"{WORKSPACES}/results/job-{job}-attempt-{attempt}"
    fd = probe._open_dir_nofollow(root)
    try:
        service = pwd.getpwnam("buster-bench")
        info = os.fstat(fd)
        require(stat.S_ISDIR(info.st_mode) and info.st_uid == service.pw_uid and
                stat.S_IMODE(info.st_mode) == 0o700,
                "result directory has unexpected owner or writable group/world")
        name = f"validate-buster-v1.{stage}.manifest"
        try:
            raw, file_info = probe._read_variable_snapshot(fd, name, MAX_MANIFEST,
                                                           service.pw_uid, service.pw_gid, 0o400)
        except probe.ProbeError as exc:
            if str(exc) != f"invalid file metadata: {name}":
                raise
            provisional = os.stat(name, dir_fd=fd, follow_symlinks=False)
            if (stat.S_ISREG(provisional.st_mode) and provisional.st_uid == service.pw_uid and
                    provisional.st_gid == service.pw_gid and stat.S_IMODE(provisional.st_mode) == 0o400 and
                    provisional.st_nlink == 2 and 0 < provisional.st_size <= MAX_MANIFEST):
                raise ManifestPending("provisional manifest link awaiting unlink") from exc
            raise
        require(stat_identity(os.fstat(fd)) == stat_identity(info),
                "result directory changed during manifest read")
    finally:
        os.close(fd)
    reopened = probe._open_dir_nofollow(root)
    try:
        require(stat_identity(os.fstat(reopened)) == stat_identity(info),
                "canonical result directory replaced during manifest read")
    finally:
        os.close(reopened)
    if output is not None:
        probe._write_private(output, f"{stage}-published-manifest.txt", raw)
    require(raw.endswith(b"\n") and b"\x00" not in raw and b"\r" not in raw,
            "malformed stage manifest bytes")
    try:
        lines = raw[:-1].decode("ascii").split("\n")
    except UnicodeDecodeError as exc:
        raise probe.ProbeError("non-ASCII stage manifest") from exc
    fields = {}
    for line in lines:
        key, sep, value = line.partition("=")
        require(sep == "=" and key and key not in fields and "\n" not in value,
                "malformed or duplicate stage manifest field")
        fields[key] = value
    attempt_root = f"{WORKSPACES}/job-{job}-attempt-{attempt}"
    expected = {"schema": "1", "recipe": "validate-buster-v1", "status": "running",
                "process-result": "success", "stage": stage,
                "job-id": str(job), "attempt-token": str(attempt),
                "workspace-root": WORKSPACES, "result-root": root,
                "base-revision": baseline, "candidate-revision": subject,
                "base-build": f"{attempt_root}/base/build",
                "candidate-build": f"{attempt_root}/candidate/build",
                "base-binary": f"{attempt_root}/base/build/Release/ide",
                "candidate-binary": f"{attempt_root}/candidate/build/Release/ide"}
    for key, value in expected.items():
        require(fields.get(key) == value, f"stage manifest {key} mismatch")
    digest_field = "base-binary-sha256" if stage == "base-build" else "candidate-binary-sha256"
    require(probe.HEX64.fullmatch(fields.get(digest_field, "")) is not None,
            "stage manifest binary SHA-256 missing")
    fields["manifest_sha256"] = hashlib.sha256(raw).hexdigest()
    fields["manifest_identity"] = file_info
    return fields


def stat_identity(info: os.stat_result) -> tuple[int, ...]:
    return (info.st_dev, info.st_ino, info.st_mode, info.st_uid, info.st_gid,
            info.st_nlink, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def node_record(path: str, info: os.stat_result, allowed_uids: set[int],
                allowed_gids: set[int], is_dir: bool) -> dict:
    mode = stat.S_IMODE(info.st_mode)
    require((stat.S_ISDIR(info.st_mode) if is_dir else stat.S_ISREG(info.st_mode)) and
            info.st_uid in allowed_uids and info.st_gid in allowed_gids and
            (mode == 0o550 if is_dir else mode in (0o440, 0o550)) and
            (info.st_nlink >= 1 if is_dir else info.st_nlink == 1) and
            0 <= info.st_size <= MAX_BINARY and 0 <= len(os.fsencode(path)) <= MAX_PATH,
            f"unsafe, writable, special, linked, or oversized build node: {path}")
    return {"path": path, "type": "dir" if is_dir else "file", "device": info.st_dev,
            "inode": info.st_ino, "uid": info.st_uid, "gid": info.st_gid,
            "mode": mode, "links": info.st_nlink, "size": info.st_size,
            "mtime_ns": info.st_mtime_ns, "ctime_ns": info.st_ctime_ns}


def hash_pinned_binary(parent: int, name: str, expected: os.stat_result,
                       deadline: float) -> str:
    fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                 dir_fd=parent)
    try:
        require(stat_identity(os.fstat(fd)) == stat_identity(expected), "binary changed before hashing")
        digest = hashlib.sha256()
        total = 0
        while True:
            probe._remaining(deadline, 0)
            block = os.read(fd, 65536)
            if not block:
                break
            total += len(block)
            require(total <= MAX_BINARY, "binary hash exceeds byte bound")
            digest.update(block)
        require(total == expected.st_size and stat_identity(os.fstat(fd)) == stat_identity(expected) and
                stat_identity(os.stat(name, dir_fd=parent, follow_symlinks=False)) == stat_identity(expected),
                "binary changed during hashing")
        return digest.hexdigest()
    finally:
        os.close(fd)


def inventory_tree(path: str, digest: str, deadline: float,
                   allowed_uids: set[int], allowed_gids: set[int],
                   partial_path: Path | None = None) -> dict:
    """Complete bounded fd-anchored tree walk; no symlinks, samples, or omissions."""
    root = probe._open_dir_nofollow(path)
    try:
        root_info = os.fstat(root)
        entries = []
        encoded_bytes = 0
        binary_hash = None
        hashed_bytes = 0
        ledger = None
        if partial_path is not None:
            ledger = os.open(partial_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
        def append(record: dict) -> None:
            nonlocal encoded_bytes
            entries.append(record)
            encoded = (json.dumps(record, sort_keys=True) + "\n").encode()
            encoded_bytes += len(encoded)
            require(len(entries) <= MAX_NODES and encoded_bytes <= MAX_INVENTORY_BYTES,
                    "inventory node or serialization bound exceeded")
            if ledger is not None:
                require(os.write(ledger, encoded) == len(encoded), "short partial inventory write")
        # Frames hold an open directory until all of its children are checked.
        # Opening names relative to those fds prevents traversal redirection.
        frames = [(root, "", root_info, None, 0)]
        try:
            while frames:
                probe._remaining(deadline, 0)
                fd, relative, start, children, index = frames[-1]
                if children is None:
                    record = node_record(relative or ".", start, allowed_uids, allowed_gids, True)
                    append(record)
                    children = sorted(os.listdir(fd))
                    frames[-1] = (fd, relative, start, children, 0)
                    continue
                if index == len(children):
                    require(stat_identity(os.fstat(fd)) == stat_identity(start),
                            f"directory changed during inventory: {relative}")
                    frames.pop()
                    if frames:
                        parent, _, _, _, _ = frames[-1]
                        name = relative.rsplit("/", 1)[-1]
                        require(stat_identity(os.stat(name, dir_fd=parent, follow_symlinks=False)) ==
                                stat_identity(start), f"directory path replaced: {relative}")
                        os.close(fd)
                    continue
                name = children[index]
                frames[-1] = (fd, relative, start, children, index + 1)
                require(name not in ("", ".", "..") and "/" not in name,
                        "unsafe inventory name")
                child_path = f"{relative}/{name}" if relative else name
                require(len(os.fsencode(child_path)) <= MAX_PATH and
                        len(frames) < MAX_DEPTH, "inventory depth or path bound exceeded")
                info = os.stat(name, dir_fd=fd, follow_symlinks=False)
                if stat.S_ISDIR(info.st_mode):
                    node_record(child_path, info, allowed_uids, allowed_gids, True)
                    child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                                    dir_fd=fd)
                    if stat_identity(os.fstat(child)) != stat_identity(info):
                        os.close(child)
                        raise probe.ProbeError(f"directory swapped before open: {child_path}")
                    frames.append((child, child_path, info, None, 0))
                else:
                    record = node_record(child_path, info, allowed_uids, allowed_gids, False)
                    child = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                                    dir_fd=fd)
                    try:
                        require(stat_identity(os.fstat(child)) == stat_identity(info) and
                                stat_identity(os.stat(name, dir_fd=fd, follow_symlinks=False)) ==
                                stat_identity(info), f"file changed or swapped: {child_path}")
                    finally:
                        os.close(child)
                    if record["mode"] & 0o111:
                        hashed_bytes += info.st_size
                        require(hashed_bytes <= MAX_HASHED_BYTES,
                                "executable hash total byte bound exceeded")
                        record["sha256"] = hash_pinned_binary(fd, name, info, deadline)
                        if child_path == "Release/ide":
                            binary_hash = record["sha256"]
                    append(record)
        finally:
            for fd, relative, _, _, _ in frames:
                if relative:
                    os.close(fd)
            if ledger is not None:
                os.fsync(ledger)
                os.close(ledger)
        require(binary_hash == digest, "frozen build executable hash differs from stage manifest")
        fresh = probe._open_dir_nofollow(path)
        try:
            require(stat_identity(os.fstat(fresh)) == stat_identity(root_info),
                    "canonical build root changed during inventory")
        finally:
            os.close(fresh)
        return {"root": path, "nodes": entries, "node_count": len(entries),
                "binary_sha256": binary_hash, "manifest_binary_sha256": digest,
                "partial_ledger_bytes": encoded_bytes, "hashed_executable_bytes": hashed_bytes}
    finally:
        os.close(root)


def inventory_stage(identity: dict, stage: str, manifest: dict, deadline: float,
                    output: Path | None = None) -> dict:
    service = pwd.getpwnam("buster-bench")
    candidate = pwd.getpwnam("buster-bench-candidate")
    digest_field = "base-binary-sha256" if stage == "base-build" else "candidate-binary-sha256"
    root = manifest["base-build"] if stage == "base-build" else manifest["candidate-build"]
    before = record_identity(identity["job"], identity["request_sha256"], identity)
    result = inventory_tree(root, manifest[digest_field], deadline,
                            {0, service.pw_uid}, {0, service.pw_gid, candidate.pw_gid},
                            output / f"{stage}-inventory-partial.jsonl" if output else None)
    after = record_identity(identity["job"], identity["request_sha256"], before)
    require(before == after, "record identity changed during build inventory")
    result.update({"stage": stage, "boot_id": identity["boot_id"],
                   "manifest_sha256": manifest["manifest_sha256"],
                   "manifest_identity": manifest["manifest_identity"],
                   "complete_monotonic_us": time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1000})
    require(len(json.dumps(result, sort_keys=True, indent=2).encode()) + 1 <= MAX_INVENTORY_BYTES,
            "full inventory artifact exceeds byte bound")
    return result


def observe(job: int, request_sha: str, baseline: str, subject: str,
            budget: float, output: Path) -> dict:
    require(job > 0 and probe.HEX64.fullmatch(request_sha) is not None and
            HEX_REVISION.fullmatch(baseline) is not None and
            HEX_REVISION.fullmatch(subject) is not None and 1 <= budget <= 3900,
            "invalid observer arguments")
    deadline = time.monotonic() + budget
    outcome: dict = {"verdict": "OBSERVATION_INCONCLUSIVE", "job": job,
                     "request_sha256": request_sha, "baseline": baseline, "subject": subject,
                     "stages": {}, "inventories": {}, "causes": [],
                     "structural_capture_complete": False,
                     "inventory_before_throughput": False, "timing_causes": []}
    identity = None
    failed_stages: set[str] = set()
    failed_inventories: set[str] = set()
    provisional_since: dict[str, float] = {}
    saw_outer = False
    try:
        while identity is None and time.monotonic() < deadline:
            try:
                identity = record_identity(job, request_sha, output=output, prefix="initial-")
            except FileNotFoundError:
                time.sleep(min(POLL_SECONDS, max(0, deadline - time.monotonic())))
        require(identity is not None, "worker/instance record absent through deadline")
        outcome["identity"] = identity
        while time.monotonic() < deadline:
            for stage in STAGES:
                if stage in outcome["stages"] or stage in failed_stages:
                    continue
                unit = stage_unit(job, identity["attempt"], stage)
                props, raw = probe._systemd(unit, min(1.0, probe._remaining(deadline)))
                require(len(raw) <= 128 * 1024, "systemd stage response exceeds bound")
                if props.get("ActiveState") == "active" and props.get("MainPID", "0").isdecimal() and int(props["MainPID"]) > 0:
                    try:
                        captured = capture_stage(stage, identity, output, deadline)
                        outcome["stages"][stage] = captured
                    except (OSError, ValueError, probe.ProbeError) as exc:
                        add_cause(outcome, f"{stage} live capture: {exc}")
                        failed_stages.add(stage)
            # The finished-stage manifests trigger inventories. Never use them
            # to reconstruct a departed unit's missing live process evidence.
            for stage in BUILD_STAGES:
                if stage not in outcome["inventories"] and stage not in failed_inventories:
                    try:
                        manifest = read_stage_manifest(identity, baseline, subject, stage, output)
                    except ManifestPending:
                        provisional_since.setdefault(stage, time.monotonic())
                        if time.monotonic() - provisional_since[stage] > 3:
                            add_cause(outcome, f"{stage} provisional manifest did not settle")
                            failed_inventories.add(stage)
                    except FileNotFoundError:
                        pass
                    except (OSError, ValueError, probe.ProbeError) as exc:
                        add_cause(outcome, f"{stage} manifest: {exc}")
                        failed_inventories.add(stage)
                    else:
                        try:
                            inventory = inventory_stage(identity, stage, manifest, deadline, output)
                            artifact = f"{stage}-inventory.json"
                            content = (json.dumps(inventory, sort_keys=True, indent=2) + "\n").encode()
                            require(len(content) <= MAX_INVENTORY_BYTES, "inventory artifact exceeds byte bound")
                            probe._write_private(output, artifact, content)
                            outcome["inventories"][stage] = {
                                key: value for key, value in inventory.items() if key != "nodes"}
                            outcome["inventories"][stage].update({
                                "artifact": artifact, "artifact_sha256": hashlib.sha256(content).hexdigest()})
                        except (OSError, ValueError, probe.ProbeError) as exc:
                            add_cause(outcome, f"{stage} inventory: {exc}")
                            failed_inventories.add(stage)
            if len(outcome["stages"]) == len(STAGES) and len(outcome["inventories"]) == len(BUILD_STAGES):
                break
            outer, raw = probe._systemd(identity["outer_unit"], min(1.0, probe._remaining(deadline)))
            require(len(raw) <= 128 * 1024, "systemd outer response exceeds bound")
            if outer.get("InvocationID") == identity["outer_invocation"] and outer.get("ActiveState") == "active":
                saw_outer = True
            if outer.get("InvocationID") == identity["outer_invocation"] and outer.get("ActiveState") in ("inactive", "failed"):
                add_cause(outcome, "exact outer invocation terminal before observations settled")
                break
            if saw_outer and outer.get("LoadState") == "not-found" and exact_outer_leaf_absent(identity):
                add_cause(outcome, "previously observed exact outer invocation collected with its cgroup leaf absent")
                break
            time.sleep(min(POLL_SECONDS, max(0, deadline - time.monotonic())))
        if len(outcome["stages"]) != len(STAGES):
            add_cause(outcome, "missing live stage(s): " + ",".join(s for s in STAGES if s not in outcome["stages"]))
        if len(outcome["inventories"]) != len(BUILD_STAGES):
            add_cause(outcome, "missing frozen inventory: " + ",".join(s for s in BUILD_STAGES if s not in outcome["inventories"]))
        if not outcome["causes"]:
            # Complete the independent identity check even when an inventory
            # finishes after throughput starts. Timing remains a separate,
            # failing observation; it is never repaired by changing its stamp.
            record_identity(job, request_sha, identity, output, "final-")
            outcome["structural_capture_complete"] = True
            try:
                validate_inventory_timing(outcome)
                outcome["inventory_before_throughput"] = True
            except (ValueError, probe.ProbeError) as exc:
                outcome["timing_causes"].append(str(exc)[:500])
                add_cause(outcome, str(exc))
            if not outcome["causes"]:
                outcome["verdict"] = "OBSERVATION_PASS"
    except (OSError, ValueError, probe.ProbeError) as exc:
        add_cause(outcome, str(exc))
    finally:
        write_json(output, "stage-observation.json", outcome)
    return outcome


def add_cause(outcome: dict, reason: str) -> None:
    if reason not in outcome["causes"] and len(outcome["causes"]) < 12:
        outcome["causes"].append(reason[:500])


def exact_outer_leaf_absent(identity: dict) -> bool:
    root = os.stat(probe.CGROUP_ROOT, follow_symlinks=False)
    bench_fd = probe._open_dir_nofollow(probe.BENCH_SLICE_PATH)
    try:
        bench = os.fstat(bench_fd)
        require((root.st_dev, root.st_ino) ==
                (identity["cgroup_root_device"], identity["cgroup_root_inode"]) and
                (bench.st_dev, bench.st_ino) == (identity["slice_device"], identity["slice_inode"]),
                "recorded cgroup ancestry changed")
        try:
            os.stat(identity["outer_unit"], dir_fd=bench_fd, follow_symlinks=False)
        except FileNotFoundError:
            return True
        return False
    finally:
        os.close(bench_fd)


def validate_inventory_timing(outcome: dict) -> None:
    throughput = outcome["stages"]["throughput"]
    for stage in BUILD_STAGES:
        inventory = outcome["inventories"][stage]
        require(inventory["boot_id"] == throughput["boot_id"] and
                inventory["complete_monotonic_us"] < throughput["exec_main_start_monotonic_us"],
                f"{stage} inventory not complete before exact throughput start")


def self_test() -> None:
    checks = 0
    def reject(callback, reason: str) -> None:
        nonlocal checks
        try:
            callback()
        except (OSError, ValueError, probe.ProbeError) as exc:
            assert reason in str(exc), (reason, str(exc))
        else:
            raise AssertionError(f"expected rejection: {reason}")
        checks += 1

    with tempfile.TemporaryDirectory() as temp, ExitStack() as cleanup:
        tmp = Path(temp)
        root = tmp / "build"
        release = root / "Release"
        release.mkdir(parents=True)
        # Restore owner-write before TemporaryDirectory removes the fixture;
        # CI runs this test as an unprivileged hosted user.
        cleanup.callback(os.chmod, root, 0o700)
        cleanup.callback(os.chmod, release, 0o700)
        binary = release / "ide"
        binary.write_bytes(b"fixture-executable")
        os.chmod(binary, 0o550)
        text_file = root / "build.ninja"
        text_file.write_bytes(b"fixture-input")
        os.chmod(text_file, 0o440)
        os.chmod(release, 0o550)
        os.chmod(root, 0o550)
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        deadline = time.monotonic() + 20
        uids, gids = {os.getuid()}, {os.getgid()}
        positive = inventory_tree(str(root), digest, deadline, uids, gids, tmp / "positive.jsonl")
        assert positive["node_count"] == 4 and positive["binary_sha256"] == digest
        assert (tmp / "positive.jsonl").read_text().count("\n") == 4
        checks += 1
        reject(lambda: inventory_tree(str(root), "0" * 64, deadline, uids, gids),
               "hash differs")
        link = root / "malicious-link"
        os.chmod(root, 0o750)
        link.symlink_to(binary)
        os.chmod(root, 0o550)
        reject(lambda: inventory_tree(str(root), digest, deadline, uids, gids), "unsafe, writable, special")
        os.chmod(root, 0o750)
        link.unlink()
        os.chmod(root, 0o550)
        os.chmod(text_file, 0o640)
        reject(lambda: inventory_tree(str(root), digest, deadline, uids, gids), "unsafe, writable, special")
        os.chmod(text_file, 0o440)
        with patch(__name__ + ".MAX_DEPTH", 1):
            reject(lambda: inventory_tree(str(root), digest, deadline, uids, gids), "depth or path")
        other = tmp / "other"
        other.mkdir(mode=0o550)
        real_open = probe._open_dir_nofollow
        calls = 0
        def swapped(path: str) -> int:
            nonlocal calls
            calls += 1
            return real_open(str(other) if calls > 1 else path)
        with patch.object(probe, "_open_dir_nofollow", side_effect=swapped):
            reject(lambda: inventory_tree(str(root), digest, deadline, uids, gids),
                   "canonical build root changed")

        request = "a" * 64
        identity = {"job": 7, "attempt": 4, "request_sha256": request,
                    "boot_id": "b" * 36, "outer_unit": "buster-bench-7-4.service",
                    "outer_invocation": "c" * 32, "worker_sha256": "d" * 64,
                    "instance_sha256": "e" * 64,
                    "cgroup_root_device": 1, "cgroup_root_inode": 2,
                    "slice_device": 3, "slice_inode": 4}
        workspace = tmp / "workspaces"
        result = workspace / "results" / "job-7-attempt-4"
        result.mkdir(parents=True)
        os.chmod(result, 0o700)
        stage = "base-build"
        manifest_name = result / f"validate-buster-v1.{stage}.manifest"
        attempt_root = workspace / "job-7-attempt-4"
        fields = {"schema": "1", "recipe": "validate-buster-v1", "status": "running",
                  "stage": stage, "process-result": "success", "job-id": "7", "attempt-token": "4",
                  "workspace-root": str(workspace), "result-root": str(result),
                  "base-revision": "1" * 40, "candidate-revision": "2" * 40,
                  "base-build": str(attempt_root / "base/build"),
                  "candidate-build": str(attempt_root / "candidate/build"),
                  "base-binary": str(attempt_root / "base/build/Release/ide"),
                  "candidate-binary": str(attempt_root / "candidate/build/Release/ide"),
                  "base-binary-sha256": digest}
        manifest_name.write_text("".join(f"{k}={v}\n" for k, v in fields.items()))
        os.chmod(manifest_name, 0o400)
        with patch(__name__ + ".WORKSPACES", str(workspace)), \
             patch.object(pwd, "getpwnam", return_value=SimpleNamespace(pw_uid=os.getuid(), pw_gid=os.getgid())):
            parsed = read_stage_manifest(identity, "1" * 40, "2" * 40, stage)
            assert parsed["manifest_sha256"] == hashlib.sha256(manifest_name.read_bytes()).hexdigest()
            checks += 1
            original_open = probe._open_dir_nofollow
            calls = 0
            def swapped_result(path: str) -> int:
                nonlocal calls
                calls += 1
                return original_open(str(other) if calls > 1 else path)
            with patch.object(probe, "_open_dir_nofollow", side_effect=swapped_result):
                reject(lambda: read_stage_manifest(identity, "1" * 40, "2" * 40, stage),
                       "canonical result directory replaced")
            os.link(manifest_name, result / "temporary-link")
            reject(lambda: read_stage_manifest(identity, "1" * 40, "2" * 40, stage),
                   "provisional manifest link")
            (result / "temporary-link").unlink()
            reject(lambda: read_stage_manifest({**identity, "attempt": 5}, "1" * 40,
                                               "2" * 40, stage), "No such file")
            reject(lambda: read_stage_manifest(identity, "1" * 40, "3" * 40, stage),
                   "candidate-revision mismatch")
            os.chmod(manifest_name, 0o600)
            with manifest_name.open("a") as file:
                file.write("job-id=7\n")
            os.chmod(manifest_name, 0o400)
            reject(lambda: read_stage_manifest(identity, "1" * 40, "2" * 40, stage),
                   "duplicate stage manifest field")

        timing = {"stages": {"throughput": {"boot_id": "b" * 36,
                                              "exec_main_start_monotonic_us": 100}},
                  "inventories": {s: {"boot_id": "b" * 36, "complete_monotonic_us": 99}
                                  for s in BUILD_STAGES}}
        validate_inventory_timing(timing)
        checks += 1
        timing["inventories"]["candidate-build"]["complete_monotonic_us"] = 100
        reject(lambda: validate_inventory_timing(timing), "not complete before")
        timing["inventories"]["candidate-build"]["complete_monotonic_us"] = 99
        timing["inventories"]["base-build"]["boot_id"] = "x" * 36
        reject(lambda: validate_inventory_timing(timing), "not complete before")

        def unavailable_records(*args, **kwargs):
            raise probe.ProbeError("record job/attempt mismatch")
        for name in ("bad-record-output", "missing-record-output", "departed-output"):
            (tmp / name).mkdir(mode=0o700)
        with patch(__name__ + ".record_identity", side_effect=unavailable_records):
            outcome = observe(7, request, "1" * 40, "2" * 40, 1, tmp / "bad-record-output")
            assert outcome["verdict"] == "OBSERVATION_INCONCLUSIVE"
            assert any("record job/attempt mismatch" in cause for cause in outcome["causes"])
            checks += 1
        with patch(__name__ + ".record_identity", side_effect=FileNotFoundError("records missing")):
            outcome = observe(7, request, "1" * 40, "2" * 40, 1, tmp / "missing-record-output")
            assert "record absent" in outcome["causes"][0]
            checks += 1
        def departed(unit, timeout):
            if unit == identity["outer_unit"]:
                return {"InvocationID": identity["outer_invocation"], "ActiveState": "inactive"}, ""
            return {"ActiveState": "inactive", "LoadState": "not-found"}, ""
        with patch(__name__ + ".record_identity", return_value=identity), \
             patch.object(probe, "_systemd", side_effect=departed), \
             patch(__name__ + ".read_stage_manifest", side_effect=FileNotFoundError):
            outcome = observe(7, request, "1" * 40, "2" * 40, 1, tmp / "departed-output")
            assert "missing live stage" in " ".join(outcome["causes"])
            checks += 1
        (tmp / "positive-observe-output").mkdir(mode=0o700)
        def active(unit, timeout):
            return {"ActiveState": "active", "MainPID": "123", "Id": unit}, ""
        def captured(stage, ident, output, deadline):
            return {"stage": stage, "boot_id": ident["boot_id"],
                    "exec_main_start_monotonic_us": 1000}
        def invented(ident, stage, manifest, deadline, output):
            return {"stage": stage, "boot_id": ident["boot_id"],
                    "complete_monotonic_us": 999, "node_count": 1,
                    "nodes": [{"path": ".", "mode": 0o550}]}
        with patch(__name__ + ".record_identity", return_value=identity), \
             patch.object(probe, "_systemd", side_effect=active), \
             patch(__name__ + ".capture_stage", side_effect=captured), \
             patch(__name__ + ".read_stage_manifest", return_value={"manifest_sha256": digest}), \
             patch(__name__ + ".inventory_stage", side_effect=invented):
            outcome = observe(7, request, "1" * 40, "2" * 40, 1, tmp / "positive-observe-output")
            assert outcome["verdict"] == "OBSERVATION_PASS" and len(outcome["stages"]) == 5
            assert len(outcome["inventories"]) == 2 and not outcome["inventories"]["base-build"].get("nodes")
            assert json.loads((tmp / "positive-observe-output" / "base-build-inventory.json").read_text())["nodes"]
            assert outcome["structural_capture_complete"] and outcome["inventory_before_throughput"]
            checks += 1
        def late_inventory(ident, stage, manifest, deadline, output):
            result = invented(ident, stage, manifest, deadline, output)
            result["complete_monotonic_us"] = 1001
            return result
        with patch(__name__ + ".record_identity", return_value=identity) as records, \
             patch.object(probe, "_systemd", side_effect=active), \
             patch(__name__ + ".capture_stage", side_effect=captured), \
             patch(__name__ + ".read_stage_manifest", return_value={"manifest_sha256": digest}), \
             patch(__name__ + ".inventory_stage", side_effect=late_inventory):
            (tmp / "late-inventory-output").mkdir(mode=0o700)
            outcome = observe(7, request, "1" * 40, "2" * 40, 1, tmp / "late-inventory-output")
            assert outcome["verdict"] == "OBSERVATION_INCONCLUSIVE"
            assert outcome["structural_capture_complete"] and not outcome["inventory_before_throughput"]
            assert outcome["timing_causes"] == outcome["causes"] and records.call_count == 2
            assert outcome["inventories"]["candidate-build"]["complete_monotonic_us"] == 1001
            checks += 1
        with patch(__name__ + ".record_identity", side_effect=[identity, probe.ProbeError("final identity changed")]), \
             patch.object(probe, "_systemd", side_effect=active), \
             patch(__name__ + ".capture_stage", side_effect=captured), \
             patch(__name__ + ".read_stage_manifest", return_value={"manifest_sha256": digest}), \
             patch(__name__ + ".inventory_stage", side_effect=late_inventory):
            (tmp / "late-changed-record-output").mkdir(mode=0o700)
            outcome = observe(7, request, "1" * 40, "2" * 40, 1, tmp / "late-changed-record-output")
            assert not outcome["structural_capture_complete"] and not outcome["inventory_before_throughput"]
            assert "final identity changed" in outcome["causes"] and not outcome["timing_causes"]
            checks += 1
    print(f"STAGE_OBSERVER_SELF_TEST checks={checks} failures=0 fixtures-only-not-live-proof")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--job", type=int)
    parser.add_argument("--request-sha256")
    parser.add_argument("--baseline")
    parser.add_argument("--subject")
    parser.add_argument("--budget-seconds", type=float)
    parser.add_argument("--output")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    require(args.run and args.output and args.job is not None and args.request_sha256 and
            args.baseline and args.subject and args.budget_seconds is not None,
            "--run requires job, request-sha256, baseline, subject, budget-seconds, and output")
    require(probe.is_root() and probe.in_disposable_container(),
            "stage observer must run as root inside the disposable hosted container")
    output = Path(args.output)
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    os.chmod(output, 0o700)
    result = observe(args.job, args.request_sha256, args.baseline,
                     args.subject, args.budget_seconds, output)
    print(result["verdict"], "causes=" + json.dumps(result["causes"]))
    return 0 if result["verdict"] == "OBSERVATION_PASS" else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, probe.ProbeError) as exc:
        print(f"STAGE_OBSERVER_INCONCLUSIVE {exc}", file=sys.stderr)
        sys.exit(1)
