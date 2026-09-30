#!/usr/bin/env python3
"""Publish a test export and replay retirement evidence from its downloaded bytes.

The two receipt digests are operator inputs obtained from the authenticated
service, independently of the archive. This command cannot attest either one.
It never executes a file from the bundle and does not publish a release asset.

Map: ``archive_source`` checks the pinned export receipt; ``publish_test_copy``
and ``retrieve_test_copy`` make the immutable test publication and the fresh
consumer copy; ``replay`` unpacks with the reviewed service utility, runs the
production binding validator over lane F's final binding
(``lane_f_import``, ``final_binding_check``) and ``authenticated_attempt_join``.
``copy_ledger``/``capacity_ledger`` give the receipt-derived six-copy ledger;
``a1_export_ledger`` maps the A1 campaign model (metrics shards, untimed
records) onto the export limits, printed by the ``a1-capacity`` subcommand.
``composer_binding_path`` holds the binding to E's fixed result location.
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
# export.c's separate blocked-retirement ceiling, including archive headers:
# BQ_EXPORT_RETIREMENT_TOTAL_CAP. The real-output test recomputes every
# export limit below from its C #define.
ARCHIVE_CAP = 137448259584
# The retirement store's owned-plus-external byte ceiling
# (TP_RETIREMENT_STORE_TOTAL_BYTES). The worker's non-control payload cap
# (BQ_WORKER_RETIREMENT_BUNDLE_TOTAL_CAP) has the same value; export.c's
# inventory admits up to ARCHIVE_CAP, so this is the tighter bound.
RESULT_FILE_CAP = 128 * 1024 * 1024 * 1024
# BQ_WORKER_BUNDLE_ENTRY_CAP: files, directories and the worker's controls.
ENTRY_CAP = 4096
# BQ_WORKER_BUNDLE_FILE_CAP: export and the worker reject only a larger file,
# so a full 64 MiB metrics or transcript shard is one admissible entry.
FILE_CAP = 64 * 1024 * 1024
# BQ_PATH_CAP; each archive entry has a 16-byte (type, length, size) header.
PATH_CAP = 192
ENTRY_HEADER_BYTES = 16
# Worker-owned manifest, BQ-BUNDLE-V1 index and outcome record.
WORKER_CONTROL_ENTRIES = 3
# The store-owned execution receipt at TP_RETIREMENT_RECEIPT_BYTES.
STORE_RECEIPT_BYTES = 1024 * 1024
# BQ_WORKER_BUNDLE_CAP (the 8 MiB bundle index) and the two 32 KiB control
# reservations in export.c's BQ_EXPORT_RETIREMENT_TOTAL_CAP.
BUNDLE_INDEX_CAP = 8 * 1024 * 1024
EXPORT_CONTROL_RESERVE = 2 * 32768
# export.c reserves a full 64-byte digest index up to the recipe's archive cap.
SPOOL_INDEX_BYTES = ((ARCHIVE_CAP + CHUNK - 1) // CHUNK) * 64
RECIPE = b"native-retirement-performance-v1"
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
# tp_retirement_store_job_label (tools/throughput/retirement_store.h), which
# bq_retirement_campaign_job_label and the authority handoff share: the service
# labels the execution receipt's job as "job-<numeric job id>"; the export
# receipt carries the numeric id.
SERVICE_JOB_LABEL = "job-{}"
# The reviewed composer fixes where the binding record lives inside the
# finalized service result: the worker-unit producer writes it at the result
# root (BQ_RETIREMENT_WORKER_BINDING_PATH, retirement_worker_compose.c, #881
# PR 3), and --binding must equal it. That record carries the sealed-result
# and independent-replay phases as pending descriptors (the sealed result
# binds the record's digest), so it can never pass a complete replay itself.
COMPOSER_BINDING_PATH = "retirement-binding.json"
# The composer's pending descriptor (BQ_RETIREMENT_WORKER_PENDING_SHA256).
PENDING_DESCRIPTOR = {"bytes": 1, "sha256": "0" * 64}
# Lane F's final binding: the composed record with its two late phases
# filled (the composed sealed result, and lane F's publication and
# independent replay). It is produced after the service result is exported,
# so it lives with lane F's replay evidence in the operator's lane-F
# directory (--lane-f), outside the service result; the replay copies that
# directory into the clean replay destination and validates this file.
FINAL_BINDING_NAME = "retirement-final-binding.json"
FINAL_PHASES = ("sealed_result", "independent_replay")


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


def copy_ledger(archive_bytes, file_bytes):
    """The six logical file/transport copies of one archive and its result.

    The service spool reserves its entire sparse chunk index, even for a
    smaller archive. Filesystem allocation and separate durable publication
    are deliberately outside this six-copy file-byte ledger.
    """
    exported_bytes = RECEIPT_BYTES + archive_bytes
    copies = {
        "retained_service_result_bytes": file_bytes,
        "sealed_service_spool_bytes": exported_bytes + SPOOL_INDEX_BYTES,
        "gateway_download_bytes": exported_bytes,
        "immutable_test_publication_bytes": exported_bytes,
        "fresh_retrieval_bytes": exported_bytes,
        "extracted_clean_replay_bytes": file_bytes,
    }
    return {"copies": copies, "logical_six_copy_file_bytes": sum(copies.values())}


def capacity_ledger(receipt):
    """Logical file/transport capacity from the independently pinned receipt."""
    archive_bytes = u64(receipt, 24)
    file_bytes = u64(receipt, 32)
    files = u32(receipt, 40)
    entries = u32(receipt, 44)
    if not (0 < archive_bytes <= ARCHIVE_CAP and
            0 < file_bytes <= RESULT_FILE_CAP and file_bytes <= archive_bytes and
            0 < files <= entries <= ENTRY_CAP):
        fail("export receipt has an invalid retirement capacity inventory")
    return copy_ledger(archive_bytes, file_bytes)


# tp_retirement_compose_bounds (#1879, tools/bench_service/retirement_compose.c)
# byte bounds of the composer's outputs. The unit test re-reads each from the
# composer source when that branch is reachable.
COMPOSE_RATIO_LINE_BYTES = 32
COMPOSE_MEMBER_LINE_BYTES = 256
COMPOSE_SERIES_HEADER_BYTES = 256
COMPOSE_SERIES_END_BYTES = 4
COMPOSE_REPLAY_MEMBER_BYTES = 1024
COMPOSE_REPLAY_FIXED_BYTES = 256
COMPOSE_MANIFEST_FIXED_BYTES = 256
COMPOSE_MANIFEST_SHARD_BYTES = 384
COMPOSE_BUNDLE_BYTES = 16384
COMPOSE_SEAL_FIXED_BYTES = 2048
COMPOSE_SEAL_ENTRY_BYTES = 512
# The retained manifest: sizeof(TP_RETIREMENT_RETAINED_MANIFEST_HEADER), the
# 26-byte header line plus its NUL, then one bounded line per store file.
RETAINED_MANIFEST_HEADER_SIZE = 27
COMPOSE_RETAINED_LINE_BYTES = 320
COMPOSE_DIMENSIONS = 6
COMPOSE_BOOTSTRAP_MEMBERS = 80
COMPOSE_CELL_MEMBERS = 300000
COMPOSE_PARTITIONS = 3
# (#1880) The #619 adapter input is a manifest over greedy whole-line series
# shards of at most one store file: no series line exceeds
# COMPOSE_SERIES_LINE_BYTES, the manifest has a fixed part and one line per
# shard, and at most COMPOSE_SERIES_SHARDS shards are allowed.
COMPOSE_SERIES_LINE_BYTES = 256
COMPOSE_SERIES_MANIFEST_FIXED_BYTES = 512
COMPOSE_SERIES_MANIFEST_LINE_BYTES = 256
COMPOSE_SERIES_SHARDS = 1024
# Code records, series manifest (the adapter input), replay output, bundle,
# execution receipt, retained manifest and sealed-result record, besides the
# result-input manifests and the series shards.
COMPOSE_FIXED_OUTPUTS = 7
# TP_RETIREMENT_CODE_RECORD_BYTES_MAX (tools/throughput/retirement_samples.h).
CODE_RECORD_BYTES_MAX = 649
SAMPLE_PARTITION_RECORDS = 16777216
SAMPLE_SHARD_RECORDS = 131072
ROUNDS = 2


def prior_closure_entries():
    """The fewest pre-existing sealed-closure files a binding carries.

    Derived from the production validator's ``_all_artifacts`` over a minimal
    record (one requested-work item), less its two outer phases, plus
    ``contract.source`` and ``workflow.execution_plan``, as the composer's
    ``prior`` list is built. Census projection artifacts add to this.
    """
    item = {"path": "x", "bytes": 1, "sha256": "0" * 64}
    record = {
        "support": {"files": [item] * len(binding.SUPPORT_FILE_ROLES), "validator": {"source": item}},
        "requested_work": {"items": [{"artifact": item}]},
        "subjects": {name: {"source_snapshot": item, "binary": item, "build_receipt": item}
                     for name in ("baseline", "candidate")},
        "producer": {"toolchain": {"compiler_binary": item, "resource_directory": item},
                     "build": {"configuration": item, "flags": item}},
        "measurement": {"harness_binary": item, "statistics_implementation": item},
        "execution": {"service": {"recipe": item}, "profile": {"descriptor": item},
                      "host": {"qualification_receipt": item, "aa_admission_receipt": item},
                      "lease": {"receipt": item}},
        "provenance": {name: item for name in ("relation_receipt", "replay_receipt", "replay_bundle",
                                               "census_receipt", "strict_receipt")},
        "workflow": {"phases": {name: item for name in binding.WORKFLOW_PHASES},
                     "records": {name: item for name in ("admission", "oracle", "result_input_plan")}},
    }
    names = [name for name, _artifact in binding._all_artifacts(record)
             if name not in ("workflow.phases.sealed_result", "workflow.phases.independent_replay")]
    return len(names) + 2


def composer_bounds(model, code_rows, prior_entries, bootstrap_members=COMPOSE_BOOTSTRAP_MEMBERS):
    """Mirror tp_retirement_compose_bounds for one A1 campaign scenario.

    The family's cells are the timed rows (wall time and peak memory), the
    runtime rows and the object groups (the batch pair). The aggregate and
    slice members depend on the rows' dimension values, which the capacity
    model does not carry, so they are taken at the #619 cap
    (``bootstrap_members``); that only enlarges the series and replay
    bounds. The series (#1880) is stored as greedy whole-line shards of at
    most one store file (every shard but the last holds more than
    ``FILE_CAP - COMPOSE_SERIES_LINE_BYTES`` bytes) plus their manifest, so
    ``adapter_input_series`` is a total over ``series_shards`` files, not one
    file. Returns every output's bytes, the files (series shards included),
    the total and the reasons the composer would refuse the scenario before
    timing.
    """
    per_unit = ROUNDS * model["pairs_per_round"]
    rows, runtime, objects = model["timed_rows"], model["runtime_rows"], model["object_groups"]
    refusals = []
    manifests = manifest_count = 0
    for records in (rows * per_unit, objects * per_unit):
        partitions = -(-records // SAMPLE_PARTITION_RECORDS)
        manifest_count += partitions
        manifests += partitions * COMPOSE_MANIFEST_FIXED_BYTES + \
            -(-records // SAMPLE_SHARD_RECORDS) * COMPOSE_MANIFEST_SHARD_BYTES
    if manifest_count > COMPOSE_PARTITIONS:
        refusals.append("result-input partitions exceed the #615 bound")
    cells = [rows, rows, runtime, objects, objects]
    if not all(cells):
        refusals.append("a #619 metric has no cell (no runtime-eligible row)")
    if sum(cells) > COMPOSE_CELL_MEMBERS:
        refusals.append("statistical family exceeds the #619 cell-member cap")
    members = bootstrap_members + sum(cells)
    lines = (COMPOSE_DIMENSIONS + 2) * sum(cells) * per_unit
    series = COMPOSE_SERIES_HEADER_BYTES + members * (COMPOSE_MEMBER_LINE_BYTES + COMPOSE_SERIES_END_BYTES) + \
        lines * COMPOSE_RATIO_LINE_BYTES
    series_shards = 1 + (series - 1) // (FILE_CAP - COMPOSE_SERIES_LINE_BYTES + 1)
    if series_shards > COMPOSE_SERIES_SHARDS:
        refusals.append("the #619 series needs more than the composer's series shard cap")
    outputs = {
        "result_input_manifests": manifests,
        "code_records": code_rows * CODE_RECORD_BYTES_MAX,
        "adapter_input_series": series,
        "adapter_input_manifest": COMPOSE_SERIES_MANIFEST_FIXED_BYTES + series_shards * COMPOSE_SERIES_MANIFEST_LINE_BYTES,
        "adapter_output": COMPOSE_REPLAY_FIXED_BYTES + members * COMPOSE_REPLAY_MEMBER_BYTES,
        "result_bundle": COMPOSE_BUNDLE_BYTES,
        "execution_receipt": STORE_RECEIPT_BYTES,
        "retained_manifest": RETAINED_MANIFEST_HEADER_SIZE + ENTRY_CAP * COMPOSE_RETAINED_LINE_BYTES,
        "sealed_result": COMPOSE_SEAL_FIXED_BYTES + (prior_entries + ENTRY_CAP) * COMPOSE_SEAL_ENTRY_BYTES,
    }
    if any(value > FILE_CAP for name, value in outputs.items() if name != "adapter_input_series"):
        refusals.append("a composer output exceeds the 64 MiB per-file cap")
    return {"outputs": outputs, "files": manifest_count + COMPOSE_FIXED_OUTPUTS + series_shards,
            "series_shards": series_shards, "total_bytes": sum(outputs.values()),
            "family_members_upper_bound": members, "refusals": refusals}


def a1_export_ledger(model, limits, code_rows, prior_entries):
    """Map one A1 campaign scenario onto the export and worker limits.

    ``model`` is a scenario of tools/throughput/retirement_capacity.py's
    ``campaign_model`` and ``limits`` its ``source_limits``: shard files per
    stage (transcript, numeric, 64 MiB metrics shards), untimed metrics shards
    and the single untimed batch-record file. This adds the composer's
    outputs (``composer_bounds``: result-input manifests, code records,
    the adapter input's series shards and manifest, adapter output, result
    bundle, execution receipt, retained manifest and sealed record), the
    prior sealed-closure files (``prior_entries``) and the
    worker's three control entries. It checks every file kind against the
    per-file cap and reports the entries and bytes left for census
    projections, retained logs and directories. It is a model, not an
    inventory, and never counts executed work or transferred bytes.
    """
    composer = composer_bounds(model, code_rows, prior_entries)
    largest = {
        "metrics_shard": limits["metrics_shard_bytes"],
        "transcript_shard": limits["transcript_bytes_per_shard"],
        "numeric_shard": limits["sample_records_per_shard"] * max(
            limits["sample_record_bytes_max"], limits["batch_record_bytes_max"]),
        "untimed_record_file": model["untimed_batches"] * limits["untimed_record_bytes_max"],
        **{name: value for name, value in composer["outputs"].items() if name != "adapter_input_series"},
        "adapter_input_shard": min(composer["outputs"]["adapter_input_series"], FILE_CAP),
    }
    owned_files = model["payload_files"] + composer["files"]
    owned_bytes = model["payload_bytes_upper_bound"] + composer["total_bytes"]
    entries_left = ENTRY_CAP - WORKER_CONTROL_ENTRIES - prior_entries - owned_files
    bytes_left = RESULT_FILE_CAP - owned_bytes
    # Every entry costs at most one header and a full-length path.
    header_bound = ENTRY_CAP * (ENTRY_HEADER_BYTES + PATH_CAP)
    per_file_fits = all(value <= FILE_CAP for value in largest.values())
    refusals = list(composer["refusals"])
    if not model["fits"]:
        refusals.append("campaign store model rejects the scenario")
    if entries_left < 0:
        refusals.append("entries exceed the 4,096-entry bundle")
    if bytes_left < 0:
        refusals.append("bytes exceed the 128 GiB store")
    payload_archive = owned_bytes + (owned_files + WORKER_CONTROL_ENTRIES) * (
        ENTRY_HEADER_BYTES + PATH_CAP)
    return {
        "pairs_per_round": model["pairs_per_round"],
        "per_input_metrics_bound_bytes": model["per_input_metrics_bound_bytes"],
        "runtime_rows": model["runtime_rows"],
        "metrics_shards_both_stages": 2 * model["metrics_shards_per_stage_upper_bound"],
        "untimed_metrics_shards": model["untimed_metrics_shards_upper_bound"],
        "untimed_record_files": model["untimed_record_files"],
        "composer": composer,
        "prior_closure_entries": prior_entries,
        "largest_file_bytes": largest,
        "per_file_cap": FILE_CAP,
        "per_file_fits": per_file_fits,
        "owned_files": owned_files,
        "owned_bytes_upper_bound": owned_bytes,
        "worker_control_entries": WORKER_CONTROL_ENTRIES,
        "entries_left_for_projections_logs_and_directories": entries_left,
        "bytes_left_for_prior_closure_logs_and_controls": bytes_left,
        "archive_bytes_at_full_store": RESULT_FILE_CAP + header_bound,
        "archive_cap": ARCHIVE_CAP,
        "export_binds_before_store": RESULT_FILE_CAP + header_bound > ARCHIVE_CAP,
        "payload_only_copies": copy_ledger(payload_archive, owned_bytes),
        "refusals": refusals,
        "fits": not refusals and per_file_fits,
        "model_only": True,
    }


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
        capacity_ledger(receipt)
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


def composer_binding_path(text):
    """The binding record's place in the result (#1023, E composer).

    The composer, not the operator, owns this location: any path other than
    COMPOSER_BINDING_PATH is refused (were it unset, the operator-supplied
    canonical path would be used).
    """
    relative = relative_binding_path(text)
    if COMPOSER_BINDING_PATH is not None and text != COMPOSER_BINDING_PATH:
        fail("binding path differs from the reviewed composer's fixed location")
    return relative


def lane_f_import(lane_f, destination):
    """Copy lane F's regular files (its final binding and the evidence it
    names) into the clean replay destination, never replacing a file of the
    unpacked service result. Returns the final binding's path there."""
    source = os.open(lane_f, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    target = os.open(destination, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        owner = os.fstat(source)
        if owner.st_uid != os.geteuid() or owner.st_mode & 0o022:
            fail("lane F directory must be owned by the caller and not writable by others")
        names = sorted(os.listdir(source))
        if FINAL_BINDING_NAME not in names:
            fail("lane F directory lacks the final binding")
        # Every name is checked before anything is copied.
        present = set(os.listdir(target))
        for name in names:
            info = os.stat(name, dir_fd=source, follow_symlinks=False)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                fail("lane F directory may hold only single-link regular files")
            if name in present:
                fail("lane F file collides with the service result")
        for name in names:
            data_fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=source)
            try:
                with os.fdopen(data_fd, "rb", closefd=False) as stream:
                    data = stream.read()
            finally:
                os.close(data_fd)
            try:
                out = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o400,
                              dir_fd=target)
            except FileExistsError:
                fail("lane F file collides with the service result")
            try:
                offset = 0
                while offset < len(data):
                    offset += os.write(out, data[offset:])
                os.fsync(out)
            finally:
                os.close(out)
        os.fsync(target)
    finally:
        os.close(target)
        os.close(source)
    return Path(destination) / FINAL_BINDING_NAME


def final_binding_check(destination, final_path):
    """Lane F's final binding is the composed record with only its two late
    phases filled: sealed_result the composed sealed result (by its bytes)
    and independent_replay a non-pending descriptor at the composed path.
    Every other byte of meaning is the composer's."""
    root = Path(destination)
    composed = json.loads((root / COMPOSER_BINDING_PATH).read_bytes())
    final = json.loads(Path(final_path).read_bytes())
    phases = composed["workflow"]["phases"]
    for name in FINAL_PHASES:
        if {key: phases[name].get(key) for key in PENDING_DESCRIPTOR} != PENDING_DESCRIPTOR:
            fail(f"composed binding's {name} phase is not the composer's pending descriptor")
    sealed_path = phases["sealed_result"]["path"]
    sealed = (root / relative_binding_path(sealed_path)).read_bytes()
    replayed = final["workflow"]["phases"]["independent_replay"]
    if replayed.get("path") != phases["independent_replay"]["path"] or \
            {key: replayed.get(key) for key in PENDING_DESCRIPTOR} == PENDING_DESCRIPTOR:
        fail("final binding's independent replay is pending or elsewhere")
    expected = json.loads(json.dumps(composed))
    expected["workflow"]["phases"]["sealed_result"] = {
        "path": sealed_path, "bytes": len(sealed), "sha256": hashlib.sha256(sealed).hexdigest()}
    expected["workflow"]["phases"]["independent_replay"] = replayed
    if final != expected:
        fail("final binding differs from the composed record beyond lane F's phases")
    return final


def authenticated_attempt_join(destination, record_path, job, attempt, trusted_sha256):
    """Join the export's numeric job/attempt to the trusted execution receipt.

    Reads the sealed result, result bundle and execution receipt with the
    production validator's readers; the receipt must hash to the digest from
    the authenticated control channel, never to a value taken from the
    bundle. A self-consistent forged receipt therefore fails here even when
    every in-bundle descriptor matches its own bytes.
    """
    with Path(record_path).open(encoding="utf-8") as stream:
        record_value = json.load(stream)
    bundle_descriptor = record_value["workflow"]["phases"]["sealed_result"]
    sealed = binding._read_json_evidence(destination, bundle_descriptor, "sealed_result")
    result_bundle = binding._read_json_evidence(destination, sealed["result_bundle"], "result_bundle")
    execution = binding._read_trusted_json_evidence(
        destination, result_bundle["execution_receipt"], "execution_receipt",
        trusted_sha256, binding.EXECUTION_RECEIPT_BYTE_CAP)
    if execution["job_id"] != SERVICE_JOB_LABEL.format(job) or execution["attempt"] != attempt:
        fail("service execution receipt does not identify the exported job and attempt")
    return execution


def replay(args):
    receipt_sha256 = digest_argument(args.export_receipt_sha256, "export receipt")
    trusted_sha256 = digest_argument(args.trusted_execution_receipt_sha256, "service execution receipt")
    full_sha256 = digest_argument(args.full_result_sha256, "full result")
    if args.job <= 0 or args.attempt <= 0:
        fail("job and attempt must be positive")
    record = composer_binding_path(args.binding)
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
                      "receipt_derived_capacity": capacity_ledger(receipt),
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
    # The composed record is pending; lane F's final binding is validated.
    record_path = lane_f_import(args.lane_f, args.destination)
    final_binding_check(args.destination, record_path)
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
    authenticated_attempt_join(args.destination, record_path, args.job, args.attempt, trusted_sha256)
    print(json.dumps({"replay": "verified-without-admission", "job": args.job,
                      "attempt": args.attempt, "proof": result["proof"],
                      "required_rows": result["required_rows"],
                      "trusted_execution_receipt_sha256": trusted_sha256}, sort_keys=True))


def a1_capacity_report(root, require_committed=True, performance_rows=None):
    """Export-side ledger for every scenario of the A1 capacity model."""
    sys.path.insert(0, str(Path(root).resolve() / "tools" / "throughput"))
    import retirement_capacity as capacity
    report = capacity.build_report(Path(root), require_committed=require_committed,
                                   performance_rows=performance_rows)
    stages = report["population"]["stage_rows"]
    # Code-observed rows on every target are at most every declared object
    # identity plus the counted stage singletons, timed and untimed.
    code_rows = stages["declared_object_identities"] + sum(stages["timed_singletons_by_stage"].values()) + \
        sum(stages["untimed_singletons_by_stage"].values())
    prior_entries = prior_closure_entries()
    return {
        "schema": "buster-native-retirement-export-capacity-v2",
        "source": report["source"],
        "code_rows_upper_bound": code_rows,
        "prior_closure_entries_minimum": prior_entries,
        "scenarios": {name: a1_export_ledger(model, report["policy_limits"], code_rows, prior_entries)
                      for name, model in report["scenarios"].items()},
        "six_copy_ceiling": copy_ledger(ARCHIVE_CAP, RESULT_FILE_CAP),
        "capacity_is_arithmetic_only": True,
    }


def a1_capacity_main(arguments):
    parser = argparse.ArgumentParser(prog="retirement_export_replay.py a1-capacity",
                                     description=a1_export_ledger.__doc__)
    parser.add_argument("--repository-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--performance-rows", type=Path, default=None,
                        help="canonical #508 performance rows: count stage singletons from the census")
    args = parser.parse_args(arguments)
    try:
        report = a1_capacity_report(args.repository_root, performance_rows=args.performance_rows)
    except (OSError, ValueError, OverflowError, KeyError) as error:
        print(f"retirement export capacity failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


def main():
    if sys.argv[1:2] == ["a1-capacity"]:
        return a1_capacity_main(sys.argv[2:])
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
    parser.add_argument("--lane-f", type=Path,
                        help="lane F's directory: its final binding and the evidence it names (required for replay)")
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
        if not args.publish_only and args.lane_f is None:
            fail("replay requires lane F's directory with its final binding")
        replay(args)
    except (ValueError, OSError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired, KeyError, TypeError) as error:
        print(f"retirement export replay failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
