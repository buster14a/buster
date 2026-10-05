#!/usr/bin/env python3
"""Differential census of Buster's standalone AArch64 `.s` assembler.

Usage: aarch64_assembler_census.py --ide build/Release/ide
       [--clang clang] [--llvm-mc llvm-mc] [--llvm-objdump llvm-objdump]
       [--fixtures tests] [--jobs N] [--report DIRECTORY] [--max-lines N]

The corpus is every repository `tests/*.c` fixture without `<...>` includes,
compiled by Clang for aarch64-linux-gnu at -O0, -O1 and -O2 with -S. Each
distinct instruction line that needs no label, symbol or relocation is
assembled on its own by Buster (`ide cc -target aarch64-linux -c`) and by
llvm-mc. Both objects are disassembled by llvm-objdump and the instruction
text is compared, so equivalent encodings compare equal.

LLVM is a test-time oracle only; nothing in the compiler depends on it.
The script prints a summary table and fails closed. It exits nonzero when:
- Buster encodes any line differently from llvm-mc;
- a refused line does not match a documented-unsupported operand shape below
  (`--allow-refusals` disables only this check, for exploratory runs);
- an observer fails (llvm-objdump errors or disassembles nothing);
- the corpus is empty, no fixture compiled, or no line was compared.
Fixtures Clang cannot compile are reported, never silently dropped.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

# Operand shapes whose refusal is documented in docs/agents/driver.md
# ("AArch64 base instruction vocabulary"). Keep this list in sync with that
# section; a refusal matching none of these fails the census.
DOCUMENTED_UNSUPPORTED = (
    # By-element floating-point multiply-accumulate.
    re.compile(r"^fml[as]\s+v\d+\.\d+[hsd],\s*v\d+\.\d+[hsd],\s*v\d+\.[hsd]\[\d+\]$", re.IGNORECASE),
)

SKIP_PATTERNS = (
    re.compile(r"\.L"),
    re.compile(r":[a-z0-9_]+:"),
    re.compile(r"^\s*adrp?\b"),
    re.compile(r"^\s*(b|bl|b\.[a-z]+|cbn?z|tbn?z)\s"),
    re.compile(r"^\s*ldr\s+[^,]+,\s*=?[A-Za-z_.$]"),
)
DIRECTIVE = re.compile(r"^\s*\.")
LABEL = re.compile(r"^\s*[A-Za-z_.$][\w.$]*:")
REGISTER_LIKE = re.compile(
    r"^([wx]([12]?[0-9]|3[01])|w?sp|[wx]zr|[bhsdqv]([12]?[0-9]|3[01])(\.[0-9]*[bhsd])?(\[[0-9]+\])?|"
    r"lsl|lsr|asr|ror|[us]xt[bhwx]|msl|eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al|nv|"
    r"p?[a-z0-9_]+_el[0-3]|nzcv|fpcr|fpsr|tpidr_el0|sy|ish|ishld|ishst|osh|nsh|ld|st|"
    r"pldl[123](keep|strm)|pstl[123](keep|strm)|csync|dsync|vae1is|zva|civac|cvau|ivau)$"
)


def instruction_lines(listing: str):
    for raw in listing.splitlines():
        line = raw.split("//", 1)[0].rstrip()
        if not line.strip() or DIRECTIVE.match(line) or LABEL.match(line):
            continue
        if not raw.startswith("\t"):
            continue
        text = line.strip()
        if any(pattern.search(text) for pattern in SKIP_PATTERNS):
            continue
        parts = text.split(None, 1)
        operands = parts[1] if len(parts) > 1 else ""
        words = re.findall(r"[A-Za-z_$][\w$.]*(?:\[[0-9]+\])?", operands)
        if any(not REGISTER_LIKE.match(word.lower()) for word in words):
            continue
        yield "\t" + re.sub(r"\s+", " ", text, count=1).replace(" ", "\t", 1)


def documented_refusal(line: str) -> bool:
    text = re.sub(r"\s+", " ", line.strip())
    return any(pattern.match(text) for pattern in DOCUMENTED_UNSUPPORTED)


def fixture_corpus(fixtures: Path, clang: str):
    """Return (lines, listings compiled, failed compilations)."""
    lines = set()
    compiled = 0
    failed = []
    sources = sorted(path for path in fixtures.glob("*.c") if "#include <" not in path.read_text(errors="replace"))
    with tempfile.TemporaryDirectory() as directory:
        for source in sources:
            for level in ("-O0", "-O1", "-O2"):
                output = Path(directory) / "listing.s"
                result = subprocess.run(
                    [clang, "--target=aarch64-linux-gnu", "-ffreestanding", "-w", level, "-S", str(source), "-o", str(output)],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                if result.returncode == 0:
                    compiled += 1
                    lines.update(instruction_lines(output.read_text(errors="replace")))
                else:
                    reason = result.stderr.strip().splitlines()[-1] if result.stderr.strip() else f"exit {result.returncode}"
                    failed.append(f"{source} {level}\t{reason}")
    return sorted(lines), compiled, failed


def disassemble(objdump: str, path: Path):
    result = subprocess.run([objdump, "-d", "--no-show-raw-insn", str(path)], capture_output=True, text=True)
    if result.returncode != 0:
        return None
    text = []
    for row in result.stdout.splitlines():
        match = re.match(r"^\s*[0-9a-f]+:\s+(.*)$", row)
        if match:
            text.append(re.sub(r"\s+", " ", match.group(1).split("//", 1)[0]).strip())
    return text


def check_line(arguments, line: str):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "line.s"
        source.write_text(".text\n" + line + "\n")
        buster_object = Path(directory) / "buster.o"
        llvm_object = Path(directory) / "llvm.o"
        llvm = subprocess.run(
            [arguments.llvm_mc, "-triple=aarch64-linux-gnu", "-mattr=+lse,+fullfp16", "-filetype=obj", str(source), "-o", str(llvm_object)],
            capture_output=True,
            text=True,
        )
        if llvm.returncode != 0:
            return line, "oracle-refused", ""
        buster = subprocess.run(
            [arguments.ide, "cc", "-target", "aarch64-linux", "-c", str(source), "-o", str(buster_object)],
            capture_output=True,
            text=True,
        )
        if buster.returncode != 0:
            message = re.sub(r"^.*?error: [^:]*:\d+:\d+: ", "", buster.stderr.strip().splitlines()[-1] if buster.stderr.strip() else "")
            return line, "refused", message
        expected = disassemble(arguments.llvm_objdump, llvm_object)
        actual = disassemble(arguments.llvm_objdump, buster_object)
        if not expected or not actual:
            # A failed or empty observation proves nothing either way.
            return line, "observer-failed", f"llvm={expected} buster={actual}"
        if expected != actual:
            return line, "different", f"llvm={expected} buster={actual}"
        return line, "same", ""


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    parser.add_argument("--ide", required=True)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--llvm-mc", default="llvm-mc")
    parser.add_argument("--llvm-objdump", default="llvm-objdump")
    parser.add_argument("--fixtures", default="tests")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--report", help="directory receiving refused.txt and different.txt")
    parser.add_argument("--max-lines", type=int, default=0)
    parser.add_argument("--allow-refusals", action="store_true")
    arguments = parser.parse_args(argv)

    lines, compiled, compile_failures = fixture_corpus(Path(arguments.fixtures), arguments.clang)
    if arguments.max_lines:
        lines = lines[: arguments.max_lines]
    outcomes = collections.Counter()
    refusals = collections.defaultdict(collections.Counter)
    rows = collections.defaultdict(list)
    refused_lines = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, arguments.jobs)) as pool:
        for line, outcome, detail in pool.map(lambda line: check_line(arguments, line), lines):
            outcomes[outcome] += 1
            rows[outcome].append(f"{line.strip()}\t{detail}")
            if outcome == "refused":
                refusals[detail][line.split()[0]] += 1
                refused_lines.append((line, detail))

    print(f"listings: {compiled} compiled, {len(compile_failures)} failed")
    print(f"lines: {len(lines)}")
    for outcome in ("same", "different", "refused", "observer-failed", "oracle-refused"):
        print(f"{outcome}: {outcomes[outcome]}")
    for message, mnemonics in sorted(refusals.items(), key=lambda item: -sum(item[1].values())):
        top = ", ".join(f"{name} {count}" for name, count in mnemonics.most_common(12))
        print(f"  {sum(mnemonics.values())}\t{message}\t{top}")
    if arguments.report:
        report = Path(arguments.report)
        report.mkdir(parents=True, exist_ok=True)
        for outcome in ("different", "refused", "observer-failed"):
            (report / f"{outcome}.txt").write_text("\n".join(sorted(rows[outcome])) + ("\n" if rows[outcome] else ""))
        (report / "compile-failed.txt").write_text("\n".join(compile_failures) + ("\n" if compile_failures else ""))

    # Match the instruction text itself: report rows also separate the
    # mnemonic from its operands with a tab.
    undocumented = [(line, detail) for line, detail in refused_lines if not documented_refusal(line)]
    for line, detail in undocumented:
        print(f"undocumented refusal: {line.strip()}\t{detail}")
    for row in compile_failures:
        print(f"fixture did not compile: {row}")
    problems = []
    if not compiled:
        problems.append("no fixture compiled")
    if not lines:
        problems.append("empty corpus")
    if not outcomes["same"] and not outcomes["different"]:
        problems.append("no line was compared")
    if outcomes["different"]:
        problems.append("encoding differences")
    if outcomes["observer-failed"]:
        problems.append("observer failures")
    if undocumented and not arguments.allow_refusals:
        problems.append("undocumented refusals")
    for problem in problems:
        print(f"census failure: {problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
