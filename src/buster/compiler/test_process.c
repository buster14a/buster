// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tests' parts in child processes (#465): a test that checks many independent things (cases, files, header sets)
// runs them in parts, at most test_process_cpus() at once, and prints each part's standard output and errors on its own,
// in the parts' order, as if it had run them one after another. A part's result, a struct of counts, comes back
// through a pipe.
//
// The calls take the place of fork(): test_process_start returns in both processes, true in the child, which does its
// part and ends with test_process_exit; the parent collects each part, oldest first, with test_process_finish:
//
//     for (uint32_t finished = 0; finished < parts; finished += 1)
//     {
//         while (started < parts && started - finished < processes)
//         {
//             if (test_process_start(&processes[started]))
//             {
//                 const Counts counts = part(started);
//                 test_process_exit(&processes[started], &counts, sizeof(counts));
//             }
//             started += 1;
//         }
//         ran &= test_process_finish(&processes[finished], &counts, sizeof(counts));
//     }
//
// Start parts only while a process doesn't run other threads: the child has the calling thread alone.
//
// The parts are the frontend's (the preprocessor's, the parser's and the predefines' tests) and the backend's
// (test-ssa-source's files and generated programs, #2658); the tests of these helpers are the frontend's.
#ifndef BUSTER_TEST_PROCESS_INCLUDED
#define BUSTER_TEST_PROCESS_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>

// A part's child process: the files its standard output and errors go to, the pipe its result comes through, and its
// id (-1 if it couldn't start).
#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND || BUSTER_UNIT == BUSTER_UNIT_BACKEND
typedef struct TestProcess TestProcess;
struct TestProcess
{
    FILE* output;
    FILE* errors;
    int result;
    pid_t pid;
};
#endif

// The processes of `cpus` that a command the parallel runner gave `granted` CPUs runs at once: the fewer, at least 1
// for any `cpus` and `granted` past 0.
static uint32_t test_process_share(uint32_t cpus, uint32_t granted)
{
    return cpus < granted ? cpus : granted;
}

// The processes a test may run at once (at least 1): the CPUs this process may run on, or fewer when the parallel
// runner gave its command fewer (BUSTER_PARALLEL_CPUS, which it sets to each command's CPUs; #468's review, #522), so
// that the commands' processes together come to about one per CPU of the runner's rather than one per CPU each. A
// value that isn't a count from 1 to UINT32_MAX counts as no limit.
static uint32_t test_process_cpus(void)
{
    uint64_t mask[16] = { 0 };
    const bool known = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0;
    uint32_t count = 0;
    for (uint32_t i = 0; known && i < sizeof(mask) / sizeof(mask[0]); i += 1)
    {
        count += (uint32_t)__builtin_popcountll(mask[i]);
    }
    const char* text = getenv("BUSTER_PARALLEL_CPUS");
    char* end = 0;
    const unsigned long long value = text != 0 && text[0] >= '0' && text[0] <= '9' ? strtoull(text, &end, 10) : 0;
    const uint32_t granted = end != 0 && *end == 0 && value != 0 && value <= UINT32_MAX ? (uint32_t)value : UINT32_MAX;
    return test_process_share(count != 0 ? count : 1, granted);
}

// A file in memory for a process's output (memfd_create, closed on exec: a descriptor that dup2 gives a process's
// standard output or errors stays open), or a temporary file where the kernel has none. Null if neither can be made.
static FILE* test_process_file(void)
{
    // MFD_CLOEXEC, which <sys/mman.h> declares only with _GNU_SOURCE.
    const unsigned int close_on_exec = 1;
    const long descriptor = syscall(SYS_memfd_create, "buster-test-output", close_on_exec);
    FILE* result = descriptor >= 0 ? fdopen((int)descriptor, "w+") : 0;
    if (descriptor >= 0 && result == 0)
    {
        close((int)descriptor);
    }
    return result != 0 ? result : tmpfile();
}

// Starts a part: its output files and result pipe, and a child process. Returns true in the child, whose standard
// output and errors then go to the files, and false in the parent (with pid -1 if the part couldn't start).
#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND || BUSTER_UNIT == BUSTER_UNIT_BACKEND
static bool test_process_start(TestProcess* process)
{
    int fds[2] = { -1, -1 };
    fflush(stdout);
    fflush(stderr);
    process->output = test_process_file();
    process->errors = test_process_file();
    process->pid = process->output != 0 && process->errors != 0 && pipe(fds) == 0 ? fork() : -1;
    const bool child = process->pid == 0;
    if (child)
    {
        close(fds[0]);
        const bool redirected = dup2(fileno(process->output), STDOUT_FILENO) >= 0 && dup2(fileno(process->errors), STDERR_FILENO) >= 0;
        // A child that can't write where its output goes sends no result, so the parent reports the part as failed.
        process->result = redirected ? fds[1] : -1;
    }
    else
    {
        if (fds[1] >= 0)
        {
            close(fds[1]);
        }
        process->result = fds[0];
    }
    return child;
}

// oracle_cache.c's, which main.c includes after this file: the clang server a part started for its oracle runs
// (--clang-server), stopped and waited for before the part exits.
static void oracle_serve_exit(void);

// In the child: sends `size` bytes of `result` to the parent and exits, or exits without sending them if its output
// couldn't all be written, so that the parent reports the part as failed. First the clang server the part started
// (oracle_serve_exit), so that the server's CPU time is the part's and its command's, which the runner counts: left to
// end at its socket's end, it would end after the part, an orphan outside the command's processes (#2071).
static __attribute__((noreturn)) void test_process_exit(const TestProcess* process, const void* result, size_t size)
{
    oracle_serve_exit();
    const bool flushed = fflush(stdout) == 0 && !ferror(stdout);
    const bool errors_flushed = fflush(stderr) == 0 && !ferror(stderr);
    const bool sent = flushed && errors_flushed && process->result >= 0 && write(process->result, result, size) == (ssize_t)size;
    _exit(sent ? 0 : 1);
}

// Copies the file a part's output went to into `stream`, and closes it. Returns whether all of it was read and
// written (a part that couldn't start has no file, and nothing to copy).
static bool test_process_copy(FILE* file, FILE* stream)
{
    char chunk[1 << 16];
    size_t length = 0;
    bool result = file == 0 || fseek(file, 0, SEEK_SET) == 0;
    while (result && file != 0 && (length = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        result = fwrite(chunk, 1, length, stream) == length;
    }
    result = result && (file == 0 || !ferror(file)) && fflush(stream) == 0 && !ferror(stream);
    if (file != 0)
    {
        fclose(file);
    }
    return result;
}

// In the parent: waits for the part, reads its result into `result` (`size` bytes, zeros if none came), and copies its
// standard output to `output` and then its errors to `errors`. Returns whether it ran to the end, sent its result and
// had all of its output copied.
static bool test_process_finish_to(TestProcess* process, void* result, size_t size, FILE* output, FILE* errors)
{
    int status = 0;
    const bool received = process->pid > 0 && read(process->result, result, size) == (ssize_t)size;
    const bool exited = process->pid > 0 && waitpid(process->pid, &status, 0) == process->pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (process->result >= 0)
    {
        close(process->result);
    }
    const bool copied = test_process_copy(process->output, output);
    const bool errors_copied = test_process_copy(process->errors, errors);
    if (!received)
    {
        memset(result, 0, size);
    }
    *process = (TestProcess){ .result = -1, .pid = -1 };
    return received && exited && copied && errors_copied;
}

// test_process_finish_to this process's standard output and errors.
static bool test_process_finish(TestProcess* process, void* result, size_t size)
{
    return test_process_finish_to(process, result, size, stdout, stderr);
}

#if !defined(BUSTER_UNIT) || BUSTER_UNIT == BUSTER_UNIT_FRONTEND
#include "test_process_test.c"
#endif
#endif

#endif
