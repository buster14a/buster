// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #149/#153: real mixed-tier compilations, not a synthetic backend invocation. Compare each host path with its own
// one-lane bytes: the scalar and vector allocators need not choose the same code. Every executable must also return
// the independently derived sum. A command that silently uses only Tier 0, cannot start its requested team, fails,
// hangs, or leaves an old output cannot pass. No timing result is collected or used as performance evidence.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSTER_MULTICORE_FUNCTIONS 256u
#define BUSTER_MULTICORE_ADDITIONS 40u
#define BUSTER_MULTICORE_MAX_LANES 8u
#define BUSTER_MULTICORE_SECONDS 120u
#define BUSTER_MULTICORE_REPORT_BYTES 16384u
#define BUSTER_MULTICORE_UNIT_BYTES 32768u
#define BUSTER_MULTICORE_UNIT_LANES 6u

typedef struct MulticoreReport MulticoreReport;
struct MulticoreReport
{
    char tier[BUSTER_MULTICORE_REPORT_BYTES];
    unsigned asked;
    unsigned lanes;
    unsigned functions;
    unsigned tier1;
};

static bool same_file(const char* first, const char* second)
{
    FILE* a = fopen(first, "rb");
    FILE* b = fopen(second, "rb");
    bool same = a != NULL && b != NULL;
    size_t total = 0;
    size_t got = 1;
    while (same && got != 0)
    {
        unsigned char x[8192];
        unsigned char y[8192];
        got = fread(x, 1, sizeof(x), a);
        const size_t other = fread(y, 1, sizeof(y), b);
        same = got == other && memcmp(x, y, got) == 0 && !ferror(a) && !ferror(b);
        total += got;
    }
    if (a != NULL)
    {
        same = fclose(a) == 0 && same;
    }
    if (b != NULL)
    {
        same = fclose(b) == 0 && same;
    }
    return same && total != 0;
}

static bool run(char* const* arguments)
{
    fflush(NULL);
    const pid_t child = fork();
    if (child == 0)
    {
        const struct rlimit cores = { 0 };
        const int out = open("stdout.txt", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const int err = open("stderr.txt", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const bool ready = out >= 0 && err >= 0 && dup2(out, STDOUT_FILENO) >= 0 && dup2(err, STDERR_FILENO) >= 0 &&
                           setrlimit(RLIMIT_CORE, &cores) == 0 && unsetenv("MAKEFLAGS") == 0 && unsetenv("MFLAGS") == 0;
        if (ready)
        {
            close(out);
            close(err);
            alarm(BUSTER_MULTICORE_SECONDS);
            execvp(arguments[0], arguments);
        }
        _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    } while (child > 0 && waited < 0 && errno == EINTR);
    const bool passed = waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!passed)
    {
        fprintf(stderr, "multicore: child failed (wait=%ld, status=%d):", (long)waited, status);
        for (size_t i = 0; arguments[i] != NULL; i += 1)
        {
            fprintf(stderr, " %s", arguments[i]);
        }
        fputc('\n', stderr);
        FILE* errors = fopen("stderr.txt", "r");
        if (errors != NULL)
        {
            char line[4096];
            while (fgets(line, sizeof(line), errors) != NULL)
            {
                fputs(line, stderr);
            }
            fclose(errors);
        }
    }
    return passed;
}

// Each loop's 17 iterations contribute 35*i + (0+...+16); the straight-line additions contribute
// ADDITIONS*i + (0+...+ADDITIONS-1). Summing i=0..FUNCTIONS-1 is independent of the emitted loop/CFG execution.
// Two constructs keep the fixture mixed: the variadic definition's incoming frame and the indirect floating call
// (with widen's conversion). Tier 1 refuses at least one of them today; the matrix skips with status 77, naming this
// file, if a later Tier 1 covers them all.
static bool write_sources(void)
{
    const unsigned n = BUSTER_MULTICORE_FUNCTIONS;
    const unsigned a = BUSTER_MULTICORE_ADDITIONS;
    const unsigned expected = ((35u + a) * (n * (n - 1u) / 2u) + n * (136u + a * (a - 1u) / 2u)) ^ 3u;
    FILE* body = fopen("body.c", "w");
    FILE* single = fopen("single.c", "w");
    FILE* entry = fopen("entry.c", "w");
    FILE* outputs[] = { body, single };
    bool written = body != NULL && single != NULL && entry != NULL;
    for (unsigned file = 0; written && file < 2; file += 1)
    {
        FILE* output = outputs[file];
        fputs("static double widen(unsigned x) { return (double)x; }\n"
              "unsigned fallback(unsigned x, ...)\n"
              "{\n"
              "    __builtin_va_list arguments;\n"
              "    __builtin_va_start(arguments, x);\n"
              "    const unsigned extra = __builtin_va_arg(arguments, unsigned);\n"
              "    __builtin_va_end(arguments);\n"
              "    double (*volatile operation)(unsigned) = widen;\n"
              "    return ((unsigned)operation(x) ^ extra);\n"
              "}\n", output);
        for (unsigned i = 0; i < n; i += 1)
        {
            fprintf(output, "unsigned work_%u(unsigned count)\n{\n    unsigned sum = 0;\n    for (unsigned j = 0; j < count; j += 1)\n    {\n        if (j & 1u) { sum += %uu + j; } else { sum += %uu + j; }\n    }\n", i, i, 3u * i);
            for (unsigned k = 0; k < a; k += 1)
            {
                fprintf(output, "    sum += %uu;\n", i + k);
            }
            fputs("    return sum;\n}\n", output);
        }
        fputs("unsigned aggregate(unsigned count)\n{\n    unsigned sum = 0;\n", output);
        for (unsigned i = 0; i < n; i += 1)
        {
            fprintf(output, "    sum += work_%u(count);\n", i);
        }
        fputs("    return fallback(sum, 3u);\n}\n", output);
        if (file != 0)
        {
            fprintf(output, "int main(void) { return aggregate(17u) != %uu; }\n", expected);
        }
        // The units driver's budget is paid = units + source_bytes / 32768. Six 32-KiB units of body.c, beside
        // entry.c, admit eight working lanes; a single --lanes command already asks for every lane it runs on.
        if (file == 0)
        {
            fputs("/*", output);
            long position = ftell(output);
            written = position >= 0;
            while (written && position < (long)(BUSTER_MULTICORE_UNIT_BYTES * BUSTER_MULTICORE_UNIT_LANES))
            {
                written = fputs(" fixture padding ", output) >= 0;
                position += 17;
            }
            fputs("*/\n", output);
        }
        written = written && !ferror(output);
    }
    if (entry != NULL)
    {
        fprintf(entry, "unsigned aggregate(unsigned);\nint main(void) { return aggregate(17u) != %uu; }\n", expected);
        written = written && !ferror(entry);
    }
    FILE* all[] = { body, single, entry };
    for (unsigned i = 0; i < 3; i += 1)
    {
        if (all[i] != NULL)
        {
            written = fclose(all[i]) == 0 && written;
        }
    }
    return written;
}

static bool read_report(MulticoreReport* report, unsigned lanes)
{
    *report = (MulticoreReport){ 0 };
    FILE* file = fopen("stderr.txt", "r");
    bool valid = file != NULL;
    unsigned tier_lines = 0;
    unsigned asked_lines = 0;
    unsigned lane_lines = 0;
    char line[BUSTER_MULTICORE_REPORT_BYTES];
    while (valid && fgets(line, sizeof(line), file) != NULL)
    {
        valid = strchr(line, '\n') != NULL;
        if (strncmp(line, "tier1\t", 6) == 0)
        {
            tier_lines += 1;
            snprintf(report->tier, sizeof(report->tier), "%s", line);
        }
        else if (sscanf(line, "time-process\tlanes-asked\t%u", &report->asked) == 1)
        {
            asked_lines += 1;
        }
        else if (sscanf(line, "time-process\tlanes\t%u", &report->lanes) == 1)
        {
            lane_lines += 1;
        }
    }
    if (file != NULL)
    {
        valid = !ferror(file) && valid;
        valid = fclose(file) == 0 && valid;
    }
    const char* selected = strstr(report->tier, "\ttier1=");
    valid = valid && tier_lines == 1 && sscanf(report->tier, "tier1\tfunctions=%u", &report->functions) == 1 &&
            selected != NULL && sscanf(selected, "\ttier1=%u", &report->tier1) == 1 && report->tier1 > 0 &&
            report->tier1 <= report->functions && asked_lines == 1 && lane_lines == 1 && report->asked == lanes &&
            report->lanes == lanes;
    if (!valid)
    {
        fprintf(stderr, "multicore: missing the tier/team receipt (wanted=%u, asked=%u, actual=%u, functions=%u, tier1=%u)\n",
                lanes, report->asked, report->lanes, report->functions, report->tier1);
    }
    return valid;
}

static bool compile(const char* compiler, unsigned lanes, bool scalar, bool multiple, bool object, bool perturb, MulticoreReport* report)
{
    char number[16];
    snprintf(number, sizeof(number), "%u", lanes);
    char* arguments[32];
    unsigned used = 0;
    arguments[used++] = (char*)compiler;
    arguments[used++] = "--tier1";
    arguments[used++] = "--tier1-report";
    arguments[used++] = "--tier1-refusals";
    arguments[used++] = "--time-stages";
    arguments[used++] = "-fno-asynchronous-unwind-tables";
    arguments[used++] = "--lanes";
    arguments[used++] = number;
    arguments[used++] = "--worker-budget=8";
    if (scalar)
    {
        arguments[used++] = "--scalar";
    }
    if (perturb)
    {
        if (multiple)
        {
            arguments[used++] = "--poison-arenas";
        }
        else
        {
            arguments[used++] = "--id-order";
            arguments[used++] = "149";
        }
    }
    if (object)
    {
        arguments[used++] = "-c";
    }
    else
    {
        arguments[used++] = "-o";
        arguments[used++] = "program";
    }
    arguments[used++] = multiple ? "body.c" : "single.c";
    if (multiple)
    {
        arguments[used++] = "entry.c";
    }
    arguments[used] = NULL;
    const char* outputs[] = { "program", "single.o", "body.o", "entry.o", "object-program" };
    bool fresh = true;
    for (unsigned i = 0; i < 5; i += 1)
    {
        const int removed = unlink(outputs[i]);
        fresh = fresh && (removed == 0 || errno == ENOENT);
    }
    return fresh && run(arguments) && read_report(report, lanes);
}

static int matrix(const char* compiler, unsigned maximum)
{
    bool passed = true;
    bool mixed = true;
    unsigned cohorts = 0;
    for (unsigned scalar = 0; passed && scalar < 2; scalar += 1)
    {
        for (unsigned multiple = 0; passed && multiple < 2; multiple += 1)
        {
            for (unsigned object = 0; passed && object < 2; object += 1)
            {
                const char* names[] = { object ? (multiple ? "body.o" : "single.o") : "program", "entry.o" };
                const char* saved[] = { "baseline.0", "baseline.1" };
                const unsigned count = object && multiple ? 2 : 1;
                MulticoreReport baseline;
                passed = compile(compiler, 1, scalar != 0, multiple != 0, object != 0, false, &baseline);
                mixed = passed && mixed && baseline.tier1 < baseline.functions;
                for (unsigned i = 0; passed && i < count; i += 1)
                {
                    passed = rename(names[i], saved[i]) == 0;
                }
                if (passed && !object)
                {
                    char* execute[] = { "./baseline.0", NULL };
                    passed = run(execute);
                }
                for (unsigned lanes = 1; passed && lanes <= maximum; lanes += 1)
                {
                    MulticoreReport current;
                    passed = compile(compiler, lanes, scalar != 0, multiple != 0, object != 0, true, &current);
                    passed = passed && strcmp(baseline.tier, current.tier) == 0;
                    mixed = passed && mixed && current.tier1 < current.functions;
                    for (unsigned i = 0; passed && i < count; i += 1)
                    {
                        passed = same_file(names[i], saved[i]);
                    }
                    if (passed && !object)
                    {
                        char* execute[] = { "./program", NULL };
                        passed = run(execute);
                    }
                    cohorts += passed;
                    if (!passed)
                    {
                        fprintf(stderr, "multicore: failed scalar=%u units=%u object=%u lanes=%u\n", scalar, multiple + 1, object, lanes);
                    }
                }
                // The -c cohorts prove byte equality; link the last lane's objects with the host driver and run them,
                // so that a pair of individually deterministic objects that doesn't form a working program still fails.
                if (passed && object && multiple)
                {
                    char* link[] = { "cc", "body.o", "entry.o", "-o", "object-program", NULL };
                    char* execute[] = { "./object-program", NULL };
                    passed = run(link) && run(execute);
                    if (!passed)
                    {
                        fprintf(stderr, "multicore: failed to link or run the -c objects (scalar=%u)\n", scalar);
                    }
                }
            }
        }
    }
    if (passed && cohorts != 8u * maximum)
    {
        fprintf(stderr, "multicore: ran %u cohorts, expected %u\n", cohorts, 8u * maximum);
        passed = false;
    }
    if (!passed)
    {
        printf("multicore: %u cohorts; requested and observed teams 1..%u; FAIL\n", cohorts, maximum);
        return 1;
    }
    if (!mixed)
    {
        printf("multicore: %u cohorts; requested and observed teams 1..%u; SKIP (Tier 1 covered every function)\n",
               cohorts, maximum);
        fprintf(stderr, "multicore: the mixed-tier fixture is stale: Tier 1 compiled every function; re-anchor it in "
                        "tests/driver/multicore.c and docs/multicore-acceptance.md\n");
        return 77;
    }
    printf("multicore: %u cohorts; requested and observed teams 1..%u; PASS\n", cohorts, maximum);
    return 0;
}

int main(int argc, char** argv)
{
    char compiler[PATH_MAX];
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    const bool configured = argc == 2 && realpath(argv[1], compiler) != NULL && sched_getaffinity(0, sizeof(allowed), &allowed) == 0;
    const unsigned cpus = configured ? (unsigned)CPU_COUNT(&allowed) : 0;
    const unsigned maximum = cpus < BUSTER_MULTICORE_MAX_LANES ? cpus : BUSTER_MULTICORE_MAX_LANES;
    const char* temporary = getenv("TMPDIR");
    if (temporary == NULL || temporary[0] == 0)
    {
        temporary = "/tmp";
    }
    char directory[PATH_MAX];
    const int named = snprintf(directory, sizeof(directory), "%s/buster-multicore-149-XXXXXX", temporary);
    const bool complete = named > 0 && (size_t)named < sizeof(directory);
    const int original = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const bool made = configured && maximum > 0 && complete && original >= 0 && mkdtemp(directory) != NULL;
    const bool entered = made && chdir(directory) == 0;
    int outcome = entered && write_sources() ? matrix(compiler, maximum) : 1;
    if (entered && outcome != 1)
    {
        DIR* listing = opendir(".");
        bool cleaned = listing != NULL;
        if (listing != NULL)
        {
            for (struct dirent* entry = readdir(listing); entry != NULL; entry = readdir(listing))
            {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                {
                    cleaned = unlink(entry->d_name) == 0 && cleaned;
                }
            }
            cleaned = closedir(listing) == 0 && cleaned;
        }
        if (!cleaned)
        {
            outcome = 1;
        }
    }
    if (original >= 0)
    {
        if (fchdir(original) != 0)
        {
            outcome = 1;
        }
        if (close(original) != 0)
        {
            outcome = 1;
        }
    }
    if (outcome != 1 && rmdir(directory) != 0)
    {
        outcome = 1;
    }
    if (outcome == 1)
    {
        if (!configured || maximum == 0)
        {
            fprintf(stderr, "multicore: usage: test-multicore COMPILER (with at least one allowed CPU)\n");
        }
        else
        {
            fprintf(stderr, "multicore: failed; inputs retained at %s\n", made ? directory : "(no directory created)");
        }
    }
    return outcome;
}
