#!/usr/bin/env python3
"""Capture buster's stage-1 self-compile with Superluminal on remote Linux."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile


DEFAULT_HOST = "david@benchpress"
DEFAULT_REMOTE_REPO = "/home/david/dev/buster"
DEFAULT_REMOTE_PROFILER = "/home/david/.local/lib/superluminal-cmd/SuperluminalCmd"
DEFAULT_LOCAL_PROFILER = str(Path.home() / "Downloads/Superluminal/SuperluminalCmdPackage/SuperluminalCmd")
DEFAULT_OUTPUT = str(Path.home() / "SuperluminalCaptures")


def run_command(arguments, *, check=True, capture_output=False):
    return subprocess.run(arguments, check=check, text=True, capture_output=capture_output)


def remote_command(host, arguments, *, working_directory=None):
    command = shlex.join([str(argument) for argument in arguments])
    if working_directory:
        command = f"cd {shlex.quote(str(working_directory))} && exec {command}"
    return ["ssh", "-o", "BatchMode=yes", host, command]


def run_remote(host, arguments, *, working_directory=None, check=True, capture_output=False):
    return run_command(
        remote_command(host, arguments, working_directory=working_directory),
        check=check,
        capture_output=capture_output,
    )


def remote_text(host, arguments, *, working_directory=None):
    result = run_remote(host, arguments, working_directory=working_directory, capture_output=True)
    return result.stdout.strip()


def require_local_tool(name):
    if not shutil.which(name):
        raise SystemExit(f"error: required local tool is unavailable: {name}")


def deploy_profiler_if_needed(host, local_profiler, remote_profiler):
    probe = run_remote(host, ["test", "-x", remote_profiler], check=False)
    if probe.returncode == 0:
        return
    if not os.access(local_profiler, os.X_OK):
        raise SystemExit(f"error: SuperluminalCmd is not executable: {local_profiler}")

    with tempfile.TemporaryDirectory(prefix="buster-superluminal-redist-") as temporary:
        temporary_path = Path(temporary)
        package = temporary_path / "package"
        archive = temporary_path / "package.tar.gz"
        run_command([local_profiler, "make-redist", "--output-dir", str(package)])
        with tarfile.open(archive, "w:gz") as output:
            for path in sorted(package.rglob("*")):
                output.add(path, arcname=path.relative_to(package), recursive=False)

        remote_archive = f"/tmp/buster-superluminal-redist-{os.getpid()}.tar.gz"
        run_command(["scp", "-o", "BatchMode=yes", str(archive), f"{host}:{remote_archive}"])
        run_remote(host, ["mkdir", "-p", str(Path(remote_profiler).parent)])
        run_remote(host, ["tar", "-xzf", remote_archive, "-C", str(Path(remote_profiler).parent)])
        run_remote(host, ["rm", "-f", remote_archive])


def parse_sha256sums(text):
    result = {}
    for line in text.splitlines():
        fields = line.split(maxsplit=1)
        if len(fields) != 2 or len(fields[0]) != 64:
            raise SystemExit(f"error: malformed remote sha256sum output: {line}")
        result[Path(fields[1].lstrip(" *")).name] = fields[0]
    return result


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def stage1_arguments(remote_capture_directory):
    return [
        "./build/Release/ide",
        "cc",
        "-Isrc",
        "-Ibuild/generated",
        "-DBUSTER_UNITY_BUILD=1",
        "-DBUSTER_INCLUDE_TESTS=0",
        "-g",
        "-v",
        f"-fsource-metrics={remote_capture_directory}/stage1.metrics",
        "src/buster/apps/ide/ide.c",
        "-lm",
        "-o",
        f"{remote_capture_directory}/ide-stage1",
    ]


def assert_zen5(lscpu, allow_non_zen5):
    family_26 = any(
        line.split(":", 1)[1].strip() == "26"
        for line in lscpu.splitlines()
        if line.startswith("CPU family:")
    )
    avx512 = " avx512f " in f" {lscpu.replace(chr(10), ' ')} "
    if not allow_non_zen5 and not (family_26 and avx512):
        raise SystemExit("error: remote CPU is not a verified family-26 AVX-512 Zen 5; use --allow-non-zen5 only for diagnostics")


def self_test():
    sample = "CPU family:                          26\nFlags: fpu avx512f avx512bw\n"
    assert_zen5(sample, False)
    try:
        assert_zen5("CPU family: 6\nFlags: avx512f\n", False)
        raise AssertionError("non-Zen-5 CPU was accepted")
    except SystemExit:
        pass
    command = remote_command("example", ["printf", "%s", "a b"], working_directory="/tmp/a b")
    assert command[-1] == "cd '/tmp/a b' && exec printf %s 'a b'"
    arguments = stage1_arguments("/captures/run")
    assert arguments[0:2] == ["./build/Release/ide", "cc"]
    assert arguments[-2:] == ["-o", "/captures/run/ide-stage1"]
    assert parse_sha256sums("" + "a" * 64 + "  stage1.linux\n") == {"stage1.linux": "a" * 64}
    print("remote_superluminal_capture self-test passed")


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=DEFAULT_HOST, help="SSH destination")
    parser.add_argument("--remote-repo", default=DEFAULT_REMOTE_REPO)
    parser.add_argument("--remote-profiler", default=DEFAULT_REMOTE_PROFILER)
    parser.add_argument("--local-profiler", default=DEFAULT_LOCAL_PROFILER)
    parser.add_argument("--output", default=DEFAULT_OUTPUT, help="local capture root")
    parser.add_argument("--frequency", type=int, default=8000, choices=range(500, 10001), metavar="500..10000")
    parser.add_argument("--skip-build", action="store_true", help="reuse the existing remote Release/ide")
    parser.add_argument("--allow-non-zen5", action="store_true", help="permit a diagnostic capture on another CPU")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    if arguments.self_test:
        self_test()
        return

    require_local_tool("ssh")
    require_local_tool("scp")
    deploy_profiler_if_needed(arguments.host, arguments.local_profiler, arguments.remote_profiler)

    revision = remote_text(arguments.host, ["git", "-C", arguments.remote_repo, "rev-parse", "HEAD"])
    tree = remote_text(arguments.host, ["git", "-C", arguments.remote_repo, "rev-parse", "HEAD^{tree}"])
    dirty = remote_text(arguments.host, ["git", "-C", arguments.remote_repo, "status", "--porcelain=v1"])
    if dirty:
        raise SystemExit(f"error: remote checkout has uncommitted changes:\n{dirty}")

    lscpu = remote_text(arguments.host, ["lscpu"])
    assert_zen5(lscpu, arguments.allow_non_zen5)
    remote_user = remote_text(arguments.host, ["id", "-un"])
    remote_group = remote_text(arguments.host, ["id", "-gn"])
    profiler_version = remote_text(arguments.host, [arguments.remote_profiler, "--version"])
    kernel = remote_text(arguments.host, ["uname", "-a"])
    run_remote(arguments.host, ["sudo", "-n", "true"])

    if not arguments.skip_build:
        run_remote(
            arguments.host,
            ["./build.sh", "build", "--config", "Release", "-t", "ide"],
            working_directory=arguments.remote_repo,
        )

    built_revision = remote_text(arguments.host, ["git", "-C", arguments.remote_repo, "rev-parse", "HEAD"])
    built_tree = remote_text(arguments.host, ["git", "-C", arguments.remote_repo, "rev-parse", "HEAD^{tree}"])
    built_dirty = remote_text(arguments.host, ["git", "-C", arguments.remote_repo, "status", "--porcelain=v1"])
    if built_revision != revision or built_tree != tree or built_dirty:
        raise SystemExit("error: remote source changed while preparing the capture")

    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    capture_name = f"buster-{revision[:8]}-zen5-stage1-{timestamp}"
    remote_capture_directory = f"/home/{remote_user}/SuperluminalCaptures/{capture_name}"
    local_capture_directory = Path(arguments.output).resolve() / capture_name
    local_capture_directory.mkdir(parents=True)
    run_remote(arguments.host, ["mkdir", "-p", remote_capture_directory])

    workload = stage1_arguments(remote_capture_directory)
    profiler_arguments = [
        "sudo",
        "-n",
        arguments.remote_profiler,
        "run",
        "linux",
        "--run-as-root",
        "true",
        "--resolve",
        "--symbol-location",
        f"{arguments.remote_repo}/build/Release",
        "--symbol-location",
        arguments.remote_repo,
        "--freq",
        str(arguments.frequency),
        "--capture-path",
        f"{remote_capture_directory}/stage1.linux",
        "--env-var",
        "LD_PRELOAD=",
        *workload,
    ]
    capture = run_remote(arguments.host, profiler_arguments, working_directory=arguments.remote_repo, check=False)
    ownership = run_remote(
        arguments.host,
        ["sudo", "-n", "chown", "-R", f"{remote_user}:{remote_group}", remote_capture_directory],
        check=False,
    )
    if ownership.returncode != 0:
        raise SystemExit("error: capture ownership restoration failed")
    if capture.returncode != 0:
        raise SystemExit(f"error: Superluminal capture failed with status {capture.returncode}")

    package_name = f"{capture_name}.slp"
    run_remote(
        arguments.host,
        [
            arguments.remote_profiler,
            "export",
            "--allow-overwrite",
            "--output-path",
            f"{remote_capture_directory}/{package_name}",
            f"{remote_capture_directory}/stage1.linux",
        ],
    )

    remote_files = [package_name, "stage1.linux", "stage1.metrics"]
    remote_hashes = parse_sha256sums(
        remote_text(
            arguments.host,
            ["sha256sum", *[f"{remote_capture_directory}/{name}" for name in remote_files]],
        )
    )
    run_command(
        [
            "scp",
            "-o",
            "BatchMode=yes",
            *[f"{arguments.host}:{remote_capture_directory}/{name}" for name in remote_files],
            str(local_capture_directory),
        ]
    )

    local_hashes = {name: sha256_file(local_capture_directory / name) for name in remote_files}
    if local_hashes != remote_hashes:
        raise SystemExit("error: downloaded capture hashes do not match the remote files")

    binary_hash = remote_text(
        arguments.host,
        ["sha256sum", f"{arguments.remote_repo}/build/Release/ide"],
    ).split()[0]
    provenance = {
        "schema": 1,
        "captured_utc": timestamp,
        "host": arguments.host,
        "kernel": kernel,
        "lscpu": lscpu,
        "revision": revision,
        "tree": tree,
        "remote_repo": arguments.remote_repo,
        "profiled_binary_sha256": binary_hash,
        "profiler_version": profiler_version,
        "sample_frequency_hz": arguments.frequency,
        "workload": workload,
        "sha256": local_hashes,
    }
    (local_capture_directory / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    sums = "".join(f"{digest}  {name}\n" for name, digest in sorted(local_hashes.items()))
    (local_capture_directory / "SHA256SUMS").write_text(sums)

    print(f"portable capture: {local_capture_directory / package_name}")
    print(f"raw capture:      {local_capture_directory / 'stage1.linux'}")
    print(f"revision:         {revision}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"error: command failed with status {error.returncode}") from error
