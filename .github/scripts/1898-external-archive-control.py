#!/usr/bin/env python3
"""Independent ordinary archive controls for issue 1898; temporary hosted evidence."""
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

EXPECTED_HEAD = "ffce5acfd5beddee2100da4bd7e2b8cf942d9110"


def main():
    subject = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema": 1, "expected_head": EXPECTED_HEAD, "commands": [],
              "fixtures": {}, "success": False,
              "host": {"platform": platform.platform(), "machine": platform.machine()},
              "workflow": {key: os.environ.get(key) for key in
                           ("GITHUB_SHA", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT",
                            "ImageOS", "ImageVersion")}}
    def save():
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    def run(name, arguments, expected=0, timeout=30):
        record = {"name": name, "arguments": [str(value) for value in arguments],
                  "expected": expected, "timeout_seconds": timeout}
        report["commands"].append(record)
        save()
        def retain(stdout, stderr):
            (output / (name + ".stdout")).write_bytes(stdout)
            (output / (name + ".stderr")).write_bytes(stderr)
            record["stdout_sha256"] = hashlib.sha256(stdout).hexdigest()
            record["stderr_sha256"] = hashlib.sha256(stderr).hexdigest()
        try:
            result = subprocess.run(record["arguments"], cwd=output, capture_output=True, timeout=timeout)
            record["exit"] = result.returncode
            stdout = result.stdout.decode("utf-8", errors="replace")
            stderr = result.stderr.decode("utf-8", errors="replace")
            retain(result.stdout, result.stderr)
            print("ARCHIVE_CONTROL " + json.dumps(record), flush=True)
            if stdout:
                print(stdout, end="" if stdout.endswith("\n") else "\n", flush=True)
            if stderr:
                print(stderr, end="" if stderr.endswith("\n") else "\n", flush=True)
            if expected == "refusal":
                if result.returncode <= 0:
                    raise RuntimeError(name + ": expected normal nonzero refusal")
            elif result.returncode != expected:
                raise RuntimeError(name + ": unexpected exit")
            return stdout + stderr
        except subprocess.TimeoutExpired as error:
            record["timed_out"] = True
            record["error"] = repr(error)
            retain(error.stdout or b"", error.stderr or b"")
            raise
        except OSError as error:
            record["error"] = repr(error)
            raise
        finally:
            save()
    try:
        observed = run("subject-head", ["git", "-C", str(subject), "rev-parse", "HEAD"]).strip()
        report["subject_head"] = observed
        if observed != EXPECTED_HEAD:
            raise RuntimeError("source head differs from frozen candidate")
        for tool in ("gcc", "clang", "ar", "ld"):
            run(tool + "-version", [tool, "--version"])
        sources = {
            "main.c": "int foo(void); int main(void) { return foo() != 42; }\n",
            "simple.c": "int foo(void) { return 42; }\n",
            "unused_arm.c": "int unrelated_arm_only(void) { return 17; }\n",
            "none.c": "int main(void) { return 0; }\n",
            "need_arm.c": "int unrelated_arm_only(void); int main(void) { return unrelated_arm_only() != 17; }\n",
        }
        for name, content in sources.items():
            (output / name).write_text(content, encoding="utf-8")
        for name in ("main", "simple", "none", "need_arm"):
            run("gcc-" + name, ["gcc", "-fno-pie", "-c", name + ".c", "-o", name + ".o"])
        run("clang-foreign", ["clang", "--target=aarch64-unknown-linux-gnu", "-fno-pie",
                              "-c", "unused_arm.c", "-o", "unused_arm.o"])
        ide = subject / "build" / "Release" / "ide"
        if not ide.is_file():
            raise RuntimeError("missing exact-source compiler")
        report["compiler_sha256"] = hashlib.sha256(ide.read_bytes()).hexdigest()
        for order, members in (("original", ["simple.o", "unused_arm.o"]),
                               ("reversed", ["unused_arm.o", "simple.o"])):
            archive = order + ".a"
            run("ar-" + order, ["ar", "rcs", archive] + members)
            run("members-" + order, ["ar", "t", archive])
            for root in ("main", "none"):
                control = order + "-" + root
                run("gnu-link-" + control, ["gcc", "-no-pie", root + ".o", archive, "-o", "gnu-" + control])
                run("gnu-run-" + control, [str(output / ("gnu-" + control))])
                run("buster-link-" + control, [str(ide), "cc", root + ".o", archive, "-o", "buster-" + control])
                run("buster-run-" + control, [str(output / ("buster-" + control))])
            run("gnu-refuse-" + order, ["gcc", "-no-pie", "need_arm.o", archive,
                                      "-o", "gnu-foreign-" + order], expected="refusal")
            diagnostic = run("buster-refuse-" + order, [str(ide), "cc", "need_arm.o", archive,
                              "-o", "buster-foreign-" + order], expected="refusal")
            for marker in ("unused_arm.o", "aarch64", "x86_64"):
                if marker not in diagnostic:
                    raise RuntimeError("selected-member diagnostic missing " + marker)
        report["success"] = True
        print("ARCHIVE_EXTERNAL_CONTROL_SUCCESS head=" + EXPECTED_HEAD + " orders=2 positives=4 selected-refusals=2", flush=True)
        status = 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        report["error"] = repr(error)
        print("ARCHIVE_EXTERNAL_CONTROL_FAILURE " + repr(error), flush=True)
        status = 1
    finally:
        try:
            for path in sorted(output.iterdir()):
                if path.suffix in (".c", ".o", ".a"):
                    data = path.read_bytes()
                    report["fixtures"][path.name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        except OSError as error:
            report["evidence_error"] = repr(error)
            report["success"] = False
            status = 1
        save()
    return status


if __name__ == "__main__":
    sys.exit(main())
