// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Correctness regression for #865, not a performance benchmark. This uses the production stage clock.
// With a compiler argument it also compiles an identical original C fixture from both launchers,
// compares the objects, and checks measure-compile/time-runs' explicit wait4 contracts.
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#define BUSTER_ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))
#include "../src/buster/compiler/c_check_clock.c"

#define BUSTER_RSS_MIB ((size_t)1024 * 1024)
#define BUSTER_RSS_TOLERANCE_KIB (16u * 1024u)

typedef struct RssReport RssReport;
struct RssReport
{
    uint64_t peak;
    uint64_t start;
    uint64_t rusage;
    uint64_t waited;
    uint64_t added[CHECK_STAGE_COUNT];
};

static uint32_t rss_checks;
static uint32_t rss_failures;

static void rss_expect(bool condition, const char* name)
{
    rss_checks += 1;
    rss_failures += !condition;
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
    }
}

static char* rss_text(FILE* stream)
{
    char* text = NULL;
    const bool seeked = stream != NULL && fseek(stream, 0, SEEK_END) == 0;
    const long length = seeked ? ftell(stream) : -1;
    if (length >= 0 && length < 1024 * 1024 && fseek(stream, 0, SEEK_SET) == 0)
    {
        text = malloc((size_t)length + 1);
        if (text != NULL && fread(text, 1, (size_t)length, stream) != (size_t)length)
        {
            free(text);
            text = NULL;
        }
        if (text != NULL)
        {
            text[length] = 0;
        }
    }
    return text;
}

static char* rss_report_text(const CheckClock* clock)
{
    FILE* stream = tmpfile();
    char* text = NULL;
    if (stream != NULL)
    {
        const uint64_t phases[BUSTER_CHECK_CLOCK_PHASES - 1] = { 0 };
        check_clock_report(clock, phases, BUSTER_ARRAY_LENGTH(phases), stream);
        text = rss_text(stream);
        fclose(stream);
    }
    return text;
}

static bool rss_value(const char* text, const char* name, uint64_t* value)
{
    char key[96];
    snprintf(key, sizeof(key), "time-process\t%s\t", name);
    const char* at = text != NULL ? strstr(text, key) : NULL;
    bool valid = at != NULL && (at == text || at[-1] == '\n');
    if (valid)
    {
        at += strlen(key);
        char* end = NULL;
        errno = 0;
        const unsigned long long number = strtoull(at, &end, 10);
        valid = *at >= '0' && *at <= '9' && errno == 0 && end != at && *end == '\n';
        *value = valid ? (uint64_t)number : 0;
    }
    return valid;
}

static bool rss_report_read(const char* text, RssReport* report)
{
    double stages[CHECK_STAGE_COUNT] = { 0 };
    double phases[BUSTER_CHECK_CLOCK_PHASES] = { 0 };
    double usage[3 * CHECK_STAGE_COUNT] = { 0 };
    double total = 0;
    double walk = 0;
    bool valid = text != NULL && check_clock_read(text, stages, phases, &total, &walk, usage);
    valid = valid && strstr(text, "time-process\tpeak-rss-status\tavailable\n") != NULL &&
            strstr(text, "time-process\tpeak-rss-source\tproc-self-status-vmhwm\n") != NULL &&
            strstr(text, "time-process\tpeak-rss-scope\tpost-exec-through-last-mark\n") != NULL &&
            strstr(text, "time-process\tpeak-rss-precision\tapproximate\n") != NULL;
    valid = valid && rss_value(text, "peak-rss-kb", &report->peak) && rss_value(text, "peak-rss-start-kb", &report->start) &&
            rss_value(text, "rusage-peak-rss-kb", &report->rusage);
    uint64_t sum = 0;
    for (uint32_t s = 0; valid && s < CHECK_STAGE_COUNT; s += 1)
    {
        valid = usage[3 * s + 2] >= 0 && usage[3 * s + 2] < (double)UINT64_MAX;
        if (valid)
        {
            report->added[s] = (uint64_t)usage[3 * s + 2];
            sum += report->added[s];
        }
    }
    // This is an accounting identity, not an assertion that VmHWM equals exact physical memory.
    valid = valid && report->peak >= report->start && sum == report->peak - report->start;
    return valid;
}

static void* rss_touch(size_t bytes)
{
    void* memory = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory != MAP_FAILED)
    {
        // Linux/x86-64's base pages; volatile stores prevent dead-store elimination.
        volatile unsigned char* pages = memory;
        for (size_t p = 0; p < bytes; p += 4096)
        {
            pages[p] = 1;
        }
    }
    return memory;
}

static int rss_workload(void)
{
    CheckClock clock;
    check_clock_start(&clock);
    void* first = rss_touch(32 * BUSTER_RSS_MIB);
    const bool first_ok = first != MAP_FAILED;
    check_clock_mark(&clock, CHECK_STAGE_PREPROCESS);
    if (first_ok)
    {
        munmap(first, 32 * BUSTER_RSS_MIB);
    }
    void* second = rss_touch(64 * BUSTER_RSS_MIB);
    const bool second_ok = second != MAP_FAILED;
    if (second_ok)
    {
        // Release before the mark: a VmRSS sampler would miss this peak, but VmHWM must retain it.
        munmap(second, 64 * BUSTER_RSS_MIB);
    }
    check_clock_mark(&clock, CHECK_STAGE_PARSE);
    check_clock_mark(&clock, CHECK_STAGE_RELEASE);
    const uint64_t phases[BUSTER_CHECK_CLOCK_PHASES - 1] = { 0 };
    check_clock_report(&clock, phases, BUSTER_ARRAY_LENGTH(phases), stdout);
    return first_ok && second_ok ? 0 : 1;
}

// Padding is touched in the parent and kept resident until the fork/exec child exits.
static bool rss_run(char* const* arguments, size_t padding, bool capture_stdout, RssReport* report, char** text)
{
    *text = NULL;
    FILE* stream = tmpfile();
    void* memory = padding != 0 ? rss_touch(padding) : NULL;
    const bool ready = stream != NULL && memory != MAP_FAILED;
    fflush(NULL);
    const pid_t child = ready ? fork() : -1;
    if (child == 0)
    {
        const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null != -1 && dup2(null, STDIN_FILENO) != -1 &&
            dup2(capture_stdout ? fileno(stream) : null, STDOUT_FILENO) != -1 &&
            dup2(capture_stdout ? null : fileno(stream), STDERR_FILENO) != -1)
        {
            execv(arguments[0], arguments);
        }
        _exit(127);
    }
    struct rusage usage = { 0 };
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? wait4(child, &status, 0, &usage) : -1;
    }
    while (child > 0 && waited == -1 && errno == EINTR);
    if (memory != NULL && memory != MAP_FAILED)
    {
        munmap(memory, padding);
    }
    if (stream != NULL)
    {
        *text = rss_text(stream);
        fclose(stream);
    }
    bool valid = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 && *text != NULL;
    if (valid && report != NULL)
    {
        report->waited = (uint64_t)usage.ru_maxrss;
        valid = rss_report_read(*text, report);
    }
    if (!valid && *text != NULL)
    {
        fprintf(stderr, "%s", *text);
    }
    return valid;
}

static uint64_t rss_distance(uint64_t a, uint64_t b)
{
    return a > b ? a - b : b - a;
}

static void rss_compare(const RssReport* small, const RssReport* large, size_t padding)
{
    const uint64_t tolerance = small->peak / 4 > BUSTER_RSS_TOLERANCE_KIB ? small->peak / 4 : BUSTER_RSS_TOLERANCE_KIB;
    rss_expect(rss_distance(small->peak, large->peak) <= tolerance, "post-exec peaks are independent of launcher size");
    rss_expect(small->peak > small->start && large->peak > large->start, "both launchers retain positive stage attribution");
    rss_expect(large->rusage >= padding / 1024 * 3 / 4 && large->waited >= padding / 1024 * 3 / 4, "rusage and wait4 expose the large pre-exec image");
    rss_expect(large->rusage > large->peak + 32 * 1024 && large->waited > large->peak + 32 * 1024, "raw inherited peaks remain separate from compiler-only peaks");
    for (uint32_t s = 0; s < CHECK_STAGE_COUNT; s += 1)
    {
        rss_expect(rss_distance(small->added[s], large->added[s]) <= tolerance, check_stage_names[s]);
    }
}

static void rss_parser_tests(void)
{
    struct
    {
        const char* text;
        bool valid;
        uint64_t value;
    } const cases[] = {
        { "", false, 0 },
        { "VmRSS:\t42 kB\n", false, 0 },
        { "VmHWM:\t0 kB\n", true, 0 },
        { "Name:\tx\nVmHWM:\t12345 kB\nVmRSS:\t1 kB\n", true, 12345 },
        { "VmHWM:  18446744073709551615 kB \t\n", true, UINT64_MAX },
        { "VmHWM: 18446744073709551616 kB\n", false, 0 },
        { "VmHWM: -1 kB\n", false, 0 },
        { "VmHWM: +1 kB\n", false, 0 },
        { "VmHWM: kB\n", false, 0 },
        { "VmHWM: 1 MB\n", false, 0 },
        { "VmHWM: 1kB\n", false, 0 },
        { "VmHWM: 1 kB extra\n", false, 0 },
        { "VmHWM: 1 kB", false, 0 },
        { "VmHWM: 1 kB\nVmHWM: 2 kB\n", false, 0 },
    };
    for (size_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        uint64_t value = 77;
        const bool valid = check_clock_peak_parse(cases[i].text, strlen(cases[i].text), &value);
        rss_expect(valid == cases[i].valid && value == cases[i].value, "bounded VmHWM parser");
    }
    const long page = sysconf(_SC_PAGESIZE);
    char* guard = page > 0 ? mmap(NULL, (size_t)page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const bool guarded = guard != MAP_FAILED && mprotect(guard, (size_t)page, PROT_READ | PROT_WRITE) == 0;
    rss_expect(guarded, "guard-page setup");
    if (guarded)
    {
        const char field[] = "VmHWM:\t123 kB\n";
        char* at = guard + page - (sizeof(field) - 1);
        memcpy(at, field, sizeof(field) - 1);
        uint64_t value = 0;
        rss_expect(check_clock_peak_parse(at, sizeof(field) - 1, &value) && value == 123, "non-NUL-terminated field at a guard page");
        rss_expect(!check_clock_peak_parse(at, sizeof(field) - 2, &value), "truncated field at a guard page");
    }
    if (guard != MAP_FAILED)
    {
        munmap(guard, (size_t)page * 2);
    }
    uint64_t value = 77;
    rss_expect(!check_clock_peak_read("/proc/self/no-such-rss-status", &value) && value == 0, "missing status is unavailable");
    char name[] = "/tmp/buster-rss-status-XXXXXX";
    const int file = mkstemp(name);
    rss_expect(file != -1, "status fixture created");
    if (file != -1)
    {
        rss_expect(!check_clock_peak_read(name, &value), "empty status is unavailable");
        char oversized[BUSTER_CHECK_RSS_STATUS_CAPACITY + 1];
        memset(oversized, ' ', sizeof(oversized));
        memcpy(oversized, "VmHWM: 1 kB\n", sizeof("VmHWM: 1 kB\n") - 1);
        rss_expect(write(file, oversized, sizeof(oversized)) == (ssize_t)sizeof(oversized), "oversized fixture written");
        rss_expect(!check_clock_peak_read(name, &value), "oversized status is unavailable even with an early field");
        close(file);
        unlink(name);
    }
}

static bool rss_reader_accepts(const char* text)
{
    double stages[CHECK_STAGE_COUNT] = { 0 };
    double phases[BUSTER_CHECK_CLOCK_PHASES] = { 0 };
    double usage[3 * CHECK_STAGE_COUNT] = { 0 };
    double total = 0;
    double walk = 0;
    return check_clock_read(text, stages, phases, &total, &walk, usage);
}

static bool rss_unavailable_report(const char* text)
{
    double stages[CHECK_STAGE_COUNT] = { 0 };
    double phases[BUSTER_CHECK_CLOCK_PHASES] = { 0 };
    double usage[3 * CHECK_STAGE_COUNT] = { 0 };
    double total = 0;
    double walk = 0;
    bool valid = text != NULL && check_clock_read(text, stages, phases, &total, &walk, usage) &&
                 strstr(text, "time-process\tpeak-rss-kb\t-1\n") != NULL &&
                 strstr(text, "time-process\tpeak-rss-status\tunavailable\n") != NULL;
    for (uint32_t s = 0; valid && s < CHECK_STAGE_COUNT; s += 1)
    {
        valid = usage[3 * s + 2] == -1;
    }
    return valid;
}

static void rss_failure_tests(void)
{
    struct rlimit saved;
    const bool limited = getrlimit(RLIMIT_NOFILE, &saved) == 0;
    rss_expect(limited, "read descriptor limit");
    for (uint32_t at_start = 0; limited && at_start < 2; at_start += 1)
    {
        CheckClock clock;
        if (!at_start)
        {
            check_clock_start(&clock);
            rss_expect(clock.peak_available, "RSS available before injected failure");
        }
        struct rlimit closed = saved;
        closed.rlim_cur = 0;
        const bool injected = setrlimit(RLIMIT_NOFILE, &closed) == 0;
        rss_expect(injected, "inject open failure");
        if (at_start)
        {
            check_clock_start(&clock);
        }
        else
        {
            check_clock_mark(&clock, CHECK_STAGE_SETUP);
        }
        const bool restored = setrlimit(RLIMIT_NOFILE, &saved) == 0;
        rss_expect(restored, "restore descriptor limit");
        check_clock_mark(&clock, CHECK_STAGE_PARSE);
        rss_expect(injected && !clock.peak_available, "unavailable RSS remains latched after recovery");
        char* text = rss_report_text(&clock);
        rss_expect(rss_unavailable_report(text), "unavailable report is readable without inventing zero peaks");
        if (text != NULL)
        {
            char* status = strstr(text, "time-process\tpeak-rss-status\t");
            if (status != NULL)
            {
                status[0] = 'X';
                rss_expect(!rss_reader_accepts(text), "unavailable sentinels need an explicit status");
                status[0] = 't';
            }
            char* row = strstr(text, "time-stage\tsetup\t");
            char* end = row != NULL ? strchr(row, '\n') : NULL;
            if (end != NULL && end - row >= 2)
            {
                // Turn just one -1 into 0 without shifting the text: mixed availability must fail closed.
                end[-2] = ' ';
                end[-1] = '0';
                rss_expect(!rss_reader_accepts(text), "mixed available/unavailable stage peaks are rejected");
            }
        }
        free(text);
    }
}

static bool rss_same_files(const char* a, const char* b)
{
    FILE* left = fopen(a, "rb");
    FILE* right = fopen(b, "rb");
    bool same = left != NULL && right != NULL;
    bool more = same;
    while (more)
    {
        char first[4096];
        char second[4096];
        const size_t got = fread(first, 1, sizeof(first), left);
        same = fread(second, 1, sizeof(second), right) == got && memcmp(first, second, got) == 0;
        more = same && got != 0;
    }
    same = same && !ferror(left) && !ferror(right);
    if (left != NULL)
    {
        fclose(left);
    }
    if (right != NULL)
    {
        fclose(right);
    }
    return same;
}

static void rss_compiler_tests(const char* compiler)
{
    char directory[] = "/tmp/buster-rss-compiler-XXXXXX";
    const bool made = mkdtemp(directory) != NULL;
    rss_expect(made, "compiler fixture directory");
    if (made)
    {
        char source[256];
        char objects[2][256];
        snprintf(source, sizeof(source), "%s/input.c", directory);
        snprintf(objects[0], sizeof(objects[0]), "%s/small.o", directory);
        snprintf(objects[1], sizeof(objects[1]), "%s/large.o", directory);
        FILE* file = fopen(source, "w");
        bool written = file != NULL;
        for (uint32_t f = 0; written && f < 4096; f += 1)
        {
            written = fprintf(file, "unsigned rss_function_%u(unsigned x) { return (x + %uu) * 3u; }\n", f, f) > 0;
        }
        if (file != NULL)
        {
            written = fclose(file) == 0 && written;
        }
        rss_expect(written, "original compiler fixture written");
        RssReport reports[2] = { 0 };
        size_t padding = 0;
        bool good = written;
        for (uint32_t p = 0; good && p < 2; p += 1)
        {
            char* const arguments[] = { (char*)compiler, "--time-stages", "--lanes", "1", "-c", source, "-o", objects[p], NULL };
            char* text = NULL;
            good = rss_run(arguments, p == 0 ? 0 : padding, false, &reports[p], &text);
            rss_expect(good, "compiler report and stage accounting identity");
            free(text);
            if (p == 0 && good)
            {
                // Bound test memory. The original fixture should be far below this ceiling.
                good = reports[0].peak < 768 * 1024;
                rss_expect(good, "compiler fixture fits the regression's memory bound");
                padding = good ? (size_t)(reports[0].peak + 128 * 1024) * 1024 : 0;
            }
        }
        if (good)
        {
            rss_compare(&reports[0], &reports[1], padding);
            rss_expect(rss_same_files(objects[0], objects[1]), "identical objects from both launchers");
            char* const measure[] = { (char*)compiler, "measure-compile", "--runs", "1", "--warmups", "0", "--", "--lanes", "1", "-c", source, NULL };
            char* text = NULL;
            const bool measured = rss_run(measure, 0, true, NULL, &text);
            rss_expect(measured && strstr(text, "KiB wait4 peak") != NULL && strstr(text, "include pre-exec") != NULL &&
                       strstr(text, "approximate post-exec peak KiB added") != NULL, "measure-compile distinguishes RSS scopes");
            free(text);
            char* const profile[] = { (char*)compiler, "time-runs", "--runs", "1", "--warmups", "1", "--executable", "/bin/true", "--list", "ignored", NULL };
            text = NULL;
            const bool profiled = rss_run(profile, 0, true, NULL, &text);
            rss_expect(profiled && strstr(text, "KiB wait4 peak") != NULL && strstr(text, "executable-only peak unavailable") != NULL,
                       "time-runs does not claim an executable-only peak");
            free(text);
        }
        unlink(source);
        unlink(objects[0]);
        unlink(objects[1]);
        rmdir(directory);
    }
}

int main(int argc, char** argv)
{
    int result = 0;
    if (argc == 2 && strcmp(argv[1], "--workload") == 0)
    {
        result = rss_workload();
    }
    else
    {
        rss_parser_tests();
        rss_failure_tests();
        RssReport reports[2] = { 0 };
        bool good = true;
        const size_t padding = 256 * BUSTER_RSS_MIB;
        char* const workload[] = { "/proc/self/exe", "--workload", NULL };
        for (uint32_t p = 0; p < 2; p += 1)
        {
            char* text = NULL;
            const bool ran = rss_run(workload, p == 0 ? 0 : padding, true, &reports[p], &text);
            rss_expect(ran, "fresh-exec workload report and stage accounting identity");
            good = good && ran;
            free(text);
        }
        if (good)
        {
            rss_compare(&reports[0], &reports[1], padding);
            for (uint32_t p = 0; p < 2; p += 1)
            {
                rss_expect(reports[p].peak >= 56 * 1024 && reports[p].peak < 96 * 1024, "post-exec peak covers the workload, not the launcher");
                rss_expect(reports[p].added[CHECK_STAGE_PREPROCESS] >= 24 * 1024 && reports[p].added[CHECK_STAGE_PREPROCESS] <= 40 * 1024,
                           "first stage's 32 MiB growth remains attributable");
                rss_expect(reports[p].added[CHECK_STAGE_PARSE] >= 24 * 1024 && reports[p].added[CHECK_STAGE_PARSE] <= 40 * 1024,
                           "second stage retains the high-water mark even after unmapping");
            }
        }
        if (argc == 2)
        {
            rss_compiler_tests(argv[1]);
        }
        rss_expect(argc <= 2, "usage: test-peak-rss [compiler]");
        printf("peak-rss: %u checks, %u failures\n", rss_checks, rss_failures);
        result = rss_failures != 0;
    }
    return result;
}
