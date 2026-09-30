#!/usr/bin/env python3
"""Compare two `ide` compilers' diagnostics and outputs over a fixed corpus.

A change that makes diagnostic, debug or recovery work lazy must not change
what a compilation prints, where it points, what it produces or how it exits.
This tool runs a baseline and a candidate `ide cc` over the same generated
inputs, in the same directory and with the same arguments, and compares:

- exit status, stdout and stderr byte for byte (the rendered diagnostics carry
  the wording, source path, line, column, ordering, warnings and notes);
- the object file, byte for byte, whenever a compilation succeeds;
- a second candidate run, which must repeat the first exactly.

Every case runs as `-fsyntax-only`, as `-c -g` and as `-c -g0`, so debug-enabled
and debug-disabled pipelines are both covered. The corpus spans lexical,
syntax, semantic and type errors, malformed literals, missing delimiters,
include/macro/#line contexts, long lines, invalid byte sequences, multiple
errors, recovery after an early error and error cascades, plus valid controls.
The `scaled` families repeat one error shape N times (--scale) so a lazy path
can be checked for bounded failure-path work.

With --baseline-census/--candidate-census (compilers built with
BUSTER_BENCH_ALLOCATIONS=ON), every run also writes -fsource-metrics and the
report sums the diagnostic_census.* and allocation.* fields separately over
successful and failing compilations. Census compilers must reproduce their
plain counterparts' output; the tool checks that too.

Usage:
    tools/diagnostic_equivalence.py --baseline A/ide --candidate B/ide --output DIR
    tools/diagnostic_equivalence.py ... --baseline-census A2/ide --candidate-census B2/ide
    tools/diagnostic_equivalence.py --list
    tools/diagnostic_equivalence.py --self-test

The inputs are generated here rather than stored under tests/, whose files
carry reviewed identities; nothing in the repository is written.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

MODES = (
    ("syntax", ["-fsyntax-only"]),
    ("object-g", ["-g", "-c"]),
    ("object-g0", ["-g0", "-c"]),
)


def case(name, category, files, main="main.c", std="gnu17", valid=False):
    return {
        "name": name,
        "category": category,
        "files": {path: (text.encode() if isinstance(text, str) else text) for path, text in files.items()},
        "main": main,
        "std": std,
        "valid": valid,
    }


def corpus(scale):
    cases = []
    add = cases.append
    # Lexical errors and invalid byte sequences.
    add(case("lex_invalid_utf8_identifier", "lexical", {"main.c": b"int caf\xc3(void);\nint ok;\n"}))
    add(case("lex_invalid_utf8_macro_body", "lexical", {"main.c": b"#define M caf\xe9\nint ok;\n"}))
    add(case("lex_invalid_utf8_after_splice", "lexical", {"main.c": b"int a;\nint b\\\n\xff = 1;\n"}))
    add(case("lex_unterminated_string", "lexical", {"main.c": "const char *s = \"abc;\nint x;\n"}))
    add(case("lex_unterminated_char", "lexical", {"main.c": "int c = 'a;\nint x;\n"}))
    add(case("lex_stray_character", "lexical", {"main.c": "int x = 1 @ 2;\n"}))
    add(case("lex_crlf_splice_undeclared", "lexical", {"main.c": "int a;\r\nint b = \\\r\n  undeclared_name;\r\n"}))
    add(case("lex_nul_byte", "lexical", {"main.c": b"int a;\nint b\x00;\n"}))
    add(case("lex_invalid_character", "lexical", {"main.c": "int y = 3 ` 4;\n"}))
    add(case("lex_invalid_characters_many", "lexical", {"main.c": "".join("int v%d = %d ` 1;\n" % (i, i) for i in range(70))}))
    add(case("lex_unterminated_comment", "lexical", {"main.c": "int a;\n/* never closed\nint b;\n"}))
    # Malformed literals.
    add(case("literal_integer_overflow", "literal", {"main.c": "int x = 99999999999999999999999;\n"}))
    add(case("literal_bad_suffix", "literal", {"main.c": "int x = 12uu;\n"}))
    add(case("literal_bad_binary", "literal", {"main.c": "int x = 0b102;\n"}))
    add(case("literal_bad_exponent", "literal", {"main.c": "double d = 1e+;\n"}))
    add(case("literal_in_body", "literal", {"main.c": "int f(void)\n{\n    return 0x1g;\n}\n"}))
    # Syntax errors and missing delimiters.
    add(case("syntax_missing_semicolon", "syntax", {"main.c": "int x\nint y;\n"}))
    add(case("syntax_missing_close_brace", "delimiter", {"main.c": "int f(void) { return 0;\n"}))
    add(case("syntax_unmatched_close", "delimiter", {"main.c": "int x; }\n"}))
    add(case("syntax_missing_parenthesis", "delimiter", {"main.c": "int f(void { return 0; }\n"}))
    add(case("syntax_expected_declaration", "syntax", {"main.c": "int f(void) { return 0; } )\n"}))
    # Semantic errors whose locations come from declaration records.
    add(case("sem_redefinition_function", "semantic", {"main.c": "int f(void) { return 0; }\nint f(void) { return 1; }\n"}))
    add(case("sem_redefinition_object", "semantic", {"main.c": "int x = 1;\n  int x = 2;\n"}))
    add(case("sem_conflicting_declaration", "semantic", {"main.c": "int x;\nlong x;\n"}))
    add(case("sem_enumerator_redefinition", "semantic", {"main.c": "enum A { RED };\nenum B {\n    GREEN,\n    RED\n};\n"}))
    add(case("sem_local_redefinition", "semantic", {"main.c": "int f(void)\n{\n    int a;\n    int a;\n    return 0;\n}\n"}))
    add(case("sem_undeclared_identifier", "semantic", {"main.c": "int f(void) { return missing; }\n"}))
    add(case("sem_static_assert_file", "semantic", {"main.c": "_Static_assert(sizeof(int) == 3, \"int is not 3 bytes\");\n"}))
    add(case("sem_static_assert_block", "semantic", {"main.c": "int f(void)\n{\n    _Static_assert(0, \"nope\");\n    return 0;\n}\n"}))
    add(case("sem_static_assert_nonconstant", "semantic", {"main.c": "int n;\n_Static_assert(n, \"not constant\");\n"}))
    add(case("sem_void_member", "semantic", {"main.c": "struct S\n{\n    int ok;\n    void v;\n};\n"}))
    add(case("sem_bit_field_width", "semantic", {"main.c": "struct S\n{\n    int x : 40;\n};\n"}))
    add(case("sem_flexible_array_not_last", "semantic", {"main.c": "struct S\n{\n    int items[];\n    int count;\n};\n"}))
    add(case("sem_void_object", "semantic", {"main.c": "void v;\n"}))
    add(case("sem_invalid_alignment", "semantic", {"main.c": "_Alignas(3) int x;\n"}))
    add(case("sem_alignment_redeclaration", "semantic", {"main.c": "_Alignas(16) int x;\n_Alignas(32) int x;\n"}))
    add(case("sem_alias_missing_target", "semantic", {"main.c": "int f(void) __attribute__((alias(\"missing\")));\n"}))
    add(case("sem_constexpr_function", "semantic", {"main.c": "constexpr int f(void);\n"}, std="c23"))
    add(case("sem_constexpr_no_initializer", "semantic", {"main.c": "constexpr int x;\n"}, std="c23"))
    add(case("sem_constexpr_extern", "semantic", {"main.c": "extern constexpr int y = 1;\n"}, std="c23"))
    add(case("sem_constexpr_pointer", "semantic", {"main.c": "constexpr int *p = (int *)1;\n"}, std="c23"))
    add(case("sem_constexpr_local", "semantic", {"main.c": "int f(void)\n{\n    constexpr int x = 1;\n    constexpr int *p = (int *)8;\n    return x;\n}\n"}, std="c23"))
    add(case("sem_duplicate_parameter", "semantic", {"main.c": "int f(int a, int a);\nint g(void) { return 0; }\n"}))
    add(case("sem_global_initializer", "semantic", {"main.c": "int f(void);\nint x = f();\n"}))
    add(case("sem_in_function_lowering", "semantic", {"main.c": "int f(int n)\n{\n    static int a[n];\n    return a[0];\n}\n"}))
    # Type errors.
    add(case("type_assignment", "type", {"main.c": "int g(void)\n{\n    int x;\n    x = \"t\";\n    return x;\n}\n"}))
    add(case("type_missing_member", "type", {"main.c": "struct S { int x; };\nint f(struct S *p) { return p->missing; }\n"}))
    add(case("type_struct_initializer", "type", {"main.c": "struct S{int x;};struct T{int x;};void f(struct S s){struct T t=s;}\n"}))
    add(case("type_call_arity", "type", {"main.c": "int f(int);\nint g(void) { return f(); }\n"}))
    # Nested source contexts: includes, macro expansions and #line.
    add(case("include_error", "context", {"inc.h": "int from_header = missing_symbol;\n", "main.c": "#include \"inc.h\"\nint ok;\n"}))
    add(case("include_redefinition", "context", {"b.h": "int dup = 1;\n", "a.h": "#include \"b.h\"\nint other;\n", "main.c": "#include \"a.h\"\nint dup = 2;\n"}))
    add(case("include_record_in_header", "context", {"s.h": "struct S\n{\n    void v;\n};\n", "main.c": "#include \"s.h\"\n"}))
    add(case("macro_redefinition", "context", {"main.c": "#define DECLARE(name) int name = 1; int name = 2;\n\nDECLARE(twice)\n"}))
    add(case("macro_nested_redefinition", "context", {"main.c": "#define INNER(x) x = 1\n#define OUTER(x) int INNER(x); int INNER(x);\nOUTER(v)\n"}))
    add(case("macro_enum_member", "context", {"main.c": "#define COLORS X(RED) X(GREEN) X(RED)\n#define X(n) n,\nenum C { COLORS };\n"}))
    add(case("line_directive_redefinition", "context", {"main.c": "#line 500 \"virtual.c\"\nint dup = 1;\nint dup = 2;\n"}))
    add(case("line_directive_member", "context", {"main.c": "#line 40\nstruct S\n{\n    int x : 99;\n};\n"}))
    add(case("line_directive_in_header", "context", {"h.h": "#line 7 \"renamed.h\"\nint dup;\nlong dup;\n", "main.c": "#include \"h.h\"\n"}))
    add(case("macro_static_assert", "context", {"main.c": "#define CHECK(e) _Static_assert(e, #e)\nCHECK(1 == 2);\n"}))
    # Long lines.
    add(case("long_line_error", "long-line", {"main.c": "int x = " + "1 + " * 20000 + "missing;\n"}))
    add(case("long_line_redefinition", "long-line", {"main.c": "".join("int v%d; " % i for i in range(3000)) + "int v17 = 1; int v17 = 2;\n"}))
    add(case("long_line_valid", "long-line", {"main.c": "int x = " + "1 + " * 20000 + "1;\n"}, valid=True))
    # Multiple errors, recovery after an early error, cascades.
    add(case("multiple_syntax_errors", "multiple", {"main.c": "int a\nint b\nint c;\n}\n)\n"}))
    add(case("multiple_preprocessor_errors", "multiple", {"main.c": "#error first\n#error second\n#warning third\nint x;\n"}))
    add(case("multiple_semantic_errors", "multiple", {"main.c": "int x;\nlong x;\nenum E { A };\nenum F { A };\nstruct S { void v; };\n_Static_assert(0, \"z\");\n"}))
    add(case("recovery_after_early_error", "recovery", {"main.c": "int f(void) { return missing; }\nint g(void) { return 1; }\nint g(void) { return 2; }\n"}))
    add(case("cascade_missing_brace", "cascade", {"main.c": "struct S { int x;\nint f(void) { return 0; }\n"}))
    add(case("cascade_bad_type", "cascade", {"main.c": "unknown_t a;\nunknown_t b;\nint f(unknown_t c) { return c; }\n"}))
    add(case("preprocessor_empty_if", "multiple", {"main.c": "#if\n#endif\nint x;\n"}))
    add(case("preprocessor_missing_include", "context", {"main.c": "#include \"nonexistent_header_for_equivalence.h\"\nint x;\n"}))
    add(case("preprocessor_divide_by_zero", "multiple", {"main.c": "#if 1/0\n#endif\nint x;\n"}))
    add(case("warning_then_error", "multiple", {"main.c": "#warning careful\nint f(void) { return missing; }\n"}))
    # Valid controls: macros, includes, #line, C23 respelling, debug-heavy code.
    add(case("valid_contexts", "valid", {
        "v.h": "#pragma once\nstruct P { int x; int y; };\nstatic inline int sum(struct P p) { return p.x + p.y; }\n",
        "main.c": "#include \"v.h\"\n#include \"v.h\"\n#define MAKE(a, b) ((struct P){ .x = (a), .y = (b) })\n#line 90 \"logical.c\"\n"
                  "enum Color { RED, GREEN = 4, BLUE };\nstruct Q { int a : 3; unsigned b : 5; int tail[]; };\n"
                  "int table[BLUE + 1];\nint compute(int n, int m)\n{\n    int total = 0;\n    for (int i = 0; i < n; i += 1)\n    {\n"
                  "        struct P p = MAKE(i, m);\n        total += sum(p);\n    }\n    static int calls;\n    calls += 1;\n"
                  "    return total + ({ int t = calls; t; });\n}\n_Static_assert(sizeof(int) == 4, \"int\");\n",
    }, valid=True))
    add(case("valid_c23", "valid", {"main.c": "constexpr int limit = 8;\nstatic_assert(limit == 8);\nbool flag = true;\n"
                                             "alignas(16) int aligned;\nint f(void)\n{\n    constexpr int local = 3;\n    return local + limit;\n}\n"}, std="c23", valid=True))
    add(case("valid_duplicate_tentative", "valid", {"main.c": "int x;\nint x;\nextern int x;\nint f(void);\nint f(void) { return x; }\n"}, valid=True))
    # Scaled families: one error shape repeated `scale` times, and the
    # matching valid population, to bound failure-path reconstruction.
    add(case("scaled_redefinitions", "scaled", {"main.c": "".join("int f%d(void) { return %d; }\nint f%d(void) { return 0; }\n" % (i, i, i) for i in range(scale))}))
    add(case("scaled_enumerators", "scaled", {"main.c": "enum A { " + ", ".join("E%d" % i for i in range(scale)) + " };\nenum B { " + ", ".join("E%d" % i for i in range(scale)) + " };\n"}))
    add(case("scaled_static_asserts", "scaled", {"main.c": "".join("_Static_assert(%d == 0, \"s%d\");\n" % (i + 1, i) for i in range(scale))}))
    add(case("scaled_members", "scaled", {"main.c": "".join("struct S%d { int ok; void v; };\n" % i for i in range(scale))}))
    add(case("scaled_syntax", "scaled", {"main.c": "".join("int a%d\n" % i for i in range(scale)) + "int end;\n"}))
    add(case("scaled_valid", "scaled", {"main.c": "".join("struct S%d { int x; int y; };\nenum E%d { A%d, B%d };\nint f%d(int a, int b) { struct S%d s = { a, b }; return s.x + s.y + A%d; }\n" % ((i,) * 7) for i in range(scale))}, valid=True))
    return cases


def run(compiler, arguments, directory, metrics):
    command = [compiler, "cc"] + arguments
    if metrics:
        command.append("-fsource-metrics=" + metrics)
    completed = subprocess.run(command, cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
    return completed.returncode, completed.stdout, completed.stderr


def digest(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def read_metrics(path):
    fields = {}
    if os.path.exists(path):
        with open(path) as handle:
            for line in handle:
                key, _, value = line.strip().partition("=")
                if key.startswith(("diagnostic_census.", "allocation.")) and value.isdigit():
                    fields[key] = int(value)
    return fields


def compile_once(compiler, entry, mode_arguments, directory, metrics_path):
    output = os.path.join(directory, "out.o")
    if os.path.exists(output):
        os.remove(output)
    arguments = ["-std=" + entry["std"]] + mode_arguments + [entry["main"]]
    if "-c" in mode_arguments:
        arguments += ["-o", "out.o"]
    status, stdout, stderr = run(compiler, arguments, directory, metrics_path)
    return {
        "status": status,
        "stdout": stdout,
        "stderr": stderr,
        "object": digest(output) if "-c" in mode_arguments else None,
    }


def compare(left, right):
    differences = []
    for key in ("status", "stdout", "stderr", "object"):
        if left[key] != right[key]:
            differences.append(key)
    return differences


def evaluate(arguments):
    cases = corpus(arguments.scale)
    os.makedirs(arguments.output, exist_ok=True)
    report = {"cases": [], "mismatches": [], "census": {}}
    totals = {}
    for entry in cases:
        directory = tempfile.mkdtemp(prefix="buster-diagnostic-equivalence-")
        try:
            for path, data in entry["files"].items():
                with open(os.path.join(directory, path), "wb") as handle:
                    handle.write(data)
            for mode, mode_arguments in MODES:
                baseline = compile_once(arguments.baseline, entry, mode_arguments, directory, None)
                candidate = compile_once(arguments.candidate, entry, mode_arguments, directory, None)
                repeat = compile_once(arguments.candidate, entry, mode_arguments, directory, None)
                row = {
                    "case": entry["name"],
                    "category": entry["category"],
                    "mode": mode,
                    "status": baseline["status"],
                    "stderr_lines": baseline["stderr"].count(b"\n"),
                    "stderr_sha256": hashlib.sha256(baseline["stderr"]).hexdigest(),
                    "object": baseline["object"],
                }
                checks = [("candidate", compare(baseline, candidate)), ("repeat", compare(candidate, repeat))]
                if entry["valid"] and baseline["status"] != 0:
                    checks.append(("valid-control-failed", ["status"]))
                if not entry["valid"] and baseline["status"] == 0 and entry["category"] not in ("valid",):
                    row["note"] = "accepted"
                for side, census_compiler, plain in (("baseline", arguments.baseline_census, baseline), ("candidate", arguments.candidate_census, candidate)):
                    if not census_compiler:
                        continue
                    metrics_path = os.path.join(directory, "census.metrics")
                    if os.path.exists(metrics_path):
                        os.remove(metrics_path)
                    census = compile_once(census_compiler, entry, mode_arguments, directory, metrics_path)
                    checks.append((side + "-census", compare(plain, census)))
                    fields = read_metrics(metrics_path)
                    row[side + "_census"] = fields
                    bucket = "success" if census["status"] == 0 else "failure"
                    key = "%s/%s/%s" % (side, mode, bucket)
                    total = totals.setdefault(key, {"compilations": 0})
                    total["compilations"] += 1
                    for name, value in fields.items():
                        total[name] = total.get(name, 0) + value
                for label, differences in checks:
                    if differences:
                        mismatch = {"case": entry["name"], "mode": mode, "check": label, "fields": differences}
                        if "stderr" in differences and label == "candidate":
                            mismatch["baseline_stderr"] = baseline["stderr"].decode(errors="replace")[:2000]
                            mismatch["candidate_stderr"] = candidate["stderr"].decode(errors="replace")[:2000]
                        report["mismatches"].append(mismatch)
                report["cases"].append(row)
        finally:
            shutil.rmtree(directory, ignore_errors=True)
    report["census"] = totals
    report["summary"] = {
        "cases": len(cases),
        "compilations_compared": len(report["cases"]),
        "failing_compilations": sum(1 for row in report["cases"] if row["status"] != 0),
        "successful_compilations": sum(1 for row in report["cases"] if row["status"] == 0),
        "mismatches": len(report["mismatches"]),
        "scale": arguments.scale,
    }
    with open(os.path.join(arguments.output, "diagnostic-equivalence.json"), "w") as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
    with open(os.path.join(arguments.output, "diagnostic-equivalence.txt"), "w") as handle:
        for row in report["cases"]:
            handle.write("%-34s %-10s %-9s status=%d stderr_lines=%d stderr=%s object=%s\n" % (
                row["case"], row["category"], row["mode"], row["status"], row["stderr_lines"], row["stderr_sha256"][:16],
                (row["object"] or "-")[:16]))
    print(json.dumps(report["summary"], sort_keys=True))
    for mismatch in report["mismatches"]:
        print("MISMATCH " + json.dumps(mismatch, sort_keys=True)[:4000])
    return 1 if report["mismatches"] else 0


def self_test():
    cases = corpus(4)
    names = [entry["name"] for entry in cases]
    assert len(names) == len(set(names)), "duplicate case names"
    categories = {entry["category"] for entry in cases}
    for required in ("lexical", "literal", "syntax", "delimiter", "semantic", "type", "context", "long-line", "multiple",
                     "recovery", "cascade", "valid", "scaled"):
        assert required in categories, required
    for entry in cases:
        assert entry["main"] in entry["files"], entry["name"]
        assert entry["std"] in ("gnu17", "c23"), entry["name"]
    assert corpus(8)[-1]["files"]["main.c"].count(b"enum E") == 8
    print("self-test passed: %d cases" % len(cases))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline")
    parser.add_argument("--candidate")
    parser.add_argument("--baseline-census")
    parser.add_argument("--candidate-census")
    parser.add_argument("--output", default="build/diagnostic-equivalence")
    parser.add_argument("--scale", type=int, default=256)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    if arguments.list:
        for entry in corpus(arguments.scale):
            print("%-34s %-10s std=%s valid=%s" % (entry["name"], entry["category"], entry["std"], entry["valid"]))
        return 0
    if not arguments.baseline or not arguments.candidate:
        parser.error("--baseline and --candidate are required")
    arguments.baseline = os.path.abspath(arguments.baseline)
    arguments.candidate = os.path.abspath(arguments.candidate)
    if arguments.baseline_census:
        arguments.baseline_census = os.path.abspath(arguments.baseline_census)
    if arguments.candidate_census:
        arguments.candidate_census = os.path.abspath(arguments.candidate_census)
    return evaluate(arguments)


if __name__ == "__main__":
    sys.exit(main())
