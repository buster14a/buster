// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tests' parts in child processes (#465): a test that checks many independent things (cases, files, header sets,
// inputs) runs them in parts, at most one per CPU at once, and prints each part's standard output and errors on its
// own, in the parts' order, as if it had run them one after another. A part's result, a struct of counts, comes back
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

// The CPUs this process may run on (at least 1).
static uint32_t test_process_cpus(void)
{
    uint64_t mask[16] = { 0 };
    const bool known = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0;
    uint32_t count = 0;
    for (uint32_t i = 0; known && i < sizeof(mask) / sizeof(mask[0]); i += 1)
    {
        count += (uint32_t)__builtin_popcountll(mask[i]);
    }
    return count != 0 ? count : 1;
}

// Starts a part: its output files and result pipe, and a child process. Returns true in the child, whose standard
// output and errors then go to the files, and false in the parent (with pid -1 if the part couldn't start).
static bool test_process_start(TestProcess* process)
{
    int fds[2] = { -1, -1 };
    fflush(stdout);
    fflush(stderr);
    process->output = tmpfile();
    process->errors = tmpfile();
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

// In the child: sends `size` bytes of `result` to the parent and exits.
static __attribute__((noreturn)) void test_process_exit(const TestProcess* process, const void* result, size_t size)
{
    fflush(stdout);
    fflush(stderr);
    const bool sent = process->result >= 0 && write(process->result, result, size) == (ssize_t)size;
    _exit(sent ? 0 : 1);
}

// Copies the file a part's output went to into `stream`, and closes it.
static void test_process_copy(FILE* file, FILE* stream)
{
    char chunk[1 << 16];
    size_t length = 0;
    const bool rewound = file != 0 && fseek(file, 0, SEEK_SET) == 0;
    while (rewound && (length = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        fwrite(chunk, 1, length, stream);
    }
    if (file != 0)
    {
        fclose(file);
    }
}

// In the parent: waits for the part, reads its result into `result` (`size` bytes, zeros if none came), and prints
// its standard output and then its errors. Returns whether it ran to the end and sent its result.
static bool test_process_finish(TestProcess* process, void* result, size_t size)
{
    int status = 0;
    const bool received = process->pid > 0 && read(process->result, result, size) == (ssize_t)size;
    const bool exited = process->pid > 0 && waitpid(process->pid, &status, 0) == process->pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (process->result >= 0)
    {
        close(process->result);
    }
    test_process_copy(process->output, stdout);
    fflush(stdout);
    test_process_copy(process->errors, stderr);
    if (!received)
    {
        memset(result, 0, size);
    }
    *process = (TestProcess){ .result = -1, .pid = -1 };
    return received && exited;
}

#endif
