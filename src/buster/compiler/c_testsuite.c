// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The c-testsuite harness (#145), for the test-c-testsuite command:
//
//   build/compiler test-c-testsuite [--subset FILE] [--jobs N] [--timeout SECONDS] [--compiler PATH] [--cross-check]
//                                   [--keep] DIR
//   build/compiler test-c-testsuite --self-test
//
// DIR is a checkout of c-testsuite (https://github.com/c-testsuite/c-testsuite) or its tests/single-exec directory. Each
// NAME.c there is a program that must exit with status 0 having written NAME.c.expected's bytes to its standard output
// and errors. The harness compiles and links each with this executable (or --compiler's), `compiler --verify NAME.c -o
// NAME` (the direct path, its vector paths, every function's records checked by x86_validate), and again with --scalar
// (the scalar twins): the two executables must be the same bytes, or the two failures the same diagnostics. Then it runs
// the program in a directory of its own (some tests write files), with standard input from /dev/null and a timeout.
//
// A test passes when its status and output are the expected ones. One that the compiler refuses with a diagnostic that
// names a feature it doesn't support yet ("unsupported: ...") is unsupported, with that feature, when both paths exit as
// the compiler does for a unit with errors (status 1). Anything else fails: another compile error, a crash or a timeout
// of the compiler (even after it named a feature) or of the program, a wrong status or output, the two paths
// disagreeing. With --cross-check (which needs readelf and cc), each test is also compiled with -c, its object listed by
// readelf, linked by cc with -lm and run, and that program must give the expected output too, whatever the direct path
// did: wrong code in a test the direct path can't link still fails. -c may refuse a test as unsupported, exiting as a
// refusal does, only where the direct path refused it too.
//
// --self-test checks the harness itself, on tests of its own and a stand-in compiler (c_testsuite_self_test).
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
// cleanup (c_testsuite_remove), the run (c_testsuite_execute, c_testsuite_report), the self-test (c_testsuite_self_test)
// and the command (run_c_testsuite_tests).

#include <stdint.h>
#include <inttypes.h>
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
#include <time.h>

// The most tests a directory may have, and the longest name.
#define BUSTER_C_TESTSUITE_MAX_TESTS 4096
#define BUSTER_C_TESTSUITE_NAME_BYTES 64
// The most bytes of a file the harness compares or reads a diagnostic from.
#define BUSTER_C_TESTSUITE_FILE_BYTES (UINT64_C(64) << 20)
// A compile's time limit (CTestsuite.compile_timeout, which the self-test shortens); a program's is --timeout.
#define BUSTER_C_TESTSUITE_COMPILE_SECONDS 120
// The compiler's exit status for a unit with errors, a refusal among them.
#define BUSTER_C_TESTSUITE_REFUSED_STATUS 1

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
    // A program's time limit and a compile's, in seconds.
    uint32_t timeout;
    uint32_t compile_timeout;
    bool cross_check;
    CTestsuiteResult* results;
};

// ---------------------------------------------------------------------------------------------------------------------
// The processes

// The self-test's switch: wait for every process as the harness does where it has no pidfd.
static bool c_testsuite_without_pidfd;

// Nanoseconds on the monotonic clock.
static uint64_t c_testsuite_now(void)
{
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
}

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
        // A pidfd becomes readable when the child ends, so that poll is the timeout. Without one (a kernel before 5.3, a
        // seccomp filter, no descriptor to spare) or a poll that fails, waitpid without blocking, every millisecond up to
        // the deadline.
        const int descriptor = c_testsuite_without_pidfd ? -1 : (int)syscall(SYS_pidfd_open, child, 0);
        bool polled = false;
        pid_t waited = 0;
        if (descriptor >= 0)
        {
            struct pollfd poller = { .fd = descriptor, .events = POLLIN };
            int ready = -1;
            do
            {
                ready = poll(&poller, 1, (int)(seconds > INT32_MAX / 1000 ? INT32_MAX : seconds * 1000));
            }
            while (ready < 0 && errno == EINTR);
            polled = ready >= 0;
            *timed_out = ready == 0;
            close(descriptor);
        }
        const uint64_t deadline = c_testsuite_now() + (uint64_t)seconds * 1000000000;
        while (!polled && waited == 0 && !*timed_out)
        {
            waited = waitpid(child, &status, WNOHANG);
            waited = waited < 0 && errno == EINTR ? 0 : waited;
            *timed_out = waited == 0 && c_testsuite_now() >= deadline;
            if (waited == 0 && !*timed_out)
            {
                const struct timespec pause = { .tv_nsec = 1000000 };
                nanosleep(&pause, 0);
            }
        }
        if (*timed_out)
        {
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
        }
        while (waited == 0)
        {
            waited = waitpid(child, &status, 0);
            waited = waited < 0 && errno == EINTR ? 0 : waited;
        }
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

// Whether a compile's wait status is a refusal's: the compiler exited as it does for a unit with errors, not killed by a
// signal or for its time, whatever its log says.
static bool c_testsuite_refused(int status, bool timed_out)
{
    return !timed_out && status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == BUSTER_C_TESTSUITE_REFUSED_STATUS;
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
// expected output; if not, why, in `detail`. -c may refuse a test as unsupported only where the direct path did too:
// when that compiled it (`required`), -c must compile it as well.
static bool c_testsuite_cross_check(const CTestsuite* suite, const char* source, const char* directory, const char* expected, bool required,
                                    char* detail, uint64_t capacity)
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
    int status = c_testsuite_run(compile, directory, log, suite->compile_timeout, &timed_out);
    // A test the compiler refuses has no object to check. A refusal is one only when -c exits as the compiler does
    // for a unit with errors: after a signal or a timeout, its log's feature is no excuse.
    bool valid = c_testsuite_compiled(status, timed_out, log, "-c", detail, capacity);
    uint64_t length = 0;
    char* text = !valid && c_testsuite_refused(status, timed_out) ? c_testsuite_read(log, &length) : 0;
    char feature[sizeof(((CTestsuiteResult*)0)->detail)];
    const bool unsupported = text != 0 && c_testsuite_feature(text, feature, sizeof(feature));
    free(text);
    if (unsupported && required)
    {
        snprintf(detail, capacity, "cross-check: -c refuses what the direct path compiles: %s", feature);
    }
    if (valid)
    {
        status = c_testsuite_run(readelf, directory, log, suite->compile_timeout, &timed_out);
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
        status = c_testsuite_run(link, directory, log, suite->compile_timeout, &timed_out);
        valid = c_testsuite_compiled(status, timed_out, log, "cross-check: cc", detail, capacity);
    }
    if (valid)
    {
        valid = c_testsuite_ran(suite, program, directory, output, expected, "cross-check: cc's program", detail, capacity);
    }
    return valid || (unsupported && !required);
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
    const int status = valid ? c_testsuite_run(vector_arguments, directory, log, suite->compile_timeout, &timed_out) : -1;
    const int scalar_status = valid ? c_testsuite_run(scalar_arguments, directory, scalar_log, suite->compile_timeout, &scalar_timed_out) : -1;
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
        // A refusal that names its feature is unsupported when both paths exited as the compiler does for a unit with
        // errors. Any other is a failure, its detail as set above: the vector path's, or the scalar path's when only it
        // was killed by a signal or for its time, though it wrote the same log.
        const bool refused = c_testsuite_refused(status, timed_out);
        const bool scalar_refused = c_testsuite_refused(scalar_status, scalar_timed_out);
        uint64_t length = 0;
        char* text = refused && scalar_refused ? c_testsuite_read(log, &length) : 0;
        if (refused && !scalar_refused)
        {
            snprintf(result->detail, sizeof(result->detail), "%s", scalar_detail);
        }
        else if (text != 0 && c_testsuite_feature(text, result->detail, sizeof(result->detail)))
        {
            result->outcome = C_TESTSUITE_UNSUPPORTED;
        }
        free(text);
    }
    char cross_detail[sizeof(result->detail)];
    if (suite->cross_check && result->outcome != C_TESTSUITE_FAILED &&
        !c_testsuite_cross_check(suite, source, directory, expected, result->outcome == C_TESTSUITE_PASSED, cross_detail, sizeof(cross_detail)))
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

// Removes the directory at `path` and the files in it, one level deep: whether it could.
static bool c_testsuite_remove_directory(const char* path)
{
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
    return rmdir(path) == 0;
}

// Removes the work directory: each test's directory and the files in it (the tests' own among them).
static void c_testsuite_remove(const CTestsuite* suite)
{
    for (uint32_t i = 0; i < suite->count; i += 1)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", suite->work, suite->names[i]);
        c_testsuite_remove_directory(path);
    }
    if (rmdir(suite->work) != 0)
    {
        fprintf(stderr, "test-c-testsuite: couldn't remove %s (a test left a directory in it)\n", suite->work);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The run

// Finds the tests of `root` (the checkout or its tests/single-exec, relative to the current directory or not), marks
// the subset's (from the file at `subset`, if any) and runs them on `jobs` workers (0: one per core) into
// suite->results, in a new work directory under $TMPDIR (or /tmp). A test's processes run in a directory of their own,
// so the paths they get must be absolute: the tests' directory and the work directory are made so here. Whether the
// tests could run.
static bool c_testsuite_execute(CTestsuite* suite, const char* root, const char* subset, uint64_t jobs)
{
    char nested[PATH_MAX];
    char work[PATH_MAX];
    struct stat stats;
    snprintf(nested, sizeof(nested), "%s/tests/single-exec", root);
    const char* temporary = getenv("TMPDIR") != 0 && getenv("TMPDIR")[0] != 0 ? getenv("TMPDIR") : "/tmp";
    snprintf(work, sizeof(work), "%s/buster-c-testsuite-XXXXXX", temporary);
    memset(suite->results, 0, BUSTER_C_TESTSUITE_MAX_TESTS * sizeof(CTestsuiteResult));
    suite->count = 0;
    bool valid = realpath(stat(nested, &stats) == 0 && S_ISDIR(stats.st_mode) ? nested : root, suite->tests) != 0;
    if (valid)
    {
        suite->count = c_testsuite_find(suite);
        valid = suite->count != UINT32_MAX && suite->count != 0;
    }
    if (!valid)
    {
        fprintf(stderr, "test-c-testsuite: no tests in %s\n", root);
        suite->count = 0;
    }
    valid = valid && (subset == 0 || c_testsuite_subset(suite, subset));
    valid = valid && mkdtemp(work) != 0 && realpath(work, suite->work) != 0;
    if (valid)
    {
        // Workers take the tests in turn: worker w the tests w, w + jobs, ...
        const uint64_t workers = jobs != 0 ? jobs : parallel_core_count();
        pid_t* children = malloc((workers != 0 ? workers : 1) * sizeof(pid_t));
        uint64_t started = 0;
        for (uint64_t w = 0; children != 0 && w < workers && w < suite->count; w += 1)
        {
            const pid_t child = fork();
            if (child == 0)
            {
                for (uint32_t i = (uint32_t)w; i < suite->count; i += (uint32_t)workers)
                {
                    c_testsuite_one(suite, i);
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
    }
    return valid;
}

// Prints each test's outcome in name order, then the counts: whether no test failed or wasn't run, and (`subset`) the
// subset's all passed.
static bool c_testsuite_report(const CTestsuite* suite, bool subset)
{
    uint32_t counts[C_TESTSUITE_FAILED + 1] = { 0 };
    uint32_t subset_count = 0;
    uint32_t subset_passed = 0;
    for (uint32_t i = 0; i < suite->count; i += 1)
    {
        const CTestsuiteResult* test = &suite->results[i];
        const char* outcome = test->outcome == C_TESTSUITE_PASSED ? "passed" : test->outcome == C_TESTSUITE_UNSUPPORTED ? "unsupported" :
                              test->outcome == C_TESTSUITE_FAILED ? "failed" : "not run";
        const bool missed = test->subset && test->outcome != C_TESTSUITE_PASSED;
        printf("%s %s%s%s%s\n", suite->names[i], outcome, test->detail[0] != 0 ? ": " : "", test->detail, missed ? " (in the subset)" : "");
        counts[test->outcome] += 1;
        subset_count += test->subset;
        subset_passed += test->subset && test->outcome == C_TESTSUITE_PASSED;
    }
    printf("test-c-testsuite: %u tests: %u passed, %u unsupported, %u failed, %u not run", suite->count, counts[C_TESTSUITE_PASSED],
           counts[C_TESTSUITE_UNSUPPORTED], counts[C_TESTSUITE_FAILED], counts[C_TESTSUITE_NOT_RUN]);
    if (subset)
    {
        printf("; the subset's %u: %u passed", subset_count, subset_passed);
    }
    printf("\n");
    return counts[C_TESTSUITE_FAILED] == 0 && counts[C_TESTSUITE_NOT_RUN] == 0 && subset_passed == subset_count;
}

// ---------------------------------------------------------------------------------------------------------------------
// The harness's own test (--self-test)

// The self-test's stand-in for the compiler, a shell script: it "compiles" a test into a script, after checking that the
// source it was given exists from the test's directory, where it runs. 00001's program prints its expected line, 00002
// is unsupported, and 00003's program doesn't end. With C_TESTSUITE_REFUSE_OBJECTS set, -c refuses every test as
// unsupported, and set to `kill`, it is then killed by a signal. With C_TESTSUITE_ABNORMAL set, every compile refuses
// its test, naming a feature, and then ends otherwise than a refusal does but in 00001's vector path: 00001's scalar path
// and both of 00002's are killed by a signal, and both of 00003's sleep past the compile's time limit.
static const char c_testsuite_stand_in[] =
    "#!/bin/sh\n"
    "object=0 scalar=0 source= output=\n"
    "while [ $# -gt 0 ]; do\n"
    "    case \"$1\" in -c) object=1;; --scalar) scalar=1;; -o) output=$2; shift;; -*) ;; *) source=$1;; esac; shift\n"
    "done\n"
    "[ -f \"$source\" ] || { echo \"error: no such file: $source\" >&2; exit 1; }\n"
    "refuse() { echo \"error: unsupported: $1\" >&2; case \"$2\" in kill) kill -KILL $$;; sleep) exec sleep 30;; esac; exit 1; }\n"
    "if [ -n \"$C_TESTSUITE_ABNORMAL\" ]; then\n"
    "    case \"$source\" in\n"
    "        *00001.c) [ $scalar = 1 ] && refuse 'a stand-in feature' kill; refuse 'a stand-in feature';;\n"
    "        *00002.c) refuse 'a stand-in feature' kill;;\n"
    "        *00003.c) refuse 'a stand-in feature' sleep;;\n"
    "    esac\n"
    "fi\n"
    "if [ $object = 1 ] && [ -n \"$C_TESTSUITE_REFUSE_OBJECTS\" ]; then refuse 'a stand-in object' \"$C_TESTSUITE_REFUSE_OBJECTS\"; fi\n"
    "case \"$source\" in *00002.c) refuse 'a stand-in feature';; esac\n"
    "case \"$source\" in *00003.c) body='exec sleep 30';; *) body='echo passed';; esac\n"
    "printf '#!/bin/sh\\n%s\\n' \"$body\" > \"$output\" && chmod +x \"$output\"\n";

// A self-test's expectation of a test: its outcome and part of its detail.
typedef struct CTestsuiteExpectation CTestsuiteExpectation;
struct CTestsuiteExpectation
{
    uint8_t outcome;
    const char* detail;
};

// Writes `text` to a new file at `path` with `mode`: whether it could.
static bool c_testsuite_write(const char* path, const char* text, mode_t mode)
{
    const int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    const uint64_t length = strlen(text);
    const bool written = descriptor >= 0 && write(descriptor, text, length) == (ssize_t)length;
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    return written;
}

// Whether the self-test's three tests came out as `expected`, printing each that didn't.
static bool c_testsuite_expect(const CTestsuite* suite, const char* scenario, const CTestsuiteExpectation* expected)
{
    bool same = suite->count == 3;
    for (uint32_t i = 0; i < suite->count && i < 3; i += 1)
    {
        const CTestsuiteResult* result = &suite->results[i];
        const bool matches = result->outcome == expected[i].outcome && strstr(result->detail, expected[i].detail) != 0;
        if (!matches)
        {
            fprintf(stderr, "test-c-testsuite: self-test, %s: %s came out %u (%s), not %u (%s)\n", scenario, suite->names[i], result->outcome,
                    result->detail, expected[i].outcome, expected[i].detail);
        }
        same &= matches;
    }
    return same;
}

// The harness on three tests of its own, with the stand-in compiler, in five scenarios: from a DIR and a $TMPDIR relative
// to the current directory; waiting without a pidfd, by polling; with --cross-check where -c refuses what the direct
// path compiles, which must fail; with refusals that name their feature but are then killed by a signal or for their
// time, in one path or both, which must fail, not be unsupported (#377's review); and with --cross-check where -c's
// refusals are killed, which must fail whether or not the direct path compiled the test. 00003's program, and in the
// fourth scenario its compiles, must be killed at their one-second timeouts, long before they would end.
static int c_testsuite_self_test(CTestsuite* suite)
{
    static const char* const directories[] = { "checkout", "checkout/tests", "checkout/tests/single-exec", "work" };
    static const char* const files[][2] = {
        { "checkout/tests/single-exec/00001.c", "int main(void) { return 0; }\n" },
        { "checkout/tests/single-exec/00001.c.expected", "passed\n" },
        { "checkout/tests/single-exec/00002.c", "int main(void) { return 0; }\n" },
        { "checkout/tests/single-exec/00002.c.expected", "" },
        { "checkout/tests/single-exec/00003.c", "int main(void) { for (;;) { } }\n" },
        { "checkout/tests/single-exec/00003.c.expected", "" },
    };
    static const char* const scenarios[] = { "relative paths", "without a pidfd", "cross-check", "killed refusals", "killed -c refusals" };
    static const CTestsuiteExpectation expectations[][3] = {
        {
            { C_TESTSUITE_PASSED, "" },
            { C_TESTSUITE_UNSUPPORTED, "a stand-in feature" },
            { C_TESTSUITE_FAILED, "the program: timed out" },
        },
        {
            { C_TESTSUITE_PASSED, "" },
            { C_TESTSUITE_UNSUPPORTED, "a stand-in feature" },
            { C_TESTSUITE_FAILED, "the program: timed out" },
        },
        {
            { C_TESTSUITE_FAILED, "cross-check: -c refuses what the direct path compiles: a stand-in object" },
            { C_TESTSUITE_UNSUPPORTED, "a stand-in feature" },
            { C_TESTSUITE_FAILED, "the program: timed out" },
        },
        {
            { C_TESTSUITE_FAILED, "the compiler, --scalar: killed by signal 9" },
            { C_TESTSUITE_FAILED, "the compiler: killed by signal 9" },
            { C_TESTSUITE_FAILED, "the compiler: timed out" },
        },
        {
            { C_TESTSUITE_FAILED, "-c: killed by signal 9" },
            { C_TESTSUITE_FAILED, "-c: killed by signal 9" },
            { C_TESTSUITE_FAILED, "the program: timed out" },
        },
    };
    const uint32_t scenario_count = sizeof(scenarios) / sizeof(scenarios[0]);
    char base[PATH_MAX];
    char original[PATH_MAX];
    char path[PATH_MAX];
    const char* temporary = getenv("TMPDIR") != 0 && getenv("TMPDIR")[0] != 0 ? getenv("TMPDIR") : "/tmp";
    snprintf(base, sizeof(base), "%s/buster-c-testsuite-self-XXXXXX", temporary);
    char* saved = getenv("TMPDIR") != 0 ? strdup(getenv("TMPDIR")) : 0;
    bool valid = getcwd(original, sizeof(original)) != 0 && mkdtemp(base) != 0 && realpath(base, path) != 0;
    if (valid)
    {
        snprintf(base, sizeof(base), "%s", path);
    }
    for (uint32_t i = 0; valid && i < sizeof(directories) / sizeof(directories[0]); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", base, directories[i]);
        valid = mkdir(path, 0755) == 0;
    }
    for (uint32_t i = 0; valid && i < sizeof(files) / sizeof(files[0]); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", base, files[i][0]);
        valid = c_testsuite_write(path, files[i][1], 0644);
    }
    snprintf(suite->compiler, sizeof(suite->compiler), "%s/stand-in.sh", base);
    valid = valid && c_testsuite_write(suite->compiler, c_testsuite_stand_in, 0755);
    suite->timeout = 1;
    suite->compile_timeout = 1;
    uint32_t failures = valid ? 0 : 1;
    for (uint32_t scenario = 0; valid && scenario < scenario_count; scenario += 1)
    {
        const bool relative = scenario == 0;
        c_testsuite_without_pidfd = scenario == 1;
        suite->cross_check = scenario == 2 || scenario == 4;
        snprintf(path, sizeof(path), "%s/checkout", base);
        valid = !relative || (chdir(base) == 0 && setenv("TMPDIR", "work", 1) == 0);
        valid = valid && (scenario != 2 || setenv("C_TESTSUITE_REFUSE_OBJECTS", "1", 1) == 0);
        valid = valid && (scenario != 3 || setenv("C_TESTSUITE_ABNORMAL", "1", 1) == 0);
        valid = valid && (scenario != 4 || setenv("C_TESTSUITE_REFUSE_OBJECTS", "kill", 1) == 0);
        const uint64_t start = c_testsuite_now();
        const bool ran = valid && c_testsuite_execute(suite, relative ? "checkout" : path, 0, 3);
        const uint64_t elapsed = c_testsuite_now() - start;
        unsetenv("C_TESTSUITE_REFUSE_OBJECTS");
        unsetenv("C_TESTSUITE_ABNORMAL");
        valid = (saved != 0 ? setenv("TMPDIR", saved, 1) == 0 : unsetenv("TMPDIR") == 0) && chdir(original) == 0;
        const bool expected = ran && c_testsuite_expect(suite, scenarios[scenario], expectations[scenario]) && elapsed < UINT64_C(20) * 1000000000;
        if (ran)
        {
            c_testsuite_remove(suite);
        }
        if (!expected)
        {
            fprintf(stderr, "test-c-testsuite: self-test, %s: failed (%" PRIu64 " ms)\n", scenarios[scenario], elapsed / 1000000);
        }
        failures += !expected;
    }
    c_testsuite_without_pidfd = false;
    for (uint32_t i = 0; i < sizeof(files) / sizeof(files[0]); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", base, files[i][0]);
        unlink(path);
    }
    unlink(suite->compiler);
    for (uint32_t i = sizeof(directories) / sizeof(directories[0]); i-- > 0;)
    {
        snprintf(path, sizeof(path), "%s/%s", base, directories[i]);
        rmdir(path);
    }
    rmdir(base);
    free(saved);
    printf("test-c-testsuite: self-test: %u scenarios, %u failed\n", scenario_count, failures + (valid ? 0 : 1));
    return valid && failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static int run_c_testsuite_tests(int argc, char** argv)
{
    static CTestsuite suite;
    const char* subset = 0;
    const char* root = 0;
    const char* compiler = 0;
    uint64_t jobs = 0;
    bool keep = false;
    bool self_test = false;
    bool valid = true;
    suite.timeout = 10;
    suite.compile_timeout = BUSTER_C_TESTSUITE_COMPILE_SECONDS;
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
        else if (strcmp(argv[i], "--compiler") == 0 && i + 1 < argc)
        {
            compiler = argv[++i];
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            suite.cross_check = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argv[i], "--self-test") == 0)
        {
            self_test = true;
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
    if (!valid || (self_test ? root != 0 : root == 0))
    {
        fprintf(stderr, "usage: compiler test-c-testsuite [--subset FILE] [--jobs N] [--timeout SECONDS] [--compiler PATH] [--cross-check] "
                        "[--keep] DIR\n       compiler test-c-testsuite --self-test\n");
    }
    else
    {
        // The compiler is this executable, or --compiler's, as an absolute path: each test's processes run elsewhere.
        const ssize_t self = compiler != 0 ? 0 : readlink("/proc/self/exe", suite.compiler, sizeof(suite.compiler) - 1);
        suite.names = malloc((uint64_t)BUSTER_C_TESTSUITE_MAX_TESTS * BUSTER_C_TESTSUITE_NAME_BYTES);
        suite.results = mmap(0, BUSTER_C_TESTSUITE_MAX_TESTS * sizeof(CTestsuiteResult), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        valid = suite.names != 0 && suite.results != MAP_FAILED && (compiler != 0 ? realpath(compiler, suite.compiler) != 0 : self > 0);
        if (valid && compiler == 0)
        {
            suite.compiler[self] = 0;
        }
        if (valid && self_test)
        {
            result = c_testsuite_self_test(&suite);
        }
        else if (valid && c_testsuite_execute(&suite, root, subset, jobs))
        {
            const bool passed = c_testsuite_report(&suite, subset != 0);
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
