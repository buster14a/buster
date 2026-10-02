#!/usr/bin/env python3
"""Exercise Linux x86-64 DWARF variable lifetimes through GDB (#1375).

Run only on an authorized correctness host. This fixture is new repository
material derived from issue #1375; no external source or asset is copied.
GDB is an independent consumer, not a format-only validator. Requires a GDB
built with Python support. All commands and raw consumer transcripts persist
in --output. Timings are hosted-runner diagnostics, not acceptance evidence.
"""
from __future__ import annotations

import argparse
import json
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

PROGRAM = """typedef unsigned long long U;
static U g;
__attribute__((noinline)) static U mix(U v)
{
    v ^= v >> 31;
    v *= 0x9E3779B97F4A7C15ull;
    g += v;
    return v;
}
__attribute__((noinline)) static void mark(int i)
{
    g += (U)i; /* STOP_MARK */
}
int main(void)
{
    U x = mix(1);
    U y = mix(x);
    g += x + y; /* STOP_LIVE */
    for (int i = 0; i < 3; i++)
    {
        mark(i); /* STOP_LOOP */
        U t = mix((U)i + 100);
        U u = mix(t);
        g += t + u;
    }
    return g == 16128008249110363803ull ? 0 : 1;
}
"""
EXPECTED_X = 11400714819323198485
MASK = (1 << 64) - 1
EXPECTED_Y = ((EXPECTED_X ^ (EXPECTED_X >> 31)) * EXPECTED_X) & MASK
RESULT_PREFIX = "BUSTER_GDB_RESULT "

# GDB's public Python contract: Frame.read_var starts at the current lexical
# block; find_sal identifies the stop's source location; older unwinds callers;
# Value.is_optimized_out means the value cannot be fetched. is_unavailable is
# available on newer consumers. Arbitrary read/format errors never count as
# a successfully unavailable variable.
# https://sourceware.org/gdb/current/onlinedocs/gdb.html/Frames-In-Python.html
# https://sourceware.org/gdb/current/onlinedocs/gdb.html/Values-From-Inferior.html
GDB_SCRIPT = r'''
import gdb, json, pathlib, time, traceback
config = json.loads(pathlib.Path(CONFIG_PATH).read_text())
checks = []
unavailable_count = 0
query_ns = 0

def demand(condition, message):
    if not condition:
        raise AssertionError(message)

def inspect(frame, name, expected, allow_unavailable=False):
    global query_ns, unavailable_count
    started = time.perf_counter_ns()
    value = frame.read_var(name)
    optimized = value.is_optimized_out
    unavailable = getattr(value, "is_unavailable", False)
    record = {"frame": frame.name(), "variable": name,
              "optimized_out": optimized, "unavailable": unavailable}
    if optimized or unavailable:
        demand(allow_unavailable, "VALUE {} unexpectedly unavailable".format(name))
        unavailable_count += 1
    else:
        actual = int(value)
        record["value"] = actual
        record["address"] = str(value.address) if value.address is not None else None
        demand(actual == expected, "VALUE {} got {} expected {}".format(name, actual, expected))
    record["query_ns"] = time.perf_counter_ns() - started
    query_ns += record["query_ns"]
    checks.append(record)

def stop_at(name, line):
    frame = gdb.newest_frame()
    demand(frame is not None, "FRAME missing newest frame")
    demand(frame.name() == name, "FRAME got {} expected {}".format(frame.name(), name))
    sal = frame.find_sal()
    demand(sal.symtab is not None, "SOURCE no source table")
    demand(pathlib.Path(sal.symtab.fullname()).resolve() == pathlib.Path(config["source"]).resolve(),
           "SOURCE unexpected file {}".format(sal.symtab.fullname()))
    demand(sal.line == line, "SOURCE got line {} expected {}".format(sal.line, line))
    checks.append({"stop": name, "line": sal.line, "pc": str(frame.pc())})
    return frame

result = {"status": "fail", "checks": checks}
try:
    for command in ("set pagination off", "set confirm off", "set breakpoint pending off",
                    "set print entry-values no", "set debuginfod enabled off"):
        gdb.execute(command)
    started = time.perf_counter_ns()
    gdb.execute("file " + json.dumps(config["executable"]))
    result["symbol_load_ns"] = time.perf_counter_ns() - started
    started = time.perf_counter_ns()
    for line in (config["live_line"], config["loop_line"], config["mark_line"]):
        try:
            bp = gdb.Breakpoint(source=config["source"], line=line)
        except gdb.error as error:
            raise AssertionError("BREAKPOINT {}".format(error))
        demand(not bp.pending and len(bp.locations) > 0, "BREAKPOINT unresolved line {}".format(line))
        for location in bp.locations:
            demand(location.source is not None and location.source[1] == line,
                   "BREAKPOINT moved line {} to {}".format(line, location.source))
    result["breakpoint_resolution_ns"] = time.perf_counter_ns() - started
    gdb.execute("run")
    live = stop_at("main", config["live_line"])
    inspect(live, "x", config["expected_x"])
    inspect(live, "y", config["expected_y"])
    for iteration in range(3):
        gdb.execute("continue")
        caller = stop_at("main", config["loop_line"])
        inspect(caller, "i", iteration)
        inspect(caller, "x", config["expected_x"], allow_unavailable=True)
        gdb.execute("continue")
        frame = stop_at("mark", config["mark_line"])
        inspect(frame, "i", iteration)
        caller = frame.older()
        demand(caller is not None and caller.name() == "main", "FRAME mark caller is not main")
        inspect(caller, "i", iteration)
        inspect(caller, "x", config["expected_x"], allow_unavailable=True)
        # x belongs to main's lexical scope. A callee must not expose it as a
        # local even when its caller still retains a correct value.
        try:
            frame.read_var("x")
        except gdb.error as error:
            demand('not found' in str(error).lower(), "SCOPE unexpected read error {}".format(error))
            checks.append({"scope": "mark", "x": "not in scope"})
        else:
            raise AssertionError("SCOPE mark exposes main's x")
    demand(unavailable_count > 0 or not config["require_unavailable"],
           "TRANSITION no explicitly unavailable value observed")
    gdb.execute("continue")
    demand(gdb.selected_inferior().pid == 0, "EXIT inferior did not terminate")
    # GDB's convenience exitcode is unavailable for signal exits.
    demand(int(gdb.parse_and_eval("$_exitcode")) == 0, "EXIT fixture returned nonzero")
    result["status"] = "pass"
except Exception as error:
    result["error"] = str(error)
    traceback.print_exc()
result["query_ns"] = query_ns
result["unavailable_count"] = unavailable_count
print("BUSTER_GDB_RESULT " + json.dumps(result, sort_keys=True))
gdb.execute("quit " + ("0" if result["status"] == "pass" else "1"))
'''


def invoke(command: list[str], output: Path, stem: str, timeout: int) -> dict:
    """Retain raw output, command, exit status and wall duration, including failure."""
    started = time.perf_counter_ns()
    try:
        completed = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        stdout, stderr = completed.stdout, completed.stderr
        returncode = completed.returncode
        timed_out = False
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        stdout = stdout.decode(errors="replace") if isinstance(stdout, bytes) else stdout
        stderr = stderr.decode(errors="replace") if isinstance(stderr, bytes) else stderr
        returncode, timed_out = None, True
    record = {"command": command, "returncode": returncode, "timed_out": timed_out,
              "wall_ns": time.perf_counter_ns() - started}
    (output / f"{stem}.stdout.txt").write_text(stdout)
    (output / f"{stem}.stderr.txt").write_text(stderr)
    (output / f"{stem}.command.json").write_text(json.dumps(record, indent=2) + "\n")
    record["stdout"] = stdout
    record["stderr"] = stderr
    return record


def debugger(gdb: str, executable: Path, source: Path, output: Path, stem: str,
             timeout: int, wrong_value: bool = False, require_unavailable: bool = True) -> dict:
    config = {"executable": str(executable.resolve()), "source": str(source.resolve()),
              "expected_x": EXPECTED_X + int(wrong_value), "expected_y": EXPECTED_Y,
              "require_unavailable": require_unavailable}
    for key, marker in (("live_line", "STOP_LIVE"), ("loop_line", "STOP_LOOP"), ("mark_line", "STOP_MARK")):
        config[key] = next(i for i, line in enumerate(PROGRAM.splitlines(), 1) if marker in line)
    config_path = output / f"{stem}.config.json"
    config_path.write_text(json.dumps(config, indent=2) + "\n")
    script_path = output / f"{stem}.gdb.py"
    script_path.write_text("CONFIG_PATH = " + repr(str(config_path.resolve())) + "\n" + GDB_SCRIPT)
    record = invoke([gdb, "--nx", "--quiet", "--batch", "-x", str(script_path.resolve())], output, stem, timeout)
    records = [line[len(RESULT_PREFIX):] for line in record["stdout"].splitlines() if line.startswith(RESULT_PREFIX)]
    if len(records) != 1:
        raise RuntimeError(f"{stem}: GDB did not emit exactly one result; see transcripts")
    result = json.loads(records[0])
    result["process_wall_ns"] = record["wall_ns"]
    result["returncode"] = record["returncode"]
    result["timed_out"] = record["timed_out"]
    (output / f"{stem}.result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def checked(command: list[str], output: Path, stem: str, timeout: int) -> dict:
    record = invoke(command, output, stem, timeout)
    if record["returncode"] != 0 or record["timed_out"]:
        raise RuntimeError(f"{stem}: command failed; see preserved transcripts")
    return record


def footprint(readelf: str, artifact: Path, output: Path, stem: str, timeout: int) -> dict:
    """Read section sizes from an independent ELF consumer, retain its listing."""
    record = checked([readelf, "--section-headers", "--wide", str(artifact)], output, stem, timeout)
    sections = {}
    for line in record["stdout"].splitlines():
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[\da-fA-F]+\s+[\da-fA-F]+\s+([\da-fA-F]+)\b", line)
        if match:
            sections[match.group(1)] = int(match.group(2), 16)
    if not any(name == ".text" or name.startswith(".text.") for name in sections):
        raise RuntimeError(f"{stem}: readelf listing has no measured text section")
    return {"file_bytes": artifact.stat().st_size, "sections": sections,
            "debug_bytes": sum(size for name, size in sections.items() if name.startswith(".debug_")),
            "debug_relocation_bytes": sum(size for name, size in sections.items()
                                          if name.startswith((".rela.debug_", ".rel.debug_"))),
            "text_bytes": sum(size for name, size in sections.items() if name == ".text" or name.startswith(".text."))}


def cost_slice(arguments, compilers: list[tuple[str, str]], source: Path, output: Path) -> dict:
    """Keep compile-only, emitted bytes, external/native link and consumer costs separate.

    Trials reverse compiler and g/g0 order alternately. Every trial's exact
    commands, tool status and wall duration are preserved; differences near
    fixed process startup cannot support a production throughput claim.
    """
    rows, order = [], []
    for trial in range(arguments.trials):
        ordered = compilers if trial % 2 == 0 else list(reversed(compilers))
        variants = ("g", "g0") if trial % 2 == 0 else ("g0", "g")
        for label, compiler in ordered:
            for mode in ("fast", "quality"):
                for variant in variants:
                    stem = f"cost-{trial}-{label}-{mode}-{variant}"
                    order.append(stem)
                    obj = output / (stem + ".o")
                    record = checked([compiler, "cc", "-" + variant, "-O0", f"-fregister-allocator={mode}",
                                      "-c", str(source), "-o", str(obj)], output, stem + "-compile", arguments.timeout)
                    row = {"trial": trial, "compiler": label, "mode": mode, "debug": variant,
                           "compile_only_wall_ns": record["wall_ns"],
                           "object": footprint(arguments.readelf, obj, output, stem + "-sections", arguments.timeout),
                           "links": {}}
                    if variant == "g0" and row["object"]["debug_bytes"]:
                        raise RuntimeError(f"{stem}: -g0 object retained DWARF sections")
                    for linker, command in (("external", [arguments.clang, "-no-pie"]),
                                            ("buster", [compiler, "cc"])):
                        executable = output / (stem + "-" + linker)
                        linked = checked(command + [str(obj), "-o", str(executable)], output,
                                         stem + "-link-" + linker, arguments.timeout)
                        link = {"link_wall_ns": linked["wall_ns"],
                                "executable": footprint(arguments.readelf, executable, output,
                                                        stem + "-" + linker + "-sections", arguments.timeout)}
                        if variant == "g":
                            consumed = debugger(arguments.gdb, executable, source, output,
                                                stem + "-" + linker + "-consumer", arguments.timeout)
                            # Keep failures as real measurements and explicit correctness
                            # failures, rather than silently discarding baseline rows.
                            link["consumer"] = consumed
                        else:
                            checked([str(executable)], output, stem + "-" + linker + "-run", arguments.timeout)
                        row["links"][linker] = link
                    rows.append(row)
    return {"unit": "nanoseconds", "trials": arguments.trials, "execution_order": order, "rows": rows,
            "limitations": ["diagnostic correctness host; qualified-host acceptance remains pending",
                            "small fixed fixture includes compiler/linker process startup",
                            "external link includes the host C runtime; Buster link has its own entry stub",
                            "symbol load, breakpoint resolution, stopped-variable queries and total debugger wall are distinct",
                            "debug bytes are a footprint measurement, not an end-to-end speedup"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ide", required=True)
    parser.add_argument("--baseline-ide", help="optional same-host before/after compiler")
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--gdb", default="gdb")
    parser.add_argument("--readelf", default="readelf")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--skip-costs", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=60)
    arguments = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        parser.error("the executed consumer slice requires Linux x86-64")
    if arguments.trials < 1 or arguments.trials > 15:
        parser.error("--trials must be between 1 and 15")
    for tool in (arguments.ide, arguments.clang, arguments.gdb, arguments.readelf,
                 *([arguments.baseline_ide] if arguments.baseline_ide else [])):
        if not shutil.which(tool):
            parser.error(f"required tool not found: {tool}")
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = output / "lifetime.c"
    source.write_text(PROGRAM)
    failures, results, costs = [], {}, None
    try:
        checked([arguments.gdb, "--version"], output, "gdb-version", arguments.timeout)
        checked([arguments.clang, "--version"], output, "clang-version", arguments.timeout)
        reference = output / "clang-reference"
        checked([arguments.clang, "-g", "-O0", str(source), "-o", str(reference)], output, "clang-build", arguments.timeout)
        result = debugger(arguments.gdb, reference, source, output, "clang-consumer", arguments.timeout, require_unavailable=False)
        results["clang"] = result
        if result["status"] != "pass" or result["returncode"] != 0:
            failures.append("independent Clang/GDB fixture control failed")
        compilers = [("candidate", arguments.ide)]
        if arguments.baseline_ide:
            compilers.insert(0, ("baseline", arguments.baseline_ide))
        for label, compiler in compilers:
            for mode in ("fast", "quality"):
                stem = f"{label}-{mode}"
                executable = output / stem
                command = [compiler, "cc", "-g", "-O0", f"-fregister-allocator={mode}"]
                checked(command + [str(source), "-o", str(executable)], output, stem + "-build", arguments.timeout)
                result = debugger(arguments.gdb, executable, source, output, stem + "-consumer", arguments.timeout)
                results[stem] = result
                if result["status"] != "pass" or result["returncode"] != 0:
                    failures.append(stem + ": " + result.get("error", "consumer process failed"))
                wrong = debugger(arguments.gdb, executable, source, output, stem + "-wrong-value", arguments.timeout, wrong_value=True)
                if wrong["status"] != "fail" or wrong["returncode"] != 1 or not wrong.get("error", "").startswith("VALUE x got"):
                    failures.append(stem + ": wrong-value negative control did not fail for the expected value assertion")
                no_debug = output / (stem + "-g0")
                checked([compiler, "cc", "-g0", "-O0", f"-fregister-allocator={mode}", str(source), "-o", str(no_debug)],
                        output, stem + "-g0-build", arguments.timeout)
                absent = debugger(arguments.gdb, no_debug, source, output, stem + "-g0-consumer", arguments.timeout)
                if absent["status"] != "fail" or absent["returncode"] != 1 or not absent.get("error", "").startswith("BREAKPOINT"):
                    failures.append(stem + ": absent-debug negative control did not reject missing source breakpoints")
        if not arguments.skip_costs:
            costs = cost_slice(arguments, compilers, source, output)
            for row in costs["rows"]:
                for linker, link in row["links"].items():
                    consumer = link.get("consumer")
                    if consumer and (consumer["status"] != "pass" or consumer["returncode"] != 0):
                        failures.append(f"cost trial {row['trial']} {row['compiler']} {row['mode']} {linker}: " +
                                        consumer.get("error", "consumer process failed"))
    except (RuntimeError, OSError, ValueError) as error:
        failures.append(str(error))
    summary = {"results": results, "costs": costs, "failures": failures,
               "qualified_performance_acceptance": "pending; this oracle supplies correctness-host diagnostics"}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    for failure in failures:
        print(failure, file=sys.stderr)
    print(f"debug lifetime oracle: {len(failures)} failure(s); evidence in {output}")
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
