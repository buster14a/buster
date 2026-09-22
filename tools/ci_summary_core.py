#!/usr/bin/env python3
"""Write bounded, fail-closed CI diagnostics without copying the environment.

Inputs are explicit step outcomes, required step IDs, runner metadata, and the
driver-owned coverage manifest (BUSTER_CI_COVERAGE_MANIFEST or
BUSTER_CI_COVERAGE_OUTPUT, required when BUSTER_CI_COVERAGE_REQUIRED=1).
Android lanes also expose bounded wrapper diagnostics from android.log; these
records explain, but never replace, the authoritative step outcome.
"""
import html
import hashlib
import json
import ci_matrix_phases
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


COVERAGE_POLICY_VERSION = 1
_COVERAGE_HEX64 = re.compile(r"^[0-9a-f]{64}$")
_COVERAGE_HEX16 = re.compile(r"^[0-9a-f]{16}$")
_COVERAGE_CHECKOUT_ROOT = Path(__file__).resolve().parents[1]

# Compact, independently reviewed policy anchors. The driver remains the
# source of row construction; these six versioned digests/counts prevent a
# result producer from shrinking its own expected set and then re-signing it.
_COVERAGE_POLICY_ANCHORS = {
    ("linux", "x86_64"): (20, 5, 15, "071e0d8ff5c7d954"),
    ("linux", "aarch64"): (20, 5, 15, "e1a4270f4c9b81da"),
    ("macos", "x86_64"): (23, 5, 18, "63bcfb8fade23151"),
    ("macos", "aarch64"): (23, 5, 18, "674f7970f517e29b"),
    ("windows", "x86_64"): (28, 6, 22, "46ffb69c2ceae9c0"),
    ("windows", "aarch64"): (19, 2, 17, "709a010f922e385b"),
}
def assess(steps, required):
    missing = [name for name in required if steps.get(name, {}).get("outcome") != "success"]
    failed = [name for name, step in steps.items() if step.get("outcome") in ("failure", "cancelled")]
    return sorted(set(missing + failed))

# Read only the wrapper's anchored records, never terminal-marker substrings,
# emulator diagnostics, or workflow command echoes. This is an explanation of
# the existing step outcome, not a second test oracle or a replacement gate.
_ANDROID_LOG_MAX_BYTES = 64 * 1024 * 1024
_ANDROID_LOG_LINE_BYTES = 4096
_ANDROID_STATUS = r"(?:0|[1-9][0-9]{0,2})"
_ANDROID_CONFIG = r"(?P<config>Debug|Release)"
_ANDROID_DEADLINE = r"[1-9][0-9]{0,8}"
_ANDROID_SECONDS = r"(?:0|[1-9][0-9]{0,8})"
# Producer statuses GNU timeout reports for an exhausted payload deadline.
_ANDROID_DEADLINE_STATUSES = ("124", "137")
_ANDROID_RECORDS = {
    "ANDROID_CONFIG_RESULT": ("config", re.compile(
        rf"ANDROID_CONFIG_RESULT config={_ANDROID_CONFIG} status=(?P<status>{_ANDROID_STATUS}|not-run)")),
    "ANDROID_PAYLOAD_RESULT": ("payload", re.compile(
        rf"ANDROID_PAYLOAD_RESULT config={_ANDROID_CONFIG} phase=(?P<phase>wait-device|install|launch|monitor) status=(?P<status>{_ANDROID_STATUS})")),
    "ANDROID_MONITOR_RESULT": ("monitor", re.compile(
        rf"ANDROID_MONITOR_RESULT config={_ANDROID_CONFIG} reader_status=(?P<reader_status>{_ANDROID_STATUS}) producer_status=(?P<producer_status>{_ANDROID_STATUS}) timeout_seconds=(?P<timeout_seconds>{_ANDROID_DEADLINE}) elapsed_seconds=(?P<elapsed_seconds>{_ANDROID_SECONDS}) headroom_seconds=(?P<headroom_seconds>{_ANDROID_SECONDS}) headroom_warning=(?P<headroom_warning>yes|no)")),
    "ANDROID_BATCH_RESULT": ("batch", re.compile(
        rf"ANDROID_BATCH_RESULT phase=(?P<phase>configure|build|boot-wait|tests) config=(?P<config>none|Debug|Release) status=(?P<status>{_ANDROID_STATUS}) cleanup_status=(?P<cleanup_status>{_ANDROID_STATUS}|not-run)")),
    "ANDROID_CI_RESULT": ("ci", re.compile(
        rf"ANDROID_CI_RESULT phase=(?P<phase>start|tests) payload_status=(?P<payload_status>{_ANDROID_STATUS}) cleanup_status=(?P<cleanup_status>{_ANDROID_STATUS}|not-run) status=(?P<status>{_ANDROID_STATUS})")),
}


def _android_diagnostics(path):
    result = {"configurations": {"Debug": {}, "Release": {}}, "batch": None, "ci": None, "warnings": []}
    warnings = set()
    seen = set()
    try:
        if path.is_symlink() or not path.is_file():
            raise OSError("not a regular Android log")
        with path.open("rb") as stream:
            remaining = _ANDROID_LOG_MAX_BYTES
            line_start = True
            while remaining:
                chunk = stream.readline(min(_ANDROID_LOG_LINE_BYTES, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
                complete = chunk.endswith(b"\n")
                if line_start and complete:
                    line = chunk.rstrip(b"\r\n").decode("ascii", errors="replace")
                    name = line.partition(" ")[0]
                    if name in _ANDROID_RECORDS:
                        kind, expression = _ANDROID_RECORDS[name]
                        match = expression.fullmatch(line)
                        record = match.groupdict() if match else None
                        if record is None or any(int(value) > 255 for key, value in record.items()
                                                 if key.endswith("status") and value != "not-run"):
                            warnings.add(f"Malformed {name} record ignored.")
                        else:
                            config = record.get("config") if kind in ("config", "payload", "monitor") else None
                            target = result["configurations"][config] if config else result
                            identity = (kind, config)
                            if identity in seen:
                                target[kind] = None
                                warnings.add(f"Duplicate {name} ({config or 'batch-wide'}) is ambiguous.")
                            else:
                                target[kind] = record
                                seen.add(identity)
                line_start = complete
            if not line_start:
                warnings.add("An overlong or unterminated Android log line was ignored.")
            if remaining == 0 and stream.read(1):
                warnings.add("Android diagnostic scan truncated at its byte limit; some records may be missing.")
    except OSError:
        warnings.add("Android log unavailable; no configuration or cleanup outcome can be inferred.")
    result["warnings"] = sorted(warnings)
    return result


def _android_summary(diagnostics):
    lines = ["", "### Android configuration and cleanup diagnostics", "",
             "Reported exit statuses from android.log; the workflow step outcome above remains authoritative.",
             "A later Release success does not clear an earlier Debug failure. Missing or ambiguous records are not proof of success.", "",
             "| Configuration | Batch config status | Payload phase | Payload status | Monitor reader / producer | Deadline (s) | Payload elapsed (s) | Headroom (s) |",
             "|---|---|---|---|---|---|---|---|"]
    deadlines = []
    for config, records in diagnostics["configurations"].items():
        config_result = records.get("config") or {}
        payload = records.get("payload") or {}
        monitor = records.get("monitor") or {}
        lines.append(f"| {config} | {config_result.get('status', 'missing')} | {payload.get('phase', 'missing')} | "
                     f"{payload.get('status', 'missing')} | {monitor.get('reader_status', 'missing')} / "
                     f"{monitor.get('producer_status', 'missing')} | {monitor.get('timeout_seconds', 'missing')} | "
                     f"{monitor.get('elapsed_seconds', 'missing')} | {monitor.get('headroom_seconds', 'missing')} |")
        if monitor.get("reader_status") == "0" and monitor.get("producer_status") in _ANDROID_DEADLINE_STATUSES:
            deadlines.append(f"{config} exhausted its {monitor['timeout_seconds']}s payload deadline without a terminal "
                             f"result (monitor producer status {monitor['producer_status']}). This is a payload timeout; "
                             "emulator cleanup runs afterwards and is not this failure.")
        elif monitor.get("headroom_warning") == "yes":
            deadlines.append(f"{config} passed {monitor['headroom_seconds']}s inside its {monitor['timeout_seconds']}s "
                             f"payload deadline ({monitor['elapsed_seconds']}s elapsed). The wrapper flagged this margin "
                             "as thin: a slower runner or added tests can cross it.")
    if deadlines:
        lines += [""] + [f"Payload deadline: {note}" for note in deadlines]
    batch = diagnostics["batch"]
    ci = diagnostics["ci"]
    if batch:
        lines += ["", f"Batch result: phase={batch['phase']}, first failed configuration={batch['config']}, "
                  f"status={batch['status']}, cleanup_status={batch['cleanup_status']}."]
    else:
        lines += ["", "Batch result: missing or ambiguous."]
    if ci:
        lines += ["", f"Final CI result: phase={ci['phase']}, payload_status={ci['payload_status']}, "
                  f"cleanup_status={ci['cleanup_status']}, status={ci['status']}."]
        if ci["payload_status"] != "0":
            lines += ["", "Android CI failed before final emulator cleanup; cleanup output is not the original payload failure."]
        elif ci["status"] != "0" and ci["cleanup_status"] not in ("0", "not-run"):
            lines += ["", "Android CI required emulator cleanup failed after a successful payload."]
    else:
        lines += ["", "Final CI result: missing or ambiguous."]
    if diagnostics["warnings"]:
        lines += ["", "Diagnostic limitations: " + " ".join(diagnostics["warnings"])]
    return lines


def _coverage_row_id(identity, row):
    def state(name):
        return "on" if row.get(name) else "off"

    return "/".join((identity.get("suite", ""), "combinations", identity.get("platform", ""), identity.get("architecture", ""),
                     f"compiler={row.get('compiler', '')}", f"configuration={row.get('configuration', '')}", f"sanitize={state('sanitize')}",
                     f"fuzz={state('fuzz')}", f"unity={state('unity')}", f"execution={row.get('execution', '')}"))


def _coverage_row_owner(row):
    # Independently check the native driver's semantic partition. Never let
    # an evidence file choose its own ownership or shrink the full policy.
    return "release" if row.get("compiler") == "clang" and not row.get("sanitize") and row.get("optimize") else "checks"


def _coverage_selected_ids(rows, shard):
    return {row_id for row_id, row in rows.items() if row.get("state") == "required" and
            (shard == "combinations" or _coverage_row_owner(row) == shard)}


def _coverage_runner_identity(environment):
    platform_value = environment.get("BUSTER_CI_COVERAGE_PLATFORM")
    architecture_value = environment.get("BUSTER_CI_COVERAGE_ARCH")
    if not platform_value:
        runner_os = str(environment.get("RUNNER_OS", "")).lower()
        platform_value = "windows" if "windows" in runner_os else "macos" if "mac" in runner_os else "linux" if "linux" in runner_os else ""
    if not architecture_value:
        runner_arch = str(environment.get("RUNNER_ARCH", "")).lower()
        architecture_value = "aarch64" if runner_arch in ("arm64", "aarch64") or "arm" in runner_arch else \
                             "x86_64" if runner_arch in ("x64", "amd64", "x86_64") else ""
    platform_aliases = {"linux": "linux", "macos": "macos", "darwin": "macos", "windows": "windows"}
    architecture_aliases = {"x86_64": "x86_64", "x64": "x86_64", "amd64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}
    return {"platform": platform_aliases.get(str(platform_value).lower(), ""),
            "architecture": architecture_aliases.get(str(architecture_value).lower(), "")}


def _coverage_target_contains(value, *needles):
    value = str(value).lower()
    return any(needle in value for needle in needles)


def _coverage_target_matches(platform, architecture, compiler, observed):
    if compiler == "cl":
        return platform == "windows" and ((architecture == "aarch64" and observed.lower() == "arm64") or
                                           (architecture == "x86_64" and observed.lower() in ("x64", "amd64")))
    architecture_markers = (("aarch64", "arm64") if architecture == "aarch64" else
                            ("x86_64", "x86-64", "amd64"))
    conflicting_architecture_markers = (("x86_64", "x86-64", "amd64") if architecture == "aarch64" else
                                        ("aarch64", "arm64"))
    architecture_matches = _coverage_target_contains(observed, *architecture_markers)
    architecture_conflict = _coverage_target_contains(observed, *conflicting_architecture_markers)
    platform_markers = (("windows", "mingw", "w64", "msvc") if platform == "windows" else
                        ("apple", "macos", "darwin") if platform == "macos" else ("linux",))
    all_platform_markers = ("windows", "mingw", "w64", "msvc", "apple", "macos", "darwin", "linux")
    platform_matches = _coverage_target_contains(observed, *platform_markers)
    platform_conflict = _coverage_target_contains(observed, *(marker for marker in all_platform_markers if marker not in platform_markers))
    return architecture_matches and not architecture_conflict and platform_matches and not platform_conflict


def _coverage_version_matches(compiler, version):
    lowered = str(version).lower()
    if compiler == "gcc":
        return "gcc" in lowered or "gnu" in lowered
    if compiler == "clang":
        return "clang" in lowered
    if compiler == "zig":
        return re.match(r"^\d+\.\d+\.\d+", lowered) is not None
    return compiler == "cl" and any(character.isdigit() for character in lowered)


def _coverage_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _coverage_expected_path(environment, identity, kind, mode):
    """Return the consumer's checkout/driver path expectation.

    Production paths are derived from the checked-out repository and the
    documented bootstrap location, so a manifest cannot select its own
    evidence files. The self-test accepts an explicit fixture driver only
    because its producer intentionally lives in a temporary directory.
    """
    if mode == "self-test":
        fixture_name = f"BUSTER_CI_COVERAGE_EXPECTED_{kind.upper()}_PATH"
        fixture_path = environment.get(fixture_name)
        if fixture_path:
            return Path(fixture_path).resolve(strict=False)
    if kind == "source":
        return (_COVERAGE_CHECKOUT_ROOT / "build.c").resolve(strict=False)
    executable_name = "build.exe" if identity.get("platform") == "windows" else "build"
    return (_COVERAGE_CHECKOUT_ROOT / "build" / executable_name).resolve(strict=False)


def _coverage_expected_compiler_path(environment, platform, compiler):
    """Resolve the compiler path expected by the consumer's lane environment."""
    requested = {
        "cl": "cl",
        "clang": "clang",
        "gcc": "gcc-15" if platform == "macos" else "gcc",
        "zig": "zig",
    }.get(compiler)
    if compiler == "gcc":
        requested = environment.get("BUSTER_GCC", os.environ.get("BUSTER_GCC", requested))
    if not requested:
        return None
    requested_path = Path(requested)
    has_separator = "/" in requested or "\\" in requested
    if requested_path.is_absolute() or has_separator:
        resolved = requested_path.resolve(strict=False)
    else:
        resolved_name = shutil.which(requested, path=environment.get("PATH"))
        if not resolved_name:
            return None
        resolved = Path(resolved_name).resolve(strict=False)
    return resolved if resolved.is_file() else None


def _coverage_probe_output(path, arguments, environment, prefer_stderr=False):
    probe_environment = os.environ.copy()
    probe_environment.update({key: value for key, value in environment.items() if isinstance(value, str)})
    try:
        completed = subprocess.run([str(path), *arguments], cwd=_COVERAGE_CHECKOUT_ROOT, env=probe_environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if completed.returncode != 0:
        return None
    output = completed.stderr if prefer_stderr and completed.stderr.strip() else \
        (completed.stdout if completed.stdout.strip() else completed.stderr)
    try:
        return output.strip().decode("utf-8")
    except UnicodeDecodeError:
        return None


def _coverage_probe_compiler(path, compiler, environment):
    if compiler == "cl":
        target = environment.get("VSCMD_ARG_TGT_ARCH", os.environ.get("VSCMD_ARG_TGT_ARCH", ""))
        # A bare cl.exe /Bv exits with D8003 because no source is supplied.
        # Preprocess the stable identity fixture as C, without emitting an
        # object or executable, while retaining /Bv's exact version evidence.
        version = _coverage_probe_output(path, ("/Bv", "/EP", "/TC", "tests/build_compiler_identity.h"), environment,
                                         prefer_stderr=True)
        version = version.splitlines()[0].strip() if version else None
        return {"identity": "BUSTER_BUILD_COMPILER_MSVC", "target": target, "version": version} if target and version else None
    if compiler == "zig":
        version = _coverage_probe_output(path, ("version",), environment)
        target = _coverage_probe_output(path, ("cc", "-dumpmachine"), environment)
        return {"identity": "BUSTER_BUILD_COMPILER_ZIG", "target": target, "version": version} \
            if target and version else None
    identity = _coverage_probe_output(path, ("-E", "-P", "-x", "c", "tests/build_compiler_identity.h"), environment)
    target = _coverage_probe_output(path, ("-dumpmachine",), environment)
    version = _coverage_probe_output(path, ("--version",), environment)
    return {"identity": identity, "target": target, "version": version} if identity and target and version else None


def _coverage_flag_is_on(environment, name):
    return str(environment.get(name, "")).lower() in ("on", "true", "yes", "1")


def _coverage_expected_obligations(identity, mode, environment, has_unity):
    """Derive lane obligations independently from manifest-observed values."""
    if mode == "self-test":
        reason = "coverage-manifest-self-test-only"
        return {name: ("not-applicable", reason) for name in ("self_host", "fixed_point", "unity_analysis", "table_audit")}

    if identity.get("shard") == "checks":
        return {name: ("not-applicable", "owned-by-release-shard")
                for name in ("self_host", "fixed_point", "unity_analysis", "table_audit")}

    platform = identity.get("platform")
    architecture = identity.get("architecture")
    direct = _coverage_flag_is_on(environment, "BUSTER_MATRIX_DIRECT") or (platform == "macos" and architecture == "x86_64")
    forced = _coverage_flag_is_on(environment, "BUSTER_TEST_FORCE_ARTIFACT_FANOUT")
    fanout_requested = forced or (mode == "ci" and (platform == "macos" or (architecture == "x86_64" and platform in ("linux", "windows"))))
    self_host = fanout_requested and not direct
    self_reason = "canonical-release-fanout" if self_host else (
        "direct-matrix-does-not-consume-fanout" if direct else "superbuild-does-not-consume-fanout")
    fixed_reason = "canonical-release-fanout" if self_host else (
        "direct-matrix-does-not-run-self-host" if direct else "superbuild-does-not-run-self-host")
    unity_reason = "canonical-clang-release" if has_unity else "no-canonical-clang-release"
    table_reason = unity_reason if not has_unity else ("direct-matrix-default-audit" if direct else "canonical-superbuild-tree")
    return {
        "self_host": ("scheduled" if self_host else "not-applicable", self_reason),
        "fixed_point": ("scheduled" if self_host else "not-applicable", fixed_reason),
        "unity_analysis": ("scheduled" if has_unity else "not-applicable", unity_reason),
        "table_audit": ("scheduled" if has_unity else "not-applicable", table_reason),
    }


def _coverage_policy_fingerprint(identity, rows):
    value = 1469598103934665603

    def add(text):
        nonlocal value
        for byte in str(text).encode("utf-8") + b"\0":
            value ^= byte
            value = (value * 1099511628211) & ((1 << 64) - 1)

    add("matrix-policy-v1")
    for row in rows:
        add(row.get("id", ""))
        add(row.get("exclusion", ""))
    return f"{value:016x}"


def _coverage_strict_hash(value, width):
    expression = _COVERAGE_HEX64 if width == 64 else _COVERAGE_HEX16
    return isinstance(value, str) and expression.fullmatch(value) is not None


def validate_coverage_manifest(manifest, environment=None, *, expected_mode="ci"):
    """Validate the caller's contract; fixture/local modes require explicit opt-in."""
    errors = []
    environment = environment or {}
    if not isinstance(manifest, dict):
        return ["coverage manifest is not an object"]
    if manifest.get("schema") != 1 or manifest.get("kind") != "desktop-matrix-coverage" or manifest.get("hash_algorithm") != "sha256":
        errors.append("coverage manifest schema/kind is unsupported")
    if manifest.get("phase") != "complete":
        errors.append("coverage manifest is not complete")
    mode = manifest.get("mode")
    # The evidence cannot choose a weaker policy. Production callers use the
    # default; only explicit Python callers may authorize fixture/local modes.
    if mode != expected_mode:
        errors.append("coverage manifest mode does not match consumer expectation")
    if mode not in ("ci", "local", "self-test"):
        errors.append("coverage manifest mode is unsupported")
    identity = manifest.get("identity")
    if not isinstance(identity, dict):
        return errors + ["coverage identity is missing"]
    identity_names = ("lane_id", "suite", "shard", "platform", "architecture", "source_revision", "repository", "ref", "run_id",
                      "run_attempt", "source_path", "source_hash", "driver_path", "driver_hash")
    for name in identity_names:
        if not isinstance(identity.get(name), str) or not identity[name]:
            errors.append(f"coverage identity field {name} is missing")
    expected_lane = "/".join((identity.get("suite", ""), identity.get("shard", ""), identity.get("platform", ""), identity.get("architecture", ""),
                               f"source={identity.get('source_revision', '')}", f"run={identity.get('run_id', '')}", f"attempt={identity.get('run_attempt', '')}"))
    if identity.get("lane_id") != expected_lane:
        errors.append("coverage lane identity is malformed")
    expected_shard = environment.get("BUSTER_MATRIX_SHARD") or "all"
    if expected_shard == "all":
        expected_shard = "combinations"
    if expected_shard not in ("combinations", "release", "checks"):
        errors.append("coverage consumer shard is unsupported")
    if identity.get("suite") != "desktop" or identity.get("shard") != expected_shard:
        errors.append("coverage lane does not match the requested desktop shard")
    if expected_shard != "combinations" and manifest.get("partition_version") != 1:
        errors.append("coverage partition version is unsupported")
    for name in ("source_hash", "driver_hash"):
        if not _coverage_strict_hash(identity.get(name), 64):
            errors.append(f"coverage {name} is not a strict SHA-256 digest")
    for name in ("source_path", "driver_path"):
        value = identity.get(name)
        if not isinstance(value, str) or not Path(value).is_absolute():
            errors.append(f"coverage {name} is not an absolute resolved path")
        else:
            try:
                resolved = Path(value).resolve(strict=True)
                if os.name == "nt":
                    # GitHub's Windows workspace/temp roots may be filesystem
                    # aliases. Require a canonical lexical path here, then
                    # bind the resolved file to its expected path and hash
                    # below instead of rejecting the runner's mount alias.
                    if os.path.normcase(os.path.normpath(value)) != os.path.normcase(value):
                        errors.append(f"coverage {name} is not a canonical path")
                elif resolved != Path(value):
                    errors.append(f"coverage {name} is not the resolved real path")
                if name == "source_path" and _coverage_sha256(resolved) != identity.get("source_hash"):
                    errors.append("coverage source hash does not match source path")
                if name == "driver_path" and _coverage_sha256(resolved) != identity.get("driver_hash"):
                    errors.append("coverage driver hash does not match driver path")
            except (OSError, ValueError):
                errors.append(f"coverage {name} cannot be read")
    for name in ("source", "driver"):
        expected_path = _coverage_expected_path(environment, identity, name, mode)
        actual_name = f"{name}_path"
        actual_path = identity.get(actual_name)
        if isinstance(actual_path, str) and actual_path:
            try:
                if Path(actual_path).resolve(strict=True) != expected_path.resolve(strict=True):
                    errors.append(f"coverage {actual_name} is outside the consumer checkout expectation")
            except (OSError, ValueError):
                errors.append(f"coverage expected {actual_name} cannot be resolved")
    environment_values = _coverage_runner_identity(environment)
    environment_values.update({"source_revision": environment.get("GITHUB_SHA"), "repository": environment.get("GITHUB_REPOSITORY"),
                               "ref": environment.get("GITHUB_REF"), "run_id": environment.get("GITHUB_RUN_ID"), "run_attempt": environment.get("GITHUB_RUN_ATTEMPT")})
    for identity_name, value in environment_values.items():
        if value and value != "local" and value != identity.get(identity_name):
            errors.append(f"coverage identity does not match {identity_name}")

    expected = manifest.get("expected")
    detected = manifest.get("detected")
    executed = manifest.get("executed")
    if not isinstance(expected, list) or not expected:
        errors.append("coverage expected rows are missing")
        expected = []
    if not isinstance(detected, list):
        errors.append("coverage detected rows are missing")
        detected = []
    if not isinstance(executed, list):
        errors.append("coverage execution records are missing")
        executed = []
    if any(name in manifest for name in ("modes", "differential", "analysis", "table_audit", "participation")):
        errors.append("desktop coverage cannot claim native or analyzer lane obligations")
    policy = manifest.get("policy")
    if not isinstance(policy, dict):
        errors.append("coverage policy counts are missing")
        policy = {}
    if policy.get("version") != COVERAGE_POLICY_VERSION:
        errors.append("coverage policy version is unsupported")
    if not _coverage_strict_hash(policy.get("fingerprint"), 16):
        errors.append("coverage policy fingerprint is malformed")
    for count_name in ("row_count", "required_count", "excluded_count"):
        count = policy.get(count_name)
        if not isinstance(count, int) or isinstance(count, bool) or count < 0:
            errors.append(f"coverage policy count {count_name} is malformed")
    obligations = manifest.get("obligations")
    obligation_names = ("self_host", "fixed_point", "unity_analysis", "table_audit")
    if not isinstance(obligations, dict) or set(obligations) != set(obligation_names):
        errors.append("coverage obligations must describe only the combinations lane")
        obligations = {}
    for name in obligation_names:
        obligation = obligations.get(name)
        if not isinstance(obligation, dict) or set(obligation) != {"state", "reason"}:
            errors.append(f"coverage obligation {name} is malformed")
            continue
        if obligation.get("state") not in ("scheduled", "not-applicable"):
            errors.append(f"coverage obligation {name} has an unsupported state")
        if not isinstance(obligation.get("reason"), str) or not obligation["reason"]:
            errors.append(f"coverage obligation {name} has no reason")
    obligation_states = {name: obligations.get(name, {}).get("state") if isinstance(obligations.get(name), dict) else None
                         for name in obligation_names}
    obligation_reasons = {name: obligations.get(name, {}).get("reason") if isinstance(obligations.get(name), dict) else None
                          for name in obligation_names}
    if obligations:
        if obligation_states["self_host"] and obligation_states["fixed_point"] and \
                obligation_states["self_host"] != obligation_states["fixed_point"]:
            errors.append("coverage self-host and fixed-point obligations disagree")
        if obligation_states["self_host"] == "scheduled" and obligation_reasons["self_host"] != "canonical-release-fanout":
            errors.append("coverage self-host reason is not the canonical fan-out policy")
        if obligation_states["fixed_point"] == "scheduled" and obligation_reasons["fixed_point"] != "canonical-release-fanout":
            errors.append("coverage fixed-point reason is not the canonical fan-out policy")
        if obligation_states["self_host"] == "not-applicable" and obligation_reasons["self_host"] not in (
                "direct-matrix-does-not-consume-fanout", "superbuild-does-not-consume-fanout", "coverage-manifest-self-test-only", "owned-by-release-shard"):
            errors.append("coverage self-host exclusion reason is not explicit")
        if obligation_states["fixed_point"] == "not-applicable" and obligation_reasons["fixed_point"] not in (
                "direct-matrix-does-not-run-self-host", "superbuild-does-not-run-self-host", "coverage-manifest-self-test-only", "owned-by-release-shard"):
            errors.append("coverage fixed-point exclusion reason is not explicit")
        if obligation_states["table_audit"] == "scheduled" and obligation_states["unity_analysis"] != "scheduled":
            errors.append("coverage table audit is scheduled without unity analysis")
        if obligation_states["unity_analysis"] == "scheduled" and obligation_reasons["unity_analysis"] != "canonical-clang-release":
            errors.append("coverage unity-analysis reason is not the canonical release policy")
        if obligation_states["table_audit"] == "scheduled" and obligation_reasons["table_audit"] not in (
                "canonical-superbuild-tree", "direct-matrix-default-audit"):
            errors.append("coverage table-audit reason is not a lane-owned policy")
        if obligation_states["unity_analysis"] == "not-applicable" and obligation_reasons["unity_analysis"] not in (
                "no-canonical-clang-release", "coverage-manifest-self-test-only", "owned-by-release-shard"):
            errors.append("coverage unity-analysis exclusion reason is not explicit")
        if obligation_states["table_audit"] == "not-applicable" and obligation_reasons["table_audit"] not in (
                "no-canonical-clang-release", "coverage-manifest-self-test-only", "owned-by-release-shard"):
            errors.append("coverage table-audit exclusion reason is not explicit")
    expected_by_id = {}
    for row in expected:
        if not isinstance(row, dict):
            errors.append("coverage expected row is malformed")
            continue
        row_id = row.get("id")
        if not isinstance(row_id, str) or not row_id or row_id in expected_by_id:
            errors.append("coverage expected row identity is missing or duplicated")
            continue
        expected_by_id[row_id] = row
        if row_id != _coverage_row_id(identity, row):
            errors.append(f"coverage expected row identity is not semantic: {row_id}")
        if (expected_shard != "combinations" or "owner_shard" in row) and row.get("owner_shard") != _coverage_row_owner(row):
            errors.append(f"coverage row owner does not match the semantic partition: {row_id}")
        if row.get("compiler") not in ("cl", "clang", "gcc", "zig") or row.get("configuration") not in ("Debug", "Release"):
            errors.append(f"coverage expected row has unsupported compiler/configuration: {row_id}")
        if not all(isinstance(row.get(name), bool) for name in ("optimize", "sanitize", "fuzz", "unity")):
            errors.append(f"coverage expected row policy is malformed: {row_id}")
        elif row.get("optimize") != (row.get("configuration") == "Release"):
            errors.append(f"coverage optimization does not match configuration: {row_id}")
        elif row.get("unity") != (row.get("compiler") == "clang" and not row.get("sanitize") and row.get("optimize")):
            errors.append(f"coverage unity policy does not match configuration: {row_id}")
        state = row.get("state")
        exclusion = row.get("exclusion")
        execution_name = row.get("execution")
        if state == "excluded":
            if not isinstance(exclusion, str) or not exclusion or execution_name != "none":
                errors.append(f"coverage exclusion is not explicit: {row_id}")
        elif state == "required":
            if exclusion != "" or execution_name not in ("runtime", "compile-link", "package-only"):
                errors.append(f"coverage required execution is malformed: {row_id}")
            elif execution_name != "package-only" and execution_name != ("runtime" if row.get("compiler") == "clang" else "compile-link"):
                errors.append(f"coverage execution kind does not match compiler: {row_id}")
        else:
            errors.append(f"coverage expected state is malformed: {row_id}")

    if mode in ("ci", "local"):
        policy_anchor = _COVERAGE_POLICY_ANCHORS.get((identity.get("platform"), identity.get("architecture")))
        if policy_anchor is None:
            errors.append("coverage lane has no independently reviewed policy anchor")
        else:
            expected_row_count, expected_required_count, expected_excluded_count, expected_fingerprint = policy_anchor
            if policy.get("version") != COVERAGE_POLICY_VERSION:
                errors.append("coverage policy version does not match the lane anchor")
            for name, expected_count in (("row_count", expected_row_count), ("required_count", expected_required_count),
                                         ("excluded_count", expected_excluded_count)):
                if policy.get(name) != expected_count:
                    errors.append(f"coverage policy {name} does not match the lane anchor")
            if policy.get("fingerprint") != expected_fingerprint:
                errors.append("coverage policy fingerprint does not match the lane anchor")
    elif mode == "self-test":
        if policy.get("row_count") != 2 or policy.get("required_count") != 1 or policy.get("excluded_count") != 1:
            errors.append("coverage self-test policy counts do not match the fixed fixture contract")

    expected_compiler_paths = {}
    compiler_probes = {}
    detected_by_id = {}
    for row in detected:
        if not isinstance(row, dict):
            errors.append("coverage detected row is malformed")
            continue
        row_id = row.get("id")
        if not isinstance(row_id, str) or not row_id or row_id in detected_by_id:
            errors.append("coverage detected row identity is missing or duplicated")
            continue
        detected_by_id[row_id] = row
        expected_row = expected_by_id.get(row_id)
        if expected_row is None:
            errors.append(f"coverage detected row is not expected: {row_id}")
            continue
        if row.get("compiler") != expected_row.get("compiler"):
            errors.append(f"coverage detected compiler disagrees with expected row: {row_id}")
        excluded = expected_row.get("state") == "excluded"
        if excluded:
            if row.get("state") != "excluded" or row.get("reason") != expected_row.get("exclusion") or \
                    any(row.get(name) not in ("", None) for name in ("path", "path_hash", "identity", "target", "version")):
                errors.append(f"coverage excluded row changed state: {row_id}")
            continue
        if row.get("state") != "available" or not all(isinstance(row.get(name), str) and row[name] and
                                                        row[name] not in ("local", "unknown", "unavailable")
                                                        for name in ("path", "path_hash", "identity", "target", "version")):
            errors.append(f"coverage required capability is unavailable: {row_id}")
            continue
        resolved = None
        if not Path(row["path"]).is_absolute() or not _coverage_strict_hash(row["path_hash"], 64):
            errors.append(f"coverage compiler path/hash binding is malformed: {row_id}")
        else:
            try:
                resolved = Path(row["path"]).resolve(strict=True)
                if resolved != Path(row["path"]):
                    errors.append(f"coverage compiler path is not the resolved real path: {row_id}")
                if _coverage_sha256(resolved) != row["path_hash"]:
                    errors.append(f"coverage compiler hash does not match executable: {row_id}")
            except (OSError, ValueError):
                errors.append(f"coverage compiler executable cannot be read: {row_id}")
        compiler = expected_row.get("compiler")
        if compiler not in expected_compiler_paths:
            expected_compiler_paths[compiler] = _coverage_expected_compiler_path(
                environment, identity.get("platform"), compiler)
        expected_compiler_path = expected_compiler_paths.get(compiler)
        if expected_compiler_path is None:
            errors.append(f"coverage expected compiler {compiler} is not available to the consumer: {row_id}")
        elif resolved is not None and resolved != expected_compiler_path:
            errors.append(f"coverage compiler path is not the consumer-selected executable: {row_id}")
        probe_key = (compiler, str(resolved)) if resolved is not None else (compiler, "")
        if probe_key not in compiler_probes:
            compiler_probes[probe_key] = _coverage_probe_compiler(resolved, compiler, environment) if resolved is not None else None
        compiler_probe = compiler_probes.get(probe_key)
        if compiler_probe is None or any(row.get(name) != compiler_probe.get(name) for name in ("identity", "target", "version")):
            errors.append(f"coverage compiler probe disagrees with detected identity: {row_id}")
        identity_name = row.get("identity")
        valid_identity = {"cl": "BUSTER_BUILD_COMPILER_MSVC", "clang": "BUSTER_BUILD_COMPILER_CLANG",
                          "gcc": "BUSTER_BUILD_COMPILER_GNU", "zig": "BUSTER_BUILD_COMPILER_ZIG"}.get(compiler)
        if identity_name != valid_identity:
            errors.append(f"coverage compiler identity does not match logical family: {row_id}")
        if not _coverage_target_matches(identity.get("platform"), identity.get("architecture"), compiler, row.get("target", "")):
            errors.append(f"coverage compiler target does not match lane: {row_id}")
        if not _coverage_version_matches(compiler, row.get("version", "")):
            errors.append(f"coverage compiler version does not match logical family: {row_id}")
        if row.get("reason") != "":
            errors.append(f"coverage available row has a failure reason: {row_id}")
    if set(expected_by_id) != set(detected_by_id):
        errors.append("coverage expected and detected row sets differ")
    required_count = sum(row.get("state") == "required" for row in expected_by_id.values())
    excluded_count = sum(row.get("state") == "excluded" for row in expected_by_id.values())
    if policy.get("row_count") != len(expected_by_id) or policy.get("required_count") != required_count or \
            policy.get("excluded_count") != excluded_count:
        errors.append("coverage policy counts do not match the authoritative expected rows")
    if _coverage_strict_hash(policy.get("fingerprint"), 16) and \
            policy.get("fingerprint") != _coverage_policy_fingerprint(identity, expected):
        errors.append("coverage policy fingerprint does not match expected rows")
    required_ids = _coverage_selected_ids(expected_by_id, expected_shard)
    if not required_ids:
        errors.append("coverage shard has no required configurations")
    has_unity = any(row_id in required_ids and row.get("unity") for row_id, row in expected_by_id.items())
    expected_obligations = _coverage_expected_obligations(identity, mode, environment, has_unity)
    for name, (expected_state, expected_reason) in expected_obligations.items():
        if obligation_states.get(name) != expected_state or obligation_reasons.get(name) != expected_reason:
            errors.append(f"coverage {name} obligation does not match the independent lane policy")
    probe_count = manifest.get("capability_probe_count")
    paths = {(expected_by_id.get(row_id, {}).get("compiler"), row.get("path"), row.get("path_hash")) for row_id, row in detected_by_id.items()
             if expected_by_id.get(row_id, {}).get("state") == "required" and isinstance(row.get("path"), str)}
    if not isinstance(probe_count, int) or isinstance(probe_count, bool) or probe_count != len(paths) or probe_count <= 0:
        errors.append("coverage capability probe count does not match detected identities")

    if mode == "ci" and environment.get("BUSTER_TEST_TABLE_AUDITS") not in (None, ""):
        errors.append("BUSTER_TEST_TABLE_AUDITS cannot override CI coverage policy")

    executed_ids = []
    if len(executed) != 1:
        errors.append("coverage execution must contain exactly one lane completion record")
    for record in executed:
        if not isinstance(record, dict) or record.get("lane_id") != identity.get("lane_id") or record.get("status") != "success":
            errors.append("coverage execution record is not a successful lane completion")
            continue
        if any(name in record for name in ("modes", "differential", "analysis", "table_audit", "categories")):
            errors.append("desktop completion cannot satisfy native or analyzer obligations")
        if record.get("evidence") != "driver-complete" or not isinstance(record.get("rows"), list):
            errors.append("coverage execution evidence is malformed")
            continue
        executed_ids.extend(record["rows"])
    if not all(isinstance(row_id, str) for row_id in executed_ids) or len(executed_ids) != len(set(executed_ids)) or set(executed_ids) != required_ids:
        errors.append("coverage execution does not prove exactly the required rows")
    return sorted(set(errors))
def _coverage_summary(manifest, errors):
    lines = ["", "### Desktop coverage"]
    manifest_data = manifest if isinstance(manifest, dict) else {}
    policy = manifest_data.get("policy", {})
    policy = policy if isinstance(policy, dict) else {}
    expected_value = manifest_data.get("expected", [])
    detected_value = manifest_data.get("detected", [])
    executed_value = manifest_data.get("executed", [])
    expected = expected_value if isinstance(expected_value, list) else []
    detected = detected_value if isinstance(detected_value, list) else []
    executed = executed_value if isinstance(executed_value, list) else []
    required_count = sum(row.get("state") == "required" for row in expected if isinstance(row, dict))
    excluded_count = sum(row.get("state") == "excluded" for row in expected if isinstance(row, dict))
    available_count = sum(row.get("state") == "available" for row in detected if isinstance(row, dict))
    executed_count = sum(len(record.get("rows", [])) for record in executed
                         if isinstance(record, dict) and isinstance(record.get("rows"), list))
    identity = manifest_data.get("identity", {})
    shard = identity.get("shard", "missing") if isinstance(identity, dict) else "missing"
    selected_count = sum(row.get("state") == "required" and (shard == "combinations" or _coverage_row_owner(row) == shard)
                         for row in expected if isinstance(row, dict))
    lines += ["", f"Selected shard: {html.escape(str(shard))}; required here: {selected_count}. "
              "Other shards' configurations remain visible below, not counted as executed.",
              "", f"Expected rows: {len(expected)} (required {required_count}, excluded {excluded_count}); "
              f"detected available: {available_count}; executed: {executed_count}; "
              f"probes: {manifest_data.get('capability_probe_count', 'missing')}",
              f"Policy counts: {html.escape(str(policy.get('row_count', 'missing')))} expected / "
              f"{html.escape(str(policy.get('required_count', 'missing')))} required / "
              f"{html.escape(str(policy.get('excluded_count', 'missing')))} excluded; "
              f"version {html.escape(str(policy.get('version', 'missing')))} fingerprint "
              f"{html.escape(str(policy.get('fingerprint', 'missing')))}."]
    identity = manifest_data.get("identity", {})
    if isinstance(identity, dict):
        display = lambda value: html.escape(str(value)).replace("|", "&#124;")
        lines.append("Lane identity: " + display(identity.get("lane_id", "missing")))
        lines.append("Source identity: " + display(identity.get("source_revision", "missing")) +
                     " path=" + display(identity.get("source_path", "missing")) +
                     " sha256=" + display(identity.get("source_hash", "missing")))
        lines.append("Driver identity: path=" + display(identity.get("driver_path", "missing")) +
                     " sha256=" + display(identity.get("driver_hash", "missing")))
    obligations = manifest_data.get("obligations", {})
    if isinstance(obligations, dict):
        obligation_text = ", ".join(f"{name}={value.get('state', 'malformed')} ({value.get('reason', 'missing')})"
                                    for name, value in obligations.items() if isinstance(value, dict))
        if obligation_text:
            lines.append("Lane obligation reasons: " + html.escape(obligation_text))
    evidence = ", ".join(str(record.get("evidence", "missing")) for record in executed if isinstance(record, dict))
    lines.append("Completion evidence: " + html.escape(evidence or "missing"))
    lines += ["", "| Row identity | Owner shard | State | Detected compiler | Identity | Target | Version | Execution | Exclusion |",
              "|---|---|---|---|---|---|---|---|---|"]
    detected_by_id = {row.get("id"): row for row in detected if isinstance(row, dict)}
    rows = expected
    for row in rows if isinstance(rows, list) else []:
        row_id = html.escape(str(row.get("id", ""))).replace("|", "&#124;")
        detected_row = detected_by_id.get(row.get("id"), {})
        display = lambda value: html.escape(str(value)).replace("\n", "<br>").replace("|", "&#124;")
        lines.append(f"| {row_id} | {display(_coverage_row_owner(row))} | {display(row.get('state', 'malformed'))} | "
                     f"{display(detected_row.get('compiler', 'missing'))} | {display(detected_row.get('identity', 'missing'))} | "
                     f"{display(detected_row.get('target', 'missing'))} | "
                     f"{display(detected_row.get('version', 'missing'))} | {display(row.get('execution', 'missing'))} | "
                     f"{display(row.get('exclusion', ''))} |")
    if errors:
        lines += ["", "Coverage manifest failures: " + ", ".join(html.escape(error) for error in errors)]
    return lines
def write_report(environment, *, expected_coverage_mode="ci"):
    # main() deliberately exposes no CLI/environment override for this contract.
    steps = json.loads(environment.get("BUSTER_CI_STEPS", "{}"))
    required = environment.get("BUSTER_CI_REQUIRED", "").split()
    if not isinstance(steps, dict) or not required:
        raise ValueError("A step map and an explicit nonempty required-step list are mandatory")
    failures = assess(steps, required)
    coverage, coverage_errors = None, []
    coverage_path = environment.get("BUSTER_CI_COVERAGE_MANIFEST") or environment.get("BUSTER_CI_COVERAGE_OUTPUT")
    coverage_required = environment.get("BUSTER_CI_COVERAGE_REQUIRED") == "1" or bool(coverage_path)
    if coverage_required:
        if not coverage_path:
            coverage_errors = ["mandatory coverage manifest path is missing"]
        else:
            try:
                coverage = json.loads(Path(coverage_path).read_text(encoding="utf-8"))
                coverage_errors = validate_coverage_manifest(coverage, environment, expected_mode=expected_coverage_mode)
            except (OSError, ValueError, TypeError) as error:
                coverage_errors = [f"coverage manifest could not be read: {error}"]
        failures = sorted(set(failures + ["coverage"] if coverage_errors else failures))
    phases = None
    if environment.get("BUSTER_MATRIX_PHASE_OUTPUT"):
        try:
            phases = ci_matrix_phases.collect(environment, coverage)
        except (OSError, ValueError, TypeError) as error:
            phases = {"complete": False, "errors": [str(error)]}
        if not phases["complete"]:
            failures = sorted(set(failures + ["matrix_phases"]))
    metadata = {key: environment.get(key, "unknown") for key in (
        "GITHUB_REPOSITORY", "GITHUB_SHA", "GITHUB_REF", "GITHUB_RUN_ID",
        "GITHUB_RUN_ATTEMPT", "RUNNER_OS", "RUNNER_ARCH", "ImageOS", "ImageVersion", "BUSTER_CI_RUNNER",
        "BUSTER_MATRIX_SHARD", "BUSTER_CI_ZIG_CACHE_HIT")}
    report = {"schema": 1, "metadata": metadata, "required_steps": required,
              "steps": steps, "coverage": coverage, "coverage_errors": coverage_errors, "matrix_phases": phases,
              "unsatisfied_steps": failures, "success": not failures}
    output = Path(environment["RUNNER_TEMP"]) / "buster-ci"
    output.mkdir(parents=True, exist_ok=True)
    if "android" in required:
        report["android"] = _android_diagnostics(output / "android.log")
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = ["## Buster CI", "", "**Result: " + ("FAILURE" if failures else "SUCCESS") + "**", ""]
    for key, value in metadata.items():
        lines.append(f"{key}: <code>{html.escape(str(value))}</code><br>")
    lines += ["", "| Step | Outcome |", "|---|---|"]
    for name, step in steps.items():
        lines.append(f"| {html.escape(name).replace('|', '&#124;')} | "
                     f"{html.escape(step.get('outcome', 'missing'))} |")
    if "android" in report:
        lines += _android_summary(report["android"])
    if coverage_required:
        lines += _coverage_summary(coverage, coverage_errors)
    if phases is not None:
        lines += [ci_matrix_phases.markdown(phases)]
    if failures:
        lines += ["", "Missing, skipped, cancelled, or failed required work is not a pass."]
    reproduction = environment.get("BUSTER_CI_REPRO", "See docs/ci-github-actions.md.")
    # HTML escaping keeps branch names and command text out of Markdown fences.
    lines += ["", "### Reproduce", "", "Check out the exact GITHUB_SHA above. "
              "Use the same runner image and tool versions recorded in the job log.",
              "", "<pre>" + html.escape(reproduction) + "</pre>", "",
              "The diagnostic artifact contains result.json and captured logs. "
              "It contains no cached build products or environment/credential dump.", ""]
    text = "\n".join(lines)
    (output / "summary.md").write_text(text, encoding="utf-8")
    if environment.get("GITHUB_STEP_SUMMARY"):
        with Path(environment["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as stream:
            stream.write(text)
    print("CI_SUMMARY " + ("failure: " + ", ".join(failures) if failures else "success"))
    return 1 if failures else 0


def main():
    status = 1
    try:
        status = write_report(os.environ)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"CI summary failed: {error}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
