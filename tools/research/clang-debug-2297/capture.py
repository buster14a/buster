#!/usr/bin/env python3
"""Bounded, diagnostic hosted comparison. Never invoke on a developer workstation.

All timed commands use the supplied fork/exec/wait4 helper. Producers must be
Release builds; -g below controls their generated programs, not the producers.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import platform
import shutil
import signal
import statistics
import subprocess
import sys
import time

SOURCES = ("runtime.c", "scalar_4.c", "scalar_32.c", "scalar_512.c",
           "scalar_4096.c", "macro_1024.c", "cjson_driver.c", "cJSON.c")
KERNELS = ("reduction", "division", "licm", "gvn", "inlining", "branches",
           "memory", "floating", "cjson")
LANES = ("buster0", "clang00", "clang02")  # Equal output-path lengths.
SEEDS = (1, 123456789, 4294967295)
VALIDATION_REPS = (1, 3, 19)
SAMPLES = 7


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            h.update(block)
    return h.hexdigest()


def manifest(path):
    p = Path(path)
    return {"path": str(p), "bytes": p.stat().st_size, "sha256": sha(p)}


def successful(row):
    return row.get("exit_code") == 0 and not row.get("timed_out") and not row.get("error")


def measured_success(row):
    timing = row.get("measurement", {})
    return (successful(row) and isinstance(timing.get("wall_s"), (int, float))
            and math.isfinite(timing["wall_s"]) and timing["wall_s"] > 0
            and isinstance(timing.get("peak_rss_kib"), (int, float)))


def checksum(row):
    s = row.get("stdout", "")
    return s if len(s) == 17 and s.endswith("\n") and all(c in "0123456789abcdef" for c in s[:16]) else None


def stats(values):
    median = statistics.median(values)
    return {"median": median, "min": min(values), "max": max(values),
            "range_percent_of_median": 100 * (max(values) - min(values)) / median if median else None}


class Capture:
    def __init__(self, args):
        self.args = args
        self.out = Path(args.output).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        if (self.out / "raw_results.json").exists():
            raise RuntimeError("Refusing to overwrite an existing raw_results.json")
        self.src = Path(args.sources).resolve()
        self.helper = self.resolve(args.measure_child)
        self.buster = self.resolve(args.buster)
        self.clang = self.resolve(args.clang)
        self.sequence = 0
        self.data = {"schema": 1, "status": "running", "methods": {}, "provenance": {},
                     "commands": [], "compile": {}, "runtime": {}, "validation": [],
                     "links": {}, "diagnostics": {}, "errors": []}
        common = ["-g", "-fno-pic", "-std=gnu17", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char"]
        self.prefixes = {
            "buster0": [self.buster, "cc", "-O0", "-fregister-allocator=fast", "-target", "x86_64-linux-gnu", "-march=baseline", *common],
            "clang00": [self.clang, "-O0", "-target", "x86_64-linux-gnu", "-march=x86-64", *common],
            "clang02": [self.clang, "-O2", "-target", "x86_64-linux-gnu", "-march=x86-64", *common],
        }
        for lane in LANES:
            (self.out / lane).mkdir(exist_ok=True)
        (self.out / "timings").mkdir(exist_ok=True)
        (self.out / "sources").mkdir(exist_ok=True)
        (self.out / "tools").mkdir(exist_ok=True)
        self.save()

    @staticmethod
    def resolve(executable):
        p = shutil.which(executable)
        if not p:
            raise RuntimeError("Missing executable: " + executable)
        return str(Path(p).resolve())

    def save(self):
        p = self.out / "raw_results.json.tmp"
        p.write_text(json.dumps(self.data, indent=2, sort_keys=True) + "\n")
        p.replace(self.out / "raw_results.json")

    def run(self, command, tag, timeout=30, measured=False):
        self.sequence += 1
        row = {"id": self.sequence, "tag": tag, "command": [str(x) for x in command],
               "timeout_s": timeout, "measured": measured,
               "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
        usage = self.out / "timings" / (f"{self.sequence:05d}.json")
        actual = [self.helper, str(usage), *row["command"]] if measured else row["command"]
        row["wrapper_command"] = actual
        start = time.monotonic()
        try:
            child = subprocess.Popen(actual, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     text=True, errors="replace", start_new_session=True)
            try:
                stdout, stderr = child.communicate(timeout=timeout)
                row.update(exit_code=child.returncode, stdout=stdout, stderr=stderr, timed_out=False)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                stdout, stderr = child.communicate()
                row.update(exit_code=child.returncode, stdout=stdout, stderr=stderr, timed_out=True)
        except OSError as exc:
            row.update(exit_code=None, stdout="", stderr="", error=str(exc), timed_out=False)
        row["wrapper_elapsed_s"] = time.monotonic() - start
        if measured and usage.exists():
            try:
                row["measurement"] = json.loads(usage.read_text())
            except (ValueError, OSError) as exc:
                row["measurement_error"] = str(exc)
        elif measured:
            row["measurement_error"] = "Timer produced no usage record"
        self.data["commands"].append(row)
        self.save()
        return row

    def skip(self, tag, reason):
        self.sequence += 1
        row = {"id": self.sequence, "tag": tag, "status": "skipped", "reason": reason}
        self.data["commands"].append(row)
        self.save()
        return row

    def artifact(self, path, tag):
        if not Path(path).is_file():
            return None
        record = manifest(path)
        objdump = shutil.which("objdump")
        if objdump:
            row = self.run([objdump, "-h", str(path)], tag + ":sections")
            sections = []
            if successful(row):
                for line in row["stdout"].splitlines():
                    fields = line.split()
                    if len(fields) >= 7 and fields[0].isdigit():
                        try:
                            sections.append({"name": fields[1], "bytes": int(fields[2], 16)})
                        except ValueError:
                            pass
            if sections:
                record["sections"] = sections
                record["text_bytes"] = sum(x["bytes"] for x in sections if x["name"] == ".text" or x["name"].startswith(".text."))
                record["dwarf_bytes"] = sum(x["bytes"] for x in sections if x["name"].startswith((".debug_", ".zdebug_")))
                record["has_debug_info"] = any(x["name"] == ".debug_info" and x["bytes"] > 0 for x in sections)
                record["has_debug_line"] = any(x["name"] == ".debug_line" and x["bytes"] > 0 for x in sections)
            record["section_command_id"] = row["id"]
        return record

    def provenance(self):
        before = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None
        affinity_error = None
        if before:
            try:
                os.sched_setaffinity(0, {before[0]})
            except OSError as exc:
                affinity_error = str(exc)
        self.data["methods"] = {
            "compile_samples": SAMPLES, "compile_warmups": 1, "runtime_samples": SAMPLES,
            "runtime_warmups": 1, "order": "Cyclically rotate the three lanes for warmups and each sample; sequential execution",
            "prefixes": self.prefixes, "linker": [self.clang, "-no-pie"],
            "affinity_before": before, "affinity_used": sorted(os.sched_getaffinity(0)) if before else None,
            "affinity_error": affinity_error, "compile_timeout_s": self.args.compile_timeout,
            "runtime_budget_s_per_lane_workload": 12,
            "timing": "Helper CLOCK_MONOTONIC begins before fork and ends after wait4; includes child fork/exec and program startup, excludes helper launch. Python wrapper elapsed is diagnostic only.",
            "rss": "wait4 ru_maxrss KiB: maximum observed process RSS including waited descendants; not a simultaneous process-tree sum",
            "generated_debug_policy": "All generated objects use -g. Buster -O0 uses its native pipeline with FAST allocator; no assumption of identical optimization pipelines with Clang -O0.",
            "target": "x86_64-linux-gnu; Buster -march=baseline and Clang -march=x86-64 are respective baseline CPU spellings",
            "contract": "Common -fwrapv -fno-strict-aliasing -funsigned-char. Buster accepts the first two as compatibility no-ops; this is not evidence of independent per-invocation controls.",
            "oracle": "Independent Python unsigned/binary64 oracle for 8 kernels, seeds 1/123456789/4294967295 and reps 1/3/19. cJSON is validated by identical exact stdout across the three lanes at reps 1/3/19; no seed argument.",
            "runtime": "Same workload-specific repetitions across lanes, chosen using Clang -O2 approximately 50ms calibration; all-lane preflight permits two predeclared repetition reductions when the slowest lane exceeds 0.6s. The final 7 samples plus warmup share a 12-second budget per lane/workload, excluding separate calibration/validation. Includes startup and initialization. Failed/incomplete/mismatching cells have no numerical winners.",
            "summary": "Medians and full min/max/range are descriptive observations on this hosted VM; median winners have no statistical-significance or universal-workload claim. One Buster lane is reused in both comparisons.",
            "dwarf_bytes": "Sum of .debug_* and .zdebug_* section payload bytes; excludes relocation sections, ELF headers and padding",
            "output_paths": "Lane directory names all seven characters; same source and object basename and total object-path length per source across lanes",
            "instrumentation": "Source metrics, verbose diagnostics, strict codegen, section inspection and DWARF verification run outside all timed sample intervals",
        }
        env_names = ("BUSTER_SOURCE_REVISION", "BUSTER_SOURCE_REF", "BUSTER_PRODUCER_CONFIGURATION", "BUSTER_RESEARCH_PRODUCTION_BASE", "CJSON_SOURCE_REVISION", "ImageOS", "ImageVersion", "GITHUB_SHA", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_WORKFLOW", "RUNNER_OS", "RUNNER_ARCH")
        self.data["provenance"] = {"timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                                   "platform": platform.platform(), "uname": list(platform.uname()),
                                   "environment_receipts": {k: os.environ[k] for k in env_names if k in os.environ},
                                   "sources": {}, "source_c_count": len(SOURCES),
                                   "binaries": {"buster": manifest(self.buster), "clang": manifest(self.clang), "measure_child": manifest(self.helper)}}
        for name in ("cpuinfo", "meminfo", "loadavg"):
            p = Path("/proc") / name
            if p.exists():
                self.data["provenance"][name] = p.read_text()
        for name in (*SOURCES, "cJSON.h"):
            p = self.src / name
            if not p.is_file():
                raise RuntimeError("Missing required source: " + str(p))
            shutil.copy2(p, self.out / "sources" / name)
            self.data["provenance"]["sources"][name] = {**manifest(p), "lines": len(p.read_bytes().splitlines())}
        for p in self.src.glob("*LICENSE*"):
            if p.is_file():
                shutil.copy2(p, self.out / "sources" / p.name)
        for name in ("capture.py", "runtime_oracle.py", "measure_child.c", "debug_probe.py"):
            p = Path(__file__).resolve().parent / name
            if p.exists():
                shutil.copy2(p, self.out / "tools" / name)
        self.data["provenance"]["versions"] = {
            "buster": self.run([self.buster, "cc", "--version"], "version:buster"),
            "clang": self.run([self.clang, "--version"], "version:clang")}
        self.data["provenance"]["producer_configuration"] = "Buster Release / Clang distribution Release; workflow/build receipts must corroborate this declaration"
        self.save()

    def compile(self):
        for source_index, name in enumerate(SOURCES):
            cells = {lane: {"warmup": None, "samples": [], "output_path": str(self.out / lane / (Path(name).stem + ".o"))} for lane in LANES}
            self.data["compile"][name] = cells
            for turn in range(SAMPLES + 1):
                order = LANES[(source_index + turn) % 3:] + LANES[:(source_index + turn) % 3]
                for lane in order:
                    cell = cells[lane]
                    tag = f"compile:{name}:{lane}:" + ("warmup" if turn == 0 else str(turn))
                    if turn and not measured_success(cell["warmup"]):
                        row = self.skip(tag, "Compilation warmup failed")
                    else:
                        p = Path(cell["output_path"])
                        if p.exists():
                            p.unlink()  # A stale object can never qualify a failed sample.
                        command = self.prefixes[lane] + ["-c", str(self.src / name), "-o", str(p)]
                        row = self.run(command, tag, timeout=self.args.compile_timeout, measured=True)
                        if successful(row) and p.is_file():
                            row["artifact"] = manifest(p)  # Outside timed interval.
                        elif successful(row):
                            row["error"] = "Compiler returned success but produced no object"
                    if turn == 0:
                        cell["warmup"] = row
                    else:
                        cell["samples"].append(row)
                    self.save()
            for lane in LANES:
                cell = cells[lane]
                rows = [cell["warmup"], *cell["samples"]]
                cell["hashes_stable"] = (all(measured_success(r) and r.get("artifact") for r in rows)
                                         and len({r["artifact"]["sha256"] for r in rows}) == 1)
                cell["final_artifact"] = self.artifact(cell["output_path"], f"compile:{name}:{lane}")
                cell["timing_eligible"] = len(cell["samples"]) == SAMPLES and all(measured_success(r) and r.get("artifact") for r in rows)
                cell["eligible"] = (cell["timing_eligible"] and cell["hashes_stable"]
                                    and (cell.get("final_artifact") or {}).get("has_debug_info", False)
                                    and (cell.get("final_artifact") or {}).get("has_debug_line", False))
                if cell["timing_eligible"]:
                    cell["wall_s"] = stats([r["measurement"]["wall_s"] for r in cell["samples"]])
                    cell["rss_kib"] = stats([r["measurement"]["peak_rss_kib"] for r in cell["samples"]])
                self.save()

    def runtime_command(self, lane, k, reps, seed):
        exe = self.data["links"][lane]["cjson" if k == 8 else "runtime"]["executable"]
        return [exe, str(reps)] if k == 8 else [exe, str(k), str(reps), str(seed)]

    def link(self):
        for lane in LANES:
            self.data["links"][lane] = {}
            for workload, objects in (("runtime", ("runtime.c",)), ("cjson", ("cjson_driver.c", "cJSON.c"))):
                exe = self.out / lane / workload
                cell = {"executable": str(exe)}
                self.data["links"][lane][workload] = cell
                if not all(self.data["compile"][n][lane]["eligible"] for n in objects):
                    cell["link"] = self.skip(f"link:{lane}:{workload}", "Required compiled object is ineligible")
                else:
                    cell["link"] = self.run([self.clang, "-no-pie", *[self.data["compile"][n][lane]["output_path"] for n in objects], "-lm", "-o", str(exe)], f"link:{lane}:{workload}", timeout=60)
                    if successful(cell["link"]) and exe.exists():
                        cell["artifact"] = self.artifact(exe, f"link:{lane}:{workload}")
                cell["eligible"] = successful(cell["link"]) and bool(cell.get("artifact"))
                self.save()

    def validate(self):
        p = Path(__file__).resolve().parent / "runtime_oracle.py"
        spec = importlib.util.spec_from_file_location("runtime_oracle", p)
        oracle = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(oracle)
        oracle.self_check()
        self.data["provenance"]["oracle"] = manifest(p)
        valid = {k: {lane: self.data["links"][lane]["cjson" if k == 8 else "runtime"]["eligible"] for lane in LANES} for k in range(9)}
        for k, kernel in enumerate(KERNELS):
            for seed in ((None,) if k == 8 else SEEDS):
                for reps in VALIDATION_REPS:
                    rows = {}
                    expected = None if k == 8 else oracle.expected_checksum(k, reps, seed) + "\n"
                    for lane in LANES:
                        if not self.data["links"][lane]["cjson" if k == 8 else "runtime"]["eligible"]:
                            row = self.skip(f"validate:{kernel}:{lane}:{reps}:{seed}", "No eligible executable")
                        else:
                            row = self.run(self.runtime_command(lane, k, reps, seed), f"validate:{kernel}:{lane}:{reps}:{seed}", timeout=12, measured=True)
                        rows[lane] = row
                    if k == 8:
                        refs = [checksum(rows[lane]) for lane in ("clang00", "clang02")]
                        expected = refs[0] if refs[0] and refs[0] == refs[1] and all(successful(rows[lane]) for lane in ("clang00", "clang02")) else None
                    for lane in LANES:
                        row = rows[lane]
                        row["expected_stdout"] = expected
                        row["checksum_matches"] = bool(expected and successful(row) and row.get("stdout") == expected)
                        valid[k][lane] &= row["checksum_matches"]
                        self.data["validation"].append({"kernel": kernel, "lane": lane, "seed": seed, "reps": reps, "row": row})
                    self.save()
        return valid

    def runtime(self, valid):
        for k, kernel in enumerate(KERNELS):
            cell = {"lanes": {lane: {"validated": valid[k][lane], "samples": []} for lane in LANES}, "calibration": []}
            self.data["runtime"][kernel] = cell
            if not valid[k]["clang02"]:
                cell["error"] = "Optimized calibration lane failed correctness validation"
                self.save()
                continue
            reps = 1024
            for attempt in range(5):
                row = self.run(self.runtime_command("clang02", k, reps, 123456789), f"calibrate:{kernel}:{attempt}", timeout=12, measured=True)
                cell["calibration"].append({"reps": reps, "row": row})
                if not measured_success(row) or not checksum(row):
                    cell["error"] = "Calibration command failed"
                    break
                elapsed = row["measurement"]["wall_s"]
                if 0.035 <= elapsed <= 0.085 or reps == 20000000:
                    break
                reps = max(1, min(20000000, round(reps * 0.05 / elapsed)))
                self.save()
            if cell.get("error"):
                self.save()
                continue
            cell["reps"] = reps
            # Predeclared budget control: use the same final repetition count in
            # all lanes. Keep all probes; permit two reductions if a slow debug
            # lane would threaten the eight measured runs' 12-second budget.
            cell["budget_calibration"] = []
            for adjustment in range(3):
                probes = {}
                for lane in LANES:
                    if not valid[k][lane]:
                        continue
                    row = self.run(self.runtime_command(lane, k, reps, 123456789), f"budget-calibrate:{kernel}:{adjustment}:{lane}", timeout=12, measured=True)
                    probes[lane] = row
                    cell["budget_calibration"].append({"lane": lane, "reps": reps, "row": row})
                reference = checksum(probes.get("clang02", {}))
                if not reference or any(not measured_success(r) or checksum(r) != reference for r in probes.values()):
                    cell["error"] = "Budget calibration failed or checksum mismatch"
                    break
                slowest = max(r["measurement"]["wall_s"] for r in probes.values())
                if slowest <= 0.6:
                    break
                if adjustment == 2:
                    cell["error"] = "Budget calibration remained above 0.6 seconds after two predeclared reductions"
                    break
                reps = max(1, min(reps - 1, int(reps * 0.5 / slowest))) if reps > 1 else 1
                cell["calibration_target_reduced_for_debug_budget"] = True
                self.save()
            cell["reps"] = reps
            if cell.get("error"):
                self.save()
                continue
            spent = {lane: 0.0 for lane in LANES}
            for turn in range(SAMPLES + 1):
                order = LANES[(k + turn) % 3:] + LANES[:(k + turn) % 3]
                rows = {}
                for lane in order:
                    lane_cell = cell["lanes"][lane]
                    tag = f"runtime:{kernel}:{lane}:" + ("warmup" if turn == 0 else str(turn))
                    remaining = 12 - spent[lane]
                    if not valid[k][lane]:
                        row = self.skip(tag, "Small-input correctness or compilation validation failed")
                    elif remaining <= 0 or (turn and not lane_cell.get("warmup_good", False)):
                        row = self.skip(tag, "Runtime budget exhausted or warmup failed")
                    else:
                        row = self.run(self.runtime_command(lane, k, reps, 123456789), tag, timeout=remaining, measured=True)
                        spent[lane] += row.get("measurement", {}).get("wall_s", row.get("wrapper_elapsed_s", 0))
                    rows[lane] = row
                expected = checksum(rows["clang02"]) if successful(rows["clang02"]) else None
                for lane in LANES:
                    row = rows[lane]
                    row["expected_stdout"] = expected
                    row["checksum_matches"] = bool(expected and successful(row) and row.get("stdout") == expected)
                    lane_cell = cell["lanes"][lane]
                    if turn == 0:
                        lane_cell["warmup"] = row
                        lane_cell["warmup_good"] = measured_success(row) and row["checksum_matches"]
                    else:
                        lane_cell["samples"].append(row)
                    self.save()
            for lane in LANES:
                lane_cell = cell["lanes"][lane]
                rows = lane_cell["samples"]
                lane_cell["budget_consumed_s"] = spent[lane]
                lane_cell["eligible"] = (lane_cell["validated"] and lane_cell.get("warmup_good", False)
                                         and len(rows) == SAMPLES and all(measured_success(r) and r.get("checksum_matches") for r in rows))
                if lane_cell["eligible"]:
                    lane_cell["wall_s"] = stats([r["measurement"]["wall_s"] for r in rows])
                    lane_cell["rss_kib"] = stats([r["measurement"]["peak_rss_kib"] for r in rows])
            self.save()

    def diagnostics(self):
        dwarfdump = self.args.llvm_dwarfdump or shutil.which("llvm-dwarfdump") or shutil.which("llvm-dwarfdump-21")
        for name in SOURCES:
            self.data["diagnostics"][name] = {}
            for lane in LANES:
                cell = self.data["compile"][name][lane]
                if not cell.get("eligible"):
                    continue
                row = self.run([dwarfdump, "--verify", cell["output_path"]], f"dwarf-verify:{name}:{lane}", timeout=60) if dwarfdump else self.skip(f"dwarf-verify:{name}:{lane}", "llvm-dwarfdump unavailable")
                self.data["diagnostics"][name][lane] = {"dwarf_verify": row}
            p = self.out / "buster0" / (Path(name).stem + ".diagnostic.o")
            metric = self.out / "buster0" / (Path(name).stem + ".metrics.txt")
            command = self.prefixes["buster0"] + ["-v", "-fverify-codegen", "-fcodegen-fallback-census", "-fsource-metrics=" + str(metric), "-c", str(self.src / name), "-o", str(p)]
            row = self.run(command, f"verified-source-metrics:{name}", timeout=self.args.compile_timeout)
            entry = self.data["diagnostics"][name].setdefault("buster0", {})
            entry["verified_codegen_source_metrics"] = row
            if p.exists():
                entry["diagnostic_artifact"] = self.artifact(p, f"verified-source-metrics:{name}")
            if metric.exists():
                entry["source_metrics_file"] = manifest(metric)
                entry["source_metrics_raw"] = metric.read_text()
                parsed = {}
                for line in entry["source_metrics_raw"].splitlines():
                    if "=" in line:
                        key, value = line.split("=", 1)
                        try:
                            value = int(value.strip())
                        except ValueError:
                            value = value.strip()
                        parsed[key.strip()] = value
                entry["source_metrics"] = parsed
            strict_p = self.out / "buster0" / (Path(name).stem + ".strict.o")
            strict_command = self.prefixes["buster0"] + ["-v", "-fverify-codegen", "-fcodegen-fallback-census", "-fno-machine-fallback", "-c", str(self.src / name), "-o", str(strict_p)]
            entry["strict_no_fallback"] = self.run(strict_command, f"strict-no-fallback:{name}", timeout=self.args.compile_timeout)
            for lane in LANES:
                cell = self.data["compile"][name][lane]
                cell["performance_eligible_before_diagnostics"] = cell["eligible"]
                dwarf_row = self.data["diagnostics"][name].get(lane, {}).get("dwarf_verify")
                # Missing optional verifier remains explicit; a performed failed
                # verification invalidates evidence about a debug artifact.
                dwarf_failed = bool(dwarf_row and dwarf_row.get("status") != "skipped" and not successful(dwarf_row))
                codegen_failed = lane == "buster0" and not successful(row)
                if dwarf_failed or codegen_failed:
                    cell["eligible"] = False
                    cell["diagnostic_invalidity"] = {"dwarf_failed": dwarf_failed, "codegen_failed": codegen_failed}
            self.save()
        for k, kernel in enumerate(KERNELS):
            required = ("cjson_driver.c", "cJSON.c") if k == 8 else ("runtime.c",)
            for lane in LANES:
                cell = self.data["runtime"][kernel]["lanes"][lane]
                cell["performance_eligible_before_diagnostics"] = cell.get("eligible", False)
                if not all(self.data["compile"][name][lane]["eligible"] for name in required):
                    cell["eligible"] = False
                    cell["diagnostic_invalidity"] = "A required source object failed eligibility or performed verification"
        self.save()

    def summary(self):
        lines = ["# Buster generated debug programs versus Clang", "",
                 "Hosted diagnostic observations only. Producers are Release builds. Both generated lanes have debug information. Buster `-O0 -g` uses its native FAST pipeline; lane names do not assert equal optimizer behavior.", "",
                 "One Buster lane is reused in both comparisons. Each cell requires seven successful samples, correctness gates where applicable, deterministic compilation output and debug sections; any performed DWARF or codegen-verifier failure suppresses its winner. Strict fallback rejection is a separate capability control and may disclose supported fallback without invalidating default compilation. A failed/incomplete cell has no numeric winner. 'Winner' means the smaller observed median/size only, without statistical significance or a universal compiler ranking. Smaller DWARF payload is a footprint result, not proof of better debugging.", "",
                 "Wall timing includes fork/exec and child startup. RSS is wait4 maximum process RSS, not summed simultaneous tree memory. DWARF bytes exclude relocations and ELF overhead. Full commands, stdout/stderr, failures, hashes, min/max and noise are in raw_results.json. Source metrics and DWARF verification are untimed.", ""]
        def pair(a, b, key, scale=1, unit=""):
            if not a.get("eligible") or not b.get("eligible"):
                return "ineligible", "ineligible", "—"
            av, bv = key(a), key(b)
            if av is None or bv is None or not av or not bv:
                return "unmeasured", "unmeasured", "—"
            return (f"{av * scale:.3f}{unit}", f"{bv * scale:.3f}{unit}",
                    (f"Buster {bv / av:.3f}×" if av < bv else f"Clang {av / bv:.3f}×" if bv < av else "tie"))
        for rival, title in (("clang00", "Debug / debug: Clang -O0 -g"), ("clang02", "Debug / optimized: Clang -O2 -g")):
            lines += ["## " + title, "", "| Translation unit | Metric | Buster | Clang | Observed winner / ratio |", "|---|---|---:|---:|---|"]
            for name in SOURCES:
                a, b = self.data["compile"][name]["buster0"], self.data["compile"][name][rival]
                for metric, key, scale, unit in (
                    ("Compile wall median", lambda c: c["wall_s"]["median"], 1000, " ms"),
                    ("Compile RSS median", lambda c: c["rss_kib"]["median"], 1, " KiB"),
                    (".text", lambda c: c["final_artifact"].get("text_bytes"), 1, " B"),
                    ("DWARF", lambda c: c["final_artifact"].get("dwarf_bytes"), 1, " B"),
                    ("Object", lambda c: c["final_artifact"].get("bytes"), 1, " B")):
                    x, y, winner = pair(a, b, key, scale, unit)
                    if a.get("eligible") and b.get("eligible") and metric.startswith("Compile"):
                        stat_key = "wall_s" if metric == "Compile wall median" else "rss_kib"
                        x += f" [{a[stat_key]['min']*scale:.3f}, {a[stat_key]['max']*scale:.3f}]"
                        y += f" [{b[stat_key]['min']*scale:.3f}, {b[stat_key]['max']*scale:.3f}]"
                    lines.append(f"| {name} | {metric} | {x} | {y} | {winner} |")
            lines += ["", "| Runtime workload | Common reps | Buster median [min, max] ms | Clang median [min, max] ms | Observed winner / ratio |", "|---|---:|---:|---:|---|"]
            for kernel in KERNELS:
                c = self.data["runtime"][kernel]
                a, b = c["lanes"]["buster0"], c["lanes"][rival]
                x, y, winner = pair(a, b, lambda q: q["wall_s"]["median"], 1000, "")
                if a.get("eligible") and b.get("eligible"):
                    x += f" [{a['wall_s']['min']*1000:.3f}, {a['wall_s']['max']*1000:.3f}]"
                    y += f" [{b['wall_s']['min']*1000:.3f}, {b['wall_s']['max']*1000:.3f}]"
                lines.append(f"| {kernel} | {c.get('reps', '—')} | {x} | {y} | {winner} |")
            lines += [""]
        lines += ["## Untimed controls", "", "| Translation unit | Buster verifier / metrics | Buster strict no fallback | Buster DWARF | Clang O0 DWARF | Clang O2 DWARF |", "|---|---|---|---|---|---|"]
        for name in SOURCES:
            d = self.data["diagnostics"].get(name, {})
            vals = []
            for lane in LANES:
                r = d.get(lane, {}).get("dwarf_verify")
                vals.append("pass" if r and successful(r) else "fail/unavailable")
            r = d.get("buster0", {}).get("verified_codegen_source_metrics")
            strict = d.get("buster0", {}).get("strict_no_fallback")
            lines.append(f"| {name} | {'pass' if r and successful(r) else 'fail'} | {'pass' if strict and successful(strict) else 'fail'} | {' | '.join(vals)} |")
        (self.out / "summary.md").write_text("\n".join(lines) + "\n")

    def execute(self):
        try:
            self.provenance()
            self.compile()
            self.link()
            valid = self.validate()
            self.runtime(valid)
            self.diagnostics()
            self.summary()
            self.data["status"] = "complete"
            self.data["all_timed_cells_eligible"] = (all(c["eligible"] for s in self.data["compile"].values() for c in s.values())
                and all(c.get("eligible", False) for r in self.data["runtime"].values() for c in r["lanes"].values()))
            self.save()
            return 0 if self.data["all_timed_cells_eligible"] else 1
        except Exception as exc:
            self.data["status"] = "aborted"
            self.data["errors"].append({"type": type(exc).__name__, "message": str(exc)})
            self.save()
            raise


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for flag in ("buster", "clang", "sources", "output", "measure-child"):
        p.add_argument("--" + flag, required=True)
    p.add_argument("--llvm-dwarfdump")
    p.add_argument("--compile-timeout", type=float, default=120)
    args = p.parse_args()
    if args.compile_timeout <= 0:
        p.error("--compile-timeout must be positive")
    return Capture(args).execute()


if __name__ == "__main__":
    sys.exit(main())
