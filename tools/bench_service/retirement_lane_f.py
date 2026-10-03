#!/usr/bin/env python3
"""Lane F's production writer: the final binding and the independent replay.

Ownership: lane F of the native-retirement performance recipe (#881, #1024).
Its input is an unpacked, sealed and composed retirement export: the service
result the worker-unit producer finalized, reconstructed by `bench_service
unpack-export` (see EXPORT.md). It writes lane F's directory, which
`retirement_export_replay.py --lane-f` imports into the clean replay (laying
out the flat evidence with ``evidence_layout`` and ``lay_out_evidence`` from
here), and it runs the production binding validator over the result and that
directory.

The composed record (``replay.COMPOSER_BINDING_PATH``) names its two late
phases with the composer's pending descriptor. Lane F fills them from bytes,
never from a value a candidate produced as its own expectation:

- ``bundle`` streams the independent-replay archive (tar: the
  ``bundle-manifest.json`` then every sealed-closure file and the sealed
  result) that the operator publishes. Its bytes are a function of the
  unpacked result and the publication ID alone (``write_bundle``).
- ``bind`` takes the freshly downloaded copy of that publication and the
  publisher's digest, requires both to equal the archive re-derived from the
  unpacked result, extracts the downloaded copy with the validator's own
  reader, rebuilds the reviewed #619 adapter from the pinned checkout at the
  harness commit and tree the operator supplies (which must equal the
  record's), reruns it over the downloaded adapter input and requires the
  sealed adapter result byte for byte. Only then does it write the
  publication receipt, the replay bundle, the
  ``buster-native-retirement-independent-replay-v1`` phase record and
  ``retirement-final-binding.json`` (``replay.FINAL_BINDING_NAME``) into a
  ``.pending`` directory, check the final binding with
  ``replay.final_binding_check`` and rename it, never replacing anything, to
  the lane F directory (``rename_noreplace``).
- ``replay`` pins the production validator's closure before anything runs:
  the entry file by SHA-256 (by default the profile's ``binding-validator``
  pin, ``profile_validator_pin``) and every local module it imports plus the
  data files it reads beside itself by one closure digest
  (``validator_closure``). It installs those verified bytes into a private
  directory (``install_validator``) and runs them isolated (``python -I -B``,
  an empty private bytecode prefix, a minimal environment). It copies the
  result into a new clean directory in fixed chunks, each file at its own
  per-file cap (``replay.result_file_cap``: flat evidence may reach 512 MiB,
  #1880), imports lane F's directory there
  (``replay.lane_f_import``), checks the final binding, lays out the flat
  ``retirement-evidence-*`` files at the paths the binding names
  (``evidence_layout``) and runs the validator. The verdict record names the
  validator closure, the layout, the final binding, the phase record and the
  validator's output or refusal; it succeeds only for a complete independent
  replay.

The phase record's keys are fixed by the validator (``_workflow_phase``), so
the validator identity and verdict live in the separate verdict record.
Nothing here publishes durably (#510) or admits the recipe.

Map: ``evidence_name`` and ``evidence_layout`` (flat evidence names),
``composed_state`` (composed record, sealed closure), ``write_bundle`` and
``bundle_digest`` (the deterministic archive), ``adapter_replay``,
``rename_noreplace``, ``bind``, ``copy_result``, ``profile_validator_pin``,
``validator_closure``, ``install_validator``, ``validator_environment``,
``replay_lane_f``, ``main``.
"""

import argparse
import ast
import ctypes
import errno
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import retirement_export_replay as replay  # noqa: E402

binding = replay.binding

# Lane F's own files beside the final binding and the phase record (whose
# path the composer fixes: BQ_RETIREMENT_WORKER_REPLAY_PATH).
BUNDLE_NAME = "retirement-downloaded-independent.bundle.tar"
PUBLICATION_NAME = "retirement-performance-publication.json"
REPLAY_BUNDLE_NAME = "retirement-independent-replay.bundle"
BUNDLE_MANIFEST_NAME = "bundle-manifest.json"
BUNDLE_MANIFEST_SCHEMA = "buster-native-retirement-independent-bundle-manifest-v1"
PUBLISHER = "native-retirement-evidence-v1"
ADAPTER_COMMAND = "bench_throughput retirement-replay --input SERIES_FILE --output RESULT_JSON"
VERDICT_SCHEMA = "buster-native-retirement-lane-f-replay-verdict-v1"
# Each lane F JSON record and each composed JSON document is one bundle file.
JSON_CAP = replay.FILE_CAP
# The validator's own archive bound (_extract_downloaded_bundle): the closure
# bytes plus 64 MiB of tar headers and padding.
BUNDLE_OVERHEAD_CAP = 64 * 1024 * 1024
# The independent binding replay's 24-hour budget (EXPORT.md); the adapter
# replay is one part of it.
REPLAY_TIMEOUT = 86400
PROOF = "independent-evidence-and-receipts-checked"
PROOF_FLAGS = ("rows_recomputed", "support_checked", "execution_checked", "bundle_checked", "git_checked")
# The integration-owned profile pins the validator's path and SHA-256.
PROFILE_PATH = "tools/bench_service/profiles/native-retirement-performance-v1.blocked"
# The producer publishes every evidence file the binding context names as one
# flat result-root entry (bq_retirement_worker_evidence_map, #1998): this
# prefix, then the named path's segments joined by "--", in [A-Za-z0-9._-]
# and at most BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP bytes (evidence_name).
EVIDENCE_PREFIX = replay.EVIDENCE_PREFIX
EVIDENCE_SEPARATOR = "--"
EVIDENCE_PATH_CAP = 128
EVIDENCE_SEGMENT = re.compile(r"[A-Za-z0-9._-]+\Z")
# The validator's reviewed closure: the entry file, every repository-local
# module it imports (transitively, including the #508 validator it runs as a
# subprocess) and the data files those modules read beside themselves
# (_approved_support_counts, native_retirement_contract's dependency
# authority). validator_closure derives the module list from the verified
# bytes and refuses one that differs from VALIDATOR_MODULES.
VALIDATOR_MODULES = (
    "tools/native_retirement_contract.py",
    "tools/native_retirement_dependency_binding.py",
    "tools/native_retirement_materializer.py",
    "tools/native_retirement_performance_binding.py",
    "tools/native_retirement_performance_schema.py",
    "tools/native_retirement_reference.py",
    "tools/native_retirement_result_input.py",
)
VALIDATOR_DATA = (
    "docs/native-retirement-dependencies-v1.json",
    "docs/native-retirement-repository-sources-v1.json",
    "docs/native-retirement-support-v1.tsv",
    "tools/native_retirement_dependency_binding.generated.h",
)
VALIDATOR_FILE_CAP = 16 * 1024 * 1024
# Linux renameat2 and Darwin renamex_np flags for a rename that never
# replaces its target.
AT_FDCWD = -100
RENAME_NOREPLACE = 1
RENAME_EXCL = 4

fail = replay.fail


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def open_regular(root, text):
    """Open ROOT/TEXT read-only without following any link; returns the fd."""
    relative = replay.relative_binding_path(text)
    descriptor = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        for part in relative.parts[:-1]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = child
        opened = os.open(relative.parts[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=descriptor)
    finally:
        os.close(descriptor)
    if not stat.S_ISREG(os.fstat(opened).st_mode):
        os.close(opened)
        fail(f"{text} is not a regular file")
    return opened


def read_bounded(root, text, cap=JSON_CAP):
    descriptor = open_regular(root, text)
    try:
        if os.fstat(descriptor).st_size > cap:
            fail(f"{text} exceeds its {cap}-byte bound")
        with os.fdopen(descriptor, "rb", closefd=False) as stream:
            data = stream.read(cap + 1)
    finally:
        os.close(descriptor)
    if len(data) > cap:
        fail(f"{text} grew beyond its bound while it was read")
    return data


def parse_json(data):
    """Strict JSON: duplicate keys are refused, as the validator refuses them."""
    return json.loads(data, object_pairs_hook=binding._json_object)


def descriptor_of(path, data):
    return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def phase_is_pending(phase):
    return {key: phase.get(key) for key in replay.PENDING_DESCRIPTOR} == replay.PENDING_DESCRIPTOR


def evidence_name(path):
    """The flat result-root name of the evidence the binding names at PATH:
    EVIDENCE_PREFIX, then PATH's segments joined by EVIDENCE_SEPARATOR. None
    when PATH is a single segment (read where it is) or has no unambiguous
    flat name: an empty, "." or ".." segment, a segment with a byte outside
    [A-Za-z0-9._-], containing "--" or beginning or ending with "-" (so every
    "--" in a name is a separator and the mapping is injective), or a name
    longer than EVIDENCE_PATH_CAP. The producer's
    bq_retirement_worker_evidence_map is the same rule; its cross-language
    table (bq_prep_worker_unit_evidence_names) holds them to it."""
    segments = path.split("/")
    valid = len(segments) > 1 and all(
        segment not in (".", "..") and EVIDENCE_SEGMENT.fullmatch(segment) is not None and
        EVIDENCE_SEPARATOR not in segment and not segment.startswith("-") and not segment.endswith("-")
        for segment in segments)
    name = EVIDENCE_PREFIX + EVIDENCE_SEPARATOR.join(segments)
    return name if valid and len(name) <= EVIDENCE_PATH_CAP else None


def record_descriptors(record):
    """Every {path, bytes, sha256} descriptor in RECORD, found with an
    explicit worklist; a path named twice with other bytes is refused."""
    found = {}
    pending = [record]
    while pending:
        value = pending.pop()
        if type(value) is dict:
            if type(value.get("path")) is str and type(value.get("bytes")) is int and \
                    type(value.get("sha256")) is str:
                descriptor = (value["bytes"], value["sha256"])
                if found.setdefault(value["path"], descriptor) != descriptor:
                    fail(f"the binding names {value['path']} twice with different bytes")
            pending.extend(value.values())
        elif type(value) is list:
            pending.extend(value)
    return found


def evidence_layout(result, record):
    """Where each binding-named evidence file is in the flat RESULT.

    A descriptor whose path is present in the result is read there; one
    that is absent and has a flat ``evidence_name`` present is read from it
    and laid out at its path for the validator; one with neither is left for
    the validator to refuse. Returns the sorted layout entries and their
    digest. A path present both ways, a flat name claimed twice (evidence_name
    is injective, so only by its path and by a descriptor naming the flat
    entry itself, which a producer never writes but a binding can), or a flat
    evidence entry that no descriptor names is refused."""
    root = Path(result)
    names = set(os.listdir(root))
    claimed = {}
    entries = []
    for path, (size, sha256) in sorted(record_descriptors(record).items()):
        replay.relative_binding_path(path)
        flat = evidence_name(path)
        direct = os.path.lexists(root.joinpath(*PurePosixPath(path).parts))
        if flat is not None and flat in names:
            if direct:
                fail(f"evidence {path} is in the result both at its path and as {flat}")
            if flat in claimed:
                fail(f"evidence {flat} is named by both {claimed[flat]} and {path}")
            claimed[flat] = path
            entries.append({"evidence": flat, "path": path, "bytes": size, "sha256": sha256})
        elif direct and path.startswith(EVIDENCE_PREFIX):
            if path in claimed:
                fail(f"evidence {path} is named by both {claimed[path]} and {path}")
            claimed[path] = path
    unmapped = sorted(name for name in names if name.startswith(EVIDENCE_PREFIX) and name not in claimed)
    if unmapped:
        fail(f"evidence entry {unmapped[0]} is not named by the binding")
    return entries, hashlib.sha256(canonical(entries)).hexdigest()


def composed_state(result):
    """The composed record, its sealed result and the sealed closure, each
    descriptor checked against the unpacked bytes (read through the flat
    evidence layout)."""
    root = Path(result)
    composed = parse_json(read_bounded(root, replay.COMPOSER_BINDING_PATH))
    phases = composed["workflow"]["phases"]
    for name in replay.FINAL_PHASES:
        if name not in phases or not phase_is_pending(phases[name]):
            fail(f"composed binding's {name} phase is not the composer's pending descriptor")
    sealed_path = phases["sealed_result"]["path"]
    replay_path = phases["independent_replay"]["path"]
    replay.relative_binding_path(sealed_path)
    if "/" in replay.relative_binding_path(replay_path).as_posix():
        fail("the independent-replay phase must name a file of lane F's flat directory")
    layout, _layout_sha256 = evidence_layout(root, composed)
    sources = {entry["path"]: entry["evidence"] for entry in layout}
    sealed_bytes = read_bounded(root, sealed_path)
    sealed_descriptor = descriptor_of(sealed_path, sealed_bytes)
    sealed = parse_json(sealed_bytes)
    seal = sealed["seal"]
    files = seal["files"]
    if type(files) is not list or not 0 < len(files) < replay.ENTRY_CAP:
        fail("sealed result's closure is empty or exceeds the bundle's entry cap")
    closure = []
    by_name = {}
    for index, item in enumerate(files):
        entry = binding._keys(item, ("name", "path", "bytes", "sha256"), f"seal.files[{index}]")
        artifact = binding._artifact({key: entry[key] for key in ("path", "bytes", "sha256")},
                                     f"seal.files[{index}]")
        stored = sources.get(artifact["path"], artifact["path"])
        if artifact["bytes"] > replay.result_file_cap(stored):
            fail(f"sealed closure file {stored} exceeds its per-file cap")
        binding._check_evidence(root, dict(artifact, path=stored), f"seal.files[{index}]")
        if entry["name"] in by_name:
            fail("sealed closure names one identity twice")
        by_name[entry["name"]] = artifact
        closure.append(artifact)
    if seal["root_sha256"] != binding._canonical_files_digest(files):
        fail("sealed result's seal root differs from its closure")
    for name in ("workflow.result_bundle", "workflow.adapter_input", "workflow.adapter_result"):
        if name not in by_name:
            fail(f"sealed closure lacks {name}")
    result_bundle_descriptor = by_name["workflow.result_bundle"]
    if sealed["result_bundle"] != result_bundle_descriptor:
        fail("sealed result's result bundle is not its sealed closure's")
    result_bundle = parse_json(read_bounded(root, sources.get(result_bundle_descriptor["path"],
                                                              result_bundle_descriptor["path"])))
    untimed = result_bundle["untimed_batches"]
    if (untimed is None) != ("workflow.untimed_batches" not in by_name) or \
            (untimed is not None and {key: untimed.get(key) for key in ("path", "bytes", "sha256")}
             != by_name["workflow.untimed_batches"]):
        fail("result bundle's untimed batches are not its sealed closure's")
    if result_bundle["adapter_input"] != by_name["workflow.adapter_input"]:
        fail("result bundle's adapter input is not its sealed closure's")
    closure.append(sealed_descriptor)
    if len({item["path"] for item in closure}) != len(closure):
        fail("sealed closure names one path twice")
    return {"composed": composed, "sealed": sealed_descriptor, "replay_path": replay_path,
            "closure": sorted(closure, key=lambda item: item["path"].encode()), "sources": sources,
            "result_bundle": result_bundle_descriptor, "result_bundle_value": result_bundle,
            "adapter_input": by_name["workflow.adapter_input"],
            "adapter_result": by_name["workflow.adapter_result"],
            "untimed": by_name.get("workflow.untimed_batches")}


class HashSink:
    """A write-only stream that keeps the SHA-256 and length of what it is
    given and passes the bytes on to ``stream`` when there is one."""

    def __init__(self, stream=None):
        self.stream = stream
        self.digest = hashlib.sha256()
        self.length = 0

    def write(self, data):
        if self.stream is not None:
            self.stream.write(data)
        self.digest.update(data)
        self.length += len(data)
        return len(data)


class HashingReader:
    """Reads one file for the archive, hashing exactly what the archive holds."""

    def __init__(self, stream):
        self.stream = stream
        self.digest = hashlib.sha256()

    def read(self, size=-1):
        data = self.stream.read(size)
        self.digest.update(data)
        return data


def _tar_info(name, size):
    # Fixed metadata: the archive is a function of the closure bytes alone.
    info = tarfile.TarInfo(name)
    info.size = size
    info.mode = 0o444
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    return info


def write_bundle(result, state, publication_id, sink):
    """Stream the independent-replay archive for the sealed closure of STATE
    into ``sink``. Its first member is the bundle manifest the validator
    requires; members carry the paths the seal names and are read through
    the evidence layout; each is re-hashed while archived."""
    files = [{key: item[key] for key in ("path", "bytes", "sha256")} for item in state["closure"]]
    manifest = canonical({
        "schema": BUNDLE_MANIFEST_SCHEMA, "version": 1, "publication_id": publication_id, "files": files,
        "root_sha256": binding._canonical_files_digest([{"name": item["path"], **item} for item in files])})
    with tarfile.open(fileobj=sink, mode="w|", format=tarfile.PAX_FORMAT) as archive:
        archive.addfile(_tar_info(BUNDLE_MANIFEST_NAME, len(manifest)), io.BytesIO(manifest))
        for item in files:
            descriptor = open_regular(result, state["sources"].get(item["path"], item["path"]))
            try:
                if os.fstat(descriptor).st_size != item["bytes"]:
                    fail(f"{item['path']} changed size before it was archived")
                with os.fdopen(descriptor, "rb", closefd=False) as stream:
                    reader = HashingReader(stream)
                    archive.addfile(_tar_info(item["path"], item["bytes"]), reader)
            finally:
                os.close(descriptor)
            if reader.digest.hexdigest() != item["sha256"]:
                fail(f"{item['path']} changed while it was archived")


def bundle_cap(state):
    return sum(item["bytes"] for item in state["closure"]) + BUNDLE_OVERHEAD_CAP


def bundle_digest(result, state, publication_id):
    sink = HashSink()
    write_bundle(result, state, publication_id, sink)
    if sink.length > bundle_cap(state):
        fail("the independent-replay archive exceeds the validator's closure bound")
    return sink.length, sink.digest.hexdigest()


def create_exclusive(path, data=None, mode=0o400):
    """Create PATH exclusively with DATA (or return its fd when DATA is None)."""
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
    if data is None:
        return descriptor
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(descriptor, data[offset:])
        os.fchmod(descriptor, mode)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    return None


def copy_bounded(source, target, cap):
    """Copy SOURCE to a new TARGET in fixed chunks, returning (bytes,
    SHA-256). A source already over CAP is refused before TARGET exists."""
    descriptor = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    digest = hashlib.sha256()
    copied = 0
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode):
            fail(f"{source} is not a regular file")
        if info.st_size > cap:
            fail(f"{source} exceeds its {cap}-byte bound")
        output = create_exclusive(target)
        try:
            while True:
                block = os.read(descriptor, replay.CHUNK)
                if not block:
                    break
                copied += len(block)
                if copied > cap:
                    fail(f"{source} exceeds its {cap}-byte bound")
                digest.update(block)
                offset = 0
                while offset < len(block):
                    offset += os.write(output, block[offset:])
            os.fchmod(output, 0o400)
            os.fsync(output)
        finally:
            os.close(output)
    finally:
        os.close(descriptor)
    return copied, digest.hexdigest()


def write_bundle_file(result, state, publication_id, output):
    """Write the archive to OUTPUT through ``OUTPUT.partial``: the output is
    published by a hard link that never replaces a file, and a failed or
    oversized write leaves no partial file."""
    output = Path(output)
    partial = output.with_name(output.name + ".partial")
    descriptor = create_exclusive(partial)
    try:
        try:
            with os.fdopen(descriptor, "wb", closefd=False) as stream:
                sink = HashSink(stream)
                write_bundle(result, state, publication_id, sink)
            if sink.length > bundle_cap(state):
                fail("the independent-replay archive exceeds the validator's closure bound")
            os.fchmod(descriptor, 0o400)
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        os.link(partial, output, follow_symlinks=False)
    finally:
        os.unlink(partial)
    return {"bundle": str(output), "bytes": sink.length, "sha256": sink.digest.hexdigest()}


def adapter_replay(evidence, downloaded, publication_id, state, repository_root):
    """Rerun the reviewed #619 adapter over the downloaded archive's input.

    The archive is extracted with the validator's reader against the sealed
    closure; the adapter is rebuilt from the pinned checkout at the record's
    harness commit and tree (``bind`` has already required them to equal the
    operator's) and runs with a minimal environment in a private directory.
    Returns the adapter identity for the replay bundle: (source digest,
    toolchain digest, build command).
    """
    measurement = state["composed"]["measurement"]
    with tempfile.TemporaryDirectory(prefix="retirement-lane-f-") as scratch:
        extracted = binding._extract_downloaded_bundle(
            evidence, downloaded, publication_id, state["closure"], Path(scratch) / "bundle")
        adapter = Path(scratch) / "adapter"
        adapter.mkdir(mode=0o700)
        executable, _binary, source_digest, toolchain_digest, build_command = \
            binding._compile_trusted_retirement_adapter(adapter, repository_root,
                                                        measurement["harness_source_commit"],
                                                        measurement["harness_source_tree"])
        work = Path(scratch) / "work"
        work.mkdir(mode=0o700)
        output = work / "statistics-replay.json"
        adapter_input = extracted.joinpath(*PurePosixPath(state["adapter_input"]["path"]).parts)
        process = subprocess.run([str(executable), "retirement-replay", "--input", str(adapter_input),
                                  "--output", str(output)], check=False, capture_output=True, cwd=work,
                                 env={"PATH": os.environ.get("PATH", ""), "LANG": "C"}, timeout=REPLAY_TIMEOUT)
        if process.returncode != 0 or not output.is_file():
            fail("lane F's #619 adapter replay failed")
        sealed_output = extracted.joinpath(*PurePosixPath(state["adapter_result"]["path"]).parts)
        if output.read_bytes() != sealed_output.read_bytes():
            fail("lane F's #619 adapter replay differs from the sealed adapter result")
    return source_digest, toolchain_digest, build_command


def rename_noreplace(source, target):
    """Rename SOURCE to TARGET atomically, refusing when TARGET exists (even
    an empty directory, which os.rename would silently replace)."""
    libc = ctypes.CDLL(None, use_errno=True)
    source_bytes, target_bytes = os.fsencode(source), os.fsencode(target)
    if sys.platform.startswith("linux") and hasattr(libc, "renameat2"):
        libc.renameat2.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        status = libc.renameat2(AT_FDCWD, source_bytes, AT_FDCWD, target_bytes, RENAME_NOREPLACE)
    elif sys.platform == "darwin" and hasattr(libc, "renamex_np"):
        libc.renamex_np.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        status = libc.renamex_np(source_bytes, target_bytes, RENAME_EXCL)
    else:
        fail("no atomic no-replace rename is available here; the pending directory is kept")
    if status != 0:
        code = ctypes.get_errno()
        if code in (errno.EEXIST, errno.ENOTEMPTY):
            fail(f"{target} appeared during the bind; it is not replaced and the pending directory is kept")
        raise OSError(code, os.strerror(code), str(target))


def fsync_directory(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def bind(result, lane_f, downloaded, published_sha256, publication_id, release, run_id, repository_root,
         harness_commit, harness_tree):
    """Write lane F's directory for the unpacked RESULT (see the module doc).

    HARNESS_COMMIT and HARNESS_TREE come from a trusted source, never from
    the result; the record's measurement must name exactly them before the
    adapter is compiled."""
    replay.digest_argument(published_sha256, "published bundle")
    for value, name in ((publication_id, "publication ID"), (release, "release"), (run_id, "run ID")):
        binding._token(value, name)
    binding._commit(harness_commit, "harness commit")
    binding._commit(harness_tree, "harness tree")
    lane_f = Path(lane_f)
    pending = lane_f.with_name(lane_f.name + ".pending")
    if os.path.lexists(lane_f):
        fail("lane F directory already exists")
    if os.path.lexists(pending):
        fail(f"a previous pending attempt exists at {pending}; inspect or remove it")
    state = composed_state(result)
    measurement = state["composed"]["measurement"]
    if (measurement["harness_source_commit"], measurement["harness_source_tree"]) != (harness_commit, harness_tree):
        fail("the record's harness commit and tree differ from the trusted harness identity")
    names = (BUNDLE_NAME, PUBLICATION_NAME, REPLAY_BUNDLE_NAME, state["replay_path"], replay.FINAL_BINDING_NAME)
    present = set(os.listdir(result))
    if len(set(names)) != len(names) or present.intersection(names):
        fail("a lane F file collides with the service result or another lane F file")
    derived_bytes, derived_sha256 = bundle_digest(result, state, publication_id)
    # A failed attempt keeps its pending directory as evidence; it is never
    # promoted or replaced.
    os.mkdir(pending, 0o700)
    downloaded_bytes, downloaded_sha256 = copy_bounded(downloaded, pending / BUNDLE_NAME, derived_bytes)
    if downloaded_sha256 != published_sha256:
        fail("downloaded bundle differs from the publisher's digest")
    if (downloaded_bytes, downloaded_sha256) != (derived_bytes, derived_sha256):
        fail("downloaded bundle differs from the archive derived from the unpacked export")
    archive = {"path": BUNDLE_NAME, "bytes": downloaded_bytes, "sha256": downloaded_sha256}
    source_digest, toolchain_digest, build_command = adapter_replay(
        pending, archive, publication_id, state, repository_root)
    composed = state["composed"]
    result_bundle = state["result_bundle_value"]
    bundle_sha256 = state["result_bundle"]["sha256"]
    publication = canonical({
        "schema": binding.PUBLICATION_SCHEMA, "version": 1, "publisher": PUBLISHER, "release": release,
        "run_id": run_id, "service_id": composed["execution"]["service"]["id"], "publication_id": publication_id,
        "sealed_result_sha256": bundle_sha256, "published_bundle_sha256": published_sha256,
        "downloaded_bundle_sha256": downloaded_sha256, "replay_result": "independently-replayed"})
    create_exclusive(pending / PUBLICATION_NAME, publication)
    publication_descriptor = descriptor_of(PUBLICATION_NAME, publication)
    # The measurement, family and code-byte fields restate the sealed result
    # bundle; the validator recomputes every one of them from the evidence.
    replay_bundle = canonical({
        "schema": binding.REPLAY_BUNDLE_SCHEMA, "version": 1, "sealed_result_sha256": bundle_sha256,
        "raw_measurements_sha256": result_bundle["raw_measurements_sha256"],
        "family_sha256": result_bundle["family_sha256"],
        "member_invocations_sha256": result_bundle["member_invocations_sha256"],
        "member_count": result_bundle["member_count"], "adapter_command": ADAPTER_COMMAND,
        "adapter_build_command": build_command, "adapter_toolchain_sha256": toolchain_digest,
        "adapter_source_sha256": source_digest,
        "code_bytes_summary_sha256": binding._canonical_json_digest(result_bundle["code_bytes_summary"]),
        "untimed_batches_sha256": state["untimed"]["sha256"] if state["untimed"] is not None else None,
        "publication_id": publication_id, "published_bundle_sha256": published_sha256,
        "downloaded_bundle_sha256": downloaded_sha256, "downloaded_bundle": archive,
        "adapter_result": state["adapter_result"], "publication_receipt": publication_descriptor})
    create_exclusive(pending / REPLAY_BUNDLE_NAME, replay_bundle)
    independent = canonical({
        "schema": binding.PHASE_SCHEMA["independent_replay"], "version": 1, "status": "independently-replayed",
        "sealed_result_sha256": state["sealed"]["sha256"],
        "replay_bundle": descriptor_of(REPLAY_BUNDLE_NAME, replay_bundle),
        "publication_receipt": publication_descriptor})
    create_exclusive(pending / state["replay_path"], independent)
    final = json.loads(json.dumps(composed))
    final["workflow"]["phases"]["sealed_result"] = state["sealed"]
    final["workflow"]["phases"]["independent_replay"] = descriptor_of(state["replay_path"], independent)
    create_exclusive(pending / replay.FINAL_BINDING_NAME, canonical(final))
    replay.final_binding_check(result, pending / replay.FINAL_BINDING_NAME)
    fsync_directory(pending)
    if os.path.lexists(lane_f):
        fail("lane F directory appeared during the bind; the pending directory is kept")
    rename_noreplace(pending, lane_f)
    fsync_directory(lane_f.parent)
    return {"lane_f": str(lane_f), "final_binding": descriptor_of(replay.FINAL_BINDING_NAME, canonical(final)),
            "independent_replay": final["workflow"]["phases"]["independent_replay"],
            "downloaded_bundle": archive}


def copy_result(source, destination):
    """Copy an unpacked result's regular files and directories into a new
    private DESTINATION with an explicit worklist; links and other types are
    refused."""
    os.mkdir(destination, 0o700)
    pending = [PurePosixPath()]
    entries = 0
    while pending:
        relative = pending.pop()
        directory = Path(source).joinpath(*relative.parts)
        for name in sorted(os.listdir(directory)):
            entries += 1
            if entries > replay.ENTRY_CAP:
                fail("unpacked result exceeds the bundle's entry cap")
            info = os.lstat(directory / name)
            child = relative / name
            target = Path(destination).joinpath(*child.parts)
            if stat.S_ISDIR(info.st_mode):
                os.mkdir(target, 0o700)
                pending.append(child)
            elif stat.S_ISREG(info.st_mode):
                copy_bounded(directory / name, target, replay.result_file_cap(child.as_posix()))
            else:
                fail("unpacked result holds a link or a special file")


def lay_out_evidence(evidence, entries):
    """Move each flat evidence file of the clean copy to the path the binding
    names, creating private parent directories."""
    root = Path(evidence)
    for entry in entries:
        target = root.joinpath(*PurePosixPath(entry["path"]).parts)
        target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        if os.path.lexists(target):
            fail(f"evidence {entry['path']} already exists in the clean replay")
        os.rename(root / entry["evidence"], target)


def profile_validator_pin(repository_root):
    """The validator path and digest the pinned checkout's profile declares."""
    fields = {}
    for line in read_bounded(repository_root, PROFILE_PATH).decode("ascii").splitlines():
        key, separator, value = line.partition("=")
        if separator and key in ("binding-validator", "binding-validator-sha256"):
            if key in fields:
                fail(f"profile declares {key} twice")
            fields[key] = value
    if set(fields) != {"binding-validator", "binding-validator-sha256"}:
        fail("profile does not pin the binding validator")
    replay.relative_binding_path(fields["binding-validator"])
    return Path(repository_root) / fields["binding-validator"], fields["binding-validator-sha256"]


def local_imports(data, directory, repository):
    """The repository-local modules one module's source names: every
    ``import``/``from`` target and every ``NAME.py`` string constant (the
    validator's file-location fallbacks and subprocess scripts) that is a
    file beside it."""
    names = set()
    for node in ast.walk(ast.parse(data)):
        if isinstance(node, ast.Import):
            names.update(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            names.add(node.module.split(".")[0])
        elif isinstance(node, ast.Constant) and type(node.value) is str and \
                re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*\.py", node.value):
            names.add(node.value[:-3])
    found = []
    for name in sorted(names):
        relative = f"{directory}/{name}.py"
        if (Path(repository) / relative).is_file():
            found.append(relative)
    return found


def validator_closure(repository_root, validator, validator_sha256):
    """The validator's closure from the pinned checkout: [(path, bytes)] for
    the entry (which must hash to VALIDATOR_SHA256), every local module it
    imports transitively (which must be exactly VALIDATOR_MODULES) and
    VALIDATOR_DATA, plus the closure digest. Every byte returned is the byte
    hashed; nothing is read again."""
    repository = Path(repository_root).resolve()
    try:
        entry = Path(validator).resolve().relative_to(repository).as_posix()
    except ValueError:
        fail("validator is not a file of the pinned checkout")
    files = {entry: read_bounded(repository, entry, VALIDATOR_FILE_CAP)}
    if hashlib.sha256(files[entry]).hexdigest() != validator_sha256:
        fail("validator differs from its pinned SHA-256")
    pending = [entry]
    while pending:
        module = pending.pop()
        for relative in local_imports(files[module], PurePosixPath(module).parent.as_posix(), repository):
            if relative not in files:
                files[relative] = read_bounded(repository, relative, VALIDATOR_FILE_CAP)
                pending.append(relative)
    undeclared = sorted(set(files) - set(VALIDATOR_MODULES))
    missing = sorted(set(VALIDATOR_MODULES) - set(files))
    if undeclared or missing:
        fail(f"validator closure differs from the reviewed module list: undeclared {undeclared}, missing {missing}")
    for relative in VALIDATOR_DATA:
        files[relative] = read_bounded(repository, relative, VALIDATOR_FILE_CAP)
    closure = sorted(files.items())
    digest = binding._canonical_files_digest([dict(descriptor_of(path, data), name=path) for path, data in closure])
    return entry, closure, digest


def install_validator(closure, directory):
    """Write the verified closure bytes into the new private DIRECTORY at
    their repository paths; it holds no bytecode cache."""
    root = Path(directory)
    root.mkdir(mode=0o700)
    for path, data in closure:
        target = root.joinpath(*PurePosixPath(path).parts)
        target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        create_exclusive(target, data)


def normalise(text, replacements):
    """Replace machine-specific paths in TEXT with fixed tokens."""
    for path, token in sorted(replacements, key=lambda item: len(item[0]), reverse=True):
        text = text.replace(path, token)
    return text


def validator_environment(prefix):
    """The validator's minimal environment. ``-I`` covers only the validator
    itself; the #508 validator it runs as ``[sys.executable, script]``
    inherits this environment, so it disables the user site (no
    ``usercustomize`` or user ``.pth`` file) and bytecode writes there too.
    PYTHONSAFEPATH is not set: that subprocess imports its siblings from its
    own (installed, verified) directory."""
    return {"PATH": os.environ.get("PATH", ""), "LANG": "C", "PYTHONNOUSERSITE": "1",
            "PYTHONDONTWRITEBYTECODE": "1", "PYTHONPYCACHEPREFIX": str(prefix)}


def replay_lane_f(result, lane_f, clean, repository_root, trusted_sha256, validator, validator_sha256,
                  closure_sha256, verdict_path):
    """Run the pinned validator closure over RESULT and lane F's directory in
    the new CLEAN directory; write and return the verdict record.

    A wrong pin refuses before anything is created. A refusal by lane F's own
    import, final-binding check or evidence layout is recorded in the
    verdict like the validator's."""
    replay.digest_argument(trusted_sha256, "service execution receipt")
    replay.digest_argument(validator_sha256, "validator")
    replay.digest_argument(closure_sha256, "validator closure")
    repository = Path(repository_root).resolve()
    entry, closure, digest = validator_closure(repository, validator, validator_sha256)
    if digest != closure_sha256:
        fail("validator closure differs from its pinned digest")
    clean = Path(clean)
    os.mkdir(clean, 0o700)
    clean = clean.resolve()
    evidence = clean / "result"
    installed = clean / "validator"
    prefix = clean / "bytecode"
    install_validator(closure, installed)
    prefix.mkdir(mode=0o700)
    replacements = [(str(evidence), "EVIDENCE_ROOT"), (str(installed), "VALIDATOR_ROOT"),
                    (str(clean), "CLEAN_ROOT"), (str(repository), "REPOSITORY_ROOT"),
                    (str(Path(lane_f).resolve()), "LANE_F_ROOT"), (str(Path(result).resolve()), "RESULT_ROOT"),
                    (tempfile.gettempdir(), "TMPDIR")]
    output = None
    refusal = None
    final_descriptor = None
    independent = None
    layout = None
    try:
        copy_result(result, evidence)
        final_path = replay.lane_f_import(lane_f, evidence)
        final_data = final_path.read_bytes()
        final_descriptor = descriptor_of(replay.FINAL_BINDING_NAME, final_data)
        final = replay.final_binding_check(evidence, final_path)
        independent = final["workflow"]["phases"]["independent_replay"]
        entries, layout_sha256 = evidence_layout(evidence, final)
        lay_out_evidence(evidence, entries)
        layout = {"entries": len(entries), "sha256": layout_sha256}
    except (ValueError, OSError, KeyError, TypeError, AttributeError) as error:
        refusal = f"lane F: {error}"
    if refusal is None:
        process = subprocess.run(
            [sys.executable, "-I", "-B", "-X", f"pycache_prefix={prefix}", str(installed / entry), str(final_path),
             "--evidence-root", str(evidence), "--repository-root", str(repository),
             "--trusted-execution-receipt-sha256", trusted_sha256],
            check=False, capture_output=True, text=True, cwd=evidence, timeout=REPLAY_TIMEOUT,
            env=validator_environment(prefix))
        if process.returncode == 0:
            try:
                output = json.loads(process.stdout)
            except json.JSONDecodeError:
                refusal = "validator output is not one JSON result"
        else:
            lines = process.stderr.strip().splitlines() or ["validator exited without a diagnostic"]
            refusal = lines[-1]
    accepted = output is not None and output.get("proof") == PROOF and \
        all(output.get(key) is True for key in PROOF_FLAGS)
    if output is not None and not accepted:
        refusal = "validator did not establish a complete independent replay"
    verdict = {
        "schema": VERDICT_SCHEMA, "version": 1,
        "validator": {"path": entry, "sha256": validator_sha256, "closure_sha256": digest,
                      "closure": [descriptor_of(path, data) for path, data in closure]},
        "evidence_layout": layout, "final_binding": final_descriptor, "independent_replay": independent,
        "trusted_execution_receipt_sha256": trusted_sha256,
        "verdict": "accepted" if accepted else "refused",
        "validator_result": output,
        "refusal": normalise(refusal, replacements) if refusal is not None else None,
    }
    create_exclusive(verdict_path, canonical(verdict))
    return verdict


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("bundle", help="write the independent-replay archive to publish")
    make.add_argument("result", type=Path, help="unpacked service result (bench_service unpack-export)")
    make.add_argument("output", type=Path, help="new archive file (written through OUTPUT.partial)")
    make.add_argument("--publication-id", required=True)
    write = commands.add_parser("bind", help="write lane F's directory from the downloaded publication")
    write.add_argument("result", type=Path)
    write.add_argument("lane_f", type=Path, help="new lane F directory")
    write.add_argument("--downloaded-bundle", required=True, type=Path,
                       help="a fresh download of the published archive")
    write.add_argument("--published-bundle-sha256", required=True,
                       help="from the publisher, never from the downloaded file")
    write.add_argument("--publication-id", required=True)
    write.add_argument("--release", required=True)
    write.add_argument("--run-id", required=True)
    write.add_argument("--repository-root", required=True, type=Path,
                       help="clean pinned checkout at the harness commit")
    write.add_argument("--harness-commit", required=True,
                       help="the reviewed harness source commit from a trusted source, never the result; "
                            "the record's measurement.harness_source_commit must equal it")
    write.add_argument("--harness-tree", required=True,
                       help="the reviewed harness source tree from a trusted source; "
                            "the record's measurement.harness_source_tree must equal it")
    check = commands.add_parser("replay", help="run the pinned validator closure in a clean directory")
    check.add_argument("result", type=Path)
    check.add_argument("lane_f", type=Path)
    check.add_argument("clean", type=Path, help="new private clean replay directory")
    check.add_argument("--repository-root", required=True, type=Path)
    check.add_argument("--validator", type=Path,
                       help="the production binding validator (default: the profile's binding-validator)")
    check.add_argument("--validator-sha256",
                       help="the validator's pinned digest (default: the profile's binding-validator-sha256)")
    check.add_argument("--validator-closure-sha256", required=True,
                       help="the reviewed digest of the validator's closure (see validator-closure)")
    check.add_argument("--trusted-execution-receipt-sha256", required=True,
                       help="from the service control authority, never the bundle")
    check.add_argument("--verdict", required=True, type=Path, help="new verdict record")
    show = commands.add_parser("validator-closure", help="print the validator closure of a reviewed checkout")
    show.add_argument("--repository-root", required=True, type=Path)
    show.add_argument("--validator", type=Path)
    show.add_argument("--validator-sha256")
    args = parser.parse_args(arguments)
    status = 1
    try:
        if args.command == "bundle":
            binding._token(args.publication_id, "publication ID")
            state = composed_state(args.result)
            print(json.dumps(write_bundle_file(args.result, state, args.publication_id, args.output),
                             sort_keys=True))
            status = 0
        elif args.command == "bind":
            print(json.dumps(bind(args.result, args.lane_f, args.downloaded_bundle, args.published_bundle_sha256,
                                  args.publication_id, args.release, args.run_id, args.repository_root,
                                  args.harness_commit, args.harness_tree), sort_keys=True))
            status = 0
        else:
            validator, validator_sha256 = args.validator, args.validator_sha256
            if validator is None or validator_sha256 is None:
                profile_validator, profile_sha256 = profile_validator_pin(args.repository_root)
                validator = validator or profile_validator
                validator_sha256 = validator_sha256 or profile_sha256
            if args.command == "validator-closure":
                entry, closure, digest = validator_closure(args.repository_root, validator, validator_sha256)
                print(json.dumps({"validator": entry, "closure_sha256": digest,
                                  "closure": [descriptor_of(path, data) for path, data in closure]},
                                 sort_keys=True))
                status = 0
            else:
                verdict = replay_lane_f(args.result, args.lane_f, args.clean, args.repository_root,
                                        args.trusted_execution_receipt_sha256, validator, validator_sha256,
                                        args.validator_closure_sha256, args.verdict)
                print(json.dumps(verdict, sort_keys=True))
                if verdict["verdict"] != "accepted":
                    print(f"lane F replay refused: {verdict['refusal']}", file=sys.stderr)
                status = 0 if verdict["verdict"] == "accepted" else 1
    except (ValueError, OSError, KeyError, TypeError, AttributeError, SyntaxError, subprocess.TimeoutExpired,
            tarfile.TarError) as error:
        print(f"lane F failed: {error}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
