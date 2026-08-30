#!/usr/bin/env python3
"""Reduce a differential-harness divergence to a minimal fixture.

Takes family:seed:unit, regenerates that single-unit program, and greedily
deletes lines while the divergence between clang and `ide cc` survives.  The
interestingness check preserves the divergence category:

  behavior   all four modes compile, clang -O0 and -O2 agree (so the reduced
             program is still well defined), and at least one ide mode's
             (exit, stdout) differs from clang's
  rejects    clang -O0 and -O2 compile and agree, and ide fails to compile
             with the same normalized diagnostic as the original
  ide-crash  same, but ide dies on a signal
  run-crash  all compile, clang modes agree, an ide binary dies on a signal

Line deletion is tried in coarse-to-fine blocks (a poor man's ddmin), then
single lines, repeated to a fixpoint.  Deleting a line can only shrink the
program, and the clang -O0/-O2 agreement gate keeps undefined behavior from
sneaking into the reduced fixture.

Usage:
    tools/reduce_differential_case.py bit_field:17:2
    tools/reduce_differential_case.py --source some.c --category behavior
"""

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from differential_c_harness import generate_program, DEFAULT_IDE, REPOSITORY_ROOT

# A healthy candidate runs in milliseconds; a deletion that breaks the
# prelude's emit loop spins forever, so the run timeout stays tight.
RUN_TIMEOUT_SECONDS = 1
COMPILE_TIMEOUT_SECONDS = 30


def normalize_diagnostic(text):
    for line in text.splitlines():
        if "error:" in line:
            line = re.sub(r"/[^ ]*\.c:\d+:\d+:", "<loc>:", line)
            line = re.sub(r"'unit_\d+'", "'unit'", line)
            line = re.sub(r"'[A-Za-z_][A-Za-z0-9_]*'", "'<id>'", line)
            line = re.sub(r"\(previous type \d+, new type \d+\)", "(types)", line)
            return line.strip()
    return ""


class Checker:
    def __init__(self, ide_path, work_directory):
        self.ide_path = ide_path
        self.work_directory = work_directory
        self.attempts = 0

    def compile_one(self, command, source_path, binary_path):
        try:
            process = subprocess.run(
                command + [source_path, "-o", binary_path],
                capture_output=True, timeout=COMPILE_TIMEOUT_SECONDS, cwd=self.work_directory,
            )
        except subprocess.TimeoutExpired:
            return None, ""
        return process.returncode, (process.stdout + process.stderr).decode("utf-8", "replace")

    def run_one(self, binary_path):
        try:
            process = subprocess.run(
                [binary_path], capture_output=True, timeout=RUN_TIMEOUT_SECONDS,
                cwd=self.work_directory,
            )
        except subprocess.TimeoutExpired:
            return ("timeout", b"")
        return (process.returncode, process.stdout)

    def observe(self, text, ide_modes=("ide", "ide-canon")):
        """Returns (category, detail) for the candidate program text.

        The expensive checks run lazily: clang -O0 and the ide modes decide
        interestingness; the clang -O2 control is only consulted once an ide
        divergence exists, since an attempt that already matches -O0 is
        uninteresting whatever the control says.  During reduction the caller
        pins ide_modes to the one mode that diverged at the start, halving
        the work again.
        """
        self.attempts += 1
        source_path = os.path.join(self.work_directory, "candidate.c")
        with open(source_path, "w") as source_file:
            source_file.write(text)
        commands = {
            "clang-O0": ["clang", "-O0", "-w"],
            "clang-O2": ["clang", "-O2", "-w"],
            "ide": [self.ide_path, "cc"],
            "ide-canon": [self.ide_path, "cc", "-fno-register-allocator"],
        }

        def evaluate(label):
            binary_path = os.path.join(self.work_directory, "candidate." + label)
            returncode, output = self.compile_one(commands[label], source_path, binary_path)
            if returncode != 0:
                return ("compile-fail", returncode, output)
            return ("ran",) + self.run_one(binary_path)

        reference = evaluate("clang-O0")
        if reference[0] != "ran":
            return ("invalid", "clang rejected")
        if reference[1] == "timeout":
            # A candidate whose reference build hangs has left the
            # deterministic regime (a deletion broke the emit loop); an ide
            # timeout is only a divergence when clang terminates.
            return ("invalid", "reference timed out")
        control = None
        for label in ide_modes:
            state = evaluate(label)
            if state[0] == "compile-fail":
                if state[1] is not None and state[1] < 0:
                    return ("ide-crash", "signal %d" % -state[1])
                return ("rejects", normalize_diagnostic(state[2]))
            if state[1] == "timeout" or state[1:] != reference[1:]:
                if control is None:
                    control = evaluate("clang-O2")
                    if control[0] != "ran" or control[1:] != reference[1:]:
                        return ("invalid", "clang modes disagree")
                if state[1] == "timeout":
                    return ("behavior", "%s timeout" % label)
                if isinstance(state[1], int) and state[1] < 0:
                    return ("run-crash", "%s signal %d" % (label, -state[1]))
                return ("behavior", "%s differs" % label)
        return ("ok", "")


def reduce_lines(lines, interesting):
    changed = True
    while changed:
        changed = False
        block = max(1, len(lines) // 4)
        while block >= 1:
            index = 0
            while index < len(lines):
                candidate = lines[:index] + lines[index + block:]
                if candidate and interesting(candidate):
                    lines = candidate
                    changed = True
                else:
                    index += block
            block //= 2
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("case", nargs="?", help="family:seed:unit")
    parser.add_argument("--source", help="reduce this file instead of regenerating a case")
    parser.add_argument("--category", help="expected category when --source is used")
    parser.add_argument("--units", type=int, default=6)
    parser.add_argument("--ide", default=DEFAULT_IDE)
    parser.add_argument("--out", default=None, help="write the reduced fixture here")
    arguments = parser.parse_args()

    os.chdir(REPOSITORY_ROOT)
    if arguments.source:
        with open(arguments.source) as source_file:
            text = source_file.read()
        tag = os.path.basename(arguments.source).replace(".c", "")
    else:
        family, seed_text, unit_text = arguments.case.split(":")
        program = generate_program(family, int(seed_text), arguments.units)
        text = program.render(selected=[int(unit_text)])
        tag = "%s_%s_u%s" % (family, seed_text, unit_text)

    # A per-case work directory: concurrent reductions must not share
    # candidate binaries (relinking a binary another case is executing
    # fails with ETXTBSY and shows up as a phantom reject).
    work_directory = os.path.join("build", "differential-c", "reduce", tag)
    os.makedirs(work_directory, exist_ok=True)
    checker = Checker(os.path.abspath(arguments.ide), os.path.abspath(work_directory))

    category, detail = checker.observe(text)
    if category in ("ok", "invalid"):
        print("not divergent at the start (%s: %s); nothing to reduce" % (category, detail))
        return 1
    print("reducing %s: %s (%s)" % (tag, category, detail))
    # Pin reduction to the one mode that diverged: rejects come from the
    # shared frontend, behavior stays with the mode that showed it.
    if "ide-canon" in detail:
        ide_modes = ("ide-canon",)
    else:
        ide_modes = ("ide",)

    def interesting(candidate_lines):
        candidate_category, candidate_detail = checker.observe("\n".join(candidate_lines) + "\n",
                                                               ide_modes=ide_modes)
        if candidate_category != category:
            return False
        if category == "rejects" and candidate_detail != detail:
            return False
        return True

    lines = text.splitlines()
    lines = reduce_lines(lines, interesting)
    reduced = "\n".join(lines) + "\n"
    final_category, final_detail = checker.observe(reduced)
    out_path = arguments.out or os.path.join("build", "differential-c", "reduce", tag + ".min.c")
    with open(out_path, "w") as out_file:
        out_file.write(reduced)
    print("reduced to %d lines (%d attempts): %s (%s)" % (len(lines), checker.attempts, final_category, final_detail))
    print("wrote %s" % out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
