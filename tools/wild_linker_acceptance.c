/* #2645 supplementary hosted acceptance. Reuses the campaign's bounded
 * arena, process capture and paired statistics. No production source changes.
 * direct_link freezes the compiler's printed linker argv; traced_inputs pins
 * successful read opens, including linker scripts, startup and system files.
 */
#define main wild_linker_campaign_main
#include "wild_linker_bench.c"
#undef main
#include <ctype.h>

static char* linker_argv(const char* text)
{
    char* copy = format("%s", text);
    char* found = NULL;
    for (char* line = copy; *line;)
    {
        char* end = strchr(line, '\n');
        if (end) *end = 0;
        char* start = line;
        while (*start == ' ' || *start == '\t') ++start;
        if (*start == '"' && (strstr(start, "/wild\"") || strstr(start, "/ld.wild\""))) found = start;
        if (!end) break;
        line = end + 1;
    }
    if (!found) fail("Clang dry-run did not print the selected Wild linker");
    return found;
}

static char* direct_link(const char* compiler_command, const char* directory)
{
    char* stripped = format("%s", compiler_command);
    size_t length = strlen(stripped);
    if (length >= 5 && !strcmp(stripped + length - 5, " && :")) stripped[length - 5] = 0;
    BenchResult dry = must("capture-direct-linker", format("%s -###", stripped));
    char* printed = linker_argv(read_text(dry.log));
    write_text(format("%s/direct-argv-%u.txt", evidence, sequence), printed);
    return format("cd %s && %s", quote(directory), printed);
}

static char* change_linker(const char* command, const char* directory, const char* name)
{
    const char* prefix = format("cd %s && ", quote(directory));
    if (strncmp(command, prefix, strlen(prefix))) fail("direct cwd prefix");
    const char* exe = command + strlen(prefix);
    if (*exe != '"') fail("printed linker executable must be quoted");
    const char* end = strchr(exe + 1, '"');
    if (!end) fail("unterminated linker executable");
    return format("%s%s%s", prefix, quote(name), end + 1);
}

static char* frozen_command(const char* input_root, const char* cell)
{
    char* records = read_text(format("%s/commands.tsv", input_root));
    char* result = NULL;
    for (char* at = records; *at && !result;)
    {
        char* end = strchr(at, '\n');
        if (end) *end = 0;
        char* fields[5] = {at};
        unsigned count = 1;
        for (char* p = at; *p && count < 5; ++p)
        {
            if (*p == '\t')
            {
                *p = 0;
                fields[count++] = p + 1;
            }
        }
        if (count == 5 && !strcmp(fields[1], cell) && !strcmp(fields[2], "B")) result = fields[4];
        if (!end) break;
        at = end + 1;
    }
    if (!result) fail("frozen campaign command missing");
    return result;
}

static void traced_inputs(const char* cell, const char* command, const char* directory, FILE* manifest_paths)
{
    const char* trace = format("%s/%s.opens.txt", evidence, cell);
    must("trace-link-inputs", format("strace -f -qq -s 4096 -e trace=openat,openat2 -o %s sh -c %s", quote(trace), quote(command)));
    char* records = read_text(trace);
    for (char* at = records; *at;)
    {
        char* end = strchr(at, '\n');
        if (end) *end = 0;
        char* first = strchr(at, '"');
        if (first && strstr(at, "O_RDONLY") && !strstr(at, " = -1"))
        {
            char* last = strchr(first + 1, '"');
            if (last)
            {
                *last = 0;
                const char* path = first[1] == '/' ? first + 1 : format("%s/%s", directory, first + 1);
                char resolved[4096];
                struct stat st;
                if (realpath(path, resolved) && strncmp(resolved, "/proc/", 6) && strncmp(resolved, "/sys/", 5) && !stat(resolved, &st) && S_ISREG(st.st_mode))
                {
                    if (strchr(resolved, '\n') || strchr(resolved, '\\')) fail("unhandled trace filename");
                    fprintf(manifest_paths, "%s\n", resolved);
                }
            }
        }
        if (!end) break;
        at = end + 1;
    }
}

static void check_debugger(const char* cell, const char* binary, const char* breakpoint, const char* args, const char* required_a, const char* required_b)
{
    BenchResult run = must(cell, format("gdb -q -batch -ex 'set pagination off' -ex %s -ex run -ex bt --args %s %s",
         quote(format("break %s", breakpoint)), quote(binary), args));
    const char* log = read_text(run.log);
    if (!strstr(log, required_a) || !strstr(log, required_b)) fail("debugger source/unwind assertion; retained log");
}

static void direct_cell(const char* cell, const char* compiler_command, const char* directory, const char* binary, const char* runtime)
{
    const char* gc = !strcmp(cell, "hot-reload-debug") ? "--gc-sections" : "--no-gc-sections";
    char* wild = format("%s %s --no-fork --icf=none --build-id=0x2645264526452645264526452645264526452645", direct_link(compiler_command, directory), gc);
    char* mold = change_linker(wild, directory, "mold");
    const char* paths = format("%s/%s-read-paths.txt", evidence, cell);
    FILE* manifest_paths = fopen(paths, "wx");
    if (!manifest_paths) fail("fresh trace manifest");
    traced_inputs(format("%s-wild", cell), wild, directory, manifest_paths);
    traced_inputs(format("%s-mold", cell), mold, directory, manifest_paths);
    if (fclose(manifest_paths)) fail("trace manifest close");
    const char* hashes = format("%s/%s-all-read-inputs.sha256", evidence, cell);
    must("freeze-system-and-object-inputs", format("sort -u %s | xargs -d '\\n' sha256sum > %s", quote(paths), quote(hashes)));
    compare(format("%s-direct-default", cell), mold, wild, 21);
    compare(format("%s-direct-threads1", cell), format("%s --threads=1", mold), format("%s --threads=1", wild), 21);
    if (!strcmp(cell, "ide-debug"))
    {
        compare("direct-mold-aa", mold, mold, 9);
        compare("direct-wild-aa", wild, wild, 9);
    }
    const char* commands_pair[] = {mold, wild};
    const char* names[] = {"mold", "wild"};
    for (unsigned i = 0; i < 2; ++i)
    {
        must("direct-validated-link", commands_pair[i]);
        const char* saved = format("%s/%s-%s.elf", evidence, cell, names[i]);
        must("direct-output-metadata", format("cp %s %s && stat -c 'bytes=%%s' %s && sha256sum %s && size -A %s && readelf -W -h -l -S -d %s",
             quote(binary), quote(saved), quote(saved), quote(saved), quote(saved), quote(saved)));
        BenchResult dwarf = run_command("direct-dwarf-verifier", names[i], -1, format("llvm-dwarfdump --verify --error-display=summary %s", quote(saved)));
        if (dwarf.status)
        {
            if (strcmp(cell, "hot-reload-debug")) ++failures;
            else write_text(format("%s/hot-reload-%s-debug-limit.txt", evidence, names[i]), "FAIL: full DWARF range verification with required section GC; discarded-code zero-address ranges. Runtime lifecycle is a separate check. Strict full-DWARF acceptance for this target is unsupported under both tested linkers, not a PASS.\n");
        }
        must("direct-runtime", runtime);
        must("direct-repeat", commands_pair[i]);
        must("direct-repeat-byte-identity", format("cmp %s %s", quote(binary), quote(saved)));
    }
    must("verify-frozen-system-and-object-inputs", format("sha256sum --check %s", quote(hashes)));
}


static void cold_comparison(const char* cell, const char* a, const char* b)
{
    double values[2][BUSTER_BENCH_SAMPLES];
    const char* variants[] = {"A", "B"};
    const char* command[] = {a, b};
    for (unsigned round = 0; round < 9; ++round)
    {
        unsigned first = random_u32() & 1;
        for (unsigned position = 0; position < 2; ++position)
        {
            unsigned which = first ^ position;
            must("cold-guest-cache-reset", "sudo -n sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches'");
            BenchResult result = run_command(cell, variants[which], (int)round, command[which]);
            if (result.status) fail("cold direct link failed");
            values[which][round] = result.wall_ms;
        }
    }
    report(cell, values[0], values[1], 9);
}

static char* target_command(const char* directory, const char* target)
{
    BenchResult captured = must("capture-target-command", format("ninja -C %s -f build-Debug.ninja -t commands %s", quote(directory), target));
    char* text = read_text(captured.log);
    char* found = NULL;
    for (char* at = text; *at;)
    {
        char* end = strchr(at, '\n');
        if (end) *end = 0;
        if (strstr(at, "--ld-path=wild") && strstr(at, " -o ") && strstr(at, target)) found = at;
        if (!end) break;
        at = end + 1;
    }
    if (!found) fail("target link command missing");
    return format("cd %s && %s", quote(directory), found);
}

static double ninja_link_ms(const char* directory)
{
    char* text = read_text(format("%s/.ninja_log", directory));
    double result = -1;
    for (char* at = text; *at;)
    {
        char* end = strchr(at, '\n');
        if (end) *end = 0;
        unsigned long long start = 0;
        unsigned long long finish = 0;
        unsigned long long stamp = 0;
        char output[4096];
        if (sscanf(at, "%llu\t%llu\t%llu\t%4095s", &start, &finish, &stamp, output) == 4 && !strcmp(output, "Debug/ide"))
        {
            result = (double)(finish - start);
        }
        if (!end) break;
        at = end + 1;
    }
    if (result < 0) fail("Ninja link duration missing");
    return result;
}

static void build_shares(const char* directory)
{
    FILE* shares = fopen(format("%s/build-link-share.csv", evidence), "wx");
    if (!shares) fail("new link-share file");
    fprintf(shares, "kind,linker,round,end_to_end_ms,ninja_link_ms,link_fraction\n");
    const char* names[] = {"MOLD", "WILD"};
    const char* source = "src/buster/apps/ide/ide.c";
    const char* original = read_text(source);
    double edits[2][BUSTER_BENCH_SAMPLES];
    double clean_times[2][BUSTER_BENCH_SAMPLES];
    for (unsigned round = 0; round < 21; ++round)
    {
        unsigned first = random_u32() & 1;
        for (unsigned position = 0; position < 2; ++position)
        {
            unsigned which = first ^ position;
            must("share-select-linker", format("cmake -B %s -DCMAKE_LINKER_TYPE=%s", quote(directory), names[which]));
            must("share-baseline", format("%s build --build-directory %s --config Debug -t ide", quote(driver), quote(directory)));
            write_text(source, format("%s\nextern volatile unsigned buster_review_marker;\nvolatile unsigned buster_review_marker = %u;\n", original, round + 1));
            BenchResult edited = run_command("review-edit-to-executable", names[which], (int)round,
                format("%s build --build-directory %s --config Debug -t ide", quote(driver), quote(directory)));
            write_text(source, original);
            if (edited.status) fail("share edit build failed; source restored");
            edits[which][round] = edited.wall_ms;
            double link = ninja_link_ms(directory);
            fprintf(shares, "edit,%s,%u,%.6f,%.6f,%.6f\n", names[which], round, edited.wall_ms, link, link / edited.wall_ms);
            if (round < 7)
            {
                must("share-clean", format("cmake --build %s --config Debug --target clean", quote(directory)));
                BenchResult clean = must("review-clean-build", format("%s build --build-directory %s --config Debug -t ide", quote(driver), quote(directory)));
                clean_times[which][round] = clean.wall_ms;
                link = ninja_link_ms(directory);
                fprintf(shares, "clean,%s,%u,%.6f,%.6f,%.6f\n", names[which], round, clean.wall_ms, link, link / clean.wall_ms);
            }
            if (fflush(shares)) fail("share flush");
        }
    }
    report("matched-policy-edit-to-executable", edits[0], edits[1], 21);
    report("matched-policy-clean-build", clean_times[0], clean_times[1], 7);
    if (fclose(shares)) fail("share close");
    must("review-source-restored", "git diff --exit-code -- src");
}

int main(int argc, char** argv)
{
    int result = 2;
    if (argc == 4)
    {
        driver = argv[1];
        evidence = argv[2];
        const char* input = argv[3];
        raw = fopen(format("%s/raw.csv", evidence), "wx");
        commands = fopen(format("%s/commands.tsv", evidence), "wx");
        summary = fopen(format("%s/summary.csv", evidence), "wx");
        if (!raw || !commands || !summary) fail("fresh supplementary outputs");
        fprintf(raw, "sequence,cell,variant,round,exit,wall_ms,user_ms,system_ms,maxrss_kib,minor_faults,major_faults\n");
        fprintf(summary, "cell,n,median_A_ms,median_B_ms,mad_A_ms,mad_B_ms,median_paired_B_over_A,ci95_low,ci95_high\n");
        self_test();
        write_text(format("%s/method.txt", evidence),
            "Independent direct-linker replay from #2646 frozen artifacts, unchanged source baseline 3b79a042. Same objects, scripts and regular filesystem system inputs hashed before/after; read-open traces retained, including excluded dynamic /proc and /sys host telemetry.\n"
            "Direct timing excludes compiler-driver startup, includes /bin/sh and fork/wait (<1ms self-control); output close without fsync, warm page-cache. Original release package binaries, default threads and matched --threads=1. No LTO. Explicit --no-gc-sections (ide/small), --gc-sections (hot_reload requirement), --no-fork (wait4 covers complete linker lifetime), --icf=none and identical fixed 20-byte benchmark-only build ID override differing upstream defaults.\n"
            "wait4 user/system and linker process high-water RSS for no-fork direct cells; build/smoke process-tree peaks are not summed. Prior forked observations have incomplete CPU/RSS accounting and remain exploratory. Bootstrap intervals are descriptive for the observed hosted VM; no dedicated-host claim.\n"
            "Build link share uses the actual Ninja output step duration; end-to-end includes driver/Ninja/compilation. Clean samples n=7; edits n=21 per variant.\n");
        smoke();
        direct_cell("small-archive", format("clang --ld-path=wild %s %s -o %s", quote(format("%s/main.o", evidence)), quote(format("%s/libprobe.a", evidence)), quote(format("%s/review-small", evidence))),
            ".", format("%s/review-small", evidence), quote(format("%s/review-small", evidence)));
        const char* roots[] = {"build-wild-unity", "build-wild-separate"};
        for (unsigned i = 0; i < 2; ++i)
        {
            must("restore-frozen-objects", format("tar -xzf %s", quote(format("%s/%s-inputs.tar.gz", input, i ? "separate-release" : "unity-release"))));
        }
        must("prepare-frozen-output-directories", "mkdir -p build-wild-unity/Debug build-wild-unity/Release build-wild-separate/Debug build-wild-separate/Release");
        const char* original_cells[] = {"separate-debug-default", "unity-release-default", "separate-release-default"};
        const char* cells[] = {"ide-debug", "ide-unity-release", "ide-separate-release"};
        for (unsigned i = 0; i < 3; ++i)
        {
            const char* directory = i == 1 ? roots[0] : roots[1];
            const char* config = i ? "Release" : "Debug";
            const char* binary = format("%s/%s/ide", directory, config);
            direct_cell(cells[i], frozen_command(input, original_cells[i]), directory, binary,
                format("%s cc -g -c %s -o %s", quote(binary), quote(format("%s/probe.c", evidence)), quote(format("%s/review-check.o", evidence))));
            if (!i)
            {
                check_debugger("ide-mold-source-and-unwind", format("%s/ide-debug-mold.elf", evidence), "entry_point", "", "entry_point", "buster_entry_point");
                check_debugger("ide-wild-source-and-unwind", format("%s/ide-debug-wild.elf", evidence), "entry_point", "", "entry_point", "buster_entry_point");
            }
        }
        interoperability(format("%s/ide-unity-release-wild.elf", evidence));
        direct_cell("buster-produced-small", format("clang --ld-path=wild %s %s -o %s", quote(format("%s/main-buster.o", evidence)), quote(format("%s/probe-buster.o", evidence)), quote(format("%s/buster-direct", evidence))), ".", format("%s/buster-direct", evidence), quote(format("%s/buster-direct", evidence)));
        for (unsigned i = 0; i < 2; ++i)
        {
            const char* name = i ? "wild" : "mold";
            check_debugger("buster-object-source-and-unwind", format("%s/buster-produced-%s", evidence, name), "probe", "", "probe", "main");
        }
        must("configure-review-build", format("%s generate --build-directory build-wild-review --cc clang --linker WILD --no-include-tests --no-fuzz --no-sanitize --no-lto -- -DBUSTER_UNITY_BUILD=OFF -DBUSTER_HOT_RELOAD_DEMO=ON -DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-gc-sections,--no-fork,--icf=none,--build-id=0x2645264526452645264526452645264526452645", quote(driver)));
        must("build-review-application", format("%s build --build-directory build-wild-review --config Debug -t ide -t hot_reload", quote(driver)));
        direct_cell("hot-reload-debug", target_command("build-wild-review", "hot_reload"), "build-wild-review", "build-wild-review/Debug/hot_reload",
            "build-wild-review/Debug/hot_reload --self-test build-wild-review/Debug/ide");
        build_shares("build-wild-review");

        must("configure-regression-build", format("%s generate --build-directory build-wild-regression --cc clang --linker WILD --include-tests --no-fuzz --sanitize --no-lto -- -DBUSTER_UNITY_BUILD=OFF -DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-gc-sections,--icf=none,--build-id=0x2645264526452645264526452645264526452645", quote(driver)));
        for (unsigned i = 0; i < 2; ++i)
        {
            const char* linker = i ? "WILD" : "MOLD";
            must("regression-select-linker", format("cmake -B build-wild-regression -DCMAKE_LINKER_TYPE=%s", linker));
            must("build-regression-ide", format("%s build --build-directory build-wild-regression --config Debug -t ide", quote(driver)));
            BenchResult regression = run_command("sanitized-affected-regressions", linker, -1,
                "build-wild-regression/Debug/ide test --verbose=1 --module=debug_model_tests,dwarf_tests,object_tests,jit_tests,link_tests,compiler_driver_tests,compiler_driver_object_path_tests");
            if (regression.status) ++failures;
        }

        BenchResult cold = run_command("guest-cold-cache-admission", "drop-caches", -1, "sudo -n sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches'");
        write_text(format("%s/cold-cache-status.txt", evidence), cold.status ? "NOT RUN: guest refused drop-caches, diagnostic retained.\n" : "Guest Linux page cache, dentries and inode cache evicted with sync; drop_caches=3 before every observation. Reset excluded from timer. Hypervisor/storage caches uncontrolled; this is guest-cold, not physical-cold. No cold warmups. Nine randomized pairs per cell.\n");
        if (!cold.status)
        {
            char* wild = format("%s --no-gc-sections --no-fork --icf=none --build-id=0x2645264526452645264526452645264526452645", direct_link(frozen_command(input, "separate-debug-default"), "build-wild-separate"));
            char* mold = change_linker(wild, "build-wild-separate", "mold");
            cold_comparison("ide-debug-direct-guest-cold-default", mold, wild);
            cold_comparison("ide-debug-direct-guest-cold-threads1", format("%s --threads=1", mold), format("%s --threads=1", wild));
            must("cold-frozen-input-check", format("sha256sum --check %s", quote(format("%s/ide-debug-all-read-inputs.sha256", evidence))));
        }
        BenchResult gcc = run_command("driver-gcc-wild-diagnostic", "expected-failure", -1, format("%s generate --build-directory build-wild-review-gcc --cc gcc --linker WILD --no-include-tests --no-fuzz --no-sanitize --no-lto", quote(driver)));
        if (!gcc.status) ++failures;
        BenchResult flag = run_command("wild-unsupported-flag", "expected-failure", -1, "wild --buster-invalid-linker-option");
        if (!flag.status) ++failures;
        BenchResult missing = run_command("clang-missing-explicit-linker", "expected-failure", -1, format("clang --ld-path=/definitely/missing/wild %s %s -o %s", quote(format("%s/main.o", evidence)), quote(format("%s/libprobe.a", evidence)), quote(format("%s/missing-output", evidence))));
        if (!missing.status) ++failures;
        write_text(format("%s/negative-selection-status.txt", evidence), format("driver_gcc_wild=%d\nunsupported_flag=%d\nmissing_explicit_linker=%d\nNonzero means explicit request rejected, no implicit successful fallback.\n", gcc.status, flag.status, missing.status));
        must("retain-original-summary", format("cp %s %s && cp %s %s", quote(format("%s/summary.csv", input)), quote(format("%s/original-summary.csv", evidence)), quote(format("%s/raw.csv", input)), quote(format("%s/original-raw.csv", evidence))));
        write_text(format("%s/completion.txt", evidence), failures ? "supported_campaign=FAIL\n" : "supported_campaign=PASS; inspect explicit unsupported configuration diagnostics, including hot_reload full-DWARF verifier\n");
        if (fclose(raw) || fclose(commands) || fclose(summary)) fail("final review flush");
        result = failures ? 1 : 0;
    }
    else fprintf(stderr, "usage: %s BUILD_DRIVER EVIDENCE ORIGINAL_EVIDENCE\n", argv[0]);
    return result;
}
