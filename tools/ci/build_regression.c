// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The build's regressions (.github/workflows/build.yml; docs/commands/build.md). From the repository root, with clang,
// cmake, ctest and ninja on PATH:
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror -o build-regression tools/ci/build_regression.c && ./build-regression
//   ./build-regression --flags-only  # Just the fresh-tree ignored-flag regressions; cleans the selected build tree.
//   ./build-regression --compile-commands-only  # Small database dependency fixture; does not build the compiler.
//   ./build-regression --ownership-only [COMPILER]  # Check an existing build's fixture ownership; builds nothing.
//
// It removes every tree under build/cmake (clean --all) and checks that
//   - guarded test roots retain their private entry points when preprocessed without main.c's unity-unit selector;
//   - every active test, benchmark and measurement C file has one unity owner, except exact shared-fixture debts;
//   - each baseline CRT startup object relinks and post-links every affected executable, with no work when unchanged;
//   - filtered compile commands are configuration-specific, reused until an input changes and rebuilt if missing;
//   - fresh build.sh, direct CMake and preset trees ignore hostile CFLAGS/LDFLAGS before probing C, including cached
//     global/configuration flags, in generated compile/link commands and real unit compiles;
//   - a clean --all during a build waits for it, and then removes every tree and the published files;
//   - a fresh ./build.sh builds and publishes the Release executable and its configuration's compile commands, and
//     leaves no temporary file behind;
//   - an unchanged build configures, compiles, links, bakes and publishes nothing;
//   - an edited unit, an included .c and a header rebuild their dependents alone (Ninja's plan against its depfiles),
//     and an edited unit is compiled, linked, baked and published alone;
//   - Debug and Release share the tree, and switching back builds nothing and publishes that configuration's file;
//   - BUSTER_BAKE reruns the post-link step alone, both ways, and BUSTER_LINKER the link;
//   - a failed post-link step, a failed compile, an interrupt of the build's process group and a termination of
//     build.sh alone publish nothing and leave nothing running, a clean of the tree during its build waits for the
//     build, which publishes, and an invalid setting configures nothing;
//   - clean --config removes that configuration's outputs alone, and check-call-graph fails at once without them;
//   - the presets configure, build and test the same tree, and the print commands print NUL-delimited arguments;
//   - shifts link the padded executables, each shift once, and lexer-variant builds a tree of its own.
// It leaves the tree built. Each check prints what it checked; the first that fails stops it, after it restores the
// source it edits.
// `--crt-link-only` runs just the small CRT dependency fixture without building the compiler.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_REGRESSION_ENTRIES 4096
#define BUSTER_REGRESSION_INVALID_FLAG "buster-regression-invalid"

typedef struct Text Text;
struct Text
{
    char* data;
    size_t length;
};

#define BUSTER_OWNERSHIP_UNITS 64

typedef struct OwnershipUnits OwnershipUnits;
struct OwnershipUnits
{
    const char* names[BUSTER_OWNERSHIP_UNITS];
    uint32_t count;
};

typedef struct OwnershipFile OwnershipFile;
struct OwnershipFile
{
    const char* path;
    uint64_t owners;
};

typedef struct OwnershipGraph OwnershipGraph;
struct OwnershipGraph
{
    OwnershipFile files[BUSTER_REGRESSION_ENTRIES];
    uint32_t count;
    uint64_t seen;
};

typedef struct OwnershipException OwnershipException;
struct OwnershipException
{
    const char* path;
    // Space-separated main-unit basenames; null means every main unit. Never a prefix or descendant allowance.
    const char* owners;
};

typedef struct OwnershipIssue OwnershipIssue;
struct OwnershipIssue
{
    const char* reason;
    const char* path;
    uint64_t actual;
    uint64_t expected;
};

// Each output's last entry in Ninja's log: its run's times and the output's time, which Ninja keeps when it compacts
// the log, so that two snapshots differ in the outputs whose commands ran between them.
typedef struct Snapshot Snapshot;
struct Snapshot
{
    char* outputs[BUSTER_REGRESSION_ENTRIES];
    char* entries[BUSTER_REGRESSION_ENTRIES];
    uint32_t count;
};

// Half of PATH_MAX, so that a file's name in one of them fits in a PATH_MAX buffer.
#define BUSTER_REGRESSION_PATH (PATH_MAX / 2)
static char work[BUSTER_REGRESSION_PATH];
static char root[BUSTER_REGRESSION_PATH];
static char tree[BUSTER_REGRESSION_PATH];
static char release[BUSTER_REGRESSION_PATH];
static char debug[BUSTER_REGRESSION_PATH];
static char ninja_log[PATH_MAX];
// The source a check edits, and its copy, which is put back however the program ends.
static const char* const edited_unit = "src/buster/compiler/tier1_model_measure.c";
static char edited_copy[PATH_MAX];
static bool edited;

static void restore(void)
{
    if (edited)
    {
        char command[2 * PATH_MAX];
        snprintf(command, sizeof(command), "cp '%s' '%s'", edited_copy, edited_unit);
        edited = system(command) != 0;
    }
}

static void fail(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    fputs("build-regression: FAILED: ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
    restore();
    exit(1);
}

static void pass(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    fputs("build-regression: ok: ", stdout);
    vprintf(format, arguments);
    fputc('\n', stdout);
    fflush(stdout);
    va_end(arguments);
}

static Text read_file(const char* path)
{
    Text text = { 0 };
    FILE* file = fopen(path, "rb");
    if (file == 0)
    {
        fail("can't read %s", path);
    }
    size_t capacity = 1 << 16;
    text.data = malloc(capacity);
    size_t count = 0;
    while (text.data != 0 && (count = fread(text.data + text.length, 1, capacity - text.length - 1, file)) != 0)
    {
        text.length += count;
        if (capacity - text.length - 1 == 0)
        {
            capacity *= 2;
            text.data = realloc(text.data, capacity);
        }
    }
    fclose(file);
    if (text.data == 0)
    {
        fail("out of memory reading %s", path);
    }
    text.data[text.length] = 0;
    return text;
}

static void write_file(const char* path, const char* text, mode_t mode)
{
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    const size_t length = strlen(text);
    if (file < 0 || write(file, text, length) != (ssize_t)length || close(file) != 0)
    {
        fail("can't write %s", path);
    }
}

// Starts `argv` with `assignments` (NAME=VALUE) in its environment, its standard output and errors into `log` (or
// this program's when null), in a new session when `session`. Returns its process.
static pid_t start(const char* log, const char* const* assignments, char* const* argv, bool session)
{
    fflush(stdout);
    fflush(stderr);
    const pid_t child = fork();
    if (child == 0)
    {
        bool ready = !session || setsid() >= 0;
        for (uint32_t i = 0; ready && assignments != 0 && assignments[i] != 0; i += 1)
        {
            char* assignment = strdup(assignments[i]);
            char* equals = assignment != 0 ? strchr(assignment, '=') : 0;
            ready = equals != 0;
            if (ready)
            {
                *equals = 0;
                ready = setenv(assignment, equals + 1, 1) == 0;
            }
        }
        if (ready && log != 0)
        {
            const int file = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            ready = file >= 0 && dup2(file, STDOUT_FILENO) >= 0 && dup2(file, STDERR_FILENO) >= 0;
        }
        if (ready)
        {
            execvp(argv[0], argv);
        }
        _exit(127);
    }
    if (child < 0)
    {
        fail("can't start %s", argv[0]);
    }
    return child;
}

// The exit status of `child`, or 128 and its signal's number.
static int finish(pid_t child)
{
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
    {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int run(const char* log, const char* const* assignments, char* const* argv)
{
    return finish(start(log, assignments, argv, false));
}

// ./build.sh with `arguments` (null-terminated) and `assignments`, its output in work/build.log, which is printed when
// it fails. Returns its status when `may_fail`, and fails otherwise.
static int build_with(const char* const* assignments, int may_fail, ...)
{
    char* argv[16] = { "./build.sh" };
    uint32_t count = 1;
    va_list arguments;
    va_start(arguments, may_fail);
    for (char* argument = va_arg(arguments, char*); argument != 0 && count < 15; argument = va_arg(arguments, char*))
    {
        argv[count] = argument;
        count += 1;
    }
    va_end(arguments);
    argv[count] = 0;
    char log[PATH_MAX];
    snprintf(log, sizeof(log), "%s/build.log", work);
    const int status = run(log, assignments, argv);
    if (status != 0 && !may_fail)
    {
        fputs(read_file(log).data, stderr);
        fail("./build.sh %s failed with status %d", count > 1 ? argv[1] : "", status);
    }
    return status;
}

#define BUILD(...) build_with(0, false, __VA_ARGS__, (char*)0)
#define BUILD_ENV(assignments, ...) build_with(assignments, false, __VA_ARGS__, (char*)0)

// `argv`'s standard output and errors, which must succeed, as text.
static Text capture(char* const* argv)
{
    char log[PATH_MAX];
    snprintf(log, sizeof(log), "%s/captured", work);
    const int status = run(log, 0, argv);
    if (status != 0)
    {
        fputs(read_file(log).data, stderr);
        fail("%s failed with status %d", argv[0], status);
    }
    return read_file(log);
}

// The single line `argv` prints, without its newline.
static void capture_line(char* const* argv, char* line, size_t size)
{
    Text text = capture(argv);
    char* newline = strchr(text.data, '\n');
    if (newline != 0)
    {
        *newline = 0;
    }
    snprintf(line, size, "%s", text.data);
    free(text.data);
}

// The correctness inventory preprocesses main.c and every *_benchmark.c as independent inputs, without a unit
// selector. Check the roots with shared fixture islands too: a bare BUSTER_IN_UNIT(...) in #if fails outside main.c,
// while a fallback that excludes the private body silently loses part of the standalone input.
static void check_standalone_preprocessing(void)
{
    static const struct
    {
        const char* source;
        const char* entry;
    } files[] = {
        { "src/buster/compiler/c_preprocessor_test.c", "run_preprocessor_fixture_tests(" },
        { "src/buster/compiler/c_preprocessor_history_test.c", "pp_test_history(" },
        { "src/buster/compiler/c_preprocessor_oracle.c", "run_preprocessor_tests(" },
        { "src/buster/compiler/c_parser_test.c", "run_parser_tests(" },
        { "src/buster/compiler/foreign_test.c", "run_foreign_tests(" },
        { "src/buster/compiler/tier0_test.c", "run_tier0_tests(" },
        { "src/buster/compiler/tier1_select_source_benchmark.c", "run_tier1_select_source_benchmark(" },
    };
    for (uint32_t i = 0; i < sizeof(files) / sizeof(files[0]); i += 1)
    {
        char* argv[] = {
            "clang",
            "-E",
            "-P",
            "-target",
            "x86_64-pc-linux-gnu",
            "-march=x86-64",
            "-Isrc",
            "-fwrapv",
            "-funsigned-char",
            (char*)files[i].source,
            0,
        };
        Text text = capture(argv);
        if (strstr(text.data, files[i].entry) == 0)
        {
            fail("%s lost %s when preprocessed outside a unity unit", files[i].source, files[i].entry);
        }
        free(text.data);
    }
    pass("guarded roots retain their private entry points when preprocessed without a unity unit");
}

static void snapshot(Snapshot* snapshot)
{
    Text log = read_file(ninja_log);
    snapshot->count = 0;
    char* line = strchr(log.data, '\n');
    while (line != 0 && line[1] != 0)
    {
        line += 1;
        char* end = strchr(line, '\n');
        if (end != 0)
        {
            *end = 0;
        }
        // start, end, the output's time, the output, the command's hash
        char* fields[5] = { line };
        uint32_t field = 1;
        for (char* tab = strchr(line, '\t'); tab != 0 && field < 5; tab = strchr(tab + 1, '\t'))
        {
            *tab = 0;
            fields[field] = tab + 1;
            field += 1;
        }
        if (field == 5)
        {
            char entry[256];
            snprintf(entry, sizeof(entry), "%s %s %s", fields[0], fields[1], fields[2]);
            uint32_t i = 0;
            while (i < snapshot->count && strcmp(snapshot->outputs[i], fields[3]) != 0)
            {
                i += 1;
            }
            if (i == snapshot->count && i < BUSTER_REGRESSION_ENTRIES)
            {
                snapshot->outputs[i] = strdup(fields[3]);
                snapshot->count += 1;
            }
            if (i < snapshot->count)
            {
                free(snapshot->entries[i]);
                snapshot->entries[i] = strdup(entry);
            }
        }
        line = end;
    }
    free(log.data);
}

static int compare_names(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

// The file names of the outputs whose commands ran between `before` and now, sorted, each once and followed by a
// space, as "compiler compiler.link unit.c.o ".
static void ran_since(const Snapshot* before, char* names, size_t size)
{
    static Snapshot after;
    snapshot(&after);
    char* ran[BUSTER_REGRESSION_ENTRIES];
    uint32_t count = 0;
    for (uint32_t i = 0; i < after.count; i += 1)
    {
        uint32_t j = 0;
        while (j < before->count && strcmp(before->outputs[j], after.outputs[i]) != 0)
        {
            j += 1;
        }
        if (j == before->count || strcmp(before->entries[j], after.entries[i]) != 0)
        {
            const char* slash = strrchr(after.outputs[i], '/');
            ran[count] = (char*)(slash != 0 ? slash + 1 : after.outputs[i]);
            count += 1;
        }
    }
    qsort(ran, count, sizeof(ran[0]), compare_names);
    names[0] = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (i == 0 || strcmp(ran[i], ran[i - 1]) != 0)
        {
            strncat(names, ran[i], size - strlen(names) - 2);
            strcat(names, " ");
        }
    }
}

// CMake may rewrite the exported database even when only a link setting changed. Those tests still require the
// exact compiler work, allowing the metadata filter alone in addition. Ordinary no-op/source-edit tests do not.
static const char* compiler_outputs(const char* names)
{
    const char* prefix = "compile_commands.json ";
    return strncmp(names, prefix, strlen(prefix)) == 0 ? names + strlen(prefix) : names;
}

static bool same_file(const char* a, const char* b)
{
    struct stat first;
    struct stat second;
    return stat(a, &first) == 0 && stat(b, &second) == 0 && first.st_dev == second.st_dev && first.st_ino == second.st_ino;
}

static struct stat status_of(const char* path)
{
    struct stat status;
    if (stat(path, &status) != 0)
    {
        fail("can't stat %s", path);
    }
    return status;
}

static bool same_time(struct stat a, struct stat b)
{
    return a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec;
}

// Whether every "command" of `path` compiles into the configuration `config`'s objects, by its
// -o argument (CMake 3.25's entries have no "output"), and there is one.
static bool compile_commands_file_of(const char* path, const char* config)
{
    Text text = read_file(path);
    char directory[64];
    snprintf(directory, sizeof(directory), ".dir/%s/", config);
    uint32_t count = 0;
    bool result = true;
    for (char* command = strstr(text.data, "\"command\""); command != 0; command = strstr(command + 1, "\"command\""))
    {
        char* end = strchr(command, '\n');
        if (end != 0)
        {
            *end = 0;
        }
        char* object = strstr(command, " -o ");
        char* after = object != 0 ? strchr(object + 4, ' ') : 0;
        if (after != 0)
        {
            *after = 0;
        }
        result = result && object != 0 && strstr(object, directory) != 0;
        count += 1;
        command = end != 0 ? end : command + strlen(command);
    }
    free(text.data);
    return result && count != 0;
}

static bool compile_commands_of(const char* config)
{
    return compile_commands_file_of("build/compile_commands.json", config);
}

static bool ownership_suffix(const char* path, const char* suffix)
{
    const size_t length = strlen(path);
    const size_t tail = strlen(suffix);
    return length >= tail && strcmp(path + length - tail, suffix) == 0;
}

static bool ownership_unit_name(const char* name)
{
    return strcmp(name, "main.c") == 0 || (strncmp(name, "main_unit_", 10) == 0 && ownership_suffix(name, ".c"));
}

static uint64_t ownership_all(const OwnershipUnits* units)
{
    return UINT64_MAX >> (BUSTER_OWNERSHIP_UNITS - units->count);
}

// Read the actual build's unity sources, rather than maintaining another list alongside BUSTER_SOURCES. Their names
// borrow `sources`, as the graph's paths borrow the single Ninja dependency capture.
static OwnershipUnits ownership_units(Text sources)
{
    OwnershipUnits units = { 0 };
    for (size_t offset = 0; offset < sources.length;)
    {
        const char* source = sources.data + offset;
        const char* end = memchr(source, 0, sources.length - offset);
        if (end == 0)
        {
            fail("print-sources did not terminate its arguments");
        }
        const char* slash = strrchr(source, '/');
        const char* name = slash != 0 ? slash + 1 : source;
        if (ownership_unit_name(name))
        {
            if (units.count == BUSTER_OWNERSHIP_UNITS)
            {
                fail("too many unity sources for the ownership check");
            }
            for (uint32_t i = 0; i < units.count; i += 1)
            {
                if (strcmp(units.names[i], name) == 0)
                {
                    fail("print-sources repeats unity source %s", name);
                }
            }
            units.names[units.count++] = name;
        }
        offset = (size_t)(end - sources.data) + 1;
    }
    if (units.count < 2)
    {
        fail("print-sources did not name the unity sources");
    }
    return units;
}

// Ninja gives a flat transitive dependency set for each object, so this catches nested additions exactly as direct
// ones. Standalone benchmark/test objects are not unity owners. Require every selected unity record exactly once,
// with VALID dependency data: incomplete or stale captures must not make a leaked include appear single-owner.
static OwnershipIssue ownership_read(char* text, const char* directory, const char* config, const OwnershipUnits* units, OwnershipGraph* graph)
{
    OwnershipIssue issue = { 0 };
    memset(graph, 0, sizeof(*graph));
    char prefix[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s/src/buster/compiler/", directory);
    const size_t prefix_length = strlen(prefix);
    uint64_t owner = 0;
    char* saved = 0;
    for (char* line = strtok_r(text, "\n", &saved); line != 0 && issue.reason == 0; line = strtok_r(0, "\n", &saved))
    {
        if (line[0] != ' ')
        {
            owner = 0;
            char* marker = strstr(line, ": #deps ");
            if (marker != 0)
            {
                *marker = 0;
                char* slash = strrchr(line, '/');
                char* name = slash != 0 ? slash + 1 : line;
                if (ownership_suffix(name, ".c.o"))
                {
                    name[strlen(name) - 2] = 0;
                    if (ownership_unit_name(name))
                    {
                        uint32_t unit = 0;
                        while (unit < units->count && strcmp(name, units->names[unit]) != 0)
                        {
                            unit += 1;
                        }
                        char expected[PATH_MAX];
                        snprintf(expected, sizeof(expected), "CMakeFiles/buster_%.*s.dir/%s/src/buster/compiler/%s", (int)strlen(name) - 2, name, config, name);
                        if (unit == units->count || strcmp(line, expected) != 0 || strstr(marker + 1, " (VALID)") == 0)
                        {
                            issue = (OwnershipIssue){ .reason = "invalid or stale unity dependency record", .path = name };
                        }
                        else
                        {
                            owner = UINT64_C(1) << unit;
                            if ((graph->seen & owner) != 0)
                            {
                                issue = (OwnershipIssue){ .reason = "duplicate unity dependency record", .path = name };
                            }
                            graph->seen |= owner;
                        }
                    }
                }
            }
        }
        else if (owner != 0)
        {
            while (*line == ' ')
            {
                line += 1;
            }
            if (strncmp(line, prefix, prefix_length) == 0 &&
                (ownership_suffix(line, "_test.c") || ownership_suffix(line, "_benchmark.c") || ownership_suffix(line, "_measure.c")))
            {
                const char* path = line + prefix_length;
                uint32_t file = 0;
                while (file < graph->count && strcmp(graph->files[file].path, path) != 0)
                {
                    file += 1;
                }
                if (file == BUSTER_REGRESSION_ENTRIES)
                {
                    issue = (OwnershipIssue){ .reason = "too many fixture dependency paths", .path = path };
                }
                else
                {
                    if (file == graph->count)
                    {
                        graph->files[file].path = path;
                        graph->count += 1;
                    }
                    graph->files[file].owners |= owner;
                }
            }
        }
    }
    if (issue.reason == 0 && graph->seen != ownership_all(units))
    {
        issue = (OwnershipIssue){ .reason = "missing unity dependency records", .path = "Ninja dependency capture", .actual = graph->seen, .expected = ownership_all(units) };
    }
    return issue;
}

static OwnershipIssue ownership_check(const OwnershipUnits* units, const OwnershipGraph* graph, const OwnershipException* exceptions, uint32_t count)
{
    OwnershipIssue issue = { 0 };
    for (uint32_t i = 0; i < graph->count && issue.reason == 0; i += 1)
    {
        const OwnershipFile* file = &graph->files[i];
        bool shared = false;
        for (uint32_t j = 0; j < count; j += 1)
        {
            shared = shared || strcmp(file->path, exceptions[j].path) == 0;
        }
        if (!shared && (file->owners & (file->owners - 1)) != 0)
        {
            issue = (OwnershipIssue){ .reason = "private fixture has multiple unity owners", .path = file->path, .actual = file->owners };
        }
    }
    for (uint32_t i = 0; i < count && issue.reason == 0; i += 1)
    {
        const OwnershipException* exception = &exceptions[i];
        uint64_t expected = exception->owners == 0 ? ownership_all(units) : 0;
        const char* name = exception->owners;
        while (name != 0 && *name != 0 && issue.reason == 0)
        {
            const size_t length = strcspn(name, " ");
            uint32_t unit = 0;
            while (unit < units->count && (strlen(units->names[unit]) != length || strncmp(name, units->names[unit], length) != 0))
            {
                unit += 1;
            }
            if (unit == units->count)
            {
                issue = (OwnershipIssue){ .reason = "exception names an unknown unity owner", .path = exception->path };
            }
            else
            {
                expected |= UINT64_C(1) << unit;
            }
            name += length;
            if (*name == ' ')
            {
                name += 1;
            }
        }
        if (issue.reason == 0 && (expected == 0 || (expected & (expected - 1)) == 0))
        {
            issue = (OwnershipIssue){ .reason = "shared exception must name multiple unity owners", .path = exception->path, .expected = expected };
        }
        uint32_t file = 0;
        while (file < graph->count && strcmp(exception->path, graph->files[file].path) != 0)
        {
            file += 1;
        }
        if (issue.reason == 0 && file == graph->count)
        {
            issue = (OwnershipIssue){ .reason = "unused shared-fixture exception", .path = exception->path, .expected = expected };
        }
        else if (issue.reason == 0 && graph->files[file].owners != expected)
        {
            issue = (OwnershipIssue){ .reason = "shared fixture's exact owner set changed", .path = exception->path, .actual = graph->files[file].owners, .expected = expected };
        }
        for (uint32_t j = 0; j < i && issue.reason == 0; j += 1)
        {
            if (strcmp(exception->path, exceptions[j].path) == 0)
            {
                issue = (OwnershipIssue){ .reason = "duplicate shared-fixture exception", .path = exception->path };
            }
        }
    }
    return issue;
}

static void ownership_names(const OwnershipUnits* units, uint64_t mask, char* names, size_t size)
{
    names[0] = 0;
    size_t length = 0;
    for (uint32_t i = 0; i < units->count; i += 1)
    {
        if ((mask & (UINT64_C(1) << i)) != 0)
        {
            const int added = snprintf(names + length, size - length, "%s%s", length != 0 ? " " : "", units->names[i]);
            if (added < 0 || (size_t)added >= size - length)
            {
                fail("unity owner names exceed the diagnostic buffer");
            }
            length += (size_t)added;
        }
    }
    if (length == 0)
    {
        snprintf(names, size, "none");
    }
}

static void ownership_report(const OwnershipUnits* units, OwnershipIssue issue)
{
    if (issue.reason != 0)
    {
        char actual[4096];
        char expected[4096];
        ownership_names(units, issue.actual, actual, sizeof(actual));
        ownership_names(units, issue.expected, expected, sizeof(expected));
        fail("%s: %s; actual unity owners: %s; expected: %s. Guard private includes for their command's unit; "
             "keep only justified exact shared exceptions, remove obsolete ones, and build before checking dependencies",
             issue.path, issue.reason, actual, issue.expected != 0 ? expected : "one unity unit (or complete valid dependency records)");
    }
}

static void ownership_fixture_append(char* text, size_t size, size_t* length, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int added = vsnprintf(text + *length, size - *length, format, arguments);
    va_end(arguments);
    if (added < 0 || (size_t)added >= size - *length)
    {
        fail("ownership regression fixture exceeds its buffer");
    }
    *length += (size_t)added;
}

// These are flattened compiler dependency fixtures, including an unrelated standalone consumer. A shared root's
// exception must not authorize a new nested file, and incomplete dependency data must not authorize anything.
static void check_ownership_fixtures(void)
{
    static const OwnershipUnits units = {
        .names = { "main.c", "main_unit_backend.c", "main_unit_frontend.c" },
        .count = 3,
    };
    static const OwnershipException shared[] = {
        { "shared_test.c", "main_unit_backend.c main_unit_frontend.c" },
        { "shared_test.c", "main_unit_backend.c main_unit_frontend.c" },
    };
    static const struct
    {
        const char* label;
        const char* files[3];
        uint32_t shared;
        const char* extra;
        const char* error;
    } cases[] = {
        { "private and exact shared sources", { "driver_test.c", "new_test.c shared_test.c", "shared_test.c" }, true,
          "CMakeFiles/buster_other_benchmark.dir/Release/src/buster/compiler/other_benchmark.c.o: #deps 1, deps mtime 1 (VALID)\n"
          "    /fixture/src/buster/compiler/new_test.c\n", 0 },
        { "new direct test leak", { "new_test.c", "new_test.c", "" }, false, "", "private fixture has multiple unity owners" },
        { "new nested benchmark leak", { "", "shared_test.c nested/new_benchmark.c", "shared_test.c nested/new_benchmark.c" }, true, "",
          "private fixture has multiple unity owners" },
        { "new measurement leak", { "new_measure.c", "new_measure.c", "" }, false, "", "private fixture has multiple unity owners" },
        { "shared production and declarations", { "production.c fixture_test.h", "production.c fixture_test.h", "" }, false, "", 0 },
        { "changed shared owners", { "shared_test.c", "shared_test.c", "" }, true, "", "shared fixture's exact owner set changed" },
        { "widened shared owners", { "shared_test.c", "shared_test.c", "shared_test.c" }, true, "", "shared fixture's exact owner set changed" },
        { "obsolete shared exception", { "", "shared_test.c", "" }, true, "", "shared fixture's exact owner set changed" },
        { "unused shared exception", { "", "", "" }, true, "", "unused shared-fixture exception" },
        { "duplicate shared exception", { "", "shared_test.c", "shared_test.c" }, 2, "", "duplicate shared-fixture exception" },
        { "missing unity record", { "", "", 0 }, false, "", "missing unity dependency records" },
        { "empty dependency capture", { 0, 0, 0 }, false, "", "missing unity dependency records" },
        { "duplicate unity record", { "", "", "" }, false,
          "CMakeFiles/buster_main.dir/Release/src/buster/compiler/main.c.o: #deps 0, deps mtime 1 (VALID)\n", "duplicate unity dependency record" },
        { "stale unity record", { "", "", 0 }, false,
          "CMakeFiles/buster_main_unit_frontend.dir/Release/src/buster/compiler/main_unit_frontend.c.o: #deps 0, deps mtime 1 (STALE)\n",
          "invalid or stale unity dependency record" },
        { "wrong configuration", { "", "", 0 }, false,
          "CMakeFiles/buster_main_unit_frontend.dir/Debug/src/buster/compiler/main_unit_frontend.c.o: #deps 0, deps mtime 1 (VALID)\n",
          "invalid or stale unity dependency record" },
        { "unknown unity record", { "", "", "" }, false,
          "CMakeFiles/buster_main_unit_unknown.dir/Release/src/buster/compiler/main_unit_unknown.c.o: #deps 0, deps mtime 1 (VALID)\n",
          "invalid or stale unity dependency record" },
    };
    static OwnershipGraph graph;
    for (uint32_t test = 0; test < sizeof(cases) / sizeof(cases[0]); test += 1)
    {
        char text[4096];
        size_t length = 0;
        for (uint32_t unit = 0; unit < units.count; unit += 1)
        {
            if (cases[test].files[unit] != 0)
            {
                ownership_fixture_append(text, sizeof(text), &length,
                                         "CMakeFiles/buster_%.*s.dir/Release/src/buster/compiler/%s.o: #deps 2, deps mtime 1 (VALID)\n",
                                         (int)strlen(units.names[unit]) - 2, units.names[unit], units.names[unit]);
                const char* file = cases[test].files[unit];
                while (*file != 0)
                {
                    const size_t bytes = strcspn(file, " ");
                    ownership_fixture_append(text, sizeof(text), &length, "    /fixture/src/buster/compiler/%.*s\n", (int)bytes, file);
                    file += bytes + (file[bytes] == ' ');
                }
            }
        }
        ownership_fixture_append(text, sizeof(text), &length, "%s", cases[test].extra);
        OwnershipIssue issue = ownership_read(text, "/fixture", "Release", &units, &graph);
        if (issue.reason == 0)
        {
            issue = ownership_check(&units, &graph, shared, cases[test].shared);
        }
        if ((issue.reason == 0) != (cases[test].error == 0) ||
            (issue.reason != 0 && cases[test].error != 0 && strcmp(issue.reason, cases[test].error) != 0))
        {
            fail("ownership fixture '%s': %s; expected %s", cases[test].label, issue.reason != 0 ? issue.reason : "success",
                 cases[test].error != 0 ? cases[test].error : "success");
        }
    }
    // Narrowing an obsolete exception to match its now-private file must not keep the exception alive.
    static const OwnershipException unnecessary[] = {
        { "shared_test.c", "main_unit_backend.c" },
    };
    memset(&graph, 0, sizeof(graph));
    graph.files[0] = (OwnershipFile){ .path = "shared_test.c", .owners = UINT64_C(1) << 1 };
    graph.count = 1;
    const OwnershipIssue unnecessary_issue = ownership_check(&units, &graph, unnecessary, 1);
    if (unnecessary_issue.reason == 0 || strcmp(unnecessary_issue.reason, "shared exception must name multiple unity owners") != 0)
    {
        fail("ownership accepted a matching single-owner shared exception");
    }
    pass("ownership rejects new direct/nested leaks, changed/shared masks, obsolete exceptions and incomplete records");
}

// This is only the current sharing debt, not an inventory of the private tests. New files default to one unity
// owner. An exception names one exact path and one exact owner set; neither its children nor a new file with the
// same prefix inherit it. Remove a row when its file becomes private, and justify any new sharing at its include.
static const OwnershipException ownership_exceptions[] = {
    // Shared comparison, parser/oracle and source-preparation helpers retained at their original include positions.
    { "c_checker_test.c", 0 },
    { "c_parser_test.c", 0 },
    { "c_preprocessor_test.c", 0 },
    { "c_preprocessor_history_test.c", "main_unit_frontend.c main_unit_checker.c" },
    { "foreign_test.c", "main_unit_executable.c main_unit_checker.c" },
    { "ir_source_test.c", 0 },
    { "tier1_select_test.c", 0 },
    { "tier1_select_burs_test.c", 0 },
    { "tier1_select_source_benchmark.c", 0 },
    { "tier0_test.c", 0 },
    // These roots preserve the first inclusion of production optimizer code while guarding their private bodies.
    { "ir_if_convert_test.c", 0 },
    { "ir_optimize_test.c", 0 },
    { "ir_optimize_text_test.c", 0 },
    { "ir_optimize_verified_test.c", 0 },
    // Remaining legacy sharing, to narrow as each family's outward helper references are isolated. These rows do
    // not endorse parsing private fixtures everywhere; exact paths keep that existing debt from silently growing.
    { "assembler_benchmark.c", 0 },
    { "bind_test.c", 0 },
    { "branch_sizing_test.c", 0 },
    { "c_builtin_atomic_helpers_test.c", 0 },
    { "c_builtin_atomic_rmw_helpers_test.c", 0 },
    { "c_builtin_float_lower_test.c", 0 },
    { "c_builtin_lower_benchmark.c", 0 },
    { "c_builtin_lower_test.c", 0 },
    { "c_builtin_math_helpers_test.c", 0 },
    { "c_builtin_misc_lower_test.c", 0 },
    { "c_builtin_rounds_helper_test.c", 0 },
    { "c_builtin_terminal_helpers_test.c", 0 },
    { "c_builtin_test.c", 0 },
    { "c_builtin_varargs_lower_test.c", 0 },
    { "c_check_binding_benchmark.c", 0 },
    { "c_check_deferred_benchmark.c", 0 },
    { "c_check_lanes_benchmark.c", 0 },
    { "c_check_source_benchmark.c", 0 },
    { "c_environment_test.c", 0 },
    { "c_lexer_test.c", 0 },
    { "c_lowering_source_benchmark.c", 0 },
    { "c_types_deferred_test.c", 0 },
    { "c_types_lookup_test.c", 0 },
    { "c_types_test.c", 0 },
    { "calling_convention_test.c", 0 },
    { "compile_measure.c", 0 },
    { "dataflow_test.c", 0 },
    { "debug_benchmark.c", 0 },
    { "debug_executable_test.c", 0 },
    { "debug_test.c", 0 },
    { "diagnostic_benchmark.c", 0 },
    { "diagnostic_test.c", 0 },
    { "differential_test.c", 0 },
    { "dominators_test.c", 0 },
    { "dynamic_bounds_test.c", 0 },
    { "dynamic_libraries_test.c", 0 },
    { "dynamic_test.c", 0 },
    { "executable_symbols_test.c", 0 },
    { "executable_test.c", 0 },
    { "frame_measure.c", 0 },
    { "helpers_measure.c", 0 },
    { "import_test.c", 0 },
    { "inflate_test.c", 0 },
    { "lane_test.c", 0 },
    { "link_order_test.c", 0 },
    { "link_test.c", 0 },
    { "mask_spill_measure.c", 0 },
    { "mask_spill_measure_test.c", 0 },
    { "object_debug_test.c", 0 },
    { "object_writer_test.c", 0 },
    { "oracle_cache_test.c", 0 },
    { "runtime_library_search_test.c", 0 },
    { "snapshot_test.c", 0 },
    { "sqlite_test.c", 0 },
    { "ssa_benchmark.c", 0 },
    { "ssa_test.c", 0 },
    { "static_test.c", 0 },
    { "tier0_benchmark_measure.c", 0 },
    { "tier0_source_benchmark.c", 0 },
    { "tier0_variadic_frame_test.c", 0 },
    { "tier1_allocate_expiry_test.c", 0 },
    { "tier1_allocate_hints_test.c", 0 },
    { "tier1_allocate_test.c", 0 },
    { "tier1_certificate_interval_test.c", 0 },
    { "tier1_certificate_test.c", 0 },
    { "tier1_code_certificate_benchmark.c", 0 },
    { "tier1_frame_execution_test.c", 0 },
    { "tier1_frame_test.c", 0 },
    { "tier1_moves_test.c", 0 },
    { "tier1_programs_measure.c", 0 },
    { "tier1_rows_measure.c", 0 },
    { "tier1_schedule_test.c", 0 },
    { "unwind_benchmark.c", 0 },
    { "unwind_native_test.c", 0 },
    { "unwind_test.c", 0 },
    { "unwind_tier0_test.c", 0 },
    { "x86_64_disassembler_test.c", 0 },
    { "x86_64_encoder_test.c", 0 },
    { "x86_intrinsics_test.c", 0 },
    { "x86_semantics_test.c", 0 },
    { "x86_zen5_measure.c", 0 },
    { "x86_zen5_test.c", 0 },
};

static void check_ownership(const char* directory, const char* config)
{
    check_ownership_fixtures();
    char ninja_file[64];
    snprintf(ninja_file, sizeof(ninja_file), "build-%s.ninja", config);
    // Ninja's VALID mark compares the object with its recorded mtime, not the source with the object. A dry run
    // also detects edits that would change the include graph. Never build here, including in the inventory mode.
    char* plan_argv[] = { "ninja", "-C", (char*)directory, "-f", ninja_file, "-n", "compiler", 0 };
    Text plan = capture(plan_argv);
    if (strstr(plan.data, "ninja: no work to do.") == 0)
    {
        fputs(plan.data, stderr);
        fail("ownership requires an up-to-date %s compiler; build that configuration first", config);
    }
    free(plan.data);
    char* sources_argv[] = { "./build.sh", "print-sources", 0 };
    Text sources = capture(sources_argv);
    const OwnershipUnits units = ownership_units(sources);
    // Debug and Release share .ninja_deps. Ask for the selected objects explicitly in this one capture so no
    // unrelated configuration's record can hide a missing selected record or count as another owner.
    char objects[BUSTER_OWNERSHIP_UNITS][PATH_MAX];
    char* deps_argv[BUSTER_OWNERSHIP_UNITS + 8] = { "ninja", "-C", (char*)directory, "-f", ninja_file, "-t", "deps" };
    for (uint32_t i = 0; i < units.count; i += 1)
    {
        snprintf(objects[i], sizeof(objects[i]), "CMakeFiles/buster_%.*s.dir/%s/src/buster/compiler/%s.o",
                 (int)strlen(units.names[i]) - 2, units.names[i], config, units.names[i]);
        deps_argv[7 + i] = objects[i];
    }
    deps_argv[7 + units.count] = 0;
    Text deps = capture(deps_argv);
    static OwnershipGraph graph;
    ownership_report(&units, ownership_read(deps.data, root, config, &units, &graph));
    const uint32_t exceptions = sizeof(ownership_exceptions) / sizeof(ownership_exceptions[0]);
    ownership_report(&units, ownership_check(&units, &graph, ownership_exceptions, exceptions));
    pass("%s ownership: %u active test/benchmark/measurement C files, %u single-owner and %u exact shared exceptions across %u unity units",
         config, graph.count, graph.count - exceptions, exceptions, units.count);
    free(deps.data);
    free(sources.data);
}

static void check_existing_ownership(const char* compiler)
{
    char selected[PATH_MAX];
    if (compiler == 0)
    {
        char* output[] = { "./build.sh", "print-output", "--config", "Release", 0 };
        capture_line(output, selected, sizeof(selected));
        compiler = selected;
    }
    char resolved[PATH_MAX];
    if (realpath(compiler, resolved) == 0 || access(resolved, X_OK) != 0)
    {
        fail("ownership requires an existing built compiler: %s", compiler);
    }
    // realpath resolves symbolic links, not build/compiler's hard link. Match that publication against the
    // selected tree's outputs rather than guessing its configuration; unsupported detached copies fail closed.
    if (!ownership_suffix(resolved, "/Release/compiler") && !ownership_suffix(resolved, "/Debug/compiler"))
    {
        static const char* configs[] = { "Release", "Debug" };
        bool found = false;
        for (uint32_t i = 0; i < 2 && !found; i += 1)
        {
            char* output[] = { "./build.sh", "print-output", "--config", (char*)configs[i], 0 };
            capture_line(output, selected, sizeof(selected));
            found = same_file(compiler, selected);
            if (found && realpath(selected, resolved) == 0)
            {
                fail("can't resolve ownership build path %s", selected);
            }
        }
        if (!found)
        {
            fail("%s is a detached compiler copy; pass a built CONFIG/compiler path or set the selected build's environment", compiler);
        }
    }
    *strrchr(resolved, '/') = 0;
    char* config = strrchr(resolved, '/') + 1;
    config[-1] = 0;
    check_ownership(resolved, config);
}

// The objects' file names, without .o, whose depfiles name the source `file`, sorted, each followed by a space.
static void dependents(const char* file, char* names, size_t size)
{
    char* argv[] = { "ninja", "-C", tree, "-f", "build-Release.ninja", "-t", "deps", 0 };
    Text deps = capture(argv);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", root, file);
    char* found[BUSTER_REGRESSION_ENTRIES];
    uint32_t count = 0;
    char* object = 0;
    for (char* line = strtok(deps.data, "\n"); line != 0; line = strtok(0, "\n"))
    {
        char* marker = strstr(line, ": #deps ");
        while (*line == ' ')
        {
            line += 1;
        }
        if (marker != 0)
        {
            *marker = 0;
            object = line;
        }
        else if (object != 0 && strcmp(line, path) == 0 && count < BUSTER_REGRESSION_ENTRIES)
        {
            char* slash = strrchr(object, '/');
            found[count] = slash != 0 ? slash + 1 : object;
            char* dot = strrchr(found[count], '.');
            if (dot != 0)
            {
                *dot = 0;
            }
            count += 1;
        }
    }
    qsort(found, count, sizeof(found[0]), compare_names);
    names[0] = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        strncat(names, found[i], size - strlen(names) - 2);
        strcat(names, " ");
    }
    free(deps.data);
}

// The units Ninja's plan for the Release executable compiles, as dependents gives them, and whether it plans anything.
static bool planned(char* names, size_t size)
{
    char* argv[] = { "ninja", "-C", tree, "-f", "build-Release.ninja", "-n", "compiler", 0 };
    Text plan = capture(argv);
    char* found[BUSTER_REGRESSION_ENTRIES];
    uint32_t count = 0;
    bool work_to_do = false;
    for (char* line = strtok(plan.data, "\n"); line != 0; line = strtok(0, "\n"))
    {
        work_to_do = work_to_do || line[0] == '[';
        char* object = strstr(line, "Building C object ");
        if (object != 0 && count < BUSTER_REGRESSION_ENTRIES)
        {
            char* slash = strrchr(object, '/');
            found[count] = slash + 1;
            char* dot = strrchr(found[count], '.');
            *dot = 0;
            count += 1;
        }
    }
    qsort(found, count, sizeof(found[0]), compare_names);
    names[0] = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        strncat(names, found[i], size - strlen(names) - 2);
        strcat(names, " ");
    }
    free(plan.data);
    return work_to_do;
}

static void sleep_seconds(unsigned seconds)
{
    struct timespec time = { .tv_sec = seconds };
    while (nanosleep(&time, &time) != 0 && errno == EINTR)
    {
    }
}

static bool contains_line(const char* path, const char* line)
{
    Text text = read_file(path);
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "\n%s\n", line);
    const bool found = strstr(text.data, pattern) != 0 || strncmp(text.data, pattern + 1, strlen(pattern + 1)) == 0;
    free(text.data);
    return found;
}

// Whether `executable`'s symbols, by nm, end a line with `symbol` (" T name" and the like).
static bool has_symbol(const char* executable, const char* symbol)
{
    char* argv[] = { "nm", (char*)executable, 0 };
    Text symbols = capture(argv);
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%s\n", symbol);
    const bool found = strstr(symbols.data, pattern) != 0;
    free(symbols.data);
    return found;
}

static bool same_bytes(const char* a, const char* b)
{
    Text first = read_file(a);
    Text second = read_file(b);
    const bool same = first.length == second.length && memcmp(first.data, second.data, first.length) == 0;
    free(first.data);
    free(second.data);
    return same;
}

static void check_ignored_flag_text(const char* path)
{
    Text text = read_file(path);
    if (strstr(text.data, BUSTER_REGRESSION_INVALID_FLAG) != 0)
    {
        fail("an ignored flag reached %s", path);
    }
    free(text.data);
}

// Each configure starts without a cache or compiler identity. The two environment variables are tested
// independently so a compile failure cannot hide a link failure. Real unit compiles complement Ninja's commands for
// the whole executable; the full harness's fresh build below also executes the link under both hostile variables.
static void check_ignored_flags(void)
{
    static const char* const names[] =
    {
        "build.sh with CFLAGS",
        "build.sh with LDFLAGS",
        "direct CMake with CFLAGS",
        "direct CMake with LDFLAGS",
        "a preset with CFLAGS",
        "a preset with LDFLAGS",
        "direct CMake with global and configuration cache flags"
    };
    static const char* const cflags[] = { "CFLAGS=-f" BUSTER_REGRESSION_INVALID_FLAG "-cflags", "LDFLAGS=", 0 };
    static const char* const ldflags[] = { "CFLAGS=", "LDFLAGS=-Wl,--" BUSTER_REGRESSION_INVALID_FLAG "-ldflags", 0 };
    static const char* const empty[] = { "CFLAGS=", "LDFLAGS=", 0 };
    static const char* const cache_flags[] =
    {
        "-DCMAKE_C_FLAGS=-f" BUSTER_REGRESSION_INVALID_FLAG "-cache-cflags",
        "-DCMAKE_C_FLAGS_DEBUG=-f" BUSTER_REGRESSION_INVALID_FLAG "-debug-cflags",
        "-DCMAKE_C_FLAGS_RELEASE=-f" BUSTER_REGRESSION_INVALID_FLAG "-release-cflags",
        "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--" BUSTER_REGRESSION_INVALID_FLAG "-cache-ldflags",
        "-DCMAKE_EXE_LINKER_FLAGS_DEBUG=-Wl,--" BUSTER_REGRESSION_INVALID_FLAG "-debug-ldflags",
        "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=-Wl,--" BUSTER_REGRESSION_INVALID_FLAG "-release-ldflags"
    };
    static const char* const probe_logs[] =
    {
        "CMakeFiles/CMakeConfigureLog.yaml",
        "CMakeFiles/CMakeOutput.log",
        "CMakeFiles/CMakeError.log"
    };
    static const char* const configurations[] = { "Debug", "Release" };
    for (uint32_t test = 0; test < sizeof(names) / sizeof(names[0]); test += 1)
    {
        const bool wrapper = test < 2;
        const bool preset = test == 4 || test == 5;
        const bool cached = test == 6;
        const char* const* assignments = cached ? empty : test % 2 == 0 ? cflags : ldflags;
        char directory[PATH_MAX];
        char log[PATH_MAX];
        snprintf(log, sizeof(log), "%s/flags-%u.log", work, test);
        if (wrapper)
        {
            BUILD("clean");
            snprintf(directory, sizeof(directory), "%s", tree);
        }
        else
        {
            snprintf(directory, sizeof(directory), "%s/flags-%u", work, test);
        }
        char* configure[32] = { "cmake", "-B", directory };
        uint32_t count = 3;
        if (preset)
        {
            configure[count++] = "--preset";
            configure[count++] = "native";
        }
        else
        {
            configure[count++] = "-S";
            configure[count++] = root;
            configure[count++] = "-G";
            configure[count++] = "Ninja Multi-Config";
        }
        configure[count++] = "-DCMAKE_C_COMPILER=clang";
        configure[count++] = "-DCMAKE_C_COMPILER_LAUNCHER=";
        configure[count++] = "-DCMAKE_C_LINKER_LAUNCHER=";
        if (cached)
        {
            for (uint32_t flag = 0; flag < sizeof(cache_flags) / sizeof(cache_flags[0]); flag += 1)
            {
                configure[count++] = (char*)cache_flags[flag];
            }
        }
        configure[count] = 0;
        char* script[] = { "./build.sh", "configure", 0 };
        if (run(log, assignments, wrapper ? script : configure) != 0)
        {
            fputs(read_file(log).data, stderr);
            fail("%s failed to configure a fresh tree", names[test]);
        }
        check_ignored_flag_text(log);
        bool probed = false;
        for (uint32_t probe = 0; probe < sizeof(probe_logs) / sizeof(probe_logs[0]); probe += 1)
        {
            char path[PATH_MAX + 64];
            snprintf(path, sizeof(path), "%s/%s", directory, probe_logs[probe]);
            if (access(path, F_OK) == 0)
            {
                check_ignored_flag_text(path);
                Text text = read_file(path);
                probed = probed || strstr(text.data, "CMakeCCompilerABI.c") != 0;
                free(text.data);
            }
        }
        if (!probed)
        {
            fail("%s has no fresh C compiler ABI probe in its logs", names[test]);
        }
        for (uint32_t config = 0; config < sizeof(configurations) / sizeof(configurations[0]); config += 1)
        {
            char ninja_file[64];
            snprintf(ninja_file, sizeof(ninja_file), "build-%s.ninja", configurations[config]);
            char* commands[] = { "ninja", "-C", directory, "-f", ninja_file, "-t", "commands", "compiler", 0 };
            Text printed = capture(commands);
            if (strstr(printed.data, BUSTER_REGRESSION_INVALID_FLAG) != 0 || strstr(printed.data, "compiler.link") == 0)
            {
                fail("%s: %s has an ignored flag or no executable link command", names[test], configurations[config]);
            }
            free(printed.data);
            char* build[] = { "cmake", "--build", directory, "--config", (char*)configurations[config], "--parallel", "1", "--target", "buster_tier1_model_measure", "--verbose", 0 };
            if (run(log, assignments, build) != 0)
            {
                fputs(read_file(log).data, stderr);
                fail("%s: the real %s unit compile failed", names[test], configurations[config]);
            }
            check_ignored_flag_text(log);
        }
        pass("%s: fresh probes, Debug/Release compile/link commands and real unit compiles ignore the flags", names[test]);
    }
    BUILD("clean");
}

// The fresh build, the unchanged one and the edits.
static void check_incremental(void)
{
    char build_log[PATH_MAX];
    snprintf(build_log, sizeof(build_log), "%s/build.log", work);
    char temporary[PATH_MAX];
    snprintf(temporary, sizeof(temporary), "%s/tmp", work);
    mkdir(temporary, 0755);
    char tmpdir[PATH_MAX + 8];
    snprintf(tmpdir, sizeof(tmpdir), "TMPDIR=%s", temporary);
    const char* const scratch[] = { tmpdir, "CFLAGS=-f" BUSTER_REGRESSION_INVALID_FLAG "-cflags", "LDFLAGS=-Wl,--" BUSTER_REGRESSION_INVALID_FLAG "-ldflags", 0 };
    BUILD_ENV(scratch, "build", "--verbose");
    check_ignored_flag_text(build_log);
    if (access(release, X_OK) != 0 || !same_file("build/compiler", release))
    {
        fail("the fresh build didn't publish %s as build/compiler", release);
    }
    if (!compile_commands_of("Release"))
    {
        fail("build/compiler's compile commands aren't the Release configuration's alone");
    }
    char names[8192];
    if (planned(names, sizeof(names)))
    {
        fail("a fresh build left work to do: %s", names);
    }
    char* leftover[] = { "find", temporary, "-mindepth", "1", "-print", "-quit", 0 };
    Text left = capture(leftover);
    if (left.length != 0)
    {
        fail("build.sh left %s behind", left.data);
    }
    pass("a fresh build publishes the Release executable and compile commands, and leaves no temporary file");

    static Snapshot before;
    snapshot(&before);
    const struct stat compiler = status_of("build/compiler");
    char cache_path[PATH_MAX + 16];
    snprintf(cache_path, sizeof(cache_path), "%s/CMakeCache.txt", tree);
    const struct stat cache = status_of(cache_path);
    const struct stat commands = status_of("build/compile_commands.json");
    BUILD("build");
    ran_since(&before, names, sizeof(names));
    if (!contains_line(build_log, "ninja: no work to do.") || names[0] != 0)
    {
        fail("an unchanged build ran %s", names);
    }
    if (status_of("build/compiler").st_ino != compiler.st_ino || !same_time(status_of(cache_path), cache) ||
        !same_time(status_of("build/compile_commands.json"), commands))
    {
        fail("an unchanged build configured the tree or published again");
    }
    pass("an unchanged build compiles, links, bakes and publishes nothing");

    check_ownership(tree, "Release");

    char selected[PATH_MAX];
    snprintf(selected, sizeof(selected), "%s/Release/compile_commands.json", tree);
    if (unlink(selected) != 0)
    {
        fail("can't remove the cached Release compile commands");
    }
    BUILD("build");
    ran_since(&before, names, sizeof(names));
    if (strcmp(names, "compile_commands.json ") != 0 || !compile_commands_of("Release") ||
        status_of("build/compiler").st_ino != compiler.st_ino ||
        !same_time(status_of("build/compile_commands.json"), commands))
    {
        fail("a missing cached database ran %s or changed the published files", names);
    }
    snapshot(&before);
    if (unlink("build/compile_commands.json") != 0)
    {
        fail("can't remove the published compile commands");
    }
    BUILD("build");
    ran_since(&before, names, sizeof(names));
    if (names[0] != 0 || !compile_commands_of("Release") || status_of("build/compiler").st_ino != compiler.st_ino)
    {
        fail("a missing published database ran %s or wasn't restored", names);
    }
    pass("missing cached or published compile commands are restored without compiling or linking");

    static const char shared_main_units[] =
        "main.c main_unit_assembler.c main_unit_backend.c main_unit_check_builtins.c main_unit_checker.c "
        "main_unit_dynamic.c main_unit_executable.c main_unit_frontend.c main_unit_link.c main_unit_types.c main_unit_x86.c ";
    static const struct
    {
        const char* source;
        const char* owners;
    } files[] = {
        { "src/buster/compiler/tier1_model_measure.c", 0 },
        { "src/buster/compiler/c_lexer.c", 0 },
        { "src/buster/compiler/host_isa.h", 0 },
        { "src/buster/compiler/c_check_builtin_bits_test.c", "main_unit_check_builtins.c " },
        { "src/buster/compiler/profile_test.c", "main.c " },
        { "src/buster/compiler/c_files_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/tier1_wide_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/c_check_program_test.c", "main_unit_checker.c " },
        { "src/buster/compiler/c_check_generic_test.c", "main_unit_checker.c " },
        { "src/buster/compiler/c_check_attribute_test.c", "main_unit_checker.c " },
        { "src/buster/compiler/c_check_program_amx_test.c", "main_unit_checker.c " },
        { "src/buster/compiler/c_checker_driver_test.c", "main_unit_checker.c " },
        { "src/buster/compiler/c_check_debug_tier1_parameters_test.c", "main_unit_checker.c " },
        { "src/buster/compiler/c_checker_test.c", shared_main_units },
        { "src/buster/compiler/ir_simd_model_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/interpreter_wide_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/ir_optimize_simd_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/tier1_call_probe_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/tier1_address_coalesce_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/ir_if_convert.c", shared_main_units },
        { "src/buster/compiler/ir_optimize_verified.c", shared_main_units },
        { "src/buster/compiler/tier1_select_burs_test.c", shared_main_units },
        { "src/buster/compiler/preprocessor_fuzz_generate.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_preprocessor_directives_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_predefines_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_gnu_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_parser_bindings_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/x86_target_policy_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_preprocessor_history_test.c", "main_unit_checker.c main_unit_frontend.c " },
        { "src/buster/compiler/c_preprocessor_test.c", shared_main_units },
        { "src/buster/compiler/c_preprocessor_oracle.c", shared_main_units },
        { "src/buster/compiler/c_parser_test.c", shared_main_units },
        { "src/buster/compiler/c_parser_bindings.c", shared_main_units },
        { "src/buster/compiler/c_builtin_intrinsic_catalog.c", shared_main_units },
        { "src/buster/compiler/foreign_unwind_runtime_test.c", "main_unit_executable.c " },
        { "src/buster/compiler/foreign_driver_symbols_test.c", "main_unit_executable.c " },
        { "src/buster/compiler/foreign_strings_output_test.c", "main_unit_executable.c " },
        { "src/buster/compiler/foreign_gc_benchmark.c", "main_unit_executable.c " },
        { "src/buster/compiler/foreign_test.c", "main_unit_checker.c main_unit_executable.c " },
        { "src/buster/compiler/foreign_link.c", shared_main_units },
        { "src/buster/compiler/foreign_unwind.c", shared_main_units },
        { "src/buster/compiler/tier0_x87_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/tier0_vector_baseline_test.inc", "main_unit_backend.c " },
        { "src/buster/compiler/tier0_mask_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/streaming_benchmark_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/tier0_test.c", shared_main_units },
    };
    for (uint32_t i = 0; i < sizeof(files) / sizeof(files[0]); i += 1)
    {
        const char* source = files[i].source;
        char expected[8192];
        dependents(source, expected, sizeof(expected));
        if (expected[0] == 0)
        {
            fail("no unit depends on %s", source);
        }
        // The vector-only standalone checker benchmark also reads the intrinsic catalog. Allow that exact sorted
        // extra owner while still requiring every main unit; keep the full dependency list for Ninja's plan below.
        const char* checked_owners = expected;
        static const char catalog_benchmark[] = "c_checker_benchmark.c ";
        if (strcmp(source, "src/buster/compiler/c_builtin_intrinsic_catalog.c") == 0 &&
            strncmp(checked_owners, catalog_benchmark, sizeof(catalog_benchmark) - 1) == 0)
        {
            checked_owners += sizeof(catalog_benchmark) - 1;
        }
        if (files[i].owners != 0 && strcmp(checked_owners, files[i].owners) != 0)
        {
            fail("%s is included by %s; expected exactly %s", source, expected, files[i].owners);
        }
        const struct stat file = status_of(source);
        utimensat(AT_FDCWD, source, 0, 0);
        char plan[8192];
        planned(plan, sizeof(plan));
        const struct timespec times[2] = { file.st_atim, file.st_mtim };
        utimensat(AT_FDCWD, source, times, 0);
        if (strcmp(plan, expected) != 0)
        {
            fail("%s: Ninja would compile %s, not %s", source, plan, expected);
        }
        if (planned(plan, sizeof(plan)))
        {
            fail("%s's time restored, Ninja still has work", source);
        }
        uint32_t count = 0;
        for (const char* space = strchr(expected, ' '); space != 0; space = strchr(space + 1, ' '))
        {
            count += 1;
        }
        pass("%s rebuilds its %u dependents alone", source, count);
    }
    snapshot(&before);
    const struct stat selected_before = status_of(selected);
    utimensat(AT_FDCWD, "src/buster/compiler/tier1_model_measure.c", 0, 0);
    BUILD("build");
    ran_since(&before, names, sizeof(names));
    if (strcmp(names, "compiler compiler.link tier1_model_measure.c.o ") != 0 || !same_file("build/compiler", release) ||
        !same_time(status_of(selected), selected_before))
    {
        fail("an edited unit ran %s", names);
    }
    pass("an edited unit is compiled, linked, baked and published alone");
}

static void check_configurations(void)
{
    const struct stat release_status = status_of(release);
    BUILD("--config", "Debug");
    if (access(debug, X_OK) != 0 || !same_file("build/compiler", debug) || !compile_commands_of("Debug"))
    {
        fail("the Debug build didn't publish its executable and compile commands");
    }
    if (!same_time(status_of(release), release_status))
    {
        fail("the Debug build changed the Release executable");
    }
    static Snapshot before;
    char names[8192];
    snapshot(&before);
    BUILD("build");
    ran_since(&before, names, sizeof(names));
    if (names[0] != 0 || !same_file("build/compiler", release) || !compile_commands_of("Release"))
    {
        fail("switching back to Release ran %s or didn't publish it", names);
    }
    BUILD("--config", "Debug");
    ran_since(&before, names, sizeof(names));
    if (names[0] != 0 || !same_file("build/compiler", debug) || !compile_commands_of("Debug"))
    {
        fail("switching back to Debug ran %s or didn't publish it", names);
    }
    BUILD("build");
    pass("Debug and Release share the tree and switch without building");

    static const char* const bakes[][2] = { { "BUSTER_BAKE=0", 0 }, { "BUSTER_BAKE=1", 0 } };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        snapshot(&before);
        BUILD_ENV(bakes[i], "build");
        ran_since(&before, names, sizeof(names));
        if (strcmp(compiler_outputs(names), "compiler ") != 0)
        {
            fail("%s ran %s", bakes[i][0], names);
        }
    }
    pass("BUSTER_BAKE reruns the post-link step alone, both ways");
    // GNU ld, which auto never takes by its path (a static build's is lld, then mold, then clang's default): another
    // linker than auto's, whichever of them this runner has.
    char* bfd[] = { "sh", "-c", "command -v ld.bfd", 0 };
    if (run("/dev/null", 0, bfd) == 0)
    {
        const char* const linker[] = { "BUSTER_LINKER=bfd", 0 };
        snapshot(&before);
        BUILD_ENV(linker, "build");
        ran_since(&before, names, sizeof(names));
        if (strcmp(compiler_outputs(names), "compiler compiler.link ") != 0)
        {
            fail("BUSTER_LINKER=bfd ran %s", names);
        }
        BUILD("build");
        pass("BUSTER_LINKER relinks alone");
    }
    // A static link never by wild (#652): wild 0.10.0's static executables fault at their first TLS access, so wild by
    // any name, ld.wild through a symlink among them, links dynamically, and its units compile without
    // BUSTER_HOST_STATIC, which auto's static link's have.
    char* which_wild[] = { "sh", "-c", "command -v wild", 0 };
    if (run("/dev/null", 0, which_wild) == 0)
    {
        char wild[PATH_MAX];
        capture_line(which_wild, wild, sizeof(wild));
        char alias[PATH_MAX];
        snprintf(alias, sizeof(alias), "%s/ld.wild", work);
        unlink(alias);
        if (symlink(wild, alias) != 0)
        {
            fail("can't link %s to %s", alias, wild);
        }
        char assignment[PATH_MAX + 16];
        snprintf(assignment, sizeof(assignment), "BUSTER_LINKER=%s", alias);
        char* aliased[] = { "env", assignment, "./build.sh", "print-compile-flags", 0 };
        char* automatic[] = { "env", "BUSTER_LINKER=auto", "./build.sh", "print-compile-flags", 0 };
        Text aliased_flags = capture(aliased);
        Text automatic_flags = capture(automatic);
        const char* const define = "-DBUSTER_HOST_STATIC=1";
        const bool aliased_static = memmem(aliased_flags.data, aliased_flags.length, define, strlen(define)) != 0;
        const bool automatic_static = memmem(automatic_flags.data, automatic_flags.length, define, strlen(define)) != 0;
        free(aliased_flags.data);
        free(automatic_flags.data);
        if (aliased_static || !automatic_static)
        {
            fail("BUSTER_LINKER=%s (wild) %s statically, auto %s", alias, aliased_static ? "links" : "doesn't link", automatic_static ? "does" : "doesn't");
        }
        pass("a static link takes no wild, by any name, and auto's is static");
    }
}

// Waits up to `seconds` for `child`, and returns its status, or -1 if it is still running.
static int finish_within(pid_t child, unsigned seconds)
{
    int status = -1;
    for (unsigned tenth = 0; status < 0 && tenth < 10 * seconds; tenth += 1)
    {
        int raw = 0;
        if (waitpid(child, &raw, WNOHANG) == child)
        {
            status = WIFEXITED(raw) ? WEXITSTATUS(raw) : 128 + WTERMSIG(raw);
        }
        else
        {
            struct timespec time = { .tv_nsec = 100000000 };
            nanosleep(&time, 0);
        }
    }
    return status;
}

// A build, `build`'s ./build.sh in a session of its own, and 3 s into it, as it configures or compiles, a clean,
// `clean`'s: both must end, the clean after saying it waits for `waits_for`. Lock waits that go round would leave both
// waiting for good, which is reported after 600 s, once both are stopped.
static void overlap(char* const* build, char* const* clean, const char* waits_for, const char* what)
{
    char build_log[PATH_MAX];
    char clean_log[PATH_MAX];
    snprintf(build_log, sizeof(build_log), "%s/overlapping-build.log", work);
    snprintf(clean_log, sizeof(clean_log), "%s/overlapping-clean.log", work);
    const pid_t builder = start(build_log, 0, build, true);
    sleep_seconds(3);
    const pid_t cleaner = start(clean_log, 0, clean, true);
    const int built = finish_within(builder, 600);
    const int cleaned = finish_within(cleaner, 60);
    if (built < 0 || cleaned < 0)
    {
        kill(-builder, SIGTERM);
        kill(-cleaner, SIGTERM);
        finish_within(builder, 30);
        finish_within(cleaner, 30);
        fail("%s: the build and the clean still ran after 600 s, each waiting for the other", what);
    }
    Text said = read_file(clean_log);
    const bool waited = strstr(said.data, waits_for) != 0;
    free(said.data);
    if (built != 0 || cleaned != 0 || !waited)
    {
        fputs(read_file(build_log).data, stderr);
        fputs(read_file(clean_log).data, stderr);
        fail("%s: the build exited with %d and the clean with %d, %s waiting", what, built, cleaned, waited ? "after" : "without");
    }
}

// A clean --all while a Debug build configures and compiles the tree afresh: it waits for the build, which publishes,
// and then removes every tree and the published files.
static void check_clean_all(void)
{
    char* build[] = { "./build.sh", "--config", "Debug", 0 };
    char* clean[] = { "./build.sh", "clean", "--all", 0 };
    overlap(build, clean, "waiting for the other builds and cleans to end", "clean --all during a build");
    if (access("build/cmake", F_OK) == 0 || access("build/compiler", F_OK) == 0 || access("build/compile_commands.json", F_OK) == 0)
    {
        fail("clean --all left a tree or a published file");
    }
    pass("clean --all waits for a build of a tree, then removes every tree and the published files");
}

static void check_failures(void)
{
    // A post-link step that fails, by an nm that does.
    ino_t inode = status_of("build/compiler").st_ino;
    const struct stat commands = status_of("build/compile_commands.json");
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/nm", work);
    mkdir(directory, 0755);
    char nm[PATH_MAX + 4];
    snprintf(nm, sizeof(nm), "%s/nm", directory);
    write_file(nm, "#!/bin/sh\nexit 3\n", 0755);
    char path[PATH_MAX * 2];
    snprintf(path, sizeof(path), "PATH=%s:%s", directory, getenv("PATH"));
    const char* const fake[] = { path, 0 };
    if (build_with(fake, true, "build", (char*)0) == 0)
    {
        fail("a failed post-link step succeeded");
    }
    if (status_of("build/compiler").st_ino != inode || access(release, F_OK) == 0 ||
        !same_time(status_of("build/compile_commands.json"), commands))
    {
        fail("a failed post-link step published build/compiler or left %s", release);
    }
    BUILD("build");
    if (!same_file("build/compiler", release))
    {
        fail("the build after a failed post-link step didn't publish");
    }

    // A compile that fails.
    inode = status_of("build/compiler").st_ino;
    snprintf(edited_copy, sizeof(edited_copy), "%s/unit.c", work);
    Text unit = read_file(edited_unit);
    write_file(edited_copy, unit.data, 0644);
    edited = true;
    FILE* file = fopen(edited_unit, "a");
    if (file == 0 || fputs("int buster_build_regression(void) { return }\n", file) < 0 || fclose(file) != 0)
    {
        fail("can't edit %s", edited_unit);
    }
    const int status = build_with(0, true, "build", (char*)0);
    write_file(edited_unit, unit.data, 0644);
    edited = false;
    free(unit.data);
    if (status == 0 || status_of("build/compiler").st_ino != inode)
    {
        fail("a failed compile succeeded or published build/compiler");
    }

    // An interrupt of the build's process group, as a terminal's, and a termination of build.sh alone, as
    // timeout(1)'s: main_unit_types.c takes 20 s on the 9700X and longer elsewhere, so each comes while it compiles.
    char log[PATH_MAX];
    snprintf(log, sizeof(log), "%s/interrupted.log", work);
    char* argv[] = { "./build.sh", 0 };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        utimensat(AT_FDCWD, "src/buster/compiler/main_unit_types.c", 0, 0);
        const pid_t child = start(log, 0, argv, true);
        sleep_seconds(3);
        if (i == 0)
        {
            kill(-child, SIGINT);
        }
        else
        {
            kill(child, SIGTERM);
        }
        const int result = finish_within(child, 30);
        if (result != (i == 0 ? 130 : 143))
        {
            fail("an %s build exited with %d", i == 0 ? "interrupted" : "terminated", result);
        }
        if (status_of("build/compiler").st_ino != inode)
        {
            fail("an %s build published build/compiler", i == 0 ? "interrupted" : "terminated");
        }
        // Nothing of the stopped build may still hold the tree, which the next build would wait for.
        const pid_t next = start(0, 0, argv, false);
        if (finish_within(next, 600) != 0)
        {
            fail("the build after an %s one failed or waited", i == 0 ? "interrupted" : "terminated");
        }
        inode = status_of("build/compiler").st_ino;
    }
    if (!same_file("build/compiler", release))
    {
        fail("the build after an interrupt didn't publish");
    }

    // A clean of the tree while a build compiles in it: the clean waits for the build, which publishes.
    utimensat(AT_FDCWD, "src/buster/compiler/main_unit_types.c", 0, 0);
    char* clean_debug[] = { "./build.sh", "clean", "--config", "Debug", 0 };
    overlap(argv, clean_debug, "waiting for another build or clean", "a clean of a tree during its build");
    if (!same_file("build/compiler", release) || access(debug, F_OK) == 0)
    {
        fail("a clean during a build of its tree didn't wait for its publication or didn't clean");
    }

    // An invalid setting, which must configure nothing.
    const char* const invalid[] = { "BUSTER_SANITIZER=invalid", 0 };
    if (build_with(invalid, true, "configure", (char*)0) == 0 || access("build/cmake/CMakeCache.txt", F_OK) == 0)
    {
        fail("an invalid setting configured a tree");
    }
    pass("a failed post-link step, a failed compile, an interrupt, a termination and an invalid setting publish nothing, "
         "and a clean during a build waits for it");

    // A configuration's clean, which takes its outputs alone, after which check-call-graph fails before capturing.
    BUILD("--config", "Debug");
    BUILD("build");
    BUILD("clean", "--config", "Debug");
    if (access(debug, F_OK) == 0 || access(release, X_OK) != 0 || !same_file("build/compiler", release))
    {
        fail("clean --config Debug took more or less than the Debug configuration's outputs");
    }
    char check_log[PATH_MAX];
    snprintf(check_log, sizeof(check_log), "%s/build.log", work);
    const char* const own[] = { "BUSTER_COMPILER=", 0 };
    const int checked = build_with(own, true, "check-call-graph", "--config", "Debug", (char*)0);
    Text said = read_file(check_log);
    const bool missing = strstr(said.data, "is missing") != 0 && strstr(said.data, "Capturing") == 0;
    free(said.data);
    if (checked == 0 || !missing)
    {
        fail("check-call-graph without a Debug executable didn't fail before capturing");
    }
    pass("clean --config removes that configuration's outputs, and check-call-graph needs its executable");
}

static void check_presets_and_prints(void)
{
    char native[PATH_MAX + 32];
    snprintf(native, sizeof(native), "%s/build/cmake/native", root);
    if (strcmp(tree, native) == 0)
    {
        char log[PATH_MAX];
        snprintf(log, sizeof(log), "%s/preset.log", work);
        char* configure[] = { "cmake", "--preset", "native", 0 };
        char* release_build[] = { "cmake", "--build", "--preset", "release", 0 };
        char* debug_build[] = { "cmake", "--build", "--preset", "debug", 0 };
        char* release_test[] = { "ctest", "--preset", "release-build", 0 };
        char* debug_test[] = { "ctest", "--preset", "debug-build", 0 };
        static Snapshot before;
        char names[8192];
        if (run(log, 0, configure) != 0)
        {
            fail("cmake --preset native failed");
        }
        snapshot(&before);
        if (run(log, 0, release_build) != 0)
        {
            fail("the release preset's build failed");
        }
        ran_since(&before, names, sizeof(names));
        if (compiler_outputs(names)[0] != 0)
        {
            fail("the release preset rebuilt %s", names);
        }
        if (run(log, 0, debug_build) != 0 || run(0, 0, release_test) != 0 || run(0, 0, debug_test) != 0)
        {
            fail("the debug preset's build or the presets' tests failed");
        }
        pass("the presets configure, build and test the build.sh tree");
    }

    char* sources[] = { "./build.sh", "print-sources", 0 };
    char* flags[] = { "./build.sh", "print-compile-flags", 0 };
    Text printed = capture(sources);
    if (printed.length == 0 || printed.data[printed.length - 1] != 0 || strcmp(printed.data, "src/buster/compiler/main.c") != 0)
    {
        fail("print-sources didn't print NUL-terminated sources, main.c first");
    }
    free(printed.data);
    printed = capture(flags);
    if (printed.length == 0 || printed.data[printed.length - 1] != 0)
    {
        fail("print-compile-flags didn't print NUL-terminated arguments");
    }
    free(printed.data);
    pass("the print commands print NUL-delimited arguments");
}

static void check_experiments(void)
{
    // The shifted links, 64 given twice.
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/shifts", work);
    BUILD("shifts", directory, "lane_benchmark.c", "0", "64", "64");
    for (uint32_t i = 0; i < 2; i += 1)
    {
        char executable[PATH_MAX + 16];
        snprintf(executable, sizeof(executable), "%s/compiler-%s", directory, i == 0 ? "0" : "64");
        if (!has_symbol(executable, " buster_shift_padding"))
        {
            fail("%s has no padding", executable);
        }
    }
    char shifted[PATH_MAX + 16];
    snprintf(shifted, sizeof(shifted), "%s/compiler-64", directory);
    char* baked[] = { shifted, "test-baked-tables", 0 };
    if (same_bytes(shifted, "build/compiler") || run("/dev/null", 0, baked) != 0)
    {
        fail("compiler-64 is build/compiler, or doesn't run");
    }
    BUILD("build");
    pass("shifts link padded executables, each shift once");

    // A lexer variant, in a tree of its own, published as build/compiler, and the published executable back. The
    // variant's unit defines it, and the executable has it where c_lexer_benchmark.c refers to it: on a host with the
    // vector path (host_isa.h). Elsewhere nothing does, and a linker that drops what nothing refers to, as wild does,
    // leaves it out.
    const char* const quick[] = { "BUSTER_DEBUG_INFO=0", 0 };
    BUILD_ENV(quick, "lexer-variant", "regression=src/buster/compiler/c_lexer.c");
    char* quick_output[] = { "env", "BUSTER_DEBUG_INFO=0", "./build.sh", "print-output", 0 };
    char lexer[PATH_MAX];
    capture_line(quick_output, lexer, sizeof(lexer));
    *strrchr(lexer, '/') = 0;
    *strrchr(lexer, '/') = 0;
    char lexer_compiler[PATH_MAX + 32];
    char variant[PATH_MAX + 128];
    char benchmark[PATH_MAX + 128];
    snprintf(lexer_compiler, sizeof(lexer_compiler), "%s-lexer/Release/compiler", lexer);
    snprintf(variant, sizeof(variant), "%s-lexer/CMakeFiles/buster_lexer_variant_regression.dir/Release/src/buster/compiler/c_lexer_variant.c.o", lexer);
    snprintf(benchmark, sizeof(benchmark), "%s-lexer/CMakeFiles/buster_c_lexer_benchmark.dir/Release/src/buster/compiler/c_lexer_benchmark.c.o", lexer);
    if (!same_file("build/compiler", lexer_compiler) || !has_symbol(variant, " T c_lexer_variant_regression"))
    {
        fail("lexer-variant didn't compile the variant or publish its tree's executable");
    }
    if (has_symbol(benchmark, " U c_lexer_variant_regression") && !has_symbol("build/compiler", " T c_lexer_variant_regression"))
    {
        fail("the lexer variant isn't in build/compiler");
    }
    BUILD("build");
    if (!same_file("build/compiler", release))
    {
        fail("the build after lexer-variant didn't publish");
    }
    pass("lexer-variant builds a tree of its own");
}

static void on_signal(int number)
{
    restore();
    signal(number, SIG_DFL);
    raise(number);
}

// Exercise the production buster_link function in a tiny project: it builds actual linked outputs without recompiling
// the compiler. The post-link copies stand in for baking and check that relinking propagates to downstream outputs.
static void check_crt_link_dependencies(void)
{
    if (chdir(work) != 0)
    {
        fail("can't enter %s", work);
    }
    char* dirs[] = { "cmake", "-E", "make_directory", "runtime/usr/lib/x86_64-linux-gnu",
                     "runtime/usr/lib/gcc/x86_64-linux-gnu/15", 0 };
    if (run(0, 0, dirs) != 0)
    {
        fail("can't make the CRT fixture directories");
    }
    write_file("crt1.c", "void _start(void) {}\n", 0644);
    write_file("crti.c", "void crt_init(void) {}\n", 0644);
    write_file("crtn.c", "void crt_end(void) {}\n", 0644);
    write_file("main.c", "int fixture(void) { return 0; }\n", 0644);
    const char* names[] = { "crt1", "crti", "crtn" };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        char source[32];
        char object[128];
        snprintf(source, sizeof(source), "%s.c", names[i]);
        snprintf(object, sizeof(object), "runtime/usr/lib/x86_64-linux-gnu/%s.o", names[i]);
        char* compile[] = { "clang", "-c", source, "-o", object, 0 };
        if (run(0, 0, compile) != 0)
        {
            fail("can't compile %s", source);
        }
    }
    const char* libraries[] = { "runtime/usr/lib/x86_64-linux-gnu/libm.so.6",
                                "runtime/usr/lib/x86_64-linux-gnu/libc.so.6",
                                "runtime/usr/lib/gcc/x86_64-linux-gnu/15/libgcc.a" };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        char* archive[] = { "ar", "rcs", (char*)libraries[i], 0 };
        if (run(0, 0, archive) != 0)
        {
            fail("can't make %s", libraries[i]);
        }
    }
    write_file("CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.25)\n"
        "project(crt_fixture C)\n"
        "set(CMAKE_LINK_DEPENDS_USE_LINKER OFF)\n"
        "set(BUSTER_HOST_RUNTIME ${CMAKE_CURRENT_SOURCE_DIR}/runtime)\n"
        "set(buster_arch x86-64)\n"
        "set(buster_link_mode -no-pie)\n"
        "file(READ \"${BUSTER_SOURCE}/CMakeLists.txt\" source)\n"
        "string(FIND \"${source}\" \"function(buster_link target)\" begin)\n"
        "if(begin LESS 0)\n  message(FATAL_ERROR \"buster_link not found\")\nendif()\n"
        "string(SUBSTRING \"${source}\" ${begin} -1 tail)\n"
        "string(FIND \"${tail}\" \"\\nendfunction()\" end)\n"
        "if(end LESS 0)\n  message(FATAL_ERROR \"buster_link end not found\")\nendif()\n"
        "string(LENGTH \"\\nendfunction()\" marker_length)\n"
        "math(EXPR length \"${end} + ${marker_length}\")\n"
        "string(SUBSTRING \"${tail}\" 0 ${length} function_text)\n"
        "cmake_language(EVAL CODE \"${function_text}\")\n"
        "foreach(name compiler_link compiler_shift)\n"
        "  add_executable(${name} main.c)\n"
        "  buster_link(${name})\n"
        "  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/$<CONFIG>/${name}.baked\n"
        "    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${name}> ${CMAKE_BINARY_DIR}/$<CONFIG>/${name}.baked\n"
        "    DEPENDS ${name} VERBATIM)\n"
        "  add_custom_target(${name}_baked ALL DEPENDS ${CMAKE_BINARY_DIR}/$<CONFIG>/${name}.baked)\n"
        "endforeach()\n", 0644);
    char source_arg[PATH_MAX + 32];
    snprintf(source_arg, sizeof(source_arg), "-DBUSTER_SOURCE=%s", root);
    char* configure[] = { "cmake", "-S", ".", "-B", "out", "-G", "Ninja Multi-Config", "-DCMAKE_C_COMPILER=clang",
                          source_arg, 0 };
    Text log = capture(configure);
    free(log.data);
    char* build[] = { "cmake", "--build", "out", "--config", "Debug", "--parallel", "1", 0 };
    log = capture(build);
    free(log.data);
    log = capture(build);
    if (strstr(log.data, "no work to do") == 0)
    {
        fail("unchanged CRT fixture rebuilt: %s", log.data);
    }
    free(log.data);
    for (uint32_t i = 0; i < 2; i += 1)
    {
        const struct timespec delay = { 1, 0 };
        nanosleep(&delay, 0);
        char object[128];
        snprintf(object, sizeof(object), "runtime/usr/lib/x86_64-linux-gnu/%s.o", names[i]);
        if (utimensat(AT_FDCWD, object, 0, 0) != 0)
        {
            fail("can't update %s", object);
        }
        log = capture(build);
        if (strstr(log.data, "Linking C executable Debug/compiler_link") == 0 ||
            strstr(log.data, "Linking C executable Debug/compiler_shift") == 0 ||
            strstr(log.data, "Generating Debug/compiler_link.baked") == 0 ||
            strstr(log.data, "Generating Debug/compiler_shift.baked") == 0)
        {
            fail("%s didn't relink and post-link both executables: %s", names[i], log.data);
        }
        free(log.data);
        log = capture(build);
        if (strstr(log.data, "no work to do") == 0)
        {
            fail("unchanged build after %s rebuilt: %s", names[i], log.data);
        }
        free(log.data);
    }
    if (chdir(root) != 0)
    {
        fail("can't return to %s", root);
    }
    pass("each baseline CRT startup object relinks and post-links both executables; unchanged builds are no-ops");
}

// Exercise the real database target with one tiny source: both configurations, content-changing regeneration,
// ordinary source edits, missing output and filter changes, plus a direct build with export disabled.
static void check_compile_commands(void)
{
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/compile-commands", work);
    if (mkdir(directory, 0755) != 0 || chdir(directory) != 0 || mkdir("cmake", 0755) != 0)
    {
        fail("can't make the compile commands fixture");
    }
    char filter_path[PATH_MAX];
    snprintf(filter_path, sizeof(filter_path), "%s/cmake/BusterCompileCommands.cmake", root);
    Text filter = read_file(filter_path);
    write_file("cmake/BusterCompileCommands.cmake", filter.data, 0644);
    free(filter.data);
    write_file("main.c", "int main(void) { return 0; }\n", 0644);
    write_file("CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.25)\n"
        "project(compile_commands_fixture C)\n"
        "set(CMAKE_CONFIGURATION_TYPES Debug Release)\n"
        "set(FIXTURE_VALUE 1 CACHE STRING \"A command that changes\")\n"
        "file(READ \"${BUSTER_SOURCE}/CMakeLists.txt\" source)\n"
        "string(FIND \"${source}\" \"function(buster_compile_commands target)\" begin)\n"
        "if(begin LESS 0)\n  message(FATAL_ERROR \"buster_compile_commands not found\")\nendif()\n"
        "string(SUBSTRING \"${source}\" ${begin} -1 tail)\n"
        "string(FIND \"${tail}\" \"\\nendfunction()\" end)\n"
        "if(end LESS 0)\n  message(FATAL_ERROR \"buster_compile_commands end not found\")\nendif()\n"
        "string(LENGTH \"\\nendfunction()\" marker_length)\n"
        "math(EXPR length \"${end} + ${marker_length}\")\n"
        "string(SUBSTRING \"${tail}\" 0 ${length} function_text)\n"
        "cmake_language(EVAL CODE \"${function_text}\")\n"
        "add_executable(compiler main.c)\n"
        "target_compile_definitions(compiler PRIVATE FIXTURE_VALUE=${FIXTURE_VALUE})\n"
        "buster_compile_commands(compiler)\n", 0644);
    char source_arg[PATH_MAX + 32];
    snprintf(source_arg, sizeof(source_arg), "-DBUSTER_SOURCE=%s", root);
    char* configure[] = { "cmake", "-S", ".", "-B", "out", "-G", "Ninja Multi-Config", source_arg,
                          "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DFIXTURE_VALUE=1", 0 };
    Text log = capture(configure);
    free(log.data);
    char* build[] = { "cmake", "--build", "out", "--config", "Debug", "--parallel", "1", 0 };
    const char* configurations[] = { "Debug", "Release" };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        build[4] = (char*)configurations[i];
        log = capture(build);
        free(log.data);
        char commands[128];
        snprintf(commands, sizeof(commands), "out/%s/compile_commands.json", configurations[i]);
        if (!compile_commands_file_of(commands, configurations[i]))
        {
            fail("the cached %s database has the wrong configuration", configurations[i]);
        }
        log = capture(build);
        if (strstr(log.data, "no work to do") == 0)
        {
            fail("unchanged %s database rebuilt: %s", configurations[i], log.data);
        }
        free(log.data);
    }
    build[4] = "Debug";
    const struct stat commands = status_of("out/Debug/compile_commands.json");
    if (utimensat(AT_FDCWD, "main.c", 0, 0) != 0)
    {
        fail("can't update the fixture source");
    }
    log = capture(build);
    if (strstr(log.data, "Building C object") == 0 ||
        !same_time(status_of("out/Debug/compile_commands.json"), commands))
    {
        fail("a source edit didn't compile alone: %s", log.data);
    }
    free(log.data);
    const struct stat compiler = status_of("out/Debug/compiler");
    if (unlink("out/Debug/compile_commands.json") != 0)
    {
        fail("can't remove the fixture database");
    }
    log = capture(build);
    free(log.data);
    if (!compile_commands_file_of("out/Debug/compile_commands.json", "Debug") ||
        !same_time(status_of("out/Debug/compiler"), compiler))
    {
        fail("a missing database wasn't restored alone");
    }
    // The fixture is small enough for the output and the touch to share a filesystem timestamp tick.
    const struct timespec tick = { 0, 20000000 };
    nanosleep(&tick, 0);
    if (utimensat(AT_FDCWD, "cmake/BusterCompileCommands.cmake", 0, 0) != 0)
    {
        fail("can't update the fixture filter");
    }
    log = capture(build);
    if (strstr(log.data, "Selecting Debug compile commands") == 0 ||
        !same_time(status_of("out/Debug/compiler"), compiler))
    {
        fail("an updated filter didn't regenerate the database alone: %s", log.data);
    }
    free(log.data);
    configure[9] = "-DFIXTURE_VALUE=2";
    log = capture(configure);
    free(log.data);
    log = capture(build);
    free(log.data);
    Text changed = read_file("out/Debug/compile_commands.json");
    if (!compile_commands_file_of("out/Debug/compile_commands.json", "Debug") ||
        strstr(changed.data, "-DFIXTURE_VALUE=2") == 0 || strstr(changed.data, "-DFIXTURE_VALUE=1") != 0)
    {
        fail("changed compile arguments weren't refreshed in the cached database");
    }
    const struct stat changed_compiler = status_of("out/Debug/compiler");
    log = capture(configure);
    free(log.data);
    log = capture(build);
    free(log.data);
    Text unchanged = read_file("out/Debug/compile_commands.json");
    if (strcmp(unchanged.data, changed.data) != 0 || !same_time(status_of("out/Debug/compiler"), changed_compiler))
    {
        fail("an unchanged configure changed the selected commands or rebuilt the executable");
    }
    free(unchanged.data);
    free(changed.data);
    log = capture(build);
    if (strstr(log.data, "no work to do") == 0)
    {
        fail("an unchanged build after configuration regenerated the database: %s", log.data);
    }
    free(log.data);
    configure[4] = "without-export";
    configure[8] = "-DCMAKE_EXPORT_COMPILE_COMMANDS=OFF";
    log = capture(configure);
    free(log.data);
    build[2] = "without-export";
    log = capture(build);
    free(log.data);
    if (access("without-export/Debug/compiler", X_OK) != 0 ||
        access("without-export/Debug/compile_commands.json", F_OK) == 0)
    {
        fail("a direct build with export disabled needs a compilation database");
    }
    if (chdir(root) != 0)
    {
        fail("can't return to %s", root);
    }
    pass("compile commands select each configuration, reuse unchanged inputs, refresh changes and allow export off");
}

int main(int argc, char** argv)
{
    const bool flags_only = argc == 2 && strcmp(argv[1], "--flags-only") == 0;
    const bool crt_only = argc == 2 && strcmp(argv[1], "--crt-link-only") == 0;
    const bool commands_only = argc == 2 && strcmp(argv[1], "--compile-commands-only") == 0;
    const bool ownership_only = (argc == 2 || argc == 3) && strcmp(argv[1], "--ownership-only") == 0;
    if (argc != 1 && !flags_only && !crt_only && !commands_only && !ownership_only)
    {
        fail("usage: build-regression [--flags-only | --crt-link-only | --compile-commands-only | --ownership-only [COMPILER]]");
    }
    if (getcwd(root, sizeof(root)) == 0 || access("build.sh", X_OK) != 0)
    {
        fail("run it from the repository root");
    }
    const char* temporary = getenv("TMPDIR");
    snprintf(work, sizeof(work), "%s/build-regression-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    if (mkdtemp(work) == 0)
    {
        fail("can't make a directory under %s", work);
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    if (argc == 1)
    {
        check_standalone_preprocessing();
    }
    if (ownership_only)
    {
        check_existing_ownership(argc == 3 ? argv[2] : 0);
    }
    if (!ownership_only && !flags_only && !commands_only)
    {
        check_crt_link_dependencies();
    }
    if (!ownership_only && !flags_only && !crt_only)
    {
        check_compile_commands();
    }
    if (!ownership_only && !crt_only && !commands_only)
    {
        BUILD("clean");
        char* output[] = { "./build.sh", "print-output", 0 };
        char* output_debug[] = { "./build.sh", "print-output", "--config", "Debug", 0 };
        capture_line(output, release, sizeof(release));
        capture_line(output_debug, debug, sizeof(debug));
        snprintf(tree, sizeof(tree), "%s", release);
        *strrchr(tree, '/') = 0;
        *strrchr(tree, '/') = 0;
        snprintf(ninja_log, sizeof(ninja_log), "%s/.ninja_log", tree);
        check_ignored_flags();
        if (!flags_only)
        {
            check_clean_all();
            check_incremental();
            check_configurations();
            check_failures();
            check_presets_and_prints();
            check_experiments();
        }
    }
    char* remove[] = { "rm", "-rf", work, 0 };
    run(0, 0, remove);
    return 0;
}
