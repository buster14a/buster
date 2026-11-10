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

#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_DRIVER
#include "test_tool_test.c"
#endif

#endif
