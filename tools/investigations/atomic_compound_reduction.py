#!/usr/bin/env python3
"""Second-stage evidence; original compiler reused by verified artifact hash."""
import json
import os
from pathlib import Path
import sys
from atomic_compound_probe import Collector, OUT


def main():
    c = Collector()
    ide = str(Path("build/Release/ide").resolve())
    common = ["-std=gnu17", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-g0"]
    for command in (["clang", "--version"], ["gcc", "--version"], ["uname", "-a"], ["git", "rev-parse", "HEAD", "HEAD^{tree}"], ["sha256sum", ide]):
        c.run(command)
    cases = {
        "integer_fraction_min": "_Atomic int value = -2; value += 1.5; return value != 0;",
        "float_precision_min": "_Atomic float value = 1.0f; value -= 0x1.000001p0; return value != -0x1p-24f;",
        "byte_add_min": "_Atomic unsigned char value = 255; int observed = (value += 1); return (observed != 0) | ((value != 0) << 1);",
        "short_add_min": "_Atomic unsigned short value = 65535; int observed = (value += 1); return (observed != 0) | ((value != 0) << 1);",
        "byte_sub_min": "_Atomic unsigned char value = 0; int observed = (value -= 1); return (observed != 255) | ((value != 255) << 1);",
        "short_sub_min": "_Atomic unsigned short value = 0; int observed = (value -= 1); return (observed != 65535) | ((value != 65535) << 1);",
        "byte_preinc_min": "_Atomic unsigned char value = 255; int observed = ++value; return (observed != 0) | ((value != 0) << 1);",
        "byte_postinc_control": "_Atomic unsigned char value = 255; int observed = value++; return (observed != 255) | ((value != 0) << 1);",
        "byte_predec_min": "_Atomic unsigned char value = 0; int observed = --value; return (observed != 255) | ((value != 255) << 1);",
        "byte_postdec_control": "_Atomic unsigned char value = 0; int observed = value--; return (observed != 0) | ((value != 255) << 1);",
        "byte_cast_control": "_Atomic unsigned char value = 255; int observed = (unsigned char)(value += 1); return (observed != 0) | ((value != 0) << 1);",
        "byte_nonwrap_control": "_Atomic unsigned char value = 42; int observed = (value += 1); return (observed != 43) | ((value != 43) << 1);",
        "ordinary_byte_control": "unsigned char value = 255; int observed = (value += 1); return (observed != 0) | ((value != 0) << 1);",
        "atomic_int_control": "_Atomic int value = -2; int observed = (value += 1); return (observed != -1) | ((value != -1) << 1);",
        "atomic_float_same_control": "_Atomic float value = 1.0f; float observed = (value -= 0.5f); return (observed != 0.5f) | ((value != 0.5f) << 1);",
        "atomic_double_control": "_Atomic double value = 1.0; double observed = (value -= 0x1.000001p0); return (observed != -0x1p-24) | ((value != -0x1p-24) << 1);",
        "explicit_rhs_cast_control": "_Atomic int value = -2; int observed = (value += (int)1.5); return (observed != -1) | ((value != -1) << 1);",
        "pointer_scale_control": "int objects[4] = {0}; _Atomic(int *) pointer = objects; int *observed = (pointer += 2); return (observed != objects + 2) | ((pointer != objects + 2) << 1);",
        "byte_index_once": "_Atomic unsigned char values[2] = {255, 42}; unsigned index = 0; int observed = (values[index++] += 1); return (observed != 0) | ((values[0] != 0) << 1) | ((values[1] != 42) << 2) | ((index != 1) << 3);",
        "float_index_once": "_Atomic float values[2] = {1.0f, 42.0f}; unsigned index = 0; double observed = (values[index++] -= 0x1.000001p0); return (observed != -0x1p-24) | ((values[0] != -0x1p-24f) << 1) | ((values[1] != 42.0f) << 2) | ((index != 1) << 3);",
    }
    profiles = []
    for cc, target in (("clang", ["--target=x86_64-linux-gnu"]), ("gcc", ["-m64"])):
        for optimization in (["-O0"], ["-O2"], ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]):
            profiles.append(([cc], common + target + optimization + ["-latomic"]))
    for allocator in ("none", "mir-stack", "fast", "quality"):
        for frontend in ("-ffrontend-ssa", "-fno-frontend-ssa"):
            for optimization in ("-O0", "-O2"):
                flags = common + ["-target", "x86_64-linux", optimization, "-fverify-codegen", "-fregister-allocator=" + allocator, frontend]
                if allocator != "none":
                    flags.append("-fno-machine-fallback")
                profiles.append(([ide, "cc"], flags))
    for name, body in cases.items():
        source = "int main(void) {\n    " + body.replace("; ", ";\n    ") + "\n}\n"
        for compiler, flags in profiles:
            c.case(name, source, compiler, flags)
        if name in ("integer_fraction_min", "float_precision_min", "byte_add_min"):
            for compiler, flags in profiles:
                if (len(compiler) == 1 and "-O0" in flags) or (len(compiler) == 2 and "-fregister-allocator=fast" in flags and "-O0" in flags):
                    c.case(name + "_strict_c17", source, compiler, ["-std=c17" if flag == "-std=gnu17" else flag for flag in flags])
    # Retain the original failed GCC setup. Rerun its exact archived inputs with
    # the libatomic runtime that GCC emits for atomic floating-point exception handling.
    for path in sorted((Path(os.environ["BASELINE_OUT"]) / "inputs").glob("*.c")):
        for optimization in (["-O0"], ["-O2"], ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]):
            c.case("gcc_runtime_repair_" + path.stem, path.read_text(), ["gcc"], common + ["-m64"] + optimization + ["-latomic"])
    ir_source = """int integer_add(_Atomic int *p, double rhs) { return *p += rhs; }
float floating_sub(_Atomic float *p, double rhs) { return *p -= rhs; }
int byte_add(_Atomic unsigned char *p, unsigned char rhs) { return *p += rhs; }
"""
    ir_path = OUT / "inputs" / "lowering.c"
    ir_path.write_text(ir_source)
    observations = []
    for target, clang_target in (("x86_64-linux", "x86_64-linux-gnu"), ("aarch64-linux", "aarch64-linux-gnu")):
        for frontend in ("-ffrontend-ssa", "-fno-frontend-ssa"):
            stem = target + frontend
            bc = OUT / (stem + ".bc")
            generated = c.run([ide, "cc", "-target", target, "-std=c17", "-O0", "-g0", frontend, "-fverify-codegen", "-emit-llvm", str(ir_path), "-o", str(bc)])
            decoded = c.run(["clang", "--target=" + clang_target, "-S", "-emit-llvm", "-O0", str(bc), "-o", str(OUT / (stem + ".ll"))]) if generated["exit"] == 0 else None
            observations.append({"kind": "buster_bitcode", "target": target, "frontend": frontend, "generate": generated["id"], "generate_exit": generated["exit"], "decode": decoded["id"] if decoded else None, "decode_exit": decoded["exit"] if decoded else None})
        generated = c.run(["clang", "--target=" + clang_target, "-std=c17", "-S", "-emit-llvm", "-O0", str(ir_path), "-o", str(OUT / (target + "-clang.ll"))])
        observations.append({"kind": "clang_ir", "target": target, "generate": generated["id"], "generate_exit": generated["exit"]})
    observer = '''int printf(const char *, ...);
int main(void) {
    _Atomic int integer = -2;
    _Atomic float floating = 1.0f;
    _Atomic unsigned char byte = 255;
    int integer_result = (integer += 1.5);
    double floating_result = (floating -= 0x1.000001p0);
    int byte_result = (byte += 1);
    printf("integer=%d result=%d floating=%a result=%a byte=%u result=%d\\n", (int)integer, integer_result, (double)floating, floating_result, (unsigned)byte, byte_result);
    return 0;
}
'''
    observer_path = OUT / "inputs" / "observer.c"
    observer_path.write_text(observer)
    for compiler, flags in ((["clang"], common + ["-O0", "-latomic"]), (["gcc"], common + ["-m64", "-O0", "-latomic"]), ([ide, "cc"], common + ["-target", "x86_64-linux", "-O0", "-fregister-allocator=fast", "-ffrontend-ssa", "-fverify-codegen", "-fno-machine-fallback"])):
        binary = OUT / "binaries" / ("observer-" + ("buster" if len(compiler) == 2 else compiler[0]))
        generated = c.run(compiler + flags + [str(observer_path), "-o", str(binary)])
        executed = c.run([str(binary)]) if generated["exit"] == 0 else None
        observations.append({"kind": "observer", "compiler": compiler, "generate": generated["id"], "generate_exit": generated["exit"], "run": executed["id"] if executed else None, "run_exit": executed["exit"] if executed else None})
    (OUT / "observations.json").write_text(json.dumps(observations, indent=2) + "\n")
    # A TSV is durable, compact raw evidence in addition to the command JSONL.
    with (OUT / "results.tsv").open("w") as stream:
        stream.write("case\tcompiler\tflags\tcompile_command\tcompile_exit\trun_command\trun_exit\n")
        for r in c.outcomes:
            stream.write("\t".join(str(x) for x in (r["case"], " ".join(r["compiler"]), " ".join(r["flags"]), r["compile_command"], r["compile_exit"], r["run_command"], r["run_exit"])) + "\n")
    print("OBSERVATIONS", json.dumps(observations), flush=True)
    print("OUTCOME_COUNT", len(c.outcomes), flush=True)
    failed = any(r["compile_exit"] != 0 or r["run_exit"] != 0 or r["compile_timeout"] or r["run_timeout"] for r in c.outcomes)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
