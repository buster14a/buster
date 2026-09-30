#!/usr/bin/env python3
"""Differential object/diagnostic comparison of two frozen `ide` binaries.

Compiles every tests/*.c input with both compilers under several option sets and
compares exit status, stdout/stderr text and output object bytes. Diagnostic
correctness tool only: it measures nothing about speed.
"""
import concurrent.futures
import hashlib
import json
import os
import subprocess
import sys

REPOSITORY = "/home/user/buster"
MODES = [
    ["-g", "-c"],
    ["-g0", "-c"],
    ["-g", "-fno-frontend-ssa", "-c"],
    ["-g", "-target", "aarch64-unknown-linux-gnu", "-c"],
    ["-g", "-target", "x86_64-pc-windows-msvc", "-c"],
    ["-g", "-fregister-allocator=none", "-c"],
    ["-g", "-fregister-allocator=quality", "-c"],
]


def run(compiler, source, mode, output):
    command = [compiler, "cc", "-Itests", "-Isrc"] + mode + [source, "-o", output]
    completed = subprocess.run(command, cwd=REPOSITORY, capture_output=True, timeout=300)
    digest = ""
    if os.path.exists(output):
        with open(output, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()
        os.unlink(output)
    text = (completed.stdout + completed.stderr).decode("utf-8", "replace").replace(output, "<out>")
    return completed.returncode, digest, text


def compare(job):
    base, candidate, work, source, mode_index = job
    mode = MODES[mode_index]
    stem = os.path.basename(source).replace(".c", "")
    a = run(base, source, mode, os.path.join(work, f"{stem}.{mode_index}.a.o"))
    b = run(candidate, source, mode, os.path.join(work, f"{stem}.{mode_index}.b.o"))
    return {"source": source, "mode": " ".join(mode), "base_status": a[0], "candidate_status": b[0],
            "same_status": a[0] == b[0], "same_object": a[1] == b[1], "same_text": a[2] == b[2],
            "object": a[1], "has_object": bool(a[1])}


def main():
    base, candidate, work, report = sys.argv[1:5]
    os.makedirs(work, exist_ok=True)
    sources = sorted(os.path.join("tests", name) for name in os.listdir(os.path.join(REPOSITORY, "tests")) if name.endswith(".c"))
    jobs = [(base, candidate, work, source, mode) for source in sources for mode in range(len(MODES))]
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get("JOBS", "3"))) as pool:
        for result in pool.map(compare, jobs):
            results.append(result)
    mismatches = [r for r in results if not (r["same_status"] and r["same_object"] and r["same_text"])]
    summary = {
        "invocations_per_compiler": len(results),
        "sources": len(sources),
        "modes": [" ".join(m) for m in MODES],
        "objects_compared": sum(1 for r in results if r["has_object"]),
        "failing_both": sum(1 for r in results if r["base_status"] != 0 and r["candidate_status"] != 0),
        "mismatches": len(mismatches),
    }
    with open(report, "w") as handle:
        json.dump({"summary": summary, "mismatches": mismatches, "results": results}, handle, indent=1)
    print(json.dumps(summary, indent=1))
    for m in mismatches[:20]:
        print("MISMATCH", m["source"], m["mode"], m["base_status"], m["candidate_status"], m["same_object"], m["same_text"])


if __name__ == "__main__":
    main()
