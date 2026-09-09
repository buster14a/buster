/* Native, per-invocation process measurement. The compiler is a fresh process
 * for EVERY sample, including tiny-file samples. No shell or polling timer is
 * included in the measured command. tp_process owns timeout and handle cleanup.
 * Linux counters attach before exec, inherit into descendants, and report their
 * scheduling fraction. They exclude kernel/hypervisor work. Other hosts return
 * unavailable, never fabricated zeroes. RSS is the OS per-process high-water
 * mark (not a sum of simultaneously live process-tree RSS).
 */
#ifndef BUSTER_THROUGHPUT_PLATFORM_H
#define BUSTER_THROUGHPUT_PLATFORM_H
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

typedef struct TpProcess
{
    double wall_seconds, user_seconds, system_seconds, peak_rss_bytes;
    double counters[TP_COUNTERS], running_fraction[TP_COUNTERS];
    int counter_errors[TP_COUNTERS];
    int exit_code, signal_number, timed_out, launch_error;
} TpProcess;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <direct.h>

static double tp_clock(void)
{
    LARGE_INTEGER ticks, frequency;
    QueryPerformanceCounter(&ticks);
    QueryPerformanceFrequency(&frequency);
    return (double)ticks.QuadPart / (double)frequency.QuadPart;
}

static int tp_mkdir(char const* path)
{
    return _mkdir(path) == 0 || errno == EEXIST;
}

static int tp_absolute(char const* path, char out[TP_PATH_CAP])
{
    return _fullpath(out, path, TP_PATH_CAP) != NULL;
}

/* Windows CRT quoting: double backslashes before quotes and at argument end.
 * Even an empty argument is quoted. There is no cmd.exe interpretation.
 */
static int tp_windows_command(char* const* args, char* command, size_t capacity)
{
    size_t used = 0;
    int ok = 1;
    for (unsigned a = 0; args[a] && ok; ++a)
    {
        if (used + 3 >= capacity)
        {
            ok = 0;
            break;
        }
        if (a)
        {
            command[used++] = ' ';
        }
        command[used++] = '"';
        char const* p = args[a];
        while (*p && ok)
        {
            size_t slashes = 0;
            while (*p == '\\')
            {
                ++slashes;
                ++p;
            }
            size_t copies = (*p == '"' || !*p) ? slashes * 2 : slashes;
            if (*p == '"')
            {
                ++copies;
            }
            if (used + copies + 3 >= capacity)
            {
                ok = 0;
                break;
            }
            for (size_t i = 0; i < copies; ++i)
            {
                command[used++] = '\\';
            }
            if (*p)
            {
                command[used++] = *p++;
            }
        }
        if (ok)
        {
            command[used++] = '"';
        }
    }
    if (ok)
    {
        command[used] = 0;
    }
    return ok;
}

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
    char command[32768];
    int ok = log != INVALID_HANDLE_VALUE && job != NULL &&
             SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
             tp_windows_command(args, command, sizeof(command));
    STARTUPINFOA startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = log;
    startup.hStdError = log;
    PROCESS_INFORMATION process = {0};
    double start = tp_clock();
    if (ok)
    {
        ok = CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, directory, &startup, &process) != 0;
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
            result.wall_seconds = tp_clock() - start;
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
#endif

static double tp_clock(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static int tp_mkdir(char const* path)
{
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static int tp_absolute(char const* path, char out[TP_PATH_CAP])
{
    int ok = 1;
    if (path[0] == '/')
    {
        ok = snprintf(out, TP_PATH_CAP, "%s", path) < TP_PATH_CAP;
    }
    else
    {
        char current[TP_PATH_CAP];
        ok = getcwd(current, sizeof(current)) != NULL;
        if (ok)
        {
            ok = snprintf(out, TP_PATH_CAP, "%s/%s", current, path) < TP_PATH_CAP;
        }
    }
    return ok;
}

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

static TpProcess tp_process(char* const* args, char const* directory, char const* log_path,
                            unsigned timeout_seconds, int cpu, int counters)
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
    int ready[2] = {-1, -1};
    int log = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int ok = log >= 0 && pipe(ready) == 0;
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
    double start = tp_clock();
    if (ok)
    {
        pid = fork();
        ok = pid >= 0;
    }
    if (pid == 0)
    {
        close(ready[1]);
        int child_ok = setpgid(0, 0) == 0 && dup2(log, STDOUT_FILENO) >= 0 && dup2(log, STDERR_FILENO) >= 0;
        close(log);
        if (child_ok && directory)
        {
            child_ok = chdir(directory) == 0;
        }
        if (child_ok && cpu >= 0)
        {
#ifdef __linux__
            cpu_set_t set;
            CPU_ZERO(&set);
            if (cpu >= CPU_SETSIZE)
            {
                child_ok = 0;
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
        char byte;
        ssize_t received;
        do
        {
            received = read(ready[0], &byte, 1);
        } while (received < 0 && errno == EINTR);
        close(ready[0]);
        if (child_ok && received == 1)
        {
            sigaction(SIGPIPE, &previous_pipe, NULL);
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
        result.wall_seconds = tp_clock() - start;
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
#endif
#endif
