// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The external tools' probe (#1129). A cross-check that spawns perf, GCC or binutils asks this once, before its
// sections, and reports a missing or older tool as a skip with the version it found. The probe runs the tool's
// --version in its own process group, its standard output and errors on one pipe, so a tool that prints its version
// there is seen too and a deadline kill takes a wrapper's subprocesses with it.
#ifndef BUSTER_TEST_TOOL_SOURCE_INCLUDED
#define BUSTER_TEST_TOOL_SOURCE_INCLUDED

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

#include "buster/compiler/test_tool.h"

// How long a tool's --version may run before the probe kills it. The helper's test lowers it to cover a hanging tool
// without slowing test-tool.
#define BUSTER_TEST_TOOL_RUN_SECONDS 60
static uint32_t test_tool_run_seconds = BUSTER_TEST_TOOL_RUN_SECONDS;

// The value of a run of decimal digits, saturated at UINT32_MAX.
static uint32_t test_tool_number(const char** cursor)
{
    uint64_t value = 0;
    while (**cursor >= '0' && **cursor <= '9')
    {
        const uint64_t digit = (uint64_t)(**cursor - '0');
        value = value > (UINT64_MAX - digit) / 10 ? UINT64_MAX : value * 10 + digit;
        *cursor += 1;
    }
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

// One dotted decimal run at `cursor` (at a digit), its first BUSTER_TEST_TOOL_COMPONENTS components into `values`.
// Returns the run's component count and leaves `cursor` past the run.
static uint32_t test_tool_version_run(const char** cursor, uint32_t* values)
{
    uint32_t length = 0;
    while (**cursor >= '0' && **cursor <= '9')
    {
        const uint32_t value = test_tool_number(cursor);
        if (length < BUSTER_TEST_TOOL_COMPONENTS)
        {
            values[length] = value;
        }
        length += 1;
        if ((*cursor)[0] != '.' || (*cursor)[1] < '0' || (*cursor)[1] > '9')
        {
            break;
        }
        *cursor += 1;
    }
    return length;
}

// The version a tool's --version prints, on the first output line that holds a digit. GNU banners put the package's
// identification in parentheses and the version after them ("gcc (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0"), so the run
// wins among the digit runs after the line's first ')' — or, where none follow it ("perf version 6.8.12"), among the
// ones before its first '('. The run with the most components is the version, the first on a tie: a target-prefixed
// name ("x86_64-conda-linux-gnu-gcc (conda-forge gcc 9.4.0-1) 9.4.0") is then 9.4.0, not 86 or the package's, and a
// trailing date ("gcc (GCC) 16.2.1 20260101") or suffix ("perf version 6.8.12-1-generic") doesn't win either.
// `version` keeps up to BUSTER_TEST_TOOL_COMPONENTS components. False when no line has a digit.
static bool test_tool_parse_version(const char* text, uint32_t* version, uint32_t* count)
{
    bool parsed = false;
    const char* line = text;
    while (!parsed && line != 0 && *line != 0)
    {
        const char* end = line;
        while (*end != 0 && *end != '\n')
        {
            end += 1;
        }
        const char* paren = 0;
        const char* close = 0;
        for (const char* at = line; at < end; at += 1)
        {
            if (*at == '(' && paren == 0)
            {
                paren = at;
            }
            else if (*at == ')' && close == 0)
            {
                close = at;
            }
        }
        const char* begin = line;
        const char* limit = paren != 0 ? paren : end;
        if (close != 0)
        {
            bool digits_after_close = false;
            for (const char* at = close + 1; at < end; at += 1)
            {
                digits_after_close = digits_after_close || (*at >= '0' && *at <= '9');
            }
            if (digits_after_close)
            {
                begin = close + 1;
                limit = end;
            }
        }
        uint32_t best_values[BUSTER_TEST_TOOL_COMPONENTS] = { 0 };
        uint32_t best = 0;
        const char* cursor = begin;
        while (cursor < limit)
        {
            if (*cursor >= '0' && *cursor <= '9')
            {
                uint32_t values[BUSTER_TEST_TOOL_COMPONENTS] = { 0 };
                const uint32_t length = test_tool_version_run(&cursor, values);
                if (length > best)
                {
                    best = length;
                    for (uint32_t i = 0; i < BUSTER_TEST_TOOL_COMPONENTS; i += 1)
                    {
                        best_values[i] = values[i];
                    }
                }
            }
            else
            {
                cursor += 1;
            }
        }
        if (best != 0)
        {
            for (uint32_t i = 0; i < BUSTER_TEST_TOOL_COMPONENTS; i += 1)
            {
                version[i] = best_values[i];
            }
            *count = best < BUSTER_TEST_TOOL_COMPONENTS ? best : BUSTER_TEST_TOOL_COMPONENTS;
            parsed = true;
        }
        line = *end == '\n' ? end + 1 : end;
    }
    return parsed;
}

// A version comparison in the order -1, 0, 1; a missing component is 0, so 16 equals 16.0.0.
static int test_tool_compare(const uint32_t* left, uint32_t left_count, const uint32_t* right, uint32_t right_count)
{
    int result = 0;
    const uint32_t count = left_count > right_count ? left_count : right_count;
    for (uint32_t i = 0; i < count && result == 0; i += 1)
    {
        const uint32_t a = i < left_count ? left[i] : 0;
        const uint32_t b = i < right_count ? right[i] : 0;
        result = a < b ? -1 : a > b ? 1 : 0;
    }
    return result;
}

// `version` as dotted decimal text: "16.2.1" keeps every component the tool printed.
static void test_tool_format_version(const uint32_t* version, uint32_t count, char* text, size_t capacity)
{
    size_t length = 0;
    if (capacity != 0)
    {
        text[0] = 0;
    }
    for (uint32_t i = 0; i < count && length + 1 < capacity; i += 1)
    {
        const int written = snprintf(text + length, capacity - length, i == 0 ? "%u" : ".%u", version[i]);
        length = written > 0 && (size_t)written < capacity - length ? length + (size_t)written : capacity;
    }
}

// The monotonic clock in milliseconds, for the probe's deadline.
static uint64_t test_tool_clock_milliseconds(void)
{
    struct timespec time = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}

// Reads what `fd` holds now into `text` at `*length`, keeping at most `capacity - 1` bytes in all. False when the read
// failed for a reason other than its nonblocking pipe being empty.
static bool test_tool_drain(int fd, char* text, size_t capacity, size_t* length)
{
    bool drained = true;
    char chunk[4096];
    ssize_t got = read(fd, chunk, sizeof(chunk));
    while (got < 0 && errno == EINTR)
    {
        got = read(fd, chunk, sizeof(chunk));
    }
    const size_t limit = capacity != 0 ? capacity - 1 : 0;
    while (got > 0 && *length < limit)
    {
        const size_t copy = (size_t)got < limit - *length ? (size_t)got : limit - *length;
        if (copy != 0)
        {
            memcpy(text + *length, chunk, copy);
            *length += copy;
        }
        got = read(fd, chunk, sizeof(chunk));
    }
    if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        drained = false;
    }
    if (capacity != 0)
    {
        text[*length] = 0;
    }
    return drained;
}

// Runs `command --version`, its standard output and errors into `text`, and waits for it: 0 when it ran to the end and
// exited 0, -1 when it couldn't start (*error says why), -2 when it ran but didn't exit 0 (*error is its exit status, or
// 128 plus the signal that ended it) and -3 when it didn't end within test_tool_run_seconds, in which case it was
// killed and reaped. `text` holds as much of what it wrote as
// `capacity` keeps.
static int test_tool_run_version(const char* command, char* text, size_t capacity, int* error)
{
    int result = -1;
    int fds[2] = { -1, -1 };
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    bool actions_initialized = false;
    bool attributes_initialized = false;
    *error = 0;
    if (capacity != 0)
    {
        text[0] = 0;
    }
    if (pipe(fds) == 0)
    {
        actions_initialized = posix_spawn_file_actions_init(&actions) == 0;
        attributes_initialized = posix_spawnattr_init(&attributes) == 0;
        bool ready = actions_initialized && attributes_initialized && posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO) == 0 &&
                     posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO) == 0;
        if (ready)
        {
            // The probe's own process group, so the deadline below kills a wrapper's subprocesses with it.
            short spawn_flags = 0;
            ready = posix_spawnattr_getflags(&attributes, &spawn_flags) == 0 && posix_spawnattr_setflags(&attributes, spawn_flags | POSIX_SPAWN_SETPGROUP) == 0 &&
                    posix_spawnattr_setpgroup(&attributes, 0) == 0;
        }
        char* arguments[] = { (char*)command, (char*)"--version", 0 };
        pid_t child = 0;
        if (ready)
        {
            *error = posix_spawnp(&child, command, &actions, &attributes, arguments, environ);
            ready = *error == 0;
        }
        else if (*error == 0)
        {
            *error = ENOMEM;
        }
        if (ready)
        {
            close(fds[1]);
            fds[1] = -1;
            // Nonblocking, so that the wait loop reads between checks for the child's end instead of blocking on a tool
            // that writes nothing.
            const int flags = fcntl(fds[0], F_GETFL, 0);
            if (flags >= 0)
            {
                fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
            }
            size_t length = 0;
            const uint64_t started = test_tool_clock_milliseconds();
            bool exited = false;
            int status = 0;
            do
            {
                test_tool_drain(fds[0], text, capacity, &length);
                pid_t waited = waitpid(child, &status, WNOHANG);
                exited = waited == child;
                if (!exited && waited < 0 && errno != EINTR)
                {
                    break;
                }
                if (!exited)
                {
                    const struct timespec pause = {
                        .tv_sec = 0,
                        .tv_nsec = 1000000,
                    };
                    nanosleep(&pause, 0);
                }
            }
            while (!exited && test_tool_clock_milliseconds() - started < 1000ull * test_tool_run_seconds);
            test_tool_drain(fds[0], text, capacity, &length);
            if (capacity != 0)
            {
                text[length] = 0;
            }
            if (exited)
            {
                result = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -2;
                *error = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 0;
            }
            else
            {
                // The probe's whole group: a tool's subprocesses die with it. The direct kill covers a group that
                // somehow never formed.
                if (kill(-child, SIGKILL) != 0)
                {
                    kill(child, SIGKILL);
                }
                int waited_status = 0;
                pid_t waited = -1;
                do
                {
                    waited = waitpid(child, &waited_status, 0);
                }
                while (waited < 0 && errno == EINTR);
                *error = ETIMEDOUT;
                result = -3;
            }
        }
    }
    else
    {
        *error = errno;
    }
    if (fds[0] >= 0)
    {
        close(fds[0]);
    }
    if (fds[1] >= 0)
    {
        close(fds[1]);
    }
    if (actions_initialized)
    {
        posix_spawn_file_actions_destroy(&actions);
    }
    if (attributes_initialized)
    {
        posix_spawnattr_destroy(&attributes);
    }
    return result;
}

static TestTool test_tool_probe(const char* name, const char* command, const uint32_t* minimum, uint32_t minimum_count)
{
    TestTool tool = {
        .name = name,
        .command = command != 0 ? command : name,
        .minimum_count = minimum_count < BUSTER_TEST_TOOL_COMPONENTS ? minimum_count : BUSTER_TEST_TOOL_COMPONENTS,
    };
    for (uint32_t i = 0; i < tool.minimum_count; i += 1)
    {
        tool.minimum[i] = minimum[i];
    }
    char text[1024];
    int error = 0;
    const int ran = test_tool_run_version(tool.command, text, sizeof(text), &error);
    tool.found = ran != -1;
    if (!tool.found)
    {
        if (error == ENOENT || error == ENOTDIR)
        {
            snprintf(tool.unavailable, sizeof(tool.unavailable), "not found on PATH");
        }
        else
        {
            snprintf(tool.unavailable, sizeof(tool.unavailable), "can't run: %s", strerror(error));
        }
    }
    else if (ran == -3)
    {
        tool.timed_out = true;
        snprintf(tool.unavailable, sizeof(tool.unavailable), "didn't end within %u s", test_tool_run_seconds);
    }
    else if (ran == -2)
    {
        // A tool that starts but can't report its version (a wrapper saying the running kernel has no perf) is no more
        // usable than a missing one, minimum or not: its consumers would only fail opaquely.
        tool.failed = true;
        snprintf(tool.unavailable, sizeof(tool.unavailable), "--version exited with status %d", error);
    }
    else
    {
        tool.version_parsed = test_tool_parse_version(text, tool.version, &tool.version_count);
        if (tool.version_parsed)
        {
            test_tool_format_version(tool.version, tool.version_count, tool.version_text, sizeof(tool.version_text));
        }
        else
        {
            snprintf(tool.unavailable, sizeof(tool.unavailable), "version unreadable");
        }
    }
    tool.available = tool.found && !tool.timed_out && !tool.failed && (tool.minimum_count == 0 || (tool.version_parsed && test_tool_compare(tool.version, tool.version_count, tool.minimum, tool.minimum_count) >= 0));
    return tool;
}

static bool test_tool_available(const TestTool* tool)
{
    return tool != 0 && tool->available;
}

static void test_tool_skip_to(FILE* stream, const TestTool* tool, const char* section)
{
    if (stream != 0 && tool != 0)
    {
        if (section != 0 && section[0] != 0)
        {
            fprintf(stream, "%s: ", section);
        }
        fprintf(stream, "skipped: needs %s", tool->name);
        if (tool->minimum_count != 0)
        {
            fputs(" >= ", stream);
            for (uint32_t i = 0; i < tool->minimum_count; i += 1)
            {
                fprintf(stream, i == 0 ? "%u" : ".%u", tool->minimum[i]);
            }
        }
        if (!tool->found || tool->timed_out || tool->failed)
        {
            fprintf(stream, ", %s", tool->unavailable);
        }
        else if (!tool->version_parsed)
        {
            fputs(", found version unreadable", stream);
        }
        else
        {
            fprintf(stream, ", found %s", tool->version_text);
        }
        fputc('\n', stream);
    }
}

static void test_tool_skip(const TestTool* tool, const char* section)
{
    test_tool_skip_to(stdout, tool, section);
}

static void test_tool_check(bool condition, const char* detail, uint32_t* failures, uint32_t* checks)
{
    if (!condition)
    {
        printf("test tools: %s\n", detail);
    }
    *checks += 1;
    *failures += !condition;
}

// The skip line `tool` and `section` must produce, compared whole.
static void test_tool_check_skip(const TestTool* tool, const char* section, const char* expected, uint32_t* failures, uint32_t* checks)
{
    char* text = 0;
    size_t length = 0;
    FILE* stream = open_memstream(&text, &length);
    bool same = stream != 0;
    if (stream != 0)
    {
        test_tool_skip_to(stream, tool, section);
        same = fclose(stream) == 0 && text != 0 && strcmp(text, expected) == 0;
    }
    if (!same)
    {
        printf("test tools: \"%s\" printed \"%s\", not \"%s\"\n", section, text != 0 ? text : "", expected);
    }
    *checks += 1;
    *failures += !same;
    free(text);
}

// A usage error for the test-tool command's unexpected arguments. `errors` is where it goes (stderr for the command),
// so that the helper's test can compare it; it doesn't call the helper's checks, which keeps the command's call graph
// acyclic for the repository's check.
static int test_tool_refuse(FILE* errors, int count, char** arguments)
{
    if (count != 0 && errors != 0)
    {
        fprintf(errors, "test-tool: unexpected argument '%s'\nusage: test-tool\n", arguments[0]);
    }
    return 1;
}

// The test-tool command's arguments: the helper's checks with none, and a usage error for any other. `errors` is where
// the error goes (stderr for the command), so that the helper's test can compare it.
static int test_tool_command_to(FILE* errors, int count, char** arguments)
{
    return count == 0 ? (test_tool_test() == 0 ? 0 : 1) : test_tool_refuse(errors, count, arguments);
}

static int test_tool_command(int count, char** arguments)
{
    return test_tool_command_to(stderr, count, arguments);
}

static uint32_t test_tool_test(void)
{
    uint32_t failures = 0;
    uint32_t checks = 0;
    uint32_t version[BUSTER_TEST_TOOL_COMPONENTS] = { 0 };
    uint32_t count = 0;
    bool parsed = test_tool_parse_version("gcc (GCC) 16.2.1 20260101", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 16 && version[1] == 2 && version[2] == 1, "gcc 16.2.1 parses", &failures, &checks);
    parsed = test_tool_parse_version("GNU assembler (GNU Binutils for Ubuntu) 2.42", version, &count);
    test_tool_check(parsed && count == 2 && version[0] == 2 && version[1] == 42, "as 2.42 parses", &failures, &checks);
    parsed = test_tool_parse_version("perf version 6.8.12-1-generic", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 6 && version[1] == 8 && version[2] == 12, "perf 6.8.12 parses", &failures, &checks);
    parsed = test_tool_parse_version("x86_64-linux-gnu-gcc (GCC) 16.2.1", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 16 && version[1] == 2 && version[2] == 1, "a target-prefixed gcc parses its version, not the target's digits", &failures, &checks);
    parsed = test_tool_parse_version("x86_64-conda-linux-gnu-gcc (conda-forge gcc 9.4.0-1) 9.4.0", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 9 && version[1] == 4 && version[2] == 0, "a digit-bearing package name doesn't hide the version", &failures, &checks);
    parsed = test_tool_parse_version("gcc (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 13 && version[1] == 3 && version[2] == 0, "a distribution's package doesn't hide the version", &failures, &checks);
    parsed = test_tool_parse_version("no version here", version, &count);
    test_tool_check(!parsed, "text without a version doesn't parse", &failures, &checks);
    const uint32_t sixteen[] = { 16 };
    const uint32_t sixteen_zeros[] = { 16, 0, 0 };
    const uint32_t newer[] = { 16, 2, 1 };
    const uint32_t older[] = { 13, 3, 0 };
    const uint32_t as_old[] = { 2, 42 };
    const uint32_t as_minimum[] = { 2, 47 };
    test_tool_check(test_tool_compare(sixteen_zeros, 3, sixteen, 1) == 0, "16.0.0 equals 16", &failures, &checks);
    test_tool_check(test_tool_compare(newer, 3, sixteen, 1) > 0, "16.2.1 is newer than 16", &failures, &checks);
    test_tool_check(test_tool_compare(older, 3, sixteen, 1) < 0, "13.3.0 is older than 16", &failures, &checks);
    test_tool_check(test_tool_compare(as_old, 2, as_minimum, 2) < 0, "2.42 is older than 2.47", &failures, &checks);
    const char* temporary = oracle_temporary_directory();
    char path[4200];
    snprintf(path, sizeof(path), "%s/buster-test-tool-XXXXXX", temporary);
    const int descriptor = mkstemp(path);
    const char script[] = "#!/bin/sh\nprintf 'fake gcc 16.2.1\\n'\n";
    bool written = descriptor >= 0 && write(descriptor, script, sizeof(script) - 1) == (ssize_t)(sizeof(script) - 1) && fchmod(descriptor, 0755) == 0;
    if (descriptor >= 0)
    {
        written = close(descriptor) == 0 && written;
    }
    test_tool_check(written, "the fake tool is written", &failures, &checks);
    char unreadable_path[4200];
    snprintf(unreadable_path, sizeof(unreadable_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int unreadable_descriptor = mkstemp(unreadable_path);
    const char quiet_script[] = "#!/bin/sh\nexit 0\n";
    bool quiet_written = unreadable_descriptor >= 0 && write(unreadable_descriptor, quiet_script, sizeof(quiet_script) - 1) == (ssize_t)(sizeof(quiet_script) - 1) &&
                         fchmod(unreadable_descriptor, 0755) == 0;
    if (unreadable_descriptor >= 0)
    {
        quiet_written = close(unreadable_descriptor) == 0 && quiet_written;
    }
    test_tool_check(quiet_written, "the quiet tool is written", &failures, &checks);
    char hanging_path[4200];
    snprintf(hanging_path, sizeof(hanging_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int hanging_descriptor = mkstemp(hanging_path);
    const char hanging_script[] = "#!/bin/sh\nexec /bin/sleep 5\n";
    bool hanging_written = hanging_descriptor >= 0 && write(hanging_descriptor, hanging_script, sizeof(hanging_script) - 1) == (ssize_t)(sizeof(hanging_script) - 1) &&
                           fchmod(hanging_descriptor, 0755) == 0;
    if (hanging_descriptor >= 0)
    {
        hanging_written = close(hanging_descriptor) == 0 && hanging_written;
    }
    test_tool_check(hanging_written, "the hanging tool is written", &failures, &checks);
    // A tool that waits on a subprocess of its own, which the deadline's group kill must take with it. The subprocess's
    // pid goes to a file the test reads, so it can check the kill reached the whole group rather than the tool alone.
    char wrapper_path[4200];
    snprintf(wrapper_path, sizeof(wrapper_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int wrapper_descriptor = mkstemp(wrapper_path);
    char pid_path[4200];
    snprintf(pid_path, sizeof(pid_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int pid_descriptor = mkstemp(pid_path);
    if (pid_descriptor >= 0)
    {
        close(pid_descriptor);
        unlink(pid_path);
    }
    char wrapper_script[4600];
    const int wrapper_length = snprintf(wrapper_script, sizeof(wrapper_script), "#!/bin/sh\n/bin/sleep 30 &\nprintf '%%s\\n' \"$!\" > \"%s\"\nwait\n", pid_path);
    bool wrapper_written = wrapper_length > 0 && (size_t)wrapper_length < sizeof(wrapper_script) && wrapper_descriptor >= 0 &&
                           write(wrapper_descriptor, wrapper_script, (size_t)wrapper_length) == (ssize_t)wrapper_length && fchmod(wrapper_descriptor, 0755) == 0;
    if (wrapper_descriptor >= 0)
    {
        wrapper_written = close(wrapper_descriptor) == 0 && wrapper_written;
    }
    test_tool_check(wrapper_written && pid_descriptor >= 0, "the wrapping tool is written", &failures, &checks);
    // A target-prefixed tool whose own version is old: the gate must read the version, not the target's digits.
    char target_path[4200];
    snprintf(target_path, sizeof(target_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int target_descriptor = mkstemp(target_path);
    const char target_script[] = "#!/bin/sh\nprintf 'x86_64-conda-linux-gnu-gcc (conda-forge gcc 9.4.0-1) 9.4.0\\n'\n";
    bool target_written = target_descriptor >= 0 && write(target_descriptor, target_script, sizeof(target_script) - 1) == (ssize_t)(sizeof(target_script) - 1) &&
                          fchmod(target_descriptor, 0755) == 0;
    if (target_descriptor >= 0)
    {
        target_written = close(target_descriptor) == 0 && target_written;
    }
    test_tool_check(target_written, "the target-prefixed tool is written", &failures, &checks);
    // A tool that starts but whose --version fails, as a perf wrapper does on a kernel without its perf: unavailable
    // even where no minimum is asked, since its consumers would fail opaquely.
    char failing_path[4200];
    snprintf(failing_path, sizeof(failing_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int failing_descriptor = mkstemp(failing_path);
    const char failing_script[] = "#!/bin/sh\nprintf 'WARNING: perf not found for kernel 6.18.44\\n'\nexit 2\n";
    bool failing_written = failing_descriptor >= 0 && write(failing_descriptor, failing_script, sizeof(failing_script) - 1) == (ssize_t)(sizeof(failing_script) - 1) &&
                           fchmod(failing_descriptor, 0755) == 0;
    if (failing_descriptor >= 0)
    {
        failing_written = close(failing_descriptor) == 0 && failing_written;
    }
    test_tool_check(failing_written, "the failing tool is written", &failures, &checks);
    const uint32_t minimum_fifteen[] = { 15 };
    const uint32_t minimum_sixteen[] = { 16 };
    const uint32_t minimum_seventeen[] = { 17 };
    TestTool modern = { 0 };
    TestTool old = { 0 };
    if (written)
    {
        modern = test_tool_probe("gcc", path, minimum_sixteen, 1);
        old = test_tool_probe("gcc", path, minimum_seventeen, 1);
    }
    test_tool_check(modern.found && modern.version_parsed && test_tool_available(&modern) && strcmp(modern.version_text, "16.2.1") == 0, "the fake gcc 16.2.1 meets 16", &failures, &checks);
    test_tool_check(old.found && old.version_parsed && !test_tool_available(&old) && strcmp(old.version_text, "16.2.1") == 0, "the fake gcc 16.2.1 doesn't meet 17", &failures, &checks);
    const TestTool missing = test_tool_probe("absent-tool", "buster-test-tool-that-does-not-exist-1129", minimum_sixteen, 1);
    test_tool_check(!missing.found && !missing.available && strcmp(missing.unavailable, "not found on PATH") == 0, "a missing tool isn't found", &failures, &checks);
    const TestTool unreadable = quiet_written ? test_tool_probe("quiet", unreadable_path, minimum_sixteen, 1) : missing;
    test_tool_check(quiet_written && unreadable.found && !unreadable.version_parsed && !unreadable.available && strcmp(unreadable.unavailable, "version unreadable") == 0,
                    "a tool without a version can't meet one", &failures, &checks);
    TestTool failing = { 0 };
    TestTool failing_minimum = { 0 };
    if (failing_written)
    {
        failing = test_tool_probe("perf", failing_path, 0, 0);
        failing_minimum = test_tool_probe("perf", failing_path, minimum_sixteen, 1);
    }
    test_tool_check(failing_written && failing.found && failing.failed && !failing.version_parsed && !test_tool_available(&failing) &&
                        strcmp(failing.unavailable, "--version exited with status 2") == 0,
                    "a tool whose --version fails is unavailable without a minimum", &failures, &checks);
    test_tool_check(failing_written && failing_minimum.found && failing_minimum.failed && !test_tool_available(&failing_minimum),
                    "a tool whose --version fails is unavailable with a minimum, though its output holds digits", &failures, &checks);
    TestTool hanging = { 0 };
    if (hanging_written)
    {
        const uint32_t saved_seconds = test_tool_run_seconds;
        test_tool_run_seconds = 1;
        hanging = test_tool_probe("gcc", hanging_path, minimum_sixteen, 1);
        test_tool_run_seconds = saved_seconds;
    }
    test_tool_check(hanging_written && hanging.found && hanging.timed_out && !hanging.version_parsed && !test_tool_available(&hanging),
                    "a hanging tool is killed and unavailable", &failures, &checks);
    TestTool wrapper = { 0 };
    long grandchild = 0;
    if (wrapper_written)
    {
        const uint32_t saved_seconds = test_tool_run_seconds;
        test_tool_run_seconds = 1;
        wrapper = test_tool_probe("gcc", wrapper_path, minimum_sixteen, 1);
        test_tool_run_seconds = saved_seconds;
        FILE* pid_file = fopen(pid_path, "r");
        if (pid_file != 0)
        {
            if (fscanf(pid_file, "%ld", &grandchild) != 1)
            {
                grandchild = 0;
            }
            fclose(pid_file);
        }
    }
    // The recorded subprocess must have died with the probe's group: gone entirely, or a zombie that a reaping init
    // collects. A live one means the kill missed the group; it is killed here so a regression doesn't leak it.
    bool subprocess_gone = false;
    for (uint32_t tries = 0; grandchild > 0 && !subprocess_gone && tries < 200; tries += 1)
    {
        char process_path[64];
        snprintf(process_path, sizeof(process_path), "/proc/%ld/stat", grandchild);
        FILE* process_file = fopen(process_path, "r");
        if (process_file != 0)
        {
            char status[256] = { 0 };
            const size_t got = fread(status, 1, sizeof(status) - 1, process_file);
            fclose(process_file);
            const char* after = got > 0 ? strrchr(status, ')') : 0;
            const char state = after != 0 && after[1] == ' ' ? after[2] : 0;
            subprocess_gone = state == 'Z' || state == 'X' || state == 'x';
        }
        else
        {
            subprocess_gone = kill(grandchild, 0) != 0 && errno == ESRCH;
        }
        if (!subprocess_gone)
        {
            const struct timespec pause = {
                .tv_sec = 0,
                .tv_nsec = 10000000,
            };
            nanosleep(&pause, 0);
        }
    }
    if (grandchild > 0 && !subprocess_gone)
    {
        kill(grandchild, SIGKILL);
    }
    test_tool_check(wrapper.found && wrapper.timed_out && !test_tool_available(&wrapper) && subprocess_gone,
                    "a tool's whole process group is killed at the deadline", &failures, &checks);
    TestTool target = { 0 };
    if (target_written)
    {
        target = test_tool_probe("gcc", target_path, minimum_fifteen, 1);
    }
    test_tool_check(target.found && target.version_parsed && !test_tool_available(&target) && strcmp(target.version_text, "9.4.0") == 0,
                    "a target-prefixed gcc 9.4.0 doesn't meet 15", &failures, &checks);
    if (target_written)
    {
        test_tool_check_skip(&target, "test target", "test target: skipped: needs gcc >= 15, found 9.4.0\n", &failures, &checks);
    }
    if (written)
    {
        test_tool_check_skip(&old, "test old", "test old: skipped: needs gcc >= 17, found 16.2.1\n", &failures, &checks);
    }
    test_tool_check_skip(&missing, "test missing", "test missing: skipped: needs absent-tool >= 16, not found on PATH\n", &failures, &checks);
    if (quiet_written)
    {
        test_tool_check_skip(&unreadable, "test unreadable", "test unreadable: skipped: needs quiet >= 16, found version unreadable\n", &failures, &checks);
    }
    if (failing_written)
    {
        test_tool_check_skip(&failing, "test failing", "test failing: skipped: needs perf, --version exited with status 2\n", &failures, &checks);
    }
    if (hanging_written)
    {
        test_tool_check_skip(&hanging, "test hanging", "test hanging: skipped: needs gcc >= 16, didn't end within 1 s\n", &failures, &checks);
    }
    const TestTool missing_perf = test_tool_probe("perf", "buster-test-tool-that-does-not-exist-1129", 0, 0);
    test_tool_check_skip(&missing_perf, "test perf", "test perf: skipped: needs perf, not found on PATH\n", &failures, &checks);
    // The stdout path the cross-checks call, shown once as a reader sees it.
    test_tool_skip(&missing, "test tools example");
    // test-tool itself accepts no arguments: the command's usage error, as main.c dispatches it.
    char* usage = 0;
    size_t usage_length = 0;
    FILE* usage_stream = open_memstream(&usage, &usage_length);
    char* extra[] = { "extra" };
    bool refused = usage_stream != 0 && test_tool_refuse(usage_stream, 1, extra) == 1;
    if (usage_stream != 0)
    {
        refused = fclose(usage_stream) == 0 && refused && usage != 0 && strcmp(usage, "test-tool: unexpected argument 'extra'\nusage: test-tool\n") == 0;
    }
    test_tool_check(refused, "test-tool refuses extra arguments", &failures, &checks);
    free(usage);
    if (descriptor >= 0)
    {
        unlink(path);
    }
    if (unreadable_descriptor >= 0)
    {
        unlink(unreadable_path);
    }
    if (hanging_descriptor >= 0)
    {
        unlink(hanging_path);
    }
    if (wrapper_descriptor >= 0)
    {
        unlink(wrapper_path);
    }
    if (target_descriptor >= 0)
    {
        unlink(target_path);
    }
    if (failing_descriptor >= 0)
    {
        unlink(failing_path);
    }
    unlink(pid_path);
    printf("test tools: %u checks, %u failed\n", checks, failures);
    return failures;
}

#endif
