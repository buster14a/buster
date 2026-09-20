#!/usr/bin/env python3
"""Record and classify identity-bound native CI phase observations.

The workflow keeps correctness ownership in build.c and evidence packaging in
ci_pack_evidence.py.  This helper only records process-local monotonic durations,
materializes one adjacent observation JSON file, and performs fail-closed audit
comparisons.  It never retries, skips, or changes a tested command.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shlex
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

IDENTITY_SCHEMA = "buster.native-observation.identity.v1"
TOOLCHAIN_SCHEMA = "buster.native-observation.toolchain.v1"
PHASE_SCHEMA = "buster.native-observation.phase.v1"
OBSERVATION_SCHEMA = "buster.native-observation.v1"
ENRICHED_SCHEMA = "buster.native-observation.enriched.v1"
CLASSIFICATION_SCHEMA = "buster.native-observation.classification.v1"
CLOCK = "time.perf_counter_ns"
PROBE_BYTES = 8 * 1024 * 1024
SLOW_RATIO = 1.50
MINIMUM_BROAD_PHASES = 4
SETUP_PHASES = frozenset(
    {
        "configuration",
        "producer_build",
        "mode_payload",
        "differential_preparation",
        "evidence_packing",
    }
)
CORPUS_PHASE = "differential_corpus"
REQUIRED_IDENTITY_PATHS = (
    "source.commit",
    "source.tree",
    "workflow.name",
    "workflow.run_id",
    "workflow.run_attempt",
    "workflow.job_key",
    "workflow.job_name",
    "runner.requested_label",
    "runner.image_os",
    "runner.image_version",
    "runner.os",
    "runner.arch",
    "runner.cpu_model",
    "runner.cpu_signature",
    "runner.machine",
    "runner.cores",
    "runner.ram_mib",
    "configuration.name",
    "configuration.compiler",
    "configuration.linker",
    "configuration.debug_info",
    "configuration.differential_workers",
)
MATCH_PATHS = (
    "source.repository",
    "source.commit",
    "source.tree",
    "workflow.name",
    "workflow.workflow_sha",
    "workflow.job_key",
    "workflow.job_name",
    "runner.requested_label",
    "runner.environment",
    "runner.image_os",
    "runner.image_version",
    "runner.os",
    "runner.arch",
    "runner.platform",
    "runner.matrix_os",
    "runner.matrix_arch",
    "runner.cpu_model",
    "runner.cpu_signature",
    "runner.machine",
    "runner.cores",
    "runner.ram_mib",
    "configuration.name",
    "configuration.compiler",
    "configuration.linker",
    "configuration.debug_info",
    "configuration.differential_workers",
    "configuration.cmake_build_parallel_level",
    "configuration.ninja_flags",
    "runner_log.runner_version",
    "runner_log.provisioner_version",
    "runner_log.azure_region",
    "toolchain.compiler.path",
    "toolchain.compiler.sha256",
    "toolchain.cmake.path",
    "toolchain.cmake.sha256",
    "toolchain.ninja.path",
    "toolchain.ninja.sha256",
)


class ObservationError(ValueError):
    """A phase observation is malformed, contradictory, or incomparable."""


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def strict_object(value: Any, *, where: str, required: Iterable[str], optional: Iterable[str] = ()) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ObservationError(f"{where} must be an object")
    required_set = set(required)
    allowed = required_set | set(optional)
    keys = set(value)
    missing = sorted(required_set - keys)
    extra = sorted(keys - allowed)
    if missing:
        raise ObservationError(f"{where} is missing: {', '.join(missing)}")
    if extra:
        raise ObservationError(f"{where} has unexpected fields: {', '.join(extra)}")
    return value


def strict_string(value: Any, *, where: str, allow_empty: bool = False) -> str:
    if not isinstance(value, str) or (not allow_empty and not value):
        raise ObservationError(f"{where} must be a non-empty string")
    return value


def strict_integer(value: Any, *, where: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ObservationError(f"{where} must be an integer >= {minimum}")
    return value


def parse_utc(value: Any, *, where: str) -> datetime:
    text = strict_string(value, where=where)
    try:
        parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError as error:
        raise ObservationError(f"{where} is not an ISO-8601 timestamp") from error
    if parsed.tzinfo is None:
        raise ObservationError(f"{where} must include a UTC offset")
    return parsed.astimezone(timezone.utc)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds").replace("+00:00", "Z")


def load_json(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as source:
            return json.load(source)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ObservationError(f"cannot read {path}: {error}") from error


def git_output(*arguments: str) -> str:
    result = subprocess.run(["git", *arguments], capture_output=True, text=True, check=False)
    if result.returncode:
        raise ObservationError(f"git {' '.join(arguments)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def command_identity(command: Sequence[str]) -> dict[str, Any]:
    resolved = shutil.which(command[0]) or command[0]
    try:
        resolved_path = str(Path(resolved).resolve())
    except OSError:
        resolved_path = resolved
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=20, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ObservationError(f"cannot identify {' '.join(command)}: {error}") from error
    output = (result.stdout + result.stderr).strip()
    if result.returncode or not output:
        raise ObservationError(f"identity command failed ({result.returncode}): {' '.join(command)}")
    return {
        "argv": list(command),
        "path": resolved_path,
        "first_line": output.splitlines()[0],
        "sha256": sha256_bytes(output.encode("utf-8")),
    }


def sysctl_value(key: str) -> str:
    result = subprocess.run(["sysctl", "-n", key], capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else ""


def cpu_model() -> str:
    candidates: list[str] = []
    if sys.platform == "darwin":
        for key in ("machdep.cpu.brand_string", "hw.model"):
            value = sysctl_value(key)
            if value:
                candidates.append(value)
    elif sys.platform.startswith("linux"):
        try:
            for line in Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace").splitlines():
                if line.lower().startswith(("model name", "hardware")) and ":" in line:
                    candidates.append(line.split(":", 1)[1].strip())
                    break
        except OSError:
            pass
    elif os.name == "nt":
        candidates.append(os.environ.get("PROCESSOR_IDENTIFIER", ""))
    candidates.extend((platform.processor(), platform.machine()))
    return next((candidate for candidate in candidates if candidate), "unknown")


def cpu_signature() -> str:
    values: list[str] = []
    if sys.platform == "darwin":
        for key in ("machdep.cpu.family", "machdep.cpu.model", "machdep.cpu.stepping", "hw.cpufamily", "hw.cpusubtype"):
            value = sysctl_value(key)
            if value:
                values.append(f"{key}={value}")
    elif sys.platform.startswith("linux"):
        wanted = {"cpu family", "model", "stepping"}
        try:
            for line in Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace").splitlines():
                if ":" not in line:
                    continue
                key, value = (part.strip() for part in line.split(":", 1))
                if key.lower() in wanted:
                    values.append(f"{key.lower()}={value}")
                if len(values) == len(wanted):
                    break
        except OSError:
            pass
    elif os.name == "nt":
        for key in ("PROCESSOR_ARCHITECTURE", "PROCESSOR_LEVEL", "PROCESSOR_REVISION"):
            value = os.environ.get(key, "")
            if value:
                values.append(f"{key.lower()}={value}")
    return ";".join(values) or f"machine={platform.machine()}"


def ram_mib() -> int:
    if sys.platform == "darwin":
        value = sysctl_value("hw.memsize")
        if value.isdigit():
            return max(1, int(value) // (1024 * 1024))
    if os.name == "nt":
        import ctypes

        class MemoryStatus(ctypes.Structure):
            _fields_ = [
                ("length", ctypes.c_ulong),
                ("memory_load", ctypes.c_ulong),
                ("total_physical", ctypes.c_ulonglong),
                ("available_physical", ctypes.c_ulonglong),
                ("total_page_file", ctypes.c_ulonglong),
                ("available_page_file", ctypes.c_ulonglong),
                ("total_virtual", ctypes.c_ulonglong),
                ("available_virtual", ctypes.c_ulonglong),
                ("available_extended_virtual", ctypes.c_ulonglong),
            ]

        status = MemoryStatus()
        status.length = ctypes.sizeof(status)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return max(1, status.total_physical // (1024 * 1024))
    try:
        pages = os.sysconf("SC_PHYS_PAGES")
        page_size = os.sysconf("SC_PAGE_SIZE")
        return max(1, pages * page_size // (1024 * 1024))
    except (AttributeError, OSError, ValueError):
        return 0


def nested_get(value: Mapping[str, Any], path: str) -> Any:
    current: Any = value
    for component in path.split("."):
        if not isinstance(current, Mapping) or component not in current:
            return None
        current = current[component]
    return current


def initialize(args: argparse.Namespace) -> int:
    root = Path(args.root)
    identity_path = root / "identity.json"
    if identity_path.exists():
        raise ObservationError(f"identity already exists: {identity_path}")
    commit = git_output("rev-parse", "--verify", "HEAD^{commit}")
    tree = git_output("rev-parse", "--verify", "HEAD^{tree}")
    identity = {
        "schema": IDENTITY_SCHEMA,
        "source": {
            "repository": os.environ.get("GITHUB_REPOSITORY", "local"),
            "commit": commit,
            "tree": tree,
            "ref": os.environ.get("GITHUB_REF", "local"),
        },
        "workflow": {
            "name": os.environ.get("GITHUB_WORKFLOW", "local"),
            "workflow_ref": os.environ.get("GITHUB_WORKFLOW_REF", "local"),
            "workflow_sha": os.environ.get("GITHUB_WORKFLOW_SHA", commit),
            "run_id": os.environ.get("GITHUB_RUN_ID", "local"),
            "run_number": os.environ.get("GITHUB_RUN_NUMBER", "0"),
            "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT", "1"),
            "job_key": os.environ.get("GITHUB_JOB", "native"),
            "job_name": args.job_name,
            "event": os.environ.get("GITHUB_EVENT_NAME", "local"),
        },
        "runner": {
            "requested_label": args.runner_label,
            "name": os.environ.get("RUNNER_NAME", "local"),
            "environment": os.environ.get("RUNNER_ENVIRONMENT", "local"),
            "os": os.environ.get("RUNNER_OS", platform.system()),
            "arch": os.environ.get("RUNNER_ARCH", platform.machine()),
            "image_os": os.environ.get("ImageOS", "unknown"),
            "image_version": os.environ.get("ImageVersion", "unknown"),
            "platform": args.platform,
            "matrix_os": args.os,
            "matrix_arch": args.arch,
            "cpu_model": cpu_model(),
            "cpu_signature": cpu_signature(),
            "machine": platform.machine(),
            "cores": os.cpu_count() or 0,
            "ram_mib": ram_mib(),
            "job_log_identity": {
                "required_for_comparison": True,
                "provisioner_version": None,
                "azure_region": None,
                "source": "github-job-log-preamble",
            },
        },
        "configuration": {
            "name": args.configuration,
            "compiler": args.compiler,
            "linker": args.linker,
            "debug_info": args.debug_info,
            "differential_workers": args.differential_workers,
            "cmake_build_parallel_level": os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", ""),
            "ninja_flags": os.environ.get("NINJAFLAGS", ""),
        },
        "created_utc": utc_now(),
    }
    atomic_write(identity_path, canonical_bytes(identity))
    identity_sha = sha256_bytes(canonical_bytes(identity))
    run_calibration(root, identity_sha, "calibration_cpu", 1, calibration_cpu)
    run_calibration(root, identity_sha, "calibration_filesystem", 2, lambda: calibration_filesystem(root))
    print(f"NATIVE_OBSERVATION_INIT identity_sha256={identity_sha} root={root}")
    return 0


def calibration_cpu() -> None:
    block = bytes((index * 17 + 31) & 0xFF for index in range(64 * 1024))
    digest = hashlib.sha256()
    for _ in range(128):
        digest.update(block)
    if digest.hexdigest() != "0f952d6a523d40eb29f22d2838e6a689b23a966172b2e24f0f1c95771bceb534":
        # The expected value is checked to make the work fixed, not optimized away.
        raise ObservationError("CPU calibration digest changed")


def calibration_filesystem(root: Path) -> None:
    probe = root / "calibration.bin"
    payload = (b"buster-native-observation\0" * ((PROBE_BYTES // 26) + 1))[:PROBE_BYTES]
    with probe.open("wb") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())
    with probe.open("rb") as source:
        digest = hashlib.sha256(source.read()).hexdigest()
    probe.unlink()
    if digest != sha256_bytes(payload):
        raise ObservationError("filesystem calibration digest mismatch")


def run_calibration(root: Path, identity_sha: str, phase: str, sequence: int, callback: Any) -> None:
    started_utc = utc_now()
    started_ns = time.perf_counter_ns()
    status = "success"
    message = ""
    try:
        callback()
    except Exception as error:  # calibration failures must be retained
        status = "failed"
        message = str(error)
    ended_ns = time.perf_counter_ns()
    ended_utc = utc_now()
    record_phase(
        root,
        {
            "schema": PHASE_SCHEMA,
            "sequence": sequence,
            "phase": phase,
            "identity_sha256": identity_sha,
            "clock": CLOCK,
            "started_ns": started_ns,
            "ended_ns": ended_ns,
            "elapsed_ns": ended_ns - started_ns,
            "started_utc": started_utc,
            "ended_utc": ended_utc,
            "status": status,
            "exit_code": 0 if status == "success" else 1,
            "command_sha256": sha256_bytes(phase.encode("utf-8")),
            "observer_overhead_ns": 0,
            "message": message,
        },
    )
    if status != "success":
        raise ObservationError(message)


def record_phase(root: Path, record: dict[str, Any]) -> None:
    sequence = strict_integer(record.get("sequence"), where="phase.sequence")
    phase = strict_string(record.get("phase"), where="phase.phase")
    safe_phase = re.sub(r"[^a-z0-9_]+", "-", phase.lower())
    path = root / "phases" / f"{sequence:03d}-{safe_phase}.json"
    if path.exists():
        raise ObservationError(f"duplicate phase path: {path}")
    before_write = time.perf_counter_ns()
    record["observer_overhead_ns"] = 0
    payload = canonical_bytes(record)
    atomic_write(path, payload)
    overhead = time.perf_counter_ns() - before_write
    record["observer_overhead_ns"] = overhead
    atomic_write(path, canonical_bytes(record))
    print("NATIVE_PHASE_RECORD " + canonical_bytes(record).decode("utf-8").strip(), flush=True)


def write_toolchain(args: argparse.Namespace) -> int:
    root = Path(args.root)
    identity = validate_identity(load_json(root / "identity.json"))
    toolchain = {
        "schema": TOOLCHAIN_SCHEMA,
        "identity_sha256": sha256_bytes(canonical_bytes(identity)),
        "compiler": command_identity([args.compiler, "--version"]),
        "cmake": command_identity([args.cmake, "--version"]),
        "ninja": command_identity([args.ninja, "--version"]),
        "recorded_utc": utc_now(),
    }
    atomic_write(root / "toolchain.json", canonical_bytes(toolchain))
    print(
        "NATIVE_TOOLCHAIN "
        + " ".join(
            f"{name}={shlex.quote(toolchain[name]['first_line'])}" for name in ("compiler", "cmake", "ninja")
        )
    )
    return 0


def run_command(args: argparse.Namespace) -> int:
    if not args.command:
        raise ObservationError("run requires a command after --")
    root = Path(args.root)
    identity = validate_identity(load_json(root / "identity.json"))
    identity_sha = sha256_bytes(canonical_bytes(identity))
    command = list(args.command)
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        raise ObservationError("run requires a non-empty command")

    started_utc = utc_now()
    started_ns = time.perf_counter_ns()
    child: subprocess.Popen[Any] | None = None
    terminating_signal = 0

    def forward(signum: int, _frame: Any) -> None:
        nonlocal terminating_signal
        terminating_signal = signum
        if child is not None and child.poll() is None:
            try:
                if os.name == "nt":
                    child.send_signal(signal.CTRL_BREAK_EVENT)
                else:
                    os.killpg(child.pid, signum)
            except (OSError, ProcessLookupError):
                pass

    old_handlers: dict[int, Any] = {}
    for signum in (signal.SIGINT, signal.SIGTERM):
        old_handlers[signum] = signal.signal(signum, forward)
    creationflags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) if os.name == "nt" else 0
    try:
        child = subprocess.Popen(
            command,
            start_new_session=(os.name != "nt"),
            creationflags=creationflags,
        )
        returncode = child.wait()
    except OSError as error:
        returncode = 127
        print(f"error: cannot execute {command[0]}: {error}", file=sys.stderr)
    finally:
        for signum, handler in old_handlers.items():
            signal.signal(signum, handler)
    ended_ns = time.perf_counter_ns()
    ended_utc = utc_now()
    status = "cancelled" if terminating_signal else ("success" if returncode == 0 else "failed")
    record = {
        "schema": PHASE_SCHEMA,
        "sequence": args.sequence,
        "phase": args.phase,
        "identity_sha256": identity_sha,
        "clock": CLOCK,
        "started_ns": started_ns,
        "ended_ns": ended_ns,
        "elapsed_ns": ended_ns - started_ns,
        "started_utc": started_utc,
        "ended_utc": ended_utc,
        "status": status,
        "exit_code": returncode,
        "command_sha256": sha256_bytes(b"\0".join(item.encode("utf-8", errors="surrogateescape") for item in command)),
        "observer_overhead_ns": 0,
        "message": f"signal={terminating_signal}" if terminating_signal else "",
    }
    record_phase(root, record)
    return returncode if not terminating_signal else 128 + terminating_signal


def validate_identity(value: Any) -> dict[str, Any]:
    value = strict_object(
        value,
        where="identity",
        required=("schema", "source", "workflow", "runner", "configuration", "created_utc"),
    )
    if value["schema"] != IDENTITY_SCHEMA:
        raise ObservationError("unsupported identity schema")
    for path in REQUIRED_IDENTITY_PATHS:
        found = nested_get(value, path)
        if found is None or found == "":
            raise ObservationError(f"identity field is missing: {path}")
    strict_integer(nested_get(value, "runner.cores"), where="identity.runner.cores", minimum=1)
    strict_integer(nested_get(value, "runner.ram_mib"), where="identity.runner.ram_mib", minimum=1)
    strict_integer(
        nested_get(value, "configuration.differential_workers"),
        where="identity.configuration.differential_workers",
    )
    if os.environ.get("GITHUB_ACTIONS") == "true":
        for path in ("runner.image_os", "runner.image_version"):
            if nested_get(value, path) == "unknown":
                raise ObservationError(f"GitHub-hosted identity is unavailable: {path}")
    parse_utc(value["created_utc"], where="identity.created_utc")
    return value


def validate_toolchain(value: Any, identity_sha: str) -> dict[str, Any]:
    value = strict_object(
        value,
        where="toolchain",
        required=("schema", "identity_sha256", "compiler", "cmake", "ninja", "recorded_utc"),
    )
    if value["schema"] != TOOLCHAIN_SCHEMA or value["identity_sha256"] != identity_sha:
        raise ObservationError("toolchain identity binding mismatch")
    for name in ("compiler", "cmake", "ninja"):
        entry = strict_object(
            value[name], where=f"toolchain.{name}", required=("argv", "path", "first_line", "sha256")
        )
        strict_string(entry["path"], where=f"toolchain.{name}.path")
        strict_string(entry["first_line"], where=f"toolchain.{name}.first_line")
        digest = strict_string(entry["sha256"], where=f"toolchain.{name}.sha256")
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ObservationError(f"toolchain.{name}.sha256 is invalid")
    parse_utc(value["recorded_utc"], where="toolchain.recorded_utc")
    return value


def validate_phase(value: Any, identity_sha: str, source: Path) -> dict[str, Any]:
    value = strict_object(
        value,
        where=str(source),
        required=(
            "schema",
            "sequence",
            "phase",
            "identity_sha256",
            "clock",
            "started_ns",
            "ended_ns",
            "elapsed_ns",
            "started_utc",
            "ended_utc",
            "status",
            "exit_code",
            "command_sha256",
            "observer_overhead_ns",
            "message",
        ),
    )
    if value["schema"] != PHASE_SCHEMA or value["clock"] != CLOCK:
        raise ObservationError(f"unsupported phase schema/clock in {source}")
    if value["identity_sha256"] != identity_sha:
        raise ObservationError(f"identity mismatch in {source}")
    sequence = strict_integer(value["sequence"], where=f"{source}.sequence")
    phase = strict_string(value["phase"], where=f"{source}.phase")
    started = strict_integer(value["started_ns"], where=f"{source}.started_ns")
    ended = strict_integer(value["ended_ns"], where=f"{source}.ended_ns")
    elapsed = strict_integer(value["elapsed_ns"], where=f"{source}.elapsed_ns")
    if ended < started or elapsed != ended - started:
        raise ObservationError(f"non-monotonic or contradictory timing in {source}")
    started_utc = parse_utc(value["started_utc"], where=f"{source}.started_utc")
    ended_utc = parse_utc(value["ended_utc"], where=f"{source}.ended_utc")
    if ended_utc < started_utc:
        raise ObservationError(f"UTC timestamps run backwards in {source}")
    if value["status"] not in ("success", "failed", "cancelled"):
        raise ObservationError(f"invalid status in {source}")
    strict_integer(value["exit_code"], where=f"{source}.exit_code", minimum=-2**31)
    strict_integer(value["observer_overhead_ns"], where=f"{source}.observer_overhead_ns")
    command_digest = strict_string(value["command_sha256"], where=f"{source}.command_sha256")
    if not re.fullmatch(r"[0-9a-f]{64}", command_digest):
        raise ObservationError(f"invalid command digest in {source}")
    strict_string(value["message"], where=f"{source}.message", allow_empty=True)
    return {**value, "sequence": sequence, "phase": phase, "elapsed_ns": elapsed}


def load_phases(root: Path, identity_sha: str) -> list[dict[str, Any]]:
    phase_directory = root / "phases"
    if not phase_directory.is_dir():
        raise ObservationError(f"missing phase directory: {phase_directory}")
    paths = sorted(phase_directory.iterdir())
    if not paths:
        raise ObservationError("no phase records")
    phases: list[dict[str, Any]] = []
    names: set[str] = set()
    sequences: set[int] = set()
    for path in paths:
        if path.is_symlink() or not path.is_file() or path.suffix != ".json":
            raise ObservationError(f"unexpected phase entry: {path}")
        phase = validate_phase(load_json(path), identity_sha, path)
        if phase["phase"] in names:
            raise ObservationError(f"duplicate phase name: {phase['phase']}")
        if phase["sequence"] in sequences:
            raise ObservationError(f"duplicate phase sequence: {phase['sequence']}")
        names.add(phase["phase"])
        sequences.add(phase["sequence"])
        phases.append(phase)
    phases.sort(key=lambda item: item["sequence"])
    return phases


def artifact_manifest(directory: Path) -> list[dict[str, Any]]:
    required = ("native-ci-logs.tar.gz", "result.json", "summary.md")
    manifest = []
    for name in required:
        path = directory / name
        if path.is_symlink() or not path.is_file():
            raise ObservationError(f"upload handoff is missing regular file: {path}")
        digest = hashlib.sha256()
        size = 0
        with path.open("rb") as source:
            while True:
                block = source.read(1024 * 1024)
                if not block:
                    break
                size += len(block)
                digest.update(block)
        manifest.append({"name": name, "bytes": size, "sha256": digest.hexdigest()})
    return manifest


def finalize(args: argparse.Namespace) -> int:
    root = Path(args.root)
    artifact_directory = Path(args.artifact_directory)
    expect_complete = args.expect_complete == "1"
    identity = validate_identity(load_json(root / "identity.json"))
    identity_sha = sha256_bytes(canonical_bytes(identity))
    toolchain: dict[str, Any] | None = None
    toolchain_error = ""
    try:
        toolchain = validate_toolchain(load_json(root / "toolchain.json"), identity_sha)
    except ObservationError as error:
        if expect_complete:
            raise
        toolchain_error = str(error)

    # The handoff is the process-local validation and digesting of the exact
    # directory passed to upload-artifact. The action's network duration remains
    # separate Actions metadata and is never represented as monotonic time.
    started_utc = utc_now()
    started_ns = time.perf_counter_ns()
    manifest: list[dict[str, Any]] = []
    handoff_error = ""
    try:
        manifest = artifact_manifest(artifact_directory)
    except ObservationError as error:
        if expect_complete:
            raise
        handoff_error = str(error)
    ended_ns = time.perf_counter_ns()
    ended_utc = utc_now()
    record_phase(
        root,
        {
            "schema": PHASE_SCHEMA,
            "sequence": args.handoff_sequence,
            "phase": "upload_handoff",
            "identity_sha256": identity_sha,
            "clock": CLOCK,
            "started_ns": started_ns,
            "ended_ns": ended_ns,
            "elapsed_ns": ended_ns - started_ns,
            "started_utc": started_utc,
            "ended_utc": ended_utc,
            "status": "failed" if handoff_error else "success",
            "exit_code": 1 if handoff_error else 0,
            "command_sha256": sha256_bytes(canonical_bytes(manifest)),
            "observer_overhead_ns": 0,
            "message": handoff_error or "validated upload-artifact input",
        },
    )
    phases = load_phases(root, identity_sha)
    names = {phase["phase"] for phase in phases}
    required = set(args.required_phase)
    missing = sorted(required - names)
    unsuccessful = sorted(
        phase["phase"]
        for phase in phases
        if phase["phase"] in required and phase["status"] != "success"
    )
    if expect_complete and (missing or unsuccessful):
        details = [
            f"missing={','.join(missing)}" if missing else "",
            f"unsuccessful={','.join(unsuccessful)}" if unsuccessful else "",
        ]
        raise ObservationError(
            "successful native payload has incomplete timing evidence: "
            + "; ".join(filter(None, details))
        )
    complete = not missing and not unsuccessful
    observer_overhead_ns = sum(phase["observer_overhead_ns"] for phase in phases)
    observation = {
        "schema": OBSERVATION_SCHEMA,
        "identity": identity,
        "toolchain": toolchain,
        "toolchain_error": toolchain_error,
        "identity_sha256": identity_sha,
        "complete": complete,
        "correctness_outcome": "success" if expect_complete else "non-success",
        "comparison_eligible": False,
        "comparison_ineligible_reasons": [
            "job-log provisioner/region identity has not been joined",
            *(["toolchain evidence is incomplete: " + toolchain_error] if toolchain_error else []),
            *(["upload handoff is incomplete: " + handoff_error] if handoff_error else []),
        ],
        "required_phases": sorted(required),
        "missing_phases": missing,
        "unsuccessful_phases": unsuccessful,
        "phases": phases,
        "upload_handoff": {
            "files": manifest,
            "error": handoff_error,
        },
        "observer_overhead_ns": observer_overhead_ns,
        "observer_overhead_method": "sum of measured durable first-write costs for phase records",
        "finalized_utc": utc_now(),
    }
    payload = canonical_bytes(observation)
    root_output = root / "native-observation.json"
    artifact_output = artifact_directory / "native-observation.json"
    atomic_write(root_output, payload)
    if artifact_output != root_output:
        atomic_write(artifact_output, payload)
    print(
        f"NATIVE_OBSERVATION complete={str(complete).lower()} outcome={observation['correctness_outcome']} "
        f"phases={len(phases)} overhead_us={observer_overhead_ns / 1000:.3f} output={artifact_output}"
    )
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as destination:
            destination.write("\n## Native phase observation\n\n")
            destination.write(
                f"Complete timing evidence: **{str(complete).lower()}**. "
                f"Observer overhead: **{observer_overhead_ns / 1_000_000:.3f} ms**.\n\n"
            )
            destination.write("| Phase | Status | Duration |\n|---|---:|---:|\n")
            for phase in phases:
                destination.write(
                    f"| `{phase['phase']}` | {phase['status']} | "
                    f"{phase['elapsed_ns'] / 1_000_000:.3f} ms |\n"
                )
    return 0

def parse_job_log_preamble(log_text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    group = ""
    for raw_line in log_text.splitlines():
        message = re.sub(r"^\ufeff?\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d+Z\s+", "", raw_line)
        if message.startswith("Current runner version: '") and message.endswith("'"):
            result["runner_version"] = message.split("'", 2)[1]
            continue
        if message == "##[group]Runner Image Provisioner":
            group = "provisioner"
            continue
        if message == "##[group]Runner Image":
            group = "image"
            continue
        if message == "##[endgroup]":
            group = ""
            continue
        if group == "provisioner":
            if message.startswith("Version: "):
                result["provisioner_version"] = message.removeprefix("Version: ").strip()
            elif message.startswith("Worker ID: "):
                result["worker_id"] = message.removeprefix("Worker ID: ").strip()
            elif message.startswith("Azure Region: "):
                result["azure_region"] = message.removeprefix("Azure Region: ").strip()
        elif group == "image":
            if message.startswith("Image: "):
                result["image_os"] = message.removeprefix("Image: ").strip()
            elif message.startswith("Version: "):
                result["image_version"] = message.removeprefix("Version: ").strip()
    required = ("runner_version", "provisioner_version", "azure_region", "image_os", "image_version")
    missing = [name for name in required if not result.get(name)]
    if missing:
        raise ObservationError("job log preamble is missing: " + ", ".join(missing))
    result.setdefault("worker_id", "unknown")
    return result


def normalize_image(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.lower())

LOG_TIMESTAMP = re.compile(r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d+Z)", re.M)


def enrich(args: argparse.Namespace) -> int:
    observation = load_json(Path(args.observation))
    if not isinstance(observation, dict) or observation.get("schema") != OBSERVATION_SCHEMA:
        raise ObservationError("unsupported observation input")
    try:
        log_text = Path(args.job_log).read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as error:
        raise ObservationError(f"cannot read job log: {error}") from error
    parsed = parse_job_log_preamble(log_text)
    timestamps = [parse_utc(value, where="job log timestamp") for value in LOG_TIMESTAMP.findall(log_text)]
    if not timestamps:
        raise ObservationError("job log has no timestamps")
    backwards = any(right < left for left, right in zip(timestamps, timestamps[1:]))
    clock = {
        "first_log_utc": timestamps[0].isoformat().replace("+00:00", "Z"),
        "last_log_utc": timestamps[-1].isoformat().replace("+00:00", "Z"),
        "log_timestamps_non_monotonic": backwards,
        "api_started_utc": None,
        "api_completed_utc": None,
        "api_log_disagreement": False,
    }
    metadata = load_json(Path(args.job_metadata))
    if not isinstance(metadata, dict):
        raise ObservationError("job metadata must be an object")
    job_id = metadata.get("id")
    api_started = parse_utc(metadata.get("started_at"), where="job.started_at")
    api_completed = parse_utc(metadata.get("completed_at"), where="job.completed_at")
    clock["api_started_utc"] = api_started.isoformat().replace("+00:00", "Z")
    clock["api_completed_utc"] = api_completed.isoformat().replace("+00:00", "Z")
    clock["api_log_disagreement"] = (
        api_completed < api_started
        or timestamps[-1] < api_started
        or timestamps[0] > api_completed
        or abs((timestamps[0] - api_started).total_seconds()) > args.clock_tolerance_seconds
        or abs((timestamps[-1] - api_completed).total_seconds()) > args.clock_tolerance_seconds
    )
    identity = observation["identity"]
    reasons: list[str] = []
    expected_run_id = str(identity["workflow"]["run_id"])
    expected_attempt = str(identity["workflow"]["run_attempt"])
    if str(metadata.get("run_id")) != expected_run_id:
        reasons.append("Actions job run id contradicts artifact identity")
    if str(metadata.get("run_attempt")) != expected_attempt:
        reasons.append("Actions job attempt contradicts artifact identity")
    if metadata.get("name") != identity["workflow"]["job_name"]:
        reasons.append("Actions job name contradicts artifact identity")
    if normalize_image(parsed["image_os"]) != normalize_image(identity["runner"]["image_os"]):
        reasons.append("job-log runner image contradicts artifact identity")
    if parsed["image_version"] != identity["runner"]["image_version"]:
        reasons.append("job-log image version contradicts artifact identity")
    if backwards:
        reasons.append("job-log timestamps are non-monotonic")
    if clock["api_log_disagreement"]:
        reasons.append("Actions API and job-log clocks disagree")
    if not isinstance(observation.get("toolchain"), dict):
        reasons.append("toolchain evidence is incomplete")
    enriched = {
        "schema": ENRICHED_SCHEMA,
        "observation": observation,
        "runner_log": {
            **parsed,
            "job_id": job_id,
        },
        "job_api": {
            "id": job_id,
            "run_id": metadata.get("run_id"),
            "run_attempt": metadata.get("run_attempt"),
            "name": metadata.get("name"),
            "status": metadata.get("status"),
            "conclusion": metadata.get("conclusion"),
            "started_at": metadata.get("started_at"),
            "completed_at": metadata.get("completed_at"),
        },
        "clock_evidence": clock,
        "comparison_eligible": observation.get("complete") is True
        and observation.get("correctness_outcome") == "success"
        and not reasons,
        "comparison_ineligible_reasons": reasons,
    }
    atomic_write(Path(args.output), canonical_bytes(enriched))
    print(
        f"NATIVE_OBSERVATION_ENRICHED eligible={str(enriched['comparison_eligible']).lower()} "
        f"provisioner={parsed['provisioner_version']} region={parsed['azure_region']} output={args.output}"
    )
    return 0


def comparable_identity(enriched: Mapping[str, Any]) -> dict[str, Any]:
    if enriched.get("schema") != ENRICHED_SCHEMA:
        raise ObservationError("unsupported enriched observation")
    if enriched.get("comparison_eligible") is not True:
        reasons = enriched.get("comparison_ineligible_reasons") or ["unknown reason"]
        raise ObservationError("observation is not comparison eligible: " + "; ".join(map(str, reasons)))
    observation = enriched.get("observation")
    if not isinstance(observation, Mapping):
        raise ObservationError("enriched observation is missing observation")
    toolchain = observation.get("toolchain")
    if not isinstance(toolchain, Mapping):
        raise ObservationError("enriched observation is missing toolchain evidence")
    merged = dict(observation["identity"])
    merged["toolchain"] = toolchain
    merged["runner_log"] = enriched["runner_log"]
    result = {path: nested_get(merged, path) for path in MATCH_PATHS}
    missing = [path for path, value in result.items() if value is None]
    if missing:
        raise ObservationError("comparison identity is incomplete: " + ", ".join(missing))
    return result


def phase_records(enriched: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    observation = enriched.get("observation")
    if not isinstance(observation, Mapping):
        raise ObservationError("enriched observation is missing observation")
    result: dict[str, Mapping[str, Any]] = {}
    for phase in observation.get("phases", []):
        if not isinstance(phase, Mapping):
            raise ObservationError("observation phase is not an object")
        name = strict_string(phase.get("phase"), where="observation.phase.phase")
        if name in result:
            raise ObservationError(f"duplicate phase in observation: {name}")
        result[name] = phase
    return result


def phase_map(enriched: Mapping[str, Any]) -> dict[str, int]:
    result: dict[str, int] = {}
    for name, phase in phase_records(enriched).items():
        if phase.get("status") == "success":
            result[name] = strict_integer(
                phase.get("elapsed_ns"), where=f"observation.phase.{name}.elapsed_ns"
            )
    return result


def clock_invalid_reasons(enriched: Mapping[str, Any], label: str) -> list[str]:
    reasons: list[str] = []
    clock = enriched.get("clock_evidence")
    if isinstance(clock, Mapping):
        if clock.get("log_timestamps_non_monotonic") is True:
            reasons.append(f"{label}: job-log timestamps are non-monotonic")
        if clock.get("api_log_disagreement") is True:
            reasons.append(f"{label}: Actions API and job-log clocks disagree")
    for reason in enriched.get("comparison_ineligible_reasons") or []:
        text = str(reason)
        lowered = text.lower()
        if ("clock" in lowered or "timestamp" in lowered) and f"{label}: {text}" not in reasons:
            reasons.append(f"{label}: {text}")
    return reasons


def classification_document(
    classification: str,
    *,
    observation_count: int,
    reasons: Sequence[str] = (),
    identity: Mapping[str, Any] | None = None,
    slow_phases: Sequence[str] = (),
    reproduced_phases: Sequence[str] = (),
    signature: Sequence[str] = (),
    rows: Sequence[Mapping[str, Any]] = (),
) -> dict[str, Any]:
    return {
        "schema": CLASSIFICATION_SCHEMA,
        "classification": classification,
        "retained_observation_count": observation_count,
        "matched_observation_count": observation_count if identity is not None else 0,
        "matched_identity_sha256": sha256_bytes(canonical_bytes(identity)) if identity is not None else None,
        "threshold_ratio": SLOW_RATIO,
        "minimum_broad_phases": MINIMUM_BROAD_PHASES,
        "classification_reasons": list(reasons),
        "slow_phases": sorted(slow_phases),
        "reproduced_slowdown_phases": sorted(reproduced_phases),
        "slowdown_signature": sorted(signature),
        "phase_comparisons": list(rows),
    }


def classify_observations(
    target: Mapping[str, Any],
    comparisons: Sequence[Mapping[str, Any]],
    slowdown_phases: Sequence[str] = (),
) -> dict[str, Any]:
    observations = [target, *comparisons]
    for index, enriched in enumerate(observations):
        if enriched.get("schema") != ENRICHED_SCHEMA:
            raise ObservationError(f"observation {index} has an unsupported enriched schema")

    timestamp_reasons: list[str] = []
    for index, enriched in enumerate(observations):
        label = "target" if index == 0 else f"comparison {index}"
        timestamp_reasons.extend(clock_invalid_reasons(enriched, label))
    if timestamp_reasons:
        return classification_document(
            "timestamp-invalid",
            observation_count=len(observations),
            reasons=timestamp_reasons,
            signature=slowdown_phases,
        )

    if len(comparisons) < 2:
        return classification_document(
            "mixed/inconclusive",
            observation_count=len(observations),
            reasons=("fewer than two comparator observations were retained",),
            signature=slowdown_phases,
        )

    ineligible: list[str] = []
    for index, enriched in enumerate(observations):
        if enriched.get("comparison_eligible") is not True:
            label = "target" if index == 0 else f"comparison {index}"
            reasons = enriched.get("comparison_ineligible_reasons") or ["unknown reason"]
            ineligible.extend(f"{label}: {reason}" for reason in reasons)
    if ineligible:
        return classification_document(
            "mixed/inconclusive",
            observation_count=len(observations),
            reasons=ineligible,
            signature=slowdown_phases,
        )

    try:
        identity = comparable_identity(target)
    except ObservationError as error:
        return classification_document(
            "mixed/inconclusive",
            observation_count=len(observations),
            reasons=(f"target identity is incomplete: {error}",),
            signature=slowdown_phases,
        )
    identity_reasons: list[str] = []
    for index, comparison in enumerate(comparisons, 1):
        try:
            other = comparable_identity(comparison)
        except ObservationError as error:
            identity_reasons.append(f"comparison {index} identity is incomplete: {error}")
            continue
        changed = [path for path in MATCH_PATHS if other[path] != identity[path]]
        if changed:
            identity_reasons.append(f"comparison {index} identity mismatch: {', '.join(changed)}")
    if identity_reasons:
        return classification_document(
            "mixed/inconclusive",
            observation_count=len(observations),
            reasons=identity_reasons,
            signature=slowdown_phases,
        )

    target_phases = phase_map(target)
    comparison_phases = [phase_map(item) for item in comparisons]
    common = set(target_phases)
    for phases in comparison_phases:
        common &= set(phases)
    missing_required = sorted((SETUP_PHASES | {CORPUS_PHASE}) - common)
    if missing_required:
        return classification_document(
            "mixed/inconclusive",
            observation_count=len(observations),
            reasons=("matched phase set is incomplete: " + ", ".join(missing_required),),
            identity=identity,
            signature=slowdown_phases,
        )

    records = [phase_records(item) for item in observations]
    conflicts: list[str] = []
    for phase in sorted(common):
        command_digests = {
            str(record[phase].get("command_sha256"))
            for record in records
            if record[phase].get("command_sha256") is not None
        }
        sequences = {
            int(record[phase]["sequence"])
            for record in records
            if record[phase].get("sequence") is not None
        }
        if len(command_digests) > 1:
            conflicts.append(f"{phase}: command identity differs")
        if len(sequences) > 1:
            conflicts.append(f"{phase}: sequence differs")
    if conflicts:
        return classification_document(
            "mixed/inconclusive",
            observation_count=len(observations),
            reasons=conflicts,
            identity=identity,
            signature=slowdown_phases,
        )

    signature = set(slowdown_phases)
    if signature:
        if len(signature) < MINIMUM_BROAD_PHASES:
            raise ObservationError("slowdown signature must contain at least four independent phases")
        if not signature & SETUP_PHASES or CORPUS_PHASE not in signature:
            raise ObservationError("slowdown signature must include setup/build work and differential_corpus")
        missing_signature = sorted(signature - common)
        if missing_signature:
            return classification_document(
                "mixed/inconclusive",
                observation_count=len(observations),
                reasons=("slowdown signature is missing matched phases: " + ", ".join(missing_signature),),
                identity=identity,
                signature=signature,
            )

    rows: list[dict[str, Any]] = []
    slow: set[str] = set()
    reproduced: set[str] = set()
    for phase in sorted(common):
        comparator_values = [phases[phase] for phases in comparison_phases]
        baseline = statistics.median(comparator_values)
        if baseline <= 0 or target_phases[phase] <= 0:
            raise ObservationError(f"non-positive duration for {phase}")
        ratio = target_phases[phase] / baseline
        if ratio >= SLOW_RATIO:
            slow.add(phase)
        comparator_ratios = [value / target_phases[phase] for value in comparator_values]
        reproduced_here = all(1.0 / SLOW_RATIO <= value <= SLOW_RATIO for value in comparator_ratios)
        if reproduced_here:
            reproduced.add(phase)
        rows.append(
            {
                "phase": phase,
                "target_ns": target_phases[phase],
                "comparator_ns": comparator_values,
                "comparator_median_ns": baseline,
                "target_to_median_ratio": round(ratio, 6),
                "comparator_to_target_ratios": [round(value, 6) for value in comparator_ratios],
                "slow": ratio >= SLOW_RATIO,
                "reproduced": reproduced_here,
            }
        )

    broad = (
        len(slow) >= MINIMUM_BROAD_PHASES
        and bool(slow & SETUP_PHASES)
        and CORPUS_PHASE in slow
    )
    if broad:
        classification = "hosted-runner-wide"
        reasons = (
            f"target alone met the broad rule in {len(slow)} phases against the comparator median",
        )
    elif signature and signature <= reproduced:
        classification = "source-specific"
        reasons = ("both matched reruns reproduced every predeclared slowdown-signature phase",)
    else:
        classification = "mixed/inconclusive"
        reasons = (
            "target did not meet the broad hosted-runner rule and matched reruns did not reproduce the complete predeclared slowdown signature",
        )
    return classification_document(
        classification,
        observation_count=len(observations),
        reasons=reasons,
        identity=identity,
        slow_phases=slow,
        reproduced_phases=reproduced & signature if signature else (),
        signature=signature,
        rows=rows,
    )


def classify_command(args: argparse.Namespace) -> int:
    target = load_json(Path(args.target))
    comparisons = [load_json(Path(path)) for path in args.comparison]
    result = classify_observations(target, comparisons, args.slowdown_phase)
    atomic_write(Path(args.output), canonical_bytes(result))
    print(
        f"NATIVE_CLASSIFICATION result={result['classification']} "
        f"slow_phases={','.join(result['slow_phases']) or 'none'} output={args.output}"
    )
    return 0

def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subparsers = result.add_subparsers(dest="action", required=True)

    initialize_parser = subparsers.add_parser("init", help="record immutable job identity and calibration")
    initialize_parser.add_argument("--root", required=True)
    initialize_parser.add_argument("--job-name", required=True)
    initialize_parser.add_argument("--runner-label", required=True)
    initialize_parser.add_argument("--platform", required=True)
    initialize_parser.add_argument("--os", required=True)
    initialize_parser.add_argument("--arch", required=True)
    initialize_parser.add_argument("--configuration", default="Release")
    initialize_parser.add_argument("--compiler", default="clang")
    initialize_parser.add_argument("--linker", default="DEFAULT")
    initialize_parser.add_argument("--debug-info", default="OFF")
    initialize_parser.add_argument("--differential-workers", type=int, default=0)
    initialize_parser.set_defaults(function=initialize)

    toolchain_parser = subparsers.add_parser("toolchain", help="record exact compiler/CMake/Ninja identities")
    toolchain_parser.add_argument("--root", required=True)
    toolchain_parser.add_argument("--compiler", default="clang")
    toolchain_parser.add_argument("--cmake", default="cmake")
    toolchain_parser.add_argument("--ninja", default="ninja")
    toolchain_parser.set_defaults(function=write_toolchain)

    run_parser = subparsers.add_parser("run", help="run one command and retain its monotonic phase record")
    run_parser.add_argument("--root", required=True)
    run_parser.add_argument("--phase", required=True)
    run_parser.add_argument("--sequence", required=True, type=int)
    run_parser.add_argument("command", nargs=argparse.REMAINDER)
    run_parser.set_defaults(function=run_command)

    finalize_parser = subparsers.add_parser("finalize", help="validate phases and publish adjacent artifact evidence")
    finalize_parser.add_argument("--root", required=True)
    finalize_parser.add_argument("--artifact-directory", required=True)
    finalize_parser.add_argument("--expect-complete", choices=("0", "1"), required=True)
    finalize_parser.add_argument("--required-phase", action="append", required=True)
    finalize_parser.add_argument("--handoff-sequence", type=int, default=90)
    finalize_parser.set_defaults(function=finalize)

    enrich_parser = subparsers.add_parser("enrich", help="join immutable job-log/API identity for an audit")
    enrich_parser.add_argument("--observation", required=True)
    enrich_parser.add_argument("--job-log", required=True)
    enrich_parser.add_argument("--job-metadata", required=True)
    enrich_parser.add_argument("--clock-tolerance-seconds", type=float, default=10.0)
    enrich_parser.add_argument("--output", required=True)
    enrich_parser.set_defaults(function=enrich)

    classify_parser = subparsers.add_parser("classify", help="classify one target against two or more exact matches")
    classify_parser.add_argument("--target", required=True)
    classify_parser.add_argument("--comparison", action="append", required=True)
    classify_parser.add_argument(
        "--slowdown-phase",
        action="append",
        default=[],
        help="one phase in a predeclared broad slowdown signature",
    )
    classify_parser.add_argument("--output", required=True)
    classify_parser.set_defaults(function=classify_command)
    return result


def main() -> int:
    arguments = parser().parse_args()
    try:
        return int(arguments.function(arguments))
    except ObservationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
