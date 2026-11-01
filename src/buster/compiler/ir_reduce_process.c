// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Linux process boundary for the standalone reducer's trusted, single-threaded built-in observers. The child
// inherits private arena mappings copy-on-write; only a fixed-size observation crosses the pipe. A signal,
// missing response or deadline cannot masquerade as a classified failure. This is not a hostile-code sandbox
// or an external executable protocol. Callbacks must not start threads/children or use shared writable mappings.
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct IrReduceProcess IrReduceProcess;
struct IrReduceProcess
{
    IrReducePredicate predicate;
    void* context;
    uint64_t started;
};

// Own child must be a process-group leader. Polling is deliberately outside all compiler hot paths.
// Reap only this PID, including after interruption or timeout; never consume another caller's child.
static IrReduceObservationStatus ir_reduce_process_wait(pid_t child, uint64_t started, uint64_t milliseconds, int* status)
{
    IrReduceObservationStatus result = IR_REDUCE_PREDICATE_ERROR;
    bool owned = child > 0;
    bool finished = false;
    while (owned && !finished)
    {
        const pid_t waited = waitpid(child, status, WNOHANG);
        if (waited == child)
        {
            finished = true;
            owned = false;
            const uint64_t now = ir_reduce_milliseconds();
            result = now == UINT64_MAX || now < started || now - started >= milliseconds ?
                     IR_REDUCE_PREDICATE_LIMIT : IR_REDUCE_OBSERVED;
        }
        else if (waited < 0 && errno != EINTR)
        {
            // ECHILD means a caller's SIGCHLD policy reaped it; its PID is no longer ours to signal.
            owned = errno != ECHILD;
            finished = true;
        }
        else
        {
            const uint64_t now = ir_reduce_milliseconds();
            if (now == UINT64_MAX || now < started || now - started >= milliseconds)
            {
                result = IR_REDUCE_PREDICATE_LIMIT;
                finished = true;
            }
            else
            {
                const struct timespec pause = { .tv_nsec = 1000000 };
                nanosleep(&pause, NULL);
            }
        }
    }
    if (owned)
    {
        kill(-child, SIGKILL);
        kill(child, SIGKILL);
        pid_t waited = 0;
        do
        {
            waited = waitpid(child, status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited != child)
        {
            result = IR_REDUCE_PREDICATE_ERROR;
        }
    }
    return result;
}

static IrReduceObservation ir_reduce_process(const IrFunction* function, const IrReduceLimits* limits, void* context, Arena* scratch)
{
    const IrReduceProcess* process = context;
    IrReduceObservation result = { .status = IR_REDUCE_PREDICATE_ERROR };
    const uint64_t now = ir_reduce_milliseconds();
    const bool valid = process != NULL && process->predicate != NULL && limits != NULL && scratch != NULL &&
                       now != UINT64_MAX && process->started != UINT64_MAX && now >= process->started;
    if (valid)
    {
        // The descriptor and limits are immutable in the parent throughout the child lifetime.
        const uint64_t elapsed = now - process->started;
        const uint64_t total = limits->max_milliseconds;
        const uint64_t per_call = limits->predicate_milliseconds;
        const uint64_t remaining = elapsed < total ? total - elapsed : 0;
        const uint64_t budget = remaining < per_call ? remaining : per_call;
        int descriptors[2] = { -1, -1 };
        if (budget == 0)
        {
            result.status = IR_REDUCE_PREDICATE_LIMIT;
        }
        else if (syscall(SYS_pipe2, descriptors, O_CLOEXEC | O_NONBLOCK) == 0)
        {
            const IrReducePredicate predicate = process->predicate;
            void* const observer = process->context;
            IrReduceLimits bounded = *limits;
            bounded.max_milliseconds = budget;
            bounded.predicate_milliseconds = budget;
            const pid_t child = fork();
            if (child == 0)
            {
                close(descriptors[0]);
                const struct rlimit core = { 0 };
                if (setpgid(0, 0) != 0 || setrlimit(RLIMIT_CORE, &core) != 0)
                {
                    _exit(125);
                }
                const IrReduceObservation observed = predicate(function, &bounded, observer, scratch);
                ssize_t written = -1;
                do
                {
                    written = write(descriptors[1], &observed, sizeof(observed));
                } while (written < 0 && errno == EINTR);
                // Do not flush inherited stdio buffers, run parent cleanup, or publish any child arena view.
                _exit(written == (ssize_t)sizeof(observed) ? 0 : 125);
            }
            close(descriptors[1]);
            descriptors[1] = -1;
            if (child > 0)
            {
                // Either parent or child establishes the group before any timeout can occur.
                (void)setpgid(child, child);
                int status = 0;
                result.status = ir_reduce_process_wait(child, now, budget, &status);
                if (result.status == IR_REDUCE_OBSERVED)
                {
                    result.status = IR_REDUCE_PREDICATE_FAULT;
                    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                    {
                        IrReduceObservation observed = { 0 };
                        ssize_t received = -1;
                        do
                        {
                            received = read(descriptors[0], &observed, sizeof(observed));
                        } while (received < 0 && errno == EINTR);
                        result.status = IR_REDUCE_PREDICATE_ERROR;
                        if (received == (ssize_t)sizeof(observed) &&
                            (unsigned)observed.status <= (unsigned)IR_REDUCE_PREDICATE_ERROR)
                        {
                            result = observed;
                            if (result.status != IR_REDUCE_OBSERVED)
                            {
                                result.failure = (IrReduceFailure){ 0 };
                            }
                        }
                    }
                    else if (WIFEXITED(status) && WEXITSTATUS(status) == 125)
                    {
                        result.status = IR_REDUCE_PREDICATE_ERROR;
                    }
                }
            }
            close(descriptors[0]);
        }
    }
    return result;
}
