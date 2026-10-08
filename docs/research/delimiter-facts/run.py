#!/usr/bin/env python3
"""Cloud-only bounded diagnostics; no qualified-host performance verdict."""
import argparse
import hashlib
import json
import os
import pathlib
import resource
import shutil
import signal
import statistics
import subprocess
import threading
import time

parser = argparse.ArgumentParser()
parser.add_argument("--baseline", required=True)
parser.add_argument("--candidate", required=True)
parser.add_argument("--out", required=True)
args = parser.parse_args()
if os.environ.get("GITHUB_ACTIONS") != "true":
    raise SystemExit("This experiment requires GitHub-hosted execution.")
root = pathlib.Path.cwd()
out = pathlib.Path(args.out).resolve()
out.mkdir(parents=True, exist_ok=False)
script_dir = pathlib.Path(__file__).resolve().parent
overlay = (script_dir / "instrument.py").read_bytes()
candidate = subprocess.check_output(["git", "rev-parse", args.candidate], text=True).strip()
baseline = subprocess.check_output(["git", "rev-parse", args.baseline], text=True).strip()
if subprocess.check_output(["git", "status", "--porcelain"], text=True).strip():
    raise SystemExit("Fresh isolated checkout required.")
(out / "instrument.py").write_bytes(overlay)
rows = []
failures = []

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None

def execute(command, name, env=None, required=True, seconds=180):
    stdout_path = out / (name + ".stdout")
    stderr_path = out / (name + ".stderr")
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic_ns()
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr, env=env, start_new_session=True)
        expired = []
        def terminate():
            expired.append(True)
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        timer = threading.Timer(seconds, terminate)
        timer.start()
        try:
            _, status, usage = os.wait4(process.pid, 0)
            process.returncode = os.waitstatus_to_exitcode(status)
        finally:
            timer.cancel()
    row = {
        "name": name, "argv": command, "cwd": str(root),
        "status": process.returncode, "timed_out": bool(expired),
        "wall_ns": time.monotonic_ns() - started,
        "cpu_seconds": usage.ru_utime + usage.ru_stime,
        "maxrss_kib": usage.ru_maxrss,
        "stdout_sha256": digest(stdout_path), "stderr_sha256": digest(stderr_path),
    }
    rows.append(row)
    (out / "commands.json").write_text(json.dumps(rows, indent=2))
    if required and (process.returncode or expired):
        failures.append(name)
        raise RuntimeError(name + " failed; retained logs")
    print(name, "status=", row["status"], "wall_ns=", row["wall_ns"], flush=True)
    return row

def metrics(path):
    result = {}
    if path.exists():
        for line in path.read_text().splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                try:
                    result[key] = int(value)
                except ValueError:
                    pass
    return result

env = dict(os.environ, BUSTER_TEST_JOBS="2", CMAKE_BUILD_PARALLEL_LEVEL="2")
driver = out / "build-driver"
execute(["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
         "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "build.c", "-o", str(driver)], "driver", env)
for command, label in [(["clang", "--version"], "clang"), (["cmake", "--version"], "cmake"),
                       (["ninja", "--version"], "ninja"), (["uname", "-a"], "uname"), (["lscpu"], "lscpu")]:
    execute(command, label)
(out / "driver.sha256").write_text(digest(driver) + "\n")
clang_path = pathlib.Path(shutil.which("clang")).resolve()
(out / "host-compiler.json").write_text(json.dumps({"path": str(clang_path), "sha256": digest(clang_path),
    "runner": {key: os.environ.get(key) for key in ("GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "RUNNER_OS",
                                                 "RUNNER_ARCH", "ImageOS", "ImageVersion")}}, indent=2))
frozen = out / "frozen"
frozen.mkdir()
try:
    for arm, revision in [("baseline", baseline), ("candidate", candidate)]:
        execute(["git", "checkout", "--detach", revision], arm + "-checkout")
        execute([str(driver), "generate", "--cc", "clang", "--ci", "--linker", "DEFAULT"], arm + "-generate", env, seconds=240)
        execute([str(driver), "build", "--config", "Release", "-t", "ide", "--", "-j2"], arm + "-build", env, seconds=600)
        execute([str(driver), "test_self_host", "--config", "Release"], arm + "-self-host", env, seconds=600)
        execute([str(root / "build/Release/ide"), "test", "--module=c_frontend_tests"], arm + "-frontend", env, seconds=600)
        if arm == "candidate":
            execute([str(driver), "test_mode_matrix", "--config", "Release"], arm + "-modes", env, seconds=600)
            execute([str(driver), "source_size", "--base", baseline], "source-size", env)
        binary = out / (arm + "-ide")
        shutil.copy2(root / "build/Release/ide", binary)
        (out / (arm + "-binary.sha256")).write_text(digest(binary) + "\n")
        shutil.copy2(root / "build/compile_commands.json", out / (arm + "-compile_commands.json"))
        shutil.copy2(root / "build/CMakeCache.txt", out / (arm + "-CMakeCache.txt"))
        if arm == "baseline":
            shutil.copytree(root / "src", frozen / "src")
            shutil.copytree(root / "build/generated", frozen / "generated")
            shutil.copy2(root / "tests/basic_c_operations.c", frozen / "operations.c")
        overlay_sources = ["src/buster/lib/compiler/frontend/c/c_parse.c", "src/buster/lib/compiler/work_ledger.h"]
        source_before = {path: digest(root / path) for path in overlay_sources}
        execute(["python3", str(out / "instrument.py"), str(root)], arm + "-overlay")
        source_after = {path: digest(root / path) for path in overlay_sources}
        (out / (arm + "-overlay-sources.json")).write_text(json.dumps({"before": source_before, "after": source_after,
            "overlay_sha256": digest(out / "instrument.py")}, indent=2))
        execute([str(driver), "generate", "--cc", "clang", "--ci", "--linker", "DEFAULT",
                 "-DBUSTER_BENCH_ALLOCATIONS=ON", "-DBUSTER_UNITY_BUILD=OFF"], arm + "-census-generate", env, seconds=240)
        execute([str(driver), "build", "--config", "Release", "-t", "ide", "--", "-j2"], arm + "-census-build", env, seconds=600)
        shutil.copy2(root / "build/Release/ide", out / (arm + "-census-ide"))
        (out / (arm + "-census-binary.sha256")).write_text(digest(out / (arm + "-census-ide")) + "\n")
        execute(["git", "restore", "--source=" + revision, "--", "src/buster/lib/compiler/frontend/c/c_parse.c",
                 "src/buster/lib/compiler/work_ledger.h"], arm + "-restore-overlay")
    tiny = frozen / "tiny.c"
    tiny.write_text("int tiny(int x) { return x + 1; }\n")
    updates = frozen / "updates.c"
    updates.write_text("\n".join(
        "int update_%d(int *p) { int a[2] = {1,2}; (*p)++; a[(*p)&1] += (int)sizeof(((*p)++)); return a[0] + *p; }" % n
        for n in range(256)) + "\n")
    labels = frozen / "labels.c"
    labels.write_text("\n".join(
        "int label_%d(int x) { void *target = &&done; if (x) goto *target; done: return x; }" % n
        for n in range(128)) + "\n")
    bad = frozen / "invalid-update.c"
    bad.write_text("int invalid_update(void) { const int x = 0; return x++; }\n")
    cases = [("tiny", tiny, []), ("operations", frozen / "operations.c", []),
             ("updates", updates, []), ("labels", labels, []),
             ("unity", frozen / "src/buster/apps/ide/ide.c",
              ["-I" + str(frozen / "src"), "-I" + str(frozen / "generated"),
               "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0"])]
    quality = []
    census = []
    timing = []
    stages = []
    target = out / "same-output.o"
    source_hashes = {str(path.relative_to(frozen)): digest(path) for path in frozen.rglob("*") if path.is_file()}
    (out / "sources.json").write_text(json.dumps(source_hashes, indent=2))
    for name, source, flags in cases:
        for form, form_flags in [("direct", []), ("promotion", ["-fno-frontend-ssa"])]:
            outputs = []
            diagnostics = []
            statuses = []
            for arm in ("baseline", "candidate"):
                metric_path = out / (arm + "-" + name + "-" + form + ".metrics")
                target.unlink(missing_ok=True)
                command = [str(out / (arm + "-ide")), "cc"] + flags + form_flags + [
                    "-g", "-c", "-fsource-metrics=" + str(metric_path), str(source), "-o", str(target)]
                row = execute(command, "quality-" + arm + "-" + name + "-" + form, env, required=False)
                plain_digest = digest(target)
                if row["status"] == 0 and (plain_digest is None or target.stat().st_size == 0):
                    raise RuntimeError("Missing successful output: " + row["name"])
                outputs.append(plain_digest)
                diagnostics.append(row["stderr_sha256"])
                statuses.append(row["status"])
                diag_metric = out / (arm + "-" + name + "-" + form + "-census.metrics")
                command[0] = str(out / (arm + "-census-ide"))
                command[command.index("-fsource-metrics=" + str(metric_path))] = "-fsource-metrics=" + str(diag_metric)
                target.unlink(missing_ok=True)
                row = execute(command, "census-" + arm + "-" + name + "-" + form, env)
                census_digest = digest(target)
                if census_digest != plain_digest or row["stderr_sha256"] != diagnostics[-1]:
                    raise RuntimeError("Instrumented output mismatch: " + row["name"])
                values = metrics(diag_metric)
                required_metrics = ("work.delimiter.pairs", "work.delimiter.reverse_stores",
                                    "work.delimiter.inverse_maps", "work.delimiter.inverse_tokens",
                                    "work.delimiter.inverse_clear_bytes", "work.delimiter.backward_reads")
                if not all(key in values for key in required_metrics):
                    raise RuntimeError("Missing diagnostic metrics: " + str(diag_metric))
                if arm == "candidate" and (values["work.delimiter.inverse_maps"] != 0 or
                                          values["work.delimiter.inverse_tokens"] != 0 or
                                          values["work.delimiter.inverse_clear_bytes"] != 0 or
                                          values["work.delimiter.reverse_stores"] != values["work.delimiter.pairs"]):
                    raise RuntimeError("Producer publication census failed")
                if arm == "baseline" and (values["work.delimiter.inverse_maps"] == 0 or
                                         values["work.delimiter.reverse_stores"] != 0 or
                                         values["work.delimiter.inverse_clear_bytes"] != 4 * values["work.delimiter.inverse_tokens"]):
                    raise RuntimeError("Baseline inverse-map census not exercised")
                region = values.get("work.delimiter.inverse_region_ns", 0) + values.get("work.delimiter.inverse_allocation_ns", 0)
                census.append({"arm": arm, "case": name, "form": form, "status": row["status"],
                               "wall_ns": row["wall_ns"], "timed_inverse_ns": region,
                               "diagnostic_region_fraction": region / row["wall_ns"],
                               "object_sha256": census_digest, "metrics": values})
                (out / "census.json").write_text(json.dumps(census, indent=2))
            equal = outputs[0] == outputs[1] and diagnostics[0] == diagnostics[1] and statuses == [0, 0]
            quality.append({"case": name, "form": form, "statuses": statuses, "object_sha256": outputs,
                            "diagnostic_sha256": diagnostics, "equal": equal})
            (out / "quality.json").write_text(json.dumps(quality, indent=2))
            if not equal:
                failures.append("output-" + name + "-" + form)
    for form, form_flags in [("direct", []), ("promotion", ["-fno-frontend-ssa"])]:
        checks = []
        for arm in ("baseline", "candidate"):
            target.unlink(missing_ok=True)
            row = execute([str(out / (arm + "-ide")), "cc"] + form_flags +
                          ["-g0", "-c", str(bad), "-o", str(target)], "invalid-" + arm + "-" + form, env, required=False)
            checks.append({"status": row["status"], "stderr_sha256": row["stderr_sha256"], "object_sha256": digest(target)})
        equal = checks[0]["status"] != 0 and checks[0] == checks[1] and checks[0]["object_sha256"] is None
        quality.append({"case": "invalid-update", "form": form, "equal": equal, "checks": checks})
        (out / "quality.json").write_text(json.dumps(quality, indent=2))
        if not equal:
            failures.append("invalid-" + form)
    stages.append("quality-and-census")
    timing_hashes = {}
    for name, source, flags in cases:
        for sample in range(8):
            order = ("baseline", "candidate") if sample % 2 == 0 else ("candidate", "baseline")
            for arm in order:
                command = [str(out / (arm + "-ide")), "cc"] + flags + ["-g0", "-c", str(source), "-o", str(target)]
                target.unlink(missing_ok=True)
                row = execute(command, "timing-" + name + "-" + str(sample) + "-" + arm, env)
                output_digest = digest(target)
                if output_digest is None or target.stat().st_size == 0:
                    raise RuntimeError("Missing timing output")
                if name in timing_hashes and timing_hashes[name] != output_digest:
                    raise RuntimeError("-g0 output changed: " + row["name"])
                timing_hashes[name] = output_digest
                timing.append(dict(row, arm=arm, case=name, sample=sample, warmup=sample < 2, object_sha256=output_digest))
                (out / "timing.json").write_text(json.dumps(timing, indent=2))
    summary = {}
    for name, _, _ in cases:
        selected = [r for r in timing if r["case"] == name and not r["warmup"]]
        wall = {arm: statistics.median(r["wall_ns"] for r in selected if r["arm"] == arm) for arm in ("baseline", "candidate")}
        cpu = {arm: statistics.median(r["cpu_seconds"] for r in selected if r["arm"] == arm) for arm in ("baseline", "candidate")}
        rss = {arm: max(r["maxrss_kib"] for r in selected if r["arm"] == arm) for arm in ("baseline", "candidate")}
        summary[name] = {"median_wall_ns": wall, "median_cpu_seconds": cpu, "peak_rss_kib": rss,
                         "wall_ratio": wall["candidate"] / wall["baseline"]}
    (out / "quality.json").write_text(json.dumps(quality, indent=2))
    (out / "census.json").write_text(json.dumps(census, indent=2))
    (out / "timing.json").write_text(json.dumps(timing, indent=2))
    (out / "summary.json").write_text(json.dumps(summary, indent=2))
    stages.append("paired-descriptive-timing")
    if failures:
        raise RuntimeError("Output parity failed: " + repr(failures))
    execute(["git", "checkout", "--detach", candidate], "restore-candidate")
    execute([str(driver), "generate", "--cc", "clang", "--ci", "--linker", "DEFAULT", "--sanitize"], "sanitize-generate", env, seconds=240)
    execute([str(driver), "build", "--config", "Debug", "-t", "ide", "--", "-j2"], "sanitize-build", env, seconds=600)
    execute([str(root / "build/Debug/ide"), "test", "--module=c_frontend_tests"], "sanitize-frontend", env, seconds=600)
    stages.append("sanitized-frontend")
except BaseException as error:
    failures.append(type(error).__name__ + ": " + str(error))
    raise
finally:
    execute(["git", "restore", "--source=" + candidate, "--worktree", "--staged", "."], "final-restore", required=False)
    manifest = {"baseline": baseline, "candidate": candidate, "pending_qualified_host_acceptance": True,
                "failures": failures, "completed_stages": locals().get("stages", []),
                "scope": "GitHub-hosted correctness and bounded diagnostics",
                "timing_rule": "2 warmup + 6 measured pairs, alternating order; descriptive only"}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2))
print(json.dumps({"failures": failures, "evidence": str(out)}))
