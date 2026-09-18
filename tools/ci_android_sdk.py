#!/usr/bin/env python3
"""Install and validate Android SDK packages with bounded retries and retained evidence."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from typing import Mapping, Sequence, TextIO

INSTALL_ATTEMPTS = 3
ATTEMPT_TIMEOUT_SECONDS = 300
BACKOFF_SECONDS = 5
_LICENSE_INPUT = "y\n" * 100
_PACKAGE_SEGMENT = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]*$")


class AndroidSdkError(RuntimeError):
    """A malformed request or unsafe SDK filesystem state."""


def package_segments(package: str) -> tuple[str, ...]:
    parts = tuple(package.split(";"))
    if not parts or any(not _PACKAGE_SEGMENT.fullmatch(part) for part in parts):
        raise AndroidSdkError(f"invalid Android SDK package identifier: {package!r}")
    return parts


def package_directory(sdk_root: Path, package: str) -> Path:
    return sdk_root.joinpath(*package_segments(package))


def _regular_nonempty(path: Path) -> bool:
    try:
        return not path.is_symlink() and path.is_file() and path.stat().st_size > 0
    except OSError:
        return False


def validate_package(sdk_root: Path, package: str) -> list[str]:
    directory = package_directory(sdk_root, package)
    errors: list[str] = []
    if directory.is_symlink() or not directory.is_dir():
        return [f"package directory is missing or not a real directory: {directory}"]

    if package == "emulator":
        candidates = (directory / "emulator", directory / "emulator.exe")
        executables = [
            path
            for path in candidates
            if _regular_nonempty(path) and (os.name == "nt" or os.access(path, os.X_OK))
        ]
        if not executables:
            errors.append("emulator executable is missing, empty, symlinked, or not executable")
    elif package.startswith("platforms;"):
        if not _regular_nonempty(directory / "android.jar"):
            errors.append("platform android.jar is missing, empty, or symlinked")
        if not _regular_nonempty(directory / "source.properties"):
            errors.append("platform source.properties is missing, empty, or symlinked")
    elif package.startswith("system-images;"):
        required = ("source.properties", "system.img", "ramdisk.img")
        for name in required:
            if not _regular_nonempty(directory / name):
                errors.append(f"system image {name} is missing, empty, or symlinked")
        kernels = (directory / "kernel-ranchu", directory / "kernel-ranchu-64", directory / "kernel-qemu")
        if not any(_regular_nonempty(path) for path in kernels):
            errors.append("system image kernel is missing, empty, or symlinked")
    elif not any(_regular_nonempty(directory / name) for name in ("source.properties", "package.xml")):
        errors.append("package metadata is missing, empty, or symlinked")
    return errors


def validate_packages(sdk_root: Path, packages: Sequence[str]) -> dict[str, list[str]]:
    return {package: errors for package in packages if (errors := validate_package(sdk_root, package))}


def _safe_remove(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink(missing_ok=True)
    elif path.is_dir():
        shutil.rmtree(path)


def _safe_requested_path(sdk_root: Path, package: str) -> Path:
    candidate = package_directory(sdk_root, package)
    root_text = os.path.normcase(str(sdk_root))
    candidate_text = os.path.normcase(str(candidate.absolute()))
    if os.path.commonpath((root_text, candidate_text)) != root_text or candidate == sdk_root:
        raise AndroidSdkError(f"package path escapes Android SDK root: {package!r}")
    parent = sdk_root
    for segment in package_segments(package)[:-1]:
        parent = parent / segment
        if parent.is_symlink():
            raise AndroidSdkError(f"refusing to traverse symlink while cleaning {package!r}: {parent}")
    return candidate


def _cleanup_invalid_packages(
    sdk_root: Path,
    invalid: Mapping[str, Sequence[str]],
    stream: TextIO,
) -> None:
    for package in invalid:
        candidate = _safe_requested_path(sdk_root, package)
        if candidate.exists() or candidate.is_symlink():
            _safe_remove(candidate)
            print(f"ANDROID_SDK_CLEANUP package={package} path={candidate}", file=stream)
        else:
            print(f"ANDROID_SDK_CLEANUP package={package} path=absent", file=stream)


def _snapshot_temp_entries(sdk_root: Path) -> set[str]:
    temporary = sdk_root / ".temp"
    if temporary.is_symlink() or not temporary.is_dir():
        return set()
    try:
        return {entry.name for entry in temporary.iterdir()}
    except OSError:
        return set()


def _cleanup_new_temp_entries(sdk_root: Path, previous: set[str], stream: TextIO) -> None:
    temporary = sdk_root / ".temp"
    if temporary.is_symlink():
        print(f"ANDROID_SDK_CLEANUP staging=refused-symlink path={temporary}", file=stream)
        return
    if not temporary.is_dir():
        return
    for entry in sorted(temporary.iterdir(), key=lambda item: item.name):
        if entry.name not in previous:
            _safe_remove(entry)
            print(f"ANDROID_SDK_CLEANUP staging={entry.name} path={entry}", file=stream)


def _terminate_process(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    else:
        process.kill()


def _run_attempt(
    command: Sequence[str],
    packages: Sequence[str],
    sdk_root: Path,
    attempt_directory: Path,
    log_path: Path,
    timeout_seconds: int,
    environment: Mapping[str, str],
) -> tuple[int, bool]:
    attempt_directory.mkdir(parents=True, exist_ok=False)
    effective_environment = dict(environment)
    effective_environment.update(
        {
            "ANDROID_HOME": str(sdk_root),
            "ANDROID_SDK_ROOT": str(sdk_root),
            "TMPDIR": str(attempt_directory),
            "TEMP": str(attempt_directory),
            "TMP": str(attempt_directory),
        }
    )
    arguments = [*command, "--install", *packages]
    timed_out = False
    status = 127
    with log_path.open("w", encoding="utf-8", newline="\n") as stream:
        print("ANDROID_SDK_COMMAND " + shlex.join(arguments), file=stream, flush=True)
        try:
            process = subprocess.Popen(
                arguments,
                stdin=subprocess.PIPE,
                stdout=stream,
                stderr=subprocess.STDOUT,
                text=True,
                env=effective_environment,
                start_new_session=os.name == "posix",
            )
            try:
                process.communicate(input=_LICENSE_INPUT, timeout=timeout_seconds)
                status = process.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
                _terminate_process(process)
                process.communicate()
                status = 124
                print(f"ANDROID_SDK_TIMEOUT seconds={timeout_seconds}", file=stream)
        except OSError as error:
            print(f"ANDROID_SDK_LAUNCH_ERROR {error}", file=stream)
    return status, timed_out


def _append_attempt(combined: TextIO, attempt: int, log_path: Path) -> None:
    print(f"\n===== Android SDK attempt {attempt} =====", file=combined)
    with log_path.open("r", encoding="utf-8", errors="replace") as stream:
        shutil.copyfileobj(stream, combined)


def _print_failure_tail(log_path: Path, lines: int = 200) -> None:
    try:
        content = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        print(f"Android SDK attempt log could not be read: {error}", file=sys.stderr)
        return
    print(f"--- tail of {log_path} ---", file=sys.stderr)
    for line in content[-lines:]:
        print(line, file=sys.stderr)


def install(
    command: Sequence[str],
    sdk_root: Path | str,
    packages: Sequence[str],
    log_directory: Path | str,
    *,
    attempts: int = INSTALL_ATTEMPTS,
    timeout_seconds: int = ATTEMPT_TIMEOUT_SECONDS,
    backoff_seconds: int = BACKOFF_SECONDS,
    environment: Mapping[str, str] | None = None,
) -> int:
    if not command or attempts <= 0 or timeout_seconds <= 0 or backoff_seconds < 0:
        raise AndroidSdkError("installer command, positive attempt/timeout bounds, and non-negative backoff are required")
    if not packages or len(set(packages)) != len(packages):
        raise AndroidSdkError("one or more unique Android SDK packages are required")
    for package in packages:
        package_segments(package)

    sdk_root = Path(sdk_root).resolve(strict=True)
    if sdk_root.is_symlink() or not sdk_root.is_dir():
        raise AndroidSdkError(f"Android SDK root is not a real directory: {sdk_root}")
    log_directory = Path(log_directory)
    log_directory.mkdir(parents=True, exist_ok=True)
    log_directory = log_directory.resolve(strict=True)
    combined_path = log_directory / "android-sdk-install.log"
    attempt_root = log_directory / "android-sdk-install-tmp"
    if attempt_root.exists() or attempt_root.is_symlink():
        _safe_remove(attempt_root)
    attempt_root.mkdir()
    effective_environment = dict(os.environ if environment is None else environment)

    try:
        with combined_path.open("w", encoding="utf-8", newline="\n") as combined:
            print(
                "ANDROID_SDK_INSTALL_BEGIN "
                f"attempts={attempts} timeout_seconds={timeout_seconds} backoff_seconds={backoff_seconds} "
                f"sdk_root={sdk_root} packages={','.join(packages)}",
                file=combined,
                flush=True,
            )
            initial_invalid = validate_packages(sdk_root, packages)
            if not initial_invalid:
                print("ANDROID_SDK_INSTALL_RESULT status=success attempts=0 source=preinstalled", file=combined)
                print("Android SDK packages are already installed and structurally valid.")
                return 0

            for attempt in range(1, attempts + 1):
                attempt_log = log_directory / f"android-sdk-install.attempt-{attempt}.log"
                attempt_directory = attempt_root / f"attempt-{attempt}"
                temporary_before = _snapshot_temp_entries(sdk_root)
                started = time.monotonic()
                status, timed_out = _run_attempt(
                    command,
                    packages,
                    sdk_root,
                    attempt_directory,
                    attempt_log,
                    timeout_seconds,
                    effective_environment,
                )
                invalid = validate_packages(sdk_root, packages)
                with attempt_log.open("a", encoding="utf-8", newline="\n") as stream:
                    for package in packages:
                        errors = invalid.get(package, [])
                        if errors:
                            for error in errors:
                                print(f"ANDROID_SDK_VALIDATION package={package} status=failure detail={error}", file=stream)
                        else:
                            print(f"ANDROID_SDK_VALIDATION package={package} status=success", file=stream)
                    print(
                        f"ANDROID_SDK_ATTEMPT_RESULT attempt={attempt} status={status} "
                        f"timed_out={'yes' if timed_out else 'no'} validation={'failure' if invalid else 'success'} "
                        f"seconds={time.monotonic() - started:.3f}",
                        file=stream,
                    )
                    if status != 0 or invalid:
                        _cleanup_new_temp_entries(sdk_root, temporary_before, stream)
                        _cleanup_invalid_packages(sdk_root, invalid, stream)
                        if attempt < attempts:
                            delay = backoff_seconds * attempt
                            print(f"ANDROID_SDK_RETRY next_attempt={attempt + 1} backoff_seconds={delay}", file=stream)
                _append_attempt(combined, attempt, attempt_log)
                combined.flush()

                if status == 0 and not invalid:
                    print(
                        f"ANDROID_SDK_INSTALL_RESULT status=success attempts={attempt} source=sdkmanager",
                        file=combined,
                        flush=True,
                    )
                    print(f"Android SDK installation succeeded after {attempt} attempt(s).")
                    return 0

                _print_failure_tail(attempt_log)
                if attempt < attempts:
                    time.sleep(backoff_seconds * attempt)

            print(
                f"ANDROID_SDK_INSTALL_RESULT status=failure attempts={attempts} packages={','.join(packages)}",
                file=combined,
                flush=True,
            )
        print(
            f"Android SDK installation failed after {attempts} attempts; complete logs are in {log_directory}.",
            file=sys.stderr,
        )
        return 1
    finally:
        if attempt_root.exists() or attempt_root.is_symlink():
            _safe_remove(attempt_root)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdkmanager", required=True)
    parser.add_argument("--sdk-root", required=True)
    parser.add_argument("--log-directory", required=True)
    parser.add_argument("--package", action="append", dest="packages", required=True)
    parser.add_argument("--attempts", type=int, default=INSTALL_ATTEMPTS)
    parser.add_argument("--attempt-timeout-seconds", type=int, default=ATTEMPT_TIMEOUT_SECONDS)
    parser.add_argument("--backoff-seconds", type=int, default=BACKOFF_SECONDS)
    arguments = parser.parse_args()
    try:
        return install(
            [arguments.sdkmanager],
            arguments.sdk_root,
            arguments.packages,
            arguments.log_directory,
            attempts=arguments.attempts,
            timeout_seconds=arguments.attempt_timeout_seconds,
            backoff_seconds=arguments.backoff_seconds,
        )
    except (AndroidSdkError, OSError, subprocess.SubprocessError, ValueError) as error:
        print(f"Android SDK setup failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
