// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The build's regressions (.github/workflows/build.yml; docs/commands/build.md). From the repository root, with clang,
// cmake, ctest and ninja on PATH:
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror -o build-regression tools/ci/build_regression.c && ./build-regression
//
// It removes this environment's tree and checks that
//   - a fresh ./build.sh builds and publishes the Release executable and its configuration's compile commands, and
//     leaves no temporary file behind;
//   - an unchanged build configures, compiles, links, bakes and publishes nothing;
//   - an edited unit, an included .c and a header rebuild their dependents alone (Ninja's plan against its depfiles),
//     and an edited unit is compiled, linked, baked and published alone;
//   - Debug and Release share the tree, and switching back builds nothing and publishes that configuration's file;
//   - BUSTER_BAKE reruns the post-link step alone, both ways, and BUSTER_LINKER the link;
//   - a failed post-link step, a failed compile, an interrupt of the build's process group and a termination of
//     build.sh alone publish nothing and leave nothing running, and an invalid setting configures nothing;
//   - clean --config removes that configuration's outputs alone, and check-call-graph fails at once without them;
//   - the presets configure, build and test the same tree, and the print commands print NUL-delimited arguments;
//   - shifts link the padded executables, each shift once, and lexer-variant builds a tree of its own.
// It leaves the tree built. Each check prints what it checked; the first that fails stops it, after it restores the
// source it edits.
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

typedef struct Text Text;
struct Text
{
    char* data;
    size_t length;
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

// Whether every "output" of build/compile_commands.json names the configuration `config`, and there is one.
static bool compile_commands_of(const char* config)
{
    Text text = read_file("build/compile_commands.json");
    char directory[64];
    snprintf(directory, sizeof(directory), ".dir/%s/", config);
    uint32_t count = 0;
    bool result = true;
    for (char* output = strstr(text.data, "\"output\""); output != 0; output = strstr(output + 1, "\"output\""))
    {
        char* end = strchr(output + 8, '\n');
        if (end != 0)
        {
            *end = 0;
        }
        result = result && strstr(output, directory) != 0;
        count += 1;
        output = end != 0 ? end : output;
    }
    free(text.data);
    return result && count != 0;
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
    const char* const scratch[] = { tmpdir, 0 };
    BUILD_ENV(scratch, "build");
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

    static const char* const files[] = {
        "src/buster/compiler/tier1_model_measure.c",
        "src/buster/compiler/c_lexer.c",
        "src/buster/compiler/host_isa.h",
    };
    for (uint32_t i = 0; i < sizeof(files) / sizeof(files[0]); i += 1)
    {
        char expected[8192];
        dependents(files[i], expected, sizeof(expected));
        if (expected[0] == 0)
        {
            fail("no unit depends on %s", files[i]);
        }
        const struct stat file = status_of(files[i]);
        utimensat(AT_FDCWD, files[i], 0, 0);
        char plan[8192];
        planned(plan, sizeof(plan));
        const struct timespec times[2] = { file.st_atim, file.st_mtim };
        utimensat(AT_FDCWD, files[i], times, 0);
        if (strcmp(plan, expected) != 0)
        {
            fail("%s: Ninja would compile %s, not %s", files[i], plan, expected);
        }
        if (planned(plan, sizeof(plan)))
        {
            fail("%s's time restored, Ninja still has work", files[i]);
        }
        uint32_t count = 0;
        for (const char* space = strchr(expected, ' '); space != 0; space = strchr(space + 1, ' '))
        {
            count += 1;
        }
        pass("%s rebuilds its %u dependents alone", files[i], count);
    }
    snapshot(&before);
    utimensat(AT_FDCWD, "src/buster/compiler/tier1_model_measure.c", 0, 0);
    BUILD("build");
    ran_since(&before, names, sizeof(names));
    if (strcmp(names, "compiler compiler.link tier1_model_measure.c.o ") != 0 || !same_file("build/compiler", release))
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
    if (names[0] != 0 || !same_file("build/compiler", release))
    {
        fail("switching back to Release ran %s or didn't publish it", names);
    }
    BUILD("--config", "Debug");
    ran_since(&before, names, sizeof(names));
    if (names[0] != 0 || !same_file("build/compiler", debug))
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
        if (strcmp(names, "compiler ") != 0)
        {
            fail("%s ran %s", bakes[i][0], names);
        }
    }
    pass("BUSTER_BAKE reruns the post-link step alone, both ways");
    char* lld[] = { "sh", "-c", "command -v ld.lld", 0 };
    if (run("/dev/null", 0, lld) == 0)
    {
        const char* const linker[] = { "BUSTER_LINKER=lld", 0 };
        snapshot(&before);
        BUILD_ENV(linker, "build");
        ran_since(&before, names, sizeof(names));
        if (strcmp(names, "compiler compiler.link ") != 0)
        {
            fail("BUSTER_LINKER=lld ran %s", names);
        }
        BUILD("build");
        pass("BUSTER_LINKER relinks alone");
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

static void check_failures(void)
{
    // A post-link step that fails, by an nm that does.
    ino_t inode = status_of("build/compiler").st_ino;
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
    if (status_of("build/compiler").st_ino != inode || access(release, F_OK) == 0)
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

    // An invalid setting, which must configure nothing.
    const char* const invalid[] = { "BUSTER_SANITIZER=invalid", 0 };
    if (build_with(invalid, true, "configure", (char*)0) == 0 || access("build/cmake/CMakeCache.txt", F_OK) == 0)
    {
        fail("an invalid setting configured a tree");
    }
    pass("a failed post-link step, a failed compile, an interrupt, a termination and an invalid setting publish nothing");

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
        if (names[0] != 0)
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

    // A lexer variant, in a tree of its own, and the published executable back.
    const char* const quick[] = { "BUSTER_DEBUG_INFO=0", 0 };
    BUILD_ENV(quick, "lexer-variant", "regression=src/buster/compiler/c_lexer.c");
    if (!has_symbol("build/compiler", " T c_lexer_variant_regression"))
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

int main(void)
{
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
    BUILD("clean");
    char* output[] = { "./build.sh", "print-output", 0 };
    char* output_debug[] = { "./build.sh", "print-output", "--config", "Debug", 0 };
    capture_line(output, release, sizeof(release));
    capture_line(output_debug, debug, sizeof(debug));
    snprintf(tree, sizeof(tree), "%s", release);
    *strrchr(tree, '/') = 0;
    *strrchr(tree, '/') = 0;
    snprintf(ninja_log, sizeof(ninja_log), "%s/.ninja_log", tree);
    check_incremental();
    check_configurations();
    check_failures();
    check_presets_and_prints();
    check_experiments();
    char* remove[] = { "rm", "-rf", work, 0 };
    run(0, 0, remove);
    return 0;
}
