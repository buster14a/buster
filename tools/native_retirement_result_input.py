#!/usr/bin/env python3
"""Verify bounded, content-addressed JSONL shards without judging results.

Ownership: this module is the policy-neutral evidence-input seam for #512.
Entry points: ``verify`` and ``main``.
Map: Limits fixes resource bounds; descriptor-relative helpers reject path
escape and link traversal; JSON helpers bound one record; ``verify`` streams
content into SHA-256 and an on-disk duplicate-identity index.
The ``Limits`` defaults are immutable hard maxima; callers may only reduce
them for a particular ingestion.

The input manifest is ``buster-streaming-evidence-shards-v1`` version 1:

``{"schema": ..., "version": 1, "identity_field": "record_id",
   "shards": [{"identity": TOKEN, "path": CANONICAL_RELATIVE_PATH,
               "bytes": INTEGER, "sha256": LOWERCASE_SHA256}, ...]}``

Paths are opened component-by-component beneath one held root descriptor.
Symlinks are rejected, and regular files must have link count one; this strict
hard-link policy prevents an evidence pathname from aliasing mutable storage
outside the root. Every absolute-root and shard-relative directory descriptor
is retained and revalidated against its parent entry. Each open file, its
descriptor, and its held parent entry are likewise rechecked after streaming,
so ancestor or file replacement, truncation, growth, or in-place metadata
changes fail closed. The receipt reports integrity only. It defines no
performance fields, trusted receipts, schedule, statistic, or verdict.
"""

import argparse
import errno
import hashlib
import json
import os
import re
import sqlite3
import stat
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from types import MappingProxyType


MANIFEST_SCHEMA = "buster-streaming-evidence-shards-v1"
RECEIPT_SCHEMA = "buster-streaming-evidence-integrity-v1"
TOKEN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:@+-]{0,255}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
READ_CHUNK_BYTES = 64 * 1024
HARD_CAPS = MappingProxyType({
    "max_manifest_bytes": 16 * 1024 * 1024,
    "max_total_bytes": 16 * 1024 * 1024 * 1024,
    "max_shard_bytes": 2 * 1024 * 1024 * 1024,
    "max_shards": 4096,
    "max_records": 16 * 1024 * 1024,
    "max_record_bytes": 1024 * 1024,
    "max_nesting": 32,
    "max_string_bytes": 256 * 1024,
    "max_path_bytes": 4096,
})


class IntegrityError(ValueError):
    """One fail-closed input-integrity error."""


@dataclass(frozen=True)
class Limits:
    """Explicit resource limits; callers may choose smaller admitted values."""

    max_manifest_bytes: int = HARD_CAPS["max_manifest_bytes"]
    max_total_bytes: int = HARD_CAPS["max_total_bytes"]
    max_shard_bytes: int = HARD_CAPS["max_shard_bytes"]
    max_shards: int = HARD_CAPS["max_shards"]
    max_records: int = HARD_CAPS["max_records"]
    max_record_bytes: int = HARD_CAPS["max_record_bytes"]
    max_nesting: int = HARD_CAPS["max_nesting"]
    max_string_bytes: int = HARD_CAPS["max_string_bytes"]
    max_path_bytes: int = HARD_CAPS["max_path_bytes"]

    def checked(self):
        values = asdict(self)
        for name, value in values.items():
            if type(value) is not int or value <= 0 or value > (1 << 63) - 1:
                raise IntegrityError(f"{name} must be a positive bounded integer")
            if value > HARD_CAPS[name]:
                raise IntegrityError(f"{name} exceeds its immutable hard cap of {HARD_CAPS[name]}")
        return self


@dataclass(frozen=True)
class _FileIdentity:
    device: int
    inode: int
    mode: int
    links: int
    size: int
    modified_ns: int
    changed_ns: int


@dataclass(frozen=True)
class _DirectoryIdentity:
    device: int
    inode: int
    mode: int
    links: int
    owner: int
    group: int


@dataclass(frozen=True)
class _HeldDirectory:
    descriptor: int
    name: str
    identity: _DirectoryIdentity


def _fail(message):
    raise IntegrityError(message)


def _platform_supported():
    required_flags = ("O_CLOEXEC", "O_DIRECTORY", "O_NOFOLLOW", "O_NONBLOCK")
    missing = [name for name in required_flags if not hasattr(os, name)]
    if missing or os.open not in os.supports_dir_fd or os.stat not in os.supports_dir_fd:
        _fail("descriptor-relative no-follow verification is unsupported on this platform")


def _canonical_root(value):
    root = os.fspath(value)
    if not isinstance(root, str) or not root.startswith("/") or root != os.path.normpath(root):
        _fail("trusted root must be a canonical absolute path")
    _utf8_size(root, "trusted root")
    if "\0" in root or any(component in ("", ".", "..") for component in root.split("/")[1:]):
        _fail("trusted root must be a canonical absolute path")
    return root


def _utf8_size(value, name):
    try:
        size = len(value.encode("utf-8", errors="strict"))
    except UnicodeEncodeError:
        _fail(f"{name} must contain only Unicode scalar values")
    return size


def _canonical_path(value, limits, name):
    if not isinstance(value, str):
        _fail(f"{name} must be a canonical relative path")
    size = _utf8_size(value, name)
    components = value.split("/")
    if (not value or value.startswith("/") or value.endswith("/") or "\\" in value
            or size > limits.max_path_bytes or any(part in ("", ".", "..") for part in components)
            or any(ord(character) < 0x20 or ord(character) == 0x7f for character in value)):
        _fail(f"{name} must be a canonical relative path")
    return value


def _token(value, name):
    if not isinstance(value, str) or not TOKEN.fullmatch(value):
        _fail(f"{name} must be a canonical token")
    return value


def _sha256(value, name):
    if not isinstance(value, str) or not SHA256.fullmatch(value):
        _fail(f"{name} must be a lowercase SHA-256")
    return value


def _integer(value, name, minimum=0):
    if type(value) is not int or value < minimum or value > (1 << 63) - 1:
        _fail(f"{name} must be a canonical bounded integer")
    return value


def _object(value, name):
    if not isinstance(value, dict):
        _fail(f"{name} must be a JSON object")
    return value


def _exact_keys(value, keys, name):
    value = _object(value, name)
    expected = set(keys)
    actual = set(value)
    if actual != expected:
        _fail(f"{name} fields differ: missing={sorted(expected - actual)} unknown={sorted(actual - expected)}")
    return value


def _regular_identity(value, name):
    if not stat.S_ISREG(value.st_mode):
        _fail(f"{name} is not a regular file")
    if value.st_nlink != 1:
        _fail(f"{name} violates the single-link hard link policy (link count {value.st_nlink})")
    identity = _FileIdentity(
        device=value.st_dev,
        inode=value.st_ino,
        mode=value.st_mode,
        links=value.st_nlink,
        size=value.st_size,
        modified_ns=value.st_mtime_ns,
        changed_ns=value.st_ctime_ns,
    )
    return identity


def _directory_identity(value, name):
    if not stat.S_ISDIR(value.st_mode):
        _fail(f"{name} is not a directory")
    if value.st_nlink < 1:
        _fail(f"{name} directory is unlinked")
    identity = _DirectoryIdentity(
        device=value.st_dev,
        inode=value.st_ino,
        mode=value.st_mode,
        links=value.st_nlink,
        owner=value.st_uid,
        group=value.st_gid,
    )
    return identity


def _directory_flags():
    return os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW


def _held_directory(descriptor, name):
    return _HeldDirectory(descriptor, name, _directory_identity(os.fstat(descriptor), name or "/"))


def _append_held_directory(directories, descriptor, name):
    try:
        directories.append(_held_directory(descriptor, name))
    except BaseException:
        try:
            os.close(descriptor)
        except OSError:
            pass
        raise


def _close_directories(directories):
    for directory in reversed(directories):
        os.close(directory.descriptor)


def _verify_directories(directories, name):
    for index, directory in enumerate(directories):
        try:
            descriptor_identity = _directory_identity(os.fstat(directory.descriptor), name)
            entry_identity = descriptor_identity if index == 0 else _directory_identity(
                os.stat(directory.name, dir_fd=directories[index - 1].descriptor,
                        follow_symlinks=False), name)
        except FileNotFoundError:
            _fail(f"{name} directory ancestor was unlinked or replaced during verification")
        except OSError as error:
            _fail(f"{name} directory ancestor cannot be revalidated: {error}")
        if descriptor_identity != directory.identity or entry_identity != directory.identity:
            _fail(f"{name} directory ancestor was replaced or changed during verification")


def _open_root(path):
    _platform_supported()
    root = _canonical_root(path)
    directories = []
    try:
        descriptor = os.open("/", _directory_flags())
        _append_held_directory(directories, descriptor, "")
        for component in root.split("/")[1:]:
            child = os.open(component, _directory_flags(), dir_fd=directories[-1].descriptor)
            _append_held_directory(directories, child, component)
            descriptor = child
    except IntegrityError:
        _close_directories(directories)
        raise
    except OSError as error:
        _close_directories(directories)
        reason = "symlink or invalid directory" if error.errno in (errno.ELOOP, errno.ENOTDIR) else str(error)
        _fail(f"trusted root traversal failed: {reason}")
    return directories


def _open_regular(root, relative, name):
    descriptor = -1
    directories = []
    components = relative.split("/")
    try:
        parent = os.dup(root)
        _append_held_directory(directories, parent, "")
        for component in components[:-1]:
            child = os.open(component, _directory_flags(), dir_fd=directories[-1].descriptor)
            _append_held_directory(directories, child, component)
            parent = child
        flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
        descriptor = os.open(components[-1], flags, dir_fd=parent)
        identity = _regular_identity(os.fstat(descriptor), name)
    except IntegrityError:
        if descriptor >= 0:
            os.close(descriptor)
        _close_directories(directories)
        raise
    except OSError as error:
        if descriptor >= 0:
            os.close(descriptor)
        _close_directories(directories)
        reason = "symlink or invalid directory" if error.errno in (errno.ELOOP, errno.ENOTDIR) else str(error)
        _fail(f"{name} cannot be opened without following a symlink: {reason}")
    return descriptor, directories, components[-1], identity


def _final_identity(descriptor, parent, leaf, opened, count, name):
    try:
        final_status = os.fstat(descriptor)
        entry_status = os.stat(leaf, dir_fd=parent, follow_symlinks=False)
    except FileNotFoundError:
        _fail(f"{name} was replaced during verification")
    except OSError as error:
        _fail(f"{name} final identity cannot be verified: {error}")
    if (final_status.st_dev, final_status.st_ino) != (opened.device, opened.inode) \
            or (entry_status.st_dev, entry_status.st_ino) != (opened.device, opened.inode):
        _fail(f"{name} was replaced during verification")
    final = _regular_identity(final_status, name)
    entry = _regular_identity(entry_status, name)
    stable = ("mode", "links", "size", "modified_ns", "changed_ns")
    if count != opened.size or any(getattr(final, field) != getattr(opened, field) for field in stable) \
            or any(getattr(entry, field) != getattr(opened, field) for field in stable):
        _fail(f"{name} changed during verification")


def _json_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            _fail(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _json_constant(value):
    _fail(f"non-finite JSON constant is forbidden: {value}")


def _scan_nesting(text, limit, name):
    depth = 0
    quoted = False
    escaped = False
    for character in text:
        if quoted:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
        elif character == '"':
            quoted = True
        elif character in "[{":
            depth += 1
            if depth > limit:
                _fail(f"{name} exceeds the JSON nesting limit")
        elif character in "]}":
            depth -= 1
    # json.loads reports mismatched delimiters and unterminated strings.


def _check_strings(value, limit, name):
    pending = [value]
    while pending:
        item = pending.pop()
        if isinstance(item, str):
            if _utf8_size(item, name) > limit:
                _fail(f"{name} exceeds the JSON string byte limit")
        elif isinstance(item, dict):
            pending.extend(item.keys())
            pending.extend(item.values())
        elif isinstance(item, list):
            pending.extend(item)


def _parse_json(raw, limits, name):
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        _fail(f"{name} is not valid UTF-8: {error}")
    _scan_nesting(text, limits.max_nesting, name)
    try:
        value = json.loads(text, object_pairs_hook=_json_pairs, parse_constant=_json_constant)
    except IntegrityError:
        raise
    except (json.JSONDecodeError, RecursionError, ValueError) as error:
        _fail(f"{name} is not valid JSON: {error}")
    _check_strings(value, limits.max_string_bytes, name)
    return value


def _read_bounded_file(root, relative, limit, limits, name):
    descriptor, directories, leaf, opened = _open_regular(root, relative, name)
    raw = bytearray()
    digest = hashlib.sha256()
    try:
        if opened.size > limit:
            _fail(f"{name} exceeds its byte limit")
        while True:
            chunk = os.read(descriptor, min(READ_CHUNK_BYTES, limit - len(raw) + 1))
            if not chunk:
                break
            raw.extend(chunk)
            digest.update(chunk)
            if len(raw) > limit:
                _fail(f"{name} exceeds its byte limit")
        _final_identity(descriptor, directories[-1].descriptor, leaf, opened, len(raw), name)
        _verify_directories(directories, name)
    finally:
        os.close(descriptor)
        _close_directories(directories)
    return bytes(raw), digest.hexdigest()


def _record(identity_database, raw, limits, identity_field, name, records,
            shard_identity, shard_record, record_consumer):
    if not raw:
        _fail(f"{name} is an empty JSONL record")
    if len(raw) > limits.max_record_bytes:
        _fail(f"{name} exceeds the record byte limit")
    value = _object(_parse_json(bytes(raw), limits, name), name)
    identity = value.get(identity_field)
    if not isinstance(identity, str) or not identity \
            or any(ord(character) < 0x20 or ord(character) == 0x7f for character in identity):
        _fail(f"{name} has no canonical string record identity")
    if records >= limits.max_records:
        _fail("input exceeds the record count limit")
    try:
        identity_database.execute("INSERT INTO identities(identity) VALUES (?)",
                                  (sqlite3.Binary(identity.encode("utf-8")),))
    except sqlite3.IntegrityError:
        _fail(f"duplicate record identity: {identity}")
    if record_consumer is not None:
        record_consumer(shard_identity, shard_record, value)
    return records + 1


def _stream_shard(root, shard, limits, identity_field, identity_database, records,
                  record_consumer, after_stream):
    name = f"shard {shard['identity']}"
    descriptor, directories, leaf, opened = _open_regular(root, shard["path"], name)
    count = 0
    shard_records = 0
    line = bytearray()
    digest = hashlib.sha256()
    try:
        if opened.size != shard["bytes"]:
            _fail(f"{name} differs from its declared byte count")
        if opened.size > limits.max_shard_bytes:
            _fail(f"{name} exceeds the shard byte limit")
        while True:
            chunk = os.read(descriptor, READ_CHUNK_BYTES)
            if not chunk:
                break
            count += len(chunk)
            digest.update(chunk)
            if count > shard["bytes"] or count > limits.max_shard_bytes:
                _fail(f"{name} grew beyond its declared byte count or shard byte limit")
            start = 0
            while start < len(chunk):
                newline = chunk.find(b"\n", start)
                end = len(chunk) if newline < 0 else newline
                line.extend(chunk[start:end])
                if len(line) > limits.max_record_bytes:
                    _fail(f"{name} record exceeds the record byte limit")
                if newline < 0:
                    start = len(chunk)
                else:
                    records = _record(identity_database, line, limits, identity_field,
                                      f"{name} record {shard_records}", records,
                                      shard["identity"], shard_records, record_consumer)
                    shard_records += 1
                    line.clear()
                    start = newline + 1
        if line:
            records = _record(identity_database, line, limits, identity_field,
                              f"{name} record {shard_records}", records,
                              shard["identity"], shard_records, record_consumer)
            shard_records += 1
        if count != shard["bytes"]:
            _fail(f"{name} differs from its declared byte count")
        if digest.hexdigest() != shard["sha256"]:
            _fail(f"{name} differs from its declared SHA-256")
        if after_stream is not None:
            after_stream(shard["identity"], shard["path"])
        _final_identity(descriptor, directories[-1].descriptor, leaf, opened, count, name)
        _verify_directories(directories, name)
    finally:
        os.close(descriptor)
        _close_directories(directories)
    receipt = {"identity": shard["identity"], "path": shard["path"],
               "bytes": count, "sha256": digest.hexdigest(), "records": shard_records}
    return receipt, records


def _manifest(value, manifest_path, manifest_bytes, limits):
    value = _exact_keys(value, ("schema", "version", "identity_field", "shards"), "manifest")
    if value["schema"] != MANIFEST_SCHEMA or type(value["version"]) is not int or value["version"] != 1:
        _fail(f"manifest is not {MANIFEST_SCHEMA} version 1")
    identity_field = _token(value["identity_field"], "manifest.identity_field")
    if not isinstance(value["shards"], list) or not value["shards"]:
        _fail("manifest.shards must be a nonempty array")
    if len(value["shards"]) > limits.max_shards:
        _fail("manifest exceeds the shard count limit")
    identities = set()
    paths = set()
    shards = []
    for index, item in enumerate(value["shards"]):
        name = f"manifest.shards[{index}]"
        item = _exact_keys(item, ("identity", "path", "bytes", "sha256"), name)
        identity = _token(item["identity"], f"{name}.identity")
        path = _canonical_path(item["path"], limits, f"{name}.path")
        size = _integer(item["bytes"], f"{name}.bytes")
        sha = _sha256(item["sha256"], f"{name}.sha256")
        if identity in identities:
            _fail(f"duplicate shard identity: {identity}")
        if path in paths:
            _fail(f"duplicate shard path: {path}")
        if path == manifest_path:
            _fail("manifest cannot also be a shard")
        if size > limits.max_shard_bytes:
            _fail(f"{name} exceeds the shard byte limit")
        identities.add(identity)
        paths.add(path)
        shards.append({"identity": identity, "path": path, "bytes": size, "sha256": sha})
    total = manifest_bytes + sum(item["bytes"] for item in shards)
    if total > limits.max_total_bytes:
        _fail("manifest and shards exceed the total byte limit")
    return identity_field, sorted(shards, key=lambda item: item["identity"]), total


def _identity_database(directory):
    database = sqlite3.connect(Path(directory) / "record-identities.sqlite3")
    database.execute("PRAGMA journal_mode=OFF")
    database.execute("PRAGMA synchronous=OFF")
    database.execute("PRAGMA temp_store=FILE")
    database.execute("PRAGMA cache_size=-1024")
    database.execute("CREATE TABLE identities(identity BLOB PRIMARY KEY) WITHOUT ROWID")
    return database


def verify(evidence_root, manifest_path, limits=Limits(), record_consumer=None, *, _after_stream=None):
    """Stream and verify one integrity manifest, returning a deterministic receipt.

    ``record_consumer(shard_identity, shard_record, value)`` may process each
    tentative record without retaining it. The consumer must not commit any
    interpretation until this function returns successfully because final
    identity and later-shard checks can still fail. ``_after_stream`` is a
    private race-injection seam used only by this module's tests. Successful
    return establishes byte integrity and unique transport identities; it never
    establishes evidence meaning or acceptance.
    """
    limits = limits.checked()
    manifest_path = _canonical_path(os.fspath(manifest_path), limits, "manifest path")
    root_directories = _open_root(evidence_root)
    root = root_directories[-1].descriptor
    try:
        raw, manifest_sha = _read_bounded_file(root, manifest_path, limits.max_manifest_bytes,
                                                limits, "manifest")
        value = _parse_json(raw, limits, "manifest")
        identity_field, shards, total_bytes = _manifest(value, manifest_path, len(raw), limits)
        receipts = []
        records = 0
        with tempfile.TemporaryDirectory(prefix="buster-result-identities-") as temporary:
            database = _identity_database(temporary)
            try:
                for shard in shards:
                    receipt, records = _stream_shard(root, shard, limits, identity_field,
                                                     database, records, record_consumer,
                                                     _after_stream)
                    receipts.append(receipt)
            finally:
                database.close()
        _verify_directories(root_directories, "trusted root")
    finally:
        _close_directories(root_directories)
    receipt = {
        "schema": RECEIPT_SCHEMA,
        "version": 1,
        "scope": "integrity-only",
        "manifest": {"path": manifest_path, "bytes": len(raw), "sha256": manifest_sha},
        "identity_field": identity_field,
        "limits": asdict(limits),
        "input_bytes": total_bytes,
        "records": records,
        "shards": receipts,
    }
    return receipt


def _arguments(argv):
    parser = argparse.ArgumentParser(
        description="Stream content-addressed JSONL shards and emit an integrity-only receipt; no result verdict is computed.")
    parser.add_argument("evidence_root", type=Path, help="canonical absolute evidence root")
    parser.add_argument("manifest", help="canonical relative integrity-manifest path")
    for field, default in asdict(Limits()).items():
        parser.add_argument("--" + field.replace("_", "-"), type=int, default=default,
                            help=f"resource bound (immutable maximum/default: {default})")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = _arguments(argv)
    values = {field: getattr(arguments, field) for field in asdict(Limits())}
    result = 1
    try:
        receipt = verify(arguments.evidence_root, arguments.manifest, Limits(**values))
        print(json.dumps(receipt, sort_keys=True, separators=(",", ":")))
        result = 0
    except (IntegrityError, OSError, sqlite3.Error) as error:
        print(f"native-retirement result input failure: {error}", file=sys.stderr)
    return result


if __name__ == "__main__":
    sys.exit(main())
