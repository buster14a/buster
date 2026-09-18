#!/usr/bin/env python3
"""Check or refresh native-retirement dependency bindings after integration.

Only existing repository-owned project records are eligible for identity
refresh. External project records, resource/SDK records, archived replay data,
and every other descriptor field remain pinned and must materialize exactly.
"""

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
import tempfile

import native_retirement_materializer as materializer
from native_retirement_rebind_contract import (
    C_BINDING_PATH,
    DESCRIPTOR_PATH,
    DOCUMENT_PATH,
    IDENTITY_ORDER,
    PYTHON_BINDING_PATH,
    REPORT_SCHEMA,
    SDK_MANIFEST_PATH,
    SHA256_RE,
    TARGET_PATHS,
    RebindError,
    RebindPlan,
    _absolute_root,
    _canonical_json,
    _decode,
    _eligible_projects,
    _extract_bindings,
    _fail,
    _load_descriptor,
    _read_no_follow,
    _replace_bindings,
    _source_identity,
    _validate_external_declarations,
    _validate_sdk_declarations,
)


def _materialize_identities(root, manifest, descriptor_raw):
    descriptor_sha256 = hashlib.sha256(descriptor_raw).hexdigest()
    temporary_parent = Path(tempfile.gettempdir()).resolve()
    temporary = Path(tempfile.mkdtemp(prefix="native-retirement-rebind-materialize-", dir=temporary_parent))
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
            _fail("materializer receipt is not bound to the checked-in descriptor identity")
        ledger_raw = _read_no_follow(output / "dependencies.tsv", "materializer ledger")
        ledger_sha256 = hashlib.sha256(ledger_raw).hexdigest()
        if receipt.get("ledger_sha256") != ledger_sha256:
            _fail("materializer ledger digest does not match its receipt")
        project_sha256 = receipt.get("project_include_sha256")
        if not isinstance(project_sha256, str) or not re.fullmatch(SHA256_RE, project_sha256):
            _fail("materializer receipt has no canonical project closure identity")
        return {
            "descriptor_sha256": descriptor_sha256,
            "receipt_sha256": hashlib.sha256(receipt_raw).hexdigest(),
            "project_sha256": project_sha256,
            "ledger_sha256": ledger_sha256,
        }
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def prepare(root):
    root = _absolute_root(root)
    originals = {
        path: _read_no_follow(root / PurePosixPath(path), path)
        for path in TARGET_PATHS
    }
    manifest = _load_descriptor(originals[DESCRIPTOR_PATH])
    old_identities = _extract_bindings(originals)
    contract_text = _decode(originals[PYTHON_BINDING_PATH], PYTHON_BINDING_PATH)
    _validate_external_declarations(manifest, contract_text)
    sdk_manifest_raw = _read_no_follow(root / PurePosixPath(SDK_MANIFEST_PATH), SDK_MANIFEST_PATH)
    _validate_sdk_declarations(manifest, sdk_manifest_raw)

    candidate = copy.deepcopy(manifest)
    source_changes = []
    for index, record in _eligible_projects(manifest):
        size, digest = _source_identity(root, record["source"], "repository project source")
        if size != record["bytes"] or digest != record["sha256"]:
            source_changes.append({
                "source": record["source"],
                "destination": record["destination"],
                "old": {"bytes": record["bytes"], "sha256": record["sha256"]},
                "new": {"bytes": size, "sha256": digest},
            })
            candidate["projects"][index]["bytes"] = size
            candidate["projects"][index]["sha256"] = digest

    descriptor_raw = _canonical_json(candidate)
    # Re-parse the exact candidate bytes before touching the filesystem.  This
    # proves that refreshing identities did not alter descriptor shape.
    candidate = _load_descriptor(descriptor_raw)
    new_identities = _materialize_identities(root, candidate, descriptor_raw)
    canonical_records, _metadata = materializer.parse_manifest(candidate)
    source_bindings = tuple(
        (record.source, record.bytes, record.sha256) for record in canonical_records
    )
    replacements = {DESCRIPTOR_PATH: descriptor_raw}
    replacements.update(_replace_bindings(originals, new_identities))
    files_changed = [path for path in TARGET_PATHS if originals[path] != replacements[path]]
    identity_changes = [
        {"name": name, "old": old_identities[name], "new": new_identities[name]}
        for name in IDENTITY_ORDER if old_identities[name] != new_identities[name]
    ]
    report = {
        "schema": REPORT_SCHEMA,
        "descriptor": DESCRIPTOR_PATH,
        "source_changes": source_changes,
        "identity_changes": identity_changes,
        "identities": {name: new_identities[name] for name in IDENTITY_ORDER},
        "files_changed": files_changed,
    }
    return RebindPlan(
        root=root, originals=originals, replacements=replacements,
        source_bindings=source_bindings, report=report,
    )


def _write_staged(path, data, mode):
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


def apply(plan):
    changed = [path for path in TARGET_PATHS if plan.originals[path] != plan.replacements[path]]
    if not changed:
        return
    for source, expected_bytes, expected_sha256 in plan.source_bindings:
        actual_bytes, actual_sha256 = _source_identity(plan.root, source)
        if actual_bytes != expected_bytes or actual_sha256 != expected_sha256:
            _fail(f"authenticated dependency source changed after validation: {source}")
    staging = Path(tempfile.mkdtemp(prefix=".native-retirement-rebind-write-", dir=plan.root))
    staged = {}
    backups = {}
    replaced = []
    try:
        for index, relative in enumerate(changed):
            target = plan.root / PurePosixPath(relative)
            current = _read_no_follow(target, relative)
            if current != plan.originals[relative]:
                _fail(f"binding target changed after validation: {relative}")
            mode = stat.S_IMODE(target.lstat().st_mode)
            candidate_path = staging / f"{index}.new"
            backup_path = staging / f"{index}.old"
            _write_staged(candidate_path, plan.replacements[relative], mode)
            _write_staged(backup_path, plan.originals[relative], mode)
            staged[relative] = candidate_path
            backups[relative] = backup_path
        try:
            for relative in changed:
                target = plan.root / PurePosixPath(relative)
                os.replace(staged[relative], target)
                replaced.append(relative)
            for relative in changed:
                if _read_no_follow(plan.root / PurePosixPath(relative), relative) != plan.replacements[relative]:
                    _fail(f"binding target verification failed after replacement: {relative}")
        except Exception as error:
            rollback_errors = []
            for relative in reversed(replaced):
                try:
                    os.replace(backups[relative], plan.root / PurePosixPath(relative))
                except OSError as rollback_error:
                    rollback_errors.append(f"{relative}: {rollback_error}")
            if rollback_errors:
                _fail(f"binding write failed ({error}); rollback also failed: {'; '.join(rollback_errors)}")
            if isinstance(error, RebindError):
                raise
            _fail(f"binding write failed and was rolled back: {error}")
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def run(mode, root):
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


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("check", "refresh"))
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (defaults to the parent of tools/)",
    )
    return parser


def main(argv=None):
    arguments = _parser().parse_args(argv)
    try:
        report = run(arguments.mode, arguments.repo_root)
        print(json.dumps(report, sort_keys=True))
        if arguments.mode == "check" and report["status"] == "stale":
            return 2
        return 0
    except (RebindError, materializer.MaterializationError, OSError, TypeError, ValueError) as error:
        print(f"native-retirement rebind failure: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
