// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The c-testsuite harness (#145), for the test-c-testsuite command:
//
//   build/compiler test-c-testsuite [--subset FILE] [--jobs N] [--timeout SECONDS] [--cross-check] [--keep] DIR
//
// DIR is a checkout of c-testsuite (https://github.com/c-testsuite/c-testsuite) or its tests/single-exec directory. Each
// NAME.c there is a program that must exit with status 0 having written NAME.c.expected's bytes to its standard output
// and errors. The harness compiles and links each with this executable, `compiler --verify NAME.c -o NAME` (the direct
// path, its vector paths, every function's records checked by x86_validate), and again with --scalar (the scalar
// twins): the two executables must be the same bytes, or the two failures the same diagnostics. Then it runs the program
// in a directory of its own (some tests write files), with standard input from /dev/null and a timeout.
//
// A test passes when its status and output are the expected ones. One that the compiler refuses with a diagnostic that
// names a feature it doesn't support yet ("unsupported: ...") is unsupported, with that feature. Anything else fails:
// another compile error, a crash or a timeout of the compiler or the program, a wrong status or output, the two paths
// disagreeing. With --cross-check (which needs readelf and cc), each test is also compiled with -c, its object listed by
// readelf, linked by cc with -lm and run, and that program must give the expected output too, whatever the direct path
// did: wrong code in a test the direct path can't link still fails.
//
// --subset FILE lists the tests that must pass (#145's declared subset, tests/c-testsuite/subset.txt): a NAME a line,
// with blank lines and comments from `#`. The exit status is 1 if a test fails, if a test of the subset doesn't pass or
// isn't in DIR, or if the harness can't run, and 0 otherwise.
//
// The tests run --jobs at a time (by default one per core of the CPUs this process may use), in worker processes that
// write their outcomes into a shared mapping; the report lists every test in name order, then the counts. This is test
// tooling outside the compile path, with no vector path.
//
// main.c includes this file after parallel.c, whose parallel_core_count it takes. Layout: the outcomes (CTestsuite,
// CTestsuiteResult), the processes (c_testsuite_run, c_testsuite_read, c_testsuite_same), a test (c_testsuite_feature,
// c_testsuite_compiled, c_testsuite_cross_check, c_testsuite_one), the tests' list (c_testsuite_find, c_testsuite_subset),
// cleanup (c_testsuite_remove) and the command (run_c_testsuite_tests).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

// OS-dependent includes
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

// The most tests a directory may have, and the longest name.
#define BUSTER_C_TESTSUITE_MAX_TESTS 4096
#define BUSTER_C_TESTSUITE_NAME_BYTES 64
// The most bytes of a file the harness compares or reads a diagnostic from.
#define BUSTER_C_TESTSUITE_FILE_BYTES (UINT64_C(64) << 20)
// A compile's time limit; a program's is --timeout.
#define BUSTER_C_TESTSUITE_COMPILE_SECONDS 120

typedef enum CTestsuiteOutcome
{
    C_TESTSUITE_NOT_RUN,
    C_TESTSUITE_PASSED,
    C_TESTSUITE_UNSUPPORTED,
    C_TESTSUITE_FAILED,
} CTestsuiteOutcome;

// A test's outcome, in the mapping the workers share: and for one that didn't pass, why (the feature, or the failure).
typedef struct CTestsuiteResult CTestsuiteResult;
struct CTestsuiteResult
{
    uint8_t outcome;
    bool subset;
    char detail[256];
};

typedef struct CTestsuite CTestsuite;
struct CTestsuite
{
    // The tests' directory, the work directory and this executable.
    char tests[PATH_MAX];
    char work[PATH_MAX];
    char compiler[PATH_MAX];
    char (*names)[BUSTER_C_TESTSUITE_NAME_BYTES];
    uint32_t count;
    uint32_t timeout;
    bool cross_check;
    CTestsuiteResult* results;
};

// ---------------------------------------------------------------------------------------------------------------------
// The processes

// Runs `arguments` (arguments[0] a path, or a name PATH has) in `directory`, its standard input from /dev/null and its
// standard output and errors into the file `output`, in a process group of its own that is killed after `seconds`: its
// wait status, or -1 if it couldn't start or be waited for, with *timed_out set when it was killed for its time.
static int c_testsuite_run(char* const* arguments, const char* directory, const char* output, uint32_t seconds, bool* timed_out)
{
    *timed_out = false;
    int status = -1;
    const pid_t child = fork();
    if (child == 0)
    {
        setpgid(0, 0);
        const int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
        const int capture = open(output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        const bool ready = input >= 0 && capture >= 0 && chdir(directory) == 0 && dup2(input, 0) == 0 && dup2(capture, 1) == 1 && dup2(capture, 2) == 2;
        if (ready)
        {
            execvp(arguments[0], arguments);
        }
        _exit(127);
    }
    else if (child > 0)
    {
        // A pidfd becomes readable when the child ends, so that poll is the timeout.
        const int descriptor = (int)syscall(SYS_pidfd_open, child, 0);
        if (descriptor >= 0)
        {
            struct pollfd poller = { .fd = descriptor, .events = POLLIN };
            int ready = -1;
            do
            {
                ready = poll(&poller, 1, (int)(seconds > INT32_MAX / 1000 ? INT32_MAX : seconds * 1000));
            }
            while (ready < 0 && errno == EINTR);
            if (ready == 0)
            {
                kill(-child, SIGKILL);
                kill(child, SIGKILL);
                *timed_out = true;
            }
            close(descriptor);
        }
        pid_t waited = -1;
        do
        {
            waited = waitpid(child, &status, 0);
        }
        while (waited < 0 && errno == EINTR);
        // Whatever the child left running in its group goes with it.
        kill(-child, SIGKILL);
        status = waited == child ? status : -1;
    }
    return status;
}

// The file at `path` (at most BUSTER_C_TESTSUITE_FILE_BYTES of it, NUL-terminated) in memory that the caller frees, or
// null if it can't be read.
static char* c_testsuite_read(const char* path, uint64_t* length)
{
    *length = 0;
    char* bytes = 0;
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    struct stat stats;
    if (descriptor >= 0 && fstat(descriptor, &stats) == 0 && S_ISREG(stats.st_mode) && (uint64_t)stats.st_size <= BUSTER_C_TESTSUITE_FILE_BYTES)
    {
        bytes = malloc((uint64_t)stats.st_size + 1);
        uint64_t read_bytes = 0;
        bool reading = bytes != 0;
        while (reading && read_bytes < (uint64_t)stats.st_size)
        {
            const ssize_t got = read(descriptor, bytes + read_bytes, (uint64_t)stats.st_size - read_bytes);
            reading = got > 0 || (got < 0 && errno == EINTR);
            read_bytes += got > 0 ? (uint64_t)got : 0;
        }
        if (bytes != 0 && read_bytes == (uint64_t)stats.st_size)
        {
            bytes[read_bytes] = 0;
            *length = read_bytes;
        }
        else
        {
            free(bytes);
            bytes = 0;
        }
    }
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    return bytes;
}

// Whether the files at `a` and `b` can both be read and have the same bytes.
static bool c_testsuite_same(const char* a, const char* b)
{
    uint64_t a_length = 0;
    uint64_t b_length = 0;
    char* a_bytes = c_testsuite_read(a, &a_length);
    char* b_bytes = c_testsuite_read(b, &b_length);
    const bool same = a_bytes != 0 && b_bytes != 0 && a_length == b_length && memcmp(a_bytes, b_bytes, a_length) == 0;
    free(a_bytes);
    free(b_bytes);
    return same;
}

// ---------------------------------------------------------------------------------------------------------------------
// A test

// The feature an "unsupported: " diagnostic names in `log`, copied into `detail` up to its line's end: whether there
// is one.
static bool c_testsuite_feature(const char* log, char* detail, uint64_t capacity)
{
    static const char marker[] = "unsupported: ";
    const char* found = log != 0 ? strstr(log, marker) : 0;
    if (found != 0)
    {
        const char* feature = found + sizeof(marker) - 1;
        const char* end = strchr(feature, '\n');
        const int length = (int)(end != 0 ? end - feature : (int64_t)strlen(feature));
        snprintf(detail, capacity, "%.*s", length, feature);
    }
    return found != 0;
}

// What a compile's wait status and log say, for a failure: whether it compiled, and if not why, in `detail`.
static bool c_testsuite_compiled(int status, bool timed_out, const char* log_path, const char* what, char* detail, uint64_t capacity)
{
    const bool compiled = status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!compiled)
    {
        uint64_t length = 0;
        char* log = c_testsuite_read(log_path, &length);
        const char* error = log != 0 ? strstr(log, "error") : 0;
        const char* end = error != 0 ? strchr(error, '\n') : 0;
        if (timed_out)
        {
            snprintf(detail, capacity, "%s: timed out", what);
        }
        else if (status >= 0 && WIFSIGNALED(status))
        {
            snprintf(detail, capacity, "%s: killed by signal %d", what, WTERMSIG(status));
        }
        else if (error != 0)
        {
            snprintf(detail, capacity, "%s: %.*s", what, (int)(end != 0 ? end - error : (int64_t)strlen(error)), error);
        }
        else
        {
            snprintf(detail, capacity, "%s: failed without a diagnostic (status %d)", what, status);
        }
        free(log);
    }
    return compiled;
}

// Whether the program at `program` exits with status 0 having written `expected`'s bytes, run in `directory` with its
// output into `output`; if not, why, in `detail`.
static bool c_testsuite_ran(const CTestsuite* suite, const char* program, const char* directory, const char* output, const char* expected,
                            const char* what, char* detail, uint64_t capacity)
{
    char* const arguments[] = { (char*)program, 0 };
    bool timed_out = false;
    const int status = c_testsuite_run(arguments, directory, output, suite->timeout, &timed_out);
    bool passed = false;
    if (timed_out)
    {
        snprintf(detail, capacity, "%s: timed out after %u s", what, suite->timeout);
    }
    else if (status < 0)
    {
        snprintf(detail, capacity, "%s: couldn't run", what);
    }
    else if (WIFSIGNALED(status))
    {
        snprintf(detail, capacity, "%s: killed by signal %d", what, WTERMSIG(status));
    }
    else if (WEXITSTATUS(status) != 0)
    {
        snprintf(detail, capacity, "%s: exited with status %d", what, WEXITSTATUS(status));
    }
    else if (!c_testsuite_same(output, expected))
    {
        snprintf(detail, capacity, "%s: its output isn't the expected one", what);
    }
    else
    {
        passed = true;
    }
    return passed;
}

// --cross-check's half of a test: its object from -c, listed by readelf, linked by cc and run. Whether it gave the
// expected output; if not, why, in `detail`.
static bool c_testsuite_cross_check(const CTestsuite* suite, const char* source, const char* directory, const char* expected, char* detail, uint64_t capacity)
{
    char object[PATH_MAX];
    char program[PATH_MAX];
    char log[PATH_MAX];
    char output[PATH_MAX];
    snprintf(object, sizeof(object), "%s/object.o", directory);
    snprintf(program, sizeof(program), "%s/program-cc", directory);
    snprintf(log, sizeof(log), "%s/object.txt", directory);
    snprintf(output, sizeof(output), "%s/output-cc.txt", directory);
    char* const compile[] = { (char*)suite->compiler, "--verify", "-c", (char*)source, "-o", object, 0 };
    char* const readelf[] = { "readelf", "-a", "-W", object, 0 };
    char* const link[] = { "cc", object, "-lm", "-o", program, 0 };
    bool timed_out = false;
    int status = c_testsuite_run(compile, directory, log, BUSTER_C_TESTSUITE_COMPILE_SECONDS, &timed_out);
    // A test the compiler refuses has no object to check.
    bool valid = c_testsuite_compiled(status, timed_out, log, "-c", detail, capacity);
    uint64_t length = 0;
    char* text = valid ? 0 : c_testsuite_read(log, &length);
    char feature[sizeof(((CTestsuiteResult*)0)->detail)];
    const bool unsupported = !valid && c_testsuite_feature(text, feature, sizeof(feature));
    free(text);
    if (valid)
    {
        status = c_testsuite_run(readelf, directory, log, BUSTER_C_TESTSUITE_COMPILE_SECONDS, &timed_out);
        text = c_testsuite_read(log, &length);
        valid = status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 && text != 0 && strstr(text, "Warning") == 0 && strstr(text, "Error") == 0;
        free(text);
        if (!valid)
        {
            snprintf(detail, capacity, "cross-check: readelf found the object malformed");
        }
    }
    if (valid)
    {
        status = c_testsuite_run(link, directory, log, BUSTER_C_TESTSUITE_COMPILE_SECONDS, &timed_out);
        valid = c_testsuite_compiled(status, timed_out, log, "cross-check: cc", detail, capacity);
    }
    if (valid)
    {
        valid = c_testsuite_ran(suite, program, directory, output, expected, "cross-check: cc's program", detail, capacity);
    }
    return valid || unsupported;
}

// Test `index`: compiled by both paths, run, and with --cross-check linked by cc too, into its result.
static void c_testsuite_one(const CTestsuite* suite, uint32_t index)
{
    CTestsuiteResult* result = &suite->results[index];
    const char* name = suite->names[index];
    char source[PATH_MAX];
    char expected[PATH_MAX];
    char directory[PATH_MAX];
    char program[PATH_MAX];
    char scalar[PATH_MAX];
    char log[PATH_MAX];
    char scalar_log[PATH_MAX];
    char output[PATH_MAX];
    snprintf(source, sizeof(source), "%s/%s.c", suite->tests, name);
    snprintf(expected, sizeof(expected), "%s/%s.c.expected", suite->tests, name);
    snprintf(directory, sizeof(directory), "%s/%s", suite->work, name);
    snprintf(program, sizeof(program), "%s/program", directory);
    snprintf(scalar, sizeof(scalar), "%s/program-scalar", directory);
    snprintf(log, sizeof(log), "%s/compile.txt", directory);
    snprintf(scalar_log, sizeof(scalar_log), "%s/compile-scalar.txt", directory);
    snprintf(output, sizeof(output), "%s/output.txt", directory);
    char* const vector_arguments[] = { (char*)suite->compiler, "--verify", source, "-o", program, 0 };
    char* const scalar_arguments[] = { (char*)suite->compiler, "--verify", "--scalar", source, "-o", scalar, 0 };
    result->outcome = C_TESTSUITE_FAILED;
    bool valid = mkdir(directory, 0755) == 0;
    if (!valid)
    {
        snprintf(result->detail, sizeof(result->detail), "can't make its directory: %s", strerror(errno));
    }
    bool timed_out = false;
    bool scalar_timed_out = false;
    const int status = valid ? c_testsuite_run(vector_arguments, directory, log, BUSTER_C_TESTSUITE_COMPILE_SECONDS, &timed_out) : -1;
    const int scalar_status = valid ? c_testsuite_run(scalar_arguments, directory, scalar_log, BUSTER_C_TESTSUITE_COMPILE_SECONDS, &scalar_timed_out) : -1;
    const bool compiled = valid && c_testsuite_compiled(status, timed_out, log, "the compiler", result->detail, sizeof(result->detail));
    char scalar_detail[sizeof(result->detail)];
    const bool scalar_compiled = valid && c_testsuite_compiled(scalar_status, scalar_timed_out, scalar_log, "the compiler, --scalar", scalar_detail,
                                                                sizeof(scalar_detail));
    if (valid && compiled != scalar_compiled)
    {
        snprintf(result->detail, sizeof(result->detail), "the vector path %s and the scalar path %s", compiled ? "compiles it" : "refuses it",
                 scalar_compiled ? "compiles it" : "refuses it");
        valid = false;
    }
    else if (valid && compiled && !c_testsuite_same(program, scalar))
    {
        snprintf(result->detail, sizeof(result->detail), "the vector and scalar paths write different executables");
        valid = false;
    }
    else if (valid && !compiled && !c_testsuite_same(log, scalar_log))
    {
        snprintf(result->detail, sizeof(result->detail), "the vector and scalar paths give different diagnostics");
        valid = false;
    }
    if (valid && compiled)
    {
        valid = c_testsuite_ran(suite, program, directory, output, expected, "the program", result->detail, sizeof(result->detail));
        result->outcome = valid ? C_TESTSUITE_PASSED : C_TESTSUITE_FAILED;
    }
    else if (valid)
    {
        uint64_t length = 0;
        char* text = c_testsuite_read(log, &length);
        // A refusal that names its feature is unsupported; any other is a failure, its detail as set above.
        if (c_testsuite_feature(text, result->detail, sizeof(result->detail)))
        {
            result->outcome = C_TESTSUITE_UNSUPPORTED;
        }
        free(text);
    }
    char cross_detail[sizeof(result->detail)];
    if (suite->cross_check && result->outcome != C_TESTSUITE_FAILED && !c_testsuite_cross_check(suite, source, directory, expected, cross_detail, sizeof(cross_detail)))
    {
        result->outcome = C_TESTSUITE_FAILED;
        snprintf(result->detail, sizeof(result->detail), "%s", cross_detail);
    }
    if (result->outcome == C_TESTSUITE_PASSED)
    {
        result->detail[0] = 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The tests' list

static int c_testsuite_compare_names(const void* a, const void* b)
{
    return strcmp(a, b);
}

// The NAME.c files in the tests' directory with a NAME.c.expected beside them, sorted: their count, or UINT32_MAX if the
// directory can't be read or has more than the harness takes.
static uint32_t c_testsuite_find(CTestsuite* suite)
{
    uint32_t count = 0;
    bool valid = true;
    DIR* directory = opendir(suite->tests);
    struct dirent* entry = directory != 0 ? readdir(directory) : 0;
    while (entry != 0 && valid)
    {
        const uint64_t length = strlen(entry->d_name);
        char expected[PATH_MAX];
        struct stat stats;
        if (length > 2 && length - 2 < BUSTER_C_TESTSUITE_NAME_BYTES && memcmp(entry->d_name + length - 2, ".c", 2) == 0)
        {
            snprintf(expected, sizeof(expected), "%s/%s.expected", suite->tests, entry->d_name);
            if (stat(expected, &stats) == 0)
            {
                valid = count < BUSTER_C_TESTSUITE_MAX_TESTS;
                if (valid)
                {
                    snprintf(suite->names[count], BUSTER_C_TESTSUITE_NAME_BYTES, "%.*s", (int)(length - 2), entry->d_name);
                    count += 1;
                }
            }
        }
        entry = readdir(directory);
    }
    if (directory != 0)
    {
        closedir(directory);
    }
    qsort(suite->names, count, BUSTER_C_TESTSUITE_NAME_BYTES, c_testsuite_compare_names);
    return directory != 0 && valid ? count : UINT32_MAX;
}

// Marks the tests the subset file at `path` names: whether it could be read and every name it has is a test.
static bool c_testsuite_subset(CTestsuite* suite, const char* path)
{
    uint64_t length = 0;
    char* text = c_testsuite_read(path, &length);
    bool valid = text != 0;
    if (!valid)
    {
        fprintf(stderr, "test-c-testsuite: can't read the subset %s\n", path);
    }
    char* line = text;
    while (valid && line != 0 && *line != 0)
    {
        char* end = strchr(line, '\n');
        char* next = end != 0 ? end + 1 : 0;
        char* comment = strchr(line, '#');
        if (end != 0)
        {
            *end = 0;
        }
        if (comment != 0 && (end == 0 || comment < end))
        {
            *comment = 0;
        }
        const char* begin = line;
        while (*begin == ' ' || *begin == '\t' || *begin == '\r')
        {
            begin += 1;
        }
        uint64_t size = strlen(begin);
        while (size != 0 && (begin[size - 1] == ' ' || begin[size - 1] == '\t' || begin[size - 1] == '\r'))
        {
            size -= 1;
        }
        bool found = size == 0;
        for (uint32_t i = 0; !found && i < suite->count; i += 1)
        {
            found = strlen(suite->names[i]) == size && memcmp(suite->names[i], begin, size) == 0;
            suite->results[i].subset |= found;
        }
        if (!found)
        {
            fprintf(stderr, "test-c-testsuite: the subset names %.*s, which isn't a test of %s\n", (int)size, begin, suite->tests);
            valid = false;
        }
        line = next;
    }
    free(text);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// Cleanup

// Removes the work directory: each test's directory, and the files in it (the tests' own among them), one level deep.
static void c_testsuite_remove(const CTestsuite* suite)
{
    for (uint32_t i = 0; i < suite->count; i += 1)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", suite->work, suite->names[i]);
        DIR* directory = opendir(path);
        struct dirent* entry = directory != 0 ? readdir(directory) : 0;
        while (entry != 0)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                char file[PATH_MAX];
                snprintf(file, sizeof(file), "%s/%s", path, entry->d_name);
                unlink(file);
            }
            entry = readdir(directory);
        }
        if (directory != 0)
        {
            closedir(directory);
        }
        rmdir(path);
    }
    if (rmdir(suite->work) != 0)
    {
        fprintf(stderr, "test-c-testsuite: couldn't remove %s (a test left a directory in it)\n", suite->work);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static int run_c_testsuite_tests(int argc, char** argv)
{
    static CTestsuite suite;
    const char* subset = 0;
    const char* root = 0;
    uint64_t jobs = 0;
    bool keep = false;
    bool valid = true;
    suite.timeout = 10;
    for (int i = 0; valid && i < argc; i += 1)
    {
        char* end = 0;
        if (strcmp(argv[i], "--subset") == 0 && i + 1 < argc)
        {
            subset = argv[++i];
        }
        else if (strcmp(argv[i], "--jobs") == 0 && i + 1 < argc)
        {
            jobs = strtoull(argv[++i], &end, 10);
            valid = *end == 0 && jobs != 0 && jobs <= 1024;
        }
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc)
        {
            const uint64_t seconds = strtoull(argv[++i], &end, 10);
            valid = *end == 0 && seconds != 0 && seconds <= 3600;
            suite.timeout = (uint32_t)seconds;
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            suite.cross_check = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else if (argv[i][0] != '-' && root == 0)
        {
            root = argv[i];
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid || root == 0)
    {
        fprintf(stderr, "usage: compiler test-c-testsuite [--subset FILE] [--jobs N] [--timeout SECONDS] [--cross-check] [--keep] DIR\n");
    }
    else
    {
        // DIR may be the checkout or its tests/single-exec.
        char nested[PATH_MAX];
        struct stat stats;
        snprintf(nested, sizeof(nested), "%s/tests/single-exec", root);
        snprintf(suite.tests, sizeof(suite.tests), "%s", stat(nested, &stats) == 0 && S_ISDIR(stats.st_mode) ? nested : root);
        const ssize_t self = readlink("/proc/self/exe", suite.compiler, sizeof(suite.compiler) - 1);
        const char* temporary = getenv("TMPDIR") != 0 && getenv("TMPDIR")[0] != 0 ? getenv("TMPDIR") : "/tmp";
        snprintf(suite.work, sizeof(suite.work), "%s/buster-c-testsuite-XXXXXX", temporary);
        suite.names = malloc((uint64_t)BUSTER_C_TESTSUITE_MAX_TESTS * BUSTER_C_TESTSUITE_NAME_BYTES);
        suite.results = mmap(0, BUSTER_C_TESTSUITE_MAX_TESTS * sizeof(CTestsuiteResult), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        valid = self > 0 && suite.names != 0 && suite.results != MAP_FAILED;
        if (valid)
        {
            suite.compiler[self] = 0;
            suite.count = c_testsuite_find(&suite);
            valid = suite.count != UINT32_MAX && suite.count != 0;
            if (!valid)
            {
                fprintf(stderr, "test-c-testsuite: no tests in %s\n", suite.tests);
            }
        }
        valid = valid && (subset == 0 || c_testsuite_subset(&suite, subset));
        valid = valid && mkdtemp(suite.work) != 0;
        if (valid)
        {
            // Workers take the tests in turn: worker w the tests w, w + jobs, ...
            const uint64_t workers = jobs != 0 ? jobs : parallel_core_count();
            pid_t* children = malloc((workers != 0 ? workers : 1) * sizeof(pid_t));
            uint64_t started = 0;
            for (uint64_t w = 0; children != 0 && w < workers && w < suite.count; w += 1)
            {
                const pid_t child = fork();
                if (child == 0)
                {
                    for (uint32_t i = (uint32_t)w; i < suite.count; i += (uint32_t)workers)
                    {
                        c_testsuite_one(&suite, i);
                    }
                    _exit(0);
                }
                children[started] = child;
                started += child > 0;
            }
            for (uint64_t w = 0; w < started; w += 1)
            {
                int status = 0;
                pid_t waited = -1;
                do
                {
                    waited = waitpid(children[w], &status, 0);
                }
                while (waited < 0 && errno == EINTR);
            }
            free(children);
            uint32_t counts[C_TESTSUITE_FAILED + 1] = { 0 };
            uint32_t subset_count = 0;
            uint32_t subset_passed = 0;
            for (uint32_t i = 0; i < suite.count; i += 1)
            {
                const CTestsuiteResult* test = &suite.results[i];
                const char* outcome = test->outcome == C_TESTSUITE_PASSED ? "passed" : test->outcome == C_TESTSUITE_UNSUPPORTED ? "unsupported" :
                                      test->outcome == C_TESTSUITE_FAILED ? "failed" : "not run";
                const bool missed = test->subset && test->outcome != C_TESTSUITE_PASSED;
                printf("%s %s%s%s%s\n", suite.names[i], outcome, test->detail[0] != 0 ? ": " : "", test->detail, missed ? " (in the subset)" : "");
                counts[test->outcome] += 1;
                subset_count += test->subset;
                subset_passed += test->subset && test->outcome == C_TESTSUITE_PASSED;
            }
            const bool passed = counts[C_TESTSUITE_FAILED] == 0 && counts[C_TESTSUITE_NOT_RUN] == 0 && subset_passed == subset_count;
            printf("test-c-testsuite: %u tests: %u passed, %u unsupported, %u failed, %u not run", suite.count, counts[C_TESTSUITE_PASSED],
                   counts[C_TESTSUITE_UNSUPPORTED], counts[C_TESTSUITE_FAILED], counts[C_TESTSUITE_NOT_RUN]);
            if (subset != 0)
            {
                printf("; the subset's %u: %u passed", subset_count, subset_passed);
            }
            printf("\n");
            result = passed ? 0 : 1;
            if (keep || !passed)
            {
                printf("test-c-testsuite: the tests' files are in %s\n", suite.work);
            }
            else
            {
                c_testsuite_remove(&suite);
            }
        }
        else
        {
            fprintf(stderr, "test-c-testsuite: can't run the tests\n");
        }
    }
    return result;
}
