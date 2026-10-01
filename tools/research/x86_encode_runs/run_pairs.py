#!/usr/bin/env python3
"""Pair complete object compiles and retain raw wall/PMU/output evidence."""

import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=15)
    parser.add_argument("compiler_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.compiler_args or args.compiler_args[0] != "--":
        parser.error("pass compiler arguments after --")
    args.compiler_args.pop(0)
    args.output.mkdir(parents=True, exist_ok=True)
    binaries = {"A": args.baseline.resolve(), "B": args.candidate.resolve()}
    result = {"baseline_sha256": sha256(binaries["A"]),
              "candidate_sha256": sha256(binaries["B"]),
              "compiler_args": args.compiler_args, "repetitions": args.repetitions,
              "runs": []}
    artifact = args.output / "artifact.o"
    for repetition in range(-2, args.repetitions):
        order = "AB" if repetition % 2 == 0 else "BA"
        for arm in order:
            binary = binaries[arm]
            perf_output = args.output / "perf.txt"
            command = ["perf", "stat", "-x,", "-e", "cycles:u,instructions:u",
                       "-o", str(perf_output), "--", str(binary), *args.compiler_args,
                       "-o", str(artifact)]
            start = time.perf_counter_ns()
            completed = subprocess.run(command, capture_output=True, text=True)
            wall_ns = time.perf_counter_ns() - start
            record = {"repetition": repetition, "arm": arm, "argv": command,
                      "exit_code": completed.returncode, "wall_ns": wall_ns,
                      "artifact_sha256": sha256(artifact) if artifact.exists() else None,
                      "stdout": completed.stdout, "stderr": completed.stderr,
                      "perf": perf_output.read_text() if perf_output.exists() else None}
            result["runs"].append(record)
            if completed.returncode:
                (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
                raise SystemExit(f"{arm} failed on repetition {repetition}")
            if len({run["artifact_sha256"] for run in result["runs"]}) != 1:
                (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
                raise SystemExit("output bytes differ")
    (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
