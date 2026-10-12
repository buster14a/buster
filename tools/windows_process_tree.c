// Native Windows case-tree ownership for the bootstrap case scheduler (#3690).
//
// Owner: tools/bootstrap_wrapper_cases.py runs every Windows case command
// through `launch`, and drains its job before fixture removal (#2985). CI
// builds this file; the scheduler only receives its path.
// Entry points:
// - `launch <job-name> <budget-ms> --deadline-ns <ns> --clock qpc|tick
//   --status-path <path> <command-line>` (Windows): create the named
//   job with kill-on-close, start the command suspended, assign it to the job,
//   resume it, wait for it, then terminate and wait for any remaining member
//   within the budget. Publish the command's status atomically; exit zero on
//   certified normal completion, 124 on certified runtime timeout, or 125
//   when lifecycle establishment, draining or publication fails.
// - `drain <job-name> <budget-ms>` (Windows): terminate every member of the
//   named job and wait for each one within the budget; print one record. The
//   launcher is not a member; it exits once its command has ended.
// - `selftest` (Windows): membership controls with a real non-member process.
// - `fixture <silent|partial|malformed|repeated|partial-late <ms>|late <ms>|sleep <ms>>` (every host):
//   deliberately broken drain records for the scheduler's caller controls.
// Map: tree_drain owns verification and waiting; tree_member is the only
// identity test; tree_launch/tree_drain_main/tree_selftest are the modes.
//
// Ownership is established before the command runs, with the same
// suspend/assign/resume sequence as os_process_spawn's process groups: every
// descendant is a member regardless of PID reuse, parent links or clock
// adjustments. A listed PID is waited on only after
// IsProcessInJob confirms that the opened handle belongs to this job; a reused
// PID naming another process is never waited on or terminated. Wait timeouts,
// wait failures, open/query failures and capacity limits fail the drain.
#include <buster/lib/base.h>
#include <buster/lib/system_headers.h>
#include <stdio.h>
#include <string.h>
#if BUSTER_WINDOWS
#include <shellapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "shell32") // CommandLineToArgvW.
#endif
#endif

#define TREE_EXIT_USAGE 2
#define TREE_EXIT_INCOMPLETE 1
#define TREE_EXIT_ABSENT 3
// A launcher whose command finished but whose job could not be emptied.
#define TREE_EXIT_DRAIN_FAILED 125
#define TREE_EXIT_RUNTIME_TIMEOUT 124
#define TREE_NS_PER_SECOND 1000000000ULL
#define TREE_NS_PER_MILLISECOND 1000000ULL
// Status given to members terminated by `drain`.
#define TREE_EXIT_TERMINATED 0xC000013A
#define TREE_MEMBER_CAPACITY 4096
#define TREE_COMMAND_CAPACITY 32768
#define TREE_POLL_MILLISECONDS 10

BUSTER_GLOBAL_LOCAL bool tree_decimal(char const* text, u64* output)
{
    u64 length = strlen(text);
    bool result = length && length <= 20 && (length == 1 || text[0] != '0');
    u64 value = 0;
    for (u64 i = 0; result && i < length; i += 1)
    {
        u8 byte = (u8)text[i];
        result = byte >= '0' && byte <= '9' && value <= (UINT64_MAX - (u64)(byte - '0')) / 10;
        if (result)
        {
            value = value * 10 + (u64)(byte - '0');
        }
    }
    if (result)
    {
        *output = value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void tree_sleep(u64 milliseconds)
{
#if BUSTER_WINDOWS
    Sleep((DWORD)milliseconds);
#else
    struct timespec duration = {.tv_sec = (time_t)(milliseconds / 1000), .tv_nsec = (long)(milliseconds % 1000) * 1000000};
    nanosleep(&duration, 0);
#endif
}

// Broken drain records: the caller must reject each one.
BUSTER_GLOBAL_LOCAL int tree_fixture(int argc, char** argv)
{
    int result = 0;
    u64 milliseconds = 0;
    char const* complete = "{\"complete\":true,\"waited\":[],\"foreign\":[],\"pending\":[],\"failed\":[]}\n";
    if (argc >= 3 && strcmp(argv[2], "silent") == 0)
    {
    }
    else if (argc >= 3 && strcmp(argv[2], "partial") == 0)
    {
        printf("{\"complete\":true,\"waited\":[");
    }
    else if (argc >= 3 && strcmp(argv[2], "malformed") == 0)
    {
        printf("{\"complete\":true}\n{\"complete\":true}\n");
    }
    else if (argc >= 3 && strcmp(argv[2], "repeated") == 0)
    {
        printf("%s%s", complete, complete);
    }
    else if (argc >= 4 && strcmp(argv[2], "partial-late") == 0 && tree_decimal(argv[3], &milliseconds))
    {
        printf("{\"complete\":true,\"waited\":[");
        fflush(stdout);
        tree_sleep(milliseconds);
    }
    else if (argc >= 4 && strcmp(argv[2], "late") == 0 && tree_decimal(argv[3], &milliseconds))
    {
        tree_sleep(milliseconds);
        printf("%s", complete);
    }
    else if (argc >= 4 && strcmp(argv[2], "sleep") == 0 && tree_decimal(argv[3], &milliseconds))
    {
        tree_sleep(milliseconds);
    }
    else
    {
        result = TREE_EXIT_USAGE;
    }
    fflush(stdout);
    return result;
}

#if BUSTER_WINDOWS
typedef struct TreeMembers TreeMembers;
struct TreeMembers
{
    DWORD assigned;
    DWORD listed;
    ULONG_PTR pid[TREE_MEMBER_CAPACITY];
};

typedef struct TreeDrain TreeDrain;
struct TreeDrain
{
    u32 waited[TREE_MEMBER_CAPACITY];
    u32 foreign[TREE_MEMBER_CAPACITY];
    u32 pending[TREE_MEMBER_CAPACITY];
    u32 failed[TREE_MEMBER_CAPACITY];
    u32 failed_code[TREE_MEMBER_CAPACITY];
    u32 waited_count;
    u32 foreign_count;
    u32 pending_count;
    u32 failed_count;
    bool complete;
};

BUSTER_GLOBAL_LOCAL TreeMembers tree_members;
BUSTER_GLOBAL_LOCAL TreeDrain tree_result;

BUSTER_GLOBAL_LOCAL void tree_record_failure(u32 pid, u32 code)
{
    if (tree_result.failed_count < TREE_MEMBER_CAPACITY)
    {
        tree_result.failed[tree_result.failed_count] = pid;
        tree_result.failed_code[tree_result.failed_count] = code;
        tree_result.failed_count += 1;
    }
    tree_result.complete = false;
}

// The kernel's job membership is the identity: a PID listed by the job and
// then reused by another process is not a member of this job.
BUSTER_GLOBAL_LOCAL bool tree_member(HANDLE process, HANDLE job, bool* member)
{
    BOOL in_job = FALSE;
    bool result = IsProcessInJob(process, job, &in_job) != 0;
    *member = result && in_job;
    return result;
}

BUSTER_GLOBAL_LOCAL void tree_record(u32* list, u32* count, u32 pid)
{
    bool present = false;
    for (u32 i = 0; i < *count; i += 1)
    {
        present = present || list[i] == pid;
    }
    if (!present && *count < TREE_MEMBER_CAPACITY)
    {
        list[*count] = pid;
        *count += 1;
    }
}

// After TerminateJobObject, wait for every member until the job's
// active-process count shows no live member, or the deadline passes. As in
// os_windows_job_wait_empty, the count drops at exit even while handles to a
// process remain open; such a member may stay listed, and its wait then
// succeeds immediately.
BUSTER_GLOBAL_LOCAL void tree_drain(HANDLE job, u64 deadline)
{
    bool members_remain = true;
    tree_result = (TreeDrain){.complete = true};
    while (members_remain && tree_result.complete)
    {
        tree_members = (TreeMembers){0};
        if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, &tree_members, sizeof(tree_members), 0))
        {
            tree_record_failure(0, (u32)GetLastError());
        }
        else if (tree_members.assigned > tree_members.listed)
        {
            tree_record_failure(0, ERROR_MORE_DATA);
        }
        for (DWORD i = 0; tree_result.complete && i < tree_members.listed; i += 1)
        {
            u32 pid = (u32)tree_members.pid[i];
            HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!process)
            {
                u32 error = (u32)GetLastError();
                if (error != ERROR_INVALID_PARAMETER) // Otherwise the listed PID has exited.
                {
                    tree_record_failure(pid, error);
                }
            }
            else
            {
                bool member = false;
                if (!tree_member(process, job, &member))
                {
                    tree_record_failure(pid, (u32)GetLastError());
                }
                else if (!member)
                {
                    tree_record(tree_result.foreign, &tree_result.foreign_count, pid);
                }
                else
                {
                    u64 now = GetTickCount64();
                    DWORD wait = WaitForSingleObject(process, now < deadline ? (DWORD)(deadline - now) : 0);
                    if (wait == WAIT_OBJECT_0)
                    {
                        // WAIT_OBJECT_0 alone certifies termination.
                        tree_record(tree_result.waited, &tree_result.waited_count, pid);
                    }
                    else if (wait == WAIT_TIMEOUT)
                    {
                        tree_record(tree_result.pending, &tree_result.pending_count, pid);
                        tree_result.complete = false;
                    }
                    else
                    {
                        tree_record_failure(pid, wait == WAIT_FAILED ? (u32)GetLastError() : (u32)wait);
                    }
                }
                if (!CloseHandle(process))
                {
                    tree_record_failure(pid, (u32)GetLastError());
                }
            }
        }
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting = {0};
        if (tree_result.complete && !QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), 0))
        {
            tree_record_failure(0, (u32)GetLastError());
        }
        members_remain = accounting.ActiveProcesses != 0;
        if (members_remain && tree_result.complete)
        {
            // An exiting member stays listed briefly after it signals.
            if (GetTickCount64() >= deadline)
            {
                tree_result.complete = false;
            }
            else
            {
                Sleep(TREE_POLL_MILLISECONDS);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void tree_print_list(char const* name, u32 const* list, u32 count)
{
    printf(",\"%s\":[", name);
    for (u32 i = 0; i < count; i += 1)
    {
        printf("%s%u", i ? "," : "", list[i]);
    }
    putchar(']');
}

BUSTER_GLOBAL_LOCAL void tree_print_drain(void)
{
    printf("{\"complete\":%s", tree_result.complete ? "true" : "false");
    tree_print_list("waited", tree_result.waited, tree_result.waited_count);
    tree_print_list("foreign", tree_result.foreign, tree_result.foreign_count);
    tree_print_list("pending", tree_result.pending, tree_result.pending_count);
    printf(",\"failed\":[");
    for (u32 i = 0; i < tree_result.failed_count; i += 1)
    {
        printf("%s[%u,%u]", i ? "," : "", tree_result.failed[i], tree_result.failed_code[i]);
    }
    printf("]}\n");
    fflush(stdout);
}

BUSTER_GLOBAL_LOCAL HANDLE tree_owned_job(WCHAR const* name)
{
    SetLastError(ERROR_SUCCESS);
    HANDLE result = CreateJobObjectW(0, name);
    if (result && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(result);
        result = 0;
    }
    if (result)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(result, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        {
            CloseHandle(result);
            result = 0;
        }
    }
    return result;
}

typedef struct TreeLaunchStatus TreeLaunchStatus;
struct TreeLaunchStatus
{
    bool complete;
    bool runtime_timed_out;
    bool root_status_known;
    bool clock_valid;
    bool tick_clock;
    DWORD root_pid;
    DWORD root_returncode;
    u64 runtime_deadline_ns;
    u64 root_finished_ns;
    u64 cleanup_started_ns;
    u64 cleanup_finished_ns;
};

#if defined(WINDOWS_PROCESS_TREE_TEST)
BUSTER_GLOBAL_LOCAL u32 tree_test_startup_delay_ms;
BUSTER_GLOBAL_LOCAL u32 tree_test_cleanup_delay_ms;
BUSTER_GLOBAL_LOCAL u32 tree_test_status_delay_ms;
#endif

// Match the adapter's actual Windows CPython monotonic clock: <=3.12 uses
// GetTickCount64; >=3.13 uses QPC/QPF. Neither has a per-process origin.
BUSTER_GLOBAL_LOCAL u64 tree_clock_ns(TreeLaunchStatus* status)
{
    u64 result = 0;
    bool valid;
    if (status->tick_clock)
    {
        u64 tick = GetTickCount64();
        valid = tick <= (u64)INT64_MAX / TREE_NS_PER_MILLISECOND;
        if (valid)
        {
            result = tick * TREE_NS_PER_MILLISECOND;
        }
    }
    else
    {
        LARGE_INTEGER counter;
        LARGE_INTEGER frequency;
        valid = QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&frequency) &&
                counter.QuadPart >= 0 && frequency.QuadPart > 0;
        if (valid)
        {
            u64 ticks = (u64)counter.QuadPart;
            u64 rate = (u64)frequency.QuadPart;
            u64 seconds = ticks / rate;
            u64 remainder = ticks % rate;
            valid = seconds <= (u64)INT64_MAX / TREE_NS_PER_SECOND &&
                    remainder <= UINT64_MAX / TREE_NS_PER_SECOND;
            if (valid)
            {
                u64 whole = seconds * TREE_NS_PER_SECOND;
                u64 fraction = remainder * TREE_NS_PER_SECOND / rate;
                valid = whole <= (u64)INT64_MAX - fraction;
                if (valid)
                {
                    result = whole + fraction;
                }
            }
        }
    }
    status->clock_valid = status->clock_valid && valid;
    return result;
}

// File-backed terminal evidence is published only after teardown and checked
// handle closes. A fresh temporary and non-replacing rename reject stale data.
BUSTER_GLOBAL_LOCAL bool tree_launch_status(WCHAR const* path, TreeLaunchStatus const* status)
{
    static WCHAR temporary[TREE_COMMAND_CAPACITY];
    char record[1024];
    size_t length = wcslen(path);
    bool result = length + 5 < TREE_COMMAND_CAPACITY;
    HANDLE file = INVALID_HANDLE_VALUE;
    if (result)
    {
        memcpy(temporary, path, length * sizeof(WCHAR));
        memcpy(temporary + length, L".tmp", 5 * sizeof(WCHAR));
        result = GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    if (result)
    {
        file = CreateFileW(temporary, GENERIC_WRITE, 0, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
        result = file != INVALID_HANDLE_VALUE;
    }
    if (result)
    {
        int bytes = snprintf(record, sizeof(record),
            "{\"complete\":%s,\"runtime_timed_out\":%s,\"root_returncode\":%lld,\"root_pid\":%lu,"
            "\"runtime_deadline_ns\":%llu,\"root_finished_ns\":%llu,\"cleanup_started_ns\":%llu,\"cleanup_finished_ns\":%llu}\n",
            status->complete ? "true" : "false", status->runtime_timed_out ? "true" : "false",
            status->root_status_known ? (long long)status->root_returncode : -1LL, (unsigned long)status->root_pid,
            (unsigned long long)status->runtime_deadline_ns, (unsigned long long)status->root_finished_ns,
            (unsigned long long)status->cleanup_started_ns, (unsigned long long)status->cleanup_finished_ns);
        DWORD written = 0;
        result = bytes > 0 && (size_t)bytes < sizeof(record) &&
                 WriteFile(file, record, (DWORD)bytes, &written, 0) && written == (DWORD)bytes && FlushFileBuffers(file);
    }
    if (file != INVALID_HANDLE_VALUE)
    {
        result = CloseHandle(file) != 0 && result;
    }
    if (result)
    {
        result = MoveFileExW(temporary, path, MOVEFILE_WRITE_THROUGH) != 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int tree_launch(WCHAR const* name, u64 budget, WCHAR const* command_line,
                                    u64 deadline_ns, bool tick_clock, WCHAR const* status_path)
{
    int result = TREE_EXIT_DRAIN_FAILED;
    static WCHAR command[TREE_COMMAND_CAPACITY];
    TreeLaunchStatus status = {.clock_valid = true, .tick_clock = tick_clock, .runtime_deadline_ns = deadline_ns};
    PROCESS_INFORMATION child = {0};
    HANDLE job = 0;
    bool valid = true;
    bool assigned = false;
#if defined(WINDOWS_PROCESS_TREE_TEST)
    Sleep(tree_test_startup_delay_ms);
#endif
    u64 now = tree_clock_ns(&status);
    status.runtime_timed_out = status.clock_valid && now >= deadline_ns;
    if (!status.runtime_timed_out && status.clock_valid)
    {
        job = tree_owned_job(name);
        valid = job != 0 && wcslen(command_line) < TREE_COMMAND_CAPACITY;
        if (valid)
        {
            STARTUPINFOW startup = {.cb = sizeof(startup), .dwFlags = STARTF_USESTDHANDLES};
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
            memcpy(command, command_line, (wcslen(command_line) + 1) * sizeof(WCHAR));
            now = tree_clock_ns(&status);
            status.runtime_timed_out = status.clock_valid && now >= deadline_ns;
            if (status.clock_valid && !status.runtime_timed_out)
            {
                valid = CreateProcessW(0, command, 0, 0, TRUE, CREATE_SUSPENDED, 0, 0, &startup, &child) != 0;
            }
        }
        if (valid && status.clock_valid && !status.runtime_timed_out)
        {
            status.root_pid = child.dwProcessId;
            assigned = AssignProcessToJobObject(job, child.hProcess) != 0;
            valid = assigned;
            now = tree_clock_ns(&status);
            status.runtime_timed_out = status.clock_valid && now >= deadline_ns;
            if (valid && status.clock_valid && !status.runtime_timed_out)
            {
                valid = ResumeThread(child.hThread) != (DWORD)-1;
            }
        }
        bool waiting = valid && status.clock_valid && !status.runtime_timed_out;
        while (waiting)
        {
            now = tree_clock_ns(&status);
            if (!status.clock_valid || now >= deadline_ns)
            {
                status.runtime_timed_out = status.clock_valid;
                waiting = false;
            }
            else
            {
                u64 remaining_ms = (deadline_ns - now + TREE_NS_PER_MILLISECOND - 1) / TREE_NS_PER_MILLISECOND;
                DWORD slice = (DWORD)(remaining_ms < UINT32_MAX ? remaining_ms : UINT32_MAX - 1);
                DWORD waited = WaitForSingleObject(child.hProcess, slice);
                if (waited == WAIT_OBJECT_0)
                {
                    status.root_finished_ns = tree_clock_ns(&status);
                    status.root_status_known = GetExitCodeProcess(child.hProcess, &status.root_returncode) != 0;
                    valid = status.root_status_known;
                    status.runtime_timed_out = status.clock_valid && status.root_finished_ns > deadline_ns;
                    waiting = false;
                }
                else if (waited != WAIT_TIMEOUT)
                {
                    valid = false;
                    waiting = false;
                }
            }
        }
    }
    status.cleanup_started_ns = tree_clock_ns(&status);
    // Every operation after runtime completion shares this one cleanup budget.
    // A late timeout observation cannot start a fresh allowance past runtime+budget.
    u64 cleanup_limit_ns = deadline_ns + budget * TREE_NS_PER_MILLISECOND;
    if (status.cleanup_started_ns < deadline_ns)
    {
        cleanup_limit_ns = status.cleanup_started_ns + budget * TREE_NS_PER_MILLISECOND;
    }
    u64 remaining_ms = status.clock_valid && status.cleanup_started_ns < cleanup_limit_ns ?
                       (cleanup_limit_ns - status.cleanup_started_ns) / TREE_NS_PER_MILLISECOND : 0;
    u64 cleanup_deadline = GetTickCount64() + remaining_ms;
#if defined(WINDOWS_PROCESS_TREE_TEST)
    Sleep(tree_test_cleanup_delay_ms);
#endif
    if (job && !TerminateJobObject(job, TREE_EXIT_TERMINATED))
    {
        valid = false;
    }
    if (child.hProcess)
    {
        if (!assigned && !TerminateProcess(child.hProcess, TREE_EXIT_DRAIN_FAILED))
        {
            valid = false;
        }
        u64 tick = GetTickCount64();
        DWORD waited = WaitForSingleObject(child.hProcess, tick < cleanup_deadline ? (DWORD)(cleanup_deadline - tick) : 0);
        valid = waited == WAIT_OBJECT_0 && valid;
        if (waited == WAIT_OBJECT_0 && !status.root_status_known)
        {
            status.root_status_known = GetExitCodeProcess(child.hProcess, &status.root_returncode) != 0;
            valid = status.root_status_known && valid;
        }
        valid = CloseHandle(child.hThread) != 0 && valid;
        valid = CloseHandle(child.hProcess) != 0 && valid;
    }
    if (job)
    {
        tree_drain(job, cleanup_deadline);
        valid = tree_result.complete && valid;
        valid = CloseHandle(job) != 0 && valid;
    }
    status.cleanup_finished_ns = tree_clock_ns(&status);
    status.complete = valid && status.clock_valid && status.cleanup_finished_ns <= cleanup_limit_ns;
    if (status.complete)
    {
        result = status.runtime_timed_out ? TREE_EXIT_RUNTIME_TIMEOUT : 0;
    }
#if defined(WINDOWS_PROCESS_TREE_TEST)
    Sleep(tree_test_status_delay_ms);
#endif
    if (!tree_launch_status(status_path, &status))
    {
        result = TREE_EXIT_DRAIN_FAILED;
    }
    // Publication is part of cleanup. Even an already-published success record
    // cannot be accepted after its budget: failure exits never match its 0/124
    // terminal code, regardless of the payload's own arbitrary return value.
    now = tree_clock_ns(&status);
    if (!status.clock_valid || now > cleanup_limit_ns)
    {
        result = TREE_EXIT_DRAIN_FAILED;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int tree_drain_main(WCHAR const* name, u64 budget)
{
    int result = TREE_EXIT_INCOMPLETE;
    u64 deadline = GetTickCount64() + budget;
    HANDLE job = OpenJobObjectW(JOB_OBJECT_QUERY | JOB_OBJECT_TERMINATE, FALSE, name);
    if (!job)
    {
        u32 error = (u32)GetLastError();
        printf("{\"complete\":false,\"absent\":%s,\"error\":%u}\n", error == ERROR_FILE_NOT_FOUND ? "true" : "false", error);
        fflush(stdout);
        result = error == ERROR_FILE_NOT_FOUND ? TREE_EXIT_ABSENT : TREE_EXIT_INCOMPLETE;
    }
    else
    {
        // The launcher is outside the job; it exits once its command ends.
        bool terminated = TerminateJobObject(job, TREE_EXIT_TERMINATED) != 0;
        u32 error = (u32)GetLastError();
        tree_drain(job, deadline);
        if (!terminated)
        {
            tree_record_failure(0, error);
        }
        tree_print_drain();
        result = tree_result.complete ? 0 : TREE_EXIT_INCOMPLETE;
        CloseHandle(job);
    }
    return result;
}

// A real process outside the job must never be treated as a member, and
// draining the job must neither wait on nor terminate it.
BUSTER_GLOBAL_LOCAL int tree_selftest(void)
{
    int result = TREE_EXIT_INCOMPLETE;
    static WCHAR const arguments[] = L"\" fixture sleep 30000";
    static WCHAR command[2][MAX_PATH + BUSTER_ARRAY_LENGTH(arguments) + 1];
    PROCESS_INFORMATION child[2] = {0};
    HANDLE job = CreateJobObjectW(0, 0);
    DWORD length = GetModuleFileNameW(0, command[0] + 1, MAX_PATH);
    bool valid = job && length && length < MAX_PATH;
    if (valid)
    {
        command[0][0] = L'"';
        memcpy(command[0] + 1 + length, arguments, sizeof(arguments));
        memcpy(command[1], command[0], sizeof(command[0]));
    }
    for (u32 i = 0; valid && i < 2; i += 1)
    {
        STARTUPINFOW startup = {.cb = sizeof(startup)};
        valid = CreateProcessW(0, command[i], 0, 0, FALSE, CREATE_SUSPENDED, 0, 0, &startup, &child[i]) != 0;
    }
    valid = valid && AssignProcessToJobObject(job, child[0].hProcess);
    for (u32 i = 0; valid && i < 2; i += 1)
    {
        valid = ResumeThread(child[i].hThread) != (DWORD)-1;
    }
    bool inside = false;
    bool outside = true;
    valid = valid && tree_member(child[0].hProcess, job, &inside) && tree_member(child[1].hProcess, job, &outside);
    if (valid)
    {
        TerminateJobObject(job, TREE_EXIT_TERMINATED);
        tree_drain(job, GetTickCount64() + 10000);
    }
    bool outsider_alive = valid && WaitForSingleObject(child[1].hProcess, 0) == WAIT_TIMEOUT;
    // A terminated member can leave the job's PID list before the drain lists
    // it, so certify termination through the retained handle and an empty job.
    bool member_waited = valid && tree_result.complete && WaitForSingleObject(child[0].hProcess, 0) == WAIT_OBJECT_0;
    printf("{\"selftest\":%s,\"member\":%s,\"outsider_member\":%s,\"outsider_alive\":%s,\"member_waited\":%s}\n",
           valid ? "true" : "false", inside ? "true" : "false", outside ? "true" : "false",
           outsider_alive ? "true" : "false", member_waited ? "true" : "false");
    fflush(stdout);
    for (u32 i = 0; i < 2; i += 1)
    {
        if (child[i].hProcess)
        {
            TerminateProcess(child[i].hProcess, TREE_EXIT_TERMINATED);
            WaitForSingleObject(child[i].hProcess, 10000);
            CloseHandle(child[i].hProcess);
            CloseHandle(child[i].hThread);
        }
    }
    if (job)
    {
        CloseHandle(job);
    }
    result = valid && inside && !outside && outsider_alive && member_waited && tree_result.complete ? 0 : TREE_EXIT_INCOMPLETE;
    return result;
}
#endif

int main(int argc, char** argv)
{
    int result = TREE_EXIT_USAGE;
    if (argc >= 3 && strcmp(argv[1], "fixture") == 0)
    {
        result = tree_fixture(argc, argv);
    }
#if BUSTER_WINDOWS
    else
    {
        // Job names and command lines stay UTF-16 end to end.
        u64 budget = 0;
        int wide_count = 0;
        WCHAR** wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
        if (!wide || wide_count != argc)
        {
            fprintf(stderr, "windows_process_tree: command line decoding failed\n");
        }
        else if (argc == 11 && strcmp(argv[1], "launch") == 0 && tree_decimal(argv[3], &budget))
        {
            u64 deadline_ns = 0;
            bool tick_clock = strcmp(argv[7], "tick") == 0;
            bool valid = strcmp(argv[4], "--deadline-ns") == 0 && tree_decimal(argv[5], &deadline_ns) &&
                         strcmp(argv[6], "--clock") == 0 && (tick_clock || strcmp(argv[7], "qpc") == 0) &&
                         strcmp(argv[8], "--status-path") == 0 && budget < UINT32_MAX &&
                         deadline_ns <= (u64)INT64_MAX - budget * TREE_NS_PER_MILLISECOND;
            if (valid)
            {
                result = tree_launch(wide[2], budget, wide[10], deadline_ns, tick_clock, wide[9]);
            }
        }
        else if (argc == 4 && strcmp(argv[1], "drain") == 0 && tree_decimal(argv[3], &budget))
        {
            result = tree_drain_main(wide[2], budget);
        }
        else if (argc == 2 && strcmp(argv[1], "selftest") == 0)
        {
            result = tree_selftest();
        }
        if (wide)
        {
            LocalFree(wide);
        }
    }
#endif
    if (result == TREE_EXIT_USAGE)
    {
        fprintf(stderr, "usage: windows_process_tree launch <job> <budget-ms> --deadline-ns <ns> --clock qpc|tick "
                        "--status-path <path> <command-line> | drain <job> <budget-ms> | "
                        "selftest | fixture <silent|partial|malformed|repeated|partial-late <ms>|late <ms>|sleep <ms>>\n");
    }
    return result;
}
