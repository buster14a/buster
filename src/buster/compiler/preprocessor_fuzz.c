// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Preprocessor fuzz tooling, independent of the production preprocessor: bounded source building, comparison of
// acceptance and preprocessing-token streams, a planted comparison adapter, and line/token deletion minimization.
// A valid rejection differs from a valid acceptance; infrastructure, allocation and lexer failures are INVALID,
// never evidence of a preprocessor disagreement. Token spellings include their source bytes, but whitespace and
// the lexer's start-of-line flag do not participate in comparison.
//
// main.c includes this file after c_lexer.c, String8 and lane.c, then the generator, clang backend and tests.
// This is test tooling outside the compile path, with no separate vector implementation. c_tokenize supplies its
// normal checked SIMD/scalar lexer. The minimizer currently calls only the explicit planted adapter below; connecting
// a real second preprocessor belongs to the later differential runner.

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUSTER_PPF_MAX_CASE_BYTES 32768
#define BUSTER_PPF_MAX_OUTPUT_BYTES (1ull << 20)
#define BUSTER_PPF_CONSTRUCT_COUNT 36

typedef struct PreprocessorFuzzCase PreprocessorFuzzCase;
struct PreprocessorFuzzCase
{
    String8 source;
    uint64_t id;
    uint64_t constructs;
    bool isolated;
};

typedef struct PreprocessorFuzzOutput PreprocessorFuzzOutput;
struct PreprocessorFuzzOutput
{
    String8 text;
    bool accepted;
    bool valid;
};

typedef struct PreprocessorFuzzBuffer PreprocessorFuzzBuffer;
struct PreprocessorFuzzBuffer
{
    char* data;
    uint64_t length;
    uint64_t capacity;
    bool valid;
};

typedef enum PreprocessorFuzzDifference PreprocessorFuzzDifference;
enum PreprocessorFuzzDifference
{
    BUSTER_PPF_EQUAL,
    BUSTER_PPF_ACCEPTANCE,
    BUSTER_PPF_TOKEN_COUNT,
    BUSTER_PPF_TOKEN_KIND,
    BUSTER_PPF_TOKEN_SPELLING,
    BUSTER_PPF_INVALID,
};

static __attribute__((unused)) bool preprocessor_fuzz_append(PreprocessorFuzzBuffer* buffer, String8 source);
static __attribute__((unused)) bool preprocessor_fuzz_format(PreprocessorFuzzBuffer* buffer, const char* format, ...);
static __attribute__((unused)) PreprocessorFuzzDifference preprocessor_fuzz_compare(Arena* scratch, PreprocessorFuzzOutput left, PreprocessorFuzzOutput right);
static __attribute__((unused)) bool preprocessor_fuzz_generate(Arena* arena, uint64_t seed, uint64_t id, bool c23, PreprocessorFuzzCase* result);
static __attribute__((unused)) bool preprocessor_fuzz_clang_version(const char* clang_path);
static __attribute__((unused)) bool preprocessor_fuzz_clang(Arena* arena, const char* clang_path, bool c23, const PreprocessorFuzzCase* cases, uint64_t count, bool individual, PreprocessorFuzzOutput* outputs);
static __attribute__((unused)) bool preprocessor_fuzz_minimize(Arena* arena, String8 input, String8* result);
static __attribute__((unused)) bool preprocessor_fuzz_save(String8 source, uint64_t number);
static __attribute__((unused)) bool preprocessor_fuzz_planted(Arena* scratch, String8 source);
static __attribute__((unused)) int run_preprocessor_fuzz_tests(int argc, char** argv);

typedef struct PreprocessorFuzzTokens PreprocessorFuzzTokens;
struct PreprocessorFuzzTokens
{
    uint8_t* kinds;
    uint32_t* starts;
    uint32_t* ends;
    uint64_t count;
};

// Tokens retain their exclusive source ends and start-of-line flag. On failure, preserve *result and arena length.
static __attribute__((unused)) bool preprocessor_fuzz_tokenize(Arena* arena, String8 source, PreprocessorFuzzTokens* result);
// Batch output has a separate bound; every source still obeys the lexer's u32-position limit.
static __attribute__((unused)) bool preprocessor_fuzz_tokenize_limit(Arena* arena, String8 source, uint64_t limit, PreprocessorFuzzTokens* result);

static bool preprocessor_fuzz_append(PreprocessorFuzzBuffer* buffer, String8 source)
{
    bool success = buffer->valid && buffer->length <= buffer->capacity && source.length <= buffer->capacity - buffer->length;
    success = success && (source.length == 0 || (buffer->data != 0 && source.pointer != 0));
    if (success && source.length != 0)
    {
        memmove(buffer->data + buffer->length, source.pointer, source.length);
        buffer->length += source.length;
    }
    buffer->valid = success;
    return success;
}

static bool preprocessor_fuzz_format(PreprocessorFuzzBuffer* buffer, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    va_list count_arguments;
    va_copy(count_arguments, arguments);
    const int needed = vsnprintf(0, 0, format, count_arguments);
    va_end(count_arguments);
    // Formatting needs a temporary trailing NUL inside the buffer; length excludes it, and later appends replace it.
    bool success = buffer->valid && buffer->data != 0 && buffer->length <= buffer->capacity && needed >= 0;
    success = success && (uint64_t)needed < buffer->capacity - buffer->length;
    if (success)
    {
        const int written = vsnprintf(buffer->data + buffer->length, (size_t)needed + 1, format, arguments);
        success = written == needed;
        if (success)
        {
            buffer->length += (uint64_t)written;
        }
    }
    va_end(arguments);
    buffer->valid = success;
    return success;
}

static bool preprocessor_fuzz_tokenize(Arena* arena, String8 source, PreprocessorFuzzTokens* result)
{
    return preprocessor_fuzz_tokenize_limit(arena, source, BUSTER_PPF_MAX_OUTPUT_BYTES, result);
}

static bool preprocessor_fuzz_tokenize_limit(Arena* arena, String8 source, uint64_t limit, PreprocessorFuzzTokens* result)
{
    const uint64_t saved_length = arena->length;
    bool success = source.length <= limit && source.length <= UINT32_MAX - BUSTER_C_TOKENIZER_OUTPUT_SLACK && (source.length == 0 || source.pointer != 0);
    PreprocessorFuzzTokens tokens = { 0 };
    if (success && source.length != 0)
    {
        const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(source.length);
        tokens.kinds = arena_push_array(arena, capacity, sizeof(uint8_t), _Alignof(uint8_t));
        tokens.starts = arena_push_array(arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
        tokens.ends = arena_push_array(arena, capacity, sizeof(uint32_t), _Alignof(uint32_t));
        success = tokens.kinds != 0 && tokens.starts != 0 && tokens.ends != 0;
        if (success)
        {
            const int64_t count = c_tokenize((const uint8_t*)source.pointer, source.length, tokens.kinds, tokens.starts, tokens.ends, 0);
            success = count >= 0 && (uint64_t)count <= source.length;
            if (success)
            {
                tokens.count = (uint64_t)count;
            }
        }
        for (uint64_t token = 0; success && token < tokens.count; token += 1)
        {
            const uint8_t kind = tokens.kinds[token] & ~BUSTER_C_TOKEN_START_OF_LINE;
            success = kind < C_TOKEN_KIND_COUNT && tokens.starts[token] < tokens.ends[token] && tokens.ends[token] <= source.length;
            success = success && (token == 0 || tokens.ends[token - 1] <= tokens.starts[token]);
        }
    }
    if (success)
    {
        *result = tokens;
    }
    else
    {
        arena_pop_to(arena, saved_length);
    }
    return success;
}

static PreprocessorFuzzDifference preprocessor_fuzz_compare(Arena* scratch, PreprocessorFuzzOutput left, PreprocessorFuzzOutput right)
{
    const uint64_t saved_length = scratch->length;
    PreprocessorFuzzDifference difference = BUSTER_PPF_EQUAL;
    PreprocessorFuzzTokens left_tokens = { 0 };
    PreprocessorFuzzTokens right_tokens = { 0 };
    bool valid = left.valid && right.valid;
    // Validate every accepted side before classifying acceptance, so a broken result is never a difference.
    if (valid && left.accepted)
    {
        valid = preprocessor_fuzz_tokenize(scratch, left.text, &left_tokens);
    }
    if (valid && right.accepted)
    {
        valid = preprocessor_fuzz_tokenize(scratch, right.text, &right_tokens);
    }
    if (!valid)
    {
        difference = BUSTER_PPF_INVALID;
    }
    else if (left.accepted != right.accepted)
    {
        difference = BUSTER_PPF_ACCEPTANCE;
    }
    else if (left.accepted)
    {
        if (left_tokens.count != right_tokens.count)
        {
            difference = BUSTER_PPF_TOKEN_COUNT;
        }
        for (uint64_t token = 0; difference == BUSTER_PPF_EQUAL && token < left_tokens.count; token += 1)
        {
            const uint8_t left_kind = left_tokens.kinds[token] & ~BUSTER_C_TOKEN_START_OF_LINE;
            const uint8_t right_kind = right_tokens.kinds[token] & ~BUSTER_C_TOKEN_START_OF_LINE;
            const uint64_t left_length = (uint64_t)left_tokens.ends[token] - left_tokens.starts[token];
            const uint64_t right_length = (uint64_t)right_tokens.ends[token] - right_tokens.starts[token];
            if (left_kind != right_kind)
            {
                difference = BUSTER_PPF_TOKEN_KIND;
            }
            else if (left_length != right_length || memcmp(left.text.pointer + left_tokens.starts[token], right.text.pointer + right_tokens.starts[token], left_length) != 0)
            {
                difference = BUSTER_PPF_TOKEN_SPELLING;
            }
        }
    }
    arena_pop_to(scratch, saved_length);
    return difference;
}

static PreprocessorFuzzDifference preprocessor_fuzz_planted_difference(Arena* scratch, String8 source)
{
    const uint64_t saved_length = scratch->length;
    PreprocessorFuzzTokens tokens = { 0 };
    bool left_found = false;
    bool right_found = false;
    PreprocessorFuzzDifference difference = BUSTER_PPF_INVALID;
    if (preprocessor_fuzz_tokenize(scratch, source, &tokens))
    {
        for (uint64_t token = 0; token < tokens.count; token += 1)
        {
            const uint8_t kind = tokens.kinds[token] & ~BUSTER_C_TOKEN_START_OF_LINE;
            const uint64_t length = (uint64_t)tokens.ends[token] - tokens.starts[token];
            if (kind == C_TOKEN_RAW_IDENTIFIER)
            {
                left_found |= length == sizeof("BUSTER_PPF_LEFT") - 1 && memcmp(source.pointer + tokens.starts[token], "BUSTER_PPF_LEFT", length) == 0;
                right_found |= length == sizeof("BUSTER_PPF_RIGHT") - 1 && memcmp(source.pointer + tokens.starts[token], "BUSTER_PPF_RIGHT", length) == 0;
            }
        }
        const PreprocessorFuzzOutput left = {
            .text = {
                .pointer = "0",
                .length = 1,
            },
            .accepted = true,
            .valid = true,
        };
        const PreprocessorFuzzOutput right = {
            .text = {
                .pointer = left_found && right_found ? "1" : "0",
                .length = 1,
            },
            .accepted = true,
            .valid = true,
        };
        difference = preprocessor_fuzz_compare(scratch, left, right);
    }
    arena_pop_to(scratch, saved_length);
    return difference;
}

static bool preprocessor_fuzz_planted(Arena* scratch, String8 source)
{
    const PreprocessorFuzzDifference difference = preprocessor_fuzz_planted_difference(scratch, source);
    return difference != BUSTER_PPF_EQUAL && difference != BUSTER_PPF_INVALID;
}

static bool preprocessor_fuzz_minimize(Arena* arena, String8 input, String8* result)
{
    const uint64_t saved_length = arena->length;
    bool success = input.length <= BUSTER_PPF_MAX_CASE_BYTES && (input.length == 0 || input.pointer != 0);
    if (success)
    {
        const PreprocessorFuzzDifference difference = preprocessor_fuzz_planted_difference(arena, input);
        success = difference != BUSTER_PPF_EQUAL && difference != BUSTER_PPF_INVALID;
    }
    char* current = 0;
    char* candidate = 0;
    uint64_t current_length = input.length;
    uint64_t result_end = saved_length;
    if (success)
    {
        current = arena_push(arena, input.length + 1, _Alignof(char));
        result_end = arena->length;
        candidate = arena_push(arena, input.length + 1, _Alignof(char));
        success = current != 0 && candidate != 0;
        if (success)
        {
            memcpy(current, input.pointer, input.length);
        }
    }
    bool reduced = true;
    while (success && reduced)
    {
        reduced = false;
        uint64_t begin = 0;
        while (success && !reduced && begin < current_length)
        {
            uint64_t end = begin;
            while (end < current_length && current[end] != '\n' && current[end] != '\r')
            {
                end += 1;
            }
            if (end < current_length)
            {
                end += 1;
                if (current[end - 1] == '\r' && end < current_length && current[end] == '\n')
                {
                    end += 1;
                }
            }
            memcpy(candidate, current, begin);
            memcpy(candidate + begin, current + end, current_length - end);
            const String8 trial = {
                .pointer = candidate,
                .length = current_length - (end - begin),
            };
            const PreprocessorFuzzDifference difference = preprocessor_fuzz_planted_difference(arena, trial);
            success = difference != BUSTER_PPF_INVALID;
            if (success && difference != BUSTER_PPF_EQUAL)
            {
                memcpy(current, candidate, trial.length);
                current_length = trial.length;
                reduced = true;
            }
            begin = end;
        }
    }
    reduced = true;
    while (success && reduced)
    {
        reduced = false;
        const uint64_t tokens_begin = arena->length;
        const String8 source = {
            .pointer = current,
            .length = current_length,
        };
        PreprocessorFuzzTokens tokens = { 0 };
        success = preprocessor_fuzz_tokenize(arena, source, &tokens);
        for (uint64_t token = 0; success && !reduced && token < tokens.count; token += 1)
        {
            const uint64_t begin = tokens.starts[token];
            const uint64_t end = tokens.ends[token];
            PreprocessorFuzzBuffer buffer = {
                .data = candidate,
                .length = 0,
                .capacity = input.length + 1,
                .valid = true,
            };
            preprocessor_fuzz_append(&buffer, (String8){
                .pointer = current,
                .length = begin,
            });
            // Keep the surviving source's gaps and every newline, including those inside a spliced token.
            // One space prevents the two neighboring spellings from becoming a different token or comment.
            preprocessor_fuzz_append(&buffer, (String8){
                .pointer = " ",
                .length = 1,
            });
            for (uint64_t position = begin; position < end; position += 1)
            {
                if (current[position] == '\n' || current[position] == '\r')
                {
                    preprocessor_fuzz_append(&buffer, (String8){
                        .pointer = current + position,
                        .length = 1,
                    });
                }
            }
            preprocessor_fuzz_append(&buffer, (String8){
                .pointer = current + end,
                .length = current_length - end,
            });
            const String8 trial = {
                .pointer = candidate,
                .length = buffer.length,
            };
            const PreprocessorFuzzDifference difference = buffer.valid ? preprocessor_fuzz_planted_difference(arena, trial) : BUSTER_PPF_INVALID;
            success = difference != BUSTER_PPF_INVALID;
            if (success && difference != BUSTER_PPF_EQUAL)
            {
                memcpy(current, candidate, trial.length);
                current_length = trial.length;
                reduced = true;
            }
        }
        arena_pop_to(arena, tokens_begin);
    }
    if (success)
    {
        current[current_length] = 0;
        arena_pop_to(arena, result_end);
        *result = (String8){
            .pointer = current,
            .length = current_length,
        };
    }
    else
    {
        arena_pop_to(arena, saved_length);
    }
    return success;
}

static bool preprocessor_fuzz_save(String8 source, uint64_t number)
{
    char path[64];
    const int path_length = snprintf(path, sizeof(path), "fuzz_failure_%" PRIu64 ".c", number);
    bool success = source.length <= BUSTER_PPF_MAX_CASE_BYTES && (source.length == 0 || source.pointer != 0);
    success = success && path_length > 0 && (uint64_t)path_length < sizeof(path);
    int descriptor = -1;
    if (success)
    {
        // Never replace an earlier reproducer or follow a same-named symlink.
        descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        success = descriptor >= 0;
    }
    uint64_t written = 0;
    while (success && written < source.length)
    {
        const ssize_t count = write(descriptor, source.pointer + written, source.length - written);
        if (count > 0)
        {
            written += (uint64_t)count;
        }
        else if (count == 0 || errno != EINTR)
        {
            success = false;
        }
    }
    if (descriptor >= 0)
    {
        success = close(descriptor) == 0 && success;
        if (!success)
        {
            unlink(path);
        }
    }
    return success;
}
