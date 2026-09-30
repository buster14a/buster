#!/usr/bin/env python3
"""Lane F's production writer: the final binding and the independent replay.

Ownership: lane F of the native-retirement performance recipe (#881, #1024).
Its input is an unpacked, sealed and composed retirement export: the service
result the worker-unit producer finalized, reconstructed by `bench_service
unpack-export` (see EXPORT.md). It writes lane F's directory, which
`retirement_export_replay.py --lane-f` imports into the clean replay, and it
runs the production binding validator over the result and that directory.

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
  record's harness commit, reruns it over the downloaded adapter input and
  requires the sealed adapter result byte for byte. Only then does it write
  the publication receipt, the replay bundle, the
  ``buster-native-retirement-independent-replay-v1`` phase record and
  ``retirement-final-binding.json`` (``replay.FINAL_BINDING_NAME``) into a
  ``.pending`` directory, check the final binding with
  ``replay.final_binding_check`` and rename it to the lane F directory.
- ``replay`` pins the production validator by path and SHA-256 (by default
  the profile's ``binding-validator`` pin, ``profile_validator_pin``) before
  it runs, copies the result into a new clean directory, imports lane F's
  directory there (``replay.lane_f_import``), checks the final binding and
  runs the real validator. It writes a verdict record naming the validator's
  identity, the final binding, the phase record and the validator's output or
  refusal, and succeeds only for a complete independent replay.

The phase record's keys are fixed by the validator (``_workflow_phase``), so
the validator identity and verdict live in the separate verdict record.
Nothing here publishes durably (#510) or admits the recipe.

Map: ``composed_state`` (composed record, sealed closure), ``write_bundle``
and ``bundle_digest`` (the deterministic archive), ``adapter_replay``,
``bind``, ``copy_result``, ``profile_validator_pin``, ``replay_lane_f``,
``main``.
"""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
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
EVIDENCE_ROOT_TOKEN = "EVIDENCE_ROOT"
# The integration-owned profile pins the validator's path and SHA-256.
PROFILE_PATH = "tools/bench_service/profiles/native-retirement-performance-v1.blocked"

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


def composed_state(result):
    """The composed record, its sealed result and the sealed closure, each
    descriptor checked against the unpacked bytes."""
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
        if artifact["bytes"] > replay.FILE_CAP:
            fail("a sealed closure file exceeds the per-file cap")
        binding._check_evidence(root, artifact, f"seal.files[{index}]")
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
    result_bundle = parse_json(read_bounded(root, result_bundle_descriptor["path"]))
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
            "closure": sorted(closure, key=lambda item: item["path"].encode()),
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


def write_bundle(result, closure, publication_id, sink):
    """Stream the independent-replay archive for ``closure`` into ``sink``.

    Its first member is the bundle manifest the validator requires; every
    member is re-hashed while it is archived and must match its descriptor.
    """
    files = [{key: item[key] for key in ("path", "bytes", "sha256")} for item in closure]
    manifest = canonical({
        "schema": BUNDLE_MANIFEST_SCHEMA, "version": 1, "publication_id": publication_id, "files": files,
        "root_sha256": binding._canonical_files_digest([{"name": item["path"], **item} for item in files])})
    with tarfile.open(fileobj=sink, mode="w|", format=tarfile.PAX_FORMAT) as archive:
        archive.addfile(_tar_info(BUNDLE_MANIFEST_NAME, len(manifest)), io.BytesIO(manifest))
        for item in files:
            descriptor = open_regular(result, item["path"])
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


def bundle_digest(result, closure, publication_id):
    sink = HashSink()
    write_bundle(result, closure, publication_id, sink)
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
    """Copy SOURCE to a new TARGET, returning (bytes, SHA-256)."""
    descriptor = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    digest = hashlib.sha256()
    copied = 0
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            fail(f"{source} is not a regular file")
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


def adapter_replay(evidence, downloaded, publication_id, state, repository_root):
    """Rerun the reviewed #619 adapter over the downloaded archive's input.

    The archive is extracted with the validator's reader against the sealed
    closure; the adapter is rebuilt from the pinned checkout at the record's
    harness commit and tree. Returns the adapter identity for the replay
    bundle: (source digest, toolchain digest, build command).
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
        output = Path(scratch) / "statistics-replay.json"
        adapter_input = extracted.joinpath(*PurePosixPath(state["adapter_input"]["path"]).parts)
        process = subprocess.run([str(executable), "retirement-replay", "--input", str(adapter_input),
                                  "--output", str(output)], check=False, capture_output=True,
                                 cwd=repository_root, timeout=REPLAY_TIMEOUT)
        if process.returncode != 0 or not output.is_file():
            fail("lane F's #619 adapter replay failed")
        sealed_output = extracted.joinpath(*PurePosixPath(state["adapter_result"]["path"]).parts)
        if output.read_bytes() != sealed_output.read_bytes():
            fail("lane F's #619 adapter replay differs from the sealed adapter result")
    return source_digest, toolchain_digest, build_command


def bind(result, lane_f, downloaded, published_sha256, publication_id, release, run_id, repository_root):
    """Write lane F's directory for the unpacked RESULT (see the module doc)."""
    replay.digest_argument(published_sha256, "published bundle")
    for value, name in ((publication_id, "publication ID"), (release, "release"), (run_id, "run ID")):
        binding._token(value, name)
    lane_f = Path(lane_f)
    pending = lane_f.with_name(lane_f.name + ".pending")
    if os.path.lexists(lane_f):
        fail("lane F directory already exists")
    state = composed_state(result)
    names = (BUNDLE_NAME, PUBLICATION_NAME, REPLAY_BUNDLE_NAME, state["replay_path"], replay.FINAL_BINDING_NAME)
    present = set(os.listdir(result))
    if len(set(names)) != len(names) or present.intersection(names):
        fail("a lane F file collides with the service result or another lane F file")
    derived_bytes, derived_sha256 = bundle_digest(result, state["closure"], publication_id)
    closure_bytes = sum(item["bytes"] for item in state["closure"])
    if derived_bytes > closure_bytes + BUNDLE_OVERHEAD_CAP:
        fail("the independent-replay archive exceeds the validator's closure bound")
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
    directory = os.open(pending, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)
    if os.path.lexists(lane_f):
        fail("lane F directory appeared during the bind")
    os.rename(pending, lane_f)
    parent = os.open(lane_f.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(parent)
    finally:
        os.close(parent)
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
                copy_bounded(directory / name, target, replay.FILE_CAP)
            else:
                fail("unpacked result holds a link or a special file")


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


def replay_lane_f(result, lane_f, clean, repository_root, trusted_sha256, validator, validator_sha256,
                  verdict_path):
    """Run the pinned production validator over RESULT and lane F's directory
    in the new CLEAN directory; write and return the verdict record."""
    replay.digest_argument(trusted_sha256, "service execution receipt")
    replay.digest_argument(validator_sha256, "validator")
    repository = Path(repository_root).resolve()
    validator = Path(validator).resolve()
    try:
        validator_name = validator.relative_to(repository).as_posix()
    except ValueError:
        fail("validator is not a file of the pinned checkout")
    validator_bytes = read_bounded(repository, validator_name)
    if hashlib.sha256(validator_bytes).hexdigest() != validator_sha256:
        fail("validator differs from its pinned SHA-256")
    evidence = Path(clean) / "result"
    os.mkdir(clean, 0o700)
    copy_result(result, evidence)
    final_path = replay.lane_f_import(lane_f, evidence)
    final = replay.final_binding_check(evidence, final_path)
    process = subprocess.run(
        [sys.executable, str(validator), str(final_path), "--evidence-root", str(evidence),
         "--repository-root", str(repository), "--trusted-execution-receipt-sha256", trusted_sha256],
        check=False, capture_output=True, text=True, cwd=evidence, timeout=REPLAY_TIMEOUT,
        env={"PATH": os.environ.get("PATH", ""), "LANG": "C"})
    output = None
    refusal = None
    if process.returncode == 0:
        try:
            output = json.loads(process.stdout)
        except json.JSONDecodeError:
            refusal = "validator output is not one JSON result"
    else:
        lines = process.stderr.strip().splitlines() or ["validator exited without a diagnostic"]
        refusal = lines[-1].replace(str(evidence), EVIDENCE_ROOT_TOKEN)
    accepted = output is not None and output.get("proof") == PROOF and \
        all(output.get(key) is True for key in PROOF_FLAGS)
    if output is not None and not accepted:
        refusal = "validator did not establish a complete independent replay"
    verdict = {
        "schema": VERDICT_SCHEMA, "version": 1,
        "validator": {"path": validator_name, "sha256": validator_sha256},
        "final_binding": descriptor_of(replay.FINAL_BINDING_NAME, final_path.read_bytes()),
        "independent_replay": final["workflow"]["phases"]["independent_replay"],
        "trusted_execution_receipt_sha256": trusted_sha256,
        "verdict": "accepted" if accepted else "refused",
        "validator_result": output, "refusal": refusal,
    }
    create_exclusive(verdict_path, canonical(verdict))
    return verdict


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("bundle", help="write the independent-replay archive to publish")
    make.add_argument("result", type=Path, help="unpacked service result (bench_service unpack-export)")
    make.add_argument("output", type=Path, help="new archive file")
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
                       help="pinned checkout at the record's harness commit")
    check = commands.add_parser("replay", help="run the pinned validator in a clean directory")
    check.add_argument("result", type=Path)
    check.add_argument("lane_f", type=Path)
    check.add_argument("clean", type=Path, help="new private clean replay directory")
    check.add_argument("--repository-root", required=True, type=Path)
    check.add_argument("--validator", type=Path,
                       help="the production binding validator (default: the profile's binding-validator)")
    check.add_argument("--validator-sha256",
                       help="the validator's pinned digest (default: the profile's binding-validator-sha256)")
    check.add_argument("--trusted-execution-receipt-sha256", required=True,
                       help="from the service control authority, never the bundle")
    check.add_argument("--verdict", required=True, type=Path, help="new verdict record")
    args = parser.parse_args(arguments)
    status = 1
    try:
        if args.command == "bundle":
            binding._token(args.publication_id, "publication ID")
            state = composed_state(args.result)
            descriptor = create_exclusive(args.output)
            try:
                with os.fdopen(descriptor, "wb", closefd=False) as stream:
                    sink = HashSink(stream)
                    write_bundle(args.result, state["closure"], args.publication_id, sink)
                os.fchmod(descriptor, 0o400)
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
            print(json.dumps({"bundle": str(args.output), "bytes": sink.length,
                              "sha256": sink.digest.hexdigest()}, sort_keys=True))
            status = 0
        elif args.command == "bind":
            print(json.dumps(bind(args.result, args.lane_f, args.downloaded_bundle, args.published_bundle_sha256,
                                  args.publication_id, args.release, args.run_id, args.repository_root),
                             sort_keys=True))
            status = 0
        else:
            validator, validator_sha256 = profile_validator_pin(args.repository_root)
            verdict = replay_lane_f(args.result, args.lane_f, args.clean, args.repository_root,
                                    args.trusted_execution_receipt_sha256, args.validator or validator,
                                    args.validator_sha256 or validator_sha256, args.verdict)
            print(json.dumps(verdict, sort_keys=True))
            if verdict["verdict"] != "accepted":
                print(f"lane F replay refused: {verdict['refusal']}", file=sys.stderr)
            status = 0 if verdict["verdict"] == "accepted" else 1
    except (ValueError, OSError, KeyError, TypeError, subprocess.TimeoutExpired, tarfile.TarError) as error:
        print(f"lane F failed: {error}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
