#!/usr/bin/env python3
"""Observe the actual search bodies in a private, bounded C harness.

Only generated test code receives counters. Counts are logical byte comparisons,
including both maximal-suffix orderings and a scalar periodicity comparison;
they are not retired instructions, SIMD work, or timing. Production bodies are
extracted afresh and every instrumented expression must match exactly once.
"""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess


PREFIX = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
typedef uint8_t u8;
typedef char char8;
typedef struct String8 { char8* pointer; u64 length; } String8;
#define BUSTER_GLOBAL_LOCAL static
#define BUSTER_STRING_NO_MATCH UINT64_MAX
#define BUSTER_BENCH_ALLOCATIONS 0
#define BUSTER_OPTIMIZE 0
static u64 comparisons;
static bool compare_byte(u8 a, u8 b, unsigned order)
{
    comparisons += 1;
    bool result = order == 0 ? a == b : order == 1 ? a > b : a < b;
    return result;
}
static u64 direct_search(String8 haystack, String8 needle)
{
    u64 result = needle.length ? BUSTER_STRING_NO_MATCH : 0;
    for (u64 position = 0; needle.length && needle.length <= haystack.length &&
         position <= haystack.length - needle.length && result == BUSTER_STRING_NO_MATCH; position += 1)
    {
        bool equal = true;
        for (u64 i = 0; equal && i < needle.length; i += 1)
        {
            equal = compare_byte((u8)haystack.pointer[position + i], (u8)needle.pointer[i], 0);
        }
        if (equal) result = position;
    }
    return result;
}
'''

FIXTURES = r'''
static u64 state = UINT64_C(0x9e3779b97f4a7c15);
static u8 random_byte(void)
{
    state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (u8)(state >> 33);
}
static bool check_case(u64 n, u64 m, unsigned shape, bool work_mutant)
{
    char haystack[8192], needle[8192], haystack_before[8192], needle_before[8192];
    memset(haystack, 'a', sizeof(haystack));
    memset(needle, 'a', sizeof(needle));
    if (shape == 0 && m) needle[m - 1] = 'b'; // Late mismatch at every direct-scan offset.
    if (shape == 1 && m && n >= m) // First exact match only at the last possible offset.
    {
        needle[m - 1] = 'b';
        haystack[n - 1] = 'b';
    }
    if (shape == 3) // Periodic exact prefix, including non-power-of-two lengths.
    {
        for (u64 i = 0; i < n; i += 1) haystack[i] = (char)('a' + i % 2);
        for (u64 i = 0; i < m; i += 1) needle[i] = (char)('a' + i % 2);
    }
    if (shape == 4 || shape == 5) // Full byte alphabet includes zero and high-bit bytes.
    {
        for (u64 i = 0; i < n; i += 1) haystack[i] = shape == 4 ? (char)((i * 71 + 13) % 251) : (char)random_byte();
        for (u64 i = 0; i < m; i += 1) needle[i] = (char)random_byte();
        if (m && n >= m)
        {
            memcpy(needle, haystack + n - m, (size_t)m);
            if (shape == 5 && ((n + m) & 1)) needle[m / 2] ^= 1;
        }
    }
    memcpy(haystack_before, haystack, sizeof(haystack));
    memcpy(needle_before, needle, sizeof(needle));
    String8 h = {n ? haystack : 0, n}, p = {m ? needle : 0, m};
    comparisons = 0;
    u64 expected = direct_search(h, p);
    u64 direct_work = comparisons;
    comparisons = 0;
    u64 actual = work_mutant ? direct_search(h, p) : string_first_sequence(h, p);
    u64 observed = comparisons;
    // This conservative linear envelope includes preprocessing and both byte
    // comparisons on a maximal-suffix mismatch. Short needles retain a bounded
    // direct scan, so their envelope includes the fixed threshold factor.
    u64 budget = m >= STRING_FIRST_SEQUENCE_TWO_WAY_MINIMUM_NEEDLE ? 8 * (n + m) + 64 :
        STRING_FIRST_SEQUENCE_TWO_WAY_MINIMUM_NEEDLE * (n + m + 1);
    bool immutable = memcmp(haystack, haystack_before, sizeof(haystack)) == 0 &&
                     memcmp(needle, needle_before, sizeof(needle)) == 0;
    bool exact_work = shape != 0 || !m || n < m || direct_work == (n - m + 1) * m;
    // A complete first-position match must stop after the initial equality
    // probe, without either maximal-suffix pass or a second matching scan.
    bool prefix_work = m < STRING_FIRST_SEQUENCE_TWO_WAY_MINIMUM_NEEDLE || n < m || expected != 0 || observed == m;
    bool valid = expected == actual && observed <= budget && immutable && exact_work && prefix_work;
    printf("%llu,%llu,%u,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u\n", (unsigned long long)n, (unsigned long long)m, shape,
           (unsigned long long)expected, (unsigned long long)actual, (unsigned long long)observed,
           (unsigned long long)direct_work, (unsigned long long)budget, (unsigned)immutable,
           (unsigned)exact_work, (unsigned)prefix_work, (unsigned)valid);
    return valid;
}
int main(int argc, char** argv)
{
    // Independent cross product: neither axis is defined as a fraction of the
    // other. Include the 31/32/33 switch, null-empty, oversize and equality bounds.
    const u64 lengths[] = {0, 1, 2, 31, 32, 33, 63, 64, 65, 128, 129, 257, 512, 1024, 2048, 4096};
    bool work_mutant = argc > 1 && strcmp(argv[1], "work-mutant") == 0;
    bool valid = true;
    puts("n,m,shape,expected,actual,comparisons,direct_comparisons,budget,immutable,direct_formula,prefix_work,pass");
    for (unsigned ni = 0; ni < sizeof(lengths) / sizeof(lengths[0]); ni += 1)
    {
        for (unsigned mi = 0; mi < sizeof(lengths) / sizeof(lengths[0]); mi += 1)
        {
            for (unsigned shape = 0; shape < 6; shape += 1)
            {
                valid = check_case(lengths[ni], lengths[mi], shape, work_mutant) && valid;
            }
        }
    }
    // Full-byte randomized positions exercise both critical-suffix orders
    // and shifts independently of the repeated-prefix budget population.
    for (u64 trial = 0; trial < 1024; trial += 1)
    {
        u64 n = 32 + random_byte();
        u64 m = 32 + random_byte() % 96;
        valid = check_case(n, m, 5, work_mutant) && valid;
    }
    return valid ? 0 : 1;
}
'''


def extract(source, name, result_type):
    match = re.search(rf"(?m)^(?:BUSTER_GLOBAL_LOCAL )?{result_type} {name}\([^\n]*\)\n\{{\n", source)
    if not match:
        raise ValueError(f"missing source function {name}")
    end = source.index("\n}\n", match.end()) + 3
    return source[match.start():end]


def instrument(body):
    sites = {
        "if (a == b)": "if (compare_byte(a, b, 0))",
        "else if (order == 0 ? a > b : a < b)": "else if (compare_byte(a, b, (unsigned)order + 1))",
        "needle[k] == window[k]": "compare_byte(needle[k], window[k], 0)",
        "needle[k - 1] == window[k - 1]": "compare_byte(needle[k - 1], window[k - 1], 0)",
    }
    for old, new in sites.items():
        if body.count(old) != 1:
            raise ValueError(f"comparison site changed: {old}")
        body = body.replace(old, new)
    if re.search(r"\b(?:needle|window)\[[^]]+\]\s*(?:==|>|<)", body):
        raise ValueError("uncounted direct byte comparison")
    return body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    source = (root / "src/buster/lib/string.c").read_text()
    threshold = re.search(r"(?m)^#define STRING_FIRST_SEQUENCE_TWO_WAY_MINIMUM_NEEDLE \(\d+\)$", source)
    if not threshold:
        raise ValueError("threshold definition changed")
    helper = extract(source, "string_first_sequence_two_way", "u64")
    search = extract(source, "string_first_sequence", "u64")
    slice_body = extract(source, "string_slice", "String8")
    equal = extract(source, "string_equal", "bool")
    equal_site = "result = s1.pointer[i] == s2.pointer[i];"
    if equal.count(equal_site) != 1:
        raise ValueError("scalar string_equal comparison changed")
    observed_equal = equal.replace(equal_site, "result = compare_byte((u8)s1.pointer[i], (u8)s2.pointer[i], 0);")
    unit = PREFIX + slice_body + "\n" + observed_equal + "\n" + threshold[0] + "\n" + instrument(helper) + "\n" + search + "\n" + FIXTURES
    args.output.mkdir(parents=True, exist_ok=False)
    c_path = args.output / "observer.c"
    c_path.write_text(unit)
    binary = args.output / "observer"
    command = [args.cc, "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", str(c_path), "-o", str(binary)]
    subprocess.run(command, check=True)
    runs = {}
    for label, argv in [("candidate", [str(binary)]), ("quadratic-work-control", [str(binary), "work-mutant"])]:
        run = subprocess.run(argv, capture_output=True, text=True)
        (args.output / (label + ".csv")).write_text(run.stdout)
        (args.output / (label + ".stderr")).write_text(run.stderr)
        runs[label] = run.returncode
    shift = "position += k - critical;"
    if unit.count(shift) != 1:
        raise ValueError("shift control source changed")
    shifted = args.output / "shift-mutant.c"
    shifted.write_text(unit.replace(shift, "position += k - critical + 1;"))
    mutant = args.output / "shift-mutant"
    mutant_command = command.copy()
    mutant_command[-3:] = [str(shifted), "-o", str(mutant)]
    subprocess.run(mutant_command, check=True)
    run = subprocess.run([str(mutant)], capture_output=True, text=True)
    (args.output / "shift-mutant.csv").write_text(run.stdout)
    (args.output / "shift-mutant.stderr").write_text(run.stderr)
    runs["shift-mutant"] = run.returncode
    probe = "if (string_equal(string_slice(s, 0, sub.length), sub))"
    if unit.count(probe) != 1:
        raise ValueError("prefix probe source changed")
    preprocessing = args.output / "preprocessing-mutant.c"
    preprocessing.write_text(unit.replace(probe, "if (false)"))
    preprocessing_binary = args.output / "preprocessing-mutant"
    preprocessing_command = command.copy()
    preprocessing_command[-3:] = [str(preprocessing), "-o", str(preprocessing_binary)]
    subprocess.run(preprocessing_command, check=True)
    run = subprocess.run([str(preprocessing_binary)], capture_output=True, text=True)
    (args.output / "preprocessing-mutant.csv").write_text(run.stdout)
    (args.output / "preprocessing-mutant.stderr").write_text(run.stderr)
    runs["preprocessing-mutant"] = run.returncode
    controls = {}
    for label in runs:
        with (args.output / (label + ".csv")).open() as file:
            rows = list(csv.DictReader(file))
        controls[label] = {"rows": len(rows),
                           "offset_failures": sum(row["actual"] != row["expected"] for row in rows),
                           "work_failures": sum(int(row["comparisons"]) > int(row["budget"]) for row in rows),
                           "mutation_failures": sum(row["immutable"] != "1" for row in rows),
                           "direct_formula_failures": sum(row["direct_formula"] != "1" for row in rows),
                           "prefix_work_failures": sum(row["prefix_work"] != "1" for row in rows),
                           "failed_rows": sum(row["pass"] != "1" for row in rows)}
    report = {"source_sha256": hashlib.sha256(source.encode()).hexdigest(),
              "helper_sha256": hashlib.sha256(helper.encode()).hexdigest(),
              "search_sha256": hashlib.sha256(search.encode()).hexdigest(),
              "equal_sha256": hashlib.sha256(equal.encode()).hexdigest(),
              "command": command, "mutant_command": mutant_command,
              "status": runs, "controls": controls,
              "csv_sha256": {label: hashlib.sha256((args.output / (label + ".csv")).read_bytes()).hexdigest() for label in runs},
              "unit": "logical scalar byte comparisons; not timing or PMU instructions"}
    (args.output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    if runs != {"candidate": 0, "quadratic-work-control": 1, "shift-mutant": 1, "preprocessing-mutant": 1}:
        raise SystemExit(f"unexpected controls: {runs}")
    if any(control["rows"] != 2560 or control["mutation_failures"] or control["direct_formula_failures"] for control in controls.values()):
        raise SystemExit("fixture/invariance controls failed")
    if controls["candidate"]["failed_rows"] or controls["quadratic-work-control"]["offset_failures"] or not controls["quadratic-work-control"]["work_failures"] or not controls["shift-mutant"]["offset_failures"]:
        raise SystemExit("work/offset negative controls did not fail for the intended reason")
    if controls["preprocessing-mutant"]["offset_failures"] or controls["preprocessing-mutant"]["work_failures"] or not controls["preprocessing-mutant"]["prefix_work_failures"]:
        raise SystemExit("preprocessing negative control did not isolate the prefix path")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
