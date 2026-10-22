// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The runner the 9700X job runs its tests through, for the parallel command:
//
//   build/compiler parallel [--jobs N] [--timeout SECONDS] [--github] [LIST]
//
// LIST (standard input without one) is a list of commands. Each starts at a heading, a line `== TITLE`, and its script
// is the lines after it, up to the next heading; before the first heading there may be blank lines and comments (`#`).
// The runner starts the commands in the list's order, at most --jobs at once (by default as many as the cores of the
// CPUs this process may run on), each script in a bash of its own as GitHub Actions runs a step's `run` with `shell:
// bash` (`bash --noprofile --norc -e -o pipefail`), in a process group of its own, with standard input from /dev/null
// and standard output and error into one capture (a memfd) of its own, and no other descriptor. It prints each
// command's heading with its outcome and time, then its output, in the list's order, each as soon as it and every
// command before it have ended; with --github between `::group::` and `::endgroup::`, so that GitHub's log folds each
// one under its heading, and with an `::error::` annotation for each that failed. Last comes a summary line, and a line
// naming each command that failed. The exit status is 0 when every command exited with status 0, and 1 otherwise.
//
// When a command's shell ends, whatever it left running in its process group is killed, so that nothing it started
// outlives it or writes into its output after it is printed. A command still running --timeout seconds after it
// started is killed with its group, and fails. SIGINT, SIGTERM and SIGHUP stop the run, and so does SIGPIPE, from a
// report that has no reader: the runner sends the signal to every running command's group (a second one sends
// SIGKILL), starts nothing more, and prints what every command wrote.
//
// The commands aren't pinned: tests that run lanes need the CPUs the job may use (test-lanes' syncs on 3 lanes don't
// end within its watchdog's 60 s on one CPU). The runner is test tooling outside the compile path and has no vector
// path: a list is a few dozen commands, and the time goes into them.
//
// main.c includes this file after lane.c, for its arenas, and parallel_test.c after it. Layout, in this order: the list
// (ParallelList, parallel_list_parse), the run (ParallelOptions, ParallelResult, ParallelSummary, parallel_start,
// parallel_reap, parallel_print, parallel_take_signals, parallel_run), and the command (parallel_core_count,
// run_parallel_command).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

// OS-dependent includes
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/memfd.h>

extern char** environ;

// The command's arena: the list and the scripts copied from it. Its pages are committed only as they are touched.
#define BUSTER_PARALLEL_ARENA_CAPACITY (16ull << 30)
// sched_getaffinity's mask of 1,024 CPUs, as glibc's cpu_set_t.
#define BUSTER_PARALLEL_CPUS 1024
// Reads of the list and of a command's capture take this many bytes at a time.
#define BUSTER_PARALLEL_CHUNK_BYTES (64ull << 10)
#define BUSTER_PARALLEL_NANOSECONDS 1000000000ull
// The longest --timeout, in seconds: a day.
#define BUSTER_PARALLEL_MAX_TIMEOUT_SECONDS 86400ull
// A command's outcome, as its heading and the summary print it.
#define BUSTER_PARALLEL_OUTCOME_BYTES 512

// ---------------------------------------------------------------------------------------------------------------------
// The list

typedef struct ParallelCommand ParallelCommand;
struct ParallelCommand
{
    // The heading's title, in the list's text.
    String8 title;
    // The script's lines, copied with a NUL after them for bash's -c.
    char* script;
    // The heading's line in the list, from 1.
    uint64_t line;
};

typedef struct ParallelList ParallelList;
struct ParallelList
{
    ParallelCommand* commands;
    uint64_t count;
    // The first problem: what it is, or null for a valid list, and its line (from 1, or 0 for the whole list).
    const char* error;
    uint64_t error_line;
};

static bool parallel_is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

// Whether the line holds nothing but blanks.
static bool parallel_blank_line(String8 line)
{
    uint64_t i = 0;
    while (i < line.length && parallel_is_blank(line.pointer[i]))
    {
        i += 1;
    }
    return i == line.length;
}

// Whether the line's first character that isn't a blank is a comment's '#'.
static bool parallel_comment_line(String8 line)
{
    uint64_t i = 0;
    while (i < line.length && parallel_is_blank(line.pointer[i]))
    {
        i += 1;
    }
    return i < line.length && line.pointer[i] == '#';
}

static bool parallel_equal(String8 a, String8 b)
{
    return a.length == b.length && memcmp(a.pointer, b.pointer, a.length) == 0;
}

// A heading's title: the line after its "==", without the blanks around it.
static String8 parallel_title(String8 line)
{
    uint64_t start = 2;
    uint64_t end = line.length;
    while (start < end && parallel_is_blank(line.pointer[start]))
    {
        start += 1;
    }
    while (end > start && parallel_is_blank(line.pointer[end - 1]))
    {
        end -= 1;
    }
    return (String8){ .pointer = line.pointer + start, .length = end - start };
}

// The script of the command whose heading ends at `start`: the list's text from there to `end`, copied with a NUL after
// it. A script of blank lines alone is an error, as is a full arena.
static void parallel_list_script(Arena* arena, ParallelList* list, String8 text, uint64_t start, uint64_t end, bool has_content)
{
    ParallelCommand* command = &list->commands[list->count - 1];
    char* script = has_content ? arena_push(arena, end - start + 1, 1) : 0;
    if (!has_content)
    {
        list->error = "a command without a script";
        list->error_line = command->line;
    }
    else if (script == 0)
    {
        list->error = "out of memory for the list";
        list->error_line = 0;
    }
    else
    {
        memcpy(script, text.pointer + start, end - start);
        script[end - start] = 0;
        command->script = script;
    }
}

// Reads a list's headings and scripts, which stay in `text`'s bytes and the arena. On an error, the list says which and
// where, and its commands are incomplete.
static ParallelList parallel_list_parse(Arena* arena, String8 text)
{
    ParallelList result = { 0 };
    uint64_t headings = 0;
    for (uint64_t i = 0; i + 1 < text.length; i += 1)
    {
        headings += (i == 0 || text.pointer[i - 1] == '\n') && text.pointer[i] == '=' && text.pointer[i + 1] == '=';
    }
    result.commands = headings != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, ParallelCommand, headings) : 0;
    if (headings != 0 && result.commands == 0)
    {
        result.error = "out of memory for the list";
    }
    uint64_t position = 0;
    uint64_t line_number = 0;
    // Where the current command's script starts, and whether it has a line that isn't blank.
    uint64_t script_start = 0;
    bool has_content = false;
    while (result.error == 0 && position < text.length)
    {
        const uint64_t line_start = position;
        while (position < text.length && text.pointer[position] != '\n')
        {
            position += 1;
        }
        const String8 line = { .pointer = text.pointer + line_start, .length = position - line_start };
        position += position < text.length;
        line_number += 1;
        const bool heading = line.length >= 2 && line.pointer[0] == '=' && line.pointer[1] == '=';
        if (memchr(line.pointer, 0, line.length) != 0)
        {
            result.error = "a NUL byte";
            result.error_line = line_number;
        }
        else if (heading)
        {
            if (result.count != 0)
            {
                parallel_list_script(arena, &result, text, script_start, line_start, has_content);
            }
            const String8 title = parallel_title(line);
            bool repeated = false;
            for (uint64_t i = 0; i < result.count; i += 1)
            {
                repeated = repeated || parallel_equal(result.commands[i].title, title);
            }
            if (result.error == 0 && title.length == 0)
            {
                result.error = "a heading without a title";
                result.error_line = line_number;
            }
            else if (result.error == 0 && repeated)
            {
                result.error = "a title that an earlier command has";
                result.error_line = line_number;
            }
            else if (result.error == 0)
            {
                result.commands[result.count] = (ParallelCommand){ .title = title, .line = line_number };
                result.count += 1;
                script_start = position;
                has_content = false;
            }
        }
        else if (result.count == 0 && !parallel_blank_line(line) && !parallel_comment_line(line))
        {
            result.error = "a line before the first heading";
            result.error_line = line_number;
        }
        else
        {
            has_content = has_content || !parallel_blank_line(line);
        }
    }
    if (result.error == 0 && result.count == 0)
    {
        result.error = "no commands";
        result.error_line = 0;
    }
    else if (result.error == 0)
    {
        parallel_list_script(arena, &result, text, script_start, text.length, has_content);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The run

typedef struct ParallelOptions ParallelOptions;
struct ParallelOptions
{
    // At most this many commands run at once: at least 1.
    uint64_t jobs;
    // A command still running this many nanoseconds after it started is killed, or never when 0.
    uint64_t timeout;
    // GitHub Actions' workflow commands: a group around each command's output, and an annotation for each failure.
    bool github;
    // The shell that runs each script, found on PATH as GitHub Actions finds bash.
    const char* shell;
    // Where the report goes.
    int output;
};

typedef enum ParallelState
{
    PARALLEL_STATE_WAITING,
    PARALLEL_STATE_RUNNING,
    PARALLEL_STATE_ENDED,
} ParallelState;

typedef struct ParallelResult ParallelResult;
struct ParallelResult
{
    // When the command started and ended, in nanoseconds from the run's start: before its shell started, and after it
    // was reaped.
    uint64_t start;
    uint64_t end;
    // Its shell, the leader of its process group.
    pid_t pid;
    // The capture of its output and errors, until it is printed, or -1.
    int capture;
    // waitpid's status, once it has ended.
    int status;
    // Why it couldn't start (an errno), or 0.
    int start_error;
    ParallelState state;
    // The runner killed it at its timeout, or sent its group the signal that stopped the run.
    bool timed_out;
    bool interrupted;
    // It ended with exit status 0.
    bool passed;
};

typedef struct ParallelSummary ParallelSummary;
struct ParallelSummary
{
    uint64_t passed;
    uint64_t failed;
    uint64_t not_started;
    // The most commands that ran at once.
    uint64_t peak;
    // The run's time, in nanoseconds.
    uint64_t elapsed;
    // The signal that stopped the run, or 0.
    int signal;
    // Every command passed, and no signal stopped the run.
    bool succeeded;
};

static uint64_t parallel_clock(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * BUSTER_PARALLEL_NANOSECONDS + (uint64_t)time.tv_nsec;
}

static double parallel_seconds(uint64_t nanoseconds)
{
    return (double)nanoseconds / (double)BUSTER_PARALLEL_NANOSECONDS;
}

static void parallel_write(int fd, const char* bytes, uint64_t length)
{
    uint64_t written = 0;
    bool writing = true;
    while (writing && written < length)
    {
        const ssize_t count = write(fd, bytes + written, length - written);
        if (count > 0)
        {
            written += (uint64_t)count;
        }
        else
        {
            writing = count < 0 && errno == EINTR;
        }
    }
}

// Text inside a workflow command, which GitHub reads with '%', CR and LF escaped, and in a property also ':' and ','.
static void parallel_write_escaped(int fd, String8 text, bool property)
{
    uint64_t start = 0;
    for (uint64_t i = 0; i < text.length; i += 1)
    {
        const char c = text.pointer[i];
        const char* escape = c == '%' ? "%25" : c == '\r' ? "%0D" : c == '\n' ? "%0A" : property && c == ':' ? "%3A" : property && c == ',' ? "%2C" : 0;
        if (escape != 0)
        {
            parallel_write(fd, text.pointer + start, i - start);
            parallel_write(fd, escape, 3);
            start = i + 1;
        }
    }
    parallel_write(fd, text.pointer + start, text.length - start);
}

// What became of a command: "passed", "failed (exit status 1)" and the others, with its time when it ran.
static void parallel_outcome(const ParallelResult* result, const ParallelOptions* options, char* text, uint64_t size)
{
    char reason[BUSTER_PARALLEL_OUTCOME_BYTES];
    if (result->state == PARALLEL_STATE_WAITING)
    {
        snprintf(reason, sizeof(reason), "not started");
    }
    else if (result->start_error != 0)
    {
        snprintf(reason, sizeof(reason), "failed (can't start %s: %s)", options->shell, strerror(result->start_error));
    }
    else if (result->passed)
    {
        snprintf(reason, sizeof(reason), "passed");
    }
    else if (result->timed_out)
    {
        snprintf(reason, sizeof(reason), "failed (timed out)");
    }
    else if (WIFSIGNALED(result->status))
    {
        snprintf(reason, sizeof(reason), "failed (killed by signal %d: %s)", WTERMSIG(result->status), strsignal(WTERMSIG(result->status)));
    }
    else
    {
        snprintf(reason, sizeof(reason), "failed (exit status %d)", WEXITSTATUS(result->status));
    }
    if (result->state == PARALLEL_STATE_WAITING)
    {
        snprintf(text, size, "%s", reason);
    }
    else
    {
        snprintf(text, size, "%s in %.1f s", reason, parallel_seconds(result->end - result->start));
    }
}

// Starts a command at `now`: its capture, then its shell in a process group of its own, with the signal mask the
// runner's caller had. A command that can't start has ended, and start_error says why.
static void parallel_start(const ParallelCommand* command, const ParallelOptions* options, const sigset_t* mask, ParallelResult* result, uint64_t now)
{
    result->start = now;
    result->end = now;
    // A capture below 3 would be one of the descriptors the child's shell gets, and its dup2 would keep FD_CLOEXEC.
    int capture = (int)syscall(SYS_memfd_create, "parallel", MFD_CLOEXEC);
    if (capture >= 0 && capture < 3)
    {
        const int low = capture;
        capture = fcntl(low, F_DUPFD_CLOEXEC, 3);
        close(low);
    }
    int error = capture < 0 ? errno : 0;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    const bool actions_ready = error == 0 && posix_spawn_file_actions_init(&actions) == 0;
    const bool attributes_ready = actions_ready && posix_spawnattr_init(&attributes) == 0;
    if (error == 0 && !attributes_ready)
    {
        error = ENOMEM;
    }
    if (error == 0)
    {
        error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    }
    if (error == 0)
    {
        error = posix_spawn_file_actions_adddup2(&actions, capture, STDOUT_FILENO);
    }
    if (error == 0)
    {
        error = posix_spawn_file_actions_adddup2(&actions, capture, STDERR_FILENO);
    }
    if (error == 0)
    {
        error = posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
    }
    if (error == 0)
    {
        error = posix_spawnattr_setpgroup(&attributes, 0);
    }
    if (error == 0)
    {
        error = posix_spawnattr_setsigmask(&attributes, mask);
    }
    if (error == 0)
    {
        error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK);
    }
    pid_t pid = -1;
    if (error == 0)
    {
        char* arguments[] = {
            (char*)options->shell,
            "--noprofile",
            "--norc",
            "-e",
            "-o",
            "pipefail",
            "-c",
            command->script,
            0,
        };
        error = posix_spawnp(&pid, options->shell, &actions, &attributes, arguments, environ);
    }
    if (attributes_ready)
    {
        posix_spawnattr_destroy(&attributes);
    }
    if (actions_ready)
    {
        posix_spawn_file_actions_destroy(&actions);
    }
    if (error == 0)
    {
        result->state = PARALLEL_STATE_RUNNING;
        result->pid = pid;
        result->capture = capture;
    }
    else
    {
        if (capture >= 0)
        {
            close(capture);
        }
        result->state = PARALLEL_STATE_ENDED;
        result->start_error = error;
    }
}

// Reaps every running command whose shell has ended, at `now`, after killing whatever it left in its process group:
// its shell, a zombie until it is reaped, still holds the group's id. Returns how many it reaped.
static uint64_t parallel_reap(ParallelResult* results, uint64_t count, uint64_t now)
{
    uint64_t reaped = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        ParallelResult* result = &results[i];
        siginfo_t info;
        memset(&info, 0, sizeof(info));
        const bool ended = result->state == PARALLEL_STATE_RUNNING && waitid(P_PID, (id_t)result->pid, &info, WEXITED | WNOHANG | WNOWAIT) == 0 && info.si_pid == result->pid;
        if (ended)
        {
            kill(-result->pid, SIGKILL);
            int status = 0;
            pid_t waited = -1;
            do
            {
                waited = waitpid(result->pid, &status, 0);
            } while (waited < 0 && errno == EINTR);
            result->status = status;
            result->passed = waited == result->pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
            result->state = PARALLEL_STATE_ENDED;
            result->end = now;
            reaped += 1;
        }
    }
    return reaped;
}

// Prints a command's heading, with its outcome and time, and what it wrote, then closes its capture.
static void parallel_print(const ParallelCommand* command, const ParallelOptions* options, ParallelResult* result)
{
    char outcome[BUSTER_PARALLEL_OUTCOME_BYTES];
    parallel_outcome(result, options, outcome, sizeof(outcome));
    const String8 outcome_text = { .pointer = outcome, .length = strlen(outcome) };
    parallel_write(options->output, options->github ? "::group::" : "== ", options->github ? 9 : 3);
    if (options->github)
    {
        parallel_write_escaped(options->output, command->title, false);
        parallel_write(options->output, ": ", 2);
        parallel_write_escaped(options->output, outcome_text, false);
    }
    else
    {
        parallel_write(options->output, command->title.pointer, command->title.length);
        parallel_write(options->output, ": ", 2);
        parallel_write(options->output, outcome_text.pointer, outcome_text.length);
    }
    parallel_write(options->output, "\n", 1);
    char buffer[BUSTER_PARALLEL_CHUNK_BYTES];
    uint64_t offset = 0;
    char last = '\n';
    bool reading = result->capture >= 0;
    while (reading)
    {
        const ssize_t count = pread(result->capture, buffer, sizeof(buffer), (off_t)offset);
        if (count > 0)
        {
            parallel_write(options->output, buffer, (uint64_t)count);
            offset += (uint64_t)count;
            last = buffer[count - 1];
        }
        else
        {
            reading = count < 0 && errno == EINTR;
        }
    }
    // The next heading, or the group's end, starts a line of its own.
    if (last != '\n')
    {
        parallel_write(options->output, "\n", 1);
    }
    if (options->github)
    {
        parallel_write(options->output, "::endgroup::\n", 13);
    }
    if (result->capture >= 0)
    {
        close(result->capture);
        result->capture = -1;
    }
}

// Takes every pending signal of the set, which the run keeps blocked, and returns the first, or 0.
static int parallel_take_signals(const sigset_t* signals)
{
    const struct timespec now = { 0 };
    int first = 0;
    int pending = 0;
    while ((pending = sigtimedwait(signals, 0, &now)) > 0)
    {
        first = first != 0 ? first : pending;
    }
    return first;
}

// Runs the list's commands, prints the report into options->output, and returns what became of them, each command's in
// `results` (one per command).
static ParallelSummary parallel_run(const ParallelList* list, const ParallelOptions* options, ParallelResult* results)
{
    ParallelSummary summary = { 0 };
    for (uint64_t i = 0; i < list->count; i += 1)
    {
        results[i] = (ParallelResult){ .pid = -1, .capture = -1 };
    }
    // The signals the run waits for, blocked until sigtimedwait takes them: the commands' SIGCHLD, and the signals that
    // stop the run, unless they were ignored (as nohup does with SIGHUP). SIGCHLD must not be ignored either, or the
    // kernel would reap the commands itself. A write into a pipe with no reader (`parallel | head`) raises SIGPIPE,
    // which then stops the run rather than ending the runner with its commands left running.
    sigset_t handled;
    sigemptyset(&handled);
    sigaddset(&handled, SIGCHLD);
    const int stops[] = {
        SIGINT,
        SIGTERM,
        SIGHUP,
        SIGPIPE,
    };
    for (uint64_t i = 0; i < sizeof(stops) / sizeof(stops[0]); i += 1)
    {
        struct sigaction current;
        if (sigaction(stops[i], 0, &current) == 0 && current.sa_handler != SIG_IGN)
        {
            sigaddset(&handled, stops[i]);
        }
    }
    sigset_t previous_mask;
    sigprocmask(SIG_BLOCK, &handled, &previous_mask);
    struct sigaction child_default = { .sa_handler = SIG_DFL };
    struct sigaction previous_child;
    sigaction(SIGCHLD, &child_default, &previous_child);

    const uint64_t jobs = options->jobs != 0 ? options->jobs : 1;
    const uint64_t origin = parallel_clock();
    dprintf(options->output, "parallel: %llu commands, at most %llu at once\n", (unsigned long long)list->count, (unsigned long long)jobs);
    uint64_t started = 0;
    uint64_t running = 0;
    uint64_t printed = 0;
    while (running != 0 || (summary.signal == 0 && started < list->count))
    {
        while (summary.signal == 0 && running < jobs && started < list->count)
        {
            parallel_start(&list->commands[started], options, &previous_mask, &results[started], parallel_clock() - origin);
            running += results[started].state == PARALLEL_STATE_RUNNING;
            summary.peak = running > summary.peak ? running : summary.peak;
            started += 1;
        }
        // Each command is printed once it and every command before it have ended.
        while (printed < list->count && results[printed].state == PARALLEL_STATE_ENDED)
        {
            parallel_print(&list->commands[printed], options, &results[printed]);
            printed += 1;
        }
        if (running != 0)
        {
            // Wait for a signal, or the next command's timeout.
            const uint64_t now = parallel_clock() - origin;
            uint64_t wait = UINT64_MAX;
            for (uint64_t i = 0; options->timeout != 0 && i < started; i += 1)
            {
                const uint64_t deadline = results[i].start + options->timeout;
                if (results[i].state == PARALLEL_STATE_RUNNING && !results[i].timed_out)
                {
                    const uint64_t left = deadline > now ? deadline - now : 0;
                    wait = left < wait ? left : wait;
                }
            }
            const struct timespec timeout = {
                .tv_sec = (time_t)(wait / BUSTER_PARALLEL_NANOSECONDS),
                .tv_nsec = (long)(wait % BUSTER_PARALLEL_NANOSECONDS),
            };
            const int received = sigtimedwait(&handled, 0, wait == UINT64_MAX ? 0 : &timeout);
            if (received > 0 && received != SIGCHLD)
            {
                // The first signal goes to every running command's group, and stops the run; another kills them.
                const int forwarded = summary.signal == 0 ? received : SIGKILL;
                summary.signal = summary.signal == 0 ? received : summary.signal;
                for (uint64_t i = 0; i < started; i += 1)
                {
                    if (results[i].state == PARALLEL_STATE_RUNNING)
                    {
                        kill(-results[i].pid, forwarded);
                        results[i].interrupted = true;
                    }
                }
            }
            const uint64_t after = parallel_clock() - origin;
            running -= parallel_reap(results, started, after);
            for (uint64_t i = 0; options->timeout != 0 && i < started; i += 1)
            {
                if (results[i].state == PARALLEL_STATE_RUNNING && !results[i].timed_out && after >= results[i].start + options->timeout)
                {
                    kill(-results[i].pid, SIGKILL);
                    results[i].timed_out = true;
                }
            }
        }
    }
    // After a signal, the commands that didn't start too.
    while (printed < list->count)
    {
        parallel_print(&list->commands[printed], options, &results[printed]);
        printed += 1;
    }
    // A signal that came as the last command ended stops the run too.
    sigset_t stopping = handled;
    sigdelset(&stopping, SIGCHLD);
    const int last = parallel_take_signals(&stopping);
    summary.signal = summary.signal != 0 ? summary.signal : last;
    summary.elapsed = parallel_clock() - origin;
    for (uint64_t i = 0; i < list->count; i += 1)
    {
        summary.passed += results[i].passed;
        summary.not_started += results[i].state == PARALLEL_STATE_WAITING;
    }
    summary.failed = list->count - summary.passed - summary.not_started;
    dprintf(options->output, "parallel: %llu commands in %.1f s, at most %llu at once: %llu passed, %llu failed, %llu not started", (unsigned long long)list->count,
            parallel_seconds(summary.elapsed), (unsigned long long)jobs, (unsigned long long)summary.passed, (unsigned long long)summary.failed,
            (unsigned long long)summary.not_started);
    if (summary.signal != 0)
    {
        dprintf(options->output, ", stopped by signal %d: %s", summary.signal, strsignal(summary.signal));
    }
    parallel_write(options->output, "\n", 1);
    for (uint64_t i = 0; i < list->count; i += 1)
    {
        if (!results[i].passed && results[i].state == PARALLEL_STATE_ENDED)
        {
            char outcome[BUSTER_PARALLEL_OUTCOME_BYTES];
            parallel_outcome(&results[i], options, outcome, sizeof(outcome));
            const String8 outcome_text = { .pointer = outcome, .length = strlen(outcome) };
            if (options->github)
            {
                parallel_write(options->output, "::error title=", 14);
                parallel_write_escaped(options->output, list->commands[i].title, true);
                parallel_write(options->output, "::", 2);
                parallel_write_escaped(options->output, outcome_text, false);
            }
            else
            {
                parallel_write(options->output, "parallel: ", 10);
                parallel_write(options->output, list->commands[i].title.pointer, list->commands[i].title.length);
                parallel_write(options->output, " ", 1);
                parallel_write(options->output, outcome_text.pointer, outcome_text.length);
            }
            parallel_write(options->output, "\n", 1);
        }
    }
    // A report with no reader raised SIGPIPE as the summary was printed, which then fails the run, here rather than
    // ending the runner once the mask is put back. Then the handlers and the mask are put back.
    const int late = parallel_take_signals(&stopping);
    summary.signal = summary.signal != 0 ? summary.signal : late;
    summary.succeeded = summary.passed == list->count && summary.signal == 0;
    sigset_t children;
    sigemptyset(&children);
    sigaddset(&children, SIGCHLD);
    parallel_take_signals(&children);
    sigaction(SIGCHLD, &previous_child, 0);
    sigprocmask(SIG_SETMASK, &previous_mask, 0);
    return summary;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

// The first CPU of a core's siblings' list ("2,10", "0-1" or "5"), or `cpu` itself when the list can't be read.
static uint64_t parallel_core_first(uint64_t cpu)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%llu/topology/core_cpus_list", (unsigned long long)cpu);
    char text[64];
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    const ssize_t length = fd >= 0 ? read(fd, text, sizeof(text)) : -1;
    if (fd >= 0)
    {
        close(fd);
    }
    uint64_t first = 0;
    ssize_t digits = 0;
    while (digits < length && text[digits] >= '0' && text[digits] <= '9' && first < BUSTER_PARALLEL_CPUS)
    {
        first = first * 10 + (uint64_t)(text[digits] - '0');
        digits += 1;
    }
    return digits != 0 && first < BUSTER_PARALLEL_CPUS ? first : cpu;
}

// The cores of the CPUs this process may run on, at least 1: --jobs' default. Two commands on a core's two SMT siblings
// slow each other down, and so do the lanes' waits, which spin while their group's other lanes don't run. On the
// 9700X (8 cores, 15 CPUs online) the job's tests took 21.3 to 22.4 s with 8 jobs, and 22.9 to 23.3 s with 15, in
// which test-lanes took 2.3 to 2.9 s rather than 0.2.
static uint64_t parallel_core_count(void)
{
    uint64_t mask[BUSTER_PARALLEL_CPUS / 64] = { 0 };
    uint64_t cores[BUSTER_PARALLEL_CPUS / 64] = { 0 };
    const bool known = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0;
    for (uint64_t cpu = 0; known && cpu < BUSTER_PARALLEL_CPUS; cpu += 1)
    {
        if (((mask[cpu / 64] >> (cpu % 64)) & 1) != 0)
        {
            const uint64_t first = parallel_core_first(cpu);
            cores[first / 64] |= 1ull << (first % 64);
        }
    }
    uint64_t count = 0;
    for (uint64_t i = 0; i < BUSTER_PARALLEL_CPUS / 64; i += 1)
    {
        count += (uint64_t)__builtin_popcountll(cores[i]);
    }
    return count != 0 ? count : 1;
}

// A count option's value: decimal digits alone, from 1 to `limit`.
static bool parallel_count(const char* text, uint64_t limit, uint64_t* value)
{
    uint64_t number = 0;
    uint64_t i = 0;
    bool valid = text[0] != 0;
    while (valid && text[i] != 0)
    {
        valid = text[i] >= '0' && text[i] <= '9' && number <= (limit - (uint64_t)(text[i] - '0')) / 10;
        number = valid ? number * 10 + (uint64_t)(text[i] - '0') : number;
        i += 1;
    }
    valid = valid && number != 0;
    *value = valid ? number : 0;
    return valid;
}

// Reads all of `fd` into the arena. A null pointer if it can't.
static String8 parallel_read(Arena* arena, int fd)
{
    const uint64_t mark = arena->length;
    uint64_t length = 0;
    bool reading = true;
    bool failed = false;
    while (reading)
    {
        char* chunk = arena_push(arena, BUSTER_PARALLEL_CHUNK_BYTES, 1);
        const ssize_t count = chunk != 0 ? read(fd, chunk, BUSTER_PARALLEL_CHUNK_BYTES) : -1;
        const bool interrupted = chunk != 0 && count < 0 && errno == EINTR;
        length += count > 0 ? (uint64_t)count : 0;
        arena_pop_to(arena, mark + length);
        reading = count > 0 || interrupted;
        failed = !reading && count < 0;
    }
    return failed ? (String8){ 0 } : (String8){ .pointer = (char*)arena->base + mark, .length = length };
}

// argv[0] is the command's name, parallel.
static int run_parallel_command(int argc, char** argv)
{
    int result = 1;
    ParallelOptions options = { .shell = "bash", .output = STDOUT_FILENO };
    const char* path = 0;
    bool valid = true;
    int i = 1;
    while (valid && i < argc)
    {
        uint64_t value = 0;
        if (strcmp(argv[i], "--jobs") == 0 && i + 1 < argc && parallel_count(argv[i + 1], UINT32_MAX, &value))
        {
            options.jobs = value;
            i += 2;
        }
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc && parallel_count(argv[i + 1], BUSTER_PARALLEL_MAX_TIMEOUT_SECONDS, &value))
        {
            options.timeout = value * BUSTER_PARALLEL_NANOSECONDS;
            i += 2;
        }
        else if (strcmp(argv[i], "--github") == 0)
        {
            options.github = true;
            i += 1;
        }
        else if (path == 0 && argv[i][0] != '-')
        {
            path = argv[i];
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: parallel [--jobs N] [--timeout SECONDS] [--github] [LIST]\n");
    }
    else
    {
        Arena arena = arena_reserve(BUSTER_PARALLEL_ARENA_CAPACITY);
        const int fd = path != 0 ? open(path, O_RDONLY | O_CLOEXEC) : STDIN_FILENO;
        const String8 text = fd >= 0 ? parallel_read(&arena, fd) : (String8){ 0 };
        if (path != 0 && fd >= 0)
        {
            close(fd);
        }
        const char* name = path != 0 ? path : "standard input";
        if (text.pointer == 0)
        {
            fprintf(stderr, "parallel: can't read %s\n", name);
        }
        else
        {
            const ParallelList list = parallel_list_parse(&arena, text);
            ParallelResult* results = list.error == 0 ? BUSTER_ARENA_PUSH_ARRAY(&arena, ParallelResult, list.count) : 0;
            if (list.error != 0)
            {
                fprintf(stderr, "parallel: %s:%llu: %s\n", name, (unsigned long long)list.error_line, list.error);
            }
            else if (results == 0)
            {
                fprintf(stderr, "parallel: out of memory for the results\n");
            }
            else
            {
                options.jobs = options.jobs != 0 ? options.jobs : parallel_core_count();
                fflush(stdout);
                result = parallel_run(&list, &options, results).succeeded ? 0 : 1;
            }
        }
        arena_release(&arena);
    }
    return result;
}
