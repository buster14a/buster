// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clang oracle for the original preprocessor fuzz cases. Ordinary cases share a process; process-global counter cases
// do not. The lexer classifies a batch's output once before exact identifier markers select its case slices. Rejected
// batches fall back to individual invocations. Process, file, marker and allocation failures never become rejections.
// The subprocess boundary is deliberately outside the compiler hot path; benchmark_sample measures this harness cost.

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
                    diagnostic_metadata.st_size >= 0 && (uint64_t)diagnostic_metadata.st_size <= BUSTER_PPF_CLANG_DIAGNOSTIC_BYTES;
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
            valid = valid && preprocessor_fuzz_clang_read(arena, descriptors[2], BUSTER_PPF_CLANG_DIAGNOSTIC_BYTES, &result.diagnostic);
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
        if (!severity_found)
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

static bool preprocessor_fuzz_clang_split(Arena* arena, Arena* scratch, String8 text, uint64_t count, PreprocessorFuzzOutput* outputs)
{
    PreprocessorFuzzTokens tokens = {0};
    const uint64_t limit = count * (BUSTER_PPF_MAX_OUTPUT_BYTES + 128);
    bool result = preprocessor_fuzz_tokenize_limit(scratch, text, limit, &tokens);
    uint64_t begin = 0;
    uint64_t marker_count = 0;
    for (uint64_t i = 0; result && i < tokens.count; ++i)
    {
        const uint64_t length = (uint64_t)tokens.ends[i] - tokens.starts[i];
        const char* spelling = text.pointer + tokens.starts[i];
        const String8 prefix = BUSTER_S8(BUSTER_PPF_CLANG_MARKER_PREFIX);
        bool marker = (tokens.kinds[i] & ~BUSTER_C_TOKEN_START_OF_LINE) == C_TOKEN_RAW_IDENTIFIER &&
                      length > prefix.length && memcmp(spelling, prefix.pointer, prefix.length) == 0;
        for (uint64_t digit = prefix.length; marker && digit < length; ++digit)
        {
            marker = spelling[digit] >= '0' && spelling[digit] <= '9';
        }
        if (marker)
        {
            char expected[64];
            const int expected_length = snprintf(expected, sizeof(expected), BUSTER_PPF_CLANG_MARKER_PREFIX "%llu", (unsigned long long)marker_count);
            result = marker_count < count && expected_length > 0 && (uint64_t)expected_length == length && memcmp(spelling, expected, length) == 0;
            if (result)
            {
                uint64_t end = tokens.starts[i];
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
                    begin = tokens.ends[i];
                }
            }
        }
    }
    result = result && marker_count == count;
    for (uint64_t i = begin; result && i < text.length; ++i)
    {
        result = preprocessor_fuzz_clang_space(text.pointer[i]);
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
    bool result = source.data != 0;
    for (uint64_t i = 0; result && i < count; ++i)
    {
        result = preprocessor_fuzz_format(&source, "#line 1 \"ppf_%llu.c\"\n", (unsigned long long)cases[i]->id);
        result = result && preprocessor_fuzz_append(&source, cases[i]->source);
        // Two newlines also terminate a final escaped newline before the marker's own logical line.
        result = result && preprocessor_fuzz_format(&source, "\n\n" BUSTER_PPF_CLANG_MARKER_PREFIX "%llu\n", (unsigned long long)i);
    }
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
            "-ferror-limit=1",
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
            result = preprocessor_fuzz_clang_split(arena, scratch, process.output, count, outputs);
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

static bool preprocessor_fuzz_benchmark_sample(const char* clang_path, bool c23, uint64_t seed, uint64_t count, bool individual, uint64_t* accepted, uint64_t* bytes)
{
    uint64_t capacity = 0;
    bool result = accepted != 0 && bytes != 0 && count <= UINT64_MAX / BUSTER_PPF_CONSTRUCT_COUNT &&
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
        result = preprocessor_fuzz_generate(&arena, seed, i * BUSTER_PPF_CONSTRUCT_COUNT, c23, cases + i);
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
