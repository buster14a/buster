// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The hosted correctness rows' shard costs (#2031): what each command of tools/ci/correctness.tests costs a runner of
// each row of .github/workflows/hosted-correctness.yml, read from the rows' own job logs, where `parallel --github`
// heads each command's output with `TITLE: passed in X s, Y s of CPU`. The tables in tools/ci/costs/, one per row,
// are this tool's output alone: `check`, which the selector's job runs on every pull request, fails a table that isn't
// exactly what `refresh` makes of it, so that a table that lacks a heading of the inventory, or has one the inventory
// doesn't, or was edited by hand, fails before any test runs.
//
//   costs collect --tests LIST --output DIR LOG...   each row's table from its jobs' logs (the rows the logs have)
//   costs refresh --tests LIST --costs DIR           every row's table for the inventory as it is, keeping its
//                                                    measurements: a new command costs the row's median per CPU
//   costs check --tests LIST --costs DIR             every row's table as refresh would write it
//   costs seed --tests LIST --costs DIR --row ROW --from ROW
//                                                    a row without logs yet: its table from another row's measurements,
//                                                    which its `#: seeded` line names, until a collect over its own
//   costs plan --tests LIST --costs FILE --shards N[-M] [--deal FILE] [--jobs J] [--setup SECONDS] [--first SECONDS]
//                                                    the runner's deal and each shard's predicted time, for each N
//   costs report LOG...                              a run's rows as they ran: each shard's step and job, the longest,
//                                                    the imbalance and the runner-minutes
//   costs --self-test [--root DIR]
//
// A LOG is a hosted row's job log as GitHub serves it (`gh api repos/O/R/actions/jobs/ID/logs`), or any part of it that
// keeps the lines read here: the job's name (`Complete job name:`), the runner's CPU (`model name`, which the job's
// "Report host CPU" step prints), the revision it checked out (actions/checkout's first `git checkout --progress --force
// SHA`), and the runner's report (`parallel: shard K of N, ...`, the commands' headings and the summary). A log is data:
// nothing here runs or interpolates what it reads, and a log that lacks a line it needs, or holds one twice, is
// rejected whole, never half-read.
//
// A measured cost is a command's time times the CPUs it held (its heading's `[N CPUs]`, at most the runner's `--jobs`),
// the 90th percentile (nearest rank) of its samples. Samples come from runners of whichever CPUs the jobs drew, which
// the deal can't choose (#2031's review: a high quantile across CPU classes, not a mean). A failed command's time is a
// lower bound, used only where no sample passed or skipped; a command without any sample costs the row's median per
// CPU times its CPUs, and the table says so, until a collect over logs that have it. The deal itself is
// `parallel --shard K/N`'s (src/buster/compiler/test_deal.h, which `plan` shares), so a table changes which shard runs a
// command, never whether it runs.
//
// Layout: limits and rows; text (costs_read, costs_next_line, CostsBuffer); the inventory (costs_list_parse); logs
// (costs_log_parse); tables (costs_table_parse, costs_table_write, costs_refresh_text, costs_check_text); collect
// (costs_collect_rows); the plan (costs_simulate, costs_plan); the report (costs_report); the self-test; main.
// Administrative tooling outside the compiler: it reads some thousands of lines, so it is scalar.

// memmem and strndup.
#define _GNU_SOURCE

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/buster/compiler/test_deal.h"

// ---------------------------------------------------------------------------------------------------------------------
// Limits and rows

// The most commands an inventory may have, logs one run may pass, and shards a plan may deal.
#define BUSTER_COSTS_COMMANDS 4096u
#define BUSTER_COSTS_LOGS 65536u
#define BUSTER_COSTS_SHARDS 64u
// A CPU model's longest name, a title's longest, and a report line's.
#define BUSTER_COSTS_CPU 128u
#define BUSTER_COSTS_TITLE 1024u
// parallel's cost of a command its costs have no line for: its CPUs (uncapped) times this (parallel.c).
#define BUSTER_COSTS_DEFAULT_MILLISECONDS 10000ull
// The largest cost a table may give, in milliseconds, as parallel.c's: a day of 1,024 CPUs.
#define BUSTER_COSTS_MAX_MILLISECONDS (86400000ull * 1024)
// The quantile of a command's samples that its cost is, in percent, by nearest rank.
#define BUSTER_COSTS_QUANTILE 90u
// A CPU's speed against the reference CPU's (the one most jobs drew) is the median, over the commands that both ran in
// one row and that cost the reference at least this many tenths of a CPU-second, of the ratio of their medians; it
// needs at least BUSTER_COSTS_OVERLAP such commands, or the CPU's samples stay as they are.
#define BUSTER_COSTS_RATIO_FLOOR 50u
#define BUSTER_COSTS_OVERLAP 5u
// The rows: 9700x.yml's test matrix, host_arch by sanitizer, at its one optimization level.
#define BUSTER_COSTS_ARCHITECTURES 3u
#define BUSTER_COSTS_SANITIZERS 4u
#define BUSTER_COSTS_ROWS (BUSTER_COSTS_ARCHITECTURES * BUSTER_COSTS_SANITIZERS)
#define BUSTER_COSTS_OPTIMIZATION "O3"

static const char* const costs_architectures[BUSTER_COSTS_ARCHITECTURES] = {
    "x86-64",
    "native",
    "vector",
};
// The vector row (#2033), whose shards are items its draws assign: a pull request's plain row, and the plain and
// sanitizer configurations as one pool of items where the sanitizer rows run.
#define BUSTER_COSTS_VECTOR 2u
static const char* const costs_sanitizers[BUSTER_COSTS_SANITIZERS] = {
    "none",
    "address",
    "thread",
    "memory",
};

// A string literal as a slice.
#define BUSTER_COSTS_SLICE(literal) ((CostsSlice){ .data = (literal), .length = sizeof(literal) - 1 })

// The table's first line, which no other file has: `check` rejects a table without it.
static const char costs_marker[] = "# Generated by tools/ci/costs.c; don't edit it by hand (docs/commands/ci.md, \"Shard costs\").";

// A row's name, as its table's file is named: `x86-64-address-O3`.
static void costs_row_name(uint32_t row, char* name, size_t capacity)
{
    snprintf(name, capacity, "%s-%s-%s", costs_architectures[row / BUSTER_COSTS_SANITIZERS], costs_sanitizers[row % BUSTER_COSTS_SANITIZERS], BUSTER_COSTS_OPTIMIZATION);
}

// ---------------------------------------------------------------------------------------------------------------------
// Text

typedef struct CostsSlice CostsSlice;
struct CostsSlice
{
    const char* data;
    size_t length;
};

typedef struct CostsText CostsText;
struct CostsText
{
    char* data;
    size_t length;
};

// A growing text that the writers append to; an allocation failure ends the process, as every other here does.
typedef struct CostsBuffer CostsBuffer;
struct CostsBuffer
{
    char* data;
    size_t length;
    size_t capacity;
};

static void costs_fatal(const char* message)
{
    fprintf(stderr, "costs: %s\n", message);
    exit(2);
}

static void* costs_allocate(size_t count, size_t size)
{
    void* memory = count != 0 && size != 0 ? calloc(count, size) : calloc(1, 1);
    if (memory == NULL)
    {
        costs_fatal("out of memory");
    }
    return memory;
}

// Room for `extra` more bytes and a NUL after them.
static void costs_reserve(CostsBuffer* buffer, size_t extra)
{
    if (buffer->length + extra + 1 > buffer->capacity)
    {
        size_t capacity = buffer->capacity != 0 ? buffer->capacity : 4096;
        while (capacity < buffer->length + extra + 1)
        {
            capacity *= 2;
        }
        char* grown = realloc(buffer->data, capacity);
        if (grown == NULL)
        {
            costs_fatal("out of memory");
        }
        buffer->data = grown;
        buffer->capacity = capacity;
    }
}

static void costs_append(CostsBuffer* buffer, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    const int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0)
    {
        costs_fatal("a line that can't be formatted");
    }
    costs_reserve(buffer, (size_t)needed);
    vsnprintf(buffer->data + buffer->length, (size_t)needed + 1, format, arguments);
    buffer->length += (size_t)needed;
    va_end(arguments);
}

static CostsText costs_text_of(const char* data)
{
    const size_t length = strlen(data);
    CostsText text = { .data = costs_allocate(length + 1, 1), .length = length };
    memcpy(text.data, data, length);
    return text;
}

// The file's bytes with a NUL after them, or false.
static bool costs_read(const char* path, CostsText* text)
{
    FILE* file = fopen(path, "rb");
    bool valid = file != NULL;
    CostsBuffer buffer = { 0 };
    costs_reserve(&buffer, 0);
    buffer.data[0] = 0;
    char chunk[65536];
    size_t count = valid ? fread(chunk, 1, sizeof(chunk), file) : 0;
    while (valid && count != 0)
    {
        // A chunk may hold NUL bytes, which the parsers reject: copied whole, not formatted.
        costs_reserve(&buffer, count);
        memcpy(buffer.data + buffer.length, chunk, count);
        buffer.length += count;
        buffer.data[buffer.length] = 0;
        count = fread(chunk, 1, sizeof(chunk), file);
    }
    valid = valid && !ferror(file);
    if (file != NULL)
    {
        fclose(file);
    }
    *text = (CostsText){ .data = buffer.data, .length = buffer.length };
    if (!valid)
    {
        free(buffer.data);
        *text = (CostsText){ 0 };
    }
    return valid;
}

static bool costs_write(const char* path, const CostsBuffer* buffer)
{
    FILE* file = fopen(path, "wb");
    bool valid = file != NULL && fwrite(buffer->data, 1, buffer->length, file) == buffer->length;
    valid = file != NULL && fclose(file) == 0 && valid;
    return valid;
}

// The next line from *offset, without its newline: false past the text's end.
static bool costs_next_line(CostsText text, size_t* offset, CostsSlice* line)
{
    const bool more = *offset < text.length;
    if (more)
    {
        const char* start = text.data + *offset;
        const char* end = memchr(start, '\n', text.length - *offset);
        const size_t length = end != NULL ? (size_t)(end - start) : text.length - *offset;
        *line = (CostsSlice){ .data = start, .length = length };
        *offset += length + (end != NULL);
    }
    return more;
}

static bool costs_is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static CostsSlice costs_trim(CostsSlice slice)
{
    while (slice.length != 0 && costs_is_blank(slice.data[0]))
    {
        slice.data += 1;
        slice.length -= 1;
    }
    while (slice.length != 0 && costs_is_blank(slice.data[slice.length - 1]))
    {
        slice.length -= 1;
    }
    return slice;
}

static bool costs_equal(CostsSlice a, CostsSlice b)
{
    return a.length == b.length && memcmp(a.data, b.data, a.length) == 0;
}

static bool costs_starts(CostsSlice slice, const char* prefix)
{
    const size_t length = strlen(prefix);
    return slice.length >= length && memcmp(slice.data, prefix, length) == 0;
}

static CostsSlice costs_after(CostsSlice slice, size_t count)
{
    return (CostsSlice){ .data = slice.data + count, .length = slice.length - count };
}

static bool costs_digit(char c)
{
    return c >= '0' && c <= '9';
}

// A decimal number at the slice's start, into *value: the digits it read, or 0 for none or one past `limit`.
static size_t costs_number(CostsSlice slice, uint64_t limit, uint64_t* value)
{
    size_t i = 0;
    uint64_t result = 0;
    bool valid = true;
    while (valid && i < slice.length && costs_digit(slice.data[i]))
    {
        result = result * 10 + (uint64_t)(slice.data[i] - '0');
        valid = result <= limit;
        i += 1;
    }
    *value = valid ? result : 0;
    return valid ? i : 0;
}

// A whole slice that is exactly a decimal number of at most `limit`.
static bool costs_whole_number(CostsSlice slice, uint64_t limit, uint64_t* value)
{
    const size_t length = costs_number(slice, limit, value);
    return length != 0 && length == slice.length;
}

static bool costs_hex40(CostsSlice slice)
{
    bool valid = slice.length == 40;
    for (size_t i = 0; valid && i < slice.length; i += 1)
    {
        const char c = slice.data[i];
        valid = costs_digit(c) || (c >= 'a' && c <= 'f');
    }
    return valid;
}

// A nearest-rank quantile of `count` values, which it sorts: the ceil(count * percent / 100)-th smallest.
static int costs_compare_u64(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

static uint64_t costs_quantile(uint64_t* values, size_t count, uint32_t percent)
{
    qsort(values, count, sizeof(values[0]), costs_compare_u64);
    const size_t rank = (count * percent + 99) / 100;
    return values[rank != 0 ? rank - 1 : 0];
}

// ---------------------------------------------------------------------------------------------------------------------
// The inventory: tools/ci/correctness.tests's headings, as parallel_list_parse reads them

typedef struct CostsCommand CostsCommand;
struct CostsCommand
{
    CostsSlice title;
    uint32_t cpus;
};

typedef struct CostsList CostsList;
struct CostsList
{
    CostsCommand* commands;
    uint32_t count;
    const char* error;
    uint32_t error_line;
};

// The CPUs a title declares, as parallel_title_cpus: N for `... [N CPUs]` with N from 2, 1 for a title that doesn't end
// with "CPUs]", false for any other that does.
static bool costs_title_cpus(CostsSlice title, uint32_t* cpus)
{
    static const char word[] = "CPUs]";
    const size_t word_length = sizeof(word) - 1;
    bool valid = true;
    uint64_t count = 1;
    if (title.length >= word_length && memcmp(title.data + title.length - word_length, word, word_length) == 0)
    {
        const size_t end = title.length > word_length && title.data[title.length - word_length - 1] == ' ' ? title.length - word_length - 1 : 0;
        size_t start = end;
        while (start > 0 && costs_digit(title.data[start - 1]))
        {
            start -= 1;
        }
        valid = start != end && start != 0 && title.data[start - 1] == '[';
        count = 0;
        valid = valid && costs_whole_number((CostsSlice){ .data = title.data + start, .length = end - start }, UINT32_MAX, &count);
        valid = valid && count >= 2;
    }
    *cpus = valid ? (uint32_t)count : 1;
    return valid;
}

static uint32_t costs_list_find(const CostsList* list, CostsSlice title)
{
    uint32_t found = list->count;
    for (uint32_t i = 0; found == list->count && i < list->count; i += 1)
    {
        found = costs_equal(list->commands[i].title, title) ? i : found;
    }
    return found;
}

// The list's headings, each a line that starts with "==", its title the rest without the blanks around it; before the
// first heading only blank lines and comments. The scripts are parallel's to check.
static CostsList costs_list_parse(CostsText text)
{
    CostsList list = { .commands = costs_allocate(BUSTER_COSTS_COMMANDS, sizeof(CostsCommand)) };
    size_t offset = 0;
    uint32_t number = 0;
    CostsSlice line;
    while (list.error == NULL && costs_next_line(text, &offset, &line))
    {
        number += 1;
        const bool heading = line.length >= 2 && line.data[0] == '=' && line.data[1] == '=';
        const CostsSlice rest = costs_trim(line);
        if (memchr(line.data, 0, line.length) != NULL)
        {
            list.error = "a NUL byte";
        }
        else if (heading)
        {
            const CostsSlice title = costs_trim(costs_after(line, 2));
            uint32_t cpus = 1;
            const bool counted = costs_title_cpus(title, &cpus);
            if (title.length == 0)
            {
                list.error = "a heading without a title";
            }
            else if (title.length >= BUSTER_COSTS_TITLE)
            {
                list.error = "a title longer than this tool reads";
            }
            else if (costs_list_find(&list, title) != list.count)
            {
                list.error = "a title that an earlier command has";
            }
            else if (!counted)
            {
                list.error = "a title whose CPUs aren't [N CPUs] with N from 2";
            }
            else if (list.count == BUSTER_COSTS_COMMANDS)
            {
                list.error = "more commands than this tool reads";
            }
            else
            {
                list.commands[list.count] = (CostsCommand){ .title = title, .cpus = cpus };
                list.count += 1;
            }
        }
        else if (list.count == 0 && rest.length != 0 && rest.data[0] != '#')
        {
            list.error = "a line before the first heading";
        }
        list.error_line = list.error != NULL ? number : 0;
    }
    if (list.error == NULL && list.count == 0)
    {
        list.error = "no commands";
    }
    return list;
}

// The CPUs a command holds on a runner of `jobs`: its own, but a command of more runs alone, holding them all.
static uint32_t costs_held(const CostsCommand* command, uint32_t jobs)
{
    return command->cpus < jobs ? command->cpus : jobs;
}

// ---------------------------------------------------------------------------------------------------------------------
// Logs

typedef struct CostsSample CostsSample;
struct CostsSample
{
    uint32_t command;
    // Its time, in tenths of a second, times the CPUs it held.
    uint64_t tenths;
    // Whether it passed or was skipped: a failed command's time is only a lower bound.
    bool complete;
};

typedef struct CostsLog CostsLog;
struct CostsLog
{
    // The job that builds a portable row's compiler for its shards, which runs no part of the inventory.
    bool build;
    // A vector row's draw, whose shard is the item its collector assigned: the report's, not the name's.
    bool draw;
    // A draw's configurations, its caller's sanitizers (a bit each), and the item it took, from its `Vector item ITEM:
    // SANITIZER, shard K of N` line: the row (vector, SANITIZER) and its shard, which the report must run.
    uint32_t configurations;
    bool itemized;
    uint32_t item_row;
    uint32_t item_shard;
    uint32_t item_shards;
    uint32_t row;
    uint32_t shard;
    uint32_t shards;
    // The runner's --jobs, `at most J CPUs at once`.
    uint32_t jobs;
    char cpu[BUSTER_COSTS_CPU];
    char revision[41];
    // The log's first and last lines' times, in milliseconds since the epoch; -1 without them.
    int64_t first;
    int64_t last;
    bool reported;
    bool summarized;
    // The runner's own time for the shard's commands, its summary's `N commands in X s`, in tenths of a second.
    uint64_t step_tenths;
    CostsSample* samples;
    uint32_t sample_count;
    // Headings whose titles the inventory doesn't have: commands since renamed or removed.
    uint32_t unknown;
    const char* error;
    uint32_t error_line;
};

// Days from 1970-01-01 to a civil date (Howard Hinnant's days_from_civil).
static int64_t costs_days(int64_t year, int64_t month, int64_t day)
{
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t of_era = year - era * 400;
    const int64_t of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t of_era_day = of_era * 365 + of_era / 4 - of_era / 100 + of_year;
    return era * 146097 + of_era_day - 719468;
}

// A line's leading `YYYY-MM-DDTHH:MM:SS[.fraction]Z ` (GitHub's), into milliseconds: the bytes it took, or 0 without.
static size_t costs_timestamp(CostsSlice line, int64_t* milliseconds)
{
    static const char shape[] = "dddd-dd-ddTdd:dd:dd";
    const size_t length = sizeof(shape) - 1;
    bool valid = line.length > length;
    for (size_t i = 0; valid && i < length; i += 1)
    {
        valid = shape[i] == 'd' ? costs_digit(line.data[i]) : line.data[i] == shape[i];
    }
    size_t at = length;
    uint64_t fraction = 0;
    uint64_t scale = 1000;
    if (valid && line.data[at] == '.')
    {
        at += 1;
        const size_t digits = at;
        while (at < line.length && costs_digit(line.data[at]))
        {
            scale /= 10;
            fraction += (uint64_t)(line.data[at] - '0') * scale;
            at += 1;
        }
        valid = at != digits;
    }
    // `Z` and the blank after it.
    valid = valid && at + 1 < line.length && line.data[at] == 'Z' && line.data[at + 1] == ' ';
    if (valid)
    {
        const char* d = line.data;
        const int64_t year = (d[0] - '0') * 1000 + (d[1] - '0') * 100 + (d[2] - '0') * 10 + (d[3] - '0');
        const int64_t month = (d[5] - '0') * 10 + (d[6] - '0');
        const int64_t day = (d[8] - '0') * 10 + (d[9] - '0');
        const int64_t hour = (d[11] - '0') * 10 + (d[12] - '0');
        const int64_t minute = (d[14] - '0') * 10 + (d[15] - '0');
        const int64_t second = (d[17] - '0') * 10 + (d[18] - '0');
        valid = month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour < 24 && minute < 60 && second < 61;
        *milliseconds = ((costs_days(year, month, day) * 24 + hour) * 60 + minute) * 60000 + second * 1000 + (int64_t)fraction;
    }
    return valid ? at + 2 : 0;
}

// `X s` at the slice's start, X as parallel prints it (`%.1f`): its tenths, and the bytes it took, or 0.
static size_t costs_seconds(CostsSlice slice, uint64_t* tenths)
{
    uint64_t whole = 0;
    const size_t digits = costs_number(slice, UINT32_MAX, &whole);
    const bool valid = digits != 0 && digits + 4 <= slice.length && slice.data[digits] == '.' && costs_digit(slice.data[digits + 1]) && slice.data[digits + 2] == ' ' && slice.data[digits + 3] == 's';
    *tenths = valid ? whole * 10 + (uint64_t)(slice.data[digits + 1] - '0') : 0;
    return valid ? digits + 4 : 0;
}

// An outcome and its times, `passed in X s, Y s of CPU`, `skipped in ...` or `failed (REASON) in ...`, the whole slice,
// with the peak memory `, Z MiB peak` after the CPU time where the runner gives it (#2031): whether it is one, its
// time's tenths and whether it is complete (passed or skipped).
static bool costs_outcome(CostsSlice rest, uint64_t* tenths, bool* complete)
{
    size_t at = 0;
    bool valid = true;
    if (costs_starts(rest, "passed in ") || costs_starts(rest, "skipped in "))
    {
        *complete = true;
        at = rest.data[0] == 'p' ? 10 : 11;
    }
    else if (costs_starts(rest, "failed ("))
    {
        *complete = false;
        const char* close = memmem(rest.data, rest.length, ") in ", 5);
        valid = close != NULL;
        at = valid ? (size_t)(close - rest.data) + 5 : 0;
    }
    else
    {
        valid = false;
    }
    const size_t time = valid ? costs_seconds(costs_after(rest, at), tenths) : 0;
    valid = valid && time != 0;
    const CostsSlice after = valid ? costs_after(rest, at + time) : (CostsSlice){ 0 };
    valid = valid && costs_starts(after, ", ");
    uint64_t cpu = 0;
    const size_t cpu_time = valid ? costs_seconds(costs_after(after, 2), &cpu) : 0;
    valid = valid && cpu_time != 0;
    const CostsSlice tail = valid ? costs_after(after, 2 + cpu_time) : (CostsSlice){ 0 };
    valid = valid && costs_starts(tail, " of CPU");
    const CostsSlice memory = valid ? costs_after(tail, 7) : (CostsSlice){ 0 };
    uint64_t mebibytes = 0;
    const size_t memory_digits = memory.length != 0 && costs_starts(memory, ", ") ? costs_number(costs_after(memory, 2), UINT32_MAX, &mebibytes) : 0;
    const CostsSlice peak = memory_digits != 0 ? costs_after(memory, 2 + memory_digits) : (CostsSlice){ 0 };
    valid = valid && (memory.length == 0 || (memory_digits != 0 && peak.length == 9 && memcmp(peak.data, " MiB peak", 9) == 0));
    return valid;
}

// GitHub's escapes in a `::group::` title (parallel_append_escaped): %25, %0D and %0A, into `output`.
static size_t costs_unescape(CostsSlice slice, char* output, size_t capacity)
{
    size_t length = 0;
    size_t i = 0;
    while (i < slice.length && length + 1 < capacity)
    {
        const bool escape = slice.data[i] == '%' && i + 2 < slice.length;
        const char* code = escape ? slice.data + i + 1 : "";
        const char decoded = !escape ? 0 : memcmp(code, "25", 2) == 0 ? '%' : memcmp(code, "0D", 2) == 0 ? '\r' : memcmp(code, "0A", 2) == 0 ? '\n' : 0;
        output[length] = decoded != 0 ? decoded : slice.data[i];
        length += 1;
        i += decoded != 0 ? 3 : 1;
    }
    return length;
}

// The row a name gives, as costs_row_name writes it (`vector-address-O3`); BUSTER_COSTS_ROWS for none.
static uint32_t costs_row_named(CostsSlice name)
{
    uint32_t found = BUSTER_COSTS_ROWS;
    for (uint32_t row = 0; row < BUSTER_COSTS_ROWS; row += 1)
    {
        char text[64];
        costs_row_name(row, text, sizeof(text));
        found = costs_equal(name, (CostsSlice){ text, strlen(text) }) ? row : found;
    }
    return found;
}

// A sanitizer's index by its name, BUSTER_COSTS_SANITIZERS for none.
static uint32_t costs_sanitizer(CostsSlice name)
{
    uint32_t found = BUSTER_COSTS_SANITIZERS;
    for (uint32_t i = 0; i < BUSTER_COSTS_SANITIZERS; i += 1)
    {
        found = costs_equal(name, (CostsSlice){ costs_sanitizers[i], strlen(costs_sanitizers[i]) }) ? i : found;
    }
    return found;
}

// The row a job's name gives: `... (Clang V, ARCH, SANITIZER, O3, shard K of N)` for a shard, or, for the job that
// builds a portable row's compiler, its caller's `Tests on ... (ARCH, SANITIZER, O3) / Build the compiler once for the
// row's shards`.
static bool costs_row_fields(CostsSlice* fields, uint32_t* row)
{
    uint32_t architecture = BUSTER_COSTS_ARCHITECTURES;
    uint32_t sanitizer = BUSTER_COSTS_SANITIZERS;
    for (uint32_t i = 0; i < BUSTER_COSTS_ARCHITECTURES; i += 1)
    {
        architecture = costs_equal(fields[0], (CostsSlice){ costs_architectures[i], strlen(costs_architectures[i]) }) ? i : architecture;
    }
    for (uint32_t i = 0; i < BUSTER_COSTS_SANITIZERS; i += 1)
    {
        sanitizer = costs_equal(fields[1], (CostsSlice){ costs_sanitizers[i], strlen(costs_sanitizers[i]) }) ? i : sanitizer;
    }
    const bool valid = architecture < BUSTER_COSTS_ARCHITECTURES && sanitizer < BUSTER_COSTS_SANITIZERS && costs_equal(fields[2], (CostsSlice){ BUSTER_COSTS_OPTIMIZATION, strlen(BUSTER_COSTS_OPTIMIZATION) });
    *row = architecture * BUSTER_COSTS_SANITIZERS + sanitizer;
    return valid;
}

// The fields of a name's last parenthesized list, `(a, b, c)` closing the name, split at ", ": how many (at most
// `capacity`), or 0 when the name doesn't end so.
static uint32_t costs_name_fields(CostsSlice name, CostsSlice* fields, uint32_t capacity)
{
    size_t open = name.length;
    for (size_t i = 0; i < name.length; i += 1)
    {
        open = name.data[i] == '(' ? i : open;
    }
    const bool closed = open < name.length && name.data[name.length - 1] == ')';
    uint32_t count = 0;
    size_t start = open + 1;
    bool valid = closed;
    for (size_t i = open + 1; valid && i < name.length; i += 1)
    {
        const bool end = i == name.length - 1;
        const bool comma = !end && name.data[i] == ',' && i + 1 < name.length && name.data[i + 1] == ' ';
        if (end || comma)
        {
            valid = count < capacity && i != start;
            if (valid)
            {
                fields[count] = (CostsSlice){ .data = name.data + start, .length = i - start };
                count += 1;
            }
            start = i + 2;
        }
    }
    return valid ? count : 0;
}

static void costs_log_name(CostsLog* log, CostsSlice name)
{
    static const char build[] = "Build the compiler once for the row's shards";
    // The job's own name: after its caller's, which a called workflow's job names first.
    const char* separator = NULL;
    for (size_t i = 0; i + 3 <= name.length; i += 1)
    {
        separator = memcmp(name.data + i, " / ", 3) == 0 ? name.data + i : separator;
    }
    const CostsSlice own = separator != NULL ? (CostsSlice){ .data = separator + 3, .length = name.length - (size_t)(separator + 3 - name.data) } : name;
    const CostsSlice caller = separator != NULL ? (CostsSlice){ .data = name.data, .length = (size_t)(separator - name.data) } : (CostsSlice){ 0 };
    CostsSlice fields[6];
    if (costs_equal(own, (CostsSlice){ build, sizeof(build) - 1 }))
    {
        log->build = true;
        const uint32_t count = costs_name_fields(caller, fields, 6);
        log->error = count == 3 && costs_row_fields(fields, &log->row) ? NULL : "a build job whose caller isn't a hosted row";
    }
    else if (costs_starts(own, "Vector draw "))
    {
        // `Vector draw L, candidate C (Clang V, CONFIGURATIONS, O3)`, its caller's `(vector, CONFIGURATIONS, O3)`: the
        // row's sanitizers, separated by spaces. Its row is the one sanitizer's, or else its item line's.
        log->draw = true;
        const uint32_t count = costs_name_fields(caller, fields, 6);
        const bool vector = count == 3 && costs_equal(fields[0], BUSTER_COSTS_SLICE("vector")) && costs_equal(fields[2], BUSTER_COSTS_SLICE(BUSTER_COSTS_OPTIMIZATION));
        uint32_t single = BUSTER_COSTS_SANITIZERS;
        bool valid = vector && fields[1].length != 0;
        size_t start = 0;
        for (size_t i = 0; valid && i <= fields[1].length; i += 1)
        {
            if (i == fields[1].length || fields[1].data[i] == ' ')
            {
                const uint32_t sanitizer = costs_sanitizer((CostsSlice){ .data = fields[1].data + start, .length = i - start });
                valid = sanitizer < BUSTER_COSTS_SANITIZERS && (log->configurations & (1u << sanitizer)) == 0;
                log->configurations |= valid ? 1u << sanitizer : 0;
                single = start == 0 && i == fields[1].length ? sanitizer : BUSTER_COSTS_SANITIZERS;
                start = i + 1;
            }
        }
        log->row = valid && single < BUSTER_COSTS_SANITIZERS ? BUSTER_COSTS_VECTOR * BUSTER_COSTS_SANITIZERS + single : BUSTER_COSTS_ROWS;
        log->error = valid ? NULL : "a vector draw whose caller isn't the vector row";
    }
    else
    {
        const uint32_t count = costs_name_fields(own, fields, 6);
        uint64_t shard = 0;
        uint64_t shards = 0;
        bool valid = count == 5 && costs_starts(own, "Tests on ") && costs_starts(fields[0], "Clang ") && costs_row_fields(fields + 1, &log->row);
        valid = valid && costs_starts(fields[4], "shard ");
        const CostsSlice numbers = valid ? costs_after(fields[4], 6) : (CostsSlice){ 0 };
        const size_t k = valid ? costs_number(numbers, BUSTER_COSTS_SHARDS, &shard) : 0;
        valid = valid && k != 0 && costs_starts(costs_after(numbers, k), " of ");
        valid = valid && costs_whole_number(valid ? costs_after(numbers, k + 4) : (CostsSlice){ 0 }, BUSTER_COSTS_SHARDS, &shards);
        valid = valid && shard >= 1 && shard <= shards;
        log->shard = (uint32_t)shard;
        log->shards = (uint32_t)shards;
        log->error = valid ? NULL : "a job that is neither a hosted row's shard nor its build";
    }
}

// `parallel: shard K of N, M of T commands, at most J CPUs at once`.
static bool costs_log_shard(CostsLog* log, CostsSlice rest)
{
    uint64_t shard = 0;
    uint64_t shards = 0;
    uint64_t listed = 0;
    uint64_t total = 0;
    uint64_t jobs = 0;
    size_t at = costs_number(rest, BUSTER_COSTS_SHARDS, &shard);
    bool valid = at != 0 && costs_starts(costs_after(rest, at), " of ");
    size_t length = valid ? costs_number(costs_after(rest, at + 4), BUSTER_COSTS_SHARDS, &shards) : 0;
    at += 4 + length;
    valid = valid && length != 0 && costs_starts(costs_after(rest, at), ", ");
    length = valid ? costs_number(costs_after(rest, at + 2), BUSTER_COSTS_COMMANDS, &listed) : 0;
    at += 2 + length;
    valid = valid && length != 0 && costs_starts(costs_after(rest, at), " of ");
    length = valid ? costs_number(costs_after(rest, at + 4), BUSTER_COSTS_COMMANDS, &total) : 0;
    at += 4 + length;
    valid = valid && length != 0 && costs_starts(costs_after(rest, at), " commands, at most ");
    length = valid ? costs_number(costs_after(rest, at + 19), 1024, &jobs) : 0;
    at += 19 + length;
    // The line's end, ` CPUs at once`, exactly.
    valid = valid && length != 0 && rest.length == at + 13 && memcmp(rest.data + at, " CPUs at once", 13) == 0;
    valid = valid && shard >= 1 && shard <= shards && listed <= total && jobs >= 1;
    // A draw's shard is the one its report runs; any other job's, its name's.
    valid = valid && (log->draw || (shard == log->shard && shards == log->shards));
    log->shard = (uint32_t)shard;
    log->shards = (uint32_t)shards;
    log->jobs = (uint32_t)jobs;
    return valid;
}

// A vector draw's item, `Vector item ITEM: SANITIZER, shard K of N` (tools/ci/vector-draw.sh take), after its first two
// words: ITEM the sanitizer's initial and K, the sanitizer one of the draw's configurations.
static bool costs_log_item(CostsLog* log, CostsSlice rest)
{
    const char* colon = memchr(rest.data, ':', rest.length);
    const CostsSlice item = colon != NULL ? (CostsSlice){ .data = rest.data, .length = (size_t)(colon - rest.data) } : (CostsSlice){ 0 };
    const CostsSlice after = colon != NULL ? costs_after(rest, item.length + 1) : (CostsSlice){ 0 };
    const char* comma = after.length > 1 && after.data[0] == ' ' ? memchr(after.data, ',', after.length) : NULL;
    const CostsSlice name = comma != NULL ? (CostsSlice){ .data = after.data + 1, .length = (size_t)(comma - after.data - 1) } : (CostsSlice){ 0 };
    const uint32_t sanitizer = costs_sanitizer(name);
    uint64_t item_shard = 0;
    uint64_t shard = 0;
    uint64_t shards = 0;
    bool valid = sanitizer < BUSTER_COSTS_SANITIZERS && (log->configurations & (1u << sanitizer)) != 0;
    valid = valid && item.length >= 2 && item.data[0] == costs_sanitizers[sanitizer][0] && costs_whole_number(costs_after(item, 1), BUSTER_COSTS_SHARDS, &item_shard);
    const CostsSlice numbers = valid ? costs_after(after, name.length + 1) : (CostsSlice){ 0 };
    valid = valid && costs_starts(numbers, ", shard ");
    const size_t k = valid ? costs_number(costs_after(numbers, 8), BUSTER_COSTS_SHARDS, &shard) : 0;
    valid = valid && k != 0 && costs_starts(costs_after(numbers, 8 + k), " of ");
    valid = valid && costs_whole_number(costs_after(numbers, 12 + k), BUSTER_COSTS_SHARDS, &shards);
    valid = valid && shard == item_shard && shard >= 1 && shard <= shards;
    log->item_row = BUSTER_COSTS_VECTOR * BUSTER_COSTS_SANITIZERS + sanitizer;
    log->item_shard = (uint32_t)shard;
    log->item_shards = (uint32_t)shards;
    return valid;
}

// A heading of the runner's report, after `##[group]` (or `::group::`): the inventory's title it names and its sample,
// if it has one. The title may hold ": " and so may a failure's reason, so each ": " is tried, from the first, for a
// title the list has and an outcome after it.
static void costs_log_heading(CostsLog* log, const CostsList* list, CostsSlice heading, bool* seen)
{
    char text[BUSTER_COSTS_TITLE * 2];
    const size_t length = costs_unescape(heading, text, sizeof(text));
    bool timed = false;
    uint32_t found = list->count;
    uint64_t tenths = 0;
    bool complete = false;
    for (size_t i = 0; found == list->count && i + 2 <= length; i += 1)
    {
        if (text[i] == ':' && text[i + 1] == ' ')
        {
            uint64_t time = 0;
            bool whole = false;
            const bool outcome = costs_outcome((CostsSlice){ .data = text + i + 2, .length = length - i - 2 }, &time, &whole);
            const uint32_t command = outcome ? costs_list_find(list, (CostsSlice){ .data = text, .length = i }) : list->count;
            timed = timed || outcome;
            found = command;
            tenths = time;
            complete = whole;
        }
    }
    if (found != list->count && seen[found])
    {
        log->error = "a command's heading twice";
    }
    else if (found != list->count)
    {
        seen[found] = true;
        log->samples[log->sample_count] = (CostsSample){ .command = found, .tenths = tenths * costs_held(&list->commands[found], log->jobs), .complete = complete };
        log->sample_count += 1;
    }
    else
    {
        log->unknown += timed;
    }
}

// A job's log, its lines each after GitHub's timestamp. `list` may be null (the report's), which reads no samples.
static CostsLog costs_log_parse(const CostsList* list, CostsText text)
{
    CostsLog log = { .first = -1, .last = -1 };
    const uint32_t commands = list != NULL ? list->count : 0;
    log.samples = costs_allocate(commands + 1, sizeof(CostsSample));
    bool* seen = costs_allocate(commands + 1, sizeof(bool));
    bool named = false;
    size_t offset = text.length >= 3 && memcmp(text.data, "\xEF\xBB\xBF", 3) == 0 ? 3 : 0;
    uint32_t number = 0;
    CostsSlice line;
    while (log.error == NULL && costs_next_line(text, &offset, &line))
    {
        number += 1;
        int64_t stamp = 0;
        const size_t skip = costs_timestamp(line, &stamp);
        if (skip != 0)
        {
            log.first = log.first < 0 ? stamp : log.first;
            log.last = stamp;
        }
        const CostsSlice content = costs_after(line, skip);
        if (memchr(line.data, 0, line.length) != NULL)
        {
            log.error = "a NUL byte";
        }
        else if (costs_starts(content, "Complete job name: "))
        {
            log.error = named ? "two job names" : NULL;
            if (!named)
            {
                costs_log_name(&log, costs_trim(costs_after(content, 19)));
            }
            named = true;
        }
        else if (costs_starts(content, "model name") && log.cpu[0] == 0)
        {
            const CostsSlice rest = costs_trim(costs_after(content, 10));
            const CostsSlice model = rest.length != 0 && rest.data[0] == ':' ? costs_trim(costs_after(rest, 1)) : (CostsSlice){ 0 };
            const bool valid = model.length != 0 && model.length < BUSTER_COSTS_CPU && memchr(model.data, '\t', model.length) == NULL;
            log.error = valid ? NULL : "a CPU model that isn't one";
            snprintf(log.cpu, sizeof(log.cpu), "%.*s", valid ? (int)model.length : 0, model.data);
        }
        else if (costs_starts(content, "[command]/usr/bin/git checkout --progress --force ") && log.revision[0] == 0)
        {
            const CostsSlice revision = costs_trim(costs_after(content, 50));
            log.error = costs_hex40(revision) ? NULL : "a checkout of something other than a commit";
            snprintf(log.revision, sizeof(log.revision), "%.*s", costs_hex40(revision) ? 40 : 0, revision.data);
        }
        else if (costs_starts(content, "Vector item "))
        {
            log.error = !log.draw ? "a vector item outside a vector draw" : log.itemized ? "two vector items" : !costs_log_item(&log, costs_after(content, 12)) ? "a vector item that isn't one of its draw's items" : NULL;
            log.itemized = true;
        }
        else if (costs_starts(content, "parallel: shard "))
        {
            log.error = log.reported ? "two reports" : !named || log.build ? "a report before the job's name, or in a build job" : !costs_log_shard(&log, costs_after(content, 16)) ? "a shard line that isn't the job's shard" : NULL;
            log.reported = true;
        }
        else if (log.reported && !log.summarized && costs_starts(content, "parallel: ") && memmem(content.data, content.length, " commands in ", 13) != NULL)
        {
            // `parallel: C commands in X s, Y s of CPU, ...`: the runner's own time for them.
            const char* in = memmem(content.data, content.length, " commands in ", 13);
            const CostsSlice time = { .data = in + 13, .length = content.length - (size_t)(in + 13 - content.data) };
            log.error = costs_seconds(time, &log.step_tenths) != 0 ? NULL : "a summary without its time";
            log.summarized = true;
        }
        else if (log.reported && !log.summarized && list != NULL && (costs_starts(content, "##[group]") || costs_starts(content, "::group::")))
        {
            costs_log_heading(&log, list, costs_after(content, 9), seen);
        }
        log.error_line = log.error != NULL ? number : 0;
    }
    if (log.error == NULL)
    {
        // A draw of a pool of configurations runs its item's row; one of a single configuration, that configuration's,
        // which an item line must agree with.
        const bool item_mismatch = log.draw && log.itemized && ((log.row != BUSTER_COSTS_ROWS && log.row != log.item_row) || log.item_shard != log.shard || log.item_shards != log.shards);
        log.row = log.draw && log.itemized ? log.item_row : log.row;
        log.error = !named ? "no job name"
                  : log.build ? NULL
                  : log.cpu[0] == 0 ? "no CPU model"
                  : log.revision[0] == 0 ? "no checkout of the revision"
                  : !log.reported ? "no runner's report"
                  : !log.summarized ? "a runner's report without its summary"
                  : log.draw && log.row == BUSTER_COSTS_ROWS ? "a pool's draw without its vector item"
                  : item_mismatch ? "a vector item that isn't the draw's row or the report's shard"
                  : NULL;
    }
    free(seen);
    return log;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tables

typedef enum CostsKind
{
    COSTS_ABSENT,
    COSTS_MEASURED,
    COSTS_LOWER_BOUND,
    COSTS_UNMEASURED,
} CostsKind;

typedef struct CostsTable CostsTable;
struct CostsTable
{
    bool marked;
    // The row its `#: row` line names, BUSTER_COSTS_ROWS without one.
    uint32_t row;
    // The runner's --jobs (`#: runner-cpus`), 0 without one.
    uint32_t jobs;
    // `#: samples MIN MAX`, the fewest and most samples of a measured command.
    uint32_t samples_min;
    uint32_t samples_max;
    // `#: first-shard SECONDS`, in milliseconds: how much longer the row's shard 1 runs than its others besides their
    // commands, its once-only steps, which the deal adds to shard 1's schedule. Absent where the logs couldn't say.
    uint64_t first_milliseconds;
    bool first_given;
    // The `#: revision` and `#: cpu` lines, as they are: where the measurements came from; after a `#: seeded ROW` line,
    // another row's (BUSTER_COSTS_ROWS without one).
    CostsBuffer provenance;
    uint32_t seeded;
    // Each command's cost in milliseconds and its kind, by its place in the list.
    uint64_t* milliseconds;
    CostsKind* kinds;
    // Lines whose titles the list doesn't have, or has twice: each named in `problems`.
    uint32_t extra;
    uint32_t duplicates;
    CostsBuffer problems;
    const char* error;
    uint32_t error_line;
};

static void costs_table_free(CostsTable* table)
{
    free(table->provenance.data);
    free(table->problems.data);
    free(table->milliseconds);
    free(table->kinds);
    *table = (CostsTable){ 0 };
}

// A cost line's seconds as parallel_cost_seconds reads them: digits, then at most three after a '.', into milliseconds;
// the bytes it read, or 0.
static size_t costs_table_seconds(CostsSlice line, uint64_t* milliseconds)
{
    uint64_t whole = 0;
    const size_t digits = costs_number(line, BUSTER_COSTS_MAX_MILLISECONDS / 1000, &whole);
    size_t at = digits;
    uint64_t fraction = 0;
    bool valid = digits != 0;
    if (valid && at < line.length && line.data[at] == '.')
    {
        at += 1;
        uint64_t scale = 100;
        const size_t start = at;
        while (at < line.length && costs_digit(line.data[at]) && at - start < 3)
        {
            fraction += (uint64_t)(line.data[at] - '0') * scale;
            scale /= 10;
            at += 1;
        }
        valid = at != start && !(at < line.length && costs_digit(line.data[at]));
    }
    *milliseconds = valid ? whole * 1000 + fraction : 0;
    return valid ? at : 0;
}

// A `#: lower-bound TITLE` or `#: unmeasured TITLE` mark: the command's kind, applied after the cost lines.
static void costs_table_mark(CostsTable* table, const CostsList* list, CostsSlice title, CostsKind kind, CostsKind* marks)
{
    const uint32_t found = costs_list_find(list, costs_trim(title));
    if (found != list->count && marks[found] != COSTS_ABSENT)
    {
        table->error = "a command marked twice";
    }
    else if (found != list->count)
    {
        marks[found] = kind;
    }
}

// A table: its marker, its `#:` lines and its `SECONDS TITLE` lines, which parallel_costs_parse reads as well (a title
// the list doesn't have is counted, not an error). Other `#` lines are its prose, which the writer gives again.
static CostsTable costs_table_parse(const CostsList* list, CostsText text)
{
    CostsTable table = { .row = BUSTER_COSTS_ROWS, .seeded = BUSTER_COSTS_ROWS };
    table.milliseconds = costs_allocate(list->count + 1, sizeof(uint64_t));
    table.kinds = costs_allocate(list->count + 1, sizeof(CostsKind));
    CostsKind* marks = costs_allocate(list->count + 1, sizeof(CostsKind));
    size_t offset = 0;
    uint32_t number = 0;
    CostsSlice line;
    while (table.error == NULL && costs_next_line(text, &offset, &line))
    {
        number += 1;
        const CostsSlice content = costs_trim(line);
        table.marked = table.marked || (number == 1 && costs_equal(line, (CostsSlice){ costs_marker, sizeof(costs_marker) - 1 }));
        if (memchr(line.data, 0, line.length) != NULL)
        {
            table.error = "a NUL byte";
        }
        else if (costs_starts(content, "#: row "))
        {
            const CostsSlice value = costs_after(content, 7);
            CostsSlice fields[3] = { 0 };
            uint32_t count = 0;
            size_t start = 0;
            for (size_t i = 0; i <= value.length && count < 4; i += 1)
            {
                if (i == value.length || value.data[i] == ' ')
                {
                    if (count < 3)
                    {
                        fields[count] = (CostsSlice){ .data = value.data + start, .length = i - start };
                    }
                    count += 1;
                    start = i + 1;
                }
            }
            table.error = table.row != BUSTER_COSTS_ROWS ? "two rows" : count == 3 && costs_row_fields(fields, &table.row) ? NULL : "a row that isn't one";
        }
        else if (costs_starts(content, "#: runner-cpus "))
        {
            uint64_t jobs = 0;
            const bool valid = table.jobs == 0 && costs_whole_number(costs_after(content, 15), 1024, &jobs) && jobs >= 1;
            table.error = valid ? NULL : "runner CPUs that aren't a count, or twice";
            table.jobs = (uint32_t)jobs;
        }
        else if (costs_starts(content, "#: samples "))
        {
            const CostsSlice value = costs_after(content, 11);
            uint64_t low = 0;
            uint64_t high = 0;
            const size_t length = costs_number(value, UINT32_MAX, &low);
            const bool valid = length != 0 && length < value.length && value.data[length] == ' ' && costs_whole_number(costs_after(value, length + 1), UINT32_MAX, &high) && low <= high;
            table.error = valid ? NULL : "samples that aren't MIN MAX";
            table.samples_min = (uint32_t)low;
            table.samples_max = (uint32_t)high;
        }
        else if (costs_starts(content, "#: first-shard "))
        {
            const CostsSlice value = costs_after(content, 15);
            uint64_t milliseconds = 0;
            const size_t length = costs_table_seconds(value, &milliseconds);
            const bool valid = !table.first_given && length != 0 && length == value.length;
            table.error = valid ? NULL : "a first shard's time that isn't SECONDS, or twice";
            table.first_milliseconds = milliseconds;
            table.first_given = true;
        }
        else if (costs_starts(content, "#: revision ") || costs_starts(content, "#: cpu "))
        {
            costs_append(&table.provenance, "%.*s\n", (int)content.length, content.data);
        }
        else if (costs_starts(content, "#: seeded "))
        {
            // The row whose measurements a seeded table holds (`costs seed`), first among the provenance lines, which
            // are that row's.
            const uint32_t from = costs_row_named(costs_after(content, 10));
            table.error = from == BUSTER_COSTS_ROWS || table.seeded != BUSTER_COSTS_ROWS || table.provenance.length != 0 ? "a seeded line that isn't one row's, first" : NULL;
            table.seeded = from;
            costs_append(&table.provenance, "%.*s\n", (int)content.length, content.data);
        }
        else if (costs_starts(content, "#: lower-bound "))
        {
            costs_table_mark(&table, list, costs_after(content, 15), COSTS_LOWER_BOUND, marks);
        }
        else if (costs_starts(content, "#: unmeasured "))
        {
            costs_table_mark(&table, list, costs_after(content, 14), COSTS_UNMEASURED, marks);
        }
        else if (costs_starts(content, "#:"))
        {
            table.error = "a `#:` line this tool doesn't write";
        }
        else if (content.length != 0 && content.data[0] != '#')
        {
            uint64_t milliseconds = 0;
            const size_t length = costs_table_seconds(content, &milliseconds);
            const CostsSlice title = length != 0 ? costs_trim(costs_after(content, length)) : (CostsSlice){ 0 };
            const bool separated = length != 0 && length < content.length && costs_is_blank(content.data[length]);
            const uint32_t found = title.length != 0 ? costs_list_find(list, title) : list->count;
            if (!separated || title.length == 0)
            {
                table.error = "a cost that isn't SECONDS TITLE";
            }
            else if (found == list->count)
            {
                table.extra += 1;
                costs_append(&table.problems, "  a title the inventory doesn't have: %.*s\n", (int)title.length, title.data);
            }
            else if (table.kinds[found] != COSTS_ABSENT)
            {
                table.duplicates += 1;
                costs_append(&table.problems, "  a title twice: %.*s\n", (int)title.length, title.data);
            }
            else
            {
                table.milliseconds[found] = milliseconds;
                table.kinds[found] = COSTS_MEASURED;
            }
        }
        table.error_line = table.error != NULL ? number : 0;
    }
    for (uint32_t i = 0; table.error == NULL && i < list->count; i += 1)
    {
        // A mark names a command the table gives a cost: a lower bound is measured, an unmeasured cost estimated.
        table.error = marks[i] != COSTS_ABSENT && table.kinds[i] == COSTS_ABSENT ? "a mark for a command without a cost" : NULL;
        table.kinds[i] = marks[i] != COSTS_ABSENT ? marks[i] : table.kinds[i];
    }
    free(marks);
    return table;
}

// The row's median cost per CPU held, over its measured commands (not lower bounds, not estimates), in tenths: what a
// command without a sample costs per CPU. False when the table measures none.
static bool costs_table_median(const CostsTable* table, const CostsList* list, uint64_t* tenths)
{
    uint64_t* values = costs_allocate(list->count + 1, sizeof(uint64_t));
    size_t count = 0;
    for (uint32_t i = 0; i < list->count; i += 1)
    {
        if (table->kinds[i] == COSTS_MEASURED)
        {
            values[count] = table->milliseconds[i] / 100 / costs_held(&list->commands[i], table->jobs);
            count += 1;
        }
    }
    *tenths = count != 0 ? costs_quantile(values, count, 50) : 0;
    free(values);
    return count != 0;
}

// Writes the row's table: the marker, the prose, the `#:` lines and a cost for every command of the list, in its order.
// A command without a measurement (absent, or an estimate) costs the row's median per CPU times its CPUs.
static void costs_table_write(const CostsTable* table, const CostsList* list, CostsBuffer* output)
{
    char name[64];
    costs_row_name(table->row, name, sizeof(name));
    uint64_t median = 0;
    costs_table_median(table, list, &median);
    costs_append(output, "%s\n", costs_marker);
    costs_append(output, "# What each command of tools/ci/correctness.tests costs a runner of the hosted row %s, for `parallel --shard\n", name);
    costs_append(output, "# K/N --costs` (docs/commands/parallel.md): its time times the CPUs it held (its heading's, at most the runner's), in\n");
    costs_append(output, "# seconds, then its title as its heading has it. `costs collect` makes it from the row's job logs, at the revisions\n");
    costs_append(output, "# below (`revision`, with its jobs): a command's cost is the 90th percentile (nearest rank) of its samples, each\n");
    costs_append(output, "# scaled to the slowest CPU's time by the speeds of the CPUs the jobs drew (`cpu`: its jobs, its time against the\n");
    costs_append(output, "# slowest's over the commands both ran, `-` where too few, and its model). Where every sample failed, its longest\n");
    costs_append(output, "# failure, a lower bound (`lower-bound`); where it has none, the row's median per CPU times its CPUs (`unmeasured`),\n");
    costs_append(output, "# which `costs refresh` also gives a command the inventory adds. The costs only deal the commands to the shards: a\n");
    costs_append(output, "# stale cost makes a shard longer, never a test go missing.\n");
    if (table->first_given)
    {
        costs_append(output, "# `first-shard` is how much longer shard 1's job ran than the others' besides their commands, the medians' difference\n");
        costs_append(output, "# with each job scaled as its samples are: the steps the row runs once, which the deal adds to shard 1's schedule.\n");
    }
    costs_append(output, "#: row %s %s %s\n", costs_architectures[table->row / BUSTER_COSTS_SANITIZERS], costs_sanitizers[table->row % BUSTER_COSTS_SANITIZERS], BUSTER_COSTS_OPTIMIZATION);
    costs_append(output, "#: runner-cpus %u\n", table->jobs);
    if (table->first_given)
    {
        const uint64_t tenths = table->first_milliseconds / 100;
        costs_append(output, "#: first-shard %llu.%llu\n", (unsigned long long)(tenths / 10), (unsigned long long)(tenths % 10));
    }
    costs_append(output, "#: samples %u %u\n", table->samples_min, table->samples_max);
    costs_append(output, "%.*s", (int)table->provenance.length, table->provenance.data != NULL ? table->provenance.data : "");
    for (uint32_t i = 0; i < list->count; i += 1)
    {
        const CostsSlice title = list->commands[i].title;
        if (table->kinds[i] == COSTS_LOWER_BOUND)
        {
            costs_append(output, "#: lower-bound %.*s\n", (int)title.length, title.data);
        }
        else if (table->kinds[i] == COSTS_ABSENT || table->kinds[i] == COSTS_UNMEASURED)
        {
            costs_append(output, "#: unmeasured %.*s\n", (int)title.length, title.data);
        }
    }
    for (uint32_t i = 0; i < list->count; i += 1)
    {
        const bool measured = table->kinds[i] == COSTS_MEASURED || table->kinds[i] == COSTS_LOWER_BOUND;
        const uint64_t tenths = measured ? table->milliseconds[i] / 100 : median * costs_held(&list->commands[i], table->jobs);
        costs_append(output, "%llu.%llu %.*s\n", (unsigned long long)(tenths / 10), (unsigned long long)(tenths % 10), (int)list->commands[i].title.length, list->commands[i].title.data);
    }
}

// The table as refresh writes it for the list as it is, into `output`: false (with *error) when it can't be refreshed.
static bool costs_refresh_text(const CostsList* list, CostsText text, CostsBuffer* output, const char** error)
{
    CostsTable table = costs_table_parse(list, text);
    uint64_t median = 0;
    // The median divides by the CPUs each command held, which need the runner's.
    const bool measured = table.error == NULL && table.jobs != 0 && costs_table_median(&table, list, &median);
    *error = table.error != NULL ? table.error
           : !table.marked ? "a table this tool didn't write"
           : table.row == BUSTER_COSTS_ROWS ? "a table without its row"
           : table.jobs == 0 ? "a table without its runner's CPUs"
           : !measured ? "a table that measures no command"
           : NULL;
    if (*error == NULL)
    {
        costs_table_write(&table, list, output);
    }
    costs_table_free(&table);
    return *error == NULL;
}

// A row without logs yet (#2033's vector sanitizer rows): `row`'s table from `from`'s measurements, the source's table
// `text` as refresh would write it, with a `#: seeded FROM` line before its provenance. A seeded table is replaced by
// a collect over the row's own logs; a table that is itself seeded, or not this tool's, can't seed another.
static bool costs_seed_text(const CostsList* list, uint32_t row, uint32_t from, CostsText text, CostsBuffer* output, const char** error)
{
    CostsBuffer refreshed = { 0 };
    const bool written = costs_refresh_text(list, text, &refreshed, error);
    CostsTable table = written ? costs_table_parse(list, (CostsText){ .data = refreshed.data, .length = refreshed.length }) : (CostsTable){ .row = BUSTER_COSTS_ROWS };
    *error = !written ? *error
           : table.row != from ? "a source whose `#: row` line names another row"
           : table.seeded != BUSTER_COSTS_ROWS ? "a source that is itself seeded"
           : row == from ? "a row seeded from itself"
           : NULL;
    if (*error == NULL)
    {
        char name[64];
        costs_row_name(from, name, sizeof(name));
        CostsBuffer provenance = { 0 };
        costs_append(&provenance, "#: seeded %s\n%.*s", name, (int)table.provenance.length, table.provenance.data != NULL ? table.provenance.data : "");
        free(table.provenance.data);
        table.provenance = provenance;
        table.seeded = from;
        table.row = row;
        costs_table_write(&table, list, output);
    }
    costs_table_free(&table);
    free(refreshed.data);
    return *error == NULL;
}

// The problems of the row's table `text` against the list, each a line into `report`: how many. A table must be exactly
// what refresh writes of it, which gives every heading of the list one line, in its order, and nothing else.
static uint32_t costs_check_text(const CostsList* list, uint32_t row, CostsText text, CostsBuffer* report)
{
    char name[64];
    costs_row_name(row, name, sizeof(name));
    CostsTable table = costs_table_parse(list, text);
    uint32_t problems = 0;
    if (table.error != NULL)
    {
        costs_append(report, "%s: line %u: %s\n", name, table.error_line, table.error);
        problems += 1;
    }
    else
    {
        if (table.row != row)
        {
            costs_append(report, "%s: its `#: row` line names another row, or none\n", name);
            problems += 1;
        }
        for (uint32_t i = 0; i < list->count; i += 1)
        {
            if (table.kinds[i] == COSTS_ABSENT)
            {
                costs_append(report, "%s: no cost for the inventory's heading: %.*s\n", name, (int)list->commands[i].title.length, list->commands[i].title.data);
                problems += 1;
            }
        }
        if (table.extra + table.duplicates != 0)
        {
            costs_append(report, "%s: lines the inventory doesn't match:\n%.*s", name, (int)table.problems.length, table.problems.data);
            problems += table.extra + table.duplicates;
        }
        CostsBuffer refreshed = { 0 };
        const char* error = NULL;
        const bool written = costs_refresh_text(list, text, &refreshed, &error);
        const bool same = written && refreshed.length == text.length && memcmp(refreshed.data, text.data, text.length) == 0;
        if (!written)
        {
            costs_append(report, "%s: %s\n", name, error);
            problems += 1;
        }
        else if (!same && problems == 0)
        {
            costs_append(report, "%s: not as `costs refresh` writes it (a hand edit, an order or an estimate that changed)\n", name);
            problems += 1;
        }
        free(refreshed.data);
    }
    costs_table_free(&table);
    return problems;
}

// ---------------------------------------------------------------------------------------------------------------------
// Collect: the rows' tables from their logs

typedef struct CostsTally CostsTally;
struct CostsTally
{
    char key[BUSTER_COSTS_CPU];
    uint32_t jobs;
    int64_t first;
};

static int costs_compare_tally(const void* a, const void* b)
{
    const CostsTally* x = a;
    const CostsTally* y = b;
    const int order = (x->first > y->first) - (x->first < y->first);
    return order != 0 ? order : strcmp(x->key, y->key);
}

// Counts `key` among the tallies, with its earliest time.
static uint32_t costs_tally(CostsTally* tallies, uint32_t count, const char* key, int64_t first)
{
    uint32_t found = count;
    for (uint32_t i = 0; found == count && i < count; i += 1)
    {
        found = strcmp(tallies[i].key, key) == 0 ? i : found;
    }
    if (found == count)
    {
        snprintf(tallies[count].key, sizeof(tallies[count].key), "%s", key);
        tallies[count].jobs = 0;
        tallies[count].first = first;
        count += 1;
    }
    tallies[found].jobs += 1;
    tallies[found].first = first < tallies[found].first ? first : tallies[found].first;
    return count;
}

// The CPUs' speeds: each model the logs' shards ran on, how much time it takes against the slowest, and each log's.
typedef struct CostsSpeeds CostsSpeeds;
struct CostsSpeeds
{
    // The models, by name, and each one's time against the reference's in thousandths: 0 where it is unknown.
    CostsTally* models;
    uint64_t* factors;
    uint32_t count;
    // The largest known factor, the slowest CPU's, which every sample is scaled to.
    uint64_t slowest;
    // Each log's model, by its place.
    uint32_t* model_of;
};

typedef struct CostsGroupSample CostsGroupSample;
struct CostsGroupSample
{
    uint32_t row;
    uint32_t command;
    uint32_t model;
    uint64_t tenths;
};

static int costs_compare_group(const void* a, const void* b)
{
    const CostsGroupSample* x = a;
    const CostsGroupSample* y = b;
    const int row = (x->row > y->row) - (x->row < y->row);
    const int command = (x->command > y->command) - (x->command < y->command);
    const int model = (x->model > y->model) - (x->model < y->model);
    const int tenths = (x->tenths > y->tenths) - (x->tenths < y->tenths);
    return row != 0 ? row : command != 0 ? command : model != 0 ? model : tenths;
}

// The speeds from the shards' complete samples, pooled over the rows: for each CPU, the median ratio of a command's
// median time on it to the same command's on the reference CPU, in the same row. A CPU that shares too few commands
// with the reference keeps its samples as they are (the table marks its speed `-`).
static CostsSpeeds costs_speeds(const CostsLog* logs, uint32_t count)
{
    CostsSpeeds speeds = { .models = costs_allocate(count + 1, sizeof(CostsTally)), .model_of = costs_allocate(count + 1, sizeof(uint32_t)) };
    uint64_t samples = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (!logs[i].build)
        {
            speeds.count = costs_tally(speeds.models, speeds.count, logs[i].cpu, 0);
            samples += logs[i].sample_count;
        }
    }
    qsort(speeds.models, speeds.count, sizeof(CostsTally), costs_compare_tally);
    uint32_t reference = 0;
    for (uint32_t m = 1; m < speeds.count; m += 1)
    {
        reference = speeds.models[m].jobs > speeds.models[reference].jobs ? m : reference;
    }
    CostsGroupSample* group = costs_allocate(samples + 1, sizeof(CostsGroupSample));
    uint64_t filled = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint32_t model = 0;
        for (uint32_t m = 0; !logs[i].build && m < speeds.count; m += 1)
        {
            model = strcmp(speeds.models[m].key, logs[i].cpu) == 0 ? m : model;
        }
        speeds.model_of[i] = model;
        for (uint32_t s = 0; !logs[i].build && s < logs[i].sample_count; s += 1)
        {
            if (logs[i].samples[s].complete)
            {
                group[filled] = (CostsGroupSample){ .row = logs[i].row, .command = logs[i].samples[s].command, .model = model, .tenths = logs[i].samples[s].tenths };
                filled += 1;
            }
        }
    }
    qsort(group, filled, sizeof(CostsGroupSample), costs_compare_group);
    // Each (row, command)'s median on each model, then each model's ratio to the reference's.
    uint64_t* medians = costs_allocate(speeds.count + 1, sizeof(uint64_t));
    uint64_t** ratios = costs_allocate(speeds.count + 1, sizeof(uint64_t*));
    uint32_t* ratio_count = costs_allocate(speeds.count + 1, sizeof(uint32_t));
    for (uint32_t m = 0; m < speeds.count; m += 1)
    {
        ratios[m] = costs_allocate((size_t)filled + 1, sizeof(uint64_t));
    }
    uint64_t start = 0;
    while (start < filled)
    {
        uint64_t end = start;
        while (end < filled && group[end].row == group[start].row && group[end].command == group[start].command)
        {
            end += 1;
        }
        for (uint32_t m = 0; m < speeds.count; m += 1)
        {
            medians[m] = 0;
        }
        uint64_t at = start;
        while (at < end)
        {
            uint64_t model_end = at;
            while (model_end < end && group[model_end].model == group[at].model)
            {
                model_end += 1;
            }
            // The samples are sorted: the nearest-rank median is the ceil(n / 2)-th.
            medians[group[at].model] = group[at + (model_end - at + 1) / 2 - 1].tenths;
            at = model_end;
        }
        for (uint32_t m = 0; medians[reference] >= BUSTER_COSTS_RATIO_FLOOR && m < speeds.count; m += 1)
        {
            if (m != reference && medians[m] != 0)
            {
                ratios[m][ratio_count[m]] = medians[m] * 1000 / medians[reference];
                ratio_count[m] += 1;
            }
        }
        start = end;
    }
    speeds.factors = costs_allocate(speeds.count + 1, sizeof(uint64_t));
    for (uint32_t m = 0; m < speeds.count; m += 1)
    {
        speeds.factors[m] = m == reference ? 1000 : ratio_count[m] >= BUSTER_COSTS_OVERLAP ? costs_quantile(ratios[m], ratio_count[m], 50) : 0;
        speeds.slowest = speeds.factors[m] > speeds.slowest ? speeds.factors[m] : speeds.slowest;
        free(ratios[m]);
    }
    free(ratios);
    free(ratio_count);
    free(medians);
    free(group);
    return speeds;
}

static void costs_speeds_free(CostsSpeeds* speeds)
{
    free(speeds->models);
    free(speeds->factors);
    free(speeds->model_of);
    *speeds = (CostsSpeeds){ 0 };
}

// A sample of log `log`'s, as the slowest CPU would have taken it.
static uint64_t costs_normalize(const CostsSpeeds* speeds, uint32_t log, uint64_t tenths)
{
    const uint64_t factor = speeds->factors[speeds->model_of[log]];
    return factor != 0 ? (tenths * speeds->slowest + factor / 2) / factor : tenths;
}

// Each row's table from the logs, into tables[row] (left empty for a row without logs): false with a message into
// `report` when the logs disagree. `names` names each log for the messages.
static bool costs_collect_rows(const CostsList* list, const CostsLog* logs, const char* const* names, uint32_t count, CostsBuffer* tables, CostsBuffer* report)
{
    bool valid = true;
    for (uint32_t i = 0; i < count; i += 1)
    {
        for (uint32_t j = 0; valid && j < i; j += 1)
        {
            // The same job twice: one row's shard of one revision, started at the same instant.
            const bool same = !logs[i].build && !logs[j].build && logs[i].row == logs[j].row && logs[i].shard == logs[j].shard && logs[i].shards == logs[j].shards && logs[i].first == logs[j].first && strcmp(logs[i].revision, logs[j].revision) == 0;
            if (same)
            {
                costs_append(report, "%s: the same job as %s\n", names[i], names[j]);
                valid = false;
            }
        }
    }
    uint64_t** complete = costs_allocate(list->count + 1, sizeof(uint64_t*));
    uint64_t** partial = costs_allocate(list->count + 1, sizeof(uint64_t*));
    uint32_t* complete_count = costs_allocate(list->count + 1, sizeof(uint32_t));
    uint32_t* partial_count = costs_allocate(list->count + 1, sizeof(uint32_t));
    for (uint32_t i = 0; i < list->count; i += 1)
    {
        complete[i] = costs_allocate(count + 1, sizeof(uint64_t));
        partial[i] = costs_allocate(count + 1, sizeof(uint64_t));
    }
    CostsTally* revisions = costs_allocate(count + 1, sizeof(CostsTally));
    CostsTally* cpus = costs_allocate(count + 1, sizeof(CostsTally));
    // Each shard's job's time besides its commands, scaled as its samples are, in tenths: shard 1's and the others'.
    uint64_t* first_outside = costs_allocate(count + 1, sizeof(uint64_t));
    uint64_t* other_outside = costs_allocate(count + 1, sizeof(uint64_t));
    CostsSpeeds speeds = costs_speeds(logs, count);
    for (uint32_t row = 0; valid && row < BUSTER_COSTS_ROWS; row += 1)
    {
        uint32_t jobs = 0;
        uint32_t row_logs = 0;
        uint32_t revision_count = 0;
        uint32_t cpu_count = 0;
        uint32_t first_count = 0;
        uint32_t other_count = 0;
        for (uint32_t i = 0; i < list->count; i += 1)
        {
            complete_count[i] = 0;
            partial_count[i] = 0;
        }
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            if (!logs[i].build && logs[i].row == row)
            {
                if (jobs != 0 && logs[i].jobs != jobs)
                {
                    costs_append(report, "%s: the runner ran at most %u CPUs at once, and the row's other jobs %u\n", names[i], logs[i].jobs, jobs);
                    valid = false;
                }
                jobs = logs[i].jobs;
                row_logs += 1;
                revision_count = costs_tally(revisions, revision_count, logs[i].revision, logs[i].first);
                cpu_count = costs_tally(cpus, cpu_count, logs[i].cpu, 0);
                // A shard's time besides its commands: its job's (the log's first line to its last) less its step's.
                // Only a job whose commands all passed ran its later steps; a vector row's draw also waited for its
                // collector, so draws are left out.
                bool passed = logs[i].summarized && !logs[i].draw && logs[i].shards > 1 && logs[i].first >= 0 && logs[i].last >= logs[i].first;
                for (uint32_t s = 0; s < logs[i].sample_count; s += 1)
                {
                    passed = passed && logs[i].samples[s].complete;
                }
                const uint64_t job_tenths = passed ? (uint64_t)(logs[i].last - logs[i].first) / 100 : 0;
                const uint64_t outside = costs_normalize(&speeds, i, job_tenths > logs[i].step_tenths ? job_tenths - logs[i].step_tenths : 0);
                if (passed && logs[i].shard == 1)
                {
                    first_outside[first_count] = outside;
                    first_count += 1;
                }
                else if (passed)
                {
                    other_outside[other_count] = outside;
                    other_count += 1;
                }
                for (uint32_t s = 0; s < logs[i].sample_count; s += 1)
                {
                    const CostsSample* sample = &logs[i].samples[s];
                    uint64_t* values = sample->complete ? complete[sample->command] : partial[sample->command];
                    uint32_t* filled = sample->complete ? &complete_count[sample->command] : &partial_count[sample->command];
                    values[*filled] = costs_normalize(&speeds, i, sample->tenths);
                    *filled += 1;
                }
            }
        }
        if (valid && row_logs != 0)
        {
            CostsTable table = { .marked = true, .row = row, .jobs = jobs, .samples_min = UINT32_MAX };
            table.milliseconds = costs_allocate(list->count + 1, sizeof(uint64_t));
            table.kinds = costs_allocate(list->count + 1, sizeof(CostsKind));
            for (uint32_t i = 0; i < list->count; i += 1)
            {
                if (complete_count[i] != 0)
                {
                    table.milliseconds[i] = costs_quantile(complete[i], complete_count[i], BUSTER_COSTS_QUANTILE) * 100;
                    table.kinds[i] = COSTS_MEASURED;
                    table.samples_min = complete_count[i] < table.samples_min ? complete_count[i] : table.samples_min;
                    table.samples_max = complete_count[i] > table.samples_max ? complete_count[i] : table.samples_max;
                }
                else if (partial_count[i] != 0)
                {
                    table.milliseconds[i] = costs_quantile(partial[i], partial_count[i], 100) * 100;
                    table.kinds[i] = COSTS_LOWER_BOUND;
                }
            }
            table.samples_min = table.samples_min == UINT32_MAX ? 0 : table.samples_min;
            if (first_count != 0 && other_count != 0)
            {
                // Shard 1's once-only steps: the medians' difference, none where shard 1's is the shorter.
                const uint64_t first = costs_quantile(first_outside, first_count, 50);
                const uint64_t other = costs_quantile(other_outside, other_count, 50);
                table.first_milliseconds = first > other ? (first - other) * 100 : 0;
                table.first_given = true;
            }
            qsort(revisions, revision_count, sizeof(CostsTally), costs_compare_tally);
            qsort(cpus, cpu_count, sizeof(CostsTally), costs_compare_tally);
            for (uint32_t i = 0; i < revision_count; i += 1)
            {
                costs_append(&table.provenance, "#: revision %s %u\n", revisions[i].key, revisions[i].jobs);
            }
            for (uint32_t i = 0; i < cpu_count; i += 1)
            {
                // Its speed: its time against the slowest CPU's, `-` where the logs don't say.
                uint64_t factor = 0;
                for (uint32_t m = 0; m < speeds.count; m += 1)
                {
                    factor = strcmp(speeds.models[m].key, cpus[i].key) == 0 ? speeds.factors[m] : factor;
                }
                char speed[16];
                snprintf(speed, sizeof(speed), factor != 0 ? "%.2f" : "-", (double)factor / (double)speeds.slowest);
                costs_append(&table.provenance, "#: cpu %u %s %s\n", cpus[i].jobs, speed, cpus[i].key);
            }
            uint64_t median = 0;
            if (!costs_table_median(&table, list, &median))
            {
                char name[64];
                costs_row_name(row, name, sizeof(name));
                costs_append(report, "%s: its logs measure no command\n", name);
                valid = false;
            }
            else
            {
                costs_table_write(&table, list, &tables[row]);
            }
            costs_table_free(&table);
        }
    }
    for (uint32_t i = 0; i < list->count; i += 1)
    {
        free(complete[i]);
        free(partial[i]);
    }
    free(complete);
    free(partial);
    free(complete_count);
    free(partial_count);
    free(revisions);
    free(cpus);
    free(first_outside);
    free(other_outside);
    costs_speeds_free(&speeds);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The plan: the runner's deal (test_deal.h) and its schedule within each shard

// A shard's predicted time, in milliseconds: its commands in the list's order, each starting as soon as the running ones
// leave it its CPUs (at most `jobs` in all; a command of more runs alone, holding them all), and taking its cost over
// the CPUs it holds, as the runner schedules them (docs/commands/parallel.md).
static uint64_t costs_simulate(const CostsList* list, const uint64_t* milliseconds, const uint64_t* assigned, uint64_t shard, uint32_t jobs, CostsBuffer* listing)
{
    uint64_t* ends = costs_allocate(list->count + 1, sizeof(uint64_t));
    uint32_t* holds = costs_allocate(list->count + 1, sizeof(uint32_t));
    uint32_t running = 0;
    uint32_t free_cpus = jobs;
    uint64_t now = 0;
    uint32_t next = 0;
    while (next < list->count || running != 0)
    {
        while (next < list->count && assigned[next] != shard)
        {
            next += 1;
        }
        const uint32_t held = next < list->count ? costs_held(&list->commands[next], jobs) : 0;
        if (next < list->count && held <= free_cpus)
        {
            if (listing != NULL)
            {
                costs_append(listing, "shard %llu: %7.1f s to %7.1f s, %u CPUs: %.*s\n", (unsigned long long)shard + 1, (double)now / 1000.0, (double)(now + milliseconds[next] / held) / 1000.0, held, (int)list->commands[next].title.length, list->commands[next].title.data);
            }
            ends[running] = now + milliseconds[next] / held;
            holds[running] = held;
            running += 1;
            free_cpus -= held;
            next += 1;
        }
        else if (running != 0)
        {
            // The earliest end, and every command that ends then.
            uint64_t earliest = ends[0];
            for (uint32_t i = 1; i < running; i += 1)
            {
                earliest = ends[i] < earliest ? ends[i] : earliest;
            }
            now = earliest;
            uint32_t kept = 0;
            for (uint32_t i = 0; i < running; i += 1)
            {
                if (ends[i] == earliest)
                {
                    free_cpus += holds[i];
                }
                else
                {
                    ends[kept] = ends[i];
                    holds[kept] = holds[i];
                    kept += 1;
                }
            }
            running = kept;
        }
    }
    free(ends);
    free(holds);
    return now;
}

// A costs file's milliseconds for the list as parallel_costs_parse gives them: a command without a line costs its CPUs
// (uncapped) times 10 s. Any table, generated or not (the plan may deal by an older file's costs). *jobs is its
// runner's CPUs (`#: runner-cpus`), or 0 without them, and *first its shard 1's once-only steps' milliseconds
// (`#: first-shard`), or 0.
static bool costs_plan_costs(const CostsList* list, const char* path, uint64_t* milliseconds, uint32_t* jobs, uint64_t* first)
{
    CostsText text = { 0 };
    const bool read = costs_read(path, &text);
    CostsTable table = read ? costs_table_parse(list, text) : (CostsTable){ 0 };
    const bool valid = read && table.error == NULL && table.duplicates == 0;
    for (uint32_t i = 0; valid && i < list->count; i += 1)
    {
        const bool given = table.kinds[i] != COSTS_ABSENT;
        milliseconds[i] = given ? table.milliseconds[i] : list->commands[i].cpus * BUSTER_COSTS_DEFAULT_MILLISECONDS;
    }
    if (!valid)
    {
        fprintf(stderr, "costs: %s: %s\n", path, !read ? strerror(errno) : table.error != NULL ? table.error : "a title twice");
    }
    *jobs = valid ? table.jobs : 0;
    *first = valid && table.first_given ? table.first_milliseconds : 0;
    costs_table_free(&table);
    free(text.data);
    return valid;
}

typedef struct CostsPlan CostsPlan;
struct CostsPlan
{
    uint64_t longest;
    uint64_t longest_shard;
    uint64_t total;
    uint64_t floor;
    uint32_t floor_command;
};

// The deal of the list to `shards` shards by `deal` (as `parallel --shard K/N --costs` deals: by each shard's schedule
// on `deal_cpus` CPUs where the deal's costs name them, by costs alone where 0), each shard's predicted time by
// `milliseconds` on a runner of `jobs` CPUs, into `times` (one per shard), and the row's summary. `first` is the time of
// the steps that a row runs on its shard 1 alone, added to its time; `deal_first` is the deal's own (its file's
// `#: first-shard`), which the scheduled deal adds to shard 1's as parallel's does.
static CostsPlan costs_plan(const CostsList* list, const uint64_t* deal, uint32_t deal_cpus, uint64_t deal_first, const uint64_t* milliseconds, uint32_t shards, uint32_t jobs, uint64_t first, uint64_t* times, CostsBuffer* listing)
{
    uint64_t* order = costs_allocate(list->count + 1, sizeof(uint64_t));
    uint64_t* loads = costs_allocate(shards + 1, sizeof(uint64_t));
    uint64_t* assigned = costs_allocate(list->count + 1, sizeof(uint64_t));
    if (deal_cpus != 0)
    {
        // As parallel_shards deals by costs that name their runner's CPUs.
        uint64_t* held = costs_allocate(list->count + 1, sizeof(uint64_t));
        uint64_t* ends = costs_allocate(deal_cpus + 1, sizeof(uint64_t));
        uint64_t* holds = costs_allocate(deal_cpus + 1, sizeof(uint64_t));
        for (uint32_t i = 0; i < list->count; i += 1)
        {
            held[i] = costs_held(&list->commands[i], deal_cpus);
        }
        test_deal_scheduled(deal, held, list->count, shards, deal_cpus, deal_first, order, loads, assigned, ends, holds);
        free(held);
        free(ends);
        free(holds);
    }
    else
    {
        test_deal(deal, list->count, shards, order, loads, assigned);
    }
    CostsPlan plan = { 0 };
    for (uint32_t k = 0; k < shards; k += 1)
    {
        times[k] = costs_simulate(list, milliseconds, assigned, k, jobs, listing) + (k == 0 ? first : 0);
        plan.longest_shard = times[k] > plan.longest ? k : plan.longest_shard;
        plan.longest = times[k] > plan.longest ? times[k] : plan.longest;
        plan.total += times[k];
    }
    for (uint32_t i = 0; i < list->count; i += 1)
    {
        const uint64_t alone = milliseconds[i] / costs_held(&list->commands[i], jobs);
        plan.floor_command = alone > plan.floor ? i : plan.floor_command;
        plan.floor = alone > plan.floor ? alone : plan.floor;
    }
    free(order);
    free(loads);
    free(assigned);
    return plan;
}

// ---------------------------------------------------------------------------------------------------------------------
// The report: a run's rows as they ran

static int costs_compare_logs(const void* a, const void* b)
{
    const CostsLog* x = a;
    const CostsLog* y = b;
    const int row = (x->row > y->row) - (x->row < y->row);
    const int build = (int)y->build - (int)x->build;
    const int shard = (x->shard > y->shard) - (x->shard < y->shard);
    return row != 0 ? row : build != 0 ? build : shard;
}

static uint64_t costs_job_seconds(const CostsLog* log)
{
    return log->first >= 0 && log->last >= log->first ? (uint64_t)(log->last - log->first + 500) / 1000 : 0;
}

// Each row's shards (their test step, the runner's summary, and their whole job), its build, the longest shard, the
// imbalance (the longest shard's step over their mean) and the runner-minutes (each job's, rounded up to a minute as
// GitHub bills them), as Markdown, into `output`. False when a row has a shard twice.
static bool costs_report(CostsLog* logs, uint32_t count, CostsBuffer* output)
{
    qsort(logs, count, sizeof(CostsLog), costs_compare_logs);
    bool valid = true;
    for (uint32_t i = 1; i < count; i += 1)
    {
        valid = valid && !(logs[i].row == logs[i - 1].row && logs[i].build == logs[i - 1].build && logs[i].shard == logs[i - 1].shard);
    }
    uint64_t all_minutes = 0;
    uint64_t all_seconds = 0;
    costs_append(output, "| Row | Shards | Longest shard (test step) | Mean | Imbalance | Row's span | Runner-minutes | Jobs' CPUs |\n");
    costs_append(output, "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |\n");
    uint32_t start = 0;
    while (valid && start < count)
    {
        uint32_t end = start;
        while (end < count && logs[end].row == logs[start].row)
        {
            end += 1;
        }
        uint64_t longest = 0;
        uint64_t sum = 0;
        uint32_t shards = 0;
        uint64_t minutes = 0;
        uint64_t seconds = 0;
        int64_t first = INT64_MAX;
        int64_t last = 0;
        CostsBuffer cpus = { 0 };
        for (uint32_t i = start; i < end; i += 1)
        {
            const uint64_t job = costs_job_seconds(&logs[i]);
            seconds += job;
            minutes += (job + 59) / 60;
            first = logs[i].first >= 0 && logs[i].first < first ? logs[i].first : first;
            last = logs[i].last > last ? logs[i].last : last;
            if (!logs[i].build)
            {
                longest = logs[i].step_tenths > longest ? logs[i].step_tenths : longest;
                sum += logs[i].step_tenths;
                shards += 1;
                costs_append(&cpus, "%s%u: %s", shards == 1 ? "" : "; ", logs[i].shard, logs[i].cpu);
            }
        }
        char name[64];
        costs_row_name(logs[start].row, name, sizeof(name));
        const double mean = shards != 0 ? (double)sum / shards / 10.0 : 0.0;
        const double span = last >= first ? (double)(last - first) / 1000.0 : 0.0;
        costs_append(output, "| %s | %u | %.1f s | %.1f s | %.2f | %.0f s | %llu | %.*s |\n", name, shards, (double)longest / 10.0, mean, mean > 0 ? (double)longest / 10.0 / mean : 0.0, span, (unsigned long long)minutes, (int)cpus.length, cpus.data != NULL ? cpus.data : "");
        free(cpus.data);
        all_minutes += minutes;
        all_seconds += seconds;
        start = end;
    }
    costs_append(output, "\nRunner-minutes, every job above rounded up to a minute: %llu (%.1f minutes unrounded).\n", (unsigned long long)all_minutes, (double)all_seconds / 60.0);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The self-test

typedef struct CostsTest CostsTest;
struct CostsTest
{
    uint32_t checks;
    uint32_t failures;
};

static void costs_expect(CostsTest* test, bool condition, const char* what)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        fprintf(stderr, "costs self-test: FAILED: %s\n", what);
    }
}

static const char costs_test_list[] = "# The fixture inventory.\n"
                                      "\n"
                                      "== Alpha [4 CPUs]\n"
                                      "true\n"
                                      "== Beta: with a colon\n"
                                      "true\n"
                                      "== Gamma [2 CPUs]\n"
                                      "true\n"
                                      "== Delta\n"
                                      "true\n"
                                      "== Epsilon 100%\n"
                                      "true\n";

// A shard's log as GitHub serves it: a BOM, each line after its time, the job's name, its CPU, its checkout and the
// runner's report with `report` (headings, each `TITLE: OUTCOME`) between its shard line and its summary, and its last
// line at `ending` (`MM:SS` past 01:00).
static CostsText costs_test_log_ending(const char* name, const char* cpu, const char* revision, const char* shard, const char* report, const char* minute, const char* ending)
{
    CostsBuffer buffer = { 0 };
    costs_append(&buffer, "\xEF\xBB\xBF" "2026-11-11T01:%s:00.1000000Z Current runner version: '2.338.2'\n", minute);
    costs_append(&buffer, "2026-11-11T01:%s:01.0000000Z Complete job name: %s\n", minute, name);
    costs_append(&buffer, "2026-11-11T01:%s:02.0000000Z ##[group]Run grep -m1 '^model name[[:space:]]*:' /proc/cpuinfo\n", minute);
    costs_append(&buffer, "2026-11-11T01:%s:03.0000000Z model name\t: %s\n", minute, cpu);
    costs_append(&buffer, "2026-11-11T01:%s:04.0000000Z [command]/usr/bin/git checkout --progress --force %s\n", minute, revision);
    costs_append(&buffer, "2026-11-11T01:%s:05.0000000Z ##[group]Before the report: 9.9 s, 9.9 s of CPU\n", minute);
    costs_append(&buffer, "2026-11-11T01:%s:06.0000000Z parallel: shard %s, at most 4 CPUs at once\n", minute, shard);
    const char* line = report;
    while (*line != 0)
    {
        const char* end = strchr(line, '\n');
        costs_append(&buffer, "2026-11-11T01:%s:07.0000000Z ##[group]%.*s\n", minute, (int)(end - line), line);
        costs_append(&buffer, "2026-11-11T01:%s:07.5000000Z the command's own output\n", minute);
        costs_append(&buffer, "2026-11-11T01:%s:07.6000000Z ##[endgroup]\n", minute);
        line = end + 1;
    }
    costs_append(&buffer, "2026-11-11T01:%s:08.0000000Z parallel: 3 commands in 42.5 s, 50.0 s of CPU, at most 4 CPUs at once: 3 passed, 0 failed, 0 skipped, 0 not started\n", minute);
    costs_append(&buffer, "2026-11-11T01:%s:09.0000000Z ##[group]After the report: passed in 1.0 s, 1.0 s of CPU\n", minute);
    costs_append(&buffer, "2026-11-11T01:%s.0000000Z Cleaning up orphan processes\n", ending);
    return (CostsText){ .data = buffer.data, .length = buffer.length };
}

// The same, its last line half a minute after its first.
static CostsText costs_test_log(const char* name, const char* cpu, const char* revision, const char* shard, const char* report, const char* minute)
{
    char ending[16];
    snprintf(ending, sizeof(ending), "%s:30", minute);
    return costs_test_log_ending(name, cpu, revision, shard, report, minute, ending);
}

#define BUSTER_COSTS_TEST_SHA_A "2eff3c58125390bf06c13a681d221cc50b3643f8"
#define BUSTER_COSTS_TEST_SHA_B "92da7275e3a0d6a2c9a1c7ba3a5e8b2f3c4d5e6f"
#define BUSTER_COSTS_TEST_SHARD_1 "Tests on Ubuntu 26.04 (x86-64, none, O3) / Tests on Ubuntu 26.04 (Clang 23, x86-64, none, O3, shard 1 of 2)"
#define BUSTER_COSTS_TEST_SHARD_2 "Tests on Ubuntu 26.04 (Clang 23, x86-64, none, O3, shard 2 of 2)"

static bool costs_test_contains(const CostsBuffer* buffer, const char* text)
{
    return buffer->data != NULL && memmem(buffer->data, buffer->length, text, strlen(text)) != NULL;
}

// A text with `before` (which must occur) replaced by `after`.
static CostsText costs_test_replace(const CostsBuffer* buffer, const char* before, const char* after)
{
    const char* found = buffer->data != NULL ? memmem(buffer->data, buffer->length, before, strlen(before)) : NULL;
    CostsBuffer changed = { 0 };
    if (found != NULL)
    {
        const size_t prefix = (size_t)(found - buffer->data);
        costs_append(&changed, "%.*s%s%.*s", (int)prefix, buffer->data, after, (int)(buffer->length - prefix - strlen(before)), found + strlen(before));
    }
    else
    {
        costs_append(&changed, "%s", "not found");
    }
    return (CostsText){ .data = changed.data, .length = changed.length };
}

static void costs_test_logs(CostsTest* test, const CostsList* list)
{
    CostsText text = costs_test_log(BUSTER_COSTS_TEST_SHARD_1, "AMD EPYC 7763 64-Core Processor", BUSTER_COSTS_TEST_SHA_A, "1 of 2, 3 of 5 commands",
                                     "Alpha [4 CPUs]: passed in 10.0 s, 30.0 s of CPU\nBeta: with a colon: failed (killed by signal 9: Killed) in 2.5 s, 2.0 s of CPU\nEpsilon 100%25: skipped in 0.3 s, 0.1 s of CPU\nRemoved: passed in 1.0 s, 1.0 s of CPU\nDelta: not started\n", "14");
    CostsLog log = costs_log_parse(list, text);
    costs_expect(test, log.error == NULL, "a shard's log parses");
    costs_expect(test, !log.build && log.row == 0 && log.shard == 1 && log.shards == 2 && log.jobs == 4, "its row, shard and runner's CPUs");
    costs_expect(test, strcmp(log.cpu, "AMD EPYC 7763 64-Core Processor") == 0 && strcmp(log.revision, BUSTER_COSTS_TEST_SHA_A) == 0, "its CPU and revision");
    costs_expect(test, log.step_tenths == 425 && log.first == 1794359640100 && log.last == 1794359670000, "its step's time and its first and last lines' times");
    costs_expect(test, log.sample_count == 3 && log.unknown == 1, "three samples, a heading the list doesn't have counted, a command not started without one");
    costs_expect(test, log.sample_count == 3 && log.samples[0].command == 0 && log.samples[0].tenths == 400 && log.samples[0].complete, "a 4-CPU command's time times 4");
    costs_expect(test, log.sample_count == 3 && log.samples[1].command == 1 && log.samples[1].tenths == 25 && !log.samples[1].complete, "a title with \": \" and a failure whose reason has one: a lower bound");
    costs_expect(test, log.sample_count == 3 && log.samples[2].command == 4 && log.samples[2].tenths == 3 && log.samples[2].complete, "a skipped command's time, its escaped title read back");
    free(log.samples);
    free(text.data);
    // The report's lines before `parallel: shard` and after its summary aren't samples, nor are another group's.
    static const struct
    {
        const char* name;
        const char* before;
        const char* after;
        const char* error;
    } mutants[] = {
        { "no job name before its report", "Complete job name: ", "Job name: ", "a report before the job's name, or in a build job" },
        { "two job names", "model name\t: ", "Complete job name: x\nmodel name\t: ", "two job names" },
        { "no CPU", "model name\t: ", "model: ", "no CPU model" },
        { "no revision", "checkout --progress --force", "checkout --force", "no checkout of the revision" },
        { "a short revision", "--force " BUSTER_COSTS_TEST_SHA_A, "--force 2eff3c5", "a checkout of something other than a commit" },
        { "no report", "parallel: shard 1 of 2", "parallel: shards 1 of 2", "no runner's report" },
        { "no summary", "3 commands in 42.5 s", "3 commands", "a runner's report without its summary" },
        { "a report of another shard", "parallel: shard 1 of 2", "parallel: shard 2 of 2", "a shard line that isn't the job's shard" },
        { "a report of another count", "parallel: shard 1 of 2", "parallel: shard 1 of 3", "a shard line that isn't the job's shard" },
        { "a job of another kind", "Clang 23, x86-64, none, O3, shard 1 of 2", "Clang 23, x86-64, none, O1, shard 1 of 2", "a job that is neither a hosted row's shard nor its build" },
        { "an unknown architecture", "Clang 23, x86-64, none", "Clang 23, arm64, none", "a job that is neither a hosted row's shard nor its build" },
        { "a shard past the count", "O3, shard 1 of 2)\n", "O3, shard 3 of 2)\n", "a job that is neither a hosted row's shard nor its build" },
        { "a heading twice", "Alpha [4 CPUs]: passed in 10.0 s, 30.0 s of CPU\n", "Alpha [4 CPUs]: passed in 10.0 s, 30.0 s of CPU\n2026-11-11T01:14:07.0000000Z ##[group]Alpha [4 CPUs]: passed in 1.0 s, 3.0 s of CPU\n", "a command's heading twice" },
        { "a NUL byte", "the command's own output", "the command's\x01own output", NULL },
    };
    for (uint32_t i = 0; i < sizeof(mutants) / sizeof(mutants[0]); i += 1)
    {
        CostsText original = costs_test_log(BUSTER_COSTS_TEST_SHARD_1, "AMD EPYC 7763 64-Core Processor", BUSTER_COSTS_TEST_SHA_A, "1 of 2, 3 of 5 commands", "Alpha [4 CPUs]: passed in 10.0 s, 30.0 s of CPU\n", "14");
        const CostsBuffer as_buffer = { .data = original.data, .length = original.length };
        CostsText changed = costs_test_replace(&as_buffer, mutants[i].before, mutants[i].after);
        if (i == sizeof(mutants) / sizeof(mutants[0]) - 1)
        {
            // A NUL byte, which no format can write.
            char* at = memchr(changed.data, '\x01', changed.length);
            *at = 0;
        }
        CostsLog mutant = costs_log_parse(list, changed);
        const bool rejected = mutant.error != NULL && (mutants[i].error == NULL ? strcmp(mutant.error, "a NUL byte") == 0 : strcmp(mutant.error, mutants[i].error) == 0);
        char what[160];
        snprintf(what, sizeof(what), "a log with %s is rejected", mutants[i].name);
        costs_expect(test, rejected, what);
        free(mutant.samples);
        free(changed.data);
        free(original.data);
    }
    // A build job's log: its row from its caller's name, without a report.
    CostsText build = costs_text_of("2026-11-11T01:11:38.0000000Z Complete job name: Tests on Ubuntu 26.04 (native, thread, O3) / Build the compiler once for the row's shards\n2026-11-11T01:15:47.0000000Z done\n");
    CostsLog built = costs_log_parse(list, build);
    costs_expect(test, built.error == NULL && built.build && built.row == 1 * BUSTER_COSTS_SANITIZERS + 2 && costs_job_seconds(&built) == 249, "a build job's log: its row and its time");
    free(built.samples);
    free(build.data);
    // A vector row's draw (#2033): its row is its caller's, and its shard the item its report runs, which the name
    // doesn't give. A draw anywhere but the vector row is no row's.
    CostsText drawn = costs_test_log("Tests on Ubuntu 26.04 (vector, none, O3) / Vector draw 2, candidate 7 (Clang 23, none, O3)", "AMD EPYC 9V45 96-Core Processor", BUSTER_COSTS_TEST_SHA_A, "3 of 4, 1 of 5 commands", "Gamma [2 CPUs]: passed in 3.0 s, 6.0 s of CPU\n", "20");
    CostsLog draw = costs_log_parse(list, drawn);
    costs_expect(test, draw.error == NULL && draw.draw && draw.row == BUSTER_COSTS_VECTOR * BUSTER_COSTS_SANITIZERS && draw.shard == 3 && draw.shards == 4 && draw.sample_count == 1, "a vector draw's row from its caller, its shard from its report");
    free(draw.samples);
    free(drawn.data);
    CostsText stray = costs_test_log("Tests on Ubuntu 26.04 (x86-64, none, O3) / Vector draw 1, candidate 1 (Clang 23, none, O3)", "AMD EPYC 9V45 96-Core Processor", BUSTER_COSTS_TEST_SHA_A, "1 of 4, 1 of 5 commands", "Gamma [2 CPUs]: passed in 3.0 s, 6.0 s of CPU\n", "20");
    CostsLog strayed = costs_log_parse(list, stray);
    costs_expect(test, strayed.error != NULL && strcmp(strayed.error, "a vector draw whose caller isn't the vector row") == 0, "a draw outside the vector row is rejected");
    free(strayed.samples);
    free(stray.data);
    // A pool's draw (the plain and sanitizer configurations as one row of items): its row is its item line's, which
    // must be one of its caller's configurations and the shard its report runs; without the line it is no row's.
    static const struct
    {
        const char* caller;
        const char* item;
        const char* shard;
        uint32_t row;
        const char* error;
    } pools[] = {
        { "none address thread memory", "Vector item t3: thread, shard 3 of 4", "3 of 4", 2 * BUSTER_COSTS_SANITIZERS + 2, NULL },
        { "none address thread memory", "Vector item m1: memory, shard 1 of 4", "1 of 4", 2 * BUSTER_COSTS_SANITIZERS + 3, NULL },
        { "none", "Vector item n2: none, shard 2 of 4", "2 of 4", 2 * BUSTER_COSTS_SANITIZERS, NULL },
        { "none address thread memory", NULL, "3 of 4", 0, "a pool's draw without its vector item" },
        { "none thread", "Vector item a1: address, shard 1 of 4", "1 of 4", 0, "a vector item that isn't one of its draw's items" },
        { "none thread", "Vector item n1: thread, shard 1 of 4", "1 of 4", 0, "a vector item that isn't one of its draw's items" },
        { "none thread", "Vector item t2: thread, shard 3 of 4", "3 of 4", 0, "a vector item that isn't one of its draw's items" },
        { "none thread", "Vector item t2: thread, shard 2 of 4", "3 of 4", 0, "a vector item that isn't the draw's row or the report's shard" },
        { "none thread", "Vector item t2: thread, shard 2 of 4, more", "2 of 4", 0, "a vector item that isn't one of its draw's items" },
        { "none none", "Vector item n2: none, shard 2 of 4", "2 of 4", 0, "a vector draw whose caller isn't the vector row" },
        { "none undefined", "Vector item n2: none, shard 2 of 4", "2 of 4", 0, "a vector draw whose caller isn't the vector row" },
    };
    for (uint32_t i = 0; i < sizeof(pools) / sizeof(pools[0]); i += 1)
    {
        char name[512];
        snprintf(name, sizeof(name), "Tests on Ubuntu 26.04 (vector, %s, O3) / Vector draw 1, candidate 5 (Clang 23, %s, O3)", pools[i].caller, pools[i].caller);
        char shard[64];
        snprintf(shard, sizeof(shard), "%s, 1 of 5 commands", pools[i].shard);
        CostsText pooled = costs_test_log(name, "AMD EPYC 9V45 96-Core Processor", BUSTER_COSTS_TEST_SHA_A, shard, "Gamma [2 CPUs]: passed in 3.0 s, 6.0 s of CPU\n", "21");
        char line[256];
        snprintf(line, sizeof(line), "2026-11-11T01:21:05.5000000Z %s\n2026-11-11T01:21:06.0000000Z parallel: shard ", pools[i].item != NULL ? pools[i].item : "no item");
        CostsText itemized = costs_test_replace(&(CostsBuffer){ .data = pooled.data, .length = pooled.length }, "2026-11-11T01:21:06.0000000Z parallel: shard ", line);
        CostsLog log = costs_log_parse(list, itemized);
        const bool expected = pools[i].error == NULL ? log.error == NULL && log.draw && log.row == pools[i].row && log.sample_count == 1
                                                     : log.error != NULL && strcmp(log.error, pools[i].error) == 0;
        costs_expect(test, expected, pools[i].error == NULL ? "a pool's draw takes its item's row" : pools[i].error);
        free(log.samples);
        free(itemized.data);
        free(pooled.data);
    }
    CostsText outside = costs_test_log(BUSTER_COSTS_TEST_SHARD_1, "AMD EPYC 9V45 96-Core Processor", BUSTER_COSTS_TEST_SHA_A, "1 of 2, 1 of 5 commands", "Gamma [2 CPUs]: passed in 3.0 s, 6.0 s of CPU\n", "22");
    CostsText inserted = costs_test_replace(&(CostsBuffer){ .data = outside.data, .length = outside.length }, "2026-11-11T01:22:06.0000000Z parallel: shard ", "2026-11-11T01:22:05.0000000Z Vector item n1: none, shard 1 of 2\n2026-11-11T01:22:06.0000000Z parallel: shard ");
    CostsLog stranger = costs_log_parse(list, inserted);
    costs_expect(test, stranger.error != NULL && strcmp(stranger.error, "a vector item outside a vector draw") == 0, "a vector item outside a draw is rejected");
    free(stranger.samples);
    free(inserted.data);
    free(outside.data);
    CostsText nameless = costs_text_of("2026-11-11T01:11:38.0000000Z Current runner version: '2.338.2'\n");
    CostsLog unnamed = costs_log_parse(list, nameless);
    costs_expect(test, unnamed.error != NULL && strcmp(unnamed.error, "no job name") == 0, "a log without a job name is rejected");
    free(unnamed.samples);
    free(nameless.data);
    // Malformed outcomes are no samples: a time without its tenth, a missing CPU time, a failure without its reason.
    uint64_t tenths = 0;
    bool complete = false;
    costs_expect(test, !costs_outcome(BUSTER_COSTS_SLICE("passed in 10 s, 3.0 s of CPU"), &tenths, &complete), "a time without its tenth");
    costs_expect(test, !costs_outcome(BUSTER_COSTS_SLICE("passed in 10.0 s"), &tenths, &complete), "a time without its CPU time");
    costs_expect(test, !costs_outcome(BUSTER_COSTS_SLICE("failed in 1.0 s, 1.0 s of CPU"), &tenths, &complete), "a failure without its reason");
    costs_expect(test, !costs_outcome(BUSTER_COSTS_SLICE("passed in 1.0 s, 1.0 s of CPU and more"), &tenths, &complete), "a line that goes on after its CPU time");
    costs_expect(test, costs_outcome(BUSTER_COSTS_SLICE("passed in 2.5 s, 1.0 s of CPU, 812 MiB peak"), &tenths, &complete) && tenths == 25 && complete, "a time with its peak memory");
    costs_expect(test, !costs_outcome(BUSTER_COSTS_SLICE("passed in 2.5 s, 1.0 s of CPU, MiB peak"), &tenths, &complete), "a peak memory without its number");
    costs_expect(test, !costs_outcome(BUSTER_COSTS_SLICE("passed in 2.5 s, 1.0 s of CPU, 8.5 MiB peak"), &tenths, &complete), "a peak memory with a fraction");
    costs_expect(test, !costs_outcome(BUSTER_COSTS_SLICE("passed in 2.5 s, 1.0 s of CPU, 8 MiB peak and more"), &tenths, &complete), "a line that goes on after its peak memory");
    costs_expect(test, !costs_outcome(BUSTER_COSTS_SLICE("passed in 2.5 s, 1.0 s of CPU, 8 MiB"), &tenths, &complete), "a peak memory without its word");
    costs_expect(test, costs_outcome(BUSTER_COSTS_SLICE("failed (timed out) in 600.1 s, 1.0 s of CPU"), &tenths, &complete) && tenths == 6001 && !complete, "a timeout's time, a lower bound");
}

static void costs_test_quantiles(CostsTest* test)
{
    uint64_t one[] = { 7 };
    uint64_t two[] = { 9, 3 };
    uint64_t ten[] = { 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 };
    uint64_t eleven[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    costs_expect(test, costs_quantile(one, 1, 90) == 7, "the 90th percentile of one sample is it");
    costs_expect(test, costs_quantile(two, 2, 90) == 9, "of two, the larger");
    costs_expect(test, costs_quantile(ten, 10, 90) == 9, "of ten, the ninth smallest");
    costs_expect(test, costs_quantile(eleven, 11, 90) == 10, "of eleven, the tenth");
    costs_expect(test, costs_quantile(eleven, 11, 50) == 6, "the median of eleven, the sixth");
}

// Collects the fixture logs, in the order `order` gives, into the x86-64 plain row's table. Revision B's logs give
// each command's peak memory after its CPU time, as the runner does since #2031, and revision A's don't.
static bool costs_test_collect(const CostsList* list, const uint32_t* order, CostsBuffer* table, CostsBuffer* report)
{
    CostsText texts[4] = {
        costs_test_log(BUSTER_COSTS_TEST_SHARD_1, "AMD EPYC 7763 64-Core Processor", BUSTER_COSTS_TEST_SHA_A, "1 of 2, 2 of 5 commands", "Alpha [4 CPUs]: passed in 10.0 s, 30.0 s of CPU\nBeta: with a colon: passed in 5.0 s, 5.0 s of CPU\n", "14"),
        costs_test_log(BUSTER_COSTS_TEST_SHARD_2, "AMD EPYC 9V45 96-Core Processor", BUSTER_COSTS_TEST_SHA_A, "2 of 2, 3 of 5 commands", "Gamma [2 CPUs]: passed in 3.0 s, 6.0 s of CPU\nDelta: failed (exit status 1) in 2.0 s, 2.0 s of CPU\n", "15"),
        costs_test_log(BUSTER_COSTS_TEST_SHARD_1, "AMD EPYC 9V45 96-Core Processor", BUSTER_COSTS_TEST_SHA_B, "1 of 2, 2 of 5 commands", "Alpha [4 CPUs]: passed in 20.0 s, 60.0 s of CPU, 2048 MiB peak\nBeta: with a colon: passed in 7.0 s, 7.0 s of CPU, 3 MiB peak\n", "40"),
        costs_test_log(BUSTER_COSTS_TEST_SHARD_2, "AMD EPYC 9V45 96-Core Processor", BUSTER_COSTS_TEST_SHA_B, "2 of 2, 3 of 5 commands", "Gamma [2 CPUs]: passed in 4.0 s, 8.0 s of CPU, 12 MiB peak\nDelta: failed (exit status 1) in 3.0 s, 3.0 s of CPU, 5 MiB peak\n", "41"),
    };
    CostsLog logs[4];
    const char* names[4];
    bool parsed = true;
    for (uint32_t i = 0; i < 4; i += 1)
    {
        logs[i] = costs_log_parse(list, texts[order[i]]);
        names[i] = "fixture";
        parsed = parsed && logs[i].error == NULL;
    }
    CostsBuffer tables[BUSTER_COSTS_ROWS] = { 0 };
    bool collected = parsed && costs_collect_rows(list, logs, names, 4, tables, report);
    *table = tables[0];
    for (uint32_t i = 1; i < BUSTER_COSTS_ROWS; i += 1)
    {
        collected = collected && tables[i].length == 0;
        free(tables[i].data);
    }
    for (uint32_t i = 0; i < 4; i += 1)
    {
        free(logs[i].samples);
        free(texts[i].data);
    }
    return collected;
}

// Shard 1's once-only steps: two revisions' jobs, whose commands all passed, each step 42.5 s. Shard 1's jobs ran
// 79.9 s, 37.4 s besides their step; shard 2's 49.9 and 59.9 s, 7.4 and 17.4 s besides theirs, whose median (the
// lower of two) is 7.4 s. Shard 1 runs 30.0 s more.
static void costs_test_first(CostsTest* test, const CostsList* list)
{
    static const char cpu[] = "AMD EPYC 9V45 96-Core Processor";
    static const char first_report[] = "Alpha [4 CPUs]: passed in 10.0 s, 30.0 s of CPU\nBeta: with a colon: passed in 5.0 s, 5.0 s of CPU\n";
    static const char other_report[] = "Gamma [2 CPUs]: passed in 3.0 s, 6.0 s of CPU\nDelta: passed in 2.0 s, 2.0 s of CPU\n";
    CostsText texts[4] = {
        costs_test_log_ending(BUSTER_COSTS_TEST_SHARD_1, cpu, BUSTER_COSTS_TEST_SHA_A, "1 of 2, 2 of 5 commands", first_report, "14", "15:20"),
        costs_test_log_ending(BUSTER_COSTS_TEST_SHARD_2, cpu, BUSTER_COSTS_TEST_SHA_A, "2 of 2, 3 of 5 commands", other_report, "14", "14:50"),
        costs_test_log_ending(BUSTER_COSTS_TEST_SHARD_1, cpu, BUSTER_COSTS_TEST_SHA_B, "1 of 2, 2 of 5 commands", first_report, "40", "41:20"),
        costs_test_log_ending(BUSTER_COSTS_TEST_SHARD_2, cpu, BUSTER_COSTS_TEST_SHA_B, "2 of 2, 3 of 5 commands", other_report, "40", "41:00"),
    };
    CostsLog logs[4];
    const char* names[4] = { "a1", "a2", "b1", "b2" };
    bool parsed = true;
    for (uint32_t i = 0; i < 4; i += 1)
    {
        logs[i] = costs_log_parse(list, texts[i]);
        parsed = parsed && logs[i].error == NULL;
    }
    CostsBuffer tables[BUSTER_COSTS_ROWS] = { 0 };
    CostsBuffer report = { 0 };
    const bool collected = parsed && costs_collect_rows(list, logs, names, 4, tables, &report);
    costs_expect(test, collected && costs_test_contains(&tables[0], "\n#: runner-cpus 4\n#: first-shard 30.0\n#: samples 2 2\n"), "shard 1's once-only steps: the medians' difference of the jobs' time besides their steps");
    CostsText text = { .data = tables[0].data, .length = tables[0].length };
    CostsBuffer problems = { 0 };
    costs_expect(test, collected && costs_check_text(list, 0, text, &problems) == 0, "a table with a first shard's time checks");
    CostsTable table = collected ? costs_table_parse(list, text) : (CostsTable){ 0 };
    costs_expect(test, table.first_given && table.first_milliseconds == 30000, "and reads back");
    costs_table_free(&table);
    static const char* const malformed[] = { "#: first-shard 30.0s\n", "#: first-shard \n", "#: first-shard 30.0\n#: first-shard 30.0\n" };
    for (uint32_t i = 0; collected && i < sizeof(malformed) / sizeof(malformed[0]); i += 1)
    {
        CostsText mutant = costs_test_replace(&tables[0], "#: first-shard 30.0\n", malformed[i]);
        CostsTable bad = costs_table_parse(list, mutant);
        costs_expect(test, bad.error != NULL, "a first shard's time that isn't SECONDS, or twice, is rejected");
        costs_table_free(&bad);
        free(mutant.data);
    }
    free(problems.data);
    free(report.data);
    for (uint32_t i = 0; i < BUSTER_COSTS_ROWS; i += 1)
    {
        free(tables[i].data);
    }
    for (uint32_t i = 0; i < 4; i += 1)
    {
        free(logs[i].samples);
        free(texts[i].data);
    }
}

static void costs_test_tables(CostsTest* test, const CostsList* list)
{
    static const uint32_t forward[] = { 0, 1, 2, 3 };
    static const uint32_t backward[] = { 3, 2, 1, 0 };
    CostsBuffer table = { 0 };
    CostsBuffer again = { 0 };
    CostsBuffer report = { 0 };
    costs_expect(test, costs_test_collect(list, forward, &table, &report), "the fixture logs collect into the plain x86-64 row's table alone");
    costs_expect(test, costs_test_collect(list, backward, &again, &report), "and so in the other order");
    costs_expect(test, table.length == again.length && memcmp(table.data, again.data, table.length) == 0, "the same table whatever the logs' order");
    // Alpha: 10 and 20 s on 4 CPUs, the larger; Beta 5 and 7 s; Gamma 3 and 4 s on 2 CPUs; Delta only failed, its
    // longest a lower bound; Epsilon none: the median per CPU of the measured (20, 7 and 4 s: 7 s) times its 1 CPU.
    costs_expect(test, costs_test_contains(&table, "\n80.0 Alpha [4 CPUs]\n7.0 Beta: with a colon\n8.0 Gamma [2 CPUs]\n3.0 Delta\n7.0 Epsilon 100%\n"), "each command's cost, in the inventory's order");
    costs_expect(test, costs_test_contains(&table, "\n#: lower-bound Delta\n#: unmeasured Epsilon 100%\n"), "the lower bound and the estimate are marked");
    costs_expect(test, costs_test_contains(&table, "\n#: row x86-64 none O3\n#: runner-cpus 4\n#: samples 2 2\n#: revision " BUSTER_COSTS_TEST_SHA_A " 2\n#: revision " BUSTER_COSTS_TEST_SHA_B " 2\n#: cpu 1 - AMD EPYC 7763 64-Core Processor\n#: cpu 3 1.00 AMD EPYC 9V45 96-Core Processor\n"), "the row, its runners' CPUs, its samples and its provenance");
    costs_expect(test, table.length > sizeof(costs_marker) && memcmp(table.data, costs_marker, sizeof(costs_marker) - 1) == 0, "the marker first");
    // Every shard 2 failed a command, so its later steps didn't run: no shard 1 time is measured.
    costs_expect(test, !costs_test_contains(&table, "#: first-shard"), "no first shard's time from jobs that failed");
    CostsText text = { .data = table.data, .length = table.length };
    CostsBuffer problems = { 0 };
    costs_expect(test, costs_check_text(list, 0, text, &problems) == 0, "a collected table checks");
    costs_expect(test, costs_check_text(list, 1, text, &problems) == 1, "but not as another row's");
    free(problems.data);
    // The review's ask: a table that lacks a heading fails, and so does every other departure from the generator's.
    static const struct
    {
        const char* name;
        const char* before;
        const char* after;
    } mutants[] = {
        { "lacks an inventory heading", "7.0 Beta: with a colon\n", "" },
        { "has a title the inventory doesn't", "7.0 Epsilon 100%\n", "7.0 Epsilon 100%\n1.0 Zeta\n" },
        { "has a title twice", "7.0 Epsilon 100%\n", "7.0 Epsilon 100%\n7.0 Epsilon 100%\n" },
        { "is out of order", "7.0 Beta: with a colon\n8.0 Gamma [2 CPUs]\n", "8.0 Gamma [2 CPUs]\n7.0 Beta: with a colon\n" },
        { "has a hand-edited estimate", "7.0 Epsilon 100%\n", "9.0 Epsilon 100%\n" },
        { "has a hand-edited precision", "7.0 Beta: with a colon\n", "7.05 Beta: with a colon\n" },
        { "lacks its marker", "# Generated by tools/ci/costs.c;", "# Written by hand;" },
        { "lacks its row", "#: row x86-64 none O3\n", "" },
        { "lacks its runner's CPUs", "#: runner-cpus 4\n", "" },
        { "marks a command twice", "#: lower-bound Delta\n", "#: lower-bound Delta\n#: unmeasured Delta\n" },
        { "has a `#:` line of its own", "#: samples 2 2\n", "#: samples 2 2\n#: note mine\n" },
        { "has a cost without a title", "7.0 Epsilon 100%\n", "7.0 Epsilon 100%\n0.7\n" },
        { "has a cost of four decimals", "3.0 Delta\n", "3.0001 Delta\n" },
    };
    for (uint32_t i = 0; i < sizeof(mutants) / sizeof(mutants[0]); i += 1)
    {
        CostsText changed = costs_test_replace(&table, mutants[i].before, mutants[i].after);
        CostsBuffer found = { 0 };
        char what[160];
        snprintf(what, sizeof(what), "check rejects a table that %s", mutants[i].name);
        costs_expect(test, strcmp(changed.data, "not found") != 0 && costs_check_text(list, 0, changed, &found) != 0, what);
        if (i == 0)
        {
            costs_expect(test, costs_test_contains(&found, "x86-64-none-O3: no cost for the inventory's heading: Beta: with a colon\n"), "and names the heading");
        }
        free(found.data);
        free(changed.data);
    }
    // Refresh: the inventory adds a command and drops one; the measured keep their costs, the new one is estimated.
    CostsText changed_list_text = costs_text_of("== Alpha [4 CPUs]\ntrue\n== Beta: with a colon\ntrue\n== Gamma [2 CPUs]\ntrue\n== Zeta [8 CPUs]\ntrue\n== Delta\ntrue\n");
    const CostsList changed_list = costs_list_parse(changed_list_text);
    CostsBuffer refreshed = { 0 };
    const char* error = NULL;
    costs_expect(test, costs_refresh_text(&changed_list, text, &refreshed, &error), "a table refreshes for a changed inventory");
    costs_expect(test, costs_test_contains(&refreshed, "\n80.0 Alpha [4 CPUs]\n7.0 Beta: with a colon\n8.0 Gamma [2 CPUs]\n28.0 Zeta [8 CPUs]\n3.0 Delta\n"), "keeping the measured, estimating the new on its 4 runner CPUs, dropping the removed");
    costs_expect(test, costs_test_contains(&refreshed, "\n#: unmeasured Zeta [8 CPUs]\n#: lower-bound Delta\n80.0") && !costs_test_contains(&refreshed, "Epsilon"), "with the new one marked and the removed one gone");
    CostsText refreshed_text = { .data = refreshed.data, .length = refreshed.length };
    CostsBuffer twice = { 0 };
    costs_expect(test, costs_refresh_text(&changed_list, refreshed_text, &twice, &error) && twice.length == refreshed.length && memcmp(twice.data, refreshed.data, twice.length) == 0, "refresh is idempotent");
    CostsBuffer none = { 0 };
    costs_expect(test, costs_check_text(&changed_list, 0, text, &none) != 0 && costs_check_text(&changed_list, 0, refreshed_text, &none) == 0, "the old table fails the changed inventory; the refreshed one checks");
    free(none.data);
    // Seed: a row without logs takes another row's measurements, which its `#: seeded` line names; it checks as its own
    // row's table and refreshes as one, and can't seed another.
    const uint32_t vector_thread = BUSTER_COSTS_VECTOR * BUSTER_COSTS_SANITIZERS + 2;
    CostsBuffer seeded = { 0 };
    costs_expect(test, costs_seed_text(list, vector_thread, 0, text, &seeded, &error), "a table seeds another row's");
    costs_expect(test, costs_test_contains(&seeded, "\n#: row vector thread O3\n#: runner-cpus 4\n#: samples 2 2\n#: seeded x86-64-none-O3\n#: revision " BUSTER_COSTS_TEST_SHA_A " 2\n"), "with its own row and the source's name before its provenance");
    costs_expect(test, costs_test_contains(&seeded, "\n80.0 Alpha [4 CPUs]\n7.0 Beta: with a colon\n8.0 Gamma [2 CPUs]\n3.0 Delta\n7.0 Epsilon 100%\n"), "and the source's costs");
    CostsText seeded_text = { .data = seeded.data, .length = seeded.length };
    CostsBuffer seed_problems = { 0 };
    costs_expect(test, costs_check_text(list, vector_thread, seeded_text, &seed_problems) == 0 && costs_check_text(list, 0, seeded_text, &seed_problems) != 0, "a seeded table checks as its row's alone");
    CostsBuffer reseeded = { 0 };
    costs_expect(test, !costs_seed_text(list, vector_thread + 1, vector_thread, seeded_text, &reseeded, &error) && strcmp(error, "a source that is itself seeded") == 0, "a seeded table seeds no other");
    costs_expect(test, !costs_seed_text(list, vector_thread, 1, text, &reseeded, &error) && strcmp(error, "a source whose `#: row` line names another row") == 0, "a source of another row than named seeds nothing");
    costs_expect(test, !costs_seed_text(list, 0, 0, text, &reseeded, &error), "a row seeds not itself");
    static const struct
    {
        const char* before;
        const char* after;
    } seed_mutants[] = {
        { "#: seeded x86-64-none-O3\n", "#: seeded nothing\n" },
        { "#: seeded x86-64-none-O3\n", "#: seeded x86-64-none-O3\n#: seeded native-none-O3\n" },
        { "#: seeded x86-64-none-O3\n#: revision " BUSTER_COSTS_TEST_SHA_A " 2\n", "#: revision " BUSTER_COSTS_TEST_SHA_A " 2\n#: seeded x86-64-none-O3\n" },
    };
    for (uint32_t i = 0; i < sizeof(seed_mutants) / sizeof(seed_mutants[0]); i += 1)
    {
        CostsText changed = costs_test_replace(&seeded, seed_mutants[i].before, seed_mutants[i].after);
        costs_expect(test, strcmp(changed.data, "not found") != 0 && costs_check_text(list, vector_thread, changed, &seed_problems) != 0, "check rejects a seeded line that isn't one row's, first");
        free(changed.data);
    }
    free(reseeded.data);
    free(seed_problems.data);
    free(seeded.data);
    free(twice.data);
    free(refreshed.data);
    free(changed_list.commands);
    free(changed_list_text.data);
    free(table.data);
    free(again.data);
    free(report.data);
}

// The speeds: a CPU twice as fast as the reference on the commands both ran has its samples doubled, so that a command
// only it ran costs what the slowest CPU would take; a CPU that shares too few commands keeps its samples.
static void costs_test_speeds(CostsTest* test)
{
    CostsText text = costs_text_of("== C1\nx\n== C2\nx\n== C3\nx\n== C4\nx\n== C5\nx\n== C6\nx\n== C7\nx\n");
    const CostsList list = costs_list_parse(text);
    static const char name[] = "Tests on Ubuntu 26.04 (Clang 23, native, address, O3, shard 1 of 1)";
    CostsText texts[3] = {
        costs_test_log(name, "Slow CPU", BUSTER_COSTS_TEST_SHA_A, "1 of 1, 6 of 7 commands", "C1: passed in 20.0 s, 20.0 s of CPU\nC2: passed in 20.0 s, 20.0 s of CPU\nC3: passed in 30.0 s, 30.0 s of CPU\nC4: passed in 20.0 s, 20.0 s of CPU\nC5: passed in 40.0 s, 40.0 s of CPU\nC6: passed in 20.0 s, 20.0 s of CPU\n", "10"),
        costs_test_log(name, "Fast CPU", BUSTER_COSTS_TEST_SHA_B, "1 of 1, 6 of 7 commands", "C1: passed in 10.0 s, 10.0 s of CPU\nC2: passed in 10.0 s, 10.0 s of CPU\nC3: passed in 15.0 s, 15.0 s of CPU\nC4: passed in 10.0 s, 10.0 s of CPU\nC5: passed in 20.0 s, 20.0 s of CPU\nC7: passed in 10.0 s, 10.0 s of CPU\n", "20"),
        costs_test_log(name, "Rare CPU", BUSTER_COSTS_TEST_SHA_B, "1 of 1, 1 of 7 commands", "C6: passed in 5.0 s, 5.0 s of CPU\n", "30"),
    };
    CostsLog logs[3];
    const char* names[3] = { "slow", "fast", "rare" };
    for (uint32_t i = 0; i < 3; i += 1)
    {
        logs[i] = costs_log_parse(&list, texts[i]);
    }
    CostsBuffer tables[BUSTER_COSTS_ROWS] = { 0 };
    CostsBuffer report = { 0 };
    const uint32_t row = 1 * BUSTER_COSTS_SANITIZERS + 1;
    costs_expect(test, logs[0].error == NULL && logs[1].error == NULL && logs[2].error == NULL && costs_collect_rows(&list, logs, names, 3, tables, &report), "logs of three CPUs collect");
    // The reference is the first by name of those most jobs drew: Fast and Slow drew one each, so Fast. Slow takes
    // twice its time on the five commands both ran, so it is the slowest, and Fast's samples are doubled.
    costs_expect(test, costs_test_contains(&tables[row], "\n#: cpu 1 0.50 Fast CPU\n#: cpu 1 - Rare CPU\n#: cpu 1 1.00 Slow CPU\n"), "each CPU's speed against the slowest, and one too rarely shared");
    costs_expect(test, costs_test_contains(&tables[row], "\n20.0 C1\n20.0 C2\n30.0 C3\n20.0 C4\n40.0 C5\n20.0 C6\n20.0 C7\n"), "a command only the fast CPU ran costs what the slowest would take; the rare CPU's sample stays as it is");
    for (uint32_t i = 0; i < BUSTER_COSTS_ROWS; i += 1)
    {
        free(tables[i].data);
    }
    for (uint32_t i = 0; i < 3; i += 1)
    {
        free(logs[i].samples);
        free(texts[i].data);
    }
    free(report.data);
    free(list.commands);
    free(text.data);
}

static void costs_test_collect_rejects(CostsTest* test, const CostsList* list)
{
    CostsText one = costs_test_log(BUSTER_COSTS_TEST_SHARD_1, "AMD EPYC 7763 64-Core Processor", BUSTER_COSTS_TEST_SHA_A, "1 of 2, 2 of 5 commands", "Alpha [4 CPUs]: passed in 10.0 s, 30.0 s of CPU\n", "14");
    CostsBuffer one_buffer = { .data = one.data, .length = one.length };
    CostsText later = costs_test_replace(&one_buffer, "01:14:00.1000000Z Current", "02:14:00.1000000Z Current");
    const CostsBuffer later_buffer = { .data = later.data, .length = later.length };
    CostsText wider = costs_test_replace(&later_buffer, "at most 4 CPUs at once\n", "at most 8 CPUs at once\n");
    CostsLog logs[2] = { costs_log_parse(list, one), costs_log_parse(list, one) };
    const char* names[2] = { "first", "second" };
    CostsBuffer tables[BUSTER_COSTS_ROWS] = { 0 };
    CostsBuffer report = { 0 };
    costs_expect(test, !costs_collect_rows(list, logs, names, 2, tables, &report) && costs_test_contains(&report, "second: the same job as first"), "the same job's log twice is rejected");
    free(logs[1].samples);
    logs[1] = costs_log_parse(list, wider);
    costs_expect(test, logs[1].error == NULL && !costs_collect_rows(list, logs, names, 2, tables, &report), "a row whose runners disagree on their CPUs is rejected");
    free(logs[1].samples);
    logs[1] = costs_log_parse(list, later);
    CostsBuffer accepted[BUSTER_COSTS_ROWS] = { 0 };
    costs_expect(test, logs[1].error == NULL && costs_collect_rows(list, logs, names, 2, accepted, &report), "a rerun of the same shard is another job");
    for (uint32_t i = 0; i < BUSTER_COSTS_ROWS; i += 1)
    {
        free(accepted[i].data);
        free(tables[i].data);
    }
    free(logs[0].samples);
    free(logs[1].samples);
    free(one.data);
    free(wider.data);
    free(later.data);
    free(report.data);
}

static void costs_test_lists(CostsTest* test)
{
    static const struct
    {
        const char* text;
        const char* error;
    } cases[] = {
        { "true\n== A\ntrue\n", "a line before the first heading" },
        { "== A\ntrue\n== A\ntrue\n", "a title that an earlier command has" },
        { "== A [1 CPUs]\ntrue\n", "a title whose CPUs aren't [N CPUs] with N from 2" },
        { "== A [x CPUs]\ntrue\n", "a title whose CPUs aren't [N CPUs] with N from 2" },
        { "==   \ntrue\n", "a heading without a title" },
        { "# only a comment\n", "no commands" },
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        CostsText text = costs_text_of(cases[i].text);
        CostsList list = costs_list_parse(text);
        costs_expect(test, list.error != NULL && strcmp(list.error, cases[i].error) == 0, cases[i].error);
        free(list.commands);
        free(text.data);
    }
    CostsText text = costs_text_of("\n# c\n==A [12 CPUs]  \nx\n==  B\t\nx\n");
    CostsList list = costs_list_parse(text);
    costs_expect(test, list.error == NULL && list.count == 2 && list.commands[0].cpus == 12 && list.commands[0].title.length == 11 && list.commands[1].cpus == 1 && list.commands[1].title.length == 1, "headings without their space and blanks, and their CPUs");
    free(list.commands);
    free(text.data);
}

// The plan: the deal is test_deal's on parallel's costs, and the schedule the runner's.
static void costs_test_plan(CostsTest* test)
{
    CostsText text = costs_text_of("== Wide [8 CPUs]\nx\n== One\nx\n== Two\nx\n== Three\nx\n== Four\nx\n== Pair [2 CPUs]\nx\n");
    const CostsList list = costs_list_parse(text);
    // Wide: 40 s of 4 CPUs (10 s alone); One to Four 10 s each; Pair 2 CPUs for 5 s.
    const uint64_t milliseconds[] = { 40000, 10000, 10000, 10000, 10000, 10000 };
    uint64_t times[BUSTER_COSTS_SHARDS];
    CostsBuffer listing = { 0 };
    const CostsPlan whole = costs_plan(&list, milliseconds, 0, 0, milliseconds, 1, 4, 0, times, &listing);
    costs_expect(test, costs_test_contains(&listing, "shard 1:     0.0 s to    10.0 s, 4 CPUs: Wide [8 CPUs]\nshard 1:    10.0 s to    20.0 s, 1 CPUs: One\n"), "the listing gives each command's start and end");
    free(listing.data);
    // One shard: Wide alone (10 s), then One to Four on 4 CPUs (10 s), then Pair (5 s).
    costs_expect(test, whole.longest == 25000 && whole.total == 25000, "one shard runs the list in its order: a wide command alone, then four, then a pair");
    costs_expect(test, whole.floor == 10000 && whole.floor_command == 0, "the floor is the longest command alone");
    const CostsPlan two = costs_plan(&list, milliseconds, 0, 0, milliseconds, 2, 4, 3000, times, NULL);
    // The deal: Wide (40) to 0; One (10) to 1, Two to 1, Three to 1, Four to 1, Pair (10) to 0... the lightest each time.
    costs_expect(test, times[0] == 15000 + 3000 && times[1] == 10000 && two.longest == 18000 && two.longest_shard == 0, "two shards: shard 1 carries its once-only time");
    uint64_t order[8];
    uint64_t loads[8];
    uint64_t assigned[8];
    test_deal(milliseconds, list.count, 2, order, loads, assigned);
    costs_expect(test, assigned[0] == 0 && assigned[1] == 1 && assigned[5] == 0, "the deal is the runner's");
    // By each shard's schedule (test_deal_scheduled, as parallel deals costs that name their runner's CPUs): parallel's
    // own fixture. By costs, w3 shares shard 1 with long, which starts after it; by schedule long has shard 1 alone.
    CostsText wide_text = costs_text_of("== w1 [8 CPUs]\nx\n== w2 [8 CPUs]\nx\n== w3 [8 CPUs]\nx\n== long\nx\n");
    const CostsList wide = costs_list_parse(wide_text);
    const uint64_t wide_costs[] = { 40000, 40000, 40000, 60000 };
    const CostsPlan by_costs = costs_plan(&wide, wide_costs, 0, 0, wide_costs, 2, 4, 0, times, NULL);
    costs_expect(test, by_costs.longest == 70000 && times[1] == 20000, "by costs, a wide command holds back the long one");
    const CostsPlan by_schedule = costs_plan(&wide, wide_costs, 4, 0, wide_costs, 2, 4, 0, times, NULL);
    costs_expect(test, by_schedule.longest == 60000 && times[1] == 30000, "by schedule, the long one ends at its own time");
    // With shard 1's once-only steps (50 s), as parallel's own fixture: long goes to shard 2, after w3; w1 and w2 to
    // shard 1, which ends at 70 s with them, as shard 2 does.
    const CostsPlan by_first = costs_plan(&wide, wide_costs, 4, 50000, wide_costs, 2, 4, 50000, times, NULL);
    costs_expect(test, by_first.longest == 70000 && times[0] == 70000 && times[1] == 70000, "the deal gives shard 1 less by its once-only steps");
    // The listing's simulation and the deal's (test_deal_makespan) agree.
    const uint64_t wide_held[] = { 4, 4, 4, 1 };
    const uint64_t wide_assigned[] = { 1, 1, 0, 0 };
    uint64_t ends[4];
    uint64_t holds[4];
    costs_expect(test, test_deal_makespan(wide_costs, wide_held, 4, wide_assigned, 0, 4, ends, holds) == 70000 && costs_simulate(&wide, wide_costs, wide_assigned, 0, 4, NULL) == 70000, "the plan's schedule and the deal's agree");
    free(wide.commands);
    free(wide_text.data);
    // A command that doesn't fit yet holds back the ones after it.
    CostsText held_text = costs_text_of("== One\nx\n== Wide [4 CPUs]\nx\n== Two\nx\n");
    const CostsList held_list = costs_list_parse(held_text);
    const uint64_t held_costs[] = { 10000, 4000, 1000 };
    const uint64_t one_shard[] = { 0, 0, 0 };
    costs_expect(test, costs_simulate(&held_list, held_costs, one_shard, 0, 4, NULL) == 12000, "a command waits for its CPUs, and the next for it");
    free(held_list.commands);
    free(held_text.data);
    free(list.commands);
    free(text.data);
}

static void costs_test_report(CostsTest* test)
{
    CostsText texts[3] = {
        costs_test_log(BUSTER_COSTS_TEST_SHARD_1, "AMD EPYC 7763 64-Core Processor", BUSTER_COSTS_TEST_SHA_A, "1 of 2, 2 of 5 commands", "Alpha [4 CPUs]: passed in 10.0 s, 30.0 s of CPU\n", "14"),
        costs_test_log(BUSTER_COSTS_TEST_SHARD_2, "AMD EPYC 9V45 96-Core Processor", BUSTER_COSTS_TEST_SHA_A, "2 of 2, 3 of 5 commands", "Gamma [2 CPUs]: passed in 3.0 s, 6.0 s of CPU\n", "15"),
        costs_text_of("2026-11-11T01:13:00.0000000Z Complete job name: Tests on Ubuntu 26.04 (x86-64, none, O3) / Build the compiler once for the row's shards\n2026-11-11T01:14:00.0000000Z done\n"),
    };
    CostsLog logs[3];
    for (uint32_t i = 0; i < 3; i += 1)
    {
        logs[i] = costs_log_parse(NULL, texts[i]);
    }
    CostsBuffer output = { 0 };
    costs_expect(test, logs[0].error == NULL && logs[1].error == NULL && logs[2].error == NULL && costs_report(logs, 3, &output), "a run's logs report");
    // Both shards' steps took 42.5 s; the jobs 30 s each (one minute each) and the build 60 s; the row spans 13:00 to 15:30.
    costs_expect(test, costs_test_contains(&output, "| x86-64-none-O3 | 2 | 42.5 s | 42.5 s | 1.00 | 150 s | 3 | 1: AMD EPYC 7763 64-Core Processor; 2: AMD EPYC 9V45 96-Core Processor |\n"), "each row's longest shard, mean, imbalance, span, runner-minutes and CPUs");
    for (uint32_t i = 0; i < 3; i += 1)
    {
        free(logs[i].samples);
        free(texts[i].data);
    }
    free(output.data);
}

// The repository's tables against its inventory, and the rows against the workflows that run them.
static void costs_test_root(CostsTest* test, const char* root, uint32_t* problems)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/tools/ci/correctness.tests", root);
    CostsText tests = { 0 };
    const bool read = costs_read(path, &tests);
    costs_expect(test, read, "the inventory, tools/ci/correctness.tests");
    const CostsList list = read ? costs_list_parse(tests) : (CostsList){ 0 };
    costs_expect(test, read && list.error == NULL, "the inventory parses");
    for (uint32_t row = 0; read && list.error == NULL && row < BUSTER_COSTS_ROWS; row += 1)
    {
        char name[64];
        costs_row_name(row, name, sizeof(name));
        snprintf(path, sizeof(path), "%s/tools/ci/costs/%s.costs", root, name);
        CostsText table = { 0 };
        CostsBuffer report = { 0 };
        const bool found = costs_read(path, &table);
        const uint32_t count = found ? costs_check_text(&list, row, table, &report) : 1;
        if (!found)
        {
            costs_append(&report, "%s: no table, tools/ci/costs/%s.costs\n", name, name);
        }
        fprintf(stderr, "%.*s", (int)report.length, report.data != NULL ? report.data : "");
        *problems += count;
        free(report.data);
        free(table.data);
    }
    costs_expect(test, *problems == 0, "every row's table covers the inventory as `costs refresh` writes it (run it, or `costs collect` over a run's logs)");
    // The rows are 9700x.yml's test matrix, and the row workflow deals by its row's table.
    static const struct
    {
        const char* file;
        const char* text;
    } anchors[] = {
        { ".github/workflows/9700x.yml", "'[\"x86-64\"]' || '[\"x86-64\", \"native\"]'" },
        { ".github/workflows/hosted-correctness.yml", "description: BUSTER_HOST_ARCH, x86-64, native or vector\n" },
        { ".github/workflows/9700x.yml", "'[\"none\", \"address\", \"thread\", \"memory\"]'" },
        // The vector row's configurations: a pull request's plain one, and the four as one pool (#2033).
        { ".github/workflows/9700x.yml", "'[{\"host_arch\":\"vector\",\"sanitizer\":\"none\",\"optimization\":3}]'" },
        { ".github/workflows/9700x.yml", "'[{\"host_arch\":\"vector\",\"sanitizer\":\"none address thread memory\",\"optimization\":3}]'" },
        { ".github/workflows/9700x.yml", "        optimization: [3]\n" },
        { ".github/workflows/hosted-correctness.yml", "--costs \"tools/ci/costs/$BUSTER_HOST_ARCH-$BUSTER_SANITIZER-O$BUSTER_SANITIZER_OPTIMIZATION.costs\"" },
    };
    for (uint32_t i = 0; i < sizeof(anchors) / sizeof(anchors[0]); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", root, anchors[i].file);
        CostsText workflow = { 0 };
        const bool found = costs_read(path, &workflow);
        char what[256];
        snprintf(what, sizeof(what), "%s has %s", anchors[i].file, anchors[i].text);
        costs_expect(test, found && strstr(workflow.data, anchors[i].text) != NULL, what);
        free(workflow.data);
    }
    free(list.commands);
    free(tests.data);
}

static int costs_self_test(const char* root)
{
    CostsTest test = { 0 };
    CostsText text = costs_text_of(costs_test_list);
    const CostsList list = costs_list_parse(text);
    costs_expect(&test, list.error == NULL && list.count == 5, "the fixture inventory parses");
    costs_test_lists(&test);
    costs_test_quantiles(&test);
    costs_test_logs(&test, &list);
    costs_test_tables(&test, &list);
    costs_test_first(&test, &list);
    costs_test_collect_rejects(&test, &list);
    costs_test_speeds(&test);
    costs_test_plan(&test);
    costs_test_report(&test);
    uint32_t problems = 0;
    if (root != NULL)
    {
        costs_test_root(&test, root, &problems);
    }
    free(list.commands);
    free(text.data);
    printf("costs self-test: %u checks, %u failed\n", test.checks, test.failures);
    return test.failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// The commands

static void costs_usage(void)
{
    fprintf(stderr, "usage: costs collect --tests LIST --output DIR LOG...\n"
                    "       costs refresh --tests LIST --costs DIR\n"
                    "       costs check --tests LIST --costs DIR\n"
                    "       costs seed --tests LIST --costs DIR --row ROW --from ROW\n"
                    "       costs plan --tests LIST --costs FILE --shards N[-M] [--deal FILE] [--jobs J] [--setup SECONDS] [--first SECONDS]\n"
                    "       costs report LOG...\n"
                    "       costs --self-test [--root DIR]\n");
}

// The inventory at `path`, or a message and false.
static bool costs_load_list(const char* path, CostsText* text, CostsList* list)
{
    const bool read = costs_read(path, text);
    *list = read ? costs_list_parse(*text) : (CostsList){ 0 };
    if (!read)
    {
        fprintf(stderr, "costs: %s: %s\n", path, strerror(errno));
    }
    else if (list->error != NULL)
    {
        fprintf(stderr, "costs: %s:%u: %s\n", path, list->error_line, list->error);
    }
    return read && list->error == NULL;
}

// The logs, parsed against the list (or null): false, with each problem printed, when one can't be read or parsed.
static bool costs_load_logs(const CostsList* list, char** paths, uint32_t count, CostsLog* logs)
{
    bool valid = true;
    for (uint32_t i = 0; i < count; i += 1)
    {
        CostsText text = { 0 };
        const bool read = costs_read(paths[i], &text);
        logs[i] = read ? costs_log_parse(list, text) : (CostsLog){ .error = strerror(errno) };
        if (logs[i].error != NULL)
        {
            fprintf(stderr, "costs: %s:%u: %s\n", paths[i], logs[i].error_line, logs[i].error);
        }
        valid = valid && logs[i].error == NULL;
        free(text.data);
    }
    return valid;
}

static int costs_command_collect(const char* tests, const char* output, char** paths, uint32_t count)
{
    CostsText text = { 0 };
    CostsList list = { 0 };
    CostsLog* logs = costs_allocate(count + 1, sizeof(CostsLog));
    bool valid = costs_load_list(tests, &text, &list) && costs_load_logs(&list, paths, count, logs);
    CostsBuffer tables[BUSTER_COSTS_ROWS] = { 0 };
    CostsBuffer report = { 0 };
    valid = valid && costs_collect_rows(&list, logs, (const char* const*)paths, count, tables, &report);
    fprintf(stderr, "%.*s", (int)report.length, report.data != NULL ? report.data : "");
    uint32_t unknown = 0;
    uint32_t builds = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        unknown += logs[i].unknown;
        builds += logs[i].build;
    }
    for (uint32_t row = 0; valid && row < BUSTER_COSTS_ROWS; row += 1)
    {
        char name[64];
        costs_row_name(row, name, sizeof(name));
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s.costs", output, name);
        if (tables[row].length != 0)
        {
            valid = costs_write(path, &tables[row]);
            fprintf(stderr, valid ? "costs: wrote %s\n" : "costs: can't write %s\n", path);
        }
    }
    if (valid)
    {
        fprintf(stderr, "costs: %u logs (%u builds, which run no commands); %u headings of commands the inventory doesn't have\n", count, builds, unknown);
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        free(logs[i].samples);
    }
    for (uint32_t row = 0; row < BUSTER_COSTS_ROWS; row += 1)
    {
        free(tables[row].data);
    }
    free(report.data);
    free(logs);
    free(list.commands);
    free(text.data);
    return valid ? 0 : 1;
}

// refresh (`write` true) or check every row's table in `directory`.
static int costs_command_tables(const char* tests, const char* directory, bool write)
{
    CostsText text = { 0 };
    CostsList list = { 0 };
    bool valid = costs_load_list(tests, &text, &list);
    uint32_t problems = 0;
    for (uint32_t row = 0; valid && row < BUSTER_COSTS_ROWS; row += 1)
    {
        char name[64];
        costs_row_name(row, name, sizeof(name));
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s.costs", directory, name);
        CostsText table = { 0 };
        // Every row the test job runs has a table: each architecture by each sanitizer, the vector row's too (#2033).
        const bool found = costs_read(path, &table);
        CostsBuffer output = { 0 };
        const char* error = NULL;
        if (!found)
        {
            fprintf(stderr, "costs: %s: no table for the row (%s)\n", path, strerror(errno));
            problems += 1;
        }
        else if (found && write && !costs_refresh_text(&list, table, &output, &error))
        {
            fprintf(stderr, "costs: %s: %s\n", path, error);
            problems += 1;
        }
        else if (found && write && !costs_write(path, &output))
        {
            fprintf(stderr, "costs: can't write %s\n", path);
            problems += 1;
        }
        else if (found && !write)
        {
            const uint32_t count = costs_check_text(&list, row, table, &output);
            fprintf(stderr, "%.*s", (int)output.length, output.data != NULL ? output.data : "");
            problems += count;
        }
        free(output.data);
        free(table.data);
    }
    if (valid && problems != 0 && !write)
    {
        fprintf(stderr, "costs: %u problems. A table is `costs collect`'s output over a run's job logs, which `costs refresh --tests %s --costs %s` fits to the inventory as it is (docs/commands/ci.md, \"Shard costs\").\n", problems, tests, directory);
    }
    free(list.commands);
    free(text.data);
    return valid && problems == 0 ? 0 : 1;
}

// seed: DIRECTORY/ROW.costs, which mustn't exist, from DIRECTORY/FROM.costs.
static int costs_command_seed(const char* tests, const char* directory, const char* row_name, const char* from_name)
{
    CostsText text = { 0 };
    CostsList list = { 0 };
    const uint32_t row = costs_row_named((CostsSlice){ row_name, strlen(row_name) });
    const uint32_t from = costs_row_named((CostsSlice){ from_name, strlen(from_name) });
    bool valid = costs_load_list(tests, &text, &list);
    if (row == BUSTER_COSTS_ROWS || from == BUSTER_COSTS_ROWS)
    {
        fprintf(stderr, "costs: seed: %s isn't a row, such as vector-address-O3\n", row == BUSTER_COSTS_ROWS ? row_name : from_name);
        valid = false;
    }
    char path[4096];
    char source[4096];
    snprintf(path, sizeof(path), "%s/%s.costs", directory, row_name);
    snprintf(source, sizeof(source), "%s/%s.costs", directory, from_name);
    CostsText table = { 0 };
    CostsText existing = { 0 };
    if (valid && costs_read(path, &existing))
    {
        fprintf(stderr, "costs: seed: %s exists: a row's own table replaces a seeded one by `collect`, never the other way\n", path);
        valid = false;
    }
    if (valid && !costs_read(source, &table))
    {
        fprintf(stderr, "costs: %s: %s\n", source, strerror(errno));
        valid = false;
    }
    CostsBuffer output = { 0 };
    const char* error = NULL;
    if (valid && !costs_seed_text(&list, row, from, table, &output, &error))
    {
        fprintf(stderr, "costs: seed: %s: %s\n", source, error);
        valid = false;
    }
    if (valid && !costs_write(path, &output))
    {
        fprintf(stderr, "costs: can't write %s\n", path);
        valid = false;
    }
    if (valid)
    {
        fprintf(stderr, "costs: wrote %s, seeded from %s\n", path, source);
    }
    free(output.data);
    free(existing.data);
    free(table.data);
    free(list.commands);
    free(text.data);
    return valid ? 0 : 1;
}

static int costs_command_plan(const char* tests, const char* costs, const char* deal, uint32_t low, uint32_t high, uint32_t jobs, uint64_t setup, uint64_t first)
{
    CostsText text = { 0 };
    CostsList list = { 0 };
    bool valid = costs_load_list(tests, &text, &list);
    uint64_t* milliseconds = costs_allocate(list.count + 1, sizeof(uint64_t));
    uint64_t* dealt = costs_allocate(list.count + 1, sizeof(uint64_t));
    // The runner the times are for: the costs' own, or --jobs where they don't say. The deal's CPUs are its file's.
    uint32_t runner = 0;
    uint32_t deal_cpus = 0;
    // Shard 1's once-only steps: --first's where given, else the costs' own; the deal's are its file's.
    uint64_t table_first = 0;
    uint64_t deal_first = 0;
    valid = valid && costs_plan_costs(&list, costs, milliseconds, &runner, &table_first);
    valid = valid && costs_plan_costs(&list, deal != NULL ? deal : costs, dealt, &deal_cpus, &deal_first);
    const uint64_t first_milliseconds = first != UINT64_MAX ? first * 1000 : table_first;
    jobs = runner != 0 ? runner : jobs;
    uint64_t times[BUSTER_COSTS_SHARDS];
    if (valid)
    {
        printf("| Shards | Longest shard | Mean | Imbalance | Floor | Runner-minutes |\n| ---: | ---: | ---: | ---: | ---: | ---: |\n");
    }
    for (uint32_t shards = low; valid && shards <= high; shards += 1)
    {
        CostsBuffer listing = { 0 };
        const CostsPlan plan = costs_plan(&list, dealt, deal_cpus, deal_first, milliseconds, shards, jobs, first_milliseconds, times, low == high ? &listing : NULL);
        const double mean = (double)plan.total / shards / 1000.0;
        const double runner = ((double)plan.total / 1000.0 + (double)(setup * shards)) / 60.0;
        printf("| %u | %.1f s (shard %llu) | %.1f s | %.2f | %.1f s | %.1f |\n", shards, (double)plan.longest / 1000.0, (unsigned long long)plan.longest_shard + 1, mean, mean > 0 ? (double)plan.longest / 1000.0 / mean : 0.0, (double)plan.floor / 1000.0, runner);
        if (low == high)
        {
            for (uint32_t k = 0; k < shards; k += 1)
            {
                printf("shard %u: %.1f s\n", k + 1, (double)times[k] / 1000.0);
            }
            printf("floor: %.*s\n%.*s", (int)list.commands[plan.floor_command].title.length, list.commands[plan.floor_command].title.data, (int)listing.length, listing.data != NULL ? listing.data : "");
        }
        free(listing.data);
    }
    free(milliseconds);
    free(dealt);
    free(list.commands);
    free(text.data);
    return valid ? 0 : 1;
}

static int costs_command_report(char** paths, uint32_t count)
{
    CostsLog* logs = costs_allocate(count + 1, sizeof(CostsLog));
    bool valid = costs_load_logs(NULL, paths, count, logs);
    CostsBuffer output = { 0 };
    valid = valid && costs_report(logs, count, &output);
    if (valid)
    {
        printf("%.*s", (int)output.length, output.data);
    }
    else
    {
        fprintf(stderr, "costs: the logs aren't one run's: a row's shard twice, or a log that can't be read\n");
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        free(logs[i].samples);
    }
    free(output.data);
    free(logs);
    return valid ? 0 : 1;
}

int main(int argc, char** argv)
{
    const char* command = argc > 1 ? argv[1] : "";
    const char* tests = NULL;
    const char* costs = NULL;
    const char* output = NULL;
    const char* deal = NULL;
    const char* root = NULL;
    const char* row = NULL;
    const char* from = NULL;
    uint64_t low = 0;
    uint64_t high = 0;
    uint64_t jobs = 4;
    uint64_t setup = 0;
    // Unset: the costs' own `#: first-shard`.
    uint64_t first = UINT64_MAX;
    bool valid = argc > 1;
    int i = 2;
    // The options, then the logs.
    while (valid && i < argc && strncmp(argv[i], "--", 2) == 0)
    {
        const bool has_value = i + 1 < argc;
        const char* value = has_value ? argv[i + 1] : "";
        const CostsSlice slice = { value, strlen(value) };
        if (strcmp(argv[i], "--tests") == 0 && has_value)
        {
            tests = value;
        }
        else if (strcmp(argv[i], "--costs") == 0 && has_value)
        {
            costs = value;
        }
        else if (strcmp(argv[i], "--output") == 0 && has_value)
        {
            output = value;
        }
        else if (strcmp(argv[i], "--deal") == 0 && has_value)
        {
            deal = value;
        }
        else if (strcmp(argv[i], "--root") == 0 && has_value)
        {
            root = value;
        }
        else if (strcmp(argv[i], "--row") == 0 && has_value)
        {
            row = value;
        }
        else if (strcmp(argv[i], "--from") == 0 && has_value)
        {
            from = value;
        }
        else if (strcmp(argv[i], "--shards") == 0 && has_value)
        {
            const size_t length = costs_number(slice, BUSTER_COSTS_SHARDS, &low);
            high = low;
            valid = length != 0 && (length == slice.length || (slice.data[length] == '-' && costs_whole_number(costs_after(slice, length + 1), BUSTER_COSTS_SHARDS, &high)));
            valid = valid && low >= 1 && low <= high;
        }
        else if (strcmp(argv[i], "--jobs") == 0 && has_value)
        {
            valid = costs_whole_number(slice, 1024, &jobs) && jobs >= 1;
        }
        else if (strcmp(argv[i], "--setup") == 0 && has_value)
        {
            valid = costs_whole_number(slice, 86400, &setup);
        }
        else if (strcmp(argv[i], "--first") == 0 && has_value)
        {
            valid = costs_whole_number(slice, 86400, &first);
        }
        else
        {
            valid = false;
        }
        i += 2;
    }
    const uint32_t logs = valid && i < argc ? (uint32_t)(argc - i) : 0;
    int status = 2;
    if (strcmp(command, "--self-test") == 0 && valid && logs == 0 && tests == NULL && costs == NULL && output == NULL)
    {
        status = costs_self_test(root);
    }
    else if (strcmp(command, "collect") == 0 && valid && tests != NULL && output != NULL && logs != 0 && logs <= BUSTER_COSTS_LOGS)
    {
        status = costs_command_collect(tests, output, argv + i, logs);
    }
    else if ((strcmp(command, "refresh") == 0 || strcmp(command, "check") == 0) && valid && tests != NULL && costs != NULL && logs == 0)
    {
        status = costs_command_tables(tests, costs, command[0] == 'r');
    }
    else if (strcmp(command, "seed") == 0 && valid && tests != NULL && costs != NULL && row != NULL && from != NULL && logs == 0)
    {
        status = costs_command_seed(tests, costs, row, from);
    }
    else if (strcmp(command, "plan") == 0 && valid && tests != NULL && costs != NULL && low != 0 && logs == 0)
    {
        status = costs_command_plan(tests, costs, deal, (uint32_t)low, (uint32_t)high, (uint32_t)jobs, setup, first);
    }
    else if (strcmp(command, "report") == 0 && valid && logs != 0 && logs <= BUSTER_COSTS_LOGS)
    {
        status = costs_command_report(argv + i, logs);
    }
    else
    {
        costs_usage();
    }
    return status;
}
