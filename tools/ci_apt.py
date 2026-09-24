#!/usr/bin/env python3
"""Provision named CI inputs, not builds or benchmark acceptance.

Ownership: .github/apt-inputs.json pins authenticated Ubuntu inputs. install()
uses private apt metadata; inspect() binds installed payloads to consumers.
Existing native GPU/throughput harnesses still own functional acceptance.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
KEYRING = "/usr/share/keyrings/ubuntu-archive-keyring.gpg"
GPU_TOOLS = {
    "clang-18": "/usr/lib/llvm-18/bin/clang",
    "llc-18": "/usr/lib/llvm-18/bin/llc",
    "llvm-readobj-18": "/usr/lib/llvm-18/bin/llvm-readobj",
    "llvm-objdump-18": "/usr/lib/llvm-18/bin/llvm-objdump",
    "/usr/lib/llvm-18/bin/ld.lld": "/usr/lib/llvm-18/bin/ld.lld",
    "spirv-val": "/usr/bin/spirv-val",
}
READLINE_LIBRARIES = {
    "libreadline.so.8": "/usr/lib/x86_64-linux-gnu/libreadline.so.8",
    "libtinfo.so.6": "/usr/lib/x86_64-linux-gnu/libtinfo.so.6",
}
APT_RETRY_DELAYS = (30, 60)


def load_lock(path):
    lock = json.loads(path.read_text())
    if set(lock) != {"schema", "ubuntu", "suite", "architecture", "snapshot", "profiles"}:
        raise ValueError("unexpected or missing apt lock fields")
    if (lock["schema"], lock["ubuntu"], lock["suite"], lock["architecture"]) != (1, "26.04", "resolute", "amd64"):
        raise ValueError("apt lock requires Ubuntu 26.04/resolute amd64")
    stamp = lock["snapshot"]
    if not isinstance(stamp, str) or not re.fullmatch(r"[0-9]{8}T[0-9]{6}Z", stamp):
        raise ValueError("apt snapshot must be an exact UTC timestamp")
    datetime.datetime.strptime(stamp, "%Y%m%dT%H%M%SZ")
    if set(lock["profiles"]) != {"gpu", "readline"}:
        raise ValueError("apt lock must define both input profiles")
    for packages in lock["profiles"].values():
        if not isinstance(packages, dict) or not packages:
            raise ValueError("empty apt profile")
        for name, version in packages.items():
            if not re.fullmatch(r"[a-z0-9][a-z0-9+.-]+", name):
                raise ValueError("invalid apt package name")
            if not isinstance(version, str) or not re.fullmatch(r"[0-9][a-zA-Z0-9.+:~\-]*", version):
                raise ValueError("apt package needs an exact version: " + name)
    return lock


def sources(lock):
    return (
        "Types: deb\n"
        f"URIs: https://snapshot.ubuntu.com/ubuntu/{lock['snapshot']}/\n"
        f"Suites: {lock['suite']} {lock['suite']}-updates {lock['suite']}-security\n"
        "Components: main universe\n"
        "Architectures: amd64\n"
        f"Signed-By: {KEYRING}\n"
        # Historical snapshots need not have a currently fresh Release file.
        # This does not disable Release signatures or package digest checks.
        "Check-Valid-Until: no\n"
    )


def apt_options(directory):
    options = {
        "Dir::Etc::sourcelist": str(directory / "snapshot.sources"),
        "Dir::Etc::sourceparts": "-",
        "Dir::Etc::preferences": "/dev/null",
        "Dir::Etc::preferencesparts": "-",
        "Dir::State::lists": str(directory / "lists"),
        "Dir::Cache::archives": str(directory / "archives"),
        "Dir::Cache::pkgcache": "",
        "Dir::Cache::srcpkgcache": "",
        "APT::Update::Error-Mode": "any",
        "APT::Get::AllowUnauthenticated": "false",
        "Acquire::AllowInsecureRepositories": "false",
        "Acquire::AllowDowngradeToInsecureRepositories": "false",
        "Acquire::https::Verify-Peer": "true",
        "Acquire::https::Verify-Host": "true",
        "Acquire::Retries": "3",
    }
    return [item for key, value in options.items() for item in ("-o", f"{key}={value}")]


class Commands:
    def __init__(self, directory):
        self.directory = directory
        self.count = 0

    def __call__(self, argv):
        self.count += 1
        print("+ " + shlex.join(map(str, argv)), flush=True)
        result = subprocess.run(argv, text=True, capture_output=True,
                                env={**os.environ, "LC_ALL": "C"}, check=False)
        prefix = self.directory / f"command-{self.count:03}"
        prefix.with_suffix(".json").write_text(json.dumps({"argv": list(map(str, argv)), "status": result.returncode}) + "\n")
        prefix.with_suffix(".stdout").write_text(result.stdout)
        prefix.with_suffix(".stderr").write_text(result.stderr)
        if result.returncode:
            print(result.stdout + result.stderr, file=sys.stderr)
            raise subprocess.CalledProcessError(result.returncode, argv, output=result.stdout, stderr=result.stderr)
        return result.stdout


def transient_snapshot_failure(error, lock):
    retryable = error.returncode == 100
    if retryable:
        diagnostics = (error.stdout or "") + "\n" + (error.stderr or "")
        retryable = not re.search(
            r"(?i)GPG error|NO_PUBKEY|Hash Sum mismatch|not signed|unauthenticated|certificate verification failed",
            diagnostics)
        prefix = "https://snapshot.ubuntu.com/ubuntu/" + lock["snapshot"] + "/"
        fetch = re.compile(r"E: Failed to fetch " + re.escape(prefix) + r"\S+\s+5\d\d(?:\s|$)")
        tails = {
            "E: Some index files failed to download. They have been ignored, or old ones used instead.",
            "E: Unable to fetch some archives, maybe run apt update or try with --fix-missing?",
        }
        found = False
        for line in diagnostics.splitlines():
            if line.startswith("E: "):
                if fetch.match(line):
                    found = True
                elif line not in tails:
                    retryable = False
        retryable = retryable and found
    return retryable


def run_snapshot_apt(run, argv, lock):
    output = None
    for attempt in range(len(APT_RETRY_DELAYS) + 1):
        try:
            output = run(argv)
            break
        except subprocess.CalledProcessError as error:
            if attempt == len(APT_RETRY_DELAYS) or not transient_snapshot_failure(error, lock):
                raise
            delay = APT_RETRY_DELAYS[attempt]
            print(f"snapshot returned HTTP 5xx; retrying the same apt command in {delay}s "
                  f"(attempt {attempt + 2}/{len(APT_RETRY_DELAYS) + 1})", file=sys.stderr, flush=True)
            time.sleep(delay)
    return output


def package_versions(text, expected):
    actual = {}
    for line in text.splitlines():
        name, version, arch, status = line.split("\t")
        if name in actual or arch not in ("amd64", "all") or status != "installed":
            raise ValueError("invalid installed package record: " + line)
        actual[name] = version
    if actual != expected:
        raise ValueError(f"installed apt inputs differ from lock: expected {expected}, got {actual}")
    return actual


def fact(path):
    resolved = Path(path).resolve(strict=True)
    if not resolved.is_file():
        raise ValueError("not a regular input: " + str(path))
    digest = hashlib.sha256()
    with resolved.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"path": str(resolved), "sha256": digest.hexdigest(), "bytes": resolved.stat().st_size}


def runtime_paths(text):
    found = {}
    for line in text.splitlines():
        if "not found" in line:
            raise ValueError("unresolved runtime dependency: " + line.strip())
        match = re.fullmatch(r"\s*(?:(\S+) => )?(/\S+) \(0x[0-9a-fA-F]+\)\s*", line)
        if match:
            name = match[1] or Path(match[2]).name
            if name in found:
                raise ValueError("duplicate runtime dependency: " + name)
            found[name] = Path(match[2]).resolve(strict=True)
    if not found:
        raise ValueError("no dynamic runtime closure was reported")
    return found


def require_binding(selected, expected, payload):
    resolved = Path(selected).resolve(strict=True)
    if resolved != Path(expected).resolve(strict=True) or str(resolved) not in payload:
        raise ValueError(f"consumer selected an unpinned input: {selected}; expected {expected}")
    return fact(resolved)


def inspect(profile, packages, run, evidence):
    names = sorted(packages)
    versions = run(["dpkg-query", "-W", "-f=${Package}\t${Version}\t${Architecture}\t${db:Status-Status}\n", *names])
    package_versions(versions, packages)
    (evidence / "packages.tsv").write_text(versions)
    paths = run(["dpkg-query", "-L", *names]).splitlines()
    payload = {}
    for path in paths:
        if Path(path).is_file():
            item = fact(path)
            payload[item["path"]] = item
    if not payload:
        raise ValueError("empty installed payload")
    selected = {}
    runtimes = {}
    if profile == "gpu":
        for command, expected in GPU_TOOLS.items():
            path = shutil.which(command)
            if path is None:
                raise ValueError("missing pinned consumer: " + command)
            selected[command] = require_binding(path, expected, payload)
            libraries = runtime_paths(run(["ldd", path]))
            for name, library in libraries.items():
                if name.startswith(("libLLVM", "libclang")) and str(library) not in payload:
                    raise ValueError("LLVM consumer resolved an unpinned library: " + str(library))
                runtimes[str(library)] = fact(library)
    else:
        source = evidence / "readline-probe.c"
        source.write_text("#include <readline/readline.h>\n#include <readline/history.h>\n"
                          "int main(void) { return rl_readline_version > 0 ? 0 : 1; }\n")
        compiler = shutil.which("clang")
        if compiler is None:
            raise ValueError("missing hosted Clang for readline selection probe")
        headers = shlex.split(run([compiler, "-M", str(source)]).replace("\\\n", ""))
        for name in ("readline.h", "history.h"):
            matches = [path for path in headers if Path(path).name == name]
            if len(matches) != 1:
                raise ValueError("ambiguous or missing readline header: " + name)
            selected[name] = require_binding(matches[0], "/usr/include/readline/" + name, payload)
        binary = evidence / "readline-probe"
        trace = run([compiler, str(source), "-Wl,--trace", "-lreadline", "-o", str(binary)])
        links = [line.strip() for line in trace.splitlines() if line.strip().endswith("/libreadline.so")]
        if len(links) != 1:
            raise ValueError("ambiguous or missing readline link input")
        selected["-lreadline"] = require_binding(links[0], "/usr/lib/x86_64-linux-gnu/libreadline.so", payload)
        libraries = runtime_paths(run(["ldd", str(binary)]))
        for name, expected in READLINE_LIBRARIES.items():
            if name not in libraries:
                raise ValueError("missing runtime input: " + name)
            selected[name] = require_binding(libraries[name], expected, payload)
        for library in libraries.values():
            runtimes[str(library)] = fact(library)
        run([str(binary)])
    (evidence / "pinned.identity.json").write_text(json.dumps({"packages": packages, "files": payload}, sort_keys=True, indent=2) + "\n")
    (evidence / "selected.identity.json").write_text(json.dumps({"selected": selected, "observed_host_runtime": runtimes}, sort_keys=True, indent=2) + "\n")
    # Broader hosted-image state is evidence, not a claim that it is frozen.
    (evidence / "host-packages.tsv").write_text(run(["dpkg-query", "-W", "-f=${binary:Package}\t${Version}\n"]))


def install(profile, lock_path, directory):
    lock = load_lock(lock_path)
    evidence = directory.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    run = Commands(evidence)
    os_release = dict(line.split("=", 1) for line in Path("/etc/os-release").read_text().splitlines() if "=" in line)
    if os_release.get("ID", "").strip('"') != "ubuntu" or os_release.get("VERSION_ID", "").strip('"') != lock["ubuntu"]:
        raise ValueError("pinned inputs require an Ubuntu 26.04 runner; refusing cross-suite install")
    if run(["dpkg", "--print-architecture"]).strip() != lock["architecture"]:
        raise ValueError("pinned inputs require amd64")
    if not Path(KEYRING).is_file():
        raise ValueError("Ubuntu archive signing keyring is missing")
    (evidence / "lock.json").write_text(json.dumps(lock, sort_keys=True, indent=2) + "\n")
    (evidence / "snapshot.sources").write_text(sources(lock))
    for name in ("lists", "archives"):
        (evidence / name / "partial").mkdir(parents=True)
    apt = ["sudo", "env", "DEBIAN_FRONTEND=noninteractive", "LC_ALL=C", "apt-get", *apt_options(evidence)]
    try:
        run_snapshot_apt(run, [*apt, "update"], lock)
        packages = lock["profiles"][profile]
        # Reinstall even on image/cache hits, allowing explicit versions to
        # downgrade newer copies but never permitting package removals.
        run_snapshot_apt(run, [*apt, "install", "--yes", "--no-install-recommends", "--reinstall",
                               "--allow-downgrades", "--no-remove",
                               *[f"{name}={version}" for name, version in sorted(packages.items())]], lock)
        inspect(profile, packages, run, evidence)
    finally:
        # Keep signed metadata even on failure, but not large apt caches.
        # Root/_apt own parts of these two private, freshly created trees.
        for name in ("lists", "archives"):
            directory = evidence / name
            for release in directory.glob("*InRelease"):
                shutil.copyfile(release, evidence / release.name)
            run(["sudo", "rm", "-rf", "--", str(directory)])



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=("gpu", "readline"))
    parser.add_argument("--lock", type=Path, default=ROOT / ".github/apt-inputs.json")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    status = 0
    try:
        install(args.profile, args.lock, args.out)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f"pinned apt inputs: {error}", file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
