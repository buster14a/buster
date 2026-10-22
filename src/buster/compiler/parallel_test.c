// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The runner's tests, for the test-parallel command:
//
//   build/compiler test-parallel
//
// main.c includes this file after parallel.c and calls run_parallel_tests for the command. Every report must be exactly
// the expected one, with each time written as `*`:
//   - lists: headings, scripts, blank lines and comments before the first heading, a heading without its space, a last
//     line without a newline, `==` inside a line; then each error, at its line: a line before the first heading, a
//     heading without a title, a command without a script (before another heading and at the end), a title that
//     repeats, a NUL byte, and no commands (an empty list, and one of a comment alone);
//   - order: commands that end out of order, printed in the list's order, with what each wrote to its standard output
//     and errors in the order it wrote it, output that doesn't end in a newline and none at all, an exit status and a
//     signal; then --github's groups and annotations, with a title GitHub must read escaped; and a command's
//     descriptors, without /proc: standard input reads nothing though this process's is a file with a line in it, and
//     no other descriptor than the three is open, though this process has one open without FD_CLOEXEC;
//   - at once: three commands that each wait for the other two (files in the test's directory) pass with 3 jobs; with
//     one job, four commands write their starts and ends into a log in turn; and eight with 3 jobs are never more than
//     3 at once, by the times the runner took;
//   - output: 1 MB from one command, past a pipe's 64 KB, all of it;
//   - processes: a command past its timeout killed, with the process it started; what a command left running killed
//     when its shell ends; and a shell that can't start;
//   - signals: SIGTERM from a command stops the run, sent on to the command's group, and what didn't start doesn't; a
//     run stopped while its command passes fails; SIGINT then SIGTERM, from a command that ignores both, kill it;
//     SIGHUP while ignored (as under nohup) doesn't stop it, and SIGCHLD ignored doesn't keep the runner from its
//     commands; each run leaves the signal mask and the dispositions as they were;
//   - a slow reader: a report whose pipe its reader leaves full for 1 s while a command passes its timeout, which must
//     be killed at its deadline and fail, and while a command sends SIGTERM, which must go on to it at once; SIGTERM
//     once every command has ended, which must drop the rest of the report rather than wait for the reader; and 61
//     commands with room for 32 descriptors, 60 of them ending before the first, whose output must be read and their
//     captures closed as they end (#276's review);
//   - the command: build/compiler parallel's report and exit status, for lists on standard input and in a file that
//     pass and fail, a report with no reader (its failed write stops the run, and the runner ends at once), with SIGPIPE
//     at its default and ignored, a reader that leaves after the first line, which fails a run whose command passes, a
//     command that ends past its deadline while the runner is stopped (SIGSTOP), which must fail, a list's error, a list
//     that can't be read, and invalid options;
//   - the cores: --jobs' default, the cores of the CPUs this process may use, against the distinct physical and core
//     ids /proc/cpuinfo gives those CPUs.
//
// Its files go in a new directory under $TMPDIR (or /tmp), removed unless a check fails. The exit status is 1 if a
// check fails.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

// OS-dependent includes
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

// The reports and lists the tests build, each in a buffer of this many bytes.
#define BUSTER_PARALLEL_TEST_TEXT_BYTES 4096
// The large output's bytes.
#define BUSTER_PARALLEL_TEST_LARGE_BYTES (1ull << 20)
// How long a process the runner killed may take to be gone (reaped by whatever inherits it), in nanoseconds.
#define BUSTER_PARALLEL_TEST_GONE_NANOSECONDS (5ull * BUSTER_PARALLEL_NANOSECONDS)

typedef struct ParallelTest ParallelTest;
struct ParallelTest
{
    Arena arena;
    uint64_t cases;
    uint64_t failures;
    // The test's own directory, for its commands' files.
    char directory[512];
};

static String8 parallel_test_string(const char* text)
{
    return (String8){ .pointer = (char*)text, .length = strlen(text) };
}

// The report with each time (" in 1.2 s") written as " in * s".
static String8 parallel_test_normalize(Arena* arena, String8 text)
{
    char* bytes = arena_push(arena, text.length, 1);
    uint64_t length = 0;
    uint64_t i = 0;
    while (bytes != 0 && i < text.length)
    {
        uint64_t digits = i + 4;
        while (digits < text.length && text.pointer[digits] >= '0' && text.pointer[digits] <= '9')
        {
            digits += 1;
        }
        const bool time = i + 4 <= text.length && memcmp(text.pointer + i, " in ", 4) == 0 && digits > i + 4 && digits + 4 <= text.length && text.pointer[digits] == '.' &&
                          text.pointer[digits + 1] >= '0' && text.pointer[digits + 1] <= '9' && memcmp(text.pointer + digits + 2, " s", 2) == 0;
        if (time)
        {
            memcpy(bytes + length, " in * s", 7);
            length += 7;
            i = digits + 4;
        }
        else
        {
            bytes[length] = text.pointer[i];
            length += 1;
            i += 1;
        }
    }
    return (String8){ .pointer = bytes, .length = bytes != 0 ? length : 0 };
}

// Counts a check, and prints it when it fails: `condition`, and the report when `expected` isn't null.
static bool parallel_test_check(ParallelTest* test, const char* name, bool condition, String8 report, const char* expected)
{
    const bool same = expected == 0 || parallel_equal(report, parallel_test_string(expected));
    const bool passed = condition && same;
    test->cases += 1;
    if (!passed)
    {
        test->failures += 1;
        printf("  %s: FAILED\n--- got:\n%.*s", name, (int)report.length, report.pointer != 0 ? report.pointer : "");
        if (expected != 0)
        {
            printf("--- expected:\n%s", expected);
        }
        printf("---\n");
    }
    return passed;
}

// A path in the test's directory.
static void parallel_test_path(const ParallelTest* test, char* path, uint64_t size, const char* name)
{
    snprintf(path, size, "%s/%s", test->directory, name);
}

// Reads a whole file into the arena: a null pointer if it can't.
static String8 parallel_test_read(Arena* arena, const char* path)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    const String8 result = fd >= 0 ? parallel_read(arena, fd) : (String8){ 0 };
    if (fd >= 0)
    {
        close(fd);
    }
    return result;
}

static bool parallel_test_write(const char* path, String8 text)
{
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    uint64_t written = 0;
    bool writing = fd >= 0;
    while (writing && written < text.length)
    {
        const ssize_t count = write(fd, text.pointer + written, text.length - written);
        writing = count > 0 || (count < 0 && errno == EINTR);
        written += count > 0 ? (uint64_t)count : 0;
    }
    const bool closed = fd >= 0 && close(fd) == 0;
    return closed && written == text.length;
}

// Runs a list (which must be valid) with the options, the report into a temporary file, and returns the report with
// its times written as `*`, its commands' results in *results.
static String8 parallel_test_run(ParallelTest* test, String8 text, ParallelOptions options, ParallelSummary* summary, ParallelResult** results)
{
    const ParallelList list = parallel_list_parse(&test->arena, text);
    *results = list.error == 0 ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, ParallelResult, list.count) : 0;
    FILE* file = *results != 0 ? tmpfile() : 0;
    String8 report = { 0 };
    *summary = (ParallelSummary){ 0 };
    if (file != 0)
    {
        options.output = fileno(file);
        options.shell = options.shell != 0 ? options.shell : "bash";
        *summary = parallel_run(&test->arena, &list, &options, *results);
        report = lseek(options.output, 0, SEEK_SET) == 0 ? parallel_test_normalize(&test->arena, parallel_read(&test->arena, options.output)) : (String8){ 0 };
        fclose(file);
    }
    if (list.error != 0)
    {
        printf("  the test's list is invalid at line %llu: %s\n", (unsigned long long)list.error_line, list.error);
    }
    return report;
}

// Whether the process whose pid is in the file is gone, or a zombie, within a few seconds: the runner killed it, and
// whatever inherited it reaps it.
static bool parallel_test_gone(ParallelTest* test, const char* name)
{
    char path[600];
    parallel_test_path(test, path, sizeof(path), name);
    const String8 text = parallel_test_read(&test->arena, path);
    int64_t pid = 0;
    bool valid = text.pointer != 0 && text.length != 0;
    for (uint64_t i = 0; valid && i < text.length && text.pointer[i] != '\n'; i += 1)
    {
        valid = text.pointer[i] >= '0' && text.pointer[i] <= '9';
        pid = pid * 10 + (text.pointer[i] - '0');
    }
    valid = valid && pid > 1;
    bool gone = false;
    const uint64_t deadline = parallel_clock() + BUSTER_PARALLEL_TEST_GONE_NANOSECONDS;
    while (valid && !gone && parallel_clock() < deadline)
    {
        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%lld/stat", (long long)pid);
        const String8 stat = parallel_test_read(&test->arena, stat_path);
        // The state is the field after the command's name, which ends at the line's last ')'.
        uint64_t close_parenthesis = stat.length;
        for (uint64_t i = 0; i < stat.length; i += 1)
        {
            close_parenthesis = stat.pointer[i] == ')' ? i : close_parenthesis;
        }
        gone = stat.pointer == 0 || (close_parenthesis + 2 < stat.length && stat.pointer[close_parenthesis + 2] == 'Z');
        if (!gone)
        {
            usleep(1000);
        }
    }
    return valid && gone;
}

static void parallel_test_lists(ParallelTest* test)
{
    typedef struct ParallelTestList ParallelTestList;
    struct ParallelTestList
    {
        const char* name;
        const char* text;
        uint64_t length;
        // Each command as "LINE TITLE\n", its script and "|\n", or "error LINE: MESSAGE\n".
        const char* expected;
    };
    static const char nul_list[] = "== a\ntr\0ue\n";
    static const ParallelTestList cases[] = {
        {
            .name = "two commands",
            .text = "== a\necho a\n== b\necho b\n",
            .expected = "1 a\necho a\n|\n3 b\necho b\n|\n",
        },
        {
            .name = "blank lines and comments first, and no last newline",
            .text = "\n# a comment\n  \t\n== spaced title  \necho one\n\n  # a comment of the script\necho two",
            .expected = "4 spaced title\necho one\n\n  # a comment of the script\necho two|\n",
        },
        {
            .name = "a heading without its space, and == inside a line",
            .text = "==title\nx == y\n[ 1 == 1 ]\n\n== next\ntrue\n",
            .expected = "1 title\nx == y\n[ 1 == 1 ]\n\n|\n5 next\ntrue\n|\n",
        },
        {
            .name = "a line before the first heading",
            .text = "\necho a\n== a\ntrue\n",
            .expected = "error 2: a line before the first heading\n",
        },
        {
            .name = "a heading without a title",
            .text = "== a\ntrue\n==  \t\ntrue\n",
            .expected = "error 3: a heading without a title\n",
        },
        {
            .name = "a command without a script",
            .text = "== a\n\n  \n== b\ntrue\n",
            .expected = "error 1: a command without a script\n",
        },
        {
            .name = "a last command without a script",
            .text = "== a\ntrue\n== b\n\n",
            .expected = "error 3: a command without a script\n",
        },
        {
            .name = "a title that repeats",
            .text = "== a\ntrue\n== b\ntrue\n==  a \ntrue\n",
            .expected = "error 5: a title that an earlier command has\n",
        },
        {
            .name = "a NUL byte",
            .text = nul_list,
            .length = sizeof(nul_list) - 1,
            .expected = "error 2: a NUL byte\n",
        },
        {
            .name = "an empty list",
            .text = "",
            .expected = "error 0: no commands\n",
        },
        {
            .name = "a comment alone",
            .text = "# nothing to run\n\n",
            .expected = "error 0: no commands\n",
        },
    };
    for (uint64_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c += 1)
    {
        const String8 text = { .pointer = (char*)cases[c].text, .length = cases[c].length != 0 ? cases[c].length : strlen(cases[c].text) };
        const ParallelList list = parallel_list_parse(&test->arena, text);
        char dump[BUSTER_PARALLEL_TEST_TEXT_BYTES];
        uint64_t length = 0;
        if (list.error != 0)
        {
            length = (uint64_t)snprintf(dump, sizeof(dump), "error %llu: %s\n", (unsigned long long)list.error_line, list.error);
        }
        for (uint64_t i = 0; list.error == 0 && i < list.count && length < sizeof(dump); i += 1)
        {
            const ParallelCommand* command = &list.commands[i];
            length += (uint64_t)snprintf(dump + length, sizeof(dump) - length, "%llu %.*s\n%s|\n", (unsigned long long)command->line, (int)command->title.length,
                                         command->title.pointer, command->script);
        }
        char name[256];
        snprintf(name, sizeof(name), "list: %s", cases[c].name);
        parallel_test_check(test, name, length < sizeof(dump), (String8){ .pointer = dump, .length = length < sizeof(dump) ? length : 0 }, cases[c].expected);
        test->arena.length = 0;
    }
}

static void parallel_test_order(ParallelTest* test)
{
    ParallelSummary summary;
    ParallelResult* results = 0;
    const ParallelOptions options = { .jobs = 5 };
    String8 report = parallel_test_run(test,
                                       parallel_test_string("== slow\n"
                                                            "sleep 0.3\n"
                                                            "echo slow\n"
                                                            "== mixed\n"
                                                            "echo out\n"
                                                            "echo err >&2\n"
                                                            "echo out again\n"
                                                            "printf 'no newline'\n"
                                                            "== silent\n"
                                                            "true\n"
                                                            "== three\n"
                                                            "echo three\n"
                                                            "exit 3\n"
                                                            "== killed\n"
                                                            "kill -KILL $$\n"),
                                       options, &summary, &results);
    parallel_test_check(test, "order", !summary.succeeded && summary.passed == 3 && summary.failed == 2 && summary.peak == 5, report,
                        "parallel: 5 commands, at most 5 at once\n"
                        "== slow: passed in * s\n"
                        "slow\n"
                        "== mixed: passed in * s\n"
                        "out\n"
                        "err\n"
                        "out again\n"
                        "no newline\n"
                        "== silent: passed in * s\n"
                        "== three: failed (exit status 3) in * s\n"
                        "three\n"
                        "== killed: failed (killed by signal 9: Killed) in * s\n"
                        "parallel: 5 commands in * s, at most 5 at once: 3 passed, 2 failed, 0 not started\n"
                        "parallel: three failed (exit status 3) in * s\n"
                        "parallel: killed failed (killed by signal 9: Killed) in * s\n");
    test->arena.length = 0;

    const ParallelOptions github = { .jobs = 2, .github = true };
    report = parallel_test_run(test,
                               parallel_test_string("== ok\n"
                                                    "echo fine\n"
                                                    "== 50% done, step: 1\n"
                                                    "echo 100%\n"
                                                    "false\n"),
                               github, &summary, &results);
    parallel_test_check(test, "order with --github", !summary.succeeded && summary.passed == 1 && summary.failed == 1, report,
                        "parallel: 2 commands, at most 2 at once\n"
                        "::group::ok: passed in * s\n"
                        "fine\n"
                        "::endgroup::\n"
                        "::group::50%25 done, step: 1: failed (exit status 1) in * s\n"
                        "100%\n"
                        "::endgroup::\n"
                        "parallel: 2 commands in * s, at most 2 at once: 1 passed, 1 failed, 0 not started\n"
                        "::error title=50%25 done%2C step%3A 1::failed (exit status 1) in * s\n");
    test->arena.length = 0;

    // A command's standard input is /dev/null (it reads nothing, though this process's is a file with a line in it for
    // the run), and its shell has no other descriptor than the three, though this process has the report's open
    // without FD_CLOEXEC (tmpfile's): an external true, redirected to each descriptor from 3 to 63 in turn, starts only
    // if the shell has it. Neither needs /proc, whose PID namespace may not be the shell's.
    char input_path[600];
    parallel_test_path(test, input_path, sizeof(input_path), "input");
    const int saved_input = dup(STDIN_FILENO);
    const bool input_ready = saved_input >= 0 && parallel_test_write(input_path, parallel_test_string("input\n"));
    const int input = input_ready ? open(input_path, O_RDONLY | O_CLOEXEC) : -1;
    const bool redirected = input >= 0 && dup2(input, STDIN_FILENO) == STDIN_FILENO;
    report = parallel_test_run(test,
                               parallel_test_string("== descriptors\n"
                                                    "if IFS= read -r line; then echo \"standard input: $line\"; fi\n"
                                                    "t=\"$(type -P true)\"\n"
                                                    "for ((fd = 3; fd < 64; fd++)); do if \"$t\" 2>/dev/null >&\"$fd\"; then echo \"descriptor $fd\"; fi; done\n"
                                                    "echo checked\n"),
                               options, &summary, &results);
    const bool restored = saved_input >= 0 && dup2(saved_input, STDIN_FILENO) == STDIN_FILENO;
    if (input >= 0)
    {
        close(input);
    }
    if (saved_input >= 0)
    {
        close(saved_input);
    }
    unlink(input_path);
    parallel_test_check(test, "descriptors", redirected && restored && summary.succeeded, report,
                        "parallel: 1 commands, at most 5 at once\n"
                        "== descriptors: passed in * s\n"
                        "checked\n"
                        "parallel: 1 commands in * s, at most 5 at once: 1 passed, 0 failed, 0 not started\n");
    test->arena.length = 0;
}

static void parallel_test_at_once(ParallelTest* test)
{
    // Three commands that each wait for the other two's files: they all end only if all three run at once. A runner
    // that ran fewer would leave them to their timeout.
    char text[BUSTER_PARALLEL_TEST_TEXT_BYTES];
    uint64_t length = 0;
    for (uint64_t i = 1; i <= 3; i += 1)
    {
        length += (uint64_t)snprintf(text + length, sizeof(text) - length, "== waits %llu\ntouch '%s/barrier%llu'\nwhile [ ! -e '%s/barrier1' ] || [ ! -e '%s/barrier2' ] || [ ! -e '%s/barrier3' ]; do sleep 0.01; done\n",
                                     (unsigned long long)i, test->directory, (unsigned long long)i, test->directory, test->directory, test->directory);
    }
    ParallelSummary summary;
    ParallelResult* results = 0;
    const ParallelOptions barrier = { .jobs = 3, .timeout = 20 * BUSTER_PARALLEL_NANOSECONDS };
    String8 report = parallel_test_run(test, (String8){ .pointer = text, .length = length }, barrier, &summary, &results);
    parallel_test_check(test, "three at once", summary.succeeded && summary.peak == 3, report,
                        "parallel: 3 commands, at most 3 at once\n"
                        "== waits 1: passed in * s\n"
                        "== waits 2: passed in * s\n"
                        "== waits 3: passed in * s\n"
                        "parallel: 3 commands in * s, at most 3 at once: 3 passed, 0 failed, 0 not started\n");
    test->arena.length = 0;

    // One job: each command's start and end in the log, in turn.
    char log[600];
    parallel_test_path(test, log, sizeof(log), "log");
    length = 0;
    for (uint64_t i = 1; i <= 4; i += 1)
    {
        length += (uint64_t)snprintf(text + length, sizeof(text) - length, "== logs %llu\necho start %llu >> '%s'\nsleep 0.02\necho end %llu >> '%s'\n", (unsigned long long)i,
                                     (unsigned long long)i, log, (unsigned long long)i, log);
    }
    const ParallelOptions one = { .jobs = 1 };
    report = parallel_test_run(test, (String8){ .pointer = text, .length = length }, one, &summary, &results);
    const String8 logged = parallel_test_read(&test->arena, log);
    parallel_test_check(test, "one at a time", summary.succeeded && summary.peak == 1, logged, "start 1\nend 1\nstart 2\nend 2\nstart 3\nend 3\nstart 4\nend 4\n");
    test->arena.length = 0;

    // Eight with 3 jobs: at no command's start were more than 3 between their starts and ends, which hold their
    // processes' lives.
    length = 0;
    for (uint64_t i = 1; i <= 8; i += 1)
    {
        length += (uint64_t)snprintf(text + length, sizeof(text) - length, "== sleeps %llu\nsleep 0.05\n", (unsigned long long)i);
    }
    const ParallelOptions three = { .jobs = 3 };
    report = parallel_test_run(test, (String8){ .pointer = text, .length = length }, three, &summary, &results);
    uint64_t most = 0;
    for (uint64_t i = 0; results != 0 && i < 8; i += 1)
    {
        uint64_t at_once = 0;
        for (uint64_t j = 0; j < 8; j += 1)
        {
            at_once += results[j].start <= results[i].start && results[i].start < results[j].end;
        }
        most = at_once > most ? at_once : most;
    }
    parallel_test_check(test, "at most 3 at once", results != 0 && summary.succeeded && summary.peak == 3 && most == 3, report, 0);
    test->arena.length = 0;
}

static void parallel_test_output(ParallelTest* test)
{
    ParallelSummary summary;
    ParallelResult* results = 0;
    const ParallelOptions options = { .jobs = 1 };
    const String8 report = parallel_test_run(test, parallel_test_string("== large\nhead -c 1048576 /dev/zero | tr '\\0' x\necho\n"), options, &summary, &results);
    static const char head[] = "parallel: 1 commands, at most 1 at once\n== large: passed in * s\n";
    static const char tail[] = "\nparallel: 1 commands in * s, at most 1 at once: 1 passed, 0 failed, 0 not started\n";
    const uint64_t head_length = sizeof(head) - 1;
    const uint64_t tail_length = sizeof(tail) - 1;
    bool same = report.length == head_length + BUSTER_PARALLEL_TEST_LARGE_BYTES + tail_length && memcmp(report.pointer, head, head_length) == 0 &&
                memcmp(report.pointer + head_length + BUSTER_PARALLEL_TEST_LARGE_BYTES, tail, tail_length) == 0;
    for (uint64_t i = 0; same && i < BUSTER_PARALLEL_TEST_LARGE_BYTES; i += 1)
    {
        same = report.pointer[head_length + i] == 'x';
    }
    parallel_test_check(test, "1 MB of output", summary.succeeded && same, report, 0);
    test->arena.length = 0;
}

static void parallel_test_processes(ParallelTest* test)
{
    // A command past its timeout, and the process it started, killed with its group.
    char text[BUSTER_PARALLEL_TEST_TEXT_BYTES];
    uint64_t length = (uint64_t)snprintf(text, sizeof(text), "== stuck\n( exec sleep 30 ) &\necho $! > '%s/stuck'\nwait\n", test->directory);
    ParallelSummary summary;
    ParallelResult* results = 0;
    const ParallelOptions timeout = { .jobs = 1, .timeout = BUSTER_PARALLEL_NANOSECONDS };
    String8 report = parallel_test_run(test, (String8){ .pointer = text, .length = length }, timeout, &summary, &results);
    const bool timed = results != 0 && results[0].timed_out && results[0].end - results[0].start >= BUSTER_PARALLEL_NANOSECONDS &&
                       results[0].end - results[0].start < 10 * BUSTER_PARALLEL_NANOSECONDS;
    parallel_test_check(test, "a timeout", timed && !summary.succeeded && parallel_test_gone(test, "stuck"), report,
                        "parallel: 1 commands, at most 1 at once\n"
                        "== stuck: failed (timed out) in * s\n"
                        "parallel: 1 commands in * s, at most 1 at once: 0 passed, 1 failed, 0 not started\n"
                        "parallel: stuck failed (timed out) in * s\n");
    test->arena.length = 0;

    // What a command leaves running is killed when its shell ends.
    length = (uint64_t)snprintf(text, sizeof(text), "== leaves\n( exec sleep 30 ) &\necho $! > '%s/left'\necho left\n", test->directory);
    const ParallelOptions one = { .jobs = 1 };
    report = parallel_test_run(test, (String8){ .pointer = text, .length = length }, one, &summary, &results);
    parallel_test_check(test, "what a command leaves", summary.succeeded && summary.elapsed < 10 * BUSTER_PARALLEL_NANOSECONDS && parallel_test_gone(test, "left"), report,
                        "parallel: 1 commands, at most 1 at once\n"
                        "== leaves: passed in * s\n"
                        "left\n"
                        "parallel: 1 commands in * s, at most 1 at once: 1 passed, 0 failed, 0 not started\n");
    test->arena.length = 0;

    const ParallelOptions missing = { .jobs = 1, .shell = "/nonexistent/bash" };
    report = parallel_test_run(test, parallel_test_string("== a\ntrue\n== b\ntrue\n"), missing, &summary, &results);
    parallel_test_check(test, "a shell that can't start", !summary.succeeded && summary.failed == 2, report,
                        "parallel: 2 commands, at most 1 at once\n"
                        "== a: failed (can't start /nonexistent/bash: No such file or directory) in * s\n"
                        "== b: failed (can't start /nonexistent/bash: No such file or directory) in * s\n"
                        "parallel: 2 commands in * s, at most 1 at once: 0 passed, 2 failed, 0 not started\n"
                        "parallel: a failed (can't start /nonexistent/bash: No such file or directory) in * s\n"
                        "parallel: b failed (can't start /nonexistent/bash: No such file or directory) in * s\n");
    test->arena.length = 0;
}

// Runs a list with its report into a pipe whose reader, `sh -c 'DELAY; cat > FILE'`, takes nothing while DELAY runs (a
// sleep), so that the report's writes would wait for it; returns the report as the reader got it, with its times written
// as `*`, and the run's own time in *run_time.
static String8 parallel_test_slow_run(ParallelTest* test, String8 text, ParallelOptions options, const char* delay, ParallelSummary* summary, ParallelResult** results,
                                      uint64_t* run_time)
{
    char report_path[600];
    parallel_test_path(test, report_path, sizeof(report_path), "slow");
    char script[800];
    snprintf(script, sizeof(script), "%s; cat > '%s'", delay, report_path);
    int ends[2] = {
        -1,
        -1,
    };
    bool ready = pipe(ends) == 0 && fcntl(ends[0], F_SETFD, FD_CLOEXEC) == 0 && fcntl(ends[1], F_SETFD, FD_CLOEXEC) == 0;
    pid_t reader = -1;
    posix_spawn_file_actions_t actions;
    const bool actions_ready = ready && posix_spawn_file_actions_init(&actions) == 0;
    if (actions_ready)
    {
        char* arguments[] = {
            "sh",
            "-c",
            script,
            0,
        };
        ready = posix_spawn_file_actions_adddup2(&actions, ends[0], STDIN_FILENO) == 0 && posix_spawnp(&reader, "sh", &actions, 0, arguments, environ) == 0;
        posix_spawn_file_actions_destroy(&actions);
    }
    if (ends[0] >= 0)
    {
        close(ends[0]);
    }
    const ParallelList list = parallel_list_parse(&test->arena, text);
    *results = ready && actions_ready && list.error == 0 ? BUSTER_ARENA_PUSH_ARRAY(&test->arena, ParallelResult, list.count) : 0;
    *summary = (ParallelSummary){ 0 };
    if (*results != 0)
    {
        options.output = ends[1];
        options.shell = "bash";
        const uint64_t run_start = parallel_clock();
        *summary = parallel_run(&test->arena, &list, &options, *results);
        *run_time = parallel_clock() - run_start;
    }
    if (ends[1] >= 0)
    {
        close(ends[1]);
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = reader > 0 ? waitpid(reader, &status, 0) : -1;
    } while (waited < 0 && errno == EINTR);
    const String8 report = waited == reader && *results != 0 ? parallel_test_normalize(&test->arena, parallel_test_read(&test->arena, report_path)) : (String8){ 0 };
    unlink(report_path);
    return report;
}

// Whether `report` is `head`, 1 MB of 'x' and a newline, then `tail`.
static bool parallel_test_large(String8 report, const char* head, const char* tail)
{
    const uint64_t head_length = strlen(head);
    const uint64_t tail_length = strlen(tail);
    bool same = report.length == head_length + BUSTER_PARALLEL_TEST_LARGE_BYTES + 1 + tail_length && memcmp(report.pointer, head, head_length) == 0 &&
                report.pointer[head_length + BUSTER_PARALLEL_TEST_LARGE_BYTES] == '\n' &&
                memcmp(report.pointer + head_length + BUSTER_PARALLEL_TEST_LARGE_BYTES + 1, tail, tail_length) == 0;
    for (uint64_t i = 0; same && i < BUSTER_PARALLEL_TEST_LARGE_BYTES; i += 1)
    {
        same = report.pointer[head_length + i] == 'x';
    }
    return same;
}

// #276's review: a report whose reader is slow doesn't hold up the run. Its first part, 1 MB, fills the pipe while the
// reader sleeps for 1 s; meanwhile a command past its timeout must be killed at its deadline, and fail (it would end
// after it, before the reader wakes), and SIGTERM from a command, sent once the first part has filled the pipe, must
// go on to it at once. And with a low limit of descriptors, commands that end before the report reaches them (a slow
// first one, then many quick ones) must all start: each one's output is read when it ends, and its capture closed.
static void parallel_test_slow_reader(ParallelTest* test)
{
    struct sigaction original;
    sigaction(SIGTERM, 0, &original);
    struct sigaction default_action = { .sa_handler = SIG_DFL };
    sigaction(SIGTERM, &default_action, 0);
    ParallelSummary summary;
    ParallelResult* results = 0;
    uint64_t run_time = 0;
    const ParallelOptions timeout = { .jobs = 2, .timeout = BUSTER_PARALLEL_NANOSECONDS / 5 };
    String8 report = parallel_test_slow_run(test, parallel_test_string("== large\nhead -c 1048576 /dev/zero | tr '\\0' x\necho\n== deadline\nsleep 0.8\necho finished\n"), timeout,
                                            "sleep 1", &summary, &results, &run_time);
    bool killed = results != 0 && results[1].timed_out && !results[1].passed && results[1].end - results[1].start < 7 * BUSTER_PARALLEL_NANOSECONDS / 10;
    parallel_test_check(test, "a slow reader, and a timeout", killed && summary.passed == 1 && summary.failed == 1 &&
                                                                 parallel_test_large(report, "parallel: 2 commands, at most 2 at once\n== large: passed in * s\n",
                                                                                     "== deadline: failed (timed out) in * s\n"
                                                                                     "parallel: 2 commands in * s, at most 2 at once: 1 passed, 1 failed, 0 not started\n"
                                                                                     "parallel: deadline failed (timed out) in * s\n"),
                        report.length < 4096 ? report : (String8){ 0 }, 0);
    test->arena.length = 0;

    // The second command waits for the first to end, and 0.2 s more for its 1 MB to fill the pipe.
    char text[BUSTER_PARALLEL_TEST_TEXT_BYTES];
    uint64_t length = (uint64_t)snprintf(text, sizeof(text),
                                         "== large\nhead -c 1048576 /dev/zero | tr '\\0' x\necho\ntouch '%s/large'\n"
                                         "== stops\nwhile [ ! -e '%s/large' ]; do sleep 0.01; done\nsleep 0.2\nkill -TERM $PPID\nsleep 5\n",
                                         test->directory, test->directory);
    const ParallelOptions plain = { .jobs = 2 };
    report = parallel_test_slow_run(test, (String8){ .pointer = text, .length = length }, plain, "sleep 1", &summary, &results, &run_time);
    killed = results != 0 && results[1].interrupted && results[1].end - results[1].start < 7 * BUSTER_PARALLEL_NANOSECONDS / 10;
    parallel_test_check(test, "a slow reader, and a signal", killed && summary.signal == SIGTERM &&
                                                                parallel_test_large(report, "parallel: 2 commands, at most 2 at once\n== large: passed in * s\n",
                                                                                    "== stops: failed (killed by signal 15: Terminated) in * s\n"
                                                                                    "parallel: 2 commands in * s, at most 2 at once: 1 passed, 1 failed, 0 not started, stopped by "
                                                                                    "signal 15: Terminated\n"
                                                                                    "parallel: stops failed (killed by signal 15: Terminated) in * s\n"),
                        report.length < 4096 ? report : (String8){ 0 }, 0);
    test->arena.length = 0;

    // Every command has ended and only the reader holds the run up, which sends SIGTERM at 0.3 s: the rest of the report
    // is dropped then, rather than when the reader wakes, though the run's one command passed.
    run_time = UINT64_MAX;
    report = parallel_test_slow_run(test, parallel_test_string("== large\nhead -c 1048576 /dev/zero | tr '\\0' x\necho\n"), plain, "sleep 0.3; kill -TERM $PPID; sleep 0.7", &summary,
                                    &results, &run_time);
    parallel_test_check(test, "a slow reader, and a signal once every command has ended", summary.report_lost && summary.signal == SIGTERM && summary.passed == 1 && !summary.succeeded &&
                                                                                             run_time < 8 * BUSTER_PARALLEL_NANOSECONDS / 10 && report.length < BUSTER_PARALLEL_TEST_LARGE_BYTES,
                        (String8){ 0 }, 0);
    sigaction(SIGTERM, &original, 0);
    test->arena.length = 0;

    // A slow command, then 60 quick ones that end before it, with room for 32 descriptors.
    length = (uint64_t)snprintf(text, sizeof(text), "== slow\nsleep 0.5\n");
    for (uint64_t i = 1; i <= 60; i += 1)
    {
        length += (uint64_t)snprintf(text + length, sizeof(text) - length, "== quick %llu\ntrue\n", (unsigned long long)i);
    }
    struct rlimit saved;
    const bool known = getrlimit(RLIMIT_NOFILE, &saved) == 0;
    struct rlimit low = {
        .rlim_cur = 32,
        .rlim_max = known ? saved.rlim_max : 32,
    };
    const bool lowered = known && saved.rlim_cur > low.rlim_cur && setrlimit(RLIMIT_NOFILE, &low) == 0;
    report = lowered ? parallel_test_run(test, (String8){ .pointer = text, .length = length }, plain, &summary, &results) : (String8){ 0 };
    const bool restored = !lowered || setrlimit(RLIMIT_NOFILE, &saved) == 0;
    parallel_test_check(test, "many ended before the report reaches them, with 32 descriptors", lowered && restored && summary.succeeded && summary.passed == 61,
                        (String8){ 0 }, 0);
    test->arena.length = 0;
}

// Whether the signal mask and the dispositions of SIGCHLD, SIGINT, SIGTERM and SIGHUP are as they were.
static bool parallel_test_signals_kept(const sigset_t* mask, const struct sigaction* actions)
{
    static const int signals[] = {
        SIGCHLD,
        SIGINT,
        SIGTERM,
        SIGHUP,
    };
    sigset_t current;
    bool kept = sigprocmask(SIG_BLOCK, 0, &current) == 0;
    for (int s = 1; kept && s < SIGRTMIN; s += 1)
    {
        kept = sigismember(&current, s) == sigismember(mask, s);
    }
    for (uint64_t i = 0; kept && i < sizeof(signals) / sizeof(signals[0]); i += 1)
    {
        struct sigaction action;
        kept = sigaction(signals[i], 0, &action) == 0 && action.sa_handler == actions[i].sa_handler;
    }
    return kept;
}

static void parallel_test_signals(ParallelTest* test)
{
    static const int signals[] = {
        SIGCHLD,
        SIGINT,
        SIGTERM,
        SIGHUP,
    };
    // The runner takes only the signals that aren't ignored, and a shell starts a background command with SIGINT
    // ignored: these checks set SIGINT and SIGTERM to their defaults, and put back what this process had.
    struct sigaction original[4];
    for (uint64_t i = 0; i < 4; i += 1)
    {
        sigaction(signals[i], 0, &original[i]);
    }
    struct sigaction default_action = { .sa_handler = SIG_DFL };
    sigaction(SIGINT, &default_action, 0);
    sigaction(SIGTERM, &default_action, 0);
    sigset_t mask;
    struct sigaction actions[4];
    sigprocmask(SIG_BLOCK, 0, &mask);
    for (uint64_t i = 0; i < 4; i += 1)
    {
        sigaction(signals[i], 0, &actions[i]);
    }

    // SIGTERM from a command: sent on to its group, and the rest not started.
    ParallelSummary summary;
    ParallelResult* results = 0;
    const ParallelOptions one = { .jobs = 1 };
    String8 report = parallel_test_run(test, parallel_test_string("== stops\nkill -TERM $PPID\nsleep 30\n== waits\ntrue\n"), one, &summary, &results);
    parallel_test_check(test, "SIGTERM", summary.signal == SIGTERM && !summary.succeeded && summary.not_started == 1 && results != 0 && results[0].interrupted &&
                                             parallel_test_signals_kept(&mask, actions),
                        report,
                        "parallel: 2 commands, at most 1 at once\n"
                        "== stops: failed (killed by signal 15: Terminated) in * s\n"
                        "== waits: not started\n"
                        "parallel: 2 commands in * s, at most 1 at once: 0 passed, 1 failed, 1 not started, stopped by signal 15: Terminated\n"
                        "parallel: stops failed (killed by signal 15: Terminated) in * s\n");
    test->arena.length = 0;

    // A run stopped by a signal fails, though its command, which ignores it, passes.
    report = parallel_test_run(test, parallel_test_string("== shrugs\ntrap '' TERM\nkill -TERM $PPID\nsleep 0.05\n"), one, &summary, &results);
    parallel_test_check(test, "a signal with every command passing", summary.signal == SIGTERM && !summary.succeeded && summary.passed == 1 && results != 0 && results[0].interrupted,
                        report,
                        "parallel: 1 commands, at most 1 at once\n"
                        "== shrugs: passed in * s\n"
                        "parallel: 1 commands in * s, at most 1 at once: 1 passed, 0 failed, 0 not started, stopped by signal 15: Terminated\n");
    test->arena.length = 0;

    // A command that ignores SIGINT and SIGTERM: the first is sent on, and the second kills it.
    report = parallel_test_run(test, parallel_test_string("== ignores\ntrap '' INT TERM\nkill -INT $PPID\nkill -TERM $PPID\nexec sleep 30\n"), one, &summary, &results);
    parallel_test_check(test, "SIGINT, then SIGTERM", summary.signal == SIGINT && !summary.succeeded && parallel_test_signals_kept(&mask, actions), report,
                        "parallel: 1 commands, at most 1 at once\n"
                        "== ignores: failed (killed by signal 9: Killed) in * s\n"
                        "parallel: 1 commands in * s, at most 1 at once: 0 passed, 1 failed, 0 not started, stopped by signal 2: Interrupt\n"
                        "parallel: ignores failed (killed by signal 9: Killed) in * s\n");
    test->arena.length = 0;

    // SIGHUP ignored, as nohup leaves it, doesn't stop the run; and with SIGCHLD ignored, the runner still reaps its
    // commands. Both are put back.
    struct sigaction ignore = { .sa_handler = SIG_IGN };
    sigaction(SIGHUP, &ignore, 0);
    sigaction(SIGCHLD, &ignore, 0);
    struct sigaction ignored[4];
    for (uint64_t i = 0; i < 4; i += 1)
    {
        sigaction(signals[i], 0, &ignored[i]);
    }
    report = parallel_test_run(test, parallel_test_string("== hangs up\nkill -HUP $PPID\necho still here\n== exits\nexit 4\n"), one, &summary, &results);
    parallel_test_check(test, "SIGHUP and SIGCHLD ignored", summary.signal == 0 && summary.passed == 1 && parallel_test_signals_kept(&mask, ignored), report,
                        "parallel: 2 commands, at most 1 at once\n"
                        "== hangs up: passed in * s\n"
                        "still here\n"
                        "== exits: failed (exit status 4) in * s\n"
                        "parallel: 2 commands in * s, at most 1 at once: 1 passed, 1 failed, 0 not started\n"
                        "parallel: exits failed (exit status 4) in * s\n");
    for (uint64_t i = 0; i < 4; i += 1)
    {
        sigaction(signals[i], &original[i], 0);
    }
    test->arena.length = 0;
}

// Runs build/compiler (this executable) with `arguments`, its standard input from `input` and its output into `output`
// (or, when it's -1, a file of the test's directory) and its errors into another. Returns its exit status, or -1.
static int parallel_test_command(ParallelTest* test, char* const* arguments, const char* input, int output)
{
    char output_path[600];
    char errors[600];
    parallel_test_path(test, output_path, sizeof(output_path), "command.out");
    parallel_test_path(test, errors, sizeof(errors), "command.err");
    int result = -1;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) == 0)
    {
        pid_t child = -1;
        const bool output_ready = output >= 0 ? posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO) == 0
                                              : posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0;
        const bool ready = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, input, O_RDONLY, 0) == 0 && output_ready &&
                           posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, errors, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0;
        if (ready && posix_spawn(&child, "/proc/self/exe", &actions, 0, arguments, environ) == 0)
        {
            int status = 0;
            pid_t waited = -1;
            do
            {
                waited = waitpid(child, &status, 0);
            } while (waited < 0 && errno == EINTR);
            result = waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        posix_spawn_file_actions_destroy(&actions);
    }
    return result;
}

static void parallel_test_command_line(ParallelTest* test)
{
    char passing[600];
    char failing[600];
    char invalid[600];
    char output[600];
    char errors[600];
    parallel_test_path(test, passing, sizeof(passing), "passing.list");
    parallel_test_path(test, failing, sizeof(failing), "failing.list");
    parallel_test_path(test, invalid, sizeof(invalid), "invalid.list");
    parallel_test_path(test, output, sizeof(output), "command.out");
    parallel_test_path(test, errors, sizeof(errors), "command.err");
    char stuck[600];
    char stuck_pid[600];
    parallel_test_path(test, stuck, sizeof(stuck), "stuck.list");
    parallel_test_path(test, stuck_pid, sizeof(stuck_pid), "piped");
    char stuck_text[BUSTER_PARALLEL_TEST_TEXT_BYTES];
    snprintf(stuck_text, sizeof(stuck_text), "== stuck\necho $$ > '%s'\nexec sleep 30\n", stuck_pid);
    bool ready = parallel_test_write(passing, parallel_test_string("# A list that passes.\n== first\necho one\n== second\necho two >&2\n")) &&
                 parallel_test_write(failing, parallel_test_string("== fails\nexit 2\n")) && parallel_test_write(invalid, parallel_test_string("== a\n")) &&
                 parallel_test_write(stuck, parallel_test_string(stuck_text));

    char* on_input[] = { "compiler", "parallel", "--jobs", "2", "--timeout", "60", 0 };
    int status = ready ? parallel_test_command(test, on_input, passing, -1) : -1;
    String8 report = parallel_test_normalize(&test->arena, parallel_test_read(&test->arena, output));
    parallel_test_check(test, "the command, a list on standard input", status == 0, report,
                        "parallel: 2 commands, at most 2 at once\n"
                        "== first: passed in * s\n"
                        "one\n"
                        "== second: passed in * s\n"
                        "two\n"
                        "parallel: 2 commands in * s, at most 2 at once: 2 passed, 0 failed, 0 not started\n");
    test->arena.length = 0;

    char* from_file[] = { "compiler", "parallel", "--github", failing, 0 };
    status = ready ? parallel_test_command(test, from_file, "/dev/null", -1) : -1;
    report = parallel_test_normalize(&test->arena, parallel_test_read(&test->arena, output));
    // The default jobs are the cores of the CPUs this process may use, which the report then prints.
    char expected[BUSTER_PARALLEL_TEST_TEXT_BYTES];
    const unsigned long long cpus = (unsigned long long)parallel_core_count();
    snprintf(expected, sizeof(expected),
             "parallel: 1 commands, at most %llu at once\n"
             "::group::fails: failed (exit status 2) in * s\n"
             "::endgroup::\n"
             "parallel: 1 commands in * s, at most %llu at once: 0 passed, 1 failed, 0 not started\n"
             "::error title=fails::failed (exit status 2) in * s\n",
             cpus, cpus);
    parallel_test_check(test, "the command, a list in a file that fails", status == 1, report, expected);
    test->arena.length = 0;

    char* invalid_list[] = { "compiler", "parallel", 0 };
    status = ready ? parallel_test_command(test, invalid_list, invalid, -1) : -1;
    parallel_test_check(test, "the command, an invalid list", status == 1, parallel_test_read(&test->arena, errors), "parallel: standard input:1: a command without a script\n");
    test->arena.length = 0;

    // A report with no reader (`parallel | head`): its first write fails, which stops the run, so that the command that
    // started is killed rather than left running, and the runner ends at once with status 1: with SIGPIPE at its
    // default, and ignored (#276's review: the runner then gets EPIPE alone), which the runner inherits.
    struct sigaction pipe_original;
    sigaction(SIGPIPE, 0, &pipe_original);
    for (uint64_t ignored = 0; ignored < 2; ignored += 1)
    {
        struct sigaction pipe_action = { .sa_handler = ignored != 0 ? SIG_IGN : SIG_DFL };
        sigaction(SIGPIPE, &pipe_action, 0);
        unlink(stuck_pid);
        int pipe_ends[2] = {
            -1,
            -1,
        };
        const bool piped = ready && pipe(pipe_ends) == 0 && fcntl(pipe_ends[1], F_SETFD, FD_CLOEXEC) == 0;
        if (piped)
        {
            close(pipe_ends[0]);
        }
        char* into_pipe[] = { "compiler", "parallel", stuck, 0 };
        const uint64_t pipe_start = parallel_clock();
        status = piped ? parallel_test_command(test, into_pipe, "/dev/null", pipe_ends[1]) : -1;
        const uint64_t pipe_time = parallel_clock() - pipe_start;
        if (piped)
        {
            close(pipe_ends[1]);
        }
        // The command may have been killed before it wrote its pid; if it did, the process must be gone.
        struct stat pid_stat;
        const bool wrote_pid = stat(stuck_pid, &pid_stat) == 0;
        parallel_test_check(test, ignored != 0 ? "the command, a report with no reader, SIGPIPE ignored" : "the command, a report with no reader",
                            piped && status == 1 && pipe_time < 10 * BUSTER_PARALLEL_NANOSECONDS && (!wrote_pid || parallel_test_gone(test, "piped")), (String8){ 0 }, 0);
        test->arena.length = 0;
    }
    sigaction(SIGPIPE, &pipe_original, 0);

    // A reader that leaves after the first line, before the one command passes: the report is lost, which fails the run
    // though its command passed and no signal stopped it.
    char leaving[600];
    parallel_test_path(test, leaving, sizeof(leaving), "leaving.list");
    int leave_ends[2] = {
        -1,
        -1,
    };
    bool leave_ready = ready && parallel_test_write(leaving, parallel_test_string("== passes\nsleep 0.3\n")) && pipe(leave_ends) == 0 &&
                       fcntl(leave_ends[0], F_SETFD, FD_CLOEXEC) == 0 && fcntl(leave_ends[1], F_SETFD, FD_CLOEXEC) == 0;
    pid_t first_line = -1;
    posix_spawn_file_actions_t leave_actions;
    if (leave_ready && posix_spawn_file_actions_init(&leave_actions) == 0)
    {
        char* head_arguments[] = {
            "head",
            "-n",
            "1",
            0,
        };
        leave_ready = posix_spawn_file_actions_adddup2(&leave_actions, leave_ends[0], STDIN_FILENO) == 0 &&
                      posix_spawn_file_actions_addopen(&leave_actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) == 0 &&
                      posix_spawnp(&first_line, "head", &leave_actions, 0, head_arguments, environ) == 0;
        posix_spawn_file_actions_destroy(&leave_actions);
    }
    if (leave_ends[0] >= 0)
    {
        close(leave_ends[0]);
    }
    char* into_leaving[] = { "compiler", "parallel", leaving, 0 };
    status = leave_ready ? parallel_test_command(test, into_leaving, "/dev/null", leave_ends[1]) : -1;
    if (leave_ends[1] >= 0)
    {
        close(leave_ends[1]);
    }
    int head_status = 0;
    pid_t head_waited = -1;
    do
    {
        head_waited = first_line > 0 ? waitpid(first_line, &head_status, 0) : -1;
    } while (head_waited < 0 && errno == EINTR);
    parallel_test_check(test, "the command, a reader that leaves after the first line", leave_ready && status == 1 && head_waited == first_line, (String8){ 0 }, 0);
    unlink(leaving);
    test->arena.length = 0;

    // #276's review: a command that ends after its deadline, while the runner can't see it, fails. The runner, stopped
    // (SIGSTOP) from 0.4 s to 2 s, misses both the deadline (1 s after the command started) and the command's end (1.3 s
    // after), and when it goes on it finds the command past its deadline before it reaps it.
    char late[600];
    parallel_test_path(test, late, sizeof(late), "late.list");
    char* stopped_runner[] = { "compiler", "parallel", "--jobs", "1", "--timeout", "1", late, 0 };
    pid_t runner = -1;
    posix_spawn_file_actions_t late_actions;
    const bool late_ready = ready && parallel_test_write(late, parallel_test_string("== late\nsleep 1.3\n")) && posix_spawn_file_actions_init(&late_actions) == 0;
    if (late_ready)
    {
        const bool spawned = posix_spawn_file_actions_addopen(&late_actions, STDOUT_FILENO, output, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                             posix_spawn(&runner, "/proc/self/exe", &late_actions, 0, stopped_runner, environ) == 0;
        runner = spawned ? runner : -1;
        posix_spawn_file_actions_destroy(&late_actions);
    }
    usleep(400000);
    const bool stopped = runner > 0 && kill(runner, SIGSTOP) == 0;
    usleep(1600000);
    const bool continued = stopped && kill(runner, SIGCONT) == 0;
    int late_status = 0;
    pid_t late_waited = -1;
    do
    {
        late_waited = runner > 0 ? waitpid(runner, &late_status, 0) : -1;
    } while (late_waited < 0 && errno == EINTR);
    report = parallel_test_normalize(&test->arena, parallel_test_read(&test->arena, output));
    parallel_test_check(test, "the command, a command that ends past its deadline unseen", continued && late_waited == runner && WIFEXITED(late_status) && WEXITSTATUS(late_status) == 1,
                        report,
                        "parallel: 1 commands, at most 1 at once\n"
                        "== late: failed (timed out) in * s\n"
                        "parallel: 1 commands in * s, at most 1 at once: 0 passed, 1 failed, 0 not started\n"
                        "parallel: late failed (timed out) in * s\n");
    unlink(late);
    test->arena.length = 0;

    char* unreadable[] = { "compiler", "parallel", "/nonexistent/list", 0 };
    status = ready ? parallel_test_command(test, unreadable, "/dev/null", -1) : -1;
    parallel_test_check(test, "the command, a list that can't be read", status == 1, parallel_test_read(&test->arena, errors), "parallel: can't read /nonexistent/list\n");
    test->arena.length = 0;

    static const char usage[] = "usage: parallel [--jobs N] [--timeout SECONDS] [--github] [LIST]\n";
    char* const option_cases[][5] = {
        { "compiler", "parallel", "--jobs", "0", 0 },
        { "compiler", "parallel", "--jobs", "x", 0 },
        { "compiler", "parallel", "--timeout", "86401", 0 },
        { "compiler", "parallel", "--jobs", 0 },
        { "compiler", "parallel", "--bogus", 0 },
        { "compiler", "parallel", passing, failing, 0 },
    };
    for (uint64_t i = 0; i < sizeof(option_cases) / sizeof(option_cases[0]); i += 1)
    {
        status = ready ? parallel_test_command(test, option_cases[i], "/dev/null", -1) : -1;
        char name[128];
        snprintf(name, sizeof(name), "the command, invalid options %llu", (unsigned long long)i);
        parallel_test_check(test, name, status == 1, parallel_test_read(&test->arena, errors), usage);
        test->arena.length = 0;
    }
    unlink(passing);
    unlink(failing);
    unlink(invalid);
    unlink(stuck);
    unlink(stuck_pid);
    unlink(output);
    unlink(errors);
}

// --jobs' default against /proc/cpuinfo's cores: the distinct physical and core ids of the CPUs this process may use.
static void parallel_test_cores(ParallelTest* test)
{
    uint64_t mask[BUSTER_PARALLEL_CPUS / 64] = { 0 };
    const bool known = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0;
    const String8 text = parallel_test_read(&test->arena, "/proc/cpuinfo");
    // Each allowed processor's physical id and core id, as one number each, and how many processors had both.
    uint64_t* pairs = BUSTER_ARENA_PUSH_ARRAY(&test->arena, uint64_t, BUSTER_PARALLEL_CPUS);
    uint64_t pair_count = 0;
    uint64_t processors = 0;
    int64_t processor = -1;
    int64_t package = -1;
    uint64_t position = 0;
    while (text.pointer != 0 && pairs != 0 && position < text.length)
    {
        const uint64_t start = position;
        while (position < text.length && text.pointer[position] != '\n')
        {
            position += 1;
        }
        const String8 line = { .pointer = text.pointer + start, .length = position - start };
        position += 1;
        uint64_t colon = 0;
        while (colon < line.length && line.pointer[colon] != ':')
        {
            colon += 1;
        }
        int64_t value = -1;
        for (uint64_t i = colon + 1; i < line.length; i += 1)
        {
            const char c = line.pointer[i];
            value = c >= '0' && c <= '9' ? (value < 0 ? 0 : value) * 10 + (c - '0') : value;
        }
        const bool processor_line = line.length >= 9 && memcmp(line.pointer, "processor", 9) == 0;
        const bool package_line = line.length >= 11 && memcmp(line.pointer, "physical id", 11) == 0;
        const bool core_line = line.length >= 7 && memcmp(line.pointer, "core id", 7) == 0;
        processor = processor_line ? value : processor;
        package = processor_line ? -1 : package_line ? value : package;
        const bool allowed = processor >= 0 && processor < BUSTER_PARALLEL_CPUS && ((mask[processor / 64] >> (processor % 64)) & 1) != 0;
        if (core_line && allowed && package >= 0 && value >= 0 && pair_count < BUSTER_PARALLEL_CPUS)
        {
            const uint64_t pair = (uint64_t)package << 32 | (uint64_t)value;
            bool seen = false;
            for (uint64_t i = 0; i < pair_count; i += 1)
            {
                seen = seen || pairs[i] == pair;
            }
            pairs[pair_count] = pair;
            pair_count += !seen;
            processors += 1;
        }
    }
    uint64_t allowed_count = 0;
    for (uint64_t i = 0; i < BUSTER_PARALLEL_CPUS / 64; i += 1)
    {
        allowed_count += (uint64_t)__builtin_popcountll(mask[i]);
    }
    const uint64_t cores = parallel_core_count();
    char report[256];
    const int length = snprintf(report, sizeof(report), "%llu cores, /proc/cpuinfo's %llu of %llu CPUs (%llu processors with ids)\n", (unsigned long long)cores,
                                (unsigned long long)pair_count, (unsigned long long)allowed_count, (unsigned long long)processors);
    parallel_test_check(test, "the cores", known && processors == allowed_count && cores == pair_count && cores <= allowed_count, (String8){ .pointer = report, .length = (uint64_t)length },
                        0);
    test->arena.length = 0;
}

static int run_parallel_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: test-parallel\n");
    }
    else
    {
        ParallelTest test = { .arena = arena_reserve(BUSTER_PARALLEL_ARENA_CAPACITY) };
        const char* temporary = getenv("TMPDIR");
        snprintf(test.directory, sizeof(test.directory), "%s/buster-test-parallel-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
        printf("test-parallel:\n");
        if (mkdtemp(test.directory) == 0)
        {
            printf("  can't make %s\n", test.directory);
            test.failures += 1;
        }
        else
        {
            parallel_test_lists(&test);
            parallel_test_order(&test);
            parallel_test_at_once(&test);
            parallel_test_output(&test);
            parallel_test_processes(&test);
            parallel_test_slow_reader(&test);
            parallel_test_signals(&test);
            parallel_test_command_line(&test);
            parallel_test_cores(&test);
            if (test.failures == 0)
            {
                static const char* const names[] = {
                    "slow",
                    "large",
                    "barrier1",
                    "barrier2",
                    "barrier3",
                    "log",
                    "stuck",
                    "left",
                };
                for (uint64_t i = 0; i < sizeof(names) / sizeof(names[0]); i += 1)
                {
                    char path[600];
                    parallel_test_path(&test, path, sizeof(path), names[i]);
                    unlink(path);
                }
                rmdir(test.directory);
            }
            else
            {
                printf("  the files are in %s\n", test.directory);
            }
        }
        printf("test-parallel: %llu checks, %llu failed\n", (unsigned long long)test.cases, (unsigned long long)test.failures);
        result = test.failures == 0 ? 0 : 1;
        arena_release(&test.arena);
    }
    return result;
}
