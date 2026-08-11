#!/usr/bin/env python3
"""Adversarial checks for the pinned A64 syntax importer."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src/buster/lib/compiler/assembly/generated/arm-a64-canonical.generated.jsonl"
IMPORTER = ROOT / "tools/aarch64_syntax_import.py"


def run(source: Path, output: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(IMPORTER), str(source), str(output)],
                          text=True, capture_output=True, check=False)


def expect_rejected(name: str, mutate) -> None:
    with tempfile.TemporaryDirectory(prefix=f"aarch64-syntax-{name}-") as directory:
        directory_path = Path(directory)
        source = directory_path / SOURCE.name
        source.write_bytes(SOURCE.read_bytes())
        mutate(source)
        result = run(source, directory_path / "generated")
        if result.returncode == 0:
            raise SystemExit(f"mutation unexpectedly accepted: {name}")


def mutate_content(path: Path) -> None:
    data = path.read_bytes()
    path.write_bytes(data.replace(b'"assembly":"ABS <Vd>', b'"assembly":"ABX <Vd>', 1))


def mutate_duplicate(path: Path) -> None:
    lines = path.read_bytes().splitlines(keepends=True)
    path.write_bytes(b"".join(lines + [lines[0]]))


def mutate_missing(path: Path) -> None:
    lines = path.read_bytes().splitlines(keepends=True)
    path.write_bytes(b"".join(lines[1:]))


def mutate_kind(path: Path) -> None:
    data = path.read_bytes()
    path.write_bytes(data.replace(b'"kind":"canonical"', b'"kind":"alias"', 1))


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="aarch64-syntax-valid-") as directory:
        output = Path(directory) / "generated"
        first = run(SOURCE, output)
        if first.returncode != 0:
            raise SystemExit(first.stderr or first.stdout)
        for artifact in output.iterdir():
            if b"\r" in artifact.read_bytes():
                raise SystemExit(f"non-LF artifact: {artifact.name}")
        second = subprocess.run([sys.executable, str(IMPORTER), "--check", str(SOURCE), str(output)],
                                text=True, capture_output=True, check=False)
        if second.returncode != 0:
            raise SystemExit(second.stderr or second.stdout)
    expect_rejected("content", mutate_content)
    expect_rejected("duplicate", mutate_duplicate)
    expect_rejected("missing", mutate_missing)
    expect_rejected("kind", mutate_kind)
    print("valid source and content/duplicate/missing/kind mutations verified")


if __name__ == "__main__":
    main()
