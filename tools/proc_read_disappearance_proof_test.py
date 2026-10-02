#!/usr/bin/env python3
"""Hosted-only differential proof for #2380; extracts exact Git source."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile

BASELINE = "97c62802a732ce6374d29f3f74e01d0603d9e39d"
CANDIDATE = "245f08834a0854703151922efde28191e9adb736"
SOURCE_PATH = "src/buster/lib/os.c"

PRELUDE = r'''
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
typedef uint64_t u64;
#define BUSTER_GLOBAL_LOCAL static
enum { SCRIPT_NONE, SCRIPT_EIO, SCRIPT_ENOENT, SCRIPT_EBADF, SCRIPT_EINTR };
static int watched_fd = -1;
static int script;
static int injected;
static int close_failure;
static int close_calls;
static int open_calls;
static int native_esrch;
static int child_reaped;
static pid_t native_child;
static char native_path[64];
static unsigned failures;

static bool reap_exact(pid_t child)
{
    int status = 0;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    return result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void reset_control(void)
{
    watched_fd = -1;
    script = SCRIPT_NONE;
    injected = 0;
    close_failure = 0;
    close_calls = 0;
    open_calls = 0;
    native_esrch = 0;
    child_reaped = 0;
    native_child = 0;
    native_path[0] = 0;
}

static int proof_openat(int directory, const char* path, int flags)
{
    int result = openat(directory, path, flags);
    open_calls += 1;
    if (result >= 0)
    {
        watched_fd = result;
        if (native_child > 0 && strcmp(path, native_path) == 0)
        {
            /* Actual open completed; no read has occurred. Reap this child only. */
            child_reaped = reap_exact(native_child);
        }
    }
    return result;
}

static ssize_t proof_read(int descriptor, void* bytes, size_t capacity)
{
    ssize_t result;
    if (descriptor == watched_fd && script != SCRIPT_NONE &&
        (script != SCRIPT_EINTR || !injected))
    {
        injected += 1;
        errno = script == SCRIPT_EIO ? EIO : script == SCRIPT_ENOENT ? ENOENT :
            script == SCRIPT_EBADF ? EBADF : EINTR;
        result = -1;
    }
    else
    {
        result = read(descriptor, bytes, capacity);
        if (descriptor == watched_fd && result < 0 && errno == ESRCH)
        {
            native_esrch += 1;
        }
    }
    return result;
}

static int proof_close(int descriptor)
{
    int result = close(descriptor);
    if (descriptor == watched_fd)
    {
        close_calls += 1;
        watched_fd = -1;
        if (close_failure && result == 0)
        {
            /* The real fd is released; simulate a reported close failure. */
            errno = EIO;
            result = -1;
        }
    }
    return result;
}

#define openat proof_openat
#define read proof_read
#define close proof_close
'''

POSTLUDE = r'''
#undef openat
#undef read
#undef close

static void report_case(const char* name, bool passed, bool result, bool vanished,
                        u64 length)
{
    failures += !passed;
    printf("CASE %s %s result=%d vanished=%d length=%llu opens=%d closes=%d "
           "native_esrch=%d reaped=%d injected=%d\n", name, passed ? "PASS" : "FAIL",
           (int)result, (int)vanished, (unsigned long long)length, open_calls,
           close_calls, native_esrch, child_reaped, injected);
}

static void regular_case(const char* name, const char* path, u64 capacity,
                         int read_script, int fail_close, bool expected_result,
                         bool expected_vanished, bool missing)
{
    reset_control();
    script = read_script;
    close_failure = fail_close;
    char bytes[4096];
    memset(bytes, 0xa5, sizeof(bytes));
    u64 length = UINT64_MAX;
    bool vanished = true;
    bool result = os_linux_proc_read_at(AT_FDCWD, path, bytes, capacity, &length, &vanished);
    bool passed = result == expected_result && vanished == expected_vanished &&
        open_calls == 1 && close_calls == (missing ? 0 : 1) && native_esrch == 0;
    if (result)
    {
        passed = passed && length > 0 && length < capacity && bytes[length] == 0;
    }
    else
    {
        passed = passed && length == UINT64_MAX;
    }
    if (read_script != SCRIPT_NONE) { passed = passed && injected == 1; }
    report_case(name, passed, result, vanished, length);
}

static void native_case(const char* name, u64 capacity, bool fail_close)
{
    reset_control();
    close_failure = fail_close;
    pid_t child = fork();
    if (child == 0) { _exit(0); }
    siginfo_t information;
    memset(&information, 0, sizeof(information));
    int observed = -1;
    if (child > 0)
    {
        do
        {
            observed = waitid(P_PID, (id_t)child, &information, WEXITED | WNOWAIT);
        } while (observed < 0 && errno == EINTR);
    }
    bool setup = child > 0 && observed == 0 && information.si_pid == child &&
        information.si_code == CLD_EXITED && information.si_status == 0;
    char bytes[4096];
    memset(bytes, 0xa5, sizeof(bytes));
    u64 length = UINT64_MAX;
    bool vanished = false;
    bool result = false;
    if (setup)
    {
        int count = snprintf(native_path, sizeof(native_path), "/proc/%ld/stat", (long)child);
        setup = count > 0 && (size_t)count < sizeof(native_path);
        if (setup)
        {
            native_child = child;
            result = os_linux_proc_read_at(AT_FDCWD, native_path, bytes, capacity, &length, &vanished);
        }
    }
    bool passed = setup && child_reaped && !result &&
        vanished == !fail_close && length == UINT64_MAX &&
        native_esrch == 1 && open_calls == 1 && close_calls == 1;
    report_case(name, passed, result, vanished, length);
    if (child > 0 && !child_reaped) { (void)reap_exact(child); }
}

int main(void)
{
    alarm(12);
    regular_case("live", "/proc/self/stat", 4096, SCRIPT_NONE, 0, true, false, false);
    regular_case("empty", "/dev/null", 4096, SCRIPT_NONE, 0, false, false, false);
    regular_case("overflow", "/proc/self/stat", 1, SCRIPT_NONE, 0, false, false, false);
    regular_case("read-ebadf", "/proc/self/stat", 4096, SCRIPT_EBADF, 0, false, false, false);
    regular_case("missing-open", "/proc/self/buster-2380-nonexistent", 4096,
        SCRIPT_NONE, 0, false, true, true);
    regular_case("read-eio", "/proc/self/stat", 4096, SCRIPT_EIO, 0, false, false, false);
    regular_case("read-enoent", "/proc/self/stat", 4096, SCRIPT_ENOENT, 0, false, false, false);
    regular_case("read-eintr", "/proc/self/stat", 4096, SCRIPT_EINTR, 0, true, false, false);
    regular_case("close-failure-live", "/proc/self/stat", 4096,
        SCRIPT_NONE, 1, false, false, false);
    native_case("native-read", 4096, false);
    native_case("native-capacity", 1, false);
    native_case("close-failure-native", 4096, true);
    printf("SUMMARY cases=12 failures=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def run(argv, *, cwd=None, timeout=120, env=None):
    process = subprocess.Popen(argv, cwd=cwd, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        stdout, stderr = process.communicate()
        raise RuntimeError(f"Timeout: {argv!r}\n{stdout}\n{stderr}")
    return process.returncode, stdout, stderr


def exact_source(repo, ref):
    code, stdout, stderr = run(["git", "show", f"{ref}:{SOURCE_PATH}"], cwd=repo, timeout=30)
    if code:
        raise RuntimeError(f"git show failed for {ref}: {stderr}")
    functions = []
    for name in ("os_linux_proc_read_descriptor", "os_linux_proc_read_at"):
        pattern = rf"(?ms)^BUSTER_GLOBAL_LOCAL bool {name}\(.*?^\}}"
        matches = list(re.finditer(pattern, stdout))
        if len(matches) > 1 or (name.endswith("_at") and len(matches) != 1):
            raise RuntimeError(f"Unexpected function count: {name} {len(matches)}")
        if matches:
            functions.append(matches[0].group(0))
    extracted = "\n\n".join(functions) + "\n"
    print(json.dumps({"source_ref": ref, "path": SOURCE_PATH,
                      "file_sha256": hashlib.sha256(stdout.encode()).hexdigest(),
                      "reader_sha256": hashlib.sha256(extracted.encode()).hexdigest()}, sort_keys=True), flush=True)
    return extracted


def mutation(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Mutation anchor count is not one: {old!r}")
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".")
    parser.add_argument("--baseline", default=BASELINE)
    parser.add_argument("--candidate", default=CANDIDATE)
    parser.add_argument("--output-dir")
    args = parser.parse_args()
    baseline = exact_source(args.repo, args.baseline)
    candidate = exact_source(args.repo, args.candidate)
    variants = {
        "baseline": (baseline, {"native-read", "native-capacity"}),
        "candidate": (candidate, set()),
        "mutant-main": (mutation(candidate, "            *vanished = errno == ESRCH;",
                                  "            *vanished = false;"), {"native-read"}),
        "mutant-overflow": (mutation(candidate, "        *vanished = read_result < 0 && errno == ESRCH;",
                                      "        *vanished = false;"), {"native-capacity"}),
    }
    output = Path(args.output_dir) if args.output_dir else Path(tempfile.mkdtemp(prefix="buster-2380-proof-"))
    output.mkdir(parents=True, exist_ok=True)
    profiles = [
        ("clang-o2", "clang", ["-O2"]),
        ("gcc-o2", "gcc", ["-O2"]),
        ("clang-sanitize", "clang", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]),
    ]
    for profile, compiler, flags in profiles:
        code, version, errors = run([compiler, "--version"], timeout=15)
        if code:
            raise RuntimeError(f"Compiler unavailable: {compiler}: {errors}")
        print(f"COMPILER {profile} {version.splitlines()[0]}", flush=True)
        for variant, (reader, expected) in variants.items():
            name = f"{profile}-{variant}"
            source = output / f"{name}.c"
            executable = output / name
            source.write_text(PRELUDE + "\n" + reader + POSTLUDE)
            argv = [compiler, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                    "-fwrapv", "-fno-strict-aliasing", "-funsigned-char",
                    *flags, str(source), "-o", str(executable)]
            print("BUILD " + json.dumps(argv), flush=True)
            code, stdout, stderr = run(argv, timeout=120)
            (output / f"{name}.compile.log").write_text(stdout + stderr)
            if code:
                raise RuntimeError(f"Compile failed {name}:\n{stdout}\n{stderr}")
            env = os.environ.copy()
            env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
            code, stdout, stderr = run([str(executable.resolve())], timeout=20, env=env)
            (output / f"{name}.run.log").write_text(stdout + stderr)
            print(f"RUN {name} exit={code}\n{stdout}{stderr}", end="", flush=True)
            records = re.findall(r"^CASE ([a-z-]+) (PASS|FAIL)\b", stdout, re.M)
            failed = {case for case, status in records if status == "FAIL"}
            summary = f"SUMMARY cases=12 failures={len(expected)}"
            if (len(records) != 12 or len({case for case, _ in records}) != 12 or
                    failed != expected or summary not in stdout or
                    code != bool(expected) or stderr):
                raise RuntimeError(f"Unexpected proof result {name}: expected={sorted(expected)} actual={sorted(failed)}")
            print(f"VERIFIED {name} expected_failures={json.dumps(sorted(expected))}", flush=True)
    print(f"PROOF_PASS profiles=3 variants=4 cases_per_run=12 output={output}", flush=True)


if __name__ == "__main__":
    main()
