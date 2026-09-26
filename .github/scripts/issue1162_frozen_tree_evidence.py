#!/usr/bin/env python3
"""Offline #1162 frozen-tree receipt reconciliation; no systemd or workspace writes.

The source receipt is a self-report. A separately captured stage-observer JSON
inventory supplies the independent tree map, even if its scan finished after
throughput started. The reviewed recipe graph and successful stage manifests
supply publication ordering; a scan timestamp alone cannot do that.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

PRODUCER_HEAD = "b65ce53e32cc921fdf68ad13e499417260d6f73a"
PRODUCER_TREE = "2b26253cd1849a8bb50687ede056f3f9d764837b"
PRODUCER_BUILD_BLOB = "1ea24b90629a2c771c3d0e7d3d517b042ddb4c35"
PRODUCER_REVIEW = "https://github.com/buster14a/buster/pull/1481#issuecomment-5847791034"
COMPOSITE_REVIEW = "https://github.com/buster14a/buster/pull/1482#issuecomment-5847817058"
WORKSPACES = "/var/lib/buster-bench/workspaces"
STAGES = ("base-generate", "base-build", "candidate-generate", "candidate-build", "throughput")
BUILD_STAGES = ("base-build", "candidate-build")
MAX_RECEIPT = 64 * 1024 * 1024
MAX_INVENTORY = 64 * 1024 * 1024
MAX_OBSERVATION = 2 * 1024 * 1024
MAX_MANIFEST = 32768
MAX_NODES = 100000
MAX_DEPTH = 256
MAX_PATH = 1024
MAX_FILE = 128 * 1024 * 1024
MAX_HASHED = 2 * 1024 * 1024 * 1024
MAX_LINE = MAX_PATH * 2 + 512
U64 = (1 << 64) - 1
I64 = (1 << 63) - 1
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX32 = re.compile(r"[0-9a-f]{32}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
REVISION = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})\Z")
BOOT = re.compile(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\Z")
DECIMAL = re.compile(rb"(?:0|[1-9][0-9]*)\Z")
TIME = re.compile(rb"(0|[1-9][0-9]*)\.([0-9]{9})\Z")
NODE_KEYS = frozenset(("path", "type", "device", "inode", "links", "mode", "uid", "gid",
                       "size", "mtime_ns", "ctime_ns"))


class EvidenceError(ValueError):
    pass


def require(ok: bool, reason: str) -> None:
    if not ok:
        raise EvidenceError(reason)


def integer(raw: bytes, maximum: int = U64) -> int:
    require(DECIMAL.fullmatch(raw) is not None and len(raw) <= 20,
            "noncanonical or oversized decimal")
    value = int(raw)
    require(value <= maximum, "integer overflow")
    return value


def timestamp(raw: bytes) -> int:
    match = TIME.fullmatch(raw)
    require(match is not None and len(raw) <= 30, "noncanonical nanosecond timestamp")
    seconds = integer(match.group(1), I64 // 1000000000)
    value = seconds * 1000000000 + int(match.group(2))
    require(value <= I64, "timestamp overflow")
    return value


def hex64(raw: bytes) -> str:
    value = raw.decode("ascii", "strict")
    require(HEX64.fullmatch(value) is not None, "invalid lowercase SHA-256")
    return value


def raw_path(value: bytes) -> bytes:
    require(0 < len(value) <= MAX_PATH * 2 and len(value) % 2 == 0 and
            all(byte in b"0123456789abcdef" for byte in value),
            "invalid lowercase hex path")
    path = bytes.fromhex(value.decode("ascii"))
    require(0 < len(path) <= MAX_PATH and b"\x00" not in path,
            "empty, NUL, or oversized path")
    if path != b".":
        parts = path.split(b"/")
        require(len(parts) <= MAX_DEPTH and all(part not in (b"", b".", b"..") for part in parts),
                "noncanonical relative path or depth")
    return path


def line_at(raw: bytes, offset: int) -> tuple[bytes, int]:
    end = raw.find(b"\n", offset, min(len(raw), offset + MAX_LINE + 1))
    require(end >= offset and end - offset + 1 <= MAX_LINE and b"\r" not in raw[offset:end],
            "missing, oversized, or CR-terminated receipt line")
    return raw[offset:end + 1], end + 1


def read_regular(path: Path, maximum: int) -> bytes:
    """Bounded no-follow offline read, stable at the open descriptor and path."""
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        before = os.fstat(fd)
        require(stat.S_ISREG(before.st_mode) and before.st_nlink == 1 and
                0 < before.st_size <= maximum, f"invalid offline artifact: {path.name}")
        identity = (before.st_dev, before.st_ino, before.st_mode, before.st_nlink,
                    before.st_uid, before.st_gid, before.st_size, before.st_mtime_ns, before.st_ctime_ns)
        content = bytearray()
        while len(content) < before.st_size:
            block = os.read(fd, min(65536, before.st_size - len(content)))
            require(bool(block), f"short offline artifact: {path.name}")
            content.extend(block)
        after = os.fstat(fd)
        path_after = os.stat(path, follow_symlinks=False)
        def fields(info: os.stat_result) -> tuple[int, ...]:
            return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink, info.st_uid,
                    info.st_gid, info.st_size, info.st_mtime_ns, info.st_ctime_ns)
        require(os.read(fd, 1) == b"" and fields(after) == identity and fields(path_after) == identity,
                f"offline artifact changed: {path.name}")
        return bytes(content)
    finally:
        os.close(fd)


def duplicate_keys(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON field: {key}")
        result[key] = value
    return result


def read_json(path: Path, maximum: int) -> tuple[dict, str]:
    raw = read_regular(path, maximum)
    require(raw.endswith(b"\n") and b"\x00" not in raw, "invalid JSON artifact termination")
    parsed = json.loads(raw.decode("utf-8", "strict"), object_pairs_hook=duplicate_keys,
                        parse_constant=lambda value: (_ for _ in ()).throw(EvidenceError("nonfinite JSON")))
    require(isinstance(parsed, dict), "JSON artifact root is not an object")
    return parsed, hashlib.sha256(raw).hexdigest()


def check_parent_closure(nodes: dict[bytes, dict]) -> None:
    require(b"." in nodes and nodes[b"."]["type"] == "dir", "missing root directory node")
    for path in nodes:
        if path != b".":
            parent = path.rpartition(b"/")[0] or b"."
            require(parent in nodes and nodes[parent]["type"] == "dir",
                    "missing or nondirectory path parent")


def receipt_node(line: bytes) -> tuple[bytes, dict]:
    parts = line[:-1].split(b"\t")
    require(len(parts) == 13 and parts[0] == b"node", "malformed node line")
    path = raw_path(parts[1])
    kind = {b"d": "dir", b"f": "file"}.get(parts[2])
    require(kind is not None, "invalid node type")
    values = [integer(field) for field in parts[3:10]]
    device, inode, links, mode, uid, gid, size = values
    require(inode > 0 and links > 0 and mode <= 0o7777 and size <= MAX_FILE and
            uid <= 0xffffffff and gid <= 0xffffffff,
            "node metadata out of bounds")
    require((kind == "dir" and mode == 0o550) or
            (kind == "file" and mode in (0o440, 0o550) and links == 1),
            "node type, mode, or link policy mismatch")
    record = {"type": kind, "device": device, "inode": inode, "links": links,
              "mode": mode, "uid": uid, "gid": gid, "size": size,
              "mtime_ns": timestamp(parts[10]), "ctime_ns": timestamp(parts[11])}
    if kind == "file" and mode & 0o111:
        record["sha256"] = hex64(parts[12])
    else:
        require(parts[12] == b"-", "unexpected hash on nonexecutable node")
    require(path == b"." or path != b"Release/ide" or
            (kind == "file" and "sha256" in record), "Release/ide is not executable")
    return path, record


def parse_receipt(raw: bytes, stage: str, job: int, attempt: int,
                  baseline: str, subject: str, boot: str, build_root: str,
                  binary_digest: str) -> dict:
    """Parse the exact reviewed build.c V1 serialization; no order assumption for nodes."""
    require(0 < len(raw) <= MAX_RECEIPT and raw.endswith(b"\n") and
            b"\x00" not in raw, "receipt empty, oversized, NUL, or unterminated")
    offset = 0
    expected = (b"BQ-FROZEN-TREE-V1\n", f"stage={stage}\n".encode(),
                f"job-id={job}\n".encode(), f"attempt-token={attempt}\n".encode(),
                f"base-revision={baseline}\n".encode(), f"candidate-revision={subject}\n".encode(),
                f"build-root={build_root}\n".encode(), f"boot-id={boot}\n".encode(),
                f"binary-sha256={binary_digest}\n".encode())
    for exact in expected:
        line, offset = line_at(raw, offset)
        require(line == exact, "receipt header identity, order, or source hash mismatch")
    nodes: dict[bytes, dict] = {}
    digest = hashlib.sha256()
    hashed = 0
    while offset < len(raw) and raw.startswith(b"node\t", offset):
        line, offset = line_at(raw, offset)
        require(len(nodes) < MAX_NODES, "receipt node count bound exceeded")
        path, node = receipt_node(line)
        require(path not in nodes, "duplicate decoded node path")
        nodes[path] = node
        if "sha256" in node:
            hashed += node["size"]
            require(hashed <= MAX_HASHED, "executable hash byte bound exceeded")
        digest.update(line)
    check_parent_closure(nodes)
    trailer = ((b"node-count=", len(nodes)), (b"hashed-executable-bytes=", hashed))
    for prefix, expected_count in trailer:
        line, offset = line_at(raw, offset)
        require(line.startswith(prefix) and integer(line[len(prefix):-1]) == expected_count,
                "receipt footer count mismatch")
    line, offset = line_at(raw, offset)
    require(line == b"node-lines-sha256=" + digest.hexdigest().encode() + b"\n",
            "receipt node-line checksum mismatch")
    line, offset = line_at(raw, offset)
    require(line.startswith(b"scan-complete-monotonic-us="), "receipt scan timestamp missing")
    complete_us = integer(line[len(b"scan-complete-monotonic-us="):-1])
    require(complete_us > 0 and offset == len(raw), "zero scan timestamp or trailing receipt records")
    require(nodes.get(b"Release/ide", {}).get("sha256") == binary_digest,
            "receipt Release/ide digest mismatch")
    return {"nodes": nodes, "node_count": len(nodes), "hashed_executable_bytes": hashed,
            "binary_sha256": binary_digest, "complete_monotonic_us": complete_us,
            "node_lines_sha256": digest.hexdigest(), "receipt_sha256": hashlib.sha256(raw).hexdigest()}


def manifest_fields(raw: bytes, stage: str, job: int, attempt: int,
                    baseline: str, subject: str, workspace_root: str) -> dict[str, str]:
    require(0 < len(raw) <= MAX_MANIFEST and raw.endswith(b"\n") and
            not raw.endswith(b"\n\n") and b"\r" not in raw and b"\x00" not in raw,
            "invalid published stage manifest bytes")
    lines = raw[:-1].decode("ascii", "strict").split("\n")
    fields: dict[str, str] = {}
    for line in lines:
        key, sep, value = line.partition("=")
        require(sep == "=" and key and key not in fields, "malformed or duplicate stage manifest field")
        fields[key] = value
    attempt_root = f"{workspace_root}/job-{job}-attempt-{attempt}"
    expected = {"schema": "1", "recipe": "validate-buster-v1", "status": "running",
                "process-result": "success", "stage": stage, "job-id": str(job),
                "attempt-token": str(attempt), "workspace-root": workspace_root,
                "result-root": f"{workspace_root}/results/job-{job}-attempt-{attempt}",
                "base-revision": baseline, "candidate-revision": subject,
                "base-build": f"{attempt_root}/base/build",
                "candidate-build": f"{attempt_root}/candidate/build",
                "base-binary": f"{attempt_root}/base/build/Release/ide",
                "candidate-binary": f"{attempt_root}/candidate/build/Release/ide"}
    for key, value in expected.items():
        require(fields.get(key) == value, f"successful stage manifest {key} mismatch")
    digest_key = "base-binary-sha256" if stage == "base-build" else "candidate-binary-sha256"
    require(HEX64.fullmatch(fields.get(digest_key, "")) is not None,
            "stage manifest binary digest missing")
    return fields


def bounded_int(value: object, maximum: int = U64) -> int:
    require(type(value) is int and 0 <= value <= maximum, "invalid JSON integer")
    return value


def external_nodes(raw_nodes: object, binary_digest: str) -> tuple[dict[bytes, dict], int]:
    require(type(raw_nodes) is list and 0 < len(raw_nodes) <= MAX_NODES,
            "external inventory node count invalid")
    nodes: dict[bytes, dict] = {}
    hashed = 0
    for item in raw_nodes:
        require(type(item) is dict and type(item.get("path")) is str and
                item.get("type") in ("dir", "file"), "malformed external inventory node")
        path = os.fsencode(item["path"])
        require(path == b"." or (0 < len(path) <= MAX_PATH and b"\x00" not in path and
                not path.startswith(b"/") and len(path.split(b"/")) <= MAX_DEPTH and
                all(part not in (b"", b".", b"..") for part in path.split(b"/"))),
                "noncanonical external inventory path")
        require(path not in nodes, "duplicate external inventory path")
        kind = item["type"]
        expected_keys = NODE_KEYS | ({"sha256"} if kind == "file" and
                                     type(item.get("mode")) is int and item["mode"] & 0o111 else set())
        require(item.keys() == expected_keys, "external inventory node fields differ")
        numeric = {key: bounded_int(item[key], I64 if key in ("mtime_ns", "ctime_ns") else U64)
                   for key in ("device", "inode", "links", "mode", "uid", "gid",
                               "size", "mtime_ns", "ctime_ns")}
        require(numeric["inode"] > 0 and numeric["links"] > 0 and
                numeric["size"] <= MAX_FILE and numeric["mode"] <= 0o7777 and
                numeric["uid"] <= 0xffffffff and numeric["gid"] <= 0xffffffff and
                ((kind == "dir" and numeric["mode"] == 0o550) or
                 (kind == "file" and numeric["mode"] in (0o440, 0o550) and
                  numeric["links"] == 1)), "external inventory node policy mismatch")
        record = {"type": kind, **numeric}
        if "sha256" in item:
            require(type(item["sha256"]) is str and HEX64.fullmatch(item["sha256"]) is not None,
                    "external executable SHA-256 invalid")
            record["sha256"] = item["sha256"]
            hashed += numeric["size"]
            require(hashed <= MAX_HASHED, "external executable byte bound exceeded")
        nodes[path] = record
    check_parent_closure(nodes)
    require(nodes.get(b"Release/ide", {}).get("sha256") == binary_digest,
            "external Release/ide digest mismatch")
    return nodes, hashed


def compare_maps(source: dict[bytes, dict], independent: dict[bytes, dict]) -> None:
    require(source.keys() == independent.keys(), "source/external path sets differ")
    for path in source:
        require(source[path] == independent[path],
                f"source/external metadata or executable digest mismatch at {path[:64]!r}")


def stage_context(observation: dict, source_commit: str, source_tree: str,
                  build_blob: str) -> dict:
    require(HEX40.fullmatch(source_commit) is not None and
            HEX40.fullmatch(source_tree) is not None and build_blob == PRODUCER_BUILD_BLOB,
            "integrated source identity or reviewed build.c blob mismatch")
    identity = observation.get("identity")
    require(type(identity) is dict and type(observation.get("job")) is int and
            observation["job"] > 0 and type(identity.get("attempt")) is int and
            identity["attempt"] > 0 and identity.get("job") == observation["job"] and
            type(observation.get("request_sha256")) is str and
            HEX64.fullmatch(observation["request_sha256"]) is not None and
            identity.get("request_sha256") == observation["request_sha256"],
            "stage observation job/attempt/request identity missing")
    baseline, subject, boot = observation.get("baseline"), observation.get("subject"), identity.get("boot_id")
    require(type(identity.get("worker_sha256")) is str and
            HEX64.fullmatch(identity["worker_sha256"]) is not None and
            type(identity.get("instance_sha256")) is str and
            HEX64.fullmatch(identity["instance_sha256"]) is not None and
            type(baseline) is str and REVISION.fullmatch(baseline) is not None and
            type(subject) is str and REVISION.fullmatch(subject) is not None and
            type(boot) is str and BOOT.fullmatch(boot) is not None and source_commit == subject,
            "stage observation revision/boot differs from integrated source")
    require(observation.get("structural_capture_complete") is True and
            type(observation.get("inventory_before_throughput")) is bool and
            type(observation.get("stages")) is dict and
            set(observation["stages"]) == set(STAGES) and
            type(observation.get("inventories")) is dict and
            set(observation["inventories"]) == set(BUILD_STAGES),
            "independent stage structure or final record check incomplete")
    throughput_start = None
    for stage, captured in observation["stages"].items():
        require(type(captured) is dict and captured.get("stage") == stage and
                captured.get("unit") ==
                f"buster-bench-{observation['job']}-{identity['attempt']}-{stage}.service" and
                captured.get("boot_id") == boot and
                captured.get("record_worker_sha256") == identity.get("worker_sha256") and
                captured.get("record_instance_sha256") == identity.get("instance_sha256"),
                "live stage identity mismatch")
        bounded_int(captured.get("exec_main_start_monotonic_us"))
        require(captured["exec_main_start_monotonic_us"] > 0,
                "live stage start monotonic timestamp missing")
        live = captured.get("identity")
        require(type(live) is dict and type(live.get("invocation")) is str and
                HEX32.fullmatch(live["invocation"]) is not None and
                live["invocation"] != "0" * 32 and
                bounded_int(live.get("main_pid")) > 0 and
                bounded_int(live.get("device")) > 0 and
                bounded_int(live.get("inode")) > 0 and
                bounded_int(live.get("process_starttime_ticks")) > 0 and
                live.get("cgroup") ==
                f"/buster.slice/buster-bench.slice/{captured['unit']}",
                "live stage invocation, process, or cgroup identity missing")
        if stage == "throughput":
            throughput_start = captured["exec_main_start_monotonic_us"]
    require(type(throughput_start) is int, "throughput start absent")
    timing_causes, causes = observation.get("timing_causes"), observation.get("causes")
    require(type(timing_causes) is list and type(causes) is list and
            all(type(cause) is str for cause in timing_causes + causes) and
            (not causes and not timing_causes and observation.get("verdict") == "OBSERVATION_PASS"
             if observation["inventory_before_throughput"] else
             bool(timing_causes) and causes == timing_causes and
             observation.get("verdict") == "OBSERVATION_INCONCLUSIVE"),
            "stage observation has causes beyond preserved inventory timing")
    return {"job": observation["job"], "attempt": identity["attempt"], "baseline": baseline,
            "subject": subject, "boot_id": boot, "throughput_start_us": throughput_start,
            "inventory_before_throughput": observation["inventory_before_throughput"],
            "timing_causes": timing_causes, "source_commit": source_commit,
            "source_tree": source_tree, "build_blob": build_blob}


def verify_stage(directory: Path, receipt_path: Path, observation: dict,
                 context: dict, stage: str, workspace_root: str, checks: dict) -> dict:
    summary = observation["inventories"][stage]
    require(type(summary) is dict and summary.get("stage") == stage and
            summary.get("boot_id") == context["boot_id"] and
            summary.get("artifact") == f"{stage}-inventory.json" and
            HEX64.fullmatch(summary.get("artifact_sha256", "")) is not None,
            "independent inventory summary identity or artifact name mismatch")
    job, attempt = context["job"], context["attempt"]
    digest_key = "base-binary-sha256" if stage == "base-build" else "candidate-binary-sha256"
    kind = "base" if stage == "base-build" else "candidate"
    root = f"{workspace_root}/job-{job}-attempt-{attempt}/{kind}/build"
    manifest_name = f"{stage}-published-manifest.txt"
    raw_manifest = read_regular(directory / manifest_name, MAX_MANIFEST)
    fields = manifest_fields(raw_manifest, stage, job, attempt, context["baseline"],
                             context["subject"], workspace_root)
    digest = fields[digest_key]
    manifest_sha = hashlib.sha256(raw_manifest).hexdigest()
    require(summary.get("manifest_sha256") == manifest_sha and
            type(summary.get("manifest_identity")) is dict and
            summary["manifest_identity"].get("mode") == 0o400 and
            summary["manifest_identity"].get("links") == 1 and
            summary["manifest_identity"].get("size") == len(raw_manifest),
            "published success manifest hash or identity mismatch")
    checks["successful_stage_manifest"] = True
    require(receipt_path.name == f"validate-buster-v1.{stage}.inventory",
            "wrong fixed source receipt name")
    source = parse_receipt(read_regular(receipt_path, MAX_RECEIPT), stage, job, attempt,
                           context["baseline"], context["subject"], context["boot_id"],
                           root, digest)
    checks["source_receipt_integrity"] = True
    independent, artifact_sha = read_json(directory / summary["artifact"], MAX_INVENTORY)
    exact_summary = {key: value for key, value in independent.items() if key != "nodes"}
    exact_summary.update({"artifact": f"{stage}-inventory.json", "artifact_sha256": artifact_sha})
    require(summary == exact_summary, "independent inventory summary differs from hashed full artifact")
    require(artifact_sha == summary["artifact_sha256"] and
            independent.get("stage") == stage and independent.get("boot_id") == context["boot_id"] and
            independent.get("root") == root and independent.get("manifest_sha256") == manifest_sha and
            independent.get("manifest_identity") == summary["manifest_identity"] and
            independent.get("binary_sha256") == digest and
            independent.get("manifest_binary_sha256") == digest,
            "external inventory artifact, root, digest, or manifest mismatch")
    node_map, hashed = external_nodes(independent.get("nodes"), digest)
    require(independent.get("node_count") == len(node_map) == source["node_count"] and
            independent.get("hashed_executable_bytes") == hashed == source["hashed_executable_bytes"],
            "source/external node or executable-byte count mismatch")
    require(bounded_int(independent.get("complete_monotonic_us")) > 0 and
            summary.get("complete_monotonic_us") == independent["complete_monotonic_us"],
            "external inventory completion timestamp mismatch")
    compare_maps(source["nodes"], node_map)
    checks["full_independent_map_equality"] = True
    require(source["complete_monotonic_us"] < context["throughput_start_us"],
            "source scan did not complete before exact throughput start")
    checks["source_scan_before_throughput"] = True
    return {"passed": True, "source_receipt_sha256": source["receipt_sha256"],
            "node_lines_sha256": source["node_lines_sha256"], "node_count": source["node_count"],
            "binary_sha256": digest, "manifest_sha256": manifest_sha,
            "source_scan_complete_monotonic_us": source["complete_monotonic_us"],
            "external_inventory_sha256": artifact_sha,
            "external_complete_monotonic_us": independent["complete_monotonic_us"],
            "external_finished_before_throughput":
                independent["complete_monotonic_us"] < context["throughput_start_us"]}


def verify_artifacts(directory: Path, receipts: dict[str, Path], source_commit: str,
                     source_tree: str, build_blob: str,
                     workspace_root: str = WORKSPACES) -> dict:
    result: dict = {"verdict": "OFFLINE_EVIDENCE_INCOMPLETE", "causes": [],
                    "provenance": {"producer_child_head": PRODUCER_HEAD,
                                   "producer_child_tree": PRODUCER_TREE,
                                   "reviewed_producer_build_blob": PRODUCER_BUILD_BLOB,
                                   "producer_source_review": PRODUCER_REVIEW or None,
                                   "reviewed_composite_source": COMPOSITE_REVIEW,
                                   "integrated_source_commit_caller_verified": source_commit,
                                   "integrated_source_tree_caller_verified": source_tree,
                                   "integrated_build_blob_caller_verified": build_blob,
                                   "external_observer": "independent stage-observation.json and full inventory JSON",
                                   "source_receipts": "caller-provided private self-report paths; "
                                                      "harness must bind them to the actual replay",
                                   "offline_limit": "Git commit/tree relationship and live-systemd provenance "
                                                    "are caller-verified, not independently queried here"},
                    "components": {"structural_live_observation": False,
                                   "independent_timing_consistency": False,
                                   "producer_graph_order_reviewed": bool(PRODUCER_REVIEW),
                                   "stages": {}}, "independent_inventory_before_throughput": None,
                    "original_timing_causes": []}
    try:
        require(workspace_root.startswith("/") and not workspace_root.endswith("/") and
                ".." not in workspace_root.split("/") and len(os.fsencode(workspace_root)) <= MAX_PATH,
                "invalid canonical workspace root")
        observation, observation_sha = read_json(directory / "stage-observation.json", MAX_OBSERVATION)
        context = stage_context(observation, source_commit, source_tree, build_blob)
        result["components"]["structural_live_observation"] = True
        result["stage_observation_sha256"] = observation_sha
        result["identity"] = {key: context[key] for key in ("job", "attempt", "baseline", "subject", "boot_id")}
        result["throughput_exec_main_start_monotonic_us"] = context["throughput_start_us"]
        result["independent_inventory_before_throughput"] = context["inventory_before_throughput"]
        result["original_timing_causes"] = context["timing_causes"]
        for stage in BUILD_STAGES:
            checks = {"successful_stage_manifest": False, "source_receipt_integrity": False,
                      "full_independent_map_equality": False, "source_scan_before_throughput": False}
            result["components"]["stages"][stage] = checks
            try:
                result.setdefault("stage_evidence", {})[stage] = verify_stage(
                    directory, receipts[stage], observation, context, stage, workspace_root, checks)
            except (OSError, ValueError, UnicodeError, TypeError, RecursionError) as exc:
                checks["reason"] = str(exc)[:500]
                result["causes"].append(f"{stage}: {exc}"[:500])
        all_stages = all(result["components"]["stages"][stage].get("source_scan_before_throughput")
                         for stage in BUILD_STAGES)
        if all_stages:
            first_late = next((stage for stage in BUILD_STAGES if
                               not result["stage_evidence"][stage]["external_finished_before_throughput"]), None)
            timing_consistent = ((first_late is None) == context["inventory_before_throughput"] and
                                 (first_late is None or context["timing_causes"] == [
                                     f"{first_late} inventory not complete before exact throughput start"]))
            result["components"]["independent_timing_consistency"] = timing_consistent
            if not timing_consistent:
                result["causes"].append("preserved independent inventory timing flag or cause differs from timestamps")
                all_stages = False
        result["components"]["publication_ordering"] = {
            "passed": all_stages and bool(PRODUCER_REVIEW),
            "basis": "reviewed build.c no-replace/fsync receipt before durable successful stage manifest; "
                     "exact source scan before throughput start and matched independent tree maps",
            "source_review": PRODUCER_REVIEW or None,
            "reason": None if PRODUCER_REVIEW else "independent producer graph review pending"}
        if all_stages and PRODUCER_REVIEW:
            result["verdict"] = "OFFLINE_EVIDENCE_RECONCILED"
        elif all_stages and not PRODUCER_REVIEW:
            result["causes"].append("independent producer graph review pending")
    except (OSError, ValueError, UnicodeError, TypeError, RecursionError) as exc:
        result["components"]["structural_live_observation_reason"] = str(exc)[:500]
        result["causes"].append(str(exc)[:500])
    return result


def write_private(path: Path, value: dict) -> None:
    content = (json.dumps(value, sort_keys=True, indent=2) + "\n").encode("utf-8")
    require(len(content) <= MAX_OBSERVATION, "consumer output exceeds bound")
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
    try:
        view = memoryview(content)
        while view:
            written = os.write(descriptor, view)
            require(written > 0, "short consumer output write")
            view = view[written:]
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def self_test() -> None:
    """Exercise the production verifier, including independent-map and bound failures."""
    checks = 0
    with tempfile.TemporaryDirectory(prefix="frozen-tree-evidence-") as temporary:
        root = Path(temporary)
        observer = root / "observer"
        observer.mkdir()
        workspace = str(root / "workspaces")
        job, attempt = 1, 2
        baseline, subject, boot = "1" * 40, "2" * 40, "a" * 8 + "-" + "a" * 4 + "-" + "a" * 4 + "-" + "a" * 4 + "-" + "a" * 12
        binary = b"fixture executable"
        digest = hashlib.sha256(binary).hexdigest()
        receipts = {}
        identity = {"job": job, "attempt": attempt, "request_sha256": "b" * 64,
                    "boot_id": boot, "worker_sha256": "c" * 64,
                    "instance_sha256": "d" * 64}
        observation: dict = {"verdict": "OBSERVATION_INCONCLUSIVE", "job": job,
                             "request_sha256": identity["request_sha256"],
                             "baseline": baseline, "subject": subject, "identity": identity,
                             "structural_capture_complete": True,
                             "inventory_before_throughput": False,
                             "timing_causes": ["base-build inventory not complete before exact throughput start"],
                             "causes": ["base-build inventory not complete before exact throughput start"],
                             "stages": {}, "inventories": {}}
        for stage in STAGES:
            observation["stages"][stage] = {
                "stage": stage, "unit": f"buster-bench-{job}-{attempt}-{stage}.service",
                "boot_id": boot, "exec_main_start_monotonic_us": 300 if stage == "throughput" else 10,
                "record_worker_sha256": identity["worker_sha256"],
                "record_instance_sha256": identity["instance_sha256"],
                "identity": {"invocation": "e" * 32, "main_pid": 100 + len(stage),
                             "device": 3, "inode": 100 + len(stage),
                             "process_starttime_ticks": 1234,
                             "cgroup": f"/buster.slice/buster-bench.slice/"
                                       f"buster-bench-{job}-{attempt}-{stage}.service"}}
        for index, stage in enumerate(BUILD_STAGES):
            kind = "base" if index == 0 else "candidate"
            build_root = f"{workspace}/job-{job}-attempt-{attempt}/{kind}/build"
            digest_field = "base-binary-sha256" if index == 0 else "candidate-binary-sha256"
            manifest = {"schema": "1", "recipe": "validate-buster-v1", "status": "running",
                        "process-result": "success", "stage": stage, "job-id": str(job),
                        "attempt-token": str(attempt), "workspace-root": workspace,
                        "result-root": f"{workspace}/results/job-{job}-attempt-{attempt}",
                        "base-revision": baseline, "candidate-revision": subject,
                        "base-build": f"{workspace}/job-{job}-attempt-{attempt}/base/build",
                        "candidate-build": f"{workspace}/job-{job}-attempt-{attempt}/candidate/build",
                        "base-binary": f"{workspace}/job-{job}-attempt-{attempt}/base/build/Release/ide",
                        "candidate-binary": f"{workspace}/job-{job}-attempt-{attempt}/candidate/build/Release/ide",
                        digest_field: digest}
            manifest_raw = "".join(f"{key}={value}\n" for key, value in manifest.items()).encode()
            (observer / f"{stage}-published-manifest.txt").write_bytes(manifest_raw)
            manifest_identity = {"mode": 0o400, "links": 1, "size": len(manifest_raw)}
            nodes = []
            lines = []
            for number, (path, kind_node, mode, size, sha) in enumerate(
                ((b".", "dir", 0o550, 4096, None),
                 (b"Release", "dir", 0o550, 4096, None),
                 (b"Release/ide", "file", 0o550, len(binary), digest))):
                node = {"path": os.fsdecode(path), "type": kind_node, "device": 3,
                        "inode": 100 + index * 10 + number, "links": 2 if kind_node == "dir" else 1,
                        "mode": mode, "uid": 0, "gid": 0, "size": size,
                        "mtime_ns": 1000000000, "ctime_ns": 2000000000}
                if sha:
                    node["sha256"] = sha
                nodes.append(node)
                lines.append((f"node\t{path.hex()}\t{'d' if kind_node == 'dir' else 'f'}"
                              f"\t3\t{node['inode']}\t{node['links']}\t{mode}\t0\t0\t{size}"
                              f"\t1.000000000\t2.000000000\t{sha or '-'}\n").encode())
            node_raw = b"".join(lines)
            header = (f"BQ-FROZEN-TREE-V1\nstage={stage}\njob-id={job}\nattempt-token={attempt}\n"
                      f"base-revision={baseline}\ncandidate-revision={subject}\nbuild-root={build_root}\n"
                      f"boot-id={boot}\nbinary-sha256={digest}\n").encode()
            trailer = (f"node-count=3\nhashed-executable-bytes={len(binary)}\n"
                       f"node-lines-sha256={hashlib.sha256(node_raw).hexdigest()}\n"
                       f"scan-complete-monotonic-us={100 + index * 100}\n").encode()
            path = root / f"validate-buster-v1.{stage}.inventory"
            path.write_bytes(header + node_raw + trailer)
            receipts[stage] = path
            inventory = {"root": build_root, "nodes": nodes, "node_count": len(nodes),
                         "binary_sha256": digest, "manifest_binary_sha256": digest,
                         "hashed_executable_bytes": len(binary), "stage": stage, "boot_id": boot,
                         "manifest_sha256": hashlib.sha256(manifest_raw).hexdigest(),
                         "manifest_identity": manifest_identity,
                         "complete_monotonic_us": 400 + index * 100}
            inventory_raw = (json.dumps(inventory, sort_keys=True, indent=2) + "\n").encode()
            (observer / f"{stage}-inventory.json").write_bytes(inventory_raw)
            observation["inventories"][stage] = {
                key: value for key, value in inventory.items() if key != "nodes"}
            observation["inventories"][stage].update({
                "artifact": f"{stage}-inventory.json",
                "artifact_sha256": hashlib.sha256(inventory_raw).hexdigest()})
        observation_path = observer / "stage-observation.json"
        observation_path.write_text(json.dumps(observation, sort_keys=True, indent=2) + "\n")
        def run() -> dict:
            return verify_artifacts(observer, receipts, subject, "3" * 40,
                                    PRODUCER_BUILD_BLOB, workspace)
        positive = run()
        assert len(positive.get("stage_evidence", {})) == 2 and all(
            positive["components"]["stages"][stage]["source_scan_before_throughput"]
            for stage in BUILD_STAGES) and not positive["independent_inventory_before_throughput"]
        checks += 1
        original = {path: path.read_bytes() for path in
                    list(receipts.values()) + [observation_path] +
                    [observer / f"{stage}-inventory.json" for stage in BUILD_STAGES] +
                    [observer / f"{stage}-published-manifest.txt" for stage in BUILD_STAGES]}
        def reject(path: Path, content: bytes, reason: str) -> None:
            nonlocal checks
            path.write_bytes(content)
            try:
                outcome = run()
                assert outcome["verdict"] == "OFFLINE_EVIDENCE_INCOMPLETE"
                assert reason in " ".join(outcome["causes"])
                checks += 1
            finally:
                path.write_bytes(original[path])
        base = receipts["base-build"]
        reject(base, original[base].replace(b"job-id=1\n", b"job-id=9\n"), "header identity")
        reject(base, original[base].replace(b"node-lines-sha256=", b"node-lines-sha256=0"),
               "node-line checksum")
        reject(base, original[base].replace(b"scan-complete-monotonic-us=100\n",
                                            b"scan-complete-monotonic-us=400\n"), "source scan did not complete")
        reject(base, original[base].replace(b"node\t2e\t", b"node\t2E\t"), "lowercase hex path")
        first_line = original[base].split(b"\n")[9] + b"\n"
        reject(base, original[base].replace(first_line, first_line * 2), "duplicate decoded node path")
        manifest_path = observer / "base-build-published-manifest.txt"
        reject(manifest_path, original[manifest_path].replace(b"process-result=success",
                                                              b"process-result=failed"),
               "successful stage manifest process-result mismatch")
        inventory_path = observer / "base-build-inventory.json"
        modified = json.loads(original[inventory_path])
        modified["nodes"][0]["inode"] += 1
        updated = (json.dumps(modified, sort_keys=True, indent=2) + "\n").encode()
        observation_modified = json.loads(original[observation_path])
        observation_modified["inventories"]["base-build"]["artifact_sha256"] = hashlib.sha256(updated).hexdigest()
        inventory_path.write_bytes(updated)
        reject(observation_path, (json.dumps(observation_modified, sort_keys=True, indent=2) + "\n").encode(),
               "metadata or executable digest mismatch")
        inventory_path.write_bytes(original[inventory_path])
        changed = json.loads(original[observation_path])
        changed["identity"]["attempt"] = 3
        reject(observation_path, (json.dumps(changed, sort_keys=True, indent=2) + "\n").encode(),
               "live stage identity mismatch")
        changed = json.loads(original[observation_path])
        changed["structural_capture_complete"] = False
        reject(observation_path, (json.dumps(changed, sort_keys=True, indent=2) + "\n").encode(),
               "structure or final record")
        changed = json.loads(original[observation_path])
        changed["inventories"]["base-build"]["root"] += "-invented"
        reject(observation_path, (json.dumps(changed, sort_keys=True, indent=2) + "\n").encode(),
               "summary differs from hashed full artifact")
        changed = json.loads(original[observation_path])
        changed.update({"inventory_before_throughput": True, "timing_causes": [],
                        "causes": [], "verdict": "OBSERVATION_PASS"})
        reject(observation_path, (json.dumps(changed, sort_keys=True, indent=2) + "\n").encode(),
               "timing flag or cause differs from timestamps")
        duplicate_json = original[observation_path].replace(b'"job": 1,', b'"job": 1, "job": 1,', 1)
        reject(observation_path, duplicate_json, "duplicate JSON field")
        missing = receipts["candidate-build"]
        missing.rename(root / "absent-receipt")
        try:
            result = run()
            assert (result["verdict"] == "OFFLINE_EVIDENCE_INCOMPLETE" and
                    "No such file" in " ".join(result["causes"]))
            checks += 1
        finally:
            (root / "absent-receipt").rename(missing)
        missing.rename(root / "saved-receipt")
        missing.symlink_to(root / "saved-receipt")
        try:
            result = run()
            assert (result["verdict"] == "OFFLINE_EVIDENCE_INCOMPLETE" and
                    "Too many levels of symbolic links" in " ".join(result["causes"]))
            checks += 1
        finally:
            missing.unlink()
            (root / "saved-receipt").rename(missing)
        assert not verify_artifacts(observer, receipts, "4" * 40, "3" * 40,
                                    PRODUCER_BUILD_BLOB, workspace)["components"]["structural_live_observation"]
        checks += 1
        global MAX_NODES, MAX_RECEIPT, MAX_DEPTH
        old_nodes, old_receipt, old_depth = MAX_NODES, MAX_RECEIPT, MAX_DEPTH
        try:
            MAX_NODES = 2
            assert "node count bound" in " ".join(run()["causes"])
            checks += 1
            MAX_NODES = old_nodes
            MAX_RECEIPT = len(original[base]) - 1
            assert "invalid offline artifact" in " ".join(run()["causes"])
            checks += 1
            MAX_RECEIPT = old_receipt
            MAX_DEPTH = 1
            assert "relative path or depth" in " ".join(run()["causes"])
            checks += 1
        finally:
            MAX_NODES, MAX_RECEIPT, MAX_DEPTH = old_nodes, old_receipt, old_depth
    print(f"FROZEN_TREE_EVIDENCE_SELF_TEST checks={checks} failures=0 fixtures-only-not-hosted-proof")


def integration_fixture(fixture: Path) -> None:
    """Exercise actual C-emitted same-run receipts against a separate no-follow census.

    A synthetic stage wrapper supplies identities and throughput timing absent
    from the local C self-test. It is deleted immediately and is never live proof.
    """
    provenance, _ = read_json(fixture / "fixture-provenance.json", MAX_OBSERVATION)
    census, census_sha = read_json(fixture / "independent-census.json", MAX_INVENTORY)
    require(provenance.get("frozen_source_blob") == PRODUCER_BUILD_BLOB and
            provenance.get("label") == "local synthetic C recipe fixture; not an installed-service observation" and
            provenance.get("exact_throughput_start_monotonic_us") is None and
            set(census) == set(BUILD_STAGES), "local C fixture provenance mismatch")
    for name, expected in provenance["artifacts"].items():
        if name.endswith((".inventory", ".manifest")):
            content = read_regular(fixture / name, MAX_RECEIPT)
            require(hashlib.sha256(content).hexdigest() == expected["sha256"] and
                    len(content) == expected["size"], "C fixture artifact digest mismatch")
    root = provenance["original_fixture_root"]
    workspace = f"{root}/workspaces"
    job, attempt = int(provenance["job_id"]), int(provenance["attempt_token"])
    baseline, subject, boot = (provenance[key] for key in
                               ("base_revision", "candidate_revision", "boot_id"))
    receipts = {stage: fixture / f"validate-buster-v1.{stage}.inventory" for stage in BUILD_STAGES}
    # The source receipt time is real from the local C run. The stage start is
    # explicitly synthetic because the fixture never started a systemd unit.
    source_times = []
    for stage in BUILD_STAGES:
        raw = read_regular(receipts[stage], MAX_RECEIPT)
        marker = b"scan-complete-monotonic-us="
        require(raw.count(marker) == 1, "local fixture scan timestamp duplicate")
        source_times.append(integer(raw.split(marker)[1].split(b"\n", 1)[0]))
    synthetic_start = max(source_times) + 1
    with tempfile.TemporaryDirectory(prefix="frozen-tree-c-integration-") as temporary:
        observer = Path(temporary)
        identity = {"job": job, "attempt": attempt, "request_sha256": "b" * 64,
                    "boot_id": boot, "worker_sha256": "c" * 64,
                    "instance_sha256": "d" * 64}
        timing = "base-build inventory not complete before exact throughput start"
        observation = {"verdict": "OBSERVATION_INCONCLUSIVE", "job": job,
                       "request_sha256": identity["request_sha256"],
                       "baseline": baseline, "subject": subject, "identity": identity,
                       "structural_capture_complete": True, "inventory_before_throughput": False,
                       "timing_causes": [timing], "causes": [timing], "stages": {}, "inventories": {}}
        for stage in STAGES:
            observation["stages"][stage] = {
                "stage": stage, "unit": f"buster-bench-{job}-{attempt}-{stage}.service",
                "boot_id": boot,
                "exec_main_start_monotonic_us": synthetic_start if stage == "throughput" else 1,
                "record_worker_sha256": identity["worker_sha256"],
                "record_instance_sha256": identity["instance_sha256"],
                "identity": {"invocation": "e" * 32, "main_pid": 100 + len(stage),
                             "device": 3, "inode": 100 + len(stage),
                             "process_starttime_ticks": 1234,
                             "cgroup": f"/buster.slice/buster-bench.slice/"
                                       f"buster-bench-{job}-{attempt}-{stage}.service"}}
        for stage in BUILD_STAGES:
            raw_manifest = read_regular(fixture / f"validate-buster-v1.{stage}.manifest", MAX_MANIFEST)
            (observer / f"{stage}-published-manifest.txt").write_bytes(raw_manifest)
            fields = manifest_fields(raw_manifest, stage, job, attempt, baseline, subject, workspace)
            digest_key = "base-binary-sha256" if stage == "base-build" else "candidate-binary-sha256"
            digest = fields[digest_key]
            nodes = census[stage]
            require(type(nodes) is list and len(nodes) == provenance["node_counts"][stage],
                    "independent fixture census node count mismatch")
            source, _ = external_nodes(nodes, digest)
            hashed = sum(node["size"] for node in nodes if "sha256" in node)
            manifest_identity = {"mode": 0o400, "links": 1, "size": len(raw_manifest)}
            artifact = {"root": fields["base-build"] if stage == "base-build" else fields["candidate-build"],
                        "nodes": nodes, "node_count": len(source), "binary_sha256": digest,
                        "manifest_binary_sha256": digest, "hashed_executable_bytes": hashed,
                        "stage": stage, "boot_id": boot,
                        "manifest_sha256": hashlib.sha256(raw_manifest).hexdigest(),
                        "manifest_identity": manifest_identity,
                        "complete_monotonic_us": synthetic_start + 100}
            artifact_raw = (json.dumps(artifact, sort_keys=True, indent=2) + "\n").encode()
            (observer / f"{stage}-inventory.json").write_bytes(artifact_raw)
            observation["inventories"][stage] = {
                key: value for key, value in artifact.items() if key != "nodes"}
            observation["inventories"][stage].update({
                "artifact": f"{stage}-inventory.json",
                "artifact_sha256": hashlib.sha256(artifact_raw).hexdigest()})
        (observer / "stage-observation.json").write_text(
            json.dumps(observation, sort_keys=True, indent=2) + "\n")
        result = verify_artifacts(observer, receipts, subject, "3" * 40,
                                  PRODUCER_BUILD_BLOB, workspace)
        require(result["verdict"] == "OFFLINE_EVIDENCE_RECONCILED" and
                all(result["components"]["stages"][stage]["full_independent_map_equality"]
                    for stage in BUILD_STAGES) and
                not result["independent_inventory_before_throughput"],
                "same-run local C receipt/independent census integration failed: " +
                "; ".join(result["causes"]))
        print("FROZEN_TREE_C_INTEGRATION fixture=synthetic-not-hosted-proof "
              f"census_sha256={census_sha} base_nodes={len(census['base-build'])} "
              f"candidate_nodes={len(census['candidate-build'])} result=pass")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    verify = commands.add_parser("verify", help="reconcile retained offline artifacts")
    verify.add_argument("--observer-dir", type=Path, required=True)
    verify.add_argument("--base-receipt", type=Path, required=True)
    verify.add_argument("--candidate-receipt", type=Path, required=True)
    verify.add_argument("--source-commit", required=True)
    verify.add_argument("--source-tree", required=True)
    verify.add_argument("--build-blob", required=True)
    verify.add_argument("--output", type=Path, required=True)
    tests = commands.add_parser("self-test", help="local fixture tests; no systemd or service")
    tests.add_argument("--integration-fixture", type=Path,
                       help="optional actual C self-test receipts plus independent local census")
    args = parser.parse_args()
    if args.command == "self-test":
        self_test()
        if args.integration_fixture is not None:
            integration_fixture(args.integration_fixture)
        return 0
    result = verify_artifacts(args.observer_dir,
                              {"base-build": args.base_receipt,
                               "candidate-build": args.candidate_receipt},
                              args.source_commit, args.source_tree, args.build_blob)
    write_private(args.output, result)
    print(f"FROZEN_TREE_EVIDENCE verdict={result['verdict']} output={args.output}")
    return 0 if result["verdict"] == "OFFLINE_EVIDENCE_RECONCILED" else 1


if __name__ == "__main__":
    sys.exit(main())
