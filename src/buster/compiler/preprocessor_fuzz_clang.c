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

// A token of clang's own, after preprocessing (preprocessor_fuzz_clang_tokens): its kind as clang names it
// ("identifier", "unknown", "string_literal", ...) and its spelling.
typedef struct PreprocessorFuzzClangToken PreprocessorFuzzClangToken;
struct PreprocessorFuzzClangToken
{
    String8 kind;
    String8 spelling;
};

typedef struct PreprocessorFuzzClangTokens PreprocessorFuzzClangTokens;
struct PreprocessorFuzzClangTokens
{
    PreprocessorFuzzClangToken* tokens;
    uint64_t count;
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

static bool preprocessor_fuzz_benchmark_version(const char* clang_path)
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

// One token of clang's -dump-tokens text, its first line `line`: its kind as clang names it, padding, its spelling
// between quotes, padding, "Loc=<", its location and flags. The spelling is escaped as raw_ostream::write_escaped
// escapes it (`\\`, `\t`, `\n`, `\"` and three octal digits for any other byte that isn't printable), and ends at the
// one quote that padding and "Loc=<" follow: a line with no such quote, or more than one, can't be read. An annotation
// has no spelling. Copies the kind and the unescaped spelling into `arena`, and sets *location_start to where "Loc=<"
// starts. Returns false if the line can't be read.
static bool preprocessor_fuzz_clang_dump_token(Arena* arena, String8 line, PreprocessorFuzzClangToken* token, uint64_t* location_start)
{
    // A kind is a name: an identifier's, a keyword's spelling, a punctuator's (`l_paren`) or an annotation's.
    uint64_t kind_end = 0;
    bool naming = true;
    while (naming && kind_end < line.length)
    {
        const char c = line.pointer[kind_end];
        naming = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        kind_end += naming;
    }
    uint64_t at = kind_end;
    while (at < line.length && line.pointer[at] == ' ')
    {
        at += 1;
    }
    const String8 location = BUSTER_S8("Loc=<");
    const bool spelled = at < line.length && line.pointer[at] == '\'';
    const bool annotation = !spelled && line.length - at >= location.length && memcmp(line.pointer + at, location.pointer, location.length) == 0;
    bool result = kind_end != 0 && at > kind_end && (spelled || annotation);
    // The quote that ends the spelling: padding and "Loc=<" after it.
    uint64_t close = 0;
    uint64_t closes = 0;
    for (uint64_t q = at + 1; result && spelled && q < line.length; q += 1)
    {
        uint64_t after = q + 1;
        while (after < line.length && line.pointer[after] == ' ')
        {
            after += 1;
        }
        const bool ends = line.pointer[q] == '\'' && after > q + 1 && line.length - after >= location.length && memcmp(line.pointer + after, location.pointer, location.length) == 0;
        close = ends ? q : close;
        closes += ends;
    }
    result = result && (annotation || closes == 1);
    const uint64_t escaped = spelled && result ? close - at - 1 : 0;
    uint64_t after = spelled ? close + 1 : at;
    while (result && after < line.length && line.pointer[after] == ' ')
    {
        after += 1;
    }
    *location_start = after;
    char* kind = result ? arena_push(arena, kind_end, 1) : 0;
    uint8_t* spelling = kind != 0 ? arena_push(arena, escaped + 1, 1) : 0;
    result = result && spelling != 0;
    if (result)
    {
        memcpy(kind, line.pointer, kind_end);
    }
    uint64_t length = 0;
    for (uint64_t i = at + 1; result && i < at + 1 + escaped; i += 1)
    {
        const char c = line.pointer[i];
        const char next = i + 1 < at + 1 + escaped ? line.pointer[i + 1] : 0;
        const bool octal = c == '\\' && i + 3 < at + 1 + escaped && next >= '0' && next <= '3' && line.pointer[i + 2] >= '0' && line.pointer[i + 2] <= '7' &&
                           line.pointer[i + 3] >= '0' && line.pointer[i + 3] <= '7';
        if (c != '\\')
        {
            spelling[length] = (uint8_t)c;
        }
        else if (octal)
        {
            spelling[length] = (uint8_t)((next - '0') * 64 + (line.pointer[i + 2] - '0') * 8 + (line.pointer[i + 3] - '0'));
            i += 3;
        }
        else
        {
            result = next == '\\' || next == 't' || next == 'n' || next == '"';
            spelling[length] = next == 't' ? '\t' : next == 'n' ? '\n' : (uint8_t)next;
            i += 1;
        }
        length += 1;
    }
    if (result)
    {
        *token = (PreprocessorFuzzClangToken){
            .kind = {
                .pointer = kind,
                .length = kind_end,
            },
            .spelling = {
                .pointer = (char*)spelling,
                .length = length,
            },
        };
    }
    return result;
}

// Clang's tokens in its -dump-tokens text `dump`, a line each (preprocessor_fuzz_clang_dump_token), into `arena`: the
// case's, before the marker preprocessor_fuzz_batch_source writes after it, which must be the last token but the end
// of file. A token that needs cleaning shows its bytes as written last ([UnClean='...']), unescaped, so that its line
// splices' newlines go on to more lines; each of those but the last ends in its splice's `\` (or a trigraph's), so
// the flag ends at the first line that ends in "']". Returns false if the text can't be read.
static bool preprocessor_fuzz_clang_dump(Arena* arena, String8 dump, PreprocessorFuzzClangTokens* result)
{
    const uint64_t saved_length = arena->length;
    uint64_t lines = 0;
    for (uint64_t i = 0; i < dump.length; i += 1)
    {
        lines += dump.pointer[i] == '\n';
    }
    PreprocessorFuzzClangTokens tokens = {
        .tokens = lines != 0 ? arena_push_array(arena, lines, sizeof(PreprocessorFuzzClangToken), _Alignof(PreprocessorFuzzClangToken)) : 0,
    };
    bool success = tokens.tokens != 0 && dump.pointer[dump.length - 1] == '\n';
    uint64_t begin = 0;
    while (success && begin < dump.length)
    {
        uint64_t end = begin;
        while (dump.pointer[end] != '\n')
        {
            end += 1;
        }
        const String8 line = {
            .pointer = dump.pointer + begin,
            .length = end - begin,
        };
        uint64_t location = 0;
        success = preprocessor_fuzz_clang_dump_token(arena, line, &tokens.tokens[tokens.count], &location);
        const String8 flags = {
            .pointer = line.pointer + location,
            .length = line.length - location,
        };
        bool open = success && preprocessor_fuzz_clang_contains(flags, BUSTER_S8("[UnClean='"));
        while (open)
        {
            open = end - begin < 2 || dump.pointer[end - 2] != '\'' || dump.pointer[end - 1] != ']';
            begin = end + 1;
            success = !open || begin < dump.length;
            end = open && success ? begin : end;
            while (open && success && dump.pointer[end] != '\n')
            {
                end += 1;
            }
            open = open && success;
        }
        tokens.count += success;
        begin = end + 1;
    }
    const String8 marker = BUSTER_S8(BUSTER_PPF_CLANG_MARKER_PREFIX "0");
    const PreprocessorFuzzClangToken* last = success && tokens.count >= 2 ? &tokens.tokens[tokens.count - 2] : 0;
    success = last != 0 && last->spelling.length == marker.length && memcmp(last->spelling.pointer, marker.pointer, marker.length) == 0;
    success = success && last[1].kind.length == 3 && memcmp(last[1].kind.pointer, "eof", 3) == 0;
    if (success)
    {
        tokens.count -= 2;
        *result = tokens;
    }
    else
    {
        arena_pop_to(arena, saved_length);
    }
    return success;
}

// Clang's own tokens for case `test_case` alone, as its -dump-tokens action gives them after preprocessing, with -E's
// options, into `arena` (preprocessor_fuzz_clang_dump). They show what clang's -E text can't: where a token ends that
// its text runs on past, as an unterminated literal runs on to its line's end. A pragma's tokens aren't among them.
// Returns false if clang fails, rejects the case or its text can't be read.
static bool preprocessor_fuzz_clang_tokens(Arena* arena, const char* clang_path, bool c23, const PreprocessorFuzzCase* test_case, PreprocessorFuzzClangTokens* tokens)
{
    Arena scratch = arena_reserve(BUSTER_PPF_CLANG_SCRATCH_BYTES);
    const uint64_t capacity = BUSTER_PPF_MAX_CASE_BYTES + 128;
    PreprocessorFuzzBuffer source = {
        .data = scratch.base != 0 ? arena_push(&scratch, capacity, 1) : 0,
        .capacity = capacity,
    };
    source.valid = source.data != 0;
    bool result = test_case->source.length <= BUSTER_PPF_MAX_CASE_BYTES && preprocessor_fuzz_batch_source(&source, &test_case, 1);
    if (result)
    {
        char* arguments[] = {
            (char*)clang_path,
            "--no-default-config",
            "-Xclang",
            "-dump-tokens",
            "-fsyntax-only",
            "-w",
            "-undef",
            "-nostdinc",
            c23 ? "-std=c23" : "-std=c17",
            "-fno-color-diagnostics",
            "-fno-caret-diagnostics",
            "-x",
            "c",
            "-",
            0,
        };
        const String8 input = {
            .pointer = source.data,
            .length = source.length,
        };
        // A token's line is tens of bytes: room for a dump of the largest output.
        const PreprocessorFuzzProcess process = preprocessor_fuzz_clang_process(&scratch, arguments, input, 64 * BUSTER_PPF_MAX_OUTPUT_BYTES);
        result = process.valid && process.exit_code == 0 && process.output.length == 0 && process.diagnostic.length != 0;
        result = result && preprocessor_fuzz_clang_dump(arena, process.diagnostic, tokens);
    }
    arena_release(&scratch);
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

static bool preprocessor_fuzz_benchmark_sample(const char* clang_path, bool c23, uint64_t seed, uint64_t count, bool individual, uint64_t* accepted, uint64_t* bytes)
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
