#!/usr/bin/env python3
"""Bounded data contract for the ordinary snapshot native phase bridge; launches nothing."""
from __future__ import annotations
import hashlib
import json
import os
import re

SCHEMA = "buster-compiler-owned-phase-v1"
POPULATION_SCHEMA = "buster-compiler-snapshot-phases-v1"
OWNERSHIP_SCHEMA = "buster-native-qualification-supervisor-v1"
SCOPE = "entry-through-log-publication-before-terminal-receipt"
MEMBER_LIMIT = 8 << 20
COMMAND_LIMIT = 256 << 10
PHASE_LIMIT = 256
HASH = re.compile(r"[a-f0-9]{64}\Z")


def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def command_bytes(argv: list[str]) -> bytes:
    return b"".join(str(len(item.encode())).encode() + b":" + item.encode() + b"\n" for item in argv)


def unique(pairs: list[tuple]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON member")
        result[key] = value
    return result


def read_record(raw: bytes) -> dict:
    if not isinstance(raw, bytes) or not 0 < len(raw) <= MEMBER_LIMIT:
        raise ValueError("native owned-phase record absent or oversized")
    value = json.loads(raw, object_pairs_hook=unique, parse_constant=lambda _: (_ for _ in ()).throw(ValueError("nonfinite JSON")))
    if not isinstance(value, dict):
        raise ValueError("native owned-phase record is not an object")
    return value


def validate_record(record: object, argv: list[str], cwd: str, timeout: int, driver_sha256: str,
                    stdout: bytes, stderr: bytes, *, nominal: bool = True, receipt_path: str | None = None) -> list[str]:
    """Check bindings and proof. nominal=False retains complete timeout/cancel/failure diagnostics."""
    reasons = []
    if not isinstance(record, dict):
        return ["native owned-phase receipt missing"]
    wanted = {"schema": SCHEMA, "ownership_schema": OWNERSHIP_SCHEMA, "command_sha256": sha(command_bytes(argv)),
              "cwd_sha256": sha(cwd.encode()), "driver_sha256": driver_sha256, "timeout_us": timeout * 1_000_000,
              "stdout_sha256": sha(stdout), "stderr_sha256": sha(stderr),
              "duration_scope": SCOPE, "receipt_publication_us": None, "exit_status_encoding": "posix-wait-status"}
    if any(record.get(key) != value or type(record.get(key)) is not type(value) for key, value in wanted.items()):
        reasons.append("native owned-phase command/cwd/driver/timeout/log/publication binding mismatch")
    if receipt_path is not None and record.get("receipt_path_sha256") != sha(receipt_path.encode()):
        reasons.append("native owned-phase ordinal receipt path binding mismatch")
    integer_fields = ("bootstrap_dependency_count", "duration_us", "exit_status", "timed_out", "cancelled", "capture_failed", "output_truncated",
                      "cleanup_us", "cleanup_waves", "cleanup_signalled", "cleanup_reaped",
                      "reservation_retained", "ownership_lost", "tree_cleanup_failed")
    if any(type(record.get(key)) is not int or record[key] < 0 for key in integer_fields):
        reasons.append("native owned-phase status/timing/cleanup fields malformed")
    if type(record.get("exit_status")) is not int or not 0 <= record["exit_status"] <= 65535:
        reasons.append("native owned-phase raw POSIX wait status outside its fixed bound")
    if record.get("cleanup_proven") is not True or record.get("reservation_retained") != 0 or record.get("ownership_lost") != 0:
        reasons.append("native owned-phase manager/adopted-child cleanup unproven")
    if len(stdout) > MEMBER_LIMIT or len(stderr) > MEMBER_LIMIT or len(command_bytes(argv)) > COMMAND_LIMIT:
        reasons.append("native owned-phase retained logs/command exceed their fixed bound")
    if nominal and (record.get("state") != "complete" or any(record.get(key) != 0 for key in
            ("exit_status", "timed_out", "cancelled", "capture_failed", "output_truncated",
             "cleanup_signalled", "cleanup_reaped", "tree_cleanup_failed"))):
        reasons.append("native owned-phase did not complete without failure/cancellation/orphan recovery")
    if not nominal and record.get("state") not in ("complete", "failed"):
        reasons.append("native owned-phase diagnostic state malformed")
    return reasons


def validate_bootstrap(record: dict, marker: bytes, ownership: dict) -> list[str]:
    """Native producer checked these immutable dependencies against the trusted checkout."""
    reasons = []
    try:
        lines = marker.decode().splitlines()
        if not marker or len(marker) > MEMBER_LIMIT or len(lines) < 5 or lines[0] != "BUSTER_BOOTSTRAP_CACHE_V1" or lines[-1] != "END":
            raise ValueError("bootstrap header/population malformed")
        config, artifact = lines[1].split("\t"), lines[2].split("\t")
        if len(config) != 2 or config[0] != "config" or not HASH.fullmatch(config[1]) or \
                len(artifact) != 3 or artifact[0] != "artifact" or not re.fullmatch(r"build-[A-Za-z0-9-]+", artifact[1]) or \
                artifact[2] != ownership["driver_sha256"]:
            raise ValueError("bootstrap executable/configuration binding mismatch")
        dependencies = {}
        previous = ""
        for line in lines[3:-1]:
            fields = line.split("\t")
            if len(fields) != 3 or fields[0] != "dependency" or fields[1] <= previous or \
                    any(part in ("", ".", "..") for part in fields[1].lstrip("/").split("/")) or not HASH.fullmatch(fields[2]):
                raise ValueError("bootstrap dependency population malformed")
            dependencies[fields[1]] = fields[2]
            previous = fields[1]
        required = ("build.c", "tools/compiler_closure.c", "tools/compiler_closure_phase.c", "tools/compiler_closure_owned_phase.c")
        trusted = ownership.get("trusted_root")
        if not isinstance(trusted, str) or not trusted.startswith("/") or \
                any(key not in dependencies and trusted + "/" + key not in dependencies for key in required) or \
                record.get("trusted_root_sha256") != sha(trusted.encode()) or record.get("bootstrap_config_sha256") != config[1] or \
                record.get("bootstrap_marker_sha256") != sha(marker) or sha(marker) != ownership.get("bootstrap_marker_sha256") or \
                ownership.get("driver_path") != trusted + "/.cache/bootstrap-driver/posix/" + config[1] + "/" + artifact[1] or \
                record.get("bootstrap_dependency_count") != len(dependencies):
            raise ValueError("trusted source/helper/bootstrap manifest identity mismatch")
    except (ValueError, KeyError, UnicodeError, TypeError):
        reasons.append("native owned-phase trusted bootstrap provenance is incomplete or mismatched")
    return reasons


def validate_population(receipt: dict, bundle: object, expected_driver_sha256: str | None = None,
                        expected_trusted_revision: str | None = None) -> list[str]:
    """Require every ordinary core/extension run's persisted ordinal and native proof."""
    ownership = receipt.get("phase_ownership")
    if not isinstance(ownership, dict) or ownership.get("schema") != POPULATION_SCHEMA or ownership.get("state") != "complete":
        return ["ordinary snapshot native phase population missing or incomplete"]
    trusted_revision, trusted_tree = ownership.get("trusted_revision"), ownership.get("trusted_tree")
    if any(not isinstance(value, str) or not re.fullmatch(r"[a-f0-9]{40}", value) for value in (trusted_revision, trusted_tree)) or \
            (expected_trusted_revision is not None and trusted_revision != expected_trusted_revision):
        return ["ordinary snapshot trusted source revision/tree does not match the trusted route"]
    directory = ownership.get("directory")
    if not isinstance(directory, str) or not directory.startswith("/") or any(part in ("", ".", "..") for part in directory[1:].split("/")):
        return ["ordinary snapshot native phase directory malformed"]
    driver = ownership.get("driver_sha256")
    rows = ownership.get("phases")
    raw = bundle if isinstance(bundle, dict) else {}
    if not isinstance(driver, str) or not HASH.fullmatch(driver) or \
            (expected_driver_sha256 is not None and driver != expected_driver_sha256) or \
            not isinstance(rows, list) or not 1 <= len(rows) <= PHASE_LIMIT or type(ownership.get("count")) is not int or \
            ownership["count"] != len(rows) or len(raw) != len(rows):
        return ["ordinary snapshot native driver/population/retained record count mismatch"]
    reasons = []
    core = []
    for index, row in enumerate(rows, 1):
        prefix = f"ordinary snapshot owned phase {index}: "
        if not isinstance(row, dict) or row.get("ordinal") != index or type(row.get("ordinal")) is not int or \
                row.get("file") != f"{index:04d}.json" or not isinstance(row.get("phase"), str) or \
                row.get("kind") not in ("run", "capture") or type(row.get("allow_exit_failure")) is not bool or \
                (row.get("kind") == "run" and row.get("allow_exit_failure")) or not isinstance(row.get("cwd"), str) or \
                not row["cwd"].startswith("/") or not isinstance(row.get("argv"), list) or not row["argv"] or \
                any(not isinstance(item, str) or not item or "\x00" in item or "\n" in item or "\r" in item or "\t" in item
                    for item in row["argv"]) or type(row.get("timeout")) is not int or not 0 < row["timeout"] <= 10800 or \
                type(row.get("bridge_wall_us")) is not int or row["bridge_wall_us"] < 0:
            reasons.append(prefix + "ordinal/identity/observed wall malformed")
            continue
        member = raw.get(row["file"])
        if not isinstance(member, dict) or set(member) != {"receipt", "command", "stdout", "stderr", "bootstrap"} or \
                any(not isinstance(value, bytes) for value in member.values()) or \
                member["command"] != command_bytes(row["argv"]) or row.get("receipt_sha256") != sha(member["receipt"]):
            reasons.append(prefix + "raw command/record/log population missing or hash mismatch")
            continue
        try:
            record = read_record(member["receipt"])
        except (ValueError, UnicodeError):
            reasons.append(prefix + "raw native JSON malformed")
            continue
        reasons.extend(prefix + item for item in validate_record(record, row["argv"], row["cwd"], row["timeout"],
                                                                  driver, member["stdout"], member["stderr"],
                                                                  nominal=not row.get("allow_exit_failure", False),
                                                                  receipt_path=directory + "/" + row["file"]))
        if row.get("kind") == "capture" and row.get("allow_exit_failure") is True:
            status = record.get("exit_status")
            if type(status) is not int or not 0 <= status <= 65535 or not os.WIFEXITED(status) or \
                    record.get("state") != ("complete" if status == 0 else "failed") or any(record.get(key) != 0 for key in
                    ("timed_out", "cancelled", "capture_failed", "output_truncated", "cleanup_signalled", "cleanup_reaped", "tree_cleanup_failed")):
                reasons.append(prefix + "read-only probe did not reach a clean terminal exit")
        reasons.extend(prefix + item for item in validate_bootstrap(record, member["bootstrap"], ownership))
        if type(record.get("duration_us")) is int and record["duration_us"] > row["bridge_wall_us"]:
            reasons.append(prefix + "native partial span exceeds the actual bridge wall")
        if row["kind"] == "run":
            core.append(row)
    required = ["build-baseline"] * 3 + ["closure-snapshot"] + ["build-candidate"] * 3
    inline = receipt.get("inline_acceptance") if isinstance(receipt.get("inline_acceptance"), dict) else {}
    if inline.get("requested") is True:
        required += ["inline-acceptance"]
    required += ["build-closure"] * 2 + ["lab", "throughput"]
    if receipt.get("scaling_profile") is not None:
        from compiler_receipt import SCALING_PROFILE
        required += ["scaling"] * len(SCALING_PROFILE["series"])
    required += ["validate"]
    if [row["phase"] for row in core] != required:
        reasons.append("ordinary snapshot core/extension owned phase population differs from the complete declared route")
    identity = receipt.get("identity", {})
    for label, revision in (("build-baseline", identity.get("base")), ("build-candidate", identity.get("head")),
                            ("build-closure", identity.get("base"))):
        subset = [row for row in core if row["phase"] == label]
        wanted = [["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null",
                   "-C", subset[0]["cwd"], "checkout", "--quiet", "--detach", revision]] if subset else []
        if label != "build-closure":
            wanted += [["./build.sh", "generate", "--cc", "clang", "--no-include-tests"],
                       ["./build.sh", "build", "--config", "Release", "-t", "ide"]]
        if not subset or [row["argv"] for row in subset[:len(wanted)]] != wanted:
            reasons.append(label + " checkout/build owned command plan changed")
    from compiler_owned_plan import validate_plan
    reasons.extend(validate_plan(receipt, ownership, core))
    return reasons
