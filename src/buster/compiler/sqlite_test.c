// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// test-sqlite (#146): SQLite's sqlite3 shell, built by this compiler, against the same shell built by clang.
//
//   build/compiler test-sqlite --clang PATH --sqlite DIR --tests DIR --scripts DIR [--directory DIR] [--keep]
//
// --sqlite's DIR holds the amalgamation (sqlite3.c, sqlite3.h) and the shell's source (shell.c): the frozen corpus's
// SQLite 3.53.2. The shell is built three times, with the options SQLite's configure gives the shell (autosetup's
// OPT_SHELL) and its default math functions, from both files and -lm: by clang -O0 (the reference), by this compiler's
// Tier 0 and by its --tier1. Each build then runs, each in a new empty directory as its working directory, its standard
// input /dev/null and its standard output and errors into one file:
// - SQLite's own CLI tests (--tests: SQLite's test/ directory at 3.53.2's commit), the .sql files test/shellB.test
//   runs, each as shellB.test runs it (`sqlite3 :memory: ".read FILE"`): it must exit with status 0 and end with its
//   ".testcase"s' count, "N tests run with 0 errors";
// - each .sql file of --scripts (tests/sqlite: the SQL engine, the bundled extensions, the dot-commands, and on-disk
//   databases with .selftest), run the same way.
// Every run's output and exit status by this compiler's builds must be the clang build's, byte for byte. Each build's
// commands' errors go to DIR/errors.txt, printed on a failure. DIR is a new directory under --directory ($TMPDIR, or
// /tmp), removed at the end unless --keep or a failure.
//
// Layout: sqlite_test_spawn (one process), sqlite_test_build (one shell), sqlite_test_run (one script by one build),
// sqlite_test_check (a script by every build, against the reference), run_sqlite_tests (the command).

#define BUSTER_SQLITE_TEST_BUILDS 3
#define BUSTER_SQLITE_TEST_ARGUMENTS 40
#define BUSTER_SQLITE_TEST_SCRIPTS 32

typedef enum SqliteTestBuild
{
    SQLITE_TEST_CLANG,
    SQLITE_TEST_TIER0,
    SQLITE_TEST_TIER1,
} SqliteTestBuild;

static const char* const sqlite_test_build_names[BUSTER_SQLITE_TEST_BUILDS] = {
    "clang -O0",
    "Tier 0",
    "--tier1",
};

static const char* const sqlite_test_build_files[BUSTER_SQLITE_TEST_BUILDS] = {
    "clang",
    "tier0",
    "tier1",
};

// SQLite's configure's options for the shell (autosetup/sqlite-config.tcl's OPT_SHELL), and the math functions its
// default build enables.
static const char* const sqlite_test_shell_options[] = {
    "-DSQLITE_DQS=0",
    "-DSQLITE_ENABLE_FTS4",
    "-DSQLITE_ENABLE_RTREE",
    "-DSQLITE_ENABLE_EXPLAIN_COMMENTS",
    "-DSQLITE_ENABLE_UNKNOWN_SQL_FUNCTION",
    "-DSQLITE_ENABLE_STMTVTAB",
    "-DSQLITE_ENABLE_DBPAGE_VTAB",
    "-DSQLITE_ENABLE_DBSTAT_VTAB",
    "-DSQLITE_ENABLE_BYTECODE_VTAB",
    "-DSQLITE_ENABLE_OFFSET_SQL_FUNC",
    "-DSQLITE_ENABLE_PERCENTILE",
    "-DSQLITE_STRICT_SUBTYPE=1",
    "-DSQLITE_ENABLE_MATH_FUNCTIONS",
};

// The CLI tests test/shellB.test runs, in its order.
static const char* const sqlite_test_cli_tests[] = {
    "modeA.sql",
    "dblwidth-a.sql",
    "vt100-a.sql",
    "regexp1.sql",
    "imposter1.sql",
    "dotcmd01.sql",
    "import01.sql",
    "intck01.sql",
    "fptest01.sql",
};

#define BUSTER_SQLITE_TEST_OPTIONS (sizeof(sqlite_test_shell_options) / sizeof(sqlite_test_shell_options[0]))
#define BUSTER_SQLITE_TEST_CLI_TESTS (sizeof(sqlite_test_cli_tests) / sizeof(sqlite_test_cli_tests[0]))

typedef struct SqliteTest SqliteTest;
struct SqliteTest
{
    const char* clang;
    const char* sqlite;
    const char* tests;
    const char* scripts;
    char directory[PATH_MAX];
    char errors[PATH_MAX];
    char shells[BUSTER_SQLITE_TEST_BUILDS][PATH_MAX];
    // A counter that names each run's directory and output.
    uint32_t runs;
};

// Formats into `path`, PATH_MAX bytes; false if it doesn't fit.
static bool sqlite_test_path(char* path, const char* format, const char* a, const char* b)
{
    const int length = snprintf(path, PATH_MAX, format, a, b);
    return length > 0 && length < PATH_MAX;
}

// Runs argv (argv[0] looked up in PATH as a shell does) in `directory` (null: this one), its standard input
// /dev/null, its standard output and errors into `output` (null: appended to `errors`), with `environment` (null: none)
// set in its environment. Returns its exit status, 128 and the signal's number for one a signal ended, or -1 if it
// didn't run.
static int sqlite_test_spawn_with(char* const* argv, const char* directory, const char* output, const char* errors, const char* environment)
{
    const pid_t child = fork();
    if (child == 0)
    {
        if (environment != NULL)
        {
            putenv((char*)environment);
        }
        const int null = open("/dev/null", O_RDONLY | O_CLOEXEC);
        const int out = output != NULL ? open(output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : open(errors, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (null != -1 && out != -1 && dup2(null, STDIN_FILENO) != -1 && dup2(out, STDOUT_FILENO) != -1 && dup2(out, STDERR_FILENO) != -1 &&
            (directory == NULL || chdir(directory) == 0))
        {
            execvp(argv[0], argv);
        }
        _exit(127);
    }
    int status = 0;
    const bool waited = child != -1 && waitpid(child, &status, 0) == child;
    int result = -1;
    if (waited && WIFEXITED(status))
    {
        result = WEXITSTATUS(status);
    }
    else if (waited && WIFSIGNALED(status))
    {
        result = 128 + WTERMSIG(status);
    }
    return result;
}

static int sqlite_test_spawn(char* const* argv, const char* directory, const char* output, const char* errors)
{
    return sqlite_test_spawn_with(argv, directory, output, errors, NULL);
}

// Builds the shell of build `b` into t->shells[b], its errors appended to t->errors. Returns whether it built.
static bool sqlite_test_build(SqliteTest* t, uint32_t b)
{
    char include[PATH_MAX + 2];
    char sources[2][PATH_MAX];
    char* arguments[BUSTER_SQLITE_TEST_ARGUMENTS + 1];
    uint32_t count = 0;
    bool ok = sqlite_test_path(t->shells[b], "%s/sqlite3-%s", t->directory, sqlite_test_build_files[b]) && sqlite_test_path(sources[0], "%s/%s", t->sqlite, "shell.c") &&
              sqlite_test_path(sources[1], "%s/%s", t->sqlite, "sqlite3.c") && snprintf(include, sizeof(include), "-I%s", t->sqlite) < (int)sizeof(include);
    arguments[count++] = b == SQLITE_TEST_CLANG ? (char*)t->clang : "/proc/self/exe";
    if (b == SQLITE_TEST_CLANG)
    {
        arguments[count++] = "-O0";
    }
    if (b == SQLITE_TEST_TIER1)
    {
        arguments[count++] = "--tier1";
    }
    for (uint32_t o = 0; o < BUSTER_SQLITE_TEST_OPTIONS; o += 1)
    {
        arguments[count++] = (char*)sqlite_test_shell_options[o];
    }
    arguments[count++] = include;
    arguments[count++] = "-o";
    arguments[count++] = t->shells[b];
    arguments[count++] = sources[0];
    arguments[count++] = sources[1];
    arguments[count++] = "-lm";
    arguments[count] = NULL;
    ok = ok && sqlite_test_spawn(arguments, NULL, NULL, t->errors) == 0;
    printf("test-sqlite: %s's shell: %s\n", sqlite_test_build_names[b], ok ? "built" : "FAILED");
    return ok;
}

// Runs `script` by build `b`'s shell (`sqlite3 :memory: ".read SCRIPT"`) in a new empty directory, its output into
// `output`. Returns its exit status, or -1 if it didn't run.
static int sqlite_test_run(SqliteTest* t, uint32_t b, const char* script, char* output)
{
    char directory[PATH_MAX];
    char number[16];
    char read[PATH_MAX + 16];
    snprintf(number, sizeof(number), "%u", t->runs);
    t->runs += 1;
    const bool ok = sqlite_test_path(directory, "%s/run-%s", t->directory, number) && sqlite_test_path(output, "%s/run-%s.txt", t->directory, number) &&
                    snprintf(read, sizeof(read), ".read %s", script) < (int)sizeof(read) && mkdir(directory, 0755) == 0;
    char* arguments[] = { t->shells[b], ":memory:", read, NULL };
    return ok ? sqlite_test_spawn(arguments, directory, output, NULL) : -1;
}

// The line of `text` at which it first differs from `reference`, from 1.
static uint64_t sqlite_test_first_difference(const char* reference, const char* text)
{
    uint64_t line = 1;
    uint64_t i = 0;
    while (reference[i] != 0 && reference[i] == text[i])
    {
        line += reference[i] == '\n';
        i += 1;
    }
    return line;
}

// Whether `text` ends with the line the CLI prints after its ".testcase"s when none failed: "N test(s) run with 0
// errors", N from 1.
static bool sqlite_test_passed(const char* text)
{
    const size_t length = strlen(text);
    size_t start = length > 0 && text[length - 1] == '\n' ? length - 1 : length;
    while (start > 0 && text[start - 1] != '\n')
    {
        start -= 1;
    }
    const char* line = text + start;
    uint32_t tests = 0;
    int end = 0;
    const bool counted = sscanf(line, "%u test%n", &tests, &end) == 1 && end > 0;
    const char* rest = counted ? line + end : "";
    const bool plural = strncmp(rest, "s run with 0 errors", 19) == 0;
    const bool single = strncmp(rest, " run with 0 errors", 18) == 0;
    const char* after = plural ? rest + 19 : rest + 18;
    return counted && tests > 0 && (plural == (tests != 1)) && (plural || single) && (*after == '\n' || *after == 0);
}

// Runs `script` by every build and checks the others' output and exit status against the reference's (and with
// `counted`, that the reference's run passed its ".testcase"s). Returns whether they agree.
static bool sqlite_test_check(Arena* arena, SqliteTest* t, const char* script, const char* name, bool counted)
{
    char outputs[BUSTER_SQLITE_TEST_BUILDS][PATH_MAX];
    int statuses[BUSTER_SQLITE_TEST_BUILDS];
    const uint64_t mark = arena->length;
    const char* texts[BUSTER_SQLITE_TEST_BUILDS];
    bool ok = true;
    for (uint32_t b = 0; b < BUSTER_SQLITE_TEST_BUILDS; b += 1)
    {
        statuses[b] = sqlite_test_run(t, b, script, outputs[b]);
        texts[b] = statuses[b] != -1 ? measure_compile_read(arena, outputs[b]) : NULL;
        ok = ok && texts[b] != NULL;
    }
    const bool passed = ok && (!counted || (statuses[SQLITE_TEST_CLANG] == 0 && sqlite_test_passed(texts[SQLITE_TEST_CLANG])));
    if (!passed)
    {
        printf("test-sqlite: %s: clang -O0's shell %s (status %d)\n", name, ok ? "didn't pass its tests" : "didn't run", statuses[SQLITE_TEST_CLANG]);
    }
    ok = passed;
    for (uint32_t b = SQLITE_TEST_TIER0; passed && b < BUSTER_SQLITE_TEST_BUILDS; b += 1)
    {
        const bool same = statuses[b] == statuses[SQLITE_TEST_CLANG] && measure_compile_same(arena, outputs[b], outputs[SQLITE_TEST_CLANG]);
        if (!same)
        {
            printf("test-sqlite: %s: %s's shell DIFFERS from clang -O0's: status %d against %d, output from line %llu (%s against %s)\n", name, sqlite_test_build_names[b],
                   statuses[b], statuses[SQLITE_TEST_CLANG], (unsigned long long)sqlite_test_first_difference(texts[SQLITE_TEST_CLANG], texts[b]), outputs[b],
                   outputs[SQLITE_TEST_CLANG]);
        }
        ok = ok && same;
    }
    if (ok)
    {
        printf("test-sqlite: %s: every build's output is clang -O0's%s\n", name, counted ? ", and its tests pass" : "");
    }
    arena_pop_to(arena, mark);
    return ok;
}

// The .sql files of `directory`, sorted, into `names` (BUSTER_SQLITE_TEST_SCRIPTS of 256 bytes). Returns how many, or
// -1 if the directory can't be read or holds too many.
static int32_t sqlite_test_scripts(const char* directory, char names[][256])
{
    DIR* dir = opendir(directory);
    int32_t count = dir != NULL ? 0 : -1;
    const struct dirent* entry = dir != NULL ? readdir(dir) : NULL;
    while (entry != NULL && count >= 0)
    {
        const size_t length = strlen(entry->d_name);
        if (length > 4 && length < 256 && strcmp(entry->d_name + length - 4, ".sql") == 0)
        {
            if (count < BUSTER_SQLITE_TEST_SCRIPTS)
            {
                memcpy(names[count], entry->d_name, length + 1);
                count += 1;
            }
            else
            {
                count = -1;
            }
        }
        entry = readdir(dir);
    }
    if (dir != NULL)
    {
        closedir(dir);
    }
    // Insertion sort: a handful of names.
    for (int32_t i = 1; i < count; i += 1)
    {
        char name[256];
        memcpy(name, names[i], sizeof(name));
        int32_t j = i;
        while (j > 0 && strcmp(names[j - 1], name) > 0)
        {
            memcpy(names[j], names[j - 1], sizeof(name));
            j -= 1;
        }
        memcpy(names[j], name, sizeof(name));
    }
    return count;
}

static int run_sqlite_tests(int argc, char** argv)
{
    SqliteTest t = { 0 };
    const char* parent = getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp";
    bool keep = false;
    bool valid = true;
    int i = 1;
    while (valid && i < argc)
    {
        const bool pair = i + 1 < argc;
        const bool flag = strcmp(argv[i], "--keep") == 0;
        if (flag)
        {
            keep = true;
        }
        else if (strcmp(argv[i], "--clang") == 0 && pair)
        {
            t.clang = argv[i + 1];
        }
        else if (strcmp(argv[i], "--sqlite") == 0 && pair)
        {
            t.sqlite = argv[i + 1];
        }
        else if (strcmp(argv[i], "--tests") == 0 && pair)
        {
            t.tests = argv[i + 1];
        }
        else if (strcmp(argv[i], "--scripts") == 0 && pair)
        {
            t.scripts = argv[i + 1];
        }
        else if (strcmp(argv[i], "--directory") == 0 && pair)
        {
            parent = argv[i + 1];
        }
        else
        {
            valid = false;
        }
        i += flag ? 1 : 2;
    }
    // The scripts run in directories of their own, so their paths are made absolute.
    char tests[PATH_MAX];
    char scripts_directory[PATH_MAX];
    valid = valid && t.clang != NULL && t.sqlite != NULL && t.tests != NULL && t.scripts != NULL;
    if (valid && realpath(t.tests, tests) != NULL && realpath(t.scripts, scripts_directory) != NULL)
    {
        t.tests = tests;
        t.scripts = scripts_directory;
    }
    const int length = valid ? snprintf(t.directory, sizeof(t.directory), "%s/test-sqlite-XXXXXX", parent) : -1;
    valid = valid && length > 0 && (size_t)length + 64 < sizeof(t.directory);
    const bool made = valid && mkdtemp(t.directory) != NULL;
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-sqlite --clang PATH --sqlite DIR --tests DIR --scripts DIR [--directory DIR] [--keep]\n");
    }
    else if (!made)
    {
        fprintf(stderr, "error: can't make a directory under %s: %s\n", parent, strerror(errno));
    }
    else
    {
        Arena arena = arena_reserve(UINT64_C(1) << 32);
        char names[BUSTER_SQLITE_TEST_SCRIPTS][256];
        const int32_t scripts = sqlite_test_scripts(t.scripts, names);
        bool ok = sqlite_test_path(t.errors, "%s/%s", t.directory, "errors.txt") && scripts > 0;
        if (scripts <= 0)
        {
            printf("test-sqlite: no scripts in %s (FAILED)\n", t.scripts);
        }
        printf("test-sqlite: SQLite's shell by %u builds, in %s\n", BUSTER_SQLITE_TEST_BUILDS, t.directory);
        for (uint32_t b = 0; ok && b < BUSTER_SQLITE_TEST_BUILDS; b += 1)
        {
            ok = sqlite_test_build(&t, b);
        }
        uint32_t failed = 0;
        for (uint32_t s = 0; ok && s < BUSTER_SQLITE_TEST_CLI_TESTS; s += 1)
        {
            char script[PATH_MAX];
            const bool passed = sqlite_test_path(script, "%s/%s", t.tests, sqlite_test_cli_tests[s]) && sqlite_test_check(&arena, &t, script, sqlite_test_cli_tests[s], true);
            failed += !passed;
        }
        for (int32_t s = 0; ok && s < scripts; s += 1)
        {
            char script[PATH_MAX];
            const bool passed = sqlite_test_path(script, "%s/%s", t.scripts, names[s]) && sqlite_test_check(&arena, &t, script, names[s], false);
            failed += !passed;
        }
        ok = ok && failed == 0;
        if (ok)
        {
            printf("test-sqlite: %u of SQLite's CLI tests and %d scripts: every build agrees with clang -O0's\n", (uint32_t)BUSTER_SQLITE_TEST_CLI_TESTS, scripts);
        }
        else
        {
            const char* errors = measure_compile_read(&arena, t.errors);
            fprintf(stderr, "test-sqlite: %u FAILED; kept %s; the builds' errors:\n%s\n", failed, t.directory, errors != NULL ? errors : "");
        }
        result = ok ? 0 : 1;
        if (ok && !keep)
        {
            char* remove[] = { "rm", "-rf", t.directory, NULL };
            sqlite_test_spawn(remove, NULL, "/dev/null", NULL);
        }
        arena_release(&arena);
    }
    return result;
}
