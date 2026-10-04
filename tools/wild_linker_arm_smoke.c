#define main wild_parent_campaign_main
#include "wild_linker_bench.c"
#undef main

int main(int argc, char** argv)
{
    int result = 2;
    if (argc == 3)
    {
        driver = argv[1];
        evidence = argv[2];
        raw = fopen(format("%s/raw.csv", evidence), "wx");
        commands = fopen(format("%s/commands.tsv", evidence), "wx");
        summary = fopen(format("%s/summary.csv", evidence), "wx");
        if (!raw || !commands || !summary) fail("fresh files");
        fprintf(raw, "sequence,cell,variant,round,exit,wall_ms,user_ms,system_ms,maxrss_kib,minor_faults,major_faults\n");
        self_test();
        smoke();
        for (unsigned i = 0; i < 2; ++i)
        {
            BenchResult check = run_command("arm-smoke-full-dwarf", i ? "wild" : "mold", -1,
                format("llvm-dwarfdump --verify %s", quote(format("%s/smoke-%s", evidence, i ? "wild" : "mold"))));
            if (check.status) ++failures;
        }
        int fixture_failures = failures;
        BenchResult config = run_command("arm-driver-WILD-selection", "WILD", -1,
            format("%s generate --build-directory build-wild-arm --cc clang --linker WILD --no-include-tests --no-fuzz --no-sanitize --no-lto -- -DBUSTER_UNITY_BUILD=OFF -DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-gc-sections", quote(driver)));
        int build_status = -1;
        if (!config.status)
        {
            BenchResult build = run_command("arm-real-ide-build", "WILD", -1,
                format("%s build --build-directory build-wild-arm --config Debug -t ide", quote(driver)));
            build_status = build.status;
            if (!build.status)
            {
                must("arm-actual-link-command", "ninja -C build-wild-arm -f build-Debug.ninja -t commands ide");
                must("arm-actual-elf", "readelf -W -h -l build-wild-arm/Debug/ide");
                must("arm-actual-full-dwarf", "llvm-dwarfdump --verify build-wild-arm/Debug/ide");
                BenchResult runtime = run_command("arm-ide-object-emission", "WILD", -1,
                    format("build-wild-arm/Debug/ide cc -g -c %s -o %s", quote(format("%s/probe.c", evidence)), quote(format("%s/arm-produced.o", evidence))));
                interoperability("build-wild-arm/Debug/ide");
                BenchResult debugger = run_command("arm-actual-ide-source-and-unwind", "WILD", -1,
                    format("gdb -q -batch -ex 'break entry_point' -ex run -ex bt --args build-wild-arm/Debug/ide cc -g -c %s -o %s", quote(format("%s/probe.c", evidence)), quote(format("%s/gdb-object.o", evidence))));
                const char* stack = read_text(debugger.log);
                if (debugger.status || !strstr(stack, "entry_point") || !strstr(stack, "buster_entry_point")) ++failures;
                write_text(format("%s/arm-object-status.txt", evidence), format("object_emission_exit=%d; emission alone does not qualify native/external ARM linking.\n", runtime.status));
            }
        }
        write_text(format("%s/completion.txt", evidence), format("arm_fixture_failures=%d\narm_configure_exit=%d\narm_build_exit=%d\nNo ARM benchmark or native-linker replacement. Full Buster ARM support requires successful build/runtime checks; preserve unsupported diagnostics.\n", fixture_failures, config.status, build_status));
        if (fclose(raw) || fclose(commands) || fclose(summary)) fail("close files");
        result = failures || config.status || build_status ? 1 : 0;
    }
    else fprintf(stderr, "usage: %s DRIVER EVIDENCE\n", argv[0]);
    return result;
}
