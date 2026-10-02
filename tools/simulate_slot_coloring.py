#!/usr/bin/env python3
"""Cloud-only #1912 x86-64-v3 Cachegrind comparison, with retained failures.

This portable simulation is diagnostic, not native or 9700X acceptance.
Buster has no selected first-party license; no external source is copied.
Only CMake's native target flag is temporarily overlaid in each clean build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--head", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timing-pairs", type=int, choices=range(8), default=0)
    args = parser.parse_args()
    if os.environ.get("GITHUB_ACTIONS") != "true" or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted":
        parser.error("Requires a disposable GitHub-hosted runner")
    if not all(re.fullmatch(r"[0-9a-f]{40}", ref) for ref in (args.baseline, args.head)):
        parser.error("Both source identities must be full commit SHAs")
    root = Path.cwd()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binaries = out.parent / (out.name + "-binaries")
    binaries.mkdir(exist_ok=False)
    driver = binaries / "driver"
    target = binaries / "subject"
    summary = {"baseline": args.baseline, "head": args.head, "acceptance": False,
               "architecture": "x86-64-v3", "license": "First-party license unselected",
               "builds": {}, "workloads": {}, "binary_directory": str(binaries)}

    def save():
        (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    def run(label, command, timeout=1200, required=True):
        command = [str(part) for part in command]
        started = time.perf_counter_ns()
        try:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                output, _ = process.communicate(timeout=timeout)
                returncode = process.returncode
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                output, _ = process.communicate()
                returncode = 124
        except OSError as error:
            output, returncode = (repr(error) + "\n").encode(), 125
        elapsed = time.perf_counter_ns() - started
        (out / (label + ".log")).write_bytes(output)
        record = {"label": label, "command": command, "returncode": returncode, "nanoseconds": elapsed}
        with (out / "commands.jsonl").open("a") as log:
            log.write(json.dumps(record) + "\n")
        print(json.dumps(record), flush=True)
        if required and returncode:
            print(output.decode(errors="replace")[-12000:], flush=True)
            raise RuntimeError(label + " failed")
        return record

    def build(label, revision):
        run(label + "-checkout", ["git", "checkout", "--detach", revision])
        run(label + "-source-identity", ["git", "rev-parse", "HEAD", "HEAD^{tree}"])
        cmake = root / "CMakeLists.txt"
        original = cmake.read_bytes()
        anchor = b"set(GNU_FAMILY_NATIVE_TARGET -march=native)"
        if original.count(anchor) != 1:
            raise RuntimeError("Native-target overlay anchor is not unique")
        overlay = original.replace(anchor, b"set(GNU_FAMILY_NATIVE_TARGET -march=x86-64-v3)", 1)
        record = {"source_revision": revision, "cmake_original_sha256": hashlib.sha256(original).hexdigest(),
                  "cmake_overlay_sha256": hashlib.sha256(overlay).hexdigest(), "instrumented": False}
        summary["builds"][label] = record
        save()
        try:
            cmake.write_bytes(overlay)
            run(label + "-bootstrap", ["clang", "-Isrc", "-I.", "-Wall", "-Werror", "-Wno-unused-function",
                                      "-Wno-unused-variable", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char",
                                      "build.c", "-o", driver])
            run(label + "-generate", [driver, "generate", "--ci", "--no-sanitize", "--no-fuzz", "--no-lto",
                                     "--linker", "DEFAULT", "--", "-DBUSTER_DEBUG_INFO=OFF"])
            run(label + "-build", [driver, "build", "--config", "Release", "-t", "ide"])
            binary = binaries / (label + "-ide")
            shutil.copy2(root / "build/Release/ide", binary)
            record["binary_sha256"] = digest(binary)
            save()
            run(label + "-self-host", [driver, "test_self_host", "--config", "Release"])
            record["self_host_passed"] = True
        finally:
            cmake.write_bytes(original)
            record["cmake_restored_sha256"] = digest(cmake)
            save()
        return binary

    run("identity", ["bash", "-c", "git rev-parse HEAD 'HEAD^{tree}'; uname -a; lscpu; clang --version; valgrind --version"])
    copyright = Path("/usr/share/doc/valgrind/copyright")
    if copyright.is_file():
        shutil.copyfile(copyright, out / "valgrind-copyright.txt")
        summary["valgrind_copyright_sha256"] = digest(copyright)
    save()
    clean = {"baseline": build("baseline", args.baseline), "candidate": build("candidate", args.head)}
    run("restore-head", ["git", "checkout", "--detach", args.head])
    run("machine-tests", [clean["candidate"], "test", "--ci=1", "--verbose=1", "--module=machine_tests"])
    small = out / "controls.c"
    small.write_text("int sink(volatile int *p) { return *p; }\n"
                     "int fixed(int x) { volatile int a[2] = {x, x+1}; return sink(a); }\n"
                     "int leaf(int x) { return x+3; }\n"
                     "int main(void) { return fixed(2)+leaf(1)-6; }\n")
    specs = {"unity": ["-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0",
                       "-g", "src/buster/apps/ide/ide.c", "-lm"], "controls": ["-g", str(small)]}
    for name, flags in specs.items():
        cell = {"hashes": {}, "timings": [], "cachegrind": {}}
        summary["workloads"][name] = cell
        def command(variant):
            return [clean[variant], "cc", "-fregister-allocator=fast", "-fverify-codegen", *flags, "-o", target]
        for variant in ("baseline", "candidate"):
            run(name + "-" + variant + "-clean", command(variant))
            cell["hashes"][variant] = digest(target)
            save()
        if cell["hashes"]["baseline"] != cell["hashes"]["candidate"]:
            raise RuntimeError("Portable clean A/B output differs")
        for trial in range(args.timing_pairs):
            for variant in (("baseline", "candidate") if trial % 2 == 0 else ("candidate", "baseline")):
                rss = out / (name + "-" + variant + "-" + str(trial) + ".rss")
                record = run(name + "-" + variant + "-time-" + str(trial),
                             ["/usr/bin/time", "-f", "%M", "-o", rss, *command(variant)])
                record.update(trial=trial, variant=variant, rss_kib=int(rss.read_text().strip()))
                cell["timings"].append(record)
                if digest(target) != cell["hashes"][variant]:
                    raise RuntimeError("Portable timing output differs")
                save()
        if name == "controls":
            run("execute-controls", [target])
        for variant in ("baseline", "candidate"):
            cache = out / (name + "-" + variant + ".cachegrind")
            record = run(name + "-" + variant + "-cachegrind", ["valgrind", "--tool=cachegrind", "--cache-sim=yes",
                         "--branch-sim=no", "--I1=32768,8,64", "--D1=32768,8,64", "--LL=33554432,16,64",
                         "--cachegrind-out-file=" + str(cache), *command(variant)], timeout=1800, required=False)
            if record["returncode"]:
                cell["cachegrind"][variant] = {"unavailable": True, **record}
            else:
                text = cache.read_text()
                events = re.search(r"^events: (.*)$", text, re.MULTILINE)
                values = re.search(r"^summary: (.*)$", text, re.MULTILINE)
                if not events or not values or len(events.group(1).split()) != len(values.group(1).split()):
                    raise RuntimeError("Incomplete Cachegrind event summary")
                cell["cachegrind"][variant] = dict(zip(events.group(1).split(), map(int, values.group(1).split())))
                if digest(target) != cell["hashes"][variant]:
                    raise RuntimeError("Portable cache-simulation output differs")
            save()
        print(json.dumps({"workload": name, "cachegrind": cell["cachegrind"]}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
