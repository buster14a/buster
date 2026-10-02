#!/usr/bin/env python3
"""Bounded Linux x86-64 source-debugging comparison, not a throughput benchmark.

Three generated-product lanes: Buster ordinary -O0 -g FAST, Clang -O0 -g,
and Clang -O2 -g. All objects use baseline ISA, identical semantic flags,
and the same Clang -no-pie link. GDB checks three exact source stops, nine
parameter/local observations, one caller unwind, and the exit status.
Explicitly unavailable values, missing symbols, and wrong values are distinct.
Raw commands, stdout/stderr, scripts, source hashes and DWARF verification
are retained. The Buster producer must be built in Release by the caller.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

PREFIX = "DEBUG_PROBE_RESULT "
EXPECTED_STDOUT = "checksum=49\n"
QUERY_COUNT = 9

GDB_SCRIPT = r'''
import gdb, json, pathlib, traceback
config = json.loads(pathlib.Path(CONFIG_PATH).read_text())
result = {"status": "incomplete", "breakpoints": [], "stops": [],
          "variables": [], "call_stacks": [], "errors": []}
source = pathlib.Path(config["source"]).resolve()
stop_numbers = []
observed = set()
by_number = {}

def stopped(event):
    global stop_numbers
    stop_numbers = [bp.number for bp in getattr(event, "breakpoints", ())]

def inspect(frame, name, expected, point, frame_role):
    record = {"point": point, "frame_role": frame_role, "frame": frame.name(),
              "variable": name, "expected": expected}
    try:
        value = frame.read_var(name)
        if value.is_optimized_out or getattr(value, "is_unavailable", False):
            record["status"] = "explicitly_unavailable"
            record["optimized_out"] = bool(value.is_optimized_out)
            record["unavailable"] = bool(getattr(value, "is_unavailable", False))
        else:
            record["actual"] = int(value)
            record["status"] = "correct" if int(value) == expected else "wrong_value"
            record["address"] = str(value.address) if value.address is not None else None
    except Exception as error:
        message = str(error)
        record["error"] = message
        # Absent debug symbols are not a successful unavailable-value answer.
        # Arbitrary memory/format errors are never counted as truthful missing.
        if isinstance(error, (gdb.error, ValueError)) and (
                "not found" in message.lower() or "no symbol" in message.lower()):
            record["status"] = "missing_symbol"
        else:
            record["status"] = "query_error"
    result["variables"].append(record)

try:
    for command in ("set pagination off", "set confirm off", "set breakpoint pending off",
                    "set print entry-values no", "set debuginfod enabled off"):
        gdb.execute(command)
    gdb.execute("file " + json.dumps(config["executable"]))
    gdb.events.stop.connect(stopped)
    for point in config["points"]:
        row = {"point": point["name"], "requested_line": point["line"], "locations": []}
        result["breakpoints"].append(row)
        try:
            bp = gdb.Breakpoint(source=config["source"], line=point["line"])
            row["number"] = bp.number
            row["pending"] = bool(bp.pending)
            for location in bp.locations:
                loc_source = location.source
                row["locations"].append({"address": str(location.address),
                                          "source": list(loc_source) if loc_source else None})
            row["exact_resolution"] = bool(row["locations"]) and all(
                location["source"] and location["source"][1] == point["line"]
                and pathlib.Path(location["source"][0]).resolve() == source
                for location in row["locations"])
            by_number[bp.number] = (point, bp)
        except Exception as error:
            row["error"] = str(error)
            row["exact_resolution"] = False
    gdb.execute("run")
    # The fixture has no loop; guard this consumer too if symbols are malformed.
    for unused in range(10):
        if gdb.selected_inferior().pid == 0:
            break
        frame = gdb.newest_frame()
        if frame is None:
            raise RuntimeError("stopped inferior has no newest frame")
        sal = frame.find_sal()
        actual_source = pathlib.Path(sal.symtab.fullname()).resolve() if sal.symtab else None
        matched = [by_number[number] for number in stop_numbers if number in by_number]
        if not matched:
            result["errors"].append("inferior stopped without a fixture breakpoint")
            break
        for point, bp in matched:
            bp.enabled = False
            exact = (actual_source == source and sal.line == point["line"]
                     and frame.name() == point["frame"])
            row = {"point": point["name"], "requested_line": point["line"],
                   "actual_line": sal.line, "actual_source": str(actual_source),
                   "actual_frame": frame.name(), "exact_stop": exact, "pc": str(frame.pc())}
            result["stops"].append(row)
            if not exact or point["name"] in observed:
                continue
            observed.add(point["name"])
            for name, expected in point["variables"].items():
                inspect(frame, name, expected, point["name"], "current")
            if point.get("caller"):
                caller = frame.older()
                names = []
                walker = frame
                for depth in range(8):
                    if walker is None:
                        break
                    names.append(walker.name())
                    walker = walker.older()
                caller_ok = caller is not None and caller.name() == point["caller"]["frame"]
                result["call_stacks"].append({"point": point["name"], "frames": names,
                    "caller_correct": caller_ok, "backtrace": gdb.execute("bt", to_string=True)})
                if caller_ok:
                    for name, expected in point["caller"]["variables"].items():
                        inspect(caller, name, expected, point["name"], "caller")
        stop_numbers = []
        gdb.execute("continue")
    if gdb.selected_inferior().pid != 0:
        result["errors"].append("inferior did not exit within bounded stop count")
    else:
        try:
            result["exit_code"] = int(gdb.parse_and_eval("$_exitcode"))
        except Exception as error:
            result["errors"].append("inferior exit code unavailable: " + str(error))
    result["status"] = "completed" if not result["errors"] else "consumer_error"
except Exception as error:
    result["status"] = "consumer_error"
    result["errors"].append(str(error))
    traceback.print_exc()
result["exact_stops"] = len(observed)
print("DEBUG_PROBE_RESULT " + json.dumps(result, sort_keys=True))
gdb.execute("quit " + ("0" if result["status"] == "completed" else "1"))
'''


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def invoke(command: list[str], output: Path, stem: str, deadline: float,
           limit: float = 8.0) -> dict:
    record = {"command": command, "returncode": None, "timed_out": False}
    started = time.perf_counter_ns()
    remaining = deadline - time.monotonic()
    stdout = stderr = ""
    if remaining <= 0:
        record["status"] = "budget_exhausted"
    else:
        try:
            completed = subprocess.run(command, capture_output=True, text=True,
                                       timeout=min(limit, remaining))
            stdout, stderr = completed.stdout, completed.stderr
            record["returncode"] = completed.returncode
            record["status"] = "completed"
        except subprocess.TimeoutExpired as error:
            stdout, stderr = error.stdout or "", error.stderr or ""
            stdout = stdout.decode(errors="replace") if isinstance(stdout, bytes) else stdout
            stderr = stderr.decode(errors="replace") if isinstance(stderr, bytes) else stderr
            record["timed_out"] = True
            record["status"] = "timeout"
        except OSError as error:
            stderr = str(error)
            record["status"] = "execution_error"
    record["wall_ns"] = time.perf_counter_ns() - started
    record["stdout_file"] = stem + ".stdout.txt"
    record["stderr_file"] = stem + ".stderr.txt"
    (output / record["stdout_file"]).write_text(stdout)
    (output / record["stderr_file"]).write_text(stderr)
    write_json(output / (stem + ".command.json"), record)
    return {**record, "stdout": stdout, "stderr": stderr}


def successful(record: dict) -> bool:
    return record["status"] == "completed" and record["returncode"] == 0


def points(source: str) -> list[dict]:
    specs = [
        {"name": "main", "marker": "STOP_MAIN:", "frame": "main",
         "variables": {"input": 5, "saved": 7}},
        {"name": "callee", "marker": "STOP_CALLEE:", "frame": "debug_add",
         "variables": {"parameter": 7, "local": 14},
         "caller": {"frame": "main", "variables": {"input": 5, "saved": 7}}},
        {"name": "result", "marker": "STOP_RESULT:", "frame": "main",
         "variables": {"input": 5, "saved": 7, "result": 42}},
    ]
    for spec in specs:
        lines = [number for number, line in enumerate(source.splitlines(), 1)
                 if spec["marker"] in line]
        if len(lines) != 1:
            raise ValueError("source must contain one " + spec["marker"])
        spec["line"] = lines[0]
    return specs


def consume(gdb: str, executable: Path, source: Path, output: Path,
            stem: str, deadline: float) -> dict:
    config = {"executable": str(executable), "source": str(source),
              "points": points(source.read_text())}
    config_path = output / (stem + ".config.json")
    script_path = output / (stem + ".gdb.py")
    write_json(config_path, config)
    script_path.write_text("CONFIG_PATH = " + repr(str(config_path)) + "\n" + GDB_SCRIPT)
    command = invoke([gdb, "--nx", "--quiet", "--batch", "-x", str(script_path)],
                     output, stem, deadline)
    lines = [line[len(PREFIX):] for line in command["stdout"].splitlines()
             if line.startswith(PREFIX)]
    if len(lines) == 1:
        try:
            result = json.loads(lines[0])
        except ValueError as error:
            result = {"status": "consumer_error", "errors": ["invalid JSON: " + str(error)]}
    else:
        result = {"status": "consumer_error", "errors": ["expected exactly one GDB result"]}
    result["process"] = {key: value for key, value in command.items()
                         if key not in ("stdout", "stderr")}
    result["script_sha256"] = digest(script_path)
    observations = result.get("variables", [])
    result["counts"] = {status: sum(row["status"] == status for row in observations)
                        for status in ("correct", "wrong_value", "explicitly_unavailable",
                                       "missing_symbol", "query_error")}
    result["counts"].update({"requested_queries": QUERY_COUNT,
                             "observed_queries": len(observations),
                             "unobserved_queries": max(0, QUERY_COUNT - len(observations)),
                             "exact_source_stops": result.get("exact_stops", 0),
                             "requested_source_stops": 3,
                             "correct_callers": sum(row["caller_correct"]
                                                    for row in result.get("call_stacks", [])),
                             "requested_callers": 1})
    write_json(output / (stem + ".result.json"), result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--buster", required=True, help="Release-built Buster ide executable")
    parser.add_argument("--clang", required=True)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    parser.add_argument("--gdb", default="gdb")
    parser.add_argument("--dwarfdump", help="optional explicit llvm-dwarfdump executable")
    parser.add_argument("--timeout-seconds", type=float, default=55.0)
    arguments = parser.parse_args()
    if platform.system() != "Linux" or platform.machine().lower() not in ("x86_64", "amd64"):
        parser.error("the executed fixture requires Linux x86-64")
    if not 1 <= arguments.timeout_seconds <= 55:
        parser.error("--timeout-seconds must be between 1 and 55")
    tool_paths = {}
    for name in ("buster", "clang", "gdb"):
        found = shutil.which(getattr(arguments, name))
        if not found:
            parser.error("required executable not found: " + getattr(arguments, name))
        tool_paths[name] = str(Path(found).resolve())
    if arguments.dwarfdump:
        dwarf_tool = shutil.which(arguments.dwarfdump)
        if not dwarf_tool:
            parser.error("requested llvm-dwarfdump executable not found")
    else:
        dwarf_tool = next((found for name in ("llvm-dwarfdump", "llvm-dwarfdump-22",
                         "llvm-dwarfdump-21", "llvm-dwarfdump-20", "llvm-dwarfdump-19",
                         "llvm-dwarfdump-18", "llvm-dwarfdump-17", "llvm-dwarfdump-16")
                         if (found := shutil.which(name))), None)
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    deadline = started + arguments.timeout_seconds
    fixture = Path(__file__).resolve().with_suffix(".c")
    source = output / "debug_probe.c"
    source.write_bytes(fixture.read_bytes())
    summary = {"schema": 1, "host": {"system": platform.system(),
               "machine": platform.machine(), "platform": platform.platform()},
               "source_sha256": digest(source), "probe_sha256": digest(Path(__file__).resolve()),
               "source": str(source), "tools": {}, "lanes": {}, "failures": [],
               "limitations": ["one small executed fixture; not full debugger or standards coverage",
                 "ordinary Buster -O0 retains its default bounded FAST transformations",
                 "Buster producer Release mode must be established by enclosing build workflow",
                 "default -g debug formats may differ; consumer usability is the primary metric",
                 "unobserved source queries and absent symbols are separate from explicit unavailable values",
                 "wall durations include process startup and are not performance acceptance data"]}
    for name, path in tool_paths.items():
        summary["tools"][name] = {"path": path, "sha256": digest(Path(path))}
        if name != "buster":
            summary["tools"][name]["version"] = invoke([path, "--version"], output,
                                                        name + "-version", deadline, 2)
        else:
            summary["tools"][name]["version_query"] = "unsupported; artifact SHA-256 retained"
    if dwarf_tool:
        dwarf_tool = str(Path(dwarf_tool).resolve())
        summary["tools"]["dwarfdump"] = {"path": dwarf_tool,
            "version": invoke([dwarf_tool, "--version"], output, "dwarfdump-version", deadline, 2)}
    common = ["-target", "x86_64-linux-gnu", "-std=gnu17", "-g", "-fno-pic",
              "-fwrapv", "-fno-strict-aliasing", "-funsigned-char"]
    lanes = [("buster_O0g_FAST", [tool_paths["buster"], "cc", "-O0", *common,
                               "-march=baseline", "-fregister-allocator=fast"]),
             ("clang_O0g", [tool_paths["clang"], "-O0", *common, "-march=x86-64"]),
             ("clang_O2g", [tool_paths["clang"], "-O2", *common, "-march=x86-64"])]
    for name, compiler in lanes:
        obj, executable = output / (name + ".o"), output / name
        row = {"compile": invoke(compiler + ["-c", str(source), "-o", str(obj)],
                                 output, name + "-compile", deadline)}
        summary["lanes"][name] = row
        if not successful(row["compile"]):
            summary["failures"].append(name + ": compilation failed or budget exhausted")
            continue
        row["object"] = {"bytes": obj.stat().st_size, "sha256": digest(obj)}
        row["link"] = invoke([tool_paths["clang"], "-no-pie", str(obj), "-o", str(executable)],
                             output, name + "-link", deadline)
        if not successful(row["link"]):
            summary["failures"].append(name + ": common Clang link failed")
            continue
        row["executable"] = {"bytes": executable.stat().st_size, "sha256": digest(executable)}
        row["run"] = invoke([str(executable)], output, name + "-run", deadline, 2)
        row["checksum_correct"] = successful(row["run"]) and row["run"]["stdout"] == EXPECTED_STDOUT
        if not row["checksum_correct"]:
            summary["failures"].append(name + ": independent checksum/exit check failed")
        if dwarf_tool:
            record = invoke([dwarf_tool, "--verify", str(executable)], output,
                            name + "-dwarf-verify", deadline, 3)
            row["dwarf_verify"] = {"status": "pass" if successful(record) else "failed",
                                   "process": record}
            if not successful(record):
                summary["failures"].append(name + ": llvm-dwarfdump verification failed")
        else:
            row["dwarf_verify"] = {"status": "not_run", "reason": "llvm-dwarfdump unavailable"}
        row["gdb"] = consume(tool_paths["gdb"], executable, source, output, name + "-gdb", deadline)
        consumer = row["gdb"]
        if consumer["status"] != "completed" or consumer.get("exit_code") != 0:
            summary["failures"].append(name + ": debugger consumer failed")
        if consumer["counts"]["wrong_value"] or consumer["counts"]["query_error"]:
            summary["failures"].append(name + ": wrong value or debugger query error observed")
        if name == "clang_O0g" and (consumer["counts"]["correct"] != QUERY_COUNT
                or consumer["counts"]["exact_source_stops"] != 3
                or consumer["counts"]["correct_callers"] != 1):
            summary["failures"].append("independent Clang -O0 fixture control incomplete")
    summary["wall_seconds"] = time.monotonic() - started
    summary["status"] = "pass" if not summary["failures"] else "failed"
    write_json(output / "summary.json", summary)
    print(json.dumps({"status": summary["status"], "output": str(output),
          "source_sha256": summary["source_sha256"], "failures": summary["failures"],
          "lanes": {name: {"checksum_correct": row.get("checksum_correct"),
                    "dwarf_verify": row.get("dwarf_verify", {}).get("status"),
                    "gdb": row.get("gdb", {}).get("counts")}
                    for name, row in summary["lanes"].items()}}, sort_keys=True))
    return int(bool(summary["failures"]))


if __name__ == "__main__":
    sys.exit(main())
