#!/usr/bin/env python3
"""Temporary, correctness-only #1028 experiment. Never use these binaries for timing.

Run in an isolated checkout of BASE on a standard GitHub runner. Instrumentation
is ephemeral: neither compiler source nor generated authority files are committed.
The trace compares scalar kind/qualifier projections, not complete composite types.
"""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

BASE = "744a948f41291b4238d1755b1507678799f1206d"
PARSE = "src/buster/lib/compiler/frontend/c/c_parse.c"
PARSE_BLOB = "b5c6a8e4cd76ab6ba81ba6fa44432176b11a6b2c"
PREFIX = b"SEM_CACHE_V1 "


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError("Expected one source anchor: " + old[:100])
    return text.replace(old, new, 1)


def instrument(original: bytes, cold: bool) -> bytes:
    if blob(original) != PARSE_BLOB:
        raise RuntimeError("Unexpected parser blob")
    text = original.decode("utf-8")
    start = text.index("BUSTER_C_INTERNAL bool c_parse_expression_type_query(")
    end = text.index("// Resolve one generic selection", start)
    fn = text[start:end]
    fn = replace_once(fn, "    bool valid;", "    bool valid;\n    bool probe_hit = false;\n    u32 probe_diagnostics = result->diagnostic_count;")
    fn = replace_once(fn, "        valid = true;", "        valid = true;\n        probe_hit = true;")
    trace = '''    // Diagnostic projection only; no TypeId or arena pointer is compared.
    if (query && getenv("BUSTER_SEMANTIC_CACHE_TRACE"))
    {
        CType value = valid && type_out->value < result->type_count ? result->types[type_out->value] : (CType){0};
        u32 qualifiers = (value.is_const ? 1u : 0u) | (value.is_volatile ? 2u : 0u) |
                         (value.is_restrict ? 4u : 0u) | (value.is_atomic ? 8u : 0u);
        u64 constraint_hash = 14695981039346656037ULL;
        for (u64 character = 0; character < machine->expression_constraint.length; character += 1)
        {
            constraint_hash ^= (u8)machine->expression_constraint.pointer[character];
            constraint_hash *= 1099511628211ULL;
        }
        u32 scalar = valid && value.kind >= C_TYPE_VOID && value.kind <= C_TYPE_NULLPTR && value.kind != C_TYPE_VA_LIST;
        fprintf(stderr, "SEM_CACHE_V1 %u %u %u %u %u %u %u %u %u %u %llu\\n",
                start, end, scope.value, flags, (u32)valid, (u32)value.kind, qualifiers, scalar,
                (u32)probe_hit, result->diagnostic_count - probe_diagnostics, (unsigned long long)constraint_hash);
    }
    return valid;'''
    fn = replace_once(fn, "    return valid;", trace)
    if cold:
        # VALID is always required by both production readers. An all-zero
        # published flags word therefore disables both, retaining the same
        # allocation, traversal, query entry points and diagnostic checks.
        fn = replace_once(fn, ".type = *type_out, .flags = flags};", ".type = *type_out, .flags = 0};")
    return ("#include <stdio.h>\n#include <stdlib.h>\n" + text[:start] + fn + text[end:]).encode()


def cases_from_source(root: Path) -> list[dict]:
    text = (root / "src/buster/tests/compiler/driver/driver_test.c").read_text()
    text = text[text.index("UnitTestResult compiler_driver_test_syntax_diagnostic_equivalence"):]
    text = text[text.index("} cases[] = {"):text.index("    String8 forms[]")]
    pattern = r'\{S8\(("(?:[^"\\]|\\.)*")\),\s*(true|false)(?:,\s*(true|false))?\}'
    cases = [{"name": f"frozen-{n:03}", "source": ast.literal_eval(s), "valid": valid == "true", "gnu": gnu == "true"}
             for n, (s, valid, gnu) in enumerate(re.findall(pattern, text))]
    if len(cases) != 222:
        raise RuntimeError(f"Expected 222 frozen cases, got {len(cases)}")
    controls = [
        ("typedef-shadow", "typedef long T; int f(void){T a=0; {typedef char T; T b=0; _Static_assert(sizeof b==1,\"inner\");} return sizeof a;}", True),
        ("typedef-value-shadow", "typedef int T; int f(void){int T=1; return sizeof(T+1);}", True),
        ("completed-tag", "struct S; struct S { int x; }; int f(void){return sizeof(struct S);}", True),
        ("array-context", "int f(void){int a[4]; _Static_assert(sizeof a==4*sizeof(int),\"array\");return _Generic(a,int *:1);}", True),
        ("unevaluated-invalid-update", "int f(void){return sizeof(++42);}", False),
        ("unevaluated-valid-update", "int f(void){int x=0;return sizeof(++x);}", True),
        ("object-alignment", "int f(void){_Alignas(32) int x;return __alignof__(x);}", True),
        ("vla", "int f(int n){int a[n];return sizeof a;}", True),
        ("compound-literal", "typedef int T; int f(void){return sizeof((T){1});}", True),
        ("parenthesized-type", "typedef int T; int f(void){return sizeof((T));}", False),
        ("enum-visibility", "enum { A=1, B=A+sizeof(int) }; int f(void){return B;}", True),
    ]
    cases.extend({"name": n, "source": s + "\n", "valid": v, "gnu": True, "control": True} for n, s, v in controls)
    return cases


def run_logged(command: list[str], root: Path, log: Path) -> None:
    with log.open("ab") as stream:
        stream.write(("$ " + repr(command) + "\n").encode())
        stream.flush()
        subprocess.run(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=1800)


def split_trace(data: bytes) -> tuple[bytes, list[tuple[int, ...]]]:
    ordinary: list[bytes] = []
    trace: list[tuple[int, ...]] = []
    for line in data.splitlines(keepends=True):
        if line.startswith(PREFIX):
            row = tuple(int(x) for x in line[len(PREFIX):].split())
            if len(row) != 11:
                raise RuntimeError("Malformed trace")
            trace.append(row)
        else:
            ordinary.append(line)
    return b"".join(ordinary), trace


def execute(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    original = (root / PARSE).read_bytes()
    variants = {"warm": instrument(original, False), "cold": instrument(original, True)}
    cases = cases_from_source(root)
    (out / "cases.json").write_text(json.dumps(cases, indent=2) + "\n")
    for name, data in variants.items():
        (out / (name + "-c_parse.c")).write_bytes(data)
    if args.prepare_only:
        print(json.dumps({"parser_blob": blob(original), "cases": len(cases), "prepared": True}))
        return
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    if head != BASE:
        raise RuntimeError("Refusing a non-pinned subject")
    env = os.environ.copy()
    env["BUSTER_TEST_JOBS"] = "1"
    env["BUSTER_SEMANTIC_CACHE_TRACE"] = "1"
    os.environ.pop("BUSTER_SEMANTIC_CACHE_TRACE", None)
    os.environ["BUSTER_TEST_JOBS"] = "1"
    identities = {"base": head, "tree": subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=root, text=True).strip(),
                  "parser_blob": blob(original), "instrumented_parser_sha256": {k: sha(v) for k, v in variants.items()},
                  "timing_evidence": False, "projection": "scalar kind + qualifiers; no composite equivalence claim"}
    (out / "identities.json").write_text(json.dumps(identities, indent=2) + "\n")
    driver = out / "buster-build"
    run_logged(["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "build.c", "-o", str(driver)], root, out / "build.log")
    run_logged([str(driver), "generate", "--config", "Release", "--cc", "clang", "--ci", "-DBUSTER_UNITY_BUILD=OFF", "-DBUSTER_INCLUDE_TESTS=ON", "-DBUSTER_BENCH_ALLOCATIONS=ON"], root, out / "build.log")
    if "BUSTER_BENCH_ALLOCATIONS:BOOL=ON" not in (root / "build/CMakeCache.txt").read_text():
        raise RuntimeError("Canonical construction counters were not enabled")
    binaries: dict[str, Path] = {}
    try:
        for name in ("base", "warm", "cold"):
            (root / PARSE).write_bytes(original if name == "base" else variants[name])
            run_logged([str(driver), "build", "--config", "Release", "-t", "ide", "--", "-j2"], root, out / "build.log")
            binary = out / ("ide-" + name)
            shutil.copy2(root / "build/Release/ide", binary)
            binaries[name] = binary
            identities[name + "_binary_sha256"] = sha(binary.read_bytes())
            (out / "identities.json").write_text(json.dumps(identities, indent=2) + "\n")
            if name in ("base", "cold"):
                run_logged([str(driver), "build", "--config", "Release", "-t", "test_all", "--", "-j2"], root, out / (name + "-test-all.log"))
    finally:
        (root / PARSE).write_bytes(original)
    shutil.copy2(root / "build/CMakeCache.txt", out / "CMakeCache.txt")
    input_dir = out / "inputs"
    input_dir.mkdir(exist_ok=True)
    summary = {"frozen_cases": 222, "controls": len(cases) - 222, "invocations": 0,
               "warm_hits": 0, "cold_hits": 0, "scalar_rows": 0, "trace_pairs": 0, "failures": []}
    with (out / "results.jsonl").open("w") as results:
        for case in cases:
            source = input_dir / (case["name"] + ".c")
            source.write_text(case["source"])
            if case.get("control"):
                oracle = subprocess.run(["clang", "-std=gnu2x", "-fsyntax-only", str(source)], capture_output=True, timeout=30)
                results.write(json.dumps({"case": case["name"], "oracle": "clang", "accepted": oracle.returncode == 0,
                                          "stderr": oracle.stderr.decode(errors="replace")}) + "\n")
                if (oracle.returncode == 0) != case["valid"]:
                    summary["failures"].append([case["name"], "independent-control-expectation"])
            for form in ("-ffrontend-ssa", "-fno-frontend-ssa"):
                syntax_observations: dict[str, tuple] = {}
                for action in ("syntax", "object"):
                    output = out / "current.o"
                    command = ["-g0", "-std=gnu23" if case["gnu"] else "-std=c23", form]
                    command += ["-fsyntax-only", str(source)] if action == "syntax" else ["-c", "-o", str(output), str(source)]
                    observations: dict[str, tuple] = {}
                    traces: dict[str, list[tuple[int, ...]]] = {}
                    for name, binary in binaries.items():
                        output.unlink(missing_ok=True)
                        process = subprocess.run([str(binary), "cc", *command], cwd=root, env=env, capture_output=True, timeout=45)
                        stdout, out_trace = split_trace(process.stdout)
                        stderr, err_trace = split_trace(process.stderr)
                        trace = out_trace + err_trace
                        observations[name] = (process.returncode, stdout, stderr, sha(output.read_bytes()) if output.exists() else None)
                        traces[name] = trace
                        summary["invocations"] += 1
                        record = {"case": case["name"], "source_sha256": sha(source.read_bytes()), "form": form, "action": action,
                                  "binary": name, "returncode": process.returncode, "stdout_sha256": sha(stdout), "stderr_sha256": sha(stderr),
                                  "object_sha256": observations[name][3], "trace": trace}
                        results.write(json.dumps(record) + "\n")
                        if (process.returncode == 0) != case["valid"]:
                            summary["failures"].append([case["name"], form, action, name, "frozen-acceptance", process.returncode])
                        if process.returncode and len(summary["failures"]) < 30:
                            (out / (case["name"] + "-" + action + "-" + name + ".diagnostics")).write_bytes(stdout + stderr)
                    if action == "syntax":
                        syntax_observations = {n: v[:3] for n, v in observations.items()}
                    elif syntax_observations != {n: v[:3] for n, v in observations.items()}:
                        summary["failures"].append([case["name"], form, "syntax-object-diagnostic-parity"])
                    if observations["base"] != observations["warm"] or observations["base"] != observations["cold"]:
                        summary["failures"].append([case["name"], form, action, "baseline-byte-parity"])
                    warm = [r[:8] + r[9:] for r in traces["warm"] if r[7]]
                    cold = [r[:8] + r[9:] for r in traces["cold"] if r[7]]
                    if warm != cold:
                        summary["failures"].append([case["name"], form, action, "scalar-query-projection"])
                    summary["scalar_rows"] += len(warm)
                    summary["trace_pairs"] += 1
                    summary["warm_hits"] += sum(r[8] for r in traces["warm"])
                    summary["cold_hits"] += sum(r[8] for r in traces["cold"])
    if not summary["warm_hits"] or summary["cold_hits"] or not summary["scalar_rows"]:
        summary["failures"].append(["cache-hit-control"])
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    if summary["failures"]:
        raise RuntimeError("Correctness experiment found discrepancies; inspect raw evidence")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    status = 0
    try:
        execute(args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"semantic-cache-validity: {error}", file=sys.stderr)
        status = 1
    return status


if __name__ == "__main__":
    raise SystemExit(main())
