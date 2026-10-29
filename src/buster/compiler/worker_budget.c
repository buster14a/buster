// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C driver's external worker ownership (#673). A command owns its implicit calling-thread slot; each additional
// emission lane needs one GNU make jobserver byte, taken without waiting. Authentication is captured at command
// entry; bytes are taken only when useful work would start extra lanes. A private descriptor supplies nonblocking IO.
// A guardian owns the bytes until the parent closes its control pipe, also on signal-driven process exit. The child
// starts from a clean exec in its own session before taking any token, inherits blocked catchable signals, returns
// the exact bytes and sends an acknowledgement before exiting. The driver has one lease at a time, covering only the
// joined worker phase. posix_spawn avoids retaining a copy-on-write snapshot of the live front end during emission.
// GNU make's protocol: https://www.gnu.org/software/make/manual/html_node/POSIX-Jobserver.html

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSTER_WORKER_BUDGET_MAX 64u
// Linux's O_DIRECT flag on a pipe selects packet mode (asm-generic/fcntl.h). The protocol requires byte-stream IO.
#define BUSTER_WORKER_BUDGET_PACKET_FLAG 040000
// Linux F_GETPIPE_SZ, also available when the caller does not enable GNU libc extensions.
#define BUSTER_WORKER_BUDGET_GETPIPE_SIZE 1032

extern char** environ;

typedef enum WorkerBudgetMode
{
    WORKER_BUDGET_NONE,
    WORKER_BUDGET_JOBSERVER,
    WORKER_BUDGET_UNAVAILABLE,
} WorkerBudgetMode;

typedef struct WorkerBudget WorkerBudget;
struct WorkerBudget
{
    uint32_t maximum;
    const char* makeflags;
    uint32_t requested;
    uint32_t granted;
    WorkerBudgetMode mode;
    bool return_failed;
    int read_fd;
    int write_fd;
    bool prepared;
    bool dry_run;
};

typedef struct WorkerBudgetLease WorkerBudgetLease;
struct WorkerBudgetLease
{
    int control;
    int reply;
    pid_t child;
    WorkerBudget* owner;
};

// A '+' recipe is needed for inherited pipe descriptors and also runs under make -n. GNU make's first MAKEFLAGS
// word contains its short flags only when it does not begin with '-'; ignore option words and variable values.
static bool worker_budget_dry_run(const char* flags)
{
    const char* cursor = flags;
    while (cursor != NULL && isspace((unsigned char)*cursor))
    {
        cursor += 1;
    }
    const char* first = cursor;
    bool result = false;
    bool option = cursor != NULL && cursor[0] != '-';
    while (cursor != NULL && *cursor != 0 && !isspace((unsigned char)*cursor))
    {
        option &= *cursor != '=';
        result |= *cursor == 'n';
        cursor += 1;
    }
    return first != cursor && option && result;
}

// GNU make escapes spaces in MAKEFLAGS words with backslashes. Only a complete option word is authentication;
// an auth-looking substring in a variable assignment is not. Later modern or legacy authentication replaces earlier.
static bool worker_budget_auth(const char* flags, char* auth, uint32_t capacity)
{
    bool present = false;
    const char* cursor = flags;
    while (cursor != NULL && *cursor != 0)
    {
        while (isspace((unsigned char)*cursor))
        {
            cursor += 1;
        }
        char word[PATH_MAX + 64];
        uint32_t length = 0;
        bool overflow = false;
        while (*cursor != 0 && !isspace((unsigned char)*cursor))
        {
            cursor += *cursor == '\\' && cursor[1] != 0;
            if (length + 1 < sizeof(word))
            {
                word[length++] = *cursor;
            }
            else
            {
                overflow = true;
            }
            cursor += 1;
        }
        word[length] = 0;
        const char* value = strncmp(word, "--jobserver-auth=", 17) == 0 ? word + 17 :
                            strncmp(word, "--jobserver-fds=", 16) == 0 ? word + 16 : NULL;
        const bool missing = strcmp(word, "--jobserver-auth") == 0 || strcmp(word, "--jobserver-fds") == 0;
        if (value != NULL || missing)
        {
            present = true;
            const uint32_t bytes = value != NULL ? (uint32_t)strlen(value) : 0;
            auth[0] = 0;
            if (!overflow && bytes < capacity)
            {
                memcpy(auth, value != NULL ? value : "", (uint64_t)bytes + 1);
            }
        }
    }
    return present;
}

static bool worker_budget_fd(const char** text, int* fd)
{
    const char* cursor = *text;
    uint32_t value = 0;
    bool result = *cursor >= '0' && *cursor <= '9';
    while (result && *cursor >= '0' && *cursor <= '9')
    {
        const uint32_t digit = (uint32_t)(*cursor - '0');
        result = value <= ((uint32_t)INT_MAX - digit) / 10;
        value = result ? value * 10 + digit : value;
        cursor += 1;
    }
    *text = cursor;
    *fd = (int)value;
    return result;
}

static bool worker_budget_pipe_pair(int read_fd, int write_fd, const struct stat* expected)
{
    struct stat reader;
    struct stat writer;
    const int read_flags = fcntl(read_fd, F_GETFL);
    const int write_flags = fcntl(write_fd, F_GETFL);
    return read_flags >= 0 && write_flags >= 0 && (read_flags & O_ACCMODE) != O_WRONLY &&
           (write_flags & O_ACCMODE) != O_RDONLY && ((read_flags | write_flags) & BUSTER_WORKER_BUDGET_PACKET_FLAG) == 0 &&
           fstat(read_fd, &reader) == 0 && fstat(write_fd, &writer) == 0 && S_ISFIFO(reader.st_mode) &&
           S_ISFIFO(writer.st_mode) && reader.st_dev == writer.st_dev && reader.st_ino == writer.st_ino &&
           (expected == NULL || (reader.st_dev == expected->st_dev && reader.st_ino == expected->st_ino));
}

// Opening /proc/self/fd makes independent open file descriptions for a pipe. dup would share O_NONBLOCK with make.
// A missing procfs, closed/disabled descriptors, packet-mode pipe or unsupported authentication means one worker.
static bool worker_budget_open(const char* auth, int* read_fd, int* write_fd)
{
    *read_fd = -1;
    *write_fd = -1;
    bool valid = false;
    if (strncmp(auth, "fifo:", 5) == 0 && auth[5] != 0)
    {
        *read_fd = open(auth + 5, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        *write_fd = *read_fd >= 0 ? open(auth + 5, O_WRONLY | O_NONBLOCK | O_CLOEXEC) : -1;
        valid = worker_budget_pipe_pair(*read_fd, *write_fd, NULL);
    }
    else
    {
        const char* cursor = auth;
        int inherited_read = -1;
        int inherited_write = -1;
        valid = worker_budget_fd(&cursor, &inherited_read) && *cursor == ',';
        cursor += valid;
        valid = valid && worker_budget_fd(&cursor, &inherited_write) && *cursor == 0 &&
                worker_budget_pipe_pair(inherited_read, inherited_write, NULL);
        struct stat expected;
        valid = valid && fstat(inherited_read, &expected) == 0;
        if (valid)
        {
            char path[64];
            snprintf(path, sizeof(path), "/proc/self/fd/%d", inherited_read);
            *read_fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            snprintf(path, sizeof(path), "/proc/self/fd/%d", inherited_write);
            *write_fd = *read_fd >= 0 ? open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC) : -1;
            valid = worker_budget_pipe_pair(*read_fd, *write_fd, &expected);
        }
    }
    if (!valid)
    {
        if (*read_fd >= 0)
        {
            close(*read_fd);
        }
        if (*write_fd >= 0)
        {
            close(*write_fd);
        }
        *read_fd = -1;
        *write_fd = -1;
    }
    return valid;
}

// Capture at command entry, before compiler IO can reuse descriptor numbers that make deliberately closed for a
// nonrecursive recipe. Later phases use these private descriptions, never the original numbers from MAKEFLAGS.
static __attribute__((unused)) void worker_budget_initialize(WorkerBudget* budget, const char* flags, uint32_t maximum)
{
    *budget = (WorkerBudget){ .maximum = maximum, .makeflags = flags, .read_fd = -1, .write_fd = -1, .prepared = true,
                             .dry_run = worker_budget_dry_run(flags) };
    char auth[PATH_MAX + 1];
    if (!budget->dry_run && worker_budget_auth(flags, auth, sizeof(auth)))
    {
        budget->mode = worker_budget_open(auth, &budget->read_fd, &budget->write_fd) ? WORKER_BUDGET_JOBSERVER : WORKER_BUDGET_UNAVAILABLE;
    }
}

static __attribute__((unused)) void worker_budget_close(WorkerBudget* budget)
{
    if (budget->prepared && budget->read_fd >= 0)
    {
        close(budget->read_fd);
    }
    if (budget->prepared && budget->write_fd >= 0)
    {
        close(budget->write_fd);
    }
    budget->read_fd = -1;
    budget->write_fd = -1;
    budget->mode = budget->mode == WORKER_BUDGET_JOBSERVER ? WORKER_BUDGET_UNAVAILABLE : budget->mode;
}

// Close unrelated inherited descriptors in the clean guardian. In particular another control pipe or a caller's
// captured output must not stay alive through it. Linux's close_range is available on the host.
static bool worker_budget_child_fds(int read_fd, int write_fd, int control, int reply)
{
    uint32_t keep[] = { (uint32_t)read_fd, (uint32_t)write_fd, (uint32_t)control, (uint32_t)reply };
    for (uint32_t i = 1; i < 4; i += 1)
    {
        const uint32_t fd = keep[i];
        uint32_t at = i;
        while (at > 0 && keep[at - 1] > fd)
        {
            keep[at] = keep[at - 1];
            at -= 1;
        }
        keep[at] = fd;
    }
    uint32_t first = 3;
    bool result = true;
    for (uint32_t i = 0; result && i < 4; i += 1)
    {
        if (keep[i] >= first)
        {
            result = keep[i] == first || syscall(SYS_close_range, first, keep[i] - 1, 0) == 0;
            first = keep[i] + 1;
        }
    }
    result = result && syscall(SYS_close_range, first, UINT32_MAX, 0) == 0;
    return result;
}

// A partial read from a full Linux pipe may leave its first page occupied and its last page without append space.
// Merely waiting for POLLOUT would deadlock a build with no other recipe to drain it. Compact the available bytes
// through storage reserved before borrowing: GNU make initializes at most the pipe capacity before starting jobs.
// Additional bytes taken here do no work and are returned with the original lease; every partial write keeps its
// unwritten suffix. A temporary IO failure must retain ownership, never turn an error acknowledgement into lost slots.
static void worker_budget_return(int read_fd, int write_fd, uint8_t* tokens, uint32_t count, uint32_t capacity)
{
    while (count != 0)
    {
        const ssize_t returned = write(write_fd, tokens, count);
        if (returned > 0)
        {
            count -= (uint32_t)returned;
            memmove(tokens, tokens + returned, count);
        }
        else if (returned < 0 && errno == EINTR)
        {
            continue;
        }
        else if (returned < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            bool reading = true;
            bool progress = false;
            while (reading && count < capacity)
            {
                const ssize_t taken = read(read_fd, tokens + count, capacity - count);
                count += taken > 0 ? (uint32_t)taken : 0;
                progress |= taken > 0;
                reading = taken > 0 || (taken < 0 && errno == EINTR);
            }
            if (!progress)
            {
                struct pollfd descriptors[] = {
                    { .fd = read_fd, .events = count < capacity ? POLLIN : 0 },
                    { .fd = write_fd, .events = POLLOUT },
                };
                poll(descriptors, 2, 100);
            }
        }
        else
        {
            // The private validated endpoints remain open. Retain their bytes through a transient resource failure.
            poll(NULL, 0, 100);
        }
    }
}

static void worker_budget_guardian(int read_fd, int write_fd, int control, int reply, uint32_t maximum)
{
    uint32_t count = 0;
    // A runner may kill the compiler's whole process group, even with SIGKILL. Leave it before taking a token;
    // before this succeeds a dead child owns nothing, and afterward parent exit closes the control pipe.
    const bool isolated = syscall(SYS_setsid) >= 0 && worker_budget_child_fds(read_fd, write_fd, control, reply);
    const int pipe_capacity = isolated ? fcntl(read_fd, BUSTER_WORKER_BUDGET_GETPIPE_SIZE) : -1;
    const uint32_t token_capacity = pipe_capacity > 0 ? (uint32_t)pipe_capacity : 0;
    uint8_t* tokens = token_capacity != 0 ? mmap(NULL, token_capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    if (tokens != MAP_FAILED)
    {
        // Fault the recovery pages while no byte is owned, rather than first demanding physical memory on return.
        memset(tokens, 0, token_capacity);
    }
    const bool prepared = tokens != MAP_FAILED;
    bool valid = true;
    bool reading = prepared;
    while (reading && count < maximum)
    {
        // Byte-stream tokens can be acquired in one bounded batch. Packet-mode pipes were rejected before fork.
        const ssize_t taken = read(read_fd, tokens + count, maximum - count);
        count += taken > 0 ? (uint32_t)taken : 0;
        reading = taken > 0 || (taken < 0 && errno == EINTR);
    }
    ssize_t sent;
    do
    {
        sent = write(reply, &count, sizeof(count));
    }
    while (sent < 0 && errno == EINTR);
    if (sent == sizeof(count) && count != 0)
    {
        uint8_t ignored;
        ssize_t finished;
        do
        {
            finished = read(control, &ignored, 1);
        }
        while (finished < 0 && errno == EINTR);
        valid = valid && finished == 0;
    }
    if (count != 0)
    {
        worker_budget_return(read_fd, write_fd, tokens, count, token_capacity);
    }
    if (tokens != MAP_FAILED)
    {
        munmap(tokens, token_capacity);
    }
    const uint8_t acknowledged = valid;
    do
    {
        sent = write(reply, &acknowledged, 1);
    }
    while (sent < 0 && errno == EINTR);
    close(read_fd);
    close(write_fd);
    close(control);
    close(reply);
    _exit(acknowledged ? 0 : 1);
}

// Dispatch before any compiler allocation or MAKEFLAGS dry-run handling. Four private handles survive exec and
// the guardian closes unrelated handles; ordinary commands return -1 without performing any guardian work.
static __attribute__((unused)) int worker_budget_guardian_command(int argc, char** argv)
{
    const bool handled = argc >= 2 && strcmp(argv[1], "--buster-worker-budget-guardian") == 0;
    bool valid = handled && argc == 7;
    int values[5] = { -1, -1, -1, -1, -1 };
    for (uint32_t i = 0; valid && i < 5; i += 1)
    {
        const char* cursor = argv[i + 2];
        valid = worker_budget_fd(&cursor, values + i) && *cursor == 0;
    }
    valid = valid && values[4] > 0 && (uint32_t)values[4] < BUSTER_WORKER_BUDGET_MAX;
    for (uint32_t i = 0; valid && i < 4; i += 1)
    {
        valid = fcntl(values[i], F_GETFD) >= 0;
        for (uint32_t j = 0; valid && j < i; j += 1)
        {
            valid = values[i] != values[j];
        }
    }
    if (valid)
    {
        sigset_t blocked;
        sigfillset(&blocked);
        valid = sigprocmask(SIG_BLOCK, &blocked, NULL) == 0;
    }
    if (valid)
    {
        worker_budget_guardian(values[0], values[1], values[2], values[3], (uint32_t)values[4]);
    }
    return handled ? 1 : -1;
}

static pid_t worker_budget_spawn(int read_fd, int write_fd, int control, int reply, uint32_t maximum)
{
    const int descriptors[] = { read_fd, write_fd, control, reply };
    char numbers[5][16];
    char* arguments[] = { "/proc/self/exe", "--buster-worker-budget-guardian", numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], NULL };
    for (uint32_t i = 0; i < 5; i += 1)
    {
        snprintf(numbers[i], sizeof(numbers[i]), "%u", i < 4 ? (uint32_t)descriptors[i] : maximum);
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    const bool actions_ready = posix_spawn_file_actions_init(&actions) == 0;
    const bool attributes_ready = posix_spawnattr_init(&attributes) == 0;
    bool valid = actions_ready && attributes_ready;
    sigset_t blocked;
    sigfillset(&blocked);
    valid = valid && posix_spawnattr_setsigmask(&attributes, &blocked) == 0 &&
            posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK) == 0;
    for (uint32_t i = 0; valid && i < 4; i += 1)
    {
        // POSIX's same-descriptor dup action clears CLOEXEC in the child alone, unlike changing the parent's FD.
        valid = posix_spawn_file_actions_adddup2(&actions, descriptors[i], descriptors[i]) == 0;
    }
    pid_t child = -1;
    if (valid)
    {
        valid = posix_spawn(&child, arguments[0], &actions, &attributes, arguments, environ) == 0;
    }
    if (actions_ready)
    {
        posix_spawn_file_actions_destroy(&actions);
    }
    if (attributes_ready)
    {
        posix_spawnattr_destroy(&attributes);
    }
    return valid ? child : -1;
}

static __attribute__((unused)) bool worker_budget_release(WorkerBudgetLease* lease)
{
    bool result = true;
    if (lease->child > 0)
    {
        close(lease->control);
        uint8_t acknowledged = 0;
        ssize_t got;
        do
        {
            got = read(lease->reply, &acknowledged, 1);
        }
        while (got < 0 && errno == EINTR);
        close(lease->reply);
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(lease->child, &status, 0);
        }
        while (waited < 0 && errno == EINTR);
        result = got == 1 && acknowledged == 1 &&
                 ((waited == lease->child && WIFEXITED(status) && WEXITSTATUS(status) == 0) || (waited < 0 && errno == ECHILD));
        if (lease->owner != NULL)
        {
            lease->owner->return_failed |= !result;
        }
    }
    *lease = (WorkerBudgetLease){ .control = -1, .reply = -1, .child = -1 };
    return result;
}

static __attribute__((unused)) uint32_t worker_budget_acquire(WorkerBudget* budget, uint32_t requested, WorkerBudgetLease* lease)
{
    if (!budget->prepared)
    {
        worker_budget_initialize(budget, budget->makeflags, budget->maximum);
    }
    *lease = (WorkerBudgetLease){ .control = -1, .reply = -1, .child = -1, .owner = budget };
    uint32_t maximum = budget->maximum != 0 ? budget->maximum : 1;
    maximum = maximum < BUSTER_WORKER_BUDGET_MAX ? maximum : BUSTER_WORKER_BUDGET_MAX;
    uint32_t result = requested < maximum ? requested : maximum;
    result = result != 0 ? result : 1;
    result = budget->dry_run ? 1 : result;
    budget->requested = requested;
    budget->return_failed = false;
    if (result > 1 && budget->mode != WORKER_BUDGET_NONE)
    {
        const uint32_t wanted = result - 1;
        result = 1;
        const int read_fd = budget->read_fd;
        const int write_fd = budget->write_fd;
        if (budget->mode == WORKER_BUDGET_JOBSERVER)
        {
            budget->mode = WORKER_BUDGET_JOBSERVER;
            int available = 0;
            const bool supplied = ioctl(read_fd, FIONREAD, &available) == 0 && available > 0;
            int control[2] = { -1, -1 };
            int reply[2] = { -1, -1 };
            const bool pipes = supplied && syscall(SYS_pipe2, control, O_CLOEXEC) == 0 && syscall(SYS_pipe2, reply, O_CLOEXEC) == 0;
            const pid_t child = pipes ? worker_budget_spawn(read_fd, write_fd, control[0], reply[1], wanted) : -1;
            if (control[0] >= 0)
            {
                close(control[0]);
            }
            if (reply[1] >= 0)
            {
                close(reply[1]);
            }
            if (child > 0)
            {
                *lease = (WorkerBudgetLease){ .control = control[1], .reply = reply[0], .child = child, .owner = budget };
                uint32_t granted = 0;
                uint32_t count = 0;
                bool reading = true;
                while (reading && count < sizeof(granted))
                {
                    const ssize_t got = read(reply[0], (uint8_t*)&granted + count, sizeof(granted) - count);
                    count += got > 0 ? (uint32_t)got : 0;
                    reading = got > 0 || (got < 0 && errno == EINTR);
                }
                if (count == sizeof(granted) && granted <= wanted)
                {
                    result += granted;
                }
                else
                {
                    budget->mode = WORKER_BUDGET_UNAVAILABLE;
                    worker_budget_release(lease);
                }
            }
            else
            {
                if (control[1] >= 0)
                {
                    close(control[1]);
                }
                if (reply[0] >= 0)
                {
                    close(reply[0]);
                }
            }
        }
    }
    budget->granted = result;
    return result;
}
