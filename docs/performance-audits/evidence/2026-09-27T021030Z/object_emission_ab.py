#!/usr/bin/env python3
"""A/B and interoperability evidence for the planned ELF64 object writer.

Subcommands (outputs are JSON under --output; nothing here is timed as proof):

  generate  adversarial C inputs: many symbols and relocations, long and
            duplicate names, many priority-group sections, alignment-heavy
            globals, an empty unit and a large instruction stream
  corpus    compile every input for each target/debug configuration with
            BASE and CANDIDATE; compare exit status, stderr and output SHA-256
  ledger    compile each input with -v under one or more compilers and record
            every OBJECT_WRITE counter line (the old writer is measured with
            the disposable reference probe build)
  tools     run independent readers, disassemblers and linkers over the
            CANDIDATE's ELF objects: GNU readelf, llvm-readelf, llvm-objdump,
            ld -r, ld.lld -r, and full GNU ld / LLD links of every x86-64
            input that defines main, whose exit status must match the
            compiler's own link

Exact counts decide; wall time is not recorded.
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

CONFIGURATIONS = [
    ("x86_64-unknown-linux-gnu", ["-g0"]),
    ("x86_64-unknown-linux-gnu", ["-g"]),
    ("x86_64-unknown-linux-gnu", ["-g0", "-fPIC"]),
    ("aarch64-unknown-linux-gnu", ["-g0"]),
    ("aarch64-unknown-linux-gnu", ["-g"]),
    ("x86_64-pc-windows-msvc", ["-g"]),
    ("aarch64-pc-windows-msvc", ["-g"]),
    ("x86_64-apple-macos", ["-g"]),
    ("arm64-apple-macos", ["-g"]),
]


def sha256(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def inputs(arguments):
    files = sorted(glob.glob(os.path.join(arguments.tests, "*.c")))
    for directory in arguments.extra or []:
        files += sorted(glob.glob(os.path.join(directory, "*.c")))
    return files


def configuration_name(target, flags):
    return target + "".join(flags)


def compile_one(binary, output_directory, label, path, target, flags, include, verbose=False):
    name = f"{label}-{os.path.basename(path)}-{configuration_name(target, flags)}.o"
    output = os.path.join(output_directory, name)
    if os.path.exists(output):
        os.remove(output)
    command = [binary, "cc", "-target", target] + flags + ["-I" + include, "-c", path, "-o", output]
    if verbose:
        command.insert(2, "-v")
    process = subprocess.run(command, capture_output=True, timeout=900)
    return dict(output=output, status=process.returncode, stdout=process.stdout.decode(errors="replace"),
                stderr=process.stderr.decode(errors="replace"), sha256=sha256(output))


def generate(arguments):
    os.makedirs(arguments.output, exist_ok=True)

    def write(name, text):
        with open(os.path.join(arguments.output, name), "w") as handle:
            handle.write(text)

    # Many symbols and relocations: externs called, statics defined, globals
    # addressed from a table, each with a long name.
    count = arguments.symbols
    long = "q" * 180
    lines = [f"extern int ext_{long}_{i}(int);" for i in range(count)]
    lines += [f"int global_{long}_{i} = {i};" for i in range(count)]
    lines += [f"static int local_{long}_{i}(int x) {{ return ext_{long}_{i}(x) + global_{long}_{i}; }}" for i in range(count)]
    lines.append("int* table[] = {" + ", ".join(f"&global_{long}_{i}" for i in range(count)) + "};")
    lines.append("int sum(int x) { int r = 0; " + " ".join(f"r += local_{long}_{i}(x);" for i in range(count)) + " return r; }")
    write("adversarial_symbols.c", "\n".join(lines) + "\n")

    # Duplicate string-table entries: function-scope statics sharing a name,
    # and one extern referenced from many sites.
    lines = ["extern int shared(int);"]
    for i in range(arguments.duplicates):
        lines.append(f"int dup_{i}(int x) {{ static int counter; counter += x; return shared(counter); }}")
    write("adversarial_duplicate_names.c", "\n".join(lines) + "\n")

    # Many small sections: one ELF section per distinct constructor priority.
    lines = ["int hits;"]
    for i in range(arguments.priorities):
        lines.append(f"__attribute__((constructor({101 + i}))) static void ctor_{i}(void) {{ hits += {i}; }}")
        lines.append(f"__attribute__((destructor({101 + i}))) static void dtor_{i}(void) {{ hits -= {i}; }}")
    lines.append("int main(void) { return hits == 0 ? 1 : 0; }")
    write("adversarial_priority_sections.c", "\n".join(lines) + "\n")

    # Alignment-heavy layout: alternating tiny and page-aligned objects in
    # every data kind, including thread-local and zero-fill storage.
    lines = []
    for i in range(64):
        lines.append(f"_Alignas({1 << (i % 13)}) char data_{i}[{1 + i % 5}] = {{{i}}};")
        lines.append(f"_Alignas({1 << (i % 13)}) static const char rodata_{i}[{1 + i % 3}] = {{{i}}};")
        lines.append(f"_Alignas({1 << (i % 13)}) char bss_{i}[{1 + i % 7}];")
        lines.append(f"_Alignas({1 << (i % 13)}) _Thread_local char tls_{i}[{1 + i % 2}] = {{{i}}};")
    lines.append("const char* use(int i) { return i ? rodata_" + "0" + " + data_1[0] + bss_2[0] + tls_3[0] : 0; }")
    lines.append("int main(void) { return use(1) == 0; }")
    write("adversarial_alignment.c", "\n".join(lines) + "\n")

    # The empty translation unit.
    write("adversarial_empty.c", "")

    # A large instruction stream with many branches and calls.
    lines = ["typedef unsigned long long u64;", "static u64 table[256];"]
    for index in range(arguments.functions):
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
    step = max(1, arguments.functions // 64)
    calls = "".join(f"r += f{index}(a + {index}u, b);" for index in range(0, arguments.functions, step))
    lines.append(f"u64 entry(u64 a, u64 b) {{ u64 r = 0; {calls} return r; }}")
    lines.append("int main(void) { return (int)(entry(3, 7) & 1u) * 0; }")
    write("adversarial_large.c", "\n".join(lines) + "\n")


def corpus(arguments):
    os.makedirs(arguments.output, exist_ok=True)
    files = inputs(arguments)
    work = [(path, target, flags) for path in files for target, flags in CONFIGURATIONS]
    summary = {configuration_name(t, f): dict(inputs=0, identical=0, produced=0, different=0) for t, f in CONFIGURATIONS}
    differences = []

    def job(item):
        path, target, flags = item
        base = compile_one(arguments.base, arguments.output, "base", path, target, flags, arguments.tests)
        candidate = compile_one(arguments.candidate, arguments.output, "cand", path, target, flags, arguments.tests)
        same = (base["status"] == candidate["status"] and base["stderr"] == candidate["stderr"] and base["sha256"] == candidate["sha256"])
        for record in (base, candidate):
            if os.path.exists(record["output"]) and not arguments.keep:
                os.remove(record["output"])
        return path, target, flags, base, candidate, same

    with concurrent.futures.ThreadPoolExecutor(arguments.jobs) as pool:
        for path, target, flags, base, candidate, same in pool.map(job, work):
            row = summary[configuration_name(target, flags)]
            row["inputs"] += 1
            row["identical"] += 1 if same else 0
            row["different"] += 0 if same else 1
            row["produced"] += 1 if candidate["sha256"] else 0
            if not same:
                differences.append(dict(input=path, configuration=configuration_name(target, flags), base=base["status"],
                                        candidate=candidate["status"], base_sha256=base["sha256"], candidate_sha256=candidate["sha256"]))
    result = dict(summary=summary, differences=differences, compiles=2 * len(work))
    with open(os.path.join(arguments.output, "corpus.json"), "w") as handle:
        json.dump(result, handle, indent=2)
    print(json.dumps(result["summary"], indent=2))
    print("differences:", len(differences))
    return 0 if not differences else 1


LEDGER_PATTERN = re.compile(r"^OBJECT_WRITE (.*)$", re.M)


def ledger(arguments):
    os.makedirs(arguments.output, exist_ok=True)
    rows = []
    for path in arguments.inputs:
        for flags in arguments.flags or ["-g0", "-g"]:
            for label, binary in zip(arguments.labels, arguments.compilers):
                split_flags = flags.split()
                record = compile_one(binary, arguments.output, label, path, arguments.target, split_flags, arguments.tests, verbose=True)
                match = LEDGER_PATTERN.search(record["stdout"] + record["stderr"])
                counters = {}
                if match:
                    for field in match.group(1).split():
                        key, _, value = field.partition("=")
                        counters[key] = int(value) if value.isdigit() else value
                rows.append(dict(input=os.path.basename(path), flags=flags, label=label, status=record["status"], sha256=record["sha256"],
                                 counters=counters))
                if not arguments.keep and os.path.exists(record["output"]):
                    os.remove(record["output"])
    with open(os.path.join(arguments.output, "ledger.json"), "w") as handle:
        json.dump(rows, handle, indent=2)
    for row in rows:
        counters = row["counters"]
        print(f"{row['input']:40} {row['flags']:10} {row['label']:10} status={row['status']} "
              f"rel={counters.get('relocation_visits')} sym={counters.get('symbol_visits')} sec={counters.get('section_visits')} "
              f"reserved={counters.get('image_reserved')} stored={counters.get('image_stored')} patched={counters.get('image_patched')} "
              f"zeroed={counters.get('image_zeroed')} scratch={counters.get('scratch')} retained={counters.get('retained')} "
              f"output={counters.get('output')} sha={str(row['sha256'])[:12]}")
    return 0


def run(command, cwd=None, timeout=300):
    process = subprocess.run(command, capture_output=True, cwd=cwd, timeout=timeout)
    return process.returncode, process.stdout.decode(errors="replace"), process.stderr.decode(errors="replace")


def llvm_tool(arguments, name):
    return os.path.join(arguments.llvm_bin, name) if arguments.llvm_bin else name


def tools(arguments):
    os.makedirs(arguments.output, exist_ok=True)
    files = inputs(arguments)
    report = dict(objects=0, readelf_clean=0, llvm_readelf_clean=0, objdump_clean=0, ld_relocatable=0, lld_relocatable=0,
                  runnable=0, gnu_ld_matches=0, lld_matches=0, failures=[])
    ld_warnings = {}

    def failure(kind, path, target, detail):
        report["failures"].append(dict(kind=kind, input=path, target=target, detail=detail[:400]))

    def job(item):
        path, target = item
        results = []
        record = compile_one(arguments.candidate, arguments.output, "tool", path, target, ["-g"], arguments.tests)
        if record["status"] != 0 or not record["sha256"]:
            return results
        obj = record["output"]
        results.append(("object", None))
        status, out, err = run(["readelf", "-W", "-a", obj])
        results.append(("readelf_clean", None) if status == 0 and not err.strip() else ("readelf", err or out[-400:]))
        status, out, err = run([llvm_tool(arguments, "llvm-readelf"), "--all", obj])
        results.append(("llvm_readelf_clean", None) if status == 0 and "warning" not in err.lower() and "error" not in err.lower() else ("llvm-readelf", err))
        status, out, err = run([llvm_tool(arguments, "llvm-objdump"), "-d", "-r", obj])
        results.append(("objdump_clean", None) if status == 0 and "error" not in err.lower() else ("llvm-objdump", err))
        # GNU ld is built for the host; x86-64 hosts usually carry no AArch64
        # emulation, so that row is skipped (LLD covers both machines).
        if target.startswith("x86_64") or arguments.gnu_ld_aarch64:
            emulation = "elf_x86_64" if target.startswith("x86_64") else "aarch64linux"
            status, out, err = run(["ld", "-m", emulation, "-r", obj, "-o", obj + ".ld-r.o"])
            results.append(("ld_relocatable", err.strip()) if status == 0 else ("ld -r", err))
        status, out, err = run([llvm_tool(arguments, "ld.lld"), "-r", obj, "-o", obj + ".lld-r.o"])
        results.append(("lld_relocatable", None) if status == 0 and not err.strip() else ("ld.lld -r", err))
        with open(path, errors="replace") as handle:
            has_main = re.search(r"\bint\s+main\s*\(", handle.read()) is not None
        if target.startswith("x86_64") and has_main:
            own = obj + ".own"
            status_own, _, _ = run([arguments.candidate, "cc", "-target", target, "-I" + arguments.tests, path, "-o", own, "-lm"])
            if status_own == 0:
                own_status, _, _ = run([own], cwd=arguments.output, timeout=60)
                results.append(("runnable", None))
                lld = ["--ld-path=" + llvm_tool(arguments, "ld.lld")] if arguments.llvm_bin else ["-fuse-ld=lld"]
                for kind, linker in (("gnu_ld_matches", ["-fuse-ld=bfd"]), ("lld_matches", lld)):
                    exe = obj + "." + kind
                    status, out, err = run(["clang"] + linker + ["-no-pie", obj, "-o", exe, "-lm"])
                    if status != 0:
                        results.append((kind + " link", err))
                        continue
                    run_status, _, _ = run([exe], cwd=arguments.output, timeout=60)
                    results.append((kind, None) if run_status == own_status else (kind + " exit", f"own={own_status} linked={run_status}"))
        for suffix in (".ld-r.o", ".lld-r.o", ".own", ".gnu_ld_matches", ".lld_matches"):
            if os.path.exists(obj + suffix):
                os.remove(obj + suffix)
        os.remove(obj)
        return results

    work = [(path, target) for path in files for target in ("x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu")]
    with concurrent.futures.ThreadPoolExecutor(arguments.jobs) as pool:
        for (path, target), results in zip(work, pool.map(job, work)):
            for kind, detail in results:
                if kind == "object":
                    report["objects"] += 1
                elif kind in report and isinstance(report[kind], int):
                    report[kind] += 1
                    if kind == "ld_relocatable" and detail:
                        for line in detail.splitlines():
                            key = re.sub(r"^.*?: ", "", re.sub(r"/[^ :]*\.o", "<obj>", line))
                            ld_warnings[key] = ld_warnings.get(key, 0) + 1
                else:
                    failure(kind, path, target, detail or "")
    report["ld_relocatable_warnings"] = ld_warnings
    with open(os.path.join(arguments.output, "tools.json"), "w") as handle:
        json.dump(report, handle, indent=2)
    summary = {key: value for key, value in report.items() if key != "failures"}
    print(json.dumps(summary, indent=2))
    print("failures:", len(report["failures"]))
    for item in report["failures"][:20]:
        print(item)
    return 0 if not report["failures"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("generate")
    command.add_argument("--output", required=True)
    command.add_argument("--symbols", type=int, default=4000)
    command.add_argument("--duplicates", type=int, default=2000)
    command.add_argument("--priorities", type=int, default=2000)
    command.add_argument("--functions", type=int, default=4000)
    command = commands.add_parser("corpus")
    command.add_argument("--base", required=True)
    command.add_argument("--candidate", required=True)
    command.add_argument("--output", required=True)
    command.add_argument("--tests", default="tests")
    command.add_argument("--extra", action="append")
    command.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    command.add_argument("--keep", action="store_true")
    command = commands.add_parser("ledger")
    command.add_argument("--compilers", nargs="+", required=True)
    command.add_argument("--labels", nargs="+", required=True)
    command.add_argument("--inputs", nargs="+", required=True)
    command.add_argument("--flags", action="append", help="one flag set per use, e.g. --flags=-g0 --flags=-g (default both)")
    command.add_argument("--target", default="x86_64-unknown-linux-gnu")
    command.add_argument("--tests", default="tests")
    command.add_argument("--output", required=True)
    command.add_argument("--keep", action="store_true")
    command = commands.add_parser("tools")
    command.add_argument("--candidate", required=True)
    command.add_argument("--output", required=True)
    command.add_argument("--tests", default="tests")
    command.add_argument("--extra", action="append")
    command.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    command.add_argument("--gnu-ld-aarch64", action="store_true", help="GNU ld supports the aarch64linux emulation")
    command.add_argument("--llvm-bin", help="directory holding llvm-readelf, llvm-objdump and ld.lld (default: PATH)")
    arguments = parser.parse_args()
    handlers = dict(generate=generate, corpus=corpus, ledger=ledger, tools=tools)
    return handlers[arguments.command](arguments) or 0


if __name__ == "__main__":
    sys.exit(main())
