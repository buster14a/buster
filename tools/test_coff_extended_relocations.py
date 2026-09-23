#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


RELOCATION_COUNT = 65536
EXPECTED_LAST_OFFSET = (RELOCATION_COUNT - 1) * 8


def run(command, cwd, env=None, log_path=None):
    command = [str(part) for part in command]
    print("+ " + subprocess.list2cmdline(command), flush=True)
    if log_path:
        with Path(log_path).open("wb") as log:
            completed = subprocess.run(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT)
    else:
        completed = subprocess.run(command, cwd=cwd, env=env)
    if completed.returncode:
        if log_path:
            try:
                lines = Path(log_path).read_text(encoding="utf-8", errors="replace").splitlines()
                print("\n".join(lines[-80:]), file=sys.stderr)
            except OSError:
                pass
        raise SystemExit("command failed with exit code " + str(completed.returncode) + ": " + command[0])


def check_extended_coff(path):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    offsets = [
        int(offset, 16)
        for offset in re.findall(
            r"^\s*0x([0-9a-fA-F]+)\s+IMAGE_REL_AMD64_ADDR64\s+relocation_marker\b",
            text,
            re.MULTILINE,
        )
    ]
    if len(offsets) != RELOCATION_COUNT:
        raise SystemExit(str(path) + ": expected 65,536 independently decoded relocations, got " + str(len(offsets)))
    if offsets[0] != 0 or offsets[-1] != EXPECTED_LAST_OFFSET:
        raise SystemExit(str(path) + ": first/last relocation offsets do not match the fixture")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ide", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    ide = Path(args.ide)
    if not ide.is_absolute():
        ide = repo / ide
    ide = ide.resolve()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)

    for tool in ("clang", "llvm-readobj", "lld-link"):
        if not shutil.which(tool):
            raise SystemExit("required tool not found on PATH: " + tool)

    clang_input = output / "clang-extended-relocations.obj"
    run(
        ["clang", "--target=x86_64-pc-windows-msvc", "-g0", "-c",
         repo / "tests/coff_extended_relocations_clang.s", "-o", clang_input],
        cwd=repo,
    )
    clang_readobj = output / "clang-extended-relocations.readobj.txt"
    run(["llvm-readobj", "--file-headers", "--sections", "--relocations", clang_input],
            cwd=repo, log_path=clang_readobj)
    check_extended_coff(clang_readobj)

    env = os.environ.copy()
    env["BUSTER_TEST_COFF_RELOCATION_FIXTURE"] = str(clang_input)
    test_log = output / "ide-test.log"
    run([ide, "test", "--verbose=1", "--ci=1"], cwd=repo, env=env, log_path=test_log)

    buster_object = output / "buster-extended-relocations.obj"
    run(
        [ide, "cc", "--target=x86_64-pc-windows-msvc", "-g0", "-O0", "-c",
         repo / "tests/coff_extended_relocations_subject.c", "-o", buster_object],
        cwd=repo,
    )
    support_object = output / "relocation-support.obj"
    run(
        ["clang", "--target=x86_64-pc-windows-msvc", "-g0", "-O0", "-c",
         repo / "tests/coff_extended_relocations_support.c", "-o", support_object],
        cwd=repo,
    )
    buster_readobj = output / "buster-extended-relocations.readobj.txt"
    run(["llvm-readobj", "--file-headers", "--sections", "--relocations", buster_object],
            cwd=repo, log_path=buster_readobj)
    check_extended_coff(buster_readobj)

    executable = output / "coff-relocation-final-entry.exe"
    run(
        ["lld-link", "/entry:main", "/subsystem:console", "/nodefaultlib", "/machine:x64",
         "/out:" + str(executable), buster_object, support_object],
        cwd=repo,
    )
    run([executable], cwd=output)
    print("COFF_EXTENDED_RELOCATIONS clang_reader=pass llvm_reader=pass lld_link=pass final_relocation=pass")


if __name__ == "__main__":
    main()
