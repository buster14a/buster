#!/usr/bin/env python3
# Closing-report measurement for #56: counters once per cell, then alternating
# base/head CPU-time pairs on the same frozen unity ide.c input.
import json, os, re, resource, subprocess, sys, time, math, statistics

S = os.path.dirname(os.path.abspath(__file__))
FROZEN = os.path.join(S, "frozen")
OUT = os.path.join(S, "out")
COMPILERS = {"base": os.path.join(S, "wt-base/build/Release/ide"), "head": os.path.join(S, "wt-head/build/Release/ide")}
TARGETS = {
    "x86_64": ["-march=native"],
    "aarch64": ["-target", "aarch64-linux", "-isystem", "/usr/aarch64-linux-gnu/include"],
}
MODES = ["fast", "quality"]
COMMON = ["-Isrc", "-Igenerated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-g0"]


def command(compiler, target, mode, obj, verbose):
    c = [COMPILERS[compiler], "cc"] + COMMON + TARGETS[target] + ["-fregister-allocator=" + mode, "-c", "src/buster/apps/ide/ide.c", "-o", obj]
    if verbose:
        c.append("-v")
    return c


def run(cmd):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.monotonic()
    p = subprocess.run(cmd, cwd=FROZEN, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    wall = time.monotonic() - t0
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if p.returncode != 0:
        sys.exit("failed: %s\n%s" % (" ".join(cmd), p.stdout[-4000:]))
    return p.stdout, (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime), wall


def fields(text, prefix):
    for line in text.splitlines():
        if line.startswith(prefix + " "):
            return {k: v for k, v in re.findall(r"(\w+)=(\S+)", line)}
    return {}


def counters():
    os.makedirs(OUT, exist_ok=True)
    result = {}
    for target in TARGETS:
        for mode in MODES:
            for compiler in COMPILERS:
                key = "%s/%s/%s" % (target, mode, compiler)
                obj = os.path.join(OUT, "%s-%s-%s.o" % (target, mode, compiler))
                text, cpu, wall = run(command(compiler, target, mode, obj, True))
                with open(obj + ".log", "w") as f:
                    f.write(text)
                cg = fields(text, "CODEGEN")
                al = fields(text, "CODEGEN_ALLOCATOR")
                em = fields(text, "CODEGEN_EMIT")
                fast = fields(text, "IR_FAST")
                result[key] = {
                    "ir_values": int(cg["values"]), "ir_instructions": int(cg["instructions"]),
                    "ir_fast_before": int(fast.get("instructions_before", 0)), "ir_fast_after": int(fast.get("instructions_after", 0)),
                    "code_bytes": int(cg["code_bytes"]), "fallback_functions": int(cg["fallback_functions"]),
                    "emit_in_place": int(em.get("machine_code_in_place", 0)), "emit_copied": int(em.get("machine_code_copied", 0)),
                    "object_bytes": os.path.getsize(obj),
                    **{k: int(v) for k, v in al.items()},
                }
                print(key, json.dumps(result[key]), flush=True)
    with open(os.path.join(OUT, "counters.json"), "w") as f:
        json.dump(result, f, indent=1, sort_keys=True)


def timing(pairs):
    samples = {}
    for target in TARGETS:
        for mode in MODES:
            cell = "%s/%s" % (target, mode)
            samples[cell] = {"base": [], "head": []}
            run(command("base", target, mode, os.devnull, False))  # warm page cache
            run(command("head", target, mode, os.devnull, False))
            for i in range(pairs):
                order = ["base", "head"] if i % 2 == 0 else ["head", "base"]
                for compiler in order:
                    _, cpu, wall = run(command(compiler, target, mode, os.devnull, False))
                    samples[cell][compiler].append([cpu, wall])
            ratios = [math.log(h[0] / b[0]) for b, h in zip(samples[cell]["base"], samples[cell]["head"])]
            m = statistics.mean(ratios)
            se = statistics.stdev(ratios) / math.sqrt(len(ratios))
            t = 2.262 if pairs == 10 else 2.0
            print(cell, "cpu head/base %.4f [%.4f, %.4f]" % (math.exp(m), math.exp(m - t * se), math.exp(m + t * se)),
                  "median base %.3f head %.3f" % (statistics.median(x[0] for x in samples[cell]["base"]), statistics.median(x[0] for x in samples[cell]["head"])), flush=True)
            with open(os.path.join(OUT, "timing.json"), "w") as f:
                json.dump(samples, f, indent=1)


if __name__ == "__main__":
    if sys.argv[1] == "counters":
        counters()
    else:
        timing(int(sys.argv[2]))
