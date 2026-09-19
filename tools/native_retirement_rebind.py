#!/usr/bin/env python3
"""Check or refresh native-retirement generated dependency state.

The reviewed policy omits repository byte/hash state. Refresh may write only
the generated repository-source snapshot and the single generated aggregate
binding header. The immutable legacy v1 descriptor remains a compatibility
input for archived evidence and is never refreshed. External, SDK, resource,
provenance, corpus, and applicability declarations remain fail-closed policy.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
import tempfile
from typing import Optional

import native_retirement_dependency_binding as authority
import native_retirement_materializer as materializer
from native_retirement_rebind_contract import (
    PYTHON_BINDING_PATH,
    SDK_MANIFEST_PATH,
    RebindError,
    _absolute_root,
    _decode,
    _fail,
    _read_no_follow,
    _source_identity,
    _validate_external_declarations,
    _validate_sdk_declarations,
)

REPORT_SCHEMA = "buster-native-retirement-rebind-report-v2"
DESCRIPTOR_PATH = authority.POLICY_PATH
SNAPSHOT_PATH = authority.SNAPSHOT_PATH
BINDING_PATH = authority.BINDING_PATH
TARGET_PATHS = (SNAPSHOT_PATH, BINDING_PATH)
IDENTITY_ORDER = ("policy_sha256", "receipt_sha256", "project_sha256", "ledger_sha256")


@dataclass(frozen=True)
class RebindPlan:
    root: Path
    originals: dict[str, Optional[bytes]]
    replacements: dict[str, bytes]
    source_bindings: tuple[tuple[str, int, str], ...]
    report: dict


def _optional_regular(path: Path, label: str) -> Optional[bytes]:
    try:
        path.lstat()
    except FileNotFoundError:
        return None
    return _read_no_follow(path, label)


def _materialize_identities(root: Path, manifest: dict, policy_raw: bytes, resolved_raw: bytes) -> dict[str, str]:
    policy_sha256 = hashlib.sha256(policy_raw).hexdigest()
    descriptor_sha256 = hashlib.sha256(resolved_raw).hexdigest()
    if authority.strict_json(resolved_raw, "resolved dependency descriptor") != manifest:
        _fail("resolved descriptor bytes do not match the materialized manifest")
    temporary = Path(tempfile.mkdtemp(prefix="native-retirement-rebind-materialize-")).resolve()
    try:
        output = temporary / "materialized"
        receipt = materializer.materialize(
            manifest,
            root,
            output,
            descriptor_sha256=descriptor_sha256,
            descriptor_path=DESCRIPTOR_PATH,
        )
        receipt_raw = _read_no_follow(output / "dependency-manifest.json", "materializer receipt")
        expected_receipt = (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8")
        if receipt_raw != expected_receipt:
            _fail("materializer receipt bytes do not match the returned receipt")
        if receipt.get("descriptor_sha256") != descriptor_sha256 or receipt.get("descriptor_path") != DESCRIPTOR_PATH:
            _fail("materializer receipt is not bound to the resolved policy/snapshot descriptor")
        ledger_raw = _read_no_follow(output / "dependencies.tsv", "materializer ledger")
        ledger_sha256 = hashlib.sha256(ledger_raw).hexdigest()
        if receipt.get("ledger_sha256") != ledger_sha256:
            _fail("materializer ledger digest does not match its receipt")
        project_sha256 = receipt.get("project_include_sha256")
        if not isinstance(project_sha256, str) or re.fullmatch(r"[0-9a-f]{64}", project_sha256) is None:
            _fail("materializer receipt has no canonical project closure identity")
        return {
            "policy_sha256": policy_sha256,
            "receipt_sha256": hashlib.sha256(receipt_raw).hexdigest(),
            "project_sha256": project_sha256,
            "ledger_sha256": ledger_sha256,
        }
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def _existing_snapshot(
    raw: Optional[bytes], policy_raw: bytes, manifest: dict
) -> tuple[Optional[tuple[dict, ...]], Optional[str]]:
    if raw is None:
        return None, "missing repository-source snapshot"
    try:
        return authority.parse_snapshot(raw, policy_raw, manifest), None
    except authority.BindingError as error:
        return None, str(error)


def _existing_binding(raw: Optional[bytes]) -> tuple[Optional[dict], Optional[str]]:
    if raw is None:
        return None, "missing aggregate binding"
    try:
        return authority.parse_binding(raw), None
    except authority.BindingError as error:
        return None, str(error)


def prepare(root: Path) -> RebindPlan:
    root = _absolute_root(root)
    policy_raw = _read_no_follow(root / PurePosixPath(DESCRIPTOR_PATH), DESCRIPTOR_PATH)
    policy = authority.parse_policy(policy_raw)
    legacy_raw = _read_no_follow(root / PurePosixPath(authority.LEGACY_DESCRIPTOR_PATH), authority.LEGACY_DESCRIPTOR_PATH)
    if policy["legacy_descriptor"]["sha256"] != hashlib.sha256(legacy_raw).hexdigest():
        _fail("reviewed policy legacy descriptor identity mismatch")

    # Independent reviewed trust declarations remain outside the generated
    # repository-source refresh path.
    contract_raw = _read_no_follow(root / PurePosixPath(PYTHON_BINDING_PATH), PYTHON_BINDING_PATH)
    _validate_external_declarations(policy, _decode(contract_raw, PYTHON_BINDING_PATH))
    sdk_raw = _read_no_follow(root / PurePosixPath(SDK_MANIFEST_PATH), SDK_MANIFEST_PATH)
    _validate_sdk_declarations(policy, sdk_raw)

    originals = {
        path: _optional_regular(root / PurePosixPath(path), path)
        for path in TARGET_PATHS
    }

    def source_identity(source: str) -> tuple[int, str]:
        return _source_identity(root, source, "repository project source")

    snapshot_raw, generated_records = authority.render_snapshot(policy_raw, policy, source_identity)
    # Parse the exact bytes that would be committed before using them.
    snapshot = authority.parse_snapshot(snapshot_raw, policy_raw, policy)
    candidate = authority.resolved_manifest(policy, snapshot)
    resolved_raw = authority.render_resolved_descriptor(policy, snapshot, legacy_raw)
    identities = _materialize_identities(root, candidate, policy_raw, resolved_raw)
    binding_values = {
        "schema": authority.BINDING_SCHEMA,
        "version": authority.BINDING_VERSION,
        "policy_path": DESCRIPTOR_PATH,
        "policy_sha256": identities["policy_sha256"],
        "snapshot_path": SNAPSHOT_PATH,
        "snapshot_sha256": hashlib.sha256(snapshot_raw).hexdigest(),
        "receipt_sha256": identities["receipt_sha256"],
        "project_sha256": identities["project_sha256"],
        "ledger_sha256": identities["ledger_sha256"],
    }
    binding_raw = authority.render_binding(binding_values)
    if authority.parse_binding(binding_raw) != binding_values:
        _fail("generated aggregate binding did not round-trip")

    existing_snapshot, snapshot_error = _existing_snapshot(
        originals[SNAPSHOT_PATH], policy_raw, policy
    )
    existing_binding, binding_error = _existing_binding(originals[BINDING_PATH])
    old_by_source = {
        record["source"]: record for record in (existing_snapshot or ())
    }
    source_changes = []
    for record in generated_records:
        old = old_by_source.get(record["source"])
        if old != record:
            source_changes.append({
                "source": record["source"],
                "old": None if old is None else {
                    "bytes": old["bytes"], "sha256": old["sha256"]
                },
                "new": {"bytes": record["bytes"], "sha256": record["sha256"]},
            })

    identity_changes = []
    for name in IDENTITY_ORDER:
        old = None if existing_binding is None else existing_binding.get(name)
        new = identities[name]
        if old != new:
            identity_changes.append({"name": name, "old": old, "new": new})
    old_snapshot_sha = None if existing_binding is None else existing_binding.get("snapshot_sha256")
    if old_snapshot_sha != binding_values["snapshot_sha256"]:
        identity_changes.append({
            "name": "snapshot_sha256",
            "old": old_snapshot_sha,
            "new": binding_values["snapshot_sha256"],
        })

    replacements = {SNAPSHOT_PATH: snapshot_raw, BINDING_PATH: binding_raw}
    files_changed = [path for path in TARGET_PATHS if originals[path] != replacements[path]]
    records, _metadata = materializer.parse_manifest(candidate)
    source_bindings = tuple((record.source, record.bytes, record.sha256) for record in records)
    diagnostics = [message for message in (snapshot_error, binding_error) if message]
    report = {
        "schema": REPORT_SCHEMA,
        "policy": DESCRIPTOR_PATH,
        "snapshot": SNAPSHOT_PATH,
        "binding": BINDING_PATH,
        "source_changes": source_changes,
        "identity_changes": identity_changes,
        "identities": {name: identities[name] for name in IDENTITY_ORDER},
        "snapshot_sha256": binding_values["snapshot_sha256"],
        "resolved_descriptor_sha256": hashlib.sha256(resolved_raw).hexdigest(),
        "generated_state_diagnostics": diagnostics,
        "files_changed": files_changed,
    }
    return RebindPlan(root, originals, replacements, source_bindings, report)


def _write_staged(path: Path, data: bytes, mode: int) -> None:
    descriptor = os.open(os.fspath(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    try:
        if hasattr(os, "fchmod"):
            os.fchmod(descriptor, mode)
        view = memoryview(data)
        while view:
            written = os.write(descriptor, view)
            if written <= 0:
                _fail(f"short write while staging {path}")
            view = view[written:]
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def apply(plan: RebindPlan) -> None:
    changed = [path for path in TARGET_PATHS if plan.originals[path] != plan.replacements[path]]
    if not changed:
        return
    for source, expected_bytes, expected_sha256 in plan.source_bindings:
        actual_bytes, actual_sha256 = _source_identity(plan.root, source)
        if actual_bytes != expected_bytes or actual_sha256 != expected_sha256:
            _fail(f"authenticated dependency source changed after validation: {source}")

    staging = Path(tempfile.mkdtemp(prefix=".native-retirement-rebind-write-", dir=plan.root))
    staged: dict[str, Path] = {}
    backups: dict[str, Optional[Path]] = {}
    replaced: list[str] = []
    try:
        for index, relative in enumerate(changed):
            target = plan.root / PurePosixPath(relative)
            expected = plan.originals[relative]
            current = _optional_regular(target, relative)
            if current != expected:
                _fail(f"generated binding target changed after validation: {relative}")
            mode = 0o644 if expected is None else stat.S_IMODE(target.lstat().st_mode)
            candidate_path = staging / f"{index}.new"
            _write_staged(candidate_path, plan.replacements[relative], mode)
            staged[relative] = candidate_path
            if expected is None:
                backups[relative] = None
            else:
                backup_path = staging / f"{index}.old"
                _write_staged(backup_path, expected, mode)
                backups[relative] = backup_path
        try:
            for relative in changed:
                target = plan.root / PurePosixPath(relative)
                target.parent.mkdir(parents=True, exist_ok=True)
                os.replace(staged[relative], target)
                replaced.append(relative)
            for relative in changed:
                if _read_no_follow(plan.root / PurePosixPath(relative), relative) != plan.replacements[relative]:
                    _fail(f"generated binding verification failed after replacement: {relative}")
        except Exception as error:
            rollback_errors = []
            for relative in reversed(replaced):
                target = plan.root / PurePosixPath(relative)
                backup = backups[relative]
                try:
                    if backup is None:
                        target.unlink(missing_ok=True)
                    else:
                        os.replace(backup, target)
                except OSError as rollback_error:
                    rollback_errors.append(f"{relative}: {rollback_error}")
            if rollback_errors:
                _fail(f"generated binding write failed ({error}); rollback also failed: {'; '.join(rollback_errors)}")
            if isinstance(error, RebindError):
                raise
            _fail(f"generated binding write failed and was rolled back: {error}")
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def run(mode: str, root: Path) -> dict:
    if mode not in ("check", "refresh"):
        _fail(f"unsupported mode: {mode}")
    plan = prepare(root)
    changed = bool(plan.report["files_changed"])
    if mode == "refresh" and changed:
        apply(plan)
        status = "refreshed"
    elif changed:
        status = "stale"
    else:
        status = "current"
    report = dict(plan.report)
    report["mode"] = mode
    report["status"] = status
    return report


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("check", "refresh"))
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    return parser


def main(argv=None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        report = run(arguments.mode, arguments.repo_root)
        print(json.dumps(report, sort_keys=True))
        return 2 if arguments.mode == "check" and report["status"] == "stale" else 0
    except (RebindError, authority.BindingError, materializer.MaterializationError,
            OSError, TypeError, ValueError) as error:
        print(f"native-retirement rebind failure: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
