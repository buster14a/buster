#!/usr/bin/env python3
"""Disposable source/correctness experiment, NOT a benchmark or conformance gate.

Run only in a disposable checkout on a standard GitHub-hosted Linux runner.
The native build driver owns construction. The instrumented compiler is never
published as a candidate. Baseline conformance failures are recorded, not hidden.
"""
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

BASE = "2e942e80a87666409cf29d3e24a68d322b9e71fd"
SOURCE_BLOB = "64533fb96983978e7cd3f481ce79491143ea515d"
ROOT = Path.cwd()
OUT = Path(os.environ["RUNNER_TEMP"]) / "pp-reuse"
OUT.mkdir(parents=True, exist_ok=True)
COMMANDS = []


def run(command, name, required=True, timeout=180):
    argv = [str(part) for part in command]
    COMMANDS.append(argv)
    result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=timeout, check=False)
    (OUT / (name + ".stdout")).write_bytes(result.stdout)
    (OUT / (name + ".stderr")).write_bytes(result.stderr)
    (OUT / "commands.json").write_text(json.dumps(COMMANDS, indent=2) + "\n")
    if required and result.returncode:
        raise RuntimeError(f"{name}: exit {result.returncode}\n" +
                           result.stdout[-6000:].decode(errors="replace") +
                           result.stderr[-6000:].decode(errors="replace"))
    return result


def tokens(data):
    # Restricted oracle for the explicit ASCII cases below, not a C lexer.
    text = re.sub(r"(?m)^\s*#.*$", "", data.decode())
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_][A-Za-z_0-9]*|[0-9][A-Za-z_0-9.]*|##|\.\.\.|<<=?|>>=?|->|\+\+|--|&&|\|\||[!=<>+*/%&|^\-]=|\S'
    return re.findall(pattern, text)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


source_path = ROOT / "src/buster/lib/compiler/frontend/c/c_source.c"
original = source_path.read_text()
blob = subprocess.check_output(["git", "hash-object", str(source_path)], text=True).strip()
assert blob == SOURCE_BLOB, (blob, SOURCE_BLOB)
(OUT / "source-identity.json").write_text(json.dumps({
    "base": BASE, "checkout": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
    "c_source_blob": blob, "performance": "UNMEASURED; hosted correctness/source census only"
}, indent=2) + "\n")
context_lines = []
for path in (ROOT / "src/buster/lib/compiler/frontend/c").glob("*.[ch]"):
    lines = path.read_text().splitlines()
    for index, line in enumerate(lines):
        if any(word in line for word in ("__COUNTER__", "__DATE__", "__TIME__", "__BASE_FILE__", "__INCLUDE_LEVEL__", "C_MACRO_BUILTIN_")):
            context_lines.append(f"{path.relative_to(ROOT)}:{index + 1}:" + "\n".join(lines[max(0, index - 2):index + 4]))
(OUT / "contextual-builtins.txt").write_text("\n\n".join(context_lines) + "\n")

# Follow the documented hosted Clang bootstrap exception; no privileged setup.
driver = OUT / "build-driver"
run(["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
     "build.c", "-o", driver], "bootstrap", timeout=300)
run([driver, "generate", "--build-directory", "build-pp-reuse", "--config", "Release",
     "--cc", "clang", "--ci", "--linker", "DEFAULT", "-DBUSTER_INCLUDE_TESTS=OFF"], "generate", timeout=300)
run([driver, "build", "--build-directory", "build-pp-reuse", "--config", "Release", "-t", "ide"], "build-base", timeout=1500)
ide = ROOT / "build-pp-reuse/Release/ide"
base = OUT / "ide-base"
shutil.copy2(ide, base)
for compiler in ("clang", "gcc"):
    run([compiler, "--version"], compiler + "-version")

inputs = OUT / "inputs"
inputs.mkdir(exist_ok=True)
cases = {
    "binding": ("#define A B\nA\n#define B 7\nA\n#undef B\n#define B 9\nA\n#undef B\nA\n#undef A\n#define A 12\nA\n", "B 7 9 B 12"),
    "suffix": ("#define A F\n#define F(x) x + 1\nA\nA(4)\n", "F 4 + 1"),
    "function-binding": ("#define A F\nA(3)\n#define F(x) x + 2\nA(3)\n#undef F\nA(3)\n", "F(3) 3 + 2 F(3)"),
    "disabled": ("#define X a.X\n#define ID(x) x\nID(ID(X)) X\n#define SELF SELF\nID(SELF) SELF\n", "a.X a.X SELF SELF"),
    "paste-lookup": ("#define CAT(a,b) a##b\nCAT(LA,TE)\n#define LATE 8\nCAT(LA,TE)\n#undef LATE\nCAT(LA,TE)\n", "LATE 8 LATE"),
    "stringize": ("#define RAW(x) #x\n#define N 41\nRAW(N) RAW(a  +b) RAW(a+b) RAW(__LINE__)\n", '"N" "a +b" "a+b" "__LINE__"'),
    "empty-variadic": ("#define CAT(a,b) a##b\nCAT(,x) CAT(x,) CAT(,)\n#define V(x,...) x , ## __VA_ARGS__\nV(1) V(1,2)\n#define VS(...) #__VA_ARGS__\nVS() VS(a,b)\n", 'x x 1 1,2 "" "a,b"'),
    "location": ('#define WHERE __LINE__ __FILE__\n#line 100 "first.c"\nWHERE\n#line 700 "second.c"\nWHERE\n', '100 "first.c" 700 "second.c"'),
    "argument-sharing": ('#define DUP(x) x x\n#define POINT __LINE__\n#line 50 "twice.c"\nDUP(POINT)\n#line 90 "twice.c"\nDUP(POINT)\n', '50 50 90 90'),
    "push-pop": ('#define X 1\n#pragma push_macro("X")\n#undef X\n#define X 2\nX\n#pragma pop_macro("X")\nX\n', '2 1'),
    "pragma-effects": ('#define SAVE _Pragma("push_macro(\\"X\\")")\n#define RESTORE _Pragma("pop_macro(\\"X\\")")\n#define X 1\nSAVE\n#undef X\n#define X 2\nX\nRESTORE\nX\n', '2 1'),
    "raw-only-prescan": ('#define BAD(x,y) x+y\n#define RAW(x) #x\nRAW(BAD(1))\n', '"BAD(1)"'),
    "unused-prescan": ('#define BAD(x,y) x+y\n#define UNUSED(x) 7\nUNUSED(BAD(1))\n', '7'),
    "paste-only-prescan": ('#define BAD(x,y) x+y\n#define PREFIX(x) prefix##x\nPREFIX(BAD(1))\n', 'prefixBAD(1)'),
}
(inputs / "once.h").write_text("#pragma once\nonce_token\n")
(inputs / "once-alias.h").symlink_to("once.h")
(inputs / "items.h").write_text("ITEM(a)\nITEM(b)\n")
(inputs / "items-alias.h").symlink_to("items.h")
(inputs / "guard.h").write_text("#ifndef GUARD\n#define GUARD\nguard_token\n#endif\n")
(inputs / "guard-alias.h").symlink_to("guard.h")
cases["includes"] = ('#include "once.h"\n#include "once-alias.h"\n#include "guard.h"\n#include "guard-alias.h"\n#undef GUARD\n#include "guard-alias.h"\n#define ITEM(x) x\n#include "items.h"\n#undef ITEM\n#define ITEM(x) #x\n#include "items-alias.h"\n', 'once_token guard_token guard_token a b "a" "b"')
observations = []
for name, (source, expected) in cases.items():
    path = inputs / (name + ".c")
    path.write_text(source)
    expected_tokens = tokens(expected.encode())
    for dialect in ("gnu17", "c17"):
        for label, command in (("buster", [base, "cc", "-E"]), ("clang", ["clang", "-E", "-P"]), ("gcc", ["gcc", "-E", "-P"])):
            result = run(command + ["-nostdinc", "-std=" + dialect, path], name + "-" + dialect + "-" + label, required=False)
            actual = tokens(result.stdout) if result.returncode == 0 else []
            observations.append({"case": name, "dialect": dialect, "compiler": label,
                                 "exit": result.returncode, "matches_expected_tokens": actual == expected_tokens and result.returncode == 0,
                                 "actual": actual, "expected": expected_tokens})
(OUT / "counterexamples.json").write_text(json.dumps(observations, indent=2) + "\n")
# Invalid-paste attribution is a separate observation, not discarded as a token failure.
invalid = inputs / "diagnostics.c"
invalid.write_text('#define BAD(a,b) a##b\n#line 37 "paste-a.c"\nBAD(x,+)\n#line 92 "paste-b.c"\nBAD(x,+)\n')
run([base, "cc", "-E", "-nostdinc", invalid], "diagnostic-provenance", required=False)
supported = inputs / "builtin-probe.c"
supported.write_text('__LINE__ __FILE__ __COUNTER__ __DATE__ __TIME__ __BASE_FILE__ __INCLUDE_LEVEL__\n')
run([base, "cc", "-E", "-nostdinc", supported], "builtin-probe", required=False)

# Freeze ACTUAL inputs before changing the compiler's source. Same configured
# build root, input paths, flags and output path for both compilers.
frozen = OUT / "frozen"
shutil.copytree(ROOT / "src", frozen / "src")
shutil.copytree(ROOT / "build-pp-reuse/generated", frozen / "generated")
workloads = {
    "self-host-object": ["-g", "-I" + str(frozen / "src"), "-I" + str(frozen / "generated"),
                         "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-c", frozen / "src/buster/apps/ide/ide.c"],
    "macro-regression-object": ["-g", "-c", ROOT / "tests/basic_c_macro_empty_paste.c"],
    "basic-operations-object": ["-g", "-c", ROOT / "tests/basic_c_operations.c"],
}
artifact = OUT / "output.o"
base_hashes = {}
for name, flags in workloads.items():
    run([base, "cc"] + flags + ["-o", artifact], name + "-base", timeout=300)
    base_hashes[name] = digest(artifact)

# Diagnostic trace ONLY: keep semantics, allocations and ordinary work. No
# candidate implementation, no timers, no cache, no callbacks or global state.
instrumented = "#include <stdio.h>\n" + original
start = instrumented.index(" c_macro_replacement_tokens(")
opening = instrumented.index("\n{", start) + 2
trace = '\n    fprintf(stderr, "PP_REUSE_CALL %.*s %u %u %u %u %u\\n", (int)macro->name.length, (char const*)macro->name.pointer, macro->definition.replacement_count, macro->definition.parameter_count, (unsigned)macro->definition.has_paste, (unsigned)macro->definition.has_stringify, (unsigned)macro->builtin);\n'
instrumented = instrumented[:opening] + trace + instrumented[opening:]
start = instrumented.index(" c_macro_continuation_advance(")
point = instrumented.index("        if (!requires_expansion)", start)
instrumented = instrumented[:point] + '        fprintf(stderr, "PP_REUSE_ARG %u %u\\n", argument->token_count, (unsigned)requires_expansion);\n' + instrumented[point:]
point = instrumented.index("TemporalArena paste_scratch =")
line = instrumented.rfind("\n", 0, point) + 1
instrumented = instrumented[:line] + '            fprintf(stderr, "PP_REUSE_PASTE %.*s\\n", (int)spelling.length, (char const*)spelling.pointer);\n' + instrumented[line:]
source_path.write_text(instrumented)
(OUT / "instrumentation.patch").write_bytes(subprocess.check_output(["git", "diff", "--", str(source_path)]))
run([driver, "build", "--build-directory", "build-pp-reuse", "--config", "Release", "-t", "ide"], "build-probe", timeout=1500)
populations = {}
for name, flags in workloads.items():
    result = run([ide, "cc"] + flags + ["-o", artifact], name + "-probe", timeout=300)
    calls = Counter()
    shapes = Counter()
    pastes = Counter()
    args = Counter()
    for line in result.stderr.decode().splitlines():
        if line.startswith("PP_REUSE_CALL "):
            fields = line.split()
            calls[fields[1]] += 1
            shapes[tuple(fields[2:])] += 1
        elif line.startswith("PP_REUSE_PASTE "):
            pastes[line[len("PP_REUSE_PASTE "):]] += 1
        elif line.startswith("PP_REUSE_ARG "):
            fields = line.split()
            args[(int(fields[1]), int(fields[2]))] += 1
    populations[name] = {
        "base_object_sha256": base_hashes[name], "probe_object_sha256": digest(artifact),
        "identical_object": base_hashes[name] == digest(artifact),
        "replacement_calls": sum(calls.values()), "distinct_macro_names": len(calls),
        "top_macro_names": calls.most_common(20),
        "shapes_replacement_parameter_paste_stringify_builtin": [[list(key), count] for key, count in shapes.most_common()],
        "paste_lex_calls": sum(pastes.values()), "distinct_paste_spellings": len(pastes),
        "repeated_paste_spellings": sum(max(0, count - 1) for count in pastes.values()),
        "top_paste_spellings": pastes.most_common(20),
        "argument_preparations": sum(args.values()),
        "raw_alias_preparations": sum(count for (_, expands), count in args.items() if not expands),
        "expanded_preparations": sum(count for (_, expands), count in args.items() if expands),
        "note": "Counts, NOT time or candidate cache hits. Macro-name counts do not imply identical definitions or expansion environments."
    }
# Confirm the repeated argument is expanded once per invocation, not per use.
run([ide, "cc", "-E", "-nostdinc", inputs / "argument-sharing.c"], "argument-sharing-probe")
(OUT / "population.json").write_text(json.dumps(populations, indent=2) + "\n")
(OUT / "binary-identities.json").write_text(json.dumps({"base": digest(base), "probe": digest(ide)}, indent=2) + "\n")
failures = [row for row in observations if not row["matches_expected_tokens"]]
print("PP_REUSE_OBSERVATIONS " + json.dumps({"comparisons": len(observations), "failures": failures}))
print("PP_REUSE_POPULATIONS " + json.dumps(populations))
print("PP_REUSE_DIAGNOSTICS " + (OUT / "diagnostic-provenance.stderr").read_text())
print("PP_REUSE_BUILTINS " + (OUT / "builtin-probe.stdout").read_text())
print("PP_REUSE_ARG_TRACE " + (OUT / "argument-sharing-probe.stderr").read_text())
assert all(row["identical_object"] for row in populations.values()), "instrumentation changed an artifact"
