#!/usr/bin/env python3
"""Isolated phase-order observations; hosted wall times are never measurements.

Requires the diagnostic-only BUSTER_PHASE_ORDER_SCHEDULE instrumentation.
Uses the unchanged native throughput generator, not a second corpus definition.
Every command, failed artifact, diagnostic and executable output is retained.
"""

import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import sys


SCHEDULES = ("FADP", "FAPD", "FPAD", "PFAD", "FADPFD", "FADPfd", "FADPFADP", "FAPfD")
FIXTURE = r"""/* Isolated hypotheses, not assumed canonical-IR shapes. */
volatile unsigned phase_observed;
unsigned phi_identity(unsigned x, unsigned condition)
{
    unsigned y;
    if (condition) y = x + 0u;
    else y = x;
    return y;
}
unsigned phi_constant_cast(unsigned x, unsigned condition)
{
    unsigned z = 7u, y;
    if (condition) y = (unsigned)z;
    else y = z;
    return y * 3u + (x & 0u);
}
unsigned equal_bits_not_ids(unsigned x, unsigned condition)
{
    unsigned z = 7u, y;
    if (condition) y = z + 0u;
    else y = z;
    return y * 3u + x;
}
unsigned same_type_address(unsigned* pointer, unsigned condition)
{
    unsigned* q = (unsigned*)pointer;
    (void)condition;
    return *(&*q);
}
unsigned pointer_join(unsigned* pointer, unsigned condition)
{
    unsigned* q;
    if (condition) q = pointer + 0;
    else q = pointer;
    return *(&*q);
}
unsigned dead_phi(unsigned b, unsigned a)
{
    unsigned x = a + 2u, y;
    if (b) y = x + 0u;
    else y = x;
    (void)(y + 1u);
    return 0u;
}
unsigned parameter_cap(unsigned n, unsigned x)
{
    unsigned a = x, b = x, c = x, d = x, e = x;
    while (n--)
    {
        a = b + 0u; b = c + 0u; c = d + 0u; d = e + 0u; e = x + 0u;
    }
    return a;
}
unsigned no_match(unsigned x, unsigned y)
{
    return (x * 33u) ^ (y >> 3);
}
unsigned negative_volatile(unsigned x, unsigned y)
{
    unsigned unused = phase_observed;
    phase_observed = x;
    (void)unused;
    return x ^ y;
}
unsigned negative_division(unsigned x, unsigned y)
{
    unsigned unused = x / (y | 1u);
    (void)unused;
    return x;
}
"""
HARNESS = r"""#include <stdio.h>
extern volatile unsigned phase_observed;
extern unsigned phi_identity(unsigned, unsigned);
extern unsigned phi_constant_cast(unsigned, unsigned);
extern unsigned equal_bits_not_ids(unsigned, unsigned);
extern unsigned same_type_address(unsigned*, unsigned);
extern unsigned pointer_join(unsigned*, unsigned);
extern unsigned dead_phi(unsigned, unsigned);
extern unsigned parameter_cap(unsigned, unsigned);
extern unsigned no_match(unsigned, unsigned);
extern unsigned negative_volatile(unsigned, unsigned);
extern unsigned negative_division(unsigned, unsigned);
int main(void)
{
    unsigned state = 20260907u;
    for (unsigned i = 0; i < 4096u; ++i)
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        unsigned x = state, y = state * 2654435761u, c = i & 1u;
        unsigned data[2] = {x, y};
        phase_observed = y;
        unsigned r[11];
        r[0] = phi_identity(x, c); r[1] = phi_constant_cast(x, c);
        r[2] = equal_bits_not_ids(x, c); r[3] = same_type_address(data + c, c);
        r[4] = pointer_join(data + c, c); r[5] = dead_phi(c, x);
        r[6] = parameter_cap(i & 7u, x); r[7] = no_match(x, y);
        r[8] = negative_volatile(x, y); r[9] = negative_division(x, y);
        r[10] = phase_observed;
        printf("%u %u %u %u %u %u %u %u %u %u %u\n",
               r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10]);
    }
    return 0;
}
"""


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_records(data):
    records = []
    for line in data.splitlines():
        if line.startswith(("PHASE_", "IR_FAST", "CODEGEN")):
            record = {"record": line.split(" ", 1)[0]}
            for key, value in re.findall(r"(\w+)=([^\s]+)", line):
                try:
                    record[key] = int(value)
                except ValueError:
                    record[key] = value
            records.append(record)
    return records


def function_observations(records):
    functions = []
    current = None
    for record in records:
        if record["record"] == "PHASE_FUNCTION":
            current = {"before": record, "steps": []}
            functions.append(current)
        elif record["record"] == "PHASE_STEP" and current is not None:
            current["steps"].append(record)
        elif record["record"] == "PHASE_DONE" and current is not None:
            current["after"] = record
    return functions


def elf_text(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        return {"error": "not little-endian ELF64"}
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    offset, width, count, names_index = header[6], header[11], header[12], header[13]
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, offset + i * width) for i in range(count)]
    names_header = sections[names_index]
    names = data[names_header[4]:names_header[4] + names_header[5]]
    text, executable = [], []
    for section in sections:
        name = names[section[0]:].split(b"\0", 1)[0].decode("utf-8", "replace")
        payload = data[section[4]:section[4] + section[5]]
        item = {"name": name, "bytes": section[5], "sha256": hashlib.sha256(payload).hexdigest()}
        if section[2] & 4:
            executable.append(item)
        if name == ".text" or name.startswith(".text."):
            text.append(item)
    return {"object_sha256": sha(path), "object_bytes": len(data), "text": text,
            "text_bytes": sum(item["bytes"] for item in text), "executable_sections": executable}


def disassembly_stats(data):
    functions = {}
    current = None
    for line in data.splitlines():
        match = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if match:
            current = {"instructions": 0, "encoded_bytes": 0, "stack_references": 0,
                       "mnemonics": collections.Counter()}
            functions[match[1]] = current
        match = re.match(r"^\s*[0-9a-f]+:\s+((?:[0-9a-f]{2}\s)+)\s*(\S+)(.*)$", line)
        if match and current is not None:
            current["instructions"] += 1
            current["encoded_bytes"] += len(match[1].split())
            current["stack_references"] += int(bool(re.search(r"\([^)]*%(?:rsp|rbp)", match[3])))
            current["mnemonics"][match[2]] += 1
    return functions


class Run:
    def __init__(self, root, output, timeout):
        self.root, self.output, self.timeout = root, output, timeout
        self.commands = []

    def command(self, name, argv, schedule=None, timeout=None, trace_parameters=False):
        stdout = self.output / (name + ".stdout.log")
        stderr = self.output / (name + ".stderr.log")
        stdout.parent.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ)
        env.pop("BUSTER_PHASE_ORDER_SCHEDULE", None)
        env.pop("BUSTER_PHASE_ORDER_TRACE_PARAMETERS", None)
        if schedule is not None:
            env["BUSTER_PHASE_ORDER_SCHEDULE"] = schedule
        if trace_parameters:
            env["BUSTER_PHASE_ORDER_TRACE_PARAMETERS"] = "1"
        record = {"name": name, "argv": [str(v) for v in argv], "cwd": str(self.root),
                  "schedule": schedule, "trace_parameters": trace_parameters,
                  "stdout": str(stdout.relative_to(self.output)),
                  "stderr": str(stderr.relative_to(self.output)), "timeout_seconds": timeout or self.timeout}
        with stdout.open("wb") as out, stderr.open("wb") as err:
            try:
                process = subprocess.run(record["argv"], cwd=self.root, env=env, stdout=out, stderr=err,
                                         timeout=record["timeout_seconds"], check=False)
                record["status"] = process.returncode
            except subprocess.TimeoutExpired:
                record.update(status=None, failure="timeout")
            except OSError as error:
                record.update(status=None, failure=str(error))
        self.commands.append(record)
        with (self.output / "commands.jsonl").open("a", encoding="utf-8") as file:
            file.write(json.dumps(record, sort_keys=True) + "\n")
        return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ide", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--throughput", type=Path)
    parser.add_argument("--source-root", type=Path, help="pristine production source snapshot for corpus inputs")
    parser.add_argument("--generated", type=Path, default=Path("build/generated"))
    parser.add_argument("--cjson", type=Path, default=Path("external/cjson/cJSON.c"))
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--memory-reference", action="store_true")
    parser.add_argument("--skip-unity", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source_root = args.source_root.resolve() if args.source_root else root
    output, ide, generated = args.output.resolve(), args.ide.resolve(), args.generated.resolve()
    output.mkdir(parents=True, exist_ok=False)
    run = Run(root, output, args.timeout)
    inputs = output / "inputs"
    inputs.mkdir()
    fixture, harness = inputs / "interactions.c", inputs / "harness.c"
    fixture.write_text(FIXTURE, encoding="utf-8")
    harness.write_text(HARNESS, encoding="utf-8")
    revision_command = run.command("metadata/revision", ["git", "rev-parse", "HEAD"])
    revision = (output / revision_command["stdout"]).read_text().strip()
    summary = {"schema": "buster-phase-order-research-v1", "revision": revision,
               "compiler": str(ide), "compiler_sha256": sha(ide), "python": sys.version,
               "source_root": str(source_root), "generated_headers": str(generated),
               "machine": platform.uname()._asdict(), "schedules": SCHEDULES,
               "measurement_scope": "structural and semantic hosted observations; no performance acceptance",
               "corpus_generator": "unchanged tools/throughput/throughput.c tp_generate profile=ci seed=20260907 scale=1",
               "unavailable": [], "runs": [], "inputs": [], "failures": []}
    source_manifest = [{"path": str(path.relative_to(source_root)), "sha256": sha(path)}
                       for folder in (source_root / "src", source_root / "tests")
                       for path in sorted(folder.rglob("*")) if path.is_file()]
    manifest = inputs / "source_manifest.json"
    manifest.write_text(json.dumps(source_manifest, indent=2, sort_keys=True) + "\n")
    summary["source_manifest"] = {"path": str(manifest.relative_to(output)), "files": len(source_manifest),
                                  "sha256": sha(manifest)}
    throughput = args.throughput.resolve() if args.throughput else output / "throughput"
    if args.throughput is None:
        run.command("generator/build", [args.clang, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                       "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-Isrc",
                                       "-DBUSTER_SINGLE_THREADED=1", "tools/throughput/throughput.c",
                                       "tools/throughput/shared.c", "-lm", "-o", throughput])
    if throughput.is_file():
        run.command("generator/generate", [throughput, "generate", "--output", inputs / "throughput",
                                          "--profile", "ci", "--seed", "20260907", "--scale", "1"])
    else:
        summary["unavailable"].append("native throughput generator failed to build; see generator/build logs")
    sources = [("interactions", fixture, []),
               ("canonical_fast", source_root / "tests/basic_c_canonical_fast.c", []),
               ("string", source_root / "src/buster/lib/string.c", ["-I" + str(source_root / "src"), "-I" + str(generated)]),
               ("integer", source_root / "src/buster/lib/integer.c", ["-I" + str(source_root / "src"), "-I" + str(generated)])]
    sources.extend((p.stem, p, []) for p in sorted((inputs / "throughput").glob("*.c")))
    if not args.skip_unity:
        sources.append(("unity", source_root / "src/buster/apps/ide/ide.c",
                        ["-I" + str(source_root / "src"), "-I" + str(generated), "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0"]))
    if args.cjson.is_file():
        sources.append(("cjson", args.cjson.resolve(), []))
    else:
        summary["unavailable"].append("optional pinned cJSON source absent")
    oracle_object, oracle_exe = output / "oracle.o", output / "oracle.exe"
    oracle_build = run.command("oracle/build", [args.clang, "-O0", "-fwrapv", "-fno-strict-aliasing",
                                               "-funsigned-char", "-c", fixture, "-o", oracle_object])
    oracle_link = run.command("oracle/link", [args.clang, "-O0", harness, oracle_object, "-o", oracle_exe])
    oracle_result = run.command("oracle/run", [oracle_exe]) if oracle_link["status"] == 0 else None
    oracle_hash = sha(output / oracle_result["stdout"]) if oracle_result and oracle_result["status"] == 0 else None
    summary["oracle"] = {"build_status": oracle_build["status"], "link_status": oracle_link["status"],
                         "status": oracle_result["status"] if oracle_result else None, "stdout_sha256": oracle_hash,
                         "cases": 4096, "checks_per_case": 11}
    for name, source, flags in sources:
        summary["inputs"].append({"name": name, "path": str(source), "sha256": sha(source),
                                  "bytes": source.stat().st_size, "flags": flags})
        modes = ("fast", "quality") if name in ("interactions", "control_flow", "backend_pressure") else ("fast",)
        configurations = [(mode, schedule, []) for mode in modes for schedule in SCHEDULES]
        if args.memory_reference and name == "interactions":
            configurations.extend(("fast", schedule, ["-fno-frontend-ssa"]) for schedule in SCHEDULES)
        for mode, schedule, additional in configurations:
            label = "memory-form" if additional else "direct-ssa"
            key = f"{name}/{label}/{mode}/{schedule}"
            obj = output / (key + ".o")
            obj.parent.mkdir(parents=True, exist_ok=True)
            command = run.command(key + "/compile", [ide, "cc", "-O0", "-g0", "-c", "-v",
                                  "-fcanonical-fast", "-fverify-codegen", "-fno-machine-fallback", *flags, *additional,
                                  "-fregister-allocator=" + mode, source, "-o", obj], schedule,
                                  trace_parameters=schedule == "FADP" and mode == "fast" and not additional)
            records = parse_records((output / command["stdout"]).read_text(errors="replace") + "\n" +
                                    (output / command["stderr"]).read_text(errors="replace"))
            row = {"source": name, "frontend": label, "allocator": mode, "schedule": schedule,
                   "command": command, "records": records, "functions": function_observations(records)}
            if command["status"] == 0 and obj.is_file():
                try:
                    row["artifact"] = elf_text(obj)
                except (ValueError, IndexError, struct.error) as error:
                    row["artifact"] = {"error": str(error), "object_sha256": sha(obj)}
                dis = run.command(key + "/objdump", ["objdump", "-dr", "-w", obj])
                row["disassembly"] = disassembly_stats((output / dis["stdout"]).read_text(errors="replace"))
                if name == "interactions":
                    exe = output / (key + ".exe")
                    link = run.command(key + "/link", [args.clang, "-O0", harness, obj, "-o", exe])
                    executed = run.command(key + "/run", [exe]) if link["status"] == 0 else None
                    result_hash = sha(output / executed["stdout"]) if executed else None
                    row["semantics"] = {"link_status": link["status"],
                                        "status": executed["status"] if executed else None,
                                        "stdout_sha256": result_hash,
                                        "matches_clang": bool(oracle_hash and executed and executed["status"] == 0 and
                                                              result_hash == oracle_hash)}
            else:
                summary["failures"].append({"run": key, "kind": "compilation", "status": command["status"]})
            if not any(r["record"] == "PHASE_FUNCTION" for r in records):
                row["instrumentation_unavailable"] = True
            summary["runs"].append(row)
            (output / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    summary["command_count"] = len(run.commands)
    summary["failed_commands"] = [c for c in run.commands if c["status"] != 0]
    summary["semantic_mismatches"] = [{k: row[k] for k in ("source", "frontend", "allocator", "schedule")}
                                      for row in summary["runs"] if "semantics" in row and not row["semantics"]["matches_clang"]]
    (output / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"output": str(output), "revision": revision, "runs": len(summary["runs"]),
                      "failed_commands": len(summary["failed_commands"]),
                      "semantic_mismatches": len(summary["semantic_mismatches"])}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
