#!/usr/bin/env python3
"""Capture and validate the effective GitHub mobile CI contract.

The Android and iOS launchers remain the policy/execution authorities.  This
module consumes their final, anchored records after the mobile step has
finished, binds those records to the produced artifacts and toolchain, and
supplies ci_summary.py's shared fail-closed retained coverage record.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import html
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Mapping, MutableMapping


SCHEMA_VERSION = 1
POLICY_VERSION = 1
KIND = "mobile-lane-coverage"
CONFIGURATIONS = ("Debug", "Release")
ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "ci.yml"
OUTPUT_NAME = "coverage.json"
_MAX_LOG_BYTES = 64 * 1024 * 1024
_MAX_LINE_BYTES = 8192
_MAX_COMMAND_OUTPUT_BYTES = 64 * 1024
_MAX_SIMULATOR_INVENTORY_BYTES = 8 * 1024 * 1024
_HEX64 = re.compile(r"^[0-9a-f]{64}$")
_HEX40 = re.compile(r"^[0-9a-f]{40}$")
_UUID = re.compile(r"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$")


class MobileCoverageError(RuntimeError):
    """A bounded evidence or policy failure."""


def _canonical_json(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _regular_file(path: Path, description: str) -> Path:
    if path.is_symlink() or not path.is_file():
        raise MobileCoverageError(f"{description} is not a regular file: {path}")
    return path.resolve(strict=True)


def _resolved_file(path: Path, description: str) -> Path:
    """Resolve toolchain symlinks while requiring a regular final target."""
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise MobileCoverageError(f"{description} could not be resolved: {path}") from error
    if not resolved.is_file():
        raise MobileCoverageError(f"{description} does not resolve to a regular file: {path}")
    return resolved


def _directory(path: Path, description: str) -> Path:
    if path.is_symlink() or not path.is_dir():
        raise MobileCoverageError(f"{description} is not a directory: {path}")
    return path.resolve(strict=True)


def _bounded_text(path: Path, description: str) -> str:
    path = _regular_file(path, description)
    size = path.stat().st_size
    if size > _MAX_LOG_BYTES:
        raise MobileCoverageError(f"{description} exceeds the {_MAX_LOG_BYTES}-byte evidence limit")
    data = path.read_bytes()
    return data.decode("utf-8", errors="replace")


def _anchored_lines(path: Path, description: str) -> list[str]:
    text = _bounded_text(path, description)
    lines: list[str] = []
    for raw in text.splitlines():
        if len(raw.encode("utf-8", errors="replace")) <= _MAX_LINE_BYTES:
            lines.append(raw.rstrip("\r"))
    return lines


def _atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def _run(
    arguments: list[str],
    description: str,
    timeout: int = 30,
    max_output_bytes: int = _MAX_COMMAND_OUTPUT_BYTES,
) -> str:
    try:
        completed = subprocess.run(
            arguments,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise MobileCoverageError(f"{description} could not run: {error}") from error
    output = completed.stdout
    if len(output.encode("utf-8", errors="replace")) > max_output_bytes:
        raise MobileCoverageError(
            f"{description} exceeded the {max_output_bytes}-byte output limit"
        )
    if completed.returncode != 0:
        raise MobileCoverageError(f"{description} failed with status {completed.returncode}: {output.strip()}")
    return output


def _runner_architecture(value: str) -> str:
    aliases = {
        "x64": "x86_64",
        "amd64": "x86_64",
        "x86_64": "x86_64",
        "arm64": "aarch64",
        "aarch64": "aarch64",
    }
    return aliases.get(value.strip().lower(), "")


def _semantic_row_id(platform: str, architecture: str, configuration: str, execution: str) -> str:
    return (
        f"mobile/{platform}/{architecture}/configuration={configuration}/"
        f"sanitizer=off/fuzz=off/execution={execution}"
    )


def _policy(platform: str, architecture: str) -> dict[str, Any]:
    common_exclusions = [
        {
            "capability": "sanitizer",
            "state": "excluded",
            "reason": "mobile-driver-explicitly-configures-sanitizer-off",
        },
        {
            "capability": "fuzz",
            "state": "excluded",
            "reason": "mobile-driver-explicitly-configures-fuzz-off",
        },
        {
            "capability": "register-allocation-mode-matrix",
            "state": "excluded",
            "reason": "native-suite-owns-register-allocation-mode-matrix",
        },
        {
            "capability": "static-analysis",
            "state": "excluded",
            "reason": "dedicated-analyzer-job-owns-static-analysis",
        },
        {
            "capability": "table-audit",
            "state": "excluded",
            "reason": "desktop-release-shard-owns-table-audit",
        },
        {
            "capability": "self-host",
            "state": "excluded",
            "reason": "desktop-release-shard-owns-self-host",
        },
        {
            "capability": "fixed-point",
            "state": "excluded",
            "reason": "desktop-release-shard-owns-fixed-point",
        },
    ]
    if platform == "android" and architecture == "x86_64":
        abi = "x86_64"
        execution = "runtime"
        artifact_kind = "apk"
        exclusions = [
            {
                "capability": "android-aarch64-runtime",
                "state": "excluded",
                "reason": "github-mobile-matrix-provisions-one-x86_64-emulator-lane",
            },
            *common_exclusions,
        ]
    elif platform == "ios" and architecture == "x86_64":
        abi = "x86_64"
        execution = "compile-link-bundle"
        artifact_kind = "app-bundle"
        exclusions = [
            {
                "capability": "runtime-execution",
                "state": "excluded",
                "reason": "github-macos-26-intel-xcode-26-runtime-does-not-provide-reliable-x86_64-execution",
            },
            *common_exclusions,
        ]
    elif platform == "ios" and architecture == "aarch64":
        abi = "arm64"
        execution = "runtime"
        artifact_kind = "app-bundle"
        exclusions = list(common_exclusions)
    else:
        raise MobileCoverageError(f"unsupported mobile lane: {platform}/{architecture}")

    obligations = {
        item["capability"]: {"state": "not-applicable", "reason": item["reason"]}
        for item in exclusions
    }

    expected = [
        {
            "id": _semantic_row_id(platform, architecture, configuration, execution),
            "state": "required",
            "exclusion": "",
            "compiler": "clang",
            "configuration": configuration,
            "optimize": False,
            "sanitize": False,
            "fuzz": False,
            "static_analysis": False,
            "table_audit": False,
            "self_host": False,
            "fixed_point": False,
            "mode_matrix": False,
            "abi": abi,
            "execution": execution,
            "artifact_kind": artifact_kind,
            "test_scope": "registered-suite" if execution == "runtime" else "build-link-bundle",
        }
        for configuration in CONFIGURATIONS
    ]
    fingerprint = _sha256_bytes(
        _canonical_json({"expected": expected, "exclusions": exclusions, "obligations": obligations})
    )[:16]
    return {
        "version": POLICY_VERSION,
        "row_count": len(expected),
        "required_count": len(expected),
        "exclusion_count": len(exclusions),
        "effective_configuration_count": len(expected),
        "fingerprint": fingerprint,
        "expected": expected,
        "exclusions": exclusions,
        "obligations": obligations,
    }


def _workflow_mobile_lanes(path: Path = WORKFLOW_PATH) -> list[dict[str, str]]:
    text = _bounded_text(path, "GitHub workflow")
    start = text.find("\n  mobile:\n")
    end = text.find("\n  uefi:\n", start + 1)
    if start < 0 or end < 0 or end <= start:
        raise MobileCoverageError("the GitHub workflow has no bounded mobile job block")
    mobile = text[start:end]
    expression = re.compile(
        r"(?m)^          - name: (?P<name>[^\n]+)\n"
        r"            runner: (?P<runner>[^\n]+)\n"
        r"            os: (?P<os>android|ios)\n"
        r"            arch: (?P<arch>x86_64|aarch64)$"
    )
    lanes = [match.groupdict() for match in expression.finditer(mobile)]
    expected = {
        ("Android x86-64", "android", "x86_64"),
        ("iOS x86-64", "ios", "x86_64"),
        ("iOS AArch64", "ios", "aarch64"),
    }
    observed = {(lane["name"], lane["os"], lane["arch"]) for lane in lanes}
    if len(lanes) != 3 or observed != expected or any(not lane["runner"].strip() for lane in lanes):
        raise MobileCoverageError(
            "the mobile workflow must contain exactly Android x86-64, iOS x86-64, and iOS AArch64 lanes"
        )
    required_fragments = (
        "./android/test_ci.sh --all",
        "./ios/test_ci.sh --all",
        "BUSTER_CI_REQUIRED: ${{ matrix.os == 'ios' && 'ios' || (steps.android_sdk.outcome == 'success' && 'android_sdk android' || 'android_sdk') }}",
        "run: python3 tools/ci_summary.py",
    )
    for fragment in required_fragments:
        if mobile.count(fragment) != 1:
            raise MobileCoverageError(f"the mobile workflow must contain exactly one {fragment!r} contract")
    return sorted(lanes, key=lambda lane: (lane["os"], lane["arch"]))


def _workflow_mobile_lane(platform: str, architecture: str) -> dict[str, str]:
    matches = [
        lane
        for lane in _workflow_mobile_lanes()
        if lane["os"] == platform and lane["arch"] == architecture
    ]
    if len(matches) != 1:
        raise MobileCoverageError(
            f"the workflow does not define exactly one {platform}/{architecture} mobile lane"
        )
    return matches[0]


def _parse_cmake_cache(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in _bounded_text(path, "CMake cache").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key = key_and_type.split(":", 1)[0]
        if key and key not in values:
            values[key] = value
    return values


def _cmake_compiler_file(build_directory: Path) -> Path:
    candidates = sorted(build_directory.glob("CMakeFiles/*/CMakeCCompiler.cmake"))
    if len(candidates) != 1:
        raise MobileCoverageError(
            f"expected one generated CMake C compiler identity file, found {len(candidates)}"
        )
    return _regular_file(candidates[0], "CMake C compiler identity")


def _cmake_set(text: str, name: str) -> str:
    match = re.search(rf'^set\({re.escape(name)} "([^"]*)"\)$', text, re.MULTILINE)
    return match.group(1) if match else ""


def _toolchain(build_directory: Path, platform: str, architecture: str) -> dict[str, Any]:
    build_directory = _directory(build_directory, "mobile build directory")
    cache_path = _regular_file(build_directory / "CMakeCache.txt", "CMake cache")
    cache = _parse_cmake_cache(cache_path)
    compiler_identity_path = _cmake_compiler_file(build_directory)
    compiler_identity_text = _bounded_text(compiler_identity_path, "CMake C compiler identity")
    compiler_value = cache.get("CMAKE_C_COMPILER") or _cmake_set(compiler_identity_text, "CMAKE_C_COMPILER")
    if not compiler_value:
        raise MobileCoverageError("CMake did not record the C compiler path")
    compiler_configured_path = Path(compiler_value)
    if not compiler_configured_path.is_absolute():
        raise MobileCoverageError("CMake did not record an absolute C compiler path")
    compiler_path = _resolved_file(compiler_configured_path, "configured C compiler")
    compiler_id = _cmake_set(compiler_identity_text, "CMAKE_C_COMPILER_ID")
    compiler_version = _cmake_set(compiler_identity_text, "CMAKE_C_COMPILER_VERSION")
    if compiler_id not in ("Clang", "AppleClang") or not compiler_version:
        raise MobileCoverageError(
            f"mobile C compiler identity must be Clang/AppleClang with a version, got {compiler_id!r} {compiler_version!r}"
        )
    version_output = _run([str(compiler_path), "--version"], "configured C compiler version probe")
    if "clang" not in version_output.lower():
        raise MobileCoverageError("configured mobile compiler version output is not Clang")

    common_evidence = {
        "BUSTER_CI": cache.get("BUSTER_CI", ""),
        "BUSTER_SANITIZE": cache.get("BUSTER_SANITIZE", ""),
        "BUSTER_FUZZ_AVAILABLE": cache.get("BUSTER_FUZZ_AVAILABLE", ""),
        "BUSTER_INCLUDE_TESTS": cache.get("BUSTER_INCLUDE_TESTS", ""),
        "BUSTER_OPTIMIZE": cache.get("BUSTER_OPTIMIZE", ""),
        "BUSTER_CHECK_OPTIONAL_WARNINGS": cache.get("BUSTER_CHECK_OPTIONAL_WARNINGS", ""),
        "BUSTER_DEVELOPER_TARGETS": cache.get("BUSTER_DEVELOPER_TARGETS", ""),
        "CMAKE_CONFIGURATION_TYPES": cache.get("CMAKE_CONFIGURATION_TYPES", ""),
    }
    expected_common = {
        "BUSTER_CI": "ON",
        "BUSTER_SANITIZE": "OFF",
        "BUSTER_FUZZ_AVAILABLE": "OFF",
        "BUSTER_INCLUDE_TESTS": "ON",
        "BUSTER_OPTIMIZE": "OFF",
        "BUSTER_CHECK_OPTIONAL_WARNINGS": "OFF",
        "BUSTER_DEVELOPER_TARGETS": "OFF",
        "CMAKE_CONFIGURATION_TYPES": "Debug;Release",
    }
    if common_evidence != expected_common:
        raise MobileCoverageError(
            "mobile CMake cache no longer matches the reviewed CI/test/sanitizer/fuzz/configuration policy"
        )

    if platform == "android":
        target_evidence = {
            "ANDROID_ABI": cache.get("ANDROID_ABI") or cache.get("CMAKE_ANDROID_ARCH_ABI", ""),
            "ANDROID_PLATFORM": cache.get("ANDROID_PLATFORM") or cache.get("CMAKE_SYSTEM_VERSION", ""),
            "CMAKE_SYSTEM_NAME": cache.get("CMAKE_SYSTEM_NAME", "Android"),
            "CMAKE_C_COMPILER_TARGET": _cmake_set(compiler_identity_text, "CMAKE_C_COMPILER_TARGET"),
        }
        if target_evidence["ANDROID_ABI"] != "x86_64":
            raise MobileCoverageError("Android CMake cache does not retain the required x86_64 ABI")
        if target_evidence["ANDROID_PLATFORM"] not in ("android-35", "35"):
            raise MobileCoverageError("Android CMake cache does not retain the required API 35 platform")
        if target_evidence["CMAKE_SYSTEM_NAME"] != "Android":
            raise MobileCoverageError("Android CMake cache does not identify the Android platform")
        effective_target = target_evidence["CMAKE_C_COMPILER_TARGET"] or "x86_64-linux-android35"
    else:
        expected_arch = "arm64" if architecture == "aarch64" else "x86_64"
        target_evidence = {
            "CMAKE_OSX_ARCHITECTURES": cache.get("CMAKE_OSX_ARCHITECTURES", ""),
            "CMAKE_OSX_SYSROOT": cache.get("CMAKE_OSX_SYSROOT", ""),
            "CMAKE_OSX_DEPLOYMENT_TARGET": cache.get("CMAKE_OSX_DEPLOYMENT_TARGET", ""),
            "CMAKE_SYSTEM_NAME": cache.get("CMAKE_SYSTEM_NAME", ""),
            "CMAKE_C_COMPILER_TARGET": _cmake_set(compiler_identity_text, "CMAKE_C_COMPILER_TARGET"),
        }
        if target_evidence["CMAKE_OSX_ARCHITECTURES"] != expected_arch:
            raise MobileCoverageError("iOS CMake cache architecture does not match the mobile lane")
        sysroot = target_evidence["CMAKE_OSX_SYSROOT"].lower()
        if sysroot != "iphonesimulator" and "iphonesimulator" not in sysroot:
            raise MobileCoverageError("iOS CMake cache does not target the simulator SDK")
        if not target_evidence["CMAKE_OSX_DEPLOYMENT_TARGET"]:
            raise MobileCoverageError("iOS CMake cache has no deployment target")
        if target_evidence["CMAKE_SYSTEM_NAME"] != "iOS":
            raise MobileCoverageError("iOS CMake cache does not identify iOS")
        effective_target = target_evidence["CMAKE_C_COMPILER_TARGET"] or (
            f"{expected_arch}-apple-ios{target_evidence['CMAKE_OSX_DEPLOYMENT_TARGET']}-simulator"
        )

    return {
        "build_directory": str(build_directory),
        "cache_path": str(cache_path),
        "cache_hash": _sha256_file(cache_path),
        "compiler_identity_path": str(compiler_identity_path),
        "compiler_identity_hash": _sha256_file(compiler_identity_path),
        "compiler_family": "clang",
        "compiler_configured_path": str(compiler_configured_path),
        "compiler_path": str(compiler_path),
        "compiler_hash": _sha256_file(compiler_path),
        "compiler_id": compiler_id,
        "compiler_version": compiler_version,
        "compiler_target": effective_target,
        "version_output_hash": _sha256_bytes(version_output.encode("utf-8")),
        "configuration_evidence": common_evidence,
        "target_evidence": target_evidence,
    }


def _hash_tree(path: Path) -> tuple[str, int, int]:
    root = _directory(path, "app bundle")
    digest = hashlib.sha256()
    total_size = 0
    entry_count = 0
    for candidate in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        relative = candidate.relative_to(root).as_posix().encode("utf-8")
        if candidate.is_symlink():
            target = os.readlink(candidate).encode("utf-8", errors="surrogateescape")
            digest.update(b"L")
            digest.update(len(relative).to_bytes(8, "little"))
            digest.update(relative)
            digest.update(len(target).to_bytes(8, "little"))
            digest.update(target)
            entry_count += 1
            continue
        if not candidate.is_file():
            continue
        digest.update(b"F")
        digest.update(len(relative).to_bytes(8, "little"))
        digest.update(relative)
        size = candidate.stat().st_size
        digest.update(size.to_bytes(8, "little"))
        with candidate.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        total_size += size
        entry_count += 1
    if entry_count == 0:
        raise MobileCoverageError(f"app bundle contains no retained entries: {root}")
    return digest.hexdigest(), total_size, entry_count


def _identity(environment: Mapping[str, str], platform: str, architecture: str) -> dict[str, str]:
    checkout_root = Path(
        _run(["git", "rev-parse", "--show-toplevel"], "mobile checkout-root probe", max_output_bytes=4096).strip()
    ).resolve(strict=True)
    if checkout_root != ROOT:
        raise MobileCoverageError(
            f"mobile coverage producer is outside the checked-out repository: {checkout_root}"
        )
    checkout_revision = _run(
        ["git", "rev-parse", "--verify", "HEAD"],
        "mobile checkout revision probe",
        max_output_bytes=4096,
    ).strip().lower()
    lane = _workflow_mobile_lane(platform, architecture)
    values = {
        "suite": "mobile",
        "platform": platform,
        "architecture": architecture,
        "lane_name": lane["name"],
        "repository": environment.get("GITHUB_REPOSITORY", ""),
        "source_revision": environment.get("GITHUB_SHA", ""),
        "checkout_revision": checkout_revision,
        "ref": environment.get("GITHUB_REF", ""),
        "run_id": environment.get("GITHUB_RUN_ID", ""),
        "run_attempt": environment.get("GITHUB_RUN_ATTEMPT", ""),
        "runner_label": environment.get("BUSTER_CI_RUNNER", ""),
        "runner_os": environment.get("RUNNER_OS", ""),
        "runner_arch": environment.get("RUNNER_ARCH", ""),
        "runner_environment": environment.get("RUNNER_ENVIRONMENT", ""),
        "runner_image": environment.get("ImageOS", ""),
        "runner_image_version": environment.get("ImageVersion", ""),
    }
    mandatory = (
        "repository", "source_revision", "checkout_revision", "ref", "run_id", "run_attempt",
        "runner_label", "runner_os", "runner_arch",
    )
    missing = [name for name in mandatory if not values[name]]
    if missing:
        raise MobileCoverageError("mobile CI identity is missing: " + ", ".join(missing))
    if not _HEX40.fullmatch(values["source_revision"].lower()):
        raise MobileCoverageError("mobile CI source revision is not a full Git commit SHA")
    if not _HEX40.fullmatch(checkout_revision) or checkout_revision != values["source_revision"].lower():
        raise MobileCoverageError("mobile checked-out HEAD does not match GITHUB_SHA")
    if values["runner_label"] != lane["runner"]:
        raise MobileCoverageError(
            f"mobile runner label {values['runner_label']!r} does not match workflow policy {lane['runner']!r}"
        )
    values["lane_id"] = (
        f"mobile/{platform}/{architecture}/source={values['source_revision']}/"
        f"run={values['run_id']}/attempt={values['run_attempt']}"
    )
    return values


def _evidence(platform: str, log_path: Path) -> dict[str, str]:
    if platform == "android":
        platform_paths = {
            "driver": ROOT / "android/test_ci.sh",
            "lifecycle": ROOT / "android/start_emulator_ci.sh",
            "payload": ROOT / "android/run_tests.sh",
        }
    else:
        platform_paths = {
            "driver": ROOT / "ios/test_ci.sh",
            "lifecycle": ROOT / "ios/launch_simulator.sh",
        }
    paths = {
        "workflow": WORKFLOW_PATH,
        **platform_paths,
        "summary": ROOT / "tools/ci_summary.py",
        "producer": Path(__file__),
        "log": log_path,
    }
    result: dict[str, str] = {}
    for name, path in paths.items():
        resolved = _regular_file(path, f"mobile {name} evidence")
        result[f"{name}_path"] = str(resolved)
        result[f"{name}_hash"] = _sha256_file(resolved)
    return result


def _unique_records(lines: list[str], expression: re.Pattern[str], description: str) -> list[dict[str, str]]:
    records: list[dict[str, str]] = []
    for line in lines:
        match = expression.fullmatch(line)
        if match:
            records.append(match.groupdict())
    if not records:
        raise MobileCoverageError(f"{description} record is missing")
    return records


def _android_runtime(lines: list[str], environment: Mapping[str, str]) -> dict[str, Any]:
    config_expression = re.compile(r"ANDROID_CONFIG_RESULT config=(?P<config>Debug|Release) status=(?P<status>0|[1-9][0-9]{0,2}|not-run)")
    payload_expression = re.compile(r"ANDROID_PAYLOAD_RESULT config=(?P<config>Debug|Release) phase=(?P<phase>wait-device|install|launch|monitor) status=(?P<status>0|[1-9][0-9]{0,2})")
    monitor_expression = re.compile(
        r"ANDROID_MONITOR_RESULT config=(?P<config>Debug|Release) reader_status=(?P<reader>0|[1-9][0-9]{0,2}) "
        r"producer_status=(?P<producer>0|[1-9][0-9]{0,2}) timeout_seconds=(?P<timeout>[1-9][0-9]{0,8}) "
        r"elapsed_seconds=(?P<elapsed>0|[1-9][0-9]{0,8}) headroom_seconds=(?P<headroom>0|[1-9][0-9]{0,8}) "
        r"headroom_warning=(?P<warning>yes|no)"
    )
    batch_expression = re.compile(r"ANDROID_BATCH_RESULT phase=tests config=none status=(?P<status>0|[1-9][0-9]{0,2}) cleanup_status=(?P<cleanup>0|[1-9][0-9]{0,2}|not-run)")
    ci_expression = re.compile(r"ANDROID_CI_RESULT phase=tests payload_status=(?P<payload>0|[1-9][0-9]{0,2}) cleanup_status=(?P<cleanup>0|[1-9][0-9]{0,2}|not-run) status=(?P<status>0|[1-9][0-9]{0,2})")

    per_kind: dict[str, dict[str, dict[str, str]]] = {"config": {}, "payload": {}, "monitor": {}}
    for kind, expression in (("config", config_expression), ("payload", payload_expression), ("monitor", monitor_expression)):
        records = _unique_records(lines, expression, f"Android {kind}")
        for record in records:
            config = record["config"]
            if config in per_kind[kind]:
                raise MobileCoverageError(f"Android {kind} has a duplicate {config} record")
            per_kind[kind][config] = record
        if set(per_kind[kind]) != set(CONFIGURATIONS):
            raise MobileCoverageError(f"Android {kind} does not prove exactly Debug and Release")

    for config in CONFIGURATIONS:
        if per_kind["config"][config]["status"] != "0":
            raise MobileCoverageError(f"Android {config} configuration did not pass")
        if per_kind["payload"][config]["status"] != "0" or per_kind["payload"][config]["phase"] != "monitor":
            raise MobileCoverageError(f"Android {config} payload did not complete the monitor phase")
        if per_kind["monitor"][config]["reader"] != "10":
            raise MobileCoverageError(f"Android {config} did not observe the success result marker")

    batch = _unique_records(lines, batch_expression, "Android batch")
    final = _unique_records(lines, ci_expression, "Android final CI")
    if len(batch) != 1 or batch[0]["status"] != "0":
        raise MobileCoverageError("Android batch completion is missing, duplicated, or failed")
    if len(final) != 1 or final[0] != {"payload": "0", "cleanup": "0", "status": "0"}:
        raise MobileCoverageError("Android final payload/cleanup result is missing, duplicated, or failed")

    devices = set()
    device_line = re.compile(r"(?P<serial>emulator-[0-9]+)[ \t]+device(?:[ \t].*)?")
    for line in lines:
        match = device_line.fullmatch(line)
        if match:
            devices.add(match.group("serial"))
    if len(devices) != 1:
        raise MobileCoverageError(f"Android evidence must identify exactly one device serial, found {sorted(devices)}")
    serial = next(iter(devices))

    validation = re.compile(r"Android device validated: abi=(?P<abi>[A-Za-z0-9._-]+) sdk=(?P<sdk>[0-9]+)")
    validations = _unique_records(lines, validation, "Android device validation")
    pairs = {(record["abi"], record["sdk"]) for record in validations}
    if pairs != {("x86_64", "35")}:
        raise MobileCoverageError(f"Android device validations disagree with x86_64/API 35: {sorted(pairs)}")

    starts = _unique_records(
        lines,
        re.compile(r"Starting Android emulator '(?P<avd>[^']+)' with gpu=(?P<gpu>[^ ]+) headless=(?P<headless>[^ ]+)"),
        "Android emulator start",
    )
    if len(starts) != 1 or starts[0]["avd"] != "buster-ci":
        raise MobileCoverageError("Android lane did not start exactly the buster-ci AVD")
    system_image = environment.get("BUSTER_ANDROID_SYSTEM_IMAGE", "")
    expected_image = "system-images;android-35;google_apis;x86_64"
    if system_image != expected_image:
        raise MobileCoverageError(f"Android system image changed from the reviewed policy: {system_image!r}")

    return {
        "kind": "android-emulator",
        "execution": "runtime",
        "serial": serial,
        "avd": starts[0]["avd"],
        "abi": "x86_64",
        "sdk_level": 35,
        "system_image": system_image,
        "gpu": starts[0]["gpu"],
        "headless": starts[0]["headless"],
        "configurations": {
            config: {
                "configuration_status": int(per_kind["config"][config]["status"]),
                "payload_status": int(per_kind["payload"][config]["status"]),
                "monitor_reader_status": int(per_kind["monitor"][config]["reader"]),
                "monitor_producer_status": int(per_kind["monitor"][config]["producer"]),
                "deadline_seconds": int(per_kind["monitor"][config]["timeout"]),
                "elapsed_seconds": int(per_kind["monitor"][config]["elapsed"]),
                "headroom_seconds": int(per_kind["monitor"][config]["headroom"]),
                "headroom_warning": per_kind["monitor"][config]["warning"] == "yes",
            }
            for config in CONFIGURATIONS
        },
        "batch_status": 0,
        "payload_status": 0,
        "cleanup_status": 0,
        "status": 0,
    }


def _xcrun_json(arguments: list[str], description: str) -> Any:
    output = _run(
        ["xcrun", *arguments],
        description,
        max_output_bytes=_MAX_SIMULATOR_INVENTORY_BYTES,
    )
    try:
        return json.loads(output)
    except json.JSONDecodeError as error:
        raise MobileCoverageError(f"{description} returned malformed JSON") from error


def _ios_boot_disposition(lines: list[str]) -> str:
    dispositions = [line.partition("=")[2] for line in lines if line.startswith("BUSTER_IOS_BOOT_DISPOSITION=")]
    allowed = {
        "first-attempt-success",
        "continued-original-boot-success",
        "recovered-infrastructure-failure",
    }
    if len(dispositions) != 1 or dispositions[0] not in allowed:
        raise MobileCoverageError("iOS boot disposition is missing, duplicated, or unsuccessful")
    return dispositions[0]


def _ios_runtime(lines: list[str], architecture: str, output_directory: Path) -> dict[str, Any]:
    sdk_path = _run(["xcrun", "--sdk", "iphonesimulator", "--show-sdk-path"], "iOS simulator SDK path").strip()
    sdk_version = _run(["xcrun", "--sdk", "iphonesimulator", "--show-sdk-version"], "iOS simulator SDK version").strip()
    if not sdk_path or not sdk_version:
        raise MobileCoverageError("iOS simulator SDK identity is incomplete")

    if architecture == "x86_64":
        for config in CONFIGURATIONS:
            expected = f"iOS {config} x86-64 simulator bundle built and linked successfully."
            if lines.count(expected) != 1:
                raise MobileCoverageError(f"iOS x86-64 {config} bundle receipt is missing or duplicated")
        skip = (
            "iOS x86-64 execution skipped on GitHub macos-26-intel: Xcode 26 simulator runtimes "
            "do not provide reliable Intel execution coverage."
        )
        if lines.count(skip) != 1:
            raise MobileCoverageError("iOS x86-64 build-only rationale is missing or duplicated")
        return {
            "kind": "ios-simulator",
            "execution": "compile-link-bundle",
            "sdk_path": sdk_path,
            "sdk_version": sdk_version,
            "runtime_id": "",
            "runtime_version": "",
            "device_name": "",
            "udid": "",
            "state": "not-run",
            "reason": "github-macos-26-intel-xcode-26-runtime-does-not-provide-reliable-x86_64-execution",
        }

    for config in CONFIGURATIONS:
        if lines.count(f"iOS {config} tests passed.") != 1:
            raise MobileCoverageError(f"iOS arm64 {config} runtime receipt is missing or duplicated")
        console_path = output_directory / f"ios-console.{config}.log"
        console = _bounded_text(console_path, f"iOS {config} console log")
        if console.count("BUSTER_IOS_RESULT: SUCCESS") != 1 or "BUSTER_IOS_RESULT: FAILURE" in console:
            raise MobileCoverageError(f"iOS arm64 {config} console log does not prove one successful result")

    cleanup_expression = re.compile(
        r"BUSTER_IOS_CLEANUP simulator_udid=(?P<udid>[0-9A-Fa-f-]+) prior_status=(?P<prior>[0-9]+) "
        r"shutdown_status=(?P<shutdown>[0-9]+) shutdown_outcome=(?P<outcome>[^ ]+) "
        r"postcondition_eligibility=(?P<eligibility>[0-9]+) postcondition_probe_status=(?P<probe>[^ ]+) "
        r"postcondition_parser_status=(?P<parser>[^ ]+) postcondition_state=(?P<post_state>[^ ]+) "
        r"shutdown_disposition=(?P<disposition>[^ ]+) result_status=(?P<result>[0-9]+)"
    )
    cleanup = _unique_records(lines, cleanup_expression, "iOS cleanup")
    if len(cleanup) != 1:
        raise MobileCoverageError("iOS cleanup receipt is duplicated")
    record = cleanup[0]
    udid = record["udid"]
    if not _UUID.fullmatch(udid):
        raise MobileCoverageError("iOS cleanup did not retain a canonical simulator UUID")
    if record["prior"] != "0" or record["result"] != "0":
        raise MobileCoverageError("iOS cleanup receipt follows a failed payload or cleanup")
    if record["disposition"] not in (
        "direct-success",
        "verified-shutdown-after-timeout",
        "recovered-shutdown-after-timeout",
    ):
        raise MobileCoverageError("iOS cleanup disposition is not a verified shutdown")
    uses = [line for line in lines if line == f"Using simulator buster-ci ({udid})"]
    replacements = [
        line
        for line in lines
        if re.fullmatch(
            rf"Created replacement iOS simulator {re.escape(udid)} using runtime=[^ ]+ device_type=[^ ]+",
            line,
        )
    ]
    if len(uses) + len(replacements) != 1:
        raise MobileCoverageError(
            "iOS runtime log does not bind the final UUID to the selected or recovered buster-ci device exactly once"
        )
    _ios_boot_disposition(lines)

    devices = _xcrun_json(["simctl", "list", "devices", "-j"], "iOS simulator device inventory")
    device_matches: list[tuple[str, dict[str, Any]]] = []
    device_groups = devices.get("devices") if isinstance(devices, dict) else None
    if not isinstance(device_groups, dict):
        raise MobileCoverageError("iOS simulator device inventory has no device map")
    for runtime_id, runtime_devices in device_groups.items():
        if not isinstance(runtime_devices, list):
            raise MobileCoverageError("iOS simulator device inventory contains a malformed runtime group")
        for device in runtime_devices:
            if isinstance(device, dict) and device.get("udid") == udid:
                device_matches.append((runtime_id, device))
    if len(device_matches) != 1:
        raise MobileCoverageError("iOS simulator inventory does not contain the exact final UUID once")
    runtime_id, device = device_matches[0]
    if device.get("state") != "Shutdown" or device.get("name") != "buster-ci":
        raise MobileCoverageError("iOS simulator did not finish as the shutdown buster-ci device")

    runtimes = _xcrun_json(["simctl", "list", "runtimes", "-j"], "iOS simulator runtime inventory")
    runtime_items = runtimes.get("runtimes") if isinstance(runtimes, dict) else None
    if not isinstance(runtime_items, list):
        raise MobileCoverageError("iOS simulator runtime inventory has no runtime list")
    runtime_matches = [item for item in runtime_items if isinstance(item, dict) and item.get("identifier") == runtime_id]
    if len(runtime_matches) != 1:
        raise MobileCoverageError("iOS simulator runtime identity is missing or duplicated")
    runtime = runtime_matches[0]
    if runtime.get("isAvailable") is False or not isinstance(runtime.get("version"), str) or not runtime["version"]:
        raise MobileCoverageError("iOS simulator runtime is unavailable or versionless")

    return {
        "kind": "ios-simulator",
        "execution": "runtime",
        "sdk_path": sdk_path,
        "sdk_version": sdk_version,
        "runtime_id": runtime_id,
        "runtime_version": runtime["version"],
        "device_name": device["name"],
        "udid": udid,
        "state": device["state"],
        "boot_disposition": dispositions[0],
        "shutdown_status": int(record["shutdown"]),
        "shutdown_outcome": record["outcome"],
        "shutdown_disposition": record["disposition"],
        "status": int(record["result"]),
    }


def _detected_rows(
    platform: str,
    build_directory: Path,
    expected: list[dict[str, Any]],
    toolchain: Mapping[str, Any],
) -> list[dict[str, Any]]:
    detected: list[dict[str, Any]] = []
    by_config = {row["configuration"]: row for row in expected}
    for config in CONFIGURATIONS:
        row = by_config[config]
        common = {
            "id": row["id"],
            "state": "available",
            "result": "passed",
            "compiler": "clang",
            "compiler_id": toolchain["compiler_id"],
            "compiler_version": toolchain["compiler_version"],
            "compiler_target": toolchain["compiler_target"],
            "compiler_path": toolchain["compiler_path"],
            "compiler_hash": toolchain["compiler_hash"],
            "execution": row["execution"],
        }
        if platform == "android":
            artifact = _regular_file(build_directory / config / "buster.apk", f"Android {config} APK")
            detected.append(
                {
                    **common,
                    "artifact_path": str(artifact),
                    "artifact_hash": _sha256_file(artifact),
                    "artifact_size": artifact.stat().st_size,
                    "artifact_entry_count": 1,
                }
            )
        else:
            bundle = _directory(build_directory / config / "ide.app", f"iOS {config} app bundle")
            executable = _regular_file(bundle / "ide", f"iOS {config} executable")
            tree_hash, tree_size, entry_count = _hash_tree(bundle)
            detected.append(
                {
                    **common,
                    "artifact_path": str(bundle),
                    "artifact_hash": tree_hash,
                    "artifact_size": tree_size,
                    "artifact_entry_count": entry_count,
                    "executable_path": str(executable),
                    "executable_hash": _sha256_file(executable),
                    "executable_size": executable.stat().st_size,
                }
            )
    return detected


def _capture_manifest(environment: Mapping[str, str], self_test_cases: int, workflow_lanes: int) -> dict[str, Any]:
    required = environment.get("BUSTER_CI_REQUIRED", "").split()
    mobile_required = [name for name in required if name in ("android", "ios")]
    if len(mobile_required) != 1:
        raise MobileCoverageError("mobile summary must require exactly one of android or ios")
    platform = mobile_required[0]
    architecture = _runner_architecture(environment.get("RUNNER_ARCH", ""))
    if not architecture:
        raise MobileCoverageError("mobile runner architecture is unsupported or missing")
    if platform == "android" and (architecture != "x86_64" or environment.get("RUNNER_OS") != "Linux"):
        raise MobileCoverageError("Android mobile coverage is supported only on the Linux x86-64 lane")
    if platform == "ios" and environment.get("RUNNER_OS") != "macOS":
        raise MobileCoverageError("iOS mobile coverage requires a macOS runner")

    policy = _policy(platform, architecture)
    output_directory = _directory(Path(environment["RUNNER_TEMP"]) / "buster-ci", "mobile CI output directory")
    log_path = output_directory / ("android.log" if platform == "android" else "ios.log")
    lines = _anchored_lines(log_path, f"{platform} CI log")
    if platform == "android":
        build_directory = ROOT / "build/android-ci-x86_64"
        runtime = _android_runtime(lines, environment)
    else:
        cmake_arch = "arm64" if architecture == "aarch64" else "x86_64"
        build_directory = ROOT / f"build/ios-simulator-{cmake_arch}"
        runtime = _ios_runtime(lines, architecture, output_directory)
    build_directory = _directory(build_directory, "mobile build directory")
    toolchain = _toolchain(build_directory, platform, architecture)
    detected = _detected_rows(platform, build_directory, policy["expected"], toolchain)
    identity = _identity(environment, platform, architecture)
    manifest_path = output_directory / OUTPUT_NAME
    return {
        "schema": SCHEMA_VERSION,
        "kind": KIND,
        "hash_algorithm": "sha256",
        "phase": "complete",
        "mode": "ci",
        "identity": identity,
        "policy": {
            "version": policy["version"],
            "row_count": policy["row_count"],
            "required_count": policy["required_count"],
            "exclusion_count": policy["exclusion_count"],
            "effective_configuration_count": policy["effective_configuration_count"],
            "fingerprint": policy["fingerprint"],
        },
        "expected": policy["expected"],
        "detected": detected,
        "executed": [
            {
                "lane_id": identity["lane_id"],
                "status": "success",
                "evidence": "driver-complete",
                "rows": [row["id"] for row in policy["expected"]],
            }
        ],
        "exclusions": policy["exclusions"],
        "obligations": policy["obligations"],
        "toolchain": toolchain,
        "runtime": runtime,
        "evidence": _evidence(platform, log_path),
        "retention": {"manifest_path": str(manifest_path.resolve(strict=False))},
        "regression_guard": {
            "self_test_cases": self_test_cases,
            "workflow_lane_count": workflow_lanes,
        },
    }


def _validate_policy_contract(manifest: Any, platform: str, architecture: str) -> list[str]:
    errors: list[str] = []
    if not isinstance(manifest, dict):
        return ["mobile coverage manifest is not an object"]
    if manifest.get("schema") != SCHEMA_VERSION or manifest.get("kind") != KIND or manifest.get("hash_algorithm") != "sha256":
        errors.append("mobile coverage schema/kind/hash algorithm is unsupported")
    if manifest.get("phase") != "complete" or manifest.get("mode") != "ci":
        errors.append("mobile coverage manifest is not a complete CI manifest")
    for error in manifest.get("capture_errors", []) if isinstance(manifest.get("capture_errors"), list) else []:
        if isinstance(error, str) and error:
            errors.append("mobile coverage capture failed: " + error)
    try:
        expected_policy = _policy(platform, architecture)
    except MobileCoverageError as error:
        return sorted(set(errors + [str(error)]))
    policy = manifest.get("policy")
    expected_counts = {
        "version": expected_policy["version"],
        "row_count": expected_policy["row_count"],
        "required_count": expected_policy["required_count"],
        "exclusion_count": expected_policy["exclusion_count"],
        "effective_configuration_count": expected_policy["effective_configuration_count"],
        "fingerprint": expected_policy["fingerprint"],
    }
    if policy != expected_counts:
        errors.append("mobile coverage policy count/version/fingerprint differs from the reviewed lane contract")
    expected = manifest.get("expected")
    if expected != expected_policy["expected"]:
        errors.append("mobile expected rows differ from the reviewed lane contract")
    if manifest.get("exclusions") != expected_policy["exclusions"]:
        errors.append("mobile exclusions or rationales differ from the reviewed lane contract")
    if manifest.get("obligations") != expected_policy["obligations"]:
        errors.append("mobile obligations or owners differ from the reviewed lane contract")

    detected = manifest.get("detected")
    if not isinstance(detected, list):
        errors.append("mobile detected rows are missing")
        detected = []
    expected_ids = [row["id"] for row in expected_policy["expected"]]
    detected_ids = [row.get("id") for row in detected if isinstance(row, dict)]
    if detected_ids != expected_ids or len(detected_ids) != len(set(detected_ids)):
        errors.append("mobile detected rows do not prove exactly the required rows in policy order")
    for row in detected:
        if not isinstance(row, dict) or row.get("state") != "available" or row.get("result") != "passed":
            errors.append("mobile detected row is malformed or not passed")
            continue
        if row.get("compiler") != "clang" or row.get("execution") not in ("runtime", "compile-link-bundle"):
            errors.append("mobile detected compiler/execution classification is malformed")
        for name in ("artifact_hash", "compiler_hash"):
            if not _HEX64.fullmatch(str(row.get(name, ""))):
                errors.append(f"mobile detected {name} is malformed")
        if not isinstance(row.get("artifact_size"), int) or isinstance(row.get("artifact_size"), bool) or row.get("artifact_size", 0) <= 0:
            errors.append("mobile detected artifact size is malformed")
        if not isinstance(row.get("artifact_entry_count"), int) or isinstance(row.get("artifact_entry_count"), bool) or row.get("artifact_entry_count", 0) <= 0:
            errors.append("mobile detected artifact inventory is malformed")

    executed = manifest.get("executed")
    expected_execution = [{
        "lane_id": manifest.get("identity", {}).get("lane_id") if isinstance(manifest.get("identity"), dict) else None,
        "status": "success",
        "evidence": "driver-complete",
        "rows": expected_ids,
    }]
    if executed != expected_execution:
        errors.append("mobile execution does not prove exactly the required rows")
    return sorted(set(errors))


def validate_manifest(manifest: Any, environment: Mapping[str, str]) -> list[str]:
    required = environment.get("BUSTER_CI_REQUIRED", "").split()
    mobile_required = [name for name in required if name in ("android", "ios")]
    if len(mobile_required) != 1:
        return ["mobile summary must require exactly one of android or ios"]
    platform = mobile_required[0]
    architecture = _runner_architecture(environment.get("RUNNER_ARCH", ""))
    errors = _validate_policy_contract(manifest, platform, architecture)
    if not isinstance(manifest, dict):
        return errors

    try:
        expected_identity = _identity(environment, platform, architecture)
        if manifest.get("identity") != expected_identity:
            errors.append("mobile manifest identity does not match the current runner/event")
    except MobileCoverageError as error:
        errors.append(str(error))

    output_directory = Path(environment.get("RUNNER_TEMP", "")) / "buster-ci"
    log_path = output_directory / ("android.log" if platform == "android" else "ios.log")
    expected_evidence_paths = {
        "workflow_path": WORKFLOW_PATH,
        "summary_path": ROOT / "tools/ci_summary.py",
        "producer_path": Path(__file__),
        "log_path": log_path,
    }
    if platform == "android":
        expected_evidence_paths.update({
            "driver_path": ROOT / "android/test_ci.sh",
            "lifecycle_path": ROOT / "android/start_emulator_ci.sh",
            "payload_path": ROOT / "android/run_tests.sh",
        })
        build_directory = ROOT / "build/android-ci-x86_64"
    else:
        expected_evidence_paths.update({
            "driver_path": ROOT / "ios/test_ci.sh",
            "lifecycle_path": ROOT / "ios/launch_simulator.sh",
        })
        cmake_arch = "arm64" if architecture == "aarch64" else "x86_64"
        build_directory = ROOT / f"build/ios-simulator-{cmake_arch}"

    evidence = manifest.get("evidence")
    if not isinstance(evidence, dict):
        errors.append("mobile evidence bindings are missing")
        evidence = {}
    for name, expected_path in expected_evidence_paths.items():
        hash_name = name.replace("_path", "_hash")
        try:
            resolved = _regular_file(expected_path, f"mobile {name}")
            if evidence.get(name) != str(resolved):
                errors.append(f"mobile {name} is not the current checkout/runner file")
            if evidence.get(hash_name) != _sha256_file(resolved):
                errors.append(f"mobile {hash_name} does not match the current file")
        except MobileCoverageError as error:
            errors.append(str(error))

    try:
        current_toolchain = _toolchain(build_directory, platform, architecture)
        if manifest.get("toolchain") != current_toolchain:
            errors.append("mobile toolchain identity/configuration no longer matches the retained manifest")
        policy = _policy(platform, architecture)
        current_detected = _detected_rows(platform, _directory(build_directory, "mobile build directory"), policy["expected"], current_toolchain)
        if manifest.get("detected") != current_detected:
            errors.append("mobile artifact/compiler detections no longer match the retained manifest")
        lines = _anchored_lines(log_path, f"{platform} CI log")
        if platform == "android":
            current_runtime = _android_runtime(lines, environment)
        else:
            current_runtime = _ios_runtime(lines, architecture, output_directory)
        if manifest.get("runtime") != current_runtime:
            errors.append("mobile runtime/build-only evidence no longer matches the retained manifest")
    except (MobileCoverageError, OSError, ValueError, TypeError, KeyError) as error:
        errors.append(str(error))

    try:
        lanes = _workflow_mobile_lanes()
        self_test_cases = run_self_test()
        guard = manifest.get("regression_guard")
        expected_guard = {
            "self_test_cases": self_test_cases,
            "workflow_lane_count": len(lanes),
        }
        if guard != expected_guard:
            errors.append("mobile regression guard evidence is incomplete")
    except MobileCoverageError as error:
        errors.append(str(error))

    retention = manifest.get("retention")
    expected_manifest_path = str((output_directory / OUTPUT_NAME).resolve(strict=False))
    if retention != {"manifest_path": expected_manifest_path}:
        errors.append("mobile retained manifest path does not match the runner artifact directory")
    return sorted(set(errors))


def run_self_test() -> int:
    lanes = _workflow_mobile_lanes()
    case_count = 0
    for platform, architecture in (("android", "x86_64"), ("ios", "x86_64"), ("ios", "aarch64")):
        policy = _policy(platform, architecture)
        lane_id = f"mobile/{platform}/{architecture}/source={'1' * 40}/run=1/attempt=1"
        manifest = {
            "schema": SCHEMA_VERSION,
            "kind": KIND,
            "hash_algorithm": "sha256",
            "phase": "complete",
            "mode": "ci",
            "identity": {"lane_id": lane_id},
            "policy": {
                "version": policy["version"],
                "row_count": policy["row_count"],
                "required_count": policy["required_count"],
                "exclusion_count": policy["exclusion_count"],
                "effective_configuration_count": policy["effective_configuration_count"],
                "fingerprint": policy["fingerprint"],
            },
            "expected": copy.deepcopy(policy["expected"]),
            "detected": [
                {
                    "id": row["id"],
                    "state": "available",
                    "result": "passed",
                    "compiler": "clang",
                    "compiler_id": "Clang",
                    "compiler_version": "1.0",
                    "compiler_target": "fixture",
                    "compiler_path": "/fixture/clang",
                    "compiler_hash": "b" * 64,
                    "execution": row["execution"],
                    "artifact_path": f"/fixture/{row['configuration']}",
                    "artifact_hash": "a" * 64,
                    "artifact_size": 1,
                    "artifact_entry_count": 1,
                }
                for row in policy["expected"]
            ],
            "executed": [{
                "lane_id": lane_id,
                "status": "success",
                "evidence": "driver-complete",
                "rows": [row["id"] for row in policy["expected"]],
            }],
            "exclusions": copy.deepcopy(policy["exclusions"]),
            "obligations": copy.deepcopy(policy["obligations"]),
        }
        if _validate_policy_contract(manifest, platform, architecture):
            raise MobileCoverageError(f"self-test rejected valid {platform}/{architecture} policy")
        case_count += 1

        mutations: list[tuple[str, Any]] = []
        missing_release = copy.deepcopy(manifest)
        missing_release["expected"].pop()
        mutations.append(("missing-release", missing_release))
        wrong_execution = copy.deepcopy(manifest)
        wrong_execution["expected"][0]["execution"] = (
            "compile-link-bundle" if platform != "ios" or architecture != "x86_64" else "runtime"
        )
        mutations.append(("wrong-execution", wrong_execution))
        wrong_count = copy.deepcopy(manifest)
        wrong_count["policy"]["effective_configuration_count"] = 1
        mutations.append(("wrong-count", wrong_count))
        missing_rationale = copy.deepcopy(manifest)
        missing_rationale["exclusions"].pop()
        mutations.append(("missing-rationale", missing_rationale))
        wrong_obligation = copy.deepcopy(manifest)
        next(iter(wrong_obligation["obligations"].values()))["reason"] = "weaker-policy"
        mutations.append(("wrong-obligation", wrong_obligation))
        duplicate_detected = copy.deepcopy(manifest)
        duplicate_detected["detected"].append(copy.deepcopy(duplicate_detected["detected"][0]))
        mutations.append(("duplicate-detected", duplicate_detected))
        incomplete_execution = copy.deepcopy(manifest)
        incomplete_execution["executed"][0]["rows"].pop()
        mutations.append(("incomplete-execution", incomplete_execution))
        bad_hash = copy.deepcopy(manifest)
        bad_hash["detected"][0]["artifact_hash"] = "bad"
        mutations.append(("bad-artifact-hash", bad_hash))
        for name, mutation in mutations:
            if not _validate_policy_contract(mutation, platform, architecture):
                raise MobileCoverageError(f"self-test accepted {name} mutation for {platform}/{architecture}")
            case_count += 1
    if len(lanes) != 3:
        raise MobileCoverageError("self-test workflow lane census changed")
    for disposition in (
        "first-attempt-success",
        "continued-original-boot-success",
        "recovered-infrastructure-failure",
    ):
        if _ios_boot_disposition([f"BUSTER_IOS_BOOT_DISPOSITION={disposition}"]) != disposition:
            raise MobileCoverageError(f"self-test rejected successful iOS boot disposition {disposition}")
        case_count += 1
    for name, lines in (
        ("missing", []),
        ("duplicate", ["BUSTER_IOS_BOOT_DISPOSITION=continued-original-boot-success"] * 2),
        ("pending", ["BUSTER_IOS_BOOT_DISPOSITION=continued-original-pending-tests"]),
        ("failed-tests", ["BUSTER_IOS_BOOT_DISPOSITION=continued-boot-but-test-failure"]),
        ("unrecovered", ["BUSTER_IOS_BOOT_DISPOSITION=unrecovered-failure"]),
    ):
        try:
            _ios_boot_disposition(lines)
        except MobileCoverageError:
            pass
        else:
            raise MobileCoverageError(f"self-test accepted {name} iOS boot disposition")
        case_count += 1
    return case_count


def should_prepare(environment: Mapping[str, str]) -> bool:
    required = environment.get("BUSTER_CI_REQUIRED", "").split()
    return environment.get("GITHUB_ACTIONS") == "true" and any(name in ("android", "ios") for name in required)


def _failure_manifest(errors: list[str]) -> dict[str, Any]:
    return {
        "schema": SCHEMA_VERSION,
        "kind": KIND,
        "hash_algorithm": "sha256",
        "phase": "incomplete",
        "mode": "ci",
        "capture_errors": sorted(set(error for error in errors if error)),
    }


def prepare_summary(environment: MutableMapping[str, str]) -> dict[str, Any] | None:
    if not should_prepare(environment):
        return None
    output_directory = Path(environment.get("RUNNER_TEMP", "")) / "buster-ci"
    manifest_path = output_directory / OUTPUT_NAME
    environment["BUSTER_CI_COVERAGE_MANIFEST"] = str(manifest_path)
    environment["BUSTER_CI_COVERAGE_REQUIRED"] = "1"
    manifest: dict[str, Any] | None = None
    errors: list[str] = []
    try:
        self_test_cases = run_self_test()
        workflow_lanes = len(_workflow_mobile_lanes())
        manifest = _capture_manifest(environment, self_test_cases, workflow_lanes)
        errors.extend(validate_manifest(manifest, environment))
    except (MobileCoverageError, OSError, ValueError, TypeError, KeyError, json.JSONDecodeError) as error:
        errors.append(str(error))

    if errors:
        if manifest is None:
            manifest = _failure_manifest(errors)
        else:
            manifest["capture_errors"] = sorted(set(errors))
    try:
        output_directory.mkdir(parents=True, exist_ok=True)
        _atomic_json(manifest_path, manifest)
    except OSError as error:
        errors.append(f"mobile coverage evidence could not be written: {error}")

    errors = sorted(set(error for error in errors if error))
    if errors:
        print("MOBILE_COVERAGE failure: " + "; ".join(errors), file=sys.stderr)
    else:
        print(
            f"MOBILE_COVERAGE success manifest={manifest_path} "
            f"configurations={manifest['policy']['effective_configuration_count'] if manifest else 0}"
        )
    return manifest


def coverage_summary(manifest: Any, errors: list[str]) -> list[str]:
    lines = ["", "### Mobile effective coverage", ""]
    if not isinstance(manifest, dict):
        lines.append("No mobile coverage manifest was produced.")
    else:
        identity = manifest.get("identity", {}) if isinstance(manifest.get("identity"), dict) else {}
        policy = manifest.get("policy", {}) if isinstance(manifest.get("policy"), dict) else {}
        runtime = manifest.get("runtime", {}) if isinstance(manifest.get("runtime"), dict) else {}
        escape = lambda value: html.escape(str(value)).replace("|", "&#124;")
        lines.append(
            "Lane: <code>" + escape(identity.get("lane_id", "missing")) + "</code>; "
            "effective configurations: **" + escape(policy.get("effective_configuration_count", "missing")) + "**; "
            "policy fingerprint: <code>" + escape(policy.get("fingerprint", "missing")) + "</code>."
        )
        lines += [
            "",
            "| Configuration | Compiler identity | Target | Sanitizer | Fuzz | ABI | Execution | Artifact result |",
            "|---|---|---|---|---|---|---|---|",
        ]
        detected = {row.get("id"): row for row in manifest.get("detected", []) if isinstance(row, dict)}
        for row in manifest.get("expected", []):
            if not isinstance(row, dict):
                continue
            observed = detected.get(row.get("id"), {})
            compiler = f"{observed.get('compiler_id', 'missing')} {observed.get('compiler_version', 'missing')}"
            lines.append(
                f"| {escape(row.get('configuration', 'missing'))} | {escape(compiler)} | "
                f"{escape(observed.get('compiler_target', 'missing'))} | "
                f"{'on' if row.get('sanitize') else 'off'} | {'on' if row.get('fuzz') else 'off'} | "
                f"{escape(row.get('abi', 'missing'))} | {escape(row.get('execution', 'missing'))} | "
                f"{escape(observed.get('result', 'missing'))} |"
            )
        runtime_fields = ", ".join(
            f"{name}={runtime.get(name)}"
            for name in ("kind", "execution", "serial", "udid", "runtime_id", "runtime_version", "state")
            if runtime.get(name) not in (None, "")
        ) or "missing"
        lines += ["", "Runtime identity: " + escape(runtime_fields)]
        lines += ["", "| Explicit exclusion | Rationale |", "|---|---|"]
        for exclusion in manifest.get("exclusions", []):
            if isinstance(exclusion, dict):
                lines.append(
                    f"| {escape(exclusion.get('capability', 'missing'))} | {escape(exclusion.get('reason', 'missing'))} |"
                )
        retention = manifest.get("retention", {}) if isinstance(manifest.get("retention"), dict) else {}
        guard = manifest.get("regression_guard", {}) if isinstance(manifest.get("regression_guard"), dict) else {}
        lines += [
            "",
            "Retained manifest: <code>" + escape(retention.get("manifest_path", "missing")) + "</code>.",
            "Regression guard: " + escape(guard.get("self_test_cases", "missing")) +
            " validity/mutation cases; " + escape(guard.get("workflow_lane_count", "missing")) +
            " required workflow lanes.",
        ]
    if errors:
        lines += ["", "Mobile coverage failures: " + "; ".join(html.escape(str(error)) for error in errors)]
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("self-test", help="run deterministic policy and workflow shrinkage controls")
    arguments = parser.parse_args()
    if arguments.command == "self-test":
        try:
            cases = run_self_test()
        except (MobileCoverageError, OSError, ValueError, TypeError, KeyError) as error:
            print(f"MOBILE_COVERAGE_SELF_TEST failure: {error}", file=sys.stderr)
            return 1
        print(f"MOBILE_COVERAGE_SELF_TEST success cases={cases} workflow_lanes=3")
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
