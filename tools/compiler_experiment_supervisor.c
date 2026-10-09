// Linux-only, single-worker child supervision for disabled compiler experiments.
// Included after build-driver OS helpers. Entry: *_begin / *_end; callers begin
// before any experiment child and end only after their normal OS manager wait
// has returned and released its exact-PID reservation. No group is signalled.
// Map: *_children reads a complete bounded kernel child list; *_owned uses
// WNOWAIT while single-thread/default-SIGCHLD guarantees forbid a competing reap;
// *_self_test proves adoption of descendants in independent private groups.
// Any uncertain cleanup retains the subreaper and blocks further admission.

#if BUSTER_LINUX && !BUSTER_ANDROID
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define BUSTER_EXPERIMENT_SUPERVISOR_MAX_CHILDREN 4096u
#define BUSTER_EXPERIMENT_SUPERVISOR_MAX_CHILD_BYTES 65536u
#define BUSTER_EXPERIMENT_SUPERVISOR_CLEANUP_US 30000000ull

typedef struct CompilerExperimentSupervisor CompilerExperimentSupervisor;
struct CompilerExperimentSupervisor
{
    String8 children_path;
    char8* child_bytes;
    u64* child_pids;
    u64 owner_pid;
    u64 child_count;
    u64 waves;
    u64 signalled;
    u64 reaped;
    s32 prior_subreaper;
    bool active;
    bool cleanup_failed;
};

// Complete parsing precedes any signalling; no partial/truncated list is used.
BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_parse_children(String8 text, u64 owner, u64* pids, u64* count)
{
    bool result = (!text.length || text.pointer) && text.length <= BUSTER_EXPERIMENT_SUPERVISOR_MAX_CHILD_BYTES;
    u64 found = 0;
    for (u64 i = 0; result && i < text.length;)
    {
        while (i < text.length && (text.pointer[i] == ' ' || text.pointer[i] == '\t' || text.pointer[i] == '\n')) i += 1;
        if (i < text.length)
        {
            bool canonical = text.pointer[i] >= '1' && text.pointer[i] <= '9';
            u64 pid = 0, digits = 0;
            while (result && i < text.length && text.pointer[i] >= '0' && text.pointer[i] <= '9')
            {
                u64 digit = (u64)(text.pointer[i] - '0');
                result = digits < 10 && pid <= (2147483647ull - digit) / 10;
                if (result) pid = pid * 10 + digit;
                digits += 1;
                i += 1;
            }
            result = result && canonical && digits && pid > 1 && pid != owner &&
                found < BUSTER_EXPERIMENT_SUPERVISOR_MAX_CHILDREN &&
                (i == text.length || text.pointer[i] == ' ' || text.pointer[i] == '\t' || text.pointer[i] == '\n');
            for (u64 previous = 0; result && previous < found; previous += 1) result = pids[previous] != pid;
            if (result) pids[found++] = pid;
        }
    }
    if (result) *count = found;
    return result;
}

#if BUSTER_LINUX && !BUSTER_ANDROID
BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_single_thread(u64 owner)
{
    bool result = (u64)getpid() == owner && owner > 1 && os_is_only_live_thread();
    DIR* directory = result ? opendir("/proc/self/task") : 0;
    result = result && directory != 0;
    u64 count = 0;
    if (directory)
    {
        bool done = false;
        while (result && !done)
        {
            errno = 0;
            struct dirent* entry = readdir(directory);
            if (!entry)
            {
                result = errno == 0;
                done = true;
            }
            else if (!(entry->d_name[0] == '.' && (!entry->d_name[1] || (entry->d_name[1] == '.' && !entry->d_name[2]))))
            {
                u64 value = 0, length = 0;
                for (; entry->d_name[length] && result; length += 1)
                {
                    unsigned char byte = (unsigned char)entry->d_name[length];
                    result = byte >= '0' && byte <= '9' && length < 10;
                    if (result) value = value * 10 + (u64)(byte - '0');
                }
                count += 1;
                result = result && length && value == owner && count == 1;
            }
        }
        result = closedir(directory) == 0 && result && count == 1;
    }
    struct sigaction action = {0};
    result = result && sigaction(SIGCHLD, 0, &action) == 0 && action.sa_handler == SIG_DFL &&
        !(action.sa_flags & SA_NOCLDWAIT);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_children(CompilerExperimentSupervisor* state, u64 deadline)
{
    bool result = state->owner_pid == (u64)getpid() && state->child_bytes && state->child_pids;
    int descriptor = result ? open((char const*)state->children_path.pointer, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    result = result && descriptor >= 0;
    u64 used = 0;
    bool eof = false;
    while (result && !eof)
    {
        result = os_now_microseconds() < deadline;
        if (result)
        {
            ssize_t got = read(descriptor, state->child_bytes + used,
                BUSTER_EXPERIMENT_SUPERVISOR_MAX_CHILD_BYTES + 1 - used);
            if (got > 0)
            {
                used += (u64)got;
                result = used <= BUSTER_EXPERIMENT_SUPERVISOR_MAX_CHILD_BYTES;
            }
            else if (!got) eof = true;
            else result = errno == EINTR;
        }
    }
    if (descriptor >= 0) result = close(descriptor) == 0 && result;
    result = result && eof && compiler_experiment_supervisor_parse_children(
        (String8){state->child_bytes, used}, state->owner_pid, state->child_pids, &state->child_count);
    return result;
}

// A zero si_pid with success still proves a currently live child. With no
// competing thread, SIGCHLD handler or auto-reap, exit cannot recycle its PID.
BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_owned(u64 pid, u64 deadline, bool* exited)
{
    bool result = pid > 1 && pid <= 2147483647ull && pid != (u64)getpid();
    siginfo_t information = {0};
    int status = -1;
    if (result)
    {
        do
        {
            status = waitid(P_PID, (id_t)pid, &information, WEXITED | WNOHANG | WNOWAIT);
        } while (status < 0 && errno == EINTR && os_now_microseconds() < deadline);
        result = status == 0 && (information.si_pid == 0 || (u64)information.si_pid == pid) &&
            os_now_microseconds() < deadline;
    }
    if (result) *exited = information.si_pid != 0;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_no_children(u64 deadline)
{
    siginfo_t information = {0};
    int status;
    do
    {
        status = waitid(P_ALL, 0, &information, WEXITED | WNOHANG | WNOWAIT);
    } while (status < 0 && errno == EINTR && os_now_microseconds() < deadline);
    bool result = status < 0 && errno == ECHILD && os_now_microseconds() < deadline;
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_begin(Arena* arena, CompilerExperimentSupervisor* state)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    if (state && !state->active && !state->cleanup_failed)
    {
        state->owner_pid = (u64)getpid();
        state->child_bytes = arena_allocate(arena, char8, BUSTER_EXPERIMENT_SUPERVISOR_MAX_CHILD_BYTES + 1);
        state->child_pids = arena_allocate(arena, u64, BUSTER_EXPERIMENT_SUPERVISOR_MAX_CHILDREN);
        state->children_path = string_format_z(arena, S8("/proc/self/task/{u64}/children"), state->owner_pid);
        u64 deadline = os_now_microseconds() + BUSTER_EXPERIMENT_SUPERVISOR_CLEANUP_US;
        sigset_t blocked = {0}, prior_mask = {0};
        bool masked = sigfillset(&blocked) == 0 && sigprocmask(SIG_BLOCK, &blocked, &prior_mask) == 0;
        result = masked && compiler_experiment_supervisor_single_thread(state->owner_pid) &&
            compiler_experiment_supervisor_children(state, deadline) && !state->child_count &&
            compiler_experiment_supervisor_no_children(deadline) &&
            prctl(PR_GET_CHILD_SUBREAPER, &state->prior_subreaper, 0, 0, 0) == 0 &&
            (state->prior_subreaper == 0 || state->prior_subreaper == 1);
        if (result)
        {
            result = prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) == 0;
            state->active = result;
            if (result)
            {
                result = compiler_experiment_supervisor_single_thread(state->owner_pid) &&
                    compiler_experiment_supervisor_children(state, deadline) && !state->child_count &&
                    compiler_experiment_supervisor_no_children(deadline);
            }
        }
        if (masked) result = sigprocmask(SIG_SETMASK, &prior_mask, 0) == 0 && result;
        // Never silently restore after uncertainty; retain active ownership.
        if (!result) state->cleanup_failed = true;
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(state);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_end(Arena* arena, CompilerExperimentSupervisor* state)
{
    bool result = false;
    BUSTER_UNUSED(arena);
#if BUSTER_LINUX && !BUSTER_ANDROID
    if (state && state->active && !state->cleanup_failed && state->owner_pid == (u64)getpid())
    {
        u64 deadline = os_now_microseconds() + BUSTER_EXPERIMENT_SUPERVISOR_CLEANUP_US;
        bool clear = false;
        result = true;
        while (result && !clear && os_now_microseconds() < deadline)
        {
            result = compiler_experiment_supervisor_single_thread(state->owner_pid) &&
                compiler_experiment_supervisor_children(state, deadline);
            state->waves += 1;
            for (u64 i = 0; result && i < state->child_count; i += 1)
            {
                u64 pid = state->child_pids[i];
                sigset_t blocked = {0}, prior = {0};
                bool masked = sigfillset(&blocked) == 0 && sigprocmask(SIG_BLOCK, &blocked, &prior) == 0;
                bool exited = false;
                result = masked && compiler_experiment_supervisor_owned(pid, deadline, &exited);
                if (result && !exited)
                {
                    result = kill((pid_t)pid, SIGKILL) == 0;
                    if (result) state->signalled += 1;
                }
                if (result)
                {
                    int status = 0;
                    pid_t reaped;
                    do
                    {
                        reaped = waitpid((pid_t)pid, &status, WNOHANG);
                    } while (reaped < 0 && errno == EINTR && os_now_microseconds() < deadline);
                    result = (reaped == 0 || (u64)reaped == pid) && os_now_microseconds() < deadline;
                    if (result && reaped > 0) state->reaped += 1;
                }
                // Handlers cannot recycle the numeric PID between proof and
                // signal/reap. The mask does not cross a measured child launch.
                if (masked) result = sigprocmask(SIG_SETMASK, &prior, 0) == 0 && result;
            }
            if (result && !state->child_count)
            {
                clear = compiler_experiment_supervisor_no_children(deadline);
                result = clear; // An empty kernel list with a live waiter is uncertain.
            }
            if (result && !clear) poll(0, 0, 1);
        }
        sigset_t blocked = {0}, prior_mask = {0};
        bool masked = sigfillset(&blocked) == 0 && sigprocmask(SIG_BLOCK, &blocked, &prior_mask) == 0;
        result = masked && result && clear && compiler_experiment_supervisor_single_thread(state->owner_pid) &&
            compiler_experiment_supervisor_children(state, deadline) && !state->child_count &&
            compiler_experiment_supervisor_no_children(deadline);
        if (result)
        {
            result = prctl(PR_SET_CHILD_SUBREAPER, state->prior_subreaper, 0, 0, 0) == 0;
            if (result) state->active = false;
        }
        if (masked) result = sigprocmask(SIG_SETMASK, &prior_mask, 0) == 0 && result;
        if (!result) state->cleanup_failed = true;
    }
#endif
    return result;
}

#if BUSTER_LINUX && !BUSTER_ANDROID
typedef struct CompilerExperimentSupervisorFixtureReport CompilerExperimentSupervisorFixtureReport;
struct CompilerExperimentSupervisorFixtureReport
{
    u64 role;
    u64 pid;
    u64 parent;
    u64 group;
};

BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_fixture_report(int descriptor, u64 role)
{
    CompilerExperimentSupervisorFixtureReport report = {.role = role, .pid = (u64)getpid(),
        .parent = (u64)getppid(), .group = (u64)getpgrp()};
    u64 used = 0, deadline = os_now_microseconds() + 2000000;
    bool result = true;
    while (result && used < sizeof(report))
    {
        ssize_t written = write(descriptor, (u8*)&report + used, sizeof(report) - used);
        if (written > 0) used += (u64)written;
        else result = written < 0 && errno == EINTR && os_now_microseconds() < deadline;
    }
    result = result && used == sizeof(report);
    return result;
}

// Three levels are explicit; no recursive fork tree or process callbacks.
BUSTER_GLOBAL_LOCAL void compiler_experiment_supervisor_fixture_manager(int descriptor)
{
    bool ready = setpgid(0, 0) == 0 && compiler_experiment_supervisor_fixture_report(descriptor, 0);
    pid_t grandchild = ready ? fork() : -1;
    if (grandchild == 0)
    {
        ready = setsid() > 1 && compiler_experiment_supervisor_fixture_report(descriptor, 1);
        pid_t great_grandchild = ready ? fork() : -1;
        if (great_grandchild == 0)
        {
            ready = setsid() > 1 && compiler_experiment_supervisor_fixture_report(descriptor, 2);
            close(descriptor);
            if (!ready) _exit(2);
            for (;;) poll(0, 0, -1);
        }
        close(descriptor);
        if (!ready || great_grandchild < 0) _exit(2);
        for (;;) poll(0, 0, -1);
    }
    close(descriptor);
    if (!ready || grandchild < 0) _exit(2);
    for (;;) poll(0, 0, -1);
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_fixture_reap(pid_t manager, u64 deadline)
{
    sigset_t blocked = {0}, prior_mask = {0};
    bool masked = sigfillset(&blocked) == 0 && sigprocmask(SIG_BLOCK, &blocked, &prior_mask) == 0;
    bool result = masked && manager > 1 && manager != getpid();
    bool exited = false;
    result = result && compiler_experiment_supervisor_owned((u64)manager, deadline, &exited);
    if (result && !exited) result = kill(manager, SIGKILL) == 0;
    bool reaped = false;
    while (result && !reaped && os_now_microseconds() < deadline)
    {
        int status = 0;
        pid_t waited = waitpid(manager, &status, WNOHANG);
        if (waited == manager) reaped = true;
        else if (waited < 0) result = errno == EINTR;
        if (result && !reaped) poll(0, 0, 1);
    }
    result = result && reaped;
    if (masked) result = sigprocmask(SIG_SETMASK, &prior_mask, 0) == 0 && result;
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL bool compiler_experiment_supervisor_self_test(Arena* arena)
{
    u64 pids[4] = {0}, count = 0;
    bool result = compiler_experiment_supervisor_parse_children(S8("101 202 303 "), 500, pids, &count) && count == 3 &&
        !compiler_experiment_supervisor_parse_children(S8("101 101 "), 500, pids, &count) &&
        !compiler_experiment_supervisor_parse_children(S8("1 "), 500, pids, &count) &&
        !compiler_experiment_supervisor_parse_children(S8("500 "), 500, pids, &count) &&
        !compiler_experiment_supervisor_parse_children(S8("01 "), 500, pids, &count) &&
        !compiler_experiment_supervisor_parse_children(S8("2147483648 "), 500, pids, &count) &&
        !compiler_experiment_supervisor_parse_children(S8("101 garbage"), 500, pids, &count);
#if BUSTER_LINUX && !BUSTER_ANDROID
    CompilerExperimentSupervisor supervisor = {0};
    bool began = compiler_experiment_supervisor_begin(arena, &supervisor);
    result = result && began;
    int channel[2] = {-1, -1};
    bool piped = began && pipe(channel) == 0;
    pid_t manager = piped ? fork() : -1;
    if (manager == 0)
    {
        close(channel[0]);
        compiler_experiment_supervisor_fixture_manager(channel[1]);
        _exit(2);
    }
    if (channel[1] >= 0) close(channel[1]);
    CompilerExperimentSupervisorFixtureReport reports[3] = {0};
    u64 used = 0, deadline = os_now_microseconds() + 5000000;
    bool collected = manager > 1;
    while (collected && used < sizeof(reports) && os_now_microseconds() < deadline)
    {
        struct pollfd event = {.fd = channel[0], .events = POLLIN};
        int status = poll(&event, 1, 10);
        if (status > 0)
        {
            ssize_t got = read(channel[0], (u8*)reports + used, sizeof(reports) - used);
            if (got > 0) used += (u64)got;
            else collected = got < 0 && errno == EINTR;
        }
        else if (status < 0) collected = errno == EINTR;
    }
    if (channel[0] >= 0) close(channel[0]);
    collected = collected && used == sizeof(reports);
    bool roles[3] = {0};
    u64 parents[3] = {0}, children[3] = {0};
    for (u64 i = 0; collected && i < 3; i += 1)
    {
        u64 role = reports[i].role;
        collected = role < 3 && !roles[role] && reports[i].pid > 1 &&
            reports[i].group == reports[i].pid && reports[i].group != (u64)getpgrp();
        if (collected)
        {
            roles[role] = true;
            parents[role] = reports[i].parent;
            children[role] = reports[i].pid;
        }
    }
    collected = collected && children[0] == (u64)manager && parents[0] == supervisor.owner_pid &&
        parents[1] == children[0] && parents[2] == children[1];
    // Exact manager ownership stays reserved until this wait returns.
    bool manager_reaped = manager > 1 && compiler_experiment_supervisor_fixture_reap(manager,
        os_now_microseconds() + 5000000);
    bool ended = began && compiler_experiment_supervisor_end(arena, &supervisor);
    result = result && piped && collected && manager_reaped && ended && !supervisor.active &&
        !supervisor.cleanup_failed && supervisor.reaped >= 2 && supervisor.signalled >= 2;
    int observed = -1;
    result = result && prctl(PR_GET_CHILD_SUBREAPER, &observed, 0, 0, 0) == 0 && observed == supervisor.prior_subreaper;
#else
    BUSTER_UNUSED(arena);
#endif
    return result;
}
