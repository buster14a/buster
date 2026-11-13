// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The build's regressions (.github/workflows/build.yml; docs/commands/build.md). From the repository root, with clang,
// cmake, ctest and ninja on PATH:
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror -o build-regression tools/ci/build_regression.c && ./build-regression
//   ./build-regression --flags-only  # Just the fresh-tree ignored-flag regressions; cleans the selected build tree.
//   ./build-regression --compile-commands-only  # Small database dependency fixture; does not build the compiler.
//   ./build-regression --driver-only  # Small build.sh helper fixtures; does not configure or build the compiler.
//   ./build-regression --parallel-codegen-only  # Small optimized-IR partition and dependency fixture.
//   ./build-regression --develop-only  # The Develop configuration's checks alone, on the selected tree.
//   ./build-regression --ownership-only [COMPILER]  # Check an existing build's fixture ownership; builds nothing.
//
// It removes every tree under build/cmake (clean --all) and checks that
//   - guarded test roots retain their private entry points when preprocessed without main.c's unity-unit selector;
//   - main.c's complete driver (-DBUSTER_COMPLETE_DRIVER=1), the self-tests' input, retains the other units' code;
//   - the target policy data owner rejects either shared row-count bound when it drifts from the canonical rows;
//   - active fixtures, headers and arbitrary include descendants have one unity owner, except justified exact sharing;
//   - shared fixture declaration/include boundaries cannot grow silently inside existing shared roots;
//   - each baseline CRT startup object relinks and post-links every affected executable, with no work when unchanged;
//   - filtered compile commands are configuration-specific, reused until an input changes and rebuilt if missing;
//   - driver helpers preserve version thresholds, description failures and publication without redundant copies;
//   - BUSTER_TOOLCHAIN refuses unknown values, takes PATH's release clang as it is and otherwise a staged toolchain only
//     once its marker is written, and never downloads in the check;
//   - the parallel native generation's splitter links llvm-config's system libraries at paths the host lacks by name;
//   - fresh build.sh, direct CMake and preset trees ignore hostile CFLAGS/LDFLAGS before probing C, including cached
//     global/configuration flags, in generated compile/link commands and real unit compiles;
//   - a clean --all during a build waits for it, and then removes every tree and the published files;
//   - a fresh ./build.sh --config Release builds and publishes the Release executable and its configuration's compile
//     commands, and leaves no temporary file behind;
//   - an unchanged build configures, compiles, links, bakes and publishes nothing;
//   - an edited unit, an included .c and a header rebuild their dependents alone (Ninja's plan against its depfiles),
//     and an edited unit is compiled, linked, baked and published alone;
//   - Debug and Release share the tree, and switching back builds nothing and publishes that configuration's file;
//   - the Develop configuration builds its units in parts beside them without changing Release's executable, leaves the
//     benchmark units out with stand-ins that name Release, rebuilds an appended function's one part, writes the
//     objects Release's executable writes, is what ./build.sh builds without --config, and after a series of edits is
//     the same file a clean build makes;
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
#include <dirent.h>
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
    // Canonical repository-relative identity, including outside descendants with colliding basenames.
    const char* path;
    uint64_t owners;
    bool fixture;
    bool scanned;
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
    // Exact space-separated main-unit basenames, never an automatic all-units or descendant allowance.
    const char* owners;
    const char* reason;
    uint64_t structure;
};

typedef struct OwnershipBoundary OwnershipBoundary;
struct OwnershipBoundary
{
    const char* path;
    const char* owners;
    bool fixture;
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
static char develop[BUSTER_REGRESSION_PATH];
static char ninja_log[PATH_MAX];
// The source a check edits, and its copy, which is put back however the program ends.
static const char* const edited_unit = "src/buster/compiler/tier1_model_measure.c";
static char edited_copy[PATH_MAX];
static bool edited;
// The sources check_develop edits, each put back from its copy the same way.
static const char* const develop_edited[] = {
    "src/buster/compiler/c_check_input.c",
    "src/buster/compiler/c_checker_driver_test.c",
};
#define BUSTER_REGRESSION_DEVELOP_EDITS (sizeof(develop_edited) / sizeof(develop_edited[0]))
static char develop_copies[BUSTER_REGRESSION_DEVELOP_EDITS][PATH_MAX];
static bool develop_saved[BUSTER_REGRESSION_DEVELOP_EDITS];

static void restore(void)
{
    if (edited)
    {
        char command[2 * PATH_MAX];
        snprintf(command, sizeof(command), "cp '%s' '%s'", edited_copy, edited_unit);
        edited = system(command) != 0;
    }
    for (uint32_t i = 0; i < BUSTER_REGRESSION_DEVELOP_EDITS; i += 1)
    {
        if (develop_saved[i])
        {
            // Room for the whole array of copies' names, which is what GCC's -Wformat-truncation bounds an element by.
            char command[sizeof(develop_copies) + PATH_MAX];
            snprintf(command, sizeof(command), "cp '%s' '%s'", develop_copies[i], develop_edited[i]);
            develop_saved[i] = system(command) != 0;
        }
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
        { "src/buster/compiler/debug_benchmark.c", "run_debug_benchmark(" },
        { "src/buster/compiler/c_builtin_test.c", "run_builtin_tests(" },
        { "src/buster/compiler/c_environment_test.c", "run_c_environment_tests(" },
        { "src/buster/compiler/c_types_test.c", "run_type_tests(" },
        { "src/buster/compiler/calling_convention_test.c", "run_calling_convention_tests(" },
        { "src/buster/compiler/lane_test.c", "run_lane_tests(" },
        { "src/buster/compiler/c_testsuite.c", "run_c_testsuite_tests(" },
        { "src/buster/compiler/branch_sizing_test.c", "run_branch_sizing_tests(" },
        { "src/buster/compiler/x86_64_encoder_test.c", "run_x86_encoder_tests(" },
        { "src/buster/compiler/x86_intrinsics_test.c", "run_x86_intrinsic_tests(" },
        { "src/buster/compiler/x86_semantics_test.c", "run_x86_semantics_tests(" },
        { "src/buster/compiler/bind_test.c", "run_bind_tests(" },
        { "src/buster/compiler/import_test.c", "run_import_tests(" },
        { "src/buster/compiler/link_test.c", "run_link_tests(" },
        { "src/buster/compiler/link_order_test.c", "link_test_permute_names(" },
        { "src/buster/compiler/object_writer_test.c", "run_object_writer_tests(" },
        { "src/buster/compiler/debug_test.c", "run_debug_tests(" },
        { "src/buster/compiler/debug_executable_test.c", "debug_test_native_write(" },
        { "src/buster/compiler/executable_test.c", "run_executable_tests(" },
        { "src/buster/compiler/dynamic_test.c", "run_dynamic_tests(" },
        { "src/buster/compiler/static_test.c", "run_static_tests(" },
        { "src/buster/compiler/snapshot_test.c", "run_snapshot_tests(" },
        { "src/buster/compiler/foreign_test.c", "run_foreign_tests(" },
        { "src/buster/compiler/tier0_test.c", "run_tier0_tests(" },
        { "src/buster/compiler/tier1_select_source_benchmark.c", "run_tier1_select_source_benchmark(" },
        { "src/buster/compiler/test_tool.c", "test_tool_test(" },
        { "src/buster/compiler/test_process.c", "test_process_test(" },
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

// The self-tests and benchmarks that take main.c as their input (tools/ci/correctness.tests and the 9700X workflow)
// pass -DBUSTER_COMPLETE_DRIVER=1, so that it reads the production code of the other units as well as the driver's,
// as a fuzz build's driver does. Check one definition from each block that the ordinary driver leaves to its owners,
// in both hosts' configurations, so that a new guard can't silently halve those inputs. Each is a static definition's
// text that no other source file has, so that a caller or a declaration elsewhere can't stand in for it.
static void check_complete_driver_preprocessing(void)
{
    static const struct
    {
        const char* source;
        const char* entry;
    } families[] = {
        { "c_preprocessor_impl.c", "static void pp_diagnostic_limit(" },
        { "c_preprocessor_history_impl.c", "static void* pp_history_initial(" },
        { "executable_impl.c", "static uint64_t executable_align(" },
        { "dominators.c", "static DominatorOptions dominators_options(" },
        { "tier1_allocate.c", "static uint32_t tier1_allocation_next_use(" },
        { "tier1_schedule.c", "static void tier1_schedule_adjust(" },
        { "c_check_lazy.c", "static uint32_t check_lazy_expansion_location(" },
        { "c_check_actions.c", "static void check_value_copy_floating(" },
        { "c_check_literals.c", "static uint32_t check_literal_digit(" },
        { "tier1_frame.c", "static uint32_t tier1_frame_row(" },
        { "tier0.c", "static uint8_t tier0_mask_register(" },
        { "c_check_program.c", "static void check_program_fail(" },
        { "c_check_prefix.c", "static void* check_prefix_column(" },
        { "baked.c", "static uint64_t bake_section_size(" },
        { "c_check_corpus.c", "static void check_corpus_input_diagnostic(" },
        { "c_check_source_prepare.c", "static void check_source_print_receipt(" },
        { "ir_lower.c", "static uint32_t ir_typed_vector_bytes(" },
        { "tier1_select.c", "static X86Instruction tier1_select_register_record(" },
        { "c_check_tier1.c", "static Tier1EmitCause check_tier1_fault_emission_cause(" },
    };
    static const char* const hosts[] = { "-march=x86-64", "-march=znver5" };
    for (uint32_t host = 0; host < sizeof(hosts) / sizeof(hosts[0]); host += 1)
    {
        char* argv[] = {
            "clang",
            "-E",
            "-P",
            "-target",
            "x86_64-pc-linux-gnu",
            (char*)hosts[host],
            "-Isrc",
            "-fwrapv",
            "-funsigned-char",
            "-DBUSTER_COMPLETE_DRIVER=1",
            "src/buster/compiler/main.c",
            0,
        };
        Text text = capture(argv);
        for (uint32_t i = 0; i < sizeof(families) / sizeof(families[0]); i += 1)
        {
            if (strstr(text.data, families[i].entry) == 0)
            {
                fail("main.c with -DBUSTER_COMPLETE_DRIVER=1 %s lost %s's %s", hosts[host], families[i].source, families[i].entry);
            }
        }
        free(text.data);
    }
    pass("main.c's complete driver retains a definition of each block the ordinary driver leaves out, on both hosts");
}

// Check copied source, never the working tree: increasing either reader bound would otherwise leave a valid but
// incorrect array or extended-row loop. Require the owner's count assertion, not just an unrelated compile error.
static void check_target_row_counts(void)
{
    Text forms = read_file("src/buster/compiler/x86_target_forms.c");
    char source[PATH_MAX];
    char copy[PATH_MAX];
    char log[PATH_MAX];
    snprintf(source, sizeof(source), "%s/target-counts.c", work);
    snprintf(copy, sizeof(copy), "%s/target-forms.c", work);
    snprintf(log, sizeof(log), "%s/target-counts.log", work);
    write_file(source,
               "#include <stdio.h>\n#include <assert.h>\n"
               "#include \"buster/compiler/x86_64_encoder.c\"\n"
               "#include \"target-forms.c\"\n", 0644);
    char* changed = malloc(forms.length + 32);
    if (changed == 0 || forms.length > INT_MAX)
    {
        fail("can't prepare target row-count fixtures");
    }
    static const struct
    {
        const char* declaration;
        const char* diagnostic;
    } cases[] = {
        { 0, 0 },
        { "BUSTER_X86_TARGET_CLASSIFIED_COUNT = ", "classified target row count must match its readers" },
        { "BUSTER_X86_TARGET_COMPACT_COUNT = ", "compact target row count must match its readers" },
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        memcpy(changed, forms.data, forms.length + 1);
        if (cases[i].declaration != 0)
        {
            const char* declaration = strstr(forms.data, cases[i].declaration);
            if (declaration == 0)
            {
                fail("can't find target count declaration: %s", cases[i].declaration);
            }
            const char* value = declaration + strlen(cases[i].declaration);
            char* end = 0;
            const unsigned long count = strtoul(value, &end, 10);
            if (end == value || count >= UINT16_MAX)
            {
                fail("invalid target row-count fixture: %s", cases[i].declaration);
            }
            snprintf(changed, forms.length + 32, "%.*s%lu%s", (int)(value - forms.data), forms.data, count + 1, end);
        }
        write_file(copy, changed, 0644);
        char* argv[] = { "clang", "-fsyntax-only", "-target", "x86_64-pc-linux-gnu", "-march=x86-64", "-Isrc", "-fwrapv", "-funsigned-char",
                         "-fno-strict-aliasing", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-psabi", source, 0 };
        const int status = run(log, 0, argv);
        Text diagnostics = read_file(log);
        if ((status == 0) != (cases[i].diagnostic == 0) || (cases[i].diagnostic != 0 && strstr(diagnostics.data, cases[i].diagnostic) == 0))
        {
            fputs(diagnostics.data, stderr);
            fail("target row-count fixture %u did not preserve its compile-time bound", i);
        }
        free(diagnostics.data);
    }
    free(changed);
    free(forms.data);
    pass("target row-count bounds compile unchanged and reject drift in either count");
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

#include "build_ownership_structure.h"
#include "build_ownership_includes.h"

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

// Ninja supplies a flat transitive set. Retain all repository paths so arbitrary fixture descendants can be
// classified after reading the graph. Validate the declared record size and self-source as well as VALID/mtime.
static OwnershipIssue ownership_read(char* text, const char* directory, const char* config, const OwnershipUnits* units, OwnershipGraph* graph)
{
    OwnershipIssue issue = { 0 };
    memset(graph, 0, sizeof(*graph));
    char prefix[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s/", directory);
    const size_t prefix_length = strlen(prefix);
    static const char compiler_prefix[] = "src/buster/compiler/";
    uint64_t owner = 0;
    uint32_t declared = 0;
    uint32_t dependencies = 0;
    bool self = false;
    const char* unit_name = "Ninja dependency capture";
    char* saved = 0;
    for (char* line = strtok_r(text, "\n", &saved); line != 0 && issue.reason == 0; line = strtok_r(0, "\n", &saved))
    {
        if (line[0] != ' ')
        {
            if (owner != 0 && (dependencies != declared || !self))
            {
                issue = (OwnershipIssue){ .reason = "incomplete unity dependency record", .path = unit_name };
            }
            owner = 0;
            char* marker = strstr(line, ": #deps ");
            if (marker != 0 && issue.reason == 0)
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
                        char* end = 0;
                        errno = 0;
                        const unsigned long count = strtoul(marker + 8, &end, 10);
                        if (unit == units->count || strcmp(line, expected) != 0 || strstr(marker + 8, " (VALID)") == 0 ||
                            end == marker + 8 || *end != ',' || errno != 0 || count == 0 || count > UINT32_MAX)
                        {
                            issue = (OwnershipIssue){ .reason = "invalid or stale unity dependency record", .path = name };
                        }
                        else
                        {
                            owner = UINT64_C(1) << unit;
                            declared = (uint32_t)count;
                            dependencies = 0;
                            self = false;
                            unit_name = units->names[unit];
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
            dependencies += 1;
            if (strncmp(line, prefix, prefix_length) == 0)
            {
                const char* repo_path = line + prefix_length;
                const bool compiler = strncmp(repo_path, compiler_prefix, sizeof(compiler_prefix) - 1) == 0;
                const char* path = compiler ? repo_path + sizeof(compiler_prefix) - 1 : repo_path;
                self = self || (compiler && strcmp(path, unit_name) == 0);
                uint32_t file = 0;
                while (file < graph->count && strcmp(graph->files[file].path, repo_path) != 0)
                {
                    file += 1;
                }
                if (file == BUSTER_REGRESSION_ENTRIES)
                {
                    issue = (OwnershipIssue){ .reason = "too many repository dependency paths", .path = path };
                }
                else
                {
                    if (file == graph->count)
                    {
                        graph->files[file].path = repo_path;
                        graph->files[file].fixture = ownership_fixture_name(repo_path);
                        graph->count += 1;
                    }
                    graph->files[file].owners |= owner;
                }
            }
        }
    }
    if (issue.reason == 0 && owner != 0 && (dependencies != declared || !self))
    {
        issue = (OwnershipIssue){ .reason = "incomplete unity dependency record", .path = unit_name };
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
        if (file->fixture && !shared && (file->owners & (file->owners - 1)) != 0)
        {
            issue = (OwnershipIssue){ .reason = "private fixture has multiple unity owners", .path = file->path, .actual = file->owners };
        }
    }
    for (uint32_t i = 0; i < count && issue.reason == 0; i += 1)
    {
        const OwnershipException* exception = &exceptions[i];
        uint64_t expected = 0;
        if (exception->owners == 0 || exception->owners[0] == 0 || exception->reason == 0 || exception->reason[0] == 0)
        {
            issue = (OwnershipIssue){ .reason = "shared exception requires exact owners and a reason", .path = exception->path };
        }
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
                if ((expected & (UINT64_C(1) << unit)) != 0)
                {
                    issue = (OwnershipIssue){ .reason = "duplicate unity owner in exception", .path = exception->path };
                }
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
        if (issue.reason == 0 && (file == graph->count || !graph->files[file].fixture))
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

// Production boundaries can have several consumers. Pin their names exactly, as for fixture sharing, while
// allowing an implementation to be private to one unit and keeping fixture classification a separate contract.
static OwnershipIssue ownership_boundaries(const OwnershipUnits* units, const OwnershipGraph* graph,
                                          const OwnershipBoundary* boundaries, uint32_t count)
{
    OwnershipIssue issue = { 0 };
    for (uint32_t i = 0; i < count && issue.reason == 0; i += 1)
    {
        const OwnershipBoundary* boundary = &boundaries[i];
        uint64_t expected = 0;
        const char* name = boundary->owners;
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
                issue = (OwnershipIssue){ .reason = "implementation boundary names an unknown owner", .path = boundary->path };
            }
            else if ((expected & (UINT64_C(1) << unit)) != 0)
            {
                issue = (OwnershipIssue){ .reason = "implementation boundary repeats an owner", .path = boundary->path };
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
        uint32_t file = 0;
        while (file < graph->count && strcmp(graph->files[file].path, boundary->path) != 0)
        {
            file += 1;
        }
        if (issue.reason == 0 && (file == graph->count || expected == 0 || graph->files[file].owners != expected ||
                                 (boundary->fixture && !graph->files[file].fixture)))
        {
            issue = (OwnershipIssue){ .reason = "named implementation's owner or classification changed", .path = boundary->path,
                                      .actual = file < graph->count ? graph->files[file].owners : 0, .expected = expected };
        }
        for (uint32_t j = 0; j < i && issue.reason == 0; j += 1)
        {
            if (strcmp(boundaries[j].path, boundary->path) == 0)
            {
                issue = (OwnershipIssue){ .reason = "duplicate implementation boundary", .path = boundary->path };
            }
        }
    }
    return issue;
}

static void check_ownership_boundaries(void)
{
    const OwnershipUnits units = { .names = { "main.c", "main_unit_backend.c", "main_unit_checker.c" }, .count = 3 };
    static OwnershipGraph graph;
    graph = (OwnershipGraph){ .count = 2 };
    graph.files[0] = (OwnershipFile){ .path = "production.c", .owners = UINT64_C(6) };
    graph.files[1] = (OwnershipFile){ .path = "private_test.c", .owners = UINT64_C(4), .fixture = true };
    const OwnershipBoundary boundaries[] = {
        { "production.c", "main_unit_backend.c main_unit_checker.c", false },
        { "private_test.c", "main_unit_checker.c", true },
    };
    if (ownership_boundaries(&units, &graph, boundaries, 2).reason != 0)
    {
        fail("implementation boundaries rejected their exact consumers");
    }
    const uint64_t wrong_masks[] = { 0, 2, 3, 5, 7 };
    for (uint32_t i = 0; i < sizeof(wrong_masks) / sizeof(wrong_masks[0]); i += 1)
    {
        graph.files[0].owners = wrong_masks[i];
        if (ownership_boundaries(&units, &graph, boundaries, 2).reason == 0)
        {
            fail("implementation boundaries accepted missing or widened consumers");
        }
    }
    graph.files[0].owners = UINT64_C(6);
    graph.files[0].fixture = true;
    if (ownership_boundaries(&units, &graph, boundaries, 2).reason != 0)
    {
        fail("implementation boundaries rejected production also reached through fixture includes");
    }
    graph.files[1].fixture = false;
    if (ownership_boundaries(&units, &graph, boundaries, 2).reason == 0)
    {
        fail("implementation boundaries accepted missing fixture classification");
    }
    graph.files[1].fixture = true;
    const OwnershipBoundary invalid[] = {
        { "production.c", 0, false },
        { "production.c", "", false },
        { "production.c", "main_unit_missing.c", false },
        { "production.c", "main_unit_backend.c main_unit_backend.c main_unit_checker.c", false },
        { "absent.c", "main_unit_checker.c", false },
    };
    for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i += 1)
    {
        if (ownership_boundaries(&units, &graph, &invalid[i], 1).reason == 0)
        {
            fail("implementation boundaries accepted invalid declaration %u", i);
        }
    }
    const OwnershipBoundary duplicate[] = { boundaries[0], boundaries[0] };
    if (ownership_boundaries(&units, &graph, duplicate, 2).reason == 0)
    {
        fail("implementation boundaries accepted duplicate paths");
    }
    pass("implementation boundaries reject wider/missing owners, unknown/duplicate names and missing fixture classification");
}

static bool ownership_fuzz_setting(const char* cache, bool* fuzz)
{
    static const char prefix[] = "BUSTER_FUZZ_AVAILABLE:";
    uint32_t found = 0;
    bool valid = true;
    for (const char* line = cache; *line != 0; )
    {
        const size_t length = strcspn(line, "\n");
        if (length >= sizeof(prefix) - 1 && memcmp(line, prefix, sizeof(prefix) - 1) == 0)
        {
            const char* value = memchr(line + sizeof(prefix) - 1, '=', length - (sizeof(prefix) - 1));
            found += 1;
            if (value == 0 || value == line + sizeof(prefix) - 1 || value + 2 < line + length ||
                (value + 1 != line + length && value[1] != '0' && value[1] != '1'))
            {
                valid = false;
            }
            else
            {
                // Direct CMake users may leave the cache value empty; BusterSettings.cmake normalizes it to 0.
                *fuzz = value + 1 != line + length && value[1] == '1';
            }
        }
        line += length + (line[length] == '\n');
    }
    return valid && found == 1;
}

static void check_ownership_configuration(void)
{
    const struct
    {
        const char* cache;
        bool valid;
        bool fuzz;
    } cases[] = {
        { "// comment\nBUSTER_FUZZ_AVAILABLE:STRING=0\n", true, false },
        { "BUSTER_FUZZ_AVAILABLE:STRING=1", true, true },
        { "BUSTER_FUZZ_AVAILABLE:UNINITIALIZED=1\nOTHER:STRING=0\n", true, true },
        { "BUSTER_FUZZ_AVAILABLE:STRING=\n", true, false },
        { "BUSTER_FUZZ_AVAILABLE:STRING=2\n", false, false },
        { "BUSTER_FUZZ_AVAILABLE:STRING=10\n", false, false },
        { "BUSTER_FUZZ_AVAILABLE:=1\n", false, false },
        { "BUSTER_FUZZ_AVAILABLE:STRING=0\nBUSTER_FUZZ_AVAILABLE:STRING=1\n", false, false },
        { "BUSTER_FUZZ_AVAILABLE:\nBUSTER_FUZZ_AVAILABLE:STRING=1\n", false, false },
        { "OTHER:STRING=1\n", false, false },
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        bool fuzz = false;
        const bool valid = ownership_fuzz_setting(cases[i].cache, &fuzz);
        if (valid != cases[i].valid || (valid && fuzz != cases[i].fuzz))
        {
            fail("ownership misread selected fuzz configuration %u", i);
        }
    }
    pass("ownership selects ordinary/fuzz boundaries only from one valid selected-tree cache setting");
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
#include "build_ownership_test.h"

static void check_ownership_fixtures(void)
{
    check_ownership_boundaries();
    check_ownership_configuration();
    check_ownership_structure_fixtures();
    check_ownership_include_fixtures();
    check_ownership_exception_fixtures();
    static const OwnershipUnits units = {
        .names = { "main.c", "main_unit_backend.c", "main_unit_frontend.c" },
        .count = 3,
    };
    static const OwnershipException shared[] = {
        { "src/buster/compiler/shared_test.c", "main_unit_backend.c main_unit_frontend.c", "fixture comparison helper", 0 },
        { "src/buster/compiler/shared_test.c", "main_unit_backend.c main_unit_frontend.c", "duplicate negative fixture", 0 },
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
        { "shared production", { "production.c", "production.c", "" }, false, "", 0 },
        { "new test header leak", { "fixture_test.h", "fixture_test.h", "" }, false, "", "private fixture has multiple unity owners" },
        { "new test include fragment leak", { "fixture_test.inc", "fixture_test.inc", "" }, false, "", "private fixture has multiple unity owners" },
        { "nonstandard fixture leak", { "schedule_fixture.c", "schedule_fixture.c", "" }, false, "", "private fixture has multiple unity owners" },
        { "changed shared owners", { "shared_test.c", "shared_test.c", "" }, true, "", "shared fixture's exact owner set changed" },
        { "widened shared owners", { "shared_test.c", "shared_test.c", "shared_test.c" }, true, "", "shared fixture's exact owner set changed" },
        { "obsolete shared exception", { "", "shared_test.c", "" }, true, "", "shared fixture's exact owner set changed" },
        { "unused shared exception", { "", "", "" }, true, "", "unused shared-fixture exception" },
        { "duplicate shared exception", { "", "shared_test.c", "shared_test.c" }, 2, "", "duplicate shared-fixture exception" },
        { "missing unity record", { "", "", 0 }, false, "", "missing unity dependency records" },
        { "empty dependency capture", { 0, 0, 0 }, false, "", "missing unity dependency records" },
        { "duplicate unity record", { "", "", "" }, false,
          "CMakeFiles/buster_main.dir/Release/src/buster/compiler/main.c.o: #deps 1, deps mtime 1 (VALID)\n", "duplicate unity dependency record" },
        { "truncated complete header set", { "", "", 0 }, false,
          "CMakeFiles/buster_main_unit_frontend.dir/Release/src/buster/compiler/main_unit_frontend.c.o: #deps 2, deps mtime 1 (VALID)\n"
          "    /fixture/src/buster/compiler/main_unit_frontend.c\n", "incomplete unity dependency record" },
        { "missing self-source", { "", "", 0 }, false,
          "CMakeFiles/buster_main_unit_frontend.dir/Release/src/buster/compiler/main_unit_frontend.c.o: #deps 1, deps mtime 1 (VALID)\n"
          "    /fixture/src/buster/compiler/new_test.c\n", "incomplete unity dependency record" },
        { "understated dependency count", { "", "", 0 }, false,
          "CMakeFiles/buster_main_unit_frontend.dir/Release/src/buster/compiler/main_unit_frontend.c.o: #deps 1, deps mtime 1 (VALID)\n"
          "    /fixture/src/buster/compiler/main_unit_frontend.c\n    /fixture/src/buster/compiler/new_test.c\n", "incomplete unity dependency record" },
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
                uint32_t dependencies = 1;
                for (const char* name = cases[test].files[unit]; *name != 0;)
                {
                    const size_t bytes = strcspn(name, " ");
                    dependencies += 1;
                    name += bytes + (name[bytes] == ' ');
                }
                ownership_fixture_append(text, sizeof(text), &length,
                                         "CMakeFiles/buster_%.*s.dir/Release/src/buster/compiler/%s.o: #deps %u, deps mtime 1 (VALID)\n"
                                         "    /fixture/src/buster/compiler/%s\n",
                                         (int)strlen(units.names[unit]) - 2, units.names[unit], units.names[unit], dependencies, units.names[unit]);
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
        { "src/buster/compiler/shared_test.c", "main_unit_backend.c", "obsolete exception negative fixture", 0 },
    };
    memset(&graph, 0, sizeof(graph));
    graph.files[0] = (OwnershipFile){ .path = "src/buster/compiler/shared_test.c", .owners = UINT64_C(1) << 1, .fixture = true };
    graph.count = 1;
    const OwnershipIssue unnecessary_issue = ownership_check(&units, &graph, unnecessary, 1);
    if (unnecessary_issue.reason == 0 || strcmp(unnecessary_issue.reason, "shared exception must name multiple unity owners") != 0)
    {
        fail("ownership accepted a matching single-owner shared exception");
    }
    pass("ownership rejects new direct/nested leaks, changed/shared masks, obsolete exceptions and incomplete records");
}

// Only necessary sharing. Each path has an explicit owner set and a reason. The fingerprint records the current
// shared declaration/include boundary, not function bodies or private islands; see the contributor rule in build.md.
static const char ownership_main_units[] =
    "main.c main_unit_assembler.c main_unit_backend.c main_unit_check_builtins.c main_unit_checker.c "
    "main_unit_dynamic.c main_unit_executable.c main_unit_frontend.c main_unit_link.c main_unit_types.c main_unit_x86.c";

static const char ownership_checker_units[] = "main_unit_backend.c main_unit_check_builtins.c main_unit_checker.c";
static const char ownership_backend_units[] = "main_unit_backend.c main_unit_checker.c";

static const OwnershipException ownership_exceptions[] = {
    { "src/buster/compiler/c_checker_test.c", ownership_checker_units, "comparison helpers for checker, builtin and backend consumers", UINT64_C(0xd9302eb5f10668a6) },
    { "src/buster/compiler/c_parser_test.c", ownership_main_units, "parser construction and subprocess helpers used by shared parser/checker roots", UINT64_C(0xe85650d00dd1a0ba) },
    { "src/buster/compiler/c_preprocessor_test.c", ownership_main_units, "preprocessor input and result builders used by shared parser helpers", UINT64_C(0x530f34f7bb6a2975) },
    { "src/buster/compiler/c_preprocessor_history_test.c", "main_unit_frontend.c main_unit_checker.c", "history matcher for preprocessor and checker pragma tests", UINT64_C(0x338bcc2b5286ad74) },
    { "src/buster/compiler/foreign_test.c", "main_unit_executable.c main_unit_checker.c", "foreign executable and checker debug/unwind builders", UINT64_C(0x3932f8b6055a62cb) },
    { "src/buster/compiler/ir_source_test.c", "main_unit_backend.c main_unit_checker.c", "source preparation interface for IR commands; CHECKER owns its implementation", UINT64_C(0x8366575ba00f1461) },
    { "src/buster/compiler/tier1_select_test.c", "main_unit_backend.c main_unit_checker.c", "selection equality helper for backend and certificate checks", UINT64_C(0x71c71f12f96e31e5) },
    { "src/buster/compiler/tier1_select_burs_test.c", "main_unit_backend.c main_unit_checker.c", "BURS verification for backend and checker certificate benchmarks", UINT64_C(0x35a69924196eb09e) },
    { "src/buster/compiler/tier1_select_source_benchmark.c", "main_unit_backend.c main_unit_checker.c", "source selection runner reused by checker certificate benchmarks", UINT64_C(0xc64fc7b53c7bc1e0) },
    { "src/buster/compiler/bind_test.c", "main_unit_link.c main_unit_dynamic.c main_unit_executable.c", "bound-program builders for link, dynamic and executable fixtures", UINT64_C(0xf34bd6e22e26a890) },
    { "src/buster/compiler/branch_sizing_test.c", "main.c main_unit_x86.c", "process runner for driver semantics and x86 branch checks", UINT64_C(0x08ec91d539e1427a) },
    { "src/buster/compiler/c_builtin_test.c", "main_unit_types.c main_unit_check_builtins.c", "integer oracle for builtin actions and builtin type tests", UINT64_C(0x0a6a824fedab403e) },
    { "src/buster/compiler/c_environment_test.c", "main_unit_frontend.c main_unit_checker.c", "type-array comparison in frontend and checker tests", UINT64_C(0xcbebecd69c5dc0e6) },
    { "src/buster/compiler/c_types_test.c", "main_unit_types.c main_unit_frontend.c main_unit_backend.c main_unit_checker.c", "type/table builders and comparisons for parser, ABI and lowering fixtures", UINT64_C(0x2501d3624d2bce75) },
    { "src/buster/compiler/calling_convention_test.c", "main_unit_types.c main_unit_checker.c", "ABI builders for type tests and checker ABI lowering", UINT64_C(0x09c2b331f6f7727b) },
    { "src/buster/compiler/debug_benchmark.c", "main_unit_backend.c main_unit_link.c", "clock for backend unwind benchmark; benchmark body belongs to LINK", UINT64_C(0xd1c4a3865125e233) },
    { "src/buster/compiler/debug_executable_test.c", "main_unit_link.c main_unit_backend.c", "native debug executable writer for LINK and unwind fixtures", UINT64_C(0x44bb95092581d19c) },
    { "src/buster/compiler/debug_test.c", "main_unit_link.c main_unit_backend.c main_unit_checker.c", "debug object reader/comparison for LINK, unwind and checker fixtures", UINT64_C(0x89b342760d579483) },
    { "src/buster/compiler/dynamic_test.c", "main_unit_dynamic.c main_unit_executable.c main_unit_checker.c", "dynamic writer inputs for dynamic, executable and checker debug tests", UINT64_C(0x14370d8f51382099) },
    { "src/buster/compiler/executable_test.c", "main_unit_link.c main_unit_executable.c main_unit_dynamic.c main_unit_assembler.c main_unit_backend.c main_unit_checker.c", "native assembly, execution and value-check helpers for these six families", UINT64_C(0xf8a9668ff8e72f93) },
    { "src/buster/compiler/import_test.c", "main_unit_link.c main_unit_dynamic.c main_unit_executable.c", "object/import builders used by link and ELF writer fixtures", UINT64_C(0xb039b322d82e442c) },
    { "src/buster/compiler/lane_test.c", "main.c main_unit_checker.c main_unit_executable.c", "CPU topology for driver, checker lanes and foreign process tests", UINT64_C(0xd524a795555803d8) },
    { "src/buster/compiler/link_order_test.c", "main_unit_link.c main_unit_dynamic.c main_unit_executable.c", "link permutation helper used by link and writer fixtures", UINT64_C(0xe1808f7c37e1295e) },
    { "src/buster/compiler/link_test.c", "main_unit_link.c main_unit_dynamic.c main_unit_executable.c", "linked-program inputs for link and both ELF writers", UINT64_C(0xfe3eb21c41538ca2) },
    { "src/buster/compiler/object_writer_test.c", ownership_main_units, "object metadata types and helpers used by shared symbol/source readers", UINT64_C(0x1753ed9d953b02d3) },
    { "src/buster/compiler/snapshot_test.c", "main_unit_executable.c main_unit_checker.c", "executable patch checks shared with checker debug tests", UINT64_C(0xf83ab1966416eae8) },
    { "src/buster/compiler/static_test.c", "main_unit_executable.c main_unit_dynamic.c", "static executable writer shared by executable and dynamic tests", UINT64_C(0xe0e54572562aaf20) },
    { "src/buster/compiler/tier0_benchmark_measure.c", "main_unit_backend.c main_unit_checker.c", "counter utilities for backend IR and checker source benchmarks", UINT64_C(0x16670544301d2354) },
    { "src/buster/compiler/x86_64_encoder_test.c", "main.c main_unit_x86.c main_unit_frontend.c main_unit_assembler.c main_unit_link.c main_unit_backend.c", "encoding/execute helpers for these six test families", UINT64_C(0xb84cf97ab4156949) },
    { "src/buster/compiler/x86_intrinsics_test.c", "main_unit_checker.c main_unit_x86.c", "object reading and instruction decoding for checker AMX and intrinsic tests", UINT64_C(0x83a1dc58c44b290a) },
    { "src/buster/compiler/x86_semantics_test.c", ownership_main_units, "form names for DRIVER/BACKEND and six forced-emission globals retained in all units", UINT64_C(0x929ee4e6ddcf62ef) },
    { "src/buster/compiler/test_paths.h", ownership_main_units, "temporary-directory normalization for production/profile and test subprocesses", UINT64_C(0x433189fdd139cffd) },
    { "src/buster/compiler/test_tool.c", "main.c main_unit_x86.c main_unit_frontend.c main_unit_link.c main_unit_types.c main_unit_backend.c", "external tool probes for these six test families", UINT64_C(0xfc1a967b31b9b4af) },
    { "src/buster/compiler/test_tool.h", "main.c main_unit_x86.c main_unit_frontend.c main_unit_link.c main_unit_types.c main_unit_backend.c", "tool probe interface for its six implementation consumers", UINT64_C(0x398696e707640aa9) },
    { "src/buster/compiler/test_process.c", ownership_main_units, "CPU grant and capture-file helpers used by shared parser/process interfaces", UINT64_C(0x72b4864f47979300) },
    { "src/buster/compiler/test_libm.c", ownership_main_units, "host symbol resolution used by the production interpreter and builtin math oracles", UINT64_C(0x04389e52d6ae40f9) },
    { "src/buster/compiler/test_deal.h", ownership_main_units, "deterministic work assignment, by weight and by simulated shard schedule with shard 1's once-only time (#2031), used by shared parallel/preprocessor interfaces and tools/ci/costs.c", UINT64_C(0x32d159cdbc93126b) },
    { "src/buster/compiler/c_preprocessor_oracle.c", ownership_main_units, "oracle helpers retained by the shared preprocessor test interface; private suite is FRONTEND-only", UINT64_C(0xb36fa8797eaedce3) },
    { "src/buster/compiler/c_testsuite.c", "main.c main_unit_backend.c", "testsuite input helper reused by the driver IR lowering template command", UINT64_C(0x90b77b7db7d00a6d) },
    { "src/buster/compiler/c_check_tier1_rows_fixture.h", "main.c main_unit_checker.c", "generated instruction rows for driver target and checker fixtures", UINT64_C(0x458416eee6803d0d) },
    { "src/buster/compiler/calling_convention_test_limits.h", "main_unit_types.c main_unit_checker.c main_unit_backend.c", "three ABI bounds shared by types, checker and backend fixtures", UINT64_C(0x7631954cdbb8a5f4) },
    // Production implementations/data reached through fixture includes remain exact dependencies, not inherited
    // exceptions for their descendants. Their own ordinary declarations are outside the shared-fixture invariant.
    { "src/buster/compiler/host_isa.h", ownership_main_units, "production host ISA selection", 0 },
    { "src/buster/compiler/host_math.h", ownership_checker_units, "production host math interface", 0 },
    { "src/buster/compiler/tier1_certificate.c", ownership_backend_units, "production Tier 1 certificate verification", 0 },
    { "src/buster/compiler/c_builtin_fortify.c", ownership_main_units, "production fortified builtin model", 0 },
    { "src/buster/compiler/c_builtin_library.c", ownership_main_units, "production library builtin model", 0 },
    { "src/buster/compiler/c_builtin_library_calls.inc", ownership_main_units, "production generated library-call declarations", 0 },
    { "src/buster/compiler/c_builtin_library_plain.inc", ownership_main_units, "production library builtin names", 0 },
    { "src/buster/compiler/generated/clang_builtin_own_main_23_1_1.inc", ownership_main_units, "production generated builtin declarations with recorded generator provenance", 0 },
    { "src/buster/compiler/c_builtin_supplementary_scope.inc", ownership_main_units, "production supplementary builtin scope", 0 },
    { "src/buster/compiler/c_builtin_intrinsic_catalog.c", ownership_main_units, "production intrinsic catalog", 0 },
    { "src/buster/compiler/generated/clang_builtin_metadata_23_1_1.inc", ownership_main_units, "production generated intrinsic metadata with recorded generator provenance", 0 },
    { "src/buster/compiler/c_builtin_intrinsic_filter.c", ownership_main_units, "production intrinsic spelling filter", 0 },
    { "src/buster/compiler/c_builtin_intrinsic_filter_hash.h", ownership_main_units, "production intrinsic filter hash", 0 },
    { "src/buster/compiler/generated/clang_builtin_intrinsic_filter_23_1_1.inc", ownership_main_units, "production generated filter data with recorded generator provenance", 0 },
    { "src/buster/compiler/c_builtin_cpu.inc", ownership_checker_units, "production CPU builtin declarations", 0 },
    { "src/buster/compiler/ir_dataflow.c", ownership_backend_units, "production IR dataflow implementation", 0 },
    { "src/buster/compiler/ir_if_convert.c", ownership_backend_units, "production if-conversion pass; CHECKER retains its original include position", 0 },
    { "src/buster/compiler/ir_thread.c", ownership_backend_units, "production truth-value threading pass; CHECKER includes it through the Tier 1 driver", 0 },
    { "src/buster/compiler/ir_truth.c", ownership_backend_units, "production truth-value simplification; the threading fixtures reach it before the Tier 1 driver", 0 },
    { "src/buster/compiler/ir_optimize_verified.c", ownership_backend_units, "production verified optimizer pass; CHECKER retains its original include position", 0 },
};

// Fuzz callbacks run in DRIVER and may use the compiler's source and backend internals. This is an explicit
// configuration boundary, read from the selected tree's cache; a widened ordinary dependency never enables it.
static const OwnershipException ownership_fuzz_exceptions[] = {
    { "src/buster/compiler/c_checker_test.c", "main.c main_unit_backend.c main_unit_check_builtins.c main_unit_checker.c", "comparison helpers for checker, builtin, backend and fuzz consumers", UINT64_C(0x96b1c240bd73803a) },
    { "src/buster/compiler/host_math.h", "main.c main_unit_backend.c main_unit_check_builtins.c main_unit_checker.c", "production host math interface with fuzz source consumers", 0 },
    { "src/buster/compiler/tier1_certificate.c", "main.c main_unit_backend.c main_unit_checker.c", "production Tier 1 certificate verification with fuzz consumers", 0 },
    { "src/buster/compiler/c_builtin_cpu.inc", "main.c main_unit_backend.c main_unit_check_builtins.c main_unit_checker.c", "production CPU builtin declarations with fuzz source consumers", 0 },
    { "src/buster/compiler/ir_dataflow.c", "main.c main_unit_backend.c main_unit_checker.c", "production IR dataflow implementation with fuzz consumers", 0 },
    { "src/buster/compiler/ir_if_convert.c", "main.c main_unit_backend.c main_unit_checker.c", "production if-conversion pass with fuzz consumers", 0 },
    { "src/buster/compiler/ir_thread.c", "main.c main_unit_backend.c main_unit_checker.c", "production truth-value threading pass with fuzz consumers", 0 },
    { "src/buster/compiler/ir_truth.c", "main.c main_unit_backend.c main_unit_checker.c", "production truth-value simplification with fuzz consumers", 0 },
    { "src/buster/compiler/ir_optimize_verified.c", "main.c main_unit_backend.c main_unit_checker.c", "production verified optimizer pass with fuzz consumers", 0 },
};

static const OwnershipException ownership_scalar_exceptions[] = {
    { "src/buster/compiler/host_scalar.h", ownership_main_units, "production scalar host intrinsics, selected only without the vector host ISA", 0 },
    { "src/buster/compiler/host_scalar_vectors.h", ownership_main_units, "production scalar vector types, selected only without the vector host ISA", 0 },
};

static uint32_t ownership_active_exceptions(const OwnershipGraph* graph, bool fuzz, OwnershipException* active)
{
    memcpy(active, ownership_exceptions, sizeof(ownership_exceptions));
    uint32_t count = sizeof(ownership_exceptions) / sizeof(ownership_exceptions[0]);
    for (uint32_t i = 0; fuzz && i < sizeof(ownership_fuzz_exceptions) / sizeof(ownership_fuzz_exceptions[0]); i += 1)
    {
        uint32_t found = 0;
        for (uint32_t j = 0; j < count; j += 1)
        {
            if (strcmp(ownership_fuzz_exceptions[i].path, active[j].path) == 0)
            {
                active[j] = ownership_fuzz_exceptions[i];
                found += 1;
            }
        }
        if (found != 1)
        {
            fail("fuzz ownership requires one ordinary exception for %s", ownership_fuzz_exceptions[i].path);
        }
    }
    bool scalar = false;
    bool vectors = false;
    for (uint32_t i = 0; i < graph->count; i += 1)
    {
        scalar = scalar || strcmp(graph->files[i].path, "src/buster/compiler/host_scalar.h") == 0;
        vectors = vectors || strcmp(graph->files[i].path, "src/buster/compiler/host_scalar_vectors.h") == 0;
    }
    if (scalar != vectors)
    {
        fail("incomplete scalar host dependency pair");
    }
    if (scalar)
    {
        memcpy(active + count, ownership_scalar_exceptions, sizeof(ownership_scalar_exceptions));
        count += sizeof(ownership_scalar_exceptions) / sizeof(ownership_scalar_exceptions[0]);
    }
    return count;
}

static void ownership_classify(OwnershipGraph* graph)
{
    bool remaining = true;
    while (remaining)
    {
        remaining = false;
        for (uint32_t i = 0; i < graph->count; i += 1)
        {
            OwnershipFile* file = &graph->files[i];
            if (file->fixture && !file->scanned)
            {
                file->scanned = true;
                Text text = read_file(file->path);
                if (!ownership_mark_includes(graph, file, text.data))
                {
                    fail("can't read literal fixture includes in %s", file->path);
                }
                free(text.data);
                remaining = true;
            }
        }
    }
}

static void ownership_check_structure(const OwnershipUnits* units, const OwnershipGraph* graph,
                                      const OwnershipException* exceptions, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const OwnershipException* exception = &exceptions[i];
        if (ownership_fixture_name(exception->path))
        {
            uint32_t file = 0;
            while (file < graph->count && strcmp(graph->files[file].path, exception->path) != 0)
            {
                file += 1;
            }
            if (file == graph->count)
            {
                fail("can't fingerprint absent shared root %s", exception->path);
            }
            Text text = read_file(graph->files[file].path);
            const OwnershipStructure structure = ownership_structure(units, graph->files[file].owners, text.data);
            free(text.data);
            if (!structure.valid || structure.hash != exception->structure)
            {
                fail("%s: shared declaration/include boundary changed (valid %u, %u shared entries, fingerprint %016llx, expected %016llx). "
                     "Guard private work for its owner; update a fingerprint only after reviewing justified shared changes",
                     exception->path, structure.valid, structure.declarations, (unsigned long long)structure.hash, (unsigned long long)exception->structure);
            }
        }
    }
}

// Pin these paths' owning unit, not merely their owner count. Production implementations with a separate
// interface are checked here too, so every correctness build rejects a newly widened dependency even when the
// build-only workflow is not selected. The fixture entries must also retain their include-closure classification.
static void ownership_check_nonstandard(const OwnershipUnits* units, const OwnershipGraph* graph, bool fuzz)
{
    const char* source = fuzz ? "main.c main_unit_checker.c" : "main_unit_checker.c";
    const char* backend = fuzz ? "main.c main_unit_backend.c main_unit_checker.c" : ownership_backend_units;
    const char* checker = fuzz ? "main.c main_unit_backend.c main_unit_check_builtins.c main_unit_checker.c" : ownership_checker_units;
    const char* preprocessor = fuzz ? "main.c main_unit_frontend.c" : "main_unit_frontend.c";
    const char* executable = fuzz ? "main.c main_unit_dynamic.c main_unit_executable.c" : "main_unit_dynamic.c main_unit_executable.c";
    const OwnershipBoundary files[] = {
        { "src/buster/compiler/tier0_vector_baseline_test.inc", "main_unit_backend.c", true },
        { "src/buster/compiler/tier1_allocate_test_cases.c", "main_unit_backend.c", true },
        { "src/buster/compiler/tier1_schedule_fixture.c", "main_unit_backend.c", true },
        { "src/buster/compiler/c_check_lower_inventory.c", "main_unit_checker.c", true },
        { "src/buster/compiler/c_check_node_census.c", "main_unit_checker.c", true },
        { "src/buster/compiler/c_driver_compatibility_test.h", "main_unit_checker.c", true },
        { "src/buster/compiler/differential.c", "main_unit_backend.c", true },
        { "src/buster/compiler/test_tool_test.c", "main.c", true },
        { "src/buster/compiler/test_process_test.c", "main_unit_frontend.c", true },
        { "src/buster/compiler/preprocessor_fuzz.c", "main_unit_frontend.c", true },
        { "src/buster/compiler/preprocessor_fuzz_generate.c", "main_unit_frontend.c", true },
        { "src/buster/compiler/preprocessor_fuzz_clang.c", "main_unit_frontend.c", true },
        { "src/buster/compiler/c_parser_impl.c", "main_unit_frontend.c", false },
        { "src/buster/compiler/x86_target_rows.c", "main_unit_frontend.c", false },
        { "src/buster/compiler/c_preprocessor_impl.c", preprocessor, false },
        { "src/buster/compiler/c_preprocessor_history_impl.c", preprocessor, false },
        { "src/buster/compiler/c_preprocessor_literal.c", preprocessor, false },
        { "src/buster/compiler/executable_impl.c", executable, false },
        { "src/buster/compiler/c_check_environment.c", source, false },
        { "src/buster/compiler/c_check_body_bindings.c", source, false },
        { "src/buster/compiler/c_check_lazy.c", source, false },
        { "src/buster/compiler/c_check_actions.c", checker, false },
        { "src/buster/compiler/c_checker.c", checker, false },
        { "src/buster/compiler/c_check_input.c", source, false },
        { "src/buster/compiler/c_check_deferred.c", source, false },
        { "src/buster/compiler/c_check_program.c", backend, false },
        { "src/buster/compiler/c_check_program_emit.c", backend, false },
        { "src/buster/compiler/c_check_corpus.c", backend, false },
        { "src/buster/compiler/c_check_program_lower.c", source, false },
        { "src/buster/compiler/c_check_request.c", source, false },
        { "src/buster/compiler/c_checker_driver.c", source, false },
        { "src/buster/compiler/c_checker_units.c", source, false },
        { "src/buster/compiler/c_check_tier1.c", source, false },
        { "src/buster/compiler/c_check_source_prepare.c", source, false },
        { "src/buster/compiler/baked.c", source, false },
        // #2524's node machine: its state and records' accessors, and one file a sweep, the checker unit's alone.
        { "src/buster/compiler/c_check_node_machine.c", "main_unit_checker.c", false },
        { "src/buster/compiler/c_check_node_machine_type.c", "main_unit_checker.c", false },
        { "src/buster/compiler/c_check_node_machine_contexts.c", "main_unit_checker.c", false },
        { "src/buster/compiler/c_check_node_machine_expand.c", "main_unit_checker.c", false },
        { "src/buster/compiler/c_check_clock.c", "main.c main_unit_checker.c", false },
        { "src/buster/compiler/c_check_clock_rss.c", "main.c main_unit_checker.c", false },
        { "src/buster/compiler/worker_budget.c", "main.c main_unit_checker.c", false },
        { "src/buster/compiler/c_check_constant_bytes.c", fuzz ? "main.c main_unit_check_builtins.c main_unit_checker.c" : "main_unit_check_builtins.c main_unit_checker.c", false },
        { "src/buster/compiler/c_check_os_log_input.c", fuzz ? "main.c main_unit_check_builtins.c main_unit_checker.c" : "main_unit_check_builtins.c main_unit_checker.c", false },
        { "src/buster/compiler/tier0.c", backend, false },
        { "src/buster/compiler/tier1_schedule.c", backend, false },
        { "src/buster/compiler/tier1_allocate.c", backend, false },
        { "src/buster/compiler/tier1_frame.c", backend, false },
        // DRIVER still owns the shared IR scan counter and baked BURS data, without their implementation bodies.
        { "src/buster/compiler/ir.c", "main.c main_unit_backend.c main_unit_checker.c", false },
        { "src/buster/compiler/tier1_select_burs.c", "main.c main_unit_backend.c main_unit_checker.c", false },
        { "src/buster/compiler/interpreter.c", backend, false },
    };
    ownership_report(units, ownership_boundaries(units, graph, files, sizeof(files) / sizeof(files[0])));
}

static void check_ownership(const char* directory, const char* config)
{
    check_ownership_fixtures();
    char cache_path[PATH_MAX];
    snprintf(cache_path, sizeof(cache_path), "%s/CMakeCache.txt", directory);
    Text cache = read_file(cache_path);
    bool fuzz = false;
    if (!ownership_fuzz_setting(cache.data, &fuzz))
    {
        fail("ownership requires one valid BUSTER_FUZZ_AVAILABLE setting in %s", cache_path);
    }
    free(cache.data);
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
    ownership_classify(&graph);
    OwnershipException active[sizeof(ownership_exceptions) / sizeof(ownership_exceptions[0]) +
                              sizeof(ownership_scalar_exceptions) / sizeof(ownership_scalar_exceptions[0])];
    const uint32_t exceptions = ownership_active_exceptions(&graph, fuzz, active);
    ownership_report(&units, ownership_check(&units, &graph, active, exceptions));
    ownership_check_structure(&units, &graph, active, exceptions);
    ownership_check_nonstandard(&units, &graph, fuzz);
    uint32_t fixtures = 0;
    for (uint32_t i = 0; i < graph.count; i += 1)
    {
        fixtures += graph.files[i].fixture;
    }
    pass("%s ownership: %u active fixture/support paths, %u single-owner and %u exact shared exceptions across %u unity units",
         config, fixtures, fixtures - exceptions, exceptions, units.count);
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
    if (!ownership_suffix(resolved, "/Release/compiler") && !ownership_suffix(resolved, "/Debug/compiler") &&
        !ownership_suffix(resolved, "/Develop/compiler"))
    {
        static const char* configs[] = { "Release", "Debug", "Develop" };
        bool found = false;
        for (uint32_t i = 0; i < 3 && !found; i += 1)
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
    BUILD_ENV(scratch, "build", "--config", "Release", "--verbose");
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
    pass("a fresh Release build publishes its executable and compile commands, and leaves no temporary file");

    static Snapshot before;
    snapshot(&before);
    const struct stat compiler = status_of("build/compiler");
    char cache_path[PATH_MAX + 16];
    snprintf(cache_path, sizeof(cache_path), "%s/CMakeCache.txt", tree);
    const struct stat cache = status_of(cache_path);
    const struct stat commands = status_of("build/compile_commands.json");
    BUILD("build", "--config", "Release");
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
    BUILD("build", "--config", "Release");
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
    BUILD("build", "--config", "Release");
    ran_since(&before, names, sizeof(names));
    if (names[0] != 0 || !compile_commands_of("Release") || status_of("build/compiler").st_ino != compiler.st_ino)
    {
        fail("a missing published database ran %s or wasn't restored", names);
    }
    pass("missing cached or published compile commands are restored without compiling or linking");

    static const char shared_main_units[] =
        "main.c main_unit_assembler.c main_unit_backend.c main_unit_check_builtins.c main_unit_checker.c "
        "main_unit_dynamic.c main_unit_executable.c main_unit_frontend.c main_unit_link.c main_unit_types.c main_unit_x86.c ";
    static const char backend_main_units[] = "main_unit_backend.c main_unit_checker.c ";
    static const char checker_main_units[] = "main_unit_backend.c main_unit_check_builtins.c main_unit_checker.c ";
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
        { "src/buster/compiler/c_checker_test.c", checker_main_units },
        { "src/buster/compiler/c_check_actions.c", checker_main_units },
        { "src/buster/compiler/c_check_input.c", "main_unit_checker.c " },
        { "src/buster/compiler/c_check_program_lower.c", "main_unit_checker.c " },
        { "src/buster/compiler/ir_simd_model_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/interpreter_wide_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/ir_optimize_simd_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/ir_source_test.c", "main_unit_backend.c main_unit_checker.c " },
        { "src/buster/compiler/ir_dataflow.c", backend_main_units },
        { "src/buster/compiler/ir_optimize_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/ir_if_convert_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/ir_thread_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/ir_optimize_text_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/ir_optimize_verified_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/tier1_call_probe_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/tier1_address_coalesce_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/ir_if_convert.c", backend_main_units },
        { "src/buster/compiler/ir_thread.c", backend_main_units },
        { "src/buster/compiler/ir_optimize_verified.c", backend_main_units },
        { "src/buster/compiler/tier1_select_burs_test.c", "main_unit_backend.c main_unit_checker.c " },
        { "src/buster/compiler/preprocessor_fuzz_generate.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_preprocessor_directives_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_predefines_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_gnu_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_parser_bindings_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/x86_target_policy_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/x86_target_rows.c", "main_unit_frontend.c tier1_model_measure.c " },
        { "src/buster/compiler/c_parser_impl.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_preprocessor_impl.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_preprocessor_history_impl.c", "main_unit_frontend.c " },
        { "src/buster/compiler/c_preprocessor_literal.c", "main_unit_frontend.c " },
        { "src/buster/compiler/executable_impl.c", "main_unit_dynamic.c main_unit_executable.c " },
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
        { "src/buster/compiler/tier0_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/tier0.c", backend_main_units },
        { "src/buster/compiler/diagnostic_test.c", "main.c " },
        { "src/buster/compiler/dataflow_test.c", "main.c " },
        { "src/buster/compiler/oracle_cache_test.c", "main.c " },
        { "src/buster/compiler/compile_measure.c", "main.c " },
        { "src/buster/compiler/differential.c", "main_unit_backend.c " },
        { "src/buster/compiler/test_tool_test.c", "main.c " },
        { "src/buster/compiler/test_process_test.c", "main_unit_frontend.c " },
        { "src/buster/compiler/tier1_allocate_test_cases.c", "main_unit_backend.c " },
        { "src/buster/compiler/tier1_schedule_fixture.c", "main_unit_backend.c " },
        { "src/buster/compiler/c_builtin_float_lower_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/unwind_tier0_test.c", "main_unit_backend.c " },
        { "src/buster/compiler/c_check_source_benchmark.c", "main_unit_checker.c " },
        { "src/buster/compiler/c_check_lower_inventory.c", "main_unit_checker.c " },
        { "src/buster/compiler/c_check_node_census.c", "main_unit_checker.c " },
        { "src/buster/compiler/mask_spill_measure.c", "main_unit_x86.c " },
        { "src/buster/compiler/x86_64_disassembler_test.c", "main_unit_x86.c " },
        { "src/buster/compiler/debug_benchmark.c", "main_unit_backend.c main_unit_link.c " },
        { "src/buster/compiler/tier0_benchmark_measure.c", "main_unit_backend.c main_unit_checker.c " },
        { "src/buster/compiler/x86_intrinsics_test.c", "main_unit_checker.c main_unit_x86.c " },
        { "src/buster/compiler/c_check_tier1_rows_fixture.h", "main.c main_unit_checker.c " },
        { "src/buster/compiler/calling_convention_test_limits.h", "main_unit_backend.c main_unit_checker.c main_unit_types.c " },
        { "src/buster/compiler/c_types_deferred_test.c", "main_unit_types.c " },
        { "src/buster/compiler/c_types_lookup_test.c", "main_unit_types.c " },
        { "src/buster/compiler/c_testsuite.c", "main.c main_unit_backend.c " },
        { "src/buster/compiler/c_lexer_test.c", "main.c " },
        { "src/buster/compiler/lane_test.c", "main.c main_unit_checker.c main_unit_executable.c " },
        { "src/buster/compiler/branch_sizing_test.c", "main.c main_unit_x86.c " },
        { "src/buster/compiler/runtime_library_search_test.c", "main_unit_link.c " },
        { "src/buster/compiler/executable_symbols_test.c", "main_unit_executable.c " },
        { "src/buster/compiler/object_debug_test.c", "main_unit_link.c " },
        { "src/buster/compiler/dynamic_libraries_test.c", "main_unit_dynamic.c " },
        { "src/buster/compiler/dynamic_bounds_test.c", "main_unit_dynamic.c " },
        { "src/buster/compiler/debug_executable_test.c", "main_unit_backend.c main_unit_link.c " },
        { "src/buster/compiler/import_test.c", "main_unit_dynamic.c main_unit_executable.c main_unit_link.c " },
        { "src/buster/compiler/x86_64_encoder_test.c", "main.c main_unit_assembler.c main_unit_backend.c main_unit_frontend.c main_unit_link.c main_unit_x86.c " },
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
        // The vector-only standalone Tier 0 benchmark shares these counter utilities too. Preserve its exact
        // dependency in the plan while checking the two unity consumers independently of host ISA.
        static const char counter_owners[] = "main_unit_backend.c main_unit_checker.c tier0_benchmark.c ";
        if (strcmp(source, "src/buster/compiler/tier0_benchmark_measure.c") == 0 && strcmp(expected, counter_owners) == 0)
        {
            checked_owners = "main_unit_backend.c main_unit_checker.c ";
        }
        static const char catalog_benchmark[] = "c_checker_benchmark.c ";
        if ((strcmp(source, "src/buster/compiler/c_builtin_intrinsic_catalog.c") == 0 ||
             strcmp(source, "src/buster/compiler/c_check_actions.c") == 0) &&
            strncmp(checked_owners, catalog_benchmark, sizeof(catalog_benchmark) - 1) == 0)
        {
            checked_owners += sizeof(catalog_benchmark) - 1;
        }
        // The vector scheduling benchmark includes the same fixture independently of main.c. Keep it in the
        // rebuild plan, and check its exact additional owner without broadening the unity ownership assertion.
        static const char schedule_owners[] = "main_unit_backend.c tier1_schedule_benchmark.c ";
        if (strcmp(source, "src/buster/compiler/tier1_schedule_fixture.c") == 0 && strcmp(expected, schedule_owners) == 0)
        {
            checked_owners = "main_unit_backend.c ";
        }
        // These production bodies are owned by FRONTEND in main.c. Their independent vector benchmarks keep
        // complete standalone inputs; the model measurement owns target rows on scalar hosts too.
        static const char target_row_owners[] = "main_unit_frontend.c tier1_model_measure.c tier1_select_benchmark.c ";
        if (strcmp(source, "src/buster/compiler/x86_target_rows.c") == 0 && strcmp(expected, target_row_owners) == 0)
        {
            checked_owners = "main_unit_frontend.c tier1_model_measure.c ";
        }
        static const char parser_owners[] = "c_parser_benchmark.c main_unit_frontend.c ";
        if (strcmp(source, "src/buster/compiler/c_parser_impl.c") == 0 && strcmp(expected, parser_owners) == 0)
        {
            checked_owners = "main_unit_frontend.c ";
        }
        static const char preprocessor_owners[] = "c_files_benchmark.c c_parser_benchmark.c c_preprocessor_benchmark.c main_unit_frontend.c ";
        if ((strcmp(source, "src/buster/compiler/c_preprocessor_impl.c") == 0 ||
             strcmp(source, "src/buster/compiler/c_preprocessor_history_impl.c") == 0 ||
             strcmp(source, "src/buster/compiler/c_preprocessor_literal.c") == 0) && strcmp(expected, preprocessor_owners) == 0)
        {
            checked_owners = "main_unit_frontend.c ";
        }
        static const char executable_owners[] = "executable_benchmark.c link_benchmark.c main_unit_dynamic.c main_unit_executable.c snapshot_benchmark.c ";
        if (strcmp(source, "src/buster/compiler/executable_impl.c") == 0 && strcmp(expected, executable_owners) == 0)
        {
            checked_owners = "main_unit_dynamic.c main_unit_executable.c ";
        }
        static const char tier0_owners[] = "main_unit_backend.c main_unit_checker.c tier0_benchmark.c tier1_select_benchmark.c ";
        if (strcmp(source, "src/buster/compiler/tier0.c") == 0 && strcmp(expected, tier0_owners) == 0)
        {
            checked_owners = backend_main_units;
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
    BUILD("build", "--config", "Release");
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
    BUILD("build", "--config", "Release");
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
    BUILD("build", "--config", "Release");
    pass("Debug and Release share the tree and switch without building");

    static const char* const bakes[][2] = { { "BUSTER_BAKE=0", 0 }, { "BUSTER_BAKE=1", 0 } };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        snapshot(&before);
        BUILD_ENV(bakes[i], "build", "--config", "Release");
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
        BUILD_ENV(linker, "build", "--config", "Release");
        ran_since(&before, names, sizeof(names));
        if (strcmp(compiler_outputs(names), "compiler compiler.link ") != 0)
        {
            fail("BUSTER_LINKER=bfd ran %s", names);
        }
        BUILD("build", "--config", "Release");
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

// The number of times `needle` occurs in the file `path`.
static uint32_t occurrences(const char* path, const char* needle)
{
    Text text = read_file(path);
    uint32_t count = 0;
    for (const char* cursor = text.data; (cursor = strstr(cursor, needle)) != 0; cursor += strlen(needle))
    {
        count += 1;
    }
    free(text.data);
    return count;
}

// Edits develop_edited's `index`: `text` appended, or put before its first line when `before`. The first edit saves the
// file (cp -p, its time with it).
static void develop_edit(uint32_t index, const char* text, bool before)
{
    if (!develop_saved[index])
    {
        snprintf(develop_copies[index], sizeof(develop_copies[index]), "%s/develop-edit-%u.c", work, index);
        char* save[] = { "cp", "-p", (char*)develop_edited[index], develop_copies[index], 0 };
        if (run(0, 0, save) != 0)
        {
            fail("can't save %s", develop_edited[index]);
        }
        develop_saved[index] = true;
    }
    Text source = read_file(develop_edited[index]);
    const size_t length = strlen(text);
    char* edited_text = malloc(source.length + length + 1);
    if (edited_text == 0)
    {
        fail("out of memory editing %s", develop_edited[index]);
    }
    const char* first = before ? text : source.data;
    const size_t first_length = before ? length : source.length;
    memcpy(edited_text, first, first_length);
    memcpy(edited_text + first_length, before ? source.data : text, before ? source.length : length);
    edited_text[source.length + length] = 0;
    write_file(develop_edited[index], edited_text, 0644);
    free(edited_text);
    free(source.data);
}

// The Develop configuration (docs/commands/build.md, "Develop"; #2559): Release's flags with its main.c units split
// before the optimizer (tools/codegen/presplit.c), line tables, the tests' functions optnone and the benchmark units
// left out. It needs the parts' tools, as BUSTER_PARALLEL_CODEGEN's auto finds them. It leaves the sources and
// Release's executable as they were, and Develop's outputs cleaned.
static void check_develop(void)
{
    char build_log[PATH_MAX];
    snprintf(build_log, sizeof(build_log), "%s/build.log", work);
    const struct stat release_status = status_of(release);
    BUILD("--config", "Develop");
    if (access(develop, X_OK) != 0 || !same_file("build/compiler", develop) || !compile_commands_of("Develop"))
    {
        fail("the Develop build didn't publish its executable and compile commands");
    }
    if (!same_time(status_of(release), release_status))
    {
        fail("the Develop build changed the Release executable");
    }
    char stamp[PATH_MAX];
    snprintf(stamp, sizeof(stamp), "%s/Develop/develop/main_unit_checker/split.stamp", tree);
    if (access(stamp, F_OK) != 0)
    {
        fail("Develop compiled main_unit_checker.c whole: its parts need parallel native generation's tools (the configure step names what is missing)");
    }
    char benchmark_log[PATH_MAX];
    snprintf(benchmark_log, sizeof(benchmark_log), "%s/develop-benchmark.log", work);
    char* benchmark[] = { develop, "benchmark-parser", 0 };
    const int benchmark_status = run(benchmark_log, 0, benchmark);
    // A scalar host's executable refuses every benchmark command before it dispatches one (main.c), so there the
    // stand-in is checked by its symbol instead of its message.
    const bool scalar = occurrences(benchmark_log, "benchmarks require an AVX-512 host build") == 1;
    const bool named = occurrences(benchmark_log, "Develop's leaves c_parser_benchmark.c out") == 1 && occurrences(benchmark_log, "build Release") == 1;
    if (benchmark_status != 1 || (scalar ? !has_symbol(develop, " T run_parser_benchmark") : !named))
    {
        fail("Develop's benchmark-parser didn't fail naming the Release configuration: %s", read_file(benchmark_log).data);
    }
    char* baked[] = { develop, "test-baked-tables", 0 };
    Text log = capture(baked);
    free(log.data);
    pass("Develop builds in parts beside Release, which it leaves as it was, and its benchmark commands name Release%s",
         scalar ? " (a scalar host refuses them first; the stand-in is linked)" : "");

    static Snapshot before;
    char names[8192];
    snapshot(&before);
    BUILD("build", "--config", "Release");
    ran_since(&before, names, sizeof(names));
    if (names[0] != 0 || !same_file("build/compiler", release) || !compile_commands_of("Release"))
    {
        fail("switching to Release ran %s or didn't publish it", names);
    }
    // The default configuration (#2559): a build without --config is Develop's.
    BUILD("build");
    ran_since(&before, names, sizeof(names));
    if (names[0] != 0 || !same_file("build/compiler", develop) || !compile_commands_of("Develop"))
    {
        fail("a build without --config ran %s or didn't publish Develop", names);
    }
    pass("Release and Develop share the tree and switch without building, and a build without --config is Develop's");

    // The executables differ in code, not in what they write: the same object from the same C, at both tiers.
    static const char* const tiers[] = { 0, "--tier1" };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        char objects[2][PATH_MAX];
        const char* compilers[2] = { release, develop };
        for (uint32_t c = 0; c < 2; c += 1)
        {
            snprintf(objects[c], sizeof(objects[c]), "%s/develop-object-%u-%u.o", work, i, c);
            char* compile[] = { (char*)compilers[c], "-c", "tools/ci/build_regression.c", "-o", objects[c], (char*)tiers[i], 0 };
            log = capture(compile);
            free(log.data);
        }
        if (!same_bytes(objects[0], objects[1]))
        {
            fail("Release's and Develop's executables wrote different objects of tools/ci/build_regression.c%s%s", tiers[i] != 0 ? " with " : "",
                 tiers[i] != 0 ? tiers[i] : "");
        }
    }
    pass("Release's and Develop's executables write the same objects");

    // The edits: a function appended to the checker's unit's c_check_input.c, then one before its first line, then one
    // appended to a test file of that unit, then a header's time alone. An appended function changes its part alone.
    static const char appended[] = "\n__attribute__((used)) static const char* buster_regression_develop_appended(void)\n"
                                   "{\n    return \"develop appended\";\n}\n";
    static const char prepended[] = "__attribute__((used)) static const char* buster_regression_develop_prepended(void)\n"
                                    "{\n    return \"develop prepended\";\n}\n";
    static const char tested[] = "\n__attribute__((used)) static const char* buster_regression_develop_test(void)\n"
                                 "{\n    return \"develop test\";\n}\n";
    develop_edit(0, appended, false);
    BUILD("--config", "Develop");
    const uint32_t append_parts = occurrences(build_log, "Optimizing and generating main_unit_checker's part");
    if (append_parts != 1 || occurrences(build_log, "Optimizing and generating") != 1)
    {
        fail("a function appended to c_check_input.c optimized %u of the checker's parts, and %u parts in all, not 1", append_parts,
             occurrences(build_log, "Optimizing and generating"));
    }
    develop_edit(0, prepended, true);
    BUILD("--config", "Develop");
    const uint32_t prepend_parts = occurrences(build_log, "Optimizing and generating main_unit_checker's part");
    if (prepend_parts == 0 || prepend_parts > 12 || occurrences(build_log, "Optimizing and generating") != prepend_parts)
    {
        fail("a function before c_check_input.c's first line optimized %u of the checker's parts, not 1 to 12", prepend_parts);
    }
    develop_edit(1, tested, false);
    BUILD("--config", "Develop");
    if (occurrences(build_log, "Optimizing and generating") != 1)
    {
        fail("a function appended to c_checker_driver_test.c optimized %u parts, not 1", occurrences(build_log, "Optimizing and generating"));
    }
    const char* const header = "src/buster/compiler/host_isa.h";
    const struct stat header_status = status_of(header);
    if (utimensat(AT_FDCWD, header, 0, 0) != 0)
    {
        fail("can't update host_isa.h's time");
    }
    BUILD("--config", "Develop");
    // Every unit that reads the header compiles again; the split ones' parts come out the same, so none is optimized
    // or joined again (the units Develop compiles whole relink the executable).
    if (occurrences(build_log, "Optimizing and generating") != 0 || occurrences(build_log, "Combining") != 0 ||
        occurrences(build_log, "Partitioning main_unit_checker's IR before the optimizer") != 1)
    {
        fail("a header's time alone optimized %u parts or joined a unit's again", occurrences(build_log, "Optimizing and generating"));
    }
    char incremental[PATH_MAX];
    snprintf(incremental, sizeof(incremental), "%s/develop-incremental", work);
    char* keep[] = { "cp", develop, incremental, 0 };
    if (run(0, 0, keep) != 0)
    {
        fail("can't keep Develop's incremental executable");
    }
    BUILD("clean", "--config", "Develop");
    BUILD("--config", "Develop");
    if (!same_bytes(develop, incremental))
    {
        fail("Develop's executable after the edits isn't the one a clean build of the same sources makes");
    }
    pass("an appended function optimizes its one part (one before the file's first line %u of 48), a header's time none, "
         "and the executable after the edits is a clean build's",
         prepend_parts);

    // The sources as they were, their times with them, so that Release's objects stay current; Develop's, built from
    // the edits, go.
    const struct timespec header_times[2] = { header_status.st_atim, header_status.st_mtim };
    if (utimensat(AT_FDCWD, header, header_times, 0) != 0)
    {
        fail("can't restore host_isa.h's time");
    }
    for (uint32_t i = 0; i < BUSTER_REGRESSION_DEVELOP_EDITS; i += 1)
    {
        char* put_back[] = { "cp", "-p", develop_copies[i], (char*)develop_edited[i], 0 };
        if (develop_saved[i] && run(0, 0, put_back) != 0)
        {
            fail("can't restore %s", develop_edited[i]);
        }
        develop_saved[i] = false;
    }
    BUILD("clean", "--config", "Develop");
    snapshot(&before);
    BUILD("build", "--config", "Release");
    ran_since(&before, names, sizeof(names));
    if (strstr(names, ".c.o") != 0 || !same_time(status_of(release), release_status))
    {
        fail("restoring the sources Develop's checks edited rebuilt Release's %s", names);
    }
}

// The last line `argv` prints, without its newline: a command's result after what it says on stderr.
static void capture_last_line(char* const* argv, char* line, size_t size)
{
    Text text = capture(argv);
    while (text.length > 0 && text.data[text.length - 1] == '\n')
    {
        text.length -= 1;
        text.data[text.length] = 0;
    }
    const char* last = strrchr(text.data, '\n');
    snprintf(line, size, "%s", last != 0 ? last + 1 : text.data);
    free(text.data);
}

// CI's toolchain, which build.sh builds with unless BUSTER_TOOLCHAIN=path (docs/commands/build.md, "The toolchain"),
// first on this program's PATH too, so that the trees it configures itself (the flags' fresh trees, the presets) use
// the clang build.sh's do: a preset configured with another would start build.sh's tree afresh.
static void align_toolchain(void)
{
    const char* setting = getenv("BUSTER_TOOLCHAIN");
    if (setting == 0 || setting[0] == 0 || strcmp(setting, "ci") == 0)
    {
        char* identity_argv[] = { "bash", "tools/ci/session-toolchain.sh", "identity", 0 };
        char* version_argv[] = { "sh", "-c", "clang --version 2> /dev/null | head -n 1", 0 };
        char identity[512];
        char version[512];
        capture_line(identity_argv, identity, sizeof(identity));
        capture_line(version_argv, version, sizeof(version));
        if (strcmp(identity, version) != 0)
        {
            char* stage_argv[] = { "bash", "tools/ci/session-toolchain.sh", "stage", 0 };
            char directories[4 * PATH_MAX];
            capture_last_line(stage_argv, directories, sizeof(directories));
            const char* path = getenv("PATH");
            const size_t length = strlen(directories) + strlen(path != 0 ? path : "") + 2;
            char* joined = malloc(length);
            if (joined == 0)
            {
                fail("out of memory joining PATH");
            }
            snprintf(joined, length, "%s:%s", directories, path != 0 ? path : "");
            if (setenv("PATH", joined, 1) != 0)
            {
                fail("can't put CI's toolchain first on PATH");
            }
            free(joined);
            capture_line(version_argv, version, sizeof(version));
            if (strcmp(identity, version) != 0)
            {
                fail("CI's staged toolchain's clang is %s, not %s", version, identity);
            }
        }
    }
}

// The toolchain setting (#2628): an unknown value is refused; a PATH whose clang is the release's is used as it is,
// staging nothing; and any other PATH's clang gives way to the hosted layout staged in the cache, its marker the
// staging's last step. A stand-in plays the release there: a clang that reports the release's identity and runs the
// real one, beside the real CMake and Ninja, in a cache of its own. Each run has a curl that fails, so that a wrong
// decision to stage fails at once rather than downloading the release.
static void check_toolchain(void)
{
    char log[PATH_MAX];
    snprintf(log, sizeof(log), "%s/toolchain.log", work);
    const char* const bogus[] = { "BUSTER_TOOLCHAIN=bogus", 0 };
    char* output[] = { "./build.sh", "print-output", 0 };
    if (run(log, bogus, output) != 1 || occurrences(log, "BUSTER_TOOLCHAIN must be ci or path") != 1)
    {
        fputs(read_file(log).data, stderr);
        fail("BUSTER_TOOLCHAIN=bogus wasn't refused");
    }

    char identity[512];
    char* identity_argv[] = { "bash", "tools/ci/session-toolchain.sh", "identity", 0 };
    capture_line(identity_argv, identity, sizeof(identity));
    char real_clang[PATH_MAX];
    char real_cmake[PATH_MAX];
    char real_ninja[PATH_MAX];
    char* which_clang[] = { "sh", "-c", "command -v clang", 0 };
    char* which_cmake[] = { "sh", "-c", "command -v cmake", 0 };
    char* which_ninja[] = { "sh", "-c", "command -v ninja", 0 };
    capture_line(which_clang, real_clang, sizeof(real_clang));
    capture_line(which_cmake, real_cmake, sizeof(real_cmake));
    capture_line(which_ninja, real_ninja, sizeof(real_ninja));

    // Two caches, each with nothing staged, and the stand-ins' directory: a clang that isn't the release's and a curl
    // that fails.
    char staged_cache[PATH_MAX];
    char empty_cache[PATH_MAX];
    char stand_ins[PATH_MAX];
    char used[PATH_MAX];
    snprintf(staged_cache, sizeof(staged_cache), "%s/toolchain-staged", work);
    snprintf(empty_cache, sizeof(empty_cache), "%s/toolchain-empty", work);
    snprintf(stand_ins, sizeof(stand_ins), "%s/toolchain-stand-ins", work);
    snprintf(used, sizeof(used), "%s/toolchain-used", work);
    if (mkdir(staged_cache, 0755) != 0 || mkdir(empty_cache, 0755) != 0 || mkdir(stand_ins, 0755) != 0)
    {
        fail("can't make the toolchain fixture's directories");
    }
    char path[PATH_MAX + 32];
    snprintf(path, sizeof(path), "%s/clang", stand_ins);
    write_file(path, "#!/bin/sh\necho 'clang version 0 (not the release)'\nexit 1\n", 0755);
    snprintf(path, sizeof(path), "%s/curl", stand_ins);
    write_file(path, "#!/bin/sh\necho 'curl: the toolchain check downloads nothing' >&2\nexit 6\n", 0755);

    // The staged cache's hosted layout: its root, its marker's line and its PATH directories (build tools, LLVM's, the
    // linkers').
    char staged_home[PATH_MAX + 32];
    snprintf(staged_home, sizeof(staged_home), "XDG_CACHE_HOME=%s", staged_cache);
    const char* const staged_environment[] = { staged_home, 0 };
    char* layout_argv[] = { "bash", "tools/ci/session-toolchain.sh", "layout", 0 };
    if (run(log, staged_environment, layout_argv) != 0)
    {
        fputs(read_file(log).data, stderr);
        fail("session-toolchain.sh layout failed");
    }
    Text layout = read_file(log);
    char* root = layout.data;
    char* key = strchr(root, '\n');
    char* directories = key != 0 ? strchr(key + 1, '\n') : 0;
    char* end = directories != 0 ? strchr(directories + 1, '\n') : 0;
    if (end == 0)
    {
        fail("session-toolchain.sh layout didn't print a root, a key and directories: %s", layout.data);
    }
    *key++ = 0;
    *directories++ = 0;
    *end = 0;
    char* tools = directories;
    char* llvm = tools != 0 ? strchr(tools, ':') : 0;
    char* linkers = llvm != 0 ? strchr(llvm + 1, ':') : 0;
    if (linkers == 0)
    {
        fail("session-toolchain.sh layout's directories aren't three: %s", directories);
    }
    *llvm++ = 0;
    *linkers++ = 0;
    char* make[] = { "mkdir", "-p", tools, llvm, linkers, 0 };
    if (run(0, 0, make) != 0)
    {
        fail("can't make the staged layout's directories");
    }
    const size_t script_size = 3 * PATH_MAX + 1024;
    char* script = malloc(script_size);
    if (script == 0)
    {
        fail("out of memory writing the stand-in clang");
    }
    snprintf(script, script_size,
             "#!/bin/sh\nif [ \"$1\" = --version ]; then\n    echo '%s'\n    exit 0\nfi\necho used >> '%s'\nexec '%s' \"$@\"\n", identity,
             used, real_clang);
    snprintf(path, sizeof(path), "%s/clang", llvm);
    write_file(path, script, 0755);
    free(script);
    char cmake_link[PATH_MAX + 32];
    char ninja_link[PATH_MAX + 32];
    snprintf(cmake_link, sizeof(cmake_link), "%s/cmake", tools);
    snprintf(ninja_link, sizeof(ninja_link), "%s/ninja", tools);
    if (symlink(real_cmake, cmake_link) != 0 || symlink(real_ninja, ninja_link) != 0)
    {
        fail("can't link the real CMake and Ninja into the staged layout");
    }

    char stand_in_path[2 * PATH_MAX + 4096];
    snprintf(stand_in_path, sizeof(stand_in_path), "PATH=%s:%s", stand_ins, getenv("PATH"));
    char* flags[] = { "./build.sh", "print-compile-flags", 0 };

    // The release first on PATH is used as it is, with nothing staged in the cache.
    char empty_home[PATH_MAX + 32];
    char release_path[3 * PATH_MAX + 4096];
    snprintf(empty_home, sizeof(empty_home), "XDG_CACHE_HOME=%s", empty_cache);
    snprintf(release_path, sizeof(release_path), "PATH=%s:%s:%s", llvm, stand_ins, getenv("PATH"));
    const char* const release_first[] = { empty_home, release_path, 0 };
    if (run(log, release_first, flags) != 0 || access(used, F_OK) != 0)
    {
        fputs(read_file(log).data, stderr);
        fail("build.sh didn't take PATH's release clang as it was");
    }
    char* staged_anything[] = { "find", empty_cache, "-mindepth", "1", "-print", "-quit", 0 };
    Text left = capture(staged_anything);
    if (left.length != 0)
    {
        fail("build.sh staged %s with the release first on PATH", left.data);
    }
    free(left.data);
    unlink(used);

    // Marked, the staged layout goes first on PATH: the stand-in's clang answers the description's queries.
    const char* const staged[] = { staged_home, stand_in_path, 0 };
    char marker[PATH_MAX + 32];
    char marker_line[64];
    snprintf(marker, sizeof(marker), "%s/.complete", root);
    snprintf(marker_line, sizeof(marker_line), "%s\n", key);
    write_file(marker, marker_line, 0644);
    if (run(log, staged, flags) != 0 || access(used, F_OK) != 0)
    {
        fputs(read_file(log).data, stderr);
        fail("build.sh didn't build with the marked staged toolchain");
    }
    unlink(used);

    // Unmarked, the layout is a partial staging, which build.sh stages afresh (taking it away first), and the failing
    // curl stops: it fails, naming BUSTER_TOOLCHAIN=path, and runs no clang.
    unlink(marker);
    if (run(log, staged, flags) == 0 || occurrences(log, "BUSTER_TOOLCHAIN=path builds with PATH's") != 1 || access(used, F_OK) == 0)
    {
        fputs(read_file(log).data, stderr);
        fail("build.sh used an unmarked staged toolchain, or didn't fail naming BUSTER_TOOLCHAIN=path");
    }
    free(layout.data);
    pass("BUSTER_TOOLCHAIN refuses unknown values; CI's toolchain is PATH's release as it is, or else the staged one once marked");
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
    if (build_with(fake, true, "build", "--config", "Release", (char*)0) == 0)
    {
        fail("a failed post-link step succeeded");
    }
    if (status_of("build/compiler").st_ino != inode || access(release, F_OK) == 0 ||
        !same_time(status_of("build/compile_commands.json"), commands))
    {
        fail("a failed post-link step published build/compiler or left %s", release);
    }
    BUILD("build", "--config", "Release");
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
    const int status = build_with(0, true, "build", "--config", "Release", (char*)0);
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
    char* argv[] = { "./build.sh", "--config", "Release", 0 };
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
    BUILD("build", "--config", "Release");
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
    BUILD("build", "--config", "Release");
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
    BUILD("build", "--config", "Release");
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

// Extract the real shell functions, as the locale check extracts now(), without executing build.sh's setup.
static void driver_function(FILE* file, const Text* script, const char* name)
{
    char marker[128];
    snprintf(marker, sizeof(marker), "\n%s()\n{\n", name);
    const char* begin = strstr(script->data, marker);
    const char* end = begin != 0 ? strstr(begin, "\n}\n") : 0;
    if (end == 0 || fwrite(begin + 1, 1, (size_t)(end + 3 - begin - 1), file) != (size_t)(end + 3 - begin - 1))
    {
        fail("can't extract build.sh's %s", name);
    }
}

// Real publication in disposable trees, with only cp/ln wrapped to count work and inject partial staging failures.
// Final mtimes alone would miss a no-op that copied and then deleted the unchanged database on every invocation.
static void check_driver(void)
{
    Text script = read_file("build.sh");
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/driver", work);
    if (mkdir(directory, 0755) != 0 || chdir(directory) != 0)
    {
        fail("can't make the driver fixture");
    }
    FILE* file = fopen("driver.sh", "w");
    if (file == 0 || fputs("#!/usr/bin/env bash\nset -euo pipefail\nshopt -s inherit_errexit\n", file) == EOF)
    {
        fail("can't create the driver fixture script");
    }
    driver_function(file, &script, "version_at_least");
    driver_function(file, &script, "describe");
    driver_function(file, &script, "publish");
    free(script.data);
    if (fputs(
        "root=$PWD\ntree=$root/tree\nconfig=Release\nbinary=$tree/$config/compiler\nsettings=()\nclang=clang\n"
        "cp()\n{\n"
        "    local source=\"${@: -2:1}\" output=\"${!#}\"\n"
        "    printf '%s\\n' \"$source\" >> \"$root/copies\"\n"
        "    if { [[ $failure == database* && $source == *.json ]]; } || { [[ $failure == compiler && $source == \"$binary\" ]]; }; then\n"
        "        [ \"$failure\" = database ] || printf partial > \"$output\"\n"
        "        return 23\n"
        "    fi\n"
        "    command cp \"$@\"\n}\n"
        "ln()\n{\n"
        "    [ \"$failure\" != fallback ] && [ \"$failure\" != compiler ] || return 23\n"
        "    command ln \"$@\"\n}\n"
        "case $1 in\n"
        "    version) version_at_least \"$2\" \"$3\" ;;\n"
        "    describe) cmake=$2; describe sanitizer-flags | tr '\\n' '\\0' ;;\n"
        "    publish) failure=$2; publish ;;\n"
        "esac\n", file) == EOF || fclose(file) != 0)
    {
        fail("can't finish the driver fixture script");
    }
    static const struct
    {
        const char* actual;
        const char* minimum;
        int status;
    } versions[] = {
        { "3.24.9", "3.25", 1 },
        { "3.25", "3.25", 0 },
        { "3.25.0-rc1", "3.25", 0 },
        { "3.100.1", "3.25", 0 },
        { "4.0.0", "3.25", 0 },
        { "2.99.0", "3.25", 1 },
        { "1.09.9", "1.10", 1 },
        { "1.10.0", "1.10", 0 },
        { "1.13.2\nadditional version output\n", "1.10", 0 },
        { "1.9.9\n1.13.2\n", "1.10", 1 },
        { "", "3.25", 1 },
        { "invalid 4.4.3", "3.25", 1 },
    };
    for (uint32_t i = 0; i < sizeof(versions) / sizeof(versions[0]); i += 1)
    {
        char* argv[] = { "bash", "driver.sh", "version", (char*)versions[i].actual, (char*)versions[i].minimum, 0 };
        if (run("version.log", 0, argv) != versions[i].status)
        {
            fail("version '%s' didn't honor minimum %s", versions[i].actual, versions[i].minimum);
        }
    }
    write_file("cmake",
        "#!/usr/bin/env bash\nset -eu\n"
        "[ \"${BUSTER_DRIVER_STATUS:-0}\" = 0 ] || exit \"$BUSTER_DRIVER_STATUS\"\n"
        "for argument in \"$@\"; do\n"
        "    case $argument in -DBUSTER_PRINT_OUTPUT=*) output=${argument#*=} ;; esac\n"
        "done\n"
        "printf '%s' \"${BUSTER_DRIVER_DESCRIPTION-}\" > \"$output\"\n", 0755);
    const char* const description[] = { "BUSTER_DRIVER_DESCRIPTION=first\nsecond\n", 0 };
    const char* const empty[] = { "BUSTER_DRIVER_DESCRIPTION=", 0 };
    const char* const failed[] = { "BUSTER_DRIVER_STATUS=23", 0 };
    char* describe[] = { "bash", "driver.sh", "describe", "./cmake", 0 };
    if (run("description.log", description, describe) != 0)
    {
        fail("the driver description failed");
    }
    Text output = read_file("description.log");
    if (output.length != sizeof("first\0second") || memcmp(output.data, "first\0second", sizeof("first\0second")) != 0)
    {
        fail("the driver description lost its argument boundaries");
    }
    free(output.data);
    if (run("description.log", empty, describe) != 0 || status_of("description.log").st_size != 0 ||
        run("description.log", failed, describe) != 23 || status_of("description.log").st_size != 0)
    {
        fail("an empty or failed driver description changed status or output");
    }
    static const struct
    {
        const char* name;
        const char* failure;
        bool same_compiler;
        bool same_database;
        bool missing_published;
        bool missing_cached;
        uint32_t copies;
        bool succeeds;
    } publications[] = {
        { "unchanged", "", true, true, false, false, 0, true },
        { "new compiler, same database", "", false, true, false, false, 0, true },
        { "changed database", "", false, false, false, false, 1, true },
        { "missing published database", "", false, false, true, false, 1, true },
        { "missing cached database", "", false, false, false, true, 1, false },
        { "failed database copy", "database", false, false, false, false, 1, false },
        { "partial database copy", "database-partial", false, false, false, false, 1, false },
        { "hardlink fallback", "fallback", false, false, false, false, 2, true },
        { "failed compiler staging", "compiler", false, false, false, false, 2, false },
    };
    for (uint32_t i = 0; i < sizeof(publications) / sizeof(publications[0]); i += 1)
    {
        char name[32];
        snprintf(name, sizeof(name), "publication-%u", i);
        if (mkdir(name, 0755) != 0 || chdir(name) != 0 || mkdir("build", 0755) != 0 || mkdir("tree", 0755) != 0 || mkdir("tree/Release", 0755) != 0)
        {
            fail("can't make a publication fixture");
        }
        const char* compiler = publications[i].same_compiler ? "old compiler\n" : "new compiler\n";
        const char* commands = publications[i].same_database ? "old database\n" : "new database\n";
        write_file("tree/Release/compiler", compiler, 0755);
        if (publications[i].same_compiler)
        {
            if (link("tree/Release/compiler", "build/compiler") != 0)
            {
                fail("can't link the fixture compiler");
            }
        }
        else
        {
            write_file("build/compiler", "old compiler\n", 0755);
        }
        if (!publications[i].missing_published)
        {
            write_file("build/compile_commands.json", "old database\n", 0644);
        }
        if (!publications[i].missing_cached)
        {
            write_file("tree/Release/compile_commands.json", commands, 0644);
        }
        write_file("expected.compiler", publications[i].succeeds ? compiler : "old compiler\n", 0644);
        write_file("expected.commands", publications[i].succeeds ? commands : "old database\n", 0644);
        write_file("copies", "", 0644);
        const struct stat before_compiler = status_of("build/compiler");
        const struct stat before_commands = publications[i].missing_published ? (struct stat){ 0 } : status_of("build/compile_commands.json");
        char* argv[] = { "bash", "../driver.sh", "publish", (char*)publications[i].failure, 0 };
        const int status = run("publication.log", 0, argv);
        Text copied = read_file("copies");
        uint32_t copies = 0;
        for (size_t byte = 0; byte < copied.length; byte += 1)
        {
            copies += copied.data[byte] == '\n';
        }
        free(copied.data);
        if ((status == 0) != publications[i].succeeds || copies != publications[i].copies ||
            !same_bytes("build/compiler", "expected.compiler") || !same_bytes("build/compile_commands.json", "expected.commands") ||
            ((!publications[i].succeeds || publications[i].same_compiler) && status_of("build/compiler").st_ino != before_compiler.st_ino) ||
            ((!publications[i].succeeds || publications[i].same_database) &&
             (status_of("build/compile_commands.json").st_ino != before_commands.st_ino || !same_time(status_of("build/compile_commands.json"), before_commands))))
        {
            fputs(read_file("publication.log").data, stderr);
            fail("publication '%s': status %d, copies %u (expected %u), or published state changed", publications[i].name, status, copies, publications[i].copies);
        }
        if (publications[i].succeeds)
        {
            write_file("tree/Release/compile_commands.json", "later cached database\n", 0644);
            if (!same_bytes("build/compile_commands.json", "expected.commands") || access("build/compiler", X_OK) != 0)
            {
                fail("publication '%s' aliased the cached database or lost executable permissions", publications[i].name);
            }
        }
        DIR* published = opendir("build");
        if (published == 0)
        {
            fail("can't inspect publication temporary files");
        }
        for (struct dirent* entry = readdir(published); entry != 0; entry = readdir(published))
        {
            if (strncmp(entry->d_name, ".compiler.", 10) == 0 || strncmp(entry->d_name, ".compile_commands.json.", 23) == 0)
            {
                fail("publication '%s' left temporary %s", publications[i].name, entry->d_name);
            }
        }
        closedir(published);
        if (chdir("..") != 0)
        {
            fail("can't leave the publication fixture");
        }
    }
    if (chdir(root) != 0)
    {
        fail("can't return to %s", root);
    }
    pass("driver versions and descriptions retain their contracts; publication skips unchanged copies and preserves both outputs on staging failure");
}

// BusterCodegen.cmake's mapping of llvm-config's system libraries: one named by a path the host lacks (the official
// release names Ubuntu's static zstd, which the 9700X's Arch host has as /usr/lib/libzstd.so) links by its name, and
// every other entry is kept as given, in its order. Hosted runners have Ubuntu's paths, so their builds never map one.
static void check_codegen_system_libraries(void)
{
    char present[PATH_MAX];
    char script[PATH_MAX];
    char text[6 * PATH_MAX];
    char expected[3 * PATH_MAX];
    snprintf(present, sizeof(present), "%s/libpresent.a", work);
    snprintf(script, sizeof(script), "%s/system-libraries.cmake", work);
    write_file(present, "!<arch>\n", 0644);
    // The host's library directories: one with a development link (libdevelopment.so) and its runtime file, one with
    // a runtime file alone (libruntime.so.2), its longer name's too.
    char directories[PATH_MAX];
    char file[PATH_MAX + 64];
    snprintf(directories, sizeof(directories), "%s/system-libraries", work);
    mkdir(directories, 0755);
    snprintf(file, sizeof(file), "%s/libdevelopment.so", directories);
    write_file(file, "", 0644);
    snprintf(file, sizeof(file), "%s/libdevelopment.so.1", directories);
    write_file(file, "", 0644);
    snprintf(file, sizeof(file), "%s/libruntime.so.2.5", directories);
    write_file(file, "", 0644);
    snprintf(file, sizeof(file), "%s/libruntime.so.2", directories);
    write_file(file, "", 0644);
    snprintf(text, sizeof(text),
        "set(BUSTER_CODEGEN_LIBRARY_DIRECTORIES \"%s/absent\" \"%s\")\n"
        "include(\"%s/cmake/BusterCodegen.cmake\")\n"
        "buster_codegen_system_libraries(libraries -lrt /buster-absent/x86_64-linux-gnu/libzstd.a \"%s\" /buster-absent/libxml2.so.2 "
        "/buster-absent/libdevelopment.a /buster-absent/libruntime.a -lm)\n"
        "message(\"libraries: ${libraries}\")\n",
        directories, directories, root, present);
    write_file(script, text, 0644);
    snprintf(expected, sizeof(expected), "libraries: -lrt;-lzstd;%s;-lxml2;-ldevelopment;%s/libruntime.so.2;-lm\n", present, directories);
    char* argv[] = { "cmake", "-P", script, 0 };
    Text log = capture(argv);
    if (strcmp(log.data, expected) != 0)
    {
        fail("BusterCodegen.cmake's system libraries: expected \"%s\", got \"%s\"", expected, log.data);
    }
    free(log.data);
    pass("llvm-config's system libraries at paths the host lacks link by name, or by their runtime file's path where the host has no development file, the others as given");
}

// Exercise the real CMake split/merge helpers on two independent translation units with colliding private names.
// Copies keep the source edits, malformed IR and export-manifest failures inside the disposable fixture tree.
static void check_parallel_codegen(void)
{
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/codegen", work);
    if (mkdir(directory, 0755) != 0 || chdir(directory) != 0 || mkdir("source", 0755) != 0)
    {
        fail("can't create the parallel-codegen fixture");
    }
    const char* files[] = { "CMakeLists.txt", "fixture_test.h", "checker_test.c", "backend_test.c", "main_test.c" };
    for (uint32_t i = 0; i < sizeof(files) / sizeof(files[0]); i += 1)
    {
        char input[PATH_MAX];
        char output[PATH_MAX];
        snprintf(input, sizeof(input), "%s/tools/ci/codegen_fixture/%s", root, files[i]);
        snprintf(output, sizeof(output), "source/%s", files[i]);
        Text source = read_file(input);
        write_file(output, source.data, 0644);
        free(source.data);
    }
    char module[PATH_MAX];
    snprintf(module, sizeof(module), "-DBUSTER_CODEGEN_MODULE_DIR=%s/cmake", root);
    char* configure[] = { "cmake", "-S", "source", "-B", "out", "-G", "Ninja Multi-Config",
        "-DCMAKE_C_COMPILER=clang", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", module, "-DBUSTER_FIXTURE_FAIL_SPLIT=OFF", 0 };
    char* build[] = { "cmake", "--build", "out", "--config", "Release", "--parallel", "1", 0 };
    char* execute[] = { "out/Release/codegen_fixture", 0 };
    Text log = capture(configure);
    free(log.data);
    log = capture(build);
    free(log.data);
    log = capture(execute);
    free(log.data);
    const char* alpha = "out/Release/codegen/main_unit_checker/native.o";
    const char* beta = "out/Release/codegen/main_unit_backend/native.o";
    const char* binary = "out/Release/codegen_fixture";
    const struct stat initial_alpha = status_of(alpha);
    const struct stat initial_beta = status_of(beta);
    const struct stat initial_binary = status_of(binary);
    const char* exports = "out/Release/codegen/main_unit_checker/exports.txt";
    const struct stat initial_exports = status_of(exports);
    // BusterCodegen.cmake's eight code parts and the data part (tools/codegen/split.c).
    enum { BUSTER_CODEGEN_FIXTURE_PARTITIONS = 9 };
    char partitions[BUSTER_CODEGEN_FIXTURE_PARTITIONS][PATH_MAX];
    struct stat initial_partitions[BUSTER_CODEGEN_FIXTURE_PARTITIONS];
    for (uint32_t part = 0; part < BUSTER_CODEGEN_FIXTURE_PARTITIONS; part += 1)
    {
        snprintf(partitions[part], sizeof(partitions[part]), "out/Release/codegen/main_unit_checker/part%u.bc", part);
        initial_partitions[part] = status_of(partitions[part]);
    }
    char* copy_alpha[] = { "cp", (char*)alpha, "expected-alpha.o", 0 };
    char* copy_beta[] = { "cp", (char*)beta, "expected-beta.o", 0 };
    if (run(0, 0, copy_alpha) != 0 || run(0, 0, copy_beta) != 0)
    {
        fail("can't preserve the native-code fixture outputs");
    }
    Text database = read_file("out/compile_commands.json");
    uint32_t commands = 0;
    for (const char* cursor = database.data; (cursor = strstr(cursor, "\"command\"")) != 0; cursor += 9)
    {
        commands += 1;
    }
    if (commands != 3 || strstr(database.data, ".bc") != 0 || !compile_commands_file_of("out/compile_commands.json", "Release"))
    {
        fail("generated IR jobs leaked into the source compilation database");
    }
    free(database.data);
    log = capture(build);
    if (strstr(log.data, "no work to do") == 0 || !same_time(status_of(alpha), initial_alpha) ||
        !same_time(status_of(beta), initial_beta) || !same_time(status_of(binary), initial_binary))
    {
        fail("an unchanged parallel-codegen graph rebuilt an output: %s", log.data);
    }
    free(log.data);
    Text original_checker = read_file("source/checker_test.c");
    // A real source-byte change with identical optimized IR, then a timestamp-only change, must both keep every
    // partition and export mtime. Their completed split stamps alone must not schedule native work or relink.
    for (uint32_t edit = 0; edit < 2; edit += 1)
    {
        if (edit == 0)
        {
            FILE* comment = fopen("source/checker_test.c", "a");
            if (comment == 0 || fputs("\n// A source-only edit reuses unchanged optimized partitions.\n", comment) == EOF || fclose(comment) != 0)
            {
                fail("can't append the checker fixture comment");
            }
        }
        else if (utimensat(AT_FDCWD, "source/checker_test.c", 0, 0) != 0)
        {
            fail("can't update the checker fixture timestamp");
        }
        log = capture(build);
        if (strstr(log.data, "checker_test.c.o") == 0 || strstr(log.data, "Partitioning main_unit_checker's") == 0 ||
            strstr(log.data, "native part") != 0 || strstr(log.data, "Combining") != 0 || strstr(log.data, "Linking") != 0 ||
            !same_time(status_of(alpha), initial_alpha) || !same_time(status_of(beta), initial_beta) ||
            !same_time(status_of(binary), initial_binary) || !same_time(status_of(exports), initial_exports))
        {
            fail("an unchanged optimized module repeated native work or changed an export mtime: %s", log.data);
        }
        free(log.data);
        for (uint32_t part = 0; part < BUSTER_CODEGEN_FIXTURE_PARTITIONS; part += 1)
        {
            if (!same_time(status_of(partitions[part]), initial_partitions[part]))
            {
                fail("a source-only edit rewrote unchanged partition %u", part);
            }
        }
    }

    // Change one private data initializer without changing the module's declarations or public boundary. Exactly
    // its defining part, the data part, must compile again; the code parts and the other translation unit are reused.
    char* initializer = strstr(original_checker.data, "private_data = 10");
    char* value = initializer != 0 ? strstr(initializer, "10") : 0;
    if (value == 0)
    {
        fail("can't find the private data initializer");
    }
    value[1] = '1';
    write_file("source/checker_test.c", original_checker.data, 0644);
    value[1] = '0';
    log = capture(build);
    uint32_t changed_partitions = 0;
    uint32_t native_jobs = 0;
    for (uint32_t part = 0; part < BUSTER_CODEGEN_FIXTURE_PARTITIONS; part += 1)
    {
        changed_partitions += !same_time(status_of(partitions[part]), initial_partitions[part]);
    }
    for (const char* cursor = log.data; (cursor = strstr(cursor, "Generating main_unit_checker's native part")) != 0; cursor += 1)
    {
        native_jobs += 1;
    }
    if (changed_partitions != 1 || native_jobs != 1 || strstr(log.data, "main_unit_backend") != 0 ||
        strstr(log.data, "Combining main_unit_checker's") == 0 || strstr(log.data, "Linking") == 0 ||
        same_time(status_of(alpha), initial_alpha) || same_bytes(alpha, "expected-alpha.o") ||
        !same_time(status_of(beta), initial_beta) || !same_time(status_of(exports), initial_exports))
    {
        fail("a private content edit didn't rebuild exactly its native partition and unit: %s", log.data);
    }
    free(log.data);
    write_file("source/checker_test.c", original_checker.data, 0644);
    free(original_checker.data);
    log = capture(build);
    free(log.data);
    if (!same_bytes(alpha, "expected-alpha.o") || !same_time(status_of(beta), initial_beta))
    {
        fail("restoring the private initializer didn't restore deterministic native output");
    }
    const struct stat before_main_alpha = status_of(alpha);
    if (utimensat(AT_FDCWD, "source/main_test.c", 0, 0) != 0)
    {
        fail("can't update the unrelated fixture source");
    }
    log = capture(build);
    if (strstr(log.data, "Partitioning") != 0 || strstr(log.data, "Combining") != 0 ||
        !same_time(status_of(alpha), before_main_alpha) || !same_time(status_of(beta), initial_beta))
    {
        fail("an unrelated source edit repeated cached native generation: %s", log.data);
    }
    free(log.data);
    if (unlink("out/Release/codegen/main_unit_checker/part3.bc") != 0)
    {
        fail("can't remove a generated IR partition");
    }
    log = capture(build);
    if (strstr(log.data, "Partitioning main_unit_checker's") == 0 || strstr(log.data, "main_unit_backend") != 0 ||
        !same_bytes(alpha, "expected-alpha.o") || !same_bytes(beta, "expected-beta.o"))
    {
        fail("a missing partition wasn't restored in its own unit: %s", log.data);
    }
    free(log.data);

    // A failed backend and a failed export restoration must both stop before replacing the native object or link.
    const struct stat before_failure_alpha = status_of(alpha);
    const struct stat before_failure_binary = status_of(binary);
    write_file("out/Release/codegen/main_unit_checker/part3.bc", "invalid LLVM IR\n", 0644);
    if (run("backend-failure.log", 0, build) == 0 || !same_time(status_of(alpha), before_failure_alpha) ||
        !same_time(status_of(binary), before_failure_binary))
    {
        fail("a failed IR backend replaced a completed native object or executable");
    }
    if (unlink("out/Release/codegen/main_unit_checker/part3.bc") != 0)
    {
        fail("can't remove the malformed IR partition");
    }
    log = capture(build);
    free(log.data);
    Text original_exports = read_file(exports);
    write_file(exports, "missing_public_definition\n", 0644);
    const struct stat before_merge_alpha = status_of(alpha);
    const struct stat before_merge_binary = status_of(binary);
    if (run("merge-failure.log", 0, build) == 0 || !same_time(status_of(alpha), before_merge_alpha) ||
        !same_time(status_of(binary), before_merge_binary))
    {
        fail("a failed public-symbol restoration replaced a completed output");
    }
    write_file(exports, original_exports.data, 0644);
    free(original_exports.data);
    log = capture(build);
    free(log.data);
    configure[10] = "-DBUSTER_FIXTURE_FAIL_SPLIT=ON";
    log = capture(configure);
    free(log.data);
    const struct stat before_split_binary = status_of(binary);
    if (run("split-failure.log", 0, build) == 0 || !same_time(status_of(binary), before_split_binary))
    {
        fail("a failed splitter replaced the completed executable");
    }
    configure[10] = "-DBUSTER_FIXTURE_FAIL_SPLIT=OFF";
    log = capture(configure);
    free(log.data);
    log = capture(build);
    free(log.data);

    // C-to-IR cannot diagnose target assembly. The native jobs must retain -Werror for those late diagnostics.
    Text original_source = read_file("source/checker_test.c");
    FILE* warning = fopen("source/checker_test.c", "a");
    if (warning == 0 || fputs("\n__attribute__((used)) static int codegen_warning(int value)\n{\n"
        "    __asm__ volatile(\".warning \\\"codegen diagnostic fixture\\\"\" : \"+r\"(value));\n"
        "    return value;\n}\n", warning) == EOF || fclose(warning) != 0)
    {
        fail("can't append the inline-assembly diagnostic fixture");
    }
    const struct stat before_warning_binary = status_of(binary);
    if (run("warning-failure.log", 0, build) == 0 || !same_time(status_of(binary), before_warning_binary))
    {
        fail("the partition backend accepted an inline-assembly warning under -Werror");
    }
    Text diagnostic = read_file("warning-failure.log");
    if (strstr(diagnostic.data, "codegen diagnostic fixture") == 0 || strstr(diagnostic.data, "-Winline-asm") == 0)
    {
        fail("the inline-assembly fixture failed without its intended diagnostic: %s", diagnostic.data);
    }
    free(diagnostic.data);
    write_file("source/checker_test.c", original_source.data, 0644);
    free(original_source.data);
    log = capture(build);
    free(log.data);
    log = capture(execute);
    free(log.data);
    if (!same_bytes(alpha, "expected-alpha.o") || !same_bytes(beta, "expected-beta.o"))
    {
        fail("recovered native partitions weren't deterministic");
    }
    log = capture(build);
    if (strstr(log.data, "no work to do") == 0)
    {
        fail("the recovered codegen graph was not unchanged: %s", log.data);
    }
    free(log.data);
    if (chdir(root) != 0)
    {
        fail("can't return to %s", root);
    }
    pass("parallel native jobs preserve private function/data/TLS and public/weak bindings, source ownership, caching, failures and -Werror");
}

static void check_parallel_codegen_compiler(void)
{
    const char* const settings[] = {
        "BUSTER_PARALLEL_CODEGEN=1", "BUSTER_DEBUG_INFO=0", "BUSTER_SANITIZER=none", "BUSTER_FUZZ_AVAILABLE=0",
        "BUSTER_FUZZ_ENTRY=", "BUSTER_PGO=0", "BUSTER_PGO_TRAIN=", "BUSTER_HOST_RUNTIME=", "BUSTER_BUILD_DEFINES=", 0,
    };
    BUILD_ENV(settings, "build", "--config", "Release");
    char path[PATH_MAX];
    char* print[] = { "./build.sh", "print-output", "--config", "Release", 0 };
    char log[PATH_MAX];
    snprintf(log, sizeof(log), "%s/parallel-codegen-output.log", work);
    if (run(log, settings, print) != 0)
    {
        fail("can't find the opt-in compiler");
    }
    Text output = read_file(log);
    char* newline = strchr(output.data, '\n');
    if (newline != 0)
    {
        *newline = 0;
    }
    snprintf(path, sizeof(path), "%s", output.data);
    free(output.data);
    char* baked[] = { path, "test-baked-tables", 0 };
    output = capture(baked);
    free(output.data);
    const struct stat compiler = status_of(path);
    BUILD_ENV(settings, "build", "--config", "Release");
    if (!same_time(status_of(path), compiler) || !same_file("build/compiler", path))
    {
        fail("an unchanged opt-in compiler build regenerated or failed to publish its executable");
    }
    pass("the full opt-in compiler links native units, passes baking/canary checks and reuses its unchanged executable");
}

int main(int argc, char** argv)
{
    const bool flags_only = argc == 2 && strcmp(argv[1], "--flags-only") == 0;
    const bool crt_only = argc == 2 && strcmp(argv[1], "--crt-link-only") == 0;
    const bool commands_only = argc == 2 && strcmp(argv[1], "--compile-commands-only") == 0;
    const bool driver_only = argc == 2 && strcmp(argv[1], "--driver-only") == 0;
    const bool codegen_only = argc == 2 && strcmp(argv[1], "--parallel-codegen-only") == 0;
    const bool ownership_only = (argc == 2 || argc == 3) && strcmp(argv[1], "--ownership-only") == 0;
    const bool develop_only = argc == 2 && strcmp(argv[1], "--develop-only") == 0;
    if (argc != 1 && !flags_only && !crt_only && !commands_only && !driver_only && !ownership_only && !codegen_only && !develop_only)
    {
        fail("usage: build-regression [--flags-only | --crt-link-only | --compile-commands-only | --driver-only | --parallel-codegen-only | --develop-only | --ownership-only [COMPILER]]");
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
    align_toolchain();
    if (argc == 1 || ownership_only)
    {
        check_standalone_preprocessing();
        check_complete_driver_preprocessing();
        check_target_row_counts();
    }
    if (argc == 1 || driver_only)
    {
        check_driver();
        check_toolchain();
    }
    if (ownership_only)
    {
        check_existing_ownership(argc == 3 ? argv[2] : 0);
    }
    if (argc == 1 || codegen_only)
    {
        check_codegen_system_libraries();
        check_parallel_codegen();
    }
    if (!ownership_only && !driver_only && !flags_only && !commands_only && !codegen_only && !develop_only)
    {
        check_crt_link_dependencies();
    }
    if (!ownership_only && !driver_only && !flags_only && !crt_only && !codegen_only && !develop_only)
    {
        check_compile_commands();
    }
    if (!ownership_only && !driver_only && !crt_only && !commands_only && !codegen_only)
    {
        char* output[] = { "./build.sh", "print-output", "--config", "Release", 0 };
        char* output_debug[] = { "./build.sh", "print-output", "--config", "Debug", 0 };
        char* output_develop[] = { "./build.sh", "print-output", "--config", "Develop", 0 };
        capture_line(output, release, sizeof(release));
        capture_line(output_debug, debug, sizeof(debug));
        capture_line(output_develop, develop, sizeof(develop));
        snprintf(tree, sizeof(tree), "%s", release);
        *strrchr(tree, '/') = 0;
        *strrchr(tree, '/') = 0;
        snprintf(ninja_log, sizeof(ninja_log), "%s/.ninja_log", tree);
    }
    if (develop_only)
    {
        BUILD("build", "--config", "Release");
        check_develop();
    }
    if (!ownership_only && !driver_only && !crt_only && !commands_only && !codegen_only && !develop_only)
    {
        BUILD("clean");
        check_ignored_flags();
        if (!flags_only)
        {
            check_clean_all();
            check_incremental();
            check_configurations();
            check_develop();
            check_failures();
            check_presets_and_prints();
            check_experiments();
            check_parallel_codegen_compiler();
        }
    }
    char* remove[] = { "rm", "-rf", work, 0 };
    run(0, 0, remove);
    return 0;
}
