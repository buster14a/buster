#!/usr/bin/env python3
"""Exact frontend source-fact census over deterministic adversarial corpora.

Run from the repository root with one or two compilers built with
BUSTER_BENCH_ALLOCATIONS=ON (see tools/throughput/README.md). Every generated
input is compiled with `ide cc -c -g0 -fsource-metrics=...`; the `c_census.*`
fields of each metrics file are collected per corpus and scale. With both
--baseline and --candidate, the two compilers must produce byte-identical
objects and identical diagnostics for every input, and the report lists the
exact counter deltas. The counters are work populations, not timings: see
src/buster/lib/compiler/frontend/c/c_census.h for their definitions.

    python3 tools/source_fact_census.py generate --out build/source-facts/corpus
    python3 tools/source_fact_census.py run --baseline A/ide --candidate B/ide \\
        --out build/source-facts

Generated sources are pure functions of (family, scale): no randomness, no
system includes, no timestamps. `--scales` defaults to 1 2 4 so each family
also reports how every counter scales with input size.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def identifier(index, width=0):
    """A short, distinct identifier drawn from [a-z][a-z0-9_]* by index."""
    letters = "abcdefghijklmnopqrstuvwxyz"
    tail = "abcdefghijklmnopqrstuvwxyz0123456789_"
    name = letters[index % 26]
    index //= 26
    while index:
        name += tail[index % len(tail)]
        index //= len(tail)
    return name + "_" * max(0, width - len(name))


def family_short_identifiers(scale):
    count = 4096 * scale
    lines = [f"int {identifier(i)}_v;" for i in range(count)]
    body = " + ".join(f"{identifier(i)}_v" for i in range(0, count, 7))
    lines.append(f"int short_identifiers_sum(void) {{ return {body}; }}")
    return "\n".join(lines) + "\n"


def family_repeated_identifiers(scale):
    statements = []
    for i in range(2048 * scale):
        statements.append("    x = x + y * x - z; y = y ^ x; z = z + x + y;")
    return "int repeated_identifiers(int x, int y, int z)\n{\n" + "\n".join(statements) + "\n    return x + y + z;\n}\n"


def family_long_identifiers(scale):
    count = 1024 * scale
    lines = []
    for i in range(count):
        # Long unique names whose distinguishing bytes sit in the middle,
        # beyond the interner's first/last eight-byte key words.
        lines.append(f"int long_identifier_prefix_{i:08d}_shared_suffix_for_every_declaration;")
    uses = " + ".join(f"long_identifier_prefix_{i:08d}_shared_suffix_for_every_declaration" for i in range(0, count, 5))
    lines.append(f"int long_identifiers_sum(void) {{ return {uses}; }}")
    return "\n".join(lines) + "\n"


def family_literal_heavy(scale):
    lines = []
    for i in range(256 * scale):
        payload = "".join(f"\\x{(i * 7 + j) % 256:02x}text{j}\\n" for j in range(24))
        lines.append(f'const char literal_string_{i}[] = "{payload}";')
        numbers = ", ".join(str((i * 131 + j * 17) % 100003) for j in range(32))
        lines.append(f"const unsigned literal_numbers_{i}[] = {{{numbers}}};")
        floats = ", ".join(f"{(i + j) / 8.0:.3f}" for j in range(8))
        lines.append(f"const double literal_floats_{i}[] = {{{floats}}};")
        characters = ", ".join(f"'{chr(97 + (i + j) % 26)}'" for j in range(8))
        lines.append(f"const char literal_characters_{i}[] = {{{characters}, '\\n', '\\0'}};")
    lines.append("unsigned literal_heavy_size(void) { return sizeof literal_string_0 + sizeof(\"abc\\0def\") + sizeof literal_numbers_0; }")
    return "\n".join(lines) + "\n"


def family_comment_heavy(scale):
    lines = []
    for i in range(1024 * scale):
        lines.append(f"/* block comment {i}: " + "commentary " * 12 + "*/")
        lines.append(f"// line comment {i}: " + "remark " * 10)
        lines.append(f"int comment_heavy_{i}; /* trailing */ // tail")
    return "\n".join(lines) + "\n"


def family_malformed_strings(scale):
    # Unterminated literals inside skipped groups are lexed but never reach the
    # parser; the final live one is diagnosed. The compile fails identically
    # for both compilers, and the census still covers lexing and parsing.
    lines = []
    for i in range(512 * scale):
        lines.append("#if 0")
        lines.append(f'const char *broken_{i} = "unterminated {i};')
        lines.append(f"char broken_character_{i} = 'x;")
        lines.append("#endif")
        lines.append(f'const char *fine_{i} = "fine {i}\\t";')
    lines.append('const char *really_broken = "no closing quote;')
    return "\n".join(lines) + "\n"


def family_dense_punctuation(scale):
    statements = []
    for i in range(1024 * scale):
        statements.append("    a+=b<<1;b^=~a>>2;c=(a&b)|(c^a)%7+!b;a=a?b:c;b-=(c<=a)==(a>=b);c*=a!=b&&b||c;")
    return "int dense_punctuation(int a, int b, int c)\n{\n" + "\n".join(statements) + "\n    return a + b + c;\n}\n"


def family_long_lines(scale):
    terms = " + ".join(f"(value * {i % 97} ^ {i})" for i in range(8192 * scale))
    return f"long long_line(long value) {{ return {terms}; }}\n"


def family_tiny_declarations(scale):
    lines = []
    for i in range(4096 * scale):
        lines.append(f"int tiny_{i};")
        lines.append(f"extern int tiny_function_{i}(void);")
        lines.append(f"typedef int tiny_type_{i};")
    return "\n".join(lines) + "\n"


def family_large_function(scale):
    statements = []
    for i in range(4096 * scale):
        statements.append(f"    if (value > {i}) {{ total += value * {i % 13 + 1}; }} else {{ total -= {i}; }}")
    return "long large_function(long value)\n{\n    long total = 0;\n" + "\n".join(statements) + "\n    return total;\n}\n"


def family_macro_paste(scale):
    lines = [
        "#define CAT(a, b) a##b",
        "#define NAME(i) CAT(pasted_, i)",
        "#define STR(x) #x",
        "#define DECLARE(i) int NAME(i) = i; const char *CAT(name_, i) = STR(i);",
    ]
    for i in range(2048 * scale):
        lines.append(f"DECLARE({i})")
    return "\n".join(lines) + "\n"


FAMILIES = {
    "short_identifiers": family_short_identifiers,
    "repeated_identifiers": family_repeated_identifiers,
    "long_identifiers": family_long_identifiers,
    "literal_heavy": family_literal_heavy,
    "comment_heavy": family_comment_heavy,
    "malformed_strings": family_malformed_strings,
    "dense_punctuation": family_dense_punctuation,
    "long_lines": family_long_lines,
    "tiny_declarations": family_tiny_declarations,
    "large_function": family_large_function,
    "macro_paste": family_macro_paste,
}


def generate(out, scales):
    out.mkdir(parents=True, exist_ok=True)
    manifest = []
    for family, build in FAMILIES.items():
        for scale in scales:
            source = build(scale).encode()
            path = out / f"{family}-{scale}.c"
            path.write_bytes(source)
            manifest.append({"family": family, "scale": scale, "path": str(path), "bytes": len(source),
                             "sha256": hashlib.sha256(source).hexdigest()})
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def parse_census(text):
    fields = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if separator and key.startswith("c_census.") and key not in ("c_census.version",):
            fields[key[len("c_census."):]] = int(value)
    return fields


def compile_one(compiler, source, work, label):
    metrics = work / f"{source.stem}-{label}.metrics"
    output = work / f"{source.stem}-{label}.o"
    for stale in (metrics, output):
        if stale.exists():
            stale.unlink()
    command = [str(compiler), "cc", "-c", "-g0", "-std=gnu17", f"-fsource-metrics={metrics}", str(source), "-o", str(output)]
    completed = subprocess.run(command, capture_output=True)
    record = {"exit": completed.returncode, "stderr": completed.stderr.decode(errors="replace"),
              "object_sha256": hashlib.sha256(output.read_bytes()).hexdigest() if output.exists() else None,
              "census": parse_census(metrics.read_text()) if metrics.exists() else {}}
    if record["census"].get("overflowed"):
        raise SystemExit(f"census overflowed for {source}")
    return record


def run(args):
    manifest = generate(args.out / "corpus", args.scales)
    compilers = {"baseline": args.baseline}
    if args.candidate:
        compilers["candidate"] = args.candidate
    work = args.out / "work"
    work.mkdir(parents=True, exist_ok=True)
    results = []
    failures = []
    for entry in manifest:
        source = Path(entry["path"])
        records = {label: compile_one(path.resolve(), source.resolve(), work, label) for label, path in compilers.items()}
        if not records["baseline"]["census"]:
            failures.append(f"{source.name}: baseline wrote no census (is it a BUSTER_BENCH_ALLOCATIONS build?)")
        if "candidate" in records:
            base, cand = records["baseline"], records["candidate"]
            if base["object_sha256"] != cand["object_sha256"]:
                failures.append(f"{source.name}: objects differ")
            if base["exit"] != cand["exit"] or base["stderr"] != cand["stderr"]:
                failures.append(f"{source.name}: diagnostics differ")
        results.append({**entry, "records": records})
    (args.out / "census.json").write_text(json.dumps(results, indent=2) + "\n")
    write_report(args.out / "census.md", results, args.fields)
    for failure in failures:
        print(f"FAIL {failure}", file=sys.stderr)
    print(f"wrote {args.out / 'census.md'} ({len(results)} inputs, {len(failures)} failures)")
    return 1 if failures else 0


DEFAULT_FIELDS = [
    "translate_copied_bytes", "lex_token_rows", "output_token_rows", "space_foreign_bytes",
    "semantic.spelling_reads", "lower.spelling_reads",
    "semantic.string_equal_calls", "lower.string_equal_calls",
    "semantic.intern_calls", "lower.intern_calls",
    "parse.integer_conversions", "semantic.integer_conversions", "lower.integer_conversions",
    "semantic.string_counts", "semantic.string_count_bytes", "lower.string_decodes",
    "semantic.temp_spaces", "semantic.location_recoveries",
]


def write_report(path, results, fields):
    labels = list(results[0]["records"]) if results else []
    lines = ["| Input | Bytes | Counter | " + " | ".join(labels) + (" | Delta |" if len(labels) == 2 else ""),
             "|---|---:|---|" + "---:|" * len(labels) + ("---:|" if len(labels) == 2 else "")]
    for result in results:
        name = f"{result['family']}-{result['scale']}"
        for field in fields:
            values = [result["records"][label]["census"].get(field, 0) for label in labels]
            if not any(values):
                continue
            row = f"| {name} | {result['bytes']:,} | `{field}` | " + " | ".join(f"{value:,}" for value in values)
            if len(values) == 2:
                row += f" | {values[1] - values[0]:+,}"
            lines.append(row + " |")
    path.write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    generate_parser = commands.add_parser("generate", help="write the corpora and their manifest")
    generate_parser.add_argument("--out", type=Path, default=Path("build/source-facts/corpus"))
    generate_parser.add_argument("--scales", type=int, nargs="+", default=[1, 2, 4])
    run_parser = commands.add_parser("run", help="compile the corpora and collect the census")
    run_parser.add_argument("--baseline", required=True, type=Path)
    run_parser.add_argument("--candidate", type=Path)
    run_parser.add_argument("--out", type=Path, default=Path("build/source-facts"))
    run_parser.add_argument("--scales", type=int, nargs="+", default=[1, 2, 4])
    run_parser.add_argument("--fields", nargs="+", default=DEFAULT_FIELDS)
    args = parser.parse_args()
    if args.command == "generate":
        manifest = generate(args.out, args.scales)
        print(f"wrote {len(manifest)} inputs to {args.out}")
        return 0
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
