#!/usr/bin/env python3
"""Focused Linux-host regression against the repository's real C implementation.

Requires a host C compiler. This does not replace test_all or Release self-host.
Set CC=gcc, AUDIT_OPT=-O0, or AUDIT_SANITIZE=1 to vary the host configuration.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def build(source: str, out: Path) -> None:
    compiler = shlex.split(os.environ.get("CC", "clang"))
    flags = [os.environ.get("AUDIT_OPT", "-O2"), "-g", "-fwrapv", "-funsigned-char",
             "-Wno-unused-function", "-ffunction-sections", "-fdata-sections", "-I", str(ROOT / "src")]
    if os.environ.get("AUDIT_SANITIZE") == "1":
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    subprocess.run(compiler + flags + [str(ROOT / "tests" / source), "-Wl,--gc-sections",
                                      "-pthread", "-lm", "-o", str(out)], check=True)


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="buster-ebpf_scalar_regression-") as directory:
        binary = Path(directory) / "probe"
        build("ebpf_scalar_regression.c", binary)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
