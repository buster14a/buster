// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Make dependency serialization, independent of preprocessing and filesystem I/O.
// The caller supplies String8 and one record per file identity, in first-use order.
// `used` means actually consumed, not merely opened by speculative file-cache work;
// `system` means every consumption was in a system-header context. A later user
// consumption must clear it. Source and command-line target spellings are borrowed.
//
// dependency_rule writes no bytes on invalid input or insufficient capacity. Its
// output and written destinations must be disjoint from one another and from
// the input records and strings, which stay immutable for sizing and emission. A null output with zero capacity is a size query.
// The result is a byte slice, not a NUL-terminated string. No allocation or I/O is
// hidden here. Filename classification has bounded AVX-512BW and scalar twins.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__AVX512F__) && defined(__AVX512BW__)
#include <immintrin.h>
#define BUSTER_DEPENDENCY_VECTOR 1
#else
#define BUSTER_DEPENDENCY_VECTOR 0
#endif

typedef struct DependencyTarget DependencyTarget;
struct DependencyTarget
{
    String8 text;
    bool quote; // -MQ; false is -MT's verbatim Make expression.
};

typedef struct DependencyFile DependencyFile;
struct DependencyFile
{
    String8 path;
    bool used;
    bool system;
};

typedef struct DependencyRule DependencyRule;
struct DependencyRule
{
    String8 source;
    String8 object; // Already resolved by the driver; automatically quoted.
    const DependencyTarget* targets;
    uint32_t target_count;
    const DependencyFile* files;
    uint32_t file_count;
    bool include_system;
    bool phony;
    bool scalar;
};

typedef enum DependencyStatus
{
    DEPENDENCY_OK,
    DEPENDENCY_INVALID_INPUT,
    DEPENDENCY_OVERFLOW,
    DEPENDENCY_CAPACITY,
} DependencyStatus;

typedef struct DependencySink DependencySink;
struct DependencySink
{
    char* pointer;
    uint64_t count;
    DependencyStatus status;
};

// Classification alone reads the source; count is in [0, 64]. Bits beyond it are zero.
static uint64_t dependency_special_scalar(const char* text, uint32_t count)
{
    uint64_t mask = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const unsigned char c = (unsigned char)text[i];
        const bool special = c == 0 || c == '\r' || c == '\n' || c == ' ' || c == '\t' ||
                             c == '$' || c == '#' || c == '\\';
        mask |= (uint64_t)special << i;
    }
    return mask;
}

static uint64_t dependency_special(const char* text, uint32_t count, bool scalar)
{
    uint64_t mask;
#if BUSTER_DEPENDENCY_VECTOR
    if (!scalar && count != 0)
    {
        const uint64_t live = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
        const __m512i bytes = _mm512_maskz_loadu_epi8((__mmask64)live, text);
        mask = (uint64_t)(_mm512_cmpeq_epi8_mask(bytes, _mm512_setzero_si512()) |
                          _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\r')) |
                          _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\n')) |
                          _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(' ')) |
                          _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\t')) |
                          _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('$')) |
                          _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('#')) |
                          _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\\'))) & live;
    }
    else
#endif
    {
        (void)scalar;
        mask = dependency_special_scalar(text, count);
    }
    return mask;
}

static void dependency_append(DependencySink* sink, const char* text, uint64_t length)
{
    if (sink->status == DEPENDENCY_OK)
    {
        const uint64_t before = sink->count;
        if (length > UINT64_MAX - before)
        {
            sink->status = DEPENDENCY_OVERFLOW;
        }
        else
        {
            if (sink->pointer != NULL && length != 0)
            {
                memcpy(sink->pointer + before, text, (size_t)length);
            }
            sink->count = before + length;
        }
    }
}

static void dependency_backslashes(DependencySink* sink, uint64_t length)
{
    if (sink->status == DEPENDENCY_OK)
    {
        const uint64_t before = sink->count;
        if (length > UINT64_MAX - before)
        {
            sink->status = DEPENDENCY_OVERFLOW;
        }
        else
        {
            if (sink->pointer != NULL && length != 0)
            {
                memset(sink->pointer + before, '\\', (size_t)length);
            }
            sink->count = before + length;
        }
    }
}

// GNU/Clang dependency quoting: double $, prefix # and whitespace with \, and
// double any immediately preceding backslashes when escaping whitespace.
// Verbatim -MT targets are only checked for bytes that would inject rule lines.
static void dependency_text(DependencySink* sink, String8 text, bool quote, bool scalar, bool empty_allowed)
{
    const uint64_t length = text.length;
    const char* const bytes = text.pointer;
    if ((length != 0 && bytes == NULL) || (!empty_allowed && length == 0) || length > SIZE_MAX)
    {
        sink->status = DEPENDENCY_INVALID_INPUT;
    }
    uint64_t backslashes = 0;
    for (uint64_t offset = 0; offset < length && sink->status == DEPENDENCY_OK;)
    {
        const uint64_t remaining = length - offset;
        const uint32_t count = remaining < 64 ? (uint32_t)remaining : 64;
        uint64_t mask = dependency_special(bytes + offset, count, scalar);
        uint32_t first = 0;
        while (mask != 0 && sink->status == DEPENDENCY_OK)
        {
            const uint32_t at = (uint32_t)__builtin_ctzll(mask);
            if (at != first)
            {
                dependency_append(sink, bytes + offset + first, at - first);
                backslashes = 0;
            }
            const char c = bytes[offset + at];
            if (c == 0 || c == '\r' || c == '\n')
            {
                sink->status = DEPENDENCY_INVALID_INPUT;
            }
            else
            {
                if (quote && (c == ' ' || c == '\t'))
                {
                    dependency_backslashes(sink, backslashes);
                    dependency_append(sink, "\\", 1);
                }
                else if (quote && c == '$')
                {
                    dependency_append(sink, "$", 1);
                }
                else if (quote && c == '#')
                {
                    dependency_append(sink, "\\", 1);
                }
                dependency_append(sink, bytes + offset + at, 1);
                backslashes = c == '\\' ? backslashes + 1 : 0;
            }
            first = at + 1;
            mask &= mask - 1;
        }
        if (first < count && sink->status == DEPENDENCY_OK)
        {
            dependency_append(sink, bytes + offset + first, count - first);
            backslashes = 0;
        }
        offset += count;
    }
}

static bool dependency_same_path(String8 a, String8 b)
{
    return a.length == b.length && (a.length == 0 || memcmp(a.pointer, b.pointer, (size_t)a.length) == 0);
}

static void dependency_emit(DependencySink* sink, const DependencyRule* rule)
{
    const uint32_t target_count = rule->target_count;
    const DependencyTarget* const targets = rule->targets;
    const uint32_t file_count = rule->file_count;
    const DependencyFile* const files = rule->files;
    const bool scalar = rule->scalar;
    const bool include_system = rule->include_system;
    const String8 source = rule->source;
    if ((target_count != 0 && targets == NULL) || (file_count != 0 && files == NULL))
    {
        sink->status = DEPENDENCY_INVALID_INPUT;
    }
    if (target_count == 0)
    {
        dependency_text(sink, rule->object, true, scalar, false);
    }
    for (uint32_t i = 0; i < target_count && sink->status == DEPENDENCY_OK; i += 1)
    {
        if (i != 0)
        {
            dependency_append(sink, " ", 1);
        }
        dependency_text(sink, targets[i].text, targets[i].quote, scalar, true);
    }
    dependency_append(sink, ": ", 2);
    dependency_text(sink, source, true, scalar, false);
    for (uint32_t i = 0; i < file_count && sink->status == DEPENDENCY_OK; i += 1)
    {
        const DependencyFile file = files[i];
        if (file.used && (include_system || !file.system))
        {
            // Validate before comparing: malformed records must never reach memcmp.
            if (file.path.pointer == NULL || file.path.length == 0 || file.path.length > SIZE_MAX)
            {
                sink->status = DEPENDENCY_INVALID_INPUT;
            }
            else if (!dependency_same_path(file.path, source))
            {
                dependency_append(sink, " ", 1);
                dependency_text(sink, file.path, true, scalar, false);
            }
        }
    }
    dependency_append(sink, "\n", 1);
    if (rule->phony)
    {
        for (uint32_t i = 0; i < file_count && sink->status == DEPENDENCY_OK; i += 1)
        {
            const DependencyFile file = files[i];
            if (file.used && (include_system || !file.system) && !dependency_same_path(file.path, source))
            {
                dependency_append(sink, "\n", 1);
                dependency_text(sink, file.path, true, scalar, false);
                dependency_append(sink, ":\n", 2);
            }
        }
    }
}

// With output == NULL, capacity must be zero. `written` receives the required byte
// count on success or DEPENDENCY_CAPACITY, and zero on malformed/overflowing input.
// Query and write share one format implementation; write preflights the full rule
// before touching the destination. The caller can then publish it atomically.
static DependencyStatus dependency_rule(const DependencyRule* rule, char* output, uint64_t capacity, uint64_t* written)
{
    DependencyStatus status = DEPENDENCY_INVALID_INPUT;
    if (written != NULL)
    {
        *written = 0;
    }
    if (rule != NULL && written != NULL && (output != NULL || capacity == 0))
    {
        DependencySink sizing = { .status = DEPENDENCY_OK };
        dependency_emit(&sizing, rule);
        status = sizing.status;
        if (status == DEPENDENCY_OK)
        {
            *written = sizing.count;
            if (output != NULL && (capacity < sizing.count || sizing.count > SIZE_MAX))
            {
                status = DEPENDENCY_CAPACITY;
            }
            else if (output != NULL)
            {
                DependencySink writing = {
                    .pointer = output,
                    .status = DEPENDENCY_OK,
                };
                dependency_emit(&writing, rule);
                status = writing.status;
            }
        }
    }
    return status;
}
