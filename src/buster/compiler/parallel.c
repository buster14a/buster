// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The runner the 9700X job runs its tests through, for the parallel command:
//
//   build/compiler parallel [--jobs N] [--timeout SECONDS] [--github] [LIST]
//
// LIST (standard input without one) is a list of commands. Each starts at a heading, a line `== TITLE`, and its script
// is the lines after it, up to the next heading; before the first heading there may be blank lines and comments (`#`).
// A title that ends with `[N CPUs]` (N from 2) says that its command keeps N CPUs busy, with lanes or processes of its
// own; any other runs on 1. The runner starts the commands in the list's order, each once the commands running leave it
// room: their CPUs come to at most --jobs (by default as many as the cores of the CPUs this process may run on), and a
// command of more runs alone, on all of them. A command that doesn't fit yet holds back the ones after it. Each script
// runs in a bash of its own as GitHub Actions runs a step's `run` with `shell: bash` (`bash --noprofile --norc -e -o
// pipefail`), in a process group of its own, with standard input from /dev/null and standard output and error into one
// capture (a memfd) of its own, no other descriptor, and BUSTER_PARALLEL_CPUS set to its CPUs, which a test that starts
// processes of its own runs at most that many of at once (test_process_cpus). Its report gives each command's heading
// with its outcome, its time and its CPU time (which shows the CPUs it kept busy), then its output, in the list's order,
// each as soon as it and every command before it have ended; with --github between `::group::` and `::endgroup::`, so
// that GitHub's log folds each one under its heading, and with an `::error::` annotation for each that failed. Last
// comes a summary line, and a line naming each command that failed. The exit status is 0 when every command exited with
// status 0, and 1 otherwise.
//
// The report never holds up the run. A command's output is read into memory when it ends and its capture closed, so
// that only running commands hold one, however many have ended before the report reaches them; and the report goes out
// only as fast as its reader takes it, at most PIPE_BUF bytes at a time into a pipe or a terminal once poll says it has
// room, which takes that many without waiting, while the runner keeps starting commands, enforcing their timeouts and
// taking signals, all from one poll on a signalfd.
//
// When a command's shell ends, whatever it left running in its process group is killed, so that nothing it started
// outlives it or writes into its output after it is read. A command still running --timeout seconds after it started is
// killed with its group, and fails, though its shell may end before the runner reaps it. SIGINT, SIGTERM and SIGHUP stop
// the run: the runner sends the signal to every running command's group (a second one sends SIGKILL), starts nothing
// more, and reports what every command wrote; once every command has ended, one drops the rest of the report rather
// than wait for its reader. A report that can't be written (EPIPE: nothing reads it, whether or not SIGPIPE is ignored)
// stops the run, and its commands are killed.
//
// The commands aren't pinned: tests that run lanes need the CPUs the job may use (test-lanes' syncs on 3 lanes don't
// end within its watchdog's 60 s on one CPU), so a command's CPUs are what it says it keeps busy, not a mask. The
// runner is test tooling outside the compile path and has no vector path: a list is a few dozen commands, and the time
// goes into them.
//
// main.c includes this file after lane.c, for its arenas, and parallel_test.c after it. Layout, in this order: the list
// (ParallelList, parallel_title_cpus, parallel_list_parse), the run (ParallelOptions, ParallelResult, ParallelSummary,
// ParallelEnvironment, parallel_start, the report's parts: parallel_render, parallel_render_summary and parallel_part,
// ParallelOutput and parallel_output_write, parallel_reap, parallel_stop, parallel_run), and the command
// (run_parallel_command).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

// OS-dependent includes
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/memfd.h>

extern char** environ;

// The command's arena: the list, the scripts copied from it, and the report's parts. Its pages are committed only as they
// are touched.
#define BUSTER_PARALLEL_ARENA_CAPACITY (16ull << 30)
// sched_getaffinity's mask of 1,024 CPUs, as glibc's cpu_set_t.
#define BUSTER_PARALLEL_CPUS 1024
// Reads of the list take this many bytes at a time, and so do writes of the report into a regular file.
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
    // The heading's title, in the list's text, with its `[N CPUs]` if it has one.
    String8 title;
    // The script's lines, copied with a NUL after them for bash's -c.
    char* script;
    // The heading's line in the list, from 1.
    uint64_t line;
    // The CPUs it keeps busy: its title's N, or 1.
    uint64_t cpus;
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

// The CPUs a title declares, into *cpus: N for a title that ends with `[N CPUs]`, N from 2 to the largest --jobs, or 1
// for a title that doesn't end with "CPUs]". False for any other title that ends so, whose *cpus is 1.
static bool parallel_title_cpus(String8 title, uint64_t* cpus)
{
    static const char word[] = "CPUs]";
    const uint64_t word_length = sizeof(word) - 1;
    bool valid = true;
    uint64_t count = 1;
    if (title.length >= word_length && memcmp(title.pointer + title.length - word_length, word, word_length) == 0)
    {
        // The count's digits, between the '[' and the blank before "CPUs]".
        const uint64_t end = title.length > word_length && title.pointer[title.length - word_length - 1] == ' ' ? title.length - word_length - 1 : 0;
        uint64_t start = end;
        while (start > 0 && title.pointer[start - 1] >= '0' && title.pointer[start - 1] <= '9')
        {
            start -= 1;
        }
        valid = start != end && start != 0 && title.pointer[start - 1] == '[';
        count = 0;
        for (uint64_t i = start; valid && i < end; i += 1)
        {
            const uint64_t digit = (uint64_t)(title.pointer[i] - '0');
            valid = count <= (UINT32_MAX - digit) / 10;
            count = count * 10 + digit;
        }
        valid = valid && count >= 2;
    }
    *cpus = valid ? count : 1;
    return valid;
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
            uint64_t cpus = 1;
            const bool counted = parallel_title_cpus(title, &cpus);
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
            else if (result.error == 0 && !counted)
            {
                result.error = "a title whose CPUs aren't [N CPUs] with N from 2";
                result.error_line = line_number;
            }
            else if (result.error == 0)
            {
                result.commands[result.count] = (ParallelCommand){ .title = title, .line = line_number, .cpus = cpus };
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
    // The CPUs the commands running at once may keep busy: at least 1.
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
    // The CPUs it holds while it runs, and its BUSTER_PARALLEL_CPUS: its command's, or all of --jobs when those are
    // fewer.
    uint64_t cpus;
    // The capture of its output and errors while it runs, or -1.
    int capture;
    // waitpid's status, once it has ended.
    int status;
    // The CPU time of its shell and of the processes under it that were waited for, in nanoseconds: wait4's user and
    // system times, once it has ended.
    uint64_t cpu;
    // Why it couldn't start (an errno), or 0.
    int start_error;
    ParallelState state;
    // The runner killed it at its timeout, or sent its group the signal that stopped the run.
    bool timed_out;
    bool interrupted;
    // It ended with exit status 0, before its timeout.
    bool passed;
    // Its part of the report, its heading with its outcome and time and then its output, once it has ended (or the run
    // stopped before it started): null until then.
    String8 report;
};

typedef struct ParallelSummary ParallelSummary;
struct ParallelSummary
{
    uint64_t passed;
    uint64_t failed;
    uint64_t not_started;
    // The most commands that ran at once.
    uint64_t peak;
    // The run's time, and its commands' CPU time, in nanoseconds.
    uint64_t elapsed;
    uint64_t cpu;
    // The signal that stopped the run, or 0.
    int signal;
    // A write of the report failed (EPIPE, when nothing reads it), or a signal dropped the rest of it, or the arena
    // couldn't hold a part of it.
    bool report_lost;
    bool memory_full;
    // Every command passed, no signal stopped the run, and the report went out whole.
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

// Appends bytes at the arena's end, where the part of the report being built grows. False when the arena is full.
static bool parallel_append(Arena* arena, const char* bytes, uint64_t length)
{
    char* destination = arena_push(arena, length, 1);
    if (destination != 0)
    {
        memcpy(destination, bytes, length);
    }
    return destination != 0;
}

// Text inside a workflow command, which GitHub reads with '%', CR and LF escaped, and in a property also ':' and ','.
static bool parallel_append_escaped(Arena* arena, String8 text, bool property)
{
    bool appended = true;
    uint64_t start = 0;
    for (uint64_t i = 0; i < text.length; i += 1)
    {
        const char c = text.pointer[i];
        const char* escape = c == '%' ? "%25" : c == '\r' ? "%0D" : c == '\n' ? "%0A" : property && c == ':' ? "%3A" : property && c == ',' ? "%2C" : 0;
        if (escape != 0)
        {
            appended = appended && parallel_append(arena, text.pointer + start, i - start) && parallel_append(arena, escape, 3);
            start = i + 1;
        }
    }
    return appended && parallel_append(arena, text.pointer + start, text.length - start);
}

// What a part of the report says when the arena can't hold it, which fails the run.
static const char parallel_lost_part[] = "parallel: out of memory for this part of the report\n";

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
    else if (result->start_error != 0)
    {
        snprintf(text, size, "%s in %.1f s", reason, parallel_seconds(result->end - result->start));
    }
    else
    {
        snprintf(text, size, "%s in %.1f s, %.1f s of CPU", reason, parallel_seconds(result->end - result->start), parallel_seconds(result->cpu));
    }
}

// The commands' environment: this process's but for BUSTER_PARALLEL_CPUS, which each command gets of its own, its CPUs,
// in the slot after the others (`cpus`, which parallel_start writes). Null variables when the arena can't hold them,
// and then no command starts.
typedef struct ParallelEnvironment ParallelEnvironment;
struct ParallelEnvironment
{
    char** variables;
    uint64_t slot;
    char cpus[48];
};

static void parallel_environment(Arena* arena, ParallelEnvironment* environment)
{
    static const char name[] = "BUSTER_PARALLEL_CPUS=";
    uint64_t count = 0;
    while (environ != 0 && environ[count] != 0)
    {
        count += 1;
    }
    environment->variables = BUSTER_ARENA_PUSH_ARRAY(arena, char*, count + 2);
    environment->slot = 0;
    for (uint64_t i = 0; environment->variables != 0 && i < count; i += 1)
    {
        if (strncmp(environ[i], name, sizeof(name) - 1) != 0)
        {
            environment->variables[environment->slot] = environ[i];
            environment->slot += 1;
        }
    }
    if (environment->variables != 0)
    {
        environment->variables[environment->slot] = environment->cpus;
        environment->variables[environment->slot + 1] = 0;
    }
}

// Starts a command at `now`, holding result->cpus: its capture, then its shell in a process group of its own, with the
// signal mask the runner's caller had and BUSTER_PARALLEL_CPUS set to its CPUs. A command that can't start has ended,
// and start_error says why.
static void parallel_start(const ParallelCommand* command, const ParallelOptions* options, const sigset_t* mask, ParallelEnvironment* environment, ParallelResult* result,
                           uint64_t now)
{
    result->start = now;
    result->end = now;
    // A capture below 3 would be one of the descriptors the child's shell gets, and its dup2 would keep FD_CLOEXEC.
    int capture = environment->variables != 0 ? (int)syscall(SYS_memfd_create, "parallel", MFD_CLOEXEC) : -1;
    if (capture >= 0 && capture < 3)
    {
        const int low = capture;
        capture = fcntl(low, F_DUPFD_CLOEXEC, 3);
        close(low);
    }
    int error = environment->variables == 0 ? ENOMEM : capture < 0 ? errno : 0;
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
        snprintf(environment->cpus, sizeof(environment->cpus), "BUSTER_PARALLEL_CPUS=%llu", (unsigned long long)result->cpus);
        error = posix_spawnp(&pid, options->shell, &actions, &attributes, arguments, environment->variables);
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

// Builds a command's part of the report at the arena's end: its heading, with its outcome and time, then what it wrote,
// read from its capture, which is then closed, so that only running commands hold one. Returns whether the arena held
// it; a part it can't hold is parallel_lost_part.
static bool parallel_render(Arena* arena, const ParallelCommand* command, const ParallelOptions* options, ParallelResult* result)
{
    char outcome[BUSTER_PARALLEL_OUTCOME_BYTES];
    parallel_outcome(result, options, outcome, sizeof(outcome));
    const String8 outcome_text = { .pointer = outcome, .length = strlen(outcome) };
    const uint64_t mark = arena->length;
    bool built = options->github ? parallel_append(arena, "::group::", 9) && parallel_append_escaped(arena, command->title, false) && parallel_append(arena, ": ", 2) &&
                                       parallel_append_escaped(arena, outcome_text, false)
                                 : parallel_append(arena, "== ", 3) && parallel_append(arena, command->title.pointer, command->title.length) && parallel_append(arena, ": ", 2) &&
                                       parallel_append(arena, outcome_text.pointer, outcome_text.length);
    built = built && parallel_append(arena, "\n", 1);
    struct stat stats;
    const uint64_t size = built && result->capture >= 0 && fstat(result->capture, &stats) == 0 && stats.st_size > 0 ? (uint64_t)stats.st_size : 0;
    char* bytes = size != 0 ? arena_push(arena, size, 1) : 0;
    built = built && (size == 0 || bytes != 0);
    uint64_t read_bytes = 0;
    bool reading = bytes != 0;
    while (reading && read_bytes < size)
    {
        const ssize_t count = pread(result->capture, bytes + read_bytes, size - read_bytes, (off_t)read_bytes);
        if (count > 0)
        {
            read_bytes += (uint64_t)count;
        }
        else
        {
            reading = count < 0 && errno == EINTR;
        }
    }
    if (bytes != 0)
    {
        arena_pop_to(arena, (uint64_t)(bytes - (char*)arena->base) + read_bytes);
    }
    // The next heading, or the group's end, starts a line of its own.
    built = built && (read_bytes == 0 || bytes[read_bytes - 1] == '\n' || parallel_append(arena, "\n", 1));
    built = built && (!options->github || parallel_append(arena, "::endgroup::\n", 13));
    if (result->capture >= 0)
    {
        close(result->capture);
        result->capture = -1;
    }
    if (!built)
    {
        arena_pop_to(arena, mark);
    }
    result->report = built ? (String8){ .pointer = (char*)arena->base + mark, .length = arena->length - mark }
                           : (String8){ .pointer = (char*)parallel_lost_part, .length = sizeof(parallel_lost_part) - 1 };
    return built;
}

// The report's last part, built at the arena's end once every command has ended or won't start: the summary line, then a
// line (or with --github an annotation) for each command that failed. Sets *full when the arena can't hold it.
static String8 parallel_render_summary(Arena* arena, const ParallelList* list, const ParallelOptions* options, const ParallelResult* results, const ParallelSummary* summary,
                                       uint64_t jobs, int signal_error, bool* full)
{
    const uint64_t mark = arena->length;
    char line[BUSTER_PARALLEL_OUTCOME_BYTES];
    int length = snprintf(line, sizeof(line), "parallel: %llu commands in %.1f s, %.1f s of CPU, at most %llu CPUs at once: %llu passed, %llu failed, %llu not started",
                          (unsigned long long)list->count, parallel_seconds(summary->elapsed), parallel_seconds(summary->cpu), (unsigned long long)jobs,
                          (unsigned long long)summary->passed, (unsigned long long)summary->failed, (unsigned long long)summary->not_started);
    bool built = length > 0 && parallel_append(arena, line, (uint64_t)length);
    if (summary->signal != 0)
    {
        length = snprintf(line, sizeof(line), ", stopped by signal %d: %s", summary->signal, strsignal(summary->signal));
        built = built && length > 0 && parallel_append(arena, line, (uint64_t)length);
    }
    if (signal_error != 0)
    {
        length = snprintf(line, sizeof(line), ", can't wait for the commands' signals: %s", strerror(signal_error));
        built = built && length > 0 && parallel_append(arena, line, (uint64_t)length);
    }
    built = built && parallel_append(arena, "\n", 1);
    for (uint64_t i = 0; i < list->count; i += 1)
    {
        if (!results[i].passed && results[i].state == PARALLEL_STATE_ENDED)
        {
            char outcome[BUSTER_PARALLEL_OUTCOME_BYTES];
            parallel_outcome(&results[i], options, outcome, sizeof(outcome));
            const String8 outcome_text = { .pointer = outcome, .length = strlen(outcome) };
            const String8 title = list->commands[i].title;
            built = built && (options->github ? parallel_append(arena, "::error title=", 14) && parallel_append_escaped(arena, title, true) && parallel_append(arena, "::", 2) &&
                                                    parallel_append_escaped(arena, outcome_text, false)
                                              : parallel_append(arena, "parallel: ", 10) && parallel_append(arena, title.pointer, title.length) && parallel_append(arena, " ", 1) &&
                                                    parallel_append(arena, outcome_text.pointer, outcome_text.length));
            built = built && parallel_append(arena, "\n", 1);
        }
    }
    if (!built)
    {
        arena_pop_to(arena, mark);
        *full = true;
    }
    return built ? (String8){ .pointer = (char*)arena->base + mark, .length = arena->length - mark }
                 : (String8){ .pointer = (char*)parallel_lost_part, .length = sizeof(parallel_lost_part) - 1 };
}

// The report's part at `index`: the opening line, then each command's, then the summary; null while it isn't built yet,
// which holds back every part after it.
static String8 parallel_part(const ParallelList* list, const ParallelResult* results, String8 opening, String8 closing, uint64_t index)
{
    String8 part = { 0 };
    if (index == 0)
    {
        part = opening;
    }
    else if (index <= list->count)
    {
        part = results[index - 1].report;
    }
    else if (index == list->count + 1)
    {
        part = closing;
    }
    return part;
}

typedef struct ParallelOutput ParallelOutput;
struct ParallelOutput
{
    int fd;
    // Anything but a regular file (a pipe, a terminal, a socket), whose writes may wait for its reader: each write into
    // it is at most PIPE_BUF bytes, once poll says it has room, which takes that many without waiting. A regular file
    // takes BUSTER_PARALLEL_CHUNK_BYTES at a time without waiting for anyone.
    bool pollable;
    // A write failed (EPIPE, when nothing reads it), or a signal dropped the rest of the report.
    bool lost;
};

// Writes what the reader takes now of `part`, from *offset, and moves *offset past it. A failed write loses the rest
// of the report.
static void parallel_output_write(ParallelOutput* output, String8 part, uint64_t* offset)
{
    const uint64_t limit = output->pollable ? PIPE_BUF : BUSTER_PARALLEL_CHUNK_BYTES;
    const uint64_t left = part.length - *offset;
    const ssize_t count = left != 0 ? write(output->fd, part.pointer + *offset, left < limit ? left : limit) : 0;
    if (count > 0)
    {
        *offset += (uint64_t)count;
    }
    else if (left != 0 && (count == 0 || (errno != EINTR && errno != EAGAIN)))
    {
        output->lost = true;
    }
}

// The user and system time in a process's resource usage, in nanoseconds.
static uint64_t parallel_cpu_time(const struct rusage* usage)
{
    const uint64_t seconds = (uint64_t)usage->ru_utime.tv_sec + (uint64_t)usage->ru_stime.tv_sec;
    const uint64_t microseconds = (uint64_t)usage->ru_utime.tv_usec + (uint64_t)usage->ru_stime.tv_usec;
    return seconds * BUSTER_PARALLEL_NANOSECONDS + microseconds * 1000;
}

// Reaps every running command whose shell has ended, at `now`, after killing whatever it left in its process group (its
// shell, a zombie until it is reaped, still holds the group's id), and builds its part of the report. Returns how many it
// reaped, whose CPUs it takes from *used; sets *full when the arena can't hold a part.
static uint64_t parallel_reap(Arena* arena, const ParallelList* list, const ParallelOptions* options, ParallelResult* results, uint64_t count, uint64_t now, uint64_t* used,
                              bool* full)
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
            struct rusage usage;
            memset(&usage, 0, sizeof(usage));
            pid_t waited = -1;
            do
            {
                waited = wait4(result->pid, &status, 0, &usage);
            } while (waited < 0 && errno == EINTR);
            result->status = status;
            result->cpu = waited == result->pid ? parallel_cpu_time(&usage) : 0;
            result->passed = waited == result->pid && WIFEXITED(status) && WEXITSTATUS(status) == 0 && !result->timed_out;
            result->state = PARALLEL_STATE_ENDED;
            result->end = now;
            *used -= result->cpus;
            *full = !parallel_render(arena, &list->commands[i], options, result) || *full;
            reaped += 1;
        }
    }
    return reaped;
}

// A stop signal. Before the run has finished, the first goes on to every running command's group, and stops the run
// (nothing more starts), and another kills them; once every command has ended and only the report's reader keeps the
// run, it drops the rest of the report.
static void parallel_stop(ParallelResult* results, uint64_t started, int signal, bool finished, uint64_t* stops, ParallelSummary* summary, ParallelOutput* output)
{
    summary->signal = summary->signal != 0 ? summary->signal : signal;
    *stops += 1;
    if (finished)
    {
        output->lost = true;
    }
    for (uint64_t i = 0; !finished && i < started; i += 1)
    {
        if (results[i].state == PARALLEL_STATE_RUNNING)
        {
            kill(-results[i].pid, *stops == 1 ? signal : SIGKILL);
            results[i].interrupted = true;
        }
    }
}

// Takes the signals the signalfd holds without waiting: each stop signal goes to parallel_stop, and SIGCHLD, which only
// says to reap, needs nothing more.
static void parallel_take_signals(int signal_fd, ParallelResult* results, uint64_t started, bool finished, uint64_t* stops, ParallelSummary* summary, ParallelOutput* output)
{
    struct signalfd_siginfo infos[8];
    bool reading = signal_fd >= 0;
    while (reading)
    {
        const ssize_t count = read(signal_fd, infos, sizeof(infos));
        reading = count > 0;
        for (uint64_t i = 0; reading && i < (uint64_t)count / sizeof(infos[0]); i += 1)
        {
            if ((int)infos[i].ssi_signo != SIGCHLD)
            {
                parallel_stop(results, started, (int)infos[i].ssi_signo, finished, stops, summary, output);
            }
        }
    }
}

// The CPUs a command holds while it runs: its own, or all of the `jobs` when it has more.
static uint64_t parallel_held(const ParallelCommand* command, uint64_t jobs)
{
    return command->cpus < jobs ? command->cpus : jobs;
}

// Runs the list's commands, writes the report into options->output, and returns what became of them, each command's in
// `results` (one per command); the report's parts are built in the arena.
static ParallelSummary parallel_run(Arena* arena, const ParallelList* list, const ParallelOptions* options, ParallelResult* results)
{
    ParallelSummary summary = { 0 };
    for (uint64_t i = 0; i < list->count; i += 1)
    {
        results[i] = (ParallelResult){ .pid = -1, .capture = -1 };
    }
    // The signals the run takes from its signalfd, blocked meanwhile: the commands' SIGCHLD, and the signals that stop
    // the run, unless they were ignored (as nohup does with SIGHUP). SIGCHLD must not be ignored either, or the kernel
    // would reap the commands itself. SIGPIPE, which a write into a pipe with no reader raises, is blocked too unless
    // it's ignored, so that the failed write stops the run rather than the signal ending the runner with its commands
    // left running.
    sigset_t handled;
    sigemptyset(&handled);
    sigaddset(&handled, SIGCHLD);
    const int stops_handled[] = {
        SIGINT,
        SIGTERM,
        SIGHUP,
    };
    for (uint64_t i = 0; i < sizeof(stops_handled) / sizeof(stops_handled[0]); i += 1)
    {
        struct sigaction current;
        if (sigaction(stops_handled[i], 0, &current) == 0 && current.sa_handler != SIG_IGN)
        {
            sigaddset(&handled, stops_handled[i]);
        }
    }
    sigset_t blocked = handled;
    struct sigaction pipe_action;
    const bool pipe_blocked = sigaction(SIGPIPE, 0, &pipe_action) == 0 && pipe_action.sa_handler != SIG_IGN;
    if (pipe_blocked)
    {
        sigaddset(&blocked, SIGPIPE);
    }
    sigset_t previous_mask;
    sigprocmask(SIG_BLOCK, &blocked, &previous_mask);
    struct sigaction child_default = { .sa_handler = SIG_DFL };
    struct sigaction previous_child;
    sigaction(SIGCHLD, &child_default, &previous_child);
    const int signal_fd = signalfd(-1, &handled, SFD_CLOEXEC | SFD_NONBLOCK);
    const int signal_error = signal_fd < 0 ? errno : 0;
    struct stat output_stats;
    ParallelOutput output = {
        .fd = options->output,
        .pollable = !(fstat(options->output, &output_stats) == 0 && S_ISREG(output_stats.st_mode)),
    };

    const uint64_t jobs = options->jobs != 0 ? options->jobs : 1;
    ParallelEnvironment environment;
    parallel_environment(arena, &environment);
    const uint64_t origin = parallel_clock();
    char line[BUSTER_PARALLEL_OUTCOME_BYTES];
    const int line_length = snprintf(line, sizeof(line), "parallel: %llu commands, at most %llu CPUs at once\n", (unsigned long long)list->count, (unsigned long long)jobs);
    const uint64_t opening_mark = arena->length;
    const bool opened = line_length > 0 && parallel_append(arena, line, (uint64_t)line_length);
    const String8 opening = opened ? (String8){ .pointer = (char*)arena->base + opening_mark, .length = (uint64_t)line_length }
                                   : (String8){ .pointer = (char*)parallel_lost_part, .length = sizeof(parallel_lost_part) - 1 };
    summary.memory_full = !opened;
    String8 closing = { 0 };
    uint64_t started = 0;
    uint64_t running = 0;
    // The CPUs the running commands hold.
    uint64_t used = 0;
    uint64_t stops = 0;
    // The part of the report being written, and how much of it is.
    uint64_t part = 0;
    uint64_t offset = 0;
    // Every command has ended or won't start, and the summary is built.
    bool finished = false;
    // The report was lost while commands ran, which were then killed.
    bool killed = false;
    bool done = false;
    while (!done)
    {
        // The next command in the list's order, once the running commands' CPUs leave room for its own (all of --jobs,
        // for one of more: it runs alone); until then, the commands after it wait too.
        while (stops == 0 && !killed && signal_fd >= 0 && started < list->count && used + parallel_held(&list->commands[started], jobs) <= jobs)
        {
            ParallelResult* result = &results[started];
            result->cpus = parallel_held(&list->commands[started], jobs);
            parallel_start(&list->commands[started], options, &previous_mask, &environment, result, parallel_clock() - origin);
            running += result->state == PARALLEL_STATE_RUNNING;
            used += result->state == PARALLEL_STATE_RUNNING ? result->cpus : 0;
            summary.peak = running > summary.peak ? running : summary.peak;
            // A command that couldn't start has ended.
            if (result->state == PARALLEL_STATE_ENDED)
            {
                summary.memory_full = !parallel_render(arena, &list->commands[started], options, result) || summary.memory_full;
            }
            started += 1;
        }
        if (!finished && running == 0 && (stops != 0 || killed || signal_fd < 0 || started == list->count))
        {
            // The run's end: what didn't start, a stop signal that came as the last command ended, then the summary.
            for (uint64_t i = started; i < list->count; i += 1)
            {
                summary.memory_full = !parallel_render(arena, &list->commands[i], options, &results[i]) || summary.memory_full;
            }
            parallel_take_signals(signal_fd, results, started, false, &stops, &summary, &output);
            summary.elapsed = parallel_clock() - origin;
            for (uint64_t i = 0; i < list->count; i += 1)
            {
                summary.passed += results[i].passed;
                summary.not_started += results[i].state == PARALLEL_STATE_WAITING;
                summary.cpu += results[i].cpu;
            }
            summary.failed = list->count - summary.passed - summary.not_started;
            closing = parallel_render_summary(arena, list, options, results, &summary, jobs, signal_error, &summary.memory_full);
            finished = true;
        }
        const String8 next = parallel_part(list, results, opening, closing, part);
        const bool pending = !output.lost && next.pointer != 0;
        done = finished && !pending;
        if (!done)
        {
            // Wait for a signal, the reader's room for the next part, or the next timeout; a regular file takes the part
            // at once.
            const uint64_t now = parallel_clock() - origin;
            uint64_t wait = pending && !output.pollable ? 0 : UINT64_MAX;
            for (uint64_t i = 0; options->timeout != 0 && i < started; i += 1)
            {
                const uint64_t deadline = results[i].start + options->timeout;
                if (results[i].state == PARALLEL_STATE_RUNNING && !results[i].timed_out)
                {
                    const uint64_t left = deadline > now ? deadline - now : 0;
                    wait = left < wait ? left : wait;
                }
            }
            const uint64_t milliseconds = wait == UINT64_MAX ? 0 : (wait + 999999) / 1000000;
            const int timeout = wait == UINT64_MAX ? -1 : milliseconds < INT32_MAX ? (int)milliseconds : INT32_MAX;
            struct pollfd fds[2] = {
                {
                    .fd = signal_fd,
                    .events = POLLIN,
                },
                {
                    .fd = output.fd,
                    .events = POLLOUT,
                },
            };
            const bool polling_output = pending && output.pollable;
            poll(fds, polling_output ? 2 : 1, timeout);
            parallel_take_signals(signal_fd, results, started, finished, &stops, &summary, &output);
            // Timeouts before reaping: a command still running at its deadline ran past it, though its shell may have
            // ended since.
            const uint64_t after = parallel_clock() - origin;
            for (uint64_t i = 0; options->timeout != 0 && i < started; i += 1)
            {
                if (results[i].state == PARALLEL_STATE_RUNNING && !results[i].timed_out && after >= results[i].start + options->timeout)
                {
                    kill(-results[i].pid, SIGKILL);
                    results[i].timed_out = true;
                }
            }
            running -= parallel_reap(arena, list, options, results, started, after, &used, &summary.memory_full);
            if (pending && !output.lost && (!polling_output || fds[1].revents != 0))
            {
                parallel_output_write(&output, next, &offset);
                if (!output.lost && offset == next.length)
                {
                    part += 1;
                    offset = 0;
                }
            }
            // A report that can't be written has no reader: the run stops, and its commands are killed.
            for (uint64_t i = 0; output.lost && !finished && !killed && i < started; i += 1)
            {
                if (results[i].state == PARALLEL_STATE_RUNNING)
                {
                    kill(-results[i].pid, SIGKILL);
                    results[i].interrupted = true;
                }
            }
            killed = killed || (output.lost && !finished);
        }
    }
    // A stop signal that came as the run ended fails it too. A write into a pipe with no reader left a SIGPIPE pending,
    // taken here before the mask is put back.
    summary.report_lost = output.lost;
    parallel_take_signals(signal_fd, results, started, true, &stops, &summary, &output);
    if (signal_fd >= 0)
    {
        close(signal_fd);
    }
    sigset_t pipe_signal;
    sigemptyset(&pipe_signal);
    sigaddset(&pipe_signal, SIGPIPE);
    const struct timespec zero = { 0 };
    while (pipe_blocked && sigtimedwait(&pipe_signal, 0, &zero) > 0)
    {
    }
    sigaction(SIGCHLD, &previous_child, 0);
    sigprocmask(SIG_SETMASK, &previous_mask, 0);
    summary.succeeded = summary.passed == list->count && summary.signal == 0 && !summary.report_lost && !summary.memory_full;
    return summary;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

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
                // By default a command per core of the CPUs this process may use (lane_core_count). Two commands on a core's
                // two SMT siblings slow each other down, and so do the lanes' waits, which spin while their group's other
                // lanes don't run. On the 9700X (8 cores, 15 CPUs online) the job's tests took 21.3 to 22.4 s with 8 jobs,
                // and 22.9 to 23.3 s with 15, in which test-lanes took 2.3 to 2.9 s rather than 0.2.
                options.jobs = options.jobs != 0 ? options.jobs : lane_core_count();
                fflush(stdout);
                result = parallel_run(&arena, &list, &options, results).succeeded ? 0 : 1;
            }
        }
        arena_release(&arena);
    }
    return result;
}
