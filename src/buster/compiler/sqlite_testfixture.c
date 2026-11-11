// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

//
// test-sqlite-testfixture (#146): SQLite's Tcl test suite, every test/*.test, through testfixture built by this compiler,
// against the same testfixture built by clang.
//
//   build/compiler test-sqlite-testfixture --clang PATH --tree DIR --tcl DIR [--amalgamation FILE] [--subset FILE]
//       [--jobs N] [--timeout SECONDS] [--directory DIR] [--keep]
//
// --tree's DIR is SQLite's source tree at 3.53.2's commit (tools/ci/fetch-sqlite.sh's), and --tcl's the directory that
// holds Tcl's tclConfig.sh (the distribution's tcl8.6 package, docs/commands/c-testsuite.md). testfixture is built three
// times, each by SQLite's own configure and Makefile (`configure --with-tcl=DIR CC=... CFLAGS=...`, then `make
// testfixture`) in a directory of its own: by clang -O0 (the reference), by this compiler's Tier 0 and by its --tier1.
// The Makefile's `CFLAGS.core=` drops -fPIC, which the driver refuses (#2101); its `-rpath DIR` is the driver's since
// #1854. Each build's sqlite3.c is the amalgamation its Makefile generates from the tree; with --amalgamation (the frozen
// corpus's sqlite3.c) each must be that file, byte for byte.
//
// Each build's testfixture then runs SQLite's test runner, `testfixture test/testrunner.tcl --jobs N full [PATTERN...]`,
// in an empty directory of its own: the `full` permutation is every Tcl test script of test/ and ext/ (but the scripts
// that only run others, such as all.test and veryquick.test), each as a job of its own, whose state, test count and
// error count the runner records in its testrunner.db. --subset's FILE names the scripts to run instead (one per line, as
// the runner names them, test/select1.test; '#' starts a comment), passed as anchored patterns ("^full select1.test$"). The reference's
// testfixture reads every build's testrunner.db (a Tcl script of this harness's). It must have run every script of the
// subset (or the permutation) once, and all of them with no errors: a failure there is the environment's (as root,
// SQLite's tests of read-only files fail), and fails the command. Every build's scripts must then be the reference's,
// each with the same state, error count and test count; a few scripts run a random number of tests
// (testfixture_varying), whose counts aren't compared.
//
// A build has 600 s, a run --timeout SECONDS (3600 by default). The builds' and runs' output goes to DIR/errors.txt and
// DIR/run-BUILD.txt, printed or named on a failure. DIR is a new directory under --directory ($TMPDIR, or /tmp),
// removed at the end unless --keep or a failure.
//
// Layout: testfixture_build (one testfixture), testfixture_run (the runner by one build), testfixture_jobs (a build's
// testrunner.db, read), testfixture_compare (a build against the reference), run_sqlite_testfixture (the command).

#define BUSTER_TESTFIXTURE_JOBS 4096
#define BUSTER_TESTFIXTURE_NAME 128
#define BUSTER_TESTFIXTURE_RUN_SECONDS 3600

// Scripts whose test counts depend on random choices made as they run (crash.test's simulated crashes), so that two
// runs of one testfixture differ: their states and error counts are still compared.
static const char* const testfixture_varying[] = {
    "test/crash.test",
};

// The CFLAGS each build's configure takes: the reference at -O0, the shell harness's (sqlite_test_build).
static const char* const testfixture_cflags[BUSTER_SQLITE_TEST_BUILDS] = {
    "CFLAGS=-O0",
    "CFLAGS=",
    "CFLAGS=--tier1",
};

// The Tcl script that prints a testrunner.db's jobs, by name: "NAME STATE NTEST NERR", a missing count as -1.
static const char testfixture_jobs_script[] = "sqlite3 db [lindex $argv 0] -readonly 1\n"
                                              "db eval {SELECT displayname, state, coalesce(ntest, -1) AS t, coalesce(nerr, -1) AS e FROM jobs ORDER BY displayname} {\n"
                                              "    puts \"$displayname $state $t $e\"\n"
                                              "}\n"
                                              "db close\n";

typedef struct TestfixtureJob TestfixtureJob;
struct TestfixtureJob
{
    char name[BUSTER_TESTFIXTURE_NAME];
    char state[16];
    int64_t tests;
    int64_t errors;
};

typedef struct TestfixtureJobs TestfixtureJobs;
struct TestfixtureJobs
{
    TestfixtureJob* jobs;
    uint32_t count;
};

typedef struct Testfixture Testfixture;
struct Testfixture
{
    const char* clang;
    const char* tree;
    const char* tcl;
    const char* amalgamation;
    char compiler[PATH_MAX];
    char directory[PATH_MAX];
    char errors[PATH_MAX];
    char fixtures[BUSTER_SQLITE_TEST_BUILDS][PATH_MAX];
    char runs[BUSTER_SQLITE_TEST_BUILDS][PATH_MAX];
    char outputs[BUSTER_SQLITE_TEST_BUILDS][PATH_MAX];
    // The subset's patterns and names, or none: the whole permutation. The runner matches a pattern against each job's
    // permutation and script's file name ("full select1.test"); "^full NAME$" matches that one alone.
    char** patterns;
    char** names;
    uint32_t subset_count;
    const char* jobs;
    uint32_t timeout;
};

// Builds build `b`'s testfixture into t->fixtures[b]: configure, then make, in DIR/build-BUILD. Returns whether it built
// (and, with --amalgamation, whether its sqlite3.c is that file).
static bool testfixture_build(Arena* arena, Testfixture* t, uint32_t b)
{
    char build[PATH_MAX];
    char configure[PATH_MAX];
    char with_tcl[PATH_MAX + 16];
    char compiler[PATH_MAX + 8];
    char generated[PATH_MAX];
    const char* cc = b == SQLITE_TEST_CLANG ? t->clang : t->compiler;
    bool ok = sqlite_test_path(build, "%s/build-%s", t->directory, sqlite_test_build_files[b]) && sqlite_test_path(configure, "%s/%s", t->tree, "configure") &&
              sqlite_test_path(t->fixtures[b], "%s/%s", build, "testfixture") && sqlite_test_path(generated, "%s/%s", build, "sqlite3.c") &&
              snprintf(with_tcl, sizeof(with_tcl), "--with-tcl=%s", t->tcl) < (int)sizeof(with_tcl) && snprintf(compiler, sizeof(compiler), "CC=%s", cc) < (int)sizeof(compiler) &&
              mkdir(build, 0755) == 0;
    char* configure_arguments[] = { configure, with_tcl, compiler, (char*)testfixture_cflags[b], NULL };
    ok = ok && sqlite_test_spawn(configure_arguments, build, NULL, t->errors) == 0;
    char* make_arguments[] = { "make", "testfixture", "CFLAGS.core=", NULL };
    ok = ok && sqlite_test_spawn(make_arguments, build, NULL, t->errors) == 0;
    const bool same = ok && (t->amalgamation == NULL || measure_compile_same(arena, generated, t->amalgamation));
    printf("test-sqlite-testfixture: %s's testfixture: %s\n", sqlite_test_build_names[b], same ? "built" : ok ? "built, but its sqlite3.c isn't --amalgamation's (FAILED)" : "FAILED");
    return same;
}

// Runs SQLite's test runner by build `b`'s testfixture in DIR/run-BUILD, its output into DIR/run-BUILD.txt. Returns
// whether it ran to its end (its exit status 0: the runner reports failed scripts in its database, not by its status).
static bool testfixture_run(Arena* arena, Testfixture* t, uint32_t b)
{
    char runner[PATH_MAX];
    const uint32_t fixed = 5;
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)fixed + t->subset_count + 1);
    bool ok = arguments != NULL && sqlite_test_path(t->runs[b], "%s/run-%s", t->directory, sqlite_test_build_files[b]) &&
              sqlite_test_path(t->outputs[b], "%s/run-%s.txt", t->directory, sqlite_test_build_files[b]) && sqlite_test_path(runner, "%s/%s", t->tree, "test/testrunner.tcl") &&
              mkdir(t->runs[b], 0755) == 0;
    int status = -1;
    if (ok)
    {
        arguments[0] = t->fixtures[b];
        arguments[1] = runner;
        arguments[2] = "--jobs";
        arguments[3] = (char*)t->jobs;
        arguments[4] = "full";
        for (uint32_t s = 0; s < t->subset_count; s += 1)
        {
            arguments[fixed + s] = t->patterns[s];
        }
        arguments[fixed + t->subset_count] = NULL;
        status = sqlite_test_spawn_with(arguments, t->runs[b], t->outputs[b], NULL, NULL, t->timeout);
    }
    printf("test-sqlite-testfixture: %s's run: %s (status %d)\n", sqlite_test_build_names[b], status == 0 ? "ended" : "FAILED", status);
    return status == 0;
}

// The next space-separated field of `*cursor` (within its line) into `field`, `size` bytes. Returns whether one fit.
static bool testfixture_field(const char** cursor, char* field, uint64_t size)
{
    const char* c = *cursor;
    while (*c == ' ')
    {
        c += 1;
    }
    uint64_t length = 0;
    while (c[length] != 0 && c[length] != ' ' && c[length] != '\n')
    {
        length += 1;
    }
    const bool fits = length > 0 && length < size;
    if (fits)
    {
        memcpy(field, c, length);
        field[length] = 0;
    }
    *cursor = c + length;
    return fits;
}

// Build `b`'s testrunner.db, read by the reference's testfixture into `jobs`, in the runner's order (by name). Returns
// whether it was read whole: every line "NAME STATE NTEST NERR", at most BUSTER_TESTFIXTURE_JOBS of them.
static bool testfixture_jobs(Arena* arena, Testfixture* t, uint32_t b, TestfixtureJobs* jobs)
{
    char script[PATH_MAX];
    char database[PATH_MAX];
    char listing[PATH_MAX];
    bool ok = sqlite_test_path(script, "%s/%s", t->directory, "jobs.tcl") && sqlite_test_path(database, "%s/%s", t->runs[b], "testrunner.db") &&
              sqlite_test_path(listing, "%s/jobs-%s.txt", t->directory, sqlite_test_build_files[b]);
    const int file = ok ? open(script, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
    ok = file != -1 && write(file, testfixture_jobs_script, sizeof(testfixture_jobs_script) - 1) == (ssize_t)(sizeof(testfixture_jobs_script) - 1);
    if (file != -1)
    {
        close(file);
    }
    char* arguments[] = { t->fixtures[SQLITE_TEST_CLANG], script, database, NULL };
    ok = ok && sqlite_test_spawn_with(arguments, NULL, listing, NULL, NULL, BUSTER_SQLITE_TEST_RUN_SECONDS) == 0;
    const char* text = ok ? measure_compile_read(arena, listing) : NULL;
    jobs->jobs = BUSTER_ARENA_PUSH_ARRAY(arena, TestfixtureJob, BUSTER_TESTFIXTURE_JOBS);
    jobs->count = 0;
    ok = text != NULL && jobs->jobs != NULL;
    const char* cursor = text;
    while (ok && *cursor != 0)
    {
        char tests[24];
        char errors[24];
        TestfixtureJob* job = &jobs->jobs[jobs->count];
        ok = jobs->count < BUSTER_TESTFIXTURE_JOBS && testfixture_field(&cursor, job->name, sizeof(job->name)) && testfixture_field(&cursor, job->state, sizeof(job->state)) &&
             testfixture_field(&cursor, tests, sizeof(tests)) && testfixture_field(&cursor, errors, sizeof(errors)) && *cursor == '\n';
        if (ok)
        {
            job->tests = strtoll(tests, NULL, 10);
            job->errors = strtoll(errors, NULL, 10);
            jobs->count += 1;
            cursor += 1;
        }
    }
    if (!ok)
    {
        printf("test-sqlite-testfixture: %s's testrunner.db: unreadable (FAILED), %s\n", sqlite_test_build_names[b], listing);
    }
    return ok;
}

// Whether `name` is one of testfixture_varying's scripts.
static bool testfixture_varies(const char* name)
{
    bool varies = false;
    for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(testfixture_varying); v += 1)
    {
        varies |= strcmp(name, testfixture_varying[v]) == 0;
    }
    return varies;
}

// Checks the reference's jobs: every subset script ran once (with a subset), and every job passed. Returns how many
// failed checks.
static uint32_t testfixture_reference(const Testfixture* t, const TestfixtureJobs* reference, uint64_t* tests)
{
    uint32_t failed = 0;
    *tests = 0;
    for (uint32_t j = 0; j < reference->count; j += 1)
    {
        const TestfixtureJob* job = &reference->jobs[j];
        const bool passed = strcmp(job->state, "done") == 0 && job->errors == 0 && job->tests >= 0;
        if (!passed)
        {
            printf("test-sqlite-testfixture: %s: clang -O0's testfixture: %s, %lld errors of %lld tests (FAILED)\n", job->name, job->state, (long long)job->errors, (long long)job->tests);
        }
        failed += !passed;
        *tests += job->tests > 0 ? (uint64_t)job->tests : 0;
    }
    for (uint32_t s = 0; s < t->subset_count; s += 1)
    {
        uint32_t found = 0;
        for (uint32_t j = 0; j < reference->count; j += 1)
        {
            found += strcmp(reference->jobs[j].name, t->names[s]) == 0;
        }
        if (found != 1)
        {
            printf("test-sqlite-testfixture: %s: the subset's script ran %u times (FAILED)\n", t->names[s], found);
        }
        failed += found != 1;
    }
    const bool counted = t->subset_count == 0 || reference->count == t->subset_count;
    if (!counted)
    {
        printf("test-sqlite-testfixture: %u scripts ran for the subset's %u (FAILED)\n", reference->count, t->subset_count);
    }
    failed += !counted;
    if (geteuid() == 0 && failed != 0)
    {
        printf("test-sqlite-testfixture: running as root, under which SQLite's tests of read-only files and directories fail\n");
    }
    return failed;
}

// Checks build `b`'s jobs against the reference's: the same scripts, each with the same state, errors and (but for
// testfixture_varying's) tests. Returns how many differ.
static uint32_t testfixture_compare(uint32_t b, const TestfixtureJobs* reference, const TestfixtureJobs* jobs)
{
    uint32_t differ = reference->count != jobs->count;
    if (differ != 0)
    {
        printf("test-sqlite-testfixture: %s ran %u scripts, clang -O0's %u (FAILED)\n", sqlite_test_build_names[b], jobs->count, reference->count);
    }
    for (uint32_t j = 0; differ == 0 && j < reference->count; j += 1)
    {
        const TestfixtureJob* expected = &reference->jobs[j];
        const TestfixtureJob* job = &jobs->jobs[j];
        const bool same = strcmp(job->name, expected->name) == 0 && strcmp(job->state, expected->state) == 0 && job->errors == expected->errors &&
                          (job->tests == expected->tests || testfixture_varies(job->name));
        if (!same)
        {
            printf("test-sqlite-testfixture: %s: %s's testfixture: %s, %lld errors of %lld tests; clang -O0's: %s, %lld of %lld (FAILED)\n", job->name, sqlite_test_build_names[b], job->state,
                   (long long)job->errors, (long long)job->tests, expected->state, (long long)expected->errors, (long long)expected->tests);
        }
        differ += !same;
    }
    return differ;
}

// Reads --subset's FILE into t->names and t->patterns. Returns whether it held at least one name and every line was a
// name (a path whose last component is the pattern), a comment or blank.
static bool testfixture_subset(Arena* arena, Testfixture* t, const char* path)
{
    char* text = measure_compile_read(arena, path);
    uint32_t lines = 0;
    for (const char* c = text; c != NULL && *c != 0; c += 1)
    {
        lines += *c == '\n';
    }
    t->names = text != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)lines + 1) : NULL;
    t->patterns = text != NULL ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)lines + 1) : NULL;
    bool ok = t->names != NULL && t->patterns != NULL;
    char* line = text;
    while (ok && line != NULL && *line != 0)
    {
        char* end = strchr(line, '\n');
        char* next = end != NULL ? end + 1 : NULL;
        if (end != NULL)
        {
            *end = 0;
        }
        const uint64_t length = strlen(line);
        const bool skipped = length == 0 || line[0] == '#';
        const char* slash = strrchr(line, '/');
        const char* file = slash != NULL ? slash + 1 : line;
        const bool named = !skipped && length < BUSTER_TESTFIXTURE_NAME && strchr(line, ' ') == NULL && strchr(line, '*') == NULL && *file != 0;
        const uint64_t size = strlen(file) + sizeof("^full $");
        char* pattern = named ? BUSTER_ARENA_PUSH_ARRAY(arena, char, size) : NULL;
        if (pattern != NULL)
        {
            snprintf(pattern, size, "^full %s$", file);
            t->names[t->subset_count] = line;
            t->patterns[t->subset_count] = pattern;
            t->subset_count += 1;
        }
        else if (!skipped)
        {
            printf("test-sqlite-testfixture: %s: not a script's name: %s (FAILED)\n", path, line);
        }
        ok = skipped || pattern != NULL;
        line = next;
    }
    return ok && t->subset_count > 0;
}

static int run_sqlite_testfixture(int argc, char** argv)
{
    // A line at a time: the builds and runs take minutes, and a log should show each step as it ends.
    setvbuf(stdout, NULL, _IOLBF, 0);
    Testfixture t = { 0 };
    const char* parent = getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp";
    const char* subset = NULL;
    const char* timeout = NULL;
    t.jobs = "4";
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
        else if (strcmp(argv[i], "--tree") == 0 && pair)
        {
            t.tree = argv[i + 1];
        }
        else if (strcmp(argv[i], "--tcl") == 0 && pair)
        {
            t.tcl = argv[i + 1];
        }
        else if (strcmp(argv[i], "--amalgamation") == 0 && pair)
        {
            t.amalgamation = argv[i + 1];
        }
        else if (strcmp(argv[i], "--subset") == 0 && pair)
        {
            subset = argv[i + 1];
        }
        else if (strcmp(argv[i], "--jobs") == 0 && pair)
        {
            t.jobs = argv[i + 1];
        }
        else if (strcmp(argv[i], "--timeout") == 0 && pair)
        {
            timeout = argv[i + 1];
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
    char* end = NULL;
    const unsigned long jobs = strtoul(t.jobs, &end, 10);
    valid = valid && end != t.jobs && *end == 0 && jobs > 0 && jobs <= 256;
    const unsigned long seconds = timeout != NULL ? strtoul(timeout, &end, 10) : BUSTER_TESTFIXTURE_RUN_SECONDS;
    valid = valid && (timeout == NULL || (end != timeout && *end == 0)) && seconds > 0 && seconds <= 86400;
    t.timeout = (uint32_t)seconds;
    // The tree, Tcl's directory and the amalgamation absolute: configure and the runner start in directories of their
    // own. The compiler is this executable's path, which the Makefile runs as CC.
    char tree[PATH_MAX];
    char tcl[PATH_MAX];
    char amalgamation[PATH_MAX];
    valid = valid && t.clang != NULL && t.tree != NULL && t.tcl != NULL;
    const bool resolved = valid && realpath(t.tree, tree) != NULL && realpath(t.tcl, tcl) != NULL && (t.amalgamation == NULL || realpath(t.amalgamation, amalgamation) != NULL) &&
                          realpath("/proc/self/exe", t.compiler) != NULL;
    if (resolved)
    {
        t.tree = tree;
        t.tcl = tcl;
        t.amalgamation = t.amalgamation != NULL ? amalgamation : NULL;
    }
    const int length = valid ? snprintf(t.directory, sizeof(t.directory), "%s/test-sqlite-testfixture-XXXXXX", parent) : -1;
    valid = valid && length > 0 && (size_t)length + 64 < sizeof(t.directory);
    char absolute[PATH_MAX];
    const bool made = valid && resolved && mkdtemp(t.directory) != NULL && realpath(t.directory, absolute) != NULL && strlen(absolute) + 64 < sizeof(t.directory);
    if (made)
    {
        memcpy(t.directory, absolute, strlen(absolute) + 1);
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-sqlite-testfixture --clang PATH --tree DIR --tcl DIR [--amalgamation FILE] [--subset FILE] [--jobs N] [--timeout SECONDS] [--directory DIR] [--keep]\n");
    }
    else if (!resolved)
    {
        fprintf(stderr, "error: --tree, --tcl, --amalgamation or this executable's path doesn't resolve: %s\n", strerror(errno));
    }
    else if (!made)
    {
        fprintf(stderr, "error: can't make a directory under %s: %s\n", parent, strerror(errno));
    }
    else
    {
        Arena arena = arena_reserve(UINT64_C(1) << 32);
        bool ok = sqlite_test_path(t.errors, "%s/%s", t.directory, "errors.txt") && (subset == NULL || testfixture_subset(&arena, &t, subset));
        if (ok)
        {
            printf("test-sqlite-testfixture: SQLite's Tcl tests (%s) by %u builds' testfixture, %s jobs each, in %s\n", subset != NULL ? subset : "the full permutation",
                   BUSTER_SQLITE_TEST_BUILDS, t.jobs, t.directory);
        }
        for (uint32_t b = 0; ok && b < BUSTER_SQLITE_TEST_BUILDS; b += 1)
        {
            ok = testfixture_build(&arena, &t, b);
        }
        for (uint32_t b = 0; ok && b < BUSTER_SQLITE_TEST_BUILDS; b += 1)
        {
            ok = testfixture_run(&arena, &t, b);
        }
        TestfixtureJobs jobs[BUSTER_SQLITE_TEST_BUILDS] = { 0 };
        for (uint32_t b = 0; ok && b < BUSTER_SQLITE_TEST_BUILDS; b += 1)
        {
            ok = testfixture_jobs(&arena, &t, b, &jobs[b]);
        }
        uint64_t tests = 0;
        uint32_t failed = ok ? testfixture_reference(&t, &jobs[SQLITE_TEST_CLANG], &tests) : 1;
        for (uint32_t b = SQLITE_TEST_TIER0; ok && failed == 0 && b < BUSTER_SQLITE_TEST_BUILDS; b += 1)
        {
            failed += testfixture_compare(b, &jobs[SQLITE_TEST_CLANG], &jobs[b]);
        }
        ok = ok && failed == 0;
        if (ok)
        {
            printf("test-sqlite-testfixture: %u scripts, %llu tests by clang -O0's testfixture, all passed; Tier 0's and --tier1's are the same\n", jobs[SQLITE_TEST_CLANG].count,
                   (unsigned long long)tests);
        }
        else
        {
            const char* errors = measure_compile_read(&arena, t.errors);
            fprintf(stderr, "test-sqlite-testfixture: FAILED; kept %s; the builds' errors:\n%s\n", t.directory, errors != NULL ? errors : "");
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
