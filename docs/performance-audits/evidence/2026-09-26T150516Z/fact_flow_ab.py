#!/usr/bin/env python3
"""A/B evidence for audit 2026-09-26T150516Z.

Subcommands (all outputs are JSON under --output; nothing is timed as proof):

  generate  deterministic scaling family: N functions with loops, a switch,
            a global table read and a call chain (s<N>.c), plus the s1000 body
            with K unused static functions (h<K>.c)
  ab        alternate BASE and CANDIDATE on one command; record exit status,
            output SHA-256, exact minor page faults and peak RSS per run
  corpus    compile every tests/*.c for eight target/debug configurations with
            both compilers; compare exit status, stderr and output SHA-256
  census    read ir_construction.* work counters from -fsource-metrics of two
            BUSTER_BENCH_ALLOCATIONS=ON compilers over the scaling family
  callgrind run both profiling builds under callgrind and total named
            functions' inclusive instruction counts

Wall and CPU times are recorded for context only; the decision uses exact
counts (validation scans, rows, instruction counts, faults, bytes).
"""
import argparse
import concurrent.futures
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import time


def generate(arguments):
    os.makedirs(arguments.output, exist_ok=True)
    for count in arguments.sizes:
        lines = ["typedef unsigned long long u64;", "static u64 table[256];"]
        for index in range(count):
            call = f"f{index - 1}(s, t)" if index else "t"
            lines.append(f"""static u64 f{index}(u64 a, u64 b)
{{
    u64 s = a ^ {index}u;
    for (u64 k = 0; k < (b & 15u); k += 1)
    {{
        if ((s + k) & 1u) s = s * 3u + table[(k + {index}u) & 255u];
        else s = (s >> 1) ^ (a + k);
        switch ((s >> 3) & 3u) {{ case 0: s += 7u; break; case 1: s -= b; break; case 2: s ^= a; break; default: s = s + (s << 2); }}
    }}
    u64 t = a > b ? a - b : b - a;
    while (t > 3u) {{ t = (t & 1u) ? t * 3u + 1u : t >> 1; s += t; }}
    return s + {call};
}}""")
        step = max(1, count // 64)
        calls = "".join(f"r += f{index}(a + {index}u, b);" for index in range(0, count, step))
        lines.append(f"u64 entry(u64 a, u64 b) {{ u64 r = 0; {calls} return r; }}")
        with open(os.path.join(arguments.output, f"s{count}.c"), "w") as handle:
            handle.write("\n".join(lines) + "\n")
    # Amalgamation-style family: the s1000 body plus K unused static functions,
    # the shape of a translation unit whose configuration leaves helpers dead.
    base = open(os.path.join(arguments.output, "s1000.c")).read() if 1000 in arguments.sizes else None
    for helpers in arguments.helpers if base else []:
        extra = "".join(f"static u64 helper{index}(u64 a) {{ return a * {index + 3}u + (a >> 3); }}\n" for index in range(helpers))
        with open(os.path.join(arguments.output, f"h{helpers}.c"), "w") as handle:
            handle.write(base.replace("static u64 table[256];", "static u64 table[256];\n" + extra, 1))


def sha256(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest() if os.path.exists(path) else None


def run_once(binary, command, output):
    if os.path.exists(output):
        os.remove(output)
    start = time.time()
    pid = os.fork()
    if pid == 0:
        null = os.open(os.devnull, os.O_WRONLY)
        os.dup2(null, 1)
        os.dup2(null, 2)
        os.execv(binary, [binary] + [part.replace("{out}", output) for part in command])
    _, status, usage = os.wait4(pid, 0)
    return dict(status=status, wall=time.time() - start, user=usage.ru_utime, sys=usage.ru_stime,
                minflt=usage.ru_minflt, maxrss_kb=usage.ru_maxrss, sha256=sha256(output))


def ab(arguments):
    rows = []
    for repetition in range(arguments.repetitions):
        for label, binary in (("base", arguments.base), ("candidate", arguments.candidate)):
            output = os.path.join(arguments.output, f"{arguments.name}-{label}.out")
            row = run_once(binary, arguments.command, output)
            row.update(label=label, repetition=repetition)
            rows.append(row)
    summary = {}
    for label in ("base", "candidate"):
        selected = [row for row in rows if row["label"] == label]
        median = lambda key: sorted(row[key] for row in selected)[len(selected) // 2]
        summary[label] = dict(statuses=sorted({row["status"] for row in selected}), sha256=sorted({row["sha256"] for row in selected if row["sha256"]}),
                              minflt=[row["minflt"] for row in selected], median_minflt=median("minflt"), median_maxrss_kb=median("maxrss_kb"),
                              median_user=median("user"), median_sys=median("sys"), median_wall=median("wall"))
    summary["identical_output"] = summary["base"]["sha256"] == summary["candidate"]["sha256"] and len(summary["base"]["sha256"]) == 1
    summary["minflt_delta"] = summary["candidate"]["median_minflt"] - summary["base"]["median_minflt"]
    summary["maxrss_kb_delta"] = summary["candidate"]["median_maxrss_kb"] - summary["base"]["median_maxrss_kb"]
    record = dict(name=arguments.name, command=arguments.command, rows=rows, summary=summary)
    os.makedirs(arguments.output, exist_ok=True)
    with open(os.path.join(arguments.output, f"ab-{arguments.name}.json"), "w") as handle:
        json.dump(record, handle, indent=1)
    print(json.dumps(dict(name=arguments.name, **summary)))


CORPUS_CONFIGURATIONS = [
    ("x86_64-unknown-linux-gnu", "-g0"), ("x86_64-unknown-linux-gnu", "-g"),
    ("aarch64-unknown-linux-gnu", "-g0"), ("aarch64-unknown-linux-gnu", "-g"),
    ("x86_64-pc-windows-msvc", "-g"), ("x86_64-apple-macos", "-g"),
    ("wasm64-unknown-freestanding", "-g0"), ("bpfel-unknown-linux", "-g0"),
]


def corpus(arguments):
    os.makedirs(arguments.output, exist_ok=True)
    files = sorted(glob.glob(os.path.join(arguments.tests, "*.c")))
    work = [(path, target, debug) for path in files for target, debug in CORPUS_CONFIGURATIONS]

    def compile_one(binary, label, path, target, debug):
        output = os.path.join(arguments.output, f"{label}-{os.path.basename(path)}-{target}{debug}.out")
        if os.path.exists(output):
            os.remove(output)
        process = subprocess.run([binary, "cc", "-target", target, debug, "-I" + arguments.tests, "-c", path, "-o", output],
                                 capture_output=True, timeout=900)
        digest = sha256(output)
        if os.path.exists(output):
            os.remove(output)
        return process.returncode, digest, process.stderr.decode("latin-1").replace(output, "<out>")

    def job(item):
        return item, compile_one(arguments.base, "base", *item), compile_one(arguments.candidate, "candidate", *item)

    identical = produced = 0
    differences = []
    per_configuration = {f"{target}{debug}": dict(identical=0, produced=0, different=0) for target, debug in CORPUS_CONFIGURATIONS}
    with concurrent.futures.ThreadPoolExecutor(arguments.jobs) as executor:
        for item, base, candidate in executor.map(job, work):
            key = f"{item[1]}{item[2]}"
            if base == candidate:
                identical += 1
                produced += base[1] is not None
                per_configuration[key]["identical"] += 1
                per_configuration[key]["produced"] += base[1] is not None
            else:
                differences.append(dict(input=item, base=base[:2], candidate=candidate[:2]))
                per_configuration[key]["different"] += 1
    record = dict(compiles=len(work), files=len(files), identical=identical, produced=produced, different=len(differences),
                  per_configuration=per_configuration, differences=differences)
    with open(os.path.join(arguments.output, "corpus.json"), "w") as handle:
        json.dump(record, handle, indent=1)
    print(json.dumps({key: record[key] for key in ("compiles", "files", "identical", "produced", "different")}))


CENSUS_KEYS = ("preparation_calls", "preparation_input_validations", "preparation_fast_input_validations",
               "preparation_fast_output_validations", "validation_calls", "validation_functions", "validation_blocks",
               "validation_instructions", "validation_values", "validation_operand_ids", "validation_globals",
               "validation_global_relocation_pairs")


def census(arguments):
    os.makedirs(arguments.output, exist_ok=True)
    rows = []
    for size in arguments.sizes:
        for target in arguments.targets:
            for label, binary in (("base", arguments.base), ("candidate", arguments.candidate)):
                metrics = os.path.join(arguments.output, f"census-{label}-{size}-{target}.metrics")
                output = os.path.join(arguments.output, f"census-{label}-{size}-{target}.out")
                process = subprocess.run([binary, "cc", "-target", target, "-c", os.path.join(arguments.inputs, f"s{size}.c"), "-o", output,
                                          f"-fsource-metrics={metrics}"], capture_output=True, timeout=1800)
                values = {}
                for line in open(metrics):
                    match = re.match(r"ir_construction\.(\w+)=(\d+)", line.strip())
                    if match and match.group(1) in CENSUS_KEYS:
                        values[match.group(1)] = int(match.group(2))
                rows.append(dict(size=size, target=target, label=label, status=process.returncode, sha256=sha256(output), counters=values))
    with open(os.path.join(arguments.output, "census.json"), "w") as handle:
        json.dump(rows, handle, indent=1)
    for row in rows:
        print(json.dumps(row))


def callgrind(arguments):
    os.makedirs(arguments.output, exist_ok=True)
    results = {}
    for label, binary in (("base", arguments.base), ("candidate", arguments.candidate)):
        profile = os.path.join(arguments.output, f"callgrind-{arguments.name}-{label}.out")
        output = os.path.join(arguments.output, f"callgrind-{arguments.name}-{label}.bin")
        subprocess.run(["valgrind", "--tool=callgrind", f"--callgrind-out-file={profile}", binary]
                       + [part.replace("{out}", output) for part in arguments.command], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=arguments.cwd)
        annotated = subprocess.run(["callgrind_annotate", "--inclusive=yes", "--threshold=100", profile], capture_output=True, text=True).stdout
        totals = {}
        total = re.search(r"([\d,]+) \(100\.0%\)\s+PROGRAM TOTALS", annotated)
        totals["PROGRAM_TOTALS"] = int(total.group(1).replace(",", "")) if total else None
        for function in arguments.functions:
            match = re.search(r"^\s*([\d,]+) \([^)]*\)\s+\S*:" + re.escape(function) + r" \[", annotated, re.MULTILINE)
            totals[function] = int(match.group(1).replace(",", "")) if match else 0
        results[label] = dict(totals=totals, sha256=sha256(output))
    results["identical_output"] = results["base"]["sha256"] == results["candidate"]["sha256"]
    with open(os.path.join(arguments.output, f"callgrind-{arguments.name}.json"), "w") as handle:
        json.dump(dict(name=arguments.name, command=arguments.command, results=results), handle, indent=1)
    print(json.dumps(dict(name=arguments.name, **results)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="subcommand", required=True)
    generate_parser = commands.add_parser("generate")
    generate_parser.add_argument("--output", required=True)
    generate_parser.add_argument("--sizes", type=int, nargs="+", default=[250, 1000, 4000, 16000])
    generate_parser.add_argument("--helpers", type=int, nargs="+", default=[250, 1000, 4000])
    ab_parser = commands.add_parser("ab")
    for parser_with_pair in (ab_parser,):
        parser_with_pair.add_argument("--base", required=True)
        parser_with_pair.add_argument("--candidate", required=True)
    ab_parser.add_argument("--output", required=True)
    ab_parser.add_argument("--name", required=True)
    ab_parser.add_argument("--repetitions", type=int, default=3)
    ab_parser.add_argument("command", nargs=argparse.REMAINDER)
    corpus_parser = commands.add_parser("corpus")
    corpus_parser.add_argument("--base", required=True)
    corpus_parser.add_argument("--candidate", required=True)
    corpus_parser.add_argument("--output", required=True)
    corpus_parser.add_argument("--tests", default="tests")
    corpus_parser.add_argument("--jobs", type=int, default=4)
    census_parser = commands.add_parser("census")
    census_parser.add_argument("--base", required=True)
    census_parser.add_argument("--candidate", required=True)
    census_parser.add_argument("--output", required=True)
    census_parser.add_argument("--inputs", required=True)
    census_parser.add_argument("--sizes", type=int, nargs="+", default=[250, 1000, 4000, 16000])
    census_parser.add_argument("--targets", nargs="+", default=["wasm64-unknown-freestanding", "bpfel-unknown-linux", "x86_64-unknown-linux-gnu"])
    callgrind_parser = commands.add_parser("callgrind")
    callgrind_parser.add_argument("--base", required=True)
    callgrind_parser.add_argument("--candidate", required=True)
    callgrind_parser.add_argument("--output", required=True)
    callgrind_parser.add_argument("--name", required=True)
    callgrind_parser.add_argument("--cwd", default=".")
    callgrind_parser.add_argument("--functions", nargs="+", default=[])
    callgrind_parser.add_argument("command", nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    if getattr(arguments, "command", None) and arguments.command[:1] == ["--"]:
        arguments.command = arguments.command[1:]
    {"generate": generate, "ab": ab, "corpus": corpus, "census": census, "callgrind": callgrind}[arguments.subcommand](arguments)
    return 0


if __name__ == "__main__":
    sys.exit(main())
