#!/usr/bin/env python3
"""Small corruption gate for the deterministic A64 semantic generator."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools/generate_aarch64_control_semantics.py"
SOURCE = ROOT / "src/buster/lib/compiler/assembly/generated/arm-a64-canonical.generated.jsonl"
JSONL = ROOT / "src/buster/lib/compiler/assembly/generated/aarch64-control-semantics.generated.jsonl"
MANIFEST = ROOT / "src/buster/lib/compiler/assembly/generated/aarch64-control-semantics-manifest.json"
HEADER = ROOT / "src/buster/lib/compiler/assembly/generated/aarch64-control-semantics.generated.h"


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="aarch64-control-semantics-") as directory:
        root = Path(directory)
        jsonl = root / JSONL.name
        manifest = root / MANIFEST.name
        header = root / HEADER.name
        jsonl.write_bytes(JSONL.read_bytes())
        manifest.write_bytes(MANIFEST.read_bytes())
        header.write_bytes(HEADER.read_bytes())
        clean = ["python3", str(GENERATOR), "--check", "--source", str(SOURCE), "--jsonl", str(jsonl), "--manifest", str(manifest), "--header", str(header)]
        subprocess.run(clean, check=True)
        corrupted = jsonl.read_text(encoding="utf-8").replace('"digest":"0xd7d70d13c8dc068d"', '"digest":"0x0000000000000000"', 1)
        jsonl.write_text(corrupted, encoding="utf-8")
        if subprocess.run(clean, check=False).returncode == 0:
            raise AssertionError("--check accepted a corrupted generated JSONL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
