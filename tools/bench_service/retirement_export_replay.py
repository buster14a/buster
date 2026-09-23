#!/usr/bin/env python3
"""Publish a test export and replay retirement evidence from its downloaded bytes.

The two receipt digests are operator inputs obtained from the authenticated
service, independently of the archive. This command cannot attest either one.
It never executes a file from the bundle and does not publish a release asset.
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

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import native_retirement_performance_binding as binding


CHUNK = 64 * 1024
RECEIPT_BYTES = 1024
# export.c's separate blocked-retirement ceiling, including archive headers.
ARCHIVE_CAP = 137448259584
# export.c reserves a full 64-byte digest index up to the recipe's archive cap.
SPOOL_INDEX_BYTES = ((ARCHIVE_CAP + CHUNK - 1) // CHUNK) * 64
RECIPE = b"native-retirement-performance-v1"
SHA256 = re.compile(r"[0-9a-f]{64}\Z")


def fail(message):
    raise ValueError(message)


def digest_argument(value, name):
    if not SHA256.fullmatch(value):
        fail(f"{name} must be a lowercase SHA-256 digest")
    return value


def u32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "little")


def u64(data, offset):
    return int.from_bytes(data[offset:offset + 8], "little")


def archive_source(path, receipt_sha256, job, attempt, full_sha256):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
            fail("download is not a unique regular file")
        with os.fdopen(descriptor, "rb", closefd=False) as stream:
            receipt = stream.read(RECEIPT_BYTES)
        if len(receipt) != RECEIPT_BYTES or receipt[:8] != b"BQEXP001":
            fail("download lacks the complete export receipt")
        size = u64(receipt, 24)
        if not 0 < size <= ARCHIVE_CAP or before.st_size != RECEIPT_BYTES + size:
            fail("download length exceeds the retirement limit or is incomplete")
        if hashlib.sha256(receipt).hexdigest() != receipt_sha256:
            fail("export receipt differs from the independently supplied digest")
        if (u64(receipt, 8), u64(receipt, 16)) != (job, attempt):
            fail("export job or attempt differs from the authenticated result")
        if receipt[240:304] != full_sha256.encode("ascii"):
            fail("export full-result digest differs from the authenticated result")
        if receipt[560:608] != RECIPE.ljust(48, b"\0"):
            fail("export is not the retirement recipe")
        if u32(receipt, 1020) != 7:
            fail("exported attempt is not terminal")
        return descriptor, before, receipt
    except BaseException:
        os.close(descriptor)
        raise


def same_file(first, second):
    return (first.st_dev, first.st_ino, first.st_mode, first.st_size,
            first.st_mtime_ns, first.st_ctime_ns) == (
            second.st_dev, second.st_ino, second.st_mode, second.st_size,
            second.st_mtime_ns, second.st_ctime_ns)


def copy_and_hash(source, target, receipt, size):
    archive = hashlib.sha256()
    complete = hashlib.sha256()
    copied = 0
    os.lseek(source, 0, os.SEEK_SET)
    while copied < size:
        block = os.read(source, min(CHUNK, size - copied))
        if not block:
            fail("download was truncated during publication")
        complete.update(block)
        if copied >= RECEIPT_BYTES:
            archive.update(block)
        elif copied + len(block) > RECEIPT_BYTES:
            archive.update(block[RECEIPT_BYTES - copied:])
        offset = 0
        while offset < len(block):
            written = os.write(target, block[offset:])
            if written <= 0:
                fail("short write during test publication")
            offset += written
        copied += len(block)
    if archive.hexdigest().encode("ascii") != receipt[304:368]:
        fail("download archive differs from the authenticated export receipt")
    return complete.hexdigest()


def file_hash(descriptor, size):
    value = hashlib.sha256()
    os.lseek(descriptor, 0, os.SEEK_SET)
    read = 0
    while read < size:
        block = os.read(descriptor, min(CHUNK, size - read))
        if not block:
            fail("published export was truncated during readback")
        value.update(block)
        read += len(block)
    return value.hexdigest()


def publish_test_copy(source, before, receipt, directory, job, attempt):
    parent = os.open(directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    pending = f"retirement-{job}-{attempt}.pending"
    sealed = f"retirement-{job}-{attempt}.bqexport"
    target = -1
    try:
        owner = os.fstat(parent)
        if owner.st_uid != os.geteuid() or owner.st_mode & 0o077:
            fail("test publication directory must be private and owned by the caller")
        # A failed prefix is retained for inspection. Neither it nor a
        # successful publication may be silently replaced by a retry.
        if any(name in os.listdir(parent) for name in (pending, sealed)):
            fail("test publication identity already exists")
        target = os.open(pending, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                         0o600, dir_fd=parent)
        original_sha256 = copy_and_hash(source, target, receipt, before.st_size)
        if not same_file(before, os.fstat(source)):
            fail("download changed while it was copied")
        os.fchmod(target, 0o400)
        os.fsync(target)
        os.close(target)
        target = -1
        os.link(pending, sealed, src_dir_fd=parent, dst_dir_fd=parent, follow_symlinks=False)
        os.fsync(parent)
        os.unlink(pending, dir_fd=parent)
        os.fsync(parent)
        published = os.open(sealed, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
        try:
            if os.fstat(published).st_size != before.st_size or \
                    file_hash(published, before.st_size) != original_sha256:
                fail("published export differs from the downloaded bytes")
        finally:
            os.close(published)
        return str(Path(directory) / sealed)
    finally:
        if target >= 0:
            os.close(target)
        os.close(parent)


def retrieve_test_copy(source, before, receipt, directory, job, attempt, publication):
    # This is a byte transfer into a separate private workspace, not a hard
    # link or a second view of the publisher's directory.
    published_parent = os.open(publication.parent, os.O_RDONLY | os.O_DIRECTORY |
                               os.O_NOFOLLOW | os.O_CLOEXEC)
    retrieval_parent = os.open(directory, os.O_RDONLY | os.O_DIRECTORY |
                               os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        first = os.fstat(published_parent)
        second = os.fstat(retrieval_parent)
        if (first.st_dev, first.st_ino) == (second.st_dev, second.st_ino):
            fail("retrieval must use a separate private directory")
    finally:
        os.close(published_parent)
        os.close(retrieval_parent)
    return publish_test_copy(source, before, receipt, directory, job, attempt)


def relative_binding_path(text):
    relative = PurePosixPath(text)
    if (not text or relative.is_absolute() or
            relative.as_posix() != text or
            any(part in ("", ".", "..") for part in text.split("/"))):
        fail("binding must be a canonical path inside the downloaded result")
    return relative


def replay(args):
    receipt_sha256 = digest_argument(args.export_receipt_sha256, "export receipt")
    trusted_sha256 = digest_argument(args.trusted_execution_receipt_sha256, "service execution receipt")
    full_sha256 = digest_argument(args.full_result_sha256, "full result")
    if args.job <= 0 or args.attempt <= 0:
        fail("job and attempt must be positive")
    record = relative_binding_path(args.binding)
    source, original, receipt = archive_source(
        args.download, receipt_sha256, args.job, args.attempt, full_sha256)
    try:
        if args.consume_published:
            published = retrieve_test_copy(source, original, receipt, args.retrieval,
                                           args.job, args.attempt, args.download)
            stage = "retrieved_export"
        else:
            published = publish_test_copy(source, original, receipt, args.test_publication,
                                          args.job, args.attempt)
            stage = "test_publication"
    finally:
        os.close(source)
    print(json.dumps({stage: published, "transferred_bytes": original.st_size,
                      "archive_bytes": u64(receipt, 24),
                      "indexed_file_bytes": u64(receipt, 32),
                      "files": u32(receipt, 40), "entries": u32(receipt, 44),
                      "archive_chunks": (u64(receipt, 24) + CHUNK - 1) // CHUNK,
                      "reserved_service_spool_bytes": RECEIPT_BYTES + SPOOL_INDEX_BYTES + u64(receipt, 24),
                      "export_receipt_sha256": receipt_sha256}, sort_keys=True), flush=True)
    if args.publish_only:
        return
    command = [str(args.bench_service), "unpack-export", published,
               str(args.destination), receipt_sha256]
    subprocess.run(command, check=True, timeout=86405)
    # A failed or interrupted attempt is retained by the export and unpack,
    # but must never be promoted to a successful performance replay.
    if u32(receipt, 1012) != 1 or u32(receipt, 1016) != 1:
        fail("attempt is retained as failed, interrupted or not valid; no performance replay")
    record_path = args.destination.joinpath(*record.parts)
    if not record_path.is_file() or record_path.is_symlink():
        fail("downloaded result lacks a regular binding record")
    validation = subprocess.run(
        [sys.executable, str(Path(binding.__file__).resolve()), str(record_path),
         "--evidence-root", str(args.destination),
         "--repository-root", str(args.repository_root),
         "--trusted-execution-receipt-sha256", trusted_sha256],
        check=True, capture_output=True, text=True, timeout=86400)
    result = json.loads(validation.stdout)
    if result["proof"] != "independent-evidence-and-receipts-checked" or \
            not all(result[key] for key in ("rows_recomputed", "support_checked",
                                           "execution_checked", "bundle_checked", "git_checked")):
        fail("complete independent retirement replay was not established")
    # Use the same descriptor-anchored, digest-bound reader as the production
    # validator. Match the authenticated export attempt to its transcript.
    with record_path.open(encoding="utf-8") as stream:
        record_value = json.load(stream)
    bundle_descriptor = record_value["workflow"]["phases"]["sealed_result"]
    sealed = binding._read_json_evidence(args.destination, bundle_descriptor, "sealed_result")
    result_bundle = binding._read_json_evidence(args.destination, sealed["result_bundle"], "result_bundle")
    execution = binding._read_trusted_json_evidence(
        args.destination, result_bundle["execution_receipt"], "execution_receipt",
        trusted_sha256, binding.EXECUTION_RECEIPT_BYTE_CAP)
    if execution["job_id"] != str(args.job) or execution["attempt"] != args.attempt:
        fail("service execution receipt does not identify the exported job and attempt")
    print(json.dumps({"replay": "verified-without-admission", "job": args.job,
                      "attempt": args.attempt, "proof": result["proof"],
                      "required_rows": result["required_rows"],
                      "trusted_execution_receipt_sha256": trusted_sha256}, sort_keys=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("download", type=Path, help="completed gateway export from stdout")
    parser.add_argument("destination", nargs="?", type=Path,
                        help="new private clean replay directory (required for replay)")
    parser.add_argument("--test-publication", type=Path,
                        help="existing private directory for an immutable test copy")
    parser.add_argument("--retrieval", type=Path,
                        help="new private directory for a fresh copy of the published export")
    parser.add_argument("--consume-published", action="store_true",
                        help="read an immutable test publication in a separate consumer invocation")
    parser.add_argument("--publish-only", action="store_true",
                        help="publish test bytes without claiming a completed replay")
    parser.add_argument("--bench-service", required=True, type=Path,
                        help="reviewed local service utility, never taken from the bundle")
    parser.add_argument("--repository-root", required=True, type=Path,
                        help="checkout with the immutable Git objects needed for replay")
    parser.add_argument("--binding", required=True, help="binding path within the service result")
    parser.add_argument("--job", required=True, type=int)
    parser.add_argument("--attempt", required=True, type=int)
    parser.add_argument("--full-result-sha256", required=True)
    parser.add_argument("--export-receipt-sha256", required=True,
                        help="from the authenticated gateway export, never the archive")
    parser.add_argument("--trusted-execution-receipt-sha256", required=True,
                        help="from the service control authority, never the bundle")
    args = parser.parse_args()
    try:
        if args.publish_only and args.consume_published:
            fail("publication and independent consumption are separate invocations")
        if args.consume_published and (args.retrieval is None or args.test_publication is not None):
            fail("consumer requires --retrieval and cannot publish")
        if not args.consume_published and (args.test_publication is None or args.retrieval is not None):
            fail("publisher requires --test-publication and cannot retrieve")
        if not args.publish_only and args.destination is None:
            fail("replay requires a new private destination")
        replay(args)
    except (ValueError, OSError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired, KeyError, TypeError) as error:
        print(f"retirement export replay failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
