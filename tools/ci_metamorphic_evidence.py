#!/usr/bin/env python3
"""Retain fail-closed evidence for metamorphic failures advertised by CI logs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
import tempfile
from typing import Iterable


_SCHEMA = 1
_MAX_LOG_BYTES = 256 * 1024 * 1024
_MAX_BUNDLES = 64
_MAX_FILES = 4096
_MAX_BUNDLE_BYTES = 512 * 1024 * 1024
_MAX_OUTCOME_BYTES = 64 * 1024 * 1024
_MARKER = re.compile(
    r"^METAMORPHIC_FAILURE seed=(?P<seed>0|[1-9][0-9]{0,9}) "
    r"mask=(?P<mask>0|[1-9][0-9]{0,9}) "
    r"target=(?P<target>[A-Za-z0-9_-]+) "
    r"allocator=(?P<allocator>0|[1-9][0-9]{0,9}) "
    r"bundle=(?P<bundle>.+)$"
)
_OUTCOME = re.compile(
    rb"^phase=(?P<phase>[0-9]+) launched=(?P<launched>[01]) "
    rb"result=(?P<result>[0-9]+) platform_status=(?P<platform_status>[0-9]+) "
    rb"timed_out=(?P<timed_out>[01])$"
)
_ROW = re.compile(
    r"(?P<tree>build-(?P<shard>[A-Za-z0-9_]+)-ci_(?P<ci>on|off)-"
    r"cc_(?P<compiler>[A-Za-z0-9_.]+)-sanitize_(?P<sanitize>on|off)-"
    r"fuzz_available_(?P<fuzz>on|off)-configs_(?P<configuration>Debug|Release))"
)
_REQUIRED = (
    "reproducer.txt",
    "observed-base.c",
    "observed-transformed.c",
    "observed-base.log",
    "observed-transformed.log",
    "base.log",
    "transformed.log",
    "minimized/base.log",
    "minimized/transformed.log",
)
_OUTCOME_FILES = {
    "observed-base": "observed-base.log",
    "observed-transformed": "observed-transformed.log",
    "replay-base": "base.log",
    "replay-transformed": "transformed.log",
    "minimized-base": "minimized/base.log",
    "minimized-transformed": "minimized/transformed.log",
}
_ALLOCATORS = {0: "none", 1: "mir-stack", 2: "fast", 3: "quality"}
_PHASES = {0: "write", 1: "compile", 2: "consume", 3: "execute", 4: "unexecuted", 5: "runner"}
_RESULTS = {0: "success", 1: "failed", 2: "failed-try-again", 3: "crash"}
_MISSING_OUTPUT = b"compiler reported success without a nonempty requested output"


class EvidenceError(RuntimeError):
    """The log advertised evidence that could not be retained faithfully."""


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _write_json_atomic(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def _decode_log(data: bytes) -> tuple[str, str]:
    if data.startswith(b"\xff\xfe"):
        return data.decode("utf-16"), "utf-16-le"
    if data.startswith(b"\xfe\xff"):
        return data.decode("utf-16"), "utf-16-be"
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig"), "utf-8"
    sample = data[:4096]
    if sample and sample.count(b"\x00") > len(sample) // 4:
        return data.decode("utf-16-le"), "utf-16-le"
    return data.decode("utf-8"), "utf-8"


def _read_markers(log_path: Path) -> tuple[list[dict[str, object]], dict[str, object]]:
    if not log_path.exists():
        return [], {"state": "missing", "path": str(log_path)}
    if log_path.is_symlink() or not log_path.is_file():
        raise EvidenceError(f"combination log is not a regular file: {log_path}")
    size = log_path.stat().st_size
    if size > _MAX_LOG_BYTES:
        raise EvidenceError(f"combination log exceeds {_MAX_LOG_BYTES} bytes: {size}")
    data = log_path.read_bytes()
    try:
        text, encoding = _decode_log(data)
    except UnicodeError as error:
        raise EvidenceError(f"combination log has unsupported encoding: {error}") from error
    markers: list[dict[str, object]] = []
    seen: set[tuple[int, int, str, int, str]] = set()
    for line in text.splitlines():
        match = _MARKER.fullmatch(line.strip())
        if not match:
            continue
        marker = {
            "seed": int(match.group("seed")),
            "mask": int(match.group("mask")),
            "target": match.group("target"),
            "allocator": int(match.group("allocator")),
            "bundle": match.group("bundle"),
        }
        identity = (marker["seed"], marker["mask"], marker["target"], marker["allocator"], marker["bundle"])
        if identity in seen:
            raise EvidenceError(f"duplicate metamorphic failure marker: {line.strip()}")
        seen.add(identity)
        markers.append(marker)
    if len(markers) > _MAX_BUNDLES:
        raise EvidenceError(f"metamorphic failure count exceeds {_MAX_BUNDLES}: {len(markers)}")
    return markers, {
        "state": "available",
        "path": str(log_path),
        "encoding": encoding,
        "bytes": len(data),
        "sha256": _sha256_bytes(data),
    }


def _bundle_source(workspace: Path, advertised: str) -> tuple[Path, str]:
    normalized = advertised.replace("\\", "/")
    pure = PurePosixPath(normalized)
    if pure.is_absolute() or not pure.parts or any(part in ("", ".", "..") for part in pure.parts):
        raise EvidenceError(f"unsafe metamorphic bundle path: {advertised}")
    if ":" in pure.parts[0]:
        raise EvidenceError(f"drive-qualified metamorphic bundle path: {advertised}")
    current = workspace
    for part in pure.parts:
        current = current / part
        try:
            mode = current.lstat().st_mode
        except OSError as error:
            raise EvidenceError(f"advertised metamorphic bundle is unavailable: {normalized}: {error}") from error
        if stat.S_ISLNK(mode):
            raise EvidenceError(f"metamorphic bundle traverses a symbolic link: {normalized}")
    workspace_resolved = workspace.resolve(strict=True)
    source = current.resolve(strict=True)
    try:
        source.relative_to(workspace_resolved)
    except ValueError as error:
        raise EvidenceError(f"metamorphic bundle escapes the workspace: {normalized}") from error
    if not source.is_dir():
        raise EvidenceError(f"metamorphic bundle is not a directory: {normalized}")
    return source, PurePosixPath(*pure.parts).as_posix()


def _regular_files(root: Path) -> list[Path]:
    files: list[Path] = []
    total = 0
    stack = [root]
    while stack:
        directory = stack.pop()
        for entry in sorted(os.scandir(directory), key=lambda item: item.name, reverse=True):
            mode = entry.stat(follow_symlinks=False).st_mode
            path = Path(entry.path)
            if stat.S_ISLNK(mode):
                raise EvidenceError(f"metamorphic bundle contains a symbolic link: {path.relative_to(root)}")
            if stat.S_ISDIR(mode):
                stack.append(path)
            elif stat.S_ISREG(mode):
                files.append(path)
                total += entry.stat(follow_symlinks=False).st_size
                if len(files) > _MAX_FILES:
                    raise EvidenceError(f"metamorphic bundle exceeds {_MAX_FILES} files")
                if total > _MAX_BUNDLE_BYTES:
                    raise EvidenceError(f"metamorphic bundle exceeds {_MAX_BUNDLE_BYTES} bytes")
            else:
                raise EvidenceError(f"metamorphic bundle contains a non-regular entry: {path.relative_to(root)}")
    return sorted(files, key=lambda path: path.relative_to(root).as_posix())


def _read_small(path: Path, limit: int) -> bytes:
    size = path.stat().st_size
    if size > limit:
        raise EvidenceError(f"evidence file exceeds {limit} bytes: {path}")
    return path.read_bytes()


def _parse_key_values(path: Path) -> dict[str, str]:
    try:
        text = _read_small(path, 1024 * 1024).decode("utf-8")
    except UnicodeError as error:
        raise EvidenceError(f"reproducer is not UTF-8: {path}: {error}") from error
    result: dict[str, str] = {}
    for line in text.splitlines():
        for field in line.split():
            key, separator, value = field.partition("=")
            if separator and key:
                if key in result:
                    raise EvidenceError(f"duplicate reproducer field {key}: {path}")
                result[key] = value
    return result


def _parse_outcome(path: Path) -> dict[str, object]:
    data = _read_small(path, _MAX_OUTCOME_BYTES)
    normalized = data.replace(b"\r\n", b"\n")
    first, separator, remainder = normalized.partition(b"\n")
    if not separator:
        raise EvidenceError(f"outcome log has no header terminator: {path}")
    match = _OUTCOME.fullmatch(first)
    if not match:
        raise EvidenceError(f"outcome log has malformed status header: {path}")
    command, separator, streams = remainder.partition(b"\nstdout:\n")
    if not separator:
        raise EvidenceError(f"outcome log has no stdout section: {path}")
    stdout, separator, stderr = streams.partition(b"\nstderr:\n")
    if not separator:
        raise EvidenceError(f"outcome log has no stderr section: {path}")
    phase = int(match.group("phase"))
    launched = match.group("launched") == b"1"
    result = int(match.group("result"))
    platform_status = int(match.group("platform_status"))
    timed_out = match.group("timed_out") == b"1"
    if timed_out:
        classification = "timeout"
    elif _MISSING_OUTPUT in stderr:
        classification = "missing-output"
    elif not launched:
        classification = "launch-failure" if phase != 0 else "write-or-prelaunch-failure"
    elif result == 2:
        classification = "wait-failure"
    elif result == 3:
        classification = "process-crash"
    elif result != 0 or platform_status != 0:
        classification = "process-failure"
    else:
        classification = "success"
    return {
        "phase": phase,
        "phase_name": _PHASES.get(phase, f"unknown-{phase}"),
        "launched": launched,
        "result": result,
        "result_name": _RESULTS.get(result, f"unknown-{result}"),
        "platform_status": platform_status,
        "timed_out": timed_out,
        "classification": classification,
        "command": command.decode("utf-8", errors="replace"),
        "command_sha256": _sha256_bytes(command),
        "stdout_bytes": len(stdout),
        "stdout_sha256": _sha256_bytes(stdout),
        "stderr_bytes": len(stderr),
        "stderr_sha256": _sha256_bytes(stderr),
    }


def _row_identity(outcome: dict[str, object], metadata: dict[str, str]) -> dict[str, object]:
    command = str(outcome.get("command", ""))
    row: dict[str, object] = {
        "matrix_shard": metadata.get("matrix_shard", "unknown"),
        "platform": metadata.get("platform", "unknown"),
        "architecture": metadata.get("architecture", "unknown"),
        "compiler_command": command,
        "compiler_command_sha256": outcome.get("command_sha256"),
    }
    match = _ROW.search(command.replace("\\", "/"))
    if match:
        row.update({
            "build_tree": match.group("tree"),
            "tree_shard": match.group("shard"),
            "ci": match.group("ci") == "on",
            "compiler": match.group("compiler"),
            "sanitize": match.group("sanitize") == "on",
            "fuzz_available": match.group("fuzz") == "on",
            "configuration": match.group("configuration"),
        })
    return row


def _metadata(environment: dict[str, str]) -> dict[str, str]:
    names = {
        "repository": "GITHUB_REPOSITORY",
        "source_sha": "GITHUB_SHA",
        "ref": "GITHUB_REF",
        "run_id": "GITHUB_RUN_ID",
        "run_attempt": "GITHUB_RUN_ATTEMPT",
        "runner_os": "RUNNER_OS",
        "runner_arch": "RUNNER_ARCH",
        "runner_image": "ImageOS",
        "runner_image_version": "ImageVersion",
        "matrix_shard": "BUSTER_MATRIX_SHARD",
        "platform": "BUSTER_CI_COVERAGE_PLATFORM",
        "architecture": "BUSTER_CI_COVERAGE_ARCH",
    }
    return {name: str(environment.get(variable, "unknown")) for name, variable in names.items()}


def _require_provenance(metadata: dict[str, str]) -> None:
    required = ("repository", "source_sha", "run_id", "run_attempt", "matrix_shard", "platform", "architecture")
    missing = [name for name in required if metadata.get(name) in (None, "", "unknown")]
    if missing:
        raise EvidenceError("metamorphic failure provenance is incomplete: " + ", ".join(missing))


def _copy_bundle(source: Path, destination: Path, files: Iterable[Path]) -> list[dict[str, object]]:
    manifest: list[dict[str, object]] = []
    for source_file in files:
        relative = source_file.relative_to(source)
        destination_file = destination / relative
        destination_file.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_file, destination_file)
        manifest.append({
            "path": relative.as_posix(),
            "bytes": destination_file.stat().st_size,
            "sha256": _sha256_file(destination_file),
        })
    return manifest


def _safe_name(index: int, marker: dict[str, object]) -> str:
    target = re.sub(r"[^A-Za-z0-9_.-]+", "-", str(marker["target"]))
    allocator = _ALLOCATORS.get(int(marker["allocator"]), f"mode-{marker['allocator']}")
    return f"{index:03d}-seed-{marker['seed']}-mask-{marker['mask']}-{target}-{allocator}"


def _collect_one(stage: Path, workspace: Path, marker: dict[str, object], metadata: dict[str, str], index: int) -> dict[str, object]:
    source, relative_source = _bundle_source(workspace, str(marker["bundle"]))
    files = _regular_files(source)
    present = {path.relative_to(source).as_posix() for path in files}
    missing = [name for name in _REQUIRED if name not in present]
    if missing:
        raise EvidenceError(f"advertised metamorphic bundle is incomplete ({relative_source}): {', '.join(missing)}")
    reproducer = _parse_key_values(source / "reproducer.txt")
    expected_allocator = _ALLOCATORS.get(int(marker["allocator"]), str(marker["allocator"]))
    expected = {
        "seed": str(marker["seed"]),
        "mask": str(marker["mask"]),
        "target": str(marker["target"]),
        "allocator": expected_allocator,
    }
    mismatches = [name for name, value in expected.items() if reproducer.get(name) != value]
    if mismatches:
        raise EvidenceError(f"metamorphic marker/reproducer mismatch ({relative_source}): {', '.join(mismatches)}")
    outcomes = {name: _parse_outcome(source / relative) for name, relative in _OUTCOME_FILES.items()}
    observed_base = outcomes["observed-base"]
    row = _row_identity(observed_base, metadata)
    name = _safe_name(index, marker)
    destination = stage / name
    destination.mkdir(parents=True)
    file_manifest = _copy_bundle(source, destination / "bundle", files)
    bundle_manifest = {
        "schema": _SCHEMA,
        "kind": "metamorphic-failure-bundle",
        "metadata": metadata,
        "marker": {
            "seed": marker["seed"],
            "mask": marker["mask"],
            "target": marker["target"],
            "allocator": marker["allocator"],
            "allocator_name": expected_allocator,
        },
        "source_bundle": relative_source,
        "row": row,
        "reproducer": reproducer,
        "outcomes": outcomes,
        "files": file_manifest,
        "file_count": len(file_manifest),
        "total_bytes": sum(int(entry["bytes"]) for entry in file_manifest),
    }
    manifest_path = destination / "manifest.json"
    _write_json_atomic(manifest_path, bundle_manifest)
    return {
        "id": name,
        "path": name,
        "manifest_sha256": _sha256_file(manifest_path),
        "seed": marker["seed"],
        "mask": marker["mask"],
        "target": marker["target"],
        "allocator": marker["allocator"],
        "allocator_name": expected_allocator,
        "source_bundle": relative_source,
        "observed_base_classification": observed_base["classification"],
        "observed_transformed_classification": outcomes["observed-transformed"]["classification"],
    }


def collect(log_path: Path, workspace: Path, output: Path, environment: dict[str, str] | None = None) -> dict[str, object]:
    environment = dict(os.environ if environment is None else environment)
    workspace = workspace.resolve(strict=True)
    markers, log = _read_markers(log_path)
    metadata = _metadata(environment)
    if markers:
        _require_provenance(metadata)
    output_parent = output.parent
    output_parent.mkdir(parents=True, exist_ok=True)
    if output.exists() or output.is_symlink():
        raise EvidenceError(f"metamorphic evidence destination already exists: {output}")
    stage = Path(tempfile.mkdtemp(prefix=output.name + ".staging-", dir=output_parent))
    try:
        bundles = [_collect_one(stage, workspace, marker, metadata, index) for index, marker in enumerate(markers)]
        result = {
            "schema": _SCHEMA,
            "kind": "metamorphic-failure-evidence-index",
            "metadata": metadata,
            "source_log": log,
            "marker_count": len(markers),
            "bundles": bundles,
        }
        _write_json_atomic(stage / "index.json", result)
        os.replace(stage, output)
    except BaseException:
        shutil.rmtree(stage, ignore_errors=True)
        raise
    return result


def collect_from_environment(environment: dict[str, str]) -> dict[str, object] | None:
    required = str(environment.get("BUSTER_CI_REQUIRED", "")).split()
    if not any(name in required for name in ("combinations_unix", "combinations_windows")):
        return None
    runner_temp = Path(environment["RUNNER_TEMP"])
    workspace = Path(environment.get("GITHUB_WORKSPACE", Path(__file__).resolve().parents[1]))
    return collect(runner_temp / "buster-ci" / "combinations.log", workspace,
                   runner_temp / "buster-ci" / "metamorphic", environment)


def record_error(output: Path, environment: dict[str, str], error: BaseException) -> Path:
    path = output.parent / "metamorphic-evidence-error.json"
    _write_json_atomic(path, {
        "schema": _SCHEMA,
        "kind": "metamorphic-failure-evidence-error",
        "metadata": _metadata(environment),
        "error": str(error),
    })
    return path


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args(arguments)
    status = 1
    try:
        result = collect(options.log, options.workspace, options.output, dict(os.environ))
        print(f"METAMORPHIC_EVIDENCE retained={result['marker_count']} output={options.output}")
        status = 0
    except (EvidenceError, OSError, UnicodeError, ValueError) as error:
        record_error(options.output, dict(os.environ), error)
        print(f"METAMORPHIC_EVIDENCE failure: {error}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
