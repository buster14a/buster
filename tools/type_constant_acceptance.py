#!/usr/bin/env python3
"""Bounded #1247 baseline/candidate acceptance and layout oracle.

This is a correctness diagnostic for an authorized hosted executor. It never
benchmarks, edits repository files, dispatches jobs, or accesses the network.
Every compiler action and its complete stdout/stderr is preserved. Both syntax
and linked-object actions are checked: a syntax pass cannot hide an IR failure.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys

import type_constant_layout_probe as previous


def layout_program(body: str, file_scope: str = "") -> str:
    return """#include <stdio.h>
#include <stddef.h>
""" + file_scope + "\nint main(void) {\n" + body + """
    enum { PS = sizeof(T), PA = _Alignof(T), PO = offsetof(T, x) };
    static T objects[2];
    static struct { char lead; T value; } alignment;
    printf("size pf=%d ir=%td\\n", PS, (char *)&objects[1] - (char *)&objects[0]);
    printf("alignment pf=%d ir=%td\\n", PA, (char *)&alignment.value - (char *)&alignment);
    printf("offset pf=%d ir=%td\\n", PO, (char *)&objects[0].x - (char *)&objects[0]);
    return 0;
}
"""


def statement_program(file_scope: str, statements: str) -> str:
    return "#include <stdio.h>\n#include <stddef.h>\n" + file_scope + "\nint main(void) {\n" + statements + "\nreturn 0;\n}\n"


def cases() -> dict[str, dict]:
    result = {}

    def add(name: str, source: str, stage: int, expectation: str = "valid", note: str = "") -> None:
        result[name] = {"source": source, "stage": stage, "expectation": expectation, "note": note}

    for name, shape in previous.TARGETED.items():
        stage = 1 if name.startswith("width") else 2 if name.startswith("align") else 3 if name.startswith("bound") else 4
        add(name, layout_program("", shape), stage, note="Recovered #1278 file-scope target; emitted alignment added independently.")
    for name, shape in previous.TARGETED_BLOCK.items():
        add(name, layout_program(shape[1], shape[0]), 1 if name.startswith("width") else 3,
            "oracle-sensitive" if name == "width_shift_overflow" else "valid",
            "Shift exceeds promoted int width; Clang/GCC may diagnose/fold differently." if name == "width_shift_overflow" else "")
    for name, source in previous.TARGETED_RAW.items():
        add(name, source, 0, "invalid" if name == "width_cast_redefines_tag" else "valid", "Recovered adversarial tag mutation target.")

    for label, expression in {
        "octal": "010", "binary": "0b101", "digit_separator": "1'0", "nested_parens": "(((5u)))",
        "negative_intermediate_unsigned": "(unsigned)(-1) & 7u", "sizeof_string": 'sizeof "abcd"',
        "sizeof_char_literal": "sizeof('a')", "sizeof_conditional": "sizeof(1 ? a : a)",
        "sizeof_new_tag": "sizeof(struct Fresh { char q[5]; })",
        "sizeof_tag_pointer": "sizeof(struct Fresh { char q[5]; } *)",
        "enum_defined_sizeof": "sizeof(enum { Q = 5 }) + Q",
        "generic_cast": "_Generic((int)0, int: 5, default: 2)",
        "tag_body_cast": "((struct Fresh { int q; } *)0 == 0 ? 5 : 2)",
        "typeof_scalar": "sizeof(typeof((unsigned char)3))",
    }.items():
        add("width_" + label, layout_program("int a[5]; typedef struct { char c; unsigned b : " + expression + "; char x; } T;"), 1)

    for label, expression in {"cast_mask": "((unsigned char)264)", "masked_enum": "(A & 15)", "sizeof": "sizeof(double)",
                              "parenthesized": "(((8)))"}.items():
        for spelling in ("attribute", "alignas"):
            member = "int x __attribute__((aligned(" + expression + ")));" if spelling == "attribute" else "_Alignas(" + expression + ") int x;"
            add("alignment_" + spelling + "_" + label, layout_program("typedef struct { char c; " + member + " } T;", "enum { A = 24 };"), 2)
    for spelling in ("attribute", "alignas"):
        member = "int x __attribute__((aligned(A)));" if spelling == "attribute" else "_Alignas(A) int x;"
        add("alignment_" + spelling + "_shadow", layout_program("enum { A = 16 }; typedef struct { char c; " + member + " } T;", "enum { A = 8 };"), 2)
    add("alignment_suffix_sizeof_self", layout_program("", "struct S { char c; int x; } __attribute__((aligned(sizeof(struct S)))); typedef struct S T;"), 2)
    add("alignment_suffix_alignof_self", layout_program("", "struct S { char c; int x; } __attribute__((aligned(_Alignof(struct S) * 2))); typedef struct S T;"), 2)
    add("alignment_alignof", layout_program("", "typedef struct { char c; _Alignas(_Alignof(double)) int x; } T;"), 2)
    add("alignment_type", layout_program("", "typedef struct { char c; _Alignas(double) int x; } T;"), 2)
    add("alignment_completes_tag", statement_program("struct F; struct S { _Alignas(sizeof(struct F { char q[8]; })) int x; };", "printf(\"tag=%zu,%zu\\n\", sizeof(struct F), sizeof(struct S));"), 2)

    invalid = {
        "width_bool_too_wide": "struct Bad { _Bool b : 2; };",
        "width_negative": "struct Bad { int b : -1; };",
        "width_type_too_wide": "struct Bad { unsigned b : 40; };",
        "width_named_zero": "struct Bad { unsigned b : 0; };",
        "width_noninteger": "struct Bad { unsigned b : 3.5; };",
        "width_later_enum": "struct Bad { unsigned b : LATER; }; enum { LATER = 5 };",
        "width_later_tag": "struct F; struct Bad { unsigned b : sizeof(struct F); }; struct F { char q[3]; };",
        "alignment_nonpower": "struct Bad { _Alignas(3) int x; };",
        "alignment_negative": "struct Bad { _Alignas(-8) int x; };",
        "alignment_attribute_nonpower": "struct Bad { int x __attribute__((aligned(3))); };",
        "alignment_attribute_negative": "struct Bad { int x __attribute__((aligned(-8))); };",
        "alignment_weak": "struct Bad { _Alignas(1) int x; };",
        "alignment_later_enum": "struct Bad { _Alignas(LATER) int x; }; enum { LATER = 8 };",
        "bound_later_enum": "struct Bad { char x[LATER]; }; enum { LATER = 5 };",
        "bound_later_tag": "struct F; struct Bad { char x[sizeof(struct F)]; }; struct F { char q[3]; };",
        "bound_member_vla": "int n; struct Bad { char x[n]; };",
        "bound_negative": "struct Bad { char x[-1]; };",
    }
    for name, declaration in invalid.items():
        add(name, declaration + "\nint main(void) { return 0; }\n", 1 if name.startswith("width") else 2 if name.startswith("alignment") else 3, "invalid")

    add("stage0_shadowed_typedef_enum", statement_program("typedef int K;", "enum { K = 5 }; enum { E = (K) + 5 }; printf(\"E=%d\\n\", E);"), 0)
    add("stage0_shadowed_typedef_assert", statement_program("typedef int K;", "enum { K = 5 }; _Static_assert((K) + 5 == 10, \"scope\"); printf(\"scope=10\\n\");"), 0)
    add("width_promotion", statement_program("", "struct S { unsigned b : (3); } s = {0}; printf(\"promotion=%d\\n\", _Generic(+s.b, int: 1, unsigned: 2, default: 3));"), 1)
    add("bound_inferred", statement_program("", "static char a[] = {1,2,3,4,5}; enum { N = sizeof a }; printf(\"inferred=%d,%zu\\n\", N, sizeof a);"), 3)
    add("bound_vla", statement_program("", "int n = 5; char a[n]; printf(\"vla=%zu\\n\", sizeof a);"), 3)
    add("bound_self_name", "int main(void) { char DIM[3]; { char DIM[sizeof DIM + 5]; return sizeof DIM == 8 ? 0 : 1; } }\n", 3)
    add("bound_flexible", layout_program("typedef struct { int n; char x[]; } T;"), 3)
    add("bound_type_name_scope", statement_program("enum { N = 3 };", "enum { N = 5 }; enum { M = sizeof(char[N]) }; printf(\"type_name=%d\\n\", M);"), 3)
    add("bound_sibling_scope", """#include <stdio.h>
static unsigned long first(void) { enum { N = 3 }; struct S { char x[N]; }; return sizeof(struct S); }
static unsigned long second(void) { enum { N = 7 }; struct S { char x[N]; }; return sizeof(struct S); }
int main(void) { printf("siblings=%lu,%lu\\n", first(), second()); return 0; }
""", 3)
    add("bound_later_file_scope_shadow", """#include <stdio.h>
static unsigned long f(void) { enum { N = 8 }; struct S { char x[N]; }; return sizeof(struct S); }
enum { N = 5 };
int main(void) { printf("later_file=%lu\\n", f()); return 0; }
""", 3)
    add("bound_lmdb_reduction", """#include <stddef.h>
typedef struct { unsigned short pad; unsigned short flags; void *ptrs[1]; } Page;
typedef struct { unsigned magic; unsigned version; } Meta;
#define PAGEHDRSZ ((unsigned) offsetof(Page, ptrs))
typedef union { Page p; char pad[PAGEHDRSZ + sizeof(Meta)]; } Metabuf;
int f(void) { Metabuf pbuf; enum { Size = sizeof(pbuf) }; (void)pbuf; return Size; }
int main(void) { return f() == (int)sizeof(Metabuf) ? 0 : 1; }
""", 3, note="Issue #1247 LMDB-shaped reduction; no third-party source copied.")
    growth = "\n".join("struct A%d { char c; _Alignas(1 << (2 + %d %% 3)) int x; };" % (i, i) for i in range(80))
    add("alignment_growth_tail", layout_program(growth + "\ntypedef struct { char c; _Alignas(16) int x; } T;"), 2,
        note="Bounded 80 prior requests followed by final independent fold/object layout; internal rollback requires parser seams.")
    return result


def execute(command: list[str], timeout: int) -> dict:
    try:
        completed = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        result = {"command": command, "exit": completed.returncode, "stdout": completed.stdout, "stderr": completed.stderr, "timeout": False}
    except subprocess.TimeoutExpired as error:
        result = {"command": command, "exit": None, "stdout": str(error.stdout or ""), "stderr": str(error.stderr or ""), "timeout": True}
    except OSError as error:
        result = {"command": command, "exit": None, "stdout": "", "stderr": str(error), "timeout": False, "unavailable": True}
    return result


def evaluate(compiler: list[str], source: Path, binary: Path, timeout: int) -> dict:
    syntax = execute(compiler + ["-fsyntax-only", str(source)], timeout)
    compile_result = execute(compiler + [str(source), "-o", str(binary)], timeout)
    run = execute([str(binary)], 30) if compile_result["exit"] == 0 else None
    return {"syntax": syntax, "compile": compile_result, "run": run}


def signature(result: dict) -> tuple:
    return (result["syntax"]["exit"] == 0, result["compile"]["exit"] == 0,
            None if result["run"] is None else result["run"]["exit"],
            None if result["run"] is None else result["run"]["stdout"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--reference", action="append", help="Repeat compiler command; defaults to clang and gcc.")
    parser.add_argument("--variant", action="append", help="Repeat 'name=flags'; defaults to default=.")
    parser.add_argument("--require-stage", type=int, choices=range(5), help="Also fail unchanged mismatches for cases at/below completed stage.")
    parser.add_argument("--case", action="append", help="Run only named cases; repeatable.")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--materialize-only", action="store_true", help="Write source/manifest without executing compilers.")
    arguments = parser.parse_args()
    arguments.out.mkdir(parents=True, exist_ok=True)
    arguments.out = arguments.out.resolve()
    selected = cases()
    if arguments.case:
        missing = set(arguments.case) - selected.keys()
        if missing:
            parser.error("unknown cases: " + ", ".join(sorted(missing)))
        selected = {name: selected[name] for name in arguments.case}
    manifest = {"base_revision": "6eb73e977c32699d8828ca73b05ded20262aec28", "base_tree": "9c62795da30b9f1cf8d7706d055d1bf25a24a795", "previous_probe_ref": "efbd3dff", "previous_probe_blob": "41e2f0c3a136eb19527fead573e0bc0e646fc5f4", "cases": {}}
    for name, case in selected.items():
        source = arguments.out / (name + ".c")
        source.write_text(case["source"])
        manifest["cases"][name] = {key: value for key, value in case.items() if key != "source"}
        manifest["cases"][name]["sha256"] = hashlib.sha256(case["source"].encode()).hexdigest()
    (arguments.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if arguments.materialize_only:
        print("Materialized %d cases; no compiler executed." % len(selected))
        return 0
    references = arguments.reference or ["clang", "gcc"]
    variants = arguments.variant or ["default="]
    variant_flags = {}
    for item in variants:
        name, separator, flags = item.partition("=")
        if not separator or not name:
            parser.error("variant must be name=flags")
        variant_flags[name] = shlex.split(flags)
    report = {"manifest": manifest, "provenance": {}, "cases": {}, "regressions": [], "remaining": [], "changed_unresolved": [], "oracle_disagreements": [], "unavailable": []}
    for compiler in references:
        report["provenance"][compiler] = execute(shlex.split(compiler) + ["--version"], 30)
    for label, binary in (("baseline", arguments.baseline), ("candidate", arguments.candidate)):
        report["provenance"][label] = {"path": str(binary.resolve()), "sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    for name, case in selected.items():
        source = arguments.out / (name + ".c")
        oracle = {ref: evaluate(shlex.split(ref) + ["-std=gnu2x", "-w"], source, arguments.out / (name + ".ref" + str(index)), arguments.timeout) for index, ref in enumerate(references)}
        reference_signatures = {signature(value) for value in oracle.values()}
        available = all(not action.get("unavailable") and not action["timeout"] for value in oracle.values() for action in value.values() if action is not None)
        if not available:
            report["unavailable"].append(name)
        consensus = available and len(reference_signatures) == 1
        if not consensus:
            report["oracle_disagreements"].append(name)
        row = {"references": oracle, "consensus": consensus, "variants": {}}
        for variant, flags in variant_flags.items():
            baseline = evaluate([str(arguments.baseline.resolve()), "cc"] + flags, source, arguments.out / (name + ".base." + variant), arguments.timeout)
            candidate = evaluate([str(arguments.candidate.resolve()), "cc"] + flags, source, arguments.out / (name + ".candidate." + variant), arguments.timeout)
            subjects_available = all(not action.get("unavailable") and not action["timeout"] for value in (baseline, candidate) for action in value.values() if action is not None)
            if not subjects_available:
                report["unavailable"].append(name + ":" + variant)
            before, after = signature(baseline), signature(candidate)
            expected = next(iter(reference_signatures)) if consensus else None
            fixed = consensus and before != expected and after == expected
            lost_valid_syntax = (not consensus or expected[0]) and before[0] and not after[0]
            lost_valid_object = (not consensus or expected[1]) and before[1] and not after[1]
            regression = lost_valid_syntax or lost_valid_object or (consensus and before == expected and after != expected)
            mismatch = consensus and after != expected
            changed_unresolved = before != after and not fixed and not regression
            if regression:
                report["regressions"].append(name + ":" + variant)
            if mismatch and arguments.require_stage is not None and case["stage"] <= arguments.require_stage:
                report["remaining"].append(name + ":" + variant)
            if changed_unresolved:
                report["changed_unresolved"].append(name + ":" + variant)
            row["variants"][variant] = {"baseline": baseline, "candidate": candidate, "fixed": fixed, "regression": regression, "changed_unresolved": changed_unresolved, "candidate_matches_consensus": consensus and not mismatch}
            print("%s:%s %s" % (name, variant, "REGRESSION" if regression else "fixed" if fixed else "matches" if consensus and not mismatch else "remaining" if consensus else "oracle-disagreement"), flush=True)
        report["cases"][name] = row
        (arguments.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("regressions", "remaining", "changed_unresolved", "oracle_disagreements", "unavailable")}))
    return int(bool(report["regressions"] or report["remaining"] or report["unavailable"]))


if __name__ == "__main__":
    sys.exit(main())
