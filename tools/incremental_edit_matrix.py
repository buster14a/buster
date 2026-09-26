#!/usr/bin/env python3
"""Edit matrix, randomized edit sequences and a real-code workload for the
opt-in function-granular code-generation cache (docs/incremental-compilation.md).

Every step compiles the edited unit with one compiler three times:

  clean        no cache: the correctness oracle;
  incremental  through the cache the previous step published;
  verify       through a copy of that same prior cache with
               -fincremental-verify, which compiles every hit again and
               compares the fresh artifact with the stored one.

A step passes only when all three objects are byte-identical, the runs agree
on exit status, stderr and every non-INCREMENTAL stdout record (-v included),
and verification reports no mismatch. For every function it records what the
cache did -- reused, or which record section changed -- and, by reading the
packs published before and after the step, whether the function's artifact
and its machine code actually changed. An invalidated function is necessary
when its code changed, a relabel when only the artifact's slots or metadata
changed, and an over-invalidation when the stored artifact was exactly right;
a reused function is correct by the oracle.

The tool writes a deterministic JSON report (no timestamps; wall times appear
only under "supplementary_wall_ms") and an optional Markdown summary. It never
edits the repository: real-code scenarios run on a private copy of src/.

Usage:
  incremental_edit_matrix.py --compiler build/Release/ide --work DIR
      [--matrix] [--random SEEDS:STEPS] [--real-code] [--self-host]
      [--audit] [--audit-targets LIST] [--targets LIST]
      [--json OUT] [--markdown OUT]
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import sys
import time

PACK_MAGIC = b"BUSTINC1"
PACK_HEADER = 64
PACK_ENTRY = 32
PACK_MANIFEST = 48
REUSED = "reused"
UNIT_NAME = "unit.c"


class HarnessError(Exception):
    pass


# ---------------------------------------------------------------------------
# Pack reader: mirrors incremental_pack_encode. Only the manifest and artifact
# bytes are needed here, to tell whether a function's artifact changed.

def read_pack(directory: Path) -> dict[str, dict]:
    packs = sorted(directory.glob("*.bpk")) if directory.exists() else []
    if len(packs) > 1:
        raise HarnessError(f"expected one pack in {directory}, found {len(packs)}")
    functions: dict[str, dict] = {}
    if packs:
        data = packs[0].read_bytes()
        if len(data) < PACK_HEADER or data[:8] != PACK_MAGIC:
            raise HarnessError(f"{packs[0]} is not a pack")
        size, = struct.unpack_from("<Q", data, 16)
        context_length, entry_count, manifest_count = struct.unpack_from("<III", data, 32)
        if size != len(data):
            raise HarnessError(f"{packs[0]} size mismatch")
        entry_table = PACK_HEADER + context_length
        manifest_table = entry_table + entry_count * PACK_ENTRY
        artifacts = []
        for index in range(entry_count):
            _, _, _, artifact_length, artifact_offset = struct.unpack_from("<QQIIQ", data, entry_table + index * PACK_ENTRY)
            artifacts.append(data[artifact_offset:artifact_offset + artifact_length])
        for index in range(manifest_count):
            name_offset, name_length, entry_index, fingerprint, body, types, symbols = struct.unpack_from(
                "<QIIQQQQ", data, manifest_table + index * PACK_MANIFEST)
            name = data[name_offset:name_offset + name_length].decode("utf-8", "replace")
            functions[name] = {
                "fingerprint": fingerprint,
                "sections": (body, types, symbols),
                "artifact": artifacts[entry_index] if entry_index != 0xFFFFFFFF else None,
            }
    return functions


def varint(data: bytes, cursor: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        byte = data[cursor]
        cursor += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, cursor


# The machine code an artifact carries: it opens with the magic and the code
# length (incremental_artifact_encode). Everything after it is relabeled
# metadata, so equal code with a different artifact is a relabel, not new code.
def artifact_code(artifact: bytes) -> bytes:
    _, cursor = varint(artifact, 0)
    length, cursor = varint(artifact, cursor)
    return artifact[cursor:cursor + length]


# ---------------------------------------------------------------------------
# Compiler runs

def parse_records(stdout: str) -> tuple[list[str], dict[str, dict], list[dict]]:
    ordinary = []
    records: dict[str, dict] = {}
    functions = []
    for line in stdout.splitlines():
        if line.startswith("INCREMENTAL_FUNCTION "):
            functions.append(dict(field.split("=", 1) for field in line.split()[1:]))
        elif line.startswith("INCREMENTAL"):
            kind, *fields = line.split()
            records[kind] = {key: int(value) if value.isdigit() else value for key, value in (field.split("=", 1) for field in fields)}
        else:
            ordinary.append(line)
    return ordinary, records, functions


class Compiler:
    def __init__(self, path: Path):
        self.path = path.resolve()

    def compile(self, directory: Path, flags: list[str], output: str, cache: Path | None = None, verify: bool = False,
                metrics: bool = False) -> dict:
        arguments = [str(self.path), "cc", "-v", "-c", UNIT_NAME, "-o", output] + flags
        if cache is not None:
            arguments += [f"-fincremental-cache={cache}", "-fincremental-stats", "-fincremental-trace"]
            if verify:
                arguments.append("-fincremental-verify")
        if metrics:
            arguments.append("-fsource-metrics=metrics.txt")
        start = time.perf_counter()
        process = subprocess.run(arguments, cwd=directory, capture_output=True, text=True, errors="replace")
        elapsed = (time.perf_counter() - start) * 1000.0
        ordinary, records, functions = parse_records(process.stdout)
        object_path = directory / output
        return {
            "returncode": process.returncode,
            "stdout": ordinary,
            "stderr": process.stderr,
            "records": records,
            "functions": functions,
            "object": object_path.read_bytes() if object_path.exists() else None,
            "wall_ms": elapsed,
        }


def source_metrics(directory: Path) -> dict[str, int]:
    path = directory / "metrics.txt"
    values = {}
    if path.exists():
        for line in path.read_text().splitlines():
            key, _, value = line.partition("=")
            if value.isdigit():
                values[key] = int(value)
    return values


# ---------------------------------------------------------------------------
# One step: clean oracle, incremental, verified incremental, ground truth.

LOOKUP_KEYS = ("reused", "miss-no-pack", "miss-context", "miss-new", "miss-body", "miss-types", "miss-symbols", "miss-uncaptured",
               "miss-malformed", "ineligible-inline-assembly", "ineligible-record")


def run_step(compiler: Compiler, directory: Path, cache: Path, source: str | None, flags: list[str], label: str,
             expect_failure: bool = False) -> dict:
    if source is not None:
        (directory / UNIT_NAME).write_text(source)
    previous = read_pack(cache)
    verify_cache = directory / "verify-cache"
    if verify_cache.exists():
        shutil.rmtree(verify_cache)
    if cache.exists():
        shutil.copytree(cache, verify_cache)
    clean = compiler.compile(directory, flags, "clean.o")
    incremental = compiler.compile(directory, flags, "incremental.o", cache=cache, metrics=True)
    verified = compiler.compile(directory, flags, "verified.o", cache=verify_cache, verify=True)
    current = read_pack(cache)
    failures = []
    for name, run in (("incremental", incremental), ("verify", verified)):
        if run["returncode"] != clean["returncode"]:
            failures.append(f"{name} exit {run['returncode']} != clean {clean['returncode']}")
        if run["object"] != clean["object"]:
            failures.append(f"{name} object differs from the clean object")
        if run["stderr"] != clean["stderr"]:
            failures.append(f"{name} stderr differs from the clean stderr")
        if run["stdout"] != clean["stdout"]:
            failures.append(f"{name} ordinary stdout records differ from the clean run")
    if expect_failure and clean["returncode"] == 0:
        failures.append("the clean compile was expected to fail")
    if not expect_failure and clean["returncode"] != 0:
        failures.append(f"the clean compile failed, so the step tests nothing: {clean['stderr'].strip()[:200]}")
    if expect_failure and read_pack(cache) != previous:
        failures.append("a failed compile replaced the pack")
    verify_record = verified["records"].get("INCREMENTAL_VERIFY", {})
    if verify_record.get("mismatches", 0):
        failures.append(f"verification found {verify_record['mismatches']} artifact mismatches")
    functions = []
    counts = {key: 0 for key in LOOKUP_KEYS}
    necessary = 0
    relabeled = 0
    over = 0
    for row in incremental["functions"]:
        name = row["name"]
        lookup = row["lookup"]
        counts[lookup] = counts.get(lookup, 0) + 1
        before = previous.get(name, {}).get("artifact")
        after = current.get(name, {}).get("artifact")
        changed = None if before is None or after is None else before != after
        code_changed = None if before is None or after is None else artifact_code(before) != artifact_code(after)
        # Necessary: the machine code differs from the stored artifact's (or
        # there was none). Relabel: same code, but symbol slots, offsets or
        # other metadata moved. Over: the stored artifact was exactly right.
        if lookup != REUSED and not lookup.startswith("ineligible"):
            if changed is False:
                over += 1
            elif code_changed is False:
                relabeled += 1
            elif code_changed is True or before is None:
                necessary += 1
        if lookup != REUSED:
            functions.append({"name": name, "lookup": lookup, "capture": row["capture"], "artifact_changed": changed,
                              "code_changed": code_changed, "instructions": int(row["instructions"])})
    work = incremental["records"].get("INCREMENTAL_WORK", {})
    metrics = source_metrics(directory)
    return {
        "label": label,
        "passed": not failures,
        "failures": failures,
        "functions_total": len(incremental["functions"]),
        "lookups": {key: value for key, value in counts.items() if value},
        "invalidated": sorted(functions, key=lambda item: item["name"]),
        "necessary_invalidations": necessary,
        "relabel_invalidations": relabeled,
        "over_invalidations": over,
        "verified_hits": verify_record.get("verified", 0),
        "work": {
            "lowered_ir_instructions": work.get("lowered_ir_instructions", 0),
            "reused_ir_instructions": work.get("reused_ir_instructions", 0),
            "compiled_ir_instructions": work.get("compiled_ir_instructions", 0),
            "reused_code_bytes": work.get("reused_code_bytes", 0),
            "reused_relocations": work.get("reused_relocations", 0),
            "reused_line_marks": work.get("reused_line_marks", 0),
            "reused_debug_locations": work.get("reused_debug_locations", 0),
            # The frontend always runs: every lexed byte is reparsed.
            "bytes_reparsed": metrics.get("lexed.bytes", 0),
            "preprocessed_tokens": metrics.get("preprocessed.tokens", 0),
        },
        "pack": incremental["records"].get("INCREMENTAL", {}),
        "record": incremental["records"].get("INCREMENTAL_RECORD", {}),
        "supplementary_wall_ms": {"clean": round(clean["wall_ms"], 1), "incremental": round(incremental["wall_ms"], 1),
                                  "incremental_time": incremental["records"].get("INCREMENTAL_TIME", {})},
    }


# ---------------------------------------------------------------------------
# The named edit matrix over a freestanding corpus (no headers, so every
# target in --targets compiles it without a sysroot).

BASE = r'''/* Incremental edit-matrix corpus: freestanding, no headers. */
#define SCALE 3
#define SQUARE(v) ((v) * (v))
typedef unsigned long size_type;
struct vec { int x; int y; };
struct buf { char* data; size_type size; size_type capacity; };
struct pair { char tag; int value; };
extern int external_counter(int value);
int shared_total;
static const int table[4] = {1, 2, 3, 4};
static const char* names[3] = {"alpha", "beta", "gamma"};
_Thread_local int thread_counter;

static int leaf_add(int a, int b)
{
    return a + b;
}

int arith(int a, int b)
{
    int t = a * SCALE;
    return leaf_add(t, b) - 7;
}

int loop_sum(int n)
{
    int total = 0;
    for (int i = 0; i < n; i += 1)
    {
        total += table[i & 3];
    }
    return total;
}

int vec_dot(struct vec a, struct vec b)
{
    return a.x * b.x + a.y * b.y;
}

int vec_len2(struct vec* v)
{
    return SQUARE(v->x) + SQUARE(v->y);
}

size_type buf_remaining(struct buf const* b)
{
    return b->capacity - b->size;
}

int pair_value(struct pair const* p)
{
    return p->value + p->tag;
}

const char* name_of(int index)
{
    return names[index % 3];
}

int use_external(int v)
{
    return external_counter(v) + shared_total;
}

int bump_thread(void)
{
    thread_counter += 1;
    return thread_counter;
}

int classify(int v)
{
    switch (v)
    {
        case 0: return 10;
        case 1: return 20;
        case 2: return 30;
        case 7: return 70;
        default: return -1;
    }
}

double mix(double a, float b)
{
    return a * 0.5 + (double)b;
}

int dispatch(int op)
{
    static void* labels[] = {&&first, &&second};
    goto *labels[op & 1];
first:
    return 1;
second:
    return 2;
}

long fib(long n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int apply(int (*fn)(int, int), int a, int b)
{
    return fn(a, b);
}

int uses_apply(int a)
{
    return apply(leaf_add, a, 5);
}

int string_len(const char* s)
{
    int n = 0;
    while (s[n])
    {
        n += 1;
    }
    return n;
}

int greet_len(void)
{
    return string_len("hello, incremental world");
}

int main(void)
{
    struct vec a = {1, 2};
    struct vec b = {3, 4};
    return arith(1, 2) + loop_sum(5) + vec_dot(a, b) + classify(2) + (int)fib(5);
}
'''


def edit(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise HarnessError(f"edit anchor must occur once: {old!r}")
    return source.replace(old, new)


def move_function(source: str, name: str, after: str) -> str:
    pattern = re.compile(r"(?ms)^[^\n]*\b" + re.escape(name) + r"\([^\n]*\)\n\{.*?^\}\n\n")
    match = pattern.search(source)
    if not match:
        raise HarnessError(f"no definition of {name}")
    text = match.group(0)
    source = source[:match.start()] + source[match.end():]
    anchor = re.compile(r"(?ms)^[^\n]*\b" + re.escape(after) + r"\([^\n]*\)\n\{.*?^\}\n\n").search(source)
    if not anchor:
        raise HarnessError(f"no definition of {after}")
    return source[:anchor.end()] + text + source[anchor.end():]


# Each scenario: (name, description, expected, transform). The transform
# returns (source, extra_flags, alternate_compiler) for the edited step.
# `expected` states the reuse the dependency model predicts; the harness
# reports it beside what happened and fails only on oracle violations.
def scenarios(target_is_sysv_x86: bool):
    rows = [
        ("function_body", "constant changed inside arith", "arith only",
         lambda s: (edit(s, "return leaf_add(t, b) - 7;", "return leaf_add(t, b) - 8;"), [], False)),
        ("local_declaration", "new local variable used in loop_sum", "loop_sum only",
         lambda s: (edit(s, "    int total = 0;\n", "    int total = 0;\n    int bias = 2;\n").replace(
             "total += table[i & 3];", "total += table[i & 3] + bias;"), [], False)),
        ("local_rename", "local renamed in loop_sum (debug names only)", "none",
         lambda s: (s.replace("total", "accumulator"), [], False)),
        ("line_shift", "comment lines inserted before every function", "none",
         lambda s: ("/* one */\n/* two */\n/* three */\n" + s, [], False)),
        ("public_type", "field appended to struct vec", "users of struct vec",
         lambda s: (edit(s, "struct vec { int x; int y; };", "struct vec { int x; int y; int z; };"), [], False)),
        ("macro", "SCALE redefined", "arith only",
         lambda s: (edit(s, "#define SCALE 3", "#define SCALE 4"), [], False)),
        ("abi_signature", "string_len returns long instead of int", "string_len and its callers",
         lambda s: (edit(s, "int string_len(const char* s)\n{", "long string_len(const char* s)\n{"), [], False)),
        ("target", "CPU model changed", "all (context)",
         lambda s: (s, ["-march=baseline"], False)),
        ("compiler", "compiler image changed", "all (context)",
         lambda s: (s, [], True)),
        ("debug", "debug information disabled", "all (context)",
         lambda s: (s, ["-g0"], False)),
        ("declaration_order", "vec_dot moved after mix, globals reordered", "none",
         lambda s: (move_function(s, "vec_dot", "mix").replace(
             "int shared_total;\nstatic const int table[4] = {1, 2, 3, 4};",
             "static const int table[4] = {1, 2, 3, 4};\nint shared_total;"), [], False)),
        ("unrelated_addition", "new function with a string literal and a new type added first", "the new function only",
         lambda s: (edit(s, "static int leaf_add(int a, int b)",
                         "struct extra { long a; long b; };\nlong extra_sum(struct extra const* e)\n{\n    return e->a + e->b + string_len(\"added\");\n}\n\n"
                         "int string_len(const char* s);\n\nstatic int leaf_add(int a, int b)"), [], False)),
        ("global_initializer", "table and names initializers changed", "none",
         lambda s: (edit(s, "{1, 2, 3, 4}", "{5, 6, 7, 8}").replace('"alpha"', '"omega"'), [], False)),
        ("linkage", "leaf_add loses static", "callers of leaf_add (symbol attributes)",
         lambda s: (edit(s, "static int leaf_add(int a, int b)", "int leaf_add(int a, int b)"), [], False)),
        ("definition", "shared_total becomes an external declaration", "use_external (symbol attributes)",
         lambda s: (edit(s, "int shared_total;", "extern int shared_total;"), [], False)),
        ("pragma_pack", "struct pair packed", "pair_value",
         lambda s: (edit(s, "struct pair { char tag; int value; };",
                         "#pragma pack(push, 1)\nstruct pair { char tag; int value; };\n#pragma pack(pop)"), [], False)),
        ("inline_assembly", "a function with inline assembly added", "the new function (never reused)",
         lambda s: (s + "\nint fence_now(void)\n{\n    __asm__ volatile(\"\" ::: \"memory\");\n    return 0;\n}\n", [], False)),
        ("label_table_neighbor", "classify edited beside the label-address table", "classify only",
         lambda s: (edit(s, "case 7: return 70;", "case 7: return 71;"), [], False)),
    ]
    if target_is_sysv_x86:
        rows.append(("abi_option", "System V unnamed bit-field policy changed", "all (context)",
                     lambda s: (s, ["-fsysv-unnamed-bitfields=integer"], False)))
    return rows


def alternate_compiler(compiler: Compiler, work: Path) -> Compiler:
    # A runnable copy whose image bytes differ from the original, so its
    # identity must differ and every record must miss. Elsewhere one appended
    # byte does; Apple targets require a valid signature, so the copy is
    # signed again ad hoc under another identifier instead.
    path = work / ("alternate-" + compiler.path.name)
    if not path.exists():
        shutil.copy2(compiler.path, path)
        if sys.platform == "darwin":
            subprocess.run(["codesign", "--force", "--sign", "-", "--identifier", "buster.incremental.alternate", str(path)], check=True,
                           capture_output=True)
        else:
            with open(path, "ab") as handle:
                handle.write(b"\0")
        if path.read_bytes() == compiler.path.read_bytes():
            raise HarnessError("the alternate compiler image is identical to the original")
    return Compiler(path)


def run_matrix(compiler: Compiler, work: Path, targets: list[str]) -> list[dict]:
    results = []
    for target in targets:
        target_flags = ["-target", target] if target != "host" else []
        # Diagnostics: a warning is reported identically, and a failed compile
        # neither publishes nor disturbs the pack the next compile reuses.
        directory = work / "matrix" / target / "diagnostics"
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
        cache = directory / "cache"
        warned = edit(BASE, "#define SCALE 3", "#define SCALE 3\n#warning \"incremental warning\"")
        base = run_step(compiler, directory, cache, warned, target_flags, "warning")
        broken = run_step(compiler, directory, cache, edit(warned, "return a + b;", "return a + ;"), target_flags, "syntax error", expect_failure=True)
        fixed = run_step(compiler, directory, cache, warned, target_flags, "error fixed")
        results.append({"target": target, "scenario": "diagnostics_warning", "description": "#warning added", "expected": "all (pack absent)",
                        "base": base, "edit": base})
        results.append({"target": target, "scenario": "diagnostics_error", "description": "syntax error: both compiles fail identically",
                        "expected": "no publication", "base": base, "edit": broken})
        results.append({"target": target, "scenario": "diagnostics_recovery", "description": "error fixed: the pre-error pack is reused",
                        "expected": "none", "base": broken, "edit": fixed})
        sysv_x86 = target.startswith("x86_64") and "windows" not in target and "apple" not in target and "darwin" not in target \
            and "macos" not in target
        for name, description, expected, transform in scenarios(sysv_x86 or (target == "host" and os.uname().machine in ("x86_64", "AMD64"))):
            directory = work / "matrix" / target / name
            if directory.exists():
                shutil.rmtree(directory)
            directory.mkdir(parents=True)
            cache = directory / "cache"
            base = run_step(compiler, directory, cache, BASE, target_flags, "base")
            source, extra_flags, use_alternate = transform(BASE)
            step_compiler = alternate_compiler(compiler, work) if use_alternate else compiler
            changed = run_step(step_compiler, directory, cache, source, target_flags + extra_flags, "edit")
            results.append({"target": target, "scenario": name, "description": description, "expected": expected,
                            "base": base, "edit": changed})
    return results


# ---------------------------------------------------------------------------
# Randomized edit sequences over a small program model. Every mutation keeps
# the program valid C; the oracle checks every step.

class Program:
    def __init__(self, rng: random.Random):
        self.rng = rng
        self.counter = 0
        self.structs = [["a", "b"], ["x", "y", "z"]]
        self.macros = [rng.randint(1, 9) for _ in range(3)]
        self.globals = [rng.randint(0, 99) for _ in range(4)]
        self.comments = {}
        self.functions = []
        self.helper_static = [True, True]
        for _ in range(8):
            self.add_function()

    def fresh(self, stem: str) -> str:
        self.counter += 1
        return f"{stem}_{self.counter}"

    def add_function(self, position: int | None = None):
        kind = self.rng.choice(["arith", "loop", "struct", "string", "switch", "call"])
        function = {"name": self.fresh(kind), "kind": kind, "constant": self.rng.randint(1, 50), "local": self.fresh("v"),
                    "struct": self.rng.randrange(len(self.structs)), "macro": self.rng.randrange(len(self.macros))}
        if position is None:
            self.functions.append(function)
        else:
            self.functions.insert(position, function)

    def render_function(self, f: dict) -> str:
        c, v, m = f["constant"], f["local"], f"K{f['macro']}"
        s = f["struct"]
        fields = self.structs[s]
        if f["kind"] == "arith":
            body = f"    int {v} = a * {m} + {c};\n    return helper_0({v}, b) ^ {c};"
        elif f["kind"] == "loop":
            body = f"    int {v} = 0;\n    for (int i = 0; i < a; i += 1)\n    {{\n        {v} += global_table[i & 3] + {c};\n    }}\n    return {v} + b;"
        elif f["kind"] == "struct":
            body = f"    struct s{s} {v} = {{0}};\n    {v}.{fields[0]} = a + {c};\n    {v}.{fields[-1]} = b;\n    return (int)({v}.{fields[0]} + {v}.{fields[-1]});"
        elif f["kind"] == "string":
            body = f"    const char* {v} = \"literal {c}\";\n    return (int){v}[a & 3] + b;"
        elif f["kind"] == "switch":
            body = f"    switch (a)\n    {{\n        case 0: return {c};\n        case 1: return b + {c};\n        case 5: return {m};\n        default: return -{c};\n    }}"
        else:
            body = f"    int {v} = helper_1(a, {c});\n    return helper_0({v}, b);"
        comment = "".join(f"/* note {n} */\n" for n in range(self.comments.get(f["name"], 0)))
        return f"{comment}int {f['name']}(int a, int b)\n{{\n{body}\n}}\n"

    def render(self) -> str:
        lines = ["/* randomized incremental sequence */"]
        lines += [f"#define K{index} {value}" for index, value in enumerate(self.macros)]
        for index, fields in enumerate(self.structs):
            lines.append(f"struct s{index} {{ " + " ".join(f"long {field};" for field in fields) + " };")
        lines.append("static const int global_table[4] = {" + ", ".join(str(value) for value in self.globals) + "};")
        for index in range(2):
            linkage = "static " if self.helper_static[index] else ""
            operation = "+" if index == 0 else "*"
            lines.append(f"{linkage}int helper_{index}(int a, int b)\n{{\n    return a {operation} b;\n}}\n")
        lines += [self.render_function(function) for function in self.functions]
        return "\n".join(lines) + "\n"

    def mutate(self) -> str:
        choice = self.rng.choice(["constant", "add", "remove", "swap", "field", "global", "rename", "comment", "macro", "linkage"])
        if choice == "constant" and self.functions:
            f = self.rng.choice(self.functions)
            f["constant"] = self.rng.randint(1, 50)
            return f"constant in {f['name']}"
        if choice == "add":
            self.add_function(self.rng.randrange(len(self.functions) + 1))
            return "function added"
        if choice == "remove" and len(self.functions) > 2:
            f = self.functions.pop(self.rng.randrange(len(self.functions)))
            return f"{f['name']} removed"
        if choice == "swap" and len(self.functions) > 1:
            i, j = self.rng.sample(range(len(self.functions)), 2)
            self.functions[i], self.functions[j] = self.functions[j], self.functions[i]
            return "two definitions swapped"
        if choice == "field":
            s = self.rng.randrange(len(self.structs))
            self.structs[s].insert(self.rng.randrange(len(self.structs[s]) + 1), self.fresh("f"))
            return f"field added to s{s}"
        if choice == "global":
            self.globals[self.rng.randrange(4)] = self.rng.randint(0, 99)
            return "global initializer changed"
        if choice == "rename" and self.functions:
            f = self.rng.choice(self.functions)
            f["local"] = self.fresh("v")
            return f"local renamed in {f['name']}"
        if choice == "comment" and self.functions:
            f = self.rng.choice(self.functions)
            self.comments[f["name"]] = self.comments.get(f["name"], 0) + self.rng.randint(1, 3)
            return f"comment lines before {f['name']}"
        if choice == "macro":
            index = self.rng.randrange(len(self.macros))
            self.macros[index] = self.rng.randint(1, 9)
            return f"K{index} redefined"
        if choice == "linkage":
            index = self.rng.randrange(2)
            self.helper_static[index] = not self.helper_static[index]
            return f"helper_{index} linkage toggled"
        return "no edit"


def run_random(compiler: Compiler, work: Path, seeds: int, steps: int, targets: list[str]) -> list[dict]:
    results = []
    for target in targets:
        target_flags = ["-target", target] if target != "host" else []
        for seed in range(seeds):
            rng = random.Random(seed)
            program = Program(rng)
            directory = work / "random" / target / f"seed-{seed}"
            if directory.exists():
                shutil.rmtree(directory)
            directory.mkdir(parents=True)
            cache = directory / "cache"
            sequence = [run_step(compiler, directory, cache, program.render(), target_flags, "base")]
            for step in range(steps):
                description = program.mutate()
                sequence.append(run_step(compiler, directory, cache, program.render(), target_flags, description))
            results.append({"target": target, "seed": seed, "steps": sequence})
    return results


# ---------------------------------------------------------------------------
# Real code: the self-host unity translation unit, on a private copy of src/.

REAL_EDITS = [
    ("leaf_body", "statement added to the body of string_equal", "src/buster/lib/string.c",
     r"(?m)^bool string_equal\(String8 s1, String8 s2\)\n\{\n", "bool string_equal(String8 s1, String8 s2)\n{\n    volatile int incremental_probe = 1;\n    (void)incremental_probe;\n"),
    ("new_function", "a new function appended to arena.c", "src/buster/lib/arena.c",
     r"\Z", "\nu64 incremental_probe_function(u64 value);\nu64 incremental_probe_function(u64 value)\n{\n    return value * 3 + 1;\n}\n"),
    ("line_shift", "comment lines inserted at the top of base.h", "src/buster/lib/base.h",
     r"\A", "/* incremental probe */\n/* incremental probe */\n"),
    ("header_struct", "field appended to ByteWriter", "src/buster/lib/byte_writer.h",
     r"    bool overflow;\n    u8 reserved\[7\];\n", "    bool overflow;\n    u8 reserved[7];\n    u64 incremental_probe;\n"),
]


def run_real_code(compiler: Compiler, repository: Path, work: Path, generated: Path) -> list[dict]:
    directory = work / "real"
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    shutil.copytree(repository / "src", directory / "src")
    shutil.copytree(generated, directory / "generated")
    flags = ["-Isrc", "-Igenerated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", *apple_sysroot()]
    unit = directory / UNIT_NAME
    unit.write_text('#include "src/buster/apps/ide/ide.c"\n')
    cache = directory / "cache"
    results = [run_step(compiler, directory, cache, None, flags, "base")]
    for name, description, relative, pattern, replacement in REAL_EDITS:
        path = directory / relative
        text = path.read_text()
        edited, count = re.subn(pattern, lambda _: replacement, text, count=1)
        if count != 1:
            raise HarnessError(f"real-code edit {name} found no anchor in {relative}")
        path.write_text(edited)
        step = run_step(compiler, directory, cache, None, flags, name)
        step["description"] = description
        results.append(step)
    return results


# ---------------------------------------------------------------------------
# Self-hosting through the cache: stage 1 (the given compiler) builds stage 2
# clean and through a cold then warm cache; stage 2 does the same for stage 3.
# Every executable must be byte-identical, which is the fixed point reached
# through the cache.

def run_self_host(compiler: Compiler, repository: Path, work: Path, generated: Path) -> dict:
    directory = work / "self-host"
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    flags = self_host_flags(repository, generated, link=True) + [str(repository / "src/buster/apps/ide/ide.c"), "-lm"]
    results = {"stages": []}
    current = compiler
    executables = []
    for stage in (2, 3):
        cache = directory / f"cache-stage{stage}"
        outputs = {}
        for kind, extra in (("clean", []), ("cold", [f"-fincremental-cache={cache}", "-fincremental-stats"]),
                            ("warm", [f"-fincremental-cache={cache}", "-fincremental-stats"])):
            output = directory / f"ide-stage{stage}-{kind}"
            start = time.perf_counter()
            process = subprocess.run([str(current.path), "cc", *flags, "-o", str(output), *extra], capture_output=True, text=True, errors="replace")
            elapsed = (time.perf_counter() - start) * 1000.0
            _, records, _ = parse_records(process.stdout)
            outputs[kind] = {"returncode": process.returncode, "bytes": output.read_bytes() if output.exists() else None,
                             "lookups": records.get("INCREMENTAL_LOOKUP", {}), "wall_ms": round(elapsed, 1),
                             "stderr": process.stderr[-2000:]}
        identical = all(outputs[kind]["bytes"] == outputs["clean"]["bytes"] and outputs[kind]["returncode"] == 0 for kind in outputs)
        results["stages"].append({"stage": stage, "identical": identical,
                                  "lookups": {kind: outputs[kind]["lookups"] for kind in ("cold", "warm")},
                                  "supplementary_wall_ms": {kind: outputs[kind]["wall_ms"] for kind in outputs},
                                  "failures": [outputs[kind]["stderr"] for kind in outputs if outputs[kind]["returncode"]]})
        executables.append(outputs["clean"]["bytes"])
        current = Compiler(directory / f"ide-stage{stage}-clean")
    results["fixed_point"] = executables[0] is not None and executables[0] == executables[1]
    results["passed"] = results["fixed_point"] and all(stage["identical"] for stage in results["stages"])
    return results


# ---------------------------------------------------------------------------
# Dependency audit: a compiler built with BUSTER_INCREMENTAL_AUDIT records every
# program-level type and symbol id code generation reads for a function and
# checks it against the ids that function's record covers. A read outside the
# record would be a dependency the key misses; the audit requires none.

AUDIT_KEYS = ("functions", "touched_types", "closure_types", "touched_symbols", "closure_symbols", "type_violations", "symbol_violations",
              "overflows")


# Host compiles on Apple targets name the SDK explicitly; links add the
# frameworks the ide executable uses.
def apple_sysroot() -> list[str]:
    flags = []
    if sys.platform == "darwin":
        sdk = subprocess.run(["xcrun", "--sdk", "macosx", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
        flags = ["-isysroot", sdk] if sdk else []
    return flags


def self_host_flags(repository: Path, generated: Path, link: bool) -> list[str]:
    flags = ["-I" + str(repository / "src"), "-I" + str(generated), "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", *apple_sysroot()]
    if link and sys.platform == "darwin":
        flags += ["-framework", "AppKit", "-framework", "Metal", "-framework", "QuartzCore", "-framework", "Foundation"]
    return flags


def audit_compile(audit: Path, arguments: list[str], cache: Path) -> tuple[int, dict, list[str]]:
    if cache.exists():
        shutil.rmtree(cache)
    process = subprocess.run([str(audit), "cc", *arguments, f"-fincremental-cache={cache}", "-fincremental-stats", "-fincremental-trace"],
                             capture_output=True, text=True, errors="replace")
    _, records, functions = parse_records(process.stdout)
    violations = [row["name"] for row in functions if row.get("audit_violation") == "1"]
    return process.returncode, records.get("INCREMENTAL_AUDIT", {}), violations


def run_audit(compiler: Compiler, repository: Path, work: Path, generated: Path, targets: list[str]) -> dict:
    directory = work / "audit"
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    audit = directory / "ide-audit"
    build = subprocess.run([str(compiler.path), "cc", *self_host_flags(repository, generated, link=True), "-DBUSTER_INCREMENTAL_AUDIT=1",
                            str(repository / "src/buster/apps/ide/ide.c"), "-lm", "-o", str(audit)], capture_output=True, text=True, errors="replace")
    report = {"built": build.returncode == 0, "totals": {key: 0 for key in AUDIT_KEYS}, "compiles": 0, "refused_compiles": 0,
              "violations": [], "self_host": {}}
    if build.returncode:
        report["build_stderr"] = build.stderr[-2000:]
        return report
    cache = directory / "cache"
    flags = self_host_flags(repository, generated, link=False)
    code, totals, violations = audit_compile(audit, [*flags, "-c", str(repository / "src/buster/apps/ide/ide.c"), "-o", str(directory / "self.o")], cache)
    report["self_host"] = {"returncode": code, **totals}
    report["violations"] += [{"unit": "self-host", "function": name} for name in violations]
    for key in AUDIT_KEYS:
        report["totals"][key] += int(totals.get(key, 0))
    for fixture in sorted((repository / "tests").glob("*.c")):
        for target in targets:
            for allocator in ("fast", "quality", "mir-stack"):
                arguments = ["-c", str(fixture), "-o", str(directory / "fixture.o"), f"-fregister-allocator={allocator}"]
                arguments = ["-target", target, *arguments] if target != "host" else [*apple_sysroot(), *arguments]
                code, totals, violations = audit_compile(audit, arguments, cache)
                if code:
                    # Fixtures that need a sysroot the target lacks, or are
                    # deliberate negative controls, are counted, not audited.
                    report["refused_compiles"] += 1
                    continue
                report["compiles"] += 1
                for key in AUDIT_KEYS:
                    report["totals"][key] += int(totals.get(key, 0))
                report["violations"] += [{"unit": fixture.name, "target": target, "allocator": allocator, "function": name} for name in violations]
    totals = report["totals"]
    report["passed"] = report["built"] and not report["violations"] and not totals["type_violations"] and not totals["symbol_violations"] \
        and not totals["overflows"] and report["self_host"].get("returncode") == 0
    return report


# ---------------------------------------------------------------------------
# Reporting

def summarize_step(step: dict) -> str:
    lookups = ", ".join(f"{key} {value}" for key, value in sorted(step["lookups"].items()))
    invalidated = ", ".join(f"{row['name']} ({row['lookup']})" for row in step["invalidated"][:12])
    if len(step["invalidated"]) > 12:
        invalidated += f", ... {len(step['invalidated']) - 12} more"
    work = step["work"]
    return (f"| {'pass' if step['passed'] else 'FAIL'} | {step['functions_total']} | {lookups} | {invalidated or 'none'} | "
            f"{step['necessary_invalidations']} | {step['relabel_invalidations']} | {step['over_invalidations']} | "
            f"{work['reused_ir_instructions']}/{work['lowered_ir_instructions']} | "
            f"{work['reused_code_bytes']} | {work['bytes_reparsed']} |")


def markdown(report: dict) -> str:
    out = ["# Incremental code-generation edit matrix", "",
           f"Compiler: `{report['compiler']}`", ""]
    header = ("| oracle | functions | lookups | invalidated (reason) | necessary | relabel | over | reused IR rows | reused code bytes | bytes reparsed |\n"
              "|---|---:|---|---|---:|---:|---:|---:|---:|---:|")
    if report.get("matrix"):
        out += ["## Edit matrix", ""]
        for target in sorted({row["target"] for row in report["matrix"]}):
            out += [f"### Target `{target}`", "", "| scenario | expected | " + header.split("\n")[0][2:], "|---|---|" + header.split("\n")[1][1:]]
            for row in report["matrix"]:
                if row["target"] == target:
                    out.append(f"| {row['scenario']} | {row['expected']} " + summarize_step(row["edit"]))
            out.append("")
    if report.get("random"):
        out += ["## Randomized edit sequences", "",
                "| target | seed | steps | passed | reused/lowered IR rows | necessary | relabel | over |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
        for sequence in report["random"]:
            steps = sequence["steps"]
            reused = sum(step["work"]["reused_ir_instructions"] for step in steps[1:])
            lowered = sum(step["work"]["lowered_ir_instructions"] for step in steps[1:])
            necessary = sum(step["necessary_invalidations"] for step in steps[1:])
            relabeled = sum(step["relabel_invalidations"] for step in steps[1:])
            over = sum(step["over_invalidations"] for step in steps[1:])
            passed = sum(1 for step in steps if step["passed"])
            out.append(f"| {sequence['target']} | {sequence['seed']} | {len(steps)} | {passed} | {reused}/{lowered} | {necessary} | {relabeled} | {over} |")
        out.append("")
    if report.get("self_host"):
        host = report["self_host"]
        out += ["## Self-hosting through the cache", "", "| stage | clean = cold = warm | warm reused | supplementary wall ms (clean / cold / warm) |",
                "|---:|---|---:|---|"]
        for stage in host["stages"]:
            wall = stage["supplementary_wall_ms"]
            out.append(f"| {stage['stage']} | {stage['identical']} | {stage['lookups']['warm'].get('reused', 0)} | "
                       f"{wall['clean']} / {wall['cold']} / {wall['warm']} |")
        out += ["", f"Fixed point (stage 2 = stage 3): {host['fixed_point']}", ""]
    if report.get("audit"):
        audit = report["audit"]
        totals = audit["totals"]
        out += ["## Dependency audit", "",
                f"Audit compiler built: {audit['built']}; compiles audited: {audit['compiles']} (+ self-host unit); refused: {audit['refused_compiles']}", "",
                "| functions | touched / keyed types | touched / keyed symbols | type violations | symbol violations | overflows |",
                "|---:|---:|---:|---:|---:|---:|",
                f"| {totals['functions']} | {totals['touched_types']} / {totals['closure_types']} | {totals['touched_symbols']} / {totals['closure_symbols']} | "
                f"{totals['type_violations']} | {totals['symbol_violations']} | {totals['overflows']} |", ""]
    if report.get("real_code"):
        out += ["## Real code: self-host unity translation unit", "", "| edit | " + header.split("\n")[0][2:], "|---|" + header.split("\n")[1][1:]]
        for step in report["real_code"]:
            out.append(f"| {step['label']} " + summarize_step(step))
        out.append("")
    out.append(f"Overall: {'PASS' if report['passed'] else 'FAIL'} ({report['steps_passed']}/{report['steps']} steps)")
    return "\n".join(out) + "\n"


def all_steps(report: dict):
    for row in report.get("matrix", []):
        yield row["base"]
        yield row["edit"]
    for sequence in report.get("random", []):
        yield from sequence["steps"]
    yield from report.get("real_code", [])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--matrix", action="store_true")
    parser.add_argument("--random", default="", help="SEEDS:STEPS")
    parser.add_argument("--real-code", action="store_true")
    parser.add_argument("--self-host", action="store_true")
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--audit-targets", default="host", help="targets for the fixture audit sweep (default: host)")
    parser.add_argument("--generated", type=Path, default=Path("build/generated"))
    parser.add_argument("--targets", default="host")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    arguments = parser.parse_args()
    repository = Path(__file__).resolve().parent.parent
    compiler = Compiler(arguments.compiler)
    work = arguments.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    targets = [target for target in arguments.targets.split(",") if target]
    report: dict = {"compiler": str(arguments.compiler), "targets": targets}
    if arguments.matrix:
        report["matrix"] = run_matrix(compiler, work, targets)
    if arguments.random:
        seeds, steps = (int(value) for value in arguments.random.split(":"))
        report["random"] = run_random(compiler, work, seeds, steps, targets)
    if arguments.real_code:
        report["real_code"] = run_real_code(compiler, repository, work, arguments.generated.resolve())
    if arguments.self_host:
        report["self_host"] = run_self_host(compiler, repository, work, arguments.generated.resolve())
    if arguments.audit:
        audit_targets = [target for target in arguments.audit_targets.split(",") if target]
        report["audit"] = run_audit(compiler, repository, work, arguments.generated.resolve(), audit_targets)
    steps = list(all_steps(report))
    report["steps"] = len(steps)
    report["steps_passed"] = sum(1 for step in steps if step["passed"])
    report["passed"] = report["steps"] == report["steps_passed"] and (report["steps"] > 0 or "self_host" in report or "audit" in report) and \
        report.get("self_host", {}).get("passed", True) and report.get("audit", {}).get("passed", True)
    if arguments.json:
        arguments.json.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    text = markdown(report)
    if arguments.markdown:
        arguments.markdown.write_text(text)
    sys.stdout.write(text)
    for step in steps:
        for failure in step["failures"]:
            sys.stdout.write(f"FAILURE {step['label']}: {failure}\n")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
