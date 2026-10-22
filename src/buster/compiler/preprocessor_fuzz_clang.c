// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clang oracle for the original preprocessor fuzz cases. Ordinary cases share a process; process-global counter cases
// do not. Exact marker lines select each case's slice of a batch's output (preprocessor_fuzz_clang_split). In a
// batch that clang rejects, each error is the case's whose #line file name its location gives, and the others are
// accepted with their slices; a batch whose errors or markers don't all fit falls back to individual invocations.
// Process, file, marker and allocation failures never become rejections. preprocessor_fuzz_batch_source builds the
// text clang reads, which test-preprocessor --fuzz gives the preprocessor too (one case at a time). The subprocess
// boundary is deliberately outside the compiler hot path; benchmark_sample measures this harness cost.

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#define BUSTER_PPF_CLANG_BATCH_COUNT 64
#define BUSTER_PPF_CLANG_DIAGNOSTIC_BYTES (64ull << 10)
#define BUSTER_PPF_CLANG_TIMEOUT_SECONDS 30
#define BUSTER_PPF_CLANG_SCRATCH_BYTES (1ull << 30)
#define BUSTER_PPF_CLANG_MARKER_PREFIX "BUSTER_PPF_END_"

extern char** environ;

typedef struct PreprocessorFuzzProcess PreprocessorFuzzProcess;
struct PreprocessorFuzzProcess
{
    String8 output;
    String8 diagnostic;
    int exit_code;
    bool valid;
};

static bool preprocessor_fuzz_clang_read(Arena* arena, int descriptor, uint64_t limit, String8* text)
{
    struct stat metadata;
    bool result = fstat(descriptor, &metadata) == 0 && S_ISREG(metadata.st_mode) && metadata.st_size >= 0 && (uint64_t)metadata.st_size <= limit;
    char* data = result ? arena_push(arena, (uint64_t)metadata.st_size + 1, 1) : 0;
    result = result && data != 0;
    uint64_t length = result ? (uint64_t)metadata.st_size : 0;
    uint64_t position = 0;
    while (result && position < length)
    {
        const ssize_t amount = pread(descriptor, data + position, length - position, (off_t)position);
        if (amount > 0)
        {
            position += (uint64_t)amount;
        }
        else if (amount == 0 || errno != EINTR)
        {
            result = false;
        }
    }
    if (result)
    {
        data[length] = 0;
        *text = (String8)
        {
            .pointer = data,
            .length = length,
        };
    }
    return result;
}

// Regular files avoid pipe backpressure. A pidfd wakes the parent immediately on completion, while the timeout also
// bounds a stuck oracle. The periodic size check bounds runaway output without imposing a polling delay per launch.
static PreprocessorFuzzProcess preprocessor_fuzz_clang_process(Arena* arena, char* const* arguments, String8 input, uint64_t output_limit)
{
    PreprocessorFuzzProcess result = {0};
    // A batch of rejected cases gives diagnostics for each (-ferror-limit=0): as much room as its output has.
    const uint64_t diagnostic_limit = output_limit > BUSTER_PPF_CLANG_DIAGNOSTIC_BYTES ? output_limit : BUSTER_PPF_CLANG_DIAGNOSTIC_BYTES;
    char directory[PATH_MAX];
    char paths[3][PATH_MAX] = {0};
    int descriptors[3] =
    {
        -1,
        -1,
        -1,
    };
    const char* temporary = getenv("TMPDIR");
    if (temporary == 0 || temporary[0] == 0)
    {
        temporary = "/tmp";
    }
    const int directory_length = snprintf(directory, sizeof(directory), "%s/buster-ppf-XXXXXX", temporary);
    bool valid = directory_length > 0 && (uint64_t)directory_length < sizeof(directory);
    bool directory_created = valid && mkdtemp(directory) != 0;
    valid = valid && directory_created;
    for (uint64_t i = 0; valid && i < 3; ++i)
    {
        const int length = snprintf(paths[i], sizeof(paths[i]), "%s/%llu", directory, (unsigned long long)i);
        valid = length > 0 && (uint64_t)length < sizeof(paths[i]);
        if (valid)
        {
            descriptors[i] = open(paths[i], O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            valid = descriptors[i] >= 0;
            if (valid && descriptors[i] < 3)
            {
                const int original = descriptors[i];
                descriptors[i] = fcntl(original, F_DUPFD_CLOEXEC, 3);
                valid = close(original) == 0 && descriptors[i] >= 0;
            }
        }
    }
    uint64_t written = 0;
    while (valid && written < input.length)
    {
        const ssize_t amount = write(descriptors[0], input.pointer + written, input.length - written);
        if (amount > 0)
        {
            written += (uint64_t)amount;
        }
        else if (amount == 0 || errno != EINTR)
        {
            valid = false;
        }
    }
    if (valid)
    {
        valid = lseek(descriptors[0], 0, SEEK_SET) == 0;
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    const bool actions_ready = valid && posix_spawn_file_actions_init(&actions) == 0;
    const bool attributes_ready = valid && posix_spawnattr_init(&attributes) == 0;
    valid = valid && actions_ready && attributes_ready;
    for (int i = 0; valid && i < 3; ++i)
    {
        valid = posix_spawn_file_actions_adddup2(&actions, descriptors[i], i) == 0;
        valid = valid && posix_spawn_file_actions_addclose(&actions, descriptors[i]) == 0;
    }
    if (valid)
    {
        valid = posix_spawnattr_setpgroup(&attributes, 0) == 0 && posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) == 0;
    }
    pid_t child = -1;
    if (valid)
    {
        pid_t spawned = -1;
        valid = posix_spawnp(&spawned, arguments[0], &actions, &attributes, arguments, environ) == 0;
        if (valid)
        {
            child = spawned;
        }
    }
    if (actions_ready)
    {
        valid = posix_spawn_file_actions_destroy(&actions) == 0 && valid;
    }
    if (attributes_ready)
    {
        valid = posix_spawnattr_destroy(&attributes) == 0 && valid;
    }
    int child_descriptor = valid ? (int)syscall(SYS_pidfd_open, child, 0) : -1;
    valid = valid && child_descriptor >= 0;
    struct timespec start;
    valid = valid && clock_gettime(CLOCK_MONOTONIC, &start) == 0;
    bool reaped = false;
    int status = 0;
    while (valid && !reaped)
    {
        struct pollfd item =
        {
            .fd = child_descriptor,
            .events = POLLIN,
        };
        const int ready = poll(&item, 1, 50);
        valid = (ready >= 0 || errno == EINTR) && (item.revents & (POLLERR | POLLNVAL)) == 0;
        struct stat output_metadata;
        struct stat diagnostic_metadata;
        struct timespec now;
        if (valid)
        {
            valid = fstat(descriptors[1], &output_metadata) == 0 && fstat(descriptors[2], &diagnostic_metadata) == 0 && clock_gettime(CLOCK_MONOTONIC, &now) == 0;
        }
        if (valid)
        {
            const bool timed_out = now.tv_sec > start.tv_sec + BUSTER_PPF_CLANG_TIMEOUT_SECONDS ||
                                   (now.tv_sec == start.tv_sec + BUSTER_PPF_CLANG_TIMEOUT_SECONDS && now.tv_nsec >= start.tv_nsec);
            valid = !timed_out && output_metadata.st_size >= 0 && (uint64_t)output_metadata.st_size <= output_limit &&
                    diagnostic_metadata.st_size >= 0 && (uint64_t)diagnostic_metadata.st_size <= diagnostic_limit;
        }
        if (valid)
        {
            const pid_t waited = waitpid(child, &status, WNOHANG);
            reaped = waited == child;
            valid = waited >= 0 || errno == EINTR;
        }
    }
    if (child > 0 && !reaped)
    {
        // The group contains only this oracle and its descendants, never another fuzz call.
        kill(-child, SIGKILL);
        kill(child, SIGKILL);
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        valid = false;
    }
    else if (child > 0 && (!WIFEXITED(status) || WEXITSTATUS(status) != 0))
    {
        // waitpid can reap a failed driver while its frontend still owns the redirected files.
        kill(-child, SIGKILL);
    }
    if (valid)
    {
        valid = WIFEXITED(status);
        if (valid)
        {
            result.exit_code = WEXITSTATUS(status);
            valid = preprocessor_fuzz_clang_read(arena, descriptors[1], output_limit, &result.output);
            valid = valid && preprocessor_fuzz_clang_read(arena, descriptors[2], diagnostic_limit, &result.diagnostic);
        }
    }
    if (child_descriptor >= 0)
    {
        valid = close(child_descriptor) == 0 && valid;
    }
    for (uint64_t i = 0; i < 3; ++i)
    {
        if (descriptors[i] >= 0)
        {
            valid = close(descriptors[i]) == 0 && valid;
        }
        if (paths[i][0] != 0)
        {
            const int removed = unlink(paths[i]);
            valid = (removed == 0 || errno == ENOENT) && valid;
        }
    }
    if (directory_created)
    {
        valid = rmdir(directory) == 0 && valid;
    }
    result.valid = valid;
    return result;
}

static bool preprocessor_fuzz_clang_contains(String8 text, String8 needle)
{
    bool found = false;
    for (uint64_t i = 0; !found && needle.length <= text.length && i <= text.length - needle.length; ++i)
    {
        found = memcmp(text.pointer + i, needle.pointer, needle.length) == 0;
    }
    return found;
}

static bool preprocessor_fuzz_clang_source_error(String8 diagnostic)
{
    bool source_error = false;
    bool driver_error = false;
    bool crashed = false;
    uint64_t begin = 0;
    while (begin < diagnostic.length)
    {
        uint64_t end = begin;
        while (end < diagnostic.length && diagnostic.pointer[end] != '\n')
        {
            ++end;
        }
        bool severity_found = false;
        for (uint64_t i = begin; !severity_found && i < end; ++i)
        {
            const uint64_t remaining = end - i;
            const char* current = diagnostic.pointer + i;
            const bool error = (remaining >= 8 && memcmp(current, ": error:", 8) == 0) ||
                               (remaining >= 14 && memcmp(current, ": fatal error:", 14) == 0);
            const bool other = (remaining >= 10 && memcmp(current, ": warning:", 10) == 0) ||
                               (remaining >= 7 && memcmp(current, ": note:", 7) == 0);
            severity_found = error || other;
            if (error)
            {
                uint64_t location = i;
                while (location > begin && diagnostic.pointer[location - 1] >= '0' && diagnostic.pointer[location - 1] <= '9')
                {
                    --location;
                }
                // Require the error's own numeric source location, including a user #line filename. A warning
                // mentioning a filename on another line never makes a driver-level error a source rejection.
                const bool positioned = location < i && location > begin && diagnostic.pointer[location - 1] == ':';
                source_error |= positioned;
                driver_error |= !positioned;
            }
        }
        // An error at no location: the frontend's for a source position it couldn't name (clang's driver prefixes its own
        // with "clang: "), as `__has_include` after an empty argument at a macro's end gives.
        const bool unplaced = !severity_found && ((end - begin >= 7 && memcmp(diagnostic.pointer + begin, "error: ", 7) == 0) ||
                                                  (end - begin >= 13 && memcmp(diagnostic.pointer + begin, "fatal error: ", 13) == 0));
        source_error |= unplaced;
        if (!severity_found && !unplaced)
        {
            const String8 line =
            {
                .pointer = diagnostic.pointer + begin,
                .length = end - begin,
            };
            crashed |= preprocessor_fuzz_clang_contains(line, BUSTER_S8("PLEASE submit a bug report")) ||
                       preprocessor_fuzz_clang_contains(line, BUSTER_S8("clang frontend command failed")) ||
                       preprocessor_fuzz_clang_contains(line, BUSTER_S8("unable to execute command")) ||
                       preprocessor_fuzz_clang_contains(line, BUSTER_S8("LLVM ERROR:"));
        }
        begin = end + (end < diagnostic.length);
    }
    const bool result = source_error && !driver_error && !crashed;
    return result;
}

static bool preprocessor_fuzz_clang_version(const char* clang_path)
{
    Arena arena = arena_reserve(1ull << 20);
    bool result = arena.base != 0 && clang_path != 0 && clang_path[0] != 0;
    if (result)
    {
        char* arguments[] =
        {
            (char*)clang_path,
            "--no-default-config",
            "--version",
            0,
        };
        const PreprocessorFuzzProcess process = preprocessor_fuzz_clang_process(&arena, arguments, BUSTER_S8(""), BUSTER_PPF_CLANG_DIAGNOSTIC_BYTES);
        result = process.valid && process.exit_code == 0 && preprocessor_fuzz_clang_contains(process.output, BUSTER_S8("clang version 23."));
    }
    arena_release(&arena);
    return result;
}

bool preprocessor_fuzz_benchmark_version(const char* clang_path)
{
    const bool result = preprocessor_fuzz_clang_version(clang_path);
    return result;
}

static bool preprocessor_fuzz_clang_space(char value)
{
    const bool result = value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' || value == '\f';
    return result;
}

// Whether a byte can be part of an identifier in clang's -P text (a UCN's backslash and UTF-8 bytes too).
static bool preprocessor_fuzz_clang_identifier_byte(char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '_' || value == '$' || value == '\\' ||
           (unsigned char)value >= 0x80;
}

// Splits a batch's text at its markers. Each ends its printed line (the batch puts it on a line of its own, after two
// newlines; -P starts a line for it, but for what an expansion printed before it, which can put it at a line's end),
// and is found by its bytes, as a whole identifier outside literals, rather than by lexing the text: a case's output
// can hold what doesn't lex back, as an unterminated character constant that took the rest of its source line and
// then ends at a quote further on its printed line, after which a `/*` would take the marker into a comment. Any other
// marker, or one out of its place, fails the split: the generator keeps the names to the markers.
static bool preprocessor_fuzz_clang_split(Arena* arena, String8 text, uint64_t count, PreprocessorFuzzOutput* outputs)
{
    const String8 prefix = BUSTER_S8(BUSTER_PPF_CLANG_MARKER_PREFIX);
    bool result = true;
    uint64_t begin = 0;
    uint64_t marker_count = 0;
    uint64_t line = 0;
    while (result && line < text.length)
    {
        uint64_t line_end = line;
        while (line_end < text.length && text.pointer[line_end] != '\n')
        {
            line_end += 1;
        }
        uint64_t last = line_end;
        while (last > line && preprocessor_fuzz_clang_space(text.pointer[last - 1]))
        {
            last -= 1;
        }
        // The markers on the line: every whole identifier that is the prefix and digits, outside string and character
        // literals (one runs to its closing quote, or to the line's end).
        bool ends_with_marker = false;
        uint64_t marker_start = last;
        char quote = 0;
        for (uint64_t at = line; result && at < last; at += 1)
        {
            const char value = text.pointer[at];
            if (quote != 0)
            {
                at += value == '\\';
                quote = value == quote ? 0 : quote;
            }
            else if (value == '"' || value == '\'')
            {
                quote = value;
            }
            else if (value == prefix.pointer[0] && last - at > prefix.length && memcmp(text.pointer + at, prefix.pointer, prefix.length) == 0 &&
                     (at == line || !preprocessor_fuzz_clang_identifier_byte(text.pointer[at - 1])))
            {
                uint64_t end = at + prefix.length;
                uint64_t number = 0;
                while (end < last && text.pointer[end] >= '0' && text.pointer[end] <= '9')
                {
                    number = number * 10 + (uint64_t)(text.pointer[end] - '0');
                    end += 1;
                }
                if (end > at + prefix.length && (end == last || !preprocessor_fuzz_clang_identifier_byte(text.pointer[end])))
                {
                    ends_with_marker = end == last && number == marker_count && marker_count < count;
                    marker_start = at;
                    result = ends_with_marker;
                }
                at = end - 1;
            }
        }
        if (result && ends_with_marker)
        {
            uint64_t end = marker_start;
            while (begin < end && preprocessor_fuzz_clang_space(text.pointer[begin]))
            {
                ++begin;
            }
            while (end > begin && preprocessor_fuzz_clang_space(text.pointer[end - 1]))
            {
                --end;
            }
            const uint64_t bytes = end - begin;
            char* copy = bytes <= BUSTER_PPF_MAX_OUTPUT_BYTES ? arena_push(arena, bytes + 1, 1) : 0;
            result = copy != 0;
            if (result)
            {
                memcpy(copy, text.pointer + begin, bytes);
                copy[bytes] = 0;
                outputs[marker_count] = (PreprocessorFuzzOutput)
                {
                    .text =
                    {
                        .pointer = copy,
                        .length = bytes,
                    },
                    .accepted = true,
                    .valid = true,
                };
                ++marker_count;
                begin = line_end;
            }
        }
        line = line_end + 1;
    }
    result = result && marker_count == count;
    for (uint64_t i = begin; result && i < text.length; ++i)
    {
        result = preprocessor_fuzz_clang_space(text.pointer[i]);
    }
    return result;
}

// The text a batch of cases is preprocessed as: each case's #line, its source and its marker, into `source` (room for
// BUSTER_PPF_MAX_CASE_BYTES + 128 bytes per case). Returns false if it doesn't fit.
static bool preprocessor_fuzz_batch_source(PreprocessorFuzzBuffer* source, const PreprocessorFuzzCase* const* cases, uint64_t count)
{
    bool result = source->valid;
    for (uint64_t i = 0; result && i < count; ++i)
    {
        result = preprocessor_fuzz_format(source, "#line 1 \"ppf_%llu.c\"\n", (unsigned long long)cases[i]->id);
        result = result && preprocessor_fuzz_append(source, cases[i]->source);
        // Two newlines also terminate a final escaped newline before the marker's own logical line.
        result = result && preprocessor_fuzz_format(source, "\n\n" BUSTER_PPF_CLANG_MARKER_PREFIX "%llu\n", (unsigned long long)i);
    }
    return result;
}

// Which cases of a rejected batch the errors in clang's diagnostics belong to: an error's location starts with the file
// name its case's #line gives, "ppf_<id>.c:<line>:<column>: error:". Sets errors[i] for case i. Returns false if an
// error names no case's file (or has no location), a case shares its id with another, or an error is fatal (after which
// clang reports nothing more, though it goes on printing), so that the batch must run case by case.
static bool preprocessor_fuzz_clang_attribute(String8 diagnostic, const PreprocessorFuzzCase* const* cases, uint64_t count, bool* errors)
{
    bool result = true;
    for (uint64_t i = 0; i < count; ++i)
    {
        errors[i] = false;
        for (uint64_t j = 0; result && j < i; ++j)
        {
            result = cases[i]->id != cases[j]->id;
        }
    }
    uint64_t begin = 0;
    while (result && begin < diagnostic.length)
    {
        uint64_t end = begin;
        while (end < diagnostic.length && diagnostic.pointer[end] != '\n')
        {
            ++end;
        }
        const String8 line =
        {
            .pointer = diagnostic.pointer + begin,
            .length = end - begin,
        };
        const bool fatal = preprocessor_fuzz_clang_contains(line, BUSTER_S8(": fatal error:"));
        const bool error = fatal || preprocessor_fuzz_clang_contains(line, BUSTER_S8(": error:"));
        // An error at no location names no case.
        const bool unplaced = (line.length >= 7 && memcmp(line.pointer, "error: ", 7) == 0) || (line.length >= 13 && memcmp(line.pointer, "fatal error: ", 13) == 0);
        result = !fatal && !unplaced;
        if (result && error)
        {
            // "ppf_", the id's digits, ".c:", then the line's digits and ':'.
            uint64_t at = 4;
            uint64_t id = 0;
            bool named = line.length > 4 && memcmp(line.pointer, "ppf_", 4) == 0;
            while (named && at < line.length && line.pointer[at] >= '0' && line.pointer[at] <= '9' && id <= (UINT64_MAX - 9) / 10)
            {
                id = id * 10 + (uint64_t)(line.pointer[at] - '0');
                ++at;
            }
            named = named && at > 4 && at + 4 <= line.length && memcmp(line.pointer + at, ".c:", 3) == 0 && line.pointer[at + 3] >= '0' && line.pointer[at + 3] <= '9';
            bool found = false;
            for (uint64_t i = 0; named && !found && i < count; ++i)
            {
                found = cases[i]->id == id;
                errors[i] |= found;
            }
            result = found;
        }
        begin = end + (end < diagnostic.length);
    }
    return result;
}

static bool preprocessor_fuzz_clang_group(Arena* arena, Arena* scratch, const char* clang_path, bool c23, const PreprocessorFuzzCase* const* cases, uint64_t count, PreprocessorFuzzOutput* outputs, bool* rejected)
{
    const uint64_t scratch_begin = scratch->length;
    const uint64_t capacity = count * (BUSTER_PPF_MAX_CASE_BYTES + 128);
    PreprocessorFuzzBuffer source =
    {
        .data = arena_push(scratch, capacity, 1),
        .capacity = capacity,
        .valid = true,
    };
    source.valid = source.data != 0;
    bool result = preprocessor_fuzz_batch_source(&source, cases, count);
    *rejected = false;
    if (result)
    {
        char* arguments[] =
        {
            (char*)clang_path,
            "--no-default-config",
            "-E",
            "-P",
            "-undef",
            "-nostdinc",
            c23 ? "-std=c23" : "-std=c17",
            "-fno-color-diagnostics",
            "-fno-caret-diagnostics",
            "-ferror-limit=0",
            "-x",
            "c",
            "-",
            0,
        };
        const String8 input =
        {
            .pointer = source.data,
            .length = source.length,
        };
        const uint64_t output_limit = count * (BUSTER_PPF_MAX_OUTPUT_BYTES + 128);
        const PreprocessorFuzzProcess process = preprocessor_fuzz_clang_process(scratch, arguments, input, output_limit);
        result = process.valid;
        if (result && process.exit_code == 0)
        {
            result = preprocessor_fuzz_clang_split(arena, process.output, count, outputs);
        }
        else if (result)
        {
            // A failed driver or crashed frontend can exit 1 too. Only a diagnostic tied to a source position
            // represents a rejection; argv/file/process failures remain infrastructure errors.
            *rejected = process.exit_code == 1 && preprocessor_fuzz_clang_source_error(process.diagnostic);
            result = *rejected;
            if (result && count == 1)
            {
                outputs[0] = (PreprocessorFuzzOutput)
                {
                    .accepted = false,
                    .valid = true,
                };
            }
            else if (result)
            {
                // Each error is its case's, and every case's slice is between the markers: the rest are accepted.
                // Otherwise *rejected stays, and the caller runs the cases one at a time.
                bool errors[BUSTER_PPF_CLANG_BATCH_COUNT];
                const uint64_t outputs_begin = arena->length;
                const bool split = count <= BUSTER_PPF_CLANG_BATCH_COUNT && preprocessor_fuzz_clang_attribute(process.diagnostic, cases, count, errors) &&
                                   preprocessor_fuzz_clang_split(arena, process.output, count, outputs);
                for (uint64_t i = 0; split && i < count; ++i)
                {
                    if (errors[i])
                    {
                        outputs[i] = (PreprocessorFuzzOutput)
                        {
                            .accepted = false,
                            .valid = true,
                        };
                    }
                }
                if (!split)
                {
                    arena_pop_to(arena, outputs_begin);
                }
                *rejected = !split;
            }
        }
    }
    arena_pop_to(scratch, scratch_begin);
    return result;
}

static bool preprocessor_fuzz_clang_flush(Arena* arena, Arena* scratch, const char* clang_path, bool c23, const PreprocessorFuzzCase* const* cases, const uint64_t* indices, uint64_t count, PreprocessorFuzzOutput* outputs)
{
    PreprocessorFuzzOutput temporary[BUSTER_PPF_CLANG_BATCH_COUNT] = {0};
    bool rejected = false;
    bool result = preprocessor_fuzz_clang_group(arena, scratch, clang_path, c23, cases, count, temporary, &rejected);
    if (result && rejected && count > 1)
    {
        for (uint64_t i = 0; result && i < count; ++i)
        {
            result = preprocessor_fuzz_clang_group(arena, scratch, clang_path, c23, cases + i, 1, temporary + i, &rejected);
        }
    }
    for (uint64_t i = 0; result && i < count; ++i)
    {
        outputs[indices[i]] = temporary[i];
    }
    return result;
}

static bool preprocessor_fuzz_clang(Arena* arena, const char* clang_path, bool c23, const PreprocessorFuzzCase* cases, uint64_t count, bool individual, PreprocessorFuzzOutput* outputs)
{
    bool result = arena != 0 && clang_path != 0 && clang_path[0] != 0 && (count == 0 || (cases != 0 && outputs != 0)) && count <= SIZE_MAX / sizeof(*outputs);
    const uint64_t arena_begin = arena != 0 ? arena->length : 0;
    if (result && count != 0)
    {
        memset(outputs, 0, count * sizeof(*outputs));
        for (uint64_t i = 0; result && i < count; ++i)
        {
            result = cases[i].source.length <= BUSTER_PPF_MAX_CASE_BYTES && (cases[i].source.length == 0 || cases[i].source.pointer != 0);
        }
    }
    Arena scratch = result && count != 0 ? arena_reserve(BUSTER_PPF_CLANG_SCRATCH_BYTES) : (Arena){0};
    result = result && (count == 0 || scratch.base != 0);
    const PreprocessorFuzzCase* pending[BUSTER_PPF_CLANG_BATCH_COUNT];
    uint64_t indices[BUSTER_PPF_CLANG_BATCH_COUNT];
    uint64_t pending_count = 0;
    for (uint64_t i = 0; result && i < count; ++i)
    {
        if (individual || cases[i].isolated)
        {
            const PreprocessorFuzzCase* single = cases + i;
            result = preprocessor_fuzz_clang_flush(arena, &scratch, clang_path, c23, &single, &i, 1, outputs);
        }
        else
        {
            pending[pending_count] = cases + i;
            indices[pending_count] = i;
            ++pending_count;
            if (pending_count == BUSTER_PPF_CLANG_BATCH_COUNT)
            {
                result = preprocessor_fuzz_clang_flush(arena, &scratch, clang_path, c23, pending, indices, pending_count, outputs);
                pending_count = 0;
            }
        }
    }
    if (result && pending_count != 0)
    {
        result = preprocessor_fuzz_clang_flush(arena, &scratch, clang_path, c23, pending, indices, pending_count, outputs);
    }
    arena_release(&scratch);
    if (!result && arena != 0 && arena_begin <= arena->length && outputs != 0 && count <= SIZE_MAX / sizeof(*outputs))
    {
        arena_pop_to(arena, arena_begin);
        memset(outputs, 0, count * sizeof(*outputs));
    }
    return result;
}

bool preprocessor_fuzz_benchmark_sample(const char* clang_path, bool c23, uint64_t seed, uint64_t count, bool individual, uint64_t* accepted, uint64_t* bytes)
{
    uint64_t capacity = 0;
    bool result = accepted != 0 && bytes != 0 && count <= UINT64_MAX / BUSTER_PPF_FAMILY_CYCLE &&
                  !__builtin_mul_overflow(count, BUSTER_PPF_MAX_CASE_BYTES + BUSTER_PPF_MAX_OUTPUT_BYTES + sizeof(PreprocessorFuzzCase) + sizeof(PreprocessorFuzzOutput) + 128, &capacity) &&
                  !__builtin_add_overflow(capacity, 1ull << 20, &capacity);
    Arena arena = result ? arena_reserve(capacity) : (Arena){0};
    result = result && arena.base != 0;
    PreprocessorFuzzCase* cases = result ? BUSTER_ARENA_PUSH_ARRAY(&arena, PreprocessorFuzzCase, count) : 0;
    PreprocessorFuzzOutput* outputs = result ? BUSTER_ARENA_PUSH_ARRAY(&arena, PreprocessorFuzzOutput, count) : 0;
    result = result && cases != 0 && outputs != 0;
    uint64_t accepted_count = 0;
    uint64_t output_bytes = 0;
    for (uint64_t i = 0; result && i < count; ++i)
    {
        result = preprocessor_fuzz_generate(&arena, seed, i * BUSTER_PPF_FAMILY_CYCLE, c23, cases + i);
    }
    if (result)
    {
        result = preprocessor_fuzz_clang(&arena, clang_path, c23, cases, count, individual, outputs);
    }
    for (uint64_t i = 0; result && i < count; ++i)
    {
        result = outputs[i].valid && outputs[i].accepted && !__builtin_add_overflow(output_bytes, outputs[i].text.length, &output_bytes);
        accepted_count += result;
    }
    if (accepted != 0)
    {
        *accepted = result ? accepted_count : 0;
    }
    if (bytes != 0)
    {
        *bytes = result ? output_bytes : 0;
    }
    arena_release(&arena);
    return result;
}
