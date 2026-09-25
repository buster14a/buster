#!/usr/bin/env python3
"""Diagnostic collector only; not a production fix or an acceptance-policy change."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys

BASE = "83c5f229288ecd8e23588c11b9ad19edc8bf4be2"
TREE = "b310e6573e6c89c618f8466941be4b995021a410"
OUT = Path(os.environ.get("PROBE_OUT", "/tmp/buster-atomic-compound-evidence"))
OUT.mkdir(parents=True, exist_ok=True)


def inspect_source():
    paths = ["AGENTS.md", "docs/agents/research.md", "docs/agents/driver.md",
             "docs/agents/frontend/semantic-validation.md", "docs/agents/frontend/atomics.md",
             "src/buster/lib/compiler/frontend/c/c_gen.c",
             "src/buster/lib/compiler/frontend/c/c_parse.c",
             "src/buster/lib/compiler/frontend/c/c_internal.h",
             "src/buster/lib/compiler/ir/ir.h",
             "src/buster/tests/compiler/frontend/c/c_test.c"]
    audits = sorted(Path("docs/performance-audits").glob("*.md"))
    if audits:
        paths.append(str(audits[-1]))
        print("NEWEST_AUDIT", audits[-1], flush=True)
    root = OUT / "source"
    manifest = []
    for name in paths:
        path = Path(name)
        if path.is_file():
            data = path.read_bytes()
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            manifest.append({"path": name, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
    (OUT / "source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    source = Path(paths[5]).read_text().splitlines()
    symbols = ["c_ir_emit_compound_assignment", "c_ir_atomic_operation", "c_ir_emit_atomic_rmw"]
    excerpts = []
    for symbol in symbols:
        for index, line in enumerate(source):
            if re.match(r"^(?:BUSTER_C_INTERNAL|BUSTER_GLOBAL_LOCAL) .*\b" + symbol + r"\(", line):
                end = index + 1
                while end < len(source) and source[end] != "}":
                    end += 1
                text = "\n".join(f"{i+1}: {source[i]}" for i in range(index, min(end + 1, len(source))))
                excerpts.append(text)
                print(text, flush=True)
    (OUT / "source-functions.txt").write_text("\n\n".join(excerpts) + "\n")
    print("SOURCE_INSPECTION_COMPLETE", flush=True)


class Collector:
    def __init__(self):
        self.serial = 0
        self.records = []
        self.outcomes = []
        (OUT / "commands").mkdir(exist_ok=True)
        (OUT / "inputs").mkdir(exist_ok=True)
        (OUT / "binaries").mkdir(exist_ok=True)

    def run(self, argv, seconds=30):
        self.serial += 1
        stem = f"commands/{self.serial:05d}"
        record = {"id": self.serial, "argv": list(map(str, argv)), "cwd": os.getcwd(), "timeout_seconds": seconds}
        try:
            child = subprocess.Popen(record["argv"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
            try:
                stdout, stderr = child.communicate(timeout=seconds)
                record["timeout"] = False
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                stdout, stderr = child.communicate()
                record["timeout"] = True
            record["exit"] = child.returncode
        except OSError as exc:
            stdout, stderr = b"", str(exc).encode()
            record["launch_error"] = str(exc)
            record["exit"] = None
            record["timeout"] = False
        for kind, data in (("stdout", stdout), ("stderr", stderr)):
            name = stem + "." + kind
            (OUT / name).write_bytes(data)
            record[kind] = name
        self.records.append(record)
        with (OUT / "commands.jsonl").open("a") as stream:
            stream.write(json.dumps(record) + "\n")
        return record

    def case(self, name, source, compiler, flags):
        input_path = OUT / "inputs" / (name + ".c")
        input_path.write_text(source)
        tag = compiler[0].replace("/", "_") + "_" + str(self.serial + 1)
        binary = OUT / "binaries" / tag
        built = self.run(compiler + flags + [str(input_path), "-o", str(binary)])
        executed = self.run([str(binary)], 5) if built["exit"] == 0 and not built["timeout"] else None
        result = {"case": name, "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                  "compiler": compiler, "flags": flags, "compile_command": built["id"],
                  "compile_exit": built["exit"], "compile_timeout": built["timeout"],
                  "run_command": executed["id"] if executed else None,
                  "run_exit": executed["exit"] if executed else None,
                  "run_timeout": executed["timeout"] if executed else None,
                  "expected_exit": 0}
        self.outcomes.append(result)
        with (OUT / "outcomes.jsonl").open("a") as stream:
            stream.write(json.dumps(result) + "\n")
        print("CASE", json.dumps(result), flush=True)


def probes():
    collector = Collector()
    for command in (["clang", "--version"], ["gcc", "--version"], ["uname", "-a"], ["git", "rev-parse", "HEAD", "HEAD^{tree}"]):
        collector.run(command)
    rows = [
        ("add_fraction", "int", "-2", "+", "double", "1.5", "0"),
        ("sub_fraction", "int", "2", "-", "double", "1.5", "0"),
        ("add_same", "int", "-2", "+", "int", "1", "-1"),
        ("sub_same", "int", "2", "-", "int", "1", "1"),
        ("add_integral_double", "int", "-2", "+", "double", "2.0", "0"),
        ("mul_fraction", "int", "5", "*", "double", "1.5", "7"),
        ("byte_div_wide", "unsigned char", "200", "/", "int", "256", "0"),
        ("bool_sub_wide", "_Bool", "1", "-", "int", "2", "1"),
        ("float_sub_precision", "float", "1.0f", "-", "double", "0x1.000001p0", "-0x1p-24"),
        ("byte_add_wrap", "unsigned char", "254", "+", "int", "2", "0"),
    ]
    common = ["-std=gnu17", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-g0"]
    profiles = []
    for cc, target in (("clang", ["--target=x86_64-linux-gnu"]), ("gcc", ["-m64"])):
        for optimization in (["-O0"], ["-O2"], ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]):
            profiles.append(([cc], common + target + optimization))
    ide = str(Path("build/Release/ide").resolve())
    for allocator in ("none", "mir-stack", "fast", "quality"):
        for frontend in ("-ffrontend-ssa", "-fno-frontend-ssa"):
            flags = common + ["-target", "x86_64-linux", "-O0", "-fverify-codegen", "-fregister-allocator=" + allocator, frontend]
            if allocator != "none":
                flags.append("-fno-machine-fallback")
            profiles.append(([ide, "cc"], flags))
    for row, typ, initial, operation, rhs_type, rhs_value, expected in rows:
        for atomic in (False, True):
            declared = "_Atomic(" + typ + ")" if atomic else typ
            expression = "(value " + operation + "= right())"
            for context in ("initializer", "return", "statement"):
                body = {"initializer": "double result = " + expression + "; return result;",
                        "return": "return " + expression + ";",
                        "statement": expression + "; return 0;"}[context]
                result_expected = "0" if context == "statement" else expected
                source = (f"static volatile {declared} value;\nstatic volatile {rhs_type} rhs;\n"
                          "static volatile unsigned hits;\n"
                          f"static {rhs_type} right(void) {{ hits += 1; return rhs; }}\n"
                          f"static double evaluate(void) {{ {body} }}\n"
                          f"int main(void) {{ value = {initial}; rhs = {rhs_value}; hits = 0; "
                          f"double observed = evaluate(); return (observed != ({result_expected})) | "
                          f"((value != ({expected})) << 1) | ((hits != 1u) << 2); }}\n")
                name = row + ("_atomic_" if atomic else "_ordinary_") + context
                for compiler, flags in profiles:
                    collector.case(name, source, compiler, flags)
    summary = {}
    for outcome in collector.outcomes:
        key = "buster" if len(outcome["compiler"]) == 2 else outcome["compiler"][0]
        counts = summary.setdefault(key, {"pass": 0, "compile_failure": 0, "runtime_failure": 0, "timeout": 0})
        status = "timeout" if outcome["compile_timeout"] or outcome["run_timeout"] else "compile_failure" if outcome["compile_exit"] != 0 else "runtime_failure" if outcome["run_exit"] != 0 else "pass"
        counts[status] += 1
    (OUT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print("SUMMARY", json.dumps(summary), flush=True)
    failed = any(outcome["compile_exit"] != 0 or outcome["run_exit"] != 0 or outcome["compile_timeout"] or outcome["run_timeout"] for outcome in collector.outcomes)
    return 1 if failed else 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--inspect", action="store_true")
    args = parser.parse_args()
    if args.inspect:
        inspect_source()
    else:
        sys.exit(probes())
