#!/usr/bin/env python3
"""Build a callgrind counting compiler: the Release unity command with -march=x86-64-v3 (valgrind
cannot decode the native AVX-512 build), -O1 -fno-inline (per-function attribution) and -g.

usage: build_counting.py SOURCE_ROOT OUT
env:   BUSTER_REPO, BUSTER_COMPILE_COMMANDS as in build_variant.py

The audit's ide-base-noinl2 (pinned base) and ide-proto-noinl (prototype) were built this way from a
git-archive of each tree. Research material, not build infrastructure."""
import json, os, shlex, subprocess, sys

repo = os.path.abspath(os.environ.get("BUSTER_REPO", os.getcwd()))
commands = os.environ.get("BUSTER_COMPILE_COMMANDS", os.path.join(repo, "build", "compile_commands.json"))
root, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
command = None
for entry in json.load(open(commands)):
    if entry["file"].endswith("apps/ide/ide.c") and "Release" in entry["command"]:
        command = shlex.split(entry["command"])
        break
if command is None:
    sys.exit("no Release ide.c entry")
rewritten = []
for flag in command:
    if flag == "-I" + os.path.join(repo, "src"):
        flag = "-I" + os.path.join(root, "src")
    elif flag == os.path.join(repo, "src/buster/apps/ide/ide.c"):
        flag = os.path.join(root, "src/buster/apps/ide/ide.c")
    elif flag == "-march=native":
        flag = "-march=x86-64-v3"
    elif flag == "-O3":
        flag = "-O1"
    rewritten.append(flag)
rewritten[rewritten.index("-o") + 1] = out + ".o"
subprocess.check_call(rewritten + ["-g", "-fno-inline"], cwd=os.path.join(repo, "build"))
subprocess.check_call(["clang", out + ".o", "-lm", "-o", out])
print("built", out)
