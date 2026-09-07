#!/usr/bin/env python3
"""Measure Buster's production selector replay and end-to-end object compilation.

The C benchmark reports warm-IR selection separately from module preparation.
This runner adds fresh-process time to an object, immutable work counts, target
features, artifact hashes, raw samples and host/compiler/source identities.
Wall-clock changes are observations, never automatic CI performance gates.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import time
import unittest

FIELDS = (
    "version iterations functions fallback_functions ir_instructions mir_instructions "
    "min_ns median_ns prepare_median_ns total_median_ns arena_bytes peak_arena_bytes module_bytes"
).split()
WORK_FIELDS = ("functions", "fallback_functions", "ir_instructions", "mir_instructions")
ALLOCATORS = ("none", "fast", "mir-stack", "quality")
MASK64 = (1 << 64) - 1


def parse_report(text: str) -> tuple[dict[str, int], str]:
    rows = [line for line in text.splitlines() if line.startswith("BENCH_SELECT ")]
    targets = [line for line in text.splitlines() if line.startswith("BENCH_SELECT_TARGET ")]
    if len(rows) != 1 or len(targets) != 1:
        raise ValueError("expected exactly one selection row and one target row")
    result: dict[str, int] = {}
    for token in rows[0].split()[1:]:
        key, separator, value = token.partition("=")
        if not separator or key in result or not value.isascii() or not value.isdecimal():
            raise ValueError(f"invalid or duplicate metric: {token}")
        result[key] = int(value)
    if any(key not in result for key in FIELDS):
        raise ValueError("missing required selection metric")
    if result["version"] != 1:
        raise ValueError("unsupported selection report version")
    if not all(result[key] > 0 for key in ("iterations", "functions", "mir_instructions", "median_ns")):
        raise ValueError("empty workload or invalid measurement")
    if result["fallback_functions"]:
        raise ValueError("selection coverage fell; refusing a misleading ns/MIR result")
    if not result["min_ns"] <= result["median_ns"] <= result["total_median_ns"]:
        raise ValueError("inconsistent timing bounds")
    if result["peak_arena_bytes"] > result["arena_bytes"]:
        raise ValueError("inconsistent arena byte counts")
    return result, targets[0]


def run(command: list[str], timeout: float) -> tuple[str, int]:
    start = time.perf_counter_ns()
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding="utf-8", errors="replace", timeout=timeout)
    duration = time.perf_counter_ns() - start
    if completed.returncode:
        raise RuntimeError(f"command failed ({completed.returncode}): {command!r}\n{completed.stdout}")
    return completed.stdout, duration


def run_program(command: list[str], timeout: float) -> tuple[bytes, bytes]:
    # Preserve byte values and stream identity: replacement decoding could
    # make distinct invalid UTF-8 outputs compare equal.
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    if completed.returncode:
        raise RuntimeError(f"program failed ({completed.returncode}): {command!r}\n{completed.stderr!r}")
    return completed.stdout, completed.stderr


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def workload_small(count: int = 256) -> str:
    definitions = ["typedef unsigned long long U;\n"]
    calls = []
    total = 0
    for index in range(count):
        arity = index % 5
        parameters = ", ".join(f"U a{i}" for i in range(arity)) or "void"
        expression = " + ".join(f"a{i}" for i in range(arity)) or f"{index + 1}ULL"
        definitions.append(f"__attribute__((noinline)) static U f{index}({parameters}) "
                           f"{{ U x = {expression}; x ^= x >> 7; x *= 6364136223846793005ULL; return x + {index}ULL; }}\n")
        arguments = [index + i + 1 for i in range(arity)]
        value = sum(arguments) if arguments else index + 1
        value = (((value ^ (value >> 7)) * 6364136223846793005) + index) & MASK64
        total = (total + value) & MASK64
        calls.append(f"    sum += f{index}({', '.join(str(a) + 'ULL' for a in arguments)});\n")
    return "".join(definitions) + "int main(void) { U sum = 0;\n" + "".join(calls) + f"return sum != {total}ULL; }}\n"


def workload_large(count: int = 512) -> str:
    lines = ["typedef unsigned long long U;\n__attribute__((noinline)) static U chain(U x) {\n"]
    value = 1234567
    for index in range(count):
        shift = index % 13 + 1
        constant = index * 2 + 3
        lines.append(f"x ^= x >> {shift}; x = x * {constant}ULL + {index}ULL;\n")
        value = ((value ^ (value >> shift)) * constant + index) & MASK64
    return "".join(lines) + f"return x; }}\nint main(void) {{ return chain(1234567ULL) != {value}ULL; }}\n"


def workload_abi() -> str:
    definitions = ["typedef unsigned long long U;\n"]
    checks = []
    for arity in range(25):
        parameters = ", ".join(f"U a{i}" for i in range(arity)) or "void"
        expression = " + ".join(f"a{i}" for i in range(arity)) or "7ULL"
        arguments = ", ".join(f"{i + 1}ULL" for i in range(arity))
        expected = arity * (arity + 1) // 2 if arity else 7
        definitions.append(f"__attribute__((noinline)) static U arity{arity}({parameters}) {{ return {expression}; }}\n")
        checks.append(f"ok &= arity{arity}({arguments}) == {expected}ULL;\n")
    definitions.append("""
struct Pair { U a; double b; };
__attribute__((noinline)) static struct Pair pair(struct Pair p) { p.a += 7; p.b += 0.5; return p; }
__attribute__((noinline)) static U memory(signed char* a, unsigned short* b, U* c, int i) { return (U)(long long)a[i] + b[i] + c[i]; }
__attribute__((noinline)) static U loop(U n) { U sum = 0; for (U i = 0; i < n; ++i) { if (i & 1) sum += i; else sum ^= i; } return sum; }
""")
    return "".join(definitions) + "int main(void) { int ok = 1;\n" + "".join(checks) + """
struct Pair p = {5, 2.0}; p = pair(p); ok &= p.a == 12 && p.b == 2.5;
signed char a[2] = {-8, -3}; unsigned short b[2] = {60000, 65000}; U c[2] = {0, 4294967296ULL};
ok &= memory(a, b, c, 1) == 4295032293ULL; ok &= loop(10) == 9;
return !ok; }
"""


def make_sources(directory: Path) -> list[Path]:
    directory.mkdir(parents=True, exist_ok=True)
    sources = []
    for name, text in (("many-small", workload_small()), ("one-large", workload_large()), ("abi-and-memory", workload_abi())):
        path = directory / f"{name}.c"
        path.write_text(text, encoding="utf-8")
        sources.append(path.resolve())
    return sources


def check_failures(compiler: Path, directory: Path, timeout: float) -> None:
    bad = {
        "empty": "",
        "syntax": "int f( {",
        "declarations": "int external(void);",
        "fallback": "int too_many(" + ", ".join(f"int a{i}" for i in range(25)) + "){return a0 + a24;}",
    }
    for name, source in bad.items():
        path = directory / f"reject-{name}.c"
        path.write_text(source, encoding="utf-8")
        completed = subprocess.run([str(compiler), "bench-select", str(path)], capture_output=True, text=True, timeout=timeout)
        if completed.returncode == 0 or any(line.startswith("BENCH_SELECT ") for line in completed.stdout.splitlines()):
            raise RuntimeError(f"benchmark incorrectly accepted {name}: {completed.stdout}")
    for arguments in ([], ["one.c", "two.c"]):
        completed = subprocess.run([str(compiler), "bench-select", *arguments], capture_output=True, timeout=timeout)
        if completed.returncode == 0:
            raise RuntimeError("bench-select accepted incorrect argument count")


def host_info() -> dict[str, object]:
    cpu = platform.processor()
    info = Path("/proc/cpuinfo")
    if info.exists():
        for line in info.read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.partition(":")[2].strip()
                break
    return {"system": platform.system(), "release": platform.release(), "machine": platform.machine(), "cpu": cpu,
            "affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None}


def benchmark(args: argparse.Namespace) -> dict[str, object]:
    compilers = {"baseline": args.compiler.resolve()}
    if args.candidate:
        compilers["candidate"] = args.candidate.resolve()
    sources = [path.resolve() for path in args.source] if args.source else make_sources(args.workloads)
    report: dict[str, object] = {"schema_version": 1, "host": host_info(), "build_description": args.build_description,
                               "compilers": {key: {"path": str(path), "sha256": digest(path)} for key, path in compilers.items()},
                               "workloads": []}
    with tempfile.TemporaryDirectory(prefix="buster-selection-") as temporary:
        directory = Path(temporary)
        if args.check:
            for compiler in compilers.values():
                check_failures(compiler, directory, args.timeout)
        for source in sources:
            source_hash = digest(source)
            execution_output: tuple[bytes, bytes] | None = None
            records = {key: [] for key in compilers}
            identities: dict[str, tuple[object, ...]] = {}
            object_hashes: dict[str, str] = {}
            for iteration in range(args.samples):
                # Alternate A/B ordering; do not run timing children in parallel.
                names = list(compilers)
                if iteration % 2:
                    names.reverse()
                for name in names:
                    compiler = compilers[name]
                    text, _ = run([str(compiler), "bench-select", str(source)], args.timeout)
                    metrics, target = parse_report(text)
                    identity = (target, *(metrics[key] for key in WORK_FIELDS))
                    if identities and identity != next(iter(identities.values())):
                        raise RuntimeError(f"target or selected work changed for {source}")
                    identities[name] = identity
                    objects: dict[str, object] = {}
                    for allocator in args.allocators:
                        output = directory / "result.o"
                        output.unlink(missing_ok=True)
                        command = [str(compiler), "cc", "-g0", "-O2", f"-fregister-allocator={allocator}", "-c", str(source), "-o", str(output)]
                        _, duration = run(command, args.timeout)
                        if not output.is_file() or not output.stat().st_size:
                            raise RuntimeError("compiler produced no object")
                        sha = digest(output)
                        if allocator in object_hashes and sha != object_hashes[allocator]:
                            raise RuntimeError(f"object bytes changed for {source}, allocator {allocator}")
                        object_hashes[allocator] = sha
                        objects[allocator] = {"wall_ns": duration, "sha256": sha, "bytes": output.stat().st_size}
                        if args.check and iteration == 0:
                            executable = directory / ("program.exe" if os.name == "nt" else "program")
                            executable.unlink(missing_ok=True)
                            run([str(compiler), "cc", "-g0", "-O2", f"-fregister-allocator={allocator}", str(source), "-o", str(executable)], args.timeout)
                            output_bytes = run_program([str(executable)], args.timeout)
                            if execution_output is not None and execution_output != output_bytes:
                                raise RuntimeError(f"program output changed for {source}, allocator {allocator}")
                            execution_output = output_bytes
                    records[name].append({"selection": metrics, "target": target, "objects": objects})
            summary = {}
            for name, samples in records.items():
                ns = statistics.median(sample["selection"]["median_ns"] for sample in samples)
                count = samples[0]["selection"]["mir_instructions"]
                summary[name] = {"selection_median_ns": ns, "selection_ns_per_mir": ns / count,
                                 "compile_median_ns": {mode: statistics.median(s["objects"][mode]["wall_ns"] for s in samples) for mode in args.allocators}}
            if digest(source) != source_hash:
                raise RuntimeError(f"source changed while benchmarking: {source}")
            report["workloads"].append({"path": str(source), "sha256": source_hash, "bytes": source.stat().st_size,
                                        "samples": records, "summary": summary})
            print(json.dumps({"workload": source.name, "summary": summary}, sort_keys=True), flush=True)
    for name, compiler in compilers.items():
        if digest(compiler) != report["compilers"][name]["sha256"]:
            raise RuntimeError(f"compiler changed while benchmarking: {compiler}")
    return report


class ReportTests(unittest.TestCase):
    def setUp(self) -> None:
        values = [1, 30, 2, 0, 80, 70, 1000, 1100, 100, 1200, 2000, 1500, 400]
        self.row = "BENCH_SELECT " + " ".join(f"{k}={v}" for k, v in zip(FIELDS, values))
        self.text = self.row + "\nBENCH_SELECT_TARGET arch=x86_64 os=linux model=znver5 features=none\n"

    def test_valid(self) -> None:
        self.assertEqual(parse_report(self.text)[0]["mir_instructions"], 70)

    def test_reject_invalid(self) -> None:
        for before, after in (("version=1", "version=2"), ("functions=2", "functions=0"),
                              ("mir_instructions=70", "mir_instructions=0"), ("fallback_functions=0", "fallback_functions=1"),
                              ("median_ns=1100", "median_ns=-1"), ("min_ns=1000", "min_ns=2000"),
                              ("peak_arena_bytes=1500", "peak_arena_bytes=3000"), ("iterations=30 ", "")):
            with self.subTest(before=before), self.assertRaises(ValueError):
                parse_report(self.text.replace(before, after))

    def test_reject_duplicates(self) -> None:
        for text in (self.text + self.row, self.text.replace(" version=1", " version=1 version=1"), self.row):
            with self.assertRaises(ValueError):
                parse_report(text)

    def test_program_output_bytes(self) -> None:
        a = run_program([sys.executable, "-c", "import sys; sys.stdout.buffer.write(bytes([128]))"], 10)
        b = run_program([sys.executable, "-c", "import sys; sys.stdout.buffer.write(bytes([129]))"], 10)
        c = run_program([sys.executable, "-c", "import sys; sys.stderr.buffer.write(bytes([128]))"], 10)
        self.assertNotEqual(a, b)
        self.assertNotEqual(a, c)

    def test_additive_metric(self) -> None:
        self.assertEqual(parse_report(self.text.replace(" version=1", " future_counter=3 version=1"))[0]["future_counter"], 3)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--source", type=Path, action="append", default=[], help="self-contained source; repeat for multiple workloads")
    parser.add_argument("--workloads", type=Path, default=Path("build/selection-benchmark/sources"))
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--allocators", nargs="+", choices=ALLOCATORS, default=["fast"])
    parser.add_argument("--output", type=Path, default=Path("build/selection-benchmark/report.json"))
    parser.add_argument("--build-description", default="unspecified")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--check", action="store_true", help="also execute workloads and exercise failure paths")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(ReportTests)).wasSuccessful() else 1
    if not args.compiler or args.samples < 1 or args.timeout <= 0:
        parser.error("--compiler, positive --samples, and positive --timeout are required")
    try:
        if args.cpu is not None:
            if not hasattr(os, "sched_setaffinity"):
                raise RuntimeError("CPU affinity is unavailable on this host")
            os.sched_setaffinity(0, {args.cpu})
        report = benchmark(args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"selection-benchmark: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
