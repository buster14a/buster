// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Native self-host qualification (#1111). Run from the repository root with build.sh's NUL-delimited
// source/flag exports. One unit and one compiler lane at a time bound bootstrap memory (#1109).
// All outputs live in a new directory: a failed command cannot reuse an earlier successful output.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_SELFHOST_LIST_BYTES (1024 * 1024)
#define BUSTER_SELFHOST_LIST_ARGS 4096
#define BUSTER_SELFHOST_LARGE_COUNT 8000
#define BUSTER_SELFHOST_PROBES 5

typedef struct Arguments Arguments;
struct Arguments
{
    char *data;
    char **values;
    size_t count;
};

typedef struct Qualification Qualification;
struct Qualification
{
    const char *directory;
    const char *seconds;
    FILE *timings;
    unsigned sequence;
};

static const char *const selfhost_smoke[] =
{
    "test-baked-tables",
    "test-lexer",
    "test-x86-encoder",
    "test-branch-sizing",
    "test-dataflow",
    "test-link",
    "test-types",
    "test-refinement",
    "test-lanes",
    "test-ir",
    "test-calling-convention",
    "test-diagnostics",
    "test-bindings",
    "test-x86-semantics",
};

static void fail(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    fputs("selfhost: ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
    exit(EXIT_FAILURE);
}

static void *allocate(size_t count, size_t size)
{
    void *result = calloc(count, size);
    if (!result)
    {
        fail("allocation failed");
    }
    return result;
}

static char *format_path(const char *format, ...)
{
    char *result = NULL;
    va_list arguments;
    va_start(arguments, format);
    int length = vasprintf(&result, format, arguments);
    va_end(arguments);
    if (length < 0)
    {
        fail("path allocation failed");
    }
    return result;
}

static FILE *open_file(const char *path, const char *mode)
{
    FILE *result = fopen(path, mode);
    if (!result)
    {
        fail("%s: %s", path, strerror(errno));
    }
    return result;
}

static void close_file(FILE *file)
{
    int error = ferror(file);
    if (fclose(file) != 0 || error)
    {
        fail("file I/O failed");
    }
}

static Arguments read_arguments(const char *path)
{
    FILE *file = open_file(path, "rb");
    Arguments result = {0};
    result.data = allocate(BUSTER_SELFHOST_LIST_BYTES + 1, 1);
    result.values = allocate(BUSTER_SELFHOST_LIST_ARGS + 1, sizeof(char *));
    size_t size = fread(result.data, 1, BUSTER_SELFHOST_LIST_BYTES + 1, file);
    close_file(file);
    if (!size || size > BUSTER_SELFHOST_LIST_BYTES || result.data[size - 1])
    {
        fail("%s: expected a nonempty, bounded, NUL-terminated argument list", path);
    }
    for (size_t offset = 0; offset < size;)
    {
        char *value = result.data + offset;
        size_t length = strlen(value);
        if (!length || result.count == BUSTER_SELFHOST_LIST_ARGS || strpbrk(value, "\r\n\t"))
        {
            fail("%s: empty, excessive, or multiline argument", path);
        }
        result.values[result.count++] = value;
        offset += length + 1;
    }
    return result;
}

static void free_arguments(Arguments arguments)
{
    free(arguments.values);
    free(arguments.data);
}

static off_t require_file(const char *path, bool executable)
{
    struct stat information;
    if (lstat(path, &information) != 0 || !S_ISREG(information.st_mode) || information.st_size == 0 || (executable && access(path, X_OK) != 0))
    {
        fail("%s: missing, empty, non-regular, or non-executable output", path);
    }
    return information.st_size;
}

static void require_absent(const char *path)
{
    struct stat information;
    if (lstat(path, &information) == 0 || errno != ENOENT)
    {
        fail("%s: output must not exist", path);
    }
}

static bool files_equal(const char *left, const char *right)
{
    FILE *a = open_file(left, "rb");
    FILE *b = open_file(right, "rb");
    unsigned char a_bytes[16384];
    unsigned char b_bytes[16384];
    bool equal = true;
    size_t a_size;
    size_t b_size;
    do
    {
        a_size = fread(a_bytes, 1, sizeof(a_bytes), a);
        b_size = fread(b_bytes, 1, sizeof(b_bytes), b);
        equal = a_size == b_size && memcmp(a_bytes, b_bytes, a_size) == 0;
    } while (equal && a_size);
    close_file(a);
    close_file(b);
    return equal;
}

static uint64_t now_ns(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    {
        fail("clock_gettime: %s", strerror(errno));
    }
    return (uint64_t)time.tv_sec * 1000000000 + (uint64_t)time.tv_nsec;
}

static uint64_t run_command(Qualification *qualification, const char *phase, const char *label, char *const arguments[])
{
    size_t count = 0;
    while (arguments[count])
    {
        count++;
    }
    char **command = allocate(count + 6, sizeof(char *));
    command[0] = "timeout";
    command[1] = "--signal=TERM";
    command[2] = "--kill-after=10s";
    command[3] = (char *)qualification->seconds;
    memcpy(command + 4, arguments, (count + 1) * sizeof(char *));
    char *log = format_path("%s/%03u.log", qualification->directory, qualification->sequence);
    int descriptor = open(log, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0)
    {
        fail("%s: %s", log, strerror(errno));
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (dprintf(descriptor, "argv[%zu] = %s\n", i, arguments[i]) < 0)
        {
            fail("cannot write command log");
        }
    }
    fflush(stdout);
    uint64_t start = now_ns();
    pid_t child = fork();
    if (child < 0)
    {
        fail("fork: %s", strerror(errno));
    }
    if (child == 0)
    {
        if (dup2(descriptor, STDOUT_FILENO) < 0 || dup2(descriptor, STDERR_FILENO) < 0)
        {
            _exit(127);
        }
        close(descriptor);
        execvp(command[0], command);
        dprintf(STDERR_FILENO, "exec timeout: %s\n", strerror(errno));
        _exit(127);
    }
    close(descriptor);
    int status = 0;
    struct rusage usage = {0};
    pid_t waited;
    do
    {
        waited = wait4(child, &status, 0, &usage);
    } while (waited < 0 && errno == EINTR);
    if (waited != child)
    {
        fail("wait4: %s", strerror(errno));
    }
    uint64_t elapsed = now_ns() - start;
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    fprintf(qualification->timings, "%u\t%s\t%s\t%llu\t%ld\t%d\n", qualification->sequence, phase, label, (unsigned long long)elapsed, usage.ru_maxrss, code);
    if (fflush(qualification->timings) != 0)
    {
        fail("cannot flush timings");
    }
    printf("selfhost: %s %s: %.3f s, %ld KiB, status %d\n", phase, label, (double)elapsed / 1e9, usage.ru_maxrss, code);
    if (code != 0)
    {
        fail("%s failed; command and diagnostics: %s", label, log);
    }
    qualification->sequence++;
    free(log);
    free(command);
    return elapsed;
}

static void check_inventory(void)
{
    FILE *file = open_file("tools/ci/correctness.tests", "r");
    bool found[sizeof(selfhost_smoke) / sizeof(selfhost_smoke[0])] = {0};
    char *line = NULL;
    size_t capacity = 0;
    while (getline(&line, &capacity, file) >= 0)
    {
        for (size_t i = 0; i < sizeof(found) / sizeof(found[0]); ++i)
        {
            const char *prefix = "./build/compiler ";
            size_t prefix_size = strlen(prefix);
            size_t name_size = strlen(selfhost_smoke[i]);
            if (strncmp(line, prefix, prefix_size) == 0 && strncmp(line + prefix_size, selfhost_smoke[i], name_size) == 0)
            {
                char end = line[prefix_size + name_size];
                found[i] |= end == '\0' || end == '\n' || end == ' ' || end == '\t';
            }
        }
    }
    close_file(file);
    free(line);
    for (size_t i = 0; i < sizeof(found) / sizeof(found[0]); ++i)
    {
        if (!found[i])
        {
            fail("smoke command no longer in correctness.tests: %s", selfhost_smoke[i]);
        }
    }
}

static void write_probe(const char *path, unsigned probe)
{
    FILE *file = open_file(path, "wx");
    if (probe == 0)
    {
        fputs("int main(void) { volatile unsigned x = 0xffffffffu; volatile int y = 2147483647; volatile char c = (char)255; return (x + 1u != 0u) || (y + 1 != (-2147483647 - 1)) || c != 255; }\n", file);
    }
    else if (probe == 1)
    {
        fputs("struct Pair { long x; double y; }; struct Pair adjust(struct Pair p) { p.x += 7; p.y *= 2.0; return p; } int main(void) { struct Pair p = { 5, 3.25 }; struct Pair (*f)(struct Pair) = adjust; p = f(p); return p.x != 12 || p.y != 6.5; }\n", file);
    }
    else if (probe == 2)
    {
        fputs("static int a[4] = { 3, 5, 8, 13 }; static int *p = a + 2; static const char text[] = \"buster\"; int plus(int x) { return x + 9; } static int (*f)(int) = plus; int main(void) { int total = 0; for (int i = 0; i < 4; ++i) total += a[i]; return total != 29 || *p != 8 || text[3] != 't' || f(4) != 13; }\n", file);
    }
    else if (probe == 3)
    {
        for (unsigned i = 0; i < BUSTER_SELFHOST_LARGE_COUNT; ++i)
        {
            fprintf(file, "unsigned function_%u(unsigned x) { return x + %uu; }\n", i, i);
        }
        fputs("int main(void) { unsigned sum = 0;\n", file);
        for (unsigned i = 0; i < BUSTER_SELFHOST_LARGE_COUNT; ++i)
        {
            fprintf(file, "sum += function_%u(3u);\n", i);
        }
        unsigned expected = BUSTER_SELFHOST_LARGE_COUNT * 3 + BUSTER_SELFHOST_LARGE_COUNT * (BUSTER_SELFHOST_LARGE_COUNT - 1) / 2;
        fprintf(file, "return sum != %uu; }\n", expected);
    }
    else
    {
        fputs("unsigned choose(unsigned x) { switch (x) {\n", file);
        unsigned expected = 0;
        for (unsigned i = 0; i < BUSTER_SELFHOST_LARGE_COUNT; ++i)
        {
            unsigned value = (i * 17u) ^ 91u;
            fprintf(file, "case %uu: return %uu;\n", i, value);
            expected += value;
        }
        fprintf(file, "default: return 123u; } } int main(void) { unsigned sum = 0; for (unsigned i = 0; i < %uu; ++i) sum += choose(i); return sum != %uu || choose(%uu) != 123u; }\n", BUSTER_SELFHOST_LARGE_COUNT, expected, BUSTER_SELFHOST_LARGE_COUNT);
    }
    close_file(file);
}

static uint64_t compile(Qualification *qualification, const char *compiler, Arguments flags, const char *source, const char *output, bool object, const char *phase)
{
    require_absent(output);
    char **arguments = allocate(flags.count + 9, sizeof(char *));
    size_t count = 0;
    arguments[count++] = (char *)compiler;
    arguments[count++] = "--lanes";
    arguments[count++] = "1";
    for (size_t i = 0; i < flags.count; ++i)
    {
        arguments[count++] = flags.values[i];
    }
    if (object)
    {
        arguments[count++] = "-c";
    }
    arguments[count++] = "-o";
    arguments[count++] = (char *)output;
    arguments[count++] = (char *)source;
    uint64_t elapsed = run_command(qualification, phase, source, arguments);
    require_file(output, !object);
    free(arguments);
    return elapsed;
}

static int qualify(int argc, char **argv)
{
    if (argc != 5 && argc != 6)
    {
        fail("usage: selfhost STAGE1 SOURCES.argv FLAGS.argv NEW_DIRECTORY [COMMAND_TIMEOUT_SECONDS]");
    }
    const char *seconds = argc == 6 ? argv[5] : "600";
    char *end = NULL;
    long timeout = strtol(seconds, &end, 10);
    if (!*seconds || *end || timeout < 1 || timeout > 3600)
    {
        fail("command timeout must be between 1 and 3600 seconds");
    }
    Arguments sources = read_arguments(argv[2]);
    Arguments flags = read_arguments(argv[3]);
    // Match #1111's default-tier bootstrap instead of inheriting Clang's -O3 and warning policy.
    // Preserve the explicit ABI, overflow, char, aliasing and recorded host-default options.
    size_t kept = 0;
    for (size_t i = 0; i < flags.count; ++i)
    {
        const char *flag = flags.values[i];
        if (strncmp(flag, "-fsanitize", 10) == 0 || strncmp(flag, "-DBUSTER_FUZZ", 13) == 0 || strncmp(flag, "-g", 2) == 0)
        {
            fail("use a plain, no-debug build.sh flag export, not %s", flag);
        }
        if (strcmp(flag, "-O3") != 0 && strcmp(flag, "-Wall") != 0 && strcmp(flag, "-Werror") != 0)
        {
            flags.values[kept++] = flags.values[i];
        }
    }
    flags.count = kept;
    flags.values[kept] = NULL;
    for (size_t i = 0; i < sources.count; ++i)
    {
        require_file(sources.values[i], false);
        for (size_t j = 0; j < i; ++j)
        {
            if (strcmp(sources.values[i], sources.values[j]) == 0)
            {
                fail("duplicate source: %s", sources.values[i]);
            }
        }
    }
    check_inventory();
    char *stage1 = realpath(argv[1], NULL);
    if (!stage1)
    {
        fail("cannot resolve stage 1: %s", strerror(errno));
    }
    off_t stage1_bytes = require_file(stage1, true);
    if (mkdir(argv[4], 0700) != 0)
    {
        fail("%s: a fresh output directory is required: %s", argv[4], strerror(errno));
    }
    char *directory = realpath(argv[4], NULL);
    if (!directory || setenv("LC_ALL", "C", 1) != 0 || setenv("TZ", "UTC", 1) != 0 || setenv("SOURCE_DATE_EPOCH", "1792454400", 1) != 0)
    {
        fail("cannot initialize qualification environment");
    }
    char *timings_path = format_path("%s/timings.tsv", directory);
    Qualification qualification =
    {
        .directory = directory,
        .seconds = seconds,
        .timings = open_file(timings_path, "wx"),
        .sequence = 0,
    };
    fputs("command\tphase\tinput\telapsed_ns\tmaxrss_kib\texit_status\n", qualification.timings);
    uint64_t start = now_ns();
    uint64_t bootstrap_ns = 0;
    char **objects = allocate(sources.count + 1, sizeof(char *));
    for (size_t i = 0; i < sources.count; ++i)
    {
        objects[i] = format_path("%s/unit-%04zu.o", directory, i);
        bootstrap_ns += compile(&qualification, stage1, flags, sources.values[i], objects[i], true, "bootstrap-unit");
    }
    char *stage2 = format_path("%s/compiler", directory);
    char **link = allocate(sources.count + 8, sizeof(char *));
    link[0] = "clang";
    link[1] = "-no-pie";
    for (size_t i = 0; i < sources.count; ++i)
    {
        link[2 + i] = objects[i];
    }
    size_t next = sources.count + 2;
    link[next++] = "-lm";
    link[next++] = "-ldl";
    link[next++] = "-lpthread";
    link[next++] = "-o";
    link[next++] = stage2;
    uint64_t link_ns = run_command(&qualification, "bootstrap-link", "compiler", link);
    off_t stage2_bytes = require_file(stage2, true);
    uint64_t smoke_ns = 0;
    size_t smoke_count = sizeof(selfhost_smoke) / sizeof(selfhost_smoke[0]);
    for (size_t i = 0; i < smoke_count; ++i)
    {
        char *arguments[] = {stage2, (char *)selfhost_smoke[i], NULL, NULL};
        if (strcmp(selfhost_smoke[i], "test-refinement") == 0)
        {
            arguments[2] = "src";
        }
        smoke_ns += run_command(&qualification, "smoke", selfhost_smoke[i], arguments);
    }
    uint64_t probe_start = now_ns();
    for (unsigned probe = 0; probe < BUSTER_SELFHOST_PROBES; ++probe)
    {
        char *source = format_path("%s/probe-%u.c", directory, probe);
        write_probe(source, probe);
        for (unsigned mode = 0; mode < 2; ++mode)
        {
            const char *extension = mode ? "exe" : "o";
            char *output = format_path("%s/probe-%u.%s", directory, probe, extension);
            char *saved[2];
            for (unsigned stage = 0; stage < 2; ++stage)
            {
                // Both compilers see exactly the same input and -o paths; move each fresh result out of the way.
                compile(&qualification, stage ? stage2 : stage1, flags, source, output, !mode, stage ? "probe-stage2" : "probe-stage1");
                saved[stage] = format_path("%s/probe-%u.stage%u.%s", directory, probe, stage + 1, extension);
                if (rename(output, saved[stage]) != 0)
                {
                    fail("cannot preserve probe output: %s", strerror(errno));
                }
            }
            if (!files_equal(saved[0], saved[1]))
            {
                fail("probe %u %s differs: %s and %s", probe, extension, saved[0], saved[1]);
            }
            if (mode)
            {
                for (unsigned stage = 0; stage < 2; ++stage)
                {
                    char *arguments[] = {saved[stage], NULL};
                    run_command(&qualification, "probe-execute", saved[stage], arguments);
                }
            }
            free(saved[0]);
            free(saved[1]);
            free(output);
        }
        free(source);
    }
    uint64_t probe_ns = now_ns() - probe_start;
    close_file(qualification.timings);
    char *summary_path = format_path("%s/summary.md", directory);
    FILE *summary = open_file(summary_path, "wx");
    fprintf(summary, "## Native self-host qualification\n\nPASS: %zu units; %zu smoke commands; %u probes; %u byte comparisons; %u executable runs.\n\n", sources.count, smoke_count, BUSTER_SELFHOST_PROBES, BUSTER_SELFHOST_PROBES * 2, BUSTER_SELFHOST_PROBES * 2);
    fprintf(summary, "| Phase | Wall seconds |\n| --- | ---: |\n| Sequential unit compilation | %.3f |\n| Host link | %.3f |\n| Stage 2 smoke | %.3f |\n| Probe compilation, comparison and execution | %.3f |\n| Total | %.3f |\n\n", (double)bootstrap_ns / 1e9, (double)link_ns / 1e9, (double)smoke_ns / 1e9, (double)probe_ns / 1e9, (double)(now_ns() - start) / 1e9);
    fprintf(summary, "Stage 1: %lld bytes. Stage 2: %lld bytes. Per-command time, peak RSS and exit status are in `timings.tsv`.\n\nThis is probe-output equivalence, not a full stage-2/stage-3 compiler-binary fixed point or Tier 1 qualification.\n", (long long)stage1_bytes, (long long)stage2_bytes);
    close_file(summary);
    char *pass_path = format_path("%s/PASS", directory);
    FILE *pass = open_file(pass_path, "wx");
    fprintf(pass, "units=%zu smoke=%zu probes=%u comparisons=%u executions=%u\n", sources.count, smoke_count, BUSTER_SELFHOST_PROBES, BUSTER_SELFHOST_PROBES * 2, BUSTER_SELFHOST_PROBES * 2);
    close_file(pass);
    for (size_t i = 0; i < sources.count; ++i)
    {
        free(objects[i]);
    }
    free(objects);
    free(link);
    free(stage1);
    free(stage2);
    free(directory);
    free(timings_path);
    free(summary_path);
    free(pass_path);
    free_arguments(flags);
    free_arguments(sources);
    return 0;
}

int main(int argc, char **argv)
{
    int result = 0;
    if (argc == 2 && strcmp(argv[1], "--check-inventory") == 0)
    {
        check_inventory();
    }
    else
    {
        result = qualify(argc, argv);
    }
    return result;
}
