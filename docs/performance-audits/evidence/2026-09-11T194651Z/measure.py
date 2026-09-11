#!/usr/bin/env python3
"""Paired diagnostic compilation and generated-kernel runtime, no timed builds."""
import csv, hashlib, json, os, pathlib, resource, statistics, subprocess, sys, time
root = pathlib.Path(__file__).resolve().parents[4]
source_evidence = pathlib.Path(__file__).resolve().parent
evidence = pathlib.Path(sys.argv[4]).resolve() if len(sys.argv) > 4 else source_evidence
evidence.mkdir(exist_ok=True)
out = pathlib.Path(sys.argv[1]).resolve()
out.mkdir()
binaries = {"baseline": pathlib.Path(sys.argv[2]).resolve(), "candidate": pathlib.Path(sys.argv[3]).resolve()}
fixture = root / "tests/basic_c_predicate_bank.c"
body = fixture.read_text()
header, functions = body.split("void predicate_mask_chain", 1)
functions = "void predicate_mask_chain" + functions
scaled = out / "predicate-1024-functions.c"
scaled.write_text(header + "\n".join(functions.replace("predicate_mask_chain", "predicate_mask_chain_" + str(i)).replace("predicate_word_boundary", "predicate_word_boundary_" + str(i)) for i in range(512)))
commands = []
samples = []
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def run(argv):
    commands.append([str(x) for x in argv])
    result = subprocess.run(argv, cwd=root, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return result.stdout
def timed(argv):
    commands.append([str(x) for x in argv])
    before = time.perf_counter_ns()
    process = subprocess.Popen(argv, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pid, status, usage = os.wait4(process.pid, 0)
    process.returncode = os.waitstatus_to_exitcode(status)
    assert process.returncode == 0, (argv, status)
    return {"wall_ns": time.perf_counter_ns() - before, "cpu_seconds": usage.ru_utime + usage.ru_stime, "peak_rss_kib": usage.ru_maxrss}
for mode in ("mir-stack", "fast", "quality"):
    kernels = {}
    for variant, binary in binaries.items():
        obj = out / (variant + "-" + mode + ".o")
        run([binary, "cc", "-c", "-g0", "-O0", "-march=znver5", "-fregister-allocator=" + mode, "-fno-machine-fallback", fixture, "-o", obj])
        kernels[variant] = out / (variant + "-" + mode)
        run(["clang", "-O3", source_evidence / "runtime.c", obj, "-o", kernels[variant]])
        assembly = run(["objdump", "-d", obj])
        evidence.joinpath(variant + "-" + mode + ".asm").write_text(assembly)
        run([kernels[variant]])
        timed([binary, "cc", "-c", "-g0", "-O0", "-march=znver5", "-fregister-allocator=" + mode, "-fno-machine-fallback", scaled, "-o", out / "scaled.o"])
    for pair in range(8):
        for variant in (("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline")):
            binary = binaries[variant]
            observation = timed([binary, "cc", "-c", "-g0", "-O0", "-march=znver5", "-fregister-allocator=" + mode, "-fno-machine-fallback", scaled, "-o", out / "scaled.o"])
            observation.update(workload="compile_1024_functions", mode=mode, pair=pair, variant=variant, output_bytes=(out / "scaled.o").stat().st_size)
            samples.append(observation)
            measured = json.loads(run([kernels[variant]]))
            samples.append(dict(workload="kernel_10000000_calls", mode=mode, pair=pair, variant=variant, wall_ns=measured["elapsed_ns"], checksum=measured["checksum"]))
keys = sorted({key for row in samples for key in row})
with evidence.joinpath("predicate-samples.csv").open("w") as file:
    writer = csv.DictWriter(file, keys); writer.writeheader(); writer.writerows(samples)
metadata = {"baseline": {"binary": str(binaries["baseline"]), "sha256": sha(binaries["baseline"]), "bytes": binaries["baseline"].stat().st_size}, "candidate": {"binary": str(binaries["candidate"]), "sha256": sha(binaries["candidate"]), "bytes": binaries["candidate"].stat().st_size}, "fixture_sha256": sha(fixture), "scaled_source_sha256": sha(scaled), "scaled_source_bytes": scaled.stat().st_size, "functions": 1024, "cpu": run(["lscpu"]), "clang": run(["clang", "--version"]), "platform": run(["uname", "-a"]), "affinity": sorted(os.sched_getaffinity(0))}
evidence.joinpath("predicate-metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
evidence.joinpath("predicate-commands.json").write_text(json.dumps(commands, indent=2) + "\n")
summary = []
for workload in ("compile_1024_functions", "kernel_10000000_calls"):
    for mode in ("mir-stack", "fast", "quality"):
        values = {variant: [row["wall_ns"] for row in samples if row["workload"] == workload and row["mode"] == mode and row["variant"] == variant] for variant in binaries}
        summary.append({"workload": workload, "mode": mode, "baseline_median_ns": statistics.median(values["baseline"]), "candidate_median_ns": statistics.median(values["candidate"])})
evidence.joinpath("predicate-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, indent=2))
