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
// Tier 0 and by its --tier1. Every script is read first, and each must be a readable regular file. Each build then runs
// each script, each in a new empty directory as its working directory, as a copy there (script.sql) with a `.print` of
// a sentinel line after its last line, its standard input /dev/null and its standard output and errors into one file:
// - SQLite's own CLI tests (--tests: SQLite's test/ directory at 3.53.2's commit), the .sql files test/shellB.test
//   runs, each as shellB.test runs it (`sqlite3 :memory: ".read FILE"`): it must exit with status 0 and end with its
//   ".testcase"s' count, "N tests run with 0 errors";
// - each .sql file of --scripts (tests/sqlite: the SQL engine, the bundled extensions, the dot-commands, and on-disk
//   databases with .selftest), run the same way.
// Every run's output and exit status by this compiler's builds must be the clang build's, byte for byte, and the clang
// build's run must have read the script to its end (status 0 or 1, and the sentinel's line). A run has 120 s, a build
// 600 s. Each build's
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
// set in its environment, for at most `seconds`: an alarm, which execve keeps, ends it with SIGALRM then. Returns its
// exit status, 128 and the signal's number for one a signal ended (142 for the alarm's), or -1 if it didn't run.
static int sqlite_test_spawn_with(char* const* argv, const char* directory, const char* output, const char* errors, const char* environment, uint32_t seconds)
{
    const pid_t child = fork();
    if (child == 0)
    {
        if (environment != NULL)
        {
            putenv((char*)environment);
        }
        alarm(seconds);
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

// A build's limit, and a script run's (SQLite's CLI tests and tests/sqlite's take about a second each).
#define BUSTER_SQLITE_TEST_BUILD_SECONDS 600
#define BUSTER_SQLITE_TEST_RUN_SECONDS 120

static int sqlite_test_spawn(char* const* argv, const char* directory, const char* output, const char* errors)
{
    return sqlite_test_spawn_with(argv, directory, output, errors, NULL, BUSTER_SQLITE_TEST_BUILD_SECONDS);
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

// The line each run's copy of a script ends with, whose output proves that the shell read the script to its end: an
// error in the script doesn't stop `.read` (the shell doesn't bail by default), but a script it couldn't open, or one
// that stopped it, prints no such line.
#define BUSTER_SQLITE_TEST_SENTINEL "test-sqlite: the script's end"

// A script's text: `path` read whole, which must be a regular file. Returns null if it isn't one or can't be read.
static char* sqlite_test_load(Arena* arena, const char* path, uint64_t* length)
{
    char* text = NULL;
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    if (file != -1 && fstat(file, &status) == 0 && S_ISREG(status.st_mode) && status.st_size >= 0)
    {
        const uint64_t size = (uint64_t)status.st_size;
        char* bytes = BUSTER_ARENA_PUSH_ARRAY(arena, char, size + 1);
        uint64_t done = 0;
        ssize_t got = 1;
        while (bytes != NULL && done < size && got > 0)
        {
            got = read(file, bytes + done, size - done);
            done += got > 0 ? (uint64_t)got : 0;
        }
        if (bytes != NULL && done == size)
        {
            bytes[size] = 0;
            text = bytes;
            *length = size;
        }
    }
    if (file != -1)
    {
        close(file);
    }
    return text;
}

// Runs a script (`text`, `length` bytes) by build `b`'s shell in a new empty directory: its copy there, script.sql, with
// the sentinel's `.print` after its last line, as `sqlite3 :memory: ".read script.sql"`, whose output goes into
// `output`. The copy's name is the same for every run, so an error's message names the same file whatever the build.
// Returns the shell's exit status, or -1 if it didn't run.
static int sqlite_test_run(SqliteTest* t, uint32_t b, const char* text, uint64_t length, char* output)
{
    static const char sentinel[] = "\n.print " BUSTER_SQLITE_TEST_SENTINEL "\n";
    char directory[PATH_MAX];
    char copy[PATH_MAX];
    char number[16];
    snprintf(number, sizeof(number), "%u", t->runs);
    t->runs += 1;
    bool ok = sqlite_test_path(directory, "%s/run-%s", t->directory, number) && sqlite_test_path(output, "%s/run-%s.txt", t->directory, number) &&
              sqlite_test_path(copy, "%s/%s", directory, "script.sql") && mkdir(directory, 0755) == 0;
    const int file = ok ? open(copy, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
    ok = file != -1 && write(file, text, length) == (ssize_t)length && write(file, sentinel, sizeof(sentinel) - 1) == (ssize_t)(sizeof(sentinel) - 1);
    if (file != -1)
    {
        close(file);
    }
    char* arguments[] = { t->shells[b], ":memory:", ".read script.sql", NULL };
    return ok ? sqlite_test_spawn_with(arguments, directory, output, NULL, NULL, BUSTER_SQLITE_TEST_RUN_SECONDS) : -1;
}

// Whether `text` holds the sentinel's line: the shell read its copy of the script to the end.
static bool sqlite_test_ended(const char* text)
{
    static const char line[] = BUSTER_SQLITE_TEST_SENTINEL "\n";
    const bool first = strncmp(text, line, sizeof(line) - 1) == 0;
    return first || strstr(text, "\n" BUSTER_SQLITE_TEST_SENTINEL "\n") != NULL;
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

// Runs a script (`script`, `length` bytes) by every build and checks the others' output and exit status against the
// reference's (and with `counted`, that the reference's run passed its ".testcase"s). Returns whether they agree.
static bool sqlite_test_check(Arena* arena, SqliteTest* t, const char* script, uint64_t length, const char* name, bool counted)
{
    char outputs[BUSTER_SQLITE_TEST_BUILDS][PATH_MAX];
    int statuses[BUSTER_SQLITE_TEST_BUILDS];
    const uint64_t mark = arena->length;
    const char* texts[BUSTER_SQLITE_TEST_BUILDS];
    bool ok = true;
    for (uint32_t b = 0; b < BUSTER_SQLITE_TEST_BUILDS; b += 1)
    {
        statuses[b] = sqlite_test_run(t, b, script, length, outputs[b]);
        texts[b] = statuses[b] != -1 ? measure_compile_read(arena, outputs[b]) : NULL;
        ok = ok && texts[b] != NULL;
    }
    // The reference must have run the script to its end: the shell's own statuses (0, or 1 after an error), never the
    // 127 of a shell that didn't start or a signal's, and the sentinel's line; a comparison of two failures to start, or
    // to open the script, would pass vacuously.
    const int reference = statuses[SQLITE_TEST_CLANG];
    const bool ran = ok && (reference == 0 || reference == 1) && sqlite_test_ended(texts[SQLITE_TEST_CLANG]);
    const bool passed = ran && (!counted || (reference == 0 && sqlite_test_passed(texts[SQLITE_TEST_CLANG])));
    if (!passed)
    {
        printf("test-sqlite: %s: clang -O0's shell %s (status %d)\n", name, ran ? "didn't pass its tests" : "didn't run it", reference);
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
    // The work directory absolute (a relative --directory or TMPDIR among them): each run's shell starts in a directory
    // of its own, by the path made from it.
    char absolute[PATH_MAX];
    const bool made = valid && mkdtemp(t.directory) != NULL && realpath(t.directory, absolute) != NULL && strlen(absolute) + 64 < sizeof(t.directory);
    if (made)
    {
        memcpy(t.directory, absolute, strlen(absolute) + 1);
    }
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
        // Every script read first, SQLite's CLI tests then --scripts': each must be a readable regular file (not a
        // dangling link, a directory or a file it may not read), before any build.
        const uint32_t total = (uint32_t)BUSTER_SQLITE_TEST_CLI_TESTS + (uint32_t)(scripts > 0 ? scripts : 0);
        const char* script_names[BUSTER_SQLITE_TEST_CLI_TESTS + BUSTER_SQLITE_TEST_SCRIPTS];
        char* texts[BUSTER_SQLITE_TEST_CLI_TESTS + BUSTER_SQLITE_TEST_SCRIPTS];
        uint64_t lengths[BUSTER_SQLITE_TEST_CLI_TESTS + BUSTER_SQLITE_TEST_SCRIPTS];
        uint32_t unreadable = 0;
        for (uint32_t s = 0; ok && s < total; s += 1)
        {
            const bool cli = s < BUSTER_SQLITE_TEST_CLI_TESTS;
            char script[PATH_MAX];
            script_names[s] = cli ? sqlite_test_cli_tests[s] : names[s - BUSTER_SQLITE_TEST_CLI_TESTS];
            lengths[s] = 0;
            texts[s] = sqlite_test_path(script, "%s/%s", cli ? t.tests : t.scripts, script_names[s]) ? sqlite_test_load(&arena, script, &lengths[s]) : NULL;
            if (texts[s] == NULL)
            {
                printf("test-sqlite: %s: not a readable regular file (FAILED)\n", script);
            }
            unreadable += texts[s] == NULL;
        }
        ok = ok && unreadable == 0;
        if (ok)
        {
            printf("test-sqlite: SQLite's shell by %u builds, in %s\n", BUSTER_SQLITE_TEST_BUILDS, t.directory);
        }
        for (uint32_t b = 0; ok && b < BUSTER_SQLITE_TEST_BUILDS; b += 1)
        {
            ok = sqlite_test_build(&t, b);
        }
        uint32_t failed = unreadable;
        for (uint32_t s = 0; ok && s < total; s += 1)
        {
            failed += !sqlite_test_check(&arena, &t, texts[s], lengths[s], script_names[s], s < BUSTER_SQLITE_TEST_CLI_TESTS);
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
