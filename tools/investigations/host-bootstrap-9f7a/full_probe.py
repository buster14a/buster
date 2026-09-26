#!/usr/bin/env python3
"""Bounded hosted correctness experiment. Build policy remains in build.c.

No benchmarks, production edits, acceptance-policy changes or retries. The
fixture observer's exit 1 means an actual semantic mismatch, not a tool failure.
"""
import csv
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

PIN = "ade6ac4b6ecb21f30b61b656439bac476c145e2f"
TREE = "4c5306221fdb22fccc929b55e333163742de17d0"
FLAGS = ["-fwrapv", "-fno-strict-aliasing", "-funsigned-char"]
TARGET = ["-target", "x86_64-linux", "-march=baseline",
          "-fregister-allocator=fast", "-fno-machine-fallback", "-fcompile-jobs=1"]


def main():
    if len(sys.argv) != 3 or os.environ.get("GITHUB_ACTIONS") != "true":
        raise SystemExit("usage on authorized GitHub-hosted executor: full_probe.py PINNED_GIT_ROOT EVIDENCE")
    root, out = (Path(arg).resolve() for arg in sys.argv[1:])
    out.mkdir(parents=True, exist_ok=True)
    records = []

    def digest(path):
        with path.open("rb") as stream:
            return hashlib.file_digest(stream, "sha256").hexdigest()

    def run(name, args, cwd=root, allowed=(0,), timeout=600):
        args = [str(x) for x in args]
        print(name + ": " + shlex.join(args), flush=True)
        record = {"name": name, "argv": args, "cwd": str(cwd), "status": "unrun"}
        records.append(record)
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        with (out / (name + ".stdout")).open("wb") as stdout, (out / (name + ".stderr")).open("wb") as stderr:
            try:
                result = subprocess.run(args, cwd=cwd, stdout=stdout, stderr=stderr, timeout=timeout, check=False)
                record["status"] = result.returncode
            except subprocess.TimeoutExpired:
                record["status"] = "timeout"
                (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
                raise
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if result.returncode not in allowed:
            raise RuntimeError(f"{name}: exit {result.returncode}; see retained stdout/stderr")
        return result.returncode

    for key, value in [("HEAD", PIN), ("HEAD^{tree}", TREE)]:
        actual = subprocess.check_output(["git", "-C", str(root), "rev-parse", key], text=True).strip()
        if actual != value:
            raise RuntimeError("source identity mismatch: " + key)
    (out / "source.json").write_text(json.dumps({"commit": PIN, "tree": TREE,
        "experiment_commit": os.environ["GITHUB_SHA"], "run_id": os.environ["GITHUB_RUN_ID"],
        "run_attempt": os.environ["GITHUB_RUN_ATTEMPT"],
        "scope": "diagnostic host contraction pair plus two bootstrap generations; no benchmark",
        "unrun": ["GCC/Zig/MSVC-built full compilers", "AArch64/Windows/macOS",
                  "full sanitized Buster", "ordinary self-host benchmark", "stronger repeated three-generation audit",
                  "full test_all", "dedicated performance acceptance"]}, indent=2) + "\n")

    # Exact decimal midpoints; all expected values are integer deductions.
    rows = []
    for index in (719, 724):
        scaled = (2049 + 2 * index) * 48828125  # numerator * 5**11
        spelling = f"{scaled // 100000000000}.{scaled % 100000000000:011d}"
        expected = 0x3c00 + index + (index & 1)
        for padding in range(17):
            rows.append({"family": "decimal-half", "midpoint_index": index, "padding": padding,
                         "input": spelling + "0" * padding + "f16", "expected": expected})
        rows.append({"family": "double-control", "midpoint_index": index, "padding": 16,
                     "input": spelling + "0" * 16, "expected": expected})
        rows.append({"family": "negative-half", "midpoint_index": index, "padding": 16,
                     "input": "-" + spelling + "0" * 16 + "f16", "expected": expected | 0x8000})
    count = len(rows)
    (out / "frozen-rows.json").write_text(json.dumps(rows, indent=2) + "\n")
    initializers = ",\n".join(row["input"] for row in rows)
    local_stores = "\n".join(f"    output[{i}] = {row['input']};" for i, row in enumerate(rows))
    fixture = f"const _Float16 values[{count}] = {{\n{initializers}\n}};\nvoid local_values(_Float16 *output)\n{{\n{local_stores}\n}}\n"
    (out / "fixture.c").write_text(fixture)
    expected = ",".join(f"0x{row['expected']:04x}" for row in rows)
    observer = f'''#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern const _Float16 values[{count}];
extern void local_values(_Float16 *);
static const uint16_t expected[{count}] = {{{expected}}};
int main(void)
{{
    _Float16 local[{count}];
    local_values(local);
    unsigned mismatches = 0;
    puts("path,index,observed,expected");
    for (unsigned i = 0; i < {count}; ++i) {{
        uint16_t global_bits = 0, local_bits = 0;
        memcpy(&global_bits, values + i, 2);
        memcpy(&local_bits, local + i, 2);
        printf("global,%u,%04x,%04x\\n", i, (unsigned)global_bits, (unsigned)expected[i]);
        printf("local,%u,%04x,%04x\\n", i, (unsigned)local_bits, (unsigned)expected[i]);
        mismatches += global_bits != expected[i];
        mismatches += local_bits != expected[i];
    }}
    if (ferror(stdout)) return 2;
    return mismatches ? 1 : 0;
}}
'''
    (out / "observer.c").write_text(observer)
    (out / "frozen-input-hashes.json").write_text(json.dumps({name: digest(out / name)
        for name in ("frozen-rows.json", "fixture.c", "observer.c")}, indent=2) + "\n")

    run("host-clang-version", ["clang", "--version"])
    run("host-gcc-version", ["gcc", "--version"])
    run("host-cpu", ["lscpu"])
    run("host-uname", ["uname", "-a"])
    run("host-feature-macros", ["clang", "-march=native", "-dM", "-E", "-x", "c", "/dev/null"])
    # The production recipe is always native. A no-FMA host would not answer
    # this particular contraction hypothesis, rather than falsifying it.
    if "#define __FMA__ 1" not in (out / "host-feature-macros.stdout").read_text():
        raise RuntimeError("unavailable matrix leg: hosted CPU lacks native FMA")
    for compiler in ("clang", "gcc"):
        run(compiler + "-reference-object", [compiler, "-std=gnu11", "-O0", "-c", out / "fixture.c", "-o", out / (compiler + ".o")])
        run(compiler + "-reference-observer-link", ["clang", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
             out / "observer.c", out / (compiler + ".o"), "-o", out / (compiler + "-observer")])
        run(compiler + "-reference-observe", [out / (compiler + "-observer")])

    worktrees = {}
    compilers = {}
    for variant in ("default", "contract-off"):
        work = out.parent / ("host-bootstrap-work-" + variant)
        run(variant + "-worktree", ["git", "worktree", "add", "--detach", work, PIN])
        worktrees[variant] = work
        driver = work / "audit-build-driver"
        run(variant + "-driver-build", ["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function",
             "-Wno-unused-variable", "-g", *FLAGS, "build.c", "-o", driver], cwd=work)
        extra = ["-DCMAKE_C_FLAGS=-ffp-contract=off"] if variant == "contract-off" else []
        run(variant + "-configure", [driver, "generate", "--cc", "clang", "--ci", "--linker", "DEFAULT",
             "-DBUSTER_INCLUDE_TESTS=OFF", *extra], cwd=work)
        run(variant + "-build", [driver, "build", "--config", "Release", "-t", "ide"], cwd=work, timeout=900)
        ide = work / "build/Release/ide"
        if not ide.is_file():
            raise RuntimeError("build succeeded without expected compiler")
        compilers["S0-" + variant] = ide
        for source_name in ("compile_commands.json", "CMakeCache.txt"):
            shutil.copyfile(work / "build" / source_name, out / (variant + "-" + source_name))
        commands_text = (out / (variant + "-compile_commands.json")).read_text()
        for flag in FLAGS:
            if flag not in commands_text:
                raise RuntimeError("implementation flag missing: " + flag)
        shutil.copytree(work / "build/generated", out / (variant + "-generated-identity"))
        run(variant + "-pristine-source", ["git", "diff", "--exit-code"], cwd=work)

    work = worktrees["default"]
    for previous, name in (("S0-default", "S1"), ("S1", "S2")):
        compiler = work / "build" / ("audit-" + name)
        # Expanded documented self-host compile, deliberately without bench.
        # Child BUSTER_OPTIMIZE defaults to 0; record rather than equating it
        # with the Release host build. Both children get identical arguments.
        run(name + "-self-compile", [compilers[previous], "cc", "-Isrc", "-Ibuild/generated",
             "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-g", "-v", *FLAGS,
             "src/buster/apps/ide/ide.c", "-lm", "-o", compiler], cwd=work, timeout=600)
        compilers[name] = compiler
    fixed_point = compilers["S1"].read_bytes() == compilers["S2"].read_bytes()
    identities = {name: {"path": str(path), "sha256": digest(path), "size": path.stat().st_size}
                  for name, path in compilers.items()}
    (out / "compiler-identities.json").write_text(json.dumps(identities, indent=2) + "\n")
    (out / "fixed-point.json").write_text(json.dumps({"S1_equals_S2": fixed_point,
        "property_only": "byte fixed point; not a semantic proof or ordinary bench gate"}, indent=2) + "\n")
    observations = {}
    for name, compiler in compilers.items():
        target_object = out / (name + ".o")
        run(name + "-compile", [compiler, "cc", *TARGET, "-v", "-fbootstrap-trace=" + str(out / (name + "-trace")),
             "-c", out / "fixture.c", "-o", target_object], cwd=work)
        run(name + "-dump", ["objdump", "-s", target_object])
        run(name + "-observer-link", ["clang", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
             out / "observer.c", target_object, "-o", out / (name + "-observer")])
        status = run(name + "-observe", [out / (name + "-observer")], allowed=(0, 1))
        data = list(csv.DictReader((out / (name + "-observe.stdout")).open()))
        if len(data) != 2 * count or {(r["path"], int(r["index"])) for r in data} != {(p, i) for p in ("global", "local") for i in range(count)}:
            raise RuntimeError(name + ": incomplete observation")
        for row in data:
            if int(row["expected"], 16) != rows[int(row["index"])]["expected"]:
                raise RuntimeError(name + ": corrupt expected value")
        errors = sum(row["observed"] != row["expected"] for row in data)
        if status != bool(errors):
            raise RuntimeError(name + ": observer status inconsistent")
        observations[name] = data
    (out / "full-matrix.json").write_text(json.dumps(observations, indent=2) + "\n")
    default = [r for r in observations["S0-default"] if r["path"] == "global"]
    no_contract = [r for r in observations["S0-contract-off"] if r["path"] == "global"]
    differing = [i for i in range(count) if default[i]["observed"] != no_contract[i]["observed"]]
    if differing:
        minimum = min(differing, key=lambda i: (len(rows[i]["input"]), i))
        (out / "minimal.json").write_text(json.dumps({"index": minimum, **rows[minimum]}, indent=2) + "\n")
        (out / "minimal.c").write_text("const _Float16 witness = " + rows[minimum]["input"] + ";\n")
        (out / "minimal-observer.c").write_text('''#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern const _Float16 witness;
int main(void) { uint16_t bits = 0; memcpy(&bits, &witness, 2); printf("%04x\\n", (unsigned)bits); return ferror(stdout) ? 2 : 0; }
''')
        for name, compiler in compilers.items():
            obj = out / (name + "-minimal.o")
            run(name + "-minimal-compile", [compiler, "cc", *TARGET, "-fbootstrap-trace=" + str(out / (name + "-minimal-trace")),
                 "-c", out / "minimal.c", "-o", obj], cwd=work)
            run(name + "-minimal-dump", ["objdump", "-s", obj])
            run(name + "-minimal-link", ["clang", "-O0", out / "minimal-observer.c", obj, "-o", out / (name + "-minimal-observer")])
            run(name + "-minimal-observe", [out / (name + "-minimal-observer")])
    for variant, work in worktrees.items():
        run(variant + "-final-pristine-source", ["git", "diff", "--exit-code"], cwd=work)
    summary = {"source_commit": PIN, "source_tree": TREE, "S1_equals_S2": fixed_point,
               "observations_per_compiler": count * 2,
               "mismatches": {name: sum(r["observed"] != r["expected"] for r in data) for name, data in observations.items()},
               "S0_global_host_contraction_differences": len(differing),
               "verdict": "data collected; semantic failures remain failures; no repair or acceptance claim"}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)
    (out / "hashes.json").write_text(json.dumps({str(p.relative_to(out)): digest(p) for p in sorted(out.rglob("*")) if p.is_file()}, indent=2) + "\n")


if __name__ == "__main__":
    main()
