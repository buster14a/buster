"""Shared model-specific Zen 5 qualification primitives."""

from __future__ import annotations

from decimal import Decimal, InvalidOperation
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
from typing import Any

RESULT_SCHEMA = "buster-zen5-host-qualification-v1"
MANIFEST_SCHEMA = "buster-zen5-pmu-events-v1"
MANIFEST_NAME = "zen5_pmu_events_v1.json"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
INTEGER_RE = re.compile(r"^[0-9]+$")
MAX_CAPTURE_BYTES = 8 * 1024 * 1024
MAX_FILE_BYTES = 256 * 1024 * 1024


class QualificationError(ValueError):
    """A deterministic input, capture, or replay failure."""


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            size += len(chunk)
            if size > MAX_FILE_BYTES:
                raise QualificationError(f"file exceeds {MAX_FILE_BYTES} bytes: {path}")
            digest.update(chunk)
    return digest.hexdigest()


def atomic_write_json(path: Path, value: Any) -> None:
    path = path.resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = canonical_bytes(value)
    if len(payload) > MAX_CAPTURE_BYTES:
        raise QualificationError(f"result exceeds {MAX_CAPTURE_BYTES} bytes")
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    with temporary.open("xb") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def read_bounded_text(path: Path, maximum: int = 1024 * 1024) -> str | None:
    try:
        with path.open("rb") as source:
            value = source.read(maximum + 1)
    except (FileNotFoundError, PermissionError, OSError):
        return None
    if len(value) > maximum:
        raise QualificationError(f"text input exceeds {maximum} bytes: {path}")
    return value.decode("utf-8", errors="strict").strip()


def run_binary(arguments: list[str], cwd: Path | None = None) -> tuple[int, bytes, bytes]:
    result = subprocess.run(
        arguments,
        cwd=str(cwd) if cwd else None,
        check=False,
        text=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if len(result.stdout) > MAX_CAPTURE_BYTES or len(result.stderr) > MAX_CAPTURE_BYTES:
        raise QualificationError(f"command output exceeds {MAX_CAPTURE_BYTES} bytes: {arguments[0]}")
    return result.returncode, result.stdout, result.stderr


def run_text(arguments: list[str], cwd: Path | None = None) -> tuple[int, str, str]:
    status, stdout, stderr = run_binary(arguments, cwd)
    return (
        status,
        stdout.decode("utf-8", errors="replace"),
        stderr.decode("utf-8", errors="replace"),
    )


def load_json(path: Path, maximum: int = MAX_CAPTURE_BYTES) -> Any:
    raw = path.read_bytes()
    if len(raw) > maximum:
        raise QualificationError(f"JSON exceeds {maximum} bytes: {path}")
    try:
        return json.loads(raw)
    except json.JSONDecodeError as error:
        raise QualificationError(f"invalid JSON in {path}: {error}") from error


def manifest_path() -> Path:
    return Path(__file__).resolve().with_name(MANIFEST_NAME)


def normalize_event_name(value: str) -> str:
    result = value.strip().lower().replace("-", "_")
    if result.endswith(":u"):
        result = result[:-2]
    return result


def validate_manifest(value: Any) -> list[str]:
    problems: list[str] = []
    if not isinstance(value, dict) or value.get("schema") != MANIFEST_SCHEMA:
        return [f"manifest schema must be {MANIFEST_SCHEMA}"]
    if value.get("version") != 1:
        problems.append("manifest version must be 1")
    cpu = value.get("cpu")
    if not isinstance(cpu, dict):
        problems.append("manifest cpu must be an object")
    elif (cpu.get("vendor_id"), cpu.get("family"), cpu.get("model")) != ("AuthenticAMD", 26, 68):
        problems.append("manifest CPU must be AuthenticAMD family 26 model 68")
    repeats = value.get("repeats")
    if not isinstance(repeats, int) or isinstance(repeats, bool) or repeats != 3:
        problems.append("manifest repeats must be exactly 3")
    fraction = value.get("minimum_running_fraction")
    if not isinstance(fraction, (int, float)) or isinstance(fraction, bool) or not 0.0 < float(fraction) <= 1.0:
        problems.append("manifest minimum_running_fraction must be in (0, 1]")

    group_ids: set[str] = set()
    for group in value.get("groups", []):
        if not isinstance(group, dict):
            problems.append("manifest group must be an object")
            continue
        group_id = group.get("id")
        if not isinstance(group_id, str) or not group_id or group_id in group_ids:
            problems.append(f"invalid or duplicate group id: {group_id!r}")
            continue
        group_ids.add(group_id)
        keys: set[str] = set()
        for event in group.get("events", []):
            if not isinstance(event, dict):
                problems.append(f"{group_id}: event must be an object")
                continue
            key = event.get("key")
            if not isinstance(key, str) or not key or key in keys:
                problems.append(f"{group_id}: invalid or duplicate event key: {key!r}")
                continue
            keys.add(key)
            selector = event.get("selector")
            if not isinstance(selector, str) or not selector or any(character in selector for character in "\r\n\0"):
                problems.append(f"{group_id}/{key}: invalid selector")
            config = event.get("config_hex")
            try:
                parsed_config = int(config, 0)
            except (TypeError, ValueError):
                problems.append(f"{group_id}/{key}: invalid config_hex")
                continue
            if event.get("perf_type") == 4:
                try:
                    event_code = int(event["event_code_hex"], 0)
                    umask = int(event["umask_hex"], 0)
                except (KeyError, TypeError, ValueError):
                    problems.append(f"{group_id}/{key}: raw event lacks event_code_hex/umask_hex")
                else:
                    expected = event_code | (umask << 8)
                    if expected != parsed_config:
                        problems.append(
                            f"{group_id}/{key}: config {config} does not encode event {event_code:#x} umask {umask:#x}"
                        )
            names = event.get("output_names")
            if not isinstance(names, list) or not names or not all(isinstance(name, str) and name for name in names):
                problems.append(f"{group_id}/{key}: output_names must be a nonempty string list")
    if group_ids != {"core-execution", "l2-hierarchy", "fill-sources", "data-tlb"}:
        problems.append("manifest must define the four version-1 counter groups")
    return problems


def load_manifest(path: Path | None = None) -> tuple[dict[str, Any], str]:
    selected = path or manifest_path()
    value = load_json(selected)
    problems = validate_manifest(value)
    if problems:
        raise QualificationError("invalid event manifest: " + "; ".join(problems))
    return value, sha256_file(selected)


def parse_cpuinfo(text: str, cpu: int) -> dict[str, str]:
    blocks = [block for block in text.split("\n\n") if block.strip()]
    selected: dict[str, str] | None = None
    for block in blocks:
        fields: dict[str, str] = {}
        for line in block.splitlines():
            if ":" in line:
                key, value = line.split(":", 1)
                fields[key.strip()] = value.strip()
        if fields.get("processor") == str(cpu):
            selected = fields
            break
    if selected is None:
        raise QualificationError(f"/proc/cpuinfo has no processor {cpu}")
    return selected


def parse_memtotal(text: str) -> int | None:
    for line in text.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0] == "MemTotal:" and fields[2] == "kB" and fields[1].isdigit():
            return int(fields[1]) * 1024
    return None


def collect_directory_values(path: Path) -> dict[str, str] | None:
    try:
        entries = sorted(path.iterdir(), key=lambda item: item.name)
    except (FileNotFoundError, PermissionError, OSError):
        return None
    result: dict[str, str] = {}
    for entry in entries:
        if entry.is_file() and not entry.is_symlink():
            value = read_bounded_text(entry, 64 * 1024)
            if value is not None:
                result[entry.name] = value
    return result


def hash_path_record(path: Path) -> dict[str, Any]:
    if path.is_symlink():
        raise QualificationError(f"identity input must not be a symbolic link: {path}")
    resolved = path.resolve(strict=True)
    status = resolved.stat()
    if not resolved.is_file() or resolved.is_symlink():
        raise QualificationError(f"identity input must be a regular file: {path}")
    return {
        "path": str(resolved),
        "size": status.st_size,
        "mode": status.st_mode & 0o7777,
        "sha256": sha256_file(resolved),
    }


def collect_git_identity(root: Path) -> tuple[dict[str, Any], list[str]]:
    problems: list[str] = []
    root = root.resolve(strict=True)
    result: dict[str, Any] = {"root": str(root)}
    for key, arguments in (
        ("revision", ["git", "rev-parse", "HEAD"]),
        ("tree", ["git", "rev-parse", "HEAD^{tree}"]),
        ("status", ["git", "status", "--porcelain=v1"]),
    ):
        status, stdout, stderr = run_text(arguments, cwd=root)
        if status != 0:
            problems.append(f"git {key} failed: {stderr.strip() or status}")
            result[key] = None
        else:
            result[key] = stdout.strip()
    if result.get("revision") is not None and not COMMIT_RE.fullmatch(result["revision"]):
        problems.append("repository revision is not a full commit")
    if result.get("tree") is not None and not COMMIT_RE.fullmatch(result["tree"]):
        problems.append("repository tree is not a full object id")
    if result.get("status"):
        problems.append("repository checkout is not clean")
    return result, problems


def collect_ibs(root: Path, manifest: dict[str, Any]) -> list[dict[str, Any]]:
    observations: list[dict[str, Any]] = []
    for item in manifest.get("optional_attribution", []):
        pmu = item["sysfs_pmu"]
        base = root / "sys/bus/event_source/devices" / pmu
        type_text = read_bounded_text(base / "type")
        try:
            pmu_type = int(type_text) if type_text is not None else None
        except ValueError:
            pmu_type = None
        observations.append(
            {
                "id": item["id"],
                "required": False,
                "available": base.is_dir() and pmu_type is not None,
                "pmu_type": pmu_type,
                "format": collect_directory_values(base / "format"),
                "caps": collect_directory_values(base / "caps"),
                "cpumask": read_bounded_text(base / "cpumask"),
                "measurement_status": "not-requested",
                "count": None,
                "running_fraction": None,
                "scope": item["scope"],
                "semantics": item["semantics"],
            }
        )
    return observations


def collect_host(cpu: int, root: Path = Path("/")) -> tuple[dict[str, Any], list[str]]:
    problems: list[str] = []
    cpuinfo_text = read_bounded_text(root / "proc/cpuinfo", 4 * 1024 * 1024)
    meminfo_text = read_bounded_text(root / "proc/meminfo", 1024 * 1024)
    if cpuinfo_text is None:
        raise QualificationError("/proc/cpuinfo is unavailable")
    fields = parse_cpuinfo(cpuinfo_text, cpu)
    try:
        family = int(fields.get("cpu family", ""))
        model = int(fields.get("model", ""))
        stepping = int(fields.get("stepping", ""))
    except ValueError as error:
        raise QualificationError("CPU family/model/stepping is malformed") from error

    perf_status, perf_stdout, perf_stderr = run_text(["perf", "--version"])
    perf_version = perf_stdout.strip() if perf_status == 0 else None
    if perf_version is None:
        problems.append(f"perf --version failed: {perf_stderr.strip() or perf_status}")

    release = platform.release() if root == Path("/") else read_bounded_text(root / "kernel-release")
    version = platform.version() if root == Path("/") else read_bounded_text(root / "kernel-version")
    machine = platform.machine() if root == Path("/") else read_bounded_text(root / "machine")
    hostname = platform.node() if root == Path("/") else read_bounded_text(root / "hostname")
    sys_cpu = root / f"sys/devices/system/cpu/cpu{cpu}"
    dmi = root / "sys/class/dmi/id"
    host = {
        "hostname": hostname,
        "selected_cpu": cpu,
        "cpu": {
            "vendor_id": fields.get("vendor_id"),
            "family": family,
            "model": model,
            "model_name": fields.get("model name"),
            "stepping": stepping,
            "microcode": fields.get("microcode"),
            "flags_sha256": sha256_bytes(fields.get("flags", "").encode("utf-8")),
        },
        "kernel": {"release": release, "version": version, "machine": machine},
        "boot_id": read_bounded_text(root / "proc/sys/kernel/random/boot_id"),
        "perf_version": perf_version,
        "perf_event_paranoid": read_bounded_text(root / "proc/sys/kernel/perf_event_paranoid"),
        "kptr_restrict": read_bounded_text(root / "proc/sys/kernel/kptr_restrict"),
        "topology": {
            "core_id": read_bounded_text(sys_cpu / "topology/core_id"),
            "physical_package_id": read_bounded_text(sys_cpu / "topology/physical_package_id"),
            "thread_siblings_list": read_bounded_text(sys_cpu / "topology/thread_siblings_list"),
            "smt_active": read_bounded_text(root / "sys/devices/system/cpu/smt/active"),
        },
        "power_policy": {
            "boost": read_bounded_text(root / "sys/devices/system/cpu/cpufreq/boost"),
            "scaling_driver": read_bounded_text(sys_cpu / "cpufreq/scaling_driver"),
            "scaling_governor": read_bounded_text(sys_cpu / "cpufreq/scaling_governor"),
            "energy_performance_preference": read_bounded_text(sys_cpu / "cpufreq/energy_performance_preference"),
            "scaling_min_freq": read_bounded_text(sys_cpu / "cpufreq/scaling_min_freq"),
            "scaling_max_freq": read_bounded_text(sys_cpu / "cpufreq/scaling_max_freq"),
        },
        "firmware": {
            "bios_vendor": read_bounded_text(dmi / "bios_vendor"),
            "bios_version": read_bounded_text(dmi / "bios_version"),
            "bios_date": read_bounded_text(dmi / "bios_date"),
            "board_name": read_bounded_text(dmi / "board_name"),
            "board_version": read_bounded_text(dmi / "board_version"),
            "product_name": read_bounded_text(dmi / "product_name"),
        },
        "memory": {
            "mem_total_bytes": parse_memtotal(meminfo_text or ""),
            "edac": collect_directory_values(root / "sys/devices/system/edac/mc"),
        },
        "transparent_hugepage": read_bounded_text(root / "sys/kernel/mm/transparent_hugepage/enabled"),
    }
    required_paths = (
        ("boot_id", host["boot_id"]),
        ("kernel.release", host["kernel"]["release"]),
        ("perf_version", host["perf_version"]),
        ("cpu.microcode", host["cpu"]["microcode"]),
        ("topology.thread_siblings_list", host["topology"]["thread_siblings_list"]),
        ("topology.smt_active", host["topology"]["smt_active"]),
        ("power_policy.boost", host["power_policy"]["boost"]),
        ("power_policy.scaling_driver", host["power_policy"]["scaling_driver"]),
        ("power_policy.scaling_governor", host["power_policy"]["scaling_governor"]),
        ("power_policy.energy_performance_preference", host["power_policy"]["energy_performance_preference"]),
        ("firmware.bios_version", host["firmware"]["bios_version"]),
        ("memory.mem_total_bytes", host["memory"]["mem_total_bytes"]),
    )
    for name, value in required_paths:
        if value is None:
            problems.append(f"required host fact unavailable: {name}")
    return host, problems


def environment_contract(host: dict[str, Any], external: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "cpu": host["cpu"],
        "kernel": host["kernel"],
        "perf_version": host["perf_version"],
        "perf_event_paranoid": host["perf_event_paranoid"],
        "topology": host["topology"],
        "power_policy": host["power_policy"],
        "firmware": host["firmware"],
        "memory": host["memory"],
        "transparent_hugepage": host["transparent_hugepage"],
        "external_environment_inputs": external,
    }


def validate_host(manifest: dict[str, Any], host: dict[str, Any]) -> list[str]:
    expected = manifest["cpu"]
    actual = host["cpu"]
    problems: list[str] = []
    for key in ("vendor_id", "family", "model"):
        if actual.get(key) != expected.get(key):
            problems.append(f"CPU {key} is {actual.get(key)!r}, expected {expected.get(key)!r}")
    if actual.get("model_name") != expected.get("model_name"):
        problems.append(f"CPU model_name is {actual.get('model_name')!r}, expected {expected.get('model_name')!r}")
    if host["kernel"].get("machine") != "x86_64":
        problems.append("host machine must be x86_64")
    return problems


def parse_number(value: Any) -> int | float | None:
    """Parse perf's nonnegative JSON number without losing integral counts.

    perf emits counter values and event runtimes as floating-point text even
    when the represented value is integral. Multiplexed scaled counts may
    retain a fractional part, so those remain floats while exact integers
    are converted without a binary floating-point round trip.
    """
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value if value >= 0 else None
    if isinstance(value, float):
        if not math.isfinite(value) or value < 0.0:
            return None
        return int(value) if value.is_integer() else value
    if isinstance(value, str):
        stripped = value.strip().replace(",", "")
        try:
            parsed = Decimal(stripped)
        except InvalidOperation:
            return None
        if not parsed.is_finite() or parsed < 0:
            return None
        if parsed == parsed.to_integral_value():
            return int(parsed)
        converted = float(parsed)
        return converted if math.isfinite(converted) else None
    return None


def parse_float(value: Any) -> float | None:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return float(value)
    if isinstance(value, str):
        try:
            return float(value.strip().replace(",", ""))
        except ValueError:
            return None
    return None


def parse_perf_json(text: str) -> list[dict[str, Any]]:
    stripped = text.strip()
    if not stripped:
        raise QualificationError("perf produced empty JSON")
    values: list[Any]
    try:
        decoded = json.loads(stripped)
    except json.JSONDecodeError:
        values = []
        for line_number, line in enumerate(stripped.splitlines(), 1):
            candidate = line.strip().rstrip(",")
            if not candidate:
                continue
            try:
                values.append(json.loads(candidate))
            except json.JSONDecodeError as error:
                raise QualificationError(f"invalid perf JSON line {line_number}: {error}") from error
    else:
        values = decoded if isinstance(decoded, list) else [decoded]
    if not values or not all(isinstance(value, dict) for value in values):
        raise QualificationError("perf JSON must contain one or more objects")
    return values


def classify_counter(entry: dict[str, Any]) -> dict[str, Any]:
    raw_value = entry.get("counter-value")
    text = str(raw_value).strip().lower() if raw_value is not None else ""
    if text in {"<not supported>", "not supported"}:
        status = "unsupported"
        count = None
    elif text in {"<not counted>", "not counted"}:
        status = "not-counted"
        count = None
    else:
        count = parse_number(raw_value)
        status = "counted" if count is not None else "malformed"
    runtime = parse_number(entry.get("event-runtime"))
    percent = parse_float(entry.get("pcnt-running"))
    fraction = percent / 100.0 if percent is not None else None
    return {
        "status": status,
        "count": count,
        "event_runtime_ns": runtime,
        "percent_running": percent,
        "running_fraction": fraction,
        "raw": entry,
    }


def map_perf_entries(group: dict[str, Any], entries: list[dict[str, Any]], minimum_fraction: float) -> tuple[dict[str, Any], list[str]]:
    problems: list[str] = []
    by_name: dict[str, dict[str, Any]] = {}
    for entry in entries:
        event_name = entry.get("event")
        if not isinstance(event_name, str) or not event_name:
            problems.append(f"{group['id']}: perf entry lacks event name")
            continue
        normalized = normalize_event_name(event_name)
        if normalized in by_name:
            problems.append(f"{group['id']}: duplicate perf event {event_name!r}")
        else:
            by_name[normalized] = entry

    observations: dict[str, Any] = {}
    used: set[str] = set()
    for event in group["events"]:
        matches = {
            normalize_event_name(name)
            for name in event["output_names"]
            if normalize_event_name(name) in by_name
        }
        if len(matches) != 1:
            problems.append(f"{group['id']}/{event['key']}: expected one perf row, found {len(matches)}")
            observations[event["key"]] = {
                "status": "missing",
                "count": None,
                "event_runtime_ns": None,
                "percent_running": None,
                "running_fraction": None,
                "raw": None,
            }
            continue
        name = next(iter(matches))
        used.add(name)
        observation = classify_counter(by_name[name])
        observations[event["key"]] = observation
        if event.get("required") and observation["status"] != "counted":
            problems.append(f"{group['id']}/{event['key']}: required event status is {observation['status']}")
        if observation["status"] == "counted":
            fraction = observation["running_fraction"]
            if fraction is None:
                problems.append(f"{group['id']}/{event['key']}: running fraction is unavailable")
            elif not 0.0 <= fraction <= 1.0:
                problems.append(f"{group['id']}/{event['key']}: running fraction is outside [0, 1]")
            elif fraction < minimum_fraction:
                problems.append(
                    f"{group['id']}/{event['key']}: running fraction {fraction:.6f} is below {minimum_fraction:.6f}"
                )
            if observation["event_runtime_ns"] is None:
                problems.append(f"{group['id']}/{event['key']}: event runtime is unavailable")
    unknown = sorted(set(by_name) - used)
    if unknown:
        problems.append(f"{group['id']}: unexpected perf events: {', '.join(unknown)}")
    return observations, problems


def group_expression(group: dict[str, Any]) -> str:
    return "{" + ",".join(event["selector"] for event in group["events"]) + "}"


def file_snapshots(paths: list[Path]) -> list[dict[str, Any]]:
    snapshots: list[dict[str, Any]] = []
    for path in paths:
        try:
            snapshots.append(hash_path_record(path))
        except (FileNotFoundError, QualificationError) as error:
            snapshots.append({"path": str(path.resolve()), "size": None, "mode": None, "sha256": None, "error": str(error)})
    return snapshots


def validate_hash_record(value: Any, *, allow_unavailable: bool) -> list[str]:
    problems: list[str] = []
    if not isinstance(value, dict) or not isinstance(value.get("path"), str) or not value["path"]:
        return ["file identity must contain a nonempty path"]
    digest = value.get("sha256")
    size = value.get("size")
    mode = value.get("mode")
    unavailable = digest is None or size is None or mode is None
    if unavailable:
        if not allow_unavailable or not (digest is None and size is None and mode is None):
            problems.append("unavailable file identity must use null size, mode, and digest")
        if not isinstance(value.get("error"), str) or not value["error"]:
            problems.append("unavailable file identity must retain an error")
        return problems
    if not SHA256_RE.fullmatch(str(digest)):
        problems.append("file identity has a malformed digest")
    if not isinstance(size, int) or isinstance(size, bool) or size < 0:
        problems.append("file identity has a malformed size")
    if not isinstance(mode, int) or isinstance(mode, bool) or not 0 <= mode <= 0o7777:
        problems.append("file identity has a malformed mode")
    return problems


