/* Native, per-invocation process measurement. The compiler is a fresh process
 * for EVERY sample, including tiny-file samples. No shell or polling timer is
 * included in the measured command. tp_process owns timeout and handle cleanup.
 * Linux counters attach before exec, inherit into descendants, and report their
 * scheduling fraction. They exclude kernel/hypervisor work. Other hosts return
 * unavailable, never fabricated zeroes. RSS is the OS per-process high-water
 * mark (not a sum of simultaneously live process-tree RSS). Linux also retains
 * wait4 page-fault and context-switch counts after the wall interval ends.
 * Their availability bits distinguish an observed zero from an unsupported
 * platform or failed wait. They are diagnostics, never PMU events or gates.
 *
 * tp_process_observe_inputs with a ruleset (TpProcessInputs.ruleset) launches
 * in lane B's canonical child layout (retirement_sandbox.h): the child
 * normalizes, places the side's held binary at 3 or 4, A's roots at 5 and 6
 * and the work directory at 7, fchdirs to 7, enters the sandbox and only then
 * reports over a pipe. The parent takes the start time after that report,
 * arms the timeout and releases the child to execve argv[0] (the side's slot
 * path), so the timer covers one pipe wake-up, the exec and the program, and
 * none of the placement or sandbox entry. Without a ruleset the timer starts
 * before the fork, as before.
 */
#ifndef BUSTER_THROUGHPUT_PLATFORM_H
#define BUSTER_THROUGHPUT_PLATFORM_H
#include <buster/lib/arena.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/time.h>
#include <buster/lib/system_headers.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define TP_PATH_CAP 4096
#define TP_COUNTERS 6
static char const* const tp_counter_names[TP_COUNTERS] = {
    "cycles", "instructions", "branches", "branch_misses", "cache_references", "cache_misses"};

typedef enum TpDiagnostic
{
    TP_MINOR_FAULTS,
    TP_MAJOR_FAULTS,
    TP_VOLUNTARY_SWITCHES,
    TP_INVOLUNTARY_SWITCHES,
    TP_DIAGNOSTICS
} TpDiagnostic;

/* Optional execution evidence. Ordinary CI timing does not pay for proc reads.
 * Only a trusted supervisor may authenticate these observations; this structure
 * by itself is neither a service receipt nor performance acceptance. */
typedef struct TpProcessObservation
{
    uint64_t pid, start_token, started_ns, finished_ns;
    int valid;
} TpProcessObservation;

typedef struct TpProcess
{
    double wall_seconds, user_seconds, system_seconds, peak_rss_bytes;
    double counters[TP_COUNTERS], running_fraction[TP_COUNTERS];
    int counter_errors[TP_COUNTERS];
    uint64_t diagnostics[TP_DIAGNOSTICS];
    unsigned diagnostics_available;
    int exit_code, signal_number, timed_out, launch_error;
    /* The caller's cancellation descriptor became readable while the child
     * ran, so the process group was killed (TpProcessInputs.cancellation). */
    int cancelled;
    /* A layout child refused before exec: it could not be set up, placed or
     * sandboxed, and launch_error is the errno it reported. */
    int refused;
} TpProcess;

#ifdef __linux__
/* The errno of a failed step, never zero. */
static inline int32_t tp_process_errno(void)
{
    int32_t error = errno ? (int32_t)errno : (int32_t)EACCES;
    return error;
}
#endif

static inline int tp_mkdir(char const* path)
{
    return os_make_directory_attempt(string_from_pointer(path));
}

static inline int tp_absolute(char const* path, char out[TP_PATH_CAP])
{
    TemporalArena temp = scratch_begin(0, 0);
    String8 absolute = os_path_absolute_lexical(temp.arena, string_from_pointer(path), true);
    int ok = absolute.length && absolute.length < TP_PATH_CAP;
    if (ok) memcpy(out, absolute.pointer, (size_t)absolute.length + 1);
    scratch_end(temp);
    return ok;
}

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <direct.h>

static inline int tp_first_allowed_cpu(void)
{
    DWORD_PTR process_mask = 0, system_mask = 0;
    int cpu = -1;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
    {
        for (unsigned i = 0; i < sizeof(process_mask) * 8 && cpu < 0; ++i)
            if (process_mask & ((DWORD_PTR)1 << i)) cpu = (int)i;
    }
    return cpu;
}

static inline TpProcess tp_process(char* const* args, char const* directory, char const* log_path,
                            unsigned timeout_seconds, int cpu, int counters)
{
    TpProcess result = {0};
    result.exit_code = -1;
    result.peak_rss_bytes = NAN;
    (void)counters;
    for (unsigned i = 0; i < TP_COUNTERS; ++i)
    {
        result.counters[i] = NAN;
        result.running_fraction[i] = NAN;
        result.counter_errors[i] = ENOSYS;
    }
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    HANDLE log = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE job = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    TemporalArena temp = scratch_begin(0, 0);
    SliceString8 arguments = slice_string_from_posix_string_list(temp.arena, (char**)args);
    WindowsStringList command = windows_string_list_from_slice_string(temp.arena, arguments);
    String16 working_directory = directory ? string16_from_string8(temp.arena, string_from_pointer(directory), true) : (String16){0};
    int ok = log != INVALID_HANDLE_VALUE && job != NULL &&
             SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
             string16_length(command) < 32768;
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = log;
    startup.hStdError = log;
    PROCESS_INFORMATION process = {0};
    TimeDataType start = timestamp_take();
    if (ok)
    {
        ok = CreateProcessW(NULL, command, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, working_directory.pointer, &startup, &process) != 0;
    }
    if (ok)
    {
        ok = AssignProcessToJobObject(job, process.hProcess) != 0;
        if (ok && cpu >= 0)
        {
            ok = (unsigned)cpu < sizeof(DWORD_PTR) * 8 && SetProcessAffinityMask(process.hProcess, (DWORD_PTR)1 << cpu) != 0;
        }
        if (ok)
        {
            ok = ResumeThread(process.hThread) != (DWORD)-1;
        }
        if (ok)
        {
            DWORD wait = WaitForSingleObject(process.hProcess, timeout_seconds * 1000);
            result.wall_seconds = (double)timestamp_ns_between(start, timestamp_take()) * 1e-9;
            result.timed_out = wait == WAIT_TIMEOUT;
            if (wait != WAIT_OBJECT_0)
            {
                TerminateJobObject(job, 124);
                WaitForSingleObject(process.hProcess, INFINITE);
                ok = result.timed_out;
            }
            DWORD code = 0;
            GetExitCodeProcess(process.hProcess, &code);
            result.exit_code = (int)code;
            FILETIME created, exited, kernel, user;
            if (GetProcessTimes(process.hProcess, &created, &exited, &kernel, &user))
            {
                ULARGE_INTEGER k, u;
                k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
                u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
                result.system_seconds = (double)k.QuadPart * 1e-7;
                result.user_seconds = (double)u.QuadPart * 1e-7;
            }
            else
            {
                ok = 0;
            }
            PROCESS_MEMORY_COUNTERS memory = {0};
            memory.cb = sizeof(memory);
            if (GetProcessMemoryInfo(process.hProcess, &memory, sizeof(memory)))
            {
                result.peak_rss_bytes = (double)memory.PeakWorkingSetSize;
            }
            else
            {
                ok = 0;
            }
        }
        if (!ok)
        {
            result.launch_error = (int)GetLastError();
            TerminateProcess(process.hProcess, 125);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (!ok && !result.launch_error)
    {
        result.launch_error = (int)GetLastError();
        if (!result.launch_error)
        {
            result.launch_error = EINVAL;
        }
    }
    if (job)
    {
        CloseHandle(job);
    }
    if (log != INVALID_HANDLE_VALUE)
    {
        CloseHandle(log);
    }
    scratch_end(temp);
    return result;
}
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <poll.h>
#include <linux/close_range.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/syscall.h>
#include "retirement_sandbox.h"
#endif

/* Only the orchestration tool installs this handler, never the compiler.
 * kill(2) is async-signal-safe. It bounds the entire compiler process group.
 */
static volatile sig_atomic_t tp_active_pid;
static volatile sig_atomic_t tp_timeout_fired;
static void tp_alarm_handler(int signal_number)
{
    (void)signal_number;
    tp_timeout_fired = 1;
    if (tp_active_pid > 0)
    {
        kill(-(pid_t)tp_active_pid, SIGKILL);
        kill((pid_t)tp_active_pid, SIGKILL);
    }
}

static inline int tp_first_allowed_cpu(void)
{
    int cpu = -1;
#ifdef __linux__
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0)
    {
        for (int i = 0; i < CPU_SETSIZE && cpu < 0; ++i)
            if (CPU_ISSET(i, &allowed)) cpu = i;
    }
#endif
    return cpu;
}

static void tp_cancel_handler(int signal_number)
{
    if (tp_active_pid > 0)
    {
        kill(-(pid_t)tp_active_pid, SIGKILL);
        kill((pid_t)tp_active_pid, SIGKILL);
        while (waitpid((pid_t)tp_active_pid, NULL, 0) < 0 && errno == EINTR) { }
    }
    _exit(128 + signal_number);
}

#ifdef __linux__
/* /proc/PID/stat field 2 may contain spaces and ')' characters. The final
 * ')' closes comm; fields 3 through 21 precede the unsigned starttime token.
 * Parse a bounded byte slice, never a truncated NUL-terminated prefix. */
static int tp_process_start_token(char const* bytes, size_t size, uint64_t wanted_pid, uint64_t* token)
{
    size_t at = 0;
    uint64_t pid = 0, value = 0;
    int ok = bytes && token && size > 0 && size < 4096 && wanted_pid > 0;
    for (size_t i = 0; ok && i < size; ++i) ok = bytes[i] != 0;
    while (ok && at < size && bytes[at] >= '0' && bytes[at] <= '9')
    {
        unsigned digit = (unsigned)(bytes[at++] - '0');
        ok = pid <= (UINT64_MAX - digit) / 10;
        if (ok) pid = pid * 10 + digit;
    }
    ok = ok && at > 0 && pid == wanted_pid && at + 2 < size && bytes[at] == ' ' && bytes[at + 1] == '(';
    size_t end_comm = size;
    while (ok && end_comm > at + 1 && bytes[end_comm - 1] != ')') --end_comm;
    ok = ok && end_comm > at + 2 && end_comm + 3 < size && bytes[end_comm] == ' ' &&
         bytes[end_comm + 1] != ' ' && bytes[end_comm + 2] == ' ';
    at = end_comm + 3;
    for (unsigned field = 4; ok && field <= 22; ++field)
    {
        size_t begin = at;
        while (at < size && bytes[at] != ' ' && bytes[at] != '\n') ++at;
        ok = at > begin && at < size;
        if (ok && field == 22)
        {
            for (size_t i = begin; ok && i < at; ++i)
            {
                ok = bytes[i] >= '0' && bytes[i] <= '9';
                unsigned digit = ok ? (unsigned)(bytes[i] - '0') : 0;
                ok = ok && value <= (UINT64_MAX - digit) / 10;
                if (ok) value = value * 10 + digit;
            }
            ok = ok && value > 0;
        }
        ++at;
    }
    if (token) *token = ok ? value : 0;
    return ok;
}

static int tp_process_identity(pid_t pid, uint64_t* token)
{
    char path[64], bytes[4096];
    if (token) *token = 0;
    int length = snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    int descriptor = length > 0 && (size_t)length < sizeof(path) ?
                     open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK) : -1;
    size_t used = 0;
    int ok = descriptor >= 0;
    while (ok && used < sizeof(bytes))
    {
        ssize_t count = read(descriptor, bytes + used, sizeof(bytes) - used);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) ok = 0;
        else if (!count) break;
        else used += (size_t)count;
    }
    if (descriptor >= 0 && close(descriptor) != 0) ok = 0;
    ok = ok && tp_process_start_token(bytes, used, (uint64_t)pid, token);
    return ok;
}

static uint64_t tp_process_monotonic_ns(void)
{
    struct timespec now;
    uint64_t result = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec >= 0 &&
        (uint64_t)now.tv_sec <= (UINT64_MAX - 999999999) / 1000000000 &&
        now.tv_nsec >= 0 && now.tv_nsec < 1000000000)
        result = (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
    return result;
}
#endif

/* The retirement producer can bind already-open inputs and an explicit
 * environment. Ordinary throughput keeps its existing path-based launch.
 * These descriptors must be private, >= 3 and close-on-exec; the caller owns
 * their lifetime. This is process plumbing, not the service sandbox or lease. */
typedef struct TpProcessInputs
{
    int executable, directory, log;
    char* const* environment;
    /* Optional (>= 3): a readable or hung-up descriptor, such as the
     * worker's SIGTERM self-pipe, kills the child's process group while it
     * runs. The wait then polls a Linux pidfd, so exit is seen at once.
     * The caller then owns SIGINT and SIGTERM: the launch installs no
     * handler of its own for them (without one it installs one that kills
     * the child's group and exits the caller).
     * Zero means none, which keeps positional initializers unchanged. */
    int cancellation;
    /* Optional canonical child layout (retirement_sandbox.h), used when
     * ruleset >= 3 (a bq_retirement_sandbox ruleset): the executable goes to
     * BQ_RETIREMENT_ROW_SLOT_BINARY + side, sources (A's base and candidate
     * roots) to 5 and 6 and directory to 7, which is the working directory;
     * args[0] must be the binary slot's /proc/self/fd path, and the
     * environment the command's own. The child is normalized
     * (bq_retirement_sandbox_normalize with memory_bytes of address space),
     * placed, enters the sandbox and only then reports ready; the parent
     * starts the observation timer after that report, immediately before it
     * releases the child to exec. Zero keeps the plain launch. */
    int side;
    int sources[2];
    int ruleset;
    uint64_t memory_bytes;
} TpProcessInputs;

#ifdef __linux__
/* The binary slot path args[0] must name under a layout. */
static inline int tp_process_layout_binary(char const* argument, int side)
{
    char expected[32];
    int written = snprintf(expected, sizeof(expected), "/proc/self/fd/%d", BQ_RETIREMENT_ROW_SLOT_BINARY + side);
    int ok = argument && written > 0 && (size_t)written < sizeof(expected) && !strcmp(argument, expected);
    return ok;
}
#endif

static TpProcess tp_process_observe_inputs(char* const* args, char const* directory, char const* log_path,
    unsigned timeout_seconds, int cpu, int counters, TpProcessObservation* observation,
    TpProcessInputs const* inputs)
{
    TpProcess result = {0};
    if (observation) *observation = (TpProcessObservation){0};
    result.exit_code = -1;
    result.peak_rss_bytes = NAN;
    int counter_fds[TP_COUNTERS];
    for (unsigned i = 0; i < TP_COUNTERS; ++i)
    {
        counter_fds[i] = -1;
        result.counters[i] = NAN;
        result.running_fraction[i] = NAN;
        result.counter_errors[i] = counters ? ENOSYS : 0;
    }
    int ready[2] = {-1, -1}, armed[2] = {-1, -1};
    int log = -1, layout = 0;
#ifdef __linux__
    if (inputs)
    {
        layout = inputs->ruleset >= 3;
        int descriptors[] = {inputs->executable, inputs->directory, inputs->log, inputs->sources[0],
            inputs->sources[1], inputs->ruleset};
        unsigned checked = layout ? 6u : 3u;
        int valid = inputs->environment != NULL &&
            (!inputs->cancellation || (inputs->cancellation >= 3 && fcntl(inputs->cancellation, F_GETFD) >= 0)) &&
            (!layout || ((inputs->side == 0 || inputs->side == 1) && args && tp_process_layout_binary(args[0],
                inputs->side)));
        for (unsigned i = 0; valid && i < checked; ++i)
        {
            int flags = descriptors[i] >= 3 ? fcntl(descriptors[i], F_GETFD) : -1;
            valid = flags >= 0 && (flags & FD_CLOEXEC);
        }
        if (valid) log = fcntl(inputs->log, F_DUPFD_CLOEXEC, 3);
        else errno = EINVAL;
    }
    else
#endif
    if (!inputs && log_path) log = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int ok = log >= 0 && pipe(ready) == 0;
#ifdef __linux__
    ok = ok && (!layout || pipe2(armed, O_CLOEXEC) == 0);
#endif
#ifndef __linux__
    if (observation) { ok = 0; errno = ENOTSUP; }
#endif
    struct sigaction handler, previous, ignore_pipe, previous_pipe, cancel, previous_int, previous_term;
    memset(&cancel, 0, sizeof(cancel));
    cancel.sa_handler = tp_cancel_handler;
    sigemptyset(&cancel.sa_mask);
    /* A caller that supplies a cancellation descriptor owns SIGINT/SIGTERM
     * (typically a self-pipe feeding that descriptor): the wait below kills
     * the child's group when it becomes readable and reports
     * TP_PROCESS_CANCELLED, so the caller can retain its failure state.
     * Installing tp_cancel_handler there would _exit the caller instead. */
    int own_signals = !(inputs && inputs->cancellation >= 3);
    int int_owned = own_signals && sigaction(SIGINT, &cancel, &previous_int) == 0;
    int term_owned = own_signals && sigaction(SIGTERM, &cancel, &previous_term) == 0;
    int int_set = !own_signals || int_owned;
    int term_set = !own_signals || term_owned;
    memset(&ignore_pipe, 0, sizeof(ignore_pipe));
    ignore_pipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_pipe.sa_mask);
    int pipe_handler_set = sigaction(SIGPIPE, &ignore_pipe, &previous_pipe) == 0;
    memset(&handler, 0, sizeof(handler));
    handler.sa_handler = tp_alarm_handler;
    sigemptyset(&handler.sa_mask);
    int handler_set = sigaction(SIGALRM, &handler, &previous) == 0;
    ok = ok && handler_set && pipe_handler_set && int_set && term_set;
    pid_t pid = -1;
    TimeDataType start = timestamp_take();
#ifdef __linux__
    /* A layout starts the timer only after the child entered its sandbox
     * (below); a plain launch starts it before the fork. */
    if (ok && observation && !layout)
    {
        observation->started_ns = tp_process_monotonic_ns();
        ok = observation->started_ns > 0;
    }
#endif
    if (ok)
    {
        pid = fork();
        ok = pid >= 0;
    }
    if (pid == 0)
    {
        close(ready[1]);
        int go = ready[0], report = -1, parked_ends = 1;
#ifdef __linux__
        /* Under a layout both handshake ends are parked above the slots
         * first, so placing the slots cannot overwrite them. An end that
         * cannot be parked stays where it is and the child places nothing:
         * it still reports its refusal and exits. */
        if (layout)
        {
            close(armed[0]);
            go = fcntl(ready[0], F_DUPFD_CLOEXEC, BQ_RETIREMENT_ROW_SLOT_HIGH);
            report = go >= 0 ? fcntl(armed[1], F_DUPFD_CLOEXEC, BQ_RETIREMENT_ROW_SLOT_HIGH) : -1;
            parked_ends = go >= 0 && report >= 0;
            if (go >= 0) close(ready[0]);
            else go = ready[0];
            if (report >= 0) close(armed[1]);
            else report = armed[1];
        }
#endif
        int child_ok = go >= 0 && parked_ends && setpgid(0, 0) == 0 && dup2(log, STDOUT_FILENO) >= 0 &&
            dup2(log, STDERR_FILENO) >= 0;
        close(log);
        if (child_ok && directory)
        {
            child_ok = chdir(directory) == 0;
        }
#ifdef __linux__
        if (child_ok && inputs) child_ok = fchdir(inputs->directory) == 0;
#endif
        if (child_ok && cpu >= 0)
        {
#ifdef __linux__
            cpu_set_t set;
            CPU_ZERO(&set);
            if (cpu >= CPU_SETSIZE)
            {
                child_ok = 0;
                errno = EINVAL;
            }
            else
            {
                CPU_SET(cpu, &set);
                child_ok = sched_setaffinity(0, sizeof(set), &set) == 0;
            }
#else
            child_ok = 0;
#endif
        }
#ifdef __linux__
        if (layout)
        {
            /* The canonical layout and sandbox, all before the timer: /dev/null
             * stdin, the normalized state (bq_retirement_sandbox_normalize),
             * the four slots (the ruleset parked), cwd the work slot, then no
             * new privileges, Landlock and the seccomp filter. The child then
             * reports 0, or the errno of the first step that failed, and
             * closes its end at once, so the parent never waits on a child
             * that stopped before reporting. */
            int32_t failure = child_ok ? 0 : tp_process_errno();
            int input = !failure ? open("/dev/null", O_RDONLY | O_CLOEXEC) : -1;
            if (!failure && !(input >= 0 && dup2(input, STDIN_FILENO) == STDIN_FILENO)) failure = tp_process_errno();
            if (input >= 0) close(input);
            if (!failure && !bq_retirement_sandbox_normalize(0077, inputs->memory_bytes)) failure = tp_process_errno();
            int const held[4] = {inputs->executable, inputs->sources[0], inputs->sources[1], inputs->directory};
            int parked = !failure ? bq_retirement_sandbox_slots(held, (uint32_t)inputs->side, inputs->ruleset) : -1;
            if (!failure && parked < 0) failure = tp_process_errno();
            if (!failure && fchdir(BQ_RETIREMENT_ROW_SLOT_WORK) != 0) failure = tp_process_errno();
            if (!failure && !bq_retirement_sandbox_enter(parked)) failure = tp_process_errno();
            ssize_t written = write(report, &failure, sizeof(failure));
            child_ok = !failure && written == (ssize_t)sizeof(failure);
            close(report);
        }
#endif
        char byte;
        ssize_t received;
        do
        {
            received = read(go, &byte, 1);
        } while (received < 0 && errno == EINTR);
        close(go);
        if (child_ok && received == 1)
        {
            if (!layout) sigaction(SIGPIPE, &previous_pipe, NULL);
#ifdef __linux__
            if (layout) execve(args[0], args, inputs->environment);
            else if (inputs)
            {
                /* Neither a sample spool nor an unrelated supervisor handle
                 * may leak into the child. Fail closed on an older kernel. */
#ifdef SYS_close_range
                int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
                child_ok = input >= 0 && dup2(input, STDIN_FILENO) >= 0 &&
                    syscall(SYS_close_range, 3u, ~0u, CLOSE_RANGE_CLOEXEC) == 0;
                if (input >= 0) close(input);
                if (child_ok) fexecve(inputs->executable, args, inputs->environment);
#endif
            }
            else
#endif
            execv(args[0], args);
        }
        /* No buffered parent streams are flushed after a failed exec. */
        _exit(125);
    }
    if (ok)
    {
        tp_active_pid = (sig_atomic_t)pid;
        close(ready[0]);
        ready[0] = -1;
        /* Do this in both processes to close the timeout/process-group race. */
        (void)setpgid(pid, pid);
#ifdef __linux__
        static uint64_t const configs[TP_COUNTERS] = {
            PERF_COUNT_HW_CPU_CYCLES, PERF_COUNT_HW_INSTRUCTIONS, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
            PERF_COUNT_HW_BRANCH_MISSES, PERF_COUNT_HW_CACHE_REFERENCES, PERF_COUNT_HW_CACHE_MISSES};
        if (counters)
        {
            for (unsigned i = 0; i < TP_COUNTERS; ++i)
            {
                struct perf_event_attr event;
                memset(&event, 0, sizeof(event));
                event.type = PERF_TYPE_HARDWARE;
                event.size = sizeof(event);
                event.config = configs[i];
                event.disabled = 1;
                event.enable_on_exec = 1;
                event.inherit = 1;
                event.exclude_kernel = 1;
                event.exclude_hv = 1;
                event.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
                /* No PERF_FORMAT_GROUP: that format is incompatible with
                 * inherited counters on kernels supporting this tool. */
                counter_fds[i] = (int)syscall(SYS_perf_event_open, &event, pid, -1, -1, PERF_FLAG_FD_CLOEXEC);
                result.counter_errors[i] = counter_fds[i] < 0 ? errno : 0;
            }
        }
#endif
        int identity_ok = 1;
#ifdef __linux__
        if (observation)
        {
            observation->pid = (uint64_t)pid;
            identity_ok = tp_process_identity(pid, &observation->start_token);
        }
#endif
#ifdef __linux__
        /* A layout child reports once it is placed and sandboxed; only then
         * does the timer start, so neither the placement nor the sandbox
         * entry is measured. The report is 0, or the errno of the step that
         * failed: the child refused (TpProcess.refused), with that errno as
         * the launch error. The wait also watches the cancellation
         * descriptor and is bounded by the launch's timeout (at least one
         * second): a cancellation is reported as such, a child that never
         * reports as ETIMEDOUT and one that exits without a report as
         * ECHILD. */
        if (layout)
        {
            close(armed[1]);
            armed[1] = -1;
            int32_t reported = -1;
            size_t received = 0;
            int cancel_fd = inputs->cancellation >= 3 ? inputs->cancellation : -1, expired = 0, stopped = 0;
            uint64_t bound = (uint64_t)(timeout_seconds ? timeout_seconds : 1u) * UINT64_C(1000000000);
            uint64_t deadline = tp_process_monotonic_ns() + bound;
            int waiting = identity_ok;
            while (waiting)
            {
                uint64_t now = tp_process_monotonic_ns();
                uint64_t left = now < deadline ? (deadline - now) / UINT64_C(1000000) + 1u : 0;
                struct pollfd waits[2] = {{.fd = armed[0], .events = POLLIN}, {.fd = cancel_fd, .events = POLLIN}};
                int count = left ? poll(waits, 2, left > 86400000u ? 86400000 : (int)left) : 0;
                if (count < 0 && errno != EINTR) waiting = 0;
                else if (count == 0)
                {
                    expired = 1;
                    waiting = 0;
                }
                else if (count > 0 && waits[1].revents)
                {
                    stopped = 1;
                    waiting = 0;
                }
                else if (count > 0 && waits[0].revents)
                {
                    ssize_t got = read(armed[0], (char*)&reported + received, sizeof(reported) - received);
                    if (got > 0) received += (size_t)got;
                    waiting = got > 0 ? received < sizeof(reported) : got < 0 && errno == EINTR;
                }
            }
            close(armed[0]);
            armed[0] = -1;
            if (identity_ok && !(received == sizeof(reported) && reported == 0))
            {
                identity_ok = 0;
                result.refused = received == sizeof(reported) && reported > 0;
                result.cancelled = stopped;
                result.launch_error = result.refused ? reported : stopped ? 0 : expired ? ETIMEDOUT : ECHILD;
            }
            start = timestamp_take();
            if (identity_ok && observation)
            {
                observation->started_ns = tp_process_monotonic_ns();
                identity_ok = observation->started_ns > 0;
            }
        }
#endif
        tp_active_pid = (sig_atomic_t)pid;
        tp_timeout_fired = 0;
        alarm(timeout_seconds);
        char byte = 1;
        ssize_t sent;
        do
        {
            sent = identity_ok ? write(ready[1], &byte, 1) : -1;
        } while (identity_ok && sent < 0 && errno == EINTR);
        close(ready[1]);
        ready[1] = -1;
        if (sent != 1)
        {
            kill(-pid, SIGKILL);
        }
#ifdef __linux__
        /* With a cancellation descriptor, wait on the child's pidfd and that
         * descriptor together; without pidfd support the child is killed
         * rather than run uncancellable, reported as launch error ENOSYS (not
         * as a cancellation). The alarm still bounds the child. */
        int cancellation = inputs && inputs->cancellation >= 3 && sent == 1 ? inputs->cancellation : -1;
#ifdef SYS_pidfd_open
        int child_fd = cancellation >= 0 ? (int)syscall(SYS_pidfd_open, pid, 0) : -1;
#else
        int child_fd = -1;
#endif
        if (cancellation >= 0 && child_fd < 0)
        {
            result.launch_error = ENOSYS;
            kill(-pid, SIGKILL);
        }
        int watching = child_fd >= 0;
        while (watching)
        {
            struct pollfd waits[2] = {{.fd = child_fd, .events = POLLIN}, {.fd = cancellation, .events = POLLIN}};
            int ready_count = poll(waits, 2, -1);
            if (ready_count < 0 && errno != EINTR)
            {
                kill(-pid, SIGKILL);
                watching = 0;
            }
            else if (ready_count > 0 && waits[0].revents) watching = 0;
            else if (ready_count > 0 && waits[1].revents)
            {
                result.cancelled = 1;
                kill(-pid, SIGKILL);
                watching = 0;
            }
        }
        if (child_fd >= 0) close(child_fd);
#endif
        int status = 0;
        struct rusage usage;
        memset(&usage, 0, sizeof(usage));
        pid_t waited;
        do
        {
            waited = wait4(pid, &status, 0, &usage);
        } while (waited < 0 && errno == EINTR);
        /* Disarm at once: an alarm after the reap must neither kill the
         * reaped child's group nor mark a clean exit as a timeout, so the
         * pid is forgotten first and a timeout needs a signalled child. */
        tp_active_pid = 0;
        alarm(0);
        result.timed_out = tp_timeout_fired != 0 && waited == pid && WIFSIGNALED(status);
        result.wall_seconds = (double)timestamp_ns_between(start, timestamp_take()) * 1e-9;
#ifdef __linux__
        if (observation)
        {
            observation->finished_ns = tp_process_monotonic_ns();
            observation->valid = identity_ok && waited == pid && sent == 1 &&
                                 observation->finished_ns > observation->started_ns;
            if (observation->valid)
                result.wall_seconds = (double)(observation->finished_ns - observation->started_ns) / 1000000000.0;
        }
#endif
        ok = waited == pid && sent == 1 && (!observation || observation->valid);
        if (ok)
        {
            result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            result.signal_number = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
            result.user_seconds = (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec * 1e-6;
            result.system_seconds = (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec * 1e-6;
#ifdef __linux__
            long const diagnostics[TP_DIAGNOSTICS] = {
                [TP_MINOR_FAULTS] = usage.ru_minflt,
                [TP_MAJOR_FAULTS] = usage.ru_majflt,
                [TP_VOLUNTARY_SWITCHES] = usage.ru_nvcsw,
                [TP_INVOLUNTARY_SWITCHES] = usage.ru_nivcsw};
            for (unsigned i = 0; i < TP_DIAGNOSTICS; ++i)
            {
                if (diagnostics[i] >= 0)
                {
                    result.diagnostics[i] = (uint64_t)diagnostics[i];
                    result.diagnostics_available |= 1u << i;
                }
            }
#endif
#ifdef __APPLE__
            result.peak_rss_bytes = (double)usage.ru_maxrss;
#else
            result.peak_rss_bytes = (double)usage.ru_maxrss * 1024.0;
#endif
        }
        /* Clean any helper that outlived the compiler (including failures).
         * All measured compiler work is required to have finished at exit. */
        (void)kill(-pid, SIGKILL);
        for (unsigned i = 0; i < TP_COUNTERS; ++i)
        {
            if (counter_fds[i] >= 0)
            {
                struct { uint64_t count, enabled, running; } value;
                ssize_t count = read(counter_fds[i], &value, sizeof(value));
                if (count == (ssize_t)sizeof(value) && value.enabled && value.running)
                {
                    result.running_fraction[i] = (double)value.running / (double)value.enabled;
                    if (result.running_fraction[i] >= 0.90)
                    {
                        result.counters[i] = (double)value.count * (double)value.enabled / (double)value.running;
                    }
                    else
                    {
                        result.counter_errors[i] = EAGAIN;
                    }
                }
                else
                {
                    result.counter_errors[i] = count < 0 ? errno : EIO;
                }
                close(counter_fds[i]);
            }
        }
    }
    if (!ok && !result.launch_error)
    {
        result.launch_error = errno ? errno : EIO;
    }
    if (log >= 0)
    {
        close(log);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        if (armed[i] >= 0) close(armed[i]);
    }
    if (ready[0] >= 0)
    {
        close(ready[0]);
    }
    if (ready[1] >= 0)
    {
        close(ready[1]);
    }
    if (int_owned) sigaction(SIGINT, &previous_int, NULL);
    if (term_owned) sigaction(SIGTERM, &previous_term, NULL);
    if (pipe_handler_set)
    {
        sigaction(SIGPIPE, &previous_pipe, NULL);
    }
    if (handler_set)
    {
        sigaction(SIGALRM, &previous, NULL);
    }
    return result;
}

static TpProcess tp_process_observe(char* const* args, char const* directory, char const* log_path,
    unsigned timeout_seconds, int cpu, int counters, TpProcessObservation* observation)
{
    TpProcess result = tp_process_observe_inputs(args, directory, log_path,
        timeout_seconds, cpu, counters, observation, NULL);
    return result;
}

static inline TpProcess tp_process(char* const* args, char const* directory, char const* log_path,
                            unsigned timeout_seconds, int cpu, int counters)
{
    TpProcess result = tp_process_observe(args, directory, log_path, timeout_seconds, cpu, counters, NULL);
    return result;
}
#endif
#endif
