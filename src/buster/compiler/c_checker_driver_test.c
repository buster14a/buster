// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// End-to-end source diagnostics and emission barriers. Every original file is independently checked by Clang;
// the driver runs in a child so its real diagnostic renderer and return status are observed together. Supplying
// --executable also tests the production command's default C route, independently of its explicit check command.

#include "c_driver_compatibility_test.c"
#include "c_driver_mixed_test.c"

typedef struct CheckDriverFixture CheckDriverFixture;
struct CheckDriverFixture
{
    const char* name;
    const char* source;
    bool rejected;
    uint32_t warnings;
    bool quiet;
    const char* message;
    uint32_t column;
};

// test-checker-driver --part K/N: the command's checks are dealt in turn, in program order, to N parts, and this process
// runs part K's (index K - 1), so that the correctness list runs the command as N commands of one CPU each, which the
// runner packs beside the others: alone, the driver's checks were the list's longest command (197 s on a hosted
// runner, 48 s on the 9700X). A check deals one unit, or, where it is a long sweep (check_test_driver_recovery,
// check_test_driver_error_limits), one per case. Every part goes through the same units in the same order, whatever
// the others' outcomes, since each unit is dealt before anything it runs decides the next: so the parts together run
// each unit once, as the whole run runs it: no check draws from CheckTest.random, which the driver leaves at 0, and
// those that draw numbers seed their own. Without --part, the one part takes every unit.
typedef struct CheckDriverPart CheckDriverPart;
struct CheckDriverPart
{
    uint32_t index;
    uint32_t count;
    // The units dealt so far, and of them this part's.
    uint32_t dealt;
    uint32_t taken;
};

static CheckDriverPart check_driver_part = { .index = 0, .count = 1 };

// Whether the clang this run compares with is the pinned reference release's official build
// (BUSTER_CLANG_REFERENCE_VERSION), from run_checker_driver_tests, and the build it named ("23.1.4", "Ubuntu 23.1.6"):
// the library builtin comparisons that depend on its table then skip only the names that build is known to classify
// differently, and report them as a build difference.
static bool check_driver_clang_reference = true;
static char check_driver_clang_version[64] = "";

// Whether this part runs the next unit, which it deals.
static bool check_test_driver_takes(void)
{
    const bool taken = check_driver_part.dealt % check_driver_part.count == check_driver_part.index;
    check_driver_part.dealt += 1;
    check_driver_part.taken += taken;
    return taken;
}

// #2071: the checks' compiler runs without a process each. With --in-process, buster's source commands (an option
// first, as main_driver routes to main_source_command) run as this process's main_source_command_reusable, with the
// captured stream in a memfd. With --clang-server PATH, the oracle clang's runs (arguments[0] --clang's name) go to a
// clang that stays up (tools/clang-server), which runs each command line through clang's own driver and cc1 in its
// process, with the run's environment and working directory. Every other run, and a command a mode can't take (a
// response file, a jobserver's dry run, the guardian's or the fuzzer's command), runs as a child as before. With
// --differential, each run a mode takes also runs as a child: the child's result is the check's, and any difference
// in status or output is counted and printed. test-checker-driver reports the counts and the warning sweep's time.
// main.c's routing of a source command by the reusable commands, in the driver's unit, and the preprocessor's reusable
// command in its home, the frontend's unit (main.c includes this file before its own declarations of them). The checks
// call the frontend's, as main does, rather than this unit's copy of the preprocessor, whose command state would then
// be split between the two units (check-call-graph's split state).
int main_source_command_reusable(int argc, char** argv);
int main_preprocessor_command_reusable(int count, char** arguments);

typedef struct CheckDriverRunModes CheckDriverRunModes;
struct CheckDriverRunModes
{
    bool in_process;
    bool differential;
    // --clang-server's server (oracle_cache.c's OracleServer, its path the option's).
    OracleServer server;
    // The names the checks run buster and the oracle clang by: --executable's (or /proc/self/exe) and --clang's.
    const char* executable;
    const char* clang;
    uint64_t in_process_runs;
    uint64_t served_runs;
    uint64_t child_runs;
    // What the runs cost (#2263): the served runs' wall time, the server's requests past their limit (each stopped the
    // server, and its run ran as a child), and the children's wall time from fork to reap and CPU time (wait4's, theirs
    // and their own children's).
    uint64_t served_nanoseconds;
    uint64_t stalls;
    uint64_t child_nanoseconds;
    uint64_t child_cpu_nanoseconds;
    uint64_t differences;
    uint64_t sweep_fixtures;
    uint64_t sweep_nanoseconds;
};

static CheckDriverRunModes check_driver_run_modes = {
    .server = {
        .socket = -1,
    },
};

// What main_driver reads as a process starts: arena_page_policy's variables and transparent huge pages setting, and the
// lanes' test hooks. A run whose differ from this process's at its start needs a start of its own: a child.
static const char* const check_driver_start_variables[] = { "BUSTER_HUGE_PAGES", "BUSTER_TEST_HOST_BUSY", "BUSTER_TEST_NO_LANE_THREADS",
                                                            "BUSTER_TEST_LANE_FAIL_AT", "BUSTER_TEST_THREADS" };
static char* check_driver_start_values[BUSTER_ARRAY_LENGTH(check_driver_start_variables)];
static bool check_driver_start_set[BUSTER_ARRAY_LENGTH(check_driver_start_variables)];
static int check_driver_start_huge_pages = -1;

static void check_test_driver_record_start(void)
{
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_driver_start_variables); i += 1)
    {
        const char* value = getenv(check_driver_start_variables[i]);
        check_driver_start_set[i] = value != NULL;
        check_driver_start_values[i] = value != NULL ? strdup(value) : NULL;
    }
    check_driver_start_huge_pages = prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0);
}

static bool check_test_driver_same_start(void)
{
    bool same = prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0) == check_driver_start_huge_pages;
    for (uint32_t i = 0; same && i < BUSTER_ARRAY_LENGTH(check_driver_start_variables); i += 1)
    {
        const char* value = getenv(check_driver_start_variables[i]);
        same = (value != NULL) == check_driver_start_set[i] && (value == NULL || (check_driver_start_values[i] != NULL && strcmp(value, check_driver_start_values[i]) == 0));
    }
    return same;
}

static uint64_t check_test_driver_nanoseconds(void)
{
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

// Reads or writes all `size` bytes of `data` on `descriptor`.
static bool check_test_driver_transfer(int descriptor, void* data, size_t size, bool writing)
{
    char* at = data;
    bool complete = true;
    while (complete && size != 0)
    {
        const ssize_t moved = writing ? write(descriptor, at, size) : read(descriptor, at, size);
        complete = moved > 0 || (moved < 0 && errno == EINTR);
        if (moved > 0)
        {
            at += moved;
            size -= (size_t)moved;
        }
    }
    return complete;
}

// The whole of `descriptor`, a file from its start, into *text with a terminating zero (the caller frees it).
static bool check_test_driver_read_file(int descriptor, char** text)
{
    struct stat status;
    const bool sized = fstat(descriptor, &status) == 0 && lseek(descriptor, 0, SEEK_SET) == 0;
    *text = sized ? malloc((size_t)status.st_size + 1) : NULL;
    const bool read = *text != NULL && check_test_driver_transfer(descriptor, *text, (size_t)status.st_size, false);
    if (read)
    {
        (*text)[status.st_size] = 0;
    }
    else
    {
        free(*text);
        *text = NULL;
    }
    return read;
}

// Runs `arguments` as a child, capturing the selected stream into *text (the caller frees it) and discarding the other:
// with `exec`, the program arguments[0] names; without, this process's run_check_command in the forked child. Returns the
// exit status, or -1 if it didn't exit.
static int check_test_driver_child(char* const* arguments, char** text, bool stdout_capture, bool exec)
{
    FILE* captured = tmpfile();
    int status = 0;
    pid_t child = -1;
    size_t length = 0;
    *text = NULL;
    check_driver_run_modes.child_runs += 1;
    const uint64_t begun = check_test_driver_nanoseconds();
    if (captured != NULL)
    {
        fflush(NULL);
        child = fork();
        if (child == 0)
        {
            const int output = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (output >= 0 && dup2(stdout_capture ? fileno(captured) : output, STDOUT_FILENO) >= 0 &&
                dup2(stdout_capture ? output : fileno(captured), STDERR_FILENO) >= 0)
            {
                if (exec)
                {
                    execvp(arguments[0], arguments);
                }
                else
                {
                    int count = 0;
                    while (arguments[count] != NULL)
                    {
                        count += 1;
                    }
                    const int code = run_check_command(count - 1, (char**)arguments + 1, true);
                    fflush(NULL);
                    _exit(code);
                }
            }
            _exit(127);
        }
    }
    pid_t waited = -1;
    struct rusage usage = { 0 };
    do
    {
        waited = child > 0 ? wait4(child, &status, 0, &usage) : -1;
    }
    while (waited < 0 && child > 0 && errno == EINTR);
    if (child > 0 && waited == child)
    {
        check_driver_run_modes.child_nanoseconds += check_test_driver_nanoseconds() - begun;
        check_driver_run_modes.child_cpu_nanoseconds += ((uint64_t)usage.ru_utime.tv_sec + (uint64_t)usage.ru_stime.tv_sec) * 1000000000 +
                                                        ((uint64_t)usage.ru_utime.tv_usec + (uint64_t)usage.ru_stime.tv_usec) * 1000;
    }
    const bool read = captured != NULL && waited == child && pp_test_read_output(captured, text, &length);
    if (captured != NULL)
    {
        fclose(captured);
    }
    return read && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Whether --in-process takes buster's command line `arguments` (arguments[0] buster's name): a source command, an option
// first, but the guardian's and the fuzzer's, which main_driver takes before, not under a jobserver's dry run, which
// main_driver honors before any command, and with this process's start inputs (check_test_driver_same_start).
static bool check_test_driver_in_process_takes(char* const* arguments)
{
    return check_driver_run_modes.in_process && arguments[0] != NULL && arguments[1] != NULL && arguments[1][0] == '-' &&
           strcmp(arguments[1], "--buster-worker-budget-guardian") != 0 && strcmp(arguments[1], "--fuzz") != 0 && !worker_budget_dry_run(getenv("MAKEFLAGS")) &&
           check_test_driver_same_start();
}

// Buster's command line `arguments` as this process's main_source_command_reusable, with the selected stream captured
// into *text (the caller frees it) and the other discarded, as check_test_driver_child's child writes them: true with
// *status the exit status the child's would be (or -1 if the capture failed), or false for a command line the reusable
// path leaves to a process of its own (a response file).
static bool check_test_driver_run_in_process(char* const* arguments, char** text, bool stdout_capture, int* status)
{
    int count = 0;
    while (arguments[count] != NULL)
    {
        count += 1;
    }
    *text = NULL;
    *status = -1;
    int result = -1;
    fflush(NULL);
    // memfd_create with MFD_CLOEXEC, through syscall as parallel.c does: this unit has no _GNU_SOURCE.
    const int capture = (int)syscall(SYS_memfd_create, "buster-check-run", 1u);
    const int discard = open("/dev/null", O_WRONLY | O_CLOEXEC);
    const int saved_output = dup(STDOUT_FILENO);
    const int saved_errors = dup(STDERR_FILENO);
    const bool redirected = capture >= 0 && discard >= 0 && saved_output >= 0 && saved_errors >= 0 &&
                            dup2(stdout_capture ? capture : discard, STDOUT_FILENO) >= 0 && dup2(stdout_capture ? discard : capture, STDERR_FILENO) >= 0;
    if (redirected)
    {
        result = main_source_command_reusable(count, (char**)arguments);
    }
    fflush(NULL);
    const bool restored = saved_output >= 0 && saved_errors >= 0 && dup2(saved_output, STDOUT_FILENO) >= 0 && dup2(saved_errors, STDERR_FILENO) >= 0;
    const bool taken = !redirected || result >= 0;
    if (taken && redirected && restored && check_test_driver_read_file(capture, text))
    {
        *status = result & 0xff;
        check_driver_run_modes.in_process_runs += 1;
    }
    const int descriptors[] = { capture, discard, saved_output, saved_errors };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(descriptors); i += 1)
    {
        if (descriptors[i] >= 0)
        {
            close(descriptors[i]);
        }
    }
    return taken;
}

// The oracle clang's `arguments` through the server (oracle_server_run), with this process's working directory and
// environment, within the server's limit: the selected stream into *text (the caller frees it), the exit status, or -1
// if the server couldn't take it.
static int check_test_driver_clang_request(char* const* arguments, char** text, bool stdout_capture)
{
    CheckDriverRunModes* modes = &check_driver_run_modes;
    char directory[PATH_MAX];
    char* output = NULL;
    char* errors = NULL;
    uint64_t output_length = 0;
    uint64_t errors_length = 0;
    const bool placed = getcwd(directory, sizeof(directory)) != NULL;
    const uint64_t begun = check_test_driver_nanoseconds();
    const int status = placed ? oracle_server_run(&modes->server, arguments, environ, directory, NULL, stdout_capture ? 1u : 2u, 0, &output, &output_length, &errors, &errors_length) : -1;
    *text = stdout_capture ? output : errors;
    if (status >= 0)
    {
        modes->served_runs += 1;
        modes->served_nanoseconds += check_test_driver_nanoseconds() - begun;
    }
    if (placed && modes->server.stalled)
    {
        modes->stalls += 1;
        fprintf(stderr, "run modes: the clang server gave no answer within its limit: stopped, and the run runs as a child\n");
    }
    return status;
}

// Whether a line (of `length` bytes) is one of --time-stages' report lines (c_check_clock.c, and run_check_command_mode's
// worker budget), whose times, faults, sizes and lanes that worked differ between any two runs, as two children's do.
static bool check_test_driver_timing_line(const char* line, size_t length)
{
    static const char* const markers[] = { "time-stage\t", "time-check\t", "time-process\t", "time-write\t" };
    static const char budget[] = "worker-budget\t";
    bool timing = length >= sizeof(budget) - 1 && memcmp(line, budget, sizeof(budget) - 1) == 0;
    for (uint32_t i = 0; !timing && i < BUSTER_ARRAY_LENGTH(markers); i += 1)
    {
        const size_t size = strlen(markers[i]);
        timing = length >= size && memcmp(line, markers[i], size) == 0;
    }
    // A report's measured value is a number: a line whose last field isn't one (time-process' huge-pages policy, a
    // table's header) is compared as any other.
    if (timing && !(length >= sizeof(budget) - 1 && memcmp(line, budget, sizeof(budget) - 1) == 0))
    {
        size_t field = length;
        while (field > 0 && line[field - 1] != '\t')
        {
            field -= 1;
        }
        bool numeric = field < length;
        for (size_t i = field; numeric && i < length; i += 1)
        {
            numeric = (line[i] >= '0' && line[i] <= '9') || line[i] == '.' || line[i] == '-' || line[i] == '%';
        }
        timing = numeric;
    }
    return timing;
}

// qsort's order of two lines.
static int check_test_driver_compare_lines(const void* left, const void* right)
{
    return strcmp(*(char* const*)left, *(char* const*)right);
}

// The lines of `text` but --time-stages' report lines, each copied and sorted, into *lines (the caller frees each and
// the array): their count.
static uint32_t check_test_driver_sorted_lines(const char* text, char*** lines)
{
    const char* const stop = text + strlen(text);
    uint32_t count = 0;
    uint32_t capacity = 64;
    *lines = malloc(capacity * sizeof(char*));
    for (const char* at = text; *lines != NULL && at < stop;)
    {
        const char* end = memchr(at, '\n', (size_t)(stop - at));
        const size_t length = end != NULL ? (size_t)(end - at) : (size_t)(stop - at);
        if (!check_test_driver_timing_line(at, length))
        {
            if (count == capacity)
            {
                capacity *= 2;
                char** grown = realloc(*lines, capacity * sizeof(char*));
                if (grown == NULL)
                {
                    free(*lines);
                }
                *lines = grown;
            }
            if (*lines != NULL)
            {
                (*lines)[count] = strndup(at, length);
                count += 1;
            }
        }
        at += length + (end != NULL ? 1 : 0);
    }
    if (*lines != NULL)
    {
        qsort(*lines, count, sizeof(char*), check_test_driver_compare_lines);
    }
    return *lines != NULL ? count : 0;
}

// Whether two runs' outputs hold the same lines but for --time-stages' report lines, in any order: what a run with
// --time-stages or --lane-cpus prints from more than one lane (lane-pin's lines among them) comes in the order the lanes
// reach it, as two children's do.
static bool check_test_driver_same_lines(const char* text, const char* other)
{
    char** lines = NULL;
    char** other_lines = NULL;
    const uint32_t count = check_test_driver_sorted_lines(text, &lines);
    const uint32_t other_count = check_test_driver_sorted_lines(other, &other_lines);
    bool same = lines != NULL && other_lines != NULL && count == other_count;
    for (uint32_t i = 0; same && i < count; i += 1)
    {
        same = lines[i] != NULL && other_lines[i] != NULL && strcmp(lines[i], other_lines[i]) == 0;
    }
    for (uint32_t i = 0; lines != NULL && i < count; i += 1)
    {
        free(lines[i]);
    }
    for (uint32_t i = 0; other_lines != NULL && i < other_count; i += 1)
    {
        free(other_lines[i]);
    }
    free(lines);
    free(other_lines);
    return same;
}

// Counts and prints (the first few) a difference between a run's two ways.
static void check_test_driver_compare_runs(char* const* arguments, int status, const char* text, int other_status, const char* other_text)
{
    bool reports = false;
    for (uint32_t i = 1; !reports && arguments[i] != NULL; i += 1)
    {
        reports = strcmp(arguments[i], "--time-stages") == 0 || strcmp(arguments[i], "--lane-cpus") == 0;
    }
    const bool same_text = text != NULL && other_text != NULL &&
                           (strcmp(text, other_text) == 0 || (reports && check_test_driver_same_lines(text, other_text)));
    const bool same = status == other_status && ((text == NULL && other_text == NULL) || same_text);
    if (!same)
    {
        check_driver_run_modes.differences += 1;
        if (check_driver_run_modes.differences <= 8)
        {
            fprintf(stderr, "run modes: a run differs:");
            for (uint32_t i = 0; arguments[i] != NULL; i += 1)
            {
                fprintf(stderr, " %s", arguments[i]);
            }
            fprintf(stderr, "\nas a child, status %d:\n%s\nthe other way, status %d:\n%s\n", status, text != NULL ? text : "<none>", other_status,
                    other_text != NULL ? other_text : "<none>");
        }
    }
}

// Buster's command line `arguments` (arguments[0] its name for an exec'd child, or for !exec this process's code in a
// forked child): in this process where --in-process takes it, else as a child, as check_test_driver_child says. With
// --differential, a run in this process also runs as the child, whose result is returned.
static int check_test_driver_buster_command(char* const* arguments, char** text, bool stdout_capture, bool exec)
{
    char* inside = NULL;
    int inside_status = -1;
    const bool taken = check_test_driver_in_process_takes(arguments) && check_test_driver_run_in_process(arguments, &inside, stdout_capture, &inside_status);
    int status = -1;
    if (taken && !check_driver_run_modes.differential)
    {
        *text = inside;
        status = inside_status;
    }
    else
    {
        status = check_test_driver_child(arguments, text, stdout_capture, exec);
        if (taken)
        {
            check_test_driver_compare_runs(arguments, status, *text, inside_status, inside);
        }
        free(inside);
    }
    return status;
}

// Runs `arguments` (arguments[0] the program), capturing the selected stream into *text and discarding the other. The
// caller frees *text. Returns the exit status, or -1 if it didn't exit. Buster's runs (arguments[0] --executable's name
// or /proc/self/exe) and the oracle clang's take the run modes above.
static int check_test_driver_spawn_stream(char* const* arguments, char** text, bool stdout_capture)
{
    CheckDriverRunModes* modes = &check_driver_run_modes;
    const bool buster = arguments[0] != NULL && ((modes->executable != NULL && strcmp(arguments[0], modes->executable) == 0) || strcmp(arguments[0], "/proc/self/exe") == 0);
    const bool clang = !buster && arguments[0] != NULL && modes->server.path != NULL && modes->clang != NULL && strcmp(arguments[0], modes->clang) == 0;
    int status = -1;
    *text = NULL;
    if (buster)
    {
        status = check_test_driver_buster_command(arguments, text, stdout_capture, true);
    }
    else if (clang && !modes->differential)
    {
        // A run the server couldn't take (it ended, or ran past its limit) runs as a child, as without the server.
        status = check_test_driver_clang_request(arguments, text, stdout_capture);
        if (status < 0)
        {
            free(*text);
            *text = NULL;
            status = check_test_driver_child(arguments, text, stdout_capture, true);
        }
    }
    else if (clang)
    {
        char* served = NULL;
        const int served_status = check_test_driver_clang_request(arguments, &served, stdout_capture);
        status = check_test_driver_child(arguments, text, stdout_capture, true);
        check_test_driver_compare_runs(arguments, status, *text, served_status, served);
        free(served);
    }
    else
    {
        status = check_test_driver_child(arguments, text, stdout_capture, true);
    }
    return status;
}

static int check_test_driver_spawn(char* const* arguments, char** text)
{
    return check_test_driver_spawn_stream(arguments, text, false);
}

// Runs the driver (this process's run_check_command in a child, or `executable`, or with --in-process this process's)
// on `source`, with -c into `object` or without one -fsyntax-only, and `extra`'s `extra_count` arguments (at most 8)
// before the source: its first error, as clang's oracle reads one, and its warnings' count.
static bool check_test_driver_run_with(const char* executable, const char* source, const char* object, bool scalar, bool dump_static, bool quiet, bool lazy,
                                       const char* const* extra, uint32_t extra_count, CheckCorpusOracle* outcome, uint32_t* warnings)
{
    *outcome = (CheckCorpusOracle){ 0 };
    *warnings = 0;
    char* arguments[24] = { 0 };
    uint32_t count = 0;
    arguments[count++] = (char*)(executable != NULL ? executable : "checker");
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    if (dump_static)
    {
        arguments[count++] = "--dump-static";
    }
    if (quiet)
    {
        arguments[count++] = "-w";
    }
    if (lazy)
    {
        arguments[count++] = "-flazy-system-headers";
    }
    arguments[count++] = "-std=gnu17";
    // Without an object, -fsyntax-only.
    if (object != NULL)
    {
        arguments[count++] = "-c";
        arguments[count++] = "-o";
        arguments[count++] = (char*)object;
    }
    else
    {
        arguments[count++] = "-fsyntax-only";
    }
    for (uint32_t i = 0; i < extra_count && i < 8; i += 1)
    {
        arguments[count++] = (char*)extra[i];
    }
    arguments[count++] = (char*)source;
    char* text = NULL;
    const int status = check_test_driver_buster_command(arguments, &text, false, executable != NULL);
    bool ready = status >= 0 && text != NULL;
    bool unlocated = false;
    // Each line's end is found within the text (memchr), as check_test_driver_diagnostic_counts's are: strchr on the
    // rest of the text reads all of it per line under AddressSanitizer's strict string checks (#2030).
    char* stop = text != NULL ? text + strlen(text) : NULL;
    for (char* at = text; ready && at != NULL && at < stop;)
    {
        char* end = memchr(at, '\n', (size_t)(stop - at));
        if (end != NULL)
        {
            *end = 0;
        }
        if (strstr(at, "error: ") != NULL)
        {
            uint32_t line = 0;
            uint32_t column = 0;
            const bool located = sscanf(at, "%*[^:]:%u:%u:", &line, &column) == 2 && line != 0 && column != 0;
            unlocated |= !located;
            if (located && !outcome->rejected)
            {
                outcome->rejected = true;
                outcome->line = line;
                outcome->column = column;
                snprintf(outcome->message, sizeof(outcome->message), "%s", strstr(at, "error: ") + 7);
            }
        }
        *warnings += strstr(at, "warning: ") != NULL;
        at = end != NULL ? end + 1 : NULL;
    }
    ready = ready && !unlocated && ((status == 0 && !outcome->rejected) || (status == 1 && outcome->rejected));
    free(text);
    return ready;
}

static bool check_test_driver_run(const char* executable, const char* source, const char* object, bool scalar, bool dump_static, bool quiet, bool lazy,
                                  CheckCorpusOracle* outcome, uint32_t* warnings)
{
    return check_test_driver_run_with(executable, source, object, scalar, dump_static, quiet, lazy, NULL, 0, outcome, warnings);
}

// Exercise the real reservation failure in a child so its address-space limit cannot affect the test process.
static void check_test_driver_reservation_failure(CheckTest* test)
{
#if (__has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)) && !defined(BUSTER_SANITIZER_RESOURCES)
    // Until the resource-failure injection is available, a detector's shadow memory must not be constrained.
    (void)test;
#else
    for (uint32_t command = 0; command < 2; command += 1)
    {
        FILE* errors = tmpfile();
        pid_t child = -1;
        int status = 0;
        if (errors != NULL)
        {
            fflush(NULL);
            child = fork();
            if (child == 0)
            {
                const bool prepared = dup2(fileno(errors), STDERR_FILENO) == STDERR_FILENO && unsetenv("MAKEFLAGS") == 0;
#if defined(BUSTER_SANITIZER_RESOURCES) && BUSTER_SANITIZER_RESOURCES
                lane_test_resources.arena_limited = true;
                lane_test_resources.arena_limit = 0;
                const bool limited = true;
#else
                const struct rlimit limit = { 0, 0 };
                const bool limited = setrlimit(RLIMIT_AS, &limit) == 0;
#endif
                const bool ready = prepared && limited;
                char* arguments[] = { (char*)"-fsyntax-only", (char*)"missing.c" };
                const int code = ready ? run_check_command(2, arguments, command != 0) : 126;
                fflush(stderr);
                _exit(code);
            }
        }
        pid_t waited = -1;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        }
        while (waited < 0 && child > 0 && errno == EINTR);
        char* output = NULL;
        size_t length = 0;
        const bool read = errors != NULL && waited == child && pp_test_read_output(errors, &output, &length);
        check_test_expect(test, read && WIFEXITED(status) && WEXITSTATUS(status) == 1 &&
                                strstr(output, "fatal error: can't reserve checker memory") != NULL &&
                                strstr(output, "68719476736 bytes") != NULL && strstr(output, "usage:") == NULL,
                          "driver reservation failures report the requested size instead of usage");
        free(output);
        if (errors != NULL)
        {
            fclose(errors);
        }
    }
#endif
}

// Whether the files at `a` and `b` can be read and have the same bytes.
static bool check_test_driver_same_files(const char* a, const char* b)
{
    FILE* files[2] = { fopen(a, "rb"), fopen(b, "rb") };
    bool same = files[0] != NULL && files[1] != NULL;
    bool reading = same;
    while (reading)
    {
        char first[4096];
        char second[4096];
        const size_t got = fread(first, 1, sizeof(first), files[0]);
        same = fread(second, 1, sizeof(second), files[1]) == got && memcmp(first, second, got) == 0;
        reading = same && got != 0;
    }
    for (uint32_t f = 0; f < 2; f += 1)
    {
        if (files[f] != NULL)
        {
            fclose(files[f]);
        }
    }
    return same;
}

// The DT_NEEDED names of the executable at `path`, in order, joined by spaces into `names`: whether its dynamic section
// could be read (a PIE's file offsets are its addresses' in its first PT_LOAD, as the direct path lays one out).
static bool check_test_driver_needed(const char* path, char* names, uint64_t capacity)
{
    uint64_t length = 0;
    FILE* file = fopen(path, "rb");
    uint8_t* bytes = NULL;
    if (file != NULL && fseek(file, 0, SEEK_END) == 0)
    {
        const long size = ftell(file);
        bytes = size > (long)sizeof(Elf64_Ehdr) && fseek(file, 0, SEEK_SET) == 0 ? malloc((uint64_t)size) : NULL;
        length = bytes != NULL && fread(bytes, 1, (uint64_t)size, file) == (uint64_t)size ? (uint64_t)size : 0;
    }
    if (file != NULL)
    {
        fclose(file);
    }
    names[0] = 0;
    Elf64_Ehdr header = { 0 };
    bool valid = length != 0;
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_phoff + (uint64_t)header.e_phnum * sizeof(Elf64_Phdr) <= length;
    }
    uint64_t dynamic_offset = 0;
    uint64_t dynamic_size = 0;
    uint64_t load_offset = 0;
    uint64_t load_address = 0;
    bool load = false;
    for (uint32_t p = 0; valid && p < header.e_phnum; p += 1)
    {
        Elf64_Phdr segment;
        memcpy(&segment, bytes + header.e_phoff + p * sizeof(Elf64_Phdr), sizeof(segment));
        dynamic_offset = segment.p_type == PT_DYNAMIC ? segment.p_offset : dynamic_offset;
        dynamic_size = segment.p_type == PT_DYNAMIC ? segment.p_filesz : dynamic_size;
        load_offset = segment.p_type == PT_LOAD && !load ? segment.p_offset : load_offset;
        load_address = segment.p_type == PT_LOAD && !load ? segment.p_vaddr : load_address;
        load = load || segment.p_type == PT_LOAD;
    }
    valid = valid && dynamic_size != 0 && dynamic_offset + dynamic_size <= length;
    uint64_t strings = 0;
    for (uint64_t at = 0; valid && at + sizeof(Elf64_Dyn) <= dynamic_size; at += sizeof(Elf64_Dyn))
    {
        Elf64_Dyn entry;
        memcpy(&entry, bytes + dynamic_offset + at, sizeof(entry));
        strings = entry.d_tag == DT_STRTAB ? entry.d_un.d_ptr - load_address + load_offset : strings;
    }
    valid = valid && strings != 0 && strings < length;
    uint64_t used = 0;
    for (uint64_t at = 0; valid && at + sizeof(Elf64_Dyn) <= dynamic_size; at += sizeof(Elf64_Dyn))
    {
        Elf64_Dyn entry;
        memcpy(&entry, bytes + dynamic_offset + at, sizeof(entry));
        if (entry.d_tag == DT_NEEDED)
        {
            const uint64_t name = strings + entry.d_un.d_val;
            valid = name < length && memchr(bytes + name, 0, length - name) != NULL;
            const int written = valid ? snprintf(names + used, capacity - used, "%s%s", used != 0 ? " " : "", (const char*)bytes + name) : 0;
            valid = valid && written > 0 && used + (uint64_t)written < capacity;
            used += valid ? (uint64_t)written : 0;
        }
    }
    free(bytes);
    return valid;
}

// The driver's -l libraries, through the production command (#145): a program that calls libm's functions, takes
// one's address and reads libm's signgam (data, through its GOT slot), linked with -lm by both paths into the same bytes,
// that runs and prints what this executable's own libm computes, needing libm.so.6 before libc.so.6; one that calls
// none, with -lm, needing libc.so.6 alone; and the refusals, each named: no -lm and a library no directory has.
// An archive containing only its empty symbol index adds nothing and links the plain program (#1470).
static void check_test_driver_libraries(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "#include <math.h>\n#include <stdio.h>\ndouble (*pick)(double) = cos;\n"
                                  "int main(void) { double logarithm = lgamma(-0.5); printf(\"%f %f %f %f %f %d\\n\", sin(2), sqrt(2.0), pow(2.0, 10.0), "
                                  "pick(0.5), logarithm, signgam); return 0; }\n";
    static const char plain[] = "int puts(const char*);\nint main(void) { puts(\"plain\"); return 0; }\n";
    static const char archive[] = "!<arch>\n/               0           0     0     644     4         `\n\0\0\0\0";
    char source[256];
    char plain_source[256];
    char archive_path[256];
    char outputs[2][256];
    char plain_output[256];
    snprintf(source, sizeof(source), "%s/math.c", directory);
    snprintf(plain_source, sizeof(plain_source), "%s/plain.c", directory);
    snprintf(archive_path, sizeof(archive_path), "%s/libbuster_members.a", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/math", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/math-scalar", directory);
    snprintf(plain_output, sizeof(plain_output), "%s/plain", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1) && parse_test_write(plain_source, plain, sizeof(plain) - 1) &&
                         parse_test_write(archive_path, archive, sizeof(archive) - 1);
    check_test_expect(test, written, "library programs written");
    char* text = NULL;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, "-lm", NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, "-lm", NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "-lm links libm's functions" : "-lm links libm's functions, scalar");
        free(text);
    }
    check_test_expect(test, written && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable with -lm");
    char needed[256] = { 0 };
    check_test_expect(test, written && check_test_driver_needed(outputs[0], needed, sizeof(needed)) && strcmp(needed, "libm.so.6 libc.so.6") == 0,
                      "libm.so.6 needed before libc.so.6");
    // The host libm's results, from test_libm (#652): lgamma sets that libm's signgam.
    double (*const libm_sin)(double) = (double (*)(double))test_libm("sin");
    double (*const libm_sqrt)(double) = (double (*)(double))test_libm("sqrt");
    double (*const libm_pow)(double, double) = (double (*)(double, double))test_libm("pow");
    double (*const libm_cos)(double) = (double (*)(double))test_libm("cos");
    double (*const libm_lgamma)(double) = (double (*)(double))test_libm("lgamma");
    const int* const libm_signgam = (const int*)test_libm("signgam");
    const bool oracle = libm_sin != NULL && libm_sqrt != NULL && libm_pow != NULL && libm_cos != NULL && libm_lgamma != NULL && libm_signgam != NULL;
    check_test_expect(test, oracle, "find libm's functions for the program's expected output");
    char expected[256] = { 0 };
    if (oracle)
    {
        const double logarithm = libm_lgamma(-0.5);
        snprintf(expected, sizeof(expected), "%f %f %f %f %f %d\n", libm_sin(2), libm_sqrt(2.0), libm_pow(2.0, 10.0), libm_cos(0.5), logarithm, *libm_signgam);
    }
    char captured[256] = { 0 };
    FILE* output = written ? popen(outputs[0], "r") : NULL;
    const size_t got = output != NULL ? fread(captured, 1, sizeof(captured) - 1, output) : 0;
    const int closed = output != NULL ? pclose(output) : -1;
    check_test_expect(test, closed == 0 && got == strlen(expected) && memcmp(captured, expected, got) == 0, "the program prints libm's results");
    char* const plain_arguments[] = { (char*)executable, "-o", plain_output, plain_source, "-lm", NULL };
    const int plain_status = written ? check_test_driver_spawn(plain_arguments, &text) : -1;
    free(text);
    check_test_expect(test, plain_status == 0 && check_test_driver_needed(plain_output, needed, sizeof(needed)) && strcmp(needed, "libc.so.6") == 0,
                      "-lm unused needs libc.so.6 alone (as needed)");
    struct
    {
        const char* name;
        const char* message;
        char* arguments[8];
    } const refusals[] = {
        { "no -lm", "undefined reference to 'cos'", { (char*)executable, "-o", outputs[0], source, NULL } },
        { "a library nowhere", "cannot find -lbuster_nowhere", { (char*)executable, "-o", outputs[0], source, "-lm", "-lbuster_nowhere", NULL } },
    };
    for (uint32_t r = 0; written && r < BUSTER_ARRAY_LENGTH(refusals); r += 1)
    {
        const int status = check_test_driver_spawn(refusals[r].arguments, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, refusals[r].message) != NULL, refusals[r].name);
        free(text);
    }
    char* const indexed_arguments[] = { (char*)executable, "-o", outputs[0], plain_source, "-L", (char*)directory, "-lbuster_members", NULL };
    const int indexed_status = written ? check_test_driver_spawn(indexed_arguments, &text) : -1;
    if (written)
    {
        free(text);
    }
    check_test_expect(test, indexed_status == 0, "an archive containing only its empty symbol index links");
    memset(captured, 0, sizeof(captured));
    output = indexed_status == 0 ? popen(outputs[0], "r") : NULL;
    const size_t indexed_got = output != NULL ? fread(captured, 1, sizeof(captured) - 1, output) : 0;
    const int indexed_closed = output != NULL ? pclose(output) : -1;
    check_test_expect(test, indexed_closed == 0 && indexed_got == sizeof("plain\n") - 1 && memcmp(captured, "plain\n", indexed_got) == 0,
                      "the indexed-archive link replaces the math executable and runs the plain program");
    unlink(source);
    unlink(plain_source);
    unlink(archive_path);
    unlink(outputs[0]);
    unlink(outputs[1]);
    unlink(plain_output);
}

// GNU C's inline assembly by the driver (#147): an executable, linked by the direct path from each path into the same
// bytes, whose naked function has a label another template takes the address of, and whose template calls libc's puts
// by name, an undefined global that binding takes from libc.so.6; and a label named as one of the unit's functions,
// which the emission refuses by name, as GNU as and clang refuse a symbol defined twice.
// A value held across a call in a function that calls __builtin_setjmp where a longjmp's resumption wouldn't find it (#1242's
// review): a deep expression's, in its depth's slot, which later expressions share. Both paths refuse it by name, and
// compile a long double held across setjmp, whose slot the call snapshots (#1843), and a function with no value held.
static void check_test_driver_setjmp_held(CheckTest* test, const char* executable, const char* directory)
{
    static const char* const sources[3] = {
        "static void *buf[5];\n__attribute__((noinline)) static void jump(void) { __builtin_longjmp(buf, 1); }\n"
        "int run(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j)\n"
        "{ int r = a + (b * (c - (d + (e * (f - (g + (h * (i - (j + __builtin_setjmp(buf)))))))))); if (r == 0) jump(); return r; }\n",
        "static void *buf[5];\n__attribute__((noinline)) static void jump(void) { __builtin_longjmp(buf, 1); }\n"
        "long double run(long double x) { long double r = x + __builtin_setjmp(buf); if (r == x) jump(); return r; }\n",
        "static void *buf[5];\n__attribute__((noinline)) static void jump(void) { __builtin_longjmp(buf, 1); }\n"
        "long double run(long double x) { if (__builtin_setjmp(buf) == 0) { x = x * 2; jump(); } return x; }\n",
    };
    char source[256];
    char object[256];
    snprintf(source, sizeof(source), "%s/setjmp_held.c", directory);
    snprintf(object, sizeof(object), "%s/setjmp_held.o", directory);
    for (uint32_t s = 0; s < 3; s += 1)
    {
        const bool written = parse_test_write(source, sources[s], strlen(sources[s]));
        for (uint32_t path = 0; written && path < 2; path += 1)
        {
            char* const vector_arguments[] = { (char*)executable, "-c", "-o", object, source, NULL };
            char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", object, source, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
            const bool refused = status == 1 && text != NULL &&
                                 strstr(text, "unsupported: a mask value, or a deep expression's, held across a call in a function that calls __builtin_setjmp") != NULL;
            const bool compiled = status == 0;
            check_test_expect(test, s == 0 ? refused : compiled,
                              s == 0   ? (path == 0 ? "a deep expression held across __builtin_setjmp is refused by name" : "the same, scalar")
                              : s == 1 ? (path == 0 ? "a long double held across __builtin_setjmp compiles" : "the same, scalar")
                                       : (path == 0 ? "a long double not held across __builtin_setjmp compiles" : "the same, scalar"));
            free(text);
        }
        check_test_expect(test, written, "setjmp's held-value sources written");
    }
    unlink(source);
    unlink(object);
}

// Builtins clang's Sema takes and its code generation refuses for x86 (#148), which -c refuses by name on both paths:
// BPF's preserve_access_index and HLSL's resource pointer; __builtin_clear_padding of an element past 4 GiB; an NSString of
// no literal, which clang's code reads as one
// (check_test_driver_cfstrings has the constant strings' objects); z/OS's va_start and
// va_copy, which clang's x86-64 code makes over a char *; os_log's
// data taken again across side effects, a __float128's and a 2-byte vector's, those clang's code crashes on (a 32-byte
// vector), and a format of more than 255 items, whose count byte clang's code truncates (#1785); dump_struct's generated calls; and
// __builtin_available with an empty version, an invalid expression of clang's, which its code generation refuses or drops.
static void check_test_driver_codegen_refusals(CheckTest* test, const char* executable, const char* directory)
{
    static const struct
    {
        const char* source;
        const char* message;
    } cases[] = {
        { "struct S { int a; };\nint f(struct S* s) { return __builtin_preserve_access_index(s->a); }\n",
          "unsupported: __builtin_preserve_access_index, which clang compiles for BPF alone (an error without -g)" },
        { "void f(int x) { __builtin_hlsl_resource_getpointer_typed(x); }\n",
          "unsupported: __builtin_hlsl_resource_getpointer_typed, which clang doesn't compile either" },
        { "struct S { char c[1L << 32]; int i; };\nvoid f(struct S* p) { __builtin_clear_padding(p); }\n",
          "unsupported: __builtin_clear_padding of an element past 4 GiB" },
        { "const void* f(const char* s) { return __builtin___NSStringMakeConstantString(s); }\n",
          "unsupported: an NSString of no string literal, whose object clang's code makes from the literal its argument isn't" },
        // A static initializer of one, which clang's evaluator takes as an address constant (#1785): its object refused by the
        // static data.
        { "static const char m[] = \"x\";\nconst void* p = __builtin___NSStringMakeConstantString(m);\n",
          "unsupported: an NSString of no string literal, whose object clang's code makes from the literal its argument isn't" },
        { "int f(int n, ...) { char* a; __builtin_zos_va_start(&a, n); return 0; }\n",
          "unsupported: __builtin_zos_va_start and __builtin_zos_va_copy, z/OS's lists, which clang's x86-64 code overruns" },
        { "void f(char** a, char** b) { __builtin_zos_va_copy(a, b); }\n",
          "unsupported: __builtin_zos_va_start and __builtin_zos_va_copy, z/OS's lists, which clang's x86-64 code overruns" },
        { "typedef float V __attribute__((vector_size(32)));\nvoid* f(void* b, V v) { return __builtin_os_log_format(b, \"%d\", v); }\n",
          "unsupported: __builtin_os_log_format of data clang's code doesn't make (past 16 bytes, a _BitInt past 128 bits)" },
        { "void* f(void* b, _BitInt(200) w) { return __builtin_os_log_format(b, \"%d\", w); }\n",
          "unsupported: __builtin_os_log_format of data clang's code doesn't make (past 16 bytes, a _BitInt past 128 bits)" },
        { "#define F \"%1$d %1$d %1$d %1$d %1$d %1$d %1$d %1$d %1$d %1$d \"\n#define G F F F F F F F F F F\n"
          "void* f(void* b) { return __builtin_os_log_format(b, G G G, 1); }\n",
          "unsupported: __builtin_os_log_format of more than 255 items, whose count clang's code truncates" },
        { "int g(int, const char*, ...);\nstruct S { int a; };\nvoid f(struct S* s, int n) { __builtin_dump_struct(s, g, ({ n; })); }\n",
          "unsupported: __builtin_dump_struct whose printer or other arguments have a statement expression or more than 4096 nodes, which its calls evaluate again" },
        // A record in a segment's space (#1447), whose fields' loads would take no override.
        { "struct S { int a; long b; };\nint printf(const char*, ...);\nvoid f(struct S __seg_fs* s) { __builtin_dump_struct(s, printf); }\n",
          "unsupported: __builtin_dump_struct of a record in a segment's address space (256, 257 or 258: %gs, %fs or %ss)" },
        { "int f(void) { return __builtin_available(macos 0.0.0, *); }\n",
          "unsupported: __builtin_available with an empty version, an invalid expression whose code clang refuses or drops" },
    };
    char source[256];
    char object[256];
    snprintf(source, sizeof(source), "%s/codegen_refusal.c", directory);
    snprintf(object, sizeof(object), "%s/codegen_refusal.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const bool written = parse_test_write(source, cases[c].source, strlen(cases[c].source));
        for (uint32_t path = 0; written && path < 2; path += 1)
        {
            char* const vector_arguments[] = { (char*)executable, "-c", "-o", object, source, NULL };
            char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", object, source, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
            char description[256];
            snprintf(description, sizeof(description), "code generation refusal %u, %s: '%s'", c, path == 0 ? "vector" : "scalar", cases[c].message);
            check_test_expect(test, status == 1 && text != NULL && strstr(text, cases[c].message) != NULL, description);
            free(text);
        }
        check_test_expect(test, written, "a code generation refusal's source written");
    }
    unlink(source);
    unlink(object);
}

static void check_test_driver_asm_symbols(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] =
        "static __attribute__((used)) const char message[] = \"naked\";\n"
        "static __attribute__((naked, noinline)) long twice(long x)\n"
        "{\n"
        "    __asm__(\"leaq (%rdi,%rdi), %rax\\n\\tret\\nthrice:\\n\\tleaq (%rdi,%rdi,2), %rax\\n\\tret\\n\");\n"
        "}\n"
        "static long thrice_address(void) { long a; __asm__(\"leaq thrice(%%rip), %0\" : \"=r\"(a)); return a; }\n"
        "int main(void)\n"
        "{\n"
        "    long (*f)(long) = (long (*)(long))thrice_address();\n"
        "    __asm__ volatile(\"leaq message(%%rip), %%rdi\\n\\tcall puts\" ::: \"rax\", \"rcx\", \"rdx\", \"rsi\", \"rdi\", \"r8\", \"r9\", \"r10\", \"r11\", \"memory\");\n"
        "    return (int)(twice(5) + f(7)) - 31;\n"
        "}\n";
    static const char refused[] = "int tested(int n) { __asm__(\"tested: nop\"); return n; }\n";
    char source[256];
    char refused_source[256];
    char outputs[2][256];
    char object[256];
    snprintf(source, sizeof(source), "%s/asm_symbols.c", directory);
    snprintf(refused_source, sizeof(refused_source), "%s/asm_label_symbol.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/asm_symbols", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/asm_symbols_scalar", directory);
    snprintf(object, sizeof(object), "%s/asm_label_symbol.o", directory);
    const bool written = object_write_file(source, (const uint8_t*)program, sizeof(program) - 1) == OBJECT_ERROR_NONE &&
                         object_write_file(refused_source, (const uint8_t*)refused, sizeof(refused) - 1) == OBJECT_ERROR_NONE;
    char* text = NULL;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "a naked function's label and a template's call of puts link" : "the same, scalar");
        free(text);
    }
    check_test_expect(test, written && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable of templates' symbols");
    char captured[64] = { 0 };
    FILE* output = written ? popen(outputs[0], "r") : NULL;
    const size_t got = output != NULL ? fread(captured, 1, sizeof(captured) - 1, output) : 0;
    const int closed = output != NULL ? pclose(output) : -1;
    check_test_expect(test, closed == 0 && got == 6 && memcmp(captured, "naked\n", 6) == 0, "the program's templates run: puts prints, and the label is a second entry");
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-c", "-o", object, refused_source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", object, refused_source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, "unsupported: inline assembly labels named as the unit's symbols") != NULL,
                          path == 0 ? "a label named as the unit's function is refused" : "the same, scalar");
        free(text);
    }
    unlink(source);
    unlink(refused_source);
    unlink(outputs[0]);
    unlink(outputs[1]);
    unlink(object);
}

// A program whose 128-bit integers' division, remainder and conversions to and from double and long double call
// libgcc.a's functions (#133's lowering), __fixdfti among them, whose member calls __fixunsdfti, which the program calls
// too.
static const char check_test_libgcc_program[] = "#include <stdio.h>\n"
                                                "static volatile unsigned __int128 dividend = ((unsigned __int128)1 << 100) + 12345;\n"
                                                "static volatile unsigned __int128 divisor = 1000003;\n"
                                                "static volatile __int128 negative = -((__int128)1 << 90) - 7;\n"
                                                "static volatile double value = 1e30;\n"
                                                "int main(void)\n"
                                                "{\n"
                                                "    unsigned __int128 q = dividend / divisor, r = dividend % divisor;\n"
                                                "    __int128 s = negative / 77, t = negative % 77;\n"
                                                "    unsigned __int128 back = (unsigned __int128)value;\n"
                                                "    __int128 signed_back = (__int128)-value;\n"
                                                "    printf(\"%llx %llx %llx %lld %.17g %.17g %.21Lg\\n\", (unsigned long long)(q >> 64), (unsigned long long)q,\n"
                                                "           (unsigned long long)r, (long long)t, (double)back, (double)signed_back, (long double)s);\n"
                                                "    return 0;\n"
                                                "}\n";

// libgcc.a in the direct path's executables (#136): the libgcc program (check_test_libgcc_program), linked by both
// paths into the same bytes, needing libc.so.6 alone (the members join the executable), must print what this
// executable computes of the same expressions.
static void check_test_driver_libgcc(CheckTest* test, const char* executable, const char* directory)
{
    char source[256];
    char outputs[2][256];
    snprintf(source, sizeof(source), "%s/libgcc.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/libgcc", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/libgcc-scalar", directory);
    const bool written = parse_test_write(source, check_test_libgcc_program, sizeof(check_test_libgcc_program) - 1);
    check_test_expect(test, written, "libgcc program written");
    char* text = NULL;
    bool linked = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "128-bit division and conversions link libgcc.a's members" : "libgcc.a's members, scalar");
        linked = linked && status == 0;
        free(text);
    }
    check_test_expect(test, linked && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable with libgcc.a's members");
    char needed[256] = { 0 };
    check_test_expect(test, linked && check_test_driver_needed(outputs[0], needed, sizeof(needed)) && strcmp(needed, "libc.so.6") == 0,
                      "libgcc.a's members in the executable, libc.so.6 needed alone");
    volatile unsigned __int128 dividend = ((unsigned __int128)1 << 100) + 12345;
    volatile unsigned __int128 divisor = 1000003;
    volatile __int128 negative = -((__int128)1 << 90) - 7;
    volatile double value = 1e30;
    const unsigned __int128 q = dividend / divisor;
    const unsigned __int128 r = dividend % divisor;
    const __int128 s = negative / 77;
    const __int128 t = negative % 77;
    const unsigned __int128 back = (unsigned __int128)value;
    const __int128 signed_back = (__int128)-value;
    char expected[256];
    snprintf(expected, sizeof(expected), "%llx %llx %llx %lld %.17g %.17g %.21Lg\n", (unsigned long long)(q >> 64), (unsigned long long)q, (unsigned long long)r,
             (long long)t, (double)back, (double)signed_back, (long double)s);
    char captured[256] = { 0 };
    FILE* output = linked ? popen(outputs[0], "r") : NULL;
    const size_t got = output != NULL ? fread(captured, 1, sizeof(captured) - 1, output) : 0;
    const int closed = output != NULL ? pclose(output) : -1;
    check_test_expect(test, closed == 0 && got == strlen(expected) && memcmp(captured, expected, got) == 0, "the program prints the 128-bit results");
    unlink(source);
    unlink(outputs[0]);
    unlink(outputs[1]);
}

// A function's target attribute in the bodies -c and the direct path select (#180, check_program_select): each selected
// body keeps its own instruction set though bodies before it aren't selected. A BMI2 function after an unselected one, in
// a baseline unit, must get BMI2 (an executable that runs where the host has it, by both paths into the same bytes); and
// a function with vectors after an unselected one whose attribute turns AVX-512VL off, in a znver5 unit, the unit's.
static void check_test_driver_target_selection(CheckTest* test, const char* executable, const char* directory)
{
    static const char bmi2[] =
        "static int unused(int x){return x*3;}\n"
        "__attribute__((target(\"bmi2\"))) static unsigned deposit(unsigned x,unsigned m){return __builtin_ia32_pdep_si(x,m);}\n"
        "static int also_unused(int x){return x+1;}\n"
        "int main(void){volatile unsigned x=0x5u,m=0xf0f0u;return deposit(x,m)==0x50u?0:1;}\n";
    static const char vectors[] =
        "typedef int v4si __attribute__((vector_size(16)));\n"
        "__attribute__((target(\"no-avx512vl\"))) static int scalar_only(int x){return x*5;}\n"
        "static int sum(int n){v4si a={n,1,2,3},b={4,5,6,7};a=a+b;return a[0]+a[3];}\n"
        "int main(void){volatile int n=1;return sum(n)==15?0:1;}\n";
    char sources[2][256];
    char outputs[3][256];
    snprintf(sources[0], sizeof(sources[0]), "%s/target-selection-bmi2.c", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/target-selection-vectors.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/target-selection", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/target-selection-scalar", directory);
    snprintf(outputs[2], sizeof(outputs[2]), "%s/target-selection-vectors.o", directory);
    const bool written = parse_test_write(sources[0], bmi2, sizeof(bmi2) - 1) && parse_test_write(sources[1], vectors, sizeof(vectors) - 1);
    check_test_expect(test, written, "the target attributes' selection programs written");
    bool linked = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* text = NULL;
        char* const vector_arguments[] = { (char*)executable, "-march=x86-64", "-o", outputs[0], sources[0], NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-march=x86-64", "-o", outputs[1], sources[0], NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "a selected BMI2 function after an unselected one links with BMI2" :
                                                         "a selected BMI2 function after an unselected one, scalar");
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        linked = linked && status == 0;
        free(text);
        char* const vector_object[] = { (char*)executable, "-march=znver5", "-c", "-o", outputs[2], sources[1], NULL };
        char* const scalar_object[] = { (char*)executable, "--scalar", "-march=znver5", "-c", "-o", outputs[2], sources[1], NULL };
        text = NULL;
        const int compiled = check_test_driver_spawn(path == 0 ? vector_object : scalar_object, &text);
        check_test_expect(test, compiled == 0, path == 0 ? "a selected function's vectors after an unselected no-avx512vl one compile" :
                                                           "a selected function's vectors after an unselected no-avx512vl one, scalar");
        if (compiled != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
    }
    check_test_expect(test, linked && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same BMI2 executable");
    if (linked && (x86_target_native().features & BUSTER_X86_FEATURE(BMI2)) != 0)
    {
        char* text = NULL;
        char* const run[] = { outputs[0], NULL };
        check_test_expect(test, check_test_driver_spawn(run, &text) == 0, "the selected BMI2 function deposits as PDEP does");
        free(text);
    }
    unlink(sources[0]);
    unlink(sources[1]);
    unlink(outputs[0]);
    unlink(outputs[1]);
    unlink(outputs[2]);
}

static bool check_test_driver_capture(const char* path, char* captured, size_t capacity);
static uint8_t* check_test_driver_read_aligned(const char* path, uint64_t* length);

// MMX's intrinsics by the driver (#136): a program of every kind of their functions (arithmetic, comparisons, packs and
// unpacks, shifts, conversions between them and integers, floats and doubles, halves' loads and stores, the stream, the
// masked move, SSSE3's and SSE2's), which clang 23's <immintrin.h> writes as builtins of generic 8-byte vectors, linked
// for znver5 by both paths into the same bytes (its 8-bit shuffles take AVX-512 VBMI), and printing, where the host has
// the AVX-512 features its rows take, what clang -O0's build of it for x86-64-v4 prints.
static void check_test_driver_mmx(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "#include <immintrin.h>\n"
                                  "#include <stdio.h>\n"
                                  "#include <string.h>\n"
                                  "static void show(const char* name, __m64 v) { long long x; memcpy(&x, &v, 8); printf(\"%s %016llx\\n\", name, x); }\n"
                                  "static void show128(const char* name, __m128i v) { long long x[2]; memcpy(x, &v, 16); printf(\"%s %016llx %016llx\\n\", name, x[1], x[0]); }\n"
                                  "static void showps(const char* name, __m128 v) { float x[4]; memcpy(x, &v, 16); printf(\"%s %g %g %g %g\\n\", name, x[0], x[1], x[2], x[3]); }\n"
                                  "static void showpd(const char* name, __m128d v) { double x[2]; memcpy(x, &v, 16); printf(\"%s %g %g\\n\", name, x[0], x[1]); }\n"
                                  "int main(int argc, char** argv)\n"
                                  "{\n"
                                  "    (void)argv;\n"
                                  "    int n = argc + 6;\n"
                                  "    __m64 a = _mm_set_pi8((char)n, 2, 3, 4, (char)(n * 7), 6, 7, (char)-n);\n"
                                  "    __m64 b = _mm_set_pi16((short)(n * 1000), -3, 7, (short)n);\n"
                                  "    __m128 f = _mm_setr_ps(1.5f, -2.25f, (float)n, 100.75f);\n"
                                  "    __m128d d = _mm_setr_pd(-7.5, (double)n * 3.25);\n"
                                  "    show(\"add8\", _mm_add_pi8(a, b));\n"
                                  "    show(\"add16\", _mm_add_pi16(a, b));\n"
                                  "    show(\"add32\", _mm_add_pi32(a, b));\n"
                                  "    show(\"sub8\", _mm_sub_pi8(a, b));\n"
                                  "    show(\"adds8\", _mm_adds_pi8(a, b));\n"
                                  "    show(\"addsu8\", _mm_adds_pu8(a, b));\n"
                                  "    show(\"subs16\", _mm_subs_pi16(a, b));\n"
                                  "    show(\"madd\", _mm_madd_pi16(a, b));\n"
                                  "    show(\"mulhi\", _mm_mulhi_pi16(a, b));\n"
                                  "    show(\"mullo\", _mm_mullo_pi16(a, b));\n"
                                  "    show(\"and\", _mm_and_si64(a, b));\n"
                                  "    show(\"andnot\", _mm_andnot_si64(a, b));\n"
                                  "    show(\"or\", _mm_or_si64(a, b));\n"
                                  "    show(\"xor\", _mm_xor_si64(a, b));\n"
                                  "    show(\"cmpeq8\", _mm_cmpeq_pi8(a, a));\n"
                                  "    show(\"cmpgt16\", _mm_cmpgt_pi16(a, b));\n"
                                  "    show(\"cmpgt32\", _mm_cmpgt_pi32(b, a));\n"
                                  "    show(\"packs16\", _mm_packs_pi16(a, b));\n"
                                  "    show(\"packs32\", _mm_packs_pi32(a, b));\n"
                                  "    show(\"packsu16\", _mm_packs_pu16(a, b));\n"
                                  "    show(\"unpackhi8\", _mm_unpackhi_pi8(a, b));\n"
                                  "    show(\"unpacklo8\", _mm_unpacklo_pi8(a, b));\n"
                                  "    show(\"unpackhi32\", _mm_unpackhi_pi32(a, b));\n"
                                  "    show(\"unpacklo16\", _mm_unpacklo_pi16(a, b));\n"
                                  "    show(\"sll16\", _mm_sll_pi16(a, _mm_cvtsi32_si64(3)));\n"
                                  "    show(\"slli32\", _mm_slli_pi32(a, 5));\n"
                                  "    show(\"sllsi64\", _mm_sll_si64(a, _mm_cvtsi32_si64(9)));\n"
                                  "    show(\"srai16\", _mm_srai_pi16(b, 2));\n"
                                  "    show(\"sra32\", _mm_sra_pi32(b, _mm_cvtsi32_si64(4)));\n"
                                  "    show(\"srli64\", _mm_srli_si64(a, 12));\n"
                                  "    show(\"srl16\", _mm_srl_pi16(b, _mm_cvtsi32_si64(1)));\n"
                                  "    show(\"cvt\", _mm_cvtsi32_si64(n * 3));\n"
                                  "    printf(\"to32 %d\\n\", _mm_cvtsi64_si32(a));\n"
                                  "    printf(\"to64 %lld\\n\", _mm_cvtm64_si64(b));\n"
                                  "    show(\"from64\", _mm_cvtsi64_m64((long long)n << 40));\n"
                                  "    show(\"set1\", _mm_set1_pi16((short)n));\n"
                                  "    show(\"set1b\", _mm_set1_pi8((char)(n + 100)));\n"
                                  "    show(\"setr\", _mm_setr_pi32(n, -n));\n"
                                  "    show(\"zero\", _mm_setzero_si64());\n"
                                  "    show(\"avg8\", _mm_avg_pu8(a, b));\n"
                                  "    show(\"avg16\", _mm_avg_pu16(a, b));\n"
                                  "    showps(\"cvtpi32ps\", _mm_cvtpi32_ps(f, b));\n"
                                  "    show(\"cvtps32\", _mm_cvtps_pi32(f));\n"
                                  "    show(\"cvttps32\", _mm_cvttps_pi32(f));\n"
                                  "    showps(\"cvtpi8\", _mm_cvtpi8_ps(a));\n"
                                  "    showps(\"cvtpu8\", _mm_cvtpu8_ps(a));\n"
                                  "    showps(\"cvtpi16\", _mm_cvtpi16_ps(b));\n"
                                  "    showps(\"cvtpu16\", _mm_cvtpu16_ps(b));\n"
                                  "    showps(\"cvtpi32x2\", _mm_cvtpi32x2_ps(a, b));\n"
                                  "    show(\"cvtps16\", _mm_cvtps_pi16(f));\n"
                                  "    show(\"cvtps8\", _mm_cvtps_pi8(f));\n"
                                  "    __m64 mem[2] = { a, b };\n"
                                  "    showps(\"loadh\", _mm_loadh_pi(f, &mem[0]));\n"
                                  "    showps(\"loadl\", _mm_loadl_pi(f, &mem[1]));\n"
                                  "    _mm_storeh_pi(&mem[0], f);\n"
                                  "    _mm_storel_pi(&mem[1], f);\n"
                                  "    show(\"storeh\", mem[0]);\n"
                                  "    show(\"storel\", mem[1]);\n"
                                  "    show(\"max16\", _mm_max_pi16(a, b));\n"
                                  "    show(\"maxu8\", _mm_max_pu8(a, b));\n"
                                  "    show(\"min16\", _mm_min_pi16(a, b));\n"
                                  "    show(\"minu8\", _mm_min_pu8(a, b));\n"
                                  "    printf(\"movemask %d\\n\", _mm_movemask_pi8(a));\n"
                                  "    show(\"mulhu16\", _mm_mulhi_pu16(a, b));\n"
                                  "    show(\"sad\", _mm_sad_pu8(a, b));\n"
                                  "    __m64 stream;\n"
                                  "    _mm_stream_pi(&stream, a);\n"
                                  "    show(\"stream\", stream);\n"
                                  "    char bytes[16] = { 0 };\n"
                                  "    _mm_maskmove_si64(a, _mm_set_pi8(-1, 0, -1, 0, -1, 0, -1, -1), bytes + 3);\n"
                                  "    printf(\"maskmove\");\n"
                                  "    for (int i = 0; i < 16; i++)\n"
                                  "    {\n"
                                  "        printf(\" %d\", bytes[i]);\n"
                                  "    }\n"
                                  "    printf(\"\\n\");\n"
                                  "    show(\"shuffle16\", _mm_shuffle_pi16(b, 0x1b));\n"
                                  "    show(\"abs8\", _mm_abs_pi8(a));\n"
                                  "    show(\"abs16\", _mm_abs_pi16(b));\n"
                                  "    show(\"abs32\", _mm_abs_pi32(b));\n"
                                  "    show(\"hadd16\", _mm_hadd_pi16(a, b));\n"
                                  "    show(\"hadd32\", _mm_hadd_pi32(a, b));\n"
                                  "    show(\"hadds16\", _mm_hadds_pi16(a, b));\n"
                                  "    show(\"hsub16\", _mm_hsub_pi16(a, b));\n"
                                  "    show(\"hsub32\", _mm_hsub_pi32(a, b));\n"
                                  "    show(\"hsubs16\", _mm_hsubs_pi16(a, b));\n"
                                  "    show(\"maddubs\", _mm_maddubs_pi16(a, b));\n"
                                  "    show(\"mulhrs\", _mm_mulhrs_pi16(a, b));\n"
                                  "    show(\"shuffle8\", _mm_shuffle_pi8(a, b));\n"
                                  "    show(\"sign8\", _mm_sign_pi8(a, b));\n"
                                  "    show(\"sign16\", _mm_sign_pi16(a, b));\n"
                                  "    show(\"sign32\", _mm_sign_pi32(a, b));\n"
                                  "    show(\"addsi64\", _mm_add_si64(a, b));\n"
                                  "    show(\"subsi64\", _mm_sub_si64(a, b));\n"
                                  "    show(\"mulsu32\", _mm_mul_su32(a, b));\n"
                                  "    show(\"cvtpd32\", _mm_cvtpd_pi32(d));\n"
                                  "    show(\"cvttpd32\", _mm_cvttpd_pi32(d));\n"
                                  "    showpd(\"cvtpi32pd\", _mm_cvtpi32_pd(b));\n"
                                  "    show(\"movepi64\", _mm_movepi64_pi64(_mm_set_epi64x(n, -n)));\n"
                                  "    show128(\"movpi64\", _mm_movpi64_epi64(a));\n"
                                  "    show128(\"set1epi64\", _mm_set1_epi64(b));\n"
                                  "    show128(\"setepi64\", _mm_set_epi64(a, b));\n"
                                  "    show128(\"setrepi64\", _mm_setr_epi64(a, b));\n"
                                  "    _mm_empty();\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char outputs[3][256];
    snprintf(source, sizeof(source), "%s/mmx.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/mmx", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/mmx-scalar", directory);
    snprintf(outputs[2], sizeof(outputs[2]), "%s/mmx-clang", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "the MMX intrinsics' program written");
    bool linked = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* text = NULL;
        char* const vector_arguments[] = { (char*)executable, "-march=znver5", "-o", outputs[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-march=znver5", "-o", outputs[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "MMX's intrinsics link" : "MMX's intrinsics link, scalar");
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        linked = linked && status == 0;
        free(text);
    }
    check_test_expect(test, linked && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable of MMX's intrinsics");
    const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) | BUSTER_X86_FEATURE(AVX512DQ) |
                              BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512VBMI) | BUSTER_X86_FEATURE(AVX512VBMI2) |
                              BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512VPOPCNTDQ);
    if (linked && (x86_target_native().features & required) == required)
    {
        char* text = NULL;
        char* const reference[] = { (char*)clang, "-O0", "-march=x86-64-v4", "-o", outputs[2], source, NULL };
        const int built = check_test_driver_spawn(reference, &text);
        free(text);
        static char expected[8192];
        static char captured[8192];
        const bool ran = built == 0 && check_test_driver_capture(outputs[2], expected, sizeof(expected)) &&
                         check_test_driver_capture(outputs[0], captured, sizeof(captured));
        check_test_expect(test, ran && strlen(expected) > 2000 && strcmp(captured, expected) == 0, "MMX's intrinsics compute what clang's build of them does");
        if (ran && strcmp(captured, expected) != 0)
        {
            fprintf(stderr, "MMX's intrinsics:\n%s\nclang's build:\n%s\n", captured, expected);
        }
    }
    else if (linked)
    {
        printf("  MMX's intrinsics: native execution skipped (the host lacks the AVX-512 features their rows take); the program links\n");
    }
    unlink(source);
    unlink(outputs[0]);
    unlink(outputs[1]);
    unlink(outputs[2]);
}


// Half-precision scalars' intrinsics by the driver (#136): a program of the clang 23 headers' functions that pass and
// return _Float16 and __bf16 values (AVX512-FP16's sets of 8, 16 and 32 of them, their first elements, the scalar
// arithmetic, conversions from integers, loads, stores and moves of them, F16C's _cvtsh_ss and AVX512-BF16's
// conversions), linked for sapphirerapids by both paths into the same bytes, and printing, where the host has AVX512-FP16
// and AVX512-BF16, what clang -O0's build of it prints.
static void check_test_driver_halves(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "#include <immintrin.h>\n"
                                  "#include <stdio.h>\n"
                                  "#include <string.h>\n"
                                  "static void show(const char* name, const void* p, unsigned long n) { const unsigned short* x = p; printf(\"%s\", name); for (unsigned long i = 0; i < n / 2; i++) printf(\" %04x\", x[i]); printf(\"\\n\"); }\n"
                                  "static void showh(const char* name, _Float16 h) { show(name, &h, 2); }\n"
                                  "int main(int argc, char** argv)\n"
                                  "{\n"
                                  "    (void)argv;\n"
                                  "    int n = argc + 6;\n"
                                  "    _Float16 a = (_Float16)n / (_Float16)3, b = (_Float16)-2.5, c = (_Float16)(n * 1000);\n"
                                  "    __m128h v = _mm_set_ph(a, b, c, 2, 3, 4, 5, (_Float16)n);\n"
                                  "    __m128h s = _mm_set_sh(b);\n"
                                  "    show(\"set\", &v, 16);\n"
                                  "    __m128h r = _mm_setr_ph(a, b, c, 2, 3, 4, 5, (_Float16)n);\n"
                                  "    show(\"setr\", &r, 16);\n"
                                  "    __m128h one = _mm_set1_ph(a);\n"
                                  "    show(\"set1\", &one, 16);\n"
                                  "    __m128h zero = _mm_setzero_ph();\n"
                                  "    show(\"zero\", &zero, 16);\n"
                                  "    show(\"set_sh\", &s, 16);\n"
                                  "    showh(\"cvtsh_h\", _mm_cvtsh_h(v));\n"
                                  "    __m128h t = _mm_add_sh(v, s);\n"
                                  "    show(\"add_sh\", &t, 16);\n"
                                  "    t = _mm_sub_sh(v, s);\n"
                                  "    show(\"sub_sh\", &t, 16);\n"
                                  "    t = _mm_mul_sh(v, s);\n"
                                  "    show(\"mul_sh\", &t, 16);\n"
                                  "    t = _mm_div_sh(v, s);\n"
                                  "    show(\"div_sh\", &t, 16);\n"
                                  "    t = _mm_cvti32_sh(v, -n * 77);\n"
                                  "    show(\"cvti32_sh\", &t, 16);\n"
                                  "    t = _mm_cvti64_sh(v, (long long)n * 999999);\n"
                                  "    show(\"cvti64_sh\", &t, 16);\n"
                                  "    t = _mm_cvtu32_sh(v, (unsigned)n * 3001u);\n"
                                  "    show(\"cvtu32_sh\", &t, 16);\n"
                                  "    t = _mm_cvtu64_sh(v, (unsigned long long)n << 40);\n"
                                  "    show(\"cvtu64_sh\", &t, 16);\n"
                                  "    _Float16 memory[2] = { c, a };\n"
                                  "    t = _mm_load_sh(&memory[1]);\n"
                                  "    show(\"load_sh\", &t, 16);\n"
                                  "    _mm_store_sh(&memory[0], v);\n"
                                  "    show(\"store_sh\", memory, 4);\n"
                                  "    t = _mm_move_sh(v, s);\n"
                                  "    show(\"move_sh\", &t, 16);\n"
                                  "    __m256h w = _mm256_set_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, (_Float16)n);\n"
                                  "    show(\"set256\", &w, 32);\n"
                                  "    w = _mm256_setr_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, (_Float16)n);\n"
                                  "    show(\"setr256\", &w, 32);\n"
                                  "    w = _mm256_set1_ph(b);\n"
                                  "    show(\"set1_256\", &w, 32);\n"
                                  "    showh(\"cvtsh_h256\", _mm256_cvtsh_h(_mm256_set1_ph(c)));\n"
                                  "    w = _mm256_setzero_ph();\n"
                                  "    show(\"zero256\", &w, 32);\n"
                                  "    __m512h z = _mm512_set1_ph(c);\n"
                                  "    show(\"set1_512\", &z, 64);\n"
                                  "    z = _mm512_set_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, (_Float16)n);\n"
                                  "    show(\"set512\", &z, 64);\n"
                                  "    z = _mm512_setr_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, (_Float16)n);\n"
                                  "    show(\"setr512\", &z, 64);\n"
                                  "    showh(\"cvtsh_h512\", _mm512_cvtsh_h(z));\n"
                                  "    z = _mm512_setzero_ph();\n"
                                  "    show(\"zero512\", &z, 64);\n"
                                  "    float f = _cvtsh_ss((unsigned short)(0x3c00 + n));\n"
                                  "    show(\"cvtsh_ss\", &f, 4);\n"
                                  "    __bf16 bf = _mm_cvtness_sbh((float)n / 3.0f);\n"
                                  "    show(\"cvtness_sbh\", &bf, 2);\n"
                                  "    float back = _mm_cvtsbh_ss(bf);\n"
                                  "    show(\"cvtsbh_ss\", &back, 4);\n"
                                  "    __m512h u = _mm512_setr_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, (_Float16)-n, (_Float16)n);\n"
                                  "    showh(\"reduce_add_ph\", _mm_reduce_add_ph(v));\n"
                                  "    showh(\"reduce_mul_ph\", _mm_reduce_mul_ph(v));\n"
                                  "    showh(\"reduce_max_ph\", _mm_reduce_max_ph(v));\n"
                                  "    showh(\"reduce_min_ph\", _mm_reduce_min_ph(v));\n"
                                  "    showh(\"reduce_add_ph256\", _mm256_reduce_add_ph(_mm512_castph512_ph256(u)));\n"
                                  "    showh(\"reduce_mul_ph256\", _mm256_reduce_mul_ph(_mm512_castph512_ph256(u)));\n"
                                  "    showh(\"reduce_max_ph256\", _mm256_reduce_max_ph(_mm512_castph512_ph256(u)));\n"
                                  "    showh(\"reduce_min_ph256\", _mm256_reduce_min_ph(_mm512_castph512_ph256(u)));\n"
                                  "    showh(\"reduce_add_ph512\", _mm512_reduce_add_ph(u));\n"
                                  "    showh(\"reduce_mul_ph512\", _mm512_reduce_mul_ph(u));\n"
                                  "    showh(\"reduce_max_ph512\", _mm512_reduce_max_ph(u));\n"
                                  "    showh(\"reduce_min_ph512\", _mm512_reduce_min_ph(u));\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char outputs[3][256];
    snprintf(source, sizeof(source), "%s/halves.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/halves", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/halves-scalar", directory);
    snprintf(outputs[2], sizeof(outputs[2]), "%s/halves-clang", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "the half-precision intrinsics' program written");
    bool linked = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* text = NULL;
        char* const vector_arguments[] = { (char*)executable, "-march=sapphirerapids", "-o", outputs[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-march=sapphirerapids", "-o", outputs[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "the half-precision intrinsics link" : "the half-precision intrinsics link, scalar");
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        linked = linked && status == 0;
        free(text);
    }
    check_test_expect(test, linked && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable of the half-precision intrinsics");
    const X86Target native = x86_target_native();
    const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL) |
                              BUSTER_X86_FEATURE(AVX512BF16) | BUSTER_X86_FEATURE(F16C);
    if (linked && (native.features & required) == required && (native.extensions & BUSTER_X86_EXTENSION(AVX512FP16)) != 0)
    {
        char* text = NULL;
        char* const reference[] = { (char*)clang, "-O0", "-march=sapphirerapids", "-o", outputs[2], source, NULL };
        const int built = check_test_driver_spawn(reference, &text);
        free(text);
        static char expected[8192];
        static char captured[8192];
        const bool ran = built == 0 && check_test_driver_capture(outputs[2], expected, sizeof(expected)) &&
                         check_test_driver_capture(outputs[0], captured, sizeof(captured));
        check_test_expect(test, ran && strlen(expected) > 1000 && strcmp(captured, expected) == 0, "the half-precision intrinsics compute what clang's build of them does");
        if (ran && strcmp(captured, expected) != 0)
        {
            fprintf(stderr, "half-precision intrinsics:\n%s\nclang's build:\n%s\n", captured, expected);
        }
    }
    else if (linked)
    {
        printf("  half-precision intrinsics: native execution skipped (the host lacks AVX512-FP16 or AVX512-BF16); the program links\n");
    }
    unlink(source);
    unlink(outputs[0]);
    unlink(outputs[1]);
    unlink(outputs[2]);
}

// __builtin_dump_struct by the driver (#148): a program of clang's dumps of records (every kind of field: the integer
// types, bool and the character types as numbers, size_t's, ptrdiff_t's and intmax_t's typedefs, enums, the floating
// types, strings, pointers, arrays, bit-fields and unnamed ones, anonymous members, nested and unnamed records with
// their scopes (a pointer member's record's too), those of records in functions, of parameters as their declarations
// spell them (a record's array member among a parameter's tokens), of no written prototype, typedefs of qualified types
// with and without qualifiers written with them, a typedef named in one place of a function pointer's types and not in
// another of the same type (its return or its parameter, fields and parameters alike), a typedef of a function pointer
// or an array named where the same type is spelled out elsewhere in the declaration, unions, vectors, a packed record,
// _Atomic, complex, __int128 and _BitInt fields, members whose address space a macro writes (by its declaration's macro, #1447
// item 2) or a typedef, records of a function's body with its typedefs, a record of 5000 fields,
// whose 5003 lines a printer hashes, #1842), through printf, printers that return or take a struct, a 128-bit integer, a
// _Float16 or a vector, or return a complex value or take a long double (#1842), a
// variadic printer of its own that prints each format's operands (a pointer as its offset from a record's), a printer
// through a pointer that a call returns, an unprototyped one, one that takes a double and one that counts its calls,
// the rest of the arguments evaluated again for each call: by the direct path, by --scalar and by --tier1 with and
// without it, each printing what clang's build does.
static void check_test_driver_dump_struct(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] =
                                  "#include <stdarg.h>\n"
                                  "#include <stddef.h>\n"
                                  "#include <stdint.h>\n"
                                  "#include <stdio.h>\n"
                                  "#include <string.h>\n"
                                  "typedef struct { int q; short r; } Anon;\n"
                                  "typedef struct Point { int x, y; } Point;\n"
                                  "enum Color { RED, GREEN };\n"
                                  "enum Neg { MINUS = -1 };\n"
                                  "typedef enum { A1, B1 } AnonE;\n"
                                  "typedef unsigned long mysize;\n"
                                  "typedef size_t wrapped;\n"
                                  "typedef const char *cstr;\n"
                                  "typedef cstr cstr2;\n"
                                  "typedef float V4 __attribute__((vector_size(16)));\n"
                                  "typedef int I4 __attribute__((vector_size(16)));\n"
                                  "typedef int E2 __attribute__((ext_vector_type(2)));\n"
                                  "typedef double D2 __attribute__((vector_size(16)));\n"
                                  "typedef signed char C16 __attribute__((vector_size(16)));\n"
                                  "typedef long L2 __attribute__((vector_size(16)));\n"
                                  "typedef unsigned short S8 __attribute__((vector_size(16)));\n"
                                  "struct S {\n"
                                  "    _Bool b; char c; signed char sc; unsigned char uc; short s; unsigned short us;\n"
                                  "    int i; unsigned u; long l; unsigned long ul; long long ll; unsigned long long ull;\n"
                                  "    float f; double d; long double ld;\n"
                                  "    const char *str; char *mstr; signed char *sstr; const unsigned char *ustr; void *vp; int *ip; cstr2 s2; const cstr cs;\n"
                                  "    int arr[3]; char carr[4];\n"
                                  "    uint32_t u32; size_t sz; wrapped wsz; mysize ms; int8_t i8; ptrdiff_t pd; intmax_t im; uintmax_t um;\n"
                                  "    enum Color col; enum Neg ng; AnonE ae;\n"
                                  "    Point p; Anon an; struct Point *pp; const volatile int cvi;\n"
                                  "    int bf : 3; unsigned ubf : 5; int : 4; _Bool bb : 1; long long lbf : 40; unsigned long long ubig : 63; enum Color cbf : 2;\n"
                                  "    struct { int ia; union { int ib; float fc; }; struct { char deep; }; };\n"
                                  "    struct { int named_in; } named;\n"
                                  "    union U { int x; char y; } un;\n"
                                  "    __int128 i128; _Complex double cd; _Atomic int at; int (*fp)(int); unsigned _BitInt(40) ub; Point pts[2];\n"
                                  "    V4 v4; I4 i4;\n"
                                  "};\n"
                                  "struct __attribute__((packed)) Packed { char a; int b : 7; unsigned c : 20; long long d; };\n"
                                  "struct Vec { E2 e; D2 d; C16 c; L2 l; S8 s; };\n"
                                  "struct O { struct I { int a; } i; struct { int b; struct { int c; } deep; } un; union { int d; } u2; };\n"
                                  "typedef struct { struct { int e; } in; } T;\n"
                                  "#define AS1 __attribute__((address_space(1)))\n"
                                  "#define ASA __attribute__((address_space(1)))\n"
                                  "#define ASN(n) __attribute__((address_space(n)))\n"
                                  "typedef AS1 int T1;\n"
                                  "struct Spaces { AS1 int *f; ASA int *g; __attribute__((address_space(1))) int *h; ASN(3) char *i; T1 *t; AS1 int *const *pp; int AS1 *w, *v; };\n"
                                  "static const char *base;\n"
                                  "static int pr(int tag, const char *f, ...)\n"
                                  "{\n"
                                  "    va_list ap;\n"
                                  "    va_start(ap, f);\n"
                                  "    printf(\"<%d>\", tag);\n"
                                  "    for (const char *p = f; *p; p++)\n"
                                  "    {\n"
                                  "        if (*p != '%') { putchar(*p); continue; }\n"
                                  "        char spec[32]; int n = 0; spec[n++] = *p++;\n"
                                  "        while (strchr(\"-+ #0123456789.\", *p)) spec[n++] = *p++;\n"
                                  "        int v = 0; if (*p == 'v') { v = 1; p++; while (*p >= '0' && *p <= '9') p++; }\n"
                                  "        char len[3] = { 0 }; int ln = 0; while (strchr(\"hlLzjt\", *p)) len[ln++] = *p, spec[n++] = *p++;\n"
                                  "        char c = *p; spec[n++] = c; spec[n] = 0;\n"
                                  "        if (v && c == 'f') { V4 x = va_arg(ap, V4); printf(\"[%g %g %g %g]\", x[0], x[1], x[2], x[3]); }\n"
                                  "        else if (v) { I4 x = va_arg(ap, I4); printf(\"[%d %d %d %d]\", x[0], x[1], x[2], x[3]); }\n"
                                  "        else if (c == 's') { const char *s = va_arg(ap, const char *); printf(spec, s ? s : \"(null)\"); }\n"
                                  "        else if (c == 'p') { const char *q = va_arg(ap, const char *); if (q) printf(\"@%ld\", (long)(q - base)); else printf(\"nil\"); }\n"
                                  "        else if (c == 'f' && len[0] == 'L') printf(spec, va_arg(ap, long double));\n"
                                  "        else if (c == 'f') printf(spec, va_arg(ap, double));\n"
                                  "        else if (!strcmp(len, \"ll\")) printf(spec, va_arg(ap, long long));\n"
                                  "        else if (len[0] == 'l' || len[0] == 'z' || len[0] == 'j' || len[0] == 't') printf(spec, va_arg(ap, long));\n"
                                  "        else printf(spec, va_arg(ap, int));\n"
                                  "    }\n"
                                  "    va_end(ap);\n"
                                  "    return 0;\n"
                                  "}\n"
                                  "static int calls;\n"
                                  "static int next(void) { return calls++; }\n"
                                  "typedef int (*P)(int, const char *, ...);\n"
                                  "static P get(void) { printf(\"get\\n\"); return pr; }\n"
                                  "static int kr();\n"
                                  "static int kr(tag, f) int tag; const char *f; { printf(\"kr %d %s\", tag, f); return 0; }\n"
                                  "struct Ctx { int calls; };\n"
                                  "static int counted(struct Ctx *c, const char *f, ...) { (void)f; c->calls++; return 0; }\n"
                                  "static void prefixed(double x, const char *f, ...) { va_list ap; va_start(ap, f); printf(\"[%g] \", x); vprintf(f, ap); va_end(ap); }\n"
                                  "static void scoped(int a, const char *b, ...)\n"
                                  "{\n"
                                  "    (void)b;\n"
                                  "    typedef struct { uint16_t w; } Word;\n"
                                  "    struct Local { uint32_t u; size_t z; Word word; struct { int q; } in; } l = { 1, 2, { 3 }, { a } };\n"
                                  "    __builtin_dump_struct(&l, printf);\n"
                                  "    __builtin_dump_struct(&l.in, printf);\n"
                                  "    struct { struct { int k; } in; } r = { { a } };\n"
                                  "    __builtin_dump_struct(&r.in, printf);\n"
                                  "}\n"
                                  "static void old_style(a) int a; { struct Old { struct { int k; } in; } r = { { a } }; __builtin_dump_struct(&r.in, printf); }\n"
                                  "typedef int Arr4[4];\n"
                                  "static void spelled(size_t n, cstr text, uint8_t *bytes, const size_t c, Arr4 arr, int (*cb)(size_t), int plain, ...)\n"
                                  "{\n"
                                  "    (void)n; (void)text; (void)bytes; (void)c; (void)arr; (void)cb;\n"
                                  "    struct Spelled { struct { int k; } in; } r = { { plain } };\n"
                                  "    __builtin_dump_struct(&r.in, printf);\n"
                                  "}\n"
                                  "typedef const int CI;\n"
                                  "typedef int *const PC;\n"
                                  "struct Qualified { CI ci; PC pc; const CI cci; volatile CI vci; CI const cic; };\n"
                                  "static void qualified(CI x, PC p, const struct { int values[4]; } *const arg)\n"
                                  "{\n"
                                  "    (void)x; (void)p; (void)arg;\n"
                                  "    struct Inner { struct { int n; } inner; } r = { { x } };\n"
                                  "    __builtin_dump_struct(&r.inner, printf);\n"
                                  "}\n"
                                  "typedef int I;\n"
                                  "struct Placed { int (*a)(I); I (*b)(int); I c, (*d)(long), e; I (*(*f)(int))(I); size_t (*h)(const char *, ...); int (*k)(void); };\n"
                                  "static void placed_return(int (*cb)(I))\n"
                                  "{\n"
                                  "    (void)cb;\n"
                                  "    struct Outer1 { struct { int value; } inner; } r = { { 1 } };\n"
                                  "    __builtin_dump_struct(&r.inner, printf);\n"
                                  "}\n"
                                  "static void placed_parameter(I (*cb)(int), I (*(*nested)(int))(I), ...)\n"
                                  "{\n"
                                  "    (void)cb; (void)nested;\n"
                                  "    struct Outer2 { struct { int value; } inner; } r = { { 2 } };\n"
                                  "    __builtin_dump_struct(&r.inner, printf);\n"
                                  "}\n"
                                  "typedef int (*FP)(int);\n"
                                  "typedef int A4[4];\n"
                                  "struct Repeated { int (*(*a)(FP))(int); FP (*b)(int (*)(int)); A4 *p; int (*q)[4]; A4 *(*r)(int (*)[4]); };\n"
                                  "static void repeated(int (*(*cb)(FP))(int), int (*(*rev)(int (*)(int)))(FP))\n"
                                  "{\n"
                                  "    (void)cb; (void)rev;\n"
                                  "    struct Outer3 { struct { int x; } inner; } r = { { 3 } };\n"
                                  "    __builtin_dump_struct(&r.inner, printf);\n"
                                  "}\n"
                                  "struct Pointee { struct { int x; } *p; void (*fn)(struct { int w; } *); };\n"
                                  "struct Top2 { struct M { struct { int deep; } u; } m; struct { int x; } y; };\n"
                                  "static struct S s;\n"
                                  "#define F1(a) int a##0, a##1, a##2, a##3, a##4, a##5, a##6, a##7, a##8, a##9;\n"
                                  "#define F2(a) F1(a##0) F1(a##1) F1(a##2) F1(a##3) F1(a##4) F1(a##5) F1(a##6) F1(a##7) F1(a##8) F1(a##9)\n"
                                  "#define F3(a) F2(a##0) F2(a##1) F2(a##2) F2(a##3) F2(a##4) F2(a##5) F2(a##6) F2(a##7) F2(a##8) F2(a##9)\n"
                                  "static struct Big { F3(f0) F3(f1) F3(f2) F3(f3) F3(f4) } big;\n"
                                  "static unsigned long big_hash = 14695981039346656037ul;\n"
                                  "static int big_lines;\n"
                                  "static int hashed(const char *format, ...)\n"
                                  "{\n"
                                  "    char line[256];\n"
                                  "    va_list list;\n"
                                  "    va_start(list, format);\n"
                                  "    int length = vsnprintf(line, sizeof(line), format, list);\n"
                                  "    va_end(list);\n"
                                  "    for (int i = 0; i < length && i < (int)sizeof(line); i += 1) { big_hash = (big_hash ^ (unsigned char)line[i]) * 1099511628211ul; }\n"
                                  "    big_lines += 1;\n"
                                  "    return length;\n"
                                  "}\n"
                                  "struct Three { long a, b, c; };\n"
                                  "struct Context { int tag; char pad[20]; };\n"
                                  "typedef int Lanes __attribute__((vector_size(16)));\n"
                                  "static struct Three by_value(const char *format, ...) { va_list list; va_start(list, format); vprintf(format, list); va_end(list); struct Three t = { 1, 2, 3 }; return t; }\n"
                                  "static int with_context(struct Context c, const char *format, ...) { va_list list; va_start(list, format); printf(\"[%d]\", c.tag); vprintf(format, list); va_end(list); return 0; }\n"
                                  "static __int128 with_wide(__int128 k, const char *format, ...) { va_list list; va_start(list, format); printf(\"<%d>\", (int)(k >> 64)); vprintf(format, list); va_end(list); return k; }\n"
                                  "static _Float16 with_half(_Float16 h, const char *format, ...) { va_list list; va_start(list, format); printf(\"<%g>\", (double)h); vprintf(format, list); va_end(list); return h; }\n"
                                  "static Lanes with_lanes(Lanes v, const char *format, ...) { va_list list; va_start(list, format); printf(\"<%d>\", v[3]); vprintf(format, list); va_end(list); return v; }\n"
                                  "static _Complex double with_complex(const char *format, ...) { va_list list; va_start(list, format); vprintf(format, list); va_end(list); return __builtin_complex(1.0, 2.0); }\n"
                                  "static int with_long_double(long double k, const char *format, ...) { va_list list; va_start(list, format); printf(\"<%Lg>\", k); vprintf(format, list); va_end(list); return 0; }\n"
                                  "static struct Packed packed = { 'z', -5, 99999, -77 };\n"
                                  "int main(int argc, char **argv)\n"
                                  "{\n"
                                  "    (void)argv;\n"
                                  "    int n = argc;\n"
                                  "    base = (const char *)&s;\n"
                                  "    s.b = 1; s.c = -3; s.sc = -4; s.uc = 250; s.s = -300; s.us = 65000; s.i = -7 * n; s.u = 4000000000u; s.l = -8; s.ul = 9; s.ll = -10; s.ull = 11;\n"
                                  "    s.f = 1.5f; s.d = -2.25; s.ld = 3.125L; s.str = \"hello\"; s.mstr = \"a string longer than thirty-two characters for sure\"; s.vp = &s.i;\n"
                                  "    s.s2 = \"two\"; s.u32 = 5; s.sz = 6; s.wsz = 7; s.ms = 8; s.i8 = -9; s.pd = -10; s.im = -11; s.um = 12; s.col = GREEN; s.ng = MINUS; s.ae = B1;\n"
                                  "    s.p.x = 1; s.p.y = 2; s.an.q = 3; s.an.r = 4; s.pp = &s.p; s.bf = -2; s.ubf = 17; s.bb = 1; s.lbf = -12345678901LL; s.ubig = 0x7fffffffffffffffULL;\n"
                                  "    s.cbf = GREEN; s.ia = 13; s.ib = 14; s.deep = 'x'; s.named.named_in = 15; s.un.x = 0x41; s.v4 = (V4){ 1, 2, 3, 4 }; s.i4 = (I4){ -1, 2, -3, 4 };\n"
                                  "    __builtin_dump_struct(&s, pr, 1);\n"
                                  "    __builtin_dump_struct(&s.p, printf);\n"
                                  "    Anon an = { 5, 6 };\n"
                                  "    __builtin_dump_struct(&an, printf);\n"
                                  "    union U u = { 0x42 };\n"
                                  "    __builtin_dump_struct(&u, printf);\n"
                                  "    struct Spaces spaces = { 0 };\n"
                                  "    __builtin_dump_struct(&spaces, printf);\n"
                                  "    __builtin_dump_struct(&s.named, pr, next());\n"
                                  "    printf(\"calls=%d\\n\", calls);\n"
                                  "    __builtin_dump_struct(&an, get(), calls++);\n"
                                  "    printf(\"calls=%d\\n\", calls);\n"
                                  "    __builtin_dump_struct(&s.p, kr, 2);\n"
                                  "    __builtin_dump_struct(&packed, printf);\n"
                                  "    struct Vec v = { { 1, 2 }, { 1.5, -2.5 }, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }, { 7, -8 }, { 1, 2, 3, 4, 5, 6, 7, 8 } };\n"
                                  "    __builtin_dump_struct(&v, printf);\n"
                                  "    struct O o = { { 1 }, { 2, { 3 } }, { 4 } };\n"
                                  "    T t = { { 5 } };\n"
                                  "    struct { int z; } local = { 6 };\n"
                                  "    __builtin_dump_struct(&o.i, printf);\n"
                                  "    __builtin_dump_struct(&o.un, printf);\n"
                                  "    __builtin_dump_struct(&o.un.deep, printf);\n"
                                  "    __builtin_dump_struct(&o.u2, printf);\n"
                                  "    __builtin_dump_struct(&t.in, printf);\n"
                                  "    __builtin_dump_struct(&local, printf);\n"
                                  "    __builtin_dump_struct(&o, printf);\n"
                                  "    struct Ctx c = { 0 };\n"
                                  "    __builtin_dump_struct(&o, counted, &c);\n"
                                  "    printf(\"counted=%d\\n\", c.calls);\n"
                                  "    __builtin_dump_struct(&o.i, prefixed, 1.5f);\n"
                                  "    const struct Point *cp = &s.p;\n"
                                  "    __builtin_dump_struct(cp, printf);\n"
                                  "    scoped(n, \"x\");\n"
                                  "    old_style(n);\n"
                                  "    struct Top2 top2 = { { { 7 } }, { 8 } };\n"
                                  "    __builtin_dump_struct(&top2.m.u, printf);\n"
                                  "    __builtin_dump_struct(&top2.y, printf);\n"
                                  "    struct Pointee pointee;\n"
                                  "    __typeof__(*pointee.p) pointed = { 42 };\n"
                                  "    pointee.p = &pointed;\n"
                                  "    __builtin_dump_struct(pointee.p, printf);\n"
                                  "    spelled(1, \"t\", 0, 2, 0, 0, n);\n"
                                  "    struct Qualified q = { 1, 0, 2, 3, 4 };\n"
                                  "    __builtin_dump_struct(&q, printf);\n"
                                  "    qualified(n, 0, 0);\n"
                                  "    struct Placed placed = { 0 };\n"
                                  "    __builtin_dump_struct(&placed, printf);\n"
                                  "    placed_return(0);\n"
                                  "    placed_parameter(0, 0);\n"
                                  "    struct Repeated repeated_fields = { 0 };\n"
                                  "    __builtin_dump_struct(&repeated_fields, printf);\n"
                                  "    repeated(0, 0);\n"
                                  "    struct Context context = { 7 };\n"
                                  "    __builtin_dump_struct(&s.p, by_value);\n"
                                  "    __builtin_dump_struct(&s.p, with_context, context);\n"
                                  "    __builtin_dump_struct(&s.p, with_wide, (__int128)n << 64);\n"
                                  "    __builtin_dump_struct(&s.p, with_half, (_Float16)0.5f * (_Float16)n);\n"
                                  "    __builtin_dump_struct(&s.p, with_lanes, (Lanes){ 1, 2, 3, n });\n"
                                  "    __builtin_dump_struct(&s.p, with_complex);\n"
                                  "    __builtin_dump_struct(&s.p, with_long_double, 2.5L * n);\n"
                                  "    big.f0000 = 1; big.f2345 = -2; big.f4999 = n;\n"
                                  "    __builtin_dump_struct(&big, hashed);\n"
                                  "    printf(\"big=%lx lines=%d\\n\", big_hash, big_lines);\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char outputs[5][256];
    snprintf(source, sizeof(source), "%s/dump_struct.c", directory);
    for (uint32_t o = 0; o < 5; o += 1)
    {
        snprintf(outputs[o], sizeof(outputs[o]), "%s/dump_struct-%u", directory, o);
    }
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "the record dumps' program written");
    char* text = NULL;
    char* const reference[] = { (char*)clang, "-O0", "-w", "-o", outputs[4], source, NULL };
    const int built = written ? check_test_driver_spawn(reference, &text) : 1;
    free(text);
    static char expected[16384];
    static char captured[16384];
    const bool ran = built == 0 && check_test_driver_capture(outputs[4], expected, sizeof(expected));
    check_test_expect(test, ran && strlen(expected) > 2000, "clang's build of the record dumps runs");
    static const char* const option_names[] = { "the direct path", "--scalar", "--tier1", "--tier1 --scalar" };
    for (uint32_t path = 0; ran && path < 4; path += 1)
    {
        char* plain[] = { (char*)executable, "-w", "-o", outputs[path], source, NULL };
        char* scalar[] = { (char*)executable, "--scalar", "-w", "-o", outputs[path], source, NULL };
        char* tier1[] = { (char*)executable, "--tier1", "-w", "-o", outputs[path], source, NULL };
        char* tier1_scalar[] = { (char*)executable, "--tier1", "--scalar", "-w", "-o", outputs[path], source, NULL };
        char* const* arguments = path == 0 ? plain : path == 1 ? scalar : path == 2 ? tier1 : tier1_scalar;
        text = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        const bool same = status == 0 && check_test_driver_capture(outputs[path], captured, sizeof(captured)) && strcmp(captured, expected) == 0;
        char description[256];
        snprintf(description, sizeof(description), "the record dumps by %s print what clang's build does", option_names[path]);
        check_test_expect(test, same, description);
        if (status == 0 && !same)
        {
            fprintf(stderr, "record dumps by %s:\n%s\nclang's build:\n%s\n", option_names[path], captured, expected);
        }
    }
    unlink(source);
    for (uint32_t o = 0; o < 5; o += 1)
    {
        unlink(outputs[o]);
    }
}

// __builtin_dump_struct's rest of the arguments converted as a call's (#1842): complex values to a complex or a real
// printer parameter, of each floating type and of int, a real one to a complex parameter, complex values through the
// variadic part, a call's complex value evaluated again for each line, a transparent union's parameter of each of its
// members, of a void pointer and of the union itself, and _Atomic aggregates' parameters (8 and 4 bytes, lock-free, of a
// plain and of an _Atomic argument), beside a complex long double's and an _Atomic int's, whose reads each line evaluates
// again (their atomic plans past the nodes' own, CheckLowerInitializers.extra_atomics): by the direct path, by --scalar
// and by --tier1 with and without it, each printing what clang -O0's build does.
static void check_test_driver_dump_struct_rest(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] =
        "#include <stdarg.h>\n"
        "#include <stdio.h>\n"
        "struct S { int a; double b; };\n"
        "struct A { int x; int y; };\n"
        "struct B { char x[3]; };\n"
        "typedef union __attribute__((transparent_union)) { int* i; const char* c; } U;\n"
        "static int calls;\n"
        "static int pr(_Complex double z, const char* f, ...) { va_list ap; va_start(ap, f); printf(\"(%g,%g)\", __real__ z, __imag__ z); vprintf(f, ap); va_end(ap); return 0; }\n"
        "static int pf(_Complex float z, int k, const char* f, ...) { va_list ap; va_start(ap, f); printf(\"(%g,%g,%d)\", __real__ z, __imag__ z, k); vprintf(f, ap); va_end(ap); return 0; }\n"
        "static int pl(_Complex long double z, const char* f, ...) { va_list ap; va_start(ap, f); printf(\"(%Lg,%Lg)\", __real__ z, __imag__ z); vprintf(f, ap); va_end(ap); return 0; }\n"
        "static int pi(_Complex int z, const char* f, ...) { va_list ap; va_start(ap, f); printf(\"(%d,%d)\", __real__ z, __imag__ z); vprintf(f, ap); va_end(ap); return 0; }\n"
        "static int pd(double d, const char* f, ...) { va_list ap; va_start(ap, f); printf(\"(%g)\", d); vprintf(f, ap); va_end(ap); return 0; }\n"
        "static int pv(int k, ...) { va_list ap; va_start(ap, k); _Complex double z = va_arg(ap, _Complex double); printf(\"<%d|%g,%g>\", k, __real__ z, __imag__ z); calls++; va_end(ap); return 0; }\n"
        "static int pvf(int k, ...) { va_list ap; va_start(ap, k); _Complex float z = va_arg(ap, _Complex float); _Complex long double w = va_arg(ap, _Complex long double); printf(\"<%d|%g,%g|%Lg,%Lg>\", k, __real__ z, __imag__ z, __real__ w, __imag__ w); calls++; va_end(ap); return 0; }\n"
        "static int pu(U u, const char* f, ...) { va_list ap; va_start(ap, f); printf(\"[%d]\", *u.i); vprintf(f, ap); va_end(ap); return 0; }\n"
        "static int pa(_Atomic struct A a, const char* f, ...) { struct A v = a; va_list ap; va_start(ap, f); printf(\"[%d,%d]\", v.x, v.y); vprintf(f, ap); va_end(ap); return 0; }\n"
        "static int pb(_Atomic struct B a, const char* f, ...) { struct B v = a; va_list ap; va_start(ap, f); printf(\"[%d%d%d]\", v.x[0], v.x[1], v.x[2]); vprintf(f, ap); va_end(ap); return 0; }\n"
        "static int pk(int k, const char* f, ...) { va_list ap; va_start(ap, f); printf(\"{%d}\", k); vprintf(f, ap); va_end(ap); return 0; }\n"
        "static _Complex double next(int* n) { *n += 1; return *n + 0.5i; }\n"
        "int main(void) {\n"
        "    struct S s = { 1, 2.5 };\n"
        "    _Complex double z = 1.0 + 2.0i;\n"
        "    _Complex float zf = 3.0f - 1.0if;\n"
        "    int n = 0;\n"
        "    __builtin_dump_struct(&s, pr, z);\n"
        "    __builtin_dump_struct(&s, pr, zf);\n"
        "    __builtin_dump_struct(&s, pf, z, 7);\n"
        "    __builtin_dump_struct(&s, pl, (_Complex long double)(5.0L + 6.0iL));\n"
        "    __builtin_dump_struct(&s, pi, (_Complex int)(3 + 4i));\n"
        "    __builtin_dump_struct(&s, pr, 9);\n"
        "    __builtin_dump_struct(&s, pd, z);\n"
        "    __builtin_dump_struct(&s, pr, next(&n));\n"
        "    __builtin_dump_struct(&s, pv, 1, z);\n"
        "    __builtin_dump_struct(&s, pvf, 2, zf, (_Complex long double)(7.0L - 8.0iL));\n"
        "    int k = 42;\n"
        "    U u;\n"
        "    u.i = &k;\n"
        "    struct A a = { 3, 4 };\n"
        "    struct B b = { { 5, 6, 7 } };\n"
        "    _Atomic struct A aa = a;\n"
        "    __builtin_dump_struct(&s, pu, &k);\n"
        "    __builtin_dump_struct(&s, pu, u);\n"
        "    __builtin_dump_struct(&s, pu, (const char*)&k);\n"
        "    __builtin_dump_struct(&s, pu, (void*)&k);\n"
        "    __builtin_dump_struct(&s, pa, a);\n"
        "    __builtin_dump_struct(&s, pb, b);\n"
        "    __builtin_dump_struct(&s, pa, aa);\n"
        "    _Atomic int ai = 7;\n"
        "    __builtin_dump_struct(&s, pk, ai);\n"
        "    printf(\"n=%d calls=%d\\n\", n, calls);\n"
        "    return 0;\n"
        "}\n";
    char source[256];
    char outputs[5][256];
    snprintf(source, sizeof(source), "%s/dump_struct_rest.c", directory);
    for (uint32_t o = 0; o < 5; o += 1)
    {
        snprintf(outputs[o], sizeof(outputs[o]), "%s/dump_struct_rest-%u", directory, o);
    }
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "the dumps' rest program written");
    char* text = NULL;
    char* const reference[] = { (char*)clang, "-O0", "-w", "-o", outputs[4], source, NULL };
    const int built = written ? check_test_driver_spawn(reference, &text) : 1;
    free(text);
    static char expected[4096];
    static char captured[4096];
    const bool ran = built == 0 && check_test_driver_capture(outputs[4], expected, sizeof(expected));
    check_test_expect(test, ran && strlen(expected) > 1000, "clang's build of the dumps' rest runs");
    static const char* const option_names[] = { "the direct path", "--scalar", "--tier1", "--tier1 --scalar" };
    for (uint32_t path = 0; ran && path < 4; path += 1)
    {
        char* plain[] = { (char*)executable, "-w", "-o", outputs[path], source, NULL };
        char* scalar[] = { (char*)executable, "--scalar", "-w", "-o", outputs[path], source, NULL };
        char* tier1[] = { (char*)executable, "--tier1", "-w", "-o", outputs[path], source, NULL };
        char* tier1_scalar[] = { (char*)executable, "--tier1", "--scalar", "-w", "-o", outputs[path], source, NULL };
        char* const* arguments = path == 0 ? plain : path == 1 ? scalar : path == 2 ? tier1 : tier1_scalar;
        text = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        const bool same = status == 0 && check_test_driver_capture(outputs[path], captured, sizeof(captured)) && strcmp(captured, expected) == 0;
        char description[256];
        snprintf(description, sizeof(description), "the dumps' rest by %s print what clang's build does", option_names[path]);
        check_test_expect(test, same, description);
        if (status == 0 && !same)
        {
            fprintf(stderr, "dumps' rest by %s:\n%s\nclang's build:\n%s\n", option_names[path], captured, expected);
        }
    }
    unlink(source);
    for (uint32_t o = 0; o < 5; o += 1)
    {
        unlink(outputs[o]);
    }
}

// __builtin_call_with_static_chain (#148): calls whose callees read the chain in r10 through naked functions (direct,
// indirect, of an aggregate result, a floating one, variadic, past six arguments, in a loop, a musttail return's, which
// clang's code makes an ordinary call; its callee, chain and argument each evaluated once, without a fixed order), built by both paths at
// Tier 0 and Tier 1 (--tier1 --verify, whose certificate checks each call's sequence), each the output of clang -O0's
// build of the same program.
static void check_test_driver_static_chain(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "struct S { long a, b, c; };\n"
                                  "__attribute__((naked, noinline)) long read_chain(void) { __asm__(\"movq %r10, %rax\\n\\tret\"); }\n"
                                  "__attribute__((naked, noinline)) long chain_plus(long x) { __asm__(\"leaq (%r10,%rdi), %rax\\n\\tret\"); }\n"
                                  "__attribute__((naked, noinline)) long chain_sum7(long a, long b, long c, long d, long e, long f, long g) { __asm__(\"leaq (%rdi,%rsi), %rax\\n\\taddq %rdx, %rax\\n\\taddq %rcx, %rax\\n\\taddq %r8, %rax\\n\\taddq %r9, %rax\\n\\taddq 8(%rsp), %rax\\n\\taddq %r10, %rax\\n\\tret\"); }\n"
                                  "__attribute__((naked, noinline)) double chain_double(double x) { __asm__(\"cvtsi2sdq %r10, %xmm1\\n\\taddsd %xmm1, %xmm0\\n\\tret\"); }\n"
                                  "__attribute__((naked, noinline)) struct S chain_struct(void) { __asm__(\"movq %rdi, %rax\\n\\tmovq %r10, (%rdi)\\n\\tmovq $2, 8(%rdi)\\n\\tmovq $3, 16(%rdi)\\n\\tret\"); }\n"
                                  "__attribute__((naked, noinline)) long chain_variadic(int n, ...) { __asm__(\"movq %r10, %rax\\n\\taddq %rsi, %rax\\n\\tret\"); }\n"
                                  "static int evaluated[3];\n"
                                  "static long (*pick(long (*f)(long)))(long) { evaluated[0] += 1; return f; }\n"
                                  "static char* chain_of(char* p) { evaluated[1] += 1; return p; }\n"
                                  "static long arg(long x) { evaluated[2] += 1; return x; }\n"
                                  "static char buffer[64];\n"
                                  "static void target(void) {}\n"
                                  "__attribute__((noinline)) long t_direct(char* p) { return __builtin_call_with_static_chain(read_chain(), p); }\n"
                                  "__attribute__((noinline)) long t_plus(long x, char* p) { return __builtin_call_with_static_chain(chain_plus(x), p); }\n"
                                  "__attribute__((noinline)) long t_swap(char* x, long p) { return __builtin_call_with_static_chain(chain_plus(p), x); }\n"
                                  "__attribute__((noinline)) long t_sum(char* p) { return __builtin_call_with_static_chain(chain_sum7(1, 2, 3, 4, 5, 6, 7), p); }\n"
                                  "__attribute__((noinline)) long t_indirect(long (*f)(long), char* p, long x) { return __builtin_call_with_static_chain(f(x), p); }\n"
                                  "__attribute__((noinline)) long t_indirect_r10(char* p, long (*f)(long), long x) { return __builtin_call_with_static_chain(f(x), p) + 1; }\n"
                                  "__attribute__((noinline)) long t_variadic(char* p) { return __builtin_call_with_static_chain(chain_variadic(1, 30L), p); }\n"
                                  "__attribute__((noinline)) long t_variadic_indirect(long (*g)(int, ...), char* p) { return __builtin_call_with_static_chain(g(1, 5L), p); }\n"
                                  "__attribute__((noinline)) long t_loop(char* p, int n) { long s = 0; for (int i = 0; i < n; i++) s += __builtin_call_with_static_chain(chain_plus(i), p + i); return s; }\n"
                                  "__attribute__((noinline)) long t_tail(long x, char* p) { __attribute__((musttail)) return __builtin_call_with_static_chain(chain_plus(x), p); }\n"
                                  "int main(int argc, char** argv)\n"
                                  "{\n"
                                  "    (void)argv;\n"
                                  "    char array[4];\n"
                                  "    long k = argc;\n"
                                  "    printf(\"%d %d %d\\n\", __builtin_call_with_static_chain(read_chain(), buffer) == (long)buffer, __builtin_call_with_static_chain(read_chain(), array) == (long)array, __builtin_call_with_static_chain(read_chain(), target) == (long)target);\n"
                                  "    printf(\"%ld %ld %g\\n\", __builtin_call_with_static_chain(chain_plus(5 * k), (char*)100), __builtin_call_with_static_chain(chain_sum7(1, 2, 3, 4, 5, 6, 7), (char*)1000), __builtin_call_with_static_chain(chain_double(0.5), (char*)2));\n"
                                  "    struct S s = __builtin_call_with_static_chain(chain_struct(), (char*)7);\n"
                                  "    printf(\"%ld %ld %ld %ld\\n\", s.a, s.b, s.c, __builtin_call_with_static_chain(chain_variadic(1, 30L), (char*)12));\n"
                                  "    long (*volatile f)(long) = chain_plus;\n"
                                  "    long (*g)(int, ...) = chain_variadic;\n"
                                  "    long q = __builtin_call_with_static_chain(pick(f)(arg(k)), chain_of((char*)40));\n"
                                  "    printf(\"%ld %d%d%d %ld\\n\", q, evaluated[0], evaluated[1], evaluated[2], __builtin_call_with_static_chain(g(1, 5L), (char*)6));\n"
                                  "    printf(\"%ld %ld %ld %ld %ld\\n\", t_direct((char*)9), t_plus(5, (char*)100), t_swap((char*)100, 5), t_sum((char*)1000), t_indirect(chain_plus, (char*)40, k));\n"
                                  "    printf(\"%ld %ld %ld %ld %ld\\n\", t_indirect_r10((char*)40, chain_plus, k), t_variadic((char*)12), t_variadic_indirect(chain_variadic, (char*)6), t_loop((char*)10, 4), t_tail(5, (char*)300));\n"
                                  "    return evaluated[0] != 1 || evaluated[1] != 1 || evaluated[2] != 1;\n"
                                  "}\n";
    char source[256];
    char outputs[5][256];
    snprintf(source, sizeof(source), "%s/static_chain.c", directory);
    for (uint32_t k = 0; k < 5; k += 1)
    {
        snprintf(outputs[k], sizeof(outputs[k]), "%s/static_chain-%u", directory, k);
    }
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "the static chain's program written");
    char* text = NULL;
    char* const reference[] = { (char*)clang, "-O0", "-o", outputs[4], source, NULL };
    const int built = written ? check_test_driver_spawn(reference, &text) : 1;
    free(text);
    static char expected[4096];
    const bool referenced = built == 0 && check_test_driver_capture(outputs[4], expected, sizeof(expected));
    check_test_expect(test, referenced && strlen(expected) > 40, "clang's build of the static chain's program runs");
    static const char* const names[4] = { "vector, Tier 0", "scalar, Tier 0", "vector, Tier 1", "scalar, Tier 1" };
    for (uint32_t mode = 0; referenced && mode < 4; mode += 1)
    {
        char* arguments[16] = { (char*)executable };
        uint32_t count = 1;
        if ((mode & 1) != 0)
        {
            arguments[count++] = "--scalar";
        }
        if (mode >= 2)
        {
            arguments[count++] = "--tier1";
            arguments[count++] = "--verify";
        }
        arguments[count++] = "-o";
        arguments[count++] = outputs[mode];
        arguments[count++] = source;
        arguments[count] = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        static char captured[4096];
        const bool ran = status == 0 && check_test_driver_capture(outputs[mode], captured, sizeof(captured));
        char description[256];
        snprintf(description, sizeof(description), "the static chain's calls compute what clang's build of them does (%s)", names[mode]);
        check_test_expect(test, ran && strcmp(captured, expected) == 0, description);
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        else if (ran && strcmp(captured, expected) != 0)
        {
            fprintf(stderr, "static chain (%s):\n%s\nclang's build:\n%s\n", names[mode], captured, expected);
        }
        free(text);
        text = NULL;
    }
    unlink(source);
    for (uint32_t k = 0; k < 5; k += 1)
    {
        unlink(outputs[k]);
    }
}

// __builtin_eh_return (#148): a function that unwinds to a handler, as libgcc's _Unwind_RaiseException does through
// uw_install_context: after __builtin_unwind_init, it writes rax's and rdx's save slots (at the CFA less 72 and 64, as its
// CFI and clang's say) and eh_returns from one of its two sites (#1420's review: the second after a branch to the first,
// rax's slot written again), at offsets 0, 16 and 32, into an assembly handler that records the registers it
// finds (the callee-saved ones restored, rax and rdx from their slots) and rsp (the CFA plus the offset); and one that
// returns normally. The same of a variadic function that reads its variadic argument first, of one within an expression
// whose left operand calls first, and of a function that makes a musttail call where it doesn't unwind, whose callee's
// return the runner records too (its callee-saved registers restored, rsp back). #1436's review: within a ?:, an &&, a
// statement expression and a comma, with a double, a long double, a struct's member and a vector pending under it (the
// expression's dead rest takes stand-ins of their classes and widths, check_lower_dead_value), and a musttail call whose
// struct argument is copied to the stack's incoming area (no rdx argument: LLVM's own teardown pops the saved rdx over
// one); and a vector expression holding a value under __builtin_trap, a terminal helper's. The runner keeps rsp 16-byte
// aligned at its call, as the ABI asks. Built by both paths at Tier 0 (with --verify, whose verifier proves its frame
// events) and with --tier1 --verify (Tier 0's frame, which Tier 1 falls back to), each the output of clang -O0's build.
static void check_test_driver_eh_return(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdarg.h>\n"
                                  "#include <stdio.h>\n"
                                  "#include <string.h>\n"
                                  "long seen[8];\n"
                                  "long before;\n"
                                  "void* target;\n"
                                  "__attribute__((naked, noinline)) long run(long offset)\n"
                                  "{\n"
                                  "    __asm__(\n"
                                  "        \"pushq %rbx\\npushq %r12\\npushq %r13\\npushq %r14\\npushq %r15\\n\"\n"
                                  "        \"movq $0x0b0b, %rbx\\nmovq $0x0c0c, %r12\\nmovq $0x0d0d, %r13\\nmovq $0x0e0e, %r14\\nmovq $0x0f0f, %r15\\n\"\n"
                                  "        \"subq $48, %rsp\\n\"\n"
                                  "        \"movq %rsp, before(%rip)\\n\"\n"
                                  "        \"leaq handler(%rip), %rsi\\n\"\n"
                                  "        \"movq $77, %rdx\\nxorl %eax, %eax\\n\"\n"
                                  "        \"movq $6, 0(%rsp)\\nmovq $7, 8(%rsp)\\nmovq $8, 16(%rsp)\\nmovq $9, 24(%rsp)\\n\"\n"
                                  "        \"call *target(%rip)\\n\"\n"
                                  "        \"movq %rbx, seen+16(%rip)\\nmovq %r12, seen+24(%rip)\\nmovq %r13, seen+32(%rip)\\nmovq %r14, seen+40(%rip)\\n\"\n"
                                  "        \"movq %r15, seen+48(%rip)\\nmovq %rsp, seen+56(%rip)\\n\"\n"
                                  "        \"jmp 1f\\n\"\n"
                                  "        \"handler:\\n\"\n"
                                  "        \"movq %rax, seen+0(%rip)\\nmovq %rdx, seen+8(%rip)\\nmovq %rbx, seen+16(%rip)\\nmovq %r12, seen+24(%rip)\\n\"\n"
                                  "        \"movq %r13, seen+32(%rip)\\nmovq %r14, seen+40(%rip)\\nmovq %r15, seen+48(%rip)\\nmovq %rsp, seen+56(%rip)\\n\"\n"
                                  "        \"movq before(%rip), %rsp\\nxorl %eax, %eax\\n\"\n"
                                  "        \"1:\\naddq $48, %rsp\\n\"\n"
                                  "        \"popq %r15\\npopq %r14\\npopq %r13\\npopq %r12\\npopq %rbx\\nret\\n\");\n"
                                  "}\n"
                                  "static volatile long sink;\n"
                                  "static volatile int pick;\n"
                                  "__attribute__((noinline)) static long churn(long x) { sink = x; return x * 7; }\n"
                                  "__attribute__((noinline)) void thrower(long offset, void* h)\n"
                                  "{\n"
                                  "    __builtin_unwind_init();\n"
                                  "    long a = churn(offset), b = churn(a + 1), c = churn(b + 2), d = churn(c + 3), e = churn(d + 4), f = churn(e + 5);\n"
                                  "    sink = a + b + c + d + e + f;\n"
                                  "    long* frame = __builtin_frame_address(0);\n"
                                  "    frame[-7] = 0x7a7a + offset;\n"
                                  "    frame[-6] = 0x6d6d + offset;\n"
                                  "    if (pick) { frame[-7] = 0x5151 + offset; __builtin_eh_return(offset, h); }\n"
                                  "    __builtin_eh_return(offset, h);\n"
                                  "}\n"
                                  "__attribute__((noinline)) void vthrower(long offset, void* h, ...)\n"
                                  "{\n"
                                  "    __builtin_unwind_init();\n"
                                  "    va_list ap;\n"
                                  "    va_start(ap, h);\n"
                                  "    long extra = va_arg(ap, long);\n"
                                  "    va_end(ap);\n"
                                  "    long* frame = __builtin_frame_address(0);\n"
                                  "    frame[-7] = 0x2a2a + offset + extra;\n"
                                  "    frame[-6] = 0x3b3b + churn(extra);\n"
                                  "    if (pick) __builtin_eh_return(offset + extra - 77, h);\n"
                                  "    __builtin_eh_return(offset, h);\n"
                                  "}\n"
                                  "__attribute__((noinline)) long ethrower(long offset, void* h)\n"
                                  "{\n"
                                  "    __builtin_unwind_init();\n"
                                  "    long* frame = __builtin_frame_address(0);\n"
                                  "    frame[-7] = 0x3c3c + offset;\n"
                                  "    frame[-6] = 0x4d4d + offset;\n"
                                  "    if (pick) return (long)sink * 2 + churn(offset) * (__builtin_eh_return(offset, h), 3);\n"
                                  "    return churn(offset + 1) + (__builtin_eh_return(offset, (char*)h + 0), 1);\n"
                                  "}\n"
                                  "__attribute__((noinline)) long tailee(long offset, void* h) { sink = offset * 3; return offset + 1000 + (h != 0); }\n"
                                  "__attribute__((noinline)) long mthrower(long offset, void* h)\n"
                                  "{\n"
                                  "    __builtin_unwind_init();\n"
                                  "    long* frame = __builtin_frame_address(0);\n"
                                  "    frame[-7] = 0x5e5e + offset;\n"
                                  "    frame[-6] = 0x6f6f + churn(offset);\n"
                                  "    if (pick) __builtin_eh_return(offset, h);\n"
                                  "    __attribute__((musttail)) return tailee(offset, h);\n"
                                  "}\n"
                                  "static int variant;\n"
                                  "struct S { double a; long b; };\n"
                                  "struct Big { long a, b, c, d; };\n"
                                  "typedef double V2 __attribute__((vector_size(16)));\n"
                                  "__attribute__((noinline)) static double fchurn(long x) { sink = x; return x * 0.5; }\n"
                                  "__attribute__((noinline)) static struct S schurn(long x) { sink = x + 100; return (struct S){ x * 0.25, x }; }\n"
                                  "__attribute__((noinline)) static V2 vchurn(long x) { sink = x + 200; return (V2){ x * 0.5, 3.0 }; }\n"
                                  "__attribute__((noinline)) long cthrower(long offset, void* h)\n"
                                  "{\n"
                                  "    __builtin_unwind_init();\n"
                                  "    long* frame = __builtin_frame_address(0);\n"
                                  "    frame[-7] = 0x1111 + offset + variant;\n"
                                  "    frame[-6] = 0x2222 + offset;\n"
                                  "    if (variant == 0) return (long)(fchurn(offset) + (sink + 1 ? (__builtin_eh_return(offset, h), 1.0) : 2.0));\n"
                                  "    if (variant == 1) return (long)((long double)fchurn(offset + 1) * 3.0L + (sink + 1 && (__builtin_eh_return(offset, h), 1)));\n"
                                  "    if (variant == 2) return schurn(offset).b + ({ struct S t = schurn(offset + 2); t.b >= 0 ? (__builtin_eh_return(offset, h), 0) : 9; });\n"
                                  "    if (variant == 3) return (long)(vchurn(offset) * (__builtin_eh_return(offset, h), (V2){ 1.0, 2.0 }))[0];\n"
                                  "    return (long)fchurn(offset) + ((void)sink, (__builtin_eh_return(offset, h), 5));\n"
                                  "}\n"
                                  "__attribute__((noinline)) long big_tailee(long offset, void* h, struct Big b) { sink = b.a + 10 * b.b + 100 * b.c + 1000 * b.d; return offset + 2000 + (h != 0); }\n"
                                  "__attribute__((noinline)) long bthrower(long offset, void* h, struct Big b)\n"
                                  "{\n"
                                  "    __builtin_unwind_init();\n"
                                  "    long* frame = __builtin_frame_address(0);\n"
                                  "    frame[-7] = 0x7e7e + offset + b.a;\n"
                                  "    frame[-6] = 0x8f8f + churn(b.d);\n"
                                  "    if (pick) __builtin_eh_return(offset, h);\n"
                                  "    __attribute__((musttail)) return big_tailee(offset, h, (struct Big){ b.d, b.c, b.b, b.a });\n"
                                  "}\n"
                                  "__attribute__((noinline)) V2 trapping(V2 a, int go) { return go ? vchurn(2) * (__builtin_trap(), (V2){ 1.0, 2.0 }) : a + 1.0; }\n"
                                  "__attribute__((noinline)) void maybe(long offset, void* h, int go)\n"
                                  "{\n"
                                  "    __builtin_unwind_init();\n"
                                  "    if (go) __builtin_eh_return(offset, h);\n"
                                  "    sink = 5;\n"
                                  "}\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    void* const functions[6] = { (void*)thrower, (void*)vthrower, (void*)ethrower, (void*)mthrower, (void*)cthrower, (void*)bthrower };\n"
                                  "    for (int k = 0; k < 6; k += 1)\n"
                                  "    {\n"
                                  "        target = functions[k];\n"
                                  "        for (long offset = 0; offset <= 32; offset += 8)\n"
                                  "        {\n"
                                  "            memset(seen, 0, sizeof(seen));\n"
                                  "            pick = (offset & 8) != 0;\n"
                                  "            variant = (int)(offset / 8);\n"
                                  "            long r = run(offset & ~8);\n"
                                  "            printf(\"%d %ld rax=%lx rdx=%lx rbx=%lx r12=%lx r13=%lx r14=%lx r15=%lx sp=%ld sink=%ld\\n\", k, r, seen[0], seen[1], seen[2], seen[3], seen[4], seen[5], seen[6], seen[7] - before, sink);\n"
                                  "        }\n"
                                  "    }\n"
                                  "    printf(\"trapping %g\\n\", trapping((V2){ 4.0, 5.0 }, 0)[1]);\n"
                                  "    maybe(0, 0, 0);\n"
                                  "    printf(\"normal return %ld\\n\", sink);\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char outputs[5][256];
    snprintf(source, sizeof(source), "%s/eh_return.c", directory);
    for (uint32_t k = 0; k < 5; k += 1)
    {
        snprintf(outputs[k], sizeof(outputs[k]), "%s/eh_return-%u", directory, k);
    }
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "the eh_return program written");
    char* text = NULL;
    char* const reference[] = { (char*)clang, "-O0", "-o", outputs[4], source, NULL };
    const int built = written ? check_test_driver_spawn(reference, &text) : 1;
    free(text);
    static char expected[8192];
    const bool referenced = built == 0 && check_test_driver_capture(outputs[4], expected, sizeof(expected));
    check_test_expect(test, referenced && strlen(expected) > 1000, "clang's build of the eh_return program runs");
    static const char* const names[4] = { "vector, Tier 0", "scalar, Tier 0", "vector, Tier 1", "scalar, Tier 1" };
    for (uint32_t mode = 0; referenced && mode < 4; mode += 1)
    {
        char* arguments[16] = { (char*)executable };
        uint32_t count = 1;
        if ((mode & 1) != 0)
        {
            arguments[count++] = "--scalar";
        }
        if (mode >= 2)
        {
            arguments[count++] = "--tier1";
        }
        arguments[count++] = "--verify";
        arguments[count++] = "-o";
        arguments[count++] = outputs[mode];
        arguments[count++] = source;
        arguments[count] = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        static char captured[8192];
        const bool ran = status == 0 && check_test_driver_capture(outputs[mode], captured, sizeof(captured));
        char description[256];
        snprintf(description, sizeof(description), "__builtin_eh_return unwinds to its handler as clang's build does (%s)", names[mode]);
        check_test_expect(test, ran && strcmp(captured, expected) == 0, description);
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        else if (ran && strcmp(captured, expected) != 0)
        {
            fprintf(stderr, "eh_return (%s):\n%s\nclang's build:\n%s\n", names[mode], captured, expected);
        }
        free(text);
        text = NULL;
    }
    unlink(source);
    for (uint32_t k = 0; k < 5; k += 1)
    {
        unlink(outputs[k]);
    }
}

// Dead stand-ins of vectors and masks (#1438's review): an expression's values held under a terminal helper's call
// (__builtin_trap) and under __builtin_eh_return, of AVX-512 masks (kandhi's and a select's first operands, which the
// lowering converts to masks before the trap) and of 64- and 32-byte vectors, where the tracked emitter's class
// adjacency (check_lower_emit_tracked) and the expression's dead rest (check_lower_dead_value) take stand-ins of their
// widths, a mask's by an integer's CONVERT, since no CONST has one. Built for x86-64-v4 by both paths at Tier 0 and with
// --tier1, all with --verify, and, where the host has the features, printing what clang -O0's build prints (the eh_return
// functions' results through a global: clang's epilogue pops the saved rax over a returned one).
static void check_test_driver_dead_vectors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "#include <immintrin.h>\n"
                                  "#include <stdio.h>\n"
                                  "static volatile int go;\n"
                                  "static volatile long sink;\n"
                                  "__attribute__((noinline)) __mmask16 kand_trap(__mmask16 k) { return go ? __builtin_ia32_kandhi(k, (__builtin_trap(), k)) : (__mmask16)(k + 1); }\n"
                                  "__attribute__((noinline)) __m512i select_trap(__m512i a, __m512i b, __mmask16 k) { return go ? (__m512i)__builtin_ia32_selectd_512(k, (__v16si)a, (__builtin_trap(), (__v16si)b)) : b; }\n"
                                  "__attribute__((noinline)) __m512i add_trap(__m512i a, __m512i b) { return go ? (__m512i)((__v16si)a + (__builtin_trap(), (__v16si)b)) : a; }\n"
                                  "__attribute__((noinline)) __m256i add_trap256(__m256i a, __m256i b) { return go ? (__m256i)((__v8si)a + (__builtin_trap(), (__v8si)b)) : b; }\n"
                                  "__attribute__((noinline)) void eh_mask(long offset, void* h, __mmask16 k) { __builtin_unwind_init(); sink = go ? (long)__builtin_ia32_kandhi(k, (__builtin_eh_return(offset, h), k)) : (long)k; }\n"
                                  "__attribute__((noinline)) void eh_vector(long offset, void* h, __m512i a) { __builtin_unwind_init(); sink = go ? ((__v16si)a + (__builtin_eh_return(offset, h), (__v16si)a))[0] : ((__v16si)a)[1]; }\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    __m512i a = _mm512_set1_epi32(3), b = _mm512_set1_epi32(4);\n"
                                  "    __m256i c = _mm256_set1_epi32(5);\n"
                                  "    printf(\"%d %d %d %d\\n\", (int)kand_trap(6), _mm512_reduce_add_epi32(select_trap(a, b, 7)), _mm512_reduce_add_epi32(add_trap(a, b)), ((__v8si)add_trap256(c, c))[0]);\n"
                                  "    eh_mask(0, 0, 9);\n"
                                  "    long m = sink;\n"
                                  "    eh_vector(0, 0, a);\n"
                                  "    printf(\"%ld %ld\\n\", m, sink);\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char outputs[5][256];
    snprintf(source, sizeof(source), "%s/dead_vectors.c", directory);
    for (uint32_t k = 0; k < 5; k += 1)
    {
        snprintf(outputs[k], sizeof(outputs[k]), "%s/dead_vectors-%u", directory, k);
    }
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "the dead vectors' program written");
    const uint64_t required = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) | BUSTER_X86_FEATURE(AVX512DQ) |
                              BUSTER_X86_FEATURE(AVX512VL);
    const bool native = (x86_target_native().features & required) == required;
    char* text = NULL;
    static char expected[1024];
    bool referenced = false;
    if (written && native)
    {
        char* const reference[] = { (char*)clang, "-O0", "-march=x86-64-v4", "-o", outputs[4], source, NULL };
        const int built = check_test_driver_spawn(reference, &text);
        free(text);
        text = NULL;
        referenced = built == 0 && check_test_driver_capture(outputs[4], expected, sizeof(expected));
        check_test_expect(test, referenced && strlen(expected) > 10, "clang's build of the dead vectors' program runs");
    }
    static const char* const names[4] = { "vector, Tier 0", "scalar, Tier 0", "vector, Tier 1", "scalar, Tier 1" };
    for (uint32_t mode = 0; written && mode < 4; mode += 1)
    {
        char* arguments[16] = { (char*)executable, "-march=x86-64-v4" };
        uint32_t count = 2;
        if ((mode & 1) != 0)
        {
            arguments[count++] = "--scalar";
        }
        if (mode >= 2)
        {
            arguments[count++] = "--tier1";
        }
        arguments[count++] = "--verify";
        arguments[count++] = "-o";
        arguments[count++] = outputs[mode];
        arguments[count++] = source;
        arguments[count] = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        char description[256];
        snprintf(description, sizeof(description), "the dead vectors' and masks' stand-ins build and verify (%s)", names[mode]);
        check_test_expect(test, status == 0, description);
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        text = NULL;
        static char captured[1024];
        const bool ran = status == 0 && referenced && check_test_driver_capture(outputs[mode], captured, sizeof(captured));
        if (referenced)
        {
            snprintf(description, sizeof(description), "the dead vectors' program prints what clang's build does (%s)", names[mode]);
            check_test_expect(test, ran && strcmp(captured, expected) == 0, description);
        }
    }
    if (written && !native)
    {
        printf("  dead vectors: native execution skipped (the host lacks x86-64-v4's AVX-512 features); the program builds\n");
    }
    unlink(source);
    for (uint32_t k = 0; k < 5; k += 1)
    {
        unlink(outputs[k]);
    }
}

// The unwinder in the direct path (#489): a program whose own code walks its stack with _Unwind_Backtrace, which GCC's
// driver takes from libgcc_s.so.1, as needed, and so must the direct path: by both paths, it must link without a
// diagnostic into an executable that names libgcc_s.so.1 (its DT_NEEDED entry and version need), the same bytes by
// both, and run, printing what it should. And one that calls __splitstack_getcontext, whose member of libgcc.a makes
// morestack.o join, whose _Unwind_Resume binds to libgcc_s.so.1 (#489), where #483 refused it, naming morestack.o:
// binding joins generic-morestack.o too, whose thread-locals it reaches by general- and local-dynamic accesses, which
// the writer relaxes in a dynamic PIE (#515), so it must link alike, into the same bytes by both paths, which name
// libgcc_s.so.1 and run, printing nothing. On a system without libgcc_s.so.1 (check_test_driver_has_gcc_s), binding
// refuses both instead: the
// backtrace's name as undefined, and morestack.o's _Unwind_Resume naming the member and saying the system has no
// libgcc_s.so.1.
static bool check_test_driver_has_gcc_s(void);

static void check_test_driver_split_stack(CheckTest* test, const char* executable, const char* directory)
{
    static const char split[] = "extern void __splitstack_getcontext(void* context[10]);\n"
                                "int main(void)\n"
                                "{\n"
                                "    void* context[10];\n"
                                "    __splitstack_getcontext(context);\n"
                                "    return 0;\n"
                                "}\n";
    static const char backtrace[] = "#include <stdio.h>\n"
                                    "struct _Unwind_Context;\n"
                                    "typedef int (*Trace)(struct _Unwind_Context* context, void* argument);\n"
                                    "extern int _Unwind_Backtrace(Trace trace, void* argument);\n"
                                    "static int count(struct _Unwind_Context* context, void* argument)\n"
                                    "{\n"
                                    "    (void)context;\n"
                                    "    *(int*)argument += 1;\n"
                                    "    return 0;\n"
                                    "}\n"
                                    "int main(void)\n"
                                    "{\n"
                                    "    int frames = 0;\n"
                                    "    _Unwind_Backtrace(count, &frames);\n"
                                    "    printf(\"%d\\n\", frames > 0);\n"
                                    "    return 0;\n"
                                    "}\n";
    static const struct
    {
        const char* name;
        const char* source;
        const char* output;
        // A part of the refusal without libgcc_s.so.1.
        const char* unwinder_refusal;
    } programs[] = {
        { "backtrace", backtrace, "1\n", "error: undefined reference to '_Unwind_Backtrace'\n" },
        { "split-stack", split, "",
          "(morestack.o): undefined reference to '_Unwind_Resume'; GCC's driver takes it from libgcc_s.so.1, which this system doesn't have\n" },
    };
    const bool unwinder = check_test_driver_has_gcc_s();
    if (!unwinder)
    {
        printf("  the unwinder: this system has no libgcc_s.so.1, so both programs must be refused\n");
    }
    for (uint32_t p = 0; p < sizeof(programs) / sizeof(programs[0]); p += 1)
    {
        char source[256];
        char outputs[2][256];
        snprintf(source, sizeof(source), "%s/%s.c", directory, programs[p].name);
        snprintf(outputs[0], sizeof(outputs[0]), "%s/%s", directory, programs[p].name);
        snprintf(outputs[1], sizeof(outputs[1]), "%s/%s-scalar", directory, programs[p].name);
        const bool written = parse_test_write(source, programs[p].source, strlen(programs[p].source));
        check_test_expect(test, written, "unwinder program written");
        uint8_t* bytes[2] = { NULL, NULL };
        uint64_t lengths[2] = { 0, 0 };
        for (uint32_t path = 0; written && path < 2; path += 1)
        {
            char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, NULL };
            char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
            const bool quiet = text == NULL || text[0] == 0;
            const bool refused = !unwinder;
            if (!unwinder)
            {
                const size_t length = text != NULL ? strlen(text) : 0;
                const bool worded = length != 0 && strstr(text, programs[p].unwinder_refusal) != NULL && strchr(text, '\n') == text + length - 1;
                check_test_expect(test, status == 1 && worded && access(outputs[path], F_OK) != 0,
                                  path == 0 ? "the unwinder's program refused without libgcc_s.so.1" : "the unwinder's program refused, scalar");
            }
            else if (status != 0 || !quiet)
            {
                printf("  %s%s: status %d: %s", programs[p].name, path == 0 ? "" : ", scalar", status, text != NULL ? text : "\n");
            }
            bytes[path] = status == 0 ? check_test_driver_read_aligned(outputs[path], &lengths[path]) : NULL;
            const uint8_t needed[] = "libgcc_s.so.1";
            const bool named = bytes[path] != NULL && memmem(bytes[path], lengths[path], needed, sizeof(needed)) != NULL;
            char captured[64];
            const bool ran = status == 0 && check_test_driver_capture(outputs[path], captured, sizeof(captured)) && strcmp(captured, programs[p].output) == 0;
            check_test_expect(test, refused || (status == 0 && quiet && named && ran),
                              path == 0 ? "the unwinder's program links, names libgcc_s.so.1 and runs" : "the unwinder's program, scalar");
            free(text);
        }
        check_test_expect(test,
                          !unwinder ||
                              (bytes[0] != NULL && bytes[1] != NULL && lengths[0] == lengths[1] && memcmp(bytes[0], bytes[1], lengths[0]) == 0),
                          "the unwinder's program the same by both paths");
        free(bytes[0]);
        free(bytes[1]);
        unlink(source);
        unlink(outputs[0]);
        unlink(outputs[1]);
    }
}

// Whether this system has libgcc_s.so.1 where GCC's driver finds it: cc -print-file-name names its path, or the name
// alone when it finds none.
static bool check_test_driver_has_gcc_s(void)
{
    FILE* output = popen("cc -print-file-name=libgcc_s.so.1", "r");
    char path[4096] = { 0 };
    const bool read = output != NULL && fgets(path, sizeof(path), output) != NULL;
    if (output != NULL)
    {
        pclose(output);
    }
    const size_t length = read ? strcspn(path, "\n") : 0;
    path[length] = 0;
    return length != 0 && path[0] == '/' && access(path, F_OK) == 0;
}

// A file's bytes, read whole into memory aligned to 64 bytes, as a snapshot's view takes them (snapshot_view), which the
// caller frees, and their count in *length; NULL if the file can't be read.
static uint8_t* check_test_driver_read_aligned(const char* path, uint64_t* length)
{
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    const bool known = file >= 0 && fstat(file, &status) == 0 && status.st_size > 0;
    const uint64_t bytes = known ? (uint64_t)status.st_size : 0;
    uint8_t* result = known ? aligned_alloc(64, (bytes + 63) / 64 * 64) : NULL;
    uint64_t done = 0;
    bool reading = result != NULL;
    while (reading && done < bytes)
    {
        const ssize_t got = read(file, result + done, bytes - done);
        reading = got > 0;
        done += reading ? (uint64_t)got : 0;
    }
    if (file >= 0)
    {
        close(file);
    }
    if (result != NULL && done != bytes)
    {
        free(result);
        result = NULL;
    }
    *length = result != NULL ? bytes : 0;
    return result;
}

// Runs the executable at `path` and captures up to `capacity` - 1 bytes of its output into `captured`, NUL-terminated:
// whether it ran and exited with status 0.
static bool check_test_driver_capture(const char* path, char* captured, size_t capacity)
{
    FILE* output = popen(path, "r");
    const size_t got = output != NULL ? fread(captured, 1, capacity - 1, output) : 0;
    captured[got] = 0;
    return output != NULL && pclose(output) == 0;
}

// A damaged snapshot in the direct path (#483's review): the C runtime's snapshot in a cache directory of the test's own
// (XDG_CACHE_HOME), written by the driver's first link of the libgcc program, then the first atom's alignment and size
// of the member that defines __udivti3 changed in the file, to 2^255 and 2^64 - 1: records that only that member's piece
// hash covers, which the room the driver gives the link (bind_capacity_needed) mustn't read. Binding finds the piece
// damaged as it joins it (BIND_ERROR_SNAPSHOT), and the driver discards the snapshot and imports the runtime again: the
// second link must succeed without a word on its errors, its executable print what the first one's does, and the
// snapshot be the first link's again.
static void check_test_driver_damaged_snapshot(CheckTest* test, const char* executable, const char* directory)
{
    static const char name[] = "__udivti3";
    static const uint8_t ones[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    static const struct
    {
        RuntimeKind kind;
        const char* label;
        const char* slug;
    } cases[] = {
        { RUNTIME_KIND_DYNAMIC, "dynamic PIE", "dynamic" },
        { RUNTIME_KIND_STATIC, "static executable", "static" },
        { RUNTIME_KIND_STATIC_PIE, "static PIE", "static-pie" },
    };
    char cache[256];
    char place[320];
    char source[256];
    snprintf(cache, sizeof(cache), "%s/cache", directory);
    snprintf(place, sizeof(place), "%s/buster", cache);
    snprintf(source, sizeof(source), "%s/damaged.c", directory);
    const char* previous = getenv("XDG_CACHE_HOME");
    char* saved = previous != NULL ? strdup(previous) : NULL;
    const bool written = mkdir(cache, 0700) == 0 && setenv("XDG_CACHE_HOME", cache, 1) == 0 &&
                         parse_test_write(source, check_test_libgcc_program, sizeof(check_test_libgcc_program) - 1);
    check_test_expect(test, written, "a damaged snapshot's cache and program written");
    for (uint32_t c = 0; written && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const RuntimeKind kind = cases[c].kind;
        const char* const label = cases[c].label;
        char snapshot[512];
        char output[320];
        char description[256];
        snprintf(snapshot, sizeof(snapshot), "%s/%s", place, snapshot_name(kind));
        snprintf(output, sizeof(output), "%s/damaged-%s", directory, cases[c].slug);
        char* const dynamic_arguments[] = { (char*)executable, "-o", output, source, NULL };
        char* const static_arguments[] = { (char*)executable, "-static", "-o", output, source, NULL };
        char* const static_pie_arguments[] = { (char*)executable, "-static-pie", "-o", output, source, NULL };
        char* const* arguments = kind == RUNTIME_KIND_STATIC ? static_arguments : kind == RUNTIME_KIND_STATIC_PIE ? static_pie_arguments : dynamic_arguments;
        char* text = NULL;
        const int first = check_test_driver_spawn(arguments, &text);
        free(text);
        char before[256];
        const bool ran = first == 0 && check_test_driver_capture(output, before, sizeof(before));
        snprintf(description, sizeof(description), "the %s libgcc program linked, writing the snapshot", label);
        check_test_expect(test, ran, description);
        uint64_t length = 0;
        uint8_t* good = ran ? check_test_driver_read_aligned(snapshot, &length) : NULL;
        // The places of the member's first atom's alignment and size in the file, by the layout of a view of a copy, which the
        // test's own reading of it (snapshot_test_piece_at) must find the piece's records.
        uint8_t* copy = good != NULL ? aligned_alloc(64, (length + 63) / 64 * 64) : NULL;
        Runtime view = { 0 };
        if (copy != NULL)
        {
            memcpy(copy, good, length);
            snapshot_view(&view, copy, length);
        }
        const SnapshotHeader* header = (const SnapshotHeader*)copy;
        const uint32_t entry = copy != NULL ? runtime_find_global_name(&view.global_names, (const uint8_t*)name, sizeof(name) - 1, link_hash((const uint8_t*)name, sizeof(name) - 1)) : 0;
        const uint32_t member = entry != 0 ? view.global_names.member[entry - 1] : 0;
        const uint32_t piece = member != 0 ? RUNTIME_OBJECTS + member - 1 : UINT32_MAX;
        const AtomId atom = piece != UINT32_MAX ? view.pieces[piece].begin.atoms : 0;
        const uint64_t alignment = piece != UINT32_MAX ? snapshot_record_offset(header, SNAPSHOT_REGION_ATOM_ALIGNMENT, atom) : 0;
        const uint64_t size = piece != UINT32_MAX ? snapshot_record_offset(header, SNAPSHOT_REGION_ATOM_SIZE, atom) : 0;
        const bool placed = piece != UINT32_MAX && view.pieces[piece].end.atoms > atom && snapshot_test_piece_at(header, &view, alignment) == piece &&
                            snapshot_test_piece_at(header, &view, size) == piece;
        snprintf(description, sizeof(description), "the __udivti3 member's first atom's alignment and size in the %s snapshot", label);
        check_test_expect(test, placed, description);
        const int file = placed ? open(snapshot, O_WRONLY | O_CLOEXEC) : -1;
        const bool damaged = file >= 0 && snapshot_test_pwrite(file, ones, 1, alignment) && snapshot_test_pwrite(file, ones, sizeof(ones), size);
        if (file >= 0)
        {
            close(file);
        }
        snprintf(description, sizeof(description), "the member's records damaged in the %s snapshot", label);
        check_test_expect(test, damaged, description);
        const int second = damaged ? check_test_driver_spawn(arguments, &text) : -1;
        snprintf(description, sizeof(description), "a damaged %s piece: the link imports the runtime again, without a word", label);
        check_test_expect(test, second == 0 && text != NULL && text[0] == 0, description);
        free(text);
        char after[256];
        snprintf(description, sizeof(description), "the %s program runs as before", label);
        check_test_expect(test, second == 0 && check_test_driver_capture(output, after, sizeof(after)) && strcmp(before, after) == 0, description);
        uint64_t again_length = 0;
        uint8_t* again = second == 0 ? check_test_driver_read_aligned(snapshot, &again_length) : NULL;
        snprintf(description, sizeof(description), "the %s snapshot written again as the first link wrote it", label);
        check_test_expect(test, again != NULL && again_length == length && memcmp(again, good, length) == 0, description);
        free(again);
        free(copy);
        free(good);
        unlink(snapshot);
        unlink(output);
    }
    if (saved != NULL)
    {
        setenv("XDG_CACHE_HOME", saved, 1);
    }
    else
    {
        unsetenv("XDG_CACHE_HOME");
    }
    free(saved);
    rmdir(place);
    rmdir(cache);
    unlink(source);
}

// Complex values' products and quotients in the direct path (#493's review): a program that multiplies and divides
// _Complex float, double and long double values, a real dividend over a complex divisor among them, which call
// libgcc.a's __mulsc3, __muldc3, __mulxc3, __divsc3, __divdc3 and __divxc3 (#133's lowering). Linked by both paths into
// the same bytes, needing libc.so.6 alone (the members join the executable), it must print every part's bits (%a) as
// this executable, built by clang, computes them.
static void check_test_driver_complex(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "static volatile double ar = 1.5, ai = -2.25, br = 0.5, bi = 3.0;\n"
                                  "static volatile float fr = 2.0f, fi = 0.5f;\n"
                                  "static volatile long double lr = 1.25L, li = -0.75L;\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    _Complex double a = ar + ai * 1.0i, b = br + bi * 1.0i;\n"
                                  "    _Complex float f = fr + fi * 1.0fi, g = fi - fr * 1.0fi;\n"
                                  "    _Complex long double l = lr + li * 1.0Li, m = li + lr * 1.0Li;\n"
                                  "    _Complex double p = a * b, q = a / b, r = ar / b;\n"
                                  "    _Complex float s = f * g, t = f / g;\n"
                                  "    _Complex long double u = l * m, v = l / m;\n"
                                  "    printf(\"%a %a %a %a %a %a %a %a %a %a %La %La %La %La\\n\", __real__ p, __imag__ p, __real__ q, __imag__ q, __real__ r,\n"
                                  "           __imag__ r, (double)__real__ s, (double)__imag__ s, (double)__real__ t, (double)__imag__ t, __real__ u, __imag__ u,\n"
                                  "           __real__ v, __imag__ v);\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char outputs[2][256];
    snprintf(source, sizeof(source), "%s/complex.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/complex", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/complex-scalar", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "complex program written");
    char* text = NULL;
    bool linked = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-o", outputs[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-o", outputs[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "complex products and quotients link libgcc.a's members" : "complex products and quotients, scalar");
        linked = linked && status == 0;
        free(text);
    }
    check_test_expect(test, linked && check_test_driver_same_files(outputs[0], outputs[1]), "both paths link the same executable of complex products and quotients");
    char needed[256] = { 0 };
    check_test_expect(test, linked && check_test_driver_needed(outputs[0], needed, sizeof(needed)) && strcmp(needed, "libc.so.6") == 0,
                      "the complex helpers in the executable, libc.so.6 needed alone");
    volatile double ar = 1.5, ai = -2.25, br = 0.5, bi = 3.0;
    volatile float fr = 2.0f, fi = 0.5f;
    volatile long double lr = 1.25L, li = -0.75L;
    const _Complex double a = ar + ai * 1.0i;
    const _Complex double b = br + bi * 1.0i;
    const _Complex float f = fr + fi * 1.0fi;
    const _Complex float g = fi - fr * 1.0fi;
    const _Complex long double l = lr + li * 1.0Li;
    const _Complex long double m = li + lr * 1.0Li;
    const _Complex double p = a * b;
    const _Complex double q = a / b;
    const _Complex double r = ar / b;
    const _Complex float s = f * g;
    const _Complex float t = f / g;
    const _Complex long double u = l * m;
    const _Complex long double v = l / m;
    char expected[512];
    snprintf(expected, sizeof(expected), "%a %a %a %a %a %a %a %a %a %a %La %La %La %La\n", __real__ p, __imag__ p, __real__ q, __imag__ q, __real__ r, __imag__ r,
             (double)__real__ s, (double)__imag__ s, (double)__real__ t, (double)__imag__ t, __real__ u, __imag__ u, __real__ v, __imag__ v);
    char captured[512];
    check_test_expect(test, linked && check_test_driver_capture(outputs[0], captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                      "the program prints the complex results' bits");
    unlink(source);
    unlink(outputs[0]);
    unlink(outputs[1]);
}

// The jump tables' switches of check_test_driver_switch_tables, as the test's own clang build computes them.
static int check_test_switch_dense(int x)
{
    int result = -1;
    switch (x)
    {
        case 0: result = 3; break;
        case 1: result = 5; break;
        case 2: result = 7; break;
        case 3: result = 11; break;
        case 5: result = 13; break;
        case 7: result = 17; break;
        default: break;
    }
    return result;
}

static int check_test_switch_wide(long x)
{
    int result = 0;
    switch (x)
    {
        case -3: result += 1;
        case -2: result += 2; break;
        case 0 ... 2: result += 4; break;
        case 4: result += 8;
        case 5: result += 16; break;
        default: result += 100; break;
    }
    return result;
}

// Whether `bytes` (`length` of them) are an ELF object without the lowering's switch symbols (.Lbuster_switch_N and
// .Lbuster_case_N) and whose .rodata's relocations are `entries` PC32 ones, each against .text's section symbol: the
// tables and their labels the lowering's temporaries (check_driver_object), as an assembler leaves clang's .LJTI and
// .LBB. String literals' .Lbuster_string_N stay, as clang's .L.str do.
static bool check_test_driver_temporaries(const uint8_t* bytes, uint64_t length, uint32_t entries)
{
    Elf64_Ehdr header = { 0 };
    bool valid = bytes != NULL && length >= sizeof(header);
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_shentsize == sizeof(Elf64_Shdr) && header.e_shstrndx < header.e_shnum &&
                header.e_shoff + (uint64_t)header.e_shnum * sizeof(Elf64_Shdr) <= length;
    }
    Elf64_Shdr names = { 0 };
    if (valid)
    {
        memcpy(&names, bytes + header.e_shoff + header.e_shstrndx * sizeof(Elf64_Shdr), sizeof(names));
        valid = names.sh_offset + names.sh_size <= length;
    }
    uint32_t symtab = 0;
    uint32_t text = 0;
    uint32_t rodata = 0;
    for (uint32_t s = 1; valid && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, bytes + header.e_shoff + s * sizeof(Elf64_Shdr), sizeof(section));
        const char* name = section.sh_name < names.sh_size ? (const char*)bytes + names.sh_offset + section.sh_name : "";
        symtab = section.sh_type == SHT_SYMTAB ? s : symtab;
        text = strcmp(name, ".text") == 0 ? s : text;
        rodata = strcmp(name, ".rodata") == 0 ? s : rodata;
    }
    Elf64_Shdr symbols = { 0 };
    Elf64_Shdr strings = { 0 };
    valid = valid && symtab != 0 && text != 0 && rodata != 0;
    if (valid)
    {
        memcpy(&symbols, bytes + header.e_shoff + symtab * sizeof(Elf64_Shdr), sizeof(symbols));
        valid = symbols.sh_link < header.e_shnum && symbols.sh_entsize == sizeof(Elf64_Sym) && symbols.sh_offset + symbols.sh_size <= length;
    }
    if (valid)
    {
        memcpy(&strings, bytes + header.e_shoff + symbols.sh_link * sizeof(Elf64_Shdr), sizeof(strings));
        valid = strings.sh_offset + strings.sh_size <= length;
    }
    const uint64_t symbol_count = valid ? symbols.sh_size / sizeof(Elf64_Sym) : 0;
    for (uint64_t i = 0; valid && i < symbol_count; i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, bytes + symbols.sh_offset + i * sizeof(Elf64_Sym), sizeof(symbol));
        const char* name = symbol.st_name < strings.sh_size ? (const char*)bytes + strings.sh_offset + symbol.st_name : NULL;
        valid = name != NULL && strncmp(name, ".Lbuster_switch_", 16) != 0 && strncmp(name, ".Lbuster_case_", 14) != 0;
    }
    uint32_t relocations = 0;
    for (uint32_t s = 1; valid && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, bytes + header.e_shoff + s * sizeof(Elf64_Shdr), sizeof(section));
        const bool applies = section.sh_type == SHT_RELA && section.sh_info == rodata;
        valid = !applies || (section.sh_entsize == sizeof(Elf64_Rela) && section.sh_offset + section.sh_size <= length);
        for (uint64_t r = 0; valid && applies && r < section.sh_size / sizeof(Elf64_Rela); r += 1)
        {
            Elf64_Rela relocation;
            memcpy(&relocation, bytes + section.sh_offset + r * sizeof(Elf64_Rela), sizeof(relocation));
            Elf64_Sym symbol = { 0 };
            valid = ELF64_R_SYM(relocation.r_info) < symbol_count && ELF64_R_TYPE(relocation.r_info) == R_X86_64_PC32;
            if (valid)
            {
                memcpy(&symbol, bytes + symbols.sh_offset + ELF64_R_SYM(relocation.r_info) * sizeof(Elf64_Sym), sizeof(symbol));
                valid = ELF64_ST_TYPE(symbol.st_info) == STT_SECTION && symbol.st_shndx == text;
            }
            relocations += 1;
        }
    }
    return valid && relocations == entries;
}

// Jump tables in the C driver's outputs (#136's item 7): a program of two switches clang -O0 would make tables of, an
// int's of 8 entries and a long's of 9, and a 128-bit division, which calls libgcc's __udivti3 from the same part of
// the bodies (one lane's), where the tables' symbols' numbers fall among the runtime functions' provisional ones, each
// reference rewritten once. Compiled with -c by both paths into the same bytes. The object holds no symbol of the
// lowering's (.Lbuster_switch_N, .Lbuster_case_N), its tables' 17 entries are relocations against .text's section
// symbol, and linked by clang it prints what the test's clang build computes, as does the direct path's executable.
static void check_test_driver_switch_tables(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "static int dense(int x)\n"
                                  "{\n"
                                  "    int result = -1;\n"
                                  "    switch (x)\n"
                                  "    {\n"
                                  "        case 0: result = 3; break;\n"
                                  "        case 1: result = 5; break;\n"
                                  "        case 2: result = 7; break;\n"
                                  "        case 3: result = 11; break;\n"
                                  "        case 5: result = 13; break;\n"
                                  "        case 7: result = 17; break;\n"
                                  "        default: break;\n"
                                  "    }\n"
                                  "    return result;\n"
                                  "}\n"
                                  "static int wide(long x)\n"
                                  "{\n"
                                  "    int result = 0;\n"
                                  "    switch (x)\n"
                                  "    {\n"
                                  "        case -3: result += 1;\n"
                                  "        case -2: result += 2; break;\n"
                                  "        case 0 ... 2: result += 4; break;\n"
                                  "        case 4: result += 8;\n"
                                  "        case 5: result += 16; break;\n"
                                  "        default: result += 100; break;\n"
                                  "    }\n"
                                  "    return result;\n"
                                  "}\n"
                                  "static unsigned long long quotient(unsigned __int128 n, unsigned long long d)\n"
                                  "{\n"
                                  "    return (unsigned long long)(n / d);\n"
                                  "}\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    unsigned sum = 0;\n"
                                  "    for (int i = -4; i < 10; i++)\n"
                                  "    {\n"
                                  "        sum = sum * 31 + (unsigned)dense(i);\n"
                                  "        sum = sum * 7 + (unsigned)wide(i - 2);\n"
                                  "        sum = sum * 3 + (unsigned)quotient(((unsigned __int128)(unsigned)(i + 5) << 70) + 12345, 1000003);\n"
                                  "    }\n"
                                  "    printf(\"%u\\n\", sum);\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char objects[2][256];
    char linked_path[256];
    char direct_path[256];
    snprintf(source, sizeof(source), "%s/switch-tables.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/switch-tables.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/switch-tables-scalar.o", directory);
    snprintf(linked_path, sizeof(linked_path), "%s/switch-tables-linked", directory);
    snprintf(direct_path, sizeof(direct_path), "%s/switch-tables-direct", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "jump tables' program written");
    char* text = NULL;
    bool compiled = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-c", "-o", objects[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", objects[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "the jump tables' program compiles with -c" : "the jump tables' program compiles with -c, scalar");
        compiled = compiled && status == 0;
        free(text);
        text = NULL;
    }
    check_test_expect(test, compiled && check_test_driver_same_files(objects[0], objects[1]), "both paths write the same object of jump tables");
    uint64_t length = 0;
    uint8_t* bytes = compiled ? check_test_driver_read_aligned(objects[0], &length) : NULL;
    check_test_expect(test, check_test_driver_temporaries(bytes, length, 17),
                      "the object holds none of the lowering's symbols, and its tables' 17 entries name .text's section symbol");
    free(bytes);
    unsigned sum = 0;
    for (int i = -4; i < 10; i += 1)
    {
        sum = sum * 31 + (unsigned)check_test_switch_dense(i);
        sum = sum * 7 + (unsigned)check_test_switch_wide(i - 2);
        sum = sum * 3 + (unsigned)(unsigned long long)((((unsigned __int128)(unsigned)(i + 5) << 70) + 12345) / 1000003u);
    }
    char expected[32];
    snprintf(expected, sizeof(expected), "%u\n", sum);
    char* const link[] = { (char*)clang, "-o", linked_path, objects[0], NULL };
    const int linked = compiled ? check_test_driver_spawn(link, &text) : -1;
    free(text);
    text = NULL;
    char captured[64];
    check_test_expect(test, linked == 0 && check_test_driver_capture(linked_path, captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                      "clang's link of the object dispatches through the tables as clang's own code does");
    char* const direct[] = { (char*)executable, "-o", direct_path, source, NULL };
    const int built = written ? check_test_driver_spawn(direct, &text) : -1;
    free(text);
    text = NULL;
    check_test_expect(test, built == 0 && check_test_driver_capture(direct_path, captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                      "the direct path's executable dispatches through the tables as clang's own code does");
    unlink(source);
    unlink(objects[0]);
    unlink(objects[1]);
    unlink(linked_path);
    unlink(direct_path);
}

// #1122: __start_SEC and __stop_SEC over the sections of C identifiers, which GNU ld and lld define: a registry of
// three entries with an alignment, a section of one, and a weak reference to a missing one, by -c's object that cc
// links, by the direct path and by --tier1, each printing clang's build's output.
static void check_test_driver_section_bounds(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "struct entry { long value; const char* name; };\n"
                                  "__attribute__((section(\"bst_sec\"), aligned(16))) struct entry e0 = { 11, \"a\" };\n"
                                  "__attribute__((aligned(16))) long between = 99;\n"
                                  "__attribute__((section(\"bst_sec\"), aligned(16))) struct entry e1 = { 22, \"b\" };\n"
                                  "__attribute__((section(\"bst_sec\"), aligned(16))) struct entry e2 = { 33, \"c\" };\n"
                                  "__attribute__((section(\"bst_one\"))) int one = 5;\n"
                                  "extern struct entry __start_bst_sec[], __stop_bst_sec[];\n"
                                  "extern int __start_bst_one[], __stop_bst_one[];\n"
                                  "extern int __start_bst_missing[] __attribute__((weak));\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    long total = 0;\n"
                                  "    for (struct entry* e = __start_bst_sec; e < __stop_bst_sec; e += 1) total += e->value;\n"
                                  "    printf(\"%ld %ld %d %d %ld %d\\n\", total, (long)(__stop_bst_sec - __start_bst_sec), one, (int)(__stop_bst_one - __start_bst_one),\n"
                                  "           between, __start_bst_missing == 0);\n"
                                  "    return 0;\n"
                                  "}\n";
    char source[256];
    char object[256];
    char linked_path[256];
    char direct_path[3][256];
    snprintf(source, sizeof(source), "%s/section-bounds.c", directory);
    snprintf(object, sizeof(object), "%s/section-bounds.o", directory);
    snprintf(linked_path, sizeof(linked_path), "%s/section-bounds-cc", directory);
    for (uint32_t path = 0; path < 3; path += 1)
    {
        snprintf(direct_path[path], sizeof(direct_path[path]), "%s/section-bounds-%u", directory, path);
    }
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "the section bounds fixture is written");
    char reference[256];
    snprintf(reference, sizeof(reference), "%s/section-bounds-clang", directory);
    char expected[64] = { 0 };
    char* text = NULL;
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool oracle = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, oracle, "clang's build of the section bounds fixture runs");
    char* object_arguments[] = { (char*)executable, "-c", "-o", object, source, NULL };
    const int object_status = written ? check_test_driver_spawn(object_arguments, &text) : -1;
    free(text);
    text = NULL;
    char* link_arguments[] = { "cc", "-o", linked_path, object, NULL };
    const int linked = object_status == 0 ? check_test_driver_spawn(link_arguments, &text) : -1;
    free(text);
    text = NULL;
    char captured[64];
    check_test_expect(test, linked == 0 && check_test_driver_capture(linked_path, captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                      "the object, linked by cc, prints clang's build's output");
    static const char* const option_names[] = { "the direct path", "--tier1", "--tier1 --scalar" };
    for (uint32_t path = 0; path < 3; path += 1)
    {
        char* plain[] = { (char*)executable, "-o", direct_path[path], source, NULL };
        char* tier1[] = { (char*)executable, "--tier1", "-o", direct_path[path], source, NULL };
        char* tier1_scalar[] = { (char*)executable, "--tier1", "--scalar", "-o", direct_path[path], source, NULL };
        char* const* arguments = path == 0 ? plain : path == 1 ? tier1 : tier1_scalar;
        const int status = written ? check_test_driver_spawn((char* const*)arguments, &text) : -1;
        free(text);
        text = NULL;
        check_test_expect(test, status == 0 && check_test_driver_capture(direct_path[path], captured, sizeof(captured)) && strcmp(captured, expected) == 0, option_names[path]);
        unlink(direct_path[path]);
    }
    unlink(source);
    unlink(object);
    unlink(linked_path);
    unlink(reference);
}

// Constant CFStrings in code (#148, check_data_cfstring_keys): clang's build of a unit of them, and the unit's -c objects by
// each path and tier, linked by cc beside a driver that defines __CFConstantStringClassReference, and the direct path's
// link of both, each printing clang's: one object of each literal (a static initializer's, NSString's and another
// function's), an address constant cast and offset, the records' fields (the class's address, the flags, ASCII and
// UTF-16 characters, a surrogate pair, a NUL, a conversion cut short, parentheses and a cast, the empty string) and the
// record's size, and literals whose lowest calls are in a function nothing calls and in a static initializer nothing reads
// (their records are the program's all the same).
// The unit doesn't declare the class: its object's symbol is the program's own, an undefined global. A unit that declares
// it takes the declaration's object. And a unit of a CFString in sizeof alone, whose record nothing references, has no
// record, characters or class (#1363's review): its object, by -c and by -c --lower-all, names no class, and it links
// without one, by cc and by the direct path.
static bool check_test_driver_file_holds(const char* path, const char* needle)
{
    FILE* file = fopen(path, "rb");
    char buffer[1 << 16];
    const size_t length = file != NULL ? fread(buffer, 1, sizeof(buffer), file) : 0;
    const bool result = file != NULL && memmem(buffer, length, needle, strlen(needle)) != NULL;
    if (file != NULL)
    {
        fclose(file);
    }
    return result;
}

static void check_test_driver_cfstrings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char unit[] =
        "static const void* never_called(void) { return __builtin___CFStringMakeConstantString(\"only\"); }\n"
        "static const void* never_read = __builtin___CFStringMakeConstantString(\"initial\");\n"
        "static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }\n"
        "typedef struct { const int* isa; int flags; const char* str; long length; } CF;\n"
        "static const void* s1 = __builtin___CFStringMakeConstantString(\"hello\");\n"
        "static const void* s2 = __builtin___NSStringMakeConstantString(\"hello\");\n"
        "static long as_long = (long)__builtin___CFStringMakeConstantString(\"x\");\n"
        "static const char* shifted = (const char*)__builtin___CFStringMakeConstantString(\"y\") + 8;\n"
        "static const void* other(void) { return __builtin___CFStringMakeConstantString(\"hello\"); }\n"
        "const int* class_reference(void);\n"
        "static void bytes(const CF* c)\n"
        "{\n"
        "    mix(c->isa == class_reference()); mix((unsigned)c->flags); mix((unsigned long)c->length);\n"
        "    if (c->flags == 0x7d0) { const unsigned short* u = (const unsigned short*)c->str; for (long i = 0; i <= c->length; i++) mix(u[i]); }\n"
        "    else { for (long i = 0; i <= c->length; i++) mix((unsigned char)c->str[i]); }\n"
        "}\n"
        "int tested(int n)\n"
        "{\n"
        "    h = (unsigned)n;\n"
        "    const CF* a = (const CF*)__builtin___CFStringMakeConstantString(\"hello\");\n"
        "    mix(a == s1); mix(a == s2); mix(a == other()); mix((long)a == as_long);\n"
        "    mix((const CF*)as_long == (const CF*)__builtin___CFStringMakeConstantString(\"x\"));\n"
        "    mix(shifted == (const char*)__builtin___CFStringMakeConstantString(\"y\") + 8);\n"
        "    bytes(a); bytes((const CF*)__builtin___CFStringMakeConstantString(\"\"));\n"
        "    bytes((const CF*)__builtin___CFStringMakeConstantString(\"h\\xc3\\xa9llo \\xf0\\x9f\\x98\\x80\"));\n"
        "    bytes((const CF*)__builtin___CFStringMakeConstantString(\"a\\0b\"));\n"
        "    bytes((const CF*)__builtin___CFStringMakeConstantString(\"cut \\xc3\\x28 here\"));\n"
        "    bytes((const CF*)__builtin___CFStringMakeConstantString((\"pa\" \"ren\")));\n"
        "    bytes((const CF*)__builtin___CFStringMakeConstantString((const char*)\"cast\"));\n"
        "    bytes((const CF*)__builtin___NSStringMakeConstantString(\"ns\"));\n"
        "    bytes((const CF*)__builtin___CFStringMakeConstantString(\"only\"));\n"
        "    bytes((const CF*)__builtin___CFStringMakeConstantString(\"initial\"));\n"
        "    mix(__builtin___NSStringMakeConstantString(\"ns\") == __builtin___CFStringMakeConstantString(\"ns\"));\n"
        "    mix(__builtin___CFStringMakeConstantString(\"a\\0b\") != __builtin___CFStringMakeConstantString(\"a\"));\n"
        "    mix(__builtin___CFStringMakeConstantString(\"abc\")->length * 10 + sizeof(*__builtin___CFStringMakeConstantString(\"q\")));\n"
        "    return (int)(h ^ (h >> 32));\n"
        "}\n";
    static const char driver[] =
        "#include <stdio.h>\n"
        "int __CFConstantStringClassReference[4];\n"
        "const int* class_reference(void) { return __CFConstantStringClassReference; }\n"
        "int tested(int);\n"
        "int main(void) { printf(\"%d %d\\n\", tested(3), tested(-2)); return 0; }\n";
    static const char declared[] =
        "extern const int __CFConstantStringClassReference[];\n"
        "typedef struct { const int* isa; int flags; const char* str; long length; } CF;\n"
        "const int* class_reference(void);\n"
        "int tested(int n) { const CF* c = (const CF*)__builtin___CFStringMakeConstantString(\"d\"); return n + (c->isa == class_reference()) * 10 +\n"
        "(c->isa == __CFConstantStringClassReference) * 100 + (int)c->length * 1000; }\n";
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        char source[256];
        char main_source[256];
        char object[256];
        char reference[256];
        char linked[256];
        snprintf(source, sizeof(source), "%s/cfstrings-%u.c", directory, variant);
        snprintf(main_source, sizeof(main_source), "%s/cfstrings-main.c", directory);
        snprintf(object, sizeof(object), "%s/cfstrings-%u.o", directory, variant);
        snprintf(reference, sizeof(reference), "%s/cfstrings-clang", directory);
        snprintf(linked, sizeof(linked), "%s/cfstrings-linked", directory);
        const char* program = variant == 0 ? unit : declared;
        const bool written = parse_test_write(source, program, strlen(program)) && parse_test_write(main_source, driver, sizeof(driver) - 1);
        check_test_expect(test, written, "the constant CFStrings' fixture is written");
        char* text = NULL;
        char* clang_arguments[] = { (char*)clang, "-O0", "-w", "-o", reference, source, main_source, NULL };
        const bool referenced = written && check_test_driver_spawn(clang_arguments, &text) == 0;
        free(text);
        text = NULL;
        char expected[64] = { 0 };
        const bool oracle = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
        check_test_expect(test, oracle, variant == 0 ? "clang's build of the constant CFStrings runs" : "clang's build of a declared class's CFString runs");
        static const char* const names[] = { "-c's object, linked by cc", "-c --scalar's", "-c --tier1's", "-c --tier1 --scalar's", "the direct path's link" };
        for (uint32_t path = 0; oracle && path < 5; path += 1)
        {
            char* plain[] = { (char*)executable, "-w", "-c", "-o", object, source, NULL };
            char* scalar[] = { (char*)executable, "--scalar", "-w", "-c", "-o", object, source, NULL };
            char* tier1[] = { (char*)executable, "--tier1", "-w", "-c", "-o", object, source, NULL };
            char* tier1_scalar[] = { (char*)executable, "--tier1", "--scalar", "-w", "-c", "-o", object, source, NULL };
            char* direct[] = { (char*)executable, "-w", "-o", linked, source, main_source, NULL };
            char* const* arguments = path == 0 ? plain : path == 1 ? scalar : path == 2 ? tier1 : path == 3 ? tier1_scalar : direct;
            int status = check_test_driver_spawn(arguments, &text);
            free(text);
            text = NULL;
            char* link_arguments[] = { "cc", "-o", linked, object, main_source, NULL };
            status = status == 0 && path < 4 ? check_test_driver_spawn(link_arguments, &text) : status;
            free(text);
            text = NULL;
            char captured[64] = { 0 };
            char description[160];
            snprintf(description, sizeof(description), "%s CFStrings: %s prints clang's build's output", variant == 0 ? "the unit's" : "a declared class's", names[path]);
            check_test_expect(test, status == 0 && check_test_driver_capture(linked, captured, sizeof(captured)) && strcmp(captured, expected) == 0, description);
            unlink(linked);
        }
        unlink(source);
        unlink(main_source);
        unlink(object);
        unlink(reference);
    }
    // Its files' names hold no "cfstring", which the object is searched for.
    static const char unreferenced[] = "int main(void) { return sizeof(__builtin___CFStringMakeConstantString(\"x\")) != sizeof(void *); }\n";
    char source[256];
    char object[256];
    char linked[256];
    snprintf(source, sizeof(source), "%s/constant-string-sizeof.c", directory);
    snprintf(object, sizeof(object), "%s/constant-string-sizeof.o", directory);
    snprintf(linked, sizeof(linked), "%s/constant-string-sizeof", directory);
    const bool written = parse_test_write(source, unreferenced, sizeof(unreferenced) - 1);
    check_test_expect(test, written, "the unreferenced CFString's fixture is written");
    for (uint32_t path = 0; written && path < 4; path += 1)
    {
        const bool all = (path & 1) != 0;
        const bool direct = (path & 2) != 0;
        char* plain[] = { (char*)executable, "-w", "-c", "-o", object, source, NULL };
        char* plain_all[] = { (char*)executable, "--lower-all", "-w", "-c", "-o", object, source, NULL };
        char* linking[] = { (char*)executable, "-w", "-o", linked, source, NULL };
        char* linking_all[] = { (char*)executable, "--lower-all", "-w", "-o", linked, source, NULL };
        char* const* arguments = direct ? (all ? linking_all : linking) : (all ? plain_all : plain);
        char* text = NULL;
        int status = check_test_driver_spawn(arguments, &text);
        free(text);
        text = NULL;
        const bool clean = direct || (status == 0 && !check_test_driver_file_holds(object, "__CFConstantStringClassReference") &&
                                      !check_test_driver_file_holds(object, "cfstring"));
        char* link_arguments[] = { "cc", "-o", linked, object, NULL };
        status = status == 0 && !direct ? check_test_driver_spawn(link_arguments, &text) : status;
        free(text);
        text = NULL;
        char captured[16] = { 0 };
        char description[160];
        snprintf(description, sizeof(description), "a CFString in sizeof alone%s: %s, no record or class, links and runs", all ? " (--lower-all)" : "",
                 direct ? "the direct path's link" : "-c's object, linked by cc");
        check_test_expect(test, clean && status == 0 && check_test_driver_capture(linked, captured, sizeof(captured)), description);
        unlink(linked);
    }
    unlink(source);
    unlink(object);
}

// #1122's own program, one atom in the section, whose count is 1: by clang, by -c's object that cc links, by the direct
// path and by --tier1 with and without its scalar twin, each printing "5 1"; and a strong reference to a section that
// doesn't exist, which stays undefined, as GNU ld and lld leave it.
static void check_test_driver_section_bounds_exact(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "__attribute__((section(\"mysec\"))) int in_sec = 5;\n"
                                  "extern int __start_mysec[], __stop_mysec[];\n"
                                  "int main(void) { printf(\"%d %d\\n\", in_sec, (int)(__stop_mysec - __start_mysec)); return 0; }\n";
    static const char missing_program[] = "extern int __start_bst_absent[];\n"
                                          "int main(void) { return __start_bst_absent != 0; }\n";
    static const char expected[] = "5 1\n";
    char source[256];
    char missing_source[256];
    char object[256];
    char reference[256];
    char linked_path[256];
    char direct_path[3][256];
    snprintf(source, sizeof(source), "%s/section-bounds-exact.c", directory);
    snprintf(missing_source, sizeof(missing_source), "%s/section-bounds-missing.c", directory);
    snprintf(object, sizeof(object), "%s/section-bounds-exact.o", directory);
    snprintf(reference, sizeof(reference), "%s/section-bounds-exact-clang", directory);
    snprintf(linked_path, sizeof(linked_path), "%s/section-bounds-exact-cc", directory);
    for (uint32_t path = 0; path < 3; path += 1)
    {
        snprintf(direct_path[path], sizeof(direct_path[path]), "%s/section-bounds-exact-%u", directory, path);
    }
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "#1122's fixture is written");
    char* text = NULL;
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &text) == 0;
    free(text);
    text = NULL;
    char captured[64] = { 0 };
    const bool oracle = referenced && check_test_driver_capture(reference, captured, sizeof(captured)) && strcmp(captured, expected) == 0;
    check_test_expect(test, oracle, "clang's build prints 5 1");
    char* object_arguments[] = { (char*)executable, "-c", "-o", object, source, NULL };
    const int object_status = written ? check_test_driver_spawn(object_arguments, &text) : -1;
    free(text);
    text = NULL;
    char* link_arguments[] = { "cc", "-o", linked_path, object, NULL };
    const int linked = object_status == 0 ? check_test_driver_spawn(link_arguments, &text) : -1;
    free(text);
    text = NULL;
    check_test_expect(test, linked == 0 && check_test_driver_capture(linked_path, captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                      "the object, linked by cc, prints 5 1");
    static const char* const option_names[] = { "the direct path prints 5 1", "--tier1 prints 5 1", "--tier1 --scalar prints 5 1" };
    for (uint32_t path = 0; path < 3; path += 1)
    {
        char* plain[] = { (char*)executable, "-o", direct_path[path], source, NULL };
        char* tier1[] = { (char*)executable, "--tier1", "-o", direct_path[path], source, NULL };
        char* tier1_scalar[] = { (char*)executable, "--tier1", "--scalar", "-o", direct_path[path], source, NULL };
        char* const* arguments = path == 0 ? plain : path == 1 ? tier1 : tier1_scalar;
        const int status = written ? check_test_driver_spawn((char* const*)arguments, &text) : -1;
        free(text);
        text = NULL;
        check_test_expect(test, status == 0 && check_test_driver_capture(direct_path[path], captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                          option_names[path]);
        unlink(direct_path[path]);
    }
    const bool missing_written = parse_test_write(missing_source, missing_program, sizeof(missing_program) - 1);
    check_test_expect(test, missing_written, "the missing section's fixture is written");
    char* missing_arguments[] = { (char*)executable, "-o", direct_path[0], missing_source, NULL };
    const int missing_status = missing_written ? check_test_driver_spawn(missing_arguments, &text) : -1;
    const bool undefined = missing_status != 0 && text != NULL && strstr(text, "undefined reference to '__start_bst_absent'") != NULL;
    check_test_expect(test, undefined, "a strong reference to a missing section stays undefined");
    free(text);
    unlink(direct_path[0]);
    unlink(source);
    unlink(missing_source);
    unlink(object);
    unlink(reference);
    unlink(linked_path);
}

// #1122's bound section's static members: nothing references a static in a bound section, so selection's edges alone
// drop it and its section loses a member GNU ld's __stop covers. The bound references in the unit root the section's
// members, the statics too, as gcc's objects keep them; clang's -c drops them, so the oracle is gcc's output: `expected`.
// `stem` names the fixture's files and `what` its checks.
static void check_test_driver_section_bounds_rooted(CheckTest* test, const char* executable, const char* directory, const char* program, const char* stem,
                                                    const char* expected, const char* what)
{
    char source[256];
    char object[256];
    char linked_path[256];
    char direct_path[3][256];
    char message[160];
    snprintf(source, sizeof(source), "%s/%s.c", directory, stem);
    snprintf(object, sizeof(object), "%s/%s.o", directory, stem);
    snprintf(linked_path, sizeof(linked_path), "%s/%s-cc", directory, stem);
    for (uint32_t path = 0; path < 3; path += 1)
    {
        snprintf(direct_path[path], sizeof(direct_path[path]), "%s/%s-%u", directory, stem, path);
    }
    const bool written = parse_test_write(source, program, strlen(program));
    snprintf(message, sizeof(message), "the fixture of %s is written", what);
    check_test_expect(test, written, message);
    char* text = NULL;
    char* object_arguments[] = { (char*)executable, "-c", "-o", object, source, NULL };
    const int object_status = written ? check_test_driver_spawn(object_arguments, &text) : -1;
    free(text);
    text = NULL;
    char* link_arguments[] = { "cc", "-o", linked_path, object, NULL };
    const int linked = object_status == 0 ? check_test_driver_spawn(link_arguments, &text) : -1;
    free(text);
    text = NULL;
    char captured[64];
    snprintf(message, sizeof(message), "the object keeps %s, and cc's link's bounds cover them", what);
    check_test_expect(test, linked == 0 && check_test_driver_capture(linked_path, captured, sizeof(captured)) && strcmp(captured, expected) == 0, message);
    static const char* const option_names[] = { "the direct path", "--tier1", "--tier1 --scalar" };
    for (uint32_t path = 0; path < 3; path += 1)
    {
        char* plain[] = { (char*)executable, "-o", direct_path[path], source, NULL };
        char* tier1[] = { (char*)executable, "--tier1", "-o", direct_path[path], source, NULL };
        char* tier1_scalar[] = { (char*)executable, "--tier1", "--scalar", "-o", direct_path[path], source, NULL };
        char* const* arguments = path == 0 ? plain : path == 1 ? tier1 : tier1_scalar;
        const int status = written ? check_test_driver_spawn((char* const*)arguments, &text) : -1;
        free(text);
        text = NULL;
        snprintf(message, sizeof(message), "%s covers %s", option_names[path], what);
        check_test_expect(test, status == 0 && check_test_driver_capture(direct_path[path], captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                          message);
        unlink(direct_path[path]);
    }
    unlink(source);
    unlink(object);
    unlink(linked_path);
}

// The statics of a bound section, by plain bound names ("24 5", gcc's), and by bound names an asm label gives other
// identifiers (#1187's review): the label is the bound's linkage name, so its section's members are rooted all the same
// ("1 3 2", gcc's).
static void check_test_driver_section_bounds_static(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "struct entry { long value; };\n"
                                  "static __attribute__((section(\"st_sec\"), aligned(16))) struct entry s0 = { 7 };\n"
                                  "static __attribute__((section(\"st_sec\"), aligned(16))) struct entry s1 = { 8 };\n"
                                  "__attribute__((section(\"st_sec\"), aligned(16))) struct entry g0 = { 9 };\n"
                                  "extern struct entry __start_st_sec[], __stop_st_sec[];\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    long total = 0;\n"
                                  "    for (struct entry* e = __start_st_sec; e < __stop_st_sec; e += 1) total += e->value;\n"
                                  "    printf(\"%ld %ld\\n\", total, (long)(__stop_st_sec - __start_st_sec));\n"
                                  "    return 0;\n"
                                  "}\n";
    static const char renamed[] = "#include <stdio.h>\n"
                                  "__attribute__((section(\"mreg\"))) static int a = 1;\n"
                                  "__attribute__((section(\"mreg\"))) static int b = 2;\n"
                                  "extern int begin[] __asm__(\"__start_mreg\");\n"
                                  "extern int end[] __asm__(\"__stop_mreg\");\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    int total = 0;\n"
                                  "    for (int* p = begin; p < end; p++) total += *p;\n"
                                  "    printf(\"%d %d %td\\n\", a, total, end - begin);\n"
                                  "    return 0;\n"
                                  "}\n";
    check_test_driver_section_bounds_rooted(test, executable, directory, program, "section-bounds-static", "24 5\n", "the section's statics");
    check_test_driver_section_bounds_rooted(test, executable, directory, renamed, "section-bounds-renamed", "1 3 2\n", "the statics of an asm-labelled bound");
}

// #1122: two bound sections of one bucket, whose largest member alignments match while their members' don't, so the
// second run's start is its section's alignment's, not the alignment its first member's. GNU ld and lld align the run
// as its output section's; a run that only aligned its first member to the member's own would start inside the first
// section's alignment gap, changing the span the bounds report.
static void check_test_driver_section_bounds_align(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "#include <stdio.h>\n"
                                  "#include <stdint.h>\n"
                                  "__attribute__((section(\"aaa\"), aligned(16), used)) int a0 = 1;\n"
                                  "__attribute__((section(\"aaa\"), aligned(4), used)) int a1 = 2;\n"
                                  "__attribute__((section(\"bbb\"), aligned(4), used)) int b0 = 3;\n"
                                  "__attribute__((section(\"bbb\"), aligned(16), used)) int b1 = 4;\n"
                                  "extern char __start_aaa[], __stop_aaa[], __start_bbb[], __stop_bbb[];\n"
                                  "int main(void)\n"
                                  "{\n"
                                  "    printf(\"aaa start%%16=%lu span=%ld; bbb start%%16=%lu span=%ld\\n\",\n"
                                  "           (unsigned long)((uintptr_t)__start_aaa % 16),\n"
                                  "           (long)(__stop_aaa - __start_aaa),\n"
                                  "           (unsigned long)((uintptr_t)__start_bbb % 16),\n"
                                  "           (long)(__stop_bbb - __start_bbb));\n"
                                  "    return 0;\n"
                                  "}\n";
    static const char expected[] = "aaa start%16=0 span=8; bbb start%16=0 span=20\n";
    char source[256];
    char object[256];
    char linked_path[256];
    char direct_path[3][256];
    snprintf(source, sizeof(source), "%s/section-bounds-align.c", directory);
    snprintf(object, sizeof(object), "%s/section-bounds-align.o", directory);
    snprintf(linked_path, sizeof(linked_path), "%s/section-bounds-align-cc", directory);
    for (uint32_t path = 0; path < 3; path += 1)
    {
        snprintf(direct_path[path], sizeof(direct_path[path]), "%s/section-bounds-align-%u", directory, path);
    }
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "the two sections' fixture is written");
    char* text = NULL;
    char* object_arguments[] = { (char*)executable, "-c", "-o", object, source, NULL };
    const int object_status = written ? check_test_driver_spawn(object_arguments, &text) : -1;
    free(text);
    text = NULL;
    char* link_arguments[] = { "cc", "-o", linked_path, object, NULL };
    const int linked = object_status == 0 ? check_test_driver_spawn(link_arguments, &text) : -1;
    free(text);
    text = NULL;
    char captured[128];
    check_test_expect(test, linked == 0 && check_test_driver_capture(linked_path, captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                      "cc's link starts each run at its section's alignment");
    static const char* const option_names[] = { "the direct path starts each run at its section's alignment", "--tier1 starts each run at its section's alignment",
                                                "--tier1 --scalar starts each run at its section's alignment" };
    for (uint32_t path = 0; path < 3; path += 1)
    {
        char* plain[] = { (char*)executable, "-o", direct_path[path], source, NULL };
        char* tier1[] = { (char*)executable, "--tier1", "-o", direct_path[path], source, NULL };
        char* tier1_scalar[] = { (char*)executable, "--tier1", "--scalar", "-o", direct_path[path], source, NULL };
        char* const* arguments = path == 0 ? plain : path == 1 ? tier1 : tier1_scalar;
        const int status = written ? check_test_driver_spawn((char* const*)arguments, &text) : -1;
        free(text);
        text = NULL;
        check_test_expect(test, status == 0 && check_test_driver_capture(direct_path[path], captured, sizeof(captured)) && strcmp(captured, expected) == 0,
                          option_names[path]);
        unlink(direct_path[path]);
    }
    unlink(source);
    unlink(object);
    unlink(linked_path);
}

// #591: member accesses whose lookups take every kind of record's: a struct of 2,000 members, with an anonymous union
// every 40th and an anonymous struct of two bit-fields every 60th, written, read and their offsets taken; and 300 small
// structs each read by a function of its own. Both paths must write the same object, which clang links into a program
// that prints what clang's own build prints, and so must the direct path's executable: the lowering's offsets, which it
// takes from the checker's stream, are clang's. And a vector's element through a pointer, `(*p).y`, a MEMBER node that
// is no record's member, which the lowering must still refuse rather than read at the stream's offset 0.
#define BUSTER_CHECK_TEST_DRIVER_MEMBERS 2000
#define BUSTER_CHECK_TEST_DRIVER_RECORDS 300

static void check_test_driver_members(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    const uint64_t capacity = UINT64_C(1) << 20;
    char* program = malloc(capacity);
    uint64_t length = 0;
    if (program != NULL)
    {
        length += (uint64_t)snprintf(program + length, capacity - length, "#include <stdio.h>\nstruct big {\n");
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_MEMBERS; i += 1)
        {
            length += (uint64_t)(i % 40 == 39 ? snprintf(program + length, capacity - length, "    union { int u%u; short v%u; };\n", i, i) :
                                 i % 60 == 59 ? snprintf(program + length, capacity - length, "    struct { unsigned b%u : 5; unsigned c%u : 7; };\n", i, i) :
                                                snprintf(program + length, capacity - length, "    int m%u;\n", i));
        }
        length += (uint64_t)snprintf(program + length, capacity - length, "};\n");
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_RECORDS; i += 1)
        {
            length += (uint64_t)snprintf(program + length, capacity - length,
                                         "struct s%u { char a; int b; short c; };\n"
                                         "static int f%u(struct s%u* p) { return p->a * 3 + p->b * 5 + p->c * 7 + p->b; }\n",
                                         i, i, i);
        }
        length += (uint64_t)snprintf(program + length, capacity - length, "static struct big g;\nint main(void)\n{\n    struct big* p = &g;\n    unsigned long sum = 0;\n");
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_MEMBERS; i += 1)
        {
            length += (uint64_t)(i % 40 == 39 ? snprintf(program + length, capacity - length, "    p->u%u = %u;\n", i, i * 7919u) :
                                 i % 60 == 59 ? snprintf(program + length, capacity - length, "    p->b%u = %u;\n    g.c%u = %u;\n", i, i % 32, i, i % 128) :
                                                snprintf(program + length, capacity - length, "    p->m%u = %u;\n", i, i * 7 + 1));
        }
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_MEMBERS; i += 1)
        {
            length += (uint64_t)(i % 40 == 39 ? snprintf(program + length, capacity - length, "    sum = sum * 31 + (unsigned)p->v%u + (unsigned long)((char*)&g.u%u - (char*)p);\n", i, i) :
                                 i % 60 == 59 ? snprintf(program + length, capacity - length, "    sum = sum * 31 + p->b%u + g.c%u;\n", i, i) :
                                 i % 37 == 0 ? snprintf(program + length, capacity - length, "    sum = sum * 31 + (unsigned)g.m%u + (unsigned long)((char*)&p->m%u - (char*)&g);\n", i, i) :
                                               snprintf(program + length, capacity - length, "    sum = sum * 31 + (unsigned)p->m%u;\n", i));
        }
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_DRIVER_RECORDS; i += 1)
        {
            length += (uint64_t)snprintf(program + length, capacity - length, "    { struct s%u v; v.a = %u; v.b = %u; v.c = %u; sum = sum * 7 + (unsigned)f%u(&v); }\n",
                                         i, i % 100, i * 3, i * 5, i);
        }
        length += (uint64_t)snprintf(program + length, capacity - length, "    printf(\"%%lu\\n\", sum);\n    return 0;\n}\n");
    }
    char source[256];
    char objects[2][256];
    char paths[3][256];
    snprintf(source, sizeof(source), "%s/members.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/members.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/members-scalar.o", directory);
    snprintf(paths[0], sizeof(paths[0]), "%s/members-clang", directory);
    snprintf(paths[1], sizeof(paths[1]), "%s/members-linked", directory);
    snprintf(paths[2], sizeof(paths[2]), "%s/members-direct", directory);
    const bool written = program != NULL && length < capacity && parse_test_write(source, program, length);
    check_test_expect(test, written, "the members' program is written");
    char* text = NULL;
    bool compiled = written;
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-c", "-o", objects[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", objects[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0, path == 0 ? "the members' program compiles with -c" : "the members' program compiles with -c, scalar");
        compiled = compiled && status == 0;
        free(text);
        text = NULL;
    }
    check_test_expect(test, compiled && check_test_driver_same_files(objects[0], objects[1]), "both paths write the same object of the members' program");
    char* const reference[] = { (char*)clang, "-std=gnu17", "-w", "-o", paths[0], source, NULL };
    char* const link[] = { (char*)clang, "-o", paths[1], objects[0], NULL };
    char* const direct[] = { (char*)executable, "-o", paths[2], source, NULL };
    char* const* commands[3] = {
        reference,
        link,
        direct,
    };
    char captured[3][64] = { { 0 } };
    bool ran[3] = { false };
    for (uint32_t command = 0; written && command < 3; command += 1)
    {
        const int status = command != 1 || compiled ? check_test_driver_spawn(commands[command], &text) : -1;
        free(text);
        text = NULL;
        ran[command] = status == 0 && check_test_driver_capture(paths[command], captured[command], sizeof(captured[command])) && captured[command][0] != 0;
    }
    check_test_expect(test, ran[0], "clang's build of the members' program runs");
    check_test_expect(test, ran[0] && ran[1] && strcmp(captured[1], captured[0]) == 0, "clang's link of the members' object prints what clang's build prints");
    check_test_expect(test, ran[0] && ran[2] && strcmp(captured[2], captured[0]) == 0, "the direct path's members' program prints what clang's build prints");
    static const char swizzle[] = "typedef float v4 __attribute__((ext_vector_type(4)));\nfloat f(v4* p) { return (*p).y; }\n";
    const bool swizzle_written = parse_test_write(source, swizzle, sizeof(swizzle) - 1);
    for (uint32_t path = 0; swizzle_written && path < 2; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, "-c", "-o", objects[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", objects[1], source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, "unsupported: this member access") != NULL,
                          path == 0 ? "a vector's element through a pointer is refused as a member access" :
                                      "a vector's element through a pointer is refused as a member access, scalar");
        free(text);
        text = NULL;
    }
    // `p->y` (#1447's finding): the checker takes it as `(*p).y`, and the lowering refuses it as it refuses that.
    static const char arrow[] = "typedef float v4 __attribute__((ext_vector_type(4)));\nfloat f(v4* p) { return p->y; }\n";
    const bool arrow_written = parse_test_write(source, arrow, sizeof(arrow) - 1);
    for (uint32_t path = 0; arrow_written && path < 4; path += 1)
    {
        char* const vector_arguments[] = { (char*)executable, path < 2 ? "-fsyntax-only" : "-c", "-o", objects[0], source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", path < 2 ? "-fsyntax-only" : "-c", "-o", objects[1], source, NULL };
        const int status = check_test_driver_spawn(path % 2 == 0 ? vector_arguments : scalar_arguments, &text);
        const bool expected = path < 2 ? status == 0 : status == 1 && text != NULL && strstr(text, "unsupported: this member access") != NULL;
        static const char* const names[] = {
            "a pointer's extended vector's lane is checked",
            "a pointer's extended vector's lane is checked, scalar",
            "a pointer's extended vector's lane is refused as a member access",
            "a pointer's extended vector's lane is refused as a member access, scalar",
        };
        check_test_expect(test, expected, names[path]);
        free(text);
        text = NULL;
    }
    unlink(source);
    unlink(objects[0]);
    unlink(objects[1]);
    for (uint32_t i = 0; i < 3; i += 1)
    {
        unlink(paths[i]);
    }
    free(program);
}

// #978: arrays of 16 bytes or more, which the x86-64 psABI aligns to 16, after a char each so that an alignment of 1 would
// misalign them: a global one, which clang -O2's code clears with movaps through its `extern` declaration, a static one
// it clears through a pointer it may assume aligned, and a thread-local one it clears through its own declaration. The
// object, the same by both paths, linked with clang's must run to 0, where it faulted on the first movaps before.
static void check_test_driver_array_alignment(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char program[] = "char pad1 = 1;\nchar big[64];\nchar pad2 = 2;\nstatic char pad3 = 3;\nstatic char hidden[48];\n"
                                  "_Thread_local char pad4 = 4;\n_Thread_local char tls[32];\n"
                                  "char* hidden_address(void) { return hidden; }\nvoid clear_all(void);\n"
                                  "int main(void)\n{\n    big[0] = 1;\n    hidden[47] = 2;\n    tls[31] = 3;\n    clear_all();\n"
                                  "    return big[0] + hidden[47] + tls[31] + pad1 + pad2 + pad3 + pad4 - 10;\n}\n";
    static const char clear[] = "extern char big[64];\nextern _Thread_local char tls[32];\nchar* hidden_address(void);\n"
                                "void clear_all(void)\n{\n    char* hidden = __builtin_assume_aligned(hidden_address(), 16);\n"
                                "    for (int i = 0; i < 64; i += 1) big[i] = 0;\n    for (int i = 0; i < 48; i += 1) hidden[i] = 0;\n"
                                "    for (int i = 0; i < 32; i += 1) tls[i] = 0;\n}\n";
    char sources[2][256];
    char objects[3][256];
    char linked[256];
    snprintf(sources[0], sizeof(sources[0]), "%s/alignment.c", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/alignment-clear.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/alignment.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/alignment-scalar.o", directory);
    snprintf(objects[2], sizeof(objects[2]), "%s/alignment-clear.o", directory);
    snprintf(linked, sizeof(linked), "%s/alignment-linked", directory);
    const bool written = parse_test_write(sources[0], program, sizeof(program) - 1) && parse_test_write(sources[1], clear, sizeof(clear) - 1);
    check_test_expect(test, written, "the arrays' alignment program is written");
    char* text = NULL;
    char* const vector_arguments[] = { (char*)executable, "-c", "-o", objects[0], sources[0], NULL };
    char* const scalar_arguments[] = { (char*)executable, "--scalar", "-c", "-o", objects[1], sources[0], NULL };
    char* const clear_arguments[] = { (char*)clang, "-O2", "-c", "-o", objects[2], sources[1], NULL };
    char* const* compiles[3] = {
        vector_arguments,
        scalar_arguments,
        clear_arguments,
    };
    bool compiled = written;
    for (uint32_t i = 0; written && i < 3; i += 1)
    {
        const int status = check_test_driver_spawn(compiles[i], &text);
        compiled = compiled && status == 0;
        free(text);
        text = NULL;
    }
    check_test_expect(test, compiled && check_test_driver_same_files(objects[0], objects[1]), "both paths write the same object of the arrays' alignment program");
    char* const link_arguments[] = { (char*)clang, "-o", linked, objects[0], objects[2], NULL };
    const int link_status = compiled ? check_test_driver_spawn(link_arguments, &text) : -1;
    free(text);
    text = NULL;
    char* const run_arguments[] = { linked, NULL };
    const int run_status = link_status == 0 ? check_test_driver_spawn(run_arguments, &text) : -1;
    free(text);
    check_test_expect(test, run_status == 0, "arrays of 16 bytes or more are aligned to 16: clang -O2's aligned stores into them run");
    for (uint32_t i = 0; i < 2; i += 1)
    {
        unlink(sources[i]);
    }
    for (uint32_t i = 0; i < 3; i += 1)
    {
        unlink(objects[i]);
    }
    unlink(linked);
}

// Filesystem fixtures keep the conservative overlap relation independent of the identity representation.
// The two missing spellings and the directory symlink must alias even though the leaf does not exist.
static void check_test_dependency_identities(CheckTest* test, const char* directory)
{
    char paths[10][512];
    snprintf(paths[0], sizeof(paths[0]), "%s/admission-identity-file", directory);
    snprintf(paths[1], sizeof(paths[1]), "%s/admission-identity-hard", directory);
    snprintf(paths[2], sizeof(paths[2]), "%s/admission-identity-link", directory);
    snprintf(paths[3], sizeof(paths[3]), "%s/admission-identity-absent", directory);
    snprintf(paths[4], sizeof(paths[4]), "%s/./admission-identity-absent", directory);
    snprintf(paths[5], sizeof(paths[5]), "%s/admission-identity-dangling", directory);
    snprintf(paths[6], sizeof(paths[6]), "%s/admission-identity-distinct", directory);
    snprintf(paths[7], sizeof(paths[7]), "%s/admission-identity-missing-dir/admission-identity-absent", directory);
    snprintf(paths[8], sizeof(paths[8]), "%s/./admission-identity-file", directory);
    snprintf(paths[9], sizeof(paths[9]), "%s/admission-identity-directory/admission-identity-absent", directory);
    char linked_directory[512];
    snprintf(linked_directory, sizeof(linked_directory), "%s/admission-identity-directory", directory);
    const bool ready = parse_test_write(paths[0], "identity", 8) && link(paths[0], paths[1]) == 0 &&
                       symlink("admission-identity-file", paths[2]) == 0 && symlink("admission-identity-absent", paths[5]) == 0 &&
                       symlink(".", linked_directory) == 0;
    check_test_expect(test, ready, "dependency identity fixture creation");
    if (ready)
    {
        DependencyIdentity identities[10];
        uint64_t probes = 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(identities); i += 1)
        {
            identities[i] = dependency_identity(paths[i], &probes);
        }
        check_test_expect(test, probes <= 3 * BUSTER_ARRAY_LENGTH(identities), "identity collection uses at most three probes per path");
        for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(identities); a += 1)
        {
            for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(identities); b += 1)
            {
                const bool file_a = a <= 2 || a == 8;
                const bool file_b = b <= 2 || b == 8;
                const bool missing_a = a == 3 || a == 4 || a == 9;
                const bool missing_b = b == 3 || b == 4 || b == 9;
                const bool expected = a == b || a == 5 || b == 5 || (file_a && file_b) || (missing_a && missing_b);
                check_test_expect(test, dependency_identities_overlap(&identities[a], &identities[b]) == expected,
                                  "dependency identity preserves textual, inode, parent and dangling-link overlap");
            }
        }
        const DependencyIdentity root = dependency_identity("/buster-dependency-admission-root-missing", NULL);
        struct stat root_status;
        check_test_expect(test, stat("/", &root_status) == 0 && root.parent_found && root.parent_device == root_status.st_dev &&
                                root.parent_inode == root_status.st_ino, "root-level dependency path observes the root directory");
        char long_path[PATH_MAX + 8];
        memset(long_path, 'x', sizeof(long_path));
        long_path[PATH_MAX] = '/';
        long_path[PATH_MAX + 1] = 'a';
        long_path[PATH_MAX + 2] = 0;
        const DependencyIdentity long_identity = dependency_identity(long_path, NULL);
        check_test_expect(test, !long_identity.found && !long_identity.parent_found && !long_identity.dangling &&
                                dependency_identities_overlap(&long_identity, &long_identity),
                          "overlong parent keeps textual equality without truncating directory identity");
    }
    unlink(paths[0]);
    unlink(paths[1]);
    unlink(paths[2]);
    unlink(paths[5]);
    unlink(linked_directory);
}

// Exercise admission directly so its syscall count and command-arena growth are visible independently of compilation.
static void check_test_dependency_admission(CheckTest* test, const char* directory)
{
    char names[32][512];
    char* sources[32];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(names[i], sizeof(names[i]), "%s/dependency-admission-%02u.c", directory, i);
        sources[i] = names[i];
    }
    const uint32_t counts[] = { 0, 1, 16, 32 };
    DependencyOptions options = { .enabled = true };
    Arena arena = arena_reserve(UINT64_C(1) << 20);
    check_test_expect(test, arena.base != NULL, "dependency admission scratch");
    if (arena.base != NULL)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(counts); i += 1)
        {
            arena_pop_to(&arena, 0);
            const uint32_t count = counts[i];
            uint64_t probes = 0;
            const bool admitted = dependency_check_destinations_counted(&options, sources, count, NULL, true, false, &arena, &probes);
            const uint64_t storage = (uint64_t)count * (80 + 2 * sizeof(DependencyIdentity)) + 3 * _Alignof(DependencyIdentity);
            check_test_expect(test, admitted && arena.length <= storage,
                              "distinct dependency destinations use linear filename and identity storage");
            check_test_expect(test, admitted && probes <= (uint64_t)count * 9 && (count == 0 || probes != 0),
                              "dependency admission probes each source, object and destination at most three times");
        }
        arena_pop_to(&arena, 0);
        sources[31] = "-";
        uint64_t probes = 0;
        check_test_expect(test, dependency_check_destinations_counted(&options, sources, 32, NULL, true, false, &arena, &probes) && probes <= 32 * 9,
                          "reused input identities skip stdin in every destination pass");
        sources[31] = names[31];
        arena_pop_to(&arena, 0);
        probes = 0;
        check_test_expect(test, dependency_check_destinations_counted(&options, sources, 32, NULL, false, true, &arena, &probes) && probes <= (32 * 2 + 1) * 3,
                          "implicit executable identity is collected once across all destinations");
        arena_pop_to(&arena, 0);
        probes = 0;
        check_test_expect(test, dependency_check_destinations_counted(&options, sources, 32, NULL, false, false, &arena, &probes) && probes <= 32 * 6,
                          "syntax-only dependency admission probes no primary output");
        arena_pop_to(&arena, 0);
        options.path = "-";
        probes = 0;
        check_test_expect(test, dependency_check_destinations_counted(&options, sources, 32, NULL, true, false, &arena, &probes) && arena.length == 0 && probes == 0,
                          "stdout dependency admission needs no storage or filesystem probes");
        options.path = names[31];
        check_test_expect(test, !dependency_check_destinations(&options, sources, 32, NULL, true, false, &arena),
                          "shared dependency destination checks the last source");
        options.path = "dependency-admission-31.o";
        check_test_expect(test, !dependency_check_destinations(&options, sources, 32, NULL, true, false, &arena),
                          "shared dependency destination checks the last implicit object");
        options.path = "dependency-admission-shared.d";
        arena_pop_to(&arena, 0);
        check_test_expect(test, dependency_check_destinations(&options, sources, 32, NULL, true, false, &arena) && arena.length <= 32 * 40,
                          "shared dependency destination reuses bounded object names");
        const uint64_t capacity = arena.capacity;
        arena_pop_to(&arena, 0);
        arena.capacity = 64;
        options.path = NULL;
        check_test_expect(test, !dependency_check_destinations(&options, sources, 32, NULL, true, false, &arena),
                          "identity-array capacity exhaustion refuses admission before reuse");
        options.path = "dependency-admission-shared.d";
        arena_pop_to(&arena, 0);
        arena.capacity = 0;
        check_test_expect(test, !dependency_check_destinations(&options, sources, 32, NULL, true, false, &arena),
                          "object-name capacity exhaustion refuses admission");
        options.path = "-";
        check_test_expect(test, dependency_check_destinations(&options, sources, 32, NULL, true, false, &arena),
                          "stdout admission succeeds with exhausted scratch");
        options.enabled = false;
        options.path = "";
        check_test_expect(test, dependency_check_destinations(&options, NULL, 32, NULL, true, false, &arena),
                          "disabled admission does not read sources");
        options.enabled = true;
        check_test_expect(test, dependency_check_destinations(&options, NULL, 0, NULL, true, false, &arena),
                          "empty admission does not inspect destination");
        arena.capacity = capacity;
        options.path = "dependency-admission-shared.d";
        options.only = true;
        arena_pop_to(&arena, 0);
        check_test_expect(test, dependency_check_destinations(&options, sources, 32, NULL, true, false, &arena) && arena.length == 0,
                          "dependency-only admission needs no object names");
    }
    arena_release(&arena);
}


// A library that a needed library refers to (#398's review): main calls a_func, which libA defines and which calls
// b_func, which libB defines. Where libA's DT_NEEDED doesn't name libB, the executable needs both, in the line's order,
// either way round (GNU ld refuses -lB -lA); where it does (libAB), the executable needs libAB alone, as GNU ld links
// it, and the loader loads libB for it; and where main defines b_func itself, which the executable exports and the
// loader binds libA's reference to, the executable needs libA alone, as GNU ld and lld link it, and a_func calls main's
// b_func, with -lB on the line or without it. clang builds the libraries, and each program runs, finding them through
// LD_LIBRARY_PATH. Then -lA alone for main calling a_func, where nothing defines libA's b_func: refused, naming libA and
// b_func, as GNU ld and lld refuse it (--no-allow-shlib-undefined), where the program would fail to load.
static void check_test_driver_dependencies(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char a_source[] = "int b_func(void);\nint a_func(void) { return b_func() + 1; }\n";
    static const char b_source[] = "int b_func(void) { return 41; }\n";
    static const char program[] = "int a_func(void);\nint main(void) { return a_func() == 42 ? 0 : 1; }\n";
    static const char defines_b[] = "int a_func(void);\nint b_func(void) { return 9; }\nint main(void) { return a_func() == 10 ? 0 : 1; }\n";
    char a_path[256];
    char b_path[256];
    char source[256];
    char defines_source[256];
    char library_a[256];
    char library_b[256];
    char library_ab[256];
    char output[256];
    char search[300];
    snprintf(a_path, sizeof(a_path), "%s/a.c", directory);
    snprintf(b_path, sizeof(b_path), "%s/b.c", directory);
    snprintf(source, sizeof(source), "%s/uses_a.c", directory);
    snprintf(defines_source, sizeof(defines_source), "%s/defines_b.c", directory);
    snprintf(library_a, sizeof(library_a), "%s/libA.so", directory);
    snprintf(library_b, sizeof(library_b), "%s/libB.so", directory);
    snprintf(library_ab, sizeof(library_ab), "%s/libAB.so", directory);
    snprintf(output, sizeof(output), "%s/uses_a", directory);
    snprintf(search, sizeof(search), "LD_LIBRARY_PATH=%s", directory);
    const bool written = parse_test_write(a_path, a_source, sizeof(a_source) - 1) && parse_test_write(b_path, b_source, sizeof(b_source) - 1) &&
                         parse_test_write(source, program, sizeof(program) - 1) && parse_test_write(defines_source, defines_b, sizeof(defines_b) - 1);
    // libB.so, libA.so without a DT_NEEDED for libB, and libAB.so with one (libB.so, which has no soname).
    char* const build_b[] = { (char*)clang, "-shared", "-fPIC", "-o", library_b, b_path, NULL };
    char* const build_a[] = { (char*)clang, "-shared", "-fPIC", "-o", library_a, a_path, NULL };
    char* const build_ab[] = { (char*)clang, "-shared", "-fPIC", "-o", library_ab, a_path, "-L", (char*)directory, "-lB", NULL };
    char* text = NULL;
    bool built = written;
    char* const* const builds[] = { build_b, build_a, build_ab };
    for (uint32_t b = 0; built && b < BUSTER_ARRAY_LENGTH(builds); b += 1)
    {
        built = check_test_driver_spawn(builds[b], &text) == 0;
        free(text);
        text = NULL;
    }
    check_test_expect(test, built, "libraries a needed library refers to: clang builds them");
    struct
    {
        const char* name;
        const char* source;
        const char* first;
        const char* second;
        const char* needed;
    } const cases[] = {
        { "-lA -lB: libB for libA's reference", source, "-lA", "-lB", "libA.so libB.so libc.so.6" },
        { "-lB -lA: libB for libA's reference", source, "-lB", "-lA", "libB.so libA.so libc.so.6" },
        { "-lAB -lB: libB left to libAB's DT_NEEDED", source, "-lAB", "-lB", "libAB.so libc.so.6" },
        { "-lA -lB, main defining b_func: libA alone, calling main's", defines_source, "-lA", "-lB", "libA.so libc.so.6" },
        { "-lA, main defining b_func: libA alone, calling main's", defines_source, "-lA", NULL, "libA.so libc.so.6" },
    };
    for (uint32_t c = 0; built && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char* const link[] = { (char*)executable, "-o", output, (char*)cases[c].source, "-L", (char*)directory, (char*)cases[c].first, (char*)cases[c].second, NULL };
        char* const run[] = { "env", search, output, NULL };
        const int linked = check_test_driver_spawn(link, &text);
        free(text);
        text = NULL;
        char needed[256] = { 0 };
        const bool listed = linked == 0 && check_test_driver_needed(output, needed, sizeof(needed)) && strcmp(needed, cases[c].needed) == 0;
        const int ran = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
        free(text);
        text = NULL;
        if (!listed || ran != 0)
        {
            printf("  %s: link status %d, DT_NEEDED \"%s\", run status %d\n", cases[c].name, linked, needed, ran);
        }
        check_test_expect(test, listed && ran == 0, cases[c].name);
    }
    // -lA alone, nothing defining libA's b_func: refused, as GNU ld words it.
    char* const alone[] = { (char*)executable, "-o", output, source, "-L", (char*)directory, "-lA", NULL };
    const int alone_status = built ? check_test_driver_spawn(alone, &text) : -1;
    char message[320];
    snprintf(message, sizeof(message), "%s: undefined reference to 'b_func'", library_a);
    const bool refused = alone_status == 1 && text != NULL && strstr(text, message) != NULL;
    if (!refused)
    {
        printf("  -lA alone: status %d: %s\n", alone_status, text != NULL ? text : "");
    }
    free(text);
    text = NULL;
    check_test_expect(test, refused, "-lA alone: libA's b_func undefined, refused");
    const char* const files[] = { a_path, b_path, source, defines_source, library_a, library_b, library_ab, output };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
}

// A library whose strong reference to libgcc_s.so.1's _Unwind_Backtrace its own DT_NEEDED doesn't cover (#511's
// review): clang compiles the helper, which GNU ld links with -shared alone (libhelper.so), and again with
// libgcc_s.so.1 (libhelper_needs.so). main calls helper, -lhelper: GNU ld's --as-needed keeps libgcc_s.so.1 for the
// library's reference, and so must the direct path, by both paths, its DT_NEEDED entries GNU ld's (cc -fuse-ld=bfd's)
// and the program running, where lld drops the library and the program fails to load. With -lhelper_needs, the
// library's DT_NEEDED loads libgcc_s.so.1 for it, and the executable needs only it; with main defining
// _Unwind_Backtrace itself, which the executable exports, the reference is main's, and libgcc_s.so.1 isn't needed; and
// with -lplain_needs beside -lhelper, in either order, a library main calls too whose DT_NEEDED names libgcc_s.so.1, the
// loader loads it for that one, and GNU ld writes no entry of the executable's own for it. On a system without
// libgcc_s.so.1 (check_test_driver_has_gcc_s), -lhelper is refused, naming the library and the name.
static void check_test_driver_unwinder_dependency(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char helper_source[] = "struct _Unwind_Context;\n"
                                        "typedef int (*Trace)(struct _Unwind_Context* context, void* argument);\n"
                                        "extern int _Unwind_Backtrace(Trace trace, void* argument);\n"
                                        "static int frame(struct _Unwind_Context* context, void* argument)\n"
                                        "{\n"
                                        "    (void)context;\n"
                                        "    *(int*)argument += 1;\n"
                                        "    return 0;\n"
                                        "}\n"
                                        "int helper(void)\n"
                                        "{\n"
                                        "    int frames = 0;\n"
                                        "    _Unwind_Backtrace(frame, &frames);\n"
                                        "    return frames >= 2 ? 0 : 1;\n"
                                        "}\n";
    static const char program[] = "int helper(void);\nint main(void) { return helper(); }\n";
    static const char plain_source[] = "int plain(void) { return 0; }\n";
    static const char both[] = "int helper(void);\nint plain(void);\nint main(void) { return helper() + plain(); }\n";
    static const char defines[] = "int helper(void);\n"
                                  "int _Unwind_Backtrace(void* trace, void* argument)\n"
                                  "{\n"
                                  "    (void)trace;\n"
                                  "    *(int*)argument = 7;\n"
                                  "    return 0;\n"
                                  "}\n"
                                  "int main(void) { return helper(); }\n";
    const bool unwinder = check_test_driver_has_gcc_s();
    char helper_path[256];
    char helper_object[256];
    char source[256];
    char defines_source[256];
    char library[256];
    char library_needs[256];
    char plain_path[256];
    char plain_object[256];
    char plain_library[256];
    char both_source[256];
    char output[256];
    char reference[256];
    char search[300];
    char gcc_s[4096] = { 0 };
    snprintf(helper_path, sizeof(helper_path), "%s/helper.c", directory);
    snprintf(helper_object, sizeof(helper_object), "%s/helper.o", directory);
    snprintf(source, sizeof(source), "%s/calls_helper.c", directory);
    snprintf(defines_source, sizeof(defines_source), "%s/defines_unwinder.c", directory);
    snprintf(library, sizeof(library), "%s/libhelper.so", directory);
    snprintf(library_needs, sizeof(library_needs), "%s/libhelper_needs.so", directory);
    snprintf(plain_path, sizeof(plain_path), "%s/plain.c", directory);
    snprintf(plain_object, sizeof(plain_object), "%s/plain.o", directory);
    snprintf(plain_library, sizeof(plain_library), "%s/libplain_needs.so", directory);
    snprintf(both_source, sizeof(both_source), "%s/calls_both.c", directory);
    snprintf(output, sizeof(output), "%s/calls_helper", directory);
    snprintf(reference, sizeof(reference), "%s/calls_helper_bfd", directory);
    snprintf(search, sizeof(search), "LD_LIBRARY_PATH=%s", directory);
    FILE* found = unwinder ? popen("cc -print-file-name=libgcc_s.so.1", "r") : NULL;
    const bool located = found != NULL && fgets(gcc_s, sizeof(gcc_s), found) != NULL;
    if (found != NULL)
    {
        pclose(found);
    }
    gcc_s[located ? strcspn(gcc_s, "\n") : 0] = 0;
    const bool written = parse_test_write(helper_path, helper_source, sizeof(helper_source) - 1) && parse_test_write(source, program, sizeof(program) - 1) &&
                         parse_test_write(defines_source, defines, sizeof(defines) - 1) && parse_test_write(plain_path, plain_source, sizeof(plain_source) - 1) &&
                         parse_test_write(both_source, both, sizeof(both) - 1);
    char* const compile[] = { (char*)clang, "-O2", "-fPIC", "-c", "-o", helper_object, helper_path, NULL };
    char* const link_alone[] = { "ld", "-shared", "--eh-frame-hdr", "-soname", "libhelper.so", "-o", library, helper_object, NULL };
    char* const link_needs[] = { "ld", "-shared", "--eh-frame-hdr", "-soname", "libhelper_needs.so", "-o", library_needs, helper_object, gcc_s, NULL };
    char* const compile_plain[] = { (char*)clang, "-O2", "-fPIC", "-c", "-o", plain_object, plain_path, NULL };
    char* const link_plain[] = { "ld", "-shared", "-soname", "libplain_needs.so", "-o", plain_library, plain_object, gcc_s, NULL };
    char* text = NULL;
    bool built = written;
    char* const* const builds[] = { compile, link_alone, link_needs, compile_plain, link_plain };
    for (uint32_t b = 0; built && b < (unwinder ? 5u : 2u); b += 1)
    {
        built = check_test_driver_spawn(builds[b], &text) == 0;
        free(text);
        text = NULL;
    }
    check_test_expect(test, built, "a library that refers to the unwinder: clang and GNU ld build it");
    struct
    {
        const char* name;
        const char* source;
        const char* library;
        const char* second;
        const char* needed;
    } const cases[] = {
        { "-lhelper: libgcc_s.so.1 for libhelper's _Unwind_Backtrace", source, "-lhelper", NULL, "libhelper.so libgcc_s.so.1 libc.so.6" },
        { "-lhelper_needs: libgcc_s.so.1 left to its DT_NEEDED", source, "-lhelper_needs", NULL, "libhelper_needs.so libc.so.6" },
        { "-lhelper, main defining _Unwind_Backtrace: libhelper alone", defines_source, "-lhelper", NULL, "libhelper.so libc.so.6" },
        { "-lhelper -lplain_needs: libgcc_s.so.1 left to libplain_needs' DT_NEEDED", both_source, "-lhelper", "-lplain_needs",
          "libhelper.so libplain_needs.so libc.so.6" },
        { "-lplain_needs -lhelper: libgcc_s.so.1 left to libplain_needs' DT_NEEDED", both_source, "-lplain_needs", "-lhelper",
          "libplain_needs.so libhelper.so libc.so.6" },
    };
    for (uint32_t c = 0; built && unwinder && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char needed[256] = { 0 };
        char reference_needed[256] = { 0 };
        bool listed = true;
        int ran = 0;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char* const vector[] = { (char*)executable, "-o", output, (char*)cases[c].source, "-L", (char*)directory, (char*)cases[c].library, (char*)cases[c].second, NULL };
            char* const scalar[] = {
                (char*)executable, "--scalar", "-o", output, (char*)cases[c].source, "-L", (char*)directory, (char*)cases[c].library, (char*)cases[c].second, NULL,
            };
            char* const run[] = { "env", search, output, NULL };
            const int linked = check_test_driver_spawn(path == 0 ? vector : scalar, &text);
            free(text);
            text = NULL;
            listed = listed && linked == 0 && check_test_driver_needed(output, needed, sizeof(needed)) && strcmp(needed, cases[c].needed) == 0;
            const int status = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
            free(text);
            text = NULL;
            ran = ran != 0 ? ran : status;
        }
        // GNU ld's link of the same program, through GCC's driver.
        char* const bfd[] = { "cc", "-fuse-ld=bfd", "-o", reference, (char*)cases[c].source, "-L", (char*)directory, (char*)cases[c].library, (char*)cases[c].second, NULL };
        const bool reference_linked = check_test_driver_spawn(bfd, &text) == 0;
        free(text);
        text = NULL;
        const bool agrees = reference_linked && check_test_driver_needed(reference, reference_needed, sizeof(reference_needed)) && strcmp(reference_needed, needed) == 0;
        if (!listed || ran != 0 || !agrees)
        {
            printf("  %s: DT_NEEDED \"%s\", GNU ld's \"%s\", run status %d\n", cases[c].name, needed, reference_needed, ran);
        }
        check_test_expect(test, listed && ran == 0 && agrees, cases[c].name);
    }
    if (built && !unwinder)
    {
        char* const link[] = { (char*)executable, "-o", output, source, "-L", (char*)directory, "-lhelper", NULL };
        const int status = check_test_driver_spawn(link, &text);
        char message[320];
        snprintf(message, sizeof(message), "%s: undefined reference to '_Unwind_Backtrace'", library);
        const bool refused = status == 1 && text != NULL && strstr(text, message) != NULL;
        if (!refused)
        {
            printf("  -lhelper without libgcc_s.so.1: status %d: %s\n", status, text != NULL ? text : "");
        }
        free(text);
        text = NULL;
        check_test_expect(test, refused, "-lhelper without libgcc_s.so.1: libhelper's _Unwind_Backtrace undefined, refused");
    }
    const char* const files[] = { helper_path, helper_object, source, defines_source, library, library_needs, plain_path, plain_object, plain_library, both_source, output, reference };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
}

// A library that a linker script names by its path, without a soname (#398's review): its DT_NEEDED entry is that
// path, as GNU ld and lld write it, so that the program loads it with no search path.
static void check_test_driver_script_path(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char c_source[] = "int c_func(void) { return 7; }\n";
    static const char program[] = "int c_func(void);\nint main(void) { return c_func() == 7 ? 0 : 1; }\n";
    char private_directory[256];
    char scripts[256];
    char c_path[256];
    char library_c[256];
    char script[256];
    char source[256];
    char output[256];
    char script_text[320];
    char expected[320];
    snprintf(private_directory, sizeof(private_directory), "%s/private", directory);
    snprintf(scripts, sizeof(scripts), "%s/scripts", directory);
    snprintf(c_path, sizeof(c_path), "%s/c.c", directory);
    snprintf(library_c, sizeof(library_c), "%s/libC.so", private_directory);
    snprintf(script, sizeof(script), "%s/libwrap.so", scripts);
    snprintf(source, sizeof(source), "%s/uses_c.c", directory);
    snprintf(output, sizeof(output), "%s/uses_c", directory);
    snprintf(script_text, sizeof(script_text), "INPUT(%s)\n", library_c);
    snprintf(expected, sizeof(expected), "%s libc.so.6", library_c);
    const bool made = mkdir(private_directory, 0755) == 0 && mkdir(scripts, 0755) == 0 && parse_test_write(c_path, c_source, sizeof(c_source) - 1) &&
                      parse_test_write(script, script_text, strlen(script_text)) && parse_test_write(source, program, sizeof(program) - 1);
    char* const build[] = { (char*)clang, "-shared", "-fPIC", "-o", library_c, c_path, NULL };
    char* const link[] = { (char*)executable, "-o", output, source, "-L", scripts, "-lwrap", NULL };
    char* const run[] = { output, NULL };
    char* text = NULL;
    const int built = made ? check_test_driver_spawn(build, &text) : -1;
    free(text);
    text = NULL;
    const int linked = built == 0 ? check_test_driver_spawn(link, &text) : -1;
    free(text);
    text = NULL;
    char needed[512] = { 0 };
    const bool listed = linked == 0 && check_test_driver_needed(output, needed, sizeof(needed)) && strcmp(needed, expected) == 0;
    const int ran = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    text = NULL;
    if (!listed || ran != 0)
    {
        printf("  a script's library by its path: build status %d, link status %d, DT_NEEDED \"%s\", run status %d\n", built, linked, needed, ran);
    }
    check_test_expect(test, listed && ran == 0, "a script's library without a soname needed by its path");
    const char* const files[] = { c_path, library_c, script, source, output };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
    rmdir(private_directory);
    rmdir(scripts);
}

// The ways check_test_driver_versions changes libA's version need of V1 (#398's review).
typedef enum CheckTestNeed
{
    // vna_other's top bit set (VER_NEED_HIDDEN), which glibc's loader and lld mask from its index.
    CHECK_TEST_NEED_HIDDEN,
    // The entry's vn_aux past its section.
    CHECK_TEST_NEED_AUXILIARY,
    // The auxiliary entry's vna_name past the string table.
    CHECK_TEST_NEED_NAME,
    // The auxiliary entry's vna_other an index that no .gnu.version entry has, so that foo's names nothing.
    CHECK_TEST_NEED_INDEX,
    // The entry's vn_next, 0 as the last entry's, to the section's end, where the loader would read one more.
    CHECK_TEST_NEED_NEXT,
    // VER_FLG_WEAK in the auxiliary entry's vna_flags, a need the loader doesn't refuse a program for.
    CHECK_TEST_NEED_WEAK,
    // Another vna_hash, which the loader compares with the version definition's vd_hash before the names.
    CHECK_TEST_NEED_HASH,
    // The version renamed in place, each digit a 9 (GLIBC_2.2.5 to GLIBC_9.9.9), with its hash, and not weak: a version
    // that the entry's file doesn't define.
    CHECK_TEST_NEED_UNDEFINED,
} CheckTestNeed;

// The ELF hash of `name`, as vna_hash and vd_hash hold it.
static uint32_t check_test_driver_elf_hash(const char* name)
{
    uint32_t hash = 0;
    for (const unsigned char* c = (const unsigned char*)name; *c != 0; c += 1)
    {
        hash = (hash << 4) + *c;
        const uint32_t high = hash & 0xf0000000u;
        hash ^= high >> 24;
        hash &= ~high;
    }
    return hash;
}

// Copies the library at `from` to `to` with the .gnu.version_r auxiliary entry that requires `version` changed as
// `change` says: false if the file doesn't read or has no such entry.
static bool check_test_driver_change_need(const char* from, const char* to, const char* version, CheckTestNeed change)
{
    uint64_t length = 0;
    uint8_t* bytes = NULL;
    FILE* file = fopen(from, "rb");
    if (file != NULL && fseek(file, 0, SEEK_END) == 0)
    {
        const long size = ftell(file);
        bytes = size > (long)sizeof(Elf64_Ehdr) && fseek(file, 0, SEEK_SET) == 0 ? malloc((uint64_t)size) : NULL;
        length = bytes != NULL && fread(bytes, 1, (uint64_t)size, file) == (uint64_t)size ? (uint64_t)size : 0;
    }
    if (file != NULL)
    {
        fclose(file);
    }
    Elf64_Ehdr header = { 0 };
    bool valid = length != 0;
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_shentsize == sizeof(Elf64_Shdr) &&
                header.e_shoff + (uint64_t)header.e_shnum * sizeof(Elf64_Shdr) <= length;
    }
    Elf64_Shdr needs = { 0 };
    for (uint32_t s = 0; valid && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr section;
        memcpy(&section, bytes + header.e_shoff + s * sizeof(Elf64_Shdr), sizeof(section));
        needs = section.sh_type == SHT_GNU_verneed ? section : needs;
    }
    Elf64_Shdr strings = { 0 };
    valid = valid && needs.sh_type == SHT_GNU_verneed && needs.sh_link < header.e_shnum && needs.sh_offset + needs.sh_size <= length;
    if (valid)
    {
        memcpy(&strings, bytes + header.e_shoff + needs.sh_link * sizeof(Elf64_Shdr), sizeof(strings));
        valid = strings.sh_offset + strings.sh_size <= length;
    }
    bool changed = false;
    uint64_t offset = 0;
    bool more = valid;
    while (more)
    {
        Elf64_Verneed need = { 0 };
        more = offset + sizeof(need) <= needs.sh_size;
        if (more)
        {
            memcpy(&need, bytes + needs.sh_offset + offset, sizeof(need));
        }
        uint64_t at = offset + need.vn_aux;
        for (uint32_t a = 0; more && !changed && a < need.vn_cnt && at + sizeof(Elf64_Vernaux) <= needs.sh_size; a += 1)
        {
            Elf64_Vernaux auxiliary;
            memcpy(&auxiliary, bytes + needs.sh_offset + at, sizeof(auxiliary));
            char* name = (char*)bytes + strings.sh_offset + auxiliary.vna_name;
            const bool named = auxiliary.vna_name < strings.sh_size && memchr(name, 0, strings.sh_size - auxiliary.vna_name) != NULL && strcmp(name, version) == 0;
            if (named)
            {
                need.vn_aux = change == CHECK_TEST_NEED_AUXILIARY ? (uint32_t)needs.sh_size : need.vn_aux;
                need.vn_next = change == CHECK_TEST_NEED_NEXT ? (uint32_t)(needs.sh_size - offset) : need.vn_next;
                auxiliary.vna_name = change == CHECK_TEST_NEED_NAME ? (uint32_t)strings.sh_size + 64 : auxiliary.vna_name;
                auxiliary.vna_other = change == CHECK_TEST_NEED_HIDDEN ? (uint16_t)(auxiliary.vna_other | 0x8000)
                                      : change == CHECK_TEST_NEED_INDEX ? 0x7ff0
                                                                        : auxiliary.vna_other;
                for (char* c = name; change == CHECK_TEST_NEED_UNDEFINED && *c != 0; c += 1)
                {
                    *c = *c >= '0' && *c <= '9' ? '9' : *c;
                }
                auxiliary.vna_hash = change == CHECK_TEST_NEED_UNDEFINED ? check_test_driver_elf_hash(name)
                                     : change == CHECK_TEST_NEED_HASH    ? auxiliary.vna_hash ^ 1
                                                                         : auxiliary.vna_hash;
                auxiliary.vna_flags = change == CHECK_TEST_NEED_WEAK        ? (uint16_t)(auxiliary.vna_flags | VER_FLG_WEAK)
                                      : change == CHECK_TEST_NEED_UNDEFINED ? 0
                                                                            : auxiliary.vna_flags;
                memcpy(bytes + needs.sh_offset + offset, &need, sizeof(need));
                memcpy(bytes + needs.sh_offset + at, &auxiliary, sizeof(auxiliary));
                changed = true;
            }
            at += auxiliary.vna_next;
        }
        more = more && !changed && need.vn_next != 0;
        offset += more ? need.vn_next : 0;
    }
    FILE* output = changed ? fopen(to, "wb") : NULL;
    bool written = output != NULL && fwrite(bytes, 1, length, output) == length;
    if (output != NULL)
    {
        written = fclose(output) == 0 && written;
    }
    free(bytes);
    return written;
}

// A needed library's reference that requires a symbol version (#398's reviews): libA, linked against a libB whose foo
// is foo@@V1, requires foo@V1 of libB.so. It links (by both paths into the same bytes) and runs, a_func calling foo@V1,
// against a libB that exports foo@V1 only as a version that isn't its name's default, and one that exports it beside a
// default foo@@V2, as GNU ld and lld link it and the loader binds it. It is refused, naming foo@V1, as both linkers
// refuse it: against a libB whose foo is foo@@V2 alone, also where main defines foo (the loader would then find libB
// without V1); and against one whose foo has no version (which has .gnu.version, for libc's names), which glibc's
// loader would bind it to. Where main defines foo, against the libB of foo@V1, it links and runs, as both linkers link
// it. Then copies of libA with its need changed (check_test_driver_change_need): hidden (vna_other's top bit), which
// links and runs against the libB of foo@V1, as lld links it (GNU ld refuses the bit) and the loader binds it, and is
// refused against foo@@V2 alone and against foo without a version, as lld refuses it; and needs that don't read (an
// entry or a name outside its section) or that foo's index doesn't name, refused as GNU ld and lld refuse them, where
// the loader would take foo as unversioned, and a last entry's vn_next to the section's end, past which the loader would
// read another, which GNU ld refuses too (lld counts the entries by sh_info). Then each version need checked against the
// library its file names, as the loader checks it at startup (#398's review): with a libC on the line before libB, of
// foo@@V1 or of foo@V1 beside foo@@V3, which libA's foo@V1 binds to, it is refused against a libB of foo@@V2 alone,
// which doesn't define the V1 libA requires of libB.so ("libB.so: version 'V1' not found (required by libA.so)", the
// loader's words: GNU ld and lld link it and the program fails to load), and links and runs against a libB that defines
// V1, a libB without versions (the loader only warns), and with the need weak (likewise); a copy of libA whose need of
// V1 has another hash (vna_hash, which the loader compares with vd_hash) is refused against the libB of foo@V1; a copy
// of libA whose need of libc.so.6's GLIBC_2.2.5 is renamed GLIBC_9.9.9 is refused naming libc.so.6, and so is a copy
// of that libB with its own need renamed so, which the loader loads for libA's DT_NEEDED though the link doesn't need
// it. Each case links by both paths, which must write the same bytes or refuse it alike. clang builds the libraries
// with version scripts.
static void check_test_driver_versions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char a_source[] = "int foo(void);\nint a_func(void) { return foo(); }\n";
    static const char program[] = "int a_func(void);\nint main(void) { return a_func() == 11 ? 0 : 1; }\n";
    static const char defines_foo[] = "int a_func(void);\nint foo(void) { return 11; }\nint main(void) { return a_func() == 11 ? 0 : 1; }\n";
    // The libB the libA is built against, four libBs to link with, and two libCs: a name, source, version script and
    // library each (DIRECTORY/NAME/lib<LIBRARY>.so, whose soname is its file's name).
    static const char* const variants[][4] = {
        { "old", "int foo(void) { return 1; }\n", "V1 { global: foo; local: *; };\n", "B" },
        { "v1only", "int foo_v1(void) { return 11; }\n__asm__(\".symver foo_v1,foo@V1\");\n", "V1 { global: foo; local: *; };\n", "B" },
        { "v1v2", "int foo_v1(void) { return 11; }\nint foo_v2(void) { return 22; }\n__asm__(\".symver foo_v1,foo@V1\");\n__asm__(\".symver foo_v2,foo@@V2\");\n",
          "V1 { global: foo; local: *; };\nV2 { global: foo; } V1;\n", "B" },
        { "v2only", "int foo(void) { return 2; }\n", "V2 { global: foo; local: *; };\n", "B" },
        { "plain", "int foo(void) { return 11; }\n", "", "B" },
        { "c", "int foo(void) { return 11; }\n", "V1 { global: foo; local: *; };\n", "C" },
        { "cnd", "int foo_v1(void) { return 11; }\nint foo_v3(void) { return 33; }\n__asm__(\".symver foo_v1,foo@V1\");\n__asm__(\".symver foo_v3,foo@@V3\");\n",
          "V1 { global: foo; local: *; };\nV3 { global: foo; } V1;\n", "C" },
    };
    // The changed copies, each in a directory of its own: of libA, or of a libB variant's library (`of`), the library's
    // letter, and the version whose need each changes.
    static const struct
    {
        const char* name;
        const char* of;
        const char* library;
        CheckTestNeed change;
        const char* version;
    } changes[] = {
        { "hidden", 0, "A", CHECK_TEST_NEED_HIDDEN, "V1" },
        { "auxiliary", 0, "A", CHECK_TEST_NEED_AUXILIARY, "V1" },
        { "name", 0, "A", CHECK_TEST_NEED_NAME, "V1" },
        { "index", 0, "A", CHECK_TEST_NEED_INDEX, "V1" },
        { "next", 0, "A", CHECK_TEST_NEED_NEXT, "V1" },
        { "weak", 0, "A", CHECK_TEST_NEED_WEAK, "V1" },
        { "hash", 0, "A", CHECK_TEST_NEED_HASH, "V1" },
        { "glibc", 0, "A", CHECK_TEST_NEED_UNDEFINED, "GLIBC_2.2.5" },
        { "bglibc", "v1only", "B", CHECK_TEST_NEED_UNDEFINED, "GLIBC_2.2.5" },
    };
    enum
    {
        CHECK_TEST_VERSIONS_LINKED,
        CHECK_TEST_VERSIONS_REFUSED,
        CHECK_TEST_VERSIONS_UNREAD,
        CHECK_TEST_VERSIONS_VERSION,
    };
    // libA's directory under the test's ("" for libA as built), libC's ("" for none, else on the line between them),
    // libB's, whether main defines foo, and what must come of it; for a version refused, the library that doesn't define
    // it (a path under the test's directory, or a soname), the version and the library that requires it (0 for libA).
    static const struct
    {
        const char* name;
        const char* a;
        const char* c;
        const char* b;
        bool defines;
        int outcome;
        const char* lacking;
        const char* missing;
        const char* by;
    } cases[] = {
        { "libA's foo@V1 against a libB of foo@V1 alone: linked and run", "", "", "v1only", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's foo@V1 against a libB of foo@V1 and foo@@V2: linked and run", "", "", "v1v2", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's foo@V1 against a libB of foo@@V2 alone: refused", "", "", "v2only", false, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "libA's foo@V1 against a libB of foo without a version: refused", "", "", "plain", false, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "libA's foo@V1, main defining foo, against a libB of foo@V1: linked and run", "", "", "v1only", true, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's foo@V1, main defining foo, against a libB of foo@@V2 alone: refused", "", "", "v2only", true, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "a hidden foo@V1 against a libB of foo@V1 alone: linked and run", "hidden", "", "v1only", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "a hidden foo@V1 against a libB of foo@@V2 alone: refused", "hidden", "", "v2only", false, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "a hidden foo@V1 against a libB of foo without a version: refused", "hidden", "", "plain", false, CHECK_TEST_VERSIONS_REFUSED, 0, 0, 0 },
        { "libA's need of V1 with vn_aux past its section: refused", "auxiliary", "", "v1only", false, CHECK_TEST_VERSIONS_UNREAD, 0, 0, 0 },
        { "libA's need of V1 with vna_name past the strings: refused", "name", "", "v1only", false, CHECK_TEST_VERSIONS_UNREAD, 0, 0, 0 },
        { "libA's foo at an index that names no need: refused", "index", "", "v1only", false, CHECK_TEST_VERSIONS_UNREAD, 0, 0, 0 },
        { "libA's last need's vn_next to its section's end: refused", "next", "", "v1only", false, CHECK_TEST_VERSIONS_UNREAD, 0, 0, 0 },
        { "libA's foo@V1, libC's foo@@V1, a libB of foo@@V2 alone, which lacks V1: refused", "", "c", "v2only", false, CHECK_TEST_VERSIONS_VERSION,
          "v2only/libB.so", "V1", 0 },
        { "libA's foo@V1, libC's foo@V1 beside foo@@V3, a libB of foo@@V2 alone: refused", "", "cnd", "v2only", false, CHECK_TEST_VERSIONS_VERSION,
          "v2only/libB.so", "V1", 0 },
        { "libA's foo@V1, libC's foo@@V1, a libB of foo@V1 alone: linked and run", "", "c", "v1only", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's foo@V1, libC's foo@V1 beside foo@@V3, a libB of foo@V1 and foo@@V2: linked and run", "", "cnd", "v1v2", false, CHECK_TEST_VERSIONS_LINKED, 0,
          0, 0 },
        { "libA's foo@V1, libC's foo@@V1, a libB without versions: linked and run", "", "c", "plain", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "a weak need of V1, libC's foo@@V1, a libB of foo@@V2 alone: linked and run", "weak", "c", "v2only", false, CHECK_TEST_VERSIONS_LINKED, 0, 0, 0 },
        { "libA's need of GLIBC_2.2.5 renamed GLIBC_9.9.9: refused naming libc.so.6", "glibc", "", "v1only", false, CHECK_TEST_VERSIONS_VERSION, "libc.so.6",
          "GLIBC_9.9.9", 0 },
        { "libA's need of V1 with another hash: refused, as the loader compares it", "hash", "", "v1only", false, CHECK_TEST_VERSIONS_VERSION, "v1only/libB.so", "V1",
          0 },
        { "libB's need of GLIBC_9.9.9, libB loaded for libA's DT_NEEDED: refused naming libc.so.6", "", "", "bglibc", false, CHECK_TEST_VERSIONS_VERSION,
          "libc.so.6", "GLIBC_9.9.9", "bglibc/libB.so" },
    };
    char path[3][320];
    char a_path[256];
    char library_a[256];
    char sources[2][256];
    char outputs[2][256];
    snprintf(a_path, sizeof(a_path), "%s/versioned_a.c", directory);
    snprintf(library_a, sizeof(library_a), "%s/libA.so", directory);
    snprintf(sources[0], sizeof(sources[0]), "%s/versioned.c", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/versioned_defines.c", directory);
    snprintf(outputs[0], sizeof(outputs[0]), "%s/versioned", directory);
    snprintf(outputs[1], sizeof(outputs[1]), "%s/versioned-scalar", directory);
    bool built = parse_test_write(a_path, a_source, sizeof(a_source) - 1) && parse_test_write(sources[0], program, sizeof(program) - 1) &&
                 parse_test_write(sources[1], defines_foo, sizeof(defines_foo) - 1);
    char* text = NULL;
    for (uint32_t v = 0; built && v < BUSTER_ARRAY_LENGTH(variants); v += 1)
    {
        // DIRECTORY/NAME/libB.so, from NAME.c and NAME.map beside it.
        snprintf(path[0], sizeof(path[0]), "%s/%s", directory, variants[v][0]);
        snprintf(path[1], sizeof(path[1]), "%s/%s.c", directory, variants[v][0]);
        snprintf(path[2], sizeof(path[2]), "%s/%s.map", directory, variants[v][0]);
        char library[352];
        char soname[64];
        char script[352];
        snprintf(library, sizeof(library), "%s/lib%s.so", path[0], variants[v][3]);
        snprintf(soname, sizeof(soname), "-Wl,-soname,lib%s.so", variants[v][3]);
        snprintf(script, sizeof(script), "-Wl,--version-script=%s", path[2]);
        const bool scripted = variants[v][2][0] != 0;
        built = mkdir(path[0], 0755) == 0 && parse_test_write(path[1], variants[v][1], strlen(variants[v][1])) &&
                (!scripted || parse_test_write(path[2], variants[v][2], strlen(variants[v][2])));
        char* const build[] = { (char*)clang, "-shared", "-fPIC", "-o", library, path[1], soname, scripted ? script : NULL, NULL };
        built = built && check_test_driver_spawn(build, &text) == 0;
        free(text);
        text = NULL;
        // libA against the first: its foo is then foo@V1 of libB.so.
        char old[352];
        snprintf(old, sizeof(old), "%s/old", directory);
        char* const build_a[] = { (char*)clang, "-shared", "-fPIC", "-o", library_a, a_path, "-L", old, "-lB", "-Wl,-soname,libA.so", NULL };
        built = built && (v != 0 || check_test_driver_spawn(build_a, &text) == 0);
        free(text);
        text = NULL;
    }
    for (uint32_t c = 0; built && c < BUSTER_ARRAY_LENGTH(changes); c += 1)
    {
        char copy[352];
        char original[352];
        snprintf(path[0], sizeof(path[0]), "%s/%s", directory, changes[c].name);
        snprintf(copy, sizeof(copy), "%s/lib%s.so", path[0], changes[c].library);
        snprintf(original, sizeof(original), "%s/%s/lib%s.so", directory, changes[c].of != 0 ? changes[c].of : "", changes[c].library);
        built = mkdir(path[0], 0755) == 0 && check_test_driver_change_need(changes[c].of != 0 ? original : library_a, copy, changes[c].version, changes[c].change);
    }
    check_test_expect(test, built, "versioned libraries: clang builds them, and the copies of libA");
    for (uint32_t c = 0; built && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        char a_directory[320];
        char b_directory[320];
        char c_directory[320];
        char a_library[352];
        char search[1000];
        snprintf(a_directory, sizeof(a_directory), "%s%s%s", directory, cases[c].a[0] != 0 ? "/" : "", cases[c].a);
        snprintf(b_directory, sizeof(b_directory), "%s/%s", directory, cases[c].b);
        snprintf(c_directory, sizeof(c_directory), "%s/%s", directory, cases[c].c);
        snprintf(a_library, sizeof(a_library), "%s/libA.so", a_directory);
        const bool third = cases[c].c[0] != 0;
        snprintf(search, sizeof(search), "LD_LIBRARY_PATH=%s:%s%s%s", a_directory, b_directory, third ? ":" : "", third ? c_directory : "");
        const bool linking = cases[c].outcome == CHECK_TEST_VERSIONS_LINKED;
        int linked[2] = { -1, -1 };
        char messages[2][700] = { { 0 } };
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            // [--scalar] -o OUTPUT SOURCE -L A [-L C] -L B -lA [-lC] -lB.
            char* link[16];
            uint32_t count = 0;
            link[count++] = (char*)executable;
            if (scalar != 0)
            {
                link[count++] = "--scalar";
            }
            link[count++] = "-o";
            link[count++] = outputs[scalar];
            link[count++] = sources[cases[c].defines ? 1 : 0];
            link[count++] = "-L";
            link[count++] = a_directory;
            if (third)
            {
                link[count++] = "-L";
                link[count++] = c_directory;
            }
            link[count++] = "-L";
            link[count++] = b_directory;
            link[count++] = "-lA";
            if (third)
            {
                link[count++] = "-lC";
            }
            link[count++] = "-lB";
            link[count] = NULL;
            linked[scalar] = check_test_driver_spawn(link, &text);
            snprintf(messages[scalar], sizeof(messages[scalar]), "%s", text != NULL ? text : "");
            free(text);
            text = NULL;
        }
        char* const run[] = { "env", search, outputs[0], NULL };
        const int ran = linking && linked[0] == 0 ? check_test_driver_spawn(run, &text) : -1;
        free(text);
        text = NULL;
        char expected[1000];
        if (cases[c].outcome == CHECK_TEST_VERSIONS_VERSION)
        {
            const bool under = strchr(cases[c].lacking, '/') != NULL;
            char requiring[352];
            snprintf(requiring, sizeof(requiring), "%s%s%s", cases[c].by != 0 ? directory : a_library, cases[c].by != 0 ? "/" : "", cases[c].by != 0 ? cases[c].by : "");
            snprintf(expected, sizeof(expected), "%s%s%s: version '%s' not found (required by %s)", under ? directory : "", under ? "/" : "", cases[c].lacking,
                     cases[c].missing, requiring);
        }
        else
        {
            snprintf(expected, sizeof(expected), cases[c].outcome == CHECK_TEST_VERSIONS_REFUSED ? "%s: undefined reference to 'foo@V1'" : "%s isn't a shared library",
                     a_library);
        }
        const bool right = linking ? linked[0] == 0 && linked[1] == 0 && ran == 0 && check_test_driver_same_files(outputs[0], outputs[1])
                                   : linked[0] == 1 && linked[1] == 1 && strstr(messages[0], expected) != NULL && strstr(messages[1], expected) != NULL;
        if (!right)
        {
            printf("  %s: link status %d and %d, run status %d: %s%s\n", cases[c].name, linked[0], linked[1], ran, messages[0], messages[1]);
        }
        check_test_expect(test, right, cases[c].name);
    }
    for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(variants); v += 1)
    {
        char file[352];
        snprintf(file, sizeof(file), "%s/%s/lib%s.so", directory, variants[v][0], variants[v][3]);
        unlink(file);
        snprintf(file, sizeof(file), "%s/%s.c", directory, variants[v][0]);
        unlink(file);
        snprintf(file, sizeof(file), "%s/%s.map", directory, variants[v][0]);
        unlink(file);
        snprintf(file, sizeof(file), "%s/%s", directory, variants[v][0]);
        rmdir(file);
    }
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(changes); c += 1)
    {
        char file[352];
        snprintf(file, sizeof(file), "%s/%s/lib%s.so", directory, changes[c].name, changes[c].library);
        unlink(file);
        snprintf(file, sizeof(file), "%s/%s", directory, changes[c].name);
        rmdir(file);
    }
    const char* const files[] = { a_path, library_a, sources[0], sources[1], outputs[0], outputs[1] };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
}

// Compiler identification is an action, while an option operand that spells an action is still an operand.
static void check_test_driver_identification(CheckTest* test, const char* executable, const char* directory)
{
    char compiler[PATH_MAX];
    char source[256];
    char object[256];
    snprintf(source, sizeof(source), "%s/identity.c", directory);
    snprintf(object, sizeof(object), "%s/--version", directory);
    static const char program[] = "int value = 42;\n";
    const bool made = realpath(executable, compiler) != NULL && parse_test_write(source, program, sizeof(program) - 1);
    char* const version[] = { compiler, "-O2", "--version", source, NULL };
    char* const machine[] = { compiler, "-dumpmachine", NULL };
    char* const target[] = { compiler, "-target", "x86_64-acme-linux-gnu", "-dumpmachine", NULL };
    char* const target_last[] = { compiler, "--target=x86_64-first-linux-gnu", "-target", "x86_64-linux-gnu", "-dumpmachine", NULL };
    char* const both[] = { compiler, "--version", "-dumpmachine", NULL };
    char* const invalid[] = { compiler, "-target", "aarch64-linux-gnu", "-dumpmachine", NULL };
    char* const missing[] = { compiler, "-dumpmachine", "-target", NULL };
    char* const* cases[] = { version, machine, target, target_last, both, invalid, missing };
    const char* const expected[] = { "buster (development build)\nTarget: ", "x86_64-", "x86_64-acme-linux-gnu\n",
                                     "x86_64-unknown-linux-gnu\n", "x86_64-", "", "" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char* text = NULL;
        const int status = made ? check_test_driver_spawn_stream(cases[i], &text, true) : -1;
        const bool prefix = text != NULL && strncmp(text, expected[i], strlen(expected[i])) == 0;
        check_test_expect(test, i < 5 ? status == 0 && prefix : status != 0 && text != NULL && text[0] == 0,
                          "compiler identification selects the correct target and refuses unsupported targets");
        free(text);
    }
    char* const compile[] = { "env", "-C", (char*)directory, compiler, "-pipe", "-c", "-o", "--version", "identity.c", NULL };
    char* text = NULL;
    const int compiled = made ? check_test_driver_spawn(compile, &text) : -1;
    free(text);
    FILE* output = fopen(object, "rb");
    unsigned char magic[4] = { 0 };
    const bool elf = output != NULL && fread(magic, 1, sizeof(magic), output) == sizeof(magic) && memcmp(magic, "\177ELF", 4) == 0;
    if (output != NULL)
    {
        fclose(output);
    }
    check_test_expect(test, compiled == 0 && elf, "-pipe compiles and an output named --version remains an operand");
    char* const preprocess[] = { compiler, "-pipe", "-E", "-P", source, NULL };
    text = NULL;
    const int preprocessed = made ? check_test_driver_spawn_stream(preprocess, &text, true) : -1;
    check_test_expect(test, preprocessed == 0 && text != NULL && strstr(text, "int value = 42;") != NULL,
                      "-pipe is accepted in preprocessing mode");
    free(text);
    char* const command[] = { compiler, "parallel", "--version", NULL };
    text = NULL;
    const int command_status = made ? check_test_driver_spawn(command, &text) : -1;
    check_test_expect(test, command_status != 0 && text != NULL && strstr(text, "usage: parallel") != NULL,
                      "named commands take precedence over compiler identification");
    free(text);
    char* const assembly_version[] = { compiler, "-c", "--version", "missing.s", NULL };
    char* const assembly_machine[] = { compiler, "-c", "-dumpmachine", "missing.s", NULL };
    char* const* assembly_queries[] = { assembly_version, assembly_machine };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(assembly_queries); i += 1)
    {
        text = NULL;
        const int status = made ? check_test_driver_spawn_stream(assembly_queries[i], &text, true) : -1;
        check_test_expect(test, status == 0 && text != NULL && strstr(text, i == 0 ? "buster (development build)" : "x86_64-") != NULL,
                          "information actions take precedence over implicit assembly compilation");
        free(text);
    }
    char missing_assembly[256];
    snprintf(missing_assembly, sizeof(missing_assembly), "%s/missing.s", directory);
    char* const assembly_operand[] = { compiler, "-c", "-o", "--version", missing_assembly, NULL };
    text = NULL;
    const int operand_status = made ? check_test_driver_spawn_stream(assembly_operand, &text, true) : -1;
    check_test_expect(test, operand_status != 0 && text != NULL && text[0] == 0,
                      "an assembly output named --version does not select compiler identification");
    free(text);
    unlink(object);
    unlink(source);
}

// Layout flags not implemented by the checker must not silently produce objects with another ABI.
static void check_test_driver_layout_options(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "enum E { A, B }; struct S { char c; int i; }; int value;\n";
    static const char sentinel[] = "existing output";
    char source[256];
    char object[256];
    snprintf(source, sizeof(source), "%s/layout.c", directory);
    snprintf(object, sizeof(object), "%s/layout.o", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    static const struct
    {
        const char* first;
        const char* second;
        bool reject;
        const char* diagnostic;
    } cases[] = {
        { "-fshort-enums", NULL, true, "-fshort-enums" },
        { "-fpack-struct", NULL, true, "-fpack-struct" },
        { "-fpack-struct=2", NULL, true, "-fpack-struct=2" },
        { "-fpack-struct=abc", "-fno-pack-struct", true, "-fpack-struct=abc" },
        { "-fpack-struct=08", "-fno-pack-struct", true, "-fpack-struct=08" },
        { "-fpack-struct=4294967296", "-fno-pack-struct", true, "-fpack-struct=4294967296" },
        { "-fshort-enums", "-fno-short-enums", false, NULL },
        { "-fno-short-enums", "-fshort-enums", true, "-fshort-enums" },
        { "-fpack-struct=2", "-fno-pack-struct", true, "-fpack-struct=2" },
        { "-fpack-struct", "-fno-pack-struct", false, NULL },
        { "-fpack-struct", "-fpack-struct=0", false, NULL },
        { "-fpack-struct=0", "-fpack-struct", false, NULL },
        { "-fpack-struct=bad", "-fpack-struct=0", false, NULL },
        { "-fpack-struct=2", "-fpack-struct=0", false, NULL },
        { "-fpack-struct", "-fpack-struct=0x0", false, NULL },
        { "-fpack-struct=0", "-fpack-struct=2", true, "-fpack-struct=2" },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char* arguments[] = { (char*)executable, "-c", "-o", object, source, (char*)cases[i].first, (char*)cases[i].second, NULL };
        char* text = NULL;
        const bool prior = parse_test_write(object, sentinel, sizeof(sentinel));
        const int status = written && prior ? check_test_driver_spawn(arguments, &text) : -1;
        if (cases[i].reject)
        {
            FILE* file = fopen(object, "rb");
            char bytes[sizeof(sentinel) + 1] = { 0 };
            const size_t length = file != NULL ? fread(bytes, 1, sizeof(bytes), file) : 0;
            if (file != NULL)
            {
                fclose(file);
            }
            check_test_expect(test, status == 1 && text != NULL && strstr(text, cases[i].diagnostic) != NULL &&
                                    length == sizeof(sentinel) && memcmp(bytes, sentinel, sizeof(sentinel)) == 0,
                              "unsupported ABI layout option is named and preserves the output");
        }
        else
        {
            FILE* file = fopen(object, "rb");
            unsigned char magic[4] = { 0 };
            const bool elf = file != NULL && fread(magic, 1, sizeof(magic), file) == sizeof(magic) && memcmp(magic, "\177ELF", 4) == 0;
            if (file != NULL)
            {
                fclose(file);
            }
            check_test_expect(test, status == 0 && elf, "a resolved default ABI layout permits ordinary object output");
        }
        free(text);
    }
    char* const syntax[] = { (char*)executable, "check", "-fsyntax-only", "-fshort-enums", source, NULL };
    char* text = NULL;
    const int syntax_status = check_test_driver_spawn(syntax, &text);
    check_test_expect(test, syntax_status == 1 && text != NULL && strstr(text, "-fshort-enums") != NULL,
                      "the check command refuses unsupported layout options");
    free(text);
    char* const preprocess[] = { (char*)executable, "-E", "-fshort-enums", "-fpack-struct=2", source, NULL };
    text = NULL;
    const int preprocess_status = check_test_driver_spawn(preprocess, &text);
    check_test_expect(test, preprocess_status == 0, "preprocessing accepts layout flags that do not affect its output");
    free(text);
    static const char* const value_options[] = { "--lanes", "--lane-cpus", "--worker-budget", "--id-order", "-l", "-L" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(value_options); i += 1)
    {
        char* const valued[] = { (char*)executable, (char*)value_options[i], "-fshort-enums", "-fsyntax-only", source, NULL };
        text = NULL;
        const int valued_status = check_test_driver_spawn(valued, &text);
        check_test_expect(test, text != NULL && strstr(text, "the checker does not implement its ABI layout") == NULL &&
                                (i >= 4 || valued_status != 0),
                          "a driver option's operand is not interpreted as a layout option");
        free(text);
    }
    char compiler[PATH_MAX];
    char output_value[256];
    snprintf(output_value, sizeof(output_value), "%s/-fshort-enums", directory);
    const bool absolute = realpath(executable, compiler) != NULL;
    char* const named[] = { "env", "-C", (char*)directory, compiler, "-c", "-o", "-fshort-enums", "layout.c", NULL };
    text = NULL;
    const int named_status = absolute ? check_test_driver_spawn(named, &text) : -1;
    FILE* output_file = fopen(output_value, "rb");
    unsigned char magic[4] = { 0 };
    const bool elf = output_file != NULL && fread(magic, 1, sizeof(magic), output_file) == sizeof(magic) && memcmp(magic, "\177ELF", 4) == 0;
    if (output_file != NULL)
    {
        fclose(output_file);
    }
    check_test_expect(test, named_status == 0 && elf, "an output operand spelled as a layout option remains a filename");
    free(text);
    unlink(output_value);
    unlink(object);
    unlink(source);
}

// Diagnostic examples include missing indices: the writer does not attach an atom to every failure.
static void check_test_driver_link_failure_messages(CheckTest* test)
{
    const uint8_t* names[] = { NULL, (const uint8_t*)"large" };
    uint32_t lengths[] = { 0, 5 };
    NameId symbol_names[] = { 0, 1 };
    AtomId symbol_atoms[] = { 0, 1 };
    uint64_t sizes[] = { 0, (3ull << 30), (3ull << 30) };
    uint8_t classes[] = { 0, BUSTER_LINK_CLASS_BSS, BUSTER_LINK_CLASS_BSS };
    uint8_t alignments[] = { 0, 0, 0 };
    Link link = {
        .names = { .bytes = names, .length = lengths, .count = 2 },
        .atoms = { .class = classes, .alignment_log2 = alignments, .size = sizes, .count = 3 },
        .symbols = { .name = symbol_names, .atom = symbol_atoms, .count = 2 },
    };
    Runtime runtime = { 0 };
    static const struct
    {
        ExecutableResult result;
        const char* expected;
    } cases[] = {
        { { .error = EXECUTABLE_ERROR_LAYOUT, .atom = 1, .cause = EXECUTABLE_CAUSE_ATOM_TOO_LARGE },
          "atom 1 ('large') is 3221225472 bytes; the maximum atom size is 2147483648 bytes" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .atom = 2, .cause = EXECUTABLE_CAUSE_ATOM_TOO_LARGE },
          "atom 2 is 3221225472 bytes; the maximum atom size is 2147483648 bytes" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .cause = EXECUTABLE_CAUSE_IMAGE_TOO_LARGE, .value = 0x90000000, .limit = 0x80000000 },
          "the image would end at address 0x90000000; the maximum is 0x80000000" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .cause = EXECUTABLE_CAUSE_TLS_TOO_LARGE, .value = 0x90000000, .limit = 0x80000000 },
          "the TLS block is 2415919104 bytes; the maximum is 2147483648 bytes" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .cause = EXECUTABLE_CAUSE_SECTION_NAMES_TOO_LARGE, .value = 700, .limit = 640 },
          ".shstrtab is 700 bytes; the maximum is 640 bytes" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .atom = 1, .cause = EXECUTABLE_CAUSE_ATOM_ALIGNMENT, .value = 8192, .limit = 4096 },
          "atom 1 ('large') has 8192-byte alignment; the maximum atom alignment is 4096 bytes" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .atom = 1, .cause = EXECUTABLE_CAUSE_ATOM_ARRAY_ENTRY },
          "atom 1 ('large') of an init or fini array has 3221225472 bytes; each atom must be whole 8-byte entries aligned to at most 8 bytes" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .atom = 1, .cause = EXECUTABLE_CAUSE_ATOM_CLASS, .value = BUSTER_LINK_CLASS_INIT },
          "atom 1 ('large') is a .init atom, which an executable without the C runtime has no section for" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .atom = 1, .cause = EXECUTABLE_CAUSE_ATOM_ORDER, .value = 0x408010, .limit = 16 },
          "atom 1 ('large') is at image offset 0x408010, not aligned to its ordered section's 16-byte alignment" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .atom = 1, .symbol = 1, .fixup = 7, .cause = EXECUTABLE_CAUSE_ATOM_FAR, .value = 0x80000010, .limit = 0x7fffffff },
          "atom 1 ('large') is at image offset 0x80000010, which fixup 7 to 'large' can't reach" },
        { { .error = EXECUTABLE_ERROR_LAYOUT, .atom = 1, .symbol = 1, .fixup = 7, .cause = EXECUTABLE_CAUSE_FAR_GOT, .value = 0x80000010 },
          "atom 1 ('large') is far, and fixup 7 to 'large' has no GOT slot" },
        { { .error = EXECUTABLE_ERROR_LAYOUT }, "image, TLS or section-name layout exceeds executable limits" },
        { { .error = EXECUTABLE_ERROR_MEMORY, .cause = EXECUTABLE_CAUSE_MEMORY_SHARED }, "linker scratch memory exhausted while allocating the linker's shared state" },
        { { .error = EXECUTABLE_ERROR_MEMORY, .phase = EXECUTABLE_PHASE_LAYOUT }, "linker scratch memory exhausted while laying out the image" },
        { { .error = EXECUTABLE_ERROR_ENTRY, .symbol = 1, .cause = EXECUTABLE_CAUSE_ENTRY_UNDEFINED }, "entry point 'large' is not defined" },
        { { .error = EXECUTABLE_ERROR_ENTRY, .symbol = 1, .cause = EXECUTABLE_CAUSE_ENTRY_NOT_TEXT }, "entry point 'large' is defined in atom 1, which is not a code atom" },
        { { .error = EXECUTABLE_ERROR_UNWIND, .atom = 1, .cause = EXECUTABLE_CAUSE_UNWIND_ATOM_MALFORMED },
          "unwind atom 1 ('large') (size 3221225472 bytes) is not a sequence of CIE and FDE records .eh_frame_hdr can index" },
        { { .error = EXECUTABLE_ERROR_UNWIND, .atom = 1, .cause = EXECUTABLE_CAUSE_UNWIND_ATOM_TOO_LARGE },
          "unwind atom 1 ('large') is 3221225472 bytes; the maximum atom size is 2147483648 bytes" },
        { { .error = EXECUTABLE_ERROR_UNWIND, .atom = 1, .cause = EXECUTABLE_CAUSE_UNWIND_RANGE }, "farther than the table's 2147483647-byte range" },
        { { .error = EXECUTABLE_ERROR_UNWIND, .atom = 99, .phase = EXECUTABLE_PHASE_DYNAMIC }, "the unwind table can't be indexed while building the dynamic sections and unwind tables" },
        { { .error = EXECUTABLE_ERROR_UNSUPPORTED, .symbol = 1, .cause = EXECUTABLE_CAUSE_UNSUPPORTED_DEBUG_SYMBOL },
          "debug information refers to unsupported symbol 'large'" },
        { { .error = EXECUTABLE_ERROR_UNSUPPORTED, .symbol = 1, .cause = EXECUTABLE_CAUSE_UNSUPPORTED_TLS_CALL },
          "large is called outside a general- or local-dynamic TLS access" },
        { { .error = EXECUTABLE_ERROR_UNSUPPORTED, .symbol = 1, .cause = EXECUTABLE_CAUSE_UNSUPPORTED_VERSION, .value = 34 },
          "symbol 'large' has unavailable version 34" },
        { { .error = EXECUTABLE_ERROR_UNSUPPORTED, .symbol = 1 }, "unsupported symbol 'large'" },
        { { .error = EXECUTABLE_ERROR_UNSUPPORTED, .symbol = 99, .cause = EXECUTABLE_CAUSE_UNSUPPORTED_DEBUG }, "the debug information is not valid for this executable" },
        { { .error = EXECUTABLE_ERROR_UNSUPPORTED, .cause = EXECUTABLE_CAUSE_UNSUPPORTED_TLS_SECTION, .value = BUSTER_LINK_CLASS_TDATA },
          "a thread-local symbol needs an empty .tdata section, which the link has none of" },
        { { .error = EXECUTABLE_ERROR_UNSUPPORTED, .cause = EXECUTABLE_CAUSE_UNSUPPORTED_STRING_TABLE, .value = 0x100000000, .limit = UINT32_MAX },
          "the symbol names are 4294967296 bytes; the maximum is 4294967295 bytes" },
        { { .error = EXECUTABLE_ERROR_UNSUPPORTED, .symbol = 99, .phase = EXECUTABLE_PHASE_COUNT }, "unsupported executable configuration while counting" },
        { { .error = EXECUTABLE_ERROR_UNDEFINED, .symbol = 1 }, "undefined reference to 'large'" },
        { { .error = EXECUTABLE_ERROR_FILE, .call = "open", .system_error = EACCES }, "can't write out: open:" },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        FILE* output = tmpfile();
        char* text = NULL;
        size_t length = 0;
        if (output != NULL)
        {
            check_driver_executable_failure(&link, &runtime, 1, "out", &cases[i].result, output);
        }
        const bool read = output != NULL && pp_test_read_output(output, &text, &length);
        check_test_expect(test, read && strstr(text, cases[i].expected) != NULL, "link failure describes its cause and available input");
        free(text);
        if (output != NULL)
        {
            fclose(output);
        }
    }
}

// A large BSS atom is accepted without writing gigabytes; the program maps the zero-fill at run time. Both driver paths
// must write it.
static void check_test_driver_link_layout_error(CheckTest* test, const char* executable, const char* directory)
{
    static const char source_text[] = "char gbig[3L << 30]; int main(void) { gbig[(3L << 30) - 1] = 42; return gbig[(3L << 30) - 1] != 42; }\n";
    char source[256];
    char output[256];
    snprintf(source, sizeof(source), "%s/large-bss.c", directory);
    snprintf(output, sizeof(output), "%s/large-bss", directory);
    const bool made = parse_test_write(source, source_text, sizeof(source_text) - 1);
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        char* const vector[] = { (char*)executable, "-o", output, source, NULL };
        char* const scalar_args[] = { (char*)executable, "--scalar", "-o", output, source, NULL };
        char* text = NULL;
        unlink(output);
        const int status = made ? check_test_driver_spawn(scalar ? scalar_args : vector, &text) : -1;
        char* run_text = NULL;
        char* const run[] = { output, NULL };
        const int run_status = status == 0 && access(output, X_OK) == 0 ? check_test_driver_spawn(run, &run_text) : -1;
        check_test_expect(test, status == 0 && run_status == 0, "a 3 GiB .bss atom links and runs by both driver paths");
        free(text);
        free(run_text);
    }
    unlink(output);
    unlink(source);
    // Two .bss atoms whose first is too large: the second is past the references' reach, and a fixup to it is refused
    // with the atom and the limit.
    static const char far_text[] = "char a[3L << 30]; char b[3L << 30]; int main(void) { return b[0]; }\n";
    snprintf(source, sizeof(source), "%s/far-bss.c", directory);
    snprintf(output, sizeof(output), "%s/far-bss", directory);
    const bool far_made = parse_test_write(source, far_text, sizeof(far_text) - 1);
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        char* const vector[] = { (char*)executable, "-o", output, source, NULL };
        char* const scalar_args[] = { (char*)executable, "--scalar", "-o", output, source, NULL };
        char* text = NULL;
        unlink(output);
        const int status = far_made ? check_test_driver_spawn(scalar ? scalar_args : vector, &text) : -1;
        check_test_expect(test, status != 0 && text != NULL && strstr(text, "fixup to 'b'") != NULL && strstr(text, "does not fit its field") != NULL &&
                                  access(output, F_OK) != 0,
                          "a fixup to a .bss atom past the references' reach is refused with its target and reason");
        free(text);
    }
    unlink(output);
    unlink(source);
}

// -E is an action at any argument position, but an option's value can spell -E without selecting that action.
static void check_test_driver_preprocess_action(CheckTest* test, const char* executable, const char* directory)
{
    static const char program[] = "#define VALUE 42\nint value = VALUE;\n";
    char compiler[PATH_MAX];
    char source[256];
    char output[256];
    char operand[256];
    snprintf(source, sizeof(source), "%s/action.c", directory);
    snprintf(output, sizeof(output), "%s/action.i", directory);
    snprintf(operand, sizeof(operand), "%s/-E", directory);
    const bool made = realpath(executable, compiler) != NULL && parse_test_write(source, program, sizeof(program) - 1);
    char* const first[] = { compiler, "-E", "-P", "-o", output, source, NULL };
    char* const later[] = { compiler, "-O2", "-I", (char*)directory, "-E", "-P", "-o", output, source, NULL };
    char* const after[] = { compiler, "-c", "-P", source, "-E", "-o", output, NULL };
    char* const macros[] = { compiler, "-O2", "-E", "-dM", "-o", output, source, NULL };
    char* const* cases[] = { first, later, after, macros };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        unlink(output);
        char* text = NULL;
        const int status = made ? check_test_driver_spawn(cases[i], &text) : -1;
        free(text);
        char captured[65536] = { 0 };
        FILE* output_file = status == 0 ? fopen(output, "rb") : NULL;
        const size_t length = output_file != NULL ? fread(captured, 1, sizeof(captured) - 1, output_file) : 0;
        const bool read = output_file != NULL && !ferror(output_file) && feof(output_file);
        captured[length] = 0;
        if (output_file != NULL)
        {
            fclose(output_file);
        }
        check_test_expect(test, read && strstr(captured, i == 3 ? "#define VALUE 42" : "int value = 42;") != NULL,
                          "-E selects preprocessing before or after other options and input, including -c and -dM");
    }
    // A separate argument that spells -E is an output name, not the preprocessing action.
    char* const object[] = { "env", "-C", (char*)directory, compiler, "-c", "-o", "-E", "action.c", NULL };
    char* text = NULL;
    const int status = made ? check_test_driver_spawn(object, &text) : -1;
    free(text);
    FILE* file = fopen(operand, "rb");
    unsigned char magic[4] = { 0 };
    const bool elf = file != NULL && fread(magic, 1, sizeof(magic), file) == sizeof(magic) && memcmp(magic, "\177ELF", 4) == 0;
    if (file != NULL)
    {
        fclose(file);
    }
    check_test_expect(test, status == 0 && elf, "an option value spelled -E does not select preprocessing");
    char* const parallel[] = { compiler, "parallel", "-E", NULL };
    text = NULL;
    const int parallel_status = check_test_driver_spawn(parallel, &text);
    check_test_expect(test, parallel_status != 0 && text != NULL && strstr(text, "usage: parallel") != NULL,
                      "an explicit driver command takes precedence over -E");
    free(text);
    // Force C for this extensionless name: automatic classification treats unknown suffixes as linker inputs.
    char* const end_options[] = { compiler, "-x", "c", "--", "-E", source, NULL };
    text = NULL;
    const int end_status = check_test_driver_spawn(end_options, &text);
    check_test_expect(test, end_status != 0 && text != NULL && strstr(text, "can't read '-E'") != NULL &&
                              strstr(text, "unknown argument: --") == NULL,
                      "an input after -- is not an option; -E is read as a file");
    free(text);
    // Both entry points must apply command-line policy to #if's ungrouped conversion warning.
    static const char conversion[] = "#if -1 > 0u\nint a;\n#endif\n";
    const bool conversion_made = parse_test_write(source, conversion, sizeof(conversion) - 1);
    for (uint32_t action = 0; action < 2; action += 1)
    {
        for (uint32_t policy = 0; policy < 4; policy += 1)
        {
            char* arguments[] = { compiler, action == 0 ? "-E" : "-fsyntax-only", policy == 0 ? "-Wall" : "-Werror",
                                  policy == 2 ? "-w" : policy == 3 ? "-Wno-error" : "-Wall", source, NULL };
            text = NULL;
            const int result = conversion_made ? check_test_driver_spawn(arguments, &text) : -1;
            const bool warning = text != NULL && strstr(text, "warning: left side of operator converted") != NULL;
            const bool error = text != NULL && strstr(text, "error: left side of operator converted") != NULL;
            check_test_expect(test, result == (policy == 1 ? 1 : 0) && warning == (policy == 0 || policy == 3) && error == (policy == 1),
                              "#if conversion warning follows command-line policy through -E and -fsyntax-only");
            free(text);
        }
    }
    unlink(operand);
    unlink(output);
    unlink(source);
}

#include "c_check_pic_driver_test.c"
#include "c_check_linker_driver_test.c"
#include "c_check_instrumentation_driver_test.c"

// Code generation flags must not silently produce an object without the requested hardening or instrumentation.
static void check_test_driver_unsupported_codegen(CheckTest* test, const char* executable, const char* directory)
{
    check_test_driver_pic(test, executable, directory);
    check_test_driver_instrumentation(test, executable, directory);
    static const char source_text[] = "int main(void) { return 0; }\n";
    static const char sentinel[] = "unchanged output\n";
    static const char* const options[] = {
        "-fstack-protector", "-fstack-protector-strong", "-fstack-protector-all", "-fstack-protector-explicit",
        "-fcf-protection", "-fcf-protection=full", "-fcf-protection=branch", "-fcf-protection=return",
        "-fstack-check", "-fstack-check=specific", "-pg", "-finstrument-functions",
        "-finstrument-functions-after-inlining", "-fprofile-generate", "-fprofile-generate=path", "-fprofile-instr-generate",
        "-fprofile-instr-generate=path", "-fcs-profile-generate", "-fcs-profile-generate=path",
        "-fprofile-generate-cold-function-coverage", "-fprofile-generate-cold-function-coverage=path", "--coverage", "-fprofile-arcs", "-ftest-coverage", "-ftls-model=global-dynamic", "-ftls-model=local-exec",
        "-mno-red-zone", "-fno-plt", "-fpcc-struct-return", "-freg-struct-return", "-fshort-double",
        "-fwide-exec-charset=UTF-32",
    };
    char source[256];
    char output[256];
    char compiler[PATH_MAX];
    char named[256];
    snprintf(source, sizeof(source), "%s/codegen-flags.c", directory);
    snprintf(output, sizeof(output), "%s/codegen-flags.o", directory);
    snprintf(named, sizeof(named), "%s/-pg", directory);
    const bool made = realpath(executable, compiler) != NULL && parse_test_write(source, source_text, sizeof(source_text) - 1);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(options); i += 1)
    {
        const bool saved = made && parse_test_write(output, sentinel, sizeof(sentinel) - 1);
        char* const arguments[] = { compiler, "-c", (char*)options[i], "-o", output, source, NULL };
        char* text = NULL;
        const int status = saved ? check_test_driver_spawn(arguments, &text) : -1;
        char contents[sizeof(sentinel)] = { 0 };
        FILE* file = fopen(output, "rb");
        const size_t length = file != NULL ? fread(contents, 1, sizeof(contents), file) : 0;
        if (file != NULL)
        {
            fclose(file);
        }
        const bool named_option = text != NULL && strstr(text, "unsupported code generation option") != NULL && strstr(text, options[i]) != NULL;
        char label[192];
        snprintf(label, sizeof(label), "%s fails by name without overwriting output", options[i]);
        check_test_expect(test, status == 1 && named_option && length == sizeof(sentinel) - 1 && memcmp(contents, sentinel, length) == 0, label);
        free(text);
    }
    char* text = NULL;
    char* const link[] = { compiler, "-o", output, source, "-fstack-protector-strong", NULL };
    const int link_status = made ? check_test_driver_spawn(link, &text) : -1;
    check_test_expect(test, link_status == 1 && text != NULL && strstr(text, "-fstack-protector-strong") != NULL,
                      "the executable route rejects unsupported hardening");
    free(text);
    char* const allowed[] = { compiler, "-c", "-fno-stack-protector", "-fcf-protection=none", "-o", output, source, NULL };
    const int allowed_status = made ? check_test_driver_spawn(allowed, &text) : -1;
    check_test_expect(test, allowed_status == 0, "disabling stack protection and CET remains supported");
    free(text);
    char* const disabled_last[] = { compiler, "-c", "-fstack-protector-strong", "-fno-stack-protector",
                                    "-fcf-protection=full", "-fcf-protection=none", "-o", output, source, NULL };
    const int disabled_status = made ? check_test_driver_spawn(disabled_last, &text) : -1;
    check_test_expect(test, disabled_status == 0, "the last stack-protector and CET options may disable unsupported instrumentation");
    free(text);
    char* const enabled_last[] = { compiler, "-c", "-fno-stack-protector", "-fstack-protector-all", "-o", output, source, NULL };
    const int enabled_status = made ? check_test_driver_spawn(enabled_last, &text) : -1;
    check_test_expect(test, enabled_status == 1 && text != NULL && strstr(text, "-fstack-protector-all") != NULL,
                      "a later stack-protector option enables the refused instrumentation");
    free(text);
    char* const cet_last[] = { compiler, "-c", "-fcf-protection=none", "-fcf-protection=branch", "-o", output, source, NULL };
    const int cet_status = made ? check_test_driver_spawn(cet_last, &text) : -1;
    check_test_expect(test, cet_status == 1 && text != NULL && strstr(text, "-fcf-protection=branch") != NULL,
                      "a later CET option enables the refused instrumentation");
    free(text);
    char* const value[] = { "env", "-C", (char*)directory, compiler, "-c", "-o", "-pg", "codegen-flags.c", NULL };
    const int value_status = made ? check_test_driver_spawn(value, &text) : -1;
    struct stat stat_output;
    check_test_expect(test, value_status == 0 && stat(named, &stat_output) == 0 && stat_output.st_size > 0,
                      "an option value named -pg remains an output path");
    free(text);
    unlink(source);
    unlink(output);
    unlink(named);
}

// Options that take the next argument as their value keep it, however it is spelled (#398's review): run in the test's
// directory (env -C), `-o -lresult -include -local.h -I -Lheaders` writes -lresult from a program that needs the forced
// include's macro and a header that only -Lheaders has, and the program runs.
static void check_test_driver_option_values(CheckTest* test, const char* executable, const char* directory)
{
    static const char header_text[] = "#define VALUE 3\n";
    static const char forced_text[] = "#define LOCAL 4\n";
    static const char program[] = "#include \"h.h\"\nint main(void) { return VALUE + LOCAL == 7 ? 0 : 1; }\n";
    char compiler[PATH_MAX];
    char headers[256];
    char header[256];
    char forced[256];
    char source[256];
    char output[256];
    snprintf(headers, sizeof(headers), "%s/-Lheaders", directory);
    snprintf(header, sizeof(header), "%s/-Lheaders/h.h", directory);
    snprintf(forced, sizeof(forced), "%s/-local.h", directory);
    snprintf(source, sizeof(source), "%s/values.c", directory);
    snprintf(output, sizeof(output), "%s/-lresult", directory);
    // The compiler runs in the test's directory, so its path must not be relative to this one.
    const bool made = realpath(executable, compiler) != NULL && mkdir(headers, 0755) == 0 && parse_test_write(header, header_text, sizeof(header_text) - 1) &&
                      parse_test_write(forced, forced_text, sizeof(forced_text) - 1) && parse_test_write(source, program, sizeof(program) - 1);
    char* const link[] = { "env", "-C", (char*)directory, compiler, "values.c", "-o", "-lresult", "-include", "-local.h", "-I", "-Lheaders", NULL };
    char* const run[] = { output, NULL };
    char* text = NULL;
    const int linked = made ? check_test_driver_spawn(link, &text) : -1;
    if (linked != 0)
    {
        printf("  option values: link status %d: %s\n", linked, text != NULL ? text : "");
    }
    free(text);
    text = NULL;
    const int ran = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    text = NULL;
    check_test_expect(test, linked == 0 && ran == 0, "-o -lresult, -include -local.h and -I -Lheaders keep their values");
    const char* const files[] = { header, forced, source, output };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
    rmdir(headers);
}

// Reads --time-stages' report in `text` (check_clock_read): whether it has every row once, the stages adding up to the
// total and the preprocessor's and the lowering's rows to their stages (each line is rounded to a microsecond), the
// reader's expressions' walk once within the reader's stage (#1038), and the process's lines. Each stage's milliseconds
// go to `stages` (by CheckStage).
static bool check_test_driver_time_report(const char* text, double* stages)
{
    static const char* const process[] = { "\ntime-process\ttsc-mhz\t", "\ntime-process\tuser-ms\t", "\ntime-process\tsystem-ms\t",
                                           "\ntime-process\tstage-system-source\tgetrusage-rusage-self-ru-stime\n",
                                           "\ntime-process\tstage-system-boundary\tresource-marks\n",
                                           "\ntime-process\tstage-system-status\t",
                                           "\ntime-process\tstage-system-accuracy\tlow-confidence\n",
                                           "\ntime-process\tstage-system-reason\tresource-mark-getrusage-deltas\n",
                                           "\ntime-process\tstage-system-validation\tdocs-compile-time-clock-accounting-control\n",
                                           "\ntime-process\tstage-system-use\tdo-not-rank-or-subtract-from-elapsed\n",
                                           "\ntime-process\tpeak-rss-kb\t", "\ntime-process\tminor-faults\t", "\ntime-process\tmajor-faults\t",
                                           "\ntime-process\tlanes-asked\t", "\ntime-process\tlanes\t", "\ntime-process\thuge-pages\t", "\ntime-process\ttokens\t" };
    double phases[BUSTER_CHECK_CLOCK_PHASES] = { 0 };
    double total = -1;
    double walk = -1;
    bool valid = check_clock_read(text, stages, phases, &total, &walk, NULL);
    double sum = 0;
    double phase_sums[CHECK_STAGE_COUNT] = { 0 };
    for (uint32_t s = 0; s < CHECK_STAGE_COUNT; s += 1)
    {
        sum += stages[s];
    }
    for (uint32_t p = 0; p < BUSTER_CHECK_CLOCK_PHASES; p += 1)
    {
        phase_sums[check_clock_phase_stages[p]] += phases[p];
    }
    for (uint32_t p = 0; valid && p < BUSTER_ARRAY_LENGTH(process); p += 1)
    {
        valid = strstr(text, process[p]) != NULL;
    }
    // check_clock_read took the walk's row once, within the reader's stage; the walk's own line must say so too. Its parts
    // (check_clock_read_walk) are each there once, adding up to no more than the walk.
    double parts[BUSTER_CHECK_CLOCK_WALK_PARTS + 2] = { 0 };
    valid = valid && walk >= 0 && strstr(text, "\ntime-check\texpressions\t") != NULL && check_clock_read_walk(text, parts);
    // The graph run's memoized transitions' counts (#1050), each on a line of its own.
    valid = valid && strstr(text, "\ntime-check\ttransition-hits\t") != NULL && strstr(text, "\ntime-check\ttransition-misses\t") != NULL &&
            strstr(text, "\ntime-check\ttransition-folds\t") != NULL && strstr(text, "\ntime-check\ttransition-entries\t") != NULL;
    const double stage_error = sum > total ? sum - total : total - sum;
    // The preprocessor's rows add up to its stage, and the lowering's to its own.
    const double preprocess = stages[CHECK_STAGE_PREPROCESS];
    const double lower = stages[CHECK_STAGE_LOWER];
    const double preprocess_error = phase_sums[CHECK_STAGE_PREPROCESS] > preprocess ? phase_sums[CHECK_STAGE_PREPROCESS] - preprocess : preprocess - phase_sums[CHECK_STAGE_PREPROCESS];
    const double lower_error = phase_sums[CHECK_STAGE_LOWER] > lower ? phase_sums[CHECK_STAGE_LOWER] - lower : lower - phase_sums[CHECK_STAGE_LOWER];
    return valid && total > 0 && stage_error <= 0.001 * (CHECK_STAGE_COUNT + 1) && preprocess_error <= 0.001 * (BUSTER_CHECK_CLOCK_PHASES + 1) &&
           lower_error <= 0.001 * (BUSTER_CHECK_CLOCK_PHASES + 1);
}

// Builds one whole report for check_clock_read: every stage and the preprocessor's and the lowering's rows once, then the total and the
// system/peak statuses. `system_status` and `peak_status` are 0 for no status line, 1 for available and 2 for
// unavailable. `system_available` and `peak_available` choose numeric columns or their -1 form; `alter_one_system`
// makes one stage numeric in an otherwise unavailable report. Returns false if the text doesn't fit.
static bool check_test_driver_clock_text(char* text, size_t capacity, bool system_available, bool peak_available, int system_status, int peak_status,
                                         bool alter_one_system)
{
    size_t used = 0;
    bool fits = true;
    for (uint32_t s = 0; fits && s < CHECK_STAGE_COUNT; s += 1)
    {
        const bool numeric = system_available || (alter_one_system && s == CHECK_STAGE_PREPROCESS);
        const int written = numeric ? snprintf(text + used, capacity - used, "time-stage\t%s\t1.000\t0.1\t0.500\t2\t%d\n", check_stage_names[s], peak_available ? 3 : -1) :
                                      snprintf(text + used, capacity - used, "time-stage\t%s\t1.000\t0.1\t-1.000\t-1\t%d\n", check_stage_names[s], peak_available ? 3 : -1);
        fits = written > 0 && (size_t)written < capacity - used;
        used += fits ? (size_t)written : 0;
    }
    for (uint32_t p = 0; fits && p < BUSTER_CHECK_CLOCK_PHASES; p += 1)
    {
        const int written = snprintf(text + used, capacity - used, "time-stage\t%s\t0.100\t0.0\n", check_clock_phase_names[p]);
        fits = written > 0 && (size_t)written < capacity - used;
        used += fits ? (size_t)written : 0;
    }
    const char* system_line = system_status == 1 ? "time-process\tstage-system-status\tavailable\n" :
                              system_status == 2 ? "time-process\tstage-system-status\tunavailable\n" : "";
    const char* peak_line = peak_status == 1 ? "time-process\tpeak-rss-status\tavailable\n" :
                            peak_status == 2 ? "time-process\tpeak-rss-status\tunavailable\n" : "";
    const int written = snprintf(text + used, capacity - used, "time-stage\ttotal\t1.000\t100.0\ntime-check\texpressions\t0.100\t0.0\n%s%s", system_line, peak_line);
    fits = written > 0 && (size_t)written < capacity - used;
    return fits;
}

// The report's walk lines as check_clock_report prints them from a clock (#1172's review): read back by name
// (check_clock_read_walk), each part's ticks come back under its own name and the usage's columns in their order, or as
// the unavailable pair where a unit's read failed; and measure-compile's rows of them (check_clock_print_walk) give each
// median under its own name.
static void check_test_driver_clock_printer(CheckTest* test)
{
    bool printed = true;
    for (uint32_t failed = 0; failed < 4; failed += 1)
    {
        CheckClock stage_clock;
        check_clock_start(&stage_clock);
        // A billion ticks across a second and the report's own few microseconds: a millisecond is a million ticks to about
        // one part in ten thousand, which the parts' ratios below allow.
        stage_clock.monotonic.tv_sec -= 1;
        stage_clock.last = stage_clock.start + 1000000000ull;
        stage_clock.deferred_expression_parts = failed >= 2;
        stage_clock.expression_ticks = failed >= 2 ? 50000000ull : 110000000ull;
        for (uint32_t p = 0; p < BUSTER_CHECK_CLOCK_WALK_PARTS; p += 1)
        {
            stage_clock.expression_part_ticks[p] = 10000000ull * (p + 1);
        }
        stage_clock.expression_system_us = 1234;
        stage_clock.expression_faults = 5678;
        stage_clock.expression_usage_failed = (failed & 1) != 0;
        FILE* stream = tmpfile();
        char text[32768] = { 0 };
        if (stream != NULL)
        {
            check_clock_report(&stage_clock, NULL, 0, stream);
            rewind(stream);
            const size_t length = fread(text, 1, sizeof(text) - 1, stream);
            text[length] = 0;
            fclose(stream);
        }
        double parts[BUSTER_CHECK_CLOCK_WALK_PARTS + 2] = { 0 };
        bool valid = stream != NULL && check_clock_read_walk(text, parts) && parts[0] > 0 && check_clock_walk_work(text) == (failed >= 2);
        for (uint32_t p = 1; valid && p < BUSTER_CHECK_CLOCK_WALK_PARTS; p += 1)
        {
            const double ratio = parts[p] / parts[0];
            valid = ratio > (double)(p + 1) - 0.001 && ratio < (double)(p + 1) + 0.001;
        }
        printed = printed && valid && ((failed & 1) != 0 ? parts[BUSTER_CHECK_CLOCK_WALK_PARTS] == -1 && parts[BUSTER_CHECK_CLOCK_WALK_PARTS + 1] == -1
                                                   : parts[BUSTER_CHECK_CLOCK_WALK_PARTS] == 1.234 && parts[BUSTER_CHECK_CLOCK_WALK_PARTS + 1] == 5678);
    }
    check_test_expect(test, printed, "--time-stages prints each of the walk's parts under its own name, and its usage in order or as unavailable");
    // measure-compile's rows: medians and ranges distinct per part, then the usage's.
    double median[BUSTER_CHECK_CLOCK_WALK_PARTS + 2];
    double low[BUSTER_CHECK_CLOCK_WALK_PARTS + 2];
    double high[BUSTER_CHECK_CLOCK_WALK_PARTS + 2];
    for (uint32_t p = 0; p < BUSTER_CHECK_CLOCK_WALK_PARTS + 2; p += 1)
    {
        median[p] = 1.5 * (p + 1);
        low[p] = median[p] - 0.25;
        high[p] = median[p] + 0.25;
    }
    FILE* stream = tmpfile();
    char text[4096] = { 0 };
    if (stream != NULL)
    {
        check_clock_print_walk(stream, median, low, high, 100.0, false);
        rewind(stream);
        const size_t length = fread(text, 1, sizeof(text) - 1, stream);
        text[length] = 0;
        fclose(stream);
    }
    uint32_t seen[BUSTER_CHECK_CLOCK_WALK_PARTS + 1] = { 0 };
    bool rows = stream != NULL;
    for (const char* line = text; rows && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        char name[64] = { 0 };
        double value = 0;
        double from = 0;
        double to = 0;
        double faults = 0;
        const int fields = sscanf(line, "%63s %lf (%lf to %lf)", name, &value, &from, &to);
        bool known = false;
        for (uint32_t p = 0; p < BUSTER_CHECK_CLOCK_WALK_PARTS; p += 1)
        {
            char wanted[64];
            snprintf(wanted, sizeof(wanted), "expressions.%s", check_clock_walk_parts[p]);
            const bool match = strcmp(name, wanted) == 0;
            known = known || match;
            seen[p] += match;
            rows = rows && (!match || (fields == 4 && value == median[p] && from == low[p] && to == high[p]));
        }
        const bool usage = strcmp(name, "expressions.usage") == 0 && sscanf(line, "%*s %lf %lf", &value, &faults) == 2;
        seen[BUSTER_CHECK_CLOCK_WALK_PARTS] += usage;
        rows = rows && (known || usage) &&
               (!usage || (value == median[BUSTER_CHECK_CLOCK_WALK_PARTS] && faults == 9.0));
        line = end != NULL ? end + 1 : line + strlen(line);
    }
    for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(seen); p += 1)
    {
        rows = rows && seen[p] == 1;
    }
    check_test_expect(test, rows, "measure-compile prints each of the walk's parts' median and range under its own name, and its usage in order");
}

// check_clock_read's availability contract: numeric legacy reports still read, an unavailable status needs every
// system and fault column at -1, an available status or none refuses any -1, and one substituted numeric column in an
// unavailable report is refused rather than hiding a distorted later delta.
static void check_test_driver_clock_reader(CheckTest* test)
{
    char text[16384];
    double stages[CHECK_STAGE_COUNT] = { 0 };
    double phases[BUSTER_CHECK_CLOCK_PHASES] = { 0 };
    double usage[3 * CHECK_STAGE_COUNT] = { 0 };
    double total = 0;
    double walk = 0;
    const bool numeric = check_test_driver_clock_text(text, sizeof(text), true, true, 0, 0, false) &&
                         check_clock_read(text, stages, phases, &total, &walk, usage) && total == 1.0 && walk == 0.1 && usage[0] == 0.5 && usage[1] == 2 && usage[2] == 3;
    memset(stages, 0, sizeof(stages));
    memset(usage, 0, sizeof(usage));
    const bool unavailable = check_test_driver_clock_text(text, sizeof(text), false, true, 2, 0, false) &&
                             check_clock_read(text, stages, phases, &total, &walk, usage) && total == 1.0 && walk == 0.1 && usage[0] == -1 && usage[1] == -1 && usage[2] == 3;
    const bool false_available = check_test_driver_clock_text(text, sizeof(text), false, true, 1, 0, false) &&
                                 !check_clock_read(text, stages, phases, &total, &walk, usage);
    const bool mixed = check_test_driver_clock_text(text, sizeof(text), false, true, 2, 0, true) &&
                       !check_clock_read(text, stages, phases, &total, &walk, usage);
    const bool numeric_without_status = check_test_driver_clock_text(text, sizeof(text), true, true, 0, 0, false) &&
                                        check_clock_read(text, stages, phases, &total, &walk, usage);
    check_test_expect(test, numeric && unavailable && false_available && mixed && numeric_without_status,
                      "--time-stages' reader accepts numeric and wholly unavailable system-time reports and refuses mixed or falsely available ones");
    // The walk's parts (check_clock_read_walk): each once, nonnegative, within the walk's row, and its usage line, whose
    // unavailable form is -1 in both columns and nowhere else.
#define BUSTER_TEST_WALK_HEAD "time-check\texpressions\t1.000\t1.0\n"
#define BUSTER_TEST_WALK_CLASSES "time-check\texpressions.classes\t0.100\t0.1\n"
#define BUSTER_TEST_WALK_PREPARE "time-check\texpressions.prepare\t0.200\t0.2\n"
#define BUSTER_TEST_WALK_ACTIONS "time-check\texpressions.actions\t0.400\t0.4\n"
#define BUSTER_TEST_WALK_VISITS "time-check\texpressions.visits\t0.250\t0.2\n"
#define BUSTER_TEST_WALK_PARTS BUSTER_TEST_WALK_CLASSES BUSTER_TEST_WALK_PREPARE BUSTER_TEST_WALK_ACTIONS BUSTER_TEST_WALK_VISITS
    const char* const walks[] = {
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_PARTS "time-check\texpressions.usage\t0.300\t12\n",
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_PARTS "time-check\texpressions.usage\t-1.000\t-1\n",
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_CLASSES BUSTER_TEST_WALK_PREPARE BUSTER_TEST_WALK_VISITS "time-check\texpressions.usage\t0.300\t12\n",
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_PARTS BUSTER_TEST_WALK_ACTIONS "time-check\texpressions.usage\t0.300\t12\n",
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_CLASSES BUSTER_TEST_WALK_PREPARE "time-check\texpressions.actions\t0.600\t0.6\n" BUSTER_TEST_WALK_VISITS
        "time-check\texpressions.usage\t0.300\t12\n",
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_CLASSES BUSTER_TEST_WALK_PARTS "time-check\texpressions.usage\t0.300\t12\n",
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_PARTS,
        BUSTER_TEST_WALK_HEAD "time-check\texpressions.classes\t-0.100\t-0.1\n" BUSTER_TEST_WALK_PREPARE BUSTER_TEST_WALK_ACTIONS BUSTER_TEST_WALK_VISITS
        "time-check\texpressions.usage\t0.300\t12\n",
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_PARTS "time-check\texpressions.usage\t-0.300\t12\n",
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_PARTS "time-check\texpressions.usage\t0.300\t-1\n",
        BUSTER_TEST_WALK_HEAD BUSTER_TEST_WALK_PARTS "time-check\texpressions.usage\t-1.000\t12\n",
    };
#undef BUSTER_TEST_WALK_PARTS
#undef BUSTER_TEST_WALK_VISITS
#undef BUSTER_TEST_WALK_ACTIONS
#undef BUSTER_TEST_WALK_PREPARE
#undef BUSTER_TEST_WALK_CLASSES
#undef BUSTER_TEST_WALK_HEAD
    double parts[BUSTER_CHECK_CLOCK_WALK_PARTS + 2] = { 0 };
    const bool whole = check_clock_read_walk(walks[0], parts) && parts[0] == 0.1 && parts[1] == 0.2 && parts[2] == 0.4 && parts[3] == 0.25 && parts[4] == 0.3 &&
                       parts[5] == 12;
    const bool unavailable_usage = check_clock_read_walk(walks[1], parts) && parts[3] == 0.25 && parts[4] == -1 && parts[5] == -1;
    // A missing part, a repeated one, parts past the walk's row (actions' 0.6 makes their sum 1.15), a repeated part, a
    // missing usage line, a negative part, and a negative or half-unavailable usage.
    bool refused = true;
    for (uint32_t w = 2; w < BUSTER_ARRAY_LENGTH(walks); w += 1)
    {
        refused = refused && !check_clock_read_walk(walks[w], parts);
    }
    check_test_expect(test, whole && unavailable_usage && refused,
                      "--time-stages' walk reader takes each part once within the walk, and a numeric or wholly unavailable usage, and refuses a missing, "
                      "repeated, excess or negative part, a missing or negative usage and a half-unavailable one");
    check_test_driver_clock_printer(test);
}

static void check_test_driver_cpu_pinning(CheckTest* test, const char* executable, const char* directory)
{
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const long bytes = syscall(SYS_sched_getaffinity, 0, sizeof(allowed), allowed);
    uint32_t cpus[2] = { 0 };
    uint32_t count = 0;
    for (uint32_t cpu = 0; bytes > 0 && cpu < BUSTER_LANE_CPU_MASK_WORDS * 64u && count < 2; cpu += 1)
    {
        if ((allowed[cpu / 64] & (UINT64_C(1) << (cpu % 64))) != 0)
        {
            cpus[count++] = cpu;
        }
    }
    char source[PATH_MAX];
    char reference[PATH_MAX];
    char pinned[PATH_MAX];
    char cpu_list[32];
    char duplicate[32];
    char receipt[100];
    snprintf(source, sizeof(source), "%s/pin.c", directory);
    snprintf(reference, sizeof(reference), "%s/pin-reference.o", directory);
    snprintf(pinned, sizeof(pinned), "%s/pin-result.o", directory);
    snprintf(cpu_list, sizeof(cpu_list), "%u", cpus[0]);
    snprintf(duplicate, sizeof(duplicate), "%u,%u", cpus[0], cpus[0]);
    snprintf(receipt, sizeof(receipt), "lane-pin\tindex=0\texpected=%u\tobserved=%u\tstatus=0", cpus[0], cpus[0]);
    static const char program[] = "int f(int x) { return x * 2; }\n";
    const bool made = count > 0 && parse_test_write(source, program, sizeof(program) - 1);
    char* const plain[] = { (char*)executable, "--lanes", "1", "-c", source, "-o", reference, NULL };
    char* const one[] = { (char*)executable, "--lanes", "1", "--lane-cpus", cpu_list, "--time-stages", "-c", source, "-o", pinned, NULL };
    char* text = NULL;
    const int plain_status = made ? check_test_driver_spawn(plain, &text) : -1;
    free(text);
    text = NULL;
    const int one_status = made ? check_test_driver_spawn(one, &text) : -1;
    check_test_expect(test, plain_status == 0 && one_status == 0 && text != NULL && strstr(text, receipt) != NULL &&
                            check_test_driver_same_files(reference, pinned), "a pinned lane reports its actual CPU and preserves the object");
    free(text);
    static const char* const bad_lists[] = { "", "1024", "-1", "0,", "0,,1", " 0", "99999999999999999999" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bad_lists); i += 1)
    {
        char* const bad[] = { (char*)executable, "--lanes", "1", "--lane-cpus", (char*)bad_lists[i], "-c", source, "-o", pinned, NULL };
        text = NULL;
        const int status = made ? check_test_driver_spawn(bad, &text) : -1;
        check_test_expect(test, status == 1, "a malformed or unavailable CPU list is refused");
        free(text);
    }
    char* const repeated[] = { (char*)executable, "--lanes", "2", "--lane-cpus", duplicate, "-c", source, "-o", pinned, NULL };
    char* const missing_lanes[] = { (char*)executable, "--lane-cpus", cpu_list, "-c", source, "-o", pinned, NULL };
    text = NULL;
    const int repeated_status = made ? check_test_driver_spawn(repeated, &text) : -1;
    free(text);
    text = NULL;
    const int missing_status = made ? check_test_driver_spawn(missing_lanes, &text) : -1;
    free(text);
    check_test_expect(test, repeated_status == 1 && missing_status == 1, "duplicate CPUs and a list without explicit lanes are refused");
    if (count == 2)
    {
        snprintf(cpu_list, sizeof(cpu_list), "%u,%u", cpus[0], cpus[1]);
        char* const two[] = { (char*)executable, "--lanes", "2", "--lane-cpus", cpu_list, "--time-stages", "-c", source, "-o", pinned, NULL };
        text = NULL;
        const int status = check_test_driver_spawn(two, &text);
        snprintf(receipt, sizeof(receipt), "lane-pin\tindex=1\texpected=%u\tobserved=%u\tstatus=0", cpus[1], cpus[1]);
        check_test_expect(test, status == 0 && text != NULL && strstr(text, receipt) != NULL && check_test_driver_same_files(reference, pinned),
                          "two distinct pinned lanes report their CPUs and preserve the object");
        free(text);
        unlink(pinned);
        char* const capped[] = { (char*)executable, "--lanes", "2", "--lane-cpus", cpu_list, "--worker-budget", "1", "-c", source, "-o", pinned, NULL };
        text = NULL;
        const int capped_status = check_test_driver_spawn(capped, &text);
        check_test_expect(test, capped_status == 1 && access(pinned, F_OK) != 0,
                          "a worker budget smaller than the pinned lane count fails before output");
        free(text);
    }
    unlink(source);
    unlink(reference);
    unlink(pinned);
}

// --time-stages (#518): an object compiled with it must be the bytes compiled without it, which prints no report, and
// its report must account for every stage once, adding up to its total; so must the direct path's, whose executable
// runs, with -g too, whose DWARF and static data are stages of their own, and -fsyntax-only's, which emits nothing.
static void check_test_driver_time_stages(CheckTest* test, const char* executable, const char* directory)
{
    check_test_driver_clock_reader(test);
    static const char program[] = "#include <stdio.h>\n"
                                  "static int table[4] = { 1, 2, 3, 4 };\n"
                                  "static int sum(int n) { int s = 0; for (int i = 0; i < n; i++) s += table[i & 3]; return s; }\n"
                                  "int main(void) { printf(\"%d\\n\", sum(10)); return sum(4) == 10 ? 0 : 1; }\n";
    char compiler[PATH_MAX];
    char source[256];
    char plain[256];
    char timed[256];
    char linked[256];
    snprintf(source, sizeof(source), "%s/time_stages.c", directory);
    snprintf(plain, sizeof(plain), "%s/time_stages_plain.o", directory);
    snprintf(timed, sizeof(timed), "%s/time_stages_timed.o", directory);
    snprintf(linked, sizeof(linked), "%s/time_stages", directory);
    const bool made = realpath(executable, compiler) != NULL && parse_test_write(source, program, sizeof(program) - 1);
    char* const compile_plain[] = { compiler, "-c", source, "-o", plain, NULL };
    char* const compile_timed[] = { compiler, "-c", "--time-stages", source, "-o", timed, NULL };
    char* const link[] = { compiler, "--time-stages", source, "-o", linked, NULL };
    char* const syntax[] = { compiler, "-fsyntax-only", "--time-stages", source, NULL };
    char* const run[] = { linked, NULL };
    char* text = NULL;
    double stages[CHECK_STAGE_COUNT] = { 0 };
    const int plain_status = made ? check_test_driver_spawn(compile_plain, &text) : -1;
    check_test_expect(test, plain_status == 0 && text != NULL && strstr(text, "time-stage") == NULL, "-c without --time-stages prints no report");
    free(text);
    text = NULL;
    const int timed_status = made ? check_test_driver_spawn(compile_timed, &text) : -1;
    const bool object_report = timed_status == 0 && check_test_driver_time_report(text, stages);
    if (!object_report)
    {
        printf("  --time-stages -c: status %d: %s\n", timed_status, text != NULL ? text : "");
    }
    check_test_expect(test, object_report && stages[CHECK_STAGE_ENCODE] > 0 && stages[CHECK_STAGE_OBJECT_WRITE] > 0 && stages[CHECK_STAGE_EXECUTABLE] == 0 &&
                                stages[CHECK_STAGE_LANES] == 0,
                      "--time-stages -c reports every stage once, adding up to its total, its functions on one lane");
    // The lowering's rows (#1048), which add up to its stage: each measured, not left to the rest's row. Its preparation
    // reads the whole unit, <stdio.h>'s too, and on one lane each of the two functions' statements, the rest of its walk
    // and its validation take some time of their own.
    double phases[BUSTER_CHECK_CLOCK_PHASES] = { 0 };
    double read_stages[CHECK_STAGE_COUNT] = { 0 };
    double read_total = 0;
    double read_walk = 0;
    check_test_expect(test, object_report && check_clock_read(text, read_stages, phases, &read_total, &read_walk, NULL) &&
                                phases[BUSTER_CHECK_CLOCK_LOWER_FIRST + CHECK_CLOCK_LOWER_PREPARE] > 0 &&
                                phases[BUSTER_CHECK_CLOCK_LOWER_FIRST + CHECK_CLOCK_LOWER_STATEMENTS] > 0 &&
                                phases[BUSTER_CHECK_CLOCK_LOWER_FIRST + CHECK_CLOCK_LOWER_BODIES] > 0 &&
                                phases[BUSTER_CHECK_CLOCK_LOWER_FIRST + CHECK_CLOCK_LOWER_VALIDATE] > 0,
                      "--time-stages -c reports the lowering's preparation, statements, bodies and validation inside its stage");
    check_test_expect(test, timed_status == 0 && check_test_driver_same_files(plain, timed), "--time-stages -c writes the object -c writes");
    uint64_t expression_nodes = 0;
    uint64_t batch_nodes = 0;
    uint64_t template_nodes = 0;
    const char* coverage = text != NULL ? strstr(text, "lowering-nodes\tevaluated=") : NULL;
    const char* templates = coverage != NULL ? strstr(coverage, "\ttemplates=") : NULL;
    const bool counted = coverage != NULL && sscanf(coverage, "lowering-nodes\tevaluated=%" SCNu64 "\tbatched=%" SCNu64, &expression_nodes, &batch_nodes) == 2 &&
                         templates != NULL && sscanf(templates, "\ttemplates=%" SCNu64, &template_nodes) == 1;
    // No compile takes the vector path's fragments (#624) or the node templates (#1048), on either host (CheckLowerWriter's
    // `fragments` and `templates`): their counts are reported, and none of the expressions.
    check_test_expect(test, counted && expression_nodes != 0 && batch_nodes == 0 && template_nodes == 0,
                      "--time-stages reports evaluated expressions, and no fragments or templates");
    free(text);
    text = NULL;
    memset(stages, 0, sizeof(stages));
    const int link_status = made ? check_test_driver_spawn(link, &text) : -1;
    const bool link_report = link_status == 0 && check_test_driver_time_report(text, stages);
    if (!link_report)
    {
        printf("  --time-stages: status %d: %s\n", link_status, text != NULL ? text : "");
    }
    // The write's phases after the stages (#778), each once, adding up to no more than the write's stage.
    double write_phases = 0;
    bool phases_once = text != NULL;
    for (uint32_t p = 0; phases_once && p < BUSTER_CHECK_CLOCK_WRITE_PHASES; p += 1)
    {
        char needle[64];
        snprintf(needle, sizeof(needle), "\ntime-write\t%s\t", check_clock_write_names[p]);
        const char* found = strstr(text, needle);
        double value = -1;
        phases_once = found != NULL && strstr(found + 1, needle) == NULL && sscanf(found + strlen(needle), "%lf", &value) == 1 && value >= 0;
        write_phases += value;
    }
    check_test_expect(test, link_report && phases_once && write_phases <= stages[CHECK_STAGE_WRITE] + 0.001 * BUSTER_CHECK_CLOCK_WRITE_PHASES,
                      "--time-stages on the direct path gives the write's phases once each, within its stage");
    free(text);
    text = NULL;
    const int ran = link_status == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    text = NULL;
    check_test_expect(test, link_report && ran == 0 && stages[CHECK_STAGE_EXECUTABLE] > 0 && stages[CHECK_STAGE_OBJECT_WRITE] == 0,
                      "--time-stages on the direct path reports every stage once, and its executable runs");
    // The direct path's -g on a program of many functions and tables, enough that each stage takes microseconds: the
    // DWARF and the static data are their own stages, not the link's.
    memset(stages, 0, sizeof(stages));
    static char many[1u << 17];
    uint64_t length = 0;
    for (uint32_t f = 0; f < 512; f += 1)
    {
        length += (uint64_t)snprintf(many + length, sizeof(many) - length, "static int t%u[16] = { %u };\nstatic int f%u(void) { return t%u[0]; }\n", f, f, f, f);
    }
    length += (uint64_t)snprintf(many + length, sizeof(many) - length, "int main(void)\n{\n    int s = 0;\n");
    for (uint32_t f = 0; f < 512; f += 1)
    {
        length += (uint64_t)snprintf(many + length, sizeof(many) - length, "    s += f%u();\n", f);
    }
    length += (uint64_t)snprintf(many + length, sizeof(many) - length, "    return s == 512 * 511 / 2 ? 0 : 1;\n}\n");
    char debug_source[256];
    snprintf(debug_source, sizeof(debug_source), "%s/time_stages_debug.c", directory);
    const bool debug_made = made && length < sizeof(many) && parse_test_write(debug_source, many, length);
    char* const debug_link[] = { compiler, "--time-stages", "-g", debug_source, "-o", linked, NULL };
    const int debug_status = debug_made ? check_test_driver_spawn(debug_link, &text) : -1;
    const bool debug_report = debug_status == 0 && check_test_driver_time_report(text, stages);
    if (!debug_report)
    {
        printf("  --time-stages -g: status %d: %s\n", debug_status, text != NULL ? text : "");
    }
    free(text);
    text = NULL;
    const int debug_ran = debug_status == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    text = NULL;
    check_test_expect(test, debug_report && debug_ran == 0 && stages[CHECK_STAGE_DEBUG] > 0 && stages[CHECK_STAGE_PUBLISH_DATA] > 0 &&
                                stages[CHECK_STAGE_EXECUTABLE] > 0,
                      "--time-stages -g on the direct path charges the DWARF and the static data to their own stages");
    // The same program's functions on two lanes (#151): their run is the lanes' stage, not the one lane's stages from
    // lowering to unwind, and the object is one lane's.
    char one_lane[256];
    char two_lanes[256];
    snprintf(one_lane, sizeof(one_lane), "%s/time_stages_one_lane.o", directory);
    snprintf(two_lanes, sizeof(two_lanes), "%s/time_stages_two_lanes.o", directory);
    char* const compile_one[] = { compiler, "-c", "--lanes", "1", debug_source, "-o", one_lane, NULL };
    char* const compile_two[] = { compiler, "-c", "--time-stages", "--lanes", "2", debug_source, "-o", two_lanes, NULL };
    memset(stages, 0, sizeof(stages));
    const int one_status = debug_made ? check_test_driver_spawn(compile_one, &text) : -1;
    free(text);
    text = NULL;
    const int two_status = debug_made ? check_test_driver_spawn(compile_two, &text) : -1;
    const bool two_report = two_status == 0 && check_test_driver_time_report(text, stages);
    // Its session's lanes (#905): the two it asked for, and ran on.
    const bool two_counted = two_report && strstr(text, "\ntime-process\tlanes-asked\t2\n") != NULL && strstr(text, "\ntime-process\tlanes\t2\n") != NULL;
    if (!two_counted)
    {
        printf("  --time-stages --lanes 2: status %d: %s\n", two_status, text != NULL ? text : "");
    }
    free(text);
    text = NULL;
    check_test_expect(test, one_status == 0 && two_counted && stages[CHECK_STAGE_LANES] > 0 && stages[CHECK_STAGE_ENCODE] == 0 &&
                                check_test_driver_same_files(one_lane, two_lanes),
                      "--time-stages with the functions on two lanes charges their run to the lanes' stage, reports the two lanes, and writes one "
                      "lane's object");
    memset(stages, 0, sizeof(stages));
    const int syntax_status = made ? check_test_driver_spawn(syntax, &text) : -1;
    // A source's -fsyntax-only, which has no wide part, asks for one lane (#905).
    check_test_expect(test, syntax_status == 0 && check_test_driver_time_report(text, stages) && stages[CHECK_STAGE_PARSE] > 0 && stages[CHECK_STAGE_LOWER] == 0 &&
                                strstr(text, "\ntime-process\tlanes-asked\t1\n") != NULL && strstr(text, "\ntime-process\tlanes\t1\n") != NULL,
                      "--time-stages -fsyntax-only reports the stages it ran, on one lane");
    free(text);
    text = NULL;
    // BUSTER_HUGE_PAGES (#1109, arena_page_policy), each case with this process's own setting, which the child inherits,
    // as the system's or off, and with the busy test's answer forced (BUSTER_TEST_HOST_BUSY, #672): "0" turns transparent
    // huge pages off for the process; "1" leaves them as the system has them, even where the process that started it had
    // them off; without it, they are off on a virtual machine (CPUID's hypervisor bit), which the child must decide
    // itself, and elsewhere as the process that started it had them, but on a busy host, with them on, its arenas alone
    // take base pages ("busy").
    const char* pages_saved = getenv("BUSTER_HUGE_PAGES");
    char* pages_copy = pages_saved != NULL ? strdup(pages_saved) : NULL;
    const char* busy_saved = getenv("BUSTER_TEST_HOST_BUSY");
    char* busy_copy = busy_saved != NULL ? strdup(busy_saved) : NULL;
    const int pages_own = prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0);
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    const bool virtual_machine = __get_cpuid(1, &eax, &ebx, &ecx, &edx) != 0 && (ecx & (1u << 31)) != 0;
    // Each case: the variable's value (null for none), this process's setting, whether the host is busy, and the child's
    // report.
    const struct
    {
        const char* setting;
        int inherited;
        const char* busy;
        const char* report;
    } pages_cases[] = {
        { "0", 0, "0", "off" },
        { "1", 1, "0", "system" },
        { NULL, 0, "0", virtual_machine ? "off" : "system" },
        { NULL, 1, "0", "off" },
        { "0", 0, "1", "off" },
        { "1", 0, "1", "system" },
        { NULL, 0, "1", virtual_machine ? "off" : "busy" },
        { NULL, 1, "1", "off" },
    };
    bool paged = made && (pages_saved == NULL || pages_copy != NULL) && (busy_saved == NULL || busy_copy != NULL) && pages_own >= 0;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(pages_cases); c += 1)
    {
        paged = paged && prctl(PR_SET_THP_DISABLE, pages_cases[c].inherited, 0, 0, 0) == 0 && setenv("BUSTER_TEST_HOST_BUSY", pages_cases[c].busy, 1) == 0 &&
                (pages_cases[c].setting != NULL ? setenv("BUSTER_HUGE_PAGES", pages_cases[c].setting, 1) : unsetenv("BUSTER_HUGE_PAGES")) == 0;
        const int pages_status = paged ? check_test_driver_spawn(syntax, &text) : -1;
        char expected[64];
        snprintf(expected, sizeof(expected), "\ntime-process\thuge-pages\t%s\n", pages_cases[c].report);
        paged = pages_status == 0 && text != NULL && strstr(text, expected) != NULL;
        if (!paged)
        {
            printf("  huge pages, case %u: status %d, expected%s", c, pages_status, expected);
        }
        free(text);
        text = NULL;
    }
    // A busy host's decision is the arenas' alone: a program that the compiler starts, as time-runs times one, has huge
    // pages as the compiler's parent had them (#1561's review), where the virtual machine's rule turns them off for the
    // compiler and what it starts. The program reads its own setting (/proc/self/status' THP_enabled).
    char thp_path[256];
    char thp_script[400];
    snprintf(thp_path, sizeof(thp_path), "%s/time_stages_thp.txt", directory);
    snprintf(thp_script, sizeof(thp_script), "grep THP_enabled /proc/self/status > '%s'", thp_path);
    char* const timed_child[] = { compiler, "time-runs", "--runs", "1", "--executable", "/bin/sh", "--list", "-c", thp_script, NULL };
    unlink(thp_path);
    paged = paged && prctl(PR_SET_THP_DISABLE, 0, 0, 0, 0) == 0 && setenv("BUSTER_TEST_HOST_BUSY", "1", 1) == 0 && unsetenv("BUSTER_HUGE_PAGES") == 0;
    const int child_status = paged ? check_test_driver_spawn_stream(timed_child, &text, true) : -1;
    free(text);
    text = NULL;
    char thp_text[64] = { 0 };
    FILE* thp_file = child_status == 0 ? fopen(thp_path, "r") : NULL;
    const size_t thp_length = thp_file != NULL ? fread(thp_text, 1, sizeof(thp_text) - 1, thp_file) : 0;
    if (thp_file != NULL)
    {
        fclose(thp_file);
    }
    const char* thp_expected = virtual_machine ? "THP_enabled:\t0\n" : "THP_enabled:\t1\n";
    const bool inherited_right = thp_length != 0 && strcmp(thp_text, thp_expected) == 0;
    if (!inherited_right)
    {
        printf("  huge pages: the program time-runs started (status %d) reported \"%s\", where %s was expected\n", child_status, thp_text, thp_expected);
    }
    paged = paged && child_status == 0 && inherited_right;
    unlink(thp_path);
    // So does a program interpret-c runs in the compiler's own process (#1561's review): it reads its THP_enabled as a
    // program its parent started would, but where BUSTER_HUGE_PAGES or the virtual machine's rule decides.
    static const char thp_program[] = "#include <stdio.h>\n"
                                      "#include <string.h>\n"
                                      "int main(void)\n"
                                      "{\n"
                                      "    FILE* file = fopen(\"/proc/self/status\", \"r\");\n"
                                      "    char line[256];\n"
                                      "    int found = 0;\n"
                                      "    while (file != 0 && fgets(line, sizeof(line), file) != 0)\n"
                                      "    {\n"
                                      "        if (strncmp(line, \"THP_enabled:\", 12) == 0)\n"
                                      "        {\n"
                                      "            fputs(line, stdout);\n"
                                      "            found = 1;\n"
                                      "        }\n"
                                      "    }\n"
                                      "    if (file != 0)\n"
                                      "    {\n"
                                      "        fclose(file);\n"
                                      "    }\n"
                                      "    return found ? 0 : 1;\n"
                                      "}\n";
    char thp_source[256];
    snprintf(thp_source, sizeof(thp_source), "%s/time_stages_thp.c", directory);
    char* const interpreted[] = { compiler, "interpret-c", thp_source, NULL };
    const bool thp_written = parse_test_write(thp_source, thp_program, sizeof(thp_program) - 1);
    // The variable unset, then "1", with the busy test forced and the parent's huge pages on.
    const char* interpreted_settings[] = { NULL, "1" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(interpreted_settings); i += 1)
    {
        paged = paged && thp_written &&
                (interpreted_settings[i] != NULL ? setenv("BUSTER_HUGE_PAGES", interpreted_settings[i], 1) : unsetenv("BUSTER_HUGE_PAGES")) == 0;
        const int interpreted_status = paged ? check_test_driver_spawn_stream(interpreted, &text, true) : -1;
        const char* interpreted_expected = virtual_machine && interpreted_settings[i] == NULL ? "THP_enabled:\t0\n" : "THP_enabled:\t1\n";
        const bool interpreted_right = interpreted_status == 0 && text != NULL && strstr(text, interpreted_expected) != NULL;
        if (!interpreted_right)
        {
            printf("  huge pages: interpret-c's program, BUSTER_HUGE_PAGES %s (status %d), printed \"%s\", where %s was expected\n",
                   interpreted_settings[i] != NULL ? interpreted_settings[i] : "unset", interpreted_status, text != NULL ? text : "", interpreted_expected);
        }
        paged = paged && interpreted_right;
        free(text);
        text = NULL;
    }
    unlink(thp_source);
    paged = (pages_own < 0 || prctl(PR_SET_THP_DISABLE, pages_own, 0, 0, 0) == 0) && paged;
    paged = (pages_copy != NULL ? setenv("BUSTER_HUGE_PAGES", pages_copy, 1) == 0 : unsetenv("BUSTER_HUGE_PAGES") == 0) && paged;
    paged = (busy_copy != NULL ? setenv("BUSTER_TEST_HOST_BUSY", busy_copy, 1) == 0 : unsetenv("BUSTER_TEST_HOST_BUSY") == 0) && paged;
    free(pages_copy);
    free(busy_copy);
    printf("  huge pages: %s\n", virtual_machine ? "a virtual machine (CPUID's hypervisor bit): off without BUSTER_HUGE_PAGES, by the child's own test" :
                                                   "not a virtual machine: as the parent had them without BUSTER_HUGE_PAGES, the arenas' alone off on a busy host");
    check_test_expect(test, paged, virtual_machine ? "BUSTER_HUGE_PAGES=0 turns huge pages off for the process, 1 leaves them as the system has them, and "
                                                     "on this virtual machine they are off without it, for the programs it starts or interprets too" :
                                                     "BUSTER_HUGE_PAGES=0 turns huge pages off for the process, 1 leaves them as the system has them, and "
                                                     "on this machine, not virtual, they are as the process's parent had them without it; a busy host turns "
                                                     "them off for the arenas alone, not for the programs the compiler starts or interprets");
    // measure-compile on the same source, with this compiler as two references (#146: the second with an option of its
    // own), with a reference that fails, and with a reference's option and no reference.
    char* const measured[] = { compiler, "measure-compile", "--runs", "2", "--directory", (char*)directory, "--reference", compiler, "--reference", compiler,
                               "--reference-option", "-c", "--", "-c", source, NULL };
    char* const failed[] = { compiler, "measure-compile", "--runs", "1", "--directory", (char*)directory, "--reference", "/bin/false", "--", "-c", source, NULL };
    char* const unreferenced[] = { compiler, "measure-compile", "--runs", "1", "--directory", (char*)directory, "--reference-option", "-c", "--", "-c", source, NULL };
    const int measured_status = made ? check_test_driver_spawn_stream(measured, &text, true) : -1;
    const bool qualified = text != NULL && strstr(text, "stage system time: low-confidence getrusage(RUSAGE_SELF) deltas between resource marks, -1 when unavailable") != NULL;
    // Each reference against the default, by its name, and every compile's and stage's work per token.
    const char* base = strrchr(compiler, '/') != NULL ? strrchr(compiler, '/') + 1 : compiler;
    char against[2][PATH_MAX + 64];
    snprintf(against[0], sizeof(against[0]), "\ndefault against %s: ", base);
    snprintf(against[1], sizeof(against[1]), "\ndefault against %s -c: ", base);
    const bool referenced = text != NULL && strstr(text, against[0]) != NULL && strstr(text, against[1]) != NULL &&
                            strstr(text, "\nper token: ") != NULL && strstr(text, "\ncycles per token: TSC ticks at ") != NULL &&
                            strstr(text, " TSC ticks and ") != NULL;
    // The reader's expressions' walk (#1038), under its stage, and its parts' and usage's rows under it; and the lowering's
    // coverage (#1048), the first report's `lowering-nodes` line, once.
    bool walked = text != NULL && strstr(text, "\n    check-input.expressions ") != NULL && strstr(text, "\n      expressions.usage ") != NULL;
    for (uint32_t p = 0; walked && p < BUSTER_CHECK_CLOCK_WALK_PARTS; p += 1)
    {
        char row[64];
        snprintf(row, sizeof(row), "\n      expressions.%s ", check_clock_walk_parts[p]);
        walked = strstr(text, row) != NULL;
    }
    const char* covered = text != NULL ? strstr(text, "\nlowering coverage: lowering-nodes\tevaluated=") : NULL;
    const bool coverage_once = covered != NULL && strstr(covered + 1, "\nlowering coverage: ") == NULL;
    free(text);
    text = NULL;
    const int failed_status = made ? check_test_driver_spawn(failed, &text) : -1;
    free(text);
    text = NULL;
    const int unreferenced_status = made ? check_test_driver_spawn(unreferenced, &text) : -1;
    free(text);
    check_test_expect(test, measured_status == 0 && failed_status == 1, "measure-compile times the compiles, and fails when one fails");
    check_test_expect(test, referenced, "measure-compile compares the default with each of several references, and gives the work per token");
    check_test_expect(test, unreferenced_status == 1, "measure-compile refuses a reference's option without a reference");
    check_test_expect(test, qualified, "measure-compile qualifies the stage system-time attribution");
    check_test_expect(test, walked, "measure-compile reports the reader's expressions' walk, its parts and its usage under its stage");
    check_test_expect(test, coverage_once, "measure-compile reports the lowering's coverage once");
    const char* const files[] = { source, plain, timed, linked, debug_source, one_lane, two_lanes };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
}

// Exact-capacity warning sinks end at an unreadable page. Scalar scans also visit the non-warning tokens after
// their last output, which must never write a speculative token beyond the sink's logical extent.
static void check_test_driver_warning_order(CheckTest* test)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "warning-order exact-capacity guard pages are available");
    if (ready)
    {
        uint8_t seen[200] = { 0 };
        uint32_t* tokens = (uint32_t*)(memory + page) - 2;
        bool same = true;
        for (uint32_t count = 2; count <= BUSTER_ARRAY_LENGTH(seen); count += 1)
        {
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                memset(seen, 0, sizeof(seen));
                seen[0] = 2;
                seen[1] = 1;
                tokens[0] = 0;
                tokens[1] = UINT32_MAX;
                CheckInitWarningSink sink = { .tokens = tokens, .capacity = 2, .count = 2 };
                CheckInitWarningCollector collector = { .sink = &sink, .seen = seen, .token_count = count };
                check_init_warning_order(&collector, 1, scalar != 0);
                same &= sink.count == 2 && tokens[0] == 0 && tokens[1] == 1;
            }
        }
        check_test_expect(test, same, "warning ordering preserves prior entries and never writes beyond an exactly full sink on either path");
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, page * 2);
    }
}

static void check_test_driver_diagnostic_capacity(CheckTest* test)
{
    Arena scratch = arena_reserve(64);
    const uint64_t capacity = scratch.capacity;
    uint64_t* sentinel = BUSTER_ARENA_PUSH_ARRAY(&scratch, uint64_t, 1);
    check_test_expect(test, sentinel != NULL, "diagnostic rollback fixture storage is available");
    if (sentinel != NULL)
    {
        *sentinel = UINT64_C(0x73516A93);
        const uint64_t mark = scratch.length;
        Parser parser = { 0 };
        parser.tokens.count = 1;
        CheckInput input = { .report_count = 1 };
        Preprocessor pp = { 0 };
        for (uint32_t phase = 0; phase < 2; phase += 1)
        {
            scratch.capacity = mark + phase * 2 * sizeof(uint64_t);
            const bool ready = check_driver_reports(&parser, &parser, &pp, pp.output.count, &input, NULL, NULL, NULL, NULL, NULL, 0, NULL, 0, false, NULL, &scratch);
            check_test_expect(test, !ready && scratch.length == mark && *sentinel == UINT64_C(0x73516A93) && pp.errors == 0,
                              "diagnostic allocation failure restores scratch and publishes no partial diagnostics");
        }
    }
    scratch.capacity = capacity;
    arena_release(&scratch);
}

// The reports' walk before #592, every token in turn with a dense column of each token's warnings and of its first report:
// check_test_driver_reports' oracle for check_driver_reports, which visits only the tokens that have any.
typedef struct CheckTestDriverWarning CheckTestDriverWarning;
struct CheckTestDriverWarning
{
    uint16_t flags;
    uint16_t enabled;
};

static bool check_test_driver_reports_dense(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                             const TypeTable* types, const CheckRepair* repair, const CheckInput* original_pragmas, const CheckDriverDiagnostic* extra, uint32_t extra_count,
                                             const uint32_t* excess_tokens, uint32_t excess_count, bool quiet, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = parser->tokens.count;
    CheckWarningSpelling spell = { 0 };
    const bool reports = input->report_count != 0 || (stream != 0 && stream->report_count != 0) || extra_count != 0;
    uint64_t* first = reports ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : NULL;
    const bool show_warnings = !quiet;
    CheckTestDriverWarning* warnings = show_warnings ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckTestDriverWarning, (uint64_t)count + 1) : NULL;
    const bool ready = (!reports || first != NULL) && (!show_warnings || warnings != NULL);
    if (ready)
    {
        for (uint32_t token = 0; reports && token <= count; token += 1)
        {
            first[token] = UINT64_MAX;
        }
        if (show_warnings)
        {
            memset(warnings, 0, ((uint64_t)count + 1) * sizeof(*warnings));
            for (uint32_t token = 0; token < count && token < input->token_count; token += 1)
            {
                const uint32_t flags = check_diagnostic_attribute_warnings(input, token);
                warnings[token].flags = flags;
                warnings[token].enabled = flags;
            }
            for (uint32_t token = 0; input->constant_warnings != NULL && token < count && token < input->token_count; token += 1)
            {
                const uint8_t flags = input->constant_warnings[token];
                const uint8_t ignored = input->constant_warning_ignored != NULL ? input->constant_warning_ignored[token] : 0;
                warnings[token].flags |= flags;
                warnings[token].enabled |= flags & ~ignored;
            }
            for (uint32_t node = 0; stream != NULL && node < stream->count; node += 1)
            {
                const uint32_t token = input->graph.tokens[node];
                if (token < count)
                {
                    // A folded non-ICE expression's warning is at the expression's first token (check_driver_reports).
                    const uint32_t folded = stream->warnings[node] & CHECK_WARNING_GNU_CONSTANT;
                    const uint32_t flags = stream->warnings[node] & ~folded;
                    const uint32_t ignored = stream->warning_ignored != NULL ? stream->warning_ignored[node] : 0;
                    const uint32_t begin = folded != 0 ? check_common_subtree_start(parser, token) : token;
                    warnings[token].flags |= flags;
                    warnings[token].enabled |= flags & ~ignored;
                    warnings[begin].flags |= folded;
                    warnings[begin].enabled |= folded & ~ignored;
                }
            }
            for (uint32_t i = 0; i < excess_count; i += 1)
            {
                if (excess_tokens[i] < count)
                {
                    warnings[excess_tokens[i]].flags |= CHECK_WARNING_EXCESS_INITIALIZERS;
                    warnings[excess_tokens[i]].enabled |= CHECK_WARNING_EXCESS_INITIALIZERS;
                }
            }
        }
        for (uint32_t i = 0; i < input->report_count; i += 1)
        {
            const uint32_t token = input->reports[i].token < count ? input->reports[i].token : count;
            if (first[token] == UINT64_MAX && (repair == NULL || check_repair_input_report(repair, input->reports + i) || check_repair_loop_report(repair, input, input->reports + i)))
            {
                first[token] = i;
            }
        }
        for (uint32_t i = 0; stream != 0 && i < stream->report_count; i += 1)
        {
            const uint32_t token = stream->reports[i].token < count ? stream->reports[i].token : count;
            if (first[token] == UINT64_MAX && (repair == NULL || check_repair_graph_report(repair, &input->graph, stream->reports + i)))
            {
                first[token] = (UINT64_C(1) << 32) | i;
            }
        }
        for (uint32_t i = 0; i < extra_count; i += 1)
        {
            const uint32_t token = extra[i].position < count ? extra[i].position : count;
            if (first[token] == UINT64_MAX)
            {
                first[token] = (UINT64_C(2) << 32) | i;
            }
        }
        const CheckInput* pragma_input = original_pragmas != NULL ? original_pragmas : input;
        uint32_t pragma = 0;
        uint32_t packed = 0;
        for (uint32_t token = 0; !pp->fatal && token <= count; token += 1)
        {
            while (!pp->fatal && pragma < pragma_input->fp_pragma_count && pragma_input->fp_pragmas[pragma].token == token)
            {
                const CheckInputFpPragma* record = pragma_input->fp_pragmas + pragma;
                const bool retained = original_pragmas != NULL || repair == NULL || token < check_repair_pragma_end(repair);
                if (retained)
                {
                    check_driver_pragma_report(parser, pp, pragma_input, record, quiet);
                }
                pragma += 1;
            }
            uint32_t flags = show_warnings && (repair == NULL || check_repair_warning_token(repair, token)) ?
                             warnings[token].flags : 0;
            const uint32_t pointer_flags = flags & (CHECK_WARNING_POINTER_SIGN | CHECK_WARNING_DISCARDED_QUALIFIERS);
            if (pointer_flags != 0 && check_driver_pointer_arguments(parser, pp, input, stream, types, &spell, token, pointer_flags,
                warnings[token].enabled, parser->tokens.sources[token], PP_DIAGNOSTIC_SEMANTIC, scratch))
            {
                flags &= ~pointer_flags;
            }
            while (!pp->fatal && flags != 0)
            {
                const uint32_t warning = flags & (0u - flags);
                const bool ignored = (warnings[token].enabled & warning) == 0;
                const CheckWarningSeverity severity = check_driver_legacy_severity(pp, input, token, warning, ignored);
                if (severity != CHECK_WARNING_IGNORE)
                {
                    const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_FATAL :
                                                   severity == CHECK_WARNING_ERROR ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING;
                    const uint32_t index = check_diagnostic_warning_index(warning);
                    const char* option = index < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions[index].option : NULL;
                    const bool promoted = severity >= CHECK_WARNING_ERROR;
                    const char* suffix = option != NULL ? check_warning_text(scratch, " [%s%s]", promoted ? "-Werror," : "", option) :
                                         promoted ? " [-Werror]" : "";
                    uint32_t located = token;
                    const char* message = warning == CHECK_WARNING_POINTER_SIGN || warning == CHECK_WARNING_DISCARDED_QUALIFIERS ?
                                          check_driver_pointer_conversion_message(parser, pp, input, stream, types, &spell, token, warning, &located, scratch) :
                                          (warning & (CHECK_WARNING_SHIFT_NEGATIVE_VALUE | CHECK_WARNING_SHIFT_OVERFLOW)) != 0 ?
                                          check_driver_shift_message(parser, pp, input, stream, types, &spell, token, warning, scratch) :
                                          check_diagnostic_message(warning);
                    if (suffix != NULL)
                    {
                        pp_diagnose_output(pp, pp_severity, parser->tokens.sources[located], "%s%s", message, suffix);
                    }
                }
                flags &= flags - 1;
            }
            // Addresses are sparse and ordered by the '&' event. A generic selection can put the diagnosed
            // member after another address in its control expression, so its location is not the iteration key.
            // Keep these records out of the dense 16-bit warning columns.
            while (!pp->fatal && packed < input->packed_member_count && input->packed_members[packed].address <= token)
            {
                const CheckInputPackedMember* member = input->packed_members + packed;
                if (show_warnings && !member->suppressed && member->address == token &&
                    (repair == NULL || check_repair_warning_token(repair, member->token)))
                {
                    check_driver_packed_report(parser, pp, input, member);
                }
                packed += 1;
            }
            if (!pp->fatal && reports && first[token] != UINT64_MAX)
            {
                const uint32_t index = (uint32_t)first[token];
                const uint32_t phase = (uint32_t)(first[token] >> 32);
                // Share the text renderer, not the sparse walk's selection or ordering. The exact semantic
                // catalogue separately compares this spelling and its caret against Clang (#1107).
                uint32_t location = phase == 2 ? extra[index].token : token;
                PPOutputDiagnosticNote notes[BUSTER_CHECK_SEMANTIC_NOTES] = { 0 };
                const char* message = phase == 2 ? extra[index].message :
                                      phase == 1 ? check_semantic_expression(parser, pp, input, stream, types, stream->reports + index, &spell, &location, notes, scratch) :
                                                   check_semantic_input(parser, pp, input, types, input->reports + index, &spell, scratch);
                const uint32_t source = phase == 0 ? check_diagnostic_input_report_source(input, parser, input->reports + index) : BUSTER_CHECK_NONE;
                if (phase == 0 && input->reports[index].message == CHECK_INPUT_UNDECLARED && token < count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER)
                {
                    const uint32_t name = parser->tokens.names[token];
                    pp_diagnose_output(pp, PP_SEVERITY_ERROR, source, "use of undeclared identifier '%.*s'",
                                       (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                }
                else if (source != BUSTER_CHECK_NONE)
                {
                    pp_diagnose_output(pp, PP_SEVERITY_ERROR, source, "%s", message);
                }
                else
                {
                    check_driver_diagnose(parser, pp, location, message);
                }
            }
        }
        // A directive in an unfinished expression can lie beyond every safely checked prefix. Its original
        // source spelling and warning policy are independent of that expression and precede the parser failure.
        for (; !pp->fatal && original_pragmas != NULL && pragma < pragma_input->fp_pragma_count; pragma += 1)
        {
            check_driver_pragma_report(parser, pp, pragma_input, pragma_input->fp_pragmas + pragma, quiet);
        }
    }
    arena_pop_to(scratch, mark);
    return ready;
}


static uint64_t check_test_driver_random(uint64_t* state)
{
    *state = *state * 6364136223846793005u + 1442695040888963407u;
    return *state >> 33;
}

// A token of `count` at random, or with `past` sometimes one past the last or further.
static uint32_t check_test_driver_random_token(uint64_t* state, uint32_t count, bool past)
{
    const uint32_t random = (uint32_t)check_test_driver_random(state);
    return past && random % 16 == 0 ? count + random % 3 : random % (count != 0 ? count : 1);
}

// check_driver_reports against the dense walk above, on both paths, into memory: units with warnings of each kind (an
// integer constant's overflow, a pointer's sign, excess elements, an unknown floating-point pragma state, a packed
// member's address, and promoted to an error and to a fatal error), each reported as checked and then with its sparse
// records rewritten at random: the constants' and the nodes' warnings at random tokens and nodes, the floating-point
// pragmas and the packed members' addresses in token order, out of it and past the last token, with the original
// pragmas apart, the reader's and the expressions' reports and the driver's own diagnostics, and the excess elements,
// quiet and not. The texts, the counts of warnings and errors, the fatal flag and the outcome must be the oracle's.
static void check_test_driver_reports(CheckTest* test)
{
    char wide_source[8192];
    uint32_t wide_length = 0;
    for (uint32_t token = 0; token < 256; token += 1)
    {
        wide_length += (uint32_t)snprintf(wide_source + wide_length, sizeof(wide_source) - wide_length, "int padding%u;\n", token);
    }
    snprintf(wide_source + wide_length, sizeof(wide_source) - wide_length,
             "enum { W = 2147483647 + 1 }; int wide[1] = { 1, 2 }; int f(int x) { return x << 40; }\n");
    const char* const sources[] = {
        "#pragma STDC FENV_ACCESS MAYBE\n"
        "enum { A = 2147483647 + 1 };\n"
        "struct __attribute__((packed)) P { char c; int i; };\n"
        "struct P s;\n"
        "int a[1] = { 1, 2 };\n"
        "int* f(void) { return &s.i; }\n"
        "void g(char* c) { unsigned char* u = c; (void)u; }\n"
        "int h(int x) { return x << 40; }\n",
        "#pragma clang diagnostic error \"-Winteger-overflow\"\n"
        "enum { B = 2147483647 + 1 };\n"
        "#pragma clang diagnostic fatal \"-Wpointer-sign\"\n"
        "void g(char* c) { unsigned char* u = c; (void)u; }\n"
        "int b[2] = { 1, 2, 3 };\n",
        wide_source,
        "int f(void) { return 1; }\n",
        "int f(void) { return 1; } int f(void) __asm__(\"late_name\");\n",
    };
    uint64_t state = 0x592;
    uint32_t compared = 0;
    bool all = true;
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(sources); s += 1)
    {
        InternTable identifiers = { 0 };
        Preprocessor pp = { 0 };
        Parser parser = { 0 };
        const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
        const ParseOptions parsing = { .extension_metadata = true };
        CheckTestTypes types = { 0 };
        Arena retained = arena_reserve(UINT64_C(1) << 28);
        Arena scratch = arena_reserve(UINT64_C(1) << 28);
        Arena records = arena_reserve(UINT64_C(1) << 24);
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        const CheckOptions checking = { 0 };
        const bool ready = retained.base != 0 && scratch.base != 0 && records.base != 0 && check_test_types_open(&types) &&
                           intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                           parse_initialize(&parser, &parsing, preprocessing.language) &&
                           pp_run_source(&pp, "reports.c", (const uint8_t*)sources[s], strlen(sources[s])) && parse_translation_unit(&parser, &pp) &&
                           check_input(&input, &parser, &pp, &types.table, &retained, &scratch) &&
                           check_graph_run(&types.table, &input.graph, &retained, &scratch, &checking, &stream);
        check_test_expect(test, ready && parser.tokens.count != 0 && stream.count != 0, "a unit with warnings checks, for the reports' walk");
        const uint32_t count = ready ? parser.tokens.count : 0;
        if (s == 2)
        {
            check_test_expect(test, count > 256, "the reports' ordering oracle exercises multi-byte token keys");
        }
        const CheckInput checked = input;
        const CheckStream streamed = stream;
        uint32_t warning_rows = 0;
        uint32_t attribute_rows = 0;
        uint32_t constant_rows = 0;
        for (uint32_t node = 0; ready && node < stream.count; node += 1)
        {
            warning_rows += stream.warnings[node] != 0;
        }
        for (uint32_t token = 0; ready && token < count; token += 1)
        {
            attribute_rows += check_diagnostic_attribute_warnings(&input, token) != 0;
            constant_rows += input.constant_warnings[token] != 0;
        }
        check_test_expect(test, ready && stream.warnings_prepared && stream.warning_count == warning_rows && input.asm_warnings_prepared &&
                               (input.ignored_asm_count != 0) == (attribute_rows != 0) && input.constant_warnings_prepared &&
                               input.constant_warning_count == constant_rows,
                          "warning producers publish exact node/constant counts and ignored-asm presence for report collection");
        const uint32_t errors = pp.errors;
        const uint32_t warnings = pp.warnings;
        for (uint32_t round = 0; ready && round < 160; round += 1)
        {
            arena_pop_to(&records, 0);
            input = checked;
            stream = streamed;
            CheckInput pragmas = checked;
            CheckDriverDiagnostic extra[3] = { 0 };
            uint32_t extra_count = 0;
            uint32_t* excess = NULL;
            uint32_t excess_count = 0;
            const bool quiet = round % 5 == 4;
            const bool apart = round % 3 == 2;
            // Round 0 reports each unit as checked; the others rewrite its sparse records.
            if (round != 0)
            {
                const uint32_t density = (uint32_t)check_test_driver_random(&state) % 4;
                uint8_t* constants = BUSTER_ARENA_PUSH_ARRAY(&records, uint8_t, count);
                uint8_t* constants_ignored = BUSTER_ARENA_PUSH_ARRAY(&records, uint8_t, count);
                uint32_t* node_warnings = BUSTER_ARENA_PUSH_ARRAY(&records, uint32_t, stream.count);
                uint32_t* node_ignored = BUSTER_ARENA_PUSH_ARRAY(&records, uint32_t, stream.count);
                for (uint32_t t = 0; constants != NULL && constants_ignored != NULL && t < count; t += 1)
                {
                    const uint32_t random = (uint32_t)check_test_driver_random(&state);
                    const bool set = density != 0 && random % (density == 1 ? 64 : density == 2 ? 8 : 2) == 0;
                    constants[t] = set ? (uint8_t)(random >> 8) : 0;
                    constants_ignored[t] = set ? (uint8_t)(random >> 16) : 0;
                }
                for (uint32_t n = 0; node_warnings != NULL && node_ignored != NULL && n < stream.count; n += 1)
                {
                    const uint32_t random = (uint32_t)check_test_driver_random(&state);
                    const bool set = density != 0 && random % (density == 1 ? 64 : density == 2 ? 8 : 2) == 0;
                    // Bits from 16 up, which the per-token warnings don't keep, as well as the kept ones.
                    node_warnings[n] = set ? (random >> 4) & 0x3ffff : 0;
                    node_ignored[n] = set ? (random >> 10) & 0x3ffff : 0;
                }
                input.constant_warnings = constants;
                input.constant_warning_ignored = check_test_driver_random(&state) % 2 == 0 ? constants_ignored : NULL;
                input.constant_warnings_prepared = round % 2 == 0;
                input.constant_warning_count = 0;
                for (uint32_t token = 0; token < count; token += 1)
                {
                    input.constant_warning_count += constants[token] != 0;
                }
                stream.warnings = node_warnings;
                stream.warning_ignored = check_test_driver_random(&state) % 2 == 0 ? node_ignored : NULL;
                // Alternate completed producer metadata with hand-built streams, whose collector retains the dense fallback.
                stream.warnings_prepared = round % 2 == 0;
                stream.warning_count = 0;
                for (uint32_t node = 0; node < stream.count; node += 1)
                {
                    stream.warning_count += node_warnings[node] != 0;
                }
                const uint32_t pragma_count = (uint32_t)check_test_driver_random(&state) % 6;
                CheckInputFpPragma* fp = pragma_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, CheckInputFpPragma, pragma_count) : NULL;
                for (uint32_t p = 0; fp != NULL && p < pragma_count; p += 1)
                {
                    const uint32_t random = (uint32_t)check_test_driver_random(&state);
                    const CheckInputFpPragma* model = checked.fp_pragma_count != 0 ? checked.fp_pragmas + random % checked.fp_pragma_count : NULL;
                    fp[p] = model != NULL ? *model : (CheckInputFpPragma){ .source_begin = 0, .source_end = 6 };
                    fp[p].token = check_test_driver_random_token(&state, count, true);
                    fp[p].warnings = (uint8_t)((random >> 4) & (CHECK_FP_PRAGMA_INVALID_PLACEMENT | CHECK_FP_PRAGMA_UNKNOWN_STATE));
                }
                // In token order, but now and then out of it.
                for (uint32_t p = 1; fp != NULL && p < pragma_count; p += 1)
                {
                    for (uint32_t q = p; q > 0 && fp[q - 1].token > fp[q].token && check_test_driver_random(&state) % 8 != 0; q -= 1)
                    {
                        const CheckInputFpPragma swap = fp[q - 1];
                        fp[q - 1] = fp[q];
                        fp[q] = swap;
                    }
                }
                (apart ? &pragmas : &input)->fp_pragmas = fp;
                (apart ? &pragmas : &input)->fp_pragma_count = pragma_count;
                const uint32_t packed_count = (uint32_t)check_test_driver_random(&state) % 6;
                CheckInputPackedMember* packed = packed_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, CheckInputPackedMember, packed_count) : NULL;
                for (uint32_t p = 0; packed != NULL && p < packed_count; p += 1)
                {
                    packed[p] = (CheckInputPackedMember){
                        .address = check_test_driver_random_token(&state, count, true),
                        .token = check_test_driver_random_token(&state, count, false),
                        .member_name = BUSTER_S8("i"),
                        .record_name = BUSTER_S8("P"),
                        .suppressed = check_test_driver_random(&state) % 4 == 0,
                    };
                }
                for (uint32_t p = 1; packed != NULL && p < packed_count; p += 1)
                {
                    for (uint32_t q = p; q > 0 && packed[q - 1].address > packed[q].address && check_test_driver_random(&state) % 8 != 0; q -= 1)
                    {
                        const CheckInputPackedMember swap = packed[q - 1];
                        packed[q - 1] = packed[q];
                        packed[q] = swap;
                    }
                }
                input.packed_members = packed;
                input.packed_member_count = packed_count;
                const uint32_t input_reports = (uint32_t)check_test_driver_random(&state) % 4;
                CheckInputReport* reports = input_reports != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, CheckInputReport, input_reports) : NULL;
                for (uint32_t r = 0; reports != NULL && r < input_reports; r += 1)
                {
                    reports[r] = (CheckInputReport){
                        .token = check_test_driver_random_token(&state, count, true),
                        .message = (CheckInputMessage)(check_test_driver_random(&state) % 10),
                    };
                }
                input.reports = reports;
                input.report_count = input_reports;
                const uint32_t stream_reports = (uint32_t)check_test_driver_random(&state) % 4;
                CheckReport* graph_reports = stream_reports != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, CheckReport, stream_reports) : NULL;
                for (uint32_t r = 0; graph_reports != NULL && r < stream_reports; r += 1)
                {
                    graph_reports[r] = (CheckReport){
                        .token = check_test_driver_random_token(&state, count, true),
                        .message = (CheckMessage)(CHECK_MESSAGE_ICE + check_test_driver_random(&state) % 3),
                    };
                }
                stream.reports = graph_reports;
                stream.report_count = stream_reports;
                extra_count = (uint32_t)check_test_driver_random(&state) % 4;
                for (uint32_t e = 0; e < extra_count; e += 1)
                {
                    extra[e] = (CheckDriverDiagnostic){ check_test_driver_random_token(&state, count, false), "driver's own diagnostic",
                                                        check_test_driver_random_token(&state, count, true) };
                }
                excess_count = (uint32_t)check_test_driver_random(&state) % 5;
                excess = excess_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(&records, uint32_t, excess_count) : NULL;
                for (uint32_t e = 0; excess != NULL && e < excess_count; e += 1)
                {
                    excess[e] = check_test_driver_random_token(&state, count, true);
                }
            }
            char* texts[3] = { 0 };
            size_t sizes[3] = { 0 };
            bool outcomes[3] = { 0 };
            uint32_t counted[3][2] = { { 0 } };
            bool fatal[3] = { 0 };
            for (uint32_t variant = 0; variant < 3; variant += 1)
            {
                pp.errors = errors;
                pp.warnings = warnings;
                pp.fatal = false;
                parser.options.scalar = variant == 2;
                FILE* memory = open_memstream(&texts[variant], &sizes[variant]);
                pp.options.diagnostics = memory;
                const CheckInput* original = apart ? &pragmas : NULL;
                outcomes[variant] = memory != NULL &&
                                    (variant == 0 ? check_test_driver_reports_dense(&parser, &pp, &input, &stream, &types.table, NULL, original, extra, extra_count, excess, excess_count, quiet, &scratch) :
                                                    check_driver_reports(&parser, &parser, &pp, pp.output.count, &input, &stream, &types.table, NULL, original, extra, extra_count, excess, excess_count, quiet, NULL, &scratch));
                pp.options.diagnostics = NULL;
                if (memory != NULL)
                {
                    fclose(memory);
                }
                counted[variant][0] = pp.errors;
                counted[variant][1] = pp.warnings;
                fatal[variant] = pp.fatal;
            }
            for (uint32_t variant = 1; variant < 3; variant += 1)
            {
                const bool same = texts[0] != NULL && texts[variant] != NULL && outcomes[variant] == outcomes[0] && sizes[variant] == sizes[0] &&
                                  memcmp(texts[variant], texts[0], sizes[0]) == 0 && counted[variant][0] == counted[0][0] &&
                                  counted[variant][1] == counted[0][1] && fatal[variant] == fatal[0];
                all &= same;
                compared += 1;
                if (!same)
                {
                    printf("  reports' walk: source %u round %u %s path differs from the dense walk (%zu bytes against %zu)\n", s, round,
                           variant == 2 ? "scalar" : "vector", sizes[variant], sizes[0]);
                }
            }
            for (uint32_t variant = 0; variant < 3; variant += 1)
            {
                free(texts[variant]);
            }
        }
        parser.options.scalar = false;
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        check_test_types_close(&types);
        arena_release(&records);
        arena_release(&scratch);
        arena_release(&retained);
    }
    printf("  reports' walk: %u comparisons with the dense walk\n", compared);
    check_test_expect(test, all && compared != 0, "the reports' walk gives the dense walk's diagnostics, in its order, by both paths");
}

static bool check_test_driver_files_equal(const char* first, const char* second)
{
    FILE* a = fopen(first, "rb");
    FILE* b = fopen(second, "rb");
    bool equal = a != NULL && b != NULL;
    bool complete = false;
    while (equal && !complete)
    {
        unsigned char left[4096];
        unsigned char right[4096];
        const size_t count_a = fread(left, 1, sizeof(left), a);
        const size_t count_b = fread(right, 1, sizeof(right), b);
        equal = count_a == count_b && memcmp(left, right, count_a) == 0 && !ferror(a) && !ferror(b);
        complete = count_a < sizeof(left);
    }
    if (a != NULL)
    {
        equal = fclose(a) == 0 && equal;
    }
    if (b != NULL)
    {
        equal = fclose(b) == 0 && equal;
    }
    return equal;
}

static void check_test_driver_lazy(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    const struct
    {
        const char* name;
        const char* header;
        const char* source;
        bool rejected;
        bool lazy_rejected;
    } fixtures[] =
    {
        {
            "unused system static semantic error is deferred only when requested",
            "#pragma GCC system_header\nstatic int unused(void){return missing;}\n",
            "#include \"lazy.h\"\nint f(void){return 3;}\n",
            true,
            false,
        },
        {
            "referenced system static semantic error is always checked",
            "#pragma GCC system_header\nstatic int used(void){return missing;}\n",
            "#include \"lazy.h\"\nint f(void){return used();}\n",
            true,
            true,
        },
        {
            "ordinary unused static semantic error is always checked",
            "static int unused(void){return missing;}\n",
            "#include \"lazy.h\"\nint f(void){return 3;}\n",
            true,
            true,
        },
        {
            "unused system static syntax error is always parsed",
            "#pragma GCC system_header\nstatic int unused(void){int x=; return 1;}\n",
            "#include \"lazy.h\"\nint f(void){return 3;}\n",
            true,
            true,
        },
        {
            "valid referenced and unreferenced header functions have equal objects",
            "#pragma GCC system_header\nstatic int unused(void){return 99;}\nstatic int used(void){return 7;}\n",
            "#include \"lazy.h\"\nint f(void){return used();}\n",
            false,
            false,
        },
        {
            "a file-scope address retains a system body in both modes",
            "#pragma GCC system_header\nstatic int used(void){return 7;}\nstatic int unused(void){return 99;}\n",
            "#include \"lazy.h\"\nint (*pointer)(void)=used; int f(void){return 3;}\n",
            false,
            false,
        },
    };
    char header[192];
    char source[192];
    char objects[2][192];
    snprintf(header, sizeof(header), "%s/lazy.h", directory);
    snprintf(source, sizeof(source), "%s/lazy.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/eager.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/lazy.o", directory);
    const char* arguments[] = { "-std=gnu17" };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(fixtures); fixture += 1)
    {
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(header, fixtures[fixture].header, strlen(fixtures[fixture].header)) &&
                           parse_test_write(source, fixtures[fixture].source, strlen(fixtures[fixture].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        check_test_expect(test, ready && oracle.rejected == fixtures[fixture].rejected, fixtures[fixture].name);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            bool accepted[2] = { false, false };
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                unlink(objects[mode]);
                CheckCorpusOracle checked = { 0 };
                uint32_t warnings = 0;
                const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, objects[mode], (route & 1) != 0,
                                                       false, false, mode != 0, &checked, &warnings);
                const bool rejected = mode == 0 ? fixtures[fixture].rejected : fixtures[fixture].lazy_rejected;
                check_test_expect(test, ran && checked.rejected == rejected, fixtures[fixture].name);
                check_test_expect(test, rejected || access(objects[mode], R_OK) == 0, "accepted lazy-policy source publishes its object");
                check_test_expect(test, !rejected || access(objects[mode], F_OK) != 0, "rejected lazy-policy source publishes no object");
                accepted[mode] = ran && !checked.rejected;
            }
            if (!fixtures[fixture].rejected)
            {
                check_test_expect(test, accepted[0] && accepted[1] && check_test_driver_files_equal(objects[0], objects[1]),
                                  "eager and lazy modes produce byte-identical valid objects");
            }
        }
    }
    unlink(objects[0]);
    unlink(objects[1]);
    unlink(source);
    unlink(header);
}

// Named universal character names (\N{...}, #1447), which clang reads as its extension in C: in an identifier, in #if's
// character constants and in the checker's literals, each source clang accepts must be accepted by both paths and by
// the default C route when `executable` is given, writing an object. Each names a character outside the basic set, which
// C17 doesn't let a UCN name even in a literal.
static void check_test_driver_named_ucns(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const struct
    {
        const char* name;
        const char* source;
    } fixtures[] = {
        { "a named UCN in an identifier", "int \\N{LATIN SMALL LETTER E WITH ACUTE} = 1;\nint f(void) { return \xC3\xA9 + \\u00e9; }\n" },
        { "a named UCN in a later identifier", "int \xC3\xA9 = 1;\nint f(void)\n{\n    return \\N{LATIN SMALL LETTER E WITH ACUTE};\n}\n" },
        { "a named UCN in #if's character constant", "#if L'\\N{LATIN SMALL LETTER E WITH ACUTE}' == 0xE9\nint e;\n#endif\n" },
        { "a named UCN in a string literal", "const char *s = \"\\N{LATIN SMALL LETTER E WITH ACUTE}\";\n" },
        { "a named UCN in a character constant", "int c = L'\\N{LATIN SMALL LETTER E WITH ACUTE}';\n" },
        { "a named UCN in a static assertion's message", "_Static_assert(1, \"\\N{SNOWMAN}\");\n" },
    };
    const char* clang_arguments[] = { "-std=gnu17", "-w" };
    const uint32_t routes = executable != NULL ? 4 : 2;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[384];
        snprintf(source, sizeof(source), "%s/named-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/named-%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool oracle_ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                                  check_corpus_clang_file(clang, clang_arguments, 2, source, &oracle);
        snprintf(description, sizeof(description), "%s: clang accepts it", fixtures[i].name);
        check_test_expect(test, oracle_ready && !oracle.rejected, description);
        for (uint32_t route = 0; oracle_ready && route < routes; route += 1)
        {
            unlink(object);
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ready = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, false, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, %s %s: accepted (error at %u:%u)", fixtures[i].name, route >= 2 ? "default CLI" : "direct",
                     (route & 1) != 0 ? "scalar" : "vector", checked.line, checked.column);
            check_test_expect(test, ready && !checked.rejected, description);
            check_test_expect(test, access(object, F_OK) == 0, "an accepted named UCN writes an object");
        }
        unlink(object);
        unlink(source);
    }
}

// #775: code generation's check of target features, which clang makes at -c and not at -fsyntax-only. Each fixture's
// first error at -c, or none, must be clang -c's at its -march, by both paths, in this process and through the
// executable: its place, and its message where clang names the feature by the attribute's order (`message`; clang's
// order past it is its StringMap's). At -fsyntax-only each must pass, as it does clang's.
static void check_test_driver_target_features(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* march;
        const char* source;
        bool rejected;
        bool message;
        // The fixture's -std=, or null for gnu17.
        const char* standard;
    } fixtures[] = {
        { "an always_inline callee's target attribute", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\nint caller(int x) { return h(x); }\n", true, true },
        { "a caller of the callee's features", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "__attribute__((target(\"avx2,bmi2\"))) int caller(int x) { return h(x); }\n",
          false, true },
        { "an always_inline callee without an attribute", "-march=x86-64",
          "static inline __attribute__((always_inline)) int h(int x) { return x + 1; }\n__attribute__((target(\"avx2\"))) int caller(int x) { return h(x); }\n", false,
          true },
        // A target_clones body is checked against its first version's features, for every copy clang emits (#2211): no
        // error where that version has the callee's, whichever other versions lack them.
        { "a target_clones body whose first version has the callee's features", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "__attribute__((target_clones(\"avx2\", \"default\"))) int caller(int x) { return h(x); }\n",
          false, true },
        { "a target_clones body whose first version is an arch= of the callee's features", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "__attribute__((target_clones(\"arch=haswell\", \"sse4.2\", \"default\"))) int caller(int x) { return h(x); }\n",
          false, true },
        { "a target_clones body whose first version lacks the callee's features", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "__attribute__((target_clones(\"sse4.2\", \"arch=haswell\", \"default\"))) int caller(int x) { return h(x); }\n",
          true, true },
        { "a caller's attribute that turns a feature off", "-march=x86-64-v2",
          "static inline __attribute__((always_inline)) int h(int x) { return x + 1; }\n"
          "__attribute__((target(\"no-sse4.2\"))) int caller(int x) { return h(x); }\n",
          true, true },
        { "a flatten caller", "-march=x86-64",
          "__attribute__((target(\"avx2\"))) int h(int x) { return x + 1; }\n__attribute__((flatten)) int caller(int x) { return h(x); }\n", true, true },
        { "calls through the function's address", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\nint caller(int x) { return (*h)(x) + (&h)(x); }\n", false,
          true },
        { "attributes declared after the call", "-march=x86-64",
          "int h(int x);\nint caller(int x) { return h(x); }\n__attribute__((always_inline, target(\"avx2\"))) inline int h(int x) { return x + 1; }\n", false,
          true },
        { "an attribute declared before the call", "-march=x86-64",
          "__attribute__((target(\"avx2\"))) static inline __attribute__((always_inline)) int h(int x);\nint caller(int x) { return h(x); }\n"
          "static inline int h(int x) { return x + 1; }\n",
          true, true },
        { "dead branches and code", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "int caller(int x) { if (0) return h(x); return (0 && h(x)) + (1 ? x : h(x)); }\nint after(int x) { return x; return h(x); }\n",
          false, true },
        // #1108: a function that only a constant if's dead arm reaches, called there, through a block-scope static's
        // initializer there, or under a condition whose && leaves a call unevaluated (main.c's BUSTER_IN_UNIT branches),
        // is never emitted, by clang and by -c's selection, and so never checked; the live arm's call is.
        { "a function only constant ifs' dead arms reach", "-march=x86-64",
          "#include <immintrin.h>\n"
          "static int vec(const unsigned char* p)\n{\n    __m512i v = _mm512_maskz_cvtepu8_epi32(0xffff, _mm_loadu_si128((const __m128i*)p));\n"
          "    return _mm512_reduce_add_epi32(v);\n}\nint strcmp(const char* a, const char* b);\n"
          "int run(const unsigned char* p, int n)\n{\n    int r = p[0];\n    if (0)\n    {\n        r = vec(p);\n    }\n"
          "    if (0 && n >= 2 && strcmp(\"a\", \"b\") == 0)\n    {\n        r += vec(p + 1);\n    }\n"
          "    if (1)\n    {\n    }\n    else\n    {\n        static int (*pointer)(const unsigned char*) = vec;\n        r += pointer(p);\n    }\n"
          "    return r;\n}\n",
          false, true },
        { "a function a constant if's live arm reaches", "-march=x86-64",
          "#include <immintrin.h>\n"
          "static int vec(const unsigned char* p)\n{\n    __m512i v = _mm512_maskz_cvtepu8_epi32(0xffff, _mm_loadu_si128((const __m128i*)p));\n"
          "    return _mm512_reduce_add_epi32(v);\n}\n"
          "int run(const unsigned char* p)\n{\n    int r = p[0];\n    if (1)\n    {\n        r = vec(p);\n    }\n    return r;\n}\n",
          true, true },
        { "the first function to have an error", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\nstatic int s(int x) { return h(x); }\n"
          "int e1(int x) { return s(x) + h(x + 1); }\nint e2(int x) { return h(x + 2); }\n",
          true, true },
        { "a deferred function's error", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\nstatic int s(int x) { return h(x); }\n"
          "int e(int x) { return s(x); }\n",
          true, true },
        { "an argument's call first", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2,avx2\"))) int g(int x) { return x + 2; }\nint e1(int x) { return h(g(x)) + h(x); }\n",
          true, true },
        { "an arch= attribute", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"arch=haswell\"))) int h(int x) { return x + 1; }\nint caller(int x) { return h(x); }\n", true,
          false },
        { "an intrinsic of a feature the unit lacks", "-march=x86-64",
          "#include <immintrin.h>\nunsigned f(unsigned x, unsigned y) { return _bzhi_u32(x, y); }\n", true, true },
        { "an intrinsic of a feature the unit has", "-march=x86-64-v3",
          "#include <immintrin.h>\nunsigned f(unsigned x, unsigned y) { return _bzhi_u32(x, y); }\n", false, true },
        { "an intrinsic of a feature its caller's attribute adds", "-march=x86-64",
          "#include <immintrin.h>\n__attribute__((target(\"bmi2\"))) unsigned f(unsigned x, unsigned y) { return _bzhi_u32(x, y); }\n", false, true },
        // #147's cleanup attribute calls its function where its variable's scope ends (#863's review).
        { "a cleanup's call", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "int caller(void) { int x __attribute__((cleanup(h))) = 0; return x; }\n",
          true, true },
        { "a cleanup's call of a feature the unit has", "-march=x86-64-v3",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "int caller(void) { int x __attribute__((cleanup(h))) = 0; return x; }\n",
          false, true },
        { "a cleanup's call of a feature its caller's attribute adds", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "__attribute__((target(\"avx2\"))) int caller(void) { int x __attribute__((cleanup(h))) = 0; return x; }\n",
          false, true },
        { "a cleanup function's attributes declared after the cleanup", "-march=x86-64",
          "void k(int *p);\nint caller(void) { int x __attribute__((cleanup(k))) = 0; return x; }\n"
          "__attribute__((always_inline, target(\"avx2\"))) inline void k(int *p) { *p = 1; }\n",
          false, true },
        { "cleanups no exit reaches: dead code's and an endless loop's", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "int dead(void) { return 0; { int x __attribute__((cleanup(h))) = 0; } }\n"
          "void endless(void) { int x __attribute__((cleanup(h))) = 0; for (;;) { x += 1; } }\n",
          false, true },
        // Code generation emits nothing after an expression statement that ends in a call that doesn't return, and goes on
        // from a label (#863's fourth review).
        { "a call after a call of a function that doesn't return", "-march=x86-64",
          "__attribute__((noreturn)) void stop(void);\nstatic inline __attribute__((always_inline, target(\"avx2\"))) void h(void) {}\n"
          "void f(void) { stop(); h(); }\n",
          false, true },
        { "calls after code that ends where nothing branches to", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n__attribute__((noreturn)) void stop(void);\n"
          "__attribute__((noreturn)) int stopi(void);\n_Noreturn void stop2(void);\nint g(int);\nint f1(int x) { stop2(); return h(x); }\n"
          "int f2(int x) { __builtin_unreachable(); return h(x); }\nint f3(int x) { (void)stop(); return h(x); }\nint f4(int x) { (x++, stop()); return h(x); }\n"
          "int f5(int x) { (stop(), x++); return h(x); }\nint f6(int x) { x = stopi(); return h(x); }\nint f7(int x) { g(stopi()); return h(x); }\n"
          "int f8(int x) { 0 ? (void)0 : stop(); return h(x); }\nint f9(int x) { 1 && stopi(); return h(x); }\nint f10(int x) { ({ stop(); }); return h(x); }\n"
          "int f11(int x) { switch (x) { case 1: stop(); h(x); default: break; } return x; }\nint f12(int x) { while (x) { stop(); h(x); } return x; }\n",
          false, true },
        { "calls after <stdlib.h>'s exit and abort", "-march=x86-64",
          "#include <stdlib.h>\nstatic inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "int f(int x) { if (x) exit(1); else abort(); return h(x); }\n",
          false, true },
        { "a noreturn attribute declared after the call", "-march=x86-64",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\nvoid late(void);\n"
          "int f(int x) { late(); return h(x); }\n__attribute__((noreturn)) void late(void);\n",
          true, true },
        // Only an unscoped standard noreturn first added after a definition is ignored. GNU's function-type attribute
        // (gnu:: syntax too), C's keyword and an earlier standard declaration must keep stopping emission.
        { "C23 noreturn first added after a returning definition", "-march=x86-64",
          "void stop(void) {}\n[[noreturn]] void stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nint caller(void) { stop(); return h(1); }\n",
          true, true, "-std=c23" },
        { "C23 _Noreturn attribute first added after a returning definition", "-march=x86-64",
          "void stop(void) {}\n[[_Noreturn]] void stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nint caller(void) { stop(); return h(1); }\n",
          true, true, "-std=c23" },
        { "C23 ignored late noreturn with the target feature enabled", "-march=x86-64-v3",
          "void stop(void) {}\n[[noreturn]] void stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nint caller(void) { stop(); return h(1); }\n",
          false, true, "-std=c23" },
        { "C23 noreturn on a definition remains after a redeclaration", "-march=x86-64",
          "[[noreturn]] void stop(void) { for (;;) {} }\n[[noreturn]] void stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nint caller(void) { stop(); return h(1); }\n",
          false, true, "-std=c23" },
        { "C23 noreturn before a definition remains after a redeclaration", "-march=x86-64",
          "[[noreturn]] void stop(void);\nvoid stop(void) { for (;;) {} }\n[[noreturn]] void stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nint caller(void) { stop(); return h(1); }\n",
          false, true, "-std=c23" },
        { "late GNU noreturn still changes the function type in C23", "-march=x86-64",
          "void stop(void) { for (;;) {} }\n__attribute__((noreturn)) void stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nint caller(void) { stop(); return h(1); }\n",
          false, true, "-std=c23" },
        { "late gnu scoped noreturn still changes the function type in C23", "-march=x86-64",
          "void stop(void) { for (;;) {} }\n[[gnu::noreturn]] void stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nint caller(void) { stop(); return h(1); }\n",
          false, true, "-std=c23" },
        { "late C noreturn keyword remains allowed in C23", "-march=x86-64",
          "void stop(void) { for (;;) {} }\n_Noreturn void stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nint caller(void) { stop(); return h(1); }\n",
          false, true, "-std=c23" },
        { "an ignored standard record keeps a neighbouring GNU noreturn in C23", "-march=x86-64",
          "void stop(void) { for (;;) {} }\n[[noreturn, gnu::noreturn]] void stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nint caller(void) { stop(); return h(1); }\n",
          false, true, "-std=c23" },
        { "C23 late noreturn copies a shared specifier list before ignoring it", "-march=x86-64",
          "void returning(void) {}\n[[noreturn]] void returning(void), stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\n"
          "int a(void) { stop(); return h(1); }\nint b(void) { returning(); return h(2); }\n", true, true, "-std=c23" },
        // #148: an x86 builtin's own required features (err_builtin_needs_feature), against its caller's whatever either
        // function's attributes: clang's expression of them, an or's and an and's too, and the calls it emits alone.
        { "a builtin of a feature the unit lacks", "-march=x86-64",
          "typedef char v16qi __attribute__((vector_size(16)));\nv16qi f(v16qi a, v16qi b) { return __builtin_ia32_palignr128(a, b, 3); }\n", true, true },
        { "a builtin of a feature the unit has", "-march=x86-64-v3",
          "unsigned f(unsigned x, unsigned y) { return __builtin_ia32_pdep_si(x, y); }\n", false, true },
        { "a builtin of a feature its caller's attribute adds", "-march=x86-64",
          "__attribute__((target(\"bmi2\"))) unsigned f(unsigned x, unsigned y) { return __builtin_ia32_pdep_si(x, y); }\n", false, true },
        { "a builtin of a feature its caller's attribute turns off", "-march=x86-64-v3",
          "__attribute__((target(\"no-bmi2\"))) unsigned f(unsigned x, unsigned y) { return __builtin_ia32_pdep_si(x, y); }\n", true, true },
        { "a builtin of either of two features", "-march=x86-64",
          "typedef float v4sf __attribute__((vector_size(16)));\nv4sf f(v4sf a, v4sf b, v4sf c) { return __builtin_ia32_vfmaddsubps(a, b, c); }\n", true, true },
        { "a builtin of two features, one of them its caller's", "-march=x86-64",
          "__attribute__((target(\"kl\"))) unsigned char f(void* o, const void* i, const void* h) { return __builtin_ia32_aesdecwide128kl_u8(o, i, h); }\n",
          true, true },
        { "a builtin of two features, both its caller's", "-march=x86-64",
          "__attribute__((target(\"kl,widekl\"))) unsigned char f(void* o, const void* i, const void* h) { return __builtin_ia32_aesdecwide128kl_u8(o, i, h); }\n",
          false, true },
        { "a builtin of two features, one of them the unit's", "-march=x86-64-v4",
          "typedef char v16qi __attribute__((vector_size(16)));\nv16qi f(v16qi a, v16qi b, v16qi c) { return __builtin_ia32_vpermi2varqi128(a, b, c); }\n", true, true },
        { "a builtin of two features, the unit's and its caller's", "-march=x86-64-v4",
          "typedef char v16qi __attribute__((vector_size(16)));\n"
          "__attribute__((target(\"avx512vbmi\"))) v16qi f(v16qi a, v16qi b, v16qi c) { return __builtin_ia32_vpermi2varqi128(a, b, c); }\n",
          false, true },
        { "a builtin in dead code and in a function nothing emits, before one it emits", "-march=x86-64",
          "typedef char v16qi __attribute__((vector_size(16)));\nstatic v16qi unused(v16qi a, v16qi b) { return __builtin_ia32_palignr128(a, b, 3); }\n"
          "v16qi f(v16qi a, v16qi b) { if (0) return __builtin_ia32_palignr128(a, b, 3); a = 0 ? __builtin_ia32_palignr128(a, b, 2) : a;\n"
          "    return __builtin_ia32_palignr128(a, b, 1); }\n", true, true },
        { "a builtin clang folds, before one it emits", "-march=x86-64",
          "unsigned f(void) { return __builtin_ia32_pdep_si(5, 3); }\nunsigned g(unsigned x) { return __builtin_ia32_pdep_si(x, 3); }\n", true, true },
        // Clang folds a call only where its arguments' evaluation has no side effect, and goes past what it notes as
        // undefined (a signed overflow, a shift past the width), but not past a division by zero, nor past an inexact
        // operation under FENV_ACCESS.
        { "a builtin of a constant argument with a side effect, which clang emits", "-march=x86-64",
          "void side(void);\nunsigned f(void) { return __builtin_ia32_pdep_si((side(), 5u), 3u); }\n", true, true },
        { "a builtin of arguments that overflow, which clang folds, before one it emits", "-march=x86-64",
          "unsigned f(void) { return __builtin_ia32_pdep_si((unsigned)(2147483647 + 1), 1u << 40); }\n"
          "unsigned g(unsigned x) { return __builtin_ia32_pdep_si(x, 3u); }\n", true, true },
        { "a builtin of an argument that divides by zero, which clang emits", "-march=x86-64",
          "unsigned f(void) { return __builtin_ia32_pdep_si(5u / 0u, 3u); }\n", true, true },
        { "a builtin of an inexact argument under FENV_ACCESS, which clang emits", "-march=x86-64",
          "#pragma STDC FENV_ACCESS ON\nunsigned f(void) { return __builtin_ia32_pdep_si((unsigned)(1.0 / 3.0 * 30.0), 3u); }\n", true, true },
        { "a builtin in a deferred function", "-march=x86-64",
          "typedef char v16qi __attribute__((vector_size(16)));\nstatic v16qi s(v16qi a, v16qi b) { return __builtin_ia32_palignr128(a, b, 3); }\n"
          "v16qi e(v16qi a, v16qi b) { return s(a, b); }\n",
          true, true },
        { "an intrinsic's macro of a feature the unit lacks", "-march=x86-64",
          "#include <immintrin.h>\n__m128i f(__m128i a, __m128i b) { return _mm_alignr_epi8(a, b, 3); }\n", true, true },
        // SSE's compares take AVX's 32 predicates, and one past SSE's 8 needs AVX (CodeGenFunction::checkTargetFeatures'
        // own case for __builtin_ia32_cmpps, cmpss, cmppd and cmpsd). Where it has AVX, the unit's vectors are AVX-512's
        // here, which the lowering takes; SSE's predicates, and AVX from a function's attribute, are every-error
        // fixtures' (check_test_driver_target_feature_errors), which lower nothing.
        { "an SSE compare of a predicate past 7", "-march=x86-64",
          "typedef float v4sf __attribute__((vector_size(16)));\nv4sf f(v4sf a, v4sf b) { return __builtin_ia32_cmpps(a, b, 8); }\n", true, true },
        { "an SSE compare of a predicate past 7 in a unit with AVX", "-march=x86-64-v4",
          "typedef float v4sf __attribute__((vector_size(16)));\nv4sf f(v4sf a, v4sf b) { return __builtin_ia32_cmpps(a, b, 8); }\n", false, true },
        { "a scalar double compare of a predicate past 7", "-march=x86-64",
          "typedef double v2df __attribute__((vector_size(16)));\nv2df f(v2df a, v2df b) { return __builtin_ia32_cmpsd(a, b, 16); }\n", true, true },
        { "an intrinsic's macro of SSE's predicate before one of AVX's", "-march=x86-64",
          "#include <immintrin.h>\n__m128 f(__m128 a, __m128 b) { return _mm_cmp_ps(a, b, _CMP_LT_OS); }\n"
          "__m128 g(__m128 a, __m128 b) { return _mm_cmp_ps(a, b, _CMP_GE_OQ); }\n",
          true, true },
    };
    const uint32_t routes = executable != NULL ? 4 : 2;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[1024];
        snprintf(source, sizeof(source), "%s/target-feature-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/target-feature-%u.o", directory, i);
        const char* standard = fixtures[i].standard != NULL ? fixtures[i].standard : "-std=gnu17";
        const char* arguments[] = { standard, fixtures[i].march, "-w" };
        CheckCorpusOracle oracle = { 0 };
        const bool oracle_ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                                  check_corpus_clang_compile(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        snprintf(description, sizeof(description), "%s: clang -c %s it", fixtures[i].name, fixtures[i].rejected ? "refuses" : "takes");
        check_test_expect(test, oracle_ready && oracle.rejected == fixtures[i].rejected, description);
        const char* extra[] = { standard, fixtures[i].march, "-w" };
        for (uint32_t route = 0; oracle_ready && route < routes; route += 1)
        {
            unlink(object);
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const char* driver = route >= 2 ? executable : NULL;
            const bool scalar = (route & 1) != 0;
            const bool ready = check_test_driver_run_with(driver, source, object, scalar, false, false, false, extra, BUSTER_ARRAY_LENGTH(extra), &checked, &warnings);
            const bool same = ready && checked.rejected == oracle.rejected &&
                              (!oracle.rejected || (checked.line == oracle.line && checked.column == oracle.column &&
                                                    (!fixtures[i].message || strcmp(checked.message, oracle.message) == 0)));
            snprintf(description, sizeof(description), "%s, %s %s: -c's first error %u:%u \"%s\", clang's %u:%u \"%s\"", fixtures[i].name,
                     route >= 2 ? "default CLI" : "direct", scalar ? "scalar" : "vector", checked.line, checked.column, checked.message, oracle.line,
                     oracle.column, oracle.message);
            check_test_expect(test, same, description);
            snprintf(description, sizeof(description), "%s, %s %s: a refused unit writes no object", fixtures[i].name, route >= 2 ? "default CLI" : "direct",
                     scalar ? "scalar" : "vector");
            check_test_expect(test, !checked.rejected || access(object, F_OK) != 0, description);
            CheckCorpusOracle syntax = { 0 };
            const bool syntax_ready = check_test_driver_run_with(driver, source, NULL, scalar, false, false, false, extra, BUSTER_ARRAY_LENGTH(extra), &syntax, &warnings);
            snprintf(description, sizeof(description), "%s, %s %s: -fsyntax-only takes it, as clang's does", fixtures[i].name, route >= 2 ? "default CLI" : "direct",
                     scalar ? "scalar" : "vector");
            check_test_expect(test, syntax_ready && !syntax.rejected, description);
        }
        unlink(object);
        unlink(source);
    }
}

// The lines of `text` that hold an error, each from its path's end on (`path` itself cut), joined in their order. Returns
// false if one had no room in `capacity`.
static bool check_test_driver_error_lines(const char* text, const char* path, char* lines, size_t capacity)
{
    size_t used = 0;
    bool room = true;
    lines[0] = 0;
    for (const char* at = text != NULL ? text : ""; *at != 0;)
    {
        const char* end = strchr(at, '\n');
        const size_t length = end != NULL ? (size_t)(end - at) : strlen(at);
        const char* error = memmem(at, length, ": error: ", 9);
        const size_t path_length = strlen(path);
        const bool own = length > path_length && memcmp(at, path, path_length) == 0;
        room &= error == NULL || used + length + 2 < capacity;
        if (error != NULL && used + length + 2 < capacity)
        {
            const size_t skip = own ? path_length : 0;
            memcpy(lines + used, at + skip, length - skip);
            used += length - skip;
            lines[used++] = '\n';
            lines[used] = 0;
        }
        at = end != NULL ? end + 1 : at + length;
    }
    return room;
}

// #775: every error of the code-generation check, in order, must be clang -c's, which reports them all (without
// -ferror-limit): the errors of the first function that clang emits where it is defined (an external definition, or one
// with a used, constructor or destructor attribute, by the declarations up to the definition) that has any, and no
// other's, or where none has any, every deferred function's (in the order clang's emission first refers to them or a
// declaration queues them, which the fixtures keep in source order), each by its calls' ends. Through the executable.
static void check_test_driver_target_feature_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
        // The fixture's -std=, or null for gnu17.
        const char* standard;
    } fixtures[] = {
        { "the first function's errors alone",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\nstatic int s(int x) { return h(x); }\n"
          "int e1(int x) { return s(x) + h(x + 1) + h(x + 3); }\nint e2(int x) { return h(x + 2); }\n" },
        // #148: an x86 builtin's call is checked before its arguments are emitted (CodeGenFunction::EmitBuiltinExpr), so its
        // error comes before its arguments' calls' (by its beginning, theirs by their ends); the first function's alone.
        { "a builtin's error before its arguments' calls' and after the calls that end before it",
          "typedef char v16qi __attribute__((vector_size(16)));\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) v16qi h(v16qi x) { return x; }\n"
          "v16qi f(v16qi a, v16qi b) { return __builtin_ia32_palignr128(h(a), __builtin_ia32_palignr128(a, h(b), 1), 3); }\n"
          "v16qi g(v16qi a, v16qi b) { return h(__builtin_ia32_palignr128(h(a), b, 3)); }\n" },
        { "builtins' errors in a function's statements, the first function's alone",
          "typedef char v16qi __attribute__((vector_size(16)));\ntypedef float v4sf __attribute__((vector_size(16)));\n"
          "v4sf f(v4sf a, v16qi b) { v16qi c = __builtin_ia32_palignr128(b, b, 1); a = __builtin_ia32_addsubps(a, a); return a + (v4sf)c; }\n"
          "v4sf g(v4sf a) { return __builtin_ia32_addsubps(a, a); }\n" },
        // An SSE compare of a predicate past 7 needs AVX, which clang checks before the builtin's own features: both
        // errors, in that order, where the caller lacks SSE2 too; deferred functions' compares, each its own, but one of
        // SSE's predicate and one whose function's attribute adds AVX, which isn't an error in a function that another
        // builtin's error makes the check walk either; and a compare's error among a statement's builtins', but a compare
        // of SSE's predicate.
        { "an SSE compare's predicate and its own feature, in that order",
          "typedef double v2df __attribute__((vector_size(16)));\n"
          "__attribute__((target(\"no-sse2\"))) v2df f(v2df a, v2df b) { return __builtin_ia32_cmpsd(a, b, 9); }\n" },
        { "deferred functions' compares of predicates past 7, and of one that isn't",
          "typedef float v4sf __attribute__((vector_size(16)));\ntypedef double v2df __attribute__((vector_size(16)));\n"
          "static v4sf ss31(v4sf a, v4sf b) { return __builtin_ia32_cmpss(a, b, 31); }\n"
          "static v2df pd9(v2df a, v2df b) { return __builtin_ia32_cmppd(a, b, 9); }\n"
          "static v2df sd7(v2df a, v2df b) { return __builtin_ia32_cmpsd(a, b, 7); }\n"
          "v2df e(v4sf x, v2df a, v2df b) { (void)ss31(x, x); return pd9(a, b) + sd7(a, b); }\n" },
        { "a deferred compare of a predicate past 7 whose function's attribute adds AVX, beside one without",
          "typedef float v4sf __attribute__((vector_size(16)));\n"
          "static __attribute__((target(\"avx\"))) v4sf s(v4sf a, v4sf b) { return __builtin_ia32_cmpps(a, b, 8); }\n"
          "static v4sf t(v4sf a, v4sf b) { return __builtin_ia32_cmpps(a, b, 9); }\nv4sf e(v4sf a, v4sf b) { return s(a, b) + t(a, b); }\n" },
        { "a compare of a predicate past 7 in a function whose attribute adds AVX, which another builtin's error walks",
          "typedef float v4sf __attribute__((vector_size(16)));\ntypedef char v32qi __attribute__((vector_size(32)));\n"
          "__attribute__((target(\"avx\"))) v4sf f(v4sf a, v4sf b, v32qi* c) { int m = __builtin_ia32_pmovmskb256(*c); "
          "return __builtin_ia32_cmpps(a, b, 8) + (float)m; }\n" },
        { "a compare's predicate among a statement's builtins",
          "typedef float v4sf __attribute__((vector_size(16)));\ntypedef char v16qi __attribute__((vector_size(16)));\n"
          "v4sf f(v4sf a, v16qi b) { v16qi c = __builtin_ia32_palignr128(b, b, 1); return __builtin_ia32_cmpps(a, a, 8) + (v4sf)c + "
          "__builtin_ia32_cmpps(a, a, 3); }\n" },
        { "a function's errors by their calls' ends",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2,avx2\"))) int g(int x) { return x + 2; }\n"
          "int e1(int x) { return h(g(x)) + h(x) + g(h(g(x))); }\n" },
        { "a deferred function's errors where no other has any",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\nstatic int s(int x) { return h(x) + h(x + 1); }\n"
          "static int t(int x) { return h(x + 2); }\nint e(int x) { return s(x) + t(x); }\n" },
        { "an external function's errors before a static one's",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\nstatic int s(int x) { return h(x); }\n"
          "int e(int x) { return s(x) + h(x + 1); }\n" },
        { "loops' errors in clang's emission order: a for loop's increment after its body",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2\"))) int g(int x) { return x + 2; }\n"
          "int e(int n)\n{\n    int s = 0;\n    for (int i = h(n); i < g(n); i = h(i)) { s += g(i); }\n    while (h(s) < n) { s += g(s); }\n"
          "    do { s += g(s); } while (h(s) < n);\n    return ({ int t = g(s); t; }) + h(s);\n}\n" },
        // Which functions clang emits where they are (ASTContext::DeclMustBeEmitted), and so whose errors come first: an
        // external definition by the inline rules, before a later ordinary function's (#863's review).
        { "a GNU89 inline definition, emitted where it is, before an ordinary function",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\ninline int a(int x) { return h(x); }\nint b(int x) { return h(x + 1); }\n", "-std=gnu89" },
        { "a C99 extern inline definition, emitted where it is, before an ordinary function",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nextern inline int a(int x) { return h(x); }\nint b(int x) { return h(x + 1); }\n" },
        { "a gnu_inline plain inline definition, emitted where it is, before an ordinary function",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\n__attribute__((gnu_inline)) inline int a(int x) { return h(x); }\nint b(int x) { return h(x + 1); }\n" },
        { "a C99 inline definition and a gnu_inline extern inline one, never emitted",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\ninline int a(int x) { return h(x); }\n__attribute__((gnu_inline)) extern inline int c(int x) { return h(x + 2); }\n"
          "int b(int x) { return h(x + 1) + a(x) + c(x); }\n" },
        { "a used static function emitted where it is, a retain one deferred",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nstatic __attribute__((retain)) int r(void) { return h(1); }\nstatic __attribute__((used)) int s(void) { return h(2); }\n"
          "int b(int x) { return h(x + 1); }\n" },
        { "a destructor and a constructor emitted where they are",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nstatic __attribute__((destructor)) void d(void) { h(3); }\nint b(int x) { return h(x + 1); }\n"
          "static __attribute__((constructor)) void c(void) { h(4); }\n" },
        // A later declaration is no emission point (#863's second review): clang emits a function where its definition is
        // handed over, by the declarations up to the definition, and a later one that makes an inline definition
        // external only queues it for the unit's end, so the first ordinary function with errors, before or after that
        // declaration, is the only one reported, and the queued function's errors come with the deferred ones.
        { "a C99 inline definition released by a later extern declaration, after an ordinary function's errors",
          "__attribute__((always_inline,target(\"avx2\"))) int h(int x){return x;}\ninline int a(int x){return h(x);}\nint b(int x){return h(x+1);}\nextern int a(int);\n", "-std=c99" },
        { "a later extern declaration is no emission point: the ordinary function after it fails first",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\ninline int a(int x) { return h(x); }\nextern int a(int);\n"
          "int c(int x) { return h(x + 2); }\n", "-std=c99" },
        { "an ordinary function's errors between a C99 inline definition and its extern declaration, and another's after it",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\ninline int a(int x) { return h(x); }\nint b(int x) { return h(x + 1); }\n"
          "extern int a(int);\nint c(int x) { return h(x + 2); }\n", "-std=c99" },
        { "a later plain declaration of a C99 inline definition, after an ordinary function's errors and before another's",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\ninline int a(int x) { return h(x); }\nint b(int x) { return h(x + 1); }\n"
          "int a(int);\nint c(int x) { return h(x + 2); }\n", "-std=c99" },
        { "a C99 inline definition released by a later extern declaration, emitted with the deferred functions",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\ninline int a(int x) { return h(x); }\nextern int a(int);\n"
          "static int s(int x) { return h(x + 1); }\nint e(int x) { return s(x); }\n", "-std=c99" },
        { "an earlier extern declaration makes a C99 inline definition external where it is defined",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nextern int a(int);\ninline int a(int x) { return h(x); }\n"
          "int b(int x) { return h(x + 1); }\n", "-std=c99" },
        { "a GNU89 extern inline definition released by a later plain inline declaration, after an ordinary function's errors",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nextern inline int a(int x) { return h(x); }\nint b(int x) { return h(x + 1); }\n"
          "inline int a(int x);\nint c(int x) { return h(x + 2); }\n", "-std=gnu89" },
        { "a GNU89 extern inline definition released by a later plain inline declaration, emitted at the unit's end",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nextern inline int a(int x) { return h(x); }\ninline int a(int x);\n", "-std=gnu89" },
        { "a used attribute on a later declaration doesn't emit a static function where it is defined",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nstatic int s(void) { return h(2); }\nstatic int s(void) __attribute__((used));\n"
          "int b(int x) { return h(x + 1); }\n" },
        { "a used attribute on an earlier declaration does",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nstatic int s(void) __attribute__((used));\nstatic int s(void) { return h(2); }\n"
          "int b(int x) { return h(x + 1); }\n" },
        { "a constructor attribute on a later declaration doesn't emit a static function where it is defined",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nstatic void c(void) { h(4); }\nstatic void c(void) __attribute__((constructor));\n"
          "int b(int x) { return h(x + 1); }\n" },
        { "a destructor attribute on an earlier declaration does",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\nstatic void d(void) __attribute__((destructor));\nstatic void d(void) { h(3); }\n"
          "int b(int x) { return h(x + 1); }\n" },
        { "an earlier plain inline declaration makes a GNU89 extern inline definition external where it is defined",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\ninline int a(int);\nextern inline int a(int x) { return h(x); }\n"
          "int b(int x) { return h(x + 1); }\n", "-std=gnu89" },
        { "a gnu_inline attribute on an earlier declaration is the definition's: its plain inline spelling is external where it is defined",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\n__attribute__((gnu_inline)) inline int a(int);\ninline int a(int x) { return h(x); }\n"
          "int b(int x) { return h(x + 1); }\n" },
        { "an earlier plain inline declaration of a C99 inline definition leaves it inline-only",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\ninline int a(int);\ninline int a(int x) { return h(x); }\n"
          "int b(int x) { return h(x + 1); }\n" },
        // #147's cleanup attribute (#863's third review): clang calls a variable's cleanup function where its scope ends,
        // once whichever exits reach it, after the scope's statements and the innermost variable's first, and reports the
        // call at the attribute.
        { "cleanups' calls at their scopes' ends, among the calls",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2\"))) int g(int n) { return n + 1; }\n"
          "int f(int n)\n{\n    int x __attribute__((cleanup(h))) = g(n);\n    {\n        int y __attribute__((cleanup(h))) = g(1);\n        n += g(2);\n    }\n"
          "    return x + g(3);\n}\n" },
        { "a cleanup that returns and breaks reach, once at its scope's end",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2\"))) int g(int n) { return n + 1; }\n"
          "int f(int n)\n{\n    int x __attribute__((cleanup(h))) = 0;\n    if (n)\n        return g(1);\n    for (int i = 0; i < n; i++)\n    {\n"
          "        int y __attribute__((cleanup(h))) = i;\n        if (g(i))\n            break;\n        if (i == 3)\n            continue;\n    }\n"
          "    return x + g(2);\n}\n" },
        { "a goto out of a scope whose end no path reaches",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2\"))) int g(int n) { return n + 1; }\n"
          "void f(int n)\n{\n    {\n        int x __attribute__((cleanup(h))) = 0;\n        if (n)\n            goto out;\n        g(1);\n        for (;;)\n"
          "            g(2);\n    }\nout:\n    g(3);\n}\n" },
        { "a statement expression's cleanups after its value, and a return out of one",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2\"))) int g(int n) { return n + 1; }\n"
          "static inline __attribute__((always_inline, target(\"lzcnt\"))) void k(int *p) { *p = 2; }\n"
          "int f(int n)\n{\n    int x __attribute__((cleanup(h))) = 0;\n"
          "    int v = ({ int y __attribute__((cleanup(h))) = g(1); int z __attribute__((cleanup(k))) = g(6); y + z; }) + g(2);\n"
          "    int w = ({ if (n) return g(3); g(4); }) + g(5);\n    return v + w + x;\n}\n" },
        { "a for statement's own variable's cleanup after its increment",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2\"))) int g(int n) { return n + 1; }\n"
          "int f(int n)\n{\n    for (int i __attribute__((cleanup(h))) = g(0); i < n; i += g(1))\n    {\n        if (g(2))\n            break;\n"
          "        if (g(3))\n            return 1;\n        g(4);\n    }\n    return g(5);\n}\n" },
        { "variables' cleanups innermost first, a loop body's at its end",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2\"))) int g(int n) { return n + 1; }\n"
          "void f(int n)\n{\n    int a __attribute__((cleanup(h))) = 0;\n    int b __attribute__((cleanup(h))) = 0;\n    while (1)\n    {\n"
          "        int c __attribute__((cleanup(h))) = g(1);\n        if (g(2))\n            break;\n        g(3);\n    }\n    g(4);\n}\n" },
        { "a flatten caller's cleanups",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "__attribute__((target(\"avx2\"))) void k(int *p) { *p = 2; }\n"
          "__attribute__((flatten, target(\"sse4.2\"))) int f(int n)\n{\n    int x __attribute__((cleanup(h))) = n;\n"
          "    int y __attribute__((cleanup(k))) = n;\n    return x + y;\n}\n" },
        { "a standard attribute's cleanup, at its scope",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\nvoid f(void)\n{\n"
          "    [[gnu::cleanup(h)]] int x = 0;\n    [[gnu::__cleanup__(h)]] int y = 0;\n}\n", "-std=gnu23" },
        { "a cleanup among other attributes, and one from a macro",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n#define AUTO __attribute__((cleanup(h)))\n"
          "void f(void)\n{\n    int x __attribute__((unused, __cleanup__(h))) = 0;\n    int y AUTO = 0, z AUTO = 1;\n}\n" },
        { "cleanups no exit reaches: an endless loop's and dead code's",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "static inline __attribute__((always_inline, target(\"bmi2\"))) int g(int n) { return n + 1; }\n"
          "static void endless(int n)\n{\n    int x __attribute__((cleanup(h))) = 0;\n    for (;;)\n        g(n);\n}\n"
          "static int dead(int n)\n{\n    return g(n);\n    {\n        int x __attribute__((cleanup(h))) = 0;\n        g(x);\n    }\n}\n"
          "int e(int n)\n{\n    endless(n);\n    return dead(n);\n}\n" },
        { "an external function's cleanup alone, as clang emits no deferred function after it",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void h(int *p) { *p = 1; }\n"
          "static int s(void) { int x __attribute__((cleanup(h))) = 0; return x; }\nint b(void) { return s(); }\n"
          "int e(void) { int z __attribute__((cleanup(h))) = 0; return z; }\n" },
        // Code generation's emission, where it differs from Sema's reachability (#863's fourth review): nothing after an
        // expression statement that ends in a call that doesn't return, loops' blocks emitted whatever their conditions and
        // bodies' code, and code after a label whatever precedes it.
        { "calls code generation emits around calls that don't return",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n__attribute__((noreturn)) void stop(void);\n"
          "__attribute__((noreturn)) int stopi(void);\n\nint f(int n)\n{\n    if (n == 1)\n        stop();\n    n += h(1);\n    n ? (void)0 : stop();\n"
          "    n += h(2);\n    int y = stopi();\n    n += h(3) + y;\n    n && stopi();\n    n += h(4);\n    n = stopi() + h(5);\n    n += h(6);\nlbl:\n"
          "    n += h(7);\n    __builtin_trap();\n    n += h(8);\n    if (n)\n        goto lbl;\n    return n;\n}\n" },
        { "loops' blocks code generation emits whatever their conditions and bodies leave",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n__attribute__((noreturn)) void stop(void);\n"
          "__attribute__((noreturn)) int stopi(void);\n\nstatic int a(int n) { while (0) { n = h(n); } return n; }\n"
          "static int b(int n) { for (; 0; n = h(n + 1)) { n = h(n + 2); } return n; }\n"
          "static int c(int n) { do { stop(); } while (h(n + 3)); return h(n + 4); }\nstatic int d(int n) { do { n++; } while (1); return h(n + 5); }\n"
          "static int e(int n) { for (; 1;) { n++; } return h(n + 6); }\nstatic int f(int n) { for (int i = 0; i < n; i = h(i)) { stop(); } return n; }\n"
          "static int g(int n) { while (1) { n++; } return h(n + 7); }\nint entry(int n) { return a(n) + b(n) + c(n) + d(n) + e(n) + f(n) + g(n); }\n" },
        { "code after labels, whatever precedes them",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n__attribute__((noreturn)) void stop(void);\n"
          "__attribute__((noreturn)) int stopi(void);\n\nstatic void hp(int *p) { *p = h(*p); }\nstatic int a(int n) { stop(); lbl: return h(n); }\n"
          "static int b(int n) { stop(); goto end; end: return h(n + 1); }\nstatic int c(int n) { if (n) goto out; stop(); out: return h(n + 2); }\n"
          "static int d(int n) { return n; skip: n = h(n + 3); goto skip; }\nstatic void e(int n) { int x __attribute__((cleanup(hp))) = n; stop(); }\n"
          "static void g(int n) { int x __attribute__((cleanup(hp))) = n; if (n) return; stop(); }\n"
          "int entry(int n) { e(n); g(n); return a(n) + b(n) + c(n) + d(n); }\n" },
        { "a join after a call that doesn't return, in its statement, a select, which isn't one, and constant conditions' "
          "operands that aren't emitted",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n__attribute__((noreturn)) void stop(void);\n"
          "__attribute__((noreturn)) int stopi(void);\nstatic int f(int n)\n{\n    int y = 0;\n    *(n ? &n : &y) = stopi();\n    n += h(9);\n"
          "    (stop(), n ? 1 : 2);\n    n += h(10);\n    (n ? 1 : 2, stop());\n    n += h(11);\n    return n;\n}\n"
          "static int g(int n)\n{\n    1 ? (void)0 : stop();\n    n += h(12);\n    0 && stopi();\n    n += h(13);\n    1 || stopi();\n    n += h(14);\n"
          "    n = 0 ? stopi() : n;\n    return h(n);\n}\nint entry(int n) { return f(n) + g(n); }\n" },
        { "cleanups after calls that don't return",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void hp(int *p) { *p = 1; }\n__attribute__((noreturn)) void stop(void);\n"
          "static void e(int n) { int x __attribute__((cleanup(hp))) = n; stop(); }\n"
          "static void g(int n) { int x __attribute__((cleanup(hp))) = n; if (n) return; stop(); }\n"
          "static void k(int n) { int x __attribute__((cleanup(hp))) = n; stop(); lbl: x++; }\nint entry(int n) { e(n); g(n); k(n); return n; }\n" },
        { "every target error after ignored late unscoped C23 noreturn attributes",
          "void stop(void) {}\n[[noreturn]] void stop(void);\nvoid stop2(void) {}\n[[_Noreturn]] void stop2(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\n"
          "int caller(void) { stop(); h(1); stop2(); return h(2) + h(3); }\n", "-std=c23" },
        { "permitted C23 noreturn spellings keep only the final reachable target error",
          "[[noreturn]] void early(void);\nvoid early(void) { for (;;) {} }\n[[noreturn]] void early(void);\n"
          "void gnu_late(void) { for (;;) {} }\n__attribute__((noreturn)) void gnu_late(void);\n"
          "void scoped_late(void) { for (;;) {} }\n[[gnu::noreturn]] void scoped_late(void);\n"
          "void keyword_late(void) { for (;;) {} }\n_Noreturn void keyword_late(void);\n"
          "void mixed_late(void) { for (;;) {} }\n[[noreturn, gnu::noreturn]] void mixed_late(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\n"
          "int a(void) { early(); return h(1); }\nint b(void) { gnu_late(); return h(2); }\nint c(void) { scoped_late(); return h(3); }\n"
          "int d(void) { keyword_late(); return h(4); }\nint e(void) { mixed_late(); return h(5); }\nint reachable(void) { return h(6); }\n", "-std=c23" },
        { "shared C23 noreturn specifiers retain each declarator's disposition",
          "void returning(void) {}\n[[noreturn]] void returning(void), stop(void);\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x; }\n"
          "int a(void) { stop(); return h(1); }\nint b(void) { returning(); return h(2) + h(3); }\n", "-std=c23" },
        // #891's code-generation emission: the code a label can enter goes with it, from the statement's or operand's
        // start — an if, loop or switch a jump can enter, or the operand a constant condition leaves out when a
        // statement expression in it holds a label.
        { "a label the constant condition's left-out code can enter",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "static int a(int x) { if (0) { x = h(x); lbl: x = h(x + 1); } return x; }\n"
          "static int b(int x) { return 0 && ({ lbl: h(x + 2); }); }\n"
          "static int c(int x) { return 1 || ({ lbl: h(x + 3); }); }\n"
          "static int d(int x) { return 1 ? x : ({ lbl: h(x + 4); }); }\n"
          "static int g(int x) { if (0) { x = ({ lbl: h(x + 5); }); } return x; }\n"
          "static int k(int x) { if (0) { goto out; } x = h(x + 6); out: return x; }\n"
          "int e(int x) { return a(x) + b(x) + c(x) + d(x) + g(x) + k(x); }\n" },
        { "the C library's functions that don't return by their names, and a call through a pointer",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "void exit(int);\nvoid abort(void);\nvoid _Exit(int);\nvoid quick_exit(int);\n"
          "__attribute__((noreturn)) void stop(void);\n"
          "static int a(int x) { exit(0); return h(x); }\n"
          "static int b(int x) { abort(); return h(x + 1); }\n"
          "static int c(int x) { _Exit(0); return h(x + 2); }\n"
          "static int d(int x) { quick_exit(0); return h(x + 3); }\n"
          "static int g(int x) { (*stop)(); return h(x + 4); }\n"
          "static int k(int x) { (&exit)(0); return h(x + 5); }\n"
          "static int m(int x) { void (*p)(int) = exit; p(0); return h(x + 6); }\n"
          "int e(int x) { return a(x) + b(x) + c(x) + d(x) + g(x) + k(x) + m(x); }\n" },
        { "a switch on a constant emits the case it selects alone where its scan can skip the rest",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "static int a(int x) { switch (0) { case 0: x = h(x); break; case 1: x = h(x + 1); break; } return x; }\n"
          "static int b(int x) { switch (0) { case 1: x = h(x); default: x = h(x + 1); } return x; }\n"
          "static int c(int x) { switch (0) { case 1: x = h(x); } return x; }\n"
          "static int d(int x) { switch (0) { case 0: x = h(x); break; case 1: x = h(x + 1); lbl: x = h(x + 2); } return x; }\n"
          "static int g(int x) { switch (0) { case 0: x = h(x); break; case 1: x = h(x + 1); case 2 ... 4: x = h(x + 2); } return x; }\n"
          "static int k(int x) { switch (0) { case 0: { if (x) break; } break; case 1: x = h(x); } return x; }\n"
          "static int m(int x) { switch (0) { case 1: int y = h(x + 8); x = y; case 0: x = h(x); break; } return x; }\n"
          "static int q(int x) { switch (0) { case 0: x = h(x); default: x = h(x + 1); } return x; }\n"
          "static int s(int x) { switch (0) { lbl: case 0: x = h(x); break; case 1: x = h(x + 1); break; } return x; }\n"
          "int e(int x) { return a(x) + b(x) + c(x) + d(x) + g(x) + k(x) + m(x) + q(x) + s(x); }\n" },
        { "a cleanup attribute's call in a while (0) body the emission still makes",
          "static inline __attribute__((always_inline, target(\"avx2\"))) void hp(int *p) { *p = 1; }\n"
          "static int a(void) { while (0) { int y __attribute__((cleanup(hp))) = 0; (void)y; } return 0; }\n"
          "static int b(void) { if (0) { int y __attribute__((cleanup(hp))) = 0; (void)y; } return 0; }\n"
          "static int c(void) { if (0) { lbl: int y __attribute__((cleanup(hp))) = 0; (void)y; } return 0; }\n"
          "int e(void) { return a() + b() + c(); }\n" },
        // #891's re-review findings: the library builtins' noreturn merge takes the declaration's type as clang's does,
        // a `jmp_buf` typedef counts only at file scope before the declaration in effect at the call, a statement
        // expression's value and labels emit after a dead statement, an unlabeled statement after a dead one doesn't
        // in a constant switch's kept statements, and ContainsLabel sees labels inside sizeof and __alignof__.
        { "the C library's noreturn declarations inherit it whatever their result type",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "int exit(int);\nint abort(void);\nint _Exit(int);\ntypedef int jmp_buf;\nint longjmp(jmp_buf, int);\n"
          "int x1(int x) { exit(0); return h(x); }\nint x2(int x) { abort(); return h(x + 1); }\n"
          "int x3(int x) { _Exit(0); return h(x + 2); }\nint x4(int x) { longjmp(0, 1); return h(x + 3); }\n"
          "int e(int x) { return x1(x) + x2(x) + x3(x) + x4(x) + h(x + 4); }\n" },
        { "a jmp_buf typedef at file scope before the declaration in effect at the call, not a block-scope one",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "void g(void) { typedef int jmp_buf; (void)sizeof(jmp_buf); }\nvoid longjmp(void *, int);\n"
          "int x1(int x) { longjmp(0, 1); return h(x); }\ntypedef int jmp_buf;\nvoid longjmp(void *, int);\n"
          "int x2(int x) { longjmp(0, 1); return h(x + 1); }\nint e(int x) { return x1(x) + x2(x); }\n" },
        { "a statement expression's value and labels after a dead statement",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) void hp(int *p) { *p = 1; }\n"
          "__attribute__((noreturn)) void stop(void);\n"
          "static int a(int x) { int y = ({ stop(); h(x); }); return y; }\n"
          "static int b(int x) { return ({ stop(); h(x + 1); }); }\n"
          "static int c(int x) { ({ stop(); h(x + 2); }); return h(x + 3); }\n"
          "static int d(int x) { int y = ({ stop(); lbl: h(x + 4); }); return y; }\n"
          "static int v(int x) { ({ stop(); hp(&x); }); return h(x + 5); }\n"
          "static int w(int x) { ({ hp(&x); }); return h(x + 6); }\n"
          "int e(int x) { return a(x) + b(x) + c(x) + d(x) + v(x) + w(x); }\n" },
        { "a constant switch's kept statements after a dead statement",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "__attribute__((noreturn)) void stop(void);\n"
          "int a(int x) { switch (0) { case 0: stop(); x = h(x); break; } return x; }\n"
          "int b(int x) { switch (0) { case 0: stop(); x = h(x + 1); break; } return h(x + 2); }\n"
          "int c(int x) { switch (0) { case 0: stop(); break; } return h(x + 3); }\n"
          "int g(int x) { return h(x + 4); }\nint e(int x) { return a(x) + b(x) + c(x) + g(x); }\n" },
        { "labels inside sizeof and __alignof__ operands",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "static int a(int x) { return 0 && (h(x), sizeof(({ lbl: 1; }))); }\n"
          "static int b(int x) { switch (sizeof(({ lbl2: 1; }))) { case 0: x = 1; break; case 4: x = h(x + 1); break; } return x; }\n"
          "static int c(int x) { if (sizeof(({ lbl3: 1; }))) { x = 1; } else { x = h(x + 2); } return x; }\n"
          "static int d(int x) { if (__alignof__(({ lbl4: 1; })) == 4) { x = 1; } else { x = h(x + 3); } return x; }\n"
          "int e(int x) { return a(x) + b(x) + c(x) + d(x); }\n" },
        // A target_clones body's errors, against its first version's features, once for each copy clang emits (#2211):
        // each distinct version, a repeated one once; a builtin's too.
        { "a target_clones body's errors once per copy, by its first version's features",
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "__attribute__((target_clones(\"default\", \"avx2\"))) int c(int x) { return h(x) + h(x + 1); }\n" },
        { "three copies' errors, and a repeated version's once",
          "typedef char v16qi __attribute__((vector_size(16)));\n"
          "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
          "__attribute__((target_clones(\"sse4.2\", \"arch=haswell\", \"sse4.2\", \"default\"))) int c(int x, v16qi* v) "
          "{ *v = __builtin_ia32_pshufb128(*v, *v); return h(x); }\n" },
    };
    for (uint32_t i = 0; executable != NULL && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        snprintf(source, sizeof(source), "%s/target-feature-errors-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/target-feature-errors-%u.o", directory, i);
        const bool written = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        char* const standard = (char*)(fixtures[i].standard != NULL ? fixtures[i].standard : "-std=gnu17");
        char* const theirs_arguments[] = { (char*)clang, standard, "-march=x86-64", "-w", "-fno-caret-diagnostics", "-fno-color-diagnostics", "-c", "-o",
                                           "/dev/null", source, NULL };
        char* const ours_arguments[] = { (char*)executable, standard, "-march=x86-64", "-w", "-c", "-o", object, source, NULL };
        char* theirs_text = NULL;
        char* ours_text = NULL;
        const int theirs_status = written ? check_test_driver_spawn(theirs_arguments, &theirs_text) : -1;
        const int ours_status = written ? check_test_driver_spawn(ours_arguments, &ours_text) : -1;
        static char theirs[16384];
        static char ours[16384];
        static char description[2 * 16384 + 512];
        const bool theirs_whole = check_test_driver_error_lines(theirs_text, source, theirs, sizeof(theirs));
        const bool ours_whole = check_test_driver_error_lines(ours_text, source, ours, sizeof(ours));
        snprintf(description, sizeof(description), "%s: -c's errors are clang -c's, every line of both read (status %d and %d)\n-c's:\n%sclang's:\n%s",
                 fixtures[i].name, ours_status, theirs_status, ours, theirs);
        check_test_expect(test, written && theirs_whole && ours_whole && theirs_status == 1 && ours_status == 1 && theirs[0] != 0 && strcmp(ours, theirs) == 0, description);
        free(theirs_text);
        free(ours_text);
        unlink(object);
        unlink(source);
    }
}

#include "c_check_packed_test.c"
#include "c_check_x86_diagnostic_test.c"
#include "c_check_undeclared_test.c"
#include "c_check_capacity_test.c"
#include "c_check_visibility_driver_test.c"
#include "c_check_ms_struct_driver_test.c"
#include "c_check_ignored_pragma_driver_test.c"
#include "c_check_implicit_driver_test.c"
#include "c_check_self_assign_driver_test.c"
#include "c_check_kr_driver_test.c"
#include "c_check_return_driver_test.c"
#include "c_check_return_call_driver_test.c"
#include "c_check_void_conditional_driver_test.c"
#include "c_check_fortify_driver_test.c"
#include "c_check_library_driver_test.c"
#include "c_check_transparent_driver_test.c"

// #503: compare the production renderer, not just source locations sorted after the fact. Type errors are discovered
// after adaptation; placement/conflicts must retain a complete diagnostic graph through the annotated statement.
static void check_test_driver_loop_hints(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const struct
    {
        const char* name;
        const char* source;
        bool exact_message;
        // Existing argument/control columns stay as they were; these cases check first-error line precedence.
        bool line_only;
    } fixtures[] =
    {
        { "conflict after a body name error", "void f(int n) {\n#pragma clang loop unroll_count(4)\n#pragma clang loop unroll(full)\n    while (n--) { undeclared_x = 1; }\n}\n" },
        { "duplicate after a body type error", "void f(int n) {\n#pragma clang loop unroll(disable)\n#pragma clang loop unroll(enable)\n    while (n--) { *n = 1; }\n}\n" },
        { "count duplicate after a condition name error", "void f(int n) {\n#pragma clang loop unroll_count(4) unroll_count(8)\n    while (missing) {}\n}\n" },
        { "conflict after a for update type error", "void f(int n) {\n#pragma clang loop vectorize(disable) vectorize_width(4)\n    for (; n; *n) {}\n}\n" },
        { "do conflict after its trailing condition", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    do {} while (*n);\n}\n" },
        { "conflict after an initializer's type error", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { int x = *n; }\n}\n" },
        { "placement after an expression name error", "void f(int n) {\n#pragma clang loop unroll(full)\n    missing = 1;\n}\n" },
        { "placement after a block type error", "void f(int n) {\n#pragma nounroll\n    { *n = 1; }\n}\n" },
        { "placement after an if's else type error", "void f(int n) {\n#pragma unroll\n    if (n) {} else { *n = 1; }\n}\n" },
        { "placement after a labeled loop name error", "void f(int n) {\n#pragma clang loop unroll(full)\n    label: while (n--) { missing = 1; }\n}\n" },
        { "conflict before a later statement error", "void f(int n) {\n#pragma clang loop unroll_count(4)\n#pragma clang loop unroll(full)\n    while (n--) {}\n    missing = 1;\n}\n", true },
        { "placement before a later statement error", "void f(int n) {\n#pragma nounroll\n    n = 1;\n    missing = 1;\n}\n", true },
        { "duplicate's message and column", "void f(int n) {\n#pragma clang loop unroll(disable) unroll(enable)\n    while (n--) {}\n}\n", true },
        { "duplicate counts' message and column", "void f(int n) {\n#pragma unroll 4\n#pragma GCC unroll 8\n    while (n--) {}\n}\n", true },
        { "unroll zero belongs to the state category", "void f(int n) {\n#pragma unroll 0\n#pragma nounroll\n    while (n--) {}\n}\n", true },
        { "scalable width's diagnostic spelling", "void f(int n) {\n#pragma clang loop vectorize_width(4, scalable) vectorize_width(fixed)\n    while (n--) {}\n}\n", true },
        { "ordinary unroll conflict spelling", "void f(int n) {\n#pragma unroll 4\n#pragma nounroll\n    while (n--) {}\n}\n", true },
        { "placement overrides conflicts", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    n = 1;\n}\n", true },
        { "an inner pragma argument precedes an outer conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {\n#pragma unroll ()\n        while (n--) {}\n    }\n}\n", true, true },
        { "later argument before a same pragma conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4) interleave_count()\n    while (n--) { missing = 1; }\n}\n", true, true },
        { "later argument before an earlier pragma conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n#pragma unroll ()\n    while (n--) { missing = 1; }\n}\n", true, true },
        { "inner and outer loops complete together", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--)\n#pragma clang loop distribute(enable) distribute(disable)\n        while (n--) {}\n}\n", true },
        { "nested placement completes before outer conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {\n#pragma nounroll\n        n = 1;\n    }\n}\n", true },
        { "dropped hints do not hide a body error", "void f(int n) {\n#pragma clang loop unroll_count(4))\n    while (n--) { *n = 1; }\n}\n" },
        { "an annotated statement's syntax error", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { n = ; }\n}\n" },
        { "conflict before a later statement's syntax error", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n    int x = ;\n}\n", true },
        { "placement before a later statement's syntax error", "void f(int n) {\n#pragma nounroll\n    n = 1;\n    int x = ;\n}\n", true },
        { "synthetic closure does not complete the annotated loop", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { n = 1;\n" },
        { "a following pragma argument comes after completed-loop Sema", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n#pragma unroll ()\n    while (n--) {}\n}\n", true },
        { "a following pragma handler runs in the loop's lookahead", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n#pragma clang loop bogus(4)\n    while (n--) {}\n}\n", false, true },
        { "a following unclosed pragma runs in the loop's lookahead", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n#pragma unroll (4\n    while (n--) {}\n}\n", true, true },
        { "a trailing empty annotation is not a nested statement", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n#pragma unroll\n}\n", true },
        { "duplicate labels precede a loop conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {\n        same:;\n        same:;\n    }\n}\n", false, true },
        { "duplicate case values precede a loop conflict", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { switch (n) {\n        case 0: break;\n        case 0: break;\n    } }\n}\n", false, true },
        { "duplicate defaults precede loop placement", "void f(int n) {\n#pragma unroll\n    switch (n) {\n        default: break;\n        default: break;\n    }\n}\n", false, true },
        { "a later duplicate label follows the loop conflict", "void f(int n) {\n    same:;\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) {}\n    same:;\n}\n", true },
        { "an undefined goto is an end-of-function obligation", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { goto missing; }\n}\n", true },
        { "a forward label address stays typed before loop placement", "void f(int n) {\n#pragma unroll\n    { void *p = &&later; }\n    later:;\n}\n", true },
        { "an undefined label address waits for function completion", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { void *p = &&missing; }\n}\n", true },
        { "a body type error after a pending label address precedes the hint", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { void *p = &&missing;\n        *n = 1;\n    }\n}\n" },
        { "nested switch errors precede a later pragma handler", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n    while (n--) { switch (n) {\n        case 0: break;\n        case 0: break;\n    } }\n#pragma clang loop bogus(4)\n    while (n--) {}\n}\n", false, true },
        { "lookahead handler precedes completion of its direct switch", "void f(int n) {\n#pragma unroll\n    switch (n) {\n        case 0: break;\n        case 0: break;\n    }\n#pragma clang loop bogus(4)\n    while (n--) {}\n}\n", false, true },
        { "pragma expansion retains the conflict's invocation", "void f(int n) {\n_Pragma(\"clang loop unroll_count(4)\")\n_Pragma(\"clang loop unroll(full)\")\n    while (n--) {}\n}\n", true },
    };
    const char* arguments[] = { "-std=gnu17", "-w" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[384];
        snprintf(source, sizeof(source), "%s/loop-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/loop-%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        check_test_expect(test, ready && oracle.rejected, fixtures[i].name);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, true, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, route %u: first error %u:%u, clang %u:%u", fixtures[i].name, route,
                     checked.line, checked.column, oracle.line, oracle.column);
            check_test_expect(test, ran && checked.rejected && checked.line == oracle.line && (fixtures[i].line_only || checked.column == oracle.column), description);
            snprintf(description, sizeof(description), "%s: first diagnostic text matches clang", fixtures[i].name);
            check_test_expect(test, !fixtures[i].exact_message || strcmp(checked.message, oracle.message) == 0, description);
            check_test_expect(test, access(object, F_OK) != 0, "a rejected annotated statement writes no object");
        }
        unlink(object);
        unlink(source);
    }
}

// Declaration specifiers that don't combine and numeric constants clang refuses (#1127): the first error's place and its
// exact text are clang's, by every route, where the checker's own words were generic ("invalid or unsupported").
static void check_test_driver_declspec_literals(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const struct
    {
        const char* name;
        const char* source;
    } fixtures[] =
    {
        { "a repeated type", "double double x;\n" },
        { "a third long", "int a;\nlong long long x;\n" },
        { "short then long", "short long x;\n" },
        { "signed then unsigned", "signed unsigned x;\n" },
        { "a typedef name then a type", "typedef int T; T int x;\n" },
        { "a type then a struct's definition", "int struct S { int a; } x;\n" },
        { "typedef then a storage class", "typedef static int x;\n" },
        { "a thread storage class then register", "_Thread_local register int x;\n" },
        { "a parameter's repeated type", "void f(int int x);\n" },
        { "a cast's third long", "int g(int v) { return (long long long)v; }\n" },
        { "an unsigned double, at the sign", "unsigned double x;\n" },
        { "a long char, at the width", "char long x;\n" },
        { "long long double, at the first long", "long double long x;\n" },
        { "long __int128", "long __int128 x;\n" },
        { "a signed float in sizeof", "int g(void) { return sizeof(signed float); }\n" },
        { "_Complex _Bool", "_Complex _Bool b;\n" },
        { "an exponent without digits", "double d = 1e;\n" },
        { "a spliced exponent without digits", "double d = 1\\\ne;\n" },
        { "an octal constant's decimal digit", "int o = 0129;\n" },
        { "an octal digit from a macro", "#define L 08\nint c = L;\n" },
        { "a binary constant's digit", "int b = 0b102;\n" },
        { "a floating constant's suffix", "double q = 1.5b;\n" },
        { "long long on a floating constant", "void f(void) { double a = 1.5ll; }\n" },
        { "an integer constant's suffix", "int x = 1lul;\n" },
        { "a hexadecimal floating constant without an exponent", "double h = 0x1.5;\n" },
        { "an integer constant past 64 bits", "int big = 99999999999999999999999;\n" },
        { "a struct, a #pragma pack, then another type", "#pragma pack(push, 1)\nstruct P { char c; int i; }\n#pragma pack(pop)\nint x;\n" },
        { "a struct, a #pragma pack, then a typedef of a struct", "#pragma pack(push, 1)\nstruct P { char c; }\n#pragma pack(pop)\ntypedef struct node { int x; } node;\n" },
        { "a struct, a _Pragma pack, then a storage class and a type", "struct P { char c; }\n_Pragma(\"pack(pop)\") static int x;\n" },
        // A constant before a syntax error in its declaration, which only the parse reached: clang's error comes first.
        { "an octal constant's digit before a missing operand", "int x = 08 +;\n" },
        { "a floating constant's suffix before a missing `;`", "void f(void) { return 1.5b }\n" },
        { "a condition's constant before a missing `;`", "void f(int a) { if (a == 0129) a = 1 }\n" },
        { "an exponent without digits before a missing operand", "void f(void) { int x = 1e + ; }\n" },
    };
    const char* arguments[] = { "-std=gnu17", "-w" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[384];
        snprintf(source, sizeof(source), "%s/declspec-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/declspec-%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        check_test_expect(test, ready && oracle.rejected, fixtures[i].name);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, true, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, route %u: first error %u:%u \"%s\", clang %u:%u \"%s\"", fixtures[i].name, route, checked.line,
                     checked.column, checked.message, oracle.line, oracle.column, oracle.message);
            check_test_expect(test, ran && checked.rejected && checked.line == oracle.line && checked.column == oracle.column &&
                              strcmp(checked.message, oracle.message) == 0, description);
        }
        unlink(object);
        unlink(source);
    }
    // What the checker refused where clang takes it (#1127): C2y's `0o` octal prefix, which clang takes in every mode
    // and in #if, and GNU's array designator without its `=`, whose value has its own tree whatever begins it. The
    // program's values are clang's, through both host paths.
    static const char accepted[] =
        "#if 0o17 != 15\n#error 0o\n#endif\n"
        "int x = 5;\n"
        "int a1[3] = { [1] -1, [2] ~1 };\n"
        "int *a2[2] = { [1] &x };\n"
        "int a3[] = { [0] 1, [3] 4 };\n"
        "int a4[2][2] = { [1] { 1, 2 }, [0] { [1] -3 } };\n"
        "char a5[2][4] = { [1] \"abc\" };\n"
        "int a6[3] = { [0 ... 1] 7 };\n"
        "int a7[2] = { [1] 1 + 2 * 3, [0] (int)2.5 };\n"
        "static int g(int v) { return v * 3; }\n"
        "int main(void)\n"
        "{\n"
        "    int i = 2;\n"
        "    int b1[2] = { [1] g(i) + 1, [0] -i };\n"
        "    int b2[2] = { [1] (int){ 3 }, [0] sizeof i };\n"
        "    unsigned o = 0o17 + 0O7u;\n"
        "    return a1[1] == -1 && a1[2] == -2 && *a2[1] == 5 && sizeof a3 == 16 && a3[3] == 4 && a4[0][1] == -3 && a4[1][1] == 2 &&\n"
        "           a5[1][2] == 'c' && a6[1] == 7 && a6[2] == 0 && a7[1] == 7 && a7[0] == 2 && b1[1] == 7 && b1[0] == -2 && b2[1] == 3 &&\n"
        "           b2[0] == 4 && o == 22 ? 42 : 1;\n"
        "}\n";
    char source[192];
    snprintf(source, sizeof(source), "%s/declspec-accepted.c", directory);
    const bool written = parse_test_write(source, accepted, sizeof(accepted) - 1);
    for (uint32_t scalar = 0; executable != NULL && scalar < 2; scalar += 1)
    {
        char program[192];
        snprintf(program, sizeof(program), "%s/declspec-accepted-%u", directory, scalar);
        char* build[] = { (char*)executable, "-std=gnu17", scalar ? "--scalar" : "-w", "-w", "-o", program, source, NULL };
        char* run[] = { program, NULL };
        char* text = NULL;
        const int built = written ? check_test_driver_spawn(build, &text) : -1;
        free(text);
        text = NULL;
        const int status = built == 0 ? check_test_driver_spawn(run, &text) : -1;
        free(text);
        char description[192];
        snprintf(description, sizeof(description), "0o and GNU's designators without '=' build and run with clang's values (%s path, status %d)",
                 scalar ? "scalar" : "vector", status);
        check_test_expect(test, status == 42, description);
        unlink(program);
    }
    unlink(source);
}

#include "c_checker_driver_limit_test.c"
#include "c_checker_driver_recovery_test.c"
#include "c_check_cleanup_driver_test.c"
#include "c_check_musttail_driver_test.c"
#include "c_check_label_attribute_driver_test.c"
#include "c_check_resume_driver_test.c"
#include "c_check_semantic_recovery_driver_test.c"
#include "c_check_statement_recovery_driver_test.c"
#include "c_check_type_attribute_recovery_driver_test.c"
#include "c_check_structural_recovery_driver_test.c"
#include "c_check_empty_declaration_driver_test.c"
#include "c_check_block_attribute_driver_test.c"
#include "c_check_resume_corpus_test.c"
#include "c_check_kr_recovery_driver_test.c"
#include "c_check_lane_attribute_driver_test.c"
#include "c_check_generic_builtin_driver_test.c"
#include "c_checker_driver_const_test.c"
#include "c_checker_driver_gnu_test.c"

// An ELF relocatable object's sections and symbols, as the section fixtures compare clang's and -c's (#619): the parts a
// reader of either needs, every offset checked against the file, nothing else (object_view_open's checks are the
// writer's own objects' rules, which clang's needn't keep).
typedef struct CheckTestElf CheckTestElf;
struct CheckTestElf
{
    String8 file;
    const Elf64_Shdr* sections;
    uint32_t section_count;
    const char* section_names;
    uint64_t section_names_size;
    const Elf64_Sym* symbols;
    uint64_t symbol_count;
    const char* symbol_names;
    uint64_t symbol_names_size;
};

static bool check_test_elf_range(String8 file, uint64_t offset, uint64_t size)
{
    return offset <= file.length && size <= file.length - offset;
}

static bool check_test_elf_open(CheckTestElf* elf, String8 file)
{
    *elf = (CheckTestElf){ .file = file };
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file.pointer;
    bool valid = file.pointer != 0 && file.length >= sizeof(Elf64_Ehdr) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 &&
                 header->e_shentsize == sizeof(Elf64_Shdr) && header->e_shnum != 0 && header->e_shstrndx < header->e_shnum &&
                 check_test_elf_range(file, header->e_shoff, (uint64_t)header->e_shnum * sizeof(Elf64_Shdr));
    if (valid)
    {
        elf->sections = (const Elf64_Shdr*)(file.pointer + header->e_shoff);
        elf->section_count = header->e_shnum;
        const Elf64_Shdr* names = &elf->sections[header->e_shstrndx];
        valid = check_test_elf_range(file, names->sh_offset, names->sh_size);
        elf->section_names = valid ? file.pointer + names->sh_offset : 0;
        elf->section_names_size = valid ? names->sh_size : 0;
    }
    for (uint32_t s = 0; valid && s < elf->section_count; s += 1)
    {
        const Elf64_Shdr* table = &elf->sections[s];
        if (table->sh_type == SHT_SYMTAB)
        {
            const Elf64_Shdr* strings = table->sh_link < elf->section_count ? &elf->sections[table->sh_link] : 0;
            valid = strings != 0 && table->sh_entsize == sizeof(Elf64_Sym) && check_test_elf_range(file, table->sh_offset, table->sh_size) &&
                    check_test_elf_range(file, strings->sh_offset, strings->sh_size);
            elf->symbols = valid ? (const Elf64_Sym*)(file.pointer + table->sh_offset) : 0;
            elf->symbol_count = valid ? table->sh_size / sizeof(Elf64_Sym) : 0;
            elf->symbol_names = valid ? file.pointer + strings->sh_offset : 0;
            elf->symbol_names_size = valid ? strings->sh_size : 0;
        }
    }
    return valid;
}

static const char* check_test_elf_string(const char* table, uint64_t size, uint64_t offset)
{
    return offset < size && memchr(table + offset, 0, size - offset) != 0 ? table + offset : 0;
}

static const char* check_test_elf_section_name(const CheckTestElf* elf, uint32_t index)
{
    return index < elf->section_count ? check_test_elf_string(elf->section_names, elf->section_names_size, elf->sections[index].sh_name) : 0;
}

// The section of the function, object or thread-local named `name` that `elf` defines, or 0.
static uint32_t check_test_elf_defined(const CheckTestElf* elf, const char* name)
{
    uint32_t result = 0;
    for (uint64_t i = 1; result == 0 && i < elf->symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &elf->symbols[i];
        const uint8_t type = ELF64_ST_TYPE(symbol->st_info);
        const char* spelling = check_test_elf_string(elf->symbol_names, elf->symbol_names_size, symbol->st_name);
        const bool typed = type == STT_FUNC || type == STT_OBJECT || type == STT_TLS;
        result = typed && spelling != 0 && strcmp(spelling, name) == 0 && symbol->st_shndx != SHN_UNDEF && symbol->st_shndx < SHN_LORESERVE &&
                 symbol->st_shndx < elf->section_count ? symbol->st_shndx : 0;
    }
    return result;
}

// Late attributes are diagnosed at each attribute, with a note at the definition rather than an intervening
// declaration. Compare exact source coordinates and wording with clang for both checker paths.
static void check_test_driver_late_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source_text[] =
        "static int f(void) __attribute__((used));\n"
        "static int f(void){return 1;}\n"
        "static int f(void) __attribute__((used, constructor(100)));\n"
        "static int f(void) __attribute__((constructor(200),used));\n";
    static const char* const messages[] = {
        ":3:41: warning: attribute declaration must precede definition [-Wignored-attributes]",
        ":4:35: warning: attribute declaration must precede definition [-Wignored-attributes]",
        ":2:12: note: previous definition is here",
    };
    char source[256], object[256];
    snprintf(source, sizeof(source), "%s/late-attributes.c", directory);
    snprintf(object, sizeof(object), "%s/late-attributes.o", directory);
    const bool written = parse_test_write(source, source_text, sizeof(source_text) - 1);
    for (uint32_t route = 0; route < 3; route += 1)
    {
        char* arguments[] = { (char*)(route == 0 ? clang : executable), "-std=gnu17", route == 0 ? "-fno-caret-diagnostics" : route == 1 ? "--scalar" : "-std=gnu17",
                              "-c", "-o", object, source, NULL };
        char* text = NULL;
        const int status = written ? check_test_driver_spawn(arguments, &text) : -1;
        bool match = status == 0 && text != NULL;
        for (uint32_t i = 0; match && i < BUSTER_ARRAY_LENGTH(messages); i += 1)
        {
            match &= strstr(text, messages[i]) != NULL;
        }
        const char* first_note = match ? strstr(text, messages[2]) : NULL;
        match &= first_note != NULL && strstr(first_note + strlen(messages[2]), messages[2]) != NULL;
        check_test_expect(test, match, route == 0 ? "clang late attribute diagnostic locations" : route == 1 ?
                          "scalar late attribute diagnostic locations" : "vector late attribute diagnostic locations");
        free(text);
    }
    unlink(object);
    unlink(source);
}

// Syntax-only: alias symbol emission is separate. Literal note coordinates distinguish the alias/body definitions
// from every intervening prototype, across repeated ignored attributes and independent linked entities.
static void check_test_driver_alias_late_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source_text[] =
        "int g(void){return 1;}\n"
        "int h(void) __attribute__((alias(\"g\")));\n"
        "int h(void);\n"
        "int h(void);\n"
        "int h(void) __attribute__((used));\n"
        "int h(void);\n"
        "int h(void) __attribute__((used));\n"
        "int body(void){return 2;}\n"
        "int body(void);\n"
        "int body(void) __attribute__((used));\n";
    static const char* const messages[] = {
        ":5:28: warning: attribute declaration must precede definition [-Wignored-attributes]",
        ":7:28: warning: attribute declaration must precede definition [-Wignored-attributes]",
        ":10:31: warning: attribute declaration must precede definition [-Wignored-attributes]",
        ":2:5: note: previous definition is here",
        ":8:5: note: previous definition is here",
        ": note: previous definition is here",
    };
    static const uint32_t counts[] = { 1, 1, 1, 2, 1, 3 };
    _Static_assert(BUSTER_ARRAY_LENGTH(messages) == BUSTER_ARRAY_LENGTH(counts), "every diagnostic has an expected count");
    char source[256];
    snprintf(source, sizeof(source), "%s/alias-late-attributes.c", directory);
    const bool written = parse_test_write(source, source_text, sizeof(source_text) - 1);
    for (uint32_t route = 0; route < 3; route += 1)
    {
        char* arguments[] = { (char*)(route == 0 ? clang : executable), "-std=gnu17", route == 0 ? "-fno-caret-diagnostics" : route == 1 ? "--scalar" : "-std=gnu17",
                              "-fsyntax-only", source, NULL };
        char* text = NULL;
        const int status = written ? check_test_driver_spawn(arguments, &text) : -1;
        bool match = status == 0 && text != NULL;
        for (uint32_t i = 0; match && i < BUSTER_ARRAY_LENGTH(messages); i += 1)
        {
            uint32_t found = 0;
            const char* occurrence = text;
            while ((occurrence = strstr(occurrence, messages[i])) != NULL)
            {
                found += 1;
                occurrence += strlen(messages[i]);
            }
            match = found == counts[i];
        }
        check_test_expect(test, match, route == 0 ? "clang alias and body definition note origins" : route == 1 ?
                          "scalar alias and body definition note origins" : "vector alias and body definition note origins");
        free(text);
    }
    unlink(source);
}

// Definitions and redeclarations use the same GNU89 rule in -c and direct linking as their predefined macro reports.
static void check_test_driver_inline_modes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source_text[] =
        "__inline__ int plain(int x){return x+1;} extern __inline__ int ext(int x){return x+2;}"
        "__inline__ int prior(int); extern __inline__ int prior(int x){return x+3;}"
        "extern __inline__ int after(int x){return x+4;} __inline__ int after(int);"
        "extern __inline__ __attribute__((gnu_inline)) int attr(int x){return x+5;}"
        "__inline__ __attribute__((gnu_inline)) int attrprior(int); extern __inline__ int attrprior(int x){return x+7;}"
        "extern __inline__ __attribute__((gnu_inline)) int attraft(int x){return x+8;} __inline__ int attraft(int);"
        "extern __inline__ int block(int x){return x+9;}"
        "static __inline__ int local(int x){return x+6;}"
        "static int late_used(void){return 1;} static int late_used(void) __attribute__((used,retain,constructor,destructor));"
        "static int late_weak(void){return 1;} static int late_weak(void) __attribute__((weak));"
        "static int early_used(void) __attribute__((used)); static int early_used(void){return 1;} static int early_used(void) __attribute__((used));"
        "__inline__ int late_gnu(int x){return x;} __inline__ int late_gnu(int x) __attribute__((gnu_inline));"
        "int use(int x){__inline__ int block(int);return plain(x)+ext(x)+prior(x)+after(x)+attr(x)+attrprior(x)+attraft(x)+block(x)+local(x);}\n";
    static const char* const names[] = { "plain", "ext", "prior", "after", "attr", "attrprior", "attraft", "block", "local", "late_used", "late_weak", "early_used", "late_gnu", "use" };
    static const struct { const char* standard; const char* flag; const char* last; } modes[] = {
        { "-std=gnu89", "-w", "-w" }, { "-std=gnu89", "-fno-gnu89-inline", "-w" },
        { "-std=c89", "-fgnu89-inline", "-w" }, { "-std=c17", "-fgnu89-inline", "-w" },
        { "-std=c17", "-w", "-w" }, { "-std=c17", "-fno-gnu89-inline", "-w" },
        { "-std=c17", "-fgnu89-inline", "-fno-gnu89-inline" },
        { "-std=c17", "-fno-gnu89-inline", "-fgnu89-inline" },
    };
    char source[256], ours_path[256], clang_path[256];
    snprintf(source, sizeof(source), "%s/inline-modes.c", directory);
    snprintf(ours_path, sizeof(ours_path), "%s/inline-ours.o", directory);
    snprintf(clang_path, sizeof(clang_path), "%s/inline-clang.o", directory);
    const bool written = parse_test_write(source, source_text, sizeof(source_text) - 1);
    for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(modes); m += 1)
    {
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char* ours[] = { (char*)executable, (char*)modes[m].standard, (char*)modes[m].flag, (char*)modes[m].last,
                             scalar ? "--scalar" : "-w", "-c", "-o", ours_path, source, NULL };
            char* oracle[] = { (char*)clang, (char*)modes[m].standard, (char*)modes[m].flag, (char*)modes[m].last, "-w", "-c", "-o", clang_path, source, NULL };
            char* text = NULL;
            const int ours_status = written ? check_test_driver_spawn(ours, &text) : -1;
            free(text); text = NULL;
            const int clang_status = written ? check_test_driver_spawn(oracle, &text) : -1;
            free(text);
            Arena arena = arena_reserve(UINT64_C(1) << 24);
            CheckTestElf ours_elf = { 0 }, clang_elf = { 0 };
            const bool read = ours_status == 0 && clang_status == 0 && arena.base != NULL &&
                              check_test_elf_open(&ours_elf, object_test_read_file(&arena, ours_path)) &&
                              check_test_elf_open(&clang_elf, object_test_read_file(&arena, clang_path));
            for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(names); n += 1)
            {
                char description[256];
                snprintf(description, sizeof(description), "GNU inline mode %s %s %s %s %s matches clang", modes[m].standard, modes[m].flag, modes[m].last,
                         scalar ? "scalar" : "vector", names[n]);
                check_test_expect(test, read && (check_test_elf_defined(&ours_elf, names[n]) != 0) ==
                                               (check_test_elf_defined(&clang_elf, names[n]) != 0), description);
            }
            arena_release(&arena);
        }
    }
    static const char direct_source[] = "inline int f(void){return 7;} int main(void){return f();}\n";
    char direct_path[256];
    snprintf(direct_path, sizeof(direct_path), "%s/inline-direct", directory);
    const bool direct_written = parse_test_write(source, direct_source, sizeof(direct_source) - 1);
    char* direct[] = { (char*)executable, "-std=gnu89", "-o", direct_path, source, NULL };
    char* text = NULL;
    const int built = direct_written ? check_test_driver_spawn(direct, &text) : -1;
    free(text); text = NULL;
    char* run[] = { direct_path, NULL };
    const int status = built == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    check_test_expect(test, status == 7, "GNU89 plain inline definition links and runs in the direct path");
    // A same-translation-unit GNU inline definition and the ordinary definition after it: the ordinary one is the
    // external definition, in GNU89 and with -fgnu89-inline or gnu_inline in later modes.
    static const struct
    {
        const char* name;
        const char* standard;
        const char* flag;
        const char* source;
        int status;
    } same_unit[] = {
        { "GNU89 extern inline then ordinary", "-std=gnu89", "-w",
          "extern inline int same(int x){return x+1;}\nint same(int x){return x+2;}\nint same_user(int x){return same(x);}\nint main(void){return same(6);}\n", 8 },
        { "C17 -fgnu89-inline extern inline then ordinary", "-std=c17", "-fgnu89-inline",
          "extern inline int same(int x){return x+1;}\nint same(int x){return x+2;}\nint same_user(int x){return same(x);}\nint main(void){return same(6);}\n", 8 },
        { "C17 gnu_inline extern inline then ordinary", "-std=c17", "-w",
          "extern inline __attribute__((gnu_inline)) int same(int x){return x+1;}\nint same(int x){return x+2;}\nint same_user(int x){return same(x);}\nint main(void){return same(6);}\n", 8 },
    };
    for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(same_unit); m += 1)
    {
        char same_source[256];
        snprintf(same_source, sizeof(same_source), "%s/inline-same-%u.c", directory, m);
        const bool same_written = parse_test_write(same_source, same_unit[m].source, strlen(same_unit[m].source));
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char same_executable[256];
            snprintf(same_executable, sizeof(same_executable), "%s/inline-same-%u-%u", directory, m, scalar);
            char* same_build[] = { (char*)executable, (char*)same_unit[m].standard, (char*)same_unit[m].flag, scalar ? "--scalar" : "-w",
                                   "-o", same_executable, same_source, NULL };
            char* same_run[] = { same_executable, NULL };
            char* same_text = NULL;
            const int same_built = same_written ? check_test_driver_spawn(same_build, &same_text) : -1;
            free(same_text); same_text = NULL;
            const int same_status = same_built == 0 ? check_test_driver_spawn(same_run, &same_text) : -1;
            free(same_text);
            char description[256];
            snprintf(description, sizeof(description), "%s %s links and the ordinary definition runs", same_unit[m].name, scalar ? "scalar" : "vector");
            check_test_expect(test, same_status == same_unit[m].status, description);
            unlink(same_executable);
        }
        unlink(same_source);
    }
    // The same shape is still a redefinition where GNU inline doesn't apply.
    static const char same_conflict[] = "extern inline int same(void){return 1;} int same(void){return 2;}\n";
    char conflict_source[256];
    snprintf(conflict_source, sizeof(conflict_source), "%s/inline-conflict.c", directory);
    const bool conflict_written = parse_test_write(conflict_source, same_conflict, sizeof(same_conflict) - 1);
    char* conflict[] = { (char*)executable, "-std=c17", "-fsyntax-only", conflict_source, NULL };
    char* conflict_text = NULL;
    const int conflict_status = conflict_written ? check_test_driver_spawn(conflict, &conflict_text) : -1;
    const bool conflict_said = conflict_text != NULL && strstr(conflict_text, "redefinition of 'same'") != NULL;
    free(conflict_text);
    check_test_expect(test, conflict_status == 1 && conflict_said, "C17 keeps the extern inline followed by a definition a redefinition");
    unlink(conflict_source);

    static const char header_user[] = "extern inline int value(void){return 99;} int main(void){return value();}\n";
    static const char external_body[] = "int value(void){return 7;}\n";
    char second[256], second_object[256];
    snprintf(second, sizeof(second), "%s/inline-other.c", directory);
    snprintf(second_object, sizeof(second_object), "%s/inline-other.o", directory);
    const bool pair_written = parse_test_write(source, header_user, sizeof(header_user) - 1) &&
                              parse_test_write(second, external_body, sizeof(external_body) - 1);
    char* first_compile[] = { (char*)executable, "-std=gnu89", "-c", "-o", ours_path, source, NULL };
    char* second_compile[] = { (char*)executable, "-std=gnu89", "-c", "-o", second_object, second, NULL };
    char* link[] = { (char*)clang, "-o", direct_path, ours_path, second_object, NULL };
    text = NULL;
    const int first = pair_written ? check_test_driver_spawn(first_compile, &text) : -1;
    free(text); text = NULL;
    const int other = first == 0 ? check_test_driver_spawn(second_compile, &text) : -1;
    free(text); text = NULL;
    const int linked = other == 0 ? check_test_driver_spawn(link, &text) : -1;
    free(text); text = NULL;
    const int paired = linked == 0 ? check_test_driver_spawn(run, &text) : -1;
    free(text);
    check_test_expect(test, paired == 7, "GNU89 extern inline references the external definition across objects");
    unlink(second); unlink(second_object);
    unlink(source); unlink(ours_path); unlink(clang_path); unlink(direct_path);
}

// The section named `name` of `elf`, or 0.
static uint32_t check_test_elf_named(const CheckTestElf* elf, const char* name)
{
    uint32_t result = 0;
    for (uint32_t s = 1; result == 0 && s < elf->section_count; s += 1)
    {
        const char* spelling = check_test_elf_section_name(elf, s);
        result = spelling != 0 && strcmp(spelling, name) == 0 ? s : 0;
    }
    return result;
}

// Whether -c's object `ours` puts each function, object and thread-local it defines (but the compiler's own .L names)
// in a section of the name, type, flags and alignment that clang's object `theirs` of the same source puts it in; and,
// with `named`, has that section as clang's has it, with its size and, with `bytes`, its contents: a file-scope
// assembly's section, which no symbol shows. A description of the first difference goes in `error`.
static bool check_test_driver_same_sections(const char* ours_path, const char* theirs_path, const char* named, bool bytes, char* error, uint64_t error_size)
{
    Arena arena = arena_reserve(UINT64_C(1) << 30);
    CheckTestElf ours = { 0 };
    CheckTestElf theirs = { 0 };
    bool same = arena.base != 0 && check_test_elf_open(&ours, object_test_read_file(&arena, ours_path)) &&
                check_test_elf_open(&theirs, object_test_read_file(&arena, theirs_path));
    snprintf(error, error_size, "%s", same ? "" : "an object that doesn't read");
    for (uint64_t i = 1; same && i < ours.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &ours.symbols[i];
        const uint8_t type = ELF64_ST_TYPE(symbol->st_info);
        const char* name = check_test_elf_string(ours.symbol_names, ours.symbol_names_size, symbol->st_name);
        if ((type == STT_FUNC || type == STT_OBJECT || type == STT_TLS) && name != 0 && strncmp(name, ".L", 2) != 0 && symbol->st_shndx != SHN_UNDEF &&
            symbol->st_shndx < ours.section_count)
        {
            const uint32_t theirs_index = check_test_elf_defined(&theirs, name);
            const Elf64_Shdr* a = &ours.sections[symbol->st_shndx];
            const Elf64_Shdr* b = theirs_index != 0 ? &theirs.sections[theirs_index] : 0;
            const char* a_name = check_test_elf_section_name(&ours, symbol->st_shndx);
            const char* b_name = theirs_index != 0 ? check_test_elf_section_name(&theirs, theirs_index) : 0;
            same = b != 0 && a_name != 0 && b_name != 0 && strcmp(a_name, b_name) == 0 && a->sh_type == b->sh_type && a->sh_flags == b->sh_flags &&
                   a->sh_addralign == b->sh_addralign;
            if (!same)
            {
                snprintf(error, error_size, "%s in %s (type %u, flags %#llx, aligned to %llu), clang's in %s (type %u, flags %#llx, aligned to %llu)", name,
                         a_name != 0 ? a_name : "?", a->sh_type, (unsigned long long)a->sh_flags, (unsigned long long)a->sh_addralign, b_name != 0 ? b_name : "none",
                         b != 0 ? b->sh_type : 0, b != 0 ? (unsigned long long)b->sh_flags : 0, b != 0 ? (unsigned long long)b->sh_addralign : 0);
            }
        }
    }
    if (same && named != 0)
    {
        const uint32_t a_index = check_test_elf_named(&ours, named);
        const uint32_t b_index = check_test_elf_named(&theirs, named);
        const Elf64_Shdr* a = a_index != 0 ? &ours.sections[a_index] : 0;
        const Elf64_Shdr* b = b_index != 0 ? &theirs.sections[b_index] : 0;
        same = a != 0 && b != 0 && a->sh_type == b->sh_type && a->sh_flags == b->sh_flags && a->sh_addralign == b->sh_addralign &&
               (!bytes || (a->sh_size == b->sh_size && (a->sh_type == SHT_NOBITS ||
                                                         (check_test_elf_range(ours.file, a->sh_offset, a->sh_size) &&
                                                          check_test_elf_range(theirs.file, b->sh_offset, b->sh_size) &&
                                                          memcmp(ours.file.pointer + a->sh_offset, theirs.file.pointer + b->sh_offset, a->sh_size) == 0))));
        if (!same)
        {
            snprintf(error, error_size, "%s: ours %s (type %u, flags %#llx, aligned to %llu, %llu bytes), clang's %s (type %u, flags %#llx, aligned to %llu, %llu bytes)",
                     named, a != 0 ? "present" : "missing", a != 0 ? a->sh_type : 0, a != 0 ? (unsigned long long)a->sh_flags : 0,
                     a != 0 ? (unsigned long long)a->sh_addralign : 0, a != 0 ? (unsigned long long)a->sh_size : 0, b != 0 ? "present" : "missing",
                     b != 0 ? b->sh_type : 0, b != 0 ? (unsigned long long)b->sh_flags : 0, b != 0 ? (unsigned long long)b->sh_addralign : 0,
                     b != 0 ? (unsigned long long)b->sh_size : 0);
        }
    }
    arena_release(&arena);
    return same;
}

// #1250: the admitted exact q/Q subset must survive source-to-object emission.
// Compare a named data section with clang for both checker paths and both routes.
static void check_test_driver_quad_literals(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
#if BUSTER_CHECK_HAVE_QUAD_READER
    static const char code[] =
        "__float128 values[] __attribute__((section(\".quad_literals\"))) = {\n"
        "  0.0q, -0.0Q, 1.5q, -0x1.8p0Q, 0x1.0000000000000002p0q,\n"
        "  0x1p-16445Q, 0x1.fffffffffffffffep16383Q };\n";
    const char* arguments[] = { "-std=gnu17" };
    const CheckCorpusSymbols oracle = { .clang = clang, .arguments = arguments, .argument_count = BUSTER_ARRAY_LENGTH(arguments), .directory = directory };
    char source[PATH_MAX], theirs[PATH_MAX], ours[PATH_MAX];
    snprintf(source, sizeof(source), "%s/quad-literals.c", directory);
    snprintf(theirs, sizeof(theirs), "%s/quad-literals-clang.o", directory);
    snprintf(ours, sizeof(ours), "%s/quad-literals.o", directory);
    const bool reference = parse_test_write(source, code, sizeof(code) - 1) && check_corpus_clang_object(&oracle, source, theirs);
    check_test_expect(test, reference, "quad literals: clang writes the reference object");
    const uint32_t routes = executable != NULL ? 4u : 2u;
    for (uint32_t route = 0; reference && route < routes; route += 1)
    {
        CheckCorpusOracle checked = { 0 };
        uint32_t warnings = 0;
        const bool built = check_test_driver_run(route >= 2 ? executable : NULL, source, ours, (route & 1) != 0, false, true, false, &checked, &warnings) &&
                           !checked.rejected;
        char difference[512] = { 0 }, description[1024];
        const bool same = built && check_test_driver_same_sections(ours, theirs, ".quad_literals", true, difference, sizeof(difference));
        snprintf(description, sizeof(description), "quad literals: route %u writes clang's exact data bytes (%s)", route, built ? difference : checked.message);
        check_test_expect(test, same, description);
        unlink(ours);
    }
    unlink(source);
    unlink(theirs);
#else
    (void)test;
    (void)clang;
    (void)executable;
    (void)directory;
#endif
}

// #619's #pragma clang section against clang: each line's settings and errors, at clang's places with its messages, and
// the section type conflicts between settings and section attributes in clang's order, by -fsyntax-only. What the checker
// refuses by name instead: a name in a place clang reads expanded that was a macro's in the unit, and an error at the
// line's end. Then -c of the units clang takes: each function and variable in the section its attribute or a function's
// text setting gives it, and the file-scope assembly's sections (check_program_sections, check_program_asm_plan_file),
// which must be clang's -O0 -c's of the same source (check_test_driver_same_sections); and the refusals by name: a
// static-storage definition under an active object setting (conservatively, until the emission selects its section
// kind), and a section whose name gives it another kind or type.
#include "c_check_symbol_driver_test.c"
#include "c_check_asm_definition_driver_test.c"
#include "c_check_symbol_pragma_driver_test.c"
#include "c_check_alias_driver_test.c"
#include "c_check_common_driver_test.c"
#include "c_check_common_selection_test.c"
#include "c_check_common_tree_test.c"
#include "c_check_noderef_driver_test.c"
#include "c_check_attribute_effects_driver_test.c"
#include "c_check_attribute_arguments_driver_test.c"
#include "c_check_attribute_checks_driver_test.c"
#include "c_check_patchable_driver_test.c"
#include "c_check_cpu_dispatch_driver_test.c"
#include "c_check_target_versions_driver_test.c"
#include "c_check_parse_note_driver_test.c"
#include "c_check_operand_driver_test.c"
#include "c_check_conversion_driver_test.c"
#include "c_check_convention_driver_test.c"
#include "c_check_interrupt_driver_test.c"
#include "c_check_asm_constraint_driver_test.c"
#include "c_check_register_variable_driver_test.c"
#include "c_check_tier1_driver_test.c"
#include "c_check_tier0_inline_driver_test.c"
#include "c_check_tier1_if_convert_test.c"
#include "c_check_string_pool_driver_test.c"
#include "c_check_frame_pointer_driver_test.c"
#include "c_check_stack_clash_driver_test.c"
#include "c_check_fence_driver_test.c"

// Whether the objects at `ours_path` and `theirs_path` both read and their .text sections have the same bytes, into *same:
// false when either doesn't read or has no .text, with `error` saying so.
static bool check_test_driver_same_text(const char* ours_path, const char* theirs_path, bool* same, char* error, uint64_t error_size)
{
    Arena arena = arena_reserve(UINT64_C(1) << 30);
    CheckTestElf ours = { 0 };
    CheckTestElf theirs = { 0 };
    bool valid = arena.base != 0 && check_test_elf_open(&ours, object_test_read_file(&arena, ours_path)) &&
                 check_test_elf_open(&theirs, object_test_read_file(&arena, theirs_path));
    const uint32_t ours_index = valid ? check_test_elf_named(&ours, ".text") : 0;
    const uint32_t theirs_index = valid ? check_test_elf_named(&theirs, ".text") : 0;
    const Elf64_Shdr* a = ours_index != 0 ? &ours.sections[ours_index] : 0;
    const Elf64_Shdr* b = theirs_index != 0 ? &theirs.sections[theirs_index] : 0;
    valid = valid && a != 0 && b != 0 && a->sh_type == SHT_PROGBITS && b->sh_type == SHT_PROGBITS &&
            check_test_elf_range(ours.file, a->sh_offset, a->sh_size) && check_test_elf_range(theirs.file, b->sh_offset, b->sh_size);
    *same = valid && a->sh_size == b->sh_size && memcmp(ours.file.pointer + a->sh_offset, theirs.file.pointer + b->sh_offset, a->sh_size) == 0;
    snprintf(error, error_size, "%s", !valid ? "an object or its .text doesn't read" :
             *same ? "" : ".text differs");
    arena_release(&arena);
    return valid;
}

// #1132: standard -O options set preprocessor macros but must not implicitly enable Tier 1
// before its code-quality and run-time gates pass. The fixture's Tier 1 code differs from
// Tier 0's on both paths, so each path's --tier1 control proves the no--O baseline is Tier 0;
// every -O spelling must then produce exactly that baseline's .text. Conversely, --tier1
// requests Tier 1 even when -O0 is present, as its report and the control's .text show.
static void check_test_driver_optimization_tier(CheckTest* test, const char* executable, const char* directory)
{
    static const char code[] =
        "int optimization_mapping(int x)\n"
        "{\n"
        "    int y = x + 1;\n"
        "    int z = y * 3;\n"
        "    return z - y;\n"
        "}\n";
    static const char* const levels[] = { NULL, "-O0", "-O1", "-O2", "-O3", "-Os", "-Oz", "-Og", "-Ofast", "-O" };
    static const char* const tier1_option[] = { "--tier1" };
    char source[PATH_MAX], baseline[PATH_MAX], control[PATH_MAX], candidate[PATH_MAX];
    snprintf(source, sizeof(source), "%s/optimization-tier.c", directory);
    snprintf(baseline, sizeof(baseline), "%s/optimization-tier-base.o", directory);
    snprintf(control, sizeof(control), "%s/optimization-tier-tier1.o", directory);
    snprintf(candidate, sizeof(candidate), "%s/optimization-tier-test.o", directory);
    const bool written = parse_test_write(source, code, sizeof(code) - 1);
    check_test_expect(test, written, "optimization tier: source fixture is available");
    const uint32_t routes = executable != NULL ? 2u : 1u;
    for (uint32_t route = 0; written && route < routes; route += 1)
    {
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const char* driver = route != 0 ? executable : NULL;
            CheckCorpusOracle oracle = { 0 };
            uint32_t warnings = 0;
            const bool base = check_test_driver_run_with(driver, source, baseline, scalar != 0, false, true, false, NULL, 0, &oracle, &warnings) &&
                              !oracle.rejected;
            char description[1024];
            snprintf(description, sizeof(description), "optimization tier: route %u scalar %u compiles Tier 0 baseline", route, scalar);
            check_test_expect(test, base, description);
            const bool tier1 = base && check_test_driver_run_with(driver, source, control, scalar != 0, false, true, false, tier1_option, 1, &oracle, &warnings) &&
                               !oracle.rejected;
            char mismatch[512] = { 0 };
            bool same = false;
            const bool read = tier1 && check_test_driver_same_text(baseline, control, &same, mismatch, sizeof(mismatch));
            const bool distinct = read && !same;
            snprintf(description, sizeof(description), "optimization tier: route %u scalar %u --tier1 differs from the default Tier 0 .text (%s)", route, scalar,
                     !tier1 ? "the control didn't compile" : !read ? mismatch : distinct ? "distinct" : "identical");
            check_test_expect(test, distinct, description);
            for (uint32_t level = 0; distinct && level < BUSTER_ARRAY_LENGTH(levels); level += 1)
            {
                const char* const* options = levels[level] != NULL ? &levels[level] : NULL;
                const uint32_t option_count = options != NULL ? 1u : 0u;
                const bool built = check_test_driver_run_with(driver, source, candidate, scalar != 0, false, true, false, options, option_count, &oracle, &warnings) &&
                                   !oracle.rejected;
                same = false;
                mismatch[0] = 0;
                const bool compared = built && check_test_driver_same_text(baseline, candidate, &same, mismatch, sizeof(mismatch));
                const bool matches = compared && same;
                const char* const detail = matches ? "" : !built ? "the candidate didn't compile" : mismatch;
                snprintf(description, sizeof(description), "optimization tier: route %u scalar %u %s keeps Tier 0 .text%s%s", route, scalar,
                         levels[level] != NULL ? levels[level] : "(no -O)", matches ? "" : ": ", detail);
                check_test_expect(test, matches, description);
                unlink(candidate);
            }
            unlink(control);
            unlink(baseline);
        }
    }
    if (written && executable != NULL)
    {
        CheckCorpusOracle oracle = { 0 };
        uint32_t warnings = 0;
        const bool base = check_test_driver_run_with(executable, source, baseline, false, false, true, false, NULL, 0, &oracle, &warnings) &&
                          !oracle.rejected;
        const bool tier1 = base && check_test_driver_run_with(executable, source, control, false, false, true, false, tier1_option, 1, &oracle, &warnings) &&
                           !oracle.rejected;
        char mismatch[512] = { 0 };
        bool same = false;
        const bool read = tier1 && check_test_driver_same_text(baseline, control, &same, mismatch, sizeof(mismatch));
        const bool distinct = read && !same;
        check_test_expect(test, distinct, "optimization tier: the vector --tier1 control differs from the default Tier 0 .text");
        const char* const levels_with_tier1[] = { "-O0", "-O2" };
        for (uint32_t i = 0; distinct && i < BUSTER_ARRAY_LENGTH(levels_with_tier1); i += 1)
        {
            char* const args[] = { (char*)executable, "-c", (char*)levels_with_tier1[i], "--tier1", "--tier1-report",
                                   "-o", candidate, source, NULL };
            char* report = NULL;
            const int status = check_test_driver_spawn(args, &report);
            uint64_t tier1_functions = 0;
            const bool selected = status == 0 && check_test_tier1_field(report, "tier1", &tier1_functions) && tier1_functions != 0;
            char description[1024];
            snprintf(description, sizeof(description), "optimization tier: %s --tier1 selects Tier 1 explicitly", levels_with_tier1[i]);
            check_test_expect(test, selected, description);
            same = false;
            mismatch[0] = 0;
            const bool compared = selected && check_test_driver_same_text(control, candidate, &same, mismatch, sizeof(mismatch));
            const bool matches = compared && same;
            const char* const detail = matches ? "" : !selected ? "the build didn't select Tier 1" : mismatch;
            snprintf(description, sizeof(description), "optimization tier: %s --tier1 keeps the control's .text%s%s", levels_with_tier1[i],
                     matches ? "" : ": ", detail);
            check_test_expect(test, matches, description);
            free(report);
            unlink(candidate);
        }
        unlink(control);
        unlink(baseline);
    }
    unlink(source);
}

static void check_test_driver_section_pragmas(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const char refused[] = "construct is not yet supported by the type checker";
    static const char placed[] = "unsupported: a definition under active '#pragma clang section' settings";
    static const char kind[] = "unsupported: a definition in a section whose name gives it another kind";
    static const char typed[] = "unsupported: a section name with an unsupported ELF type or reserved family";
    static const struct
    {
        const char* name;
        const char* source;
        // Where -fsyntax-only refuses the line instead of clang's outcome (`refused`), or 0.
        uint32_t refused_line;
        // -c of a unit clang takes: the line of its refusal and its message, or 0 for an object whose sections are clang's.
        uint32_t c_line;
        const char* c_message;
        // Compare the first error's place alone: the checker's message for another error is its own.
        bool line_only;
        // A section the object must have as clang's has it, and whether with its size and bytes too.
        const char* section;
        bool bytes;
    } fixtures[] =
    {
        { "text around a definition", "#pragma clang section text = \".text.x\"\nint f(void) { return 1; }\n#pragma clang section text = \"\"\nint main(void) { return f() - 1; }\n" },
        { "every kind in one line", "#pragma clang section text = \".t\" data = \".d\" bss = \".b\" rodata = \".r\" relro = \".rr\"\nint f(void) { return 1; }\nint g = 1;\n", 0, 3, placed },
        { "a line of no settings", "#pragma clang section\nint f(void) { return 1; }\n" },
        { "adjacent strings join", "#pragma clang section text = \".a\" \".b\"\nint f(void) { return 1; }\n" },
        { "a name read as written after the first kind", "#define data 1\n#pragma clang section text = \".a\" data = \".b\"\nint f(void) { return data; }\n" },
        { "a cleared kind places nothing", "#pragma clang section text = \".x\"\n#pragma clang section text = \"\"\nint f(void) { return 1; }\n" },
        { "data places a block's static", "#pragma clang section data = \".d\"\nint f(void) { static int s = 1; return s; }\n", 0, 2, placed },
        { "bss places a tentative definition", "#pragma clang section bss = \".x\"\nint z;\nint f(void) { return z; }\n", 0, 2, placed },
        // Clang puts these initialized writable objects in ordinary .data: the bounded refusal deliberately includes
        // every static-storage definition under any active object setting until the emission selects its final section
        // kind.
        { "bss conservatively refuses an ordinary data definition", "#pragma clang section bss = \".only_bss\"\nint x = 1;\nint f(void) { return x; }\n", 0, 2,
          placed },
        { "rodata conservatively refuses an ordinary data definition", "#pragma clang section rodata = \".only_rodata\"\nint x = 1;\nint f(void) { return x; }\n", 0, 2,
          placed },
        { "a text setting leaves static objects alone", "#pragma clang section text = \".only_text\"\nint x = 1;\n" },
        { "cleared object settings leave static objects alone", "#pragma clang section bss = \".b\" rodata = \".r\"\n#pragma clang section bss = \"\" rodata = \"\"\nint x = 1;\n" },
        { "rodata and relro share their flags", "#pragma clang section rodata = \".x\" relro = \".x\"\nconst int c __attribute__((section(\".x\"))) = 1;\n" },
        { "an extern declaration unifies nothing", "extern int e __attribute__((section(\".x\")));\nint f(void) __attribute__((section(\".x\")));\nint f(void) { return e; }\n" },
        { "a function's own section", "__attribute__((section(\".text.mine\"))) int f(void) { return 1; }\nint main(void) { return f() - 1; }\n" },
        { "a declaration's section passes to the definition", "int f(void) __attribute__((section(\".text.f\")));\nint f(void) { return 1; }\nint main(void) { return f() - 1; }\n" },
        { "an attribute over a setting", "#pragma clang section text = \".t\"\n__attribute__((section(\".own\"))) int f(void) { return 1; }\nint g(void) { return f() + 1; }\n" },
        { "a function in another kind's section", "int f(void) __attribute__((section(\".data\")));\nint f(void) { return 1; }\nint g = 1;\nint main(void) { return f() + g - 2; }\n" },
        { "a variable's own section", "int g __attribute__((section(\".data.mine\"))) = 1;\nint main(void) { return g - 1; }\n" },
        { "an empty section leaves tentative zero fill in bss", "int z __attribute__((section(\"\")));\nint main(void) { return z; }\n" },
        { "an empty section leaves initialized zero fill in bss", "int z __attribute__((section(\"\"))) = 0;\nint main(void) { return z; }\n" },
        { "an empty section leaves tls zero fill in tbss", "_Thread_local int z __attribute__((section(\"\")));\nint main(void) { return z; }\n" },
        { "an empty section clears an inherited name", "extern int z __attribute__((section(\".data.x\")));\nint z __attribute__((section(\"\")));\nint main(void) { return z; }\n" },
        { "an empty section overrides a text setting", "#pragma clang section text = \".text.x\"\nint f(void) __attribute__((section(\"\")));\nint f(void) { return 17; }\nint main(void) { return f() - 17; }\n" },
        { "an empty section overrides an object setting", "#pragma clang section bss = \".bss.x\"\nint z __attribute__((section(\"\")));\nint main(void) { return z; }\n" },
        { "the first section in one attribute list", "int x __attribute__((section(\".a\"), section(\".b\"))) = 17;\nint main(void) { return x - 17; }\n" },
        { "the first section across a declaration's groups", "__attribute__((section(\".a\"))) int x __attribute__((section(\".b\"))) = 17;\n"
                                                                   "int main(void) { return x - 17; }\n" },
        { "a later declaration replaces its section", "extern int x __attribute__((section(\".a\")));\nextern int x __attribute__((section(\".b\")));\n"
                                                        "int x = 17;\nint main(void) { return x - 17; }\n" },
        { "a function's first section in one list", "int f(void) __attribute__((section(\".a\"), section(\".b\")));\n"
                                                    "int f(void) { return 17; }\nint main(void) { return f() - 17; }\n" },
        { "a zero-filled variable's section is data", "int z __attribute__((section(\".mine\"))) = 0;\nint main(void) { return z; }\n" },
        { "a zero-filled variable in a .bss", "int z __attribute__((section(\".bss.mine\")));\nint main(void) { return z; }\n" },
        { "a constant's section", "const int c __attribute__((section(\".rodata.mine\"))) = 1;\nint main(void) { return c - 1; }\n" },
        { "a pointer constant's section", "static int x;\nint* const p __attribute__((section(\".relro.mine\"))) = &x;\nint main(void) { return *p; }\n" },
        { "thread-locals' sections", "_Thread_local int t __attribute__((section(\".tdata.mine\"))) = 1;\n_Thread_local int u __attribute__((section(\".tbss.mine\")));\n"
                                     "_Thread_local int v __attribute__((section(\".mine.tls\")));\nint main(void) { return t + u + v - 1; }\n" },
        { "a block's static's section", "int f(void) { static int s __attribute__((section(\".data.s\"))) = 1; return s; }\n" },
        { "main.c's file-scope assembly", "__asm__(\".section .text.buster_checker,\\\"ax\\\",@progbits\\n\\t.p2align 12\\n\\t.text\");\n"
                                          "#pragma clang section text = \".text.buster_checker\"\nint f(void) { return 1; }\n#pragma clang section text = \"\"\n"
                                          "int main(void) { return f() - 1; }\n", 0, 0, NULL, false, ".text.buster_checker" },
        { "file-scope assembly's bytes", "__asm__(\".section .rodata.mine,\\\"a\\\",@progbits\\n.byte 1, 2, 3\\n.text\");\nint main(void) { return 0; }\n",
          0, 0, NULL, false, ".rodata.mine", true },
        { "two file-scope statements in order", "__asm__(\".section .data.asm,\\\"aw\\\",@progbits\\n.byte 1\");\n__asm__(\".section .data.asm,\\\"aw\\\",@progbits\\n.byte 2\");\n"
                                                "int main(void) { return 0; }\n", 0, 0, NULL, false, ".data.asm", true },
        { "file-scope assembly's label", "__asm__(\".globl answer\\nanswer: .byte 42\");\nint main(void) { return 0; }\n" },
        // (An entry the startup calls: 0 would crash clang's executable too.)
        { "file-scope assembly's init array", "int ran;\nvoid buster_init(void) { ran = 1; }\n__asm__(\".section .init_array,\\\"aw\\\"\\n.quad buster_init\");\n"
                                              "int main(void) { return ran - 1; }\n", 0, 0, NULL, false, ".init_array", true },
        { "an initialized variable in a .bss", "int g __attribute__((section(\".bss.g\"))) = 1;\nint main(void) { return g - 1; }\n", 0, 1, kind },
        { "a variable in a .tdata", "int g __attribute__((section(\".tdata.g\"))) = 1;\nint main(void) { return g - 1; }\n", 0, 1, kind },
        { "a note's section", "int g __attribute__((section(\".note.g\"))) = 1;\nint main(void) { return g - 1; }\n", 0, 1, typed },
        { "the reserved llvm dot family includes ordinary PROGBITS names", "int g __attribute__((section(\".llvm.custom\"))) = 1;\nint main(void) { return g - 1; }\n", 0, 1, typed },
        { "llvm underscore is outside the reserved dot family", "int g __attribute__((section(\".llvm_custom\"))) = 1;\nint main(void) { return g - 1; }\n" },
        { "an unknown kind", "#pragma clang section foo = \".x\"\nint f(void) { return 1; }\n" },
        { "a missing '='", "#pragma clang section text \".x\"\nint f(void) { return 1; }\n" },
        { "a name that isn't a string", "#pragma clang section text = x\nint f(void) { return 1; }\n" },
        { "a wide string", "#pragma clang section text = L\".x\"\nint f(void) { return 1; }\n" },
        { "a token after a setting", "#pragma clang section text = \".x\" extra\nint f(void) { return 1; }\n" },
        { "a comma after a setting", "#pragma clang section text = \".x\",\nint f(void) { return 1; }\n" },
        { "a keyword for a kind", "#pragma clang section int = \".x\"\nint f(void) { return 1; }\n" },
        { "a macro for the name", "#define S \".x\"\n#pragma clang section text = S\nint f(void) { return 1; }\n" },
        { "two settings' flags", "#pragma clang section text = \".x\"\n#pragma clang section data = \".x\"\nint f(void) { return 1; }\n" },
        { "a cleared setting keeps its name's flags", "#pragma clang section text = \".x\"\n#pragma clang section text = \"\"\n#pragma clang section data = \".x\"\n" },
        { "a _Pragma's setting", "_Pragma(\"clang section text = \\\".x\\\"\")\nint f(void) { return 1; }\n#pragma clang section data = \".x\"\n" },
        { "two attributes", "__attribute__((section(\".x\"))) int g = 1;\n__attribute__((section(\".x\"))) int f(void) { return 1; }\n" },
        { "empty section attributes retain mandatory conflicts", "int f(void) __attribute__((section(\"\")));\nint x __attribute__((section(\"\"))) = 1;\n" },
        { "the first section in one declaration determines conflicts", "int f(void) __attribute__((section(\".a\"), section(\".b\")));\n"
                                                                      "int x __attribute__((section(\".a\"))) = 1;\n" },
        { "an attribute after a setting", "#pragma clang section data = \".x\"\n__attribute__((section(\".x\"))) int f(void) { return 1; }\n" },
        { "a variable's attribute after text", "#pragma clang section text = \".x\"\nint g __attribute__((section(\".x\"))) = 1;\n" },
        { "a setting after an attribute", "__attribute__((section(\".x\"))) int g = 1;\n#pragma clang section text = \".x\"\n" },
        { "rodata after text", "#pragma clang section text = \".x\"\nconst int c = 1; int f(void) { return c; }\n#pragma clang section rodata = \".x\"\nconst int d = 2;\n" },
        { "a line's error before the next line's conflict", "#pragma clang section text = \".x\" foo = \".y\"\n#pragma clang section data = \".x\"\n" },
        { "bss's zero fill", "#pragma clang section bss = \".x\"\nint z __attribute__((section(\".x\"))) = 0;\n" },
        { "a tentative definition at the unit's end", "int e __attribute__((section(\".x\")));\nint f(void) __attribute__((section(\".x\")));\n" },
        { "a function before a tentative definition", "int f(void) __attribute__((section(\".x\")));\nint e __attribute__((section(\".x\")));\n" },
        { "a definition inherits its tentative one's", "int g __attribute__((section(\".x\")));\nint g = 1;\nint f(void) __attribute__((section(\".x\")));\n" },
        { "the last tentative definition", "int e __attribute__((section(\".x\")));\nint e;\nint f(void) __attribute__((section(\".x\")));\n" },
        { "a definition inherits an extern's", "extern int g __attribute__((section(\".x\")));\nint f(void) __attribute__((section(\".x\")));\nint g = 1;\n" },
        { "a constant variable is read-only", "int f(void) __attribute__((section(\".x\")));\nconst int c __attribute__((section(\".x\"))) = 1;\n" },
        { "a block's static", "int f(void) { static int s __attribute__((section(\".x\"))) = 1; return s; }\nint h(void) __attribute__((section(\".x\")));\n" },
        { "a static tentative definition after a setting", "#pragma clang section text = \".x\"\nint f(void);\nint f(void) __attribute__((section(\".y\")));\nint f(void) { return 1; }\nstatic int v __attribute__((section(\".x\")));\n" },
        { "a conflict before a body's error", "#pragma clang section text = \".x\"\n#pragma clang section data = \".x\"\nint f(void) { return missing; }\n" },
        { "a body's error before a conflict", "int f(void) { return missing; }\n#pragma clang section text = \".x\"\n#pragma clang section data = \".x\"\n", 0, 0, NULL, true },
        { "an initializer's error before a conflict", "#pragma clang section text = \".x\"\nint g = missing;\n#pragma clang section data = \".x\"\n", 0, 0, NULL, true },
        { "a macro for the first kind", "#define T text\n#pragma clang section T = \".x\"\nint f(void) { return 1; }\n", 2 },
        { "a name that was a macro's", "#define text 1\n#undef text\n#pragma clang section text = \".x\"\nint f(void) { return 1; }\n", 3 },
        { "a macro for an '='", "#define EQ =\n#pragma clang section text EQ \".x\"\nint f(void) { return 1; }\n", 2 },
        { "a missing '=' at the line's end", "#pragma clang section text\nint f(void) { return 1; }\n", 1 },
        { "a missing name at the line's end", "#pragma clang section text =\nint f(void) { return 1; }\n", 1 },
    };
    const char* arguments[] = { "-std=gnu17" };
    const CheckCorpusSymbols oracle_object = { .clang = clang, .arguments = arguments, .argument_count = BUSTER_ARRAY_LENGTH(arguments), .directory = directory };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char theirs[192];
        char description[1024];
        snprintf(source, sizeof(source), "%s/section-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/section-%u.o", directory, i);
        snprintf(theirs, sizeof(theirs), "%s/section-%u-clang.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        snprintf(description, sizeof(description), "%s: clang's outcome", fixtures[i].name);
        check_test_expect(test, ready, description);
        // clang's object, for the units -c writes one of.
        const bool objects = ready && !oracle.rejected && fixtures[i].refused_line == 0 && fixtures[i].c_line == 0;
        const bool compiled_theirs = objects && check_corpus_clang_object(&oracle_object, source, theirs);
        snprintf(description, sizeof(description), "%s: clang's object", fixtures[i].name);
        check_test_expect(test, !objects || compiled_theirs, description);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, NULL, (route & 1) != 0, false, true, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, route %u: first error %u:%u '%s', clang %u:%u '%s'", fixtures[i].name, route, checked.line,
                     checked.column, checked.message, oracle.rejected ? oracle.line : 0, oracle.rejected ? oracle.column : 0, oracle.message);
            if (fixtures[i].refused_line != 0)
            {
                check_test_expect(test, ran && checked.rejected && checked.line == fixtures[i].refused_line && strcmp(checked.message, refused) == 0, description);
            }
            else
            {
                check_test_expect(test, ran && checked.rejected == oracle.rejected &&
                                  (!oracle.rejected || (checked.line == oracle.line && checked.column == oracle.column &&
                                                        (fixtures[i].line_only || strcmp(checked.message, oracle.message) == 0))), description);
            }
            if (ran && !oracle.rejected && fixtures[i].refused_line == 0)
            {
                unlink(object);
                const bool compiled = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, true, lazy, &checked, &warnings);
                snprintf(description, sizeof(description), "%s, route %u: -c's first error %u '%s', expected %u", fixtures[i].name, route, checked.line,
                         checked.message, fixtures[i].c_line);
                check_test_expect(test, compiled && (fixtures[i].c_line != 0 ? checked.rejected && checked.line == fixtures[i].c_line &&
                                                                               strcmp(checked.message, fixtures[i].c_message) == 0 && access(object, F_OK) != 0 :
                                                                               !checked.rejected && access(object, F_OK) == 0), description);
                if (compiled_theirs && fixtures[i].c_line == 0 && !checked.rejected)
                {
                    char difference[512];
                    const bool same = check_test_driver_same_sections(object, theirs, fixtures[i].section, fixtures[i].bytes, difference, sizeof(difference));
                    snprintf(description, sizeof(description), "%s, route %u: the object's sections against clang's: %s", fixtures[i].name, route, difference);
                    check_test_expect(test, same, description);
                }
                // The direct path's executable of a unit with a main, which exits with 0: its named sections are in their
                // classes' (check_program_sections).
                if (route >= 2 && fixtures[i].c_line == 0 && strstr(fixtures[i].source, "int main(") != NULL)
                {
                    char program[192];
                    snprintf(program, sizeof(program), "%s/section-%u", directory, i);
                    char* const vector_arguments[] = { (char*)executable, "-std=gnu17", "-o", program, source, NULL };
                    char* const scalar_arguments[] = { (char*)executable, "--scalar", "-std=gnu17", "-o", program, source, NULL };
                    char* output = NULL;
                    const int linked = check_test_driver_spawn(route == 2 ? vector_arguments : scalar_arguments, &output);
                    char* const run_arguments[] = { program, NULL };
                    char* run_output = NULL;
                    const int status = linked == 0 ? check_test_driver_spawn(run_arguments, &run_output) : -1;
                    snprintf(description, sizeof(description), "%s, route %u: the direct path's executable links (%d: %s) and exits with 0 (%d)", fixtures[i].name,
                             route, linked, output != NULL ? output : "", status);
                    check_test_expect(test, linked == 0 && status == 0, description);
                    free(output);
                    free(run_output);
                    unlink(program);
                }
            }
        }
        unlink(object);
        unlink(theirs);
        unlink(source);
    }
}

// #619's #pragma clang optimize (main.c's checker tests compile between `optimize off` and `on`, #681) against clang: the
// lines clang takes, anywhere in a unit, and its two errors at clang's places with its messages, by -fsyntax-only. What
// the checker refuses by name instead: an argument or an extra token that was a macro's name in the unit (clang reads
// both expanded), and a missing argument, an error at the line's end. Then -c of the units clang takes writes the object:
// the pragma's optnone changes nothing a Tier 0 compile emits.
static void check_test_driver_optimize_pragmas(CheckTest* test, const char* clang, const char* executable, const char* directory, bool lazy)
{
    static const char refused[] = "construct is not yet supported by the type checker";
    static const struct
    {
        const char* name;
        const char* source;
        // Where -fsyntax-only refuses the line instead of clang's outcome (`refused`), or 0.
        uint32_t refused_line;
        // Compare the first error's place alone: the checker's message for another error is its own.
        bool line_only;
    } fixtures[] =
    {
        { "off and on around a definition", "#pragma clang optimize off\nint f(void) { return 1; }\n#pragma clang optimize on\nint main(void) { return f() - 1; }\n" },
        { "off to the unit's end", "int g(void) { return 2; }\n#pragma clang optimize off\nstatic int f(void) { return 1; }\nint main(void) { return f() + g() - 3; }\n" },
        { "in a body", "int f(void)\n{\n#pragma clang optimize off\n    return 1;\n}\n" },
        { "in a declaration", "int f(void)\n#pragma clang optimize off\n{ return 1; }\n" },
        { "a _Pragma", "_Pragma(\"clang optimize off\")\nint f(void) { return 1; }\n" },
        { "an always_inline function", "#pragma clang optimize off\nstatic inline __attribute__((always_inline)) int f(void) { return 1; }\nint g(void) { return f(); }\n" },
        { "an unknown argument", "#pragma clang optimize maybe\nint f(void) { return 1; }\n" },
        { "an argument in capitals", "#pragma clang optimize OFF\nint f(void) { return 1; }\n" },
        { "a number", "#pragma clang optimize 1\nint f(void) { return 1; }\n" },
        { "a string", "#pragma clang optimize \"off\"\nint f(void) { return 1; }\n" },
        { "a keyword", "#pragma clang optimize int\nint f(void) { return 1; }\n" },
        { "an extra name", "#pragma clang optimize off extra\nint f(void) { return 1; }\n" },
        { "an extra punctuator", "#pragma clang optimize off ;\nint f(void) { return 1; }\n" },
        { "on then off", "#pragma clang optimize on off\nint f(void) { return 1; }\n" },
        { "a _Pragma's error", "_Pragma(\"clang optimize maybe\")\nint f(void) { return 1; }\n" },
        { "an error after a body's", "int f(void) { return missing; }\n#pragma clang optimize maybe\n", 0, true },
        { "a body's error after an off", "#pragma clang optimize off\nint f(void) { return missing; }\n", 0, true },
        { "the second line's error", "#pragma clang optimize off\nint f(void) { return 1; }\n#pragma clang optimize of\n" },
        { "a macro for the argument", "#define OFF off\n#pragma clang optimize OFF\nint f(void) { return 1; }\n", 2 },
        { "an argument that was a macro's", "#define off 1\n#undef off\n#pragma clang optimize off\nint f(void) { return 1; }\n", 3 },
        { "an extra token of a macro", "#define NOTHING\n#pragma clang optimize off NOTHING\nint f(void) { return 1; }\n", 2 },
        { "a missing argument", "#pragma clang optimize\nint f(void) { return 1; }\n", 1 },
    };
    const char* arguments[] = { "-std=gnu17" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        char description[512];
        snprintf(source, sizeof(source), "%s/optimize-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/optimize-%u.o", directory, i);
        CheckCorpusOracle oracle = { 0 };
        const bool ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                           check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
        snprintf(description, sizeof(description), "%s: clang's outcome", fixtures[i].name);
        check_test_expect(test, ready, description);
        const uint32_t routes = executable != NULL ? 4 : 2;
        for (uint32_t route = 0; ready && route < routes; route += 1)
        {
            CheckCorpusOracle checked = { 0 };
            uint32_t warnings = 0;
            const bool ran = check_test_driver_run(route >= 2 ? executable : NULL, source, NULL, (route & 1) != 0, false, true, lazy, &checked, &warnings);
            snprintf(description, sizeof(description), "%s, route %u: first error %u:%u '%s', clang %u:%u '%s'", fixtures[i].name, route, checked.line,
                     checked.column, checked.message, oracle.rejected ? oracle.line : 0, oracle.rejected ? oracle.column : 0, oracle.message);
            if (fixtures[i].refused_line != 0)
            {
                check_test_expect(test, ran && checked.rejected && checked.line == fixtures[i].refused_line && strcmp(checked.message, refused) == 0, description);
            }
            else
            {
                check_test_expect(test, ran && checked.rejected == oracle.rejected &&
                                  (!oracle.rejected || (checked.line == oracle.line && checked.column == oracle.column &&
                                                        (fixtures[i].line_only || strcmp(checked.message, oracle.message) == 0))), description);
            }
            if (ran && !oracle.rejected && fixtures[i].refused_line == 0)
            {
                unlink(object);
                const bool compiled = check_test_driver_run(route >= 2 ? executable : NULL, source, object, (route & 1) != 0, false, true, lazy, &checked, &warnings);
                snprintf(description, sizeof(description), "%s, route %u: -c's first error %u '%s'", fixtures[i].name, route, checked.line, checked.message);
                check_test_expect(test, compiled && !checked.rejected && access(object, F_OK) == 0, description);
            }
        }
        unlink(object);
        unlink(source);
    }
}
#include "c_check_initializer_warning_test.c"
#include "c_check_warning_driver_test.c"
#include "c_check_redefinition_driver_test.c"
#include "c_check_logical_unused_driver_test.c"
#include "c_check_condition_driver_test.c"
#include "c_check_implicit_value_driver_test.c"
#include "c_check_chained_comparison_driver_test.c"
#include "c_check_builtin_pointer_sign_driver_test.c"
#include "c_check_shift_op_parentheses_driver_test.c"
#include "c_check_string_plus_int_driver_test.c"
#include "c_check_asm_label_auto_driver_test.c"
#include "c_check_empty_unit_driver_test.c"
#include "c_check_conditional_mismatch_driver_test.c"
#include "c_check_generic_attribute_driver_test.c"
#include "c_check_unused_result_driver_test.c"
#include "c_check_address_space_driver_test.c"
#include "c_check_sizeof_array_decay_driver_test.c"
#include "c_check_format_bitint_driver_test.c"
#include "c_check_shift_count_driver_test.c"
#include "c_check_int_in_bool_driver_test.c"
#include "c_check_bitfield_driver_test.c"
#include "c_check_register_driver_test.c"
#include "c_check_enum_arithmetic_driver_test.c"
#include "c_check_vla_fold_driver_test.c"
#include "c_check_for_loop_driver_test.c"
#include "c_check_enum_cast_driver_test.c"
#include "c_check_type_safety_driver_test.c"
#include "c_check_objc_bridge_driver_test.c"
#include "c_check_swift_attribute_driver_test.c"
#include "c_check_swift_name_driver_test.c"
#include "c_check_cfi_salt_driver_test.c"
#include "c_check_objc_c23_driver_test.c"
#include "c_check_unevaluated_driver_test.c"
#include "c_check_pointer_cast_driver_test.c"
#include "c_check_unsequenced_driver_test.c"
#include "c_check_sign_compare_driver_test.c"
#include "c_check_type_sugar_driver_test.c"
#include "c_check_call_count_driver_test.c"

// The terminal CLI may leave mappings for exit, but callers that go on compiling must retain the reusable command's
// cleanup contract. Warm each route once so lazy thread scratch and libc state are outside the comparison.
static void check_test_driver_reuse(CheckTest* test, const char* directory)
{
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/reused-driver.c", directory);
    const char text[] = "int reused(int value) { return value + 1; }\n";
    const bool written = parse_test_write(source, text, sizeof(text) - 1);
    check_test_expect(test, written, "reusable driver fixture is available");
    char* check_arguments[] = { "-fsyntax-only", source };
    char* pp_arguments[] = { "-E", "-P", "-o", "/dev/null", source };
    for (uint32_t route = 0; written && route < 2; route += 1)
    {
        const int warm = route == 0 ? run_check_command(BUSTER_ARRAY_LENGTH(check_arguments), check_arguments, true) :
                                     main_preprocessor_command_reusable(BUSTER_ARRAY_LENGTH(pp_arguments), pp_arguments);
        const uint64_t before = parse_test_status_bytes("VmSize:");
        const int again = route == 0 ? run_check_command(BUSTER_ARRAY_LENGTH(check_arguments), check_arguments, true) :
                                      main_preprocessor_command_reusable(BUSTER_ARRAY_LENGTH(pp_arguments), pp_arguments);
        const uint64_t after = parse_test_status_bytes("VmSize:");
        check_test_expect(test, warm == 0 && again == 0 && before != 0 && after == before,
                          "reusable source and preprocessor commands release their mappings before returning");
        char* invalid_arguments[] = { "--buster-invalid-option" };
        const int saved = dup(STDERR_FILENO);
        const int quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
        int rejected = -1;
        if (saved >= 0 && quiet >= 0 && dup2(quiet, STDERR_FILENO) == STDERR_FILENO)
        {
            rejected = route == 0 ? run_check_command(BUSTER_ARRAY_LENGTH(invalid_arguments), invalid_arguments, true) :
                                    main_preprocessor_command_reusable(BUSTER_ARRAY_LENGTH(invalid_arguments), invalid_arguments);
        }
        if (saved >= 0)
        {
            dup2(saved, STDERR_FILENO);
            close(saved);
        }
        if (quiet >= 0)
        {
            close(quiet);
        }
        check_test_expect(test, rejected == 1 && parse_test_status_bytes("VmSize:") == before,
                          "a reusable command's rejected options release mappings after an earlier successful compile");
    }
    unlink(source);
}

// A failed -E -o must not leave the previous contents, truncated, in the output path.
static void check_test_preprocessor_failed_output(CheckTest* test, const char* directory)
{
    char source[PATH_MAX];
    char output[PATH_MAX];
    snprintf(source, sizeof(source), "%s/failed-e.c", directory);
    snprintf(output, sizeof(output), "%s/failed-e.i", directory);
    const char text[] = "#error boom\n";
    const char previous[] = "PREVIOUS\n";
    const bool written = parse_test_write(source, text, sizeof(text) - 1) && parse_test_write(output, previous, sizeof(previous) - 1);
    char* arguments[] = { "-E", "-o", output, source };
    const int saved = dup(STDERR_FILENO);
    const int quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
    int status = 0;
    if (saved >= 0 && quiet >= 0 && dup2(quiet, STDERR_FILENO) == STDERR_FILENO)
    {
        status = main_preprocessor_command_reusable(BUSTER_ARRAY_LENGTH(arguments), arguments);
    }
    if (saved >= 0)
    {
        dup2(saved, STDERR_FILENO);
        close(saved);
    }
    if (quiet >= 0)
    {
        close(quiet);
    }
    check_test_expect(test, written && status != 0 && access(output, F_OK) != 0, "a failed -E -o removes the output file");
    unlink(source);
    unlink(output);
}

// isfpclass of a vector of long doubles (#148): clang accepts it, its result a vector of __int128 whose lanes are the
// element's 16 bytes. The lowering's int and long long lane stores can't hold those, so it must refuse by name rather
// than write garbage; a vector of floats still compiles, as the guard against refusing too much.
static void check_test_driver_float_classify(CheckTest* test, const char* executable, const char* directory)
{
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(source, sizeof(source), "%s/classify-vector.c", directory);
    snprintf(object, sizeof(object), "%s/classify-vector.o", directory);
    static const char refused[] = "typedef long double v2ld __attribute__((vector_size(32)));\n"
                                  "typedef __int128 v2i __attribute__((vector_size(32)));\n"
                                  "v2i f(v2ld v)\n{\nreturn __builtin_isfpclass(v, 0x108);\n}\n";
    static const char accepted[] = "typedef float v4f __attribute__((vector_size(16)));\n"
                                   "typedef int v4i __attribute__((vector_size(16)));\n"
                                   "v4i f(v4f v)\n{\nreturn __builtin_isfpclass(v, 0x108);\n}\n";
    bool written = parse_test_write(source, refused, sizeof(refused) - 1);
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* text = NULL;
        char* const vector_arguments[] = { (char*)executable, "-march=x86-64-v4", "-c", "-o", object, source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-march=x86-64-v4", "-c", "-o", object, source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, "unsupported: isfpclass of a vector whose elements are not 4 or 8 bytes") != NULL,
                          path == 0 ? "isfpclass of a vector of long doubles is refused, not miscompiled" :
                                      "isfpclass of a vector of long doubles is refused, not miscompiled, scalar");
        free(text);
    }
    written = written && parse_test_write(source, accepted, sizeof(accepted) - 1);
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        char* text = NULL;
        char* const vector_arguments[] = { (char*)executable, "-march=x86-64-v4", "-c", "-o", object, source, NULL };
        char* const scalar_arguments[] = { (char*)executable, "--scalar", "-march=x86-64-v4", "-c", "-o", object, source, NULL };
        const int status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &text);
        check_test_expect(test, status == 0,
                          path == 0 ? "isfpclass of a vector of floats still compiles" : "isfpclass of a vector of floats still compiles, scalar");
        free(text);
    }
    unlink(source);
    unlink(object);
}

#include "c_check_hardening_driver_test.c"
#include "c_check_deep_driver_test.c"

static int run_checker_driver_tests(int argc, char** argv)
{
    // SOURCE_DATE_EPOCH fixed where it isn't set, so that the compilers' runs it compares with can be kept
    // (oracle_epoch_begin, oracle_cache.c).
    const bool epoch = oracle_epoch_begin();
    CheckTest test = { 0 };
    const char* clang = "clang";
    const char* executable = NULL;
    bool lazy = false;
    bool sign_compare_only = false;
    // --cpu-dispatch-only: the cpu_specific and cpu_dispatch section alone (#1447).
    bool cpu_dispatch_only = false;
    // --target-versions-only: GNU multiversioning's target and target_clones section alone (#1838).
    bool target_versions_only = false;
    // --semantic-recovery-only: the semantic error recovery section alone (#1447).
    bool semantic_recovery_only = false;
    // --type-sugar-only: the type sugar section alone (#1447).
    bool type_sugar_only = false;
    // --asm-definitions-only: what inline assembly defines, against clang's objects and builds, alone (#1848).
    bool asm_definitions_only = false;
    // --call-counts-only: the call argument count section alone (#1860).
    bool call_counts_only = false;
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
        }
        else if (strcmp(argv[i], "--executable") == 0 && i + 1 < argc)
        {
            executable = argv[++i];
        }
        else if (strcmp(argv[i], "--sign-compare-only") == 0)
        {
            sign_compare_only = true;
        }
        else if (strcmp(argv[i], "--cpu-dispatch-only") == 0)
        {
            cpu_dispatch_only = true;
        }
        else if (strcmp(argv[i], "--target-versions-only") == 0)
        {
            target_versions_only = true;
        }
        else if (strcmp(argv[i], "--semantic-recovery-only") == 0)
        {
            semantic_recovery_only = true;
        }
        else if (strcmp(argv[i], "--type-sugar-only") == 0)
        {
            type_sugar_only = true;
        }
        else if (strcmp(argv[i], "--asm-definitions-only") == 0)
        {
            asm_definitions_only = true;
        }
        else if (strcmp(argv[i], "--call-counts-only") == 0)
        {
            call_counts_only = true;
        }
        else if (strcmp(argv[i], "--lazy-system-headers") == 0)
        {
            lazy = true;
        }
        else if (strcmp(argv[i], "--in-process") == 0)
        {
            check_driver_run_modes.in_process = true;
        }
        else if (strcmp(argv[i], "--clang-server") == 0 && i + 1 < argc)
        {
            check_driver_run_modes.server.path = argv[++i];
        }
        else if (strcmp(argv[i], "--differential") == 0)
        {
            check_driver_run_modes.differential = true;
        }
        else if (strcmp(argv[i], "--part") == 0 && i + 1 < argc)
        {
            unsigned index = 0;
            unsigned count = 0;
            int used = 0;
            const char* text = argv[++i];
            options = sscanf(text, "%u/%u%n", &index, &count, &used) == 2 && text[used] == 0 && index >= 1 && index <= count && count <= 64;
            check_driver_part = (CheckDriverPart){ .index = options ? index - 1 : 0, .count = options ? count : 1 };
        }
        else
        {
            options = false;
        }
    }
    const uint32_t focuses = (uint32_t)sign_compare_only + (uint32_t)cpu_dispatch_only + (uint32_t)target_versions_only + (uint32_t)semantic_recovery_only +
                             (uint32_t)type_sugar_only + (uint32_t)asm_definitions_only + (uint32_t)call_counts_only;
    options &= focuses <= 1 && (focuses == 0 || (!lazy && check_driver_part.count == 1));
    // One focused section's alone: the others' gates take `focused` as --sign-compare-only's did.
    const bool focused = focuses != 0;
    check_test_expect(&test, options, "driver test options are recognized");
    // The names the checks run buster and the oracle clang by, which the run modes recognize (#2071).
    check_driver_run_modes.executable = executable;
    check_driver_run_modes.clang = clang;
    check_test_driver_record_start();
    // The library-builtin table is pinned to BUSTER_CLANG_REFERENCE_VERSION's official build: a different clang build is
    // reported with both, and the library comparisons then skip only the names that depend on its table (#1131).
    check_driver_clang_reference = clang_reference_report(clang, check_driver_clang_version, sizeof(check_driver_clang_version));
    check_test_expect(&test, pp_test_clang_reference(), "clang reference: version comparison and mismatch reporting");
    // The process's oracle cache, set up before any check runs clang through it (oracle_cache.c, #542).
    OracleCache* oracle = oracle_cache();
    // With --lazy-system-headers, only the checks that run the driver with -flazy-system-headers: the fixtures below and
    // the six sections that take `lazy` (check_test_driver_named_ucns, _loop_hints, _gnu_scopes, _section_pragmas,
    // _optimize_pragmas and _kr_policy), whose gates below have no `!lazy`. Every other check runs the same commands
    // either way, so the eager command's run of them is the lazy one's too; repeating them took 90% of the lazy
    // command's time. A section that comes to take `lazy` loses the `!lazy` of its gate.
    if (!focused && !lazy && check_test_driver_takes())
    {
        check_test_driver_warning_order(&test);
        check_test_driver_unselected_roots(&test);
        check_test_driver_common_trees(&test);
        check_test_driver_diagnostic_capacity(&test);
        check_test_driver_reports(&test);
        check_test_lower_capacity(&test);
    }
    const CheckDriverFixture fixtures[] =
    {
        {
            .name = "alias on a function definition is rejected at the attribute",
            .source = "int g(void) { return 0; }\n__attribute__((alias(\"g\"))) int h(void) { return 1; }\n",
            .rejected = true,
            .message = "definition 'h' cannot also be an alias",
            .column = 16,
        },
        {
            .name = "a function body after an alias is a redefinition",
            .source = "int g(void) { return 0; }\nint h(void) __attribute__((alias(\"g\")));\nint h(void) { return 1; }\n",
            .rejected = true,
            .message = "redefinition of 'h'",
            .column = 5,
        },
        {
            .name = "K&R definition emits its deprecated-non-prototype warning",
            .source = "int f(a)float a;{return (int)a;}\n",
            .warnings = 1,
        },
        {
            .name = "K&R arity warnings do not block object emission",
            .source = "int f(a)int a;{return a;}int g(void){return f()+f(1,2); }\n",
            .warnings = 3,
        },
        {
            .name = "K&R deprecation suppression leaves the independent arity warning",
            .source = "#pragma clang diagnostic ignored \"-Wdeprecated-non-prototype\"\n"
                      "int f(a)int a;{return a;}int g(void){return (&f)(1,2); }\n",
            .warnings = 1,
        },
        {
            .name = "promoted K&R definition warning blocks object emission",
            .source = "#pragma clang diagnostic error \"-Wdeprecated-non-prototype\"\nint f(a)int a;{return a;}\n",
            .rejected = true,
        },
        {
            .name = "K&R indirect call warning follows source-ordered diagnostic policy",
            .source = "#pragma clang diagnostic ignored \"-Wdeprecated-non-prototype\"\nint f(a)int a;{return a;}\n"
                      "#pragma clang diagnostic error \"-Wdeprecated-non-prototype\"\nint g(void){return (*f)(1,2);}\n",
            .rejected = true,
        },
        {
            .name = "quiet K&R warnings still emit an object",
            .source = "int f(a)int a;{return a;}int g(void){return f(1,2);}\n",
            .quiet = true,
        },
        {
            .name = "K&R source-only sizeof call retains its arity warning",
            .source = "int f(a)int a;{return a;}int g(void){return sizeof(f(1,2));}\n",
            .warnings = 2,
        },
        {
            .name = "K&R source-only grouped and addressed calls retain arity warnings",
            .source = "int f(a)int a;{return a;}int g(void){return sizeof((f)(1,2))+sizeof((&f)(1,2));}\n",
            .warnings = 3,
        },
        {
            .name = "K&R source-only addressed call retains known argument constraints",
            .source = "int f(a)int a;{return a;}int g(void){return sizeof((&f)((int*)0));}\n",
            .rejected = true,
            .warnings = 1,
        },
        {
            .name = "source-only implicit calls do not add non-prototype warnings",
            .source = "#pragma clang diagnostic ignored \"-Wimplicit-function-declaration\"\n"
                      "#pragma clang diagnostic error \"-Wdeprecated-non-prototype\"\n"
                      "int g(void){int i=missing(1);return i+sizeof((&missing)(2))+sizeof((missing)(3));}\n",
        },
        {
            .name = "K&R source-only call obeys warning promotion",
            .source = "#pragma clang diagnostic ignored \"-Wdeprecated-non-prototype\"\n"
                      "int f(a)int a;{return a;}\n#pragma clang diagnostic error \"-Weverything\"\n"
                      "int g(void){return sizeof(f(1,2));}\n",
            .rejected = true,
        },
        {
            "valid functions and relocatable static storage",
            "static int value=7; int *pointer=&value; int f(int x){return x+value;}\n",
            false,
        },
        {
            "static function relocation shares the emitted object symbols",
            "static int helper(void){return 7;} int (*pointer)(void)=helper;\n",
            false,
        },
        {
            "invalid call before later body syntax error",
            "int f(void){\n1();\nint broken=;\n}\n",
            true,
        },
        {
            "invalid original DO body before missing condition",
            "int f(void){do{\n1();\n}\n}\n",
            true,
        },
        {
            "macro error before later body syntax error",
            "#define CALL(x) x()\nint f(void){CALL(1);\nint broken=;\n}\n",
            true,
        },
        {
            "static initializer before later graph error",
            "int *p[1]={\n1\n};\nint f(void){return 1();}\n",
            true,
        },
        {
            "automatic initializer before later missing name",
            "int f(void){\nint *p[1]={1};\nreturn missing;\n}\n",
            true,
        },
        {
            "assembly destination before later missing name",
            "int f(void){\n__asm__(\"\" : \"=r\"(1));\nreturn missing;\n}\n",
            true,
        },
        {
            "duplicate switch case before later missing name",
            "int f(int n){\nswitch(n){case 0:break;\ncase 0:break;}\nreturn missing;\n}\n",
            true,
        },
        {
            "GNU enum folding retains its declaration warning",
            "enum { N=2147483647+1 }; int f(void){return N;}\n",
            false,
            1,
        },
        {
            "GNU assertion folding retains its graph warning",
            "_Static_assert(2147483647+1<0,\"wrap\"); int f(void){return 0;}\n",
            false,
            1,
        },
        {
            "GNU bound warning appears once across reader and graph",
            "int a[(2147483647+1)<0?1:2]; int f(void){return 0;}\n",
            false,
            2,
        },
        {
            "diagnostic pragma suppresses integer overflow",
            "#pragma clang diagnostic ignored \"-Winteger-overflow\"\nenum { N=2147483647+1 }; int f(void){return N;}\n",
            false,
        },
        {
            "diagnostic pragma promotes integer overflow",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { N=2147483647+1 }; int f(void){return N;}\n",
            true,
        },
        {
            "global warning suppression also suppresses promoted warning",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { N=2147483647+1 }; int f(void){return N;}\n",
            false,
            0,
            true,
        },
        {
            "mixed dialect pop restores previous warning state",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\n#pragma GCC diagnostic push\n"
            "#pragma GCC diagnostic ignored \"-Winteger-overflow\"\nenum { A=2147483647+1 };\n"
            "#pragma clang diagnostic pop\nenum { B=2147483647+1 };\n",
            true,
        },
        {
            "explicit warning overrides earlier error state",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\n#pragma clang diagnostic warning \"-Winteger-overflow\"\n"
            "enum { A=2147483647+1 };\n",
            false,
            1,
        },
        {
            "fatal warning promotion blocks emission",
            "#pragma clang diagnostic fatal \"-Winteger-overflow\"\nenum { A=2147483647+1 };\n",
            true,
        },
        {
            "later pragma cannot suppress earlier overflow",
            "enum { A=2147483647+1 };\n#pragma clang diagnostic ignored \"-Winteger-overflow\"\n",
            false,
            1,
        },
        {
            "specific pragma overrides group suppression",
            "#pragma clang diagnostic ignored \"-Wincompatible-pointer-types\"\n"
            "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\n"
            "const int *p; int *q; void f(void){q=p;}\n",
            true,
        },
        {
            "group pragma overrides earlier specific promotion",
            "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\n"
            "#pragma clang diagnostic ignored \"-Wincompatible-pointer-types\"\n"
            "const int *p; int *q; void f(void){q=p;}\n",
            false,
        },
        {
            "initializer warning promotion blocks emission",
            "#pragma clang diagnostic error \"-Wexcess-initializers\"\nint a[1]={1,2};\n",
            true,
        },
        {
            "initializer warning suppression preserves object",
            "#pragma clang diagnostic ignored \"-Wexcess-initializers\"\nint a[1]={1,2};\n",
            false,
        },
        {
            "ordinary constraint stays before later promoted warning",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nint f(void){\n1();\nreturn 2147483647+1;\n}\n",
            true,
        },
        {
            "completed control constraint stays before later promoted warning",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nint f(int n){\nswitch(n){\ncase 0:break;\n"
            "case 0:break;\n}\nenum { A=2147483647+1 }; return A;\n}\n",
            true,
        },
        {
            "promoted warning stays before later ordinary constraint",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\nenum { A=2147483647+1 };\nint f(void){return 1();}\n",
            true,
        },
        {
            "original syntax rejection cannot emit a repaired object",
            "int f(void){int broken=;}\n",
            true,
        },
        {
            "earlier original enum warning survives later syntax error",
            "#pragma clang diagnostic error \"-Winteger-overflow\"\n"
            "int f(void){enum { A=2147483647+1 };\nint broken=;\n}\n",
            true,
        },
        {
            "later promoted initializer warning survives earlier ignored warning and graph error",
            "#pragma clang diagnostic ignored \"-Wexcess-initializers\"\nint a[1]={1,2};\n"
            "#pragma clang diagnostic error \"-Wexcess-initializers\"\nint b[1]={1,2};\n"
            "int f(void){return missing;}\n",
            true,
        },
        {
            "later promoted initializer warning survives planner semantic failure",
            "#pragma clang diagnostic ignored \"-Wexcess-initializers\"\nint a[1]={1,2};\n"
            "#pragma clang diagnostic error \"-Wexcess-initializers\"\nint b[1]={1,2};\n"
            "int *p[1]={1};\n",
            true,
        },
        {
            "fatal warning retains precedence over later parser error",
            "#pragma clang diagnostic fatal \"-Winteger-overflow\"\nenum { A=2147483647+1 };\n"
            "int f(void){int broken=;}\n",
            true,
        },
        {
            "unknown floating pragma state retains its warning",
            "#pragma STDC FENV_ACCESS MAYBE\nint value;\n",
            false,
            1,
        },
        {
            .name = "file-scope GCC unroll names its source pragma at the directive",
            .source = "#pragma GCC unroll 4\nint x;\n",
            .rejected = true,
            .message = "invalid '#pragma GCC unroll' directive",
            .column = 1,
        },
        {
            .name = "a misplaced GCC unroll uses clang's normalized name at the statement",
            .source = "int f(void){\n#pragma GCC unroll 4\nreturn 0;}\n",
            .rejected = true,
            .message = "expected a for, while, or do-while loop to follow '#pragma unroll'",
            .column = 1,
        },
        {
            "unknown complex pragma state can be promoted at end of file",
            "int value;\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\n#pragma STDC CX_LIMITED_RANGE MAYBE\n",
            true,
        },
        {
            "later colocated pragma cannot change an earlier pragma warning",
            "#pragma STDC FENV_ACCESS MAYBE\n#pragma clang diagnostic ignored \"-Wunknown-pragmas\"\nint value;\n",
            false,
            1,
        },
        {
            "earlier policy suppresses unknown pragma warnings",
            "#pragma clang diagnostic ignored \"-Wunknown-pragmas\"\n#pragma STDC FENV_ACCESS MAYBE\nint value;\n",
            false,
        },
        {
            "original pragma in an unfinished expression precedes its syntax failure",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint f(void)\n{\nint value=\n#pragma STDC FENV_ACCESS MAYBE\n;\n}\n",
            true,
        },
        {
            "original pragma precedes a backwards-pointing missing semicolon diagnostic",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint f(void)\n{\nreturn 0\n#pragma STDC FENV_ACCESS MAYBE\n}\n",
            true,
        },
        {
            "original pragma is diagnosed without a repairable declaration",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\n#pragma STDC CX_LIMITED_RANGE MAYBE\n;\n",
            true,
        },
        {
            "macro body pragma warning retains its invocation source",
            "#define CHECK_PRAGMA _Pragma(\"STDC FENV_ACCESS MAYBE\")\n"
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\nCHECK_PRAGMA\n;\n",
            true,
        },
        {
            "direct pragma operator warning retains its invocation source",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\n_Pragma(\"STDC CX_LIMITED_RANGE MAYBE\")\n;\n",
            true,
        },
        {
            "forwarded pragma operator warning retains its invocation source",
            "#define CHECK_PRAGMA _Pragma(\"STDC FENV_ACCESS MAYBE\")\n#define FORWARD(x) x\n"
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\nFORWARD(CHECK_PRAGMA)\n;\n",
            true,
        },
        {
            "physical pragma warning retains its remapped source",
            "#line 71 \"original-pragma.c\"\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\n"
            "int value=\n#pragma STDC FENV_ACCESS MAYBE\n;\n",
            true,
        },
        {
            "original pragma after the syntax lookahead cannot precede it",
            "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=;\n#pragma STDC FENV_ACCESS MAYBE\n",
            true,
        },
        {
            "ignored unfinished-expression pragma leaves the original syntax location",
            "#pragma clang diagnostic ignored \"-Wunknown-pragmas\"\nint f(void)\n{\nint value=\n#pragma STDC FENV_ACCESS MAYBE\n;\n}\n",
            true,
        },
        {
            .name = "recognized pragma cannot interrupt an initializer expression",
            .source = "int f(void)\n{\nint value=\n#pragma STDC FENV_ACCESS ON\n;\n}\n",
            .rejected = true,
        },
        {
            .name = "quiet warnings retain recognized pragma syntax constraints",
            .source = "int value=\n#pragma STDC CX_LIMITED_RANGE DEFAULT\n;\n",
            .rejected = true,
            .quiet = true,
        },
        {
            .name = "misplaced floating pragma retains macro invocation source",
            .source = "#define ACCESS _Pragma(\"STDC FENV_ACCESS ON\")\nvoid f(void)\n{\nint value=0;\nACCESS\nvalue++;\n}\n",
            .rejected = true,
        },
        {
            .name = "misplaced floating pragma survives quiet mode at forwarded invocation",
            .source = "#define ACCESS _Pragma(\"STDC FENV_ACCESS ON\")\n#define FORWARD(x) x\n#line 70 \"placement.c\"\n"
                      "void f(void)\n{\nint value=0;\nFORWARD(ACCESS)\nvalue++;\n}\n",
            .rejected = true,
            .quiet = true,
        },
        // clang's default-error -Wint-conversion, suppressed in a system header: a line marker's system region returns an
        // integer as a pointer (suppressed), and so does its macro, invoked in the user's file (reported there, by its
        // expansion location, which the preprocessor's runs give: pp_system_levels).
        {
            .name = "a system header's macro returns an integer as a pointer in the user's file",
            .source = "# 1 \"sys.h\" 3\nstatic int *in_system(void) { return 1; }\n#define FROM_SYSTEM(x) return x;\n# 10 \"user.c\"\n"
                      "int *in_user(void) { FROM_SYSTEM(2) }\n",
            .rejected = true,
        },
        // A system header's macro invoked in the user's file, with a line marker among its arguments: the body's tokens
        // are where the invocation starts, before the marker, in the user's file, which only the history says (the runs are
        // inexact). The runs alone, or the tokens' own places, would suppress the error.
        {
            .name = "a line marker among a macro's arguments leaves its body in the user's file",
            .source = "# 1 \"sys.h\" 3\n#define F(x) int *f(void) { return 1; } int x;\n# 3 \"user.c\"\nF(\n# 5 \"sys2.h\" 3\ny)\n",
            .rejected = true,
        },
        {
            .name = "late weak on static function and object warns",
            .source = "static int f(void){return 1;}\nint f(void) __attribute__((weak));\n"
                      "static int x=1;\nextern int x __attribute__((weak));\n",
            .warnings = 2,
        },
        {
            .name = "late weak on static function respects promoted warning policy",
            .source = "#pragma clang diagnostic error \"-Wignored-attributes\"\nstatic int f(void){return 1;}\n"
                      "static int f(void) __attribute__((weak));\n",
            .rejected = true,
        },
        {
            .name = "weak before static function definition remains an error",
            .source = "static int f(void);\nstatic int f(void) __attribute__((weak));\nstatic int f(void){return 1;}\n",
            .rejected = true,
        },
        {
            .name = "late function attributes warn at the attributes",
            .source = "static int f(void){return 1;}\nstatic int f(void) __attribute__((used,retain,constructor,destructor));\n",
            .warnings = 4,
        },
        {
            .name = "repeated attribute already on a prior declaration does not warn",
            .source = "static int f(void) __attribute__((used));\nstatic int f(void){return 1;}\nstatic int f(void) __attribute__((used));\n",
        },
        {
            .name = "GNU noreturn does not suppress a later standard noreturn warning",
            .source = "__attribute__((noreturn)) void f(void);\nvoid f(void){for(;;){}}\n[[noreturn]] void f(void);\n",
            .warnings = 1,
        },
        {
            .name = "late gnu_inline requires inline on its declaration",
            .source = "inline int f(void){return 1;}\nint f(void) __attribute__((gnu_inline));\n",
            .warnings = 1,
        },
        {
            .name = "late function attribute respects promoted warning policy",
            .source = "#pragma clang diagnostic error \"-Wignored-attributes\"\nstatic int f(void){return 1;}\nstatic int f(void) __attribute__((used));\n",
            .rejected = true,
        },
        {
            .name = "late assembler label retains its ignored attribute warning",
            .source = "int object=1;\nextern int object asm(\"renamed\");\n",
            .warnings = 1,
        },
        {
            .name = "late assembler label respects promoted attribute group",
            .source = "#pragma clang diagnostic error \"-Wattributes\"\nint object=1;\nextern int object asm(\"renamed\");\n",
            .rejected = true,
        },
        {
            .name = "late assembler label respects ignored attribute policy",
            .source = "#pragma clang diagnostic ignored \"-Wignored-attributes\"\nint object=1;\nextern int object asm(\"renamed\");\n",
        },
        {
            .name = "visibility warning follows earlier switch constraint",
            .source = "int f(int n){switch(n){case 1:;case 1:;}return 0;}\n#pragma GCC visibility push(bad)\n",
            .rejected = true,
            .warnings = 1,
        },
        {
            .name = "visibility error cannot hide earlier switch constraint",
            .source = "int f(int n){switch(n){case 1:;case 1:;}return 0;}\n#pragma GCC visibility pop\n",
            .rejected = true,
        },
        {
            .name = "promoted visibility warning cannot hide earlier switch constraint",
            .source = "int f(int n){switch(n){case 1:;case 1:;}return 0;}\n"
                      "#pragma clang diagnostic error \"-Wignored-attributes\"\n#pragma GCC visibility push(bad)\n",
            .rejected = true,
        },
        {
            .name = "visibility warning inside unfinished initializer retains parser placement",
            .source = "int value=\n#pragma GCC visibility push(bad)\n;\n",
            .rejected = true,
        },
        {
            .name = "legal visibility directive survives an erased malformed body",
            .source = "int f(void){\n#pragma GCC visibility push(hidden)\nreturn 1+;\n}\n",
            .rejected = true,
        },
    };
    char directory[] = "/tmp/buster-checker-driver-XXXXXX";
    const bool temporary = options && mkdtemp(directory) != NULL;
    check_test_expect(&test, temporary, "driver test directory is available");
    if (temporary && sign_compare_only)
    {
        check_test_driver_sign_compare(&test, clang, executable, directory);
    }
    if (temporary && cpu_dispatch_only)
    {
        check_test_driver_cpu_dispatch(&test, clang, executable != NULL ? executable : "./build/compiler", directory);
    }
    if (temporary && target_versions_only)
    {
        check_test_driver_target_versions(&test, clang, executable != NULL ? executable : "./build/compiler", directory);
    }
    if (temporary && semantic_recovery_only)
    {
        check_test_driver_semantic_recovery(&test, clang, executable != NULL ? executable : "./build/compiler", directory);
        check_test_driver_statement_recovery(&test, clang, executable != NULL ? executable : "./build/compiler", directory);
        check_test_driver_type_attribute_recovery(&test, clang, executable != NULL ? executable : "./build/compiler", directory);
        check_test_driver_structural_recovery(&test, clang, executable != NULL ? executable : "./build/compiler", directory);
    }
    if (temporary && type_sugar_only)
    {
        check_test_driver_type_sugar(&test, clang, executable, directory);
    }
    if (temporary && call_counts_only)
    {
        check_test_driver_call_counts(&test, clang, executable, directory);
    }
    if (temporary && asm_definitions_only)
    {
        check_test_driver_asm_definitions(&test, clang, executable != NULL ? executable : "./build/compiler", directory);
    }
    const char* clang_arguments[] = { "-std=gnu17", "-w" };
    if (temporary && !focused && !lazy && check_test_driver_takes())
    {
        check_test_driver_deep(&test, clang, executable, directory);
    }
    for (uint32_t i = 0; temporary && !focused && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        if (check_test_driver_takes())
        {
            char source[192];
            char object[192];
            char description[384];
            snprintf(source, sizeof(source), "%s/%u.c", directory, i);
            snprintf(object, sizeof(object), "%s/%u.o", directory, i);
            CheckCorpusOracle oracle = { 0 };
            const bool oracle_ready = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source)) &&
                                      check_corpus_clang_file(clang, clang_arguments, fixtures[i].quiet ? 2 : 1, source, &oracle);
            check_test_expect(&test, oracle_ready && oracle.rejected == fixtures[i].rejected, fixtures[i].name);
            const uint32_t schedules = executable != NULL ? 4 : 2;
            const uint32_t routes = schedules * 2;
            for (uint32_t route = 0; oracle_ready && route < routes; route += 1)
            {
                unlink(object);
                CheckCorpusOracle checked = { 0 };
                uint32_t warnings = 0;
                const bool external = route % schedules >= 2;
                const bool dump_static = route >= schedules;
                const bool ready = check_test_driver_run(external ? executable : NULL, source, object, (route & 1) != 0, dump_static,
                                                         fixtures[i].quiet, lazy, &checked, &warnings);
                snprintf(description, sizeof(description), "%s, %s %s%s: error line %u, Clang %u", fixtures[i].name,
                         external ? "default CLI" : "direct", (route & 1) != 0 ? "scalar" : "vector", dump_static ? " with static dump" : "", checked.line, oracle.line);
                check_test_expect(&test, ready && checked.rejected == oracle.rejected && checked.line == oracle.line, description);
                if (fixtures[i].message != NULL)
                {
                    snprintf(description, sizeof(description), "%s, %s %s%s: diagnostic and attribute column", fixtures[i].name,
                             external ? "default CLI" : "direct", (route & 1) != 0 ? "scalar" : "vector", dump_static ? " with static dump" : "");
                    check_test_expect(&test, ready && checked.column == fixtures[i].column && checked.column == oracle.column &&
                                      strcmp(checked.message, fixtures[i].message) == 0, description);
                }
                snprintf(description, sizeof(description), "%s, %s %s%s: warnings %u, expected %u", fixtures[i].name,
                         external ? "default CLI" : "direct", (route & 1) != 0 ? "scalar" : "vector", dump_static ? " with static dump" : "", warnings, fixtures[i].warnings);
                check_test_expect(&test, warnings == fixtures[i].warnings, description);
                FILE* emitted = fopen(object, "rb");
                unsigned char magic[4] = { 0 };
                const bool elf = emitted != NULL && fread(magic, 1, sizeof(magic), emitted) == sizeof(magic) &&
                                 memcmp(magic, "\177ELF", sizeof(magic)) == 0;
                check_test_expect(&test, oracle.rejected ? emitted == NULL : elf, "driver emits an ELF object only for accepted original source");
                if (emitted != NULL)
                {
                    fclose(emitted);
                }
                if (fixtures[i].rejected && route == 0)
                {
                    static const char preserved[] = "existing output remains intact";
                    const bool written = parse_test_write(object, preserved, sizeof(preserved));
                    const bool rejected = written && check_test_driver_run(NULL, source, object, false, false, fixtures[i].quiet, lazy, &checked, &warnings) && checked.rejected;
                    FILE* prior = fopen(object, "rb");
                    char bytes[sizeof(preserved) + 1] = { 0 };
                    const size_t length = prior != NULL ? fread(bytes, 1, sizeof(bytes), prior) : 0;
                    check_test_expect(&test, rejected && length == sizeof(preserved) && memcmp(bytes, preserved, sizeof(preserved)) == 0,
                                      "rejected original source preserves an existing output file");
                    if (prior != NULL)
                    {
                        fclose(prior);
                    }
                }
            }
            unlink(object);
            unlink(source);
        }
    }
    if (temporary && !focused)
    {
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_reservation_failure(&test);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_link_failure_messages(&test);
        }
        if (!lazy)
        {
            check_test_driver_error_limits(&test, clang, executable, directory);
        }
        if (!lazy)
        {
            check_test_driver_recovery(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_cleanup_errors(&test, clang, executable, directory);
            check_test_driver_cleanup_chains(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_musttail_errors(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_label_attributes(&test, clang, executable, directory);
            check_test_driver_unknown_attributes(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_resumed_declarations(&test, clang, executable, directory);
            check_test_driver_empty_declarations(&test, clang, executable, directory);
            check_test_driver_block_attributes(&test, clang, executable, directory);
            check_test_corpus_resumed_declarations(&test, clang, directory);
            check_test_driver_kr_recovery(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_lane_attributes(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_semantic_recovery(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_statement_recovery(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_type_attribute_recovery(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_structural_recovery(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_generic_builtins(&test, clang, executable, directory);
            check_test_driver_availability(&test, clang, executable, directory);
            check_test_driver_cpu_supports(&test, clang, executable, directory);
            check_test_driver_builtin_declarations(&test, clang, executable, directory);
            check_test_driver_counted_by(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_const(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_initializer_warnings(&test, clang, executable, directory);
            check_test_driver_initializer_fields(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_default_warnings(&test, clang, executable, directory);
            if (executable != NULL)
            {
                check_test_driver_redefinitions(&test, clang, executable, directory);
            }
            check_test_driver_logical_unused(&test, clang, executable, directory);
            check_test_driver_format_bitints(&test, clang, executable, directory);
            check_test_driver_shift_counts(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_int_in_bool(&test, clang, executable, directory);
            check_test_driver_bitfields(&test, clang, executable, directory);
            check_test_driver_registers(&test, clang, executable, directory);
            check_test_driver_enum_arithmetic(&test, clang, executable, directory);
            check_test_driver_vla_fold(&test, clang, executable, directory);
            check_test_driver_for_loops(&test, clang, executable, directory);
            check_test_driver_enum_casts(&test, clang, executable, directory);
            check_test_driver_unevaluated(&test, clang, executable, directory);
            check_test_driver_pointer_casts(&test, clang, executable, directory);
            check_test_driver_unsequenced(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_conditions(&test, clang, executable, directory);
            check_test_driver_implicit_values(&test, clang, executable, directory);
            check_test_driver_chained_comparisons(&test, clang, executable, directory);
            check_test_driver_builtin_pointer_sign(&test, clang, executable, directory);
            check_test_driver_shift_op_parentheses(&test, clang, executable, directory);
            check_test_driver_string_plus_int(&test, clang, executable, directory);
            check_test_driver_asm_label_auto(&test, clang, executable, directory);
            check_test_driver_empty_unit(&test, clang, executable, directory);
            check_test_driver_conditional_mismatches(&test, clang, executable, directory);
            check_test_driver_generic_attributes(&test, clang, executable, directory);
            check_test_driver_unused_results(&test, clang, executable, directory);
            check_test_driver_address_spaces(&test, clang, executable, directory);
            check_test_driver_sizeof_array_decay(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_type_sugar(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_call_counts(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_lazy_final_warnings(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_reuse(&test, directory);
            check_test_preprocessor_failed_output(&test, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_large_function(&test, clang, executable, directory);
            check_test_driver_large_asm(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_packed_members(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_lazy(&test, clang, executable, directory);
        }
        if (check_test_driver_takes())
        {
            check_test_driver_named_ucns(&test, clang, executable, directory, lazy);
        }
        if (check_test_driver_takes())
        {
            check_test_driver_loop_hints(&test, clang, executable, directory, lazy);
        }
        if (check_test_driver_takes())
        {
            check_test_driver_declspec_literals(&test, clang, executable, directory, lazy);
        }
        if (check_test_driver_takes())
        {
            check_test_driver_gnu_scopes(&test, clang, executable, directory, lazy);
        }
        if (check_test_driver_takes())
        {
            check_test_driver_section_pragmas(&test, clang, executable, directory, lazy);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_quad_literals(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_target_features(&test, clang, executable, directory);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_target_feature_errors(&test, clang, executable, directory);
        }
        if (check_test_driver_takes())
        {
            check_test_driver_optimize_pragmas(&test, clang, executable, directory, lazy);
        }
        if (!lazy && check_test_driver_takes())
        {
            check_test_driver_optimization_tier(&test, executable, directory);
        }
        if (executable != NULL)
        {
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_string_pool(&test, clang, executable, directory);
                check_test_driver_weak_late(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_ignored_pragmas(&test, clang, executable, directory);
                check_test_driver_pack_includes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_libraries(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_visibility(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_symbols(&test, clang, executable, directory);
                check_test_driver_symbol_pragmas(&test, clang, executable, directory);
                check_test_driver_weak_aliases(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_asm_object_symbols(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_asm_definitions(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_aliases(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_multiversion(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_retained(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_commons(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_attribute_effects(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_attribute_arguments(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_unevaluated_strings(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_checks_attributes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_type_safety(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_objc_bridge(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_swift_attributes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_swift_names(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_cfi_salt(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_objc_c23(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_patchable(&test, clang, executable, directory);
                check_test_driver_cpu_dispatch(&test, clang, executable, directory);
                check_test_driver_target_versions(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_parse_notes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_conventions(&test, clang, executable, directory);
                check_test_driver_convention_diagnostics(&test, clang, executable, directory);
                check_test_driver_preserving_frames(&test, clang, executable, directory);
                check_test_driver_interrupt_handlers(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_interrupts(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_asm_constraints(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_asm_clobbers(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_asm_sizes(&test, clang, executable, directory);
                check_test_driver_asm_recovery(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_register_variables(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_implicit(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_unknown_builtins(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_operand_errors(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_conversion_errors(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_common_warnings(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_inline_modes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_late_attributes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_alias_late_attributes(&test, clang, executable, directory);
            }
            if (check_test_driver_takes())
            {
                check_test_driver_kr_policy(&test, clang, executable, directory, lazy);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_returns(&test, clang, executable, directory);
            }
            if (!lazy)
            {
                check_test_driver_return_calls(&test, clang, executable, directory);
            }
            if (!lazy)
            {
                check_test_driver_self_assign_macros(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_fortify(&test, clang, executable, directory);
            }
            if (!lazy)
            {
                check_test_driver_void_conditionals(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_library(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_libgcc(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_complex(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_sockets(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_target_selection(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_mmx(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_halves(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_dump_struct(&test, clang, executable, directory);
                check_test_driver_dump_struct_rest(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_static_chain(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_eh_return(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_dead_vectors(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_switch_tables(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_section_bounds(&test, clang, executable, directory);
                check_test_driver_section_bounds_exact(&test, clang, executable, directory);
                check_test_driver_section_bounds_static(&test, executable, directory);
                check_test_driver_section_bounds_align(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_members(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_array_alignment(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_reproducible(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_rematerialization(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_gvn(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_schedule(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_divide(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_if_convert(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_labels(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_variadic(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_masks(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_narrow(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_lifetimes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_frame_pointer(&test, clang, executable, directory);
                check_test_driver_asm_return(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_stack_clash(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_scopes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_stack(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_float(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_records(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_wide(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_shapes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_tls(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_segments(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_vectors(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_dynamic(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_twice(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_asm(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_asm_masks(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_bmi(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_bits(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_overflow(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_wide_results(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_results256(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_extremes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_frame_address(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_rows(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_shared(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_wide_vectors(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_lanes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_invalid(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_sse(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_vex(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_avx(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_carriers(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_half(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier1_inline(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier0_inline(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_tier0_inline_lanes(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_fences(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_counters(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_helper_report(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_asm_placed(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_asm_unwind(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_volatile_order(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_float_classify(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_asm_symbols(&test, executable, directory);
                check_test_driver_setjmp_held(&test, executable, directory);
                check_test_driver_codegen_refusals(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_cfstrings(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_split_stack(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_damaged_snapshot(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_dependency_identities(&test, directory);
                check_test_dependency_admission(&test, directory);
                check_test_driver_dependencies(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_unwinder_dependency(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_script_path(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_versions(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_option_values(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_linker(&test, clang, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_identification(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_expect(&test, driver_compatibility_tests(executable, clang) == 0, "CC dependency, stdin, probe and link-mode compatibility");
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_expect(&test, driver_mixed_tests(executable, clang) == 0, "mixed C, assembly, object and archive driver");
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_unsupported_codegen(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_hardening(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_preprocess_action(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_layout_options(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_link_layout_error(&test, executable, directory);
            }
            if (!lazy && check_test_driver_takes())
            {
                check_test_driver_time_stages(&test, executable, directory);
                check_test_driver_cpu_pinning(&test, executable, directory);
            }
        }
    }
    if (temporary)
    {
        rmdir(directory);
    }
    check_test_expect(&test, check_driver_part.taken != 0 || focused, "the driver test's part runs some of its checks");
    CheckDriverRunModes* modes = &check_driver_run_modes;
    oracle_server_stop(&modes->server);
    // The server's clang libraries must be the oracle's clang's release (clang_reference_report's name of it).
    check_test_expect(&test, modes->server.path == NULL || modes->served_runs == 0 ||
                             (modes->server.version[0] != 0 && strstr(check_driver_clang_version, modes->server.version) != NULL),
                      "run modes: the clang server started, and its clang is the oracle clang's release");
    check_test_expect(&test, modes->differences == 0, "run modes: every run gave the same status and output both ways");
    printf("checker-driver: run modes: buster %s, clang %s%s%s%s: %" PRIu64 " runs in this process, %" PRIu64 " through the server, %" PRIu64
           " as children; warning sweep: %" PRIu64 " fixtures, %.3f s; %" PRIu64 " differences\n",
           modes->in_process ? "in-process" : "as children", modes->server.path != NULL ? "through the server (" : "as children",
           modes->server.path != NULL ? modes->server.version : "", modes->server.path != NULL ? ")" : "", modes->differential ? ", differential" : "",
           modes->in_process_runs, modes->served_runs, modes->child_runs, modes->sweep_fixtures, (double)modes->sweep_nanoseconds / 1e9, modes->differences);
    const double served_seconds = (double)modes->served_nanoseconds / 1e9;
    const double child_seconds = (double)modes->child_nanoseconds / 1e9;
    const double child_cpu_seconds = (double)modes->child_cpu_nanoseconds / 1e9;
    printf("checker-driver: run costs: served %.3f s (%.3f ms each), %" PRIu64 " past the server's limit; children %.3f s wall (%.3f ms each), %.3f s CPU "
           "(%.3f ms each)\n",
           served_seconds, modes->served_runs != 0 ? served_seconds * 1e3 / (double)modes->served_runs : 0.0, modes->stalls, child_seconds,
           modes->child_runs != 0 ? child_seconds * 1e3 / (double)modes->child_runs : 0.0, child_cpu_seconds,
           modes->child_runs != 0 ? child_cpu_seconds * 1e3 / (double)modes->child_runs : 0.0);
    oracle_cache_report(oracle, "test-checker-driver");
    if (check_driver_part.count > 1)
    {
        printf("checker-driver: part %u of %u: %u of the %u units dealt\n", check_driver_part.index + 1, check_driver_part.count, check_driver_part.taken,
               check_driver_part.dealt);
    }
    printf("checker-driver: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int returned = test.failures != 0;
    oracle_epoch_end(epoch);
    return returned;
}
