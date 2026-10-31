// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// External Csmith/YARPGen driver for #142. This is original test tooling, not part of source compilation. Commands
// are argv arrays, never shell text. A private Linux subreaper owns each command's descendants; captures, deadlines,
// process cleanup and result delivery belong to that supervisor. The caller has no process-global subreaper state.
// A campaign has a fresh directory; source, commands, versions, outputs and failures are retained even on success.
// No missing tool, failed preflight, empty output or fixture run counts as a compiler campaign pass.
// Layout: DifferentialOptions; differential_process; artifact/config helpers; differential_case; campaign; command.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#define BUSTER_DIFFERENTIAL_ARGS 128
#define BUSTER_DIFFERENTIAL_EXTRA_ARGS 32
#define BUSTER_DIFFERENTIAL_BUFFER 8192
#define BUSTER_DIFFERENTIAL_MAX_BYTES (64ull << 20)
#define BUSTER_DIFFERENTIAL_CONFIG_BYTES (128u << 10)
#define BUSTER_DIFFERENTIAL_TIMEOUT_MS 30000
#define BUSTER_DIFFERENTIAL_CLEANUP_MS 5000

typedef struct DifferentialArguments DifferentialArguments;
struct DifferentialArguments
{
    char* values[BUSTER_DIFFERENTIAL_ARGS];
    uint32_t count;
};

typedef struct DifferentialOptions DifferentialOptions;
struct DifferentialOptions
{
    const char* output;
    const char* buster;
    const char* clang;
    const char* csmith;
    const char* yarpgen;
    const char* csmith_include;
    const char* revision;
    const char* generator_revision;
    const char* replay;
    const char* source_directory;
    DifferentialArguments buster_args;
    DifferentialArguments clang_args;
    DifferentialArguments tier0_args;
    DifferentialArguments tier1_args;
    DifferentialArguments csmith_args;
    DifferentialArguments yarpgen_args;
    uint64_t seed;
    uint64_t count;
    uint64_t output_limit;
    uint64_t source_limit;
    uint32_t timeout_ms;
    uint32_t generator_mask;
    bool tier0_default;
    bool tier1;
    bool generate_only;
    bool interesting;
    bool fixture;
};

typedef struct DifferentialProcess DifferentialProcess;
struct DifferentialProcess
{
    // 0 success, 1 nonzero exit, 2 signal, 3 timeout, 4 output limit, 5 system error, 6 interrupted.
    uint32_t kind;
    int exit_code;
    int signal_number;
    int error_number;
    uint64_t elapsed_ms;
    uint64_t output_bytes[2];
    bool truncated[2];
    bool cleanup_complete;
};

typedef struct DifferentialSummary DifferentialSummary;
struct DifferentialSummary
{
    char directory[PATH_MAX];
    char last_case[PATH_MAX];
    char signature[128];
    uint64_t generated;
    uint64_t passed;
    uint64_t failed;
    uint64_t generated_by_generator[2];
    uint64_t passed_by_generator[2];
    uint64_t failed_by_generator[2];
    bool unavailable;
    bool reference_accepted;
};

static volatile sig_atomic_t differential_stop_signal;

static void differential_signal(int signal_number)
{
    differential_stop_signal = signal_number;
}

static uint64_t differential_milliseconds(void)
{
    struct timespec value = {0};
    const bool valid = clock_gettime(CLOCK_MONOTONIC, &value) == 0;
    const uint64_t result = valid ? (uint64_t)value.tv_sec * 1000 + (uint64_t)value.tv_nsec / 1000000 : 0;
    return result;
}

static bool differential_write(int descriptor, const void* bytes, size_t size)
{
    const char* data = bytes;
    size_t done = 0;
    bool valid = true;
    while (valid && done < size)
    {
        const ssize_t amount = write(descriptor, data + done, size - done);
        if (amount > 0)
        {
            done += (size_t)amount;
        }
        else if (amount == 0 || errno != EINTR)
        {
            valid = false;
        }
    }
    return valid;
}

static bool differential_pipe(int descriptors[2], int flags)
{
    bool valid = syscall(SYS_pipe2, descriptors, flags) == 0;
    for (unsigned i = 0; valid && i < 2; ++i)
    {
        if (descriptors[i] < 3)
        {
            const int original = descriptors[i];
            descriptors[i] = fcntl(original, F_DUPFD_CLOEXEC, 3);
            valid = close(original) == 0 && descriptors[i] >= 0;
        }
    }
    return valid;
}

static bool differential_path(char* destination, size_t capacity, const char* directory, const char* name)
{
    const int size = snprintf(destination, capacity, "%s/%s", directory, name);
    const bool valid = size >= 0 && (size_t)size < capacity;
    return valid;
}

static bool differential_text_file(const char* directory, const char* name, const char* text)
{
    char path[PATH_MAX];
    bool valid = differential_path(path, sizeof(path), directory, name);
    const int descriptor = valid ? open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    valid = descriptor >= 0 && differential_write(descriptor, text, strlen(text));
    if (descriptor >= 0)
    {
        valid = close(descriptor) == 0 && valid;
    }
    return valid;
}

static bool differential_add(DifferentialArguments* arguments, const char* argument)
{
    const bool valid = arguments->count + 1 < BUSTER_DIFFERENTIAL_ARGS;
    if (valid)
    {
        arguments->values[arguments->count++] = (char*)argument;
        arguments->values[arguments->count] = 0;
    }
    return valid;
}

static bool differential_append(DifferentialArguments* arguments, const DifferentialArguments* extra)
{
    bool valid = true;
    for (uint32_t i = 0; valid && i < extra->count; ++i)
    {
        valid = differential_add(arguments, extra->values[i]);
    }
    return valid;
}

static unsigned differential_utf8_width(const unsigned char* bytes)
{
    unsigned width = bytes[0] >= 0xc2 && bytes[0] <= 0xdf ? 2 : bytes[0] >= 0xe0 && bytes[0] <= 0xef ? 3 : bytes[0] >= 0xf0 && bytes[0] <= 0xf4 ? 4 : 0;
    uint32_t value = width ? bytes[0] & (0x7fu >> width) : 0;
    bool valid = width != 0;
    for (unsigned i = 1; valid && i < width; ++i)
    {
        // A zero byte ends the string, so no later continuation byte may be read after one fails this check.
        valid = (bytes[i] & 0xc0) == 0x80;
        value = (value << 6) | (bytes[i] & 0x3f);
    }
    const uint32_t minimum = width == 2 ? 0x80 : width == 3 ? 0x800 : 0x10000;
    valid = valid && value >= minimum && value <= 0x10ffff && (value < 0xd800 || value > 0xdfff);
    width = valid ? width : 0;
    return width;
}

static bool differential_json_string(FILE* file, const char* string)
{
    bool valid = fputc('"', file) != EOF;
    for (const unsigned char* cursor = (const unsigned char*)string; valid && *cursor;)
    {
        unsigned width = 1;
        if (*cursor == '"' || *cursor == '\\')
        {
            valid = fputc('\\', file) != EOF && fputc(*cursor, file) != EOF;
        }
        else if (*cursor < 32 || *cursor == 127)
        {
            valid = fprintf(file, "\\u%04x", *cursor) > 0;
        }
        else if (*cursor >= 128)
        {
            const unsigned encoded = differential_utf8_width(cursor);
            if (encoded)
            {
                width = encoded;
                valid = fwrite(cursor, 1, width, file) == width;
            }
            else
            {
                // JSON text remains valid Unicode. Command hex fields and replay.args preserve the original byte.
                valid = fputs("\\ufffd", file) >= 0;
            }
        }
        else
        {
            valid = fputc(*cursor, file) != EOF;
        }
        cursor += width;
    }
    valid = fputc('"', file) != EOF && valid;
    return valid;
}

static bool differential_json_hex(FILE* file, const char* string)
{
    static const char digits[] = "0123456789abcdef";
    bool valid = fputc('"', file) != EOF;
    for (const unsigned char* cursor = (const unsigned char*)string; valid && *cursor; ++cursor)
    {
        valid = fputc(digits[*cursor >> 4], file) != EOF && fputc(digits[*cursor & 15], file) != EOF;
    }
    valid = fputc('"', file) != EOF && valid;
    return valid;
}

static FILE* differential_new_file(const char* directory, const char* name)
{
    char path[PATH_MAX];
    const bool valid = differential_path(path, sizeof(path), directory, name);
    const int descriptor = valid ? open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    FILE* result = descriptor >= 0 ? fdopen(descriptor, "w") : 0;
    if (descriptor >= 0 && !result)
    {
        close(descriptor);
    }
    return result;
}

// Kill the command's original group and every child adopted by this private subreaper. Killing adopted parents
// exposes their surviving children on the next pass, including children that used setsid/setpgid. The proc file
// contains only this supervisor's direct children. The parent compiler's unrelated processes are never inspected.
static bool differential_reap_tree(pid_t command, uint64_t deadline)
{
    bool complete = false;
    kill(-command, SIGKILL);
    kill(command, SIGKILL);
    while (!complete && differential_milliseconds() < deadline)
    {
        int status = 0;
        pid_t child = waitpid(-1, &status, WNOHANG);
        while (child > 0)
        {
            child = waitpid(-1, &status, WNOHANG);
        }
        complete = child < 0 && errno == ECHILD;
        if (!complete)
        {
            char path[128];
            const int length = snprintf(path, sizeof(path), "/proc/self/task/%ld/children", (long)getpid());
            FILE* file = length > 0 && (size_t)length < sizeof(path) ? fopen(path, "r") : 0;
            if (file)
            {
                long adopted = 0;
                while (fscanf(file, "%ld", &adopted) == 1)
                {
                    if (adopted > 0 && adopted <= INT_MAX)
                    {
                        kill((pid_t)adopted, SIGKILL);
                    }
                }
                fclose(file);
            }
            poll(0, 0, 2);
        }
    }
    return complete;
}

static void differential_capture(int input, int output, uint64_t limit, DifferentialProcess* result, unsigned stream, bool* open_stream)
{
    unsigned char bytes[BUSTER_DIFFERENTIAL_BUFFER];
    // A noisy child must not keep us in read() forever and starve deadlines or its other output stream.
    for (unsigned burst = 0; *open_stream && burst < 8; ++burst)
    {
        const ssize_t amount = read(input, bytes, sizeof(bytes));
        if (amount > 0)
        {
            const uint64_t previous = result->output_bytes[stream];
            const uint64_t available = previous < limit ? limit - previous : 0;
            const size_t retained = available < (uint64_t)amount ? (size_t)available : (size_t)amount;
            result->output_bytes[stream] += (uint64_t)amount;
            if (retained && !differential_write(output, bytes, retained))
            {
                result->kind = 5;
                result->error_number = errno;
            }
            if ((uint64_t)amount > available)
            {
                result->truncated[stream] = true;
                if (result->kind == 0)
                {
                    result->kind = 4;
                }
            }
        }
        else if (amount == 0)
        {
            *open_stream = false;
        }
        else if (errno != EINTR && errno != EAGAIN)
        {
            result->kind = 5;
            result->error_number = errno;
            *open_stream = false;
        }
        else
        {
            break;
        }
    }
}

static DifferentialProcess differential_supervise(const char* directory, char* const* arguments, int captures[2], uint32_t timeout_ms, const uint64_t output_limits[2], uint64_t file_limit)
{
    DifferentialProcess result = {0};
    const uint64_t start = differential_milliseconds();
    int pipes[2][2] = {{-1, -1}, {-1, -1}};
    int execution[2] = {-1, -1};
    bool valid = start != 0 && prctl(PR_SET_CHILD_SUBREAPER, 1) == 0;
    struct sigaction action = {0};
    action.sa_handler = differential_signal;
    sigemptyset(&action.sa_mask);
    differential_stop_signal = 0;
    valid = valid && sigaction(SIGTERM, &action, 0) == 0 && sigaction(SIGINT, &action, 0) == 0 && sigaction(SIGHUP, &action, 0) == 0;
    // The command is also cleaned up if the harness itself is killed or its parent disappears.
    valid = valid && prctl(PR_SET_PDEATHSIG, SIGTERM) == 0;
    if (getppid() == 1)
    {
        differential_stop_signal = SIGTERM;
    }
    sigset_t empty;
    sigemptyset(&empty);
    valid = valid && sigprocmask(SIG_SETMASK, &empty, 0) == 0;
    struct sigaction child_action = {0};
    child_action.sa_handler = SIG_DFL;
    sigemptyset(&child_action.sa_mask);
    valid = valid && sigaction(SIGCHLD, &child_action, 0) == 0;
    for (unsigned i = 0; valid && i < 2; ++i)
    {
        valid = differential_pipe(pipes[i], O_CLOEXEC) && fcntl(pipes[i][0], F_SETFL, O_NONBLOCK) == 0;
    }
    valid = valid && differential_pipe(execution, O_CLOEXEC | O_NONBLOCK);
    pid_t command = valid ? fork() : -1;
    if (command == 0)
    {
        int failure = 0;
        int null_input = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (null_input >= 0 && null_input < 3)
        {
            const int original = null_input;
            null_input = fcntl(original, F_DUPFD_CLOEXEC, 3);
            if (close(original) != 0)
            {
                null_input = -1;
            }
        }
        bool child_valid = setpgid(0, 0) == 0 && chdir(directory) == 0 && null_input >= 0;
        child_valid = child_valid && dup2(null_input, STDIN_FILENO) >= 0 && dup2(pipes[0][1], STDOUT_FILENO) >= 0 && dup2(pipes[1][1], STDERR_FILENO) >= 0;
        const struct rlimit core = {0};
        const struct rlimit files = {.rlim_cur = file_limit, .rlim_max = file_limit};
        child_valid = child_valid && setrlimit(RLIMIT_CORE, &core) == 0 && setrlimit(RLIMIT_FSIZE, &files) == 0;
        child_valid = child_valid && sigaction(SIGTERM, &child_action, 0) == 0 && sigaction(SIGINT, &child_action, 0) == 0 && sigaction(SIGHUP, &child_action, 0) == 0 && sigaction(SIGPIPE, &child_action, 0) == 0 && sigaction(SIGXFSZ, &child_action, 0) == 0;
        // CLOEXEC closes the supervisor's result/capture/pipe descriptors without an input-dependent fd scan.
        child_valid = child_valid && syscall(SYS_close_range, 3u, ~0u, 4u) == 0;
        if (child_valid)
        {
            execv(arguments[0], arguments);
        }
        failure = errno ? errno : EIO;
        differential_write(execution[1], &failure, sizeof(failure));
        _exit(127);
    }
    valid = valid && command > 0;
    for (unsigned i = 0; i < 2; ++i)
    {
        if (pipes[i][1] >= 0)
        {
            close(pipes[i][1]);
        }
    }
    if (execution[1] >= 0)
    {
        close(execution[1]);
    }
    if (!valid)
    {
        result.kind = 5;
        result.error_number = errno ? errno : EIO;
    }
    bool streams[2] = {true, true};
    bool ended = false;
    while (valid && !ended && result.kind == 0)
    {
        struct pollfd descriptors[2] = {{.fd = pipes[0][0], .events = POLLIN}, {.fd = pipes[1][0], .events = POLLIN}};
        const int ready = poll(descriptors, 2, 5);
        if (ready < 0 && errno != EINTR)
        {
            result.kind = 5;
            result.error_number = errno;
        }
        for (unsigned i = 0; i < 2; ++i)
        {
            differential_capture(pipes[i][0], captures[i], output_limits[i], &result, i, &streams[i]);
        }
        const uint64_t now = differential_milliseconds();
        if (now == 0)
        {
            result.kind = 5;
            result.error_number = EIO;
        }
        // The supervisor may resume past the deadline after the command already exited, when concurrent test
        // load delays this process rather than the command. Reap first: a finished command is its exit status,
        // however late the check runs. Only a command still running at the deadline has timed out.
        siginfo_t information = {0};
        const int waited = waitid(P_PID, (id_t)command, &information, WEXITED | WNOHANG | WNOWAIT);
        ended = waited == 0 && information.si_pid == command;
        if (ended)
        {
            result.exit_code = information.si_code == CLD_EXITED ? information.si_status : 0;
            result.signal_number = information.si_code == CLD_EXITED ? 0 : information.si_status;
        }
        if (waited < 0 && errno != EINTR)
        {
            result.kind = 5;
            result.error_number = errno;
        }
        else if (!ended && now != 0 && now - start >= timeout_ms && result.kind == 0)
        {
            result.kind = 3;
        }
        if (differential_stop_signal)
        {
            result.kind = 6;
            result.signal_number = differential_stop_signal;
        }
    }
    if (command > 0)
    {
        result.cleanup_complete = differential_reap_tree(command, differential_milliseconds() + BUSTER_DIFFERENTIAL_CLEANUP_MS);
        if (!result.cleanup_complete)
        {
            result.kind = 5;
            result.error_number = EBUSY;
        }
        if (ended)
        {
            if (result.kind == 0 && result.signal_number)
            {
                result.kind = 2;
            }
            else if (result.kind == 0 && result.exit_code)
            {
                result.kind = 1;
            }
        }
        for (unsigned i = 0; i < 2; ++i)
        {
            while (streams[i])
            {
                differential_capture(pipes[i][0], captures[i], output_limits[i], &result, i, &streams[i]);
                if (streams[i] && !result.cleanup_complete)
                {
                    break;
                }
            }
        }
        int failure = 0;
        if (read(execution[0], &failure, sizeof(failure)) == sizeof(failure))
        {
            result.kind = 5;
            result.error_number = failure;
        }
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        if (pipes[i][0] >= 0)
        {
            close(pipes[i][0]);
        }
    }
    if (execution[0] >= 0)
    {
        close(execution[0]);
    }
    result.elapsed_ms = differential_milliseconds() - start;
    return result;
}

static const char* differential_process_name(uint32_t kind)
{
    static const char* const names[] = {"ok", "exit", "signal", "timeout", "output-limit", "system-error", "interrupted"};
    const char* result = kind < sizeof(names) / sizeof(names[0]) ? names[kind] : "invalid";
    return result;
}

static DifferentialProcess differential_process(const char* directory, const char* label, const DifferentialArguments* arguments, uint32_t timeout_ms, uint64_t output_limit, uint64_t diagnostic_limit, uint64_t file_limit)
{
    DifferentialProcess result = {.kind = 5};
    char name[128];
    char path[PATH_MAX];
    int captures[2] = {-1, -1};
    int report[2] = {-1, -1};
    bool valid = arguments->count > 0;
    for (unsigned i = 0; valid && i < 2; ++i)
    {
        const int length = snprintf(name, sizeof(name), "%s.%s", label, i == 0 ? "stdout" : "stderr");
        valid = length > 0 && (size_t)length < sizeof(name) && differential_path(path, sizeof(path), directory, name);
        captures[i] = valid ? open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
        valid = captures[i] >= 0;
        if (valid && captures[i] < 3)
        {
            const int original = captures[i];
            captures[i] = fcntl(original, F_DUPFD_CLOEXEC, 3);
            valid = close(original) == 0 && captures[i] >= 0;
        }
    }
    valid = valid && differential_pipe(report, O_CLOEXEC);
    const pid_t supervisor = valid ? fork() : -1;
    if (supervisor == 0)
    {
        close(report[0]);
        const uint64_t output_limits[2] = {output_limit, diagnostic_limit};
        result = differential_supervise(directory, arguments->values, captures, timeout_ms, output_limits, file_limit);
        const bool sent = differential_write(report[1], &result, sizeof(result));
        _exit(sent ? 0 : 1);
    }
    valid = valid && supervisor > 0;
    if (report[1] >= 0)
    {
        close(report[1]);
    }
    size_t received = 0;
    while (valid && received < sizeof(result))
    {
        const ssize_t amount = read(report[0], (char*)&result + received, sizeof(result) - received);
        if (amount > 0)
        {
            received += (size_t)amount;
        }
        else if (amount == 0 || errno != EINTR)
        {
            valid = false;
        }
    }
    if (supervisor > 0)
    {
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(supervisor, &status, 0);
        } while (waited < 0 && errno == EINTR);
        valid = waited == supervisor && WIFEXITED(status) && WEXITSTATUS(status) == 0 && valid;
    }
    if (!valid)
    {
        result.kind = 5;
        result.error_number = errno ? errno : EIO;
    }
    if (report[0] >= 0)
    {
        close(report[0]);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        if (captures[i] >= 0 && close(captures[i]) != 0)
        {
            result.kind = 5;
            result.error_number = errno;
        }
    }
    const int length = snprintf(name, sizeof(name), "%s.json", label);
    FILE* file = length > 0 && (size_t)length < sizeof(name) ? differential_new_file(directory, name) : 0;
    bool recorded = file != 0;
    if (file)
    {
        recorded = fputs("{\"cwd\":", file) >= 0 && differential_json_string(file, directory) && fputs(",\"argv\":[", file) >= 0;
        for (uint32_t i = 0; i < arguments->count; ++i)
        {
            recorded = (i == 0 || fputc(',', file) != EOF) && differential_json_string(file, arguments->values[i]) && recorded;
        }
        recorded = fputs("],\"cwd_hex\":", file) >= 0 && differential_json_hex(file, directory) && fputs(",\"argv_hex\":[", file) >= 0 && recorded;
        for (uint32_t i = 0; i < arguments->count; ++i)
        {
            recorded = (i == 0 || fputc(',', file) != EOF) && differential_json_hex(file, arguments->values[i]) && recorded;
        }
        recorded = fprintf(file, "],\"timeout_ms\":%u,\"stdout_limit\":%llu,\"stderr_limit\":%llu,\"file_limit\":%llu,\"status\":\"%s\",\"exit_code\":%d,\"signal\":%d,\"errno\":%d,\"elapsed_ms\":%llu,\"stdout_bytes\":%llu,\"stderr_bytes\":%llu,\"stdout_truncated\":%s,\"stderr_truncated\":%s,\"cleanup_complete\":%s}\n", timeout_ms, (unsigned long long)output_limit, (unsigned long long)diagnostic_limit, (unsigned long long)file_limit, differential_process_name(result.kind), result.exit_code, result.signal_number, result.error_number, (unsigned long long)result.elapsed_ms, (unsigned long long)result.output_bytes[0], (unsigned long long)result.output_bytes[1], result.truncated[0] ? "true" : "false", result.truncated[1] ? "true" : "false", result.cleanup_complete ? "true" : "false") > 0 && recorded;
        recorded = fclose(file) == 0 && recorded;
    }
    if (!recorded)
    {
        result.kind = 5;
        result.error_number = errno ? errno : EIO;
    }
    return result;
}

static bool differential_resolve(const char* name, char result[PATH_MAX])
{
    bool valid = false;
    if (name && strchr(name, '/'))
    {
        valid = realpath(name, result) != 0 && access(result, X_OK) == 0;
    }
    else if (name && *name)
    {
        const char* path = getenv("PATH");
        path = path ? path : "/usr/bin:/bin";
        const char* cursor = path;
        while (!valid && cursor)
        {
            const char* end = strchr(cursor, ':');
            const size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
            char candidate[PATH_MAX];
            const int size = length < sizeof(candidate) ? snprintf(candidate, sizeof(candidate), "%.*s%s%s", (int)length, cursor, length ? "/" : "./", name) : -1;
            valid = size > 0 && (size_t)size < sizeof(candidate) && access(candidate, X_OK) == 0 && realpath(candidate, result) != 0;
            cursor = end ? end + 1 : 0;
        }
    }
    return valid;
}

static bool differential_copy(const char* source, const char* destination, uint64_t limit)
{
    const int input = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat metadata;
    bool valid = input >= 0 && fstat(input, &metadata) == 0 && S_ISREG(metadata.st_mode) && metadata.st_size > 0 && (uint64_t)metadata.st_size <= limit;
    const int output = valid ? open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    valid = valid && output >= 0;
    uint64_t total = 0;
    bool ended = false;
    while (valid && !ended)
    {
        char bytes[BUSTER_DIFFERENTIAL_BUFFER];
        const ssize_t amount = read(input, bytes, sizeof(bytes));
        if (amount > 0)
        {
            total += (uint64_t)amount;
            valid = total <= limit && differential_write(output, bytes, (size_t)amount);
        }
        else if (amount == 0)
        {
            ended = true;
        }
        else if (errno != EINTR)
        {
            valid = false;
        }
    }
    if (input >= 0)
    {
        valid = close(input) == 0 && valid;
    }
    if (output >= 0)
    {
        valid = close(output) == 0 && valid;
    }
    return valid;
}

static bool differential_fingerprint(FILE* file, const char* path)
{
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    uint64_t size = 0;
    uint64_t hash = 14695981039346656037ull;
    bool valid = descriptor >= 0;
    bool ended = false;
    while (valid && !ended)
    {
        unsigned char bytes[BUSTER_DIFFERENTIAL_BUFFER];
        const ssize_t amount = read(descriptor, bytes, sizeof(bytes));
        if (amount > 0)
        {
            size += (uint64_t)amount;
            for (ssize_t i = 0; i < amount; ++i)
            {
                hash = (hash ^ bytes[i]) * 1099511628211ull;
            }
        }
        else if (amount == 0)
        {
            ended = true;
        }
        else if (errno != EINTR)
        {
            valid = false;
        }
    }
    if (descriptor >= 0)
    {
        valid = close(descriptor) == 0 && valid;
    }
    valid = fputs("{\"path\":", file) >= 0 && differential_json_string(file, path) && fprintf(file, ",\"readable\":%s,\"bytes\":%llu,\"fnv1a64\":\"%016llx\"}", valid ? "true" : "false", (unsigned long long)size, (unsigned long long)hash) > 0 && valid;
    return valid;
}

static bool differential_contains(const char* directory, const char* name, const char* needle)
{
    char path[PATH_MAX];
    bool result = false;
    if (differential_path(path, sizeof(path), directory, name))
    {
        FILE* file = fopen(path, "r");
        if (file)
        {
            char buffer[BUSTER_DIFFERENTIAL_BUFFER];
            const size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
            buffer[length] = 0;
            result = !ferror(file) && strstr(buffer, needle) != 0;
            fclose(file);
        }
    }
    return result;
}

static bool differential_equal_files(const char* directory, const char* first, const char* second)
{
    char paths[2][PATH_MAX];
    bool valid = differential_path(paths[0], sizeof(paths[0]), directory, first) && differential_path(paths[1], sizeof(paths[1]), directory, second);
    FILE* files[2] = {valid ? fopen(paths[0], "rb") : 0, valid ? fopen(paths[1], "rb") : 0};
    valid = valid && files[0] && files[1];
    bool ended = false;
    while (valid && !ended)
    {
        char bytes[2][BUSTER_DIFFERENTIAL_BUFFER];
        const size_t left = fread(bytes[0], 1, sizeof(bytes[0]), files[0]);
        const size_t right = fread(bytes[1], 1, sizeof(bytes[1]), files[1]);
        valid = left == right && memcmp(bytes[0], bytes[1], left) == 0 && !ferror(files[0]) && !ferror(files[1]);
        ended = left < sizeof(bytes[0]);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        if (files[i])
        {
            valid = fclose(files[i]) == 0 && valid;
        }
    }
    return valid;
}

static DifferentialOptions differential_defaults(void)
{
    DifferentialOptions result =
    {
        .output = "differential-results",
        .clang = "clang",
        .csmith = "csmith",
        .yarpgen = "yarpgen",
        .revision = "unspecified",
        .generator_revision = "unspecified",
        .seed = 1,
        .count = 1,
        .output_limit = 1ull << 20,
        .source_limit = 16ull << 20,
        .timeout_ms = BUSTER_DIFFERENTIAL_TIMEOUT_MS,
        .generator_mask = 3,
        .tier1 = true,
    };
    return result;
}

static bool differential_unsigned(const char* text, uint64_t maximum, uint64_t* result)
{
    uint64_t value = 0;
    bool valid = text && *text;
    for (const unsigned char* p = (const unsigned char*)text; valid && *p; ++p)
    {
        const uint32_t digit = (uint32_t)*p - '0';
        valid = digit <= 9 && digit <= maximum && value <= (maximum - digit) / 10;
        if (valid)
        {
            value = value * 10 + digit;
        }
    }
    if (valid)
    {
        *result = value;
    }
    return valid;
}

static bool differential_options(int argc, char** argv, DifferentialOptions* options)
{
    bool valid = true;
    for (int i = 0; valid && i < argc; ++i)
    {
        const char* name = argv[i];
        if (strcmp(name, "--generate-only") == 0)
        {
            options->generate_only = true;
        }
        else if (strcmp(name, "--tier0-default") == 0)
        {
            options->tier0_default = true;
        }
        else if (i + 1 >= argc)
        {
            valid = false;
        }
        else
        {
            const char* value = argv[++i];
            uint64_t number = 0;
            DifferentialArguments* extra = 0;
            if (strcmp(name, "--output") == 0)
            {
                options->output = value;
            }
            else if (strcmp(name, "--buster") == 0)
            {
                options->buster = value;
            }
            else if (strcmp(name, "--clang") == 0)
            {
                options->clang = value;
            }
            else if (strcmp(name, "--csmith") == 0)
            {
                options->csmith = value;
            }
            else if (strcmp(name, "--yarpgen") == 0)
            {
                options->yarpgen = value;
            }
            else if (strcmp(name, "--csmith-include") == 0)
            {
                options->csmith_include = value;
            }
            else if (strcmp(name, "--revision") == 0)
            {
                options->revision = value;
            }
            else if (strcmp(name, "--generator-revision") == 0)
            {
                options->generator_revision = value;
            }
            else if (strcmp(name, "--source-directory") == 0)
            {
                options->source_directory = value;
            }
            else if (strcmp(name, "--replay") == 0 || strcmp(name, "--interesting") == 0)
            {
                options->replay = value;
                options->interesting = strcmp(name, "--interesting") == 0;
            }
            else if (strcmp(name, "--generator") == 0)
            {
                options->generator_mask = strcmp(value, "csmith") == 0 ? 1 : strcmp(value, "yarpgen") == 0 ? 2 : strcmp(value, "both") == 0 ? 3 : 0;
                valid = options->generator_mask != 0;
            }
            else if (strcmp(name, "--tiers") == 0)
            {
                options->tier1 = strcmp(value, "both") == 0;
                valid = options->tier1 || strcmp(value, "0") == 0;
            }
            else if (strcmp(name, "--seed") == 0 || strcmp(name, "--count") == 0)
            {
                valid = differential_unsigned(value, strcmp(name, "--count") == 0 ? 1000000 : UINT64_MAX, &number) && number != 0;
                if (strcmp(name, "--seed") == 0)
                {
                    options->seed = number;
                }
                else
                {
                    options->count = number;
                }
            }
            else if (strcmp(name, "--timeout-ms") == 0)
            {
                valid = differential_unsigned(value, 3600000, &number) && number != 0;
                options->timeout_ms = (uint32_t)number;
            }
            else if (strcmp(name, "--output-limit") == 0 || strcmp(name, "--source-limit") == 0)
            {
                valid = differential_unsigned(value, BUSTER_DIFFERENTIAL_MAX_BYTES, &number) && number != 0;
                if (strcmp(name, "--output-limit") == 0)
                {
                    options->output_limit = number;
                }
                else
                {
                    options->source_limit = number;
                }
            }
            else if (strcmp(name, "--buster-arg") == 0)
            {
                extra = &options->buster_args;
            }
            else if (strcmp(name, "--clang-arg") == 0)
            {
                extra = &options->clang_args;
            }
            else if (strcmp(name, "--tier0-arg") == 0)
            {
                extra = &options->tier0_args;
            }
            else if (strcmp(name, "--tier1-arg") == 0)
            {
                extra = &options->tier1_args;
            }
            else if (strcmp(name, "--csmith-arg") == 0)
            {
                extra = &options->csmith_args;
            }
            else if (strcmp(name, "--yarpgen-arg") == 0)
            {
                extra = &options->yarpgen_args;
            }
            else
            {
                valid = false;
            }
            if (extra)
            {
                valid = extra->count < BUSTER_DIFFERENTIAL_EXTRA_ARGS && differential_add(extra, value);
            }
        }
    }
    valid = valid && options->count - 1 <= UINT64_MAX - options->seed;
    return valid;
}

static bool differential_config_token(FILE* file, size_t* size, const char* text, bool terminated)
{
    const size_t length = strlen(text) + terminated;
    // The loader needs a byte beyond the serialized data to distinguish complete input from its size limit.
    bool valid = length < BUSTER_DIFFERENTIAL_CONFIG_BYTES - *size;
    if (valid)
    {
        *size += length;
        valid = !file || fwrite(text, 1, length, file) == length;
    }
    return valid;
}

static bool differential_config_pair(FILE* file, size_t* size, const char* option, const char* value)
{
    const bool valid = differential_config_token(file, size, option, true) && differential_config_token(file, size, value, true);
    return valid;
}

// A null file validates exactly the same bytes that saving writes, including all custom arguments and terminators.
static bool differential_config_contents(FILE* file, const DifferentialOptions* options, unsigned generator, uint64_t seed, size_t* size)
{
    *size = 0;
    bool valid = differential_config_token(file, size, "BUSTER_DIFFERENTIAL_V1\n", false);
    const char* const names[] = {"--buster", "--clang", "--csmith", "--yarpgen", "--csmith-include", "--revision", "--generator-revision"};
    const char* const values[] = {options->buster, options->clang, options->csmith, options->yarpgen, options->csmith_include, options->revision, options->generator_revision};
    for (unsigned i = 0; valid && i < sizeof(names) / sizeof(names[0]); ++i)
    {
        if (values[i])
        {
            valid = differential_config_pair(file, size, names[i], values[i]);
        }
    }
    const char* const extra_names[] = {"--buster-arg", "--clang-arg", "--tier0-arg", "--tier1-arg", "--csmith-arg", "--yarpgen-arg"};
    const DifferentialArguments* const extra[] = {&options->buster_args, &options->clang_args, &options->tier0_args, &options->tier1_args, &options->csmith_args, &options->yarpgen_args};
    for (unsigned i = 0; valid && i < sizeof(extra) / sizeof(extra[0]); ++i)
    {
        for (uint32_t j = 0; valid && j < extra[i]->count; ++j)
        {
            valid = differential_config_pair(file, size, extra_names[i], extra[i]->values[j]);
        }
    }
    const char* const numeric_names[] = {"--seed", "--timeout-ms", "--output-limit", "--source-limit"};
    const uint64_t numeric_values[] = {seed, options->timeout_ms, options->output_limit, options->source_limit};
    for (unsigned i = 0; valid && i < sizeof(numeric_values) / sizeof(numeric_values[0]); ++i)
    {
        char value[32];
        snprintf(value, sizeof(value), "%llu", (unsigned long long)numeric_values[i]);
        valid = differential_config_pair(file, size, numeric_names[i], value);
    }
    valid = valid && differential_config_pair(file, size, "--generator", generator == 0 ? "csmith" : "yarpgen") && differential_config_pair(file, size, "--tiers", options->tier1 ? "both" : "0");
    if (valid && options->tier0_default)
    {
        valid = differential_config_token(file, size, "--tier0-default", true);
    }
    return valid;
}

static bool differential_config_valid(const DifferentialOptions* options)
{
    bool valid = options->count != 0 && options->count - 1 <= UINT64_MAX - options->seed;
    for (unsigned generator = 0; valid && generator < 2; ++generator)
    {
        if (options->generator_mask & (1u << generator))
        {
            size_t size = 0;
            valid = differential_config_contents(0, options, generator, options->seed + options->count - 1, &size);
        }
    }
    return valid;
}

static bool differential_save_config(const char* directory, const DifferentialOptions* options, unsigned generator, uint64_t seed)
{
    size_t size = 0;
    bool valid = differential_config_contents(0, options, generator, seed, &size);
    FILE* file = valid ? differential_new_file(directory, "replay.args") : 0;
    valid = valid && file;
    if (file)
    {
        valid = differential_config_contents(file, options, generator, seed, &size);
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

static bool differential_load_config(const char* directory, char* storage, size_t capacity, DifferentialOptions* options)
{
    char path[PATH_MAX];
    bool valid = differential_path(path, sizeof(path), directory, "replay.args");
    FILE* file = valid ? fopen(path, "rb") : 0;
    valid = valid && file;
    if (file)
    {
        const size_t length = fread(storage, 1, capacity, file);
        valid = !ferror(file) && feof(file) && length < capacity && length > sizeof("BUSTER_DIFFERENTIAL_V1\n") && memcmp(storage, "BUSTER_DIFFERENTIAL_V1\n", sizeof("BUSTER_DIFFERENTIAL_V1\n") - 1) == 0;
        const size_t start = sizeof("BUSTER_DIFFERENTIAL_V1\n") - 1;
        char* arguments[512];
        int count = 0;
        size_t offset = start;
        while (valid && offset < length)
        {
            char* end = memchr(storage + offset, 0, length - offset);
            valid = end && count < (int)(sizeof(arguments) / sizeof(arguments[0]));
            if (valid)
            {
                arguments[count++] = storage + offset;
                offset = (size_t)(end - storage) + 1;
            }
        }
        valid = fclose(file) == 0 && valid;
        valid = valid && differential_options(count, arguments, options);
    }
    return valid;
}

static bool differential_record_case(const char* directory, unsigned generator, uint64_t seed, const DifferentialOptions* options)
{
    FILE* file = differential_new_file(directory, "input.json");
    bool valid = file != 0;
    if (file)
    {
        valid = fprintf(file, "{\"generator\":\"%s\",\"seed\":\"%llu\",\"fixture\":%s,\"sources\":[", generator == 0 ? "csmith" : "yarpgen", (unsigned long long)seed, options->fixture ? "true" : "false") > 0;
        const char* const csmith_files[] = {"program.c"};
        const char* const yarpgen_files[] = {"driver.c", "func.c", "init.h"};
        const char* const* names = generator == 0 ? csmith_files : yarpgen_files;
        const unsigned count = generator == 0 ? 1 : 3;
        uint64_t total_size = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            char path[PATH_MAX];
            struct stat metadata;
            const bool present = differential_path(path, sizeof(path), directory, names[i]) && lstat(path, &metadata) == 0 && S_ISREG(metadata.st_mode) && metadata.st_size > 0 && (uint64_t)metadata.st_size <= options->source_limit;
            if (present)
            {
                total_size += (uint64_t)metadata.st_size;
            }
            bool recorded = i == 0 || fputc(',', file) != EOF;
            if (present)
            {
                recorded = differential_fingerprint(file, path) && recorded;
            }
            else
            {
                recorded = fputs("{\"name\":", file) >= 0 && differential_json_string(file, names[i]) && fputs(",\"valid\":false}", file) >= 0 && recorded;
            }
            valid = present && recorded && valid;
        }
        valid = fputs("]}\n", file) >= 0 && total_size <= options->source_limit && valid;
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

static bool differential_compile_arguments(DifferentialArguments* arguments, const DifferentialOptions* options, unsigned mode, unsigned generator, const char* executable, bool probe, bool sanitize)
{
    const bool reference = mode < 2;
    bool valid = differential_add(arguments, reference ? options->clang : options->buster);
    valid = valid && differential_append(arguments, reference ? &options->clang_args : &options->buster_args);
    if (reference)
    {
        valid = differential_add(arguments, mode == 0 ? "-O0" : "-O2") && valid;
    }
    else
    {
        valid = differential_append(arguments, mode == 2 ? &options->tier0_args : &options->tier1_args) && valid;
    }
    if (sanitize)
    {
        valid = differential_add(arguments, "-fsanitize=undefined") && differential_add(arguments, "-fno-sanitize-recover=all") && valid;
    }
    valid = differential_add(arguments, "-std=c11") && differential_add(arguments, "-o") && differential_add(arguments, executable) && valid;
    if (generator == 0 && options->csmith_include && !probe)
    {
        valid = differential_add(arguments, "-I") && differential_add(arguments, options->csmith_include) && valid;
    }
    if (probe)
    {
        valid = differential_add(arguments, "probe.c") && valid;
    }
    else if (generator == 0)
    {
        valid = differential_add(arguments, "program.c") && valid;
    }
    else
    {
        valid = differential_add(arguments, "driver.c") && differential_add(arguments, "func.c") && valid;
    }
    return valid;
}

static void differential_failure(char signature[128], const char* label, const char* status)
{
    if (signature[0] == 0)
    {
        snprintf(signature, 128, "%s:%s", label, status);
    }
}

static bool differential_buster_failure(const char* signature)
{
    static const char* const suffixes[] =
    {
        ":mismatch",
        "-compile:exit",
        "-compile:signal",
        "-compile:timeout",
        "-compile:output-limit",
        "-compile:no-executable",
        "-run:exit",
        "-run:signal",
        "-run:timeout",
        "-run:output-limit",
        "-run:empty-output",
    };
    const size_t prefix = sizeof("buster-tier0") - 1;
    const bool buster = strncmp(signature, "buster-tier0", prefix) == 0 || strncmp(signature, "buster-tier1", prefix) == 0;
    bool valid = false;
    for (unsigned i = 0; buster && !valid && i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
    {
        valid = strcmp(signature + prefix, suffixes[i]) == 0;
    }
    return valid;
}

static bool differential_interesting_result(int result, const DifferentialSummary* summary, const char* previous_signature)
{
    const bool valid = result == 1 && summary->generated == 1 && summary->failed == 1 && summary->reference_accepted && differential_buster_failure(previous_signature) && strcmp(summary->signature, previous_signature) == 0;
    return valid;
}

static bool differential_compilers(const char* directory, const DifferentialOptions* options, unsigned generator, bool probe, char signature[128], bool* references)
{
    static const char* const modes[] = {"clang-o0", "clang-o2", "buster-tier0", "buster-tier1"};
    bool successful[4] = {false};
    const unsigned count = options->tier1 ? 4 : 3;
    for (unsigned mode = 0; mode < count; ++mode)
    {
        char label[64];
        char executable[PATH_MAX];
        snprintf(label, sizeof(label), "%s.exe", modes[mode]);
        bool valid = differential_path(executable, sizeof(executable), directory, label);
        DifferentialArguments arguments = {0};
        valid = valid && differential_compile_arguments(&arguments, options, mode, generator, executable, probe, false);
        snprintf(label, sizeof(label), "%s-compile", modes[mode]);
        DifferentialProcess compiled = {.kind = 5};
        if (valid)
        {
            compiled = differential_process(directory, label, &arguments, options->timeout_ms, options->output_limit, options->output_limit, BUSTER_DIFFERENTIAL_MAX_BYTES);
        }
        if (compiled.kind != 0)
        {
            differential_failure(signature, label, differential_process_name(compiled.kind));
        }
        struct stat metadata;
        const bool emitted = valid && lstat(executable, &metadata) == 0 && S_ISREG(metadata.st_mode) && metadata.st_size > 0 && access(executable, X_OK) == 0;
        if (compiled.kind == 0 && !emitted)
        {
            differential_failure(signature, label, "no-executable");
        }
        if (compiled.kind == 0 && emitted)
        {
            DifferentialArguments run = {0};
            differential_add(&run, executable);
            snprintf(label, sizeof(label), "%s-run", modes[mode]);
            const DifferentialProcess executed = differential_process(directory, label, &run, options->timeout_ms, options->output_limit, options->output_limit, options->source_limit);
            successful[mode] = executed.kind == 0 && executed.output_bytes[0] > 0;
            if (executed.kind != 0)
            {
                differential_failure(signature, label, differential_process_name(executed.kind));
            }
            else if (executed.output_bytes[0] == 0)
            {
                differential_failure(signature, label, "empty-output");
            }
        }
    }
    bool valid = true;
    for (unsigned mode = 0; mode < count; ++mode)
    {
        valid = successful[mode] && valid;
    }
    if (references)
    {
        *references = successful[0] && successful[1] && differential_equal_files(directory, "clang-o0-run.stdout", "clang-o2-run.stdout");
    }
    if (valid)
    {
        for (unsigned mode = 1; mode < count; ++mode)
        {
            char output[64];
            snprintf(output, sizeof(output), "%s-run.stdout", modes[mode]);
            if (!differential_equal_files(directory, "clang-o0-run.stdout", output))
            {
                differential_failure(signature, modes[mode], "mismatch");
                valid = false;
            }
        }
    }
    return valid;
}

static bool differential_generate(const char* directory, const DifferentialOptions* options, unsigned generator, uint64_t seed, char signature[128])
{
    DifferentialArguments arguments = {0};
    bool valid = differential_add(&arguments, generator == 0 ? options->csmith : options->yarpgen);
    valid = valid && differential_append(&arguments, generator == 0 ? &options->csmith_args : &options->yarpgen_args);
    char seed_text[64];
    snprintf(seed_text, sizeof(seed_text), generator == 0 ? "%llu" : "--seed=%llu", (unsigned long long)seed);
    if (generator == 0)
    {
        valid = differential_add(&arguments, "--seed") && differential_add(&arguments, seed_text) && valid;
    }
    else
    {
        valid = differential_add(&arguments, "--std=c") && differential_add(&arguments, "--out-dir=.") && differential_add(&arguments, "--check-algo=hash") && differential_add(&arguments, seed_text) && valid;
    }
    DifferentialProcess generated = {.kind = 5};
    if (valid)
    {
        generated = differential_process(directory, "generate", &arguments, options->timeout_ms, generator == 0 ? options->source_limit : options->output_limit, options->output_limit, options->source_limit);
    }
    valid = generated.kind == 0;
    if (!valid)
    {
        differential_failure(signature, "generate", differential_process_name(generated.kind));
    }
    if (valid && generator == 0)
    {
        char source[PATH_MAX];
        char destination[PATH_MAX];
        valid = differential_path(source, sizeof(source), directory, "generate.stdout") && differential_path(destination, sizeof(destination), directory, "program.c") && differential_copy(source, destination, options->source_limit);
    }
    valid = valid && differential_record_case(directory, generator, seed, options);
    if (!valid)
    {
        differential_failure(signature, "generate", "invalid-sources");
    }
    return valid;
}

static bool differential_replay_sources(const char* source_directory, const char* directory, const DifferentialOptions* options, unsigned generator, char signature[128])
{
    static const char* const names[] = {"program.c", "driver.c", "func.c", "init.h"};
    const unsigned begin = generator == 0 ? 0 : 1;
    const unsigned end = generator == 0 ? 1 : 4;
    bool valid = true;
    for (unsigned i = begin; valid && i < end; ++i)
    {
        char source[PATH_MAX];
        char destination[PATH_MAX];
        valid = differential_path(source, sizeof(source), source_directory, names[i]) && differential_path(destination, sizeof(destination), directory, names[i]) && differential_copy(source, destination, options->source_limit);
    }
    valid = valid && differential_record_case(directory, generator, options->seed, options);
    if (!valid)
    {
        differential_failure(signature, "replay", "invalid-sources");
    }
    return valid;
}

static bool differential_case(const char* directory, const DifferentialOptions* options, unsigned generator, uint64_t seed, DifferentialSummary* summary)
{
    char signature[128] = {0};
    bool valid = differential_save_config(directory, options, generator, seed);
    if (!valid)
    {
        differential_failure(signature, "artifact", "config-write");
    }
    const bool generated = valid && (options->replay ? differential_replay_sources(options->source_directory, directory, options, generator, signature) : differential_generate(directory, options, generator, seed, signature));
    summary->generated += generated;
    summary->generated_by_generator[generator] += generated;
    valid = generated && valid;
    bool references = false;
    if (valid && !options->generate_only)
    {
        valid = differential_compilers(directory, options, generator, false, signature, &references);
    }
    // Reductions must keep the successful reference executions and their agreement. A second invalid candidate
    // that merely fails the same compiler is not interesting. UBSan is a useful filter, not a proof of defined C.
    if (options->interesting && signature[0])
    {
        char executable[PATH_MAX];
        DifferentialArguments arguments = {0};
        references = references && differential_path(executable, sizeof(executable), directory, "clang-ubsan.exe") && differential_compile_arguments(&arguments, options, 0, generator, executable, false, true);
        if (references)
        {
            DifferentialProcess checked = differential_process(directory, "clang-ubsan-compile", &arguments, options->timeout_ms, options->output_limit, options->output_limit, BUSTER_DIFFERENTIAL_MAX_BYTES);
            references = checked.kind == 0;
            if (references)
            {
                DifferentialArguments run = {0};
                differential_add(&run, executable);
                checked = differential_process(directory, "clang-ubsan-run", &run, options->timeout_ms, options->output_limit, options->output_limit, options->source_limit);
                references = checked.kind == 0 && checked.output_bytes[0] > 0 && differential_equal_files(directory, "clang-o0-run.stdout", "clang-ubsan-run.stdout");
            }
        }
        if (!references)
        {
            snprintf(signature, sizeof(signature), "reduction:reference-rejected");
        }
        summary->reference_accepted = references;
    }
    const bool passed = valid && !options->generate_only;
    const bool failed = !valid;
    summary->passed += passed;
    summary->failed += failed;
    summary->passed_by_generator[generator] += passed;
    summary->failed_by_generator[generator] += failed;
    if (summary->signature[0] == 0)
    {
        snprintf(summary->signature, sizeof(summary->signature), "%s", signature);
    }
    const bool recorded_signature = differential_text_file(directory, "failure.signature", signature);
    FILE* file = differential_new_file(directory, "result.json");
    bool recorded = file != 0 && recorded_signature;
    if (file)
    {
        recorded = fprintf(file, "{\"status\":\"%s\",\"campaign_pass\":%s,\"fixture\":%s,\"tiers\":\"%s\",\"reference_accepted\":%s,\"signature\":", failed ? "failed" : options->generate_only ? "generated-only" : "passed", passed && !options->fixture ? "true" : "false", options->fixture ? "true" : "false", options->tier1 ? "0,1" : "0", options->interesting && references ? "true" : "false") > 0 && differential_json_string(file, signature) && fputs("}\n", file) >= 0 && recorded;
        recorded = fclose(file) == 0 && recorded;
    }
    if (!recorded)
    {
        if (passed)
        {
            --summary->passed;
            --summary->passed_by_generator[generator];
        }
        summary->failed += !failed;
        summary->failed_by_generator[generator] += !failed;
        snprintf(summary->signature, sizeof(summary->signature), "artifact:result-write");
    }
    return recorded;
}

static bool differential_version(const char* directory, const char* label, const char* tool, const DifferentialOptions* options, bool buster)
{
    DifferentialArguments arguments = {0};
    differential_add(&arguments, tool);
    differential_add(&arguments, "--version");
    const DifferentialProcess version = differential_process(directory, label, &arguments, options->timeout_ms, options->output_limit, options->output_limit, options->source_limit);
    char name[128];
    snprintf(name, sizeof(name), "%s.stdout", label);
    const bool identifies_buster = !buster || options->fixture || differential_contains(directory, name, "buster") || differential_contains(directory, name, "Buster");
    const bool valid = version.kind == 0 && version.output_bytes[0] > 0 && identifies_buster;
    return valid;
}

static bool differential_provenance(const char* directory, const DifferentialOptions* options)
{
    FILE* file = differential_new_file(directory, "provenance.json");
    bool valid = file != 0;
    if (file)
    {
        struct utsname host = {0};
        const bool named = uname(&host) == 0;
        valid = fprintf(file, "{\"format\":1,\"fixture\":%s,\"mode\":\"%s\",\"host\":", options->fixture ? "true" : "false", options->generate_only ? "generate-only" : options->replay ? "replay" : "campaign") > 0;
        valid = differential_json_string(file, named ? host.nodename : "unavailable") && fputs(",\"machine\":", file) >= 0 && differential_json_string(file, named ? host.machine : "unavailable") && fputs(",\"revision\":", file) >= 0 && differential_json_string(file, options->revision) && fputs(",\"generator_revision\":", file) >= 0 && differential_json_string(file, options->generator_revision) && fputs(",\"csmith_include\":", file) >= 0 && differential_json_string(file, options->csmith_include ? options->csmith_include : "unspecified") && fputs(",\"tools\":[", file) >= 0 && valid;
        const char* tools[4] = {options->buster, options->clang, options->csmith, options->yarpgen};
        unsigned written = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            const bool enabled = (i < 2 && !options->generate_only) || (i >= 2 && !options->replay && (options->generator_mask & (1u << (i - 2))));
            if (enabled && tools[i])
            {
                valid = (written++ == 0 || fputc(',', file) != EOF) && differential_fingerprint(file, tools[i]) && valid;
            }
        }
        valid = fputs("]}\n", file) >= 0 && valid;
        valid = fclose(file) == 0 && valid;
    }
    // CPU identity is evidence, not a test-host approval decision. Keep the complete Linux CPU report externally.
    char cpu_path[PATH_MAX];
    if (differential_path(cpu_path, sizeof(cpu_path), directory, "cpuinfo.txt"))
    {
        const int input = open("/proc/cpuinfo", O_RDONLY | O_CLOEXEC);
        const int output = open(cpu_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        bool copied = input >= 0 && output >= 0;
        uint64_t size = 0;
        while (copied)
        {
            char buffer[BUSTER_DIFFERENTIAL_BUFFER];
            const ssize_t amount = read(input, buffer, sizeof(buffer));
            if (amount > 0)
            {
                size += (uint64_t)amount;
                copied = size <= BUSTER_DIFFERENTIAL_MAX_BYTES && differential_write(output, buffer, (size_t)amount);
            }
            else if (amount == 0)
            {
                break;
            }
            else if (errno != EINTR)
            {
                copied = false;
            }
        }
        if (input >= 0)
        {
            close(input);
        }
        if (output >= 0)
        {
            copied = close(output) == 0 && copied;
        }
        valid = copied && valid;
    }
    else
    {
        valid = false;
    }
    return valid;
}

static bool differential_request(const char* directory, const DifferentialOptions* options)
{
    FILE* file = differential_new_file(directory, "request.json");
    bool valid = file != 0;
    if (file)
    {
        char working_directory[PATH_MAX];
        const char* working = getcwd(working_directory, sizeof(working_directory));
        valid = fputs("{\"cwd\":", file) >= 0 && differential_json_string(file, working ? working : "unavailable");
        const char* const names[] = {"buster", "clang", "csmith", "yarpgen", "csmith_include", "revision", "generator_revision"};
        const char* const values[] = {options->buster, options->clang, options->csmith, options->yarpgen, options->csmith_include, options->revision, options->generator_revision};
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        {
            valid = fputc(',', file) != EOF && differential_json_string(file, names[i]) && fputc(':', file) != EOF && differential_json_string(file, values[i] ? values[i] : "unspecified") && valid;
        }
        const char* const argument_names[] = {"buster_args", "clang_args", "tier0_args", "tier1_args", "csmith_args", "yarpgen_args"};
        const DifferentialArguments* const arguments[] = {&options->buster_args, &options->clang_args, &options->tier0_args, &options->tier1_args, &options->csmith_args, &options->yarpgen_args};
        for (unsigned i = 0; i < sizeof(arguments) / sizeof(arguments[0]); ++i)
        {
            valid = fputc(',', file) != EOF && differential_json_string(file, argument_names[i]) && fputs(":[", file) >= 0 && valid;
            for (uint32_t j = 0; j < arguments[i]->count; ++j)
            {
                valid = (j == 0 || fputc(',', file) != EOF) && differential_json_string(file, arguments[i]->values[j]) && valid;
            }
            valid = fputc(']', file) != EOF && valid;
        }
        valid = fprintf(file, ",\"generator_mask\":%u,\"seed\":\"%llu\",\"count\":%llu,\"tiers\":\"%s\",\"tier0_default\":%s,\"generate_only\":%s,\"fixture\":%s,\"timeout_ms\":%u,\"output_limit\":%llu,\"source_limit\":%llu}\n", options->generator_mask, (unsigned long long)options->seed, (unsigned long long)options->count, options->tier1 ? "0,1" : "0", options->tier0_default ? "true" : "false", options->generate_only ? "true" : "false", options->fixture ? "true" : "false", options->timeout_ms, (unsigned long long)options->output_limit, (unsigned long long)options->source_limit) > 0 && valid;
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

static int differential_campaign(const DifferentialOptions* configuration, DifferentialSummary* summary)
{
    DifferentialOptions options = *configuration;
    bool valid = mkdir(options.output, 0700) == 0 || errno == EEXIST;
    char root[PATH_MAX];
    valid = valid && realpath(options.output, root) != 0 && differential_path(summary->directory, sizeof(summary->directory), root, options.replay ? "replay-XXXXXX" : "campaign-XXXXXX") && mkdtemp(summary->directory) != 0;
    if (!valid)
    {
        summary->failed = 1;
        snprintf(summary->signature, sizeof(summary->signature), "artifact:directory");
    }
    char paths[5][PATH_MAX];
    const char** tool_fields[] = {&options.buster, &options.clang, &options.csmith, &options.yarpgen};
    bool available = valid && differential_request(summary->directory, &options);
    if (valid && !available)
    {
        ++summary->failed;
        snprintf(summary->signature, sizeof(summary->signature), "artifact:request-write");
    }
    for (unsigned i = 0; available && i < 4; ++i)
    {
        const bool enabled = (i < 2 && !options.generate_only) || (i >= 2 && !options.replay && (options.generator_mask & (1u << (i - 2))));
        if (enabled)
        {
            available = differential_resolve(*tool_fields[i], paths[i]);
            if (available)
            {
                *tool_fields[i] = paths[i];
            }
        }
    }
    if (available && options.csmith_include)
    {
        available = realpath(options.csmith_include, paths[4]) != 0;
        if (available)
        {
            options.csmith_include = paths[4];
        }
    }
    if (!options.generate_only && available)
    {
        struct stat buster_metadata;
        struct stat clang_metadata;
        available = (options.tier0_default || options.tier0_args.count > 0) && (!options.tier1 || options.tier1_args.count > 0);
        available = available && stat(options.buster, &buster_metadata) == 0 && stat(options.clang, &clang_metadata) == 0;
        available = available && (options.fixture || buster_metadata.st_dev != clang_metadata.st_dev || buster_metadata.st_ino != clang_metadata.st_ino);
    }
    if (available && !differential_config_valid(&options))
    {
        available = false;
        snprintf(summary->signature, sizeof(summary->signature), "unavailable:configuration-too-large");
    }
    if (available)
    {
        available = differential_provenance(summary->directory, &options);
        if (!available)
        {
            ++summary->failed;
            snprintf(summary->signature, sizeof(summary->signature), "artifact:provenance-write");
        }
        if (!options.generate_only)
        {
            available = differential_version(summary->directory, "version-buster", options.buster, &options, true) && differential_version(summary->directory, "version-clang", options.clang, &options, false) && available;
        }
        for (unsigned generator = 0; available && !options.replay && generator < 2; ++generator)
        {
            if (options.generator_mask & (1u << generator))
            {
                available = differential_version(summary->directory, generator == 0 ? "version-csmith" : "version-yarpgen", generator == 0 ? options.csmith : options.yarpgen, &options, false);
            }
        }
    }
    if (available && !options.generate_only && !options.replay)
    {
        char probe[PATH_MAX];
        available = differential_path(probe, sizeof(probe), summary->directory, "preflight") && mkdir(probe, 0700) == 0;
        available = available && differential_text_file(probe, "probe.c", "#include <stdio.h>\nint main(void)\n{\n    puts(\"buster differential preflight\");\n    return 0;\n}\n");
        if (available)
        {
            available = differential_compilers(probe, &options, 0, true, summary->signature, 0);
        }
    }
    summary->unavailable = valid && (!available || options.generate_only);
    if (!available && valid && !summary->signature[0])
    {
        snprintf(summary->signature, sizeof(summary->signature), "unavailable:tools-or-tier-configuration");
    }
    for (unsigned generator = 0; available && generator < 2; ++generator)
    {
        if (options.generator_mask & (1u << generator))
        {
            for (uint64_t index = 0; available && index < options.count; ++index)
            {
                char name[64];
                const uint64_t seed = options.seed + index;
                snprintf(name, sizeof(name), "%s-%020llu", generator == 0 ? "csmith" : "yarpgen", (unsigned long long)seed);
                available = differential_path(summary->last_case, sizeof(summary->last_case), summary->directory, name) && mkdir(summary->last_case, 0700) == 0;
                if (available)
                {
                    available = differential_case(summary->last_case, &options, generator, seed, summary);
                }
                else
                {
                    ++summary->failed;
                    ++summary->failed_by_generator[generator];
                    differential_failure(summary->signature, "artifact", "case-directory");
                }
            }
        }
    }
    int result = summary->failed ? 1 : summary->unavailable ? 2 : 0;
    FILE* file = valid ? differential_new_file(summary->directory, "summary.json") : 0;
    if (file)
    {
        bool written = fprintf(file, "{\"status\":\"%s\",\"fixture\":%s,\"generated\":%llu,\"passed\":%llu,\"failed\":%llu,\"campaign_passed\":%llu,\"tier1_campaign_passed\":%llu,\"tiers\":\"%s\",\"first_seed\":\"%llu\",\"requested_per_generator\":%llu,\"signature\":", result == 0 ? "passed" : result == 1 ? "failed" : "unavailable", options.fixture ? "true" : "false", (unsigned long long)summary->generated, (unsigned long long)summary->passed, (unsigned long long)summary->failed, (unsigned long long)(options.fixture ? 0 : summary->passed), (unsigned long long)(options.fixture || !options.tier1 ? 0 : summary->passed), options.tier1 ? "0,1" : "0", (unsigned long long)options.seed, (unsigned long long)options.count) > 0 && differential_json_string(file, summary->signature) && fputs(",\"generators\":[", file) >= 0;
        for (unsigned generator = 0; generator < 2; ++generator)
        {
            written = fprintf(file, "%s{\"name\":\"%s\",\"selected\":%s,\"generated\":%llu,\"passed\":%llu,\"failed\":%llu}", generator ? "," : "", generator ? "yarpgen" : "csmith", (options.generator_mask & (1u << generator)) ? "true" : "false", (unsigned long long)summary->generated_by_generator[generator], (unsigned long long)summary->passed_by_generator[generator], (unsigned long long)summary->failed_by_generator[generator]) > 0 && written;
        }
        written = fputs("]}\n", file) >= 0 && written;
        if (fclose(file) != 0 || !written)
        {
            result = 1;
        }
    }
    else
    {
        result = 1;
    }
    return result;
}

static int run_differential_command(int argc, char** argv)
{
    DifferentialOptions options = differential_defaults();
    bool valid = differential_options(argc, argv, &options);
    const bool help = argc == 1 && strcmp(argv[0], "--help") == 0;
    int result = 2;
    char* replay_storage = 0;
    char source[PATH_MAX];
    char replay[PATH_MAX];
    char previous_signature[128] = {0};
    if (valid && options.replay)
    {
        const DifferentialOptions requested = options;
        replay_storage = malloc(BUSTER_DIFFERENTIAL_CONFIG_BYTES);
        valid = replay_storage && realpath(requested.replay, replay) != 0;
        options = differential_defaults();
        valid = valid && differential_load_config(replay, replay_storage, BUSTER_DIFFERENTIAL_CONFIG_BYTES, &options);
        options.replay = replay;
        options.interesting = requested.interesting;
        options.output = requested.output;
        valid = valid && realpath(requested.source_directory ? requested.source_directory : replay, source) != 0;
        options.source_directory = source;
        if (valid && options.interesting)
        {
            char path[PATH_MAX];
            valid = differential_path(path, sizeof(path), replay, "failure.signature");
            FILE* file = valid ? fopen(path, "rb") : 0;
            valid = file != 0;
            if (file)
            {
                const size_t length = fread(previous_signature, 1, sizeof(previous_signature) - 1, file);
                valid = length > 0 && !ferror(file) && feof(file) && memchr(previous_signature, 0, length) == 0;
                previous_signature[length] = 0;
                valid = fclose(file) == 0 && valid;
                valid = valid && differential_buster_failure(previous_signature);
            }
        }
    }
    else if (valid && options.source_directory)
    {
        valid = false;
    }
    valid = valid && differential_config_valid(&options);
    if (valid)
    {
        DifferentialSummary summary = {0};
        result = differential_campaign(&options, &summary);
        fprintf(stderr, "differential: %s; generated=%llu passed=%llu failed=%llu tiers=%s artifacts=%s%s%s\n", result == 0 ? "passed" : result == 1 ? "failed" : "unavailable", (unsigned long long)summary.generated, (unsigned long long)summary.passed, (unsigned long long)summary.failed, options.tier1 ? "0,1" : "0", summary.directory, summary.signature[0] ? " reason=" : "", summary.signature);
        if (options.interesting)
        {
            result = differential_interesting_result(result, &summary, previous_signature) ? 0 : 1;
        }
    }
    else
    {
        fprintf(stderr, "Usage: test-differential --buster PATH (--tier0-arg ARG | --tier0-default) [--tier1-arg ARG | --tiers 0]\n"
                        "  [--generator both|csmith|yarpgen] [--csmith PATH] [--yarpgen PATH] [--csmith-include DIR]\n"
                        "  [--clang PATH] [--seed N] [--count N] [--output DIR] [--timeout-ms N]\n"
                        "  [--output-limit BYTES] [--source-limit BYTES] [--revision ID] [--generator-revision ID]\n"
                        "  [--buster-arg ARG] [--clang-arg ARG] [--csmith-arg ARG] [--yarpgen-arg ARG]\n"
                        "  --generate-only: preserve generator controls; exit 2, no compiler campaign passes.\n"
                        "  --replay CASE | --interesting CASE [--source-directory DIR] [--output DIR]\n"
                        "Arguments are individual argv entries; repeat *-arg for multiple entries. See docs/differential.md.\n");
        result = help ? 0 : options.interesting ? 1 : 2;
    }
    free(replay_storage);
    return result;
}
