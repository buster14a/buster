"""Read-only, frozen-source hosted native-frame batching qualification.

The native build driver owns the complete matrix and every test deadline.
This temporary campaign controller only supplies cold worktrees, serial sample
order, identity custody and retained first-attempt evidence.
"""
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time


SAMPLES = ("A1", "B1", "B2", "A2", "A3", "B3")
BASELINE = "579f221e8d2b809d17fc88bd6c2355b593f024e8"
CANDIDATE = os.environ["BUSTER_FRAME_CANDIDATE"]
REPOSITORY = Path.cwd().resolve()
ROOT = Path(os.environ["RUNNER_TEMP"]) / "native-frame-source"
OUTPUT = Path(os.environ["RUNNER_TEMP"]) / "native-frame-qualification"
WINDOWS = os.name == "nt"


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def command(argv, cwd, environment, log, show=True):
    print("COMMAND", json.dumps([str(item) for item in argv]), flush=True)
    started = time.monotonic()
    with log.open("w", encoding="utf-8") as output:
        child = subprocess.Popen(argv, cwd=cwd, env=environment,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True, encoding="utf-8", errors="replace")
        for line in child.stdout:
            output.write(line)
            if show:
                print(line, end="", flush=True)
        status = child.wait()
    return {"argv": [str(item) for item in argv], "exit_status": status,
            "wall_seconds": time.monotonic() - started}


def identities(environment):
    tools = [("clang", ["clang", "--version"]),
             ("gcc", ["gcc", "--version"]), ("zig", ["zig", "version"]),
             ("cmake", ["cmake", "--version"]), ("ninja", ["ninja", "--version"])]
    tools += [("cl", ["cl"]), ("link", ["link.exe", "/?"])] if WINDOWS else [
        ("ld", ["ld", "--version"]), ("mold", ["mold", "--version"])]
    rows = []
    for name, argv in tools:
        executable = shutil.which(argv[0])
        row = {"name": name, "executable": executable}
        if executable:
            row["sha256"] = hashlib.sha256(Path(executable).read_bytes()).hexdigest()
            row.update(command(argv, REPOSITORY, environment, OUTPUT / (name + ".txt"), False))
        rows.append(row)
    write_json(OUTPUT / "toolchains.json", rows)
    if WINDOWS:
        command(["powershell", "-NoProfile", "-Command", "Get-CimInstance Win32_Processor | Select-Object Name, Manufacturer, NumberOfLogicalProcessors, Family, Stepping | ConvertTo-Json"],
                REPOSITORY, environment, OUTPUT / "cpu.json", False)
    else:
        command(["lscpu", "--json"], REPOSITORY, environment, OUTPUT / "cpu.json", False)


def diagnostic(output, environment, toggle):
    filename = "ide.exe" if WINDOWS else "ide"
    candidates = [path for path in (ROOT / "build").glob("*cc_clang-sanitize_on*")
                  if (path / "Debug" / filename).is_file()]
    if len(candidates) != 1:
        raise RuntimeError("Exact sanitized Debug tree is not unique: " + str(candidates))
    tree = candidates[0]
    binary = (tree / "Debug" / filename).resolve()
    text = (tree / "CMakeFiles" / "impl-Debug.ninja").read_text(encoding="utf-8")
    block = re.search(r"^build CMakeFiles/test_all-Debug[^\n]*\n(?P<body>(?:  [^\n]*\n)+)", text, re.MULTILINE)
    if block is None:
        raise RuntimeError("Exact generated test_all-Debug block is absent")
    commands = [line for line in block.group("body").splitlines() if "COMMAND = " in line and "-E env" in line
                and "/Debug/" + filename in line.replace("\\", "/")]
    if len(commands) != 1:
        raise RuntimeError("Exact generated test command is not unique: " + str(commands))
    generated = commands[0]
    policy = {}
    for quoted, plain in re.findall(r'"([A-Z_]+=[^"]*)"|(?<![\w"])([A-Z_]+=[^\s"]+)', generated):
        key, value = (quoted or plain).split("=", 1)
        policy[key] = value.replace("$$", "$")
    expected = "unsupported" if WINDOWS else "fatal"
    if policy.get("BUSTER_SANITIZER_POLICY_MODE") != expected:
        raise RuntimeError("Unexpected generated sanitizer policy: " + str(policy))
    if not WINDOWS and not all(key in policy for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "LSAN_OPTIONS")):
        raise RuntimeError("Generated sanitizer policy is incomplete: " + str(policy))
    env = dict(environment, **policy)
    env["BUSTER_TEST_JOBS"] = "1"
    env["BUSTER_TEST_FIXTURE_TIMING"] = "compiler_driver_tests"
    env["BUSTER_TEST_NATIVE_FRAME_BATCH"] = str(toggle)
    argv = [str(binary), "test", "--module=compiler_driver_tests", "--verbose=1", "--ci=1"]
    identity = {"binary": str(binary), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "generated_command": generated, "policy": policy, "argv": argv, "batch": str(toggle),
                "cwd": str(ROOT), "path": environment.get("PATH"), "lib": environment.get("LIB"),
                "include": environment.get("INCLUDE"), "libpath": environment.get("LIBPATH")}
    write_json(output / ("isolated-debug-identity-" + str(toggle) + ".json"), identity)
    if not WINDOWS:
        argv = ["/usr/bin/time", "-v", *argv]
    result = command(argv, ROOT, env, output / ("isolated-debug-" + str(toggle) + ".log"))
    write_json(output / ("isolated-debug-result-" + str(toggle) + ".json"), result)
    return result["exit_status"] == 0


def retain_build_evidence(output):
    rows = []
    for path in sorted((ROOT / "build").rglob("*")):
        if path.is_file() and (path.name in ("ide", "ide.exe", "compile_commands.json", "CMakeCache.txt", "configure.log")
                               or path.suffix == ".ninja"):
            rows.append({"path": path.relative_to(ROOT).as_posix(), "bytes": path.stat().st_size,
                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
            if path.name in ("compile_commands.json", "CMakeCache.txt", "configure.log", "impl-Debug.ninja"):
                destination = output / "build-evidence" / path.relative_to(ROOT)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)
    write_json(output / "build-identities.json", rows)


def coverage(output, environment):
    code = "import json, os, sys; sys.path.insert(0, 'tools'); from ci_summary_core import validate_coverage_manifest; manifest=json.load(open(sys.argv[1], encoding='utf-8')); errors=validate_coverage_manifest(manifest, dict(os.environ)); print(json.dumps({'errors':errors})); sys.exit(bool(errors))"
    checked = command([sys.executable, "-c", code, str(output / "coverage.json")], ROOT, environment, output / "coverage-validation.log")
    if checked["exit_status"] != 0:
        raise RuntimeError("Independent complete coverage validation failed")
    data = json.loads((output / "coverage.json").read_text(encoding="utf-8"))
    contract = {key: data[key] for key in ("schema", "partition_version", "kind", "hash_algorithm", "mode", "phase", "capability_probe_count", "policy", "obligations", "expected", "detected")}
    contract["identity"] = {key: data["identity"][key] for key in ("suite", "shard", "platform", "architecture", "repository")}
    contract["executed"] = [{key: row[key] for key in ("status", "evidence", "rows")} for row in data["executed"]]
    write_json(output / "coverage-contract.json", contract)
    return contract


def manifest(path):
    text = path.read_text(encoding="utf-8")
    rows = re.findall(r"^NATIVE_FRAME_VECTOR_CASE_V1 id=(\S+) classification=(\S+).*status=(\S+)$", text, re.MULTILINE)
    if not rows or len({row[0] for row in rows}) != len(rows) or any(row[2] != "pass" for row in rows):
        raise RuntimeError("Missing, duplicated or failing per-case manifest: " + str(path))
    compiles = re.findall(r"^NATIVE_FRAME_VECTOR_COMPILE_V1 target=(\S+) allocator=(\d+) frontend=(\d+) pic=(\d+) cpu=(\d+) fixture=(\d+) error=(\d+) classification=(\S+)$", text, re.MULTILINE)
    keys = {(row[0], *(int(value) for value in row[1:6])) for row in compiles}
    platforms = ("linux", "macos", "windows", "android", "ios", "uefi")
    expected = {("x86_64-" + platform, allocator, frontend, pic, cpu, fixture)
                for platform, allocator, frontend, pic, cpu, fixture in itertools.product(platforms, range(4), range(2), range(2), range(3), range(10))}
    expected.update(("aarch64-" + platform, allocator, frontend, pic, 0, fixture)
                    for platform, allocator, frontend, pic, fixture in itertools.product(platforms, range(4), range(2), range(2), range(4, 10)))
    if len(compiles) != 3456 or keys != expected:
        raise RuntimeError("Primary compilation manifest does not contain exactly 3456 unique cells")
    runtime_ids = {row[0] + ".allocator-" + row[1] + ".frontend-" + row[2] + ".pic-" + row[3] + ".cpu-" + row[4] + ".fixture-" + row[5] for row in compiles if row[7] == "runtime"}
    if runtime_ids != {row[0] for row in rows}:
        raise RuntimeError("Runtime case IDs differ from the classified required population")
    batches = re.findall(r"^NATIVE_FRAME_VECTOR_BATCH_V1 enabled=(\d+) cases=(\d+) batches=(\d+) batch_size=(\d+) executions=(\d+)$", text, re.MULTILINE)
    if len(batches) != 1:
        raise RuntimeError("Missing or duplicated batching census")
    enabled, cases, groups, capacity, executions = map(int, batches[0])
    if cases != len(rows) or capacity != 8 or executions != cases - groups * (capacity - 1) or (enabled and groups == 0) or (not enabled and groups != 0):
        raise RuntimeError("Runtime executions do not match the exact required batch census")
    operations = dict(re.findall(r"^DRIVER_OPERATION_TIMING_V1 fixture=native_frame_vectors operation=(buster_compile|positive_compile) calls=(\d+) .+$", text, re.MULTILINE))
    if operations != {"buster_compile": "3456", "positive_compile": "48"}:
        raise RuntimeError("Missing or changed primary/positive compilation population: " + str(operations))
    return {"runtime": sorted(rows), "compile": sorted(compiles), "operations": operations}


def sample(name, environment):
    arm_started = time.monotonic()
    output = OUTPUT / name
    output.mkdir()
    revision = CANDIDATE if name.startswith("B") else BASELINE
    env = dict(environment)
    env.pop("BUSTER_TEST_FIXTURE_TIMING", None)
    env["BUSTER_TEST_NATIVE_FRAME_BATCH"] = "1" if name.startswith("B") else "0"
    env["BUSTER_MATRIX_SHARD"] = "all"
    env["BUSTER_CI_COVERAGE_OUTPUT"] = str(output / "coverage.json")
    env["BUSTER_MATRIX_PHASE_OUTPUT"] = str(output / "phases")
    env["GITHUB_SHA"] = revision
    env["GITHUB_WORKSPACE"] = str(ROOT)
    env["BUSTER_CI_COVERAGE_PLATFORM"] = "windows" if WINDOWS else "linux"
    env["BUSTER_CI_COVERAGE_ARCH"] = "x86_64"
    subprocess.run(["git", "worktree", "add", "--detach", str(ROOT), revision], cwd=REPOSITORY, check=True)
    result = {"sample": name, "source_revision": revision, "batch": env["BUSTER_TEST_NATIVE_FRAME_BATCH"],
              "attempt": 1, "complete": False, "success": False, "source_root": str(ROOT),
              "checkout_wall_seconds": time.monotonic() - arm_started}
    try:
        result["source_tree"] = subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip()
        result["src_tree"] = subprocess.check_output(["git", "rev-parse", "HEAD:src"], cwd=ROOT, text=True).strip()
        (ROOT / "build").mkdir()
        driver = ROOT / "build" / ("build.exe" if WINDOWS else "build")
        bootstrap = ["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-g", "build.c"]
        if WINDOWS:
            bootstrap += ["-Wno-microsoft-enum-forward-reference", "-lws2_32"]
        bootstrap += ["-o", str(driver)]
        started = time.monotonic()
        result["bootstrap"] = command(bootstrap, ROOT, env, output / "bootstrap.log")
        if result["bootstrap"]["exit_status"] != 0:
            raise RuntimeError("Native driver bootstrap failed")
        result["driver_sha256"] = hashlib.sha256(driver.read_bytes()).hexdigest()
        argv = [str(driver), "test_all_combinations_ci", "--verbose=1"]
        if not WINDOWS:
            argv = ["/usr/bin/time", "-v", *argv]
        result["matrix"] = command(argv, ROOT, env, output / "matrix.log")
        result["complete_source_wall_seconds"] = time.monotonic() - started
        result["complete"] = True
        if result["matrix"]["exit_status"] != 0:
            raise RuntimeError("Full matrix failed; preserve this first attempt and stop the campaign")
        phase_started = time.monotonic()
        result["coverage_contract"] = coverage(output, env)
        result["coverage_validation_wall_seconds"] = time.monotonic() - phase_started
        phase_started = time.monotonic()
        result["diagnostic_pass"] = diagnostic(output, env, int(env["BUSTER_TEST_NATIVE_FRAME_BATCH"]))
        if name.startswith("B"):
            result["isolated_reference_pass"] = diagnostic(output, env, 0)
            result["manifest_agrees"] = manifest(output / "isolated-debug-1.log") == manifest(output / "isolated-debug-0.log")
        result["qualification_diagnostics_wall_seconds"] = time.monotonic() - phase_started
        result["success"] = result["matrix"]["exit_status"] == 0 and result["diagnostic_pass"] and result.get("isolated_reference_pass", True) and result.get("manifest_agrees", True)
    except Exception as error:
        result["error"] = repr(error)
        print("SAMPLE ERROR", name, repr(error), flush=True)
    finally:
        write_json(output / "result.json", result)
        phase_started = time.monotonic()
        try:
            retain_build_evidence(output)
        except Exception as error:
            result["evidence_error"] = repr(error)
            result["success"] = False
        result["evidence_capture_wall_seconds"] = time.monotonic() - phase_started
        phase_started = time.monotonic()
        try:
            subprocess.run(["git", "worktree", "remove", "--force", str(ROOT)], cwd=REPOSITORY, check=True)
        except Exception as error:
            result["cleanup_error"] = repr(error)
            result["success"] = False
        result["cleanup_wall_seconds"] = time.monotonic() - phase_started
        result["total_arm_wall_seconds"] = time.monotonic() - arm_started
        write_json(output / "result.json", result)
    return result


def main():
    started = time.monotonic()
    OUTPUT.mkdir(parents=True)
    environment = dict(os.environ)
    write_json(OUTPUT / "campaign.json", {"schema": "NATIVE_FRAME_QUALIFICATION_V1", "baseline": BASELINE,
               "candidate": CANDIDATE, "order": SAMPLES, "attempt": 1, "fresh_output_per_arm": True,
               "identical_absolute_source_root": str(ROOT), "workflow_source": environment["GITHUB_SHA"],
               "image_os": environment.get("ImageOS"), "image_version": environment.get("ImageVersion"),
               "runner_arch": environment.get("RUNNER_ARCH"), "platform": sys.platform,
               "windows_descendant_cpu_rss": "unavailable" if WINDOWS else "gnu-time-waited-children",
               "linux_peak_rss": "maximum waited process RSS; not concurrent tree RSS"})
    rows = []
    error = None
    try:
        identities(environment)
        for name in SAMPLES:
            row = sample(name, environment)
            rows.append(row)
            if row["success"] and rows[0]["coverage_contract"] != row["coverage_contract"]:
                row["success"] = False
                row["error"] = "Coverage contract differs from first baseline sample"
                write_json(OUTPUT / name / "result.json", row)
            if not row["success"]:
                break
    except Exception as caught:
        error = repr(caught)
    write_json(OUTPUT / "terminal.json", {"samples": rows, "controller_wall_seconds": time.monotonic() - started,
               "success": len(rows) == len(SAMPLES) and all(row["success"] for row in rows), "first_attempts_only": True,
               "unexecuted_samples": SAMPLES[len(rows):], "error": error})
    return 0 if len(rows) == len(SAMPLES) and all(row["success"] for row in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
