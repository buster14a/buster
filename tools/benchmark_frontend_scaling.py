#!/usr/bin/env python3
"""Compare two trusted compilers on identical frontend scaling inputs.

Run from the repository root, with --baseline and --candidate ide executables.
Outputs remain under build/frontend-scaling unless --out overrides the path.
CPU and wall times are observations, never test thresholds. Object cases require
byte-identical output from both compilers. Linux/macOS resource accounting is
used so unrelated compiler workers do not inflate the child CPU measurements.
"""

import argparse
import hashlib
import json
from pathlib import Path
import resource
import statistics
import subprocess
import time


def pointer_source(count, order):
    if order == "range":
        source = f"int x;\nint *table[{count}] = {{[0 ... {count - 1}] = &x}};\n"
    else:
        indices = range(count - 1, -1, -1) if order == "descending" else range(count)
        entries = [f"[{index}] = &x," if order != "positional" else "&x," for index in indices]
        source = "int x;\nint *table[] = {\n" + "\n".join(entries) + "\n};\n"
    return source


def fresh_binding_source(count):
    """Each block declaration publishes a previously unbound file-scope name."""
    return "int main(void) {\n" + "".join(f"extern int fresh_{index}(void);\n" for index in range(count)) + "return 0;\n}\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=Path("build/frontend-scaling"))
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--suite", choices=["legacy", "bindings", "all"], default="legacy")
    parser.add_argument("--warmups", type=int, default=0)
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error("--repetitions must be positive")
    if args.warmups < 0:
        parser.error("--warmups must be nonnegative")
    args.out.mkdir(parents=True, exist_ok=True)
    compilers = {"baseline": args.baseline.resolve(), "candidate": args.candidate.resolve()}
    cases = [("small", "int f(int x) { return x * 3 + 1; }\n", 1, ["syntax", "object"])]
    if args.suite in ["legacy", "all"]:
        for count in [8192, 16384, 32768]:
            for order in ["positional", "ascending", "descending", "range"]:
                cases.append((order, pointer_source(count, order), count, ["syntax", "object"]))
        for count in [512, 1024, 2048, 4096, 8192]:
            source = "int f(int x) {\n" + "".join(f"for(int j=0;j<x;++j){{x+={index};}}\n" for index in range(count)) + "return x;\n}\n"
            cases.append(("scopes", source, count, ["syntax"]))
        for count in [16, 64, 256]:
            source = "int f(int x) {\n" + "for(int j=0;j<x;++j){\n" * count + "x+=1;\n" + "}\n" * count + "return x;\n}\n"
            cases.append(("deep-scopes", source, count, ["syntax"]))
    if args.suite in ["bindings", "all"]:
        for count in [0, 1, 4, 16, 64, 256, 1024, 4096, 16384, 65536]:
            cases.append(("fresh-bindings", fresh_binding_source(count), count, ["syntax", "object"]))
    binary_hashes = {label: hashlib.sha256(path.read_bytes()).hexdigest() for label, path in compilers.items()}
    (args.out / "provenance.json").write_text(json.dumps({
        "binary_sha256": binary_hashes, "suite": args.suite,
        "warmups": args.warmups, "repetitions": args.repetitions,
    }, indent=2) + "\n")
    records = []
    for kind, source, count, modes in cases:
        path = (args.out / f"{kind}-{count}.c").resolve()
        path.write_text(source)
        for mode in modes:
            record = {"kind": kind, "count": count, "mode": mode, "source_bytes": len(source.encode()),
                      "source_sha256": hashlib.sha256(source.encode()).hexdigest(), "compilers": {}}
            for label, compiler in compilers.items():
                output = args.out / f"{kind}-{count}-{label}.o"
                command = [str(compiler), "cc", "-g0"]
                if kind == "fresh-bindings":
                    command.append("-std=c17")
                command += ["-fsyntax-only"] if mode == "syntax" else ["-c", "-o", str(output)]
                command.append(str(path))
                record["compilers"][label] = {"command": command, "wall_seconds": [], "cpu_seconds": []}
            for warmup in range(args.warmups):
                for label in compilers:
                    subprocess.run(record["compilers"][label]["command"], capture_output=True, check=True, timeout=120)
            for repetition in range(args.repetitions):
                labels = list(compilers) if repetition % 2 == 0 else list(reversed(compilers))
                for label in labels:
                    samples = record["compilers"][label]
                    before = resource.getrusage(resource.RUSAGE_CHILDREN)
                    start = time.perf_counter()
                    subprocess.run(samples["command"], capture_output=True, check=True, timeout=120)
                    elapsed = time.perf_counter() - start
                    after = resource.getrusage(resource.RUSAGE_CHILDREN)
                    samples["wall_seconds"].append(elapsed)
                    samples["cpu_seconds"].append(after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime)
            if path.read_text() != source:
                raise SystemExit(f"source changed during measurement: {path}")
            if mode == "object":
                objects = [(args.out / f"{kind}-{count}-{label}.o").read_bytes() for label in compilers]
                if objects[0] != objects[1]:
                    raise SystemExit(f"object mismatch: {kind}-{count}")
                record["object_sha256"] = hashlib.sha256(objects[0]).hexdigest()
            for samples in record["compilers"].values():
                samples["cpu_median"] = statistics.median(samples["cpu_seconds"])
                samples["wall_median"] = statistics.median(samples["wall_seconds"])
            records.append(record)
            (args.out / "results.json").write_text(json.dumps(records, indent=2) + "\n")
            medians = {label: samples["cpu_median"] for label, samples in record["compilers"].items()}
            print(kind, count, mode, medians, flush=True)
    for label, compiler in compilers.items():
        if hashlib.sha256(compiler.read_bytes()).hexdigest() != binary_hashes[label]:
            raise SystemExit(f"compiler changed during measurement: {compiler}")


if __name__ == "__main__":
    main()
