// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Worker ownership protocol tests. Synthetic pipe/FIFO contents are the independent oracle: after a lease, every
// original byte must be present once, whatever its value or return order, and no implicit caller token may appear.
// Resource and termination cases run in isolated children so their limits, masks and process groups cannot escape.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct WorkerBudgetTest WorkerBudgetTest;
struct WorkerBudgetTest
{
    uint32_t checks;
    uint32_t failures;
};

static int worker_budget_test_pipe_open(int fds[2], int flags)
{
    return (int)syscall(SYS_pipe2, fds, flags);
}

static void worker_budget_test_expect(WorkerBudgetTest* test, bool valid, const char* what)
{
    test->checks += 1;
    if (!valid)
    {
        test->failures += 1;
        fprintf(stderr, "worker budget: %s\n", what);
    }
}

static uint64_t worker_budget_test_now(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static bool worker_budget_test_read(int fd, uint8_t* bytes, uint32_t count)
{
    uint32_t read_bytes = 0;
    const uint64_t deadline = worker_budget_test_now() + UINT64_C(2000000000);
    bool valid = true;
    while (valid && read_bytes < count)
    {
        const uint64_t now = worker_budget_test_now();
        struct pollfd descriptor = { .fd = fd, .events = POLLIN };
        const int remaining = now < deadline ? (int)((deadline - now + 999999) / 1000000) : 0;
        const int ready = remaining != 0 ? poll(&descriptor, 1, remaining) : 0;
        if (ready < 0 && errno == EINTR)
        {
            continue;
        }
        valid = ready > 0 && (descriptor.revents & POLLIN) != 0;
        if (valid)
        {
            const ssize_t got = read(fd, bytes + read_bytes, count - read_bytes);
            valid = got > 0 || (got < 0 && errno == EINTR);
            read_bytes += got > 0 ? (uint32_t)got : 0;
        }
    }
    return valid && read_bytes == count;
}

static int worker_budget_test_available(int fd)
{
    int bytes = -1;
    const bool valid = ioctl(fd, FIONREAD, &bytes) == 0;
    return valid ? bytes : -1;
}

static bool worker_budget_test_tokens(int fd, const uint8_t* expected, uint32_t count)
{
    uint8_t received[64];
    uint32_t wanted[256] = { 0 };
    uint32_t actual[256] = { 0 };
    bool valid = count <= sizeof(received) && worker_budget_test_read(fd, received, count);
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        wanted[expected[i]] += 1;
        actual[received[i]] += 1;
    }
    valid = valid && memcmp(wanted, actual, sizeof(wanted)) == 0 && worker_budget_test_available(fd) == 0;
    return valid;
}

static bool worker_budget_test_wait(pid_t child, int expected_signal)
{
    int status = 0;
    pid_t waited = -1;
    const uint64_t deadline = worker_budget_test_now() + UINT64_C(2000000000);
    while (child > 0 && worker_budget_test_now() < deadline)
    {
        waited = waitpid(child, &status, WNOHANG);
        if (waited == child || (waited < 0 && errno != EINTR))
        {
            break;
        }
        const struct timespec pause_time = { .tv_nsec = 1000000 };
        nanosleep(&pause_time, NULL);
    }
    const bool completed = waited == child;
    if (child > 0 && !completed && waited >= 0)
    {
        kill(child, SIGKILL);
        do
        {
            waited = waitpid(child, &status, 0);
        }
        while (waited < 0 && errno == EINTR);
    }
    return completed && (expected_signal != 0 ? WIFSIGNALED(status) && WTERMSIG(status) == expected_signal :
                                              WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void worker_budget_test_pipe(WorkerBudgetTest* test, uint32_t supplied, uint32_t requested, uint32_t maximum, bool nonblocking)
{
    static const uint8_t tokens[] = { 0, '+', 0xfe, 'A', '\n', 0x81, 'Q', 0 };
    int fds[2] = { -1, -1 };
    const bool opened = worker_budget_test_pipe_open(fds, O_CLOEXEC | (nonblocking ? O_NONBLOCK : 0)) == 0;
    const bool ready = opened && supplied <= sizeof(tokens) && (supplied == 0 || write(fds[1], tokens, supplied) == supplied);
    worker_budget_test_expect(test, ready, "synthetic pipe is available");
    if (ready)
    {
        char flags[128];
        snprintf(flags, sizeof(flags), " -j --jobserver-auth=%d,%d", fds[0], fds[1]);
        WorkerBudget budget;
        worker_budget_initialize(&budget, flags, maximum);
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const int read_flags = fcntl(fds[0], F_GETFL);
        const int write_flags = fcntl(fds[1], F_GETFL);
        uint32_t cap = maximum != 0 ? maximum : 1;
        cap = requested < cap ? requested : cap;
        const uint32_t expected = cap > 1 && supplied < cap - 1 ? supplied + 1 : cap;
        const uint32_t granted = worker_budget_acquire(&budget, requested, &lease);
        worker_budget_test_expect(test, granted == expected && budget.granted == expected, "pipe grant is bounded by available slots and explicit cap");
        worker_budget_test_expect(test, fcntl(fds[0], F_GETFL) == read_flags && fcntl(fds[1], F_GETFL) == write_flags,
                                  "leasing never changes the inherited open-file flags");
        worker_budget_test_expect(test, worker_budget_test_available(fds[0]) == (int)(supplied - (expected - 1)),
                                  "exactly one token is owned per extra granted worker");
        worker_budget_test_expect(test, worker_budget_release(&lease), "a normal pipe lease returns its exact tokens");
        worker_budget_test_expect(test, worker_budget_release(&lease), "a second release returns no extra tokens");
        worker_budget_test_expect(test, worker_budget_test_tokens(fds[0], tokens, supplied), "all token byte values return once, with no implicit token");
        worker_budget_test_expect(test, fcntl(fds[0], F_GETFL) == read_flags && fcntl(fds[1], F_GETFL) == write_flags,
                                  "release preserves inherited descriptor flags");
        worker_budget_close(&budget);
    }
    if (fds[0] >= 0)
    {
        close(fds[0]);
    }
    if (fds[1] >= 0)
    {
        close(fds[1]);
    }
}

static void worker_budget_test_flags(WorkerBudgetTest* test)
{
    const char* absent[] = { NULL, "", "w -j8", " VAR=--jobserver-auth=3,4", " -j -- OTHER=--jobserver-auth=fifo:/not-a-server" };
    for (uint32_t f = 0; f < sizeof(absent) / sizeof(absent[0]); f += 1)
    {
        WorkerBudget budget;
        worker_budget_initialize(&budget, absent[f], 5);
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const uint32_t granted = worker_budget_acquire(&budget, 8, &lease);
        worker_budget_test_expect(test, granted == 5 && budget.mode == WORKER_BUDGET_NONE && worker_budget_release(&lease),
                                  "non-jobserver words retain the explicit local worker cap");
        worker_budget_close(&budget);
    }
    const char* invalid[] = {
        " --jobserver-auth=",
        " --jobserver-auth=unknown",
        " --jobserver-auth=-1,-1",
        " --jobserver-auth=-1,2",
        " --jobserver-auth=3,",
        " --jobserver-auth=,4",
        " --jobserver-auth=999999999999999999999,4",
        " --jobserver-auth=3,4trailing",
        " --jobserver-auth=fifo:",
        " --jobserver-auth=fifo:/buster-worker-budget-does-not-exist",
        " --jobserver-auth=999998,999999",
    };
    for (uint32_t f = 0; f < sizeof(invalid) / sizeof(invalid[0]); f += 1)
    {
        WorkerBudget budget;
        worker_budget_initialize(&budget, invalid[f], 8);
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const uint32_t granted = worker_budget_acquire(&budget, 8, &lease);
        worker_budget_test_expect(test, granted == 1 && budget.mode == WORKER_BUDGET_UNAVAILABLE && worker_budget_release(&lease),
                                  "disabled, malformed, unknown or inaccessible servers conservatively grant one worker");
        worker_budget_close(&budget);
    }
    int first[2] = { -1, -1 };
    int second[2] = { -1, -1 };
    const uint8_t tokens[] = { 'x', 0, 'z' };
    const bool ready = worker_budget_test_pipe_open(first, O_CLOEXEC) == 0 && worker_budget_test_pipe_open(second, O_CLOEXEC) == 0 && write(first[1], tokens, sizeof(tokens)) == sizeof(tokens);
    worker_budget_test_expect(test, ready, "parser precedence pipes are available");
    if (ready)
    {
        char flags[256];
        for (uint32_t form = 0; form < 8; form += 1)
        {
            if (form == 0)
            {
                snprintf(flags, sizeof(flags), " --jobserver-auth=-1,-1 --jobserver-auth=%d,%d", first[0], first[1]);
            }
            else if (form == 1)
            {
                snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d --jobserver-auth=-1,-1", first[0], first[1]);
            }
            else if (form == 2)
            {
                snprintf(flags, sizeof(flags), " --jobserver-fds=%d,%d", first[0], first[1]);
            }
            else if (form == 3)
            {
                snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d", first[0], second[1]);
            }
            else if (form == 4)
            {
                snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d", first[1], first[0]);
            }
            else if (form == 5)
            {
                snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%dtrailing", first[0], first[1]);
            }
            else if (form == 6)
            {
                snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d --jobserver-fds=-1,-1", first[0], first[1]);
            }
            else
            {
                snprintf(flags, sizeof(flags), " --jobserver-fds=-1,-1 --jobserver-auth=%d,%d", first[0], first[1]);
            }
            const bool available = form == 0 || form == 2 || form == 7;
            WorkerBudget budget;
            worker_budget_initialize(&budget, flags, 8);
            WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
            const uint32_t granted = worker_budget_acquire(&budget, 8, &lease);
            worker_budget_test_expect(test, granted == (available ? 4u : 1u) &&
                                           budget.mode == (available ? WORKER_BUDGET_JOBSERVER : WORKER_BUDGET_UNAVAILABLE),
                                      "the final modern/legacy auth wins; mismatched or reversed pipe ends are refused");
            worker_budget_test_expect(test, worker_budget_release(&lease) && worker_budget_test_tokens(first[0], tokens, sizeof(tokens)) &&
                                           worker_budget_test_available(second[0]) == 0,
                                      "auth parsing never consumes or returns credits to another pipe");
            worker_budget_test_expect(test, write(first[1], tokens, sizeof(tokens)) == sizeof(tokens), "precedence pipe is reset");
            worker_budget_close(&budget);
        }
    }
    for (uint32_t end = 0; end < 2; end += 1)
    {
        if (first[end] >= 0)
        {
            close(first[end]);
        }
        if (second[end] >= 0)
        {
            close(second[end]);
        }
    }
    FILE* regular = tmpfile();
    if (regular != NULL)
    {
        const uint8_t bytes[] = { 'a', 'b', 'c' };
        const bool written = fwrite(bytes, 1, sizeof(bytes), regular) == sizeof(bytes) && fflush(regular) == 0 && fseek(regular, 0, SEEK_SET) == 0;
        char flags[128];
        snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d", fileno(regular), fileno(regular));
        WorkerBudget budget;
        worker_budget_initialize(&budget, flags, 8);
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        worker_budget_test_expect(test, written && worker_budget_acquire(&budget, 8, &lease) == 1 && budget.mode == WORKER_BUDGET_UNAVAILABLE &&
                                       worker_budget_release(&lease), "a regular file cannot supply worker credits");
        worker_budget_close(&budget);
        fclose(regular);
    }
    else
    {
        worker_budget_test_expect(test, false, "regular-file refusal fixture is available");
    }
    int packet[2] = { -1, -1 };
    if (worker_budget_test_pipe_open(packet, O_CLOEXEC | BUSTER_WORKER_BUDGET_PACKET_FLAG) == 0)
    {
        char flags[128];
        snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d", packet[0], packet[1]);
        WorkerBudget budget;
        worker_budget_initialize(&budget, flags, 8);
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const bool written = write(packet[1], tokens, sizeof(tokens)) == sizeof(tokens);
        worker_budget_test_expect(test, written && worker_budget_acquire(&budget, 8, &lease) == 1 && budget.mode == WORKER_BUDGET_UNAVAILABLE &&
                                       worker_budget_release(&lease) && worker_budget_test_tokens(packet[0], tokens, sizeof(tokens)),
                                  "packet-mode pipes are refused without discarding any packet bytes");
        worker_budget_close(&budget);
        close(packet[0]);
        close(packet[1]);
    }
}

static void worker_budget_test_fifo(WorkerBudgetTest* test)
{
    char directory[] = "/tmp/buster-worker-budget-XXXXXX";
    const bool made = mkdtemp(directory) != NULL;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/space server", directory);
    const bool created = made && mkfifo(path, 0600) == 0;
    int read_end = created ? open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    int write_end = read_end >= 0 ? open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    const uint8_t tokens[] = { 'f', 0, 'o' };
    const bool ready = write_end >= 0 && write(write_end, tokens, sizeof(tokens)) == sizeof(tokens);
    worker_budget_test_expect(test, ready, "named FIFO with a space is available");
    if (ready)
    {
        char flags[PATH_MAX + 64];
        snprintf(flags, sizeof(flags), " --jobserver-auth=fifo:%s/space\\ server", directory);
        WorkerBudget budget;
        worker_budget_initialize(&budget, flags, 8);
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const uint32_t granted = worker_budget_acquire(&budget, 8, &lease);
        worker_budget_test_expect(test, granted == 4 && budget.mode == WORKER_BUDGET_JOBSERVER && worker_budget_release(&lease) &&
                                       worker_budget_test_tokens(read_end, tokens, sizeof(tokens)), "escaped FIFO paths retain all exact credit bytes");
        worker_budget_close(&budget);
    }
    if (write_end >= 0)
    {
        close(write_end);
    }
    if (read_end >= 0)
    {
        close(read_end);
    }
    if (created)
    {
        unlink(path);
    }
    if (made)
    {
        rmdir(directory);
    }
}

static void worker_budget_test_limits(WorkerBudgetTest* test)
{
    const uint8_t tokens[] = { 'l', 'i', 'm' };
    for (uint32_t kind = 0; kind < 2; kind += 1)
    {
        if (kind == 1 && geteuid() == 0)
        {
            continue;
        }
        int fds[2] = { -1, -1 };
        const bool ready = worker_budget_test_pipe_open(fds, O_CLOEXEC) == 0 && write(fds[1], tokens, sizeof(tokens)) == sizeof(tokens);
        worker_budget_test_expect(test, ready, "resource-failure pipe is available");
        pid_t child = ready ? fork() : -1;
        if (child == 0)
        {
            char flags[128];
            snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d", fds[0], fds[1]);
            struct rlimit limit;
            const int resource = kind == 0 ? RLIMIT_NOFILE : RLIMIT_NPROC;
            bool valid = getrlimit(resource, &limit) == 0;
            limit.rlim_cur = kind == 0 ? 3 : 0;
            valid = valid && setrlimit(resource, &limit) == 0;
            WorkerBudget budget;
            worker_budget_initialize(&budget, flags, 8);
            WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
            valid = valid && worker_budget_acquire(&budget, 8, &lease) == 1 && worker_budget_release(&lease);
            worker_budget_close(&budget);
            _exit(valid ? 0 : 1);
        }
        worker_budget_test_expect(test, child > 0 && worker_budget_test_wait(child, 0) && worker_budget_test_tokens(fds[0], tokens, sizeof(tokens)),
                                  "descriptor or guardian-fork failure leaves every worker credit in the server");
        if (fds[0] >= 0)
        {
            close(fds[0]);
        }
        if (fds[1] >= 0)
        {
            close(fds[1]);
        }
    }
}

static void worker_budget_test_termination_one(WorkerBudgetTest* test, int termination)
{
    const uint8_t tokens[] = { 's', 0, 'g' };
    int fds[2] = { -1, -1 };
    int ready_pipe[2] = { -1, -1 };
    const bool ready = worker_budget_test_pipe_open(fds, O_CLOEXEC) == 0 && worker_budget_test_pipe_open(ready_pipe, O_CLOEXEC) == 0 && write(fds[1], tokens, sizeof(tokens)) == sizeof(tokens);
    worker_budget_test_expect(test, ready, "termination test pipes are available");
    const pid_t child = ready ? fork() : -1;
    if (child == 0)
    {
        close(ready_pipe[0]);
        struct sigaction action = { .sa_handler = SIG_DFL };
        sigemptyset(&action.sa_mask);
        sigset_t signals;
        sigemptyset(&signals);
        sigaddset(&signals, SIGTERM);
        const bool isolated = setsid() == getpid() && sigaction(SIGTERM, &action, NULL) == 0 && sigprocmask(SIG_UNBLOCK, &signals, NULL) == 0;
        char flags[128];
        snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d", fds[0], fds[1]);
        WorkerBudget budget;
        worker_budget_initialize(&budget, flags, 8);
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const uint8_t granted = isolated ? (uint8_t)worker_budget_acquire(&budget, 8, &lease) : 0;
        const bool reported = write(ready_pipe[1], &granted, 1) == 1;
        close(ready_pipe[1]);
        while (reported && granted == 4)
        {
            pause();
        }
        worker_budget_release(&lease);
        worker_budget_close(&budget);
        _exit(1);
    }
    if (ready_pipe[1] >= 0)
    {
        close(ready_pipe[1]);
    }
    uint8_t granted = 0;
    const bool acquired = child > 0 && worker_budget_test_read(ready_pipe[0], &granted, 1) && granted == 4;
    worker_budget_test_expect(test, acquired, "isolated compiler acquired credits before termination");
    if (child > 0)
    {
        // A successful readiness byte proves that the child made its own session. Its group contains only this child
        // and its guardian; the test runner and other agent processes are outside it.
        const int signalled = kill(acquired ? -child : child, acquired ? termination : SIGKILL);
        const bool waited = worker_budget_test_wait(child, acquired ? termination : SIGKILL);
        worker_budget_test_expect(test, signalled == 0 && waited, "termination keeps the compiler's ordinary signal behavior");
    }
    worker_budget_test_expect(test, ready && worker_budget_test_tokens(fds[0], tokens, sizeof(tokens)),
                              "a process-group termination returns every extra credit through guardian EOF");
    if (ready_pipe[0] >= 0)
    {
        close(ready_pipe[0]);
    }
    if (fds[0] >= 0)
    {
        close(fds[0]);
    }
    if (fds[1] >= 0)
    {
        close(fds[1]);
    }
}

static void worker_budget_test_child_modes(WorkerBudgetTest* test)
{
    const uint8_t tokens[] = { 'c', 0, 'h' };
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        int fds[2] = { -1, -1 };
        const bool ready = worker_budget_test_pipe_open(fds, O_CLOEXEC) == 0 && write(fds[1], tokens, sizeof(tokens)) == sizeof(tokens);
        worker_budget_test_expect(test, ready, "descriptor-zero and ignored-child test pipes are available");
        const pid_t child = ready ? fork() : -1;
        if (child == 0)
        {
            struct sigaction action = { .sa_handler = SIG_IGN };
            sigemptyset(&action.sa_mask);
            bool valid = mode == 0 ? dup2(fds[0], STDIN_FILENO) == STDIN_FILENO : sigaction(SIGCHLD, &action, NULL) == 0;
            char flags[128];
            snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d", mode == 0 ? STDIN_FILENO : fds[0], fds[1]);
            WorkerBudget budget;
            worker_budget_initialize(&budget, flags, 8);
            WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
            valid = valid && worker_budget_acquire(&budget, 8, &lease) == 4 && worker_budget_release(&lease);
            worker_budget_close(&budget);
            _exit(valid ? 0 : 1);
        }
        worker_budget_test_expect(test, child > 0 && worker_budget_test_wait(child, 0) && worker_budget_test_tokens(fds[0], tokens, sizeof(tokens)),
                                  "descriptor zero works; an ignored SIGCHLD still receives the exact token-return ACK");
        if (fds[0] >= 0)
        {
            close(fds[0]);
        }
        if (fds[1] >= 0)
        {
            close(fds[1]);
        }
    }
}

// Raw MAKEFLAGS numbers describe inherited descriptors at command entry, not whichever compiler IPC happens to
// reuse those numbers later. Check both an unavailable entry capture and a valid private capture across FD reuse.
static void worker_budget_test_reused_descriptors(WorkerBudgetTest* test)
{
    const uint8_t credits[] = { 'b', 0, 'd' };
    const uint8_t unrelated[] = { 'i', 'p', 'c' };
    for (uint32_t captured = 0; captured < 2; captured += 1)
    {
        int original[2] = { -1, -1 };
        int replacement[2] = { -1, -1 };
        const bool opened = worker_budget_test_pipe_open(original, O_CLOEXEC) == 0;
        bool ready = opened && write(original[1], credits, sizeof(credits)) == sizeof(credits);
        char flags[128];
        snprintf(flags, sizeof(flags), " --jobserver-auth=%d,%d", original[0], original[1]);
        const int saved = ready && captured ? dup(original[0]) : -1;
        ready = ready && (!captured || saved >= 0);
        WorkerBudget budget;
        if (captured)
        {
            worker_budget_initialize(&budget, flags, 8);
        }
        if (original[0] >= 0)
        {
            close(original[0]);
        }
        if (original[1] >= 0)
        {
            close(original[1]);
        }
        if (!captured)
        {
            worker_budget_initialize(&budget, flags, 8);
        }
        ready = worker_budget_test_pipe_open(replacement, O_CLOEXEC) == 0 && ready;
        ready = ready && replacement[0] == original[0] && replacement[1] == original[1] &&
                write(replacement[1], unrelated, sizeof(unrelated)) == sizeof(unrelated);
        worker_budget_test_expect(test, ready, "unrelated IPC reuses the original authentication descriptor numbers");
        if (ready)
        {
            WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
            const uint32_t granted = worker_budget_acquire(&budget, 8, &lease);
            worker_budget_test_expect(test, granted == (captured ? 4u : 1u) &&
                                           budget.mode == (captured ? WORKER_BUDGET_JOBSERVER : WORKER_BUDGET_UNAVAILABLE) &&
                                           worker_budget_release(&lease), "the entry capture survives descriptor reuse without reinterpreting IPC as a server");
        }
        worker_budget_close(&budget);
        worker_budget_test_expect(test, ready && worker_budget_test_tokens(replacement[0], unrelated, sizeof(unrelated)) &&
                                       (!captured || worker_budget_test_tokens(saved, credits, sizeof(credits))),
                                  "compiler IPC is untouched and a retained original server receives exactly its credits");
        if (saved >= 0)
        {
            close(saved);
        }
        for (uint32_t end = 0; end < 2; end += 1)
        {
            if (replacement[end] >= 0)
            {
                close(replacement[end]);
            }
        }
    }
}

static void worker_budget_test_dry_run(WorkerBudgetTest* test)
{
    const uint8_t tokens[] = { 'd', 0, 'y' };
    int fds[2] = { -1, -1 };
    const bool ready = worker_budget_test_pipe_open(fds, O_CLOEXEC) == 0 && write(fds[1], tokens, sizeof(tokens)) == sizeof(tokens);
    worker_budget_test_expect(test, ready, "dry-run credit pipe is available");
    for (uint32_t form = 0; ready && form < 5; form += 1)
    {
        char flags[128];
        const char* prefix = form == 0 ? "n" : form == 1 ? "-kn" : form == 2 ? "w VAR=n" : "w n";
        snprintf(flags, sizeof(flags), "%s --jobserver-auth=%d,%d", prefix, fds[0], fds[1]);
        if (form == 4)
        {
            snprintf(flags, sizeof(flags), "--jobserver-auth=n");
        }
        WorkerBudget budget;
        worker_budget_initialize(&budget, flags, 8);
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const bool dry = form < 2;
        const uint32_t granted = worker_budget_acquire(&budget, 8, &lease);
        worker_budget_test_expect(test, budget.dry_run == dry && granted == (dry || form == 4 ? 1u : 4u) &&
                                       worker_budget_release(&lease), "only the first short-flags word's n requests dry-run behavior");
        worker_budget_close(&budget);
        worker_budget_test_expect(test, worker_budget_test_tokens(fds[0], tokens, sizeof(tokens)), "dry-run and ordinary flag words preserve every credit");
        worker_budget_test_expect(test, write(fds[1], tokens, sizeof(tokens)) == sizeof(tokens), "dry-run credit pipe is reset");
    }
    if (fds[0] >= 0)
    {
        close(fds[0]);
    }
    if (fds[1] >= 0)
    {
        close(fds[1]);
    }
}

static bool worker_budget_test(void)
{
    WorkerBudgetTest test = { 0 };
    struct sigaction before = { 0 };
    struct sigaction after = { 0 };
    sigset_t before_mask;
    sigset_t after_mask;
    sigaction(SIGTERM, NULL, &before);
    sigprocmask(SIG_SETMASK, NULL, &before_mask);
    for (uint32_t supplied = 0; supplied <= 8; supplied += 1)
    {
        worker_budget_test_pipe(&test, supplied, 8, 8, false);
    }
    worker_budget_test_pipe(&test, 8, 8, 3, true);
    worker_budget_test_pipe(&test, 3, 8, 0, false);
    worker_budget_test_pipe(&test, 3, 1, 8, false);
    worker_budget_test_flags(&test);
    worker_budget_test_fifo(&test);
    worker_budget_test_limits(&test);
    worker_budget_test_termination_one(&test, SIGTERM);
    worker_budget_test_termination_one(&test, SIGKILL);
    worker_budget_test_child_modes(&test);
    worker_budget_test_reused_descriptors(&test);
    worker_budget_test_dry_run(&test);
    sigaction(SIGTERM, NULL, &after);
    sigprocmask(SIG_SETMASK, NULL, &after_mask);
    bool same_mask = true;
    for (int signal = 1; signal < NSIG; signal += 1)
    {
        same_mask = same_mask && sigismember(&before_mask, signal) == sigismember(&after_mask, signal);
    }
    worker_budget_test_expect(&test, before.sa_handler == after.sa_handler && (before.sa_flags & SA_SIGINFO) == (after.sa_flags & SA_SIGINFO),
                              "worker ownership preserves the caller's signal disposition");
    worker_budget_test_expect(&test, same_mask, "worker ownership restores the caller's exact signal mask");
    printf("worker budget: %u checks, %u failures\n", test.checks, test.failures);
    return test.failures == 0;
}
