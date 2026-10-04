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
#ifndef TP_PROCESS_DESCRIPTOR_ONLY
static char const* const tp_counter_names[TP_COUNTERS] = {
    "cycles", "instructions", "branches", "branch_misses", "cache_references", "cache_misses"};
#endif

typedef enum TpDiagnostic
{
    TP_MINOR_FAULTS,
    TP_MAJOR_FAULTS,
    TP_VOLUNTARY_SWITCHES,
    TP_INVOLUNTARY_SWITCHES,
    TP_DIAGNOSTICS
} TpDiagnostic;

/* Child-side failures are distinct from a program that legitimately exits 125. */
typedef enum TpLaunchStage
{
    TP_LAUNCH_NONE,
    TP_LAUNCH_GROUP,
    TP_LAUNCH_STDOUT,
    TP_LAUNCH_STDERR,
    TP_LAUNCH_DIRECTORY,
    TP_LAUNCH_CPU,
    TP_LAUNCH_READY,
    TP_LAUNCH_SIGNAL,
    TP_LAUNCH_EXEC,
    TP_LAUNCH_REPORT
} TpLaunchStage;

#ifndef TP_PROCESS_DESCRIPTOR_ONLY
static char const* tp_launch_stage_name(TpLaunchStage stage)
{
    char const* result = "unknown";
    switch (stage)
    {
    case TP_LAUNCH_NONE: result = "none"; break;
    case TP_LAUNCH_GROUP: result = "process-group"; break;
    case TP_LAUNCH_STDOUT: result = "stdout"; break;
    case TP_LAUNCH_STDERR: result = "stderr"; break;
    case TP_LAUNCH_DIRECTORY: result = "working-directory"; break;
    case TP_LAUNCH_CPU: result = "cpu-affinity"; break;
    case TP_LAUNCH_READY: result = "ready-pipe"; break;
    case TP_LAUNCH_SIGNAL: result = "restore-signal"; break;
    case TP_LAUNCH_EXEC: result = "exec"; break;
    case TP_LAUNCH_REPORT: result = "error-pipe"; break;
    }
    return result;
}

#endif

typedef struct TpProcess
{
    double wall_seconds, user_seconds, system_seconds, peak_rss_bytes;
    double counters[TP_COUNTERS], running_fraction[TP_COUNTERS];
    int counter_errors[TP_COUNTERS];
    uint64_t diagnostics[TP_DIAGNOSTICS];
    unsigned diagnostics_available;
    int exit_code, signal_number, timed_out, launch_error;
    TpLaunchStage launch_stage;
} TpProcess;

#ifndef TP_PROCESS_DESCRIPTOR_ONLY
static int tp_mkdir(char const* path)
{
    return os_make_directory_attempt(string_from_pointer(path));
}

static int tp_absolute(char const* path, char out[TP_PATH_CAP])
{
    TemporalArena temp = scratch_begin(0, 0);
    String8 absolute = os_path_absolute_lexical(temp.arena, string_from_pointer(path), true);
    int ok = absolute.length && absolute.length < TP_PATH_CAP;
    if (ok) memcpy(out, absolute.pointer, (size_t)absolute.length + 1);
    scratch_end(temp);
    return ok;
}

#endif

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <direct.h>

static int tp_first_allowed_cpu(void)
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

static TpProcess tp_process(char* const* args, char const* directory, char const* log_path,
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
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <linux/close_range.h>
#include <stddef.h>
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

#ifndef TP_PROCESS_DESCRIPTOR_ONLY
static int tp_first_allowed_cpu(void)
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

#endif

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

static int tp_process_group_self_error(int status, int error)
{
    int result = status == 0 ? 0 : error;
    /* The parent may already have installed this exact child's private group.
     * EPERM also refuses a session leader whose group already equals its PID.
     * Validate the required kernel state; do not retry or accept another group. */
    if (result == EPERM && getpgrp() == getpid()) result = 0;
    return result;
}

/* Descriptor launches share the existing timing/wait4 collector. Trusted
 * callers verify an immutable executable before timing, supply an open log,
 * and keep result records outside the payload write surface. */
typedef struct TpDescriptorLaunch
{
    int executable, log;
    unsigned file_limit;
} TpDescriptorLaunch;

static TpProcess tp_process_internal(char* const* args, char const* directory, char const* log_path,
                            unsigned timeout_seconds, int cpu, int counters, TpDescriptorLaunch const* descriptor)
{
    TpProcess result = {0};
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
    int ready[2] = {-1, -1}, launch[2] = {-1, -1};
    int log = descriptor ? fcntl(descriptor->log, F_DUPFD_CLOEXEC, 3) :
              open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    /* The child reports one small packet before exec. CLOEXEC distinguishes
     * a real exit 125; nonblocking reads cannot inherit a descendant wait. */
    int ok = log >= 0 && pipe(ready) == 0 && pipe(launch) == 0 &&
             fcntl(launch[0], F_SETFL, O_NONBLOCK) == 0 &&
             fcntl(launch[1], F_SETFL, O_NONBLOCK) == 0 &&
             fcntl(launch[0], F_SETFD, FD_CLOEXEC) == 0 &&
             fcntl(launch[1], F_SETFD, FD_CLOEXEC) == 0;
    struct sigaction handler, previous, ignore_pipe, previous_pipe, cancel, previous_int, previous_term;
    memset(&cancel, 0, sizeof(cancel));
    cancel.sa_handler = tp_cancel_handler;
    sigemptyset(&cancel.sa_mask);
    int int_set = sigaction(SIGINT, &cancel, &previous_int) == 0;
    int term_set = sigaction(SIGTERM, &cancel, &previous_term) == 0;
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
    if (ok)
    {
        pid = fork();
        ok = pid >= 0;
    }
    if (pid == 0)
    {
        close(ready[1]);
        close(launch[0]);
        struct { int stage, error; } failure = {TP_LAUNCH_NONE, 0};
        int group_status = setpgid(0, 0);
        int group_error = group_status == 0 ? 0 : errno;
        group_error = tp_process_group_self_error(group_status, group_error);
        if (group_error)
        {
            failure.stage = TP_LAUNCH_GROUP;
            failure.error = group_error;
        }
        if (!failure.error && dup2(log, STDOUT_FILENO) < 0)
        {
            failure.stage = TP_LAUNCH_STDOUT;
            failure.error = errno;
        }
        if (!failure.error && dup2(log, STDERR_FILENO) < 0)
        {
            failure.stage = TP_LAUNCH_STDERR;
            failure.error = errno;
        }
        close(log);
        if (!failure.error && directory && chdir(directory) != 0)
        {
            failure.stage = TP_LAUNCH_DIRECTORY;
            failure.error = errno;
        }
        if (!failure.error && cpu >= 0)
        {
            failure.stage = TP_LAUNCH_CPU;
#ifdef __linux__
            cpu_set_t set;
            CPU_ZERO(&set);
            if (cpu >= CPU_SETSIZE)
            {
                failure.error = EINVAL;
            }
            else
            {
                CPU_SET(cpu, &set);
                if (sched_setaffinity(0, sizeof(set), &set) != 0) failure.error = errno;
            }
#else
            failure.error = ENOSYS;
#endif
        }
        char byte;
        ssize_t received;
        do
        {
            received = read(ready[0], &byte, 1);
        } while (received < 0 && errno == EINTR);
        if (!failure.error && received != 1)
        {
            failure.stage = TP_LAUNCH_READY;
            failure.error = received < 0 ? errno : EIO;
        }
        close(ready[0]);
        if (!failure.error && sigaction(SIGPIPE, &previous_pipe, NULL) != 0)
        {
            failure.stage = TP_LAUNCH_SIGNAL;
            failure.error = errno;
        }
        if (!failure.error && descriptor)
        {
#ifdef __linux__
            struct rlimit limit = {descriptor->file_limit, descriptor->file_limit};
            struct rlimit core = {0, 0};
            int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
            int confined = input >= 0 && dup2(input, STDIN_FILENO) >= 0 &&
                           setrlimit(RLIMIT_FSIZE, &limit) == 0 && setrlimit(RLIMIT_CORE, &core) == 0 &&
                           syscall(SYS_close_range, 3u, ~0u, CLOSE_RANGE_CLOEXEC) == 0;
            if (input >= 0) close(input);
            /* Descendants stay in the collector's group until cleanup. The
             * fixed helper installs its own process group before this filter. */
            struct sock_filter policy[] = {
                BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
                BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
                BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
                BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x40000000u, 0, 1),
                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
                BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_setpgid, 0, 1),
                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
                BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_setsid, 0, 1),
                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
            struct sock_fprog program = {(unsigned short)(sizeof(policy) / sizeof(policy[0])), policy};
            confined = confined && prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
                       prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
            if (!confined) { failure.stage = TP_LAUNCH_EXEC; failure.error = errno ? errno : EIO; }
#else
            failure.stage = TP_LAUNCH_EXEC; failure.error = ENOSYS;
#endif
        }
        if (!failure.error)
        {
            if (descriptor)
            {
                char* const environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
                fexecve(descriptor->executable, args, environment);
            }
            else execv(args[0], args);
            failure.stage = TP_LAUNCH_EXEC;
            failure.error = errno;
        }
        /* No allocation or buffered streams after fork. The packet is below
         * PIPE_BUF and its empty pipe is nonblocking; interrupted writes retry. */
        ssize_t reported;
        do
        {
            reported = write(launch[1], &failure, sizeof(failure));
        } while (reported < 0 && errno == EINTR);
        close(launch[1]);
        _exit(125);
    }
    if (ok)
    {
        close(launch[1]);
        launch[1] = -1;
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
        tp_active_pid = (sig_atomic_t)pid;
        tp_timeout_fired = 0;
        alarm(timeout_seconds);
        char byte = 1;
        ssize_t sent;
        do
        {
            sent = write(ready[1], &byte, 1);
        } while (sent < 0 && errno == EINTR);
        close(ready[1]);
        ready[1] = -1;
        if (sent != 1)
        {
            kill(-pid, SIGKILL);
        }
        int status = 0;
        struct rusage usage;
        memset(&usage, 0, sizeof(usage));
        pid_t waited;
        do
        {
            waited = wait4(pid, &status, 0, &usage);
        } while (waited < 0 && errno == EINTR);
        result.wall_seconds = (double)timestamp_ns_between(start, timestamp_take()) * 1e-9;
        alarm(0);
        tp_active_pid = 0;
        result.timed_out = tp_timeout_fired != 0;
        ok = waited == pid && sent == 1;
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
        if (waited == pid)
        {
            struct { int stage, error; } failure = {0, 0};
            ssize_t received;
            do
            {
                received = read(launch[0], &failure, sizeof(failure));
            } while (received < 0 && errno == EINTR);
            if (received == (ssize_t)sizeof(failure) && failure.stage > TP_LAUNCH_NONE &&
                failure.stage < TP_LAUNCH_REPORT && failure.error > 0)
            {
                result.launch_stage = (TpLaunchStage)failure.stage;
                result.launch_error = failure.error;
            }
            else if (received != 0)
            {
                result.launch_stage = TP_LAUNCH_REPORT;
                result.launch_error = received < 0 ? errno : EIO;
            }
        }
        /* Clean any helper that outlived the compiler (including failures).
         * All measured compiler work is required to have finished at exit. */
        (void)kill(-pid, SIGKILL);
        if (descriptor)
        {
            /* The descriptor sampler is a subreaper. No descendant may
             * overlap the next sample, including a leader-exit background
             * child. Group escape was denied before payload exec. Cleanup is
             * outside the declared wall interval and bounded independently. */
            TimeDataType cleanup = timestamp_take();
            int complete = 0, outlived = 0;
            for (int cleaning = 1; cleaning;)
            {
                pid_t reaped = waitpid(-pid, NULL, WNOHANG);
                if (reaped >= 0) outlived = 1;
                if (reaped < 0 && errno == ECHILD) { complete = 1; cleaning = 0; }
                else if (reaped < 0 && errno != EINTR) cleaning = 0;
                else if (timestamp_ns_between(cleanup, timestamp_take()) >= 1000000000u) cleaning = 0;
                else if (reaped == 0) usleep(1000);
            }
            if (!complete || outlived) { result.launch_stage = TP_LAUNCH_GROUP; result.launch_error = EBUSY; }
        }
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
    if (!ok)
    {
        result.launch_error = errno ? errno : EIO;
    }
    if (log >= 0)
    {
        close(log);
    }
    if (ready[0] >= 0)
    {
        close(ready[0]);
    }
    if (ready[1] >= 0)
    {
        close(ready[1]);
    }
    if (launch[0] >= 0) close(launch[0]);
    if (launch[1] >= 0) close(launch[1]);
    if (int_set) sigaction(SIGINT, &previous_int, NULL);
    if (term_set) sigaction(SIGTERM, &previous_term, NULL);
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
#ifndef TP_PROCESS_DESCRIPTOR_ONLY
static TpProcess tp_process(char* const* args, char const* directory, char const* log_path,
                            unsigned timeout_seconds, int cpu, int counters)
{
    TpProcess result = tp_process_internal(args, directory, log_path, timeout_seconds, cpu, counters, NULL);
    return result;
}
#endif

#endif
#endif
