#!/usr/bin/env python3
"""Verify and prepare the pinned external closure for native census runs.

This command is intentionally offline.  The workflow performs the GitHub
checkouts at the revisions recorded in the dependency descriptor; this tool
only proves that those local worktrees are pristine, that their remotes name
the declared repositories, and that every admitted source is a tracked blob
at the declared commit.  It also derives the two target-specific musl headers
whose upstream build rules generate bytes rather than storing generated files
in the repository.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tempfile

import native_retirement_dependency_binding as dependency_authority
from native_retirement_materializer import (
    MaterializationError,
    SCHEMA,
    _canonical_digest,
    _descriptor_source_root,
    _external_declarations,
    _read_no_follow,
    _source_path,
    parse_manifest,
)


class ExternalClosureError(ValueError):
    """A pinned external checkout or generated closure failed closed."""


def _fail(message):
    raise ExternalClosureError(message)


def _run_git(path, *arguments):
    try:
        result = subprocess.run(
            ["git", "-C", os.fspath(path), *arguments],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", "") or str(error)
        _fail(f"git verification failed in {path}: {detail.strip()}")
    return result.stdout.strip()


def _assert_directory(path, field):
    path = Path(path)
    if not path.is_absolute():
        _fail(f"{field} must be absolute: {path}")
    current = Path(path.anchor)
    try:
        parts = path.relative_to(current).parts
    except ValueError as error:
        _fail(f"invalid {field}: {error}")
    for part in parts:
        current /= part
        try:
            info = current.lstat()
        except OSError as error:
            _fail(f"cannot inspect {field} {current}: {error}")
        if stat.S_ISLNK(info.st_mode):
            _fail(f"{field} contains a symlink: {current}")
        if not stat.S_ISDIR(info.st_mode):
            _fail(f"{field} is not a directory: {current}")


def _assert_regular(path, field):
    try:
        info = Path(path).lstat()
    except OSError as error:
        _fail(f"cannot inspect {field} {path}: {error}")
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        _fail(f"{field} is not a real file: {path}")


def _remote_repository(remote):
    remote = remote.strip()
    prefixes = ("https://github.com/", "http://github.com/", "ssh://git@github.com/", "git@github.com:")
    tail = next((remote[len(prefix):] for prefix in prefixes if remote.startswith(prefix)), "")
    match = re.fullmatch(r"([^/]+)/([^/]+?)(?:\.git)?", tail, re.IGNORECASE)
    return "/".join(match.groups()).lower() if match else ""


def _verify_checkout(root, declaration):
    checkout = root / PurePosixPath(declaration["path"])
    _assert_directory(checkout, f"external checkout {declaration['name']}")
    revision = declaration["revision"]
    actual = _run_git(checkout, "rev-parse", "--verify", "HEAD^{commit}")
    if actual != revision:
        _fail(f"external checkout {declaration['name']} is at {actual}, expected {revision}")
    dirty = _run_git(checkout, "status", "--porcelain=v1", "--untracked-files=all")
    if dirty:
        _fail(f"external checkout {declaration['name']} is dirty")
    remote = _run_git(checkout, "config", "--get", "remote.origin.url")
    if _remote_repository(remote) != declaration["repository"].lower():
        _fail(f"external checkout {declaration['name']} remote is not {declaration['repository']}")
    return checkout


def _verify_tracked_blob(checkout, revision, relative, source):
    relative = PurePosixPath(relative)
    if not relative.parts or any(part in ("", ".", "..") for part in relative.parts):
        _fail(f"external source is not canonical: {source}")
    object_name = f"{revision}:{relative.as_posix()}"
    object_type = _run_git(checkout, "cat-file", "-t", object_name)
    if object_type != "blob":
        _fail(f"external source is not a tracked file at its pin: {source}")
    local = checkout.joinpath(*relative.parts)
    _assert_regular(local, f"external source {source}")


def _ensure_directory(path, field):
    path = Path(path)
    if not path.is_absolute():
        _fail(f"{field} must be absolute")
    current = Path(path.anchor)
    try:
        parts = path.relative_to(current).parts
    except ValueError as error:
        _fail(f"invalid {field}: {error}")
    for part in parts:
        current /= part
        try:
            info = current.lstat()
        except FileNotFoundError:
            try:
                current.mkdir()
                info = current.lstat()
            except OSError as error:
                _fail(f"cannot create {field} {current}: {error}")
        except OSError as error:
            _fail(f"cannot inspect {field} {current}: {error}")
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
            _fail(f"{field} contains a non-directory: {current}")


def _publish_generated(path, data, field):
    path = Path(path)
    _ensure_directory(path.parent, f"{field} parent")
    if path.exists() or path.is_symlink():
        _assert_regular(path, field)
        if path.read_bytes() != data:
            _fail(f"generated closure tamper detected: {path}")
        return
    try:
        descriptor = os.open(os.fspath(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
        try:
            view = memoryview(data)
            while view:
                written = os.write(descriptor, view)
                view = view[written:]
        finally:
            os.close(descriptor)
    except OSError as error:
        _fail(f"cannot publish generated closure {path}: {error}")


def _musl_generated(checkout, declaration):
    name = declaration["name"]
    if name not in ("musl-x86_64", "musl-aarch64"):
        _fail(f"unsupported generated external closure: {name}")
    arch = name.removeprefix("musl-")
    expected_path = f"external/musl-generated/{arch}"
    if declaration["path"] != expected_path:
        _fail(f"generated musl path is not target-specific: {declaration['path']}")
    if declaration["generator"] != f"sed:tools/mkalltypes.sed+arch/{arch}/bits/alltypes.h.in+include/alltypes.h.in;syscall-sed":
        _fail(f"generated musl rule is not the pinned upstream rule: {name}")
    sed_script = checkout / "tools" / "mkalltypes.sed"
    alltypes_input = checkout / "arch" / arch / "bits" / "alltypes.h.in"
    common_input = checkout / "include" / "alltypes.h.in"
    syscall_input = checkout / "arch" / arch / "bits" / "syscall.h.in"
    for path, field in ((sed_script, "musl generator"), (alltypes_input, "musl alltypes input"),
                        (common_input, "musl common alltypes input"), (syscall_input, "musl syscall input")):
        _assert_regular(path, field)
    try:
        alltypes = subprocess.run(
            ["sed", "-f", os.fspath(sed_script), os.fspath(alltypes_input), os.fspath(common_input)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        ).stdout
        syscall_tail = subprocess.run(
            ["sed", "-n", "-e", "s/__NR_/SYS_/p", os.fspath(syscall_input)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        _fail(f"musl generated header command failed for {arch}: {error}")
    syscall = syscall_input.read_bytes() + syscall_tail
    output = Path(declaration["path"])
    return {"alltypes.h": alltypes, "syscall.h": syscall}, output


def _verify_record_bytes(root, records):
    for record in records:
        path = _source_path(root, record.source)
        try:
            data = path.read_bytes()
        except OSError as error:
            _fail(f"cannot read external closure source {record.source}: {error}")
        if len(data) != record.bytes or hashlib.sha256(data).hexdigest() != record.sha256:
            _fail(f"external closure source identity mismatch: {record.source}")


def prepare(manifest_path, source_root=None):
    manifest_path = Path(manifest_path).resolve()
    raw = _read_no_follow(manifest_path, "dependency manifest")
    try:
        manifest = json.loads(raw.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot read dependency manifest {manifest_path}: {error}")
    if not isinstance(manifest, dict):
        _fail("unsupported dependency manifest schema")
    if source_root is None:
        declared = manifest.get("source_root", ".")
        source_root = manifest_path.parent / PurePosixPath(declared)
    root = _descriptor_source_root(manifest_path, manifest, source_root)
    if manifest.get("schema") == dependency_authority.POLICY_SCHEMA:
        policy = dependency_authority.parse_policy(raw)
        # External preparation precedes the trusted rebinder. An ordinary
        # source change therefore still has the previous generated snapshot.
        # Resolve only policy-declared repo identities from the current tree
        # in memory; pinned external/SDK identities remain policy-owned. The
        # complete record verification below still checks every resolved byte.
        def repository_identity(source):
            data = _read_no_follow(_source_path(root, source), "repository project source")
            return len(data), hashlib.sha256(data).hexdigest()

        snapshot_raw, _records = dependency_authority.render_snapshot(raw, policy, repository_identity)
        snapshot = dependency_authority.parse_snapshot(snapshot_raw, raw, policy)
        legacy_raw = _read_no_follow(root / PurePosixPath(dependency_authority.LEGACY_DESCRIPTOR_PATH),
                                     "legacy dependency descriptor")
        resolved_raw = dependency_authority.render_resolved_descriptor(policy, snapshot, legacy_raw)
        manifest = dependency_authority.strict_json(resolved_raw, "resolved dependency descriptor")
    elif manifest.get("schema") != SCHEMA:
        _fail("unsupported dependency manifest schema")
    records, _metadata = parse_manifest(manifest)
    checkouts, generated = _external_declarations(manifest)
    checkout_paths = {}
    for declaration in checkouts:
        checkout_paths[declaration["name"]] = _verify_checkout(root, declaration)
    for record in records:
        matched = False
        for declaration in checkouts:
            prefix = declaration["path"] + "/"
            if record.source.startswith(prefix):
                relative = record.source[len(prefix):]
                _verify_tracked_blob(checkout_paths[declaration["name"]], declaration["revision"], relative, record.source)
                matched = True
                break
        if matched:
            continue
        if record.source.startswith("external/"):
            # Generated records are checked after the generator runs below.
            if any(record.source.startswith(item["path"] + "/") for item in generated):
                continue
            _fail(f"external record is not bound to a checkout or generator: {record.source}")
    generated_root = root
    for declaration in generated:
        checkout = checkout_paths[declaration["checkout"]]
        files, relative_output = _musl_generated(checkout, declaration)
        for name, data in files.items():
            _publish_generated(generated_root / relative_output / "include" / "bits" / name, data,
                               f"generated musl {declaration['name']} {name}")
    _verify_record_bytes(root, records)
    closure = _canonical_digest({
        "checkouts": list(checkouts),
        "generated": list(generated),
        "records": [{"source": r.source, "bytes": r.bytes, "sha256": r.sha256} for r in records],
    })
    return {"schema": SCHEMA, "checkouts": len(checkouts), "generated": len(generated),
            "records": len(records), "external_closure_sha256": closure}


def _self_test():
    """Exercise the generator-free failure boundary without network access."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory).resolve()
        checkout = root / "external" / "musl"
        checkout.mkdir(parents=True)
        try:
            _verify_checkout(root, {"name": "musl", "path": "external/musl",
                                    "revision": "0" * 40, "repository": "ifduyue/musl"})
        except ExternalClosureError:
            return {"schema": SCHEMA, "self_test": True}
        _fail("external self-test admitted a non-repository directory")


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "self-test"))
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--source-root", type=Path)
    return parser


def main(argv=None):
    arguments = _parser().parse_args(argv)
    try:
        if arguments.command == "self-test":
            result = _self_test()
        elif arguments.manifest is None:
            raise ExternalClosureError("prepare requires --manifest")
        else:
            result = prepare(arguments.manifest, arguments.source_root)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (ExternalClosureError, MaterializationError, OSError, ValueError, TypeError) as error:
        print(f"native-retirement external closure failure: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
