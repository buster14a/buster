#!/usr/bin/env python3
"""Compile the unity ide.c of SOURCE_ROOT with the exact Release command recorded in a
tests-off compile_commands.json (configured with -DBUSTER_INCLUDE_TESTS=OFF), then link.

usage: build_variant.py SOURCE_ROOT OUT [--no-werror] [extra compiler flags...]
env:   BUSTER_REPO              configured checkout whose build/ holds the generated headers (default: cwd)
       BUSTER_COMPILE_COMMANDS  tests-off compile_commands.json (default: $BUSTER_REPO/build/compile_commands.json)

The native variants of the audit (ide-base, ide-proto, ide-probe, ide-proto-probe) were built this
way; the probe variants first apply probe_frontend.py and probe_fast_*.py to a copy of the tree and
pass --no-werror. Research material, not build infrastructure."""
import json, os, shlex, subprocess, sys

repo = os.path.abspath(os.environ.get("BUSTER_REPO", os.getcwd()))
commands = os.environ.get("BUSTER_COMPILE_COMMANDS", os.path.join(repo, "build", "compile_commands.json"))
root, out, extra = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), sys.argv[3:]
command = None
for entry in json.load(open(commands)):
    if entry["file"].endswith("apps/ide/ide.c") and "Release" in entry["command"]:
        command = shlex.split(entry["command"])
        break
if command is None:
    sys.exit("no Release ide.c entry")
drop = {"-Werror"} if "--no-werror" in extra else set()
extra = [flag for flag in extra if flag != "--no-werror"]
rewritten = []
for flag in command:
    if flag == "-I" + os.path.join(repo, "src"):
        flag = "-I" + os.path.join(root, "src")
    elif flag == os.path.join(repo, "src/buster/apps/ide/ide.c"):
        flag = os.path.join(root, "src/buster/apps/ide/ide.c")
    if flag not in drop:
        rewritten.append(flag)
rewritten[rewritten.index("-o") + 1] = out + ".o"
subprocess.check_call(rewritten + extra, cwd=os.path.join(repo, "build"))
subprocess.check_call(["clang", "-O3", out + ".o", "-lm", "-o", out])
print("built", out)
