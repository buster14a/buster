#!/usr/bin/env python3
"""Standalone confirmations. No production changes; preserve every failed attempt."""
import json
import os
from pathlib import Path
import sys
import time
from atomic_compound_probe import Collector, OUT

class BoundedCollector(Collector):
    def run(self, argv, seconds=30):
        argv = list(argv)
        # The reference runtime must follow its input for --as-needed linkers.
        if argv[0] in ("gcc", "clang") and "-latomic" in argv:
            argv.remove("-latomic")
            argv.append("-latomic")
        return super().run(argv, min(seconds, 3 if len(argv) > 1 else 1))

def main():
    c = BoundedCollector()
    started = time.monotonic()
    ide = str(Path("build/Release/ide").resolve())
    for command in (["clang", "--version"], ["gcc", "--version"], ["uname", "-a"], ["git", "rev-parse", "HEAD", "HEAD^{tree}"], ["sha256sum", ide]):
        c.run(command)
    # File-scope objects remove automatic-object initialization from the trigger.
    cases = {
        "byte_add": ("unsigned char", "255", "int observed = (value += 1);", "0", "0"),
        "byte_sub": ("unsigned char", "0", "int observed = (value -= 1);", "255", "255"),
        "short_add": ("unsigned short", "65535", "int observed = (value += 1);", "0", "0"),
        "byte_preinc": ("unsigned char", "255", "int observed = ++value;", "0", "0"),
        "byte_postinc_control": ("unsigned char", "255", "int observed = value++;", "255", "0"),
        "byte_cast_control": ("unsigned char", "255", "int observed = (unsigned char)(value += 1);", "0", "0"),
        "float_precision": ("float", "1.0f", "double observed = (value -= 0x1.000001p0);", "-0x1p-24", "-0x1p-24f"),
        "integer_fraction": ("int", "-2", "int observed = (value += 1.5);", "0", "0"),
        "byte_nonwrap_control": ("unsigned char", "41", "int observed = (value += 1);", "42", "42"),
    }
    sources = {}
    for name, (typ, initial, operation, expected, stored) in cases.items():
        sources[name] = f"static _Atomic({typ}) value = {initial};\nint main(void) {{\n    {operation}\n    return (observed != ({expected})) | ((value != ({stored})) << 1);\n}}\n"
    sources["ordinary_byte_control"] = sources["byte_add"].replace("_Atomic(unsigned char)", "unsigned char")
    flags = ["-std=c17", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-g0"]
    profiles = []
    for compiler, target in (("clang", ["--target=x86_64-linux-gnu"]), ("gcc", ["-m64"])):
        for opt in (["-O0"], ["-O2"], ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]):
            profiles.append(([compiler], flags + target + opt + ["-latomic"]))
    for allocator in ("none", "mir-stack", "fast", "quality"):
        for frontend in ("-ffrontend-ssa", "-fno-frontend-ssa"):
            options = flags + ["-target", "x86_64-linux", "-O0", "-fverify-codegen", "-fregister-allocator=" + allocator, frontend]
            if allocator != "none":
                options.append("-fno-machine-fallback")
            profiles.append(([ide, "cc"], options))
    for frontend in ("-ffrontend-ssa", "-fno-frontend-ssa"):
        profiles.append(([ide, "cc"], flags + ["-target", "x86_64-linux", "-O2", "-fverify-codegen", "-fregister-allocator=fast", frontend, "-fno-machine-fallback"]))
    unrun = []
    for name, source in sources.items():
        for compiler, options in profiles:
            if time.monotonic() - started < 150:
                c.case(name, source, compiler, options)
            else:
                unrun.append({"case": name, "compiler": compiler, "flags": options, "reason": "collector total execution budget"})
    (OUT / "unrun.json").write_text(json.dumps(unrun, indent=2) + "\n")
    observer = '''int printf(const char *, ...);
static _Atomic(unsigned char) byte = 255;
static _Atomic(float) floating = 1.0f;
static _Atomic(int) integer = -2;
int main(void) {
    int byte_result = (byte += 1);
    double floating_result = (floating -= 0x1.000001p0);
    int integer_result = (integer += 1.5);
    printf("byte=%u result=%d floating=%a result=%a integer=%d result=%d\\n", (unsigned)byte, byte_result, (double)floating, floating_result, (int)integer, integer_result);
    return 0;
}
'''
    observations = []
    path = OUT / "inputs" / "observer.c"
    path.write_text(observer)
    for compiler, options in (profiles[0], profiles[3], profiles[10]):
        binary = OUT / "binaries" / ("observer-" + ("buster" if len(compiler) == 2 else compiler[0]))
        built = c.run(compiler + options + [str(path), "-o", str(binary)])
        executed = c.run([str(binary)]) if built["exit"] == 0 else None
        observations.append({"kind": "observer", "compiler": compiler, "compile": built["id"], "run": executed["id"] if executed else None})
    (OUT / "observations.json").write_text(json.dumps(observations, indent=2) + "\n")
    with (OUT / "results.tsv").open("w") as stream:
        stream.write("case\tcompiler\tflags\tcompile_command\tcompile_exit\tcompile_timeout\trun_command\trun_exit\trun_timeout\n")
        for r in c.outcomes:
            stream.write("\t".join(str(x) for x in (r["case"], " ".join(r["compiler"]), " ".join(r["flags"]), r["compile_command"], r["compile_exit"], r["compile_timeout"], r["run_command"], r["run_exit"], r["run_timeout"])) + "\n")
    print("COMPLETED", len(c.outcomes), "UNRUN", len(unrun), "OBSERVATIONS", json.dumps(observations), flush=True)
    failed = unrun or any(r["compile_exit"] != 0 or r["run_exit"] != 0 or r["compile_timeout"] or r["run_timeout"] for r in c.outcomes)
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
