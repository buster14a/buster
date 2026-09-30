#!/usr/bin/env python3
"""Build a callgrind-compatible twin of a configured tree's Release ide.

Valgrind cannot decode the AVX-512 that -march=native selects on some hosts,
so this reuses the tree's exact unity compile command from
build/compile_commands.json with -march=x86-64-v3 and embedded tests off. The
twin is for instruction counting only; timings and outputs come from the
ordinary Release compilers.

usage: profiling_build.py <tree> <output-directory>
"""
import json
import os
import re
import shlex
import subprocess
import sys


def main():
    tree = os.path.abspath(sys.argv[1])
    output = os.path.abspath(sys.argv[2])
    os.makedirs(output, exist_ok=True)
    commands = json.load(open(os.path.join(tree, "build", "compile_commands.json")))
    entry = next(item for item in commands if item["file"].endswith("src/buster/apps/ide/ide.c") and "/Release/" in item.get("output", item["command"]))
    arguments = shlex.split(entry["command"])
    rewritten = []
    skip = 0
    for index, argument in enumerate(arguments):
        if skip:
            skip -= 1
            continue
        if argument in ("-MD",):
            continue
        if argument in ("-MT", "-MF", "-o", "-c"):
            skip = 1
            continue
        if argument == "-march=native":
            argument = "-march=x86-64-v3"
        if argument == "-DBUSTER_INCLUDE_TESTS=1":
            argument = "-DBUSTER_INCLUDE_TESTS=0"
        rewritten.append(argument)
    source = os.path.join(tree, "src", "buster", "apps", "ide", "ide.c")
    subprocess.run(rewritten + ["-g", "-c", source, "-o", os.path.join(output, "ide.o")], check=True, cwd=entry["directory"])
    subprocess.run([rewritten[0], "-O3", os.path.join(output, "ide.o"), "-lm", "-o", os.path.join(output, "ide")], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
