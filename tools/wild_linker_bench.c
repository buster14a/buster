#define _GNU_SOURCE
/* #2645: Linux-hosted linker experiment, not part of the compiler.
 * main owns the bounded campaign; build.c owns repository construction.
 * run_command retains each command/status/log; compare retains every pair.
 * Inputs are compiled once per link-only cell. No vendor code or callbacks.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_BENCH_CAPACITY (64u * 1024u * 1024u)
#define BUSTER_BENCH_SAMPLES 21
#define BUSTER_BENCH_BOOTSTRAPS 4001
#define BUSTER_BENCH_TIMEOUT_MS 600000

typedef struct BenchResult
{
    double wall_ms;
    double user_ms;
    double system_ms;
    long rss_kib;
    int status;
    const char* log;
} BenchResult;

static char arena[BUSTER_BENCH_CAPACITY];
static size_t arena_used;
static const char* evidence;
static const char* driver;
static FILE* raw;
static FILE* commands;
static FILE* summary;
static unsigned sequence;
static uint32_t random_state = 2645;
static int failures;

static void fail(const char* message)
{
    fprintf(stderr, "HARNESS ERROR: %s (errno=%d)\n", message, errno);
    exit(2);
}

static char* reserve(size_t size)
{
    if (size > sizeof(arena) - arena_used) fail("bounded arena exhausted");
    char* result = arena + arena_used;
    arena_used += size;
    return result;
}

static char* format(const char* pattern, ...)
{
    va_list first;
    va_list second;
    va_start(first, pattern);
    va_copy(second, first);
    int count = vsnprintf(NULL, 0, pattern, first);
    va_end(first);
    if (count < 0) fail("format length");
    char* result = reserve((size_t)count + 1);
    if (vsnprintf(result, (size_t)count + 1, pattern, second) != count) fail("format");
    va_end(second);
    return result;
}

static char* quote(const char* text)
{
    char* result = reserve(strlen(text) * 4 + 3);
    char* out = result;
    *out++ = '\'';
    for (const char* in = text; *in; ++in)
    {
        if (*in == '\'')
        {
            memcpy(out, "'\\''", 4);
            out += 4;
        }
        else *out++ = *in;
    }
    *out++ = '\'';
    *out = 0;
    return result;
}

static char* read_text(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END)) fail("open input");
    long size = ftell(file);
    if (size < 0 || size > 8 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) fail("input bound");
    char* result = reserve((size_t)size + 1);
    if (fread(result, 1, (size_t)size, file) != (size_t)size || fclose(file)) fail("read input");
    result[size] = 0;
    return result;
}

static void write_text(const char* path, const char* text)
{
    FILE* file = fopen(path, "wb");
    size_t length = strlen(text);
    if (!file || fwrite(text, 1, length, file) != length || fclose(file)) fail("write text");
}

static char* replace_one(const char* text, const char* from, const char* to)
{
    const char* match = strstr(text, from);
    if (!match || strstr(match + strlen(from), from)) fail("expected one replacement anchor");
    return format("%.*s%s%s", (int)(match - text), text, to, match + strlen(from));
}

static double clock_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) fail("monotonic clock");
    return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}

static BenchResult run_command(const char* cell, const char* variant, int round, const char* command)
{
    const char* log = format("%s/%05u.log", evidence, sequence);
    int fd = open(log, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) fail("log must be new");
    fprintf(commands, "%u\t%s\t%s\t%d\t%s\n", sequence, cell, variant, round, command);
    if (fflush(commands)) fail("command flush");
    double begin = clock_ms();
    pid_t pid = fork();
    if (pid < 0) fail("fork");
    if (!pid)
    {
        if (setpgid(0, 0) || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) _exit(126);
        close(fd);
        execl("/bin/sh", "sh", "-c", command, (char*)NULL);
        _exit(127);
    }
    close(fd);
    int pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
    int ready = -1;
    if (pidfd >= 0)
    {
        struct pollfd event = {.fd = pidfd, .events = POLLIN};
        do { ready = poll(&event, 1, BUSTER_BENCH_TIMEOUT_MS); } while (ready < 0 && errno == EINTR);
        close(pidfd);
    }
    if (ready <= 0) kill(-pid, SIGKILL);
    struct rusage usage = {0};
    int status = 0;
    pid_t waited;
    do { waited = wait4(pid, &status, 0, &usage); } while (waited < 0 && errno == EINTR);
    double end = clock_ms();
    if (waited != pid) fail("wait4");
    if (ready <= 0) fail("pidfd unavailable or process timeout; no timing verdict");
    BenchResult result = {
        .wall_ms = end - begin,
        .user_ms = usage.ru_utime.tv_sec * 1000.0 + usage.ru_utime.tv_usec / 1000.0,
        .system_ms = usage.ru_stime.tv_sec * 1000.0 + usage.ru_stime.tv_usec / 1000.0,
        .rss_kib = usage.ru_maxrss,
        .status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status),
        .log = log,
    };
    fprintf(raw, "%u,%s,%s,%d,%d,%.6f,%.6f,%.6f,%ld,%ld,%ld\n", sequence++, cell, variant, round,
            result.status, result.wall_ms, result.user_ms, result.system_ms, result.rss_kib,
            usage.ru_minflt, usage.ru_majflt);
    if (fflush(raw)) fail("sample flush");
    printf("%s %s round=%d exit=%d wall_ms=%.3f log=%s\n", cell, variant, round, result.status, result.wall_ms, log);
    fflush(stdout);
    return result;
}

static BenchResult must(const char* cell, const char* command)
{
    BenchResult result = run_command(cell, "setup", -1, command);
    if (result.status)
    {
        fprintf(stderr, "%s\n", read_text(result.log));
        fail("required command failed; see retained log");
    }
    return result;
}

static uint32_t random_u32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void sort(double* values, unsigned count)
{
    for (unsigned i = 1; i < count; ++i)
    {
        double value = values[i];
        unsigned j = i;
        while (j && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = value;
    }
}

static void report(const char* cell, const double* a, const double* b, unsigned count)
{
    double sorted_a[BUSTER_BENCH_SAMPLES];
    double sorted_b[BUSTER_BENCH_SAMPLES];
    double ratios[BUSTER_BENCH_SAMPLES];
    double boots[BUSTER_BENCH_BOOTSTRAPS];
    for (unsigned i = 0; i < count; ++i)
    {
        if (a[i] <= 0 || b[i] <= 0) fail("nonpositive duration");
        sorted_a[i] = a[i];
        sorted_b[i] = b[i];
        ratios[i] = b[i] / a[i];
    }
    sort(sorted_a, count);
    sort(sorted_b, count);
    for (unsigned i = 0; i < BUSTER_BENCH_BOOTSTRAPS; ++i)
    {
        double sample[BUSTER_BENCH_SAMPLES];
        for (unsigned j = 0; j < count; ++j) sample[j] = ratios[random_u32() % count];
        sort(sample, count);
        boots[i] = sample[count / 2];
    }
    sort(boots, BUSTER_BENCH_BOOTSTRAPS);
    sort(ratios, count);
    double deviations_a[BUSTER_BENCH_SAMPLES];
    double deviations_b[BUSTER_BENCH_SAMPLES];
    for (unsigned i = 0; i < count; ++i)
    {
        double da = a[i] - sorted_a[count / 2];
        double db = b[i] - sorted_b[count / 2];
        deviations_a[i] = da < 0 ? -da : da;
        deviations_b[i] = db < 0 ? -db : db;
    }
    sort(deviations_a, count);
    sort(deviations_b, count);
    fprintf(summary, "%s,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", cell, count,
            sorted_a[count / 2], sorted_b[count / 2], deviations_a[count / 2], deviations_b[count / 2],
            ratios[count / 2], boots[100], boots[3900]);
    fflush(summary);
    printf("RESULT %s n=%u A_ms=%.3f B_ms=%.3f paired_B_over_A=%.4f ci95=[%.4f,%.4f]\n",
           cell, count, sorted_a[count / 2], sorted_b[count / 2], ratios[count / 2], boots[100], boots[3900]);
}

static void compare(const char* cell, const char* a, const char* b, unsigned count)
{
    double values[2][BUSTER_BENCH_SAMPLES];
    const char* variants[] = {"A", "B"};
    const char* argv[] = {a, b};
    for (unsigned round = 0; round < count + 2; ++round)
    {
        unsigned first = random_u32() & 1;
        for (unsigned position = 0; position < 2; ++position)
        {
            unsigned which = first ^ position;
            BenchResult result = run_command(cell, variants[which], (int)round - 2, argv[which]);
            if (result.status) fail("link comparison failure; no success ranking");
            if (round >= 2) values[which][round - 2] = result.wall_ms;
        }
    }
    report(cell, values[0], values[1], count);
}

static const char* link_command(const char* directory, const char* config)
{
    BenchResult result = must("capture-link-command", format("ninja -C %s -f build-%s.ninja -t commands ide", quote(directory), config));
    char* text = read_text(result.log);
    char* last = NULL;
    for (char* at = text; *at;)
    {
        char* end = strchr(at, '\n');
        if (end) *end = 0;
        if ((strstr(at, "--ld-path=wild") || strstr(at, "-fuse-ld=wild")) && strstr(at, " -o ")) last = at;
        if (!end) break;
        at = end + 1;
    }
    if (!last) fail("cannot identify the generated Wild link command");
    if (strstr(last, "-fuse-ld=wild")) last = replace_one(last, "-fuse-ld=wild", "--ld-path=wild");
    return format("cd %s && %s", quote(directory), last);
}

static void preserve_link(const char* directory, const char* config, const char* cell, const char* command, const char* variant)
{
    must("preserve-link", command);
    const char* binary = format("%s/%s/ide", directory, config);
    const char* frozen = format("%s/%s-%s", evidence, cell, variant);
    must("freeze-linked-image", format("cp %s %s && sha256sum %s && readelf -p .comment %s && size -A %s && readelf -W -l %s",
         quote(binary), quote(frozen), quote(frozen), quote(frozen), quote(frozen), quote(frozen)));
    must("linked-compiler-smoke", format("%s cc -g -c %s -o %s", quote(frozen), quote(format("%s/probe.c", evidence)), quote(format("%s/check.o", evidence))));
    must("same-linker-repeat", command);
    must("same-linker-byte-identity", format("cmp %s %s", quote(binary), quote(frozen)));
}

static void link_cell(const char* directory, const char* config, const char* cell)
{
    must("build-inputs", format("%s build --build-directory %s --config %s -t ide", quote(driver), quote(directory), config));
    const char* wild = link_command(directory, config);
    const char* mold = replace_one(wild, "--ld-path=wild", "--ld-path=mold");
    must("freeze-object-manifest", format("find %s -type f \\( -name '*.o' -o -name '*.a' \\) -print0 | sort -z | xargs -0 sha256sum > %s",
         quote(directory), quote(format("%s/%s-inputs.sha256", evidence, cell))));
    must("freeze-cell-provenance", format("cp %s %s && cp %s %s",
         quote(format("%s/CMakeCache.txt", directory)), quote(format("%s/%s.CMakeCache.txt", evidence, cell)),
         quote(format("%s/compile_commands.json", directory)), quote(format("%s/%s.compile_commands.json", evidence, cell))));
    must("freeze-object-inputs", format("find %s -type f \\( -name '*.o' -o -name '*.a' \\) -print0 > %s && tar --null -T %s -czf %s",
         quote(directory), quote(format("%s/%s-inputs.list", evidence, cell)),
         quote(format("%s/%s-inputs.list", evidence, cell)), quote(format("%s/%s-inputs.tar.gz", evidence, cell))));
    compare(format("%s-default", cell), mold, wild, BUSTER_BENCH_SAMPLES);
    compare(format("%s-threads1", cell), replace_one(mold, "--ld-path=mold", "--ld-path=mold -Wl,--threads=1"),
            replace_one(wild, "--ld-path=wild", "--ld-path=wild -Wl,--threads=1"), BUSTER_BENCH_SAMPLES);
    if (!strcmp(cell, "separate-debug"))
    {
        compare("mold-aa-control", mold, mold, 9);
        compare("wild-aa-control", wild, wild, 9);
    }
    preserve_link(directory, config, cell, mold, "mold");
    preserve_link(directory, config, cell, wild, "wild");
    must("unchanged-object-manifest", format("sha256sum --check %s", quote(format("%s/%s-inputs.sha256", evidence, cell))));
}

static void smoke(void)
{
    write_text(format("%s/probe.c", evidence), "long probe(long x) { return x * 3 + 1; }\n");
    write_text(format("%s/main.c", evidence), "extern long probe(long); int main(void) { return probe(9) != 28; }\n");
    must("compile-smoke-inputs", format("clang -g -O0 -fPIC -c %s -o %s && clang -g -O0 -c %s -o %s && ar rcs %s %s",
         quote(format("%s/probe.c", evidence)), quote(format("%s/probe.o", evidence)),
         quote(format("%s/main.c", evidence)), quote(format("%s/main.o", evidence)),
         quote(format("%s/libprobe.a", evidence)), quote(format("%s/probe.o", evidence))));
    const char* linkers[] = {"mold", "wild"};
    for (unsigned i = 0; i < 2; ++i)
    {
        const char* name = linkers[i];
        const char* exe = format("%s/smoke-%s", evidence, name);
        must("archive-link", format("clang -fuse-ld=%s -Wl,-z,relro,-z,now %s %s -o %s", name,
             quote(format("%s/main.o", evidence)), quote(format("%s/libprobe.a", evidence)), quote(exe)));
        must("archive-runtime", quote(exe));
        const char* library = format("%s/libprobe-%s.so", evidence, name);
        must("shared-link", format("clang -shared -fuse-ld=%s -Wl,-z,relro,-z,now %s -o %s", name, quote(format("%s/probe.o", evidence)), quote(library)));
        must("shared-consumer", format("clang -fuse-ld=%s %s %s -o %s", name, quote(format("%s/main.o", evidence)), quote(library), quote(format("%s/shared-%s", evidence, name))));
        must("shared-runtime", quote(format("%s/shared-%s", evidence, name)));
        must("elf-inspection", format("readelf -W -l %s && readelf -p .comment %s && readelf -S %s", quote(exe), quote(exe), quote(exe)));
        BenchResult debug = run_command("debugger", name, -1, format("gdb -q -batch -ex 'break probe' -ex run -ex 'print x' -ex bt --args %s", quote(exe)));
        if (debug.status || !strstr(read_text(debug.log), "probe (x=9)")) ++failures;
    }
    BenchResult missing = run_command("missing-linker-negative-control", "clang", -1,
        format("clang --ld-path=/nonexistent/buster-2645-linker %s -o %s", quote(format("%s/main.o", evidence)), quote(format("%s/missing", evidence))));
    if (!missing.status || !access(format("%s/missing", evidence), F_OK)) ++failures;
    BenchResult old_gcc = run_command("gcc-wild-selector", "gcc", -1,
        format("gcc -fuse-ld=wild %s %s -o %s", quote(format("%s/main.o", evidence)), quote(format("%s/probe.o", evidence)), quote(format("%s/gcc-smoke", evidence))));
    write_text(format("%s/gcc-selector-status.txt", evidence), old_gcc.status ? "NOT SUPPORTED by installed GCC; retain diagnostic. No Buster GCC support claim.\n" : "Link succeeded; see recorded GCC identity.\n");
}

static void interoperability(const char* compiler)
{
    must("buster-object-producer", format("%s cc -g -fPIC -c %s -o %s && %s cc -g -fPIC -c %s -o %s",
        quote(compiler), quote(format("%s/probe.c", evidence)), quote(format("%s/probe-buster.o", evidence)),
        quote(compiler), quote(format("%s/main.c", evidence)), quote(format("%s/main-buster.o", evidence))));
    const char* names[] = {"mold", "wild"};
    for (unsigned i = 0; i < 2; ++i)
    {
        const char* binary = format("%s/buster-produced-%s", evidence, names[i]);
        must("buster-object-external-link", format("clang -fuse-ld=%s %s %s -o %s", names[i],
            quote(format("%s/main-buster.o", evidence)), quote(format("%s/probe-buster.o", evidence)), quote(binary)));
        must("buster-object-runtime", quote(binary));
        must("buster-object-debug", format("llvm-dwarfdump --verify %s", quote(binary)));
    }
}

static void end_to_end(const char* directory)
{
    const char* source_path = "src/buster/apps/ide/ide.c";
    const char* original = read_text(source_path);
    const char* names[] = {"MOLD", "WILD"};
    double clean[2][BUSTER_BENCH_SAMPLES];
    double edits[2][BUSTER_BENCH_SAMPLES];
    for (unsigned round = 0; round < 3; ++round)
    {
        unsigned first = random_u32() & 1;
        for (unsigned position = 0; position < 2; ++position)
        {
            unsigned which = first ^ position;
            must("choose-build-linker", format("cmake -B %s -DCMAKE_LINKER_TYPE=%s", quote(directory), names[which]));
            must("clean-build-preparation", format("cmake --build %s --config Debug --target clean", quote(directory)));
            BenchResult result = run_command("clean-build", names[which], (int)round,
                format("%s build --build-directory %s --config Debug -t ide", quote(driver), quote(directory)));
            if (result.status) fail("clean build failed");
            clean[which][round] = result.wall_ms;
        }
    }
    report("clean-build-small-n", clean[0], clean[1], 3);
    for (unsigned round = 0; round < 7; ++round)
    {
        unsigned first = random_u32() & 1;
        for (unsigned position = 0; position < 2; ++position)
        {
            unsigned which = first ^ position;
            write_text(source_path, original);
            must("choose-edit-linker", format("cmake -B %s -DCMAKE_LINKER_TYPE=%s", quote(directory), names[which]));
            must("restore-edit-baseline", format("%s build --build-directory %s --config Debug -t ide", quote(driver), quote(directory)));
            write_text(source_path, format("%s\nextern volatile unsigned buster_link_bench_marker;\nvolatile unsigned buster_link_bench_marker = %u;\n", original, round + 1));
            BenchResult result = run_command("content-edit-to-executable", names[which], (int)round,
                format("%s build --build-directory %s --config Debug -t ide", quote(driver), quote(directory)));
            write_text(source_path, original);
            if (result.status) fail("edited build failed; original restored");
            edits[which][round] = result.wall_ms;
        }
    }
    report("content-edit-to-executable", edits[0], edits[1], 7);
    must("source-restoration", "git diff --exit-code -- src");
}

static void self_test(void)
{
    double data[] = {5, 1, 3, 2, 4};
    sort(data, 5);
    if (data[0] != 1 || data[2] != 3 || data[4] != 5) fail("sort self-test");
    if (strcmp(replace_one("alpha KEY omega", "KEY", "value"), "alpha value omega")) fail("replacement self-test");
    BenchResult good = run_command("self-test-exit", "zero", -1, "exit 0");
    BenchResult bad = run_command("self-test-exit", "seven", -1, "exit 7");
    BenchResult quoted = run_command("self-test-quoting", "literal", -1, format("printf '%%s' %s", quote("a'b c")));
    if (good.status || bad.status != 7 || quoted.status || strcmp(read_text(quoted.log), "a'b c")) fail("process self-test");
    puts("SELF_TEST PASS");
}

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
        if (!raw || !commands || !summary) fail("fresh result files required");
        fprintf(raw, "sequence,cell,variant,round,exit,wall_ms,user_ms,system_ms,maxrss_kib,minor_faults,major_faults\n");
        fprintf(summary, "cell,n,median_A_ms,median_B_ms,mad_A_ms,mad_B_ms,median_paired_B_over_A,ci95_low,ci95_high\n");
        self_test();
        if (!strcmp(driver, "--self-test")) result = 0;
        else
        {
            write_text(format("%s/method.txt", evidence),
                "Debug is always non-unity under the existing CMake policy, even when BUSTER_UNITY_BUILD=ON. default-debug-nonunity and separate-debug are two build-root controls, not distinct unity modes.\n"
                "A=mold; B=Wild except named A/A controls. Seed=2645; 2 warmups and 21 randomized AB/BA pairs per link-only cell. Warm page-cache only. Full relinking, not incremental linking. Link-only includes shell/compiler-driver startup but no source compilation.\n"
                "Wall includes fork/exec/wait; CPU is wait4 user/system including waited descendants. Linux ru_maxrss is maximum child high-water, NOT simultaneous process-tree peak. Raw minor/major faults retained. Matched --threads=1 and each linker's default. Output is closed, not fsynced; same filesystem and output path.\n"
                "CI95 is 4001 paired bootstrap medians, descriptive on this VM only. Clean builds have only 3 pairs and are exploratory. Content edit adds a named volatile global to ide.c, then restores source; 7 pairs. No source/vendor/compiler changes.\n"
                "Cold-cache, LTO, cross-architecture and full project sanitizer acceptance NOT RUN. Upstream release binaries are digest-pinned; their compiler/PGO build settings are not matched. No hardware-general or dedicated-host acceptance claim.\n");
            smoke();
            must("latest-audit-identity", "ls docs/performance-audits/*.md | sort | tail -1 | xargs cat");
            must("configure-unity", format("%s generate --build-directory build-wild-unity --cc clang --linker WILD --no-include-tests --no-fuzz --no-sanitize -- -DBUSTER_UNITY_BUILD=ON -DBUSTER_LTO=OFF -DBUSTER_DEBUG_INFO=ON -DBUSTER_FRAME_POINTERS=ON", quote(driver)));
            link_cell("build-wild-unity", "Debug", "default-debug-nonunity");
            link_cell("build-wild-unity", "Release", "unity-release");
            interoperability(format("%s/unity-release-wild", evidence));
            must("self-host", format("%s test_self_host --build-directory build-wild-unity --config Release", quote(driver)));
            must("configure-separate", format("%s generate --build-directory build-wild-separate --cc clang --linker WILD --no-include-tests --no-fuzz --no-sanitize -- -DBUSTER_UNITY_BUILD=OFF -DBUSTER_LTO=OFF -DBUSTER_DEBUG_INFO=ON -DBUSTER_FRAME_POINTERS=ON", quote(driver)));
            link_cell("build-wild-separate", "Debug", "separate-debug");
            link_cell("build-wild-separate", "Release", "separate-release");
            end_to_end("build-wild-separate");
            must("retain-build-provenance", format("cp build-wild-unity/CMakeCache.txt %s && cp build-wild-separate/CMakeCache.txt %s && cp build-wild-unity/compile_commands.json %s && cp build-wild-separate/compile_commands.json %s",
                quote(format("%s/unity.CMakeCache.txt", evidence)), quote(format("%s/separate.CMakeCache.txt", evidence)),
                quote(format("%s/unity.compile_commands.json", evidence)), quote(format("%s/separate.compile_commands.json", evidence))));
            result = failures ? 1 : 0;
            write_text(format("%s/completion.txt", evidence), format("campaign_exit=%d\nsmoke_assurance_failures=%d\n", result, failures));
        }
        if (fclose(raw) || fclose(commands) || fclose(summary)) fail("final evidence flush");
    }
    else fprintf(stderr, "usage: %s BUILD_DRIVER|--self-test EXISTING_FRESH_EVIDENCE_DIRECTORY\n", argv[0]);
    return result;
}
