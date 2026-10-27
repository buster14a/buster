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
typedef struct TestProcess TestProcess;
struct TestProcess
{
    FILE* output;
    FILE* errors;
    int result;
    pid_t pid;
};

// `cpus` divided among `jobs` commands, rounded up, at least 1 for any `cpus` and `jobs` past 0 (#468's review: a
// ceiling of (cpus + jobs - 1) / jobs wraps to 0 when jobs is near UINT32_MAX).
static uint32_t test_process_share(uint32_t cpus, uint32_t jobs)
{
    return 1 + (cpus - 1) / jobs;
}

// The processes a test may run at once (at least 1): the CPUs this process may run on, divided among the commands
// that the parallel runner runs at once (BUSTER_PARALLEL_JOBS, which it sets for them; #468's review), rounded up, so
// that the commands' processes together come to about one per CPU rather than one per CPU each.
static uint32_t test_process_cpus(void)
{
    uint64_t mask[16] = { 0 };
    const bool known = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0;
    uint32_t count = 0;
    for (uint32_t i = 0; known && i < sizeof(mask) / sizeof(mask[0]); i += 1)
    {
        count += (uint32_t)__builtin_popcountll(mask[i]);
    }
    const char* text = getenv("BUSTER_PARALLEL_JOBS");
    char* end = 0;
    const unsigned long long value = text != 0 ? strtoull(text, &end, 10) : 0;
    const uint32_t jobs = end != 0 && end != text && *end == 0 && value != 0 && value <= UINT32_MAX ? (uint32_t)value : 1;
    return test_process_share(count != 0 ? count : 1, jobs);
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

// In the child: sends `size` bytes of `result` to the parent and exits, or exits without sending them if its output
// couldn't all be written, so that the parent reports the part as failed.
static __attribute__((noreturn)) void test_process_exit(const TestProcess* process, const void* result, size_t size)
{
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

// The calls' own checks (#465's review): a part whose output and result all come back; one that can't write its
// output (its standard output closed), which must send no result; a parent that can't write a part's output (into
// /dev/full), whose part must still send its result; and a part that exits without one, whose result is zeros. Each
// case must have started its part and made both of the parent's streams, so that it fails the way it names. Then
// test_process_share at its bounds. Prints a line and returns the failures.
static uint32_t test_process_test(void)
{
    uint32_t failures = 0;
    for (uint32_t c = 0; c < 4; c += 1)
    {
        char* output_text = 0;
        size_t output_length = 0;
        char* errors_text = 0;
        size_t errors_length = 0;
        FILE* output = c == 2 ? fopen("/dev/full", "w") : open_memstream(&output_text, &output_length);
        FILE* errors = open_memstream(&errors_text, &errors_length);
        const bool streams = output != 0 && errors != 0;
        TestProcess process = { .result = -1, .pid = -1 };
        if (streams && test_process_start(&process))
        {
            uint64_t sent = 0x5eed0000u + c;
            if (c == 1)
            {
                close(STDOUT_FILENO);
            }
            printf("output %u\n", c);
            fprintf(stderr, "errors %u\n", c);
            if (c == 3)
            {
                _exit(3);
            }
            test_process_exit(&process, &sent, sizeof(sent));
        }
        const bool started = process.pid > 0;
        uint64_t received = 1;
        const bool ran = streams && test_process_finish_to(&process, &received, sizeof(received), output, errors);
        if (output != 0)
        {
            fclose(output);
        }
        if (errors != 0)
        {
            fclose(errors);
        }
        char wanted[32];
        snprintf(wanted, sizeof(wanted), "output %u\n", c);
        char wanted_errors[32];
        snprintf(wanted_errors, sizeof(wanted_errors), "errors %u\n", c);
        const bool whole = ran && received == 0x5eed0000u && output_text != 0 && strcmp(output_text, wanted) == 0 && errors_text != 0 &&
                           strcmp(errors_text, wanted_errors) == 0;
        const bool failed = !ran && received == (c == 2 ? 0x5eed0002u : 0);
        const bool expected = streams && started && (c == 0 ? whole : failed);
        if (!expected)
        {
            printf("test processes: case %u came back %s (%s, %s)\n", c, ran ? "as run" : "as failed", streams ? "streams made" : "no streams",
                   started ? "started" : "not started");
        }
        failures += !expected;
        free(output_text);
        free(errors_text);
    }
    // The share's bounds: one CPU, as many jobs as CPUs and more, and the most jobs --jobs and BUSTER_PARALLEL_JOBS take.
    const uint32_t shares[][3] = {
        { 1, 1, 1 },
        { 16, 1, 16 },
        { 16, 8, 2 },
        { 15, 8, 2 },
        { 16, 16, 1 },
        { 16, 17, 1 },
        { 16, UINT32_MAX, 1 },
        { UINT32_MAX, 1, UINT32_MAX },
        { UINT32_MAX, UINT32_MAX, 1 },
    };
    for (uint32_t i = 0; i < sizeof(shares) / sizeof(shares[0]); i += 1)
    {
        const uint32_t share = test_process_share(shares[i][0], shares[i][1]);
        if (share != shares[i][2])
        {
            printf("test processes: %u CPUs among %u jobs gave %u, not %u\n", shares[i][0], shares[i][1], share, shares[i][2]);
        }
        failures += share != shares[i][2];
    }
    printf("test processes: %u of 13 cases fail (output, a part's output lost, the parent's lost, a failed part, 9 shares)\n", failures);
    return failures;
}

#endif
